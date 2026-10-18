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
#include "shadow.h"
#include "carexport.h"

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
extern int gCameraAngle;
extern int allowSpecSpooling;
extern char* LevelNames[];
extern int PolySizes[56];
extern char* ReadFilePart(FILE* f, int offset, int size);
extern char* FindLevelLump(char* area, int areaSize, int type);
extern int GetNodePos(DRIVER2_STRAIGHT* straight, DRIVER2_JUNCTION* junction, DRIVER2_CURVE* curve, int distAlongPath, CAR_DATA* cp, int* x, int* z, int laneNo);

#define MAX_SPAWN_CARS	48
#define LUMP_CAR_MODELS_TYPE	28
#define LUMP_TEXTUREINFO_TYPE	34
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
static int slotCity = -1;		// the car in the special slot: its city and model, unknown (-1) after a level load
static int slotModel = -1;
static int slotFrame = 0;
static char* importedLump = NULL;	// car models lump of another Driver 1 city: the loaded model points into it
static int spawnExitBlock = 0;	// the confirming Enter (R3) also leaves a car: ignored for a moment after the mode ends
static int spawnPlaced = 0;		// the player is on the spot
static int spawnPending = -1;		// start the showroom at this car at the next step, -1 = none
static int spawnAuto = 0;			// test: 1 presses right, 2 presses start, 40 steps into the showroom
static int spawnX, spawnZ, spawnDir;
static char spawnText[64];
static char exportDir[260];		// -carexport: the folder of the export, empty = off

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

		// the player cars of the other Driver 1 cities come from their level file
		for (city = FIRST_DRIVER1_CITY; city < NUM_CITIES; city++)
		{
			if (city == GameLevel || D1Dlc_PlayerCars(city) == 0 || !D1Dlc_CityAvailable(city))
				continue;

			for (i = 0; i < D1Dlc_PlayerCars(city); i++)
			{
				sprintf(name, "%s player car %d", LevelNames[city], i + 1);
				AddCar(city, FIRST_D1_PLAYER_CAR + i, name);
			}
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

// a player car of another Driver 1 city: its model comes from the car models lump of that level file, its two texture pages
// are loaded into the special slot as the pages of the foreign car (allcars.c)
static int ImportD1Car(int city, int model)
{
	char path[64];
	FILE* f;
	XYPAIR lumps[4];
	char* area[2] = { NULL, NULL };
	int areaSize[2];
	char *carLump, *texInfo, *ptr, *copy = NULL, *pages = NULL, *models;
	XYPAIR *permList, *specList;
	int *offsets;
	MODEL* mdl;
	u_char* polys;
	int pageAmount, nPerm, nSpec, lumpSize, permSectors, specSectors, sector, src[2] = { -1, -1 };
	int i, j, ok = 0;

	if (!D1Dlc_ConvertCity(city - FIRST_DRIVER1_CITY, "", path, sizeof(path)))
		return 0;

	FS_FixPathSlashes(path);
	f = fopen(path, "rb");

	if (!f)
		return 0;

	fseek(f, 8, SEEK_SET);

	if (fread(lumps, sizeof(lumps), 1, f) != 1)
		goto done;

	areaSize[0] = lumps[CITYLUMP_DATA1].y;
	areaSize[1] = lumps[CITYLUMP_DATA2].y;
	area[0] = ReadFilePart(f, lumps[CITYLUMP_DATA1].x, areaSize[0]);
	area[1] = ReadFilePart(f, lumps[CITYLUMP_DATA2].x, areaSize[1]);

	if (!area[0] || !area[1])
		goto done;

	carLump = FindLevelLump(area[0], areaSize[0], LUMP_CAR_MODELS_TYPE);
	texInfo = FindLevelLump(area[0], areaSize[0], LUMP_TEXTUREINFO_TYPE);

	if (!carLump)
		carLump = FindLevelLump(area[1], areaSize[1], LUMP_CAR_MODELS_TYPE);

	if (!texInfo)
		texInfo = FindLevelLump(area[1], areaSize[1], LUMP_TEXTUREINFO_TYPE);

	if (!carLump || !texInfo)
		goto done;

	lumpSize = *(int*)(carLump - 4);
	copy = (char*)malloc(lumpSize);

	if (!copy)
		goto done;

	memcpy(copy, carLump, lumpSize);

	// the texture lump (see ProcessTextureInfo): the pages with their textures, then the permanent and the special page list
	pageAmount = *(int*)texInfo;

	if (pageAmount > 120)
		goto done;

	ptr = texInfo + 8 + (pageAmount + 1) * sizeof(TP);

	for (i = 0; i < pageAmount; i++)
		ptr += 4 + *(int*)ptr * sizeof(TEXINF);

	nPerm = *(int*)ptr;
	permList = (XYPAIR*)(ptr + 4);
	ptr = (char*)&permList[16];
	nSpec = *(int*)ptr;
	specList = (XYPAIR*)(ptr + 4);

	// the two own pages of the car: the pages of its polygons that the level keeps in the special list
	offsets = (int*)(copy + 4);
	models = copy + 4 + 160;

	if (offsets[model * 3] == -1)
		goto done;

	mdl = (MODEL*)(models + offsets[model * 3]);
	polys = GET_RELOC_MODEL_DATA(u_char, mdl, poly_block);

	for (i = 0; i < mdl->num_polys; i++)
	{
		int type = polys[0] & 0x1f;

		if (type >= 20 && type <= 23 && polys[1] != 0xFF)
		{
			for (j = 0; j < nSpec; j++)
			{
				if (specList[j].x == polys[1] && src[0] != polys[1] && src[1] < 0)
				{
					src[src[0] < 0 ? 0 : 1] = polys[1];
					break;
				}
			}
		}

		polys += PolySizes[type];
	}

	if (src[0] < 0 || src[1] < 0)
		goto done;

	// palette counts of the two pages, and the page data: behind the permanent pages, which follow the first data area
	ptr = texInfo + 8 + (pageAmount + 1) * sizeof(TP);

	for (i = 0; i < pageAmount; i++)
	{
		int texAmount = *(int*)ptr;

		for (j = 0; j < 2; j++)
		{
			if (i == src[j])
				tpage_texamts[ForeignPageId(j)] = texAmount;
		}

		ptr += 4 + texAmount * sizeof(TEXINF);
	}

	permSectors = 0;

	for (i = 0; i < nPerm; i++)
		permSectors += (permList[i].y + 2047) / CDSECTOR_SIZE;

	specSectors = 0;

	for (i = 0; i < nSpec; i++)
		specSectors += (specList[i].y + 2047) / CDSECTOR_SIZE;

	sector = (lumps[CITYLUMP_DATA1].x + lumps[CITYLUMP_DATA1].y) / CDSECTOR_SIZE + permSectors;
	pages = ReadFilePart(f, sector * CDSECTOR_SIZE, specSectors * CDSECTOR_SIZE);

	if (!pages || !LoadSpecialSlotPages(pages, specList, nSpec, src[0], src[1], ForeignPageId(0), ForeignPageId(1)))
		goto done;

	if (!LoadCarModelIntoSpecialSlot(model, copy, src[0], src[1]))
		goto done;

	carTpages[GameLevel][6] = ForeignPageId(0);
	carTpages[GameLevel][7] = ForeignPageId(1);

	// the model points into the lump copy until the next car replaces it
	free(importedLump);
	importedLump = copy;
	copy = NULL;
	ok = 1;

done:
	free(copy);
	free(pages);
	free(area[0]);
	free(area[1]);

	if (f)
		fclose(f);

	return ok;
}

// makes 'model' of the car list a resident car; returns 0 when it cannot be loaded
static int PrepareModelInner(const SPAWN_CAR* c)
{
	int i, model = c->model;

	if (slotModel < 0 || FrameCnt < slotFrame)
	{
		// the level started with its own car in the special slot
		slotCity = GameLevel;
		slotModel = residentCarModels[SPECIAL_CAR_SLOT];
	}

	slotFrame = FrameCnt;

	if (model <= 4)
	{
		for (i = 0; i < SPECIAL_CAR_SLOT; i++)
		{
			if (residentCarModels[i] == model && gCarCleanModelPtr[i])
				return 1;
		}
	}

	if (slotModel == model && slotCity == c->city && gCarCleanModelPtr[SPECIAL_CAR_SLOT])
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
		if (c->city != GameLevel)
		{
			if (!ImportD1Car(c->city, model))
				return 0;
		}
		else if ((model >= 8 && !LoadSpecialSlotFromLevel(model)) || !LoadCarModelIntoSpecialSlot(model, NULL, -1, -1))
			return 0;

		gD1CarFixCity = c->city;

		if (!D1PrepareSpecialSlotCosmetics(model))
		{
			gD1CarFixCity = -1;
			return 0;
		}

		gD1CarFixCity = -1;
		residentCarModels[SPECIAL_CAR_SLOT] = model;
		D1BuildDamageZones(SPECIAL_CAR_SLOT);
		slotCity = c->city;
		slotModel = model;
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
		slotCity = c->city;
		slotModel = model;
		return gCarCleanModelPtr[SPECIAL_CAR_SLOT] != NULL;
	}

	ForeignCarClear();

	if (!LoadCarModelIntoSpecialSlot(model, NULL, -1, -1) || !LoadD2SlotData(model))
		return 0;

	residentCarModels[SPECIAL_CAR_SLOT] = model;
	slotCity = GameLevel;
	slotModel = model;
	return 1;
}

