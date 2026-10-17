#include "d1_missions.h"
#include "d1dlc.h"
#include "d1_util.h"

#include <map>
#include <math.h>
#include <set>
#include <stdint.h>
#include <stdlib.h>

// Driver 1 story mission -> Driver 2 mission file (D2MS). Where to find what:
//   ParseScript / RecordBytes      split a MISSION.BLK script into records (opcode + arguments, text records)
//   Converter::ConvertRecords      the record loop: one branch per D1 opcode, every rule is named in a comment
//   ApplyScriptVariable            SETVAR: each D1 script variable (0..71) is mapped, ignored or refused
//   ApplyTimerRecord               TIMER_UP / TIMER_DOWN (16384 units per second) -> D2 header timer or timer commands
//   AddPendingPointTarget          TARGET0 -> D2 Point target (a TARGET0 alone already counts)
//   ApplyArrivalRulesToPointTarget speed gate, early arrival, lose the tail: new Point target flags (mission.c D1ArrivalBlocked)
//   AddRamCarTarget                STATE 5 (ram the target car) -> D2 chase car target (approximation, engine: mission.c IsDriver1ChaseCar)
//   CheckOutputStructure           proof that the output obeys the D2 script interpreter before it is written
//   PackD2msFile                   header (33 ints) + 16 targets + script words + strings
//   Prepare / ConvertAll           MISSION.BLK -> DRIVER1\CONVERTED\M<1000+n>.D2MS, MISSIONS.LST, MISSIONS.VER
// Strict: a mission converts only when every record is mapped or on the list of records the Driver 2 engine has
// no use for. Mirror of tools/d1conv/spike/d1mission_to_d2ms.py (same bytes); research in plans/d1-missions.md.
// Ram missions (STATE 5 + TARGET_SLOT, plan section 9) become a chase car driven by the civilian AI; the other chase missions (STATE 4, car
// targets that follow a recorded run of REPLAY.BLK, tails) are not converted yet and skipped on purpose.

