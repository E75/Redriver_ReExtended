#include "driver2.h"
#include "chopper.h"
#include "cars.h"
#include "players.h"
#include "mission.h"
#include "spool.h"
#include "texture.h"
#include "civ_ai.h"
#include "gamesnd.h"
#include "sound.h"
#include "main.h"
#include "camera.h"
#include "debris.h"
#include "dr2roads.h"
#include "models.h"
#include "system.h"
#include "map.h"
#include "jericho.h"

#ifndef PSX

#define CHOPPER_SPECIAL_MODEL	13		// special car slot model of the chopper: special model 6 of Rio's spool + 7
#define CHOPPER_SOUND_BANK		63		// rotor sound, the bank of the unused Lenny mission
#define CHOPPER_RIO				3		// the only city whose files hold the chopper model and textures

#define CHOPPER_ROTOR_SPEED		64		// rotor turn per step, 4096 = one turn
#define CHOPPER_PITCH_MAX		230		// nose down at full speed, ~20 degrees
#define CHOPPER_ROLL_MAX		200
#define CHOPPER_ROLL_DIV		2		// wheel angle to bank angle

extern char* LevelFiles[];
extern int gFlyingCar;
extern int allowSpecSpooling;
extern int InitChopperModel(void);
extern void SpinChopperRotor(int speed);
extern void SetChopperEvent(VECTOR* position, int rotation, int pitch, int roll);
extern void SetChopperEventModel(MODEL* model, TEXTURE_DETAILS* rotor, int rotorShade, int src1, int src2, int dest1, int dest2);
extern VECTOR* ChopperEventPosition(void);

#define CHOPPER_PARK_RANGE		5800	// the parked chopper shows up within this distance of its spot
#define CHOPPER_PARK_OFFSET		1500	// sideways from the spot

// where the chopper stands in Rio Take A Ride (the spots of the original helicopter): x, height, z, heading
static const int chopperSpots[6][4] = {
	{ 69696, 799, -518581, 3072 },		// Copacabana fort
	{ 179191, 48, -206410, 3584 },		// Urca
	{ -239748, 25, -366923, 0 },		// race track
	{ 215734, 1056, 134068, 3072 },		// Flamengo
	{ -195548, 31, 342010, 1536 },		// Praca da Bandeira
	{ -138208, 26, 65923, 0 },			// Cosme Velho
};

int gChopper = 0;
int gChopperTest = 0;	// -choppertest: take off at step 20 of a -shot run

static int chopperChan = -1;
static int chopperPitch = 0;
static int chopperRoll = 0;
static int chopperStartFrame = 0;
static int parkedSpot = -1;		// spot the idle chopper stands at, -1 = none in range
static CAR_DATA* pilotedCar = NULL;	// the car the chopper replaced while flown
static CAR_DATA* landedCar = NULL;	// the pilot got out: the chopper stays on this car until the player walks away

#define CHOPPER_LEAVE_HEIGHT	250		// the pilot may get out below this height above the ground
#define CHOPPER_LEAVE_DISTANCE	2500	// a few steps from the chopper it turns back into the car

static CAR_DATA* PilotCar(void)
{
	CAR_DATA* cp;

	if (player[0].playerType != 1 || player[0].playerCarId < 0)
		return NULL;

	cp = &car_data[(int)player[0].playerCarId];

	return cp->controlType == CONTROL_TYPE_PLAYER ? cp : NULL;
}

static int pagesLoaded = 0;		// the chopper texture pages are in the special slot

// level loaded: Rio Take A Ride starts with the chopper pages (SetupResidentModels), everything else without
void ChopperLevelLoaded(void)
{
	pagesLoaded = (GameLevel == CHOPPER_RIO && residentCarModels[SPECIAL_CAR_SLOT] == CHOPPER_SPECIAL_MODEL);
	parkedSpot = -1;
	chopperChan = -1;
	gChopper = 0;
	landedCar = NULL;
	pilotedCar = NULL;
}

// lump types of a level file (LevLumpType in main.c)
#define LUMP_MODELS			1
#define LUMP_MODELNAMES		12
#define LUMP_TEXTUREINFO	34

#define CHOPPER_PAGE1		126		// texture page numbers of the imported chopper: above the pages of any city
#define CHOPPER_PAGE2		127

static char* chopperModelMem = NULL;

#define IMPORT_FAIL() do { printWarning("Chopper import failed at line %d of chopper.c\n", __LINE__); goto done; } while (0)