// Building a car of another city writes palette rows of the pedestrians and of the cop and civilian cars (its pages are
// numbered differently here): the rows of the level are put back after every car, or the cop and civilian cars get wrong colours.
static int PrepareModel(const SPAWN_CAR* c)
{
	static u_short civClutLevel[6][32][6];
	static int civClutFrame = 0;
	int ok;

	if (civClutFrame == 0 || FrameCnt < civClutFrame)
		memcpy(civClutLevel, civ_clut, sizeof(civClutLevel));

	civClutFrame = FrameCnt;
	ok = PrepareModelInner(c);
	memcpy(civ_clut, civClutLevel, sizeof(civClutLevel));
	return ok;
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

	printInfo("car spawner: show %s (model %d), special slot holds %d\n", c->name, c->model, residentCarModels[SPECIAL_CAR_SLOT]);
	padid = player[0].padid;
	cp = &car_data[(int)player[0].playerCarId];

	pos[0] = spawnX;
	pos[1] = 0;
	pos[2] = spawnZ;
	pos[3] = 0;

	if (!spawnPlaced)
	{
		// the first car: the player and the camera move to the spot
		InitPlayer(&player[0], cp, CONTROL_TYPE_PLAYER, spawnDir, &pos, c->model, 0, &padid);
		spawnPlaced = 1;
	}
	else
	{
		// the next cars keep the player and its camera: only the car is made again in its slot
		int slot;

		for (slot = 0; slot < MAX_CAR_RESIDENT_MODELS && residentCarModels[slot] != c->model; slot++);

		InitCar(cp, spawnDir, &pos, CONTROL_TYPE_PLAYER, slot, 0, &player[0].padid);
		ResetTyreTracks(cp, 0);
		cp->controlFlags |= CONTROL_FLAG_WAS_PARKED | CONTROL_FLAG_PLAYER_START_CAR;
		player[0].pos[1] = cp->hd.where.t[1];
	}

	return 1;
}