namespace
{

// ---- Driver 1 script records ----

struct Record
{
	int					op;
	std::vector<int>	args;		// words after the opcode (for text records: variable/length words only)
	std::string			text;
	bool				hasText;
};

struct FixedLen { int op; int bytes; };

const FixedLen kFixedLengths[] = {
	{1, 4}, {3, 8}, {4, 28}, {7, 4}, {8, 4}, {9, 8}, {10, 4}, {11, 4}, {12, 8}, {13, 8}, {14, 8}, {15, 8}, {16, 12},
	{17, 12}, {19, 12}, {21, 4}, {24, 8}, {25, 16}, {26, 4}, {27, 4}, {28, 4}, {29, 4}, {30, 4}, {31, 4}, {33, 40},
	{34, 8}, {35, 8}, {36, 4}, {37, 4}, {38, 8}, {39, 4}, {40, 4}, {41, 8}, {48, 4}, {49, 4}, {50, 4}, {51, 8}, {52, 8},
	{53, 8}, {54, 28}, {55, 4}, {56, 4}, {57, 8}, {58, 4}, {59, 8}, {60, 8}, {61, 4}, {63, 4}, {64, 8}, {65, 8}, {66, 4},
	{67, 8}, {68, 8}, {69, 20}, {71, 24}, {74, 8}, {77, 4}, {78, 4}, {80, 32}, {82, 24}, {83, 12}, {84, 8}, {85, 16},
	{87, 4}, {88, 4}, {89, 4}, {90, 4}, {91, 4}, {92, 12}, {93, 16}, {95, 20}, {96, 12}, {97, 12}, {98, 16}, {99, 4},
	{100, 4}, {101, 4}, {102, 4}, {103, 16}, {105, 8}, {106, 8}, {107, 4}, {108, 8}, {109, 16}, {110, 8}, {111, 8},
	{112, 16}, {113, 12}, {114, 8},
};

const int kStringVars[] = { 0, 5, 7, 17, 20, 21, 22, 23, 36, 64, 65 };

enum
{
	OP_END = 1, OP_SETVAR = 2, OP_WAIT_STATE = 7, OP_TIMER_UP = 8, OP_TIMER_DOWN = 9, OP_START_POS = 19,
	OP_STRING_LIST = 20, OP_MESSAGE = 22, OP_TARGET0 = 25, OP_TARGET0_ON = 26, OP_CITY = 34, OP_EARLY_LIMIT = 35,
	OP_EARLY_ON = 36, OP_EARLY_OFF = 37, OP_MAX_COPS = 38, OP_COPS_OFF = 49, OP_COPS_ON = 50, OP_START_ROT = 52,
	OP_HEAT_MIN = 53, OP_SPAWN_CAR = 54, OP_WRECK_CHECK = 55, OP_TABLE = 70, OP_BLOB = 94, OP_ARRAY = 104,
	OP_INT_LIST = 86, OP_BEGIN_MISSION = 108,
};

const int kListEnd = 0x7FFFFFFF;

bool IsStringVar(int var)
{
	for (int v : kStringVars)
	{
		if (v == var)
			return true;
	}

	return false;
}

// byte length of the record at word index i; 0 when the stream is damaged
int RecordBytes(const int32_t* words, int count, int i)
{
	int op = words[i];

	for (const FixedLen& f : kFixedLengths)
	{
		if (f.op == op)
			return f.bytes;
	}

	auto word = [&](int k) { return i + k < count ? words[i + k] : -1; };
	long long bytes = 0;

	switch (op)
	{
	case OP_SETVAR:
		bytes = IsStringVar(word(1)) ? (long long)word(2) + 12 : 12;
		break;
	case OP_MESSAGE:
		bytes = (long long)word(2) + 12;
		break;
	case OP_STRING_LIST:
		bytes = (long long)word(1) * 12 + 8;
		break;
	case OP_TABLE:
		bytes = (long long)word(3) + 16;
		break;
	case OP_BLOB:
		bytes = (long long)word(1) + 8;
		break;
	case OP_ARRAY:
		bytes = ((long long)word(1) << 2) + 8;
		break;
	case OP_INT_LIST:
	{
		int j = i + 1;
		while (j < count && words[j] != kListEnd)
			j++;

		bytes = (long long)(j + 1 - i) * 4;
		break;
	}
	}

	return (bytes > 0 && bytes <= 0x100000) ? (int)bytes : 0;
}

bool ParseScript(const std::vector<char>& script, std::vector<Record>& out, std::string& why)
{
	int count = (int)(script.size() / 4);
	std::vector<int32_t> words(count + 1);
	memcpy(words.data(), script.data(), count * 4);

	for (int i = 0; i < count;)
	{
		int n = RecordBytes(words.data(), count, i) / 4;
		if (n <= 0 || i + n > count)
		{
			why = "script does not parse";
			return false;
		}

		Record r;
		r.op = words[i];
		r.hasText = false;

		int textAt = 0, textLen = 0;

		if (r.op == OP_MESSAGE || (r.op == OP_SETVAR && IsStringVar(words[i + 1])))
		{
			textAt = 3;
			textLen = words[i + 2];
			r.args.assign(words.begin() + i + 1, words.begin() + i + 3);
		}
		else if (r.op == OP_TABLE)
		{
			textAt = 4;
			textLen = words[i + 3];
			r.args.assign(words.begin() + i + 1, words.begin() + i + 4);
		}
		else
			r.args.assign(words.begin() + i + 1, words.begin() + i + n);

		if (textAt)
		{
			const char* p = script.data() + (i + textAt) * 4;
			r.text.assign(p, strnlen(p, textLen));
			r.hasText = true;
		}

		out.push_back(r);
		i += n;
	}

	return true;
}

// ---- Driver 2 mission (D2MS) ----

enum
{
	H_ID, H_SIZE, H_CITY, H_TIME, H_WEATHER, H_START_X, H_START_Z, H_START_ROT, H_TYPE, H_TIMER, H_STRINGS, H_COP_SPEED,
	H_COP_POWER, H_COP_MIN, H_COP_MAX, H_MSG_WRECKED, H_MSG_OUT_OF_TIME, H_MSG_COMPLETE, H_REGION, H_ROUTE, H_MSG_DROWNED,
	H_CAR_MODEL, H_CAR_COLOUR, H_MAX_DAMAGE, H_RES0, H_RES1, H_RES2, H_RES3, H_RES4, H_CUTSCENES, H_TITLE, H_MSG_POLICE,
	H_MSG_DOORS, H_COUNT
};

static_assert(H_COUNT * 4 == 132, "MS_MISSION header is 33 ints");

const int kMaxTargets = 16;
const int kTargetInts = 16;
const int kTargetPoint = 1;
const int kPointDisplay = 0x70;		// display flags of a Point target: minimap blip, direction arrow, 3D marker (99 of the 211 shown Driver 2 point targets)
const int kTargetCar = 2;
const int kTimeDay = 1;
const int kTimerCounterFlag = 0x8000;

// D1 city id (op 34) -> GameLevel of the converted city (g_D1DlcCities)
int CityOf(int d1City)
{
	switch (d1City)
	{
	case 0: return D1DLC_FIRST_CITY + 0;
	case 1: return D1DLC_FIRST_CITY + 1;
	case 2: return D1DLC_FIRST_CITY + 5;		// Training (TRAIN.LEV)
		case 3: return D1DLC_FIRST_CITY + 2;
	case 5: return D1DLC_FIRST_CITY + 3;
	}

	return -1;
}

// script words of the D2 mission interpreter
const uint32_t CMD_SHOW_MESSAGE = 0x1000050;
const uint32_t CMD_BRANCH_IF = 0x1000001;
const uint32_t CMD_SET_VARIABLE = 0x1000010;
const uint32_t CMD_SET_FELONY = 0x1000030;
const uint32_t CMD_TIMER_COUNT_UP = 0x1000040;		// sets the counter flag of the player timer
const uint32_t CMD_TIMER_COUNT_DOWN = 0x1000041;	// removes it
const uint32_t VAR_TIMER = 0x2000008;				// seconds
const uint32_t CMD_COMPLETE = 0x1001001;
const uint32_t CMD_STOP_THREAD = 0x1001000;
const uint32_t CMD_START_THREAD = 0x1001002;
const uint32_t FN_PROCESS_TARGET = 0x4000020;
const uint32_t VAR_MAX_COP_CARS = 0x2000103;
const uint32_t VAR_MIN_COP_CARS = 0x2000102;
const uint32_t WORD_MINUS_FOUR = 0xFFFFFFFC;

// car targets: D1 traffic type of SPAWN_CAR -> converted car model (type t = slot t - 1 of the city car list)
const int kCarModels[4] = { -1, 0, 1, 2 };	// 1 = cop car, 2 and 3 = the two civilian cars
const int kMaxPalette = 3;
const int kCarTargetIsCop = 8;

// new Point target flags of the engine (mission.c MRProcessTarget): see section 8 of the plan
const int kPointMaxSpeed = 0x4000000;		// only counts while the player is slower than target int 12
const int kPointMinTimer = 0x8000000;		// only counts once the countdown is down to int 13, int 14 = message offset
const int kSpeedUnitPercent = 100;			// D1 speed units -> D2 FIXEDH(wheel_speed), calibration knob
const int kTimerCountPerSecond = 3000;
const int kTimerUnitsPerSecond = 16384;
const int kFramesPerSecond = 30;
const int kHeatBase = 659, kHeatStep = 430, kMaxHeatLevel = 8;
const int kMaxCopsD1 = 3;
const int kMinDamage = 4096, kMaxDamage = 40000;		// D1 fragile cars use 4096 / 8192, D2 missions 20000..40000
const int kTimerActiveMin = 659;						// D1 timer values below this are "no timer"
const char kDefaultLoseTail[] = "Lose the tail.";

// Garage and Test Level missions (scripts 117 and 118 of MISSION.BLK, city id 2 = Training): start position, cops off, a long wait
// and "Well done!". Their objectives live in code the converter does not know (SETVAR 39 / 40, chase switches), so they become
// free drives: no target, no timer, no cops, no traffic (Training).
const int kTrainingCityId = 2;
const int kFreeDriveOps[] = { 1, 2, 10, 19, 22, 24, 34, 38, 40, 49, 52, 61, 67, 84 };
const int kFreeDriveVars[] = { 39, 40, 45, 66 };

// Ram missions: the D1 target car drives a recorded run (REPLAY.BLK) that needs the D1 physics. Adaptation: a chase car
// target that the engine puts on the lane nearest to the given spot (a little ahead of the player start), driven by the
// civilian AI, done once it has taken the D1 damage limit (var 31), the same rule as the D2 chases.
enum { OP_TARGET_SLOT = 69, OP_SLOT_VALUES = 71, OP_SLOT_FLAGS_13C = 96, OP_SLOT_LINK_CAR = 97, OP_TIMER_CLEAR = 58 };
struct RamSlotRecord { int op; int count; int args[5]; };
const int kRamSlotRecordCount = 4;
const RamSlotRecord kRamSlotRecords[kRamSlotRecordCount] = {		// identical in all ram missions
	{ OP_TARGET_SLOT, 4, { 0, 104857600, 0, 0, 0 } },
	{ OP_SLOT_LINK_CAR, 2, { 0, 23, 0, 0, 0 } },
	{ OP_SLOT_FLAGS_13C, 2, { 0, 65, 0, 0, 0 } },
	{ OP_SLOT_VALUES, 5, { 0, 750, 0, -200, 0 } },
};
const int kRamSetupOps[] = { 40, 61, 84 };		// D1 chase game mode switches, replaced by the D2 chase rules
const int kStateRam = 5;
const char kDefaultEscapeText[] = "Too late. Mission failed.";
const int kTargetChaseCar = 3;				// car target type: lead car that is chased
const int kRamCarModel = 1;					// converted civilian car
const int kRamCarAhead = 4000;				// units in front of the player start
// start spots (x, z) where the D1 heading faces a closed wall in the converted world: the Miami garage yard (missions 0, 4, 5, 9)
const int kTurnedStartSpots[][2] = { { 293839, -59343 } };
const int kRamCarDisplay = 0x70;			// blip, arrow and 3D marker like the D2 chases
const int kNoGettingFarMessage = 255;
const double kPi = 3.14159265358979323846;

// records the D2 mission logic has no counterpart for (cosmetic, HUD, unused tuning, hand-over to the next story mission)
const int kIgnoredOps[] = { 10, 11, 24, 28, 30, 31, 48, 51, 64, 67, 68, 88, 92, 104 };
const int kIgnoredVars[] = { 8, 9, 14, 15, 16, 28, 45, 47, 48, 49, 50, 52, 59, 60, 61, 69, 70 };
// passenger scene: pedestrians walk to or from the car, short camera. Skipped by design.
const int kSceneOps[] = { 74, 77, 78, 80, 82, 83, 112 };
// a spawned car with one of these records around has a role (target, scene, save) the converter cannot map yet
const int kCarRoleOps[] = { 20, 96, 97, 102, 113, 114 };

template<size_t N>
bool In(int value, const int (&list)[N])
{
	for (int v : list)
	{
		if (v == value)
			return true;
	}

	return false;
}

enum
{
	VAR_STATE = 3, VAR_FAIL_TEXT = 5, VAR_EARLY_TEXT = 7, VAR_MAX_DAMAGE = 10, VAR_PLAYER_DAMAGE = 12, VAR_COPS_BLOCK = 13,
	VAR_LOSE_TAIL_TEXT = 20, VAR_WRECK_TEXT = 21, VAR_SPEED_GATE = 32, VAR_SPEED_LIMIT = 33, VAR_COP_SPEED = 42,
	VAR_COP_POWER = 43, VAR_MIN_COPS = 46, VAR_SCENE_FILE = 0, VAR_TIMER_RAW = 1,
	VAR_RAM_MODE = 30, VAR_RAM_DAMAGE = 31, VAR_RAM_FLAG = 56, VAR_ESCAPE_TEXT = 23,		// D1 target code: ram mode (1), damage that completes the ram, flag (1), text when he gets away
};

const int kStateWaitTarget = 2;

// Zero terminated string block; Add returns the byte offset the script uses.
class Strings
{
public:
	int Add(const std::string& text)
	{
		auto it = m_index.find(text);
		if (it != m_index.end())
			return it->second;

		int at = (int)m_blob.size();
		m_index[text] = at;
		m_blob.insert(m_blob.end(), text.begin(), text.end());
		m_blob.push_back('\0');
		return at;
	}