char* ReadFilePart(FILE* f, int offset, int size)	// also used by allcars.c
{
	char* buffer = (char*)malloc(size);

	fseek(f, offset, SEEK_SET);

	if (buffer && fread(buffer, 1, size, f) != (size_t)size)
	{
		free(buffer);
		return NULL;
	}

	return buffer;
}

// the first lump of this type in a level data area (lumps start 8 bytes in, each is type, size, data)
char* FindLevelLump(char* area, int areaSize, int type)
{
	char* p = area + 8;

	while (p + 8 <= area + areaSize)
	{
		int lumpType = *(int*)p;
		int lumpSize = *(int*)(p + 4);

		if (lumpType == 0xff)
			break;

		if (lumpType == type)
			return p + 8;

		p += 8 + ((lumpSize + 3) & ~3);
	}

	return NULL;
}

// Only RIO.LEV holds the chopper model and its two texture pages (the special pair 6 of Rio): copies the model,
// loads the pages into the special slot of the current city as pages 126 and 127 and points the chopper event at them
static int ImportChopperFromRio(void)
{
	char path[64];
	FILE* f;
	XYPAIR lumps[4];
	char* area[2] = { NULL, NULL };
	int areaSize[2];
	char* pages = NULL;
	char* names;
	char* models;
	char* texInfo;
	char* ptr;
	char* src;
	MODEL* model;
	XYPAIR* permList;
	XYPAIR* specList;
	int pageAmount, nPerm, nSpec, modelCount;
	int i, j, size, sector, permSectors, specSectors, src1, src2;
	int ok = 0;

	sprintf(path, "%s%s", gDataFolder, LevelFiles[CHOPPER_RIO]);
	FS_FixPathSlashes(path);

	f = fopen(path, "rb");

	if (!f)
		return 0;

	fseek(f, 8, SEEK_SET);

	if (fread(lumps, sizeof(lumps), 1, f) != 1)
		IMPORT_FAIL();

	areaSize[0] = lumps[CITYLUMP_DATA1].y;
	areaSize[1] = lumps[CITYLUMP_DATA2].y;
	area[0] = ReadFilePart(f, lumps[CITYLUMP_DATA1].x, areaSize[0]);
	area[1] = ReadFilePart(f, lumps[CITYLUMP_DATA2].x, areaSize[1]);

	if (!area[0] || !area[1])
		IMPORT_FAIL();

	names = FindLevelLump(area[0], areaSize[0], LUMP_MODELNAMES);
	models = FindLevelLump(area[0], areaSize[0], LUMP_MODELS);
	texInfo = FindLevelLump(area[0], areaSize[0], LUMP_TEXTUREINFO);

	// each lump is in one of the two data areas
	if (!names) names = FindLevelLump(area[1], areaSize[1], LUMP_MODELNAMES);
	if (!models) models = FindLevelLump(area[1], areaSize[1], LUMP_MODELS);
	if (!texInfo) texInfo = FindLevelLump(area[1], areaSize[1], LUMP_TEXTUREINFO);

	if (!names || !models || !texInfo)
		IMPORT_FAIL();

	// the model: the n-th name is the n-th model of the pack
	modelCount = *(int*)models;

	for (i = 0; i < modelCount && strcmp(names, "CHOPPERCLEAN") != 0; i++)
		names += strlen(names) + 1;

	if (i == modelCount)
		IMPORT_FAIL();

	src = models + 4;

	for (j = 0; j < i; j++)
		src += 4 + *(int*)src;

	size = *(int*)src;
	free(chopperModelMem);
	chopperModelMem = (char*)calloc(size + 256, 1);	// the poly conversion of the first draw may write a few bytes past the last polygon
	memcpy(chopperModelMem, src + 4, size);

	// pointers inside a model are offsets, like ProcessMDSLump makes them real
	model = (MODEL*)chopperModelMem;

	if (model->instance_number != -1)
		IMPORT_FAIL();

	model->tri_verts = 0;
#if MODEL_RELOCATE_POINTERS
	model->poly_block += (int)(char*)model;
	model->vertices += (int)(char*)model;
	model->normals += (int)(char*)model;
	model->point_normals += (int)(char*)model;

	if (model->collision_block != 0)
		model->collision_block += (int)(char*)model;
#endif

	// the texture lump (see ProcessTextureInfo): pages with their textures, then the permanent and special page lists
	pageAmount = *(int*)texInfo;

	if (pageAmount > 120)
		IMPORT_FAIL();

	ptr = texInfo + 8 + (pageAmount + 1) * sizeof(TP);

	// skip the texture lists of the pages
	for (i = 0; i < pageAmount; i++)
		ptr += 4 + *(int*)ptr * sizeof(TEXINF);

	nPerm = *(int*)ptr;
	permList = (XYPAIR*)(ptr + 4);
	ptr = (char*)&permList[16];
	nSpec = *(int*)ptr;
	specList = (XYPAIR*)(ptr + 4);

	src1 = specTpages[CHOPPER_RIO][(CHOPPER_SPECIAL_MODEL - 8) * 2];
	src2 = specTpages[CHOPPER_RIO][(CHOPPER_SPECIAL_MODEL - 8) * 2 + 1];

	// the page data follows the first data area: permanent pages, then the special pages
	permSectors = 0;

	for (i = 0; i < nPerm; i++)
		permSectors += (permList[i].y + 2047) / CDSECTOR_SIZE;

	specSectors = 0;

	for (i = 0; i < nSpec; i++)
		specSectors += (specList[i].y + 2047) / CDSECTOR_SIZE;

	sector = (lumps[CITYLUMP_DATA1].x + lumps[CITYLUMP_DATA1].y) / CDSECTOR_SIZE + permSectors;
	pages = ReadFilePart(f, sector * CDSECTOR_SIZE, specSectors * CDSECTOR_SIZE);

	if (!pages)
		IMPORT_FAIL();

	// a Driver 1 city has no special pages: its two spare slots after the permanent ones are taken from the area textures
	if (specialSlot + 2 > slotsused)
	{
		for (i = specialSlot; i < specialSlot + 2; i++)
		{
			if (tpageslots[i] != 0xFF)
				tpageloaded[tpageslots[i]] = 0;
		}

		slotsused = specialSlot + 2;
		LoadedArea = -1;	// the area loads its textures again into the slots that are left
	}

	if (!LoadSpecialSlotPages(pages, specList, nSpec, src1, src2, CHOPPER_PAGE1, CHOPPER_PAGE2))
		IMPORT_FAIL();

	// the rotor texture of Rio sits on a permanent page of Rio: another city shows a soft dark disc instead
	SetChopperEventModel(model, &light_texture, 127, src1, src2, CHOPPER_PAGE1, CHOPPER_PAGE2);
	ok = 1;

done:
	free(area[0]);
	free(area[1]);
	free(pages);
	fclose(f);
	return ok;
}