// -carexport: the showroom steps through every car of the list; each car stands still for a moment (the suspension settles),
// then it is written to exportDir (carexport.c). The game ends after the last car.
static FILE* exportIndex = NULL;
static int exportCount = 0;
static int exportWait = 0;

static void ExportTick(void)
{
	const SPAWN_CAR* c = &spawnList[spawnIndex];
	CAR_DATA* cp = &car_data[(int)player[0].playerCarId];
	char id[96], path[300];
	char* d;
	int slot = SPECIAL_CAR_SLOT, comp[4], k;

	if (++exportWait < 10)
		return;

	exportWait = 0;

	if (!exportIndex)
	{
		sprintf(path, "%s/index.json", exportDir);
		exportIndex = fopen(path, "wb");

		if (exportIndex)
			fprintf(exportIndex, "{ \"cars\": [\n");
	}

	sprintf(id, "%s%s%s", c->model < 8 ? LevelNames[c->city] : "", c->model < 8 ? " " : "", c->name);

	for (d = id; *d; d++)
		*d = (*d >= 'A' && *d <= 'Z') ? *d + 32 : (*d == ' ' ? '_' : *d);

	if (c->model <= 4)
	{
		for (k = 0; k < SPECIAL_CAR_SLOT; k++)
		{
			if (residentCarModels[k] == c->model && gCarCleanModelPtr[k])
			{
				slot = k;
				break;
			}
		}
	}

	for (k = 0; k < 4; k++)
		comp[k] = cp->hd.wheel[k].susCompression;

	if (CarExportWrite(exportDir, id, c->name, c->city, c->model, slot, comp))
	{
		printInfo("car export: %s -> %s\n", c->name, id);

		if (exportIndex)
			fprintf(exportIndex, "%s  { \"id\": \"%s\", \"name\": \"%s\", \"city\": \"%s\", \"model\": %d }", exportCount ? ",\n" : "", id, c->name, LevelNames[c->city], c->model);

		exportCount++;
	}
	else
		printWarning("car export: %s could not be written\n", c->name);

	// the next car that loads
	while (++spawnIndex < spawnCount)
	{
		if (ShowCar())
			return;
	}

	if (exportIndex)
	{
		fprintf(exportIndex, "\n] }\n");
		fclose(exportIndex);
	}

	printInfo("car export: %d of %d cars written to %s\n", exportCount, spawnCount, exportDir);
	exit(0);
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
	spawnPlaced = 0;

	if (ShowCar())
		gCarSpawner = 1;
}