	const std::vector<char>& Blob() const { return m_blob; }

private:
	std::vector<char>			m_blob;
	std::map<std::string, int>	m_index;
};

struct Target
{
	int ints[kTargetInts];

	Target() { memset(ints, 0, sizeof(ints)); }
};

class Converter
{
public:
	bool ConvertRecords(const std::vector<Record>& recs, std::vector<char>& d2ms, std::string& title, int& city);

	std::string	why;

private:
	bool Fail(const char* format, ...);
	bool AddPendingPointTarget();
	bool ApplyArrivalRulesToPointTarget(Target& target);
	bool AddRamCarTarget();
	bool ApplyRamSlotRecord(const Record& r);
	bool ApplyScriptVariable(const Record& r);
	bool ApplyTimerRecord(const Record& r);
	bool CheckOutputStructure(bool needTargetLoop);
	bool FinishFreeDrive(const std::vector<Record>& recs, std::vector<char>& d2ms);
	void PackD2msFile(std::vector<char>& d2ms) const;

	int					m_head[H_COUNT];
	Strings				m_strings;
	std::vector<Target>	m_targets;
	std::vector<uint32_t> m_script;
	std::set<std::string> m_notes;

	// state of the D1 script while it is read
	bool m_gateOn = false, m_earlyOn = false, m_copsBlock = false, m_copsOn = false;
	int m_gateLimit = 0, m_earlyLimit = 0, m_maxCops = 1, m_minCops = 0, m_damageFactor = 4096;
	bool m_timerStarted = false, m_timerUp = false, m_waiting = false;
	int m_pointIndex = -1;
	Target m_pending;
	bool m_hasPending = false, m_pendingAdded = false;
	bool m_hasCops = false;
	std::string m_failText, m_earlyText, m_loseText, m_wreckText, m_escapeText;
	bool m_hasFailText = false, m_hasEarlyText = false, m_hasLoseText = false, m_hasWreckText = false, m_hasEscapeText = false;