// loads the chopper textures and models into the special car slot; false when it is not possible
static int PrepareChopper(void)
{
	CAR_DATA* cp;

	if (!pagesLoaded)
	{
		allowSpecSpooling = 0;
		AbortSpecialSpool();
		PingOutAllSpecialCivCars();

		// a car that still drives with a special model would lose it
		for (cp = car_data; cp < &car_data[MAX_CARS]; cp++)
		{
			if (cp->controlType != CONTROL_TYPE_NONE && residentCarModels[cp->ap.model] > 5)
			{
				printWarning("Chopper: the car in the special slot has to go first, pick a normal car\n");
				return 0;
			}
		}

		// the chopper pages are not in the special spool (Rio has 5 specials there), only in the level file of Rio
		if (GameLevel == CHOPPER_RIO ? !LoadSpecialSlotFromLevel(CHOPPER_SPECIAL_MODEL) : !ImportChopperFromRio())
		{
			printWarning("Chopper: could not load its model and texture pages\n");
			return 0;
		}

		// Driver 1 cities keep their car models, their special slot is an ordinary car
		if (!IS_DRIVER1_CITY(GameLevel))
		{
			residentCarModels[SPECIAL_CAR_SLOT] = CHOPPER_SPECIAL_MODEL;
			gCarCleanModelPtr[SPECIAL_CAR_SLOT] = NULL;
			gCarDamModelPtr[SPECIAL_CAR_SLOT] = NULL;
			gCarLowModelPtr[SPECIAL_CAR_SLOT] = NULL;
		}

		pagesLoaded = 1;
		JerichoPageLost();
	}

	if (!InitChopperModel())
	{
		printWarning("Chopper: this city has no chopper model\n");
		return 0;
	}

	LoadBankFromLump(SOUND_BANK_MISSION, CHOPPER_SOUND_BANK);
	return 1;
}

static void StopChopperSound(void)
{
	if (chopperChan >= 0)
	{
		StopChannel(chopperChan);
		UnlockChannel(chopperChan);
		chopperChan = -1;
	}
}

static void ChopperLand(void)
{
	StopChopperSound();
	gChopper = 0;
	gFlyingCar = 0;
	landedCar = NULL;
}