void CarSpawnerStart(void)
{
	spawnPending = 0;
}

void CarExportCommandLine(const char* dir)
{
	strncpy(exportDir, dir, sizeof(exportDir) - 1);
}

void CarSpawnerCommandLine(int index, int auto_mode)
{
	spawnPending = index;
	spawnAuto = auto_mode;
}

static void LeaveShowroom(void)
{
	gCarSpawner = 0;
	spawnExitBlock = FrameCnt + 25;
	gCantDrive = 0;
	gCamAngleOverride = 0;
	gCameraAngle = 2048;	// back to the chase camera behind the car
	Mission.message_timer[0] = 0;
}

// the player may not get out of the car in the showroom
int CarSpawnerBlocksExit(void)
{
	return gCarSpawner == 1 || FrameCnt < spawnExitBlock;
}

int CarSpawnerUpdate(int pressed)
{
	if (exportDir[0] && spawnPending < 0 && gCarSpawner == 0 && FrameCnt > 20)
		spawnPending = 0;	// -carexport: the showroom starts at the first car

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

	if (exportDir[0])
	{
		ExportTick();
		return 0;
	}

	sprintf(spawnText, "< %s %d/%d > A / ENTER", spawnList[spawnIndex].name, spawnIndex + 1, spawnCount);
	SetMissionMessage(spawnText, 9, 1);

	if (spawnAuto && spawnFrame == 40)
	{
		if (spawnAuto == 2)
			Pads[0].dirnew |= MPAD_CROSS;
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

	// controller A (cross) or keyboard Enter (R3) confirms the car and ends the mode
	if (Pads[0].dirnew & (MPAD_CROSS | MPAD_R3))
	{
		LeaveShowroom();
		return 1;
	}

	// controller Back: the pause menu (Start and Escape open it as always)
	if (Pads[0].dirnew & MPAD_SELECT)
	{
		EnablePause(PAUSEMODE_PAUSE);
		return 1;
	}

	return 0;
}

#endif // !PSX