	// ram target (STATE 5)
	bool m_hasRamSlot = false, m_ramWaiting = false, m_ramDone = false;
	unsigned m_ramRecords = 0;			// bit per kRamSlotRecords entry seen so far
	int m_ramIndex = -1, m_ramDamage = -1;

public:
	const std::set<std::string>& Notes() const { return m_notes; }
};

bool Converter::Fail(const char* format, ...)
{
	char line[256];
	va_list args;
	va_start(args, format);
	vsnprintf(line, sizeof(line), format, args);
	va_end(args);

	if (why.empty())
		why = line;

	return false;
}

// A target counts as soon as TARGET0 defines it (later legs of a mission have no TARGET0_ON).
bool Converter::AddPendingPointTarget()
{
	if ((int)m_targets.size() >= kMaxTargets)
		return Fail("more than 16 targets");

	m_targets.push_back(m_pending);
	m_pointIndex = (int)m_targets.size() - 1;
	m_pendingAdded = true;
	return true;
}

// The D1 target code reads these variables live while it waits, so they apply at WAIT_STATE.
bool Converter::ApplyArrivalRulesToPointTarget(Target& target)
{
	if (m_gateOn)
	{
		target.ints[1] |= kPointMaxSpeed;
		target.ints[12] = m_gateLimit;
		m_notes.insert("arrival speed limit (engine flag)");
	}

	if (m_earlyOn)
	{
		if (m_timerUp)
			return Fail("EARLY_CHECK while the timer counts up");
		if (!m_hasEarlyText)
			return Fail("EARLY_CHECK without text");

		target.ints[1] |= kPointMinTimer;
		target.ints[13] = (int)((long long)m_earlyLimit * kTimerCountPerSecond / kTimerUnitsPerSecond);
		target.ints[14] = m_strings.Add(m_earlyText);
		m_notes.insert("arrival only when the timer is low enough (engine flag)");
	}

	if (m_copsBlock)
	{
		target.ints[8] = m_strings.Add(m_hasLoseText ? m_loseText : kDefaultLoseTail);		// D2's own lose-the-tail rule
		m_notes.insert("lose the tail rule (D2 loseTailMessage)");
	}

	return true;
}

// STATE 5: the target car is a chase car a little ahead of the player start; the ram is done at the D1 damage limit
bool Converter::AddRamCarTarget()
{
	if ((int)m_targets.size() >= kMaxTargets)
		return Fail("more than 16 targets");

	double angle = m_head[H_START_ROT] * 2 * kPi / 4096;

	Target car;
	car.ints[0] = kTargetCar;
	car.ints[2] = kRamCarDisplay;
	car.ints[3] = m_head[H_START_X] + (int)floor(sin(angle) * kRamCarAhead + 0.5);
	car.ints[4] = m_head[H_START_Z] + (int)floor(cos(angle) * kRamCarAhead + 0.5);
	car.ints[5] = m_head[H_START_ROT];
	car.ints[6] = -1;
	car.ints[7] = kRamCarModel;
	car.ints[9] = kTargetChaseCar;
	car.ints[13] = m_ramDamage;
	car.ints[15] = kNoGettingFarMessage;		// int 14 (message when he gets away) is set at the end
	m_targets.push_back(car);

	m_ramIndex = (int)m_targets.size() - 1;
	m_ramWaiting = false;
	m_ramDone = true;
	m_notes.insert("ram target = civilian AI chase car (approximation, D1 replay needs D1 physics)");
	return true;
}

// TARGET_SLOT / SLOT_LINK_CAR / SLOT_FLAGS_13C / SLOT_VALUES: only the one combination seen in every ram mission is accepted
bool Converter::ApplyRamSlotRecord(const Record& r)
{
	for (int k = 0; k < (int)kRamSlotRecordCount; k++)
	{
		const RamSlotRecord& slot = kRamSlotRecords[k];
		if (slot.op != r.op)
			continue;

		unsigned bit = 1u << k;
		bool sameArgs = (int)r.args.size() == slot.count && memcmp(r.args.data(), slot.args, slot.count * sizeof(int)) == 0;

		if ((m_ramRecords & bit) || !sameArgs || (k != 0 && !(m_ramRecords & 1)))
			return Fail("target slot record %d", r.op);

		m_ramRecords |= bit;
		return true;
	}

	return Fail("record %d", r.op);
}

bool Converter::ApplyScriptVariable(const Record& r)
{
	int var = r.args[0], value = r.args[1];

	if (var == VAR_FAIL_TEXT && r.hasText)
	{
		if (m_hasFailText && m_failText != r.text)
			return Fail("two different timeout texts");

		m_failText = r.text;
		m_hasFailText = true;
	}
	else if (var == VAR_STATE && value == kStateRam)
	{
		if (m_ramRecords != (1u << kRamSlotRecordCount) - 1 || m_ramDamage < 0)
			return Fail("STATE 5 without a complete target slot and damage limit");
		if (m_hasPending || m_ramDone)
			return Fail("ram target together with another target");

		m_ramWaiting = true;
	}
	else if (var == VAR_STATE)
	{
		if (value != kStateWaitTarget)
			return Fail("STATE %d", value);

		m_waiting = true;
	}
	else if (var == VAR_ESCAPE_TEXT && r.hasText)
	{
		m_escapeText = r.text;
		m_hasEscapeText = true;
	}
	else if ((var == VAR_RAM_MODE || var == VAR_RAM_FLAG) && m_hasRamSlot)
	{
		if (value != 1)
			return Fail("SETVAR %d = %d", var, value);
	}
	else if (var == VAR_RAM_DAMAGE && m_hasRamSlot)
	{
		if (value < kMinDamage || value > kMaxDamage)
			return Fail("ram damage limit %d", value);

		m_ramDamage = value;
	}
	else if (var == VAR_EARLY_TEXT && r.hasText)
	{
		m_earlyText = r.text;
		m_hasEarlyText = true;
	}
	else if (var == VAR_LOSE_TAIL_TEXT && r.hasText)
	{
		m_loseText = r.text;
		m_hasLoseText = true;
	}
	else if (var == VAR_WRECK_TEXT && r.hasText)
	{
		m_wreckText = r.text;
		m_hasWreckText = true;
	}
	else if (var == VAR_COP_SPEED || var == VAR_COP_POWER)
	{
		if (value < 2048 || value > 8192)
			return Fail("cop scale var %d = %d", var, value);

		m_head[var == VAR_COP_SPEED ? H_COP_SPEED : H_COP_POWER] = value;
	}
	else if (var == VAR_PLAYER_DAMAGE)
	{
		if (value < 1024 || value > 8192)
			return Fail("player damage factor %d", value);

		m_damageFactor = value;
	}
	else if (var == VAR_MIN_COPS)
	{
		if (value < 0 || value > kMaxCopsD1)
			return Fail("minimum cops %d", value);

		m_minCops = value;
		if (m_copsOn)
		{
			m_script.push_back(value);
			m_script.push_back(VAR_MIN_COP_CARS);
			m_script.push_back(CMD_SET_VARIABLE);
		}
	}
	else if (var == VAR_COPS_BLOCK)
	{
		if (value != 0 && value != 1)
			return Fail("COPS_BLOCK_ARRIVAL %d", value);

		m_copsBlock = value == 1;
	}
	else if (var == VAR_SPEED_GATE)
	{
		if (value != 0 && value != 1)
			return Fail("SPEED_GATE %d", value);

		m_gateOn = value == 1;
	}
	else if (var == VAR_SPEED_LIMIT)
		m_gateLimit = (int)((long long)value * kSpeedUnitPercent / 100);
	else if (var == VAR_MAX_DAMAGE)
	{
		if (value < kMinDamage || value > kMaxDamage)
			return Fail("var 10 = %d", value);

		m_head[H_MAX_DAMAGE] = value;
	}
	else if (var == VAR_SCENE_FILE && r.hasText)
		m_notes.insert("scene file name ignored");
	else if (var == VAR_TIMER_RAW)
	{
		// var 1 is the D1 mission timer itself; a value below 659 right after TIMER_UP is the count-up start (0)
		if (value >= kTimerActiveMin || !m_timerUp)
			return Fail("SETVAR timer = %d", value);
	}
	else if (!In(var, kIgnoredVars) || r.hasText)
		return Fail("SETVAR %d", var);

	return true;
}

// TIMER_UP / TIMER_DOWN: the first one fills the mission header, later ones switch the D2 timer mode in the script
bool Converter::ApplyTimerRecord(const Record& r)
{
	bool countingUp = r.op == OP_TIMER_UP;
	int seconds = countingUp ? 0 : r.args[0] / kTimerUnitsPerSecond;

	if (seconds < 0 || seconds > 0x7FFF)
		return Fail("timer of %d seconds", seconds);

	if (!m_timerStarted)
	{
		if (m_pointIndex >= 0)
			return Fail("timer first started after the first target");

		m_head[H_TIMER] = (countingUp ? kTimerCounterFlag << 16 : 0) | seconds;
		m_timerStarted = true;
	}
	else
	{
		m_script.push_back(countingUp ? CMD_TIMER_COUNT_UP : CMD_TIMER_COUNT_DOWN);
		m_script.push_back(seconds);
		m_script.push_back(VAR_TIMER);
		m_script.push_back(CMD_SET_VARIABLE);
	}

	m_timerUp = countingUp;
	return true;
}

// Structural proof that the output obeys the D2 interpreter's rules.
bool Converter::CheckOutputStructure(bool needTargetLoop)
{
	std::set<int> starts;
	const std::vector<char>& blob = m_strings.Blob();

	starts.insert(0);
	for (size_t i = 0; i + 1 < blob.size(); i++)
	{
		if (blob[i] == 0)
			starts.insert((int)i + 1);
	}

	const int texts[] = { H_MSG_WRECKED, H_MSG_OUT_OF_TIME, H_MSG_COMPLETE, H_MSG_DROWNED };
	for (int t : texts)
	{
		if (!starts.count(m_head[t]))
			return Fail("check: header text %d is not a string start", t);
	}

	for (const Target& t : m_targets)
	{
		if (t.ints[0] != kTargetPoint)
		{
			if (t.ints[0] == kTargetCar && t.ints[9] == kTargetChaseCar && !starts.count(t.ints[14]))
				return Fail("check: chase car message offset");

			continue;
		}

		if (t.ints[8] != -1 && !starts.count(t.ints[8]))
			return Fail("check: lose-tail text offset");
		if ((t.ints[1] & kPointMinTimer) && !starts.count(t.ints[14]))
			return Fail("check: early text offset");
	}

	const std::vector<uint32_t>& s = m_script;
	int n = (int)s.size();

	if (!n || s[n - 1] != CMD_STOP_THREAD)
		return Fail("check: script does not end with StopThread");

	int loops = 0;
	for (int i = 0; i < n; i++)
	{
		if (s[i] == CMD_SHOW_MESSAGE && (i < 2 || !starts.count((int)s[i - 1]) || s[i - 2] == 0 || s[i - 2] >= 100))
			return Fail("check: message at word %d", i);

		if (s[i] == FN_PROCESS_TARGET && (i < 1 || s[i - 1] >= (uint32_t)m_targets.size() || m_targets[s[i - 1]].ints[0] == 0))
			return Fail("check: ProcessTarget at word %d uses an empty target", i);

		if (s[i] == CMD_BRANCH_IF)
		{
			if (i < 3 || s[i - 1] != WORD_MINUS_FOUR || s[i - 2] != FN_PROCESS_TARGET)
				return Fail("check: BranchIf at word %d is not a ProcessTarget loop", i);

			loops++;
		}

		if (s[i] == CMD_START_THREAD)
		{
			long long start = i < 1 ? -1 : (long long)i + 1 + (int)s[i - 1];
			if (start < 0 || start >= n - 4 || s[(size_t)start + 1] != FN_PROCESS_TARGET || s[(size_t)start + 4] != CMD_STOP_THREAD)
				return Fail("check: thread start at word %d", i);
		}
	}

	if (!loops && needTargetLoop)
		return Fail("check: no target loop");

	return true;
}

void Converter::PackD2msFile(std::vector<char>& d2ms) const
{
	d2ms.clear();
	Put(d2ms, m_head, sizeof(m_head));

	for (int i = 0; i < kMaxTargets; i++)
	{
		Target empty;
		const Target& t = i < (int)m_targets.size() ? m_targets[i] : empty;
		Put(d2ms, t.ints, sizeof(t.ints));
	}

	if (!m_script.empty())
		Put(d2ms, m_script.data(), m_script.size() * 4);

	Put(d2ms, m_strings.Blob().data(), m_strings.Blob().size());
}

bool Converter::ConvertRecords(const std::vector<Record>& recs, std::vector<char>& d2ms, std::string& title, int& city)
{
	const Record* first[128] = {};
	for (const Record& r : recs)
	{
		if (r.op >= 0 && r.op < 128 && !first[r.op])
			first[r.op] = &r;
	}

	const int header[] = { OP_START_POS, OP_START_ROT, OP_CITY };
	for (int op : header)
	{
		if (!first[op])
			return Fail("header record %d missing", op);
	}

	city = CityOf(first[OP_CITY]->args[0]);
	if (city < 0)
		return Fail("city id %d", first[OP_CITY]->args[0]);

	memset(m_head, 0, sizeof(m_head));
	m_head[H_ID] = 0x44324D53;		// "SM2D"
	m_head[H_SIZE] = sizeof(m_head);
	m_head[H_CITY] = city;
	m_head[H_TIME] = kTimeDay;
	m_head[H_START_X] = first[OP_START_POS]->args[0];
	m_head[H_START_Z] = first[OP_START_POS]->args[1];
	m_head[H_START_ROT] = first[OP_START_ROT]->args[0] & 4095;
	for (const int (&spot)[2] : kTurnedStartSpots)
	{
		if (m_head[H_START_X] == spot[0] && m_head[H_START_Z] == spot[1])
			m_head[H_START_ROT] = (m_head[H_START_ROT] + 2048) & 4095;
	}
	m_head[H_COP_SPEED] = 4096;
	m_head[H_COP_POWER] = 4096;
	m_head[H_COP_MAX] = 2;
	m_head[H_MSG_OUT_OF_TIME] = -1;
	m_head[H_MSG_COMPLETE] = -1;
	m_head[H_MSG_DROWNED] = -1;
	m_head[H_CAR_MODEL] = 1;
	m_head[H_MAX_DAMAGE] = 22000;
	m_head[H_RES0] = 1;
	m_head[H_RES1] = 2;
	m_head[H_RES2] = 3;
	m_head[H_RES4] = 10;
	m_head[H_TITLE] = -1;
	m_head[H_MSG_POLICE] = -1;
	m_head[H_MSG_DOORS] = -1;

	if (first[OP_CITY]->args[0] == kTrainingCityId)
		return FinishFreeDrive(recs, d2ms);

	for (const Record& r : recs)
	{
		m_hasCops |= r.op == OP_COPS_ON;
		m_hasRamSlot |= r.op == OP_TARGET_SLOT;
	}

	int lastWait = -1;
	for (int i = 0; i < (int)recs.size(); i++)
	{
		if (recs[i].op == OP_WAIT_STATE)
			lastWait = i;
	}

	bool carRole = false;
	for (const Record& r : recs)
		carRole |= In(r.op, kCarRoleOps);

	std::string completeText;
	bool hasCompleteText = false, titleSeen = false;
	std::vector<std::pair<int, int>> carThreads;		// (script index of the address push, target index)
	int waits = 0;

	for (int i = 0; i < (int)recs.size(); i++)
	{
		const Record& r = recs[i];
		int op = r.op;

		if (op == OP_MESSAGE)
		{
			if (!titleSeen)
			{
				titleSeen = true;
				title = r.text;
			}
			else if (i > lastWait)
			{
				completeText = r.text;
				hasCompleteText = true;
			}
			else
			{
				int seconds = r.args[0] / kFramesPerSecond;

				m_script.push_back(seconds < 1 ? 1 : seconds);
				m_script.push_back(m_strings.Add(r.text));
				m_script.push_back(CMD_SHOW_MESSAGE);
			}
		}
		else if (op == OP_SETVAR)
		{
			if (!ApplyScriptVariable(r))
				return false;
		}
		else if (op == OP_TIMER_UP || op == OP_TIMER_DOWN)
		{
			if (!ApplyTimerRecord(r))
				return false;
		}
		else if (op == OP_TARGET0)
		{
			m_pending = Target();
			m_pending.ints[0] = kTargetPoint;
			m_pending.ints[2] = kPointDisplay;
			m_pending.ints[3] = r.args[1];		// x
			m_pending.ints[4] = r.args[2];		// z
			m_pending.ints[5] = r.args[0];		// radius
			m_pending.ints[8] = -1;
			m_hasPending = true;
			m_pendingAdded = false;
		}
		else if (op == OP_TARGET0_ON)
		{
			if (!m_hasPending)
				return Fail("TARGET0_ON without a target");

			if (!m_pendingAdded && !AddPendingPointTarget())
				return false;

			m_waiting = true;		// switching a target on arms the wait (STATE keeps its value 2)
		}
		else if (op == OP_WAIT_STATE && m_ramWaiting)
		{
			if (!AddRamCarTarget())
				return false;

			m_script.push_back(m_ramIndex);
			m_script.push_back(FN_PROCESS_TARGET);
			m_script.push_back(WORD_MINUS_FOUR);
			m_script.push_back(CMD_BRANCH_IF);
			waits++;
		}
		else if (op == OP_WAIT_STATE)
		{
			if (m_hasPending && !m_pendingAdded && !AddPendingPointTarget())
				return false;

			if (!m_waiting || m_pointIndex < 0)
				return Fail("WAIT_STATE without a target");

			if (!ApplyArrivalRulesToPointTarget(m_targets[m_pointIndex]))
				return false;

			m_script.push_back(m_pointIndex);
			m_script.push_back(FN_PROCESS_TARGET);
			m_script.push_back(WORD_MINUS_FOUR);
			m_script.push_back(CMD_BRANCH_IF);
			m_waiting = false;
			waits++;
		}
		else if (op == OP_SPAWN_CAR)
		{
			int carType = r.args[1], palette = r.args[2], rot = r.args[3], cx = r.args[4], cz = r.args[5];

			if (carType < 1 || carType > 3 || palette < 0 || palette > kMaxPalette)
				return Fail("SPAWN_CAR type %d palette %d", carType, palette);
			if (carRole)
				return Fail("spawned car has a role (target, scene or save)");
			if ((int)m_targets.size() >= kMaxTargets)
				return Fail("more than 16 targets");

			Target car;
			car.ints[0] = kTargetCar;
			car.ints[3] = cx;
			car.ints[4] = cz;
			car.ints[5] = rot & 4095;
			car.ints[6] = -1;
			car.ints[7] = kCarModels[carType];
			car.ints[8] = palette;
			car.ints[10] = carType == 1 ? kCarTargetIsCop : 0;
			m_targets.push_back(car);

			carThreads.push_back(std::make_pair((int)m_script.size(), (int)m_targets.size() - 1));
			m_script.push_back(0);		// address patched below
			m_script.push_back(CMD_START_THREAD);
		}
		else if (op == OP_HEAT_MIN)
		{
			int level = r.args[0];
			if (level < 0 || level > kMaxHeatLevel)
				return Fail("HEAT_MIN %d", level);

			if (level)
			{
				m_script.push_back(kHeatBase + (level - 1) * kHeatStep);
				m_script.push_back(CMD_SET_FELONY);
				m_notes.insert("start heat set as D2 felony");
			}
		}
		else if (op == OP_MAX_COPS)
		{
			if (r.args[0] < 0 || r.args[0] > kMaxCopsD1)
				return Fail("max cops %d", r.args[0]);

			m_maxCops = r.args[0];
			if (m_copsOn)
			{
				m_script.push_back(m_maxCops);
				m_script.push_back(VAR_MAX_COP_CARS);
				m_script.push_back(CMD_SET_VARIABLE);
			}
		}
		else if (op == OP_COPS_ON || op == OP_COPS_OFF)
		{
			// cops on during a chase leg, off again after it; a mission without COPS_ON never has cops
			m_copsOn = op == OP_COPS_ON;
			if (m_hasCops)
			{
				m_script.push_back(m_copsOn ? m_maxCops : 0);
				m_script.push_back(VAR_MAX_COP_CARS);
				m_script.push_back(CMD_SET_VARIABLE);

				if (m_copsOn && m_minCops)
				{
					m_script.push_back(m_minCops);
					m_script.push_back(VAR_MIN_COP_CARS);
					m_script.push_back(CMD_SET_VARIABLE);
				}
			}
		}
		else if (op == OP_EARLY_LIMIT)
			m_earlyLimit = r.args[0];
		else if (op == OP_EARLY_ON || op == OP_EARLY_OFF)
			m_earlyOn = op == OP_EARLY_ON;
		else if (op == OP_WRECK_CHECK || op == OP_START_POS || op == OP_START_ROT || op == OP_CITY || op == OP_BEGIN_MISSION)
			;
		else if (op == OP_END)
		{
			if (i != (int)recs.size() - 1)
				return Fail("END in the middle");

			m_script.push_back(CMD_COMPLETE);
			m_script.push_back(CMD_STOP_THREAD);
		}
		else if (op == OP_TARGET_SLOT || op == OP_SLOT_LINK_CAR || op == OP_SLOT_FLAGS_13C || op == OP_SLOT_VALUES)
		{
			if (!ApplyRamSlotRecord(r))
				return false;
		}
		else if (In(op, kRamSetupOps) && m_hasRamSlot)
			m_notes.insert("D1 chase mode switch replaced by the D2 chase rules");
		else if (op == OP_TIMER_CLEAR)
		{
			if (m_timerStarted)
				return Fail("TIMER_CLEAR after a timer started");
		}
		else if (In(op, kSceneOps))
			m_notes.insert("passenger scene skipped");
		else if (In(op, kIgnoredOps))
			m_notes.insert("ignored records");
		else
			return Fail("record %d", op);
	}

	if (!waits)
		return Fail("no target to reach");

	// one helper thread per parked car: ProcessTarget every frame creates the car when the player is near
	for (const std::pair<int, int>& thread : carThreads)
	{
		m_script[thread.first] = (uint32_t)((int)m_script.size() - (thread.first + 2));		// pc is behind the command when the thread starts
		m_script.push_back(thread.second);
		m_script.push_back(FN_PROCESS_TARGET);
		m_script.push_back(WORD_MINUS_FOUR);
		m_script.push_back(CMD_BRANCH_IF);
		m_script.push_back(CMD_STOP_THREAD);
	}

	m_head[H_MAX_DAMAGE] = (int)((long long)m_head[H_MAX_DAMAGE] * 4096 / m_damageFactor);		// half damage taken = twice the capacity
	m_head[H_MSG_WRECKED] = m_strings.Add(m_hasWreckText ? m_wreckText : "You wrecked your vehicle.");
	m_head[H_MSG_OUT_OF_TIME] = m_strings.Add(m_hasFailText ? m_failText : "Out of time");
	m_head[H_MSG_COMPLETE] = m_strings.Add(hasCompleteText ? completeText : "Well done.");
	m_head[H_MSG_DROWNED] = m_strings.Add("You drowned.");
	m_head[H_STRINGS] = (int)m_script.size();

	if (m_ramDone)
	{
		m_targets[m_ramIndex].ints[14] = m_strings.Add(m_hasEscapeText ? m_escapeText : m_hasFailText ? m_failText : kDefaultEscapeText);
	}

	if (!CheckOutputStructure(true))
		return false;

	PackD2msFile(d2ms);
	return true;
}

bool Converter::FinishFreeDrive(const std::vector<Record>& recs, std::vector<char>& d2ms)
{
	for (const Record& r : recs)
	{
		if (!In(r.op, kFreeDriveOps))
			return Fail("free drive: record %d", r.op);

		if (r.op == OP_SETVAR && !In(r.args[0], kFreeDriveVars))
			return Fail("free drive: SETVAR %d", r.args[0]);
	}

	m_head[H_COP_MAX] = 0;
	m_script.push_back(CMD_STOP_THREAD);

	m_head[H_MSG_WRECKED] = m_strings.Add("You wrecked your vehicle.");
	m_head[H_MSG_OUT_OF_TIME] = m_strings.Add("Out of time");
	m_head[H_MSG_COMPLETE] = m_strings.Add("Well done.");
	m_head[H_MSG_DROWNED] = m_strings.Add("You drowned.");
	m_head[H_STRINGS] = (int)m_script.size();

	if (!CheckOutputStructure(false))
		return false;

	PackD2msFile(d2ms);
	return true;
}

} // namespace

