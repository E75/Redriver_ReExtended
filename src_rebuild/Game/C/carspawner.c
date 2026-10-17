#include "driver2.h"
#include "carspawner.h"
#include "allcars.h"
#include "chopper.h"
#include "civ_ai.h"
#include "cars.h"
#include "cosmetic.h"
#include "denting.h"
#include "handling.h"
#include "models.h"
#include "mission.h"
#include "players.h"
#include "spool.h"
#include "texture.h"
#include "system.h"
#include "dr2roads.h"
#include "pad.h"
#include "main.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PSX

// Debug menu "Car Spawner": see carspawner.h. The player's car is re-initialised in place with another model, so the showroom
// never needs a second car slot. A model that is not resident goes into the special car slot (the memory and texture slot
// of the special cars); every other car that still uses that slot is pinged out first.

extern char* CosmeticFiles[];
extern char* DentingFiles[];
extern char PingOutCivsOnly;
extern int gCantDrive;
extern int gCamAngleOverride;
extern int allowSpecSpooling;
extern char* LevelNames[];
extern int GetNodePos(DRIVER2_STRAIGHT* straight, DRIVER2_JUNCTION* junction, DRIVER2_CURVE* curve, int distAlongPath, CAR_DATA* cp, int* x, int* z, int laneNo);

#define MAX_SPAWN_CARS	24
#define SPOT_STEP		1500
#define SPOT_RINGS		9

typedef struct
{
	short city;		// the Driver 2 city a special car belongs to (the loaded city for every other car)
	short model;
	char name[32];
} SPAWN_CAR;

int gCarSpawner = 0;

static SPAWN_CAR spawnList[MAX_SPAWN_CARS];
static int spawnCount = 0;
static int spawnIndex = 0;
static int spawnFrame = 0;
static int spawnPending = -1;		// start the showroom at this car at the next step, -1 = none
static int spawnAuto = 0;			// test: 1 presses right, 2 presses start, 40 steps into the showroom
static int spawnX, spawnZ, spawnDir;
static char spawnText[64];

static void AddCar(int city, int model, const char* name)
{
	if (spawnCount >= MAX_SPAWN_CARS)
		return;

	spawnList[spawnCount].city = city;
	spawnList[spawnCount].model = model;
	strncpy(spawnList[spawnCount].name, name, sizeof(spawnList[spawnCount].name) - 1);
	spawnList[spawnCount].name[sizeof(spawnList[spawnCount].name) - 1] = 0;
	spawnCount++;
}

static void BuildList(void)
{
	char name[32];
	int city, i;

	spawnCount = 0;

	if (IS_DRIVER1_CITY(GameLevel))
	{
		AddCar(GameLevel, 0, "Cop car");
		AddCar(GameLevel, 1, "Car 1");
		AddCar(GameLevel, 2, "Car 2");

		for (i = 0; i < D1Dlc_PlayerCars(GameLevel); i++)
		{
			sprintf(name, "Player car %d", i + 1);
			AddCar(GameLevel, FIRST_D1_PLAYER_CAR + i, name);
		}

		return;
	}

	AddCar(GameLevel, 0, "Cop car");

	for (i = 1; i <= 4; i++)
	{
		sprintf(name, "Car %d", i);
		AddCar(GameLevel, i, name);
	}

	// the four specials of every Driver 2 city, the cars of the other cities come from their level file
	for (city = 0; city < FIRST_DRIVER1_CITY; city++)
	{
		for (i = 1; i <= 4; i++)
		{
			sprintf(name, "%s special %d", LevelNames[city], i);
			AddCar(city, 7 + i, name);
		}
	}

	AddCar(GameLevel, 12, "Secret car");
}

