#include "driver2.h"
#include "d1garage.h"
#include "mission.h"
#include "civ_ai.h"
#include "cars.h"
#include "pres.h"
#include "overlay.h"
#include "system.h"
#include "pad.h"
#include "gamesnd.h"
#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define D1GARAGE_FILE_SIZE		17120
#define D1GARAGE_TASKS			9
#define D1GARAGE_CARS			6
#define D1GARAGE_SEQUENCES		6
#define D1GARAGE_SEQUENCE_SIZE	15		// length, score, up to 13 zones (newest first)
#define D1GARAGE_SLALOMS		4		// sequences 0..3 are slaloms, the others laps
#define D1GARAGE_HISTORY		13
#define D1GARAGE_BANDS			5
#define D1GARAGE_TEXT_SIZE		48

#define D1GARAGE_START_PAUSE	60		// frames the start text stands before the clock runs
#define D1GARAGE_HIT_PAUSE		30		// frames after a hit before the next one counts
#define D1GARAGE_FIRST_PAUSE	10
#define D1GARAGE_WRECKED		4		// hits
#define D1GARAGE_LIST_RIGHT		102		// right edge of the task list from gOverlayXOppPos: the edge of the overlay bars

#define OP_ADDIU	9
#define OP_SLTIU	11

enum
{
	TASK_BURNOUT, TASK_HANDBRAKE, TASK_SLALOM, TASK_180, TASK_360, TASK_REVERSE_180, TASK_SPEED, TASK_BRAKE, TASK_LAP
};

static char* sTaskNames[D1GARAGE_TASKS] = { "Burnout", "Handbrake", "Slalom", "180", "360", "Reverse 180", "Speed", "Brake", "Lap" };

enum
{
	TEXT_START, TEXT_PASSED, TEXT_WRECKED, TEXT_OUT_OF_TIME, TEXT_HIT_CAR, TEXT_PAINTWORK, TEXT_COUNT
};

static const int kTextOffsets[TEXT_COUNT] = { 0x28, 0x44, 0x64, 0x90, 0xB0, 0xC4 };

// voice file lump of the test and its samples (docs/dev-notes/d1-desert-garage.md)
#define D1GARAGE_SPEECH_LUMP	2

enum
{
	SPEECH_WRECKED = 5, SPEECH_OUT_OF_TIME = 6, SPEECH_PASSED = 7
};

static int sSpeechLoaded;

typedef struct
{
	int start, size;
} D1GARAGE_SPAN;

// what the overlay file holds (offsets: docs/dev-notes/d1-desert-garage.md)
static struct
{
	int state;								// 0 not read yet, 1 usable, -1 missing or not the expected file
	char text[TEXT_COUNT][D1GARAGE_TEXT_SIZE];
	int timeFrames;
	u_char sequence[D1GARAGE_SEQUENCES][D1GARAGE_SEQUENCE_SIZE];
	D1GARAGE_SPAN column[2], band[D1GARAGE_BANDS];
	int carX[D1GARAGE_CARS], carZ[D1GARAGE_CARS], carDir[D1GARAGE_CARS];
} sFile;

static struct
{
	int frame, over, passed, hits, hitPause;
	char done[D1GARAGE_TASKS];
	int carSlot[D1GARAGE_CARS], carDamage[D1GARAGE_CARS], playerDamage;
	int zone, history[D1GARAGE_HISTORY];
	int spin180, spin360, reverseArmed, reverseFrames, fastFrames, braking, brakeFrames;
} sTest;

// signed immediate of the instruction at ofs, or 0x7fffffff when the opcode is not the expected one
static int Immediate(const u_char* bin, int ofs, int opcode)
{
	u_int word = bin[ofs] | bin[ofs + 1] << 8 | bin[ofs + 2] << 16 | (u_int)bin[ofs + 3] << 24;

	return (int)(word >> 26) == opcode ? (short)(word & 0xffff) : 0x7fffffff;
}

static int ReadSpan(const u_char* bin, int addOfs, int sizeOfs, D1GARAGE_SPAN* span)
{
	int add = Immediate(bin, addOfs, OP_ADDIU);
	int size = Immediate(bin, sizeOfs, OP_SLTIU);

	span->start = -add;
	span->size = size;
	return add != 0x7fffffff && size > 0 && size != 0x7fffffff;
}