void ChopperToggle(int direction)
{
	CAR_DATA* cp;

	if (gChopper || landedCar)
	{
		ChopperLand();
		return;
	}

	cp = PilotCar();

	if (cp == NULL || NumPlayers != 1)
		return;

	if (!PrepareChopper())
		return;

	chopperPitch = 0;
	chopperRoll = 0;
	chopperStartFrame = FrameCnt;

	chopperChan = Start3DTrackingSound(-1, SOUND_BANK_MISSION, 0, (VECTOR*)cp->hd.where.t, (LONGVECTOR3*)cp->st.n.linearVelocity);
	LockChannel(chopperChan);

	gFlyingCar = 1;
	gChopper = 1;
	pilotedCar = cp;
}

// stands the chopper at the Rio spot near the player, nowhere when none is in range
static void ParkChopper(void)
{
	VECTOR pos;
	int i, dx, dz;

	parkedSpot = -1;

	if (GameLevel != CHOPPER_RIO || !pagesLoaded ||
		ChopperEventPosition() == NULL || player[0].spoolXZ == NULL)
		return;

	for (i = 0; i < 6; i++)
	{
		dx = player[0].spoolXZ->vx - chopperSpots[i][0];
		dz = player[0].spoolXZ->vz - chopperSpots[i][2];

		if (ABS(dx) < CHOPPER_PARK_RANGE && ABS(dz) < CHOPPER_PARK_RANGE && dx * dx + dz * dz < CHOPPER_PARK_RANGE * CHOPPER_PARK_RANGE)
		{
			parkedSpot = i;
			break;
		}
	}

	if (parkedSpot < 0)
		return;

	// beside the spot: the Take A Ride start of Rio lies on the first one
	pos.vx = chopperSpots[parkedSpot][0] + (RSIN(chopperSpots[parkedSpot][3] + 1024) * CHOPPER_PARK_OFFSET >> 12);
	pos.vy = 25 - chopperSpots[parkedSpot][1];	// the spot height is a car's centre, the skids hang lower
	pos.vz = chopperSpots[parkedSpot][2] +(RCOS(chopperSpots[parkedSpot][3] + 1024) * CHOPPER_PARK_OFFSET >> 12);
	SetChopperEvent(&pos, chopperSpots[parkedSpot][3], 0, 0);
}

// the chopper event is drawn while it is flown or parked
int ChopperEventShown(void)
{
	return gChopper || landedCar != NULL || parkedSpot >= 0;
}

// round shadow on the ground under the chopper, after DrawEvents
void ChopperDrawShadow(void)
{
	VECTOR* ep;
	VECTOR pos, ground;
	CVECTOR col = { 40, 40, 40, 0 };

	if (!ChopperEventShown() || (ep = ChopperEventPosition()) == NULL)
		return;

	ground.vx = ep->vx;
	ground.vy = 0;
	ground.vz = ep->vz;

	pos.vx = ep->vx - camera_position.vx;
	pos.vy = (4 - MapHeight(&ground)) - camera_position.vy;
	pos.vz = ep->vz - camera_position.vz;

	RoundShadow(&pos, &col, 288);
}

// the pilot is on foot: the chopper follows the empty car down and goes back to a car once the player is a few steps away
static void StepLandedChopper(void)
{
	VECTOR pos;
	int dx, dz;

	dx = player[0].spoolXZ ? player[0].spoolXZ->vx - landedCar->hd.where.t[0] : 0;
	dz = player[0].spoolXZ ? player[0].spoolXZ->vz - landedCar->hd.where.t[2] : 0;

	// back in a car, the car is gone, a restart, or far enough away
	if (PilotCar() != NULL || landedCar->controlType == CONTROL_TYPE_NONE || FrameCnt < chopperStartFrame ||
		ABS(dx) > CHOPPER_LEAVE_DISTANCE || ABS(dz) > CHOPPER_LEAVE_DISTANCE ||
		dx * dx + dz * dz > CHOPPER_LEAVE_DISTANCE * CHOPPER_LEAVE_DISTANCE)
	{
		landedCar = NULL;
		return;
	}

	pos.vx = landedCar->hd.where.t[0];
	pos.vy = 25 - landedCar->hd.where.t[1];	// the skids hang lower than the car centre, like the parked chopper
	pos.vz = landedCar->hd.where.t[2];
	SetChopperEvent(&pos, landedCar->hd.direction, 0, 0);
	SpinChopperRotor(CHOPPER_ROTOR_SPEED / 4);
}