bool D1Missions_ConvertScript(const std::vector<char>& script, std::vector<char>& d2ms, std::string& title, int& city, std::string& why)
{
	std::vector<Record> recs;
	if (!ParseScript(script, recs, why))
		return false;

	Converter converter;
	bool ok = converter.ConvertRecords(recs, d2ms, title, city);
	why = converter.why;
	return ok;
}

// ---- mission list for the menu ----

int gD1MissionsEnabled = 1;

namespace
{

struct MissionEntry
{
	int			number;
	int			city;
	std::string	title;
};

std::vector<MissionEntry>	g_missions;
std::string					g_log;
signed char					g_state;		// 0 not tried, 1 prepared, -1 nothing usable

const char kBlkPath[] = D1DLC_DATA_FOLDER "MISSIONS\\MISSION.BLK";
const char kListPath[] = D1DLC_CONVERTED_FOLDER "MISSIONS.LST";
const char kStampPath[] = D1DLC_CONVERTED_FOLDER "MISSIONS.VER";

bool ReadWhole(const char* path, std::vector<char>& data)
{
	FILE* fp = fopen(path, "rb");
	if (!fp)
		return false;

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	data.resize(size > 0 ? size : 0);
	bool ok = size > 0 && fread(data.data(), 1, size, fp) == (size_t)size;
	fclose(fp);
	return ok;
}

bool WriteWhole(const std::string& path, const void* data, size_t size)
{
	FILE* fp = fopen(path.c_str(), "wb");
	if (!fp)
		return false;

	bool ok = fwrite(data, 1, size, fp) == size;
	return fclose(fp) == 0 && ok;
}

std::string MissionPath(int number)
{
	char path[128];
	snprintf(path, sizeof(path), "%sM%d.D2MS", D1DLC_CONVERTED_FOLDER, number);
	return path;
}

// printable ASCII only, the frontend font has nothing else
std::string MenuTitle(const std::string& raw, int story)
{
	std::string title;

	// the scripts hold CAPITALS, the Driver 2 menus use Capitalised Words
	bool wordStart = true;

	for (char c : raw)
	{
		if (c < 32 || c >= 127)
			c = ' ';

		title += (c >= 'A' && c <= 'Z' && !wordStart) ? (char)(c + 'a' - 'A') : c;
		wordStart = c == ' ' || c == '-';
	}

	while (!title.empty() && title.back() == ' ')
		title.pop_back();

	if (title.size() > 31)
		title.resize(31);

	return title.empty() ? "Mission " + std::to_string(story + 1) : title;
}

bool StampIsCurrent(size_t blkSize)
{
	std::vector<char> text;
	if (!ReadWhole(kStampPath, text))
		return false;

	text.push_back('\0');

	int version = 0;
	unsigned size = 0;
	return sscanf(text.data(), "%d %u", &version, &size) == 2 && version == D1MISSION_VERSION && size == blkSize;
}

// reads MISSIONS.LST; false when a listed mission file is gone (the whole set is converted again)
bool LoadList()
{
	std::vector<char> text;
	if (!ReadWhole(kListPath, text))
		return false;

	text.push_back('\0');
	g_missions.clear();

	for (const char* line = text.data(); *line;)
	{
		MissionEntry e;
		int used = 0;

		if (sscanf(line, "%d %d %n", &e.number, &e.city, &used) != 2 || used == 0)
			return false;

		const char* end = strchr(line + used, '\n');
		e.title.assign(line + used, end ? end : line + used + strlen(line + used));
		while (!e.title.empty() && e.title.back() == '\r')
			e.title.pop_back();

		FILE* fp = fopen(MissionPath(e.number).c_str(), "rb");
		if (!fp)
			return false;

		fclose(fp);
		g_missions.push_back(e);
		line = end ? end + 1 : line + strlen(line);
	}

	return !g_missions.empty();
}

bool ConvertAll(const std::vector<char>& blk)
{
	const uint32_t* offs = (const uint32_t*)blk.data();
	uint32_t table = offs[0];

	if (table % 4 || table / 4 < D1MISSION_STORY_COUNT || table > blk.size())
	{
		g_log += "MISSION.BLK: offset table is not valid\n";
		return false;
	}

	MakeFolder("DRIVER1");
	MakeFolder("DRIVER1\\CONVERTED");

	std::string list;
	int skipped = 0;
	g_missions.clear();

	// the 44 story missions, then the Garage and Test Level missions (free drives in the Training level)
	for (int k = 0; k < D1MISSION_STORY_COUNT + D1MISSION_FREE_COUNT; k++)
	{
		const int scriptIndex = k < D1MISSION_STORY_COUNT ? k : D1MISSION_FREE_FIRST + k - D1MISSION_STORY_COUNT;

		if (scriptIndex >= (int)(table / 4))
			break;

		uint32_t from = offs[scriptIndex], to = (scriptIndex + 1 < (int)(table / 4)) ? offs[scriptIndex + 1] : (uint32_t)blk.size();

		if (from > to || to > blk.size())
		{
			Log(g_log, "mission %d skipped: offsets are not valid", k);
			skipped++;
			continue;
		}

		std::vector<char> script(blk.begin() + from, blk.begin() + to), d2ms;
		std::string title, why;
		int city = 0;

		if (!D1Missions_ConvertScript(script, d2ms, title, city, why))
		{
			Log(g_log, "mission %d skipped: %s", k, why.c_str());
			skipped++;
			continue;
		}

		MissionEntry e;
		e.number = D1MISSION_BASE + scriptIndex;
		e.city = city;
		e.title = k < D1MISSION_STORY_COUNT ? MenuTitle(title, k) : k == D1MISSION_STORY_COUNT ? "Garage Mission" : "Test Level Mission";

		if (!WriteWhole(MissionPath(e.number), d2ms.data(), d2ms.size()))
		{
			Log(g_log, "mission %d skipped: cannot write %s", k, MissionPath(e.number).c_str());
			skipped++;
			continue;
		}

		list += std::to_string(e.number) + " " + std::to_string(e.city) + " " + e.title + "\n";
		g_missions.push_back(e);
	}

	Log(g_log, "Driver 1 missions: %d converted, %d skipped", (int)g_missions.size(), skipped);

	// the stamp is written last: a half written set is converted again
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "%d %u", D1MISSION_VERSION, (unsigned)blk.size());