static int ReadFile(void)
{
	static const int bandOfs[D1GARAGE_BANDS][2] = { { 0x868, 0x86C }, { 0x874, 0x880 }, { 0x888, 0x894 }, { 0x89C, 0x8A8 }, { 0x8B0, 0x8BC } };
	char filename[64];
	u_char* bin;
	int ok, i, k;

	sprintf(filename, "..\\DRIVER1\\IVIEW.BIN");
	if (!FileExists(filename))
		return 0;

	bin = (u_char*)malloc(D1GARAGE_FILE_SIZE);
	if (bin == NULL)
		return 0;

	LoadfileSeg(filename, (char*)bin, 0, D1GARAGE_FILE_SIZE);

	ok = ReadSpan(bin, 0x85C, 0x860, &sFile.column[0]) && ReadSpan(bin, 0x8D0, 0x8D4, &sFile.column[1]);

	for (i = 0; i < D1GARAGE_BANDS; i++)
		ok = ok && ReadSpan(bin, bandOfs[i][0], bandOfs[i][1], &sFile.band[i]);

	for (i = 0; i < D1GARAGE_CARS; i++)
	{
		int ofs = 0x115C + i * 0x18;

		sFile.carX[i] = Immediate(bin, ofs, OP_ADDIU);
		sFile.carZ[i] = Immediate(bin, ofs + 8, OP_ADDIU);
		sFile.carDir[i] = Immediate(bin, ofs + 12, OP_ADDIU);
		ok = ok && sFile.carX[i] != 0x7fffffff && sFile.carZ[i] != 0x7fffffff && sFile.carDir[i] != 0x7fffffff;
	}

	memcpy(&sFile.timeFrames, bin + 0x3450, sizeof(int));
	ok = ok && sFile.timeFrames > 0 && sFile.timeFrames < 600;
	sFile.timeFrames *= 60;

	memcpy(sFile.sequence, bin + 0x33F4, sizeof(sFile.sequence));
	for (i = 0; i < D1GARAGE_SEQUENCES; i++)
	{
		ok = ok && sFile.sequence[i][0] > 0 && sFile.sequence[i][0] <= D1GARAGE_HISTORY;

		for (k = 0; k < D1GARAGE_HISTORY; k++)
			ok = ok && sFile.sequence[i][2 + k] <= 10;
	}

	for (i = 0; i < TEXT_COUNT; i++)
	{
		strncpy(sFile.text[i], (char*)bin + kTextOffsets[i], D1GARAGE_TEXT_SIZE - 1);
		sFile.text[i][D1GARAGE_TEXT_SIZE - 1] = 0;
		ok = ok && strlen(sFile.text[i]) > 4;
	}

	free(bin);
	return ok;
}

void D1Garage_Reset(void)
{
	int i;

	memset(&sTest, 0, sizeof(sTest));
	sTest.hitPause = D1GARAGE_FIRST_PAUSE;

	for (i = 0; i < D1GARAGE_CARS; i++)
		sTest.carSlot[i] = -1;

	for (i = 0; i < D1GARAGE_HISTORY; i++)
		sTest.history[i] = 0xff;
}

void D1Garage_LoadSpeech(void)
{
	sSpeechLoaded = LoadBankFromBlock("..\\DRIVER1\\SOUND\\VOICES.BLK", SOUND_BANK_MISSION, D1GARAGE_SPEECH_LUMP);
}

// zone 1..5 (column 0, far to near) or 6..10 (column 1, near to far), 0 outside
static int ZoneAt(int x, int z)
{
	int column, band;

	for (column = 0; column < 2; column++)
	{
		if ((u_int)(x - sFile.column[column].start) >= (u_int)sFile.column[column].size)
			continue;

		for (band = 0; band < D1GARAGE_BANDS; band++)
		{
			if ((u_int)(z - sFile.band[band].start) < (u_int)sFile.band[band].size)
				return column == 0 ? D1GARAGE_BANDS - band : D1GARAGE_BANDS + 1 + band;
		}
	}

	return 0;
}