// the spot: the road next to the player, a lane at the kerb of a straight with room to spare
static int FindRoadSpot(int fromX, int fromZ)
{
	DRIVER2_ROAD_INFO roadInfo;
	VECTOR pos;
	int ring, a, steps, lane, dist, dx, dz, theta, x, z;

	for (ring = 0; ring < SPOT_RINGS; ring++)
	{
		steps = ring == 0 ? 1 : 12;

		for (a = 0; a < steps; a++)
		{
			int seg;

			pos.vx = fromX + (RSIN(a * 4096 / steps) * ring * SPOT_STEP >> 12);
			pos.vz = fromZ + (RCOS(a * 4096 / steps) * ring * SPOT_STEP >> 12);
			pos.vy = 0;

			seg = RoadInCell(&pos);

			if (!IS_STRAIGHT_SURFACE(seg) || !GetSurfaceRoadInfo(&roadInfo, seg) || ROAD_LANES_COUNT(&roadInfo) == 0)
				continue;

			if (roadInfo.straight->length < 4000)
				continue;

			dx = pos.vx - roadInfo.straight->Midx;
			dz = pos.vz - roadInfo.straight->Midz;
			theta = roadInfo.straight->angle - ratan2(dx, dz);
			dist = (roadInfo.straight->length / 2) + FIXEDH(RCOS(theta) * SquareRoot0(dx * dx + dz * dz));
			dist = MAX(1500, MIN(dist, roadInfo.straight->length - 1500));

			lane = 0;
			GetNodePos(roadInfo.straight, NULL, NULL, dist, NULL, &x, &z, lane);

			spawnX = x;
			spawnZ = z;
			spawnDir = (ROAD_LANE_DIR(roadInfo.straight, lane) == 0 ? roadInfo.straight->angle : roadInfo.straight->angle + 2048) & 0xfff;
			return 1;
		}
	}

	return 0;
}

// the cars (not the player's) that use the special slot go: its model is replaced
static void ClearSpecialSlotCars(void)
{
	CAR_DATA* cp;
	char oldMode = PingOutCivsOnly;

	PingOutCivsOnly = 0;

	for (cp = car_data; cp < &car_data[MAX_CARS]; cp++)
	{
		if (cp->controlType != CONTROL_TYPE_NONE && cp->ap.model == SPECIAL_CAR_SLOT && cp->id != player[0].playerCarId)
			PingOutCar(cp);
	}

	PingOutCivsOnly = oldMode;
}

// cosmetics and damage zones of a Driver 2 car that is no special: the files of the city
static int LoadD2SlotData(int model)
{
	char* buf = (char*)malloc(12727);
	int offset;

	if (!buf)
		return 0;

	LoadfileSeg(CosmeticFiles[GameLevel], buf, 0, 3120);
	offset = *(int*)(buf + model * sizeof(int));
	car_cosmetics[SPECIAL_CAR_SLOT] = *(CAR_COSMETICS*)(buf + offset);
	FixCarCos(&car_cosmetics[SPECIAL_CAR_SLOT], model);

	LoadfileSeg(DentingFiles[GameLevel], buf, 0, 12727);
	offset = *(int*)(buf + model * sizeof(int));
	SetupSpecDenting(buf + offset);

	free(buf);
	return 1;
}

// makes 'model' of the car list a resident car; returns 0 when it cannot be loaded
static int PrepareModel(const SPAWN_CAR* c)
{
	int i, model = c->model;

	if (model <= 4)
	{
		for (i = 0; i < SPECIAL_CAR_SLOT; i++)
		{
			if (residentCarModels[i] == model && gCarCleanModelPtr[i])
				return 1;
		}
	}
	else if (residentCarModels[SPECIAL_CAR_SLOT] == model && c->city == (ForeignCarMode() ? ForeignCarCity() : GameLevel) && gCarCleanModelPtr[SPECIAL_CAR_SLOT])
		return 1;

	ClearSpecialSlotCars();

	// the special spool must not fill the slot again behind the player's back
	allowSpecSpooling = 0;
	AbortSpecialSpool();
	ResumeSpecialSpool();

	if (residentCarModels[SPECIAL_CAR_SLOT] == 13)
		ChopperSlotTaken();

	if (IS_DRIVER1_CITY(GameLevel))
	{
		if (model >= 8 && !LoadSpecialSlotFromLevel(model))
			return 0;

		if (!LoadCarModelIntoSpecialSlot(model) || !D1PrepareSpecialSlotCosmetics(model))
			return 0;

		residentCarModels[SPECIAL_CAR_SLOT] = model;
		return 1;
	}

	if (model >= 8)
	{
		if (c->city != GameLevel)
		{
			if (!ForeignCarSelect(c->city, model - 7))
				return 0;
		}
		else
			ForeignCarClear();

		residentCarModels[SPECIAL_CAR_SLOT] = model;
		QuickSpoolSpecial();
		return gCarCleanModelPtr[SPECIAL_CAR_SLOT] != NULL;
	}

	ForeignCarClear();

	if (!LoadCarModelIntoSpecialSlot(model) || !LoadD2SlotData(model))
		return 0;

	residentCarModels[SPECIAL_CAR_SLOT] = model;
	return 1;
}