void ChopperStep(void)
{
	CAR_DATA* cp;
	int dir, speed, target;
	VECTOR pos;

	if (gChopperTest && FrameCnt == 20)
		ChopperToggle(0);

	// -chopperexit: the pilot gets out at step 60 and walks 3000 units away at step 150 (logs the state every 30 steps)
	if (gChopperTest == 2)
	{
		if (FrameCnt == 60 && gChopper)
		{
			extern int ActivatePlayerPedestrian(CAR_DATA* pCar, char* padId, int direction, LONGVECTOR4* position, PED_MODEL_TYPES playerType);
			ActivatePlayerPedestrian(PilotCar(), NULL, 0, NULL, TANNER_MODEL);	// what the leave car button does (handling.c)
		}

		if (FrameCnt == 150 && player[0].pPed != NULL)
			player[0].pPed->position.vx += 3000;

		if (FrameCnt % 30 == 0 && FrameCnt >= 30)
			printInfo("choppertest: step %d flying %d landed %d shown %d car hidden %d height %d\n", FrameCnt, gChopper, landedCar != NULL, ChopperEventShown(), landedCar ? ChopperHidesCar(landedCar) : 0, ChopperHeight());
	}

	if (landedCar)
	{
		StepLandedChopper();
		return;
	}

	if (!gChopper)
	{
		ParkChopper();
		SpinChopperRotor(CHOPPER_ROTOR_SPEED / 4);
		return;
	}

	parkedSpot = -1;

	cp = PilotCar();

	// the player got out: the chopper stays where the car is (a restart ends the flight at once)
	// (the car turns civilian the moment the get out starts, the player is a pedestrian 15 steps later)
	if (cp == NULL && FrameCnt >= chopperStartFrame && pilotedCar != NULL && pilotedCar->controlType != CONTROL_TYPE_NONE)
	{
		landedCar = pilotedCar;
		StopChopperSound();
		gChopper = 0;
		gFlyingCar = 0;
		return;
	}

	if (cp == NULL || FrameCnt < chopperStartFrame)
	{
		ChopperLand();
		return;
	}

	// nose down with the speed along the heading, bank into the turn
	dir = cp->hd.direction;
	speed = ((cp->st.n.linearVelocity[0] >> 4) * RSIN(dir) + (cp->st.n.linearVelocity[2] >> 4) * RCOS(dir)) >> 12;

	target = MAX(-CHOPPER_PITCH_MAX, MIN(-speed * CHOPPER_PITCH_MAX / 375, CHOPPER_PITCH_MAX));
	chopperPitch += (target - chopperPitch) / 6;

	target = MAX(-CHOPPER_ROLL_MAX, MIN(cp->wheel_angle / CHOPPER_ROLL_DIV, CHOPPER_ROLL_MAX));
	chopperRoll += (target - chopperRoll) / 6;

	SpinChopperRotor(CHOPPER_ROTOR_SPEED);

	pos.vx = cp->hd.where.t[0];
	pos.vy = -cp->hd.where.t[1];
	pos.vz = cp->hd.where.t[2];
	SetChopperEvent(&pos, dir, chopperPitch, chopperRoll);

	// no engine sound of the hidden car (both engine channels are muted while it is sounding = 2)
	player[0].car_is_sounding = 2;

	if (chopperChan >= 0)
		SetChannelPitch(chopperChan, 4096 + (cp->st.n.linearVelocity[1] >> 8));
}

int ChopperHeight(void)
{
	CAR_DATA* cp;
	VECTOR ground;

	cp = PilotCar();

	if (!gChopper || cp == NULL)
		return 0;

	// same coordinates as the shadow in ChopperDrawShadow: height of the chopper above the ground below it
	ground.vx = cp->hd.where.t[0];
	ground.vy = 0;
	ground.vz = cp->hd.where.t[2];

	return (4 - MapHeight(&ground)) + cp->hd.where.t[1];
}

int ChopperCanLeave(void)
{
	return ChopperHeight() < CHOPPER_LEAVE_HEIGHT;
}

// Jericho took the special slot: the chopper pages have to be loaded again
void ChopperPagesLost(void)
{
	pagesLoaded = 0;
}

int ChopperHidesCar(CAR_DATA* cp)
{
	return (gChopper && cp == PilotCar()) || (landedCar != NULL && cp == landedCar);
}

#else

int gChopper = 0;
void ChopperToggle(int direction) {}
void ChopperStep(void) {}
void ChopperDrawShadow(void) {}
int ChopperHidesCar(CAR_DATA* cp) { return 0; }
void ChopperPagesLost(void) {}
int ChopperHeight(void) { return 0; }
int ChopperCanLeave(void) { return 1; }

#endif
