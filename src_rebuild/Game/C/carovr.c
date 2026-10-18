#include "driver2.h"

#include "carovr.h"
#include "cosmetic.h"
#include "allcars.h"
#include "carspawner.h"
#include "cars.h"
#include "main.h"
#include "mission.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PSX

// Car override, see carovr.h: the lamps and wheels of a car come from CARS/<id>/car.json (written by the car editor) instead of
// the built-in values. The file is read when the car of a slot changes, the values are put in the cosmetics every game step.

extern char* LevelNames[];
extern SVECTOR gD1WheelVisual[MAX_CAR_RESIDENT_MODELS][4];
extern int gD1WheelSizeVisual[MAX_CAR_RESIDENT_MODELS];
extern char gD1WheelVisualOn[MAX_CAR_RESIDENT_MODELS];

char gCarsDir[200] = "CARS";

typedef struct
{
	int key;				// city * 100 + model of the file that was read, -1 = none yet
	int found;
	int cog[3];
	int head[3], frontInd[3], backInd[3], brake[3], rev[3], exhaust[3], police[3];
	int wheel[4][3];
	int hubX[4], comp[4];
	int radius;
} OVERRIDE;

static OVERRIDE ovr[MAX_CAR_RESIDENT_MODELS];
static int ovrInit = 0;

void CarOverrideId(char* out, int city, int model)
{
	const char* n = LevelNames[city];

	for (; *n; n++)
		*out++ = (*n >= 'A' && *n <= 'Z') ? *n + 32 : (*n == ' ' ? '_' : *n);

	sprintf(out, "_%d", model);
}

// the three numbers of [x, y, z] after "key1" and then "key2" (key2 may be NULL)
static int ReadVec(const char* json, const char* key1, const char* key2, int* out)
{
	const char* p = strstr(json, key1);

	if (p && key2)
		p = strstr(p, key2);

	if (!p || !(p = strchr(p, '[')))
		return 0;

	return sscanf(p, "[%d, %d, %d]", &out[0], &out[1], &out[2]) == 3;
}

static int ReadInt(const char* json, const char* key, const char* from, int* out)
{
	const char* p = strstr(from, key);

	if (!p || !(p = strchr(p, ':')))
		return 0;

	*out = atoi(p + 1);
	return 1;
}

static int LoadOverride(OVERRIDE* o, int city, int model)
{
	char id[48], path[260], *json;
	const char* w;
	FILE* f;
	long size;
	int i, ok;

	CarOverrideId(id, city, model);
	sprintf(path, "%s/%s/car.json", gCarsDir, id);
	f = fopen(path, "rb");

	if (!f)
		return 0;

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	fseek(f, 0, SEEK_SET);
	json = (char*)malloc(size + 1);

	if (!json || fread(json, 1, size, f) != (size_t)size)
	{
		free(json);
		fclose(f);
		return 0;
	}

	json[size] = 0;
	fclose(f);

	ok = ReadVec(json, "\"cog\"", NULL, o->cog) &&
		ReadVec(json, "\"headlight\"", "\"L\"", o->head) && ReadVec(json, "\"indicatorFront\"", "\"L\"", o->frontInd) &&
		ReadVec(json, "\"indicatorBack\"", "\"L\"", o->backInd) && ReadVec(json, "\"brake\"", "\"L\"", o->brake) &&
		ReadVec(json, "\"reverse\"", "\"L\"", o->rev) && ReadVec(json, "\"exhaust\"", NULL, o->exhaust) &&
		ReadVec(json, "\"police\"", NULL, o->police) && ReadInt(json, "\"wheelRadius\"", json, &o->radius);

	// the wheels: one object per wheel in the "wheels" array
	w = strstr(json, "\"wheels\"");

	for (i = 0; ok && i < 4; i++)
	{
		w = w ? strstr(w + 1, "\"hubX\"") : NULL;
		ok = w && ReadInt(json, "\"hubX\"", w, &o->hubX[i]) && ReadInt(json, "\"compression\"", w, &o->comp[i]) && ReadVec(w, "\"pos\"", NULL, o->wheel[i]);
	}

	free(json);

	if (ok)
		printInfo("car override: %s\n", path);
	else
		printWarning("car override: %s is not valid\n", path);

	return ok;
}

static void Lamp(SVECTOR* dst, const int* p, const int* cog)
{
	int x = ABS(p[0] - cog[0]);

	dst->vx = dst->vx < 0 ? -x : x;	// the side of the value decides left and right of the indicators
	dst->vy = p[1] - cog[1];
	dst->vz = p[2] - cog[2];
}

static void Apply(int slot, const OVERRIDE* o)
{
	CAR_COSMETICS* cc = &car_cosmetics[slot];
	SVECTOR* disp = gD1WheelVisualOn[slot] ? gD1WheelVisual[slot] : cc->wheelDisp;
	int w;

	Lamp(&cc->headLight, o->head, o->cog);
	Lamp(&cc->frontInd, o->frontInd, o->cog);
	Lamp(&cc->backInd, o->backInd, o->cog);
	Lamp(&cc->brakeLight, o->brake, o->cog);
	Lamp(&cc->revLight, o->rev, o->cog);

	cc->exhaust.vx = o->exhaust[0] - o->cog[0];
	cc->exhaust.vy = o->exhaust[1] - o->cog[1];
	cc->exhaust.vz = o->exhaust[2] - o->cog[2];
	cc->policeLight.vy = o->police[1] - o->cog[1];
	cc->policeLight.vz = o->police[2] - o->cog[2];

	for (w = 0; w < 4; w++)
	{
		disp[w].vx = o->hubX[w] - o->wheel[w][0];
		disp[w].vy = 14 - o->comp[w] - o->radius - o->wheel[w][1];
		disp[w].vz = -o->wheel[w][2];
	}

	if (gD1WheelVisualOn[slot])
		gD1WheelSizeVisual[slot] = o->radius;
	else
		cc->wheelSize = o->radius;
}

void CarOverrideUpdate(void)
{
	int slot;

	if (!ovrInit)
	{
		for (slot = 0; slot < MAX_CAR_RESIDENT_MODELS; slot++)
			ovr[slot].key = -1;

		ovrInit = 1;
	}

	for (slot = 0; slot < MAX_CAR_RESIDENT_MODELS; slot++)
	{
		int model = residentCarModels[slot], city = GameLevel, key;

		if (model < 0 || model == 13)
			continue;

		if (slot == SPECIAL_CAR_SLOT)
		{
			if (ForeignCarMode())
				city = ForeignCarCity();
			else
				city = CarSpawnerSlotCity(model);
		}

		key = city * 100 + model;

		if (ovr[slot].key != key)
		{
			ovr[slot].key = key;
			ovr[slot].found = LoadOverride(&ovr[slot], city, model);
			ovr[slot].key = key;
		}

		if (ovr[slot].found)
			Apply(slot, &ovr[slot]);
	}
}

#endif // !PSX