// the player's car becomes car spawnList[spawnIndex] on the spot
static int ShowCar(void)
{
	const SPAWN_CAR* c = &spawnList[spawnIndex];
	CAR_DATA* cp;
	LONGVECTOR4 pos;
	char padid;

	if (!PrepareModel(c))
	{
		printWarning("Car spawner: %s could not be loaded\n", c->name);
		return 0;
	}

	padid = player[0].padid;
	cp = &car_data[(int)player[0].playerCarId];

	pos[0] = spawnX;
	pos[1] = 0;
	pos[2] = spawnZ;
	pos[3] = 0;

	InitPlayer(&player[0], cp, CONTROL_TYPE_PLAYER, spawnDir, &pos, c->model, 0, &padid);
	return 1;
}

static void StartShowroom(int index)
{
	CAR_DATA* cp;

	if (player[0].playerType != PLAYER_TYPE_CAR || player[0].playerCarId < 0)
	{
		SetMissionMessage("Car spawner: get into a car first", 9, 3);
		return;
	}

	cp = &car_data[(int)player[0].playerCarId];

	BuildList();

	if (spawnCount == 0 || !FindRoadSpot(cp->hd.where.t[0], cp->hd.where.t[2]))
	{
		spawnX = cp->hd.where.t[0];
		spawnZ = cp->hd.where.t[2];
		spawnDir = cp->hd.direction;
	}

	// the cars on the spot go
	for (cp = car_data; cp < &car_data[MAX_CARS]; cp++)
	{
		int dx = cp->hd.where.t[0] - spawnX;
		int dz = cp->hd.where.t[2] - spawnZ;

		if (cp->controlType == CONTROL_TYPE_CIV_AI && ABS(dx) < 4000 && ABS(dz) < 4000)
			PingOutCar(cp);
	}

	spawnIndex = (index >= 0 && index < spawnCount) ? index : 0;
	spawnFrame = 0;

	if (ShowCar())
		gCarSpawner = 1;
}

void CarSpawnerStart(void)
{
	spawnPending = 0;
}

void CarSpawnerCommandLine(int index, int auto_mode)
{
	spawnPending = index;
	spawnAuto = auto_mode;
}

static void LeaveShowroom(void)
{
	gCarSpawner = 0;
	gCantDrive = 0;
	gCamAngleOverride = 0;
	Mission.message_timer[0] = 0;
}

int CarSpawnerUpdate(int pressed)
{
	if (spawnPending >= 0 && FrameCnt > 20 && gCarSpawner == 0)
	{
		int index = spawnPending;

		spawnPending = -1;
		StartShowroom(index);
		return 0;
	}

	if (gCarSpawner != 1)
		return 0;

	// the car is gone (a restart, a new level): nothing to show
	if (player[0].playerType != PLAYER_TYPE_CAR)
	{
		LeaveShowroom();
		return 0;
	}

	spawnFrame++;
	gCantDrive = 1;
	gCamAngleOverride = 2048 + ((spawnFrame * 6) & 2047);	// the camera circles the car

	sprintf(spawnText, "< %s %d/%d > ENTER", spawnList[spawnIndex].name, spawnIndex + 1, spawnCount);
	SetMissionMessage(spawnText, 9, 1);

	if (spawnAuto && spawnFrame == 40)
	{
		if (spawnAuto == 2)
			pressed |= MPAD_START;
		else
			Pads[0].dirnew |= MPAD_D_RIGHT;

		spawnAuto = 0;
	}

	if (spawnFrame < 10)
		return 0;

	if (Pads[0].dirnew & (MPAD_D_LEFT | MPAD_D_RIGHT))
	{
		int old = spawnIndex;
		int tries;

		// a car that cannot be loaded is skipped
		for (tries = 0; tries < spawnCount; tries++)
		{
			spawnIndex = (spawnIndex + ((Pads[0].dirnew & MPAD_D_RIGHT) ? 1 : spawnCount - 1)) % spawnCount;

			if (ShowCar())
				break;
		}

		if (tries == spawnCount)
			spawnIndex = old;

		Pads[0].dirnew &= ~(MPAD_D_LEFT | MPAD_D_RIGHT);
		return 1;
	}

	if (pressed & MPAD_START)
	{
		LeaveShowroom();
		return 1;
	}

	return 0;
}

#endif // !PSX