	return WriteWhole(kListPath, list.data(), list.size()) && WriteWhole(kStampPath, stamp, strlen(stamp)) && !g_missions.empty();
}

bool Prepare()
{
	std::vector<char> blk;

	g_log.clear();
	g_missions.clear();

	if (!gD1MissionsEnabled || !ReadWhole(kBlkPath, blk) || blk.size() < 4)
		return false;

	if (StampIsCurrent(blk.size()) && LoadList())
		return true;

	return ConvertAll(blk);
}

} // namespace

bool D1Missions_Available()
{
	if (g_state == 0)
		g_state = Prepare() ? 1 : -1;

	return g_state > 0;
}

int D1Missions_CityCount(int gameLevel)
{
	int count = 0;

	if (D1Missions_Available())
	{
		for (const MissionEntry& e : g_missions)
			count += e.city == gameLevel;
	}

	return count;
}

bool D1Missions_Entry(int gameLevel, int index, int* number, const char** title)
{
	if (!D1Missions_Available())
		return false;

	for (const MissionEntry& e : g_missions)
	{
		if (e.city == gameLevel && index-- == 0)
		{
			*number = e.number;
			*title = e.title.c_str();
			return true;
		}
	}

	return false;
}

const char* D1Missions_Title(int number)
{
	if (D1Missions_Available())
	{
		for (const MissionEntry& e : g_missions)
		{
			if (e.number == number)
				return e.title.c_str();
		}
	}

	return NULL;
}

const char* D1Missions_LastLog()
{
	return g_log.c_str();
}