static int SequenceMatches(const u_char* sequence)
{
	int length = sequence[0], forward = 1, backward = 1, i;

	for (i = 0; i < length; i++)
	{
		forward = forward && sequence[2 + i] == sTest.history[i];
		backward = backward && sequence[2 + i] == sTest.history[length - 1 - i];
	}

	return forward || backward;
}

// every zone change goes into the history; a slalom or lap is done when the latest changes match one of the sequences
static void TrackZones(CAR_DATA* cp)
{
	int zone = ZoneAt(cp->hd.where.t[0], cp->hd.where.t[2]);
	int i;

	if (zone == sTest.history[0])
		return;

	memmove(sTest.history + 1, sTest.history, (D1GARAGE_HISTORY - 1) * sizeof(int));
	sTest.history[0] = zone;

	for (i = 0; i < D1GARAGE_SEQUENCES; i++)
	{
		if (SequenceMatches(sFile.sequence[i]))
			sTest.done[i < D1GARAGE_SLALOMS ? TASK_SLALOM : TASK_LAP] = 1;
	}
}

static void SpawnParkedCars(void)
{
	LONGVECTOR4 pos;
	int i;

	for (i = 0; i < D1GARAGE_CARS; i++)
	{
		if (sTest.carSlot[i] >= 0)
			continue;

		pos[0] = sFile.carX[i];
		pos[1] = 10000;
		pos[2] = sFile.carZ[i];
		pos[3] = 0;

		sTest.carSlot[i] = CreateStationaryCivCar(sFile.carDir[i], 0, 0, &pos, 1 + (i & 1), i & 3, 0);

		if (sTest.carSlot[i] >= 0)
			sTest.carDamage[i] = car_data[sTest.carSlot[i]].totalDamage;
	}
}

// a parked car that takes damage was hit; new damage of the player car alone is a scratch on the paintwork
static int CheckHits(CAR_DATA* cp)
{
	int hitCar = 0, i, event = 0;

	for (i = 0; i < D1GARAGE_CARS; i++)
	{
		CAR_DATA* parked;

		if (sTest.carSlot[i] < 0)
			continue;

		parked = &car_data[sTest.carSlot[i]];
		if (parked->controlType != CONTROL_TYPE_CIV_AI)
			continue;

		hitCar |= parked->totalDamage > sTest.carDamage[i];
		sTest.carDamage[i] = parked->totalDamage;
	}

	if (sTest.hitPause > 0)
		sTest.hitPause--;
	else if (hitCar || cp->totalDamage > sTest.playerDamage)
	{
		SetMissionMessage(sFile.text[hitCar ? TEXT_HIT_CAR : TEXT_PAINTWORK], 3, 1);
		sTest.hitPause = D1GARAGE_HIT_PAUSE;
		sTest.hits++;
		event = 1;
	}

	sTest.playerDamage = cp->totalDamage;
	return event;
}

// forward speed of the body in wheel speed units: the wheel speed itself jumps to a fixed value while the rear wheels are off the ground
static int ForwardSpeed(CAR_DATA* cp)
{
	int dir = cp->hd.direction;

	return (RCOS(dir) / 64 * (cp->st.n.linearVelocity[2] / 64) + RSIN(dir) / 64 * (cp->st.n.linearVelocity[0] / 64)) >> 12;
}

static void CheckManoeuvres(CAR_DATA* cp, int hit)
{
	int speed = ForwardSpeed(cp);
	int absSpeed = ABS(speed);
	int yaw = ABS(cp->st.n.angularVelocity[1] >> 12);
	int pad = cp->lastPad;

	if ((pad & CAR_PAD_WHEELSPIN) && absSpeed >= 51)
		sTest.done[TASK_BURNOUT] = 1;

	if ((pad & CAR_PAD_HANDBRAKE) && absSpeed >= 51)
		sTest.done[TASK_HANDBRAKE] = 1;

	// spins: the yaw rate has to stay up for a while
	sTest.spin180 = yaw > 250 ? sTest.spin180 + 1 : 0;
	sTest.spin360 = yaw > 300 ? sTest.spin360 + 1 : 0;

	if (sTest.spin180 > 20)
		sTest.done[TASK_180] = 1;

	if (sTest.spin360 > 35)
		sTest.done[TASK_360] = 1;

	// reverse 180: the spin starts while reversing fast
	if (speed < 0 && absSpeed >= 101)
		sTest.reverseArmed = 1;

	if (yaw > 200 && sTest.reverseArmed)
	{
		if (++sTest.reverseFrames > 20)
			sTest.done[TASK_REVERSE_180] = 1;
	}
	else
	{
		sTest.reverseArmed = 0;
		sTest.reverseFrames = 0;
	}

	// speed: a few frames at top speed, then slow again
	if (speed > 180)
		sTest.fastFrames++;

	if (hit || speed < 0)
		sTest.fastFrames = 0;

	if (sTest.fastFrames >= 6 && speed <= 19)
	{
		sTest.done[TASK_SPEED] = 1;
		sTest.fastFrames = 0;
	}

	// brake: from high speed, the brake held until the car has slowed down
	if (speed > 170)
	{
		sTest.braking = 1;
		sTest.brakeFrames = 0;
	}
	else if (!(pad & CAR_PAD_BRAKE) || hit)
		sTest.braking = 0;

	if (sTest.braking && ++sTest.brakeFrames > 20)
		sTest.done[TASK_BRAKE] = 1;
}

static void ShowClock(int frames)
{
	MR_TIMER* timer = &Mission.timer[0];

	timer->flags = TIMER_FLAG_ACTIVE | TIMER_FLAG_PAUSED;		// the test runs the clock, not HandleTimer
	timer->count = MAX(frames, 0) * 100;
	timer->min = timer->count / 180000;
	timer->sec = timer->count / 3000 - timer->min * 60;
	timer->frac = timer->count % 3000 / 30;
}

static void EndTest(int text, int speech, int passed)
{
	sTest.over = 1;
	sTest.passed = passed;
	SetMissionMessage(sFile.text[text], 3, 3);

	if (sSpeechLoaded)
		PlaySpeech(&gSpeechQueue, 100 + speech);
}

// like the original the mission only ends once the closing line has been spoken
static void FinishAfterSpeech(void)
{
	if (sTest.over != 1 || gSpeechQueue.count > 0 || gSpeechQueue.is_playing)
		return;

	sTest.over = 2;

	if (sTest.passed)
		SetMissionOver(PAUSEMODE_COMPLETE);
	else
		SetMissionFailed(FAILED_MESSAGESET);
}

void D1Garage_Step(CAR_DATA* cp)
{
	int left, hit, i;

	if (sFile.state == 0)
		sFile.state = ReadFile() ? 1 : -1;

	if (sTest.over)
		FinishAfterSpeech();

	if (sFile.state < 0 || sTest.over || cp == NULL)
		return;

	if (sTest.frame == 0)
		SetMissionMessage(sFile.text[TEXT_START], 3, 2);

	SpawnParkedCars();

	left = sFile.timeFrames - MAX(sTest.frame - D1GARAGE_START_PAUSE, 0);
	ShowClock(left);

	if (sTest.frame++ < D1GARAGE_START_PAUSE)
		return;

	TrackZones(cp);
	hit = CheckHits(cp);
	CheckManoeuvres(cp, hit);

	for (i = 0; i < D1GARAGE_TASKS && sTest.done[i]; i++)
		;

	if (i == D1GARAGE_TASKS)
		EndTest(TEXT_PASSED, SPEECH_PASSED, 1);
	else if (sTest.hits >= D1GARAGE_WRECKED)
		EndTest(TEXT_WRECKED, SPEECH_WRECKED, 0);
	else if (left <= 0 && ABS(ForwardSpeed(cp)) < 6)
		EndTest(TEXT_OUT_OF_TIME, SPEECH_OUT_OF_TIME, 0);
}

void D1Garage_Draw(void)
{
	int i;

	if (sFile.state <= 0 || sTest.frame == 0)
		return;

	for (i = 0; i < D1GARAGE_TASKS; i++)
	{
		if (sTest.done[i])
			SetTextColour(64, 128, 64);
		else
			SetTextColour(40, 40, 40);

		PrintStringRightAligned(sTaskNames[i], gOverlayXOppPos + D1GARAGE_LIST_RIGHT, 40 + i * 12);
	}
}
