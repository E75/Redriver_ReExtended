#include "driver2.h"

#include "wspccar.h"
#include "allcars.h"
#include "carspawner.h"
#include "cars.h"
#include "cosmetic.h"
#include "denting.h"
#include "mission.h"
#include "models.h"
#include "texture.h"
#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PSX

// WSPC cars: the converted files of a car (CLEAN.MDL, DAMAGED.MDL, LOW.MDL, TEXTURE.BIN, see wspcdlc/wspc_convert.h) become the
// car of the special slot. The model goes in through the car models lump path (a lump of one car is built in memory), the texture
// pages through the page numbers of a foreign car (allcars.c) with raw pixels instead of the compressed pages of a level.

extern int MaxSpecCluts;
extern int nspecpages;

#define WSPC_LUMP_HEADER	(4 + 160)		// see LoadCarModelIntoSpecialSlot: 13 model offset triples after one dword
#define WSPC_PAGE_BYTES		0x8000

static char* wspcLump = NULL;			// the loaded model points into it until the next WSPC car replaces it

static char* ReadWholeFile(const char* path, int* size)
{
	char fixed[160];
	FILE* f;
	char* data;
	long length;

	strncpy(fixed, path, sizeof(fixed) - 1);
	fixed[sizeof(fixed) - 1] = 0;
	FS_FixPathSlashes(fixed);

	f = fopen(fixed, "rb");

	if (!f)
		return NULL;

	fseek(f, 0, SEEK_END);
	length = ftell(f);
	fseek(f, 0, SEEK_SET);

	data = (char*)malloc(length + 4);

	if (data && fread(data, 1, length, f) != (size_t)length)
	{
		free(data);
		data = NULL;
	}

	fclose(f);

	if (data)
		*size = (int)length;

	return data;
}

// the pages of TEXTURE.BIN go to the first slots of the special car: page i gets the number ForeignPageId(i)
static int LoadPages(const char* tex, int size)
{
	int pageCount, capacity, palettes = 0;
	const char* ptr;
	RECT16 pos, cl;
	int i, p;

	if (size < 4)
		return 0;

	pageCount = *(const int*)tex;

	if (pageCount < 1 || pageCount > 2)
		return 0;

	ptr = tex + 4;

	for (i = 0; i < pageCount; i++)
	{
		int count = *(const int*)ptr;

		if (count < 0 || ptr + 4 + count * 32 + WSPC_PAGE_BYTES > tex + size)
			return 0;

		palettes += count;
		ptr += 4 + count * 32 + WSPC_PAGE_BYTES;
	}

	// the same palette room as LoadSpecialSlotPages
	capacity = (nspecpages != 0 && !IS_DRIVER1_CITY(GameLevel)) ? ((MaxSpecCluts + 3) & ~3) : 64;

	if (palettes > capacity)
	{
		printWarning("WSPC car: %d palettes do not fit into the special slot (%d)\n", palettes, capacity);
		return 0;
	}

	cl.x = slot_clutpos[specialSlot].vx;
	cl.y = slot_clutpos[specialSlot].vy;
	cl.w = 16;
	cl.h = 1;

	ptr = tex + 4;

	for (i = 0; i < pageCount; i++)
	{
		int dest = ForeignPageId(i);
		int slot = specialSlot + i;
		int count = *(const int*)ptr;

		pos.x = slot_tpagepos[slot].vx;
		pos.y = slot_tpagepos[slot].vy;
		pos.w = 64;
		pos.h = 256;

		update_slotinfo(dest, slot, &pos);
		ptr += 4;

		for (p = 0; p < count; p++)
		{
			LoadImage(&cl, (u_long*)ptr);
			ptr += 32;

			texture_cluts[dest][p] = GetClut(cl.x, cl.y);
			IncrementClutNum(&cl);
		}

		LoadImage(&pos, (u_long*)ptr);
		ptr += WSPC_PAGE_BYTES;

		texture_pages[dest] = GetTPage(0, 0, pos.x, pos.y);
		tpage_texamts[dest] = count;
	}

	return pageCount;
}

// the donor's cosmetics (physics, wheels, lights) are scaled from its box to the box of the WSPC body, like the Driver 1 player cars
static void FitCosmetics(void)
{
	CAR_COSMETICS* cos = &car_cosmetics[SPECIAL_CAR_SLOT];
	MODEL* model = gCarCleanModelPtr[SPECIAL_CAR_SLOT];
	SVECTOR* verts = GET_MODEL_DATA(SVECTOR, model, vertices);
	SVECTOR oldBox = cos->colBox, newBox;
	SVECTOR* lamps[9];
	int maxX = 0, maxZ = 0, minY = 32767, maxY = -32768;
	int v, j;

	for (v = 0; v < model->num_vertices; v++)
	{
		maxX = MAX(maxX, ABS(verts[v].vx));
		maxZ = MAX(maxZ, ABS(verts[v].vz));
		minY = MIN(minY, verts[v].vy);
		maxY = MAX(maxY, verts[v].vy);
	}

	if (oldBox.vx <= 0 || oldBox.vy <= 0 || oldBox.vz <= 0)
		return;

	newBox.vx = MAX(maxX - 6, 1);
	newBox.vy = MAX((maxY - minY) / 2, 1);
	newBox.vz = MAX(maxZ, 1);

	lamps[0] = &cos->headLight;
	lamps[1] = &cos->frontInd;
	lamps[2] = &cos->backInd;
	lamps[3] = &cos->brakeLight;
	lamps[4] = &cos->revLight;
	lamps[5] = &cos->policeLight;
	lamps[6] = &cos->exhaust;
	lamps[7] = &cos->smoke;
	lamps[8] = &cos->fire;

	for (j = 0; j < 9; j++)
	{
		lamps[j]->vx = lamps[j]->vx * newBox.vx / oldBox.vx;
		lamps[j]->vy = lamps[j]->vy * newBox.vy / oldBox.vy;
		lamps[j]->vz = lamps[j]->vz * newBox.vz / oldBox.vz;
	}

	for (j = 0; j < 4; j++)
	{
		cos->wheelDisp[j].vx = cos->wheelDisp[j].vx * newBox.vx * 98 / (oldBox.vx * 100);
		cos->wheelDisp[j].vz = cos->wheelDisp[j].vz * newBox.vz / oldBox.vz;
	}

	for (j = 0; j < 12; j++)
	{
		cos->cPoints[j].vx = cos->cPoints[j].vx * newBox.vx / oldBox.vx;
		cos->cPoints[j].vy = cos->cPoints[j].vy * newBox.vy / oldBox.vy;
		cos->cPoints[j].vz = cos->cPoints[j].vz * newBox.vz / oldBox.vz;
	}

	cos->colBox = newBox;
}

typedef struct
{
	int x, y, z, radius;
} WSPC_WHEEL;

#define WSPC_WHEEL_PERCENT	108		// drawn wheels are a little bigger than the wheel boxes of the source so that they fill the arches
#define WSPC_REST_DROP		-10		// body height on the wheels (measured in the car spawner): the arches wrap the upper part of the wheels
#define WSPC_HUB_OFFSET		18	// DrawCarWheels: wheel x = +-17 - wheelDisp.vx; the wheel model sticks out from there, so the hub sits a little inside the source wheel

// the wheelN=x.. y.. z.. radius.. lines of CAR.INFO (model coordinates, the model is centred on its bounding box)
static int ReadWheels(const char* info, WSPC_WHEEL* wheels)
{
	int count = 0;
	const char* p = info;

	while (count < 4 && (p = strstr(p, "wheel")) != NULL)
	{
		int index, x, y, z, radius;

		if (sscanf(p, "wheel%d=x%d y%d z%d radius%d", &index, &x, &y, &z, &radius) == 5)
		{
			wheels[count].x = x;
			wheels[count].y = y;
			wheels[count].z = z;
			wheels[count].radius = radius;
			count++;
		}

		p += 5;
	}

	return count;
}

// Draws the wheels where the source car has them (visual wheel table, as the Driver 1 cars: the handling keeps the donor's
// values), lifts the body so that the arches sit on the wheels and puts the lamps on the body.
static void FitWheelsAndLamps(const WSPC_WHEEL* wheels)
{
	CAR_COSMETICS* cos = &car_cosmetics[SPECIAL_CAR_SLOT];
	MODEL* model = gCarCleanModelPtr[SPECIAL_CAR_SLOT];
	SVECTOR* verts = GET_MODEL_DATA(SVECTOR, model, vertices);
	int minY = 32767, maxY = -32768, maxX = 0, maxZ = 0;
	int radius = 0, wheelY = 0;
	int height, lampY, v, j, slot, shift;

	for (v = 0; v < model->num_vertices; v++)
	{
		maxX = MAX(maxX, ABS(verts[v].vx));
		maxZ = MAX(maxZ, ABS(verts[v].vz));
		minY = MIN(minY, verts[v].vy);
		maxY = MAX(maxY, verts[v].vy);
	}

	for (j = 0; j < 4; j++)
	{
		// wheel 0 and 1: left (-x) front and rear, 2 and 3: right front and rear
		slot = (wheels[j].x > 0 ? 2 : 0) + (wheels[j].z < 0 ? 0 : 1);

		gD1WheelVisual[SPECIAL_CAR_SLOT][slot].vx = (slot < 2 ? WSPC_HUB_OFFSET : -WSPC_HUB_OFFSET) - wheels[j].x;
		gD1WheelVisual[SPECIAL_CAR_SLOT][slot].vz = -wheels[j].z;

		radius += wheels[j].radius;
		wheelY += wheels[j].y;
	}

	radius = radius * WSPC_WHEEL_PERCENT / 400;
	wheelY /= 4;

	// DrawCarWheels puts the wheel centre at 14 - vy - wheelSize, and the road is where the donor's wheels touch it; the body is drawn as it is,
	// so its vertices go down (or up) until the wheel centres of the source sit at that height
	shift = (14 - cos->wheelDisp[0].vy) - radius - wheelY + WSPC_REST_DROP;

	for (j = 0; j < 3; j++)
	{
		MODEL* m = j == 0 ? gCarCleanModelPtr[SPECIAL_CAR_SLOT] : j == 1 ? gCarDamModelPtr[SPECIAL_CAR_SLOT] : gCarLowModelPtr[SPECIAL_CAR_SLOT];
		SVECTOR* mv = GET_MODEL_DATA(SVECTOR, m, vertices);

		for (v = 0; v < m->num_vertices; v++)
			mv[v].vy += shift;
	}

	minY += shift;
	maxY += shift;

	for (j = 0; j < 4; j++)
		gD1WheelVisual[SPECIAL_CAR_SLOT][j].vy = cos->wheelDisp[j].vy;

	gD1WheelSizeVisual[SPECIAL_CAR_SLOT] = radius;
	gD1WheelVisualOn[SPECIAL_CAR_SLOT] = 1;

	cos->cog.vx = 0;
	cos->cog.vy = 0;
	cos->cog.vz = 0;

	height = maxY - minY;
	lampY = minY + height * 63 / 100;

	cos->headLight.vx = maxX * 62 / 100;
	cos->headLight.vy = lampY;
	cos->headLight.vz = -(maxZ - 4);

	cos->frontInd.vx = maxX * 80 / 100;
	cos->frontInd.vy = lampY + 6;
	cos->frontInd.vz = -(maxZ - 4);

	cos->backInd.vx = maxX * 80 / 100;
	cos->backInd.vy = lampY;
	cos->backInd.vz = maxZ - 4;

	cos->brakeLight.vx = maxX * 62 / 100;
	cos->brakeLight.vy = lampY;
	cos->brakeLight.vz = maxZ - 4;

	cos->revLight.vx = maxX * 45 / 100;
	cos->revLight.vy = lampY + 6;
	cos->revLight.vz = maxZ - 4;

	cos->policeLight.vx = 0;
	cos->policeLight.vy = minY;
	cos->policeLight.vz = 0;

	cos->exhaust.vx = maxX * 45 / 100;
	cos->exhaust.vy = maxY - 8;
	cos->exhaust.vz = maxZ;
	cos->smoke = cos->exhaust;

	cos->fire.vx = 0;
	cos->fire.vy = minY + height * 30 / 100;
	cos->fire.vz = -maxZ / 2;
}

int WspcCar_LoadIntoSlot(int car)
{
	static const char* const names[3] = { "CLEAN.MDL", "DAMAGED.MDL", "LOW.MDL" };
	char folder[96], path[128];
	char* files[3] = { NULL, NULL, NULL };
	char* tex = NULL;
	char* lump = NULL;
	char* info = NULL;
	WSPC_WHEEL wheels[4];
	int sizes[3], texSize = 0, infoSize = 0, pages, i, total, at, ok = 0;
	int* offsets;

	if (car < 0 || !gWspcDlcEnabled || !(gWspcDlcCarMask & (1 << car)) || GameLevel >= FIRST_DRIVER1_CITY)
		return 0;

	WspcDlc_CarFolder(car, folder, sizeof(folder));

	for (i = 0; i < 3; i++)
	{
		sprintf(path, "%s\\%s", folder, names[i]);
		files[i] = ReadWholeFile(path, &sizes[i]);

		if (!files[i])
			goto done;
	}

	sprintf(path, "%s\\TEXTURE.BIN", folder);
	tex = ReadWholeFile(path, &texSize);

	if (!tex)
		goto done;

	// a car models lump that holds this one car as model 0 (offsets -1 = no model)
	total = WSPC_LUMP_HEADER;

	for (i = 0; i < 3; i++)
		total += (sizes[i] + 3) & ~3;

	lump = (char*)calloc(1, total);

	if (!lump)
		goto done;

	offsets = (int*)(lump + 4);

	for (i = 0; i < 13 * 3; i++)
		offsets[i] = -1;

	at = 0;

	for (i = 0; i < 3; i++)
	{
		offsets[i] = at;
		memcpy(lump + WSPC_LUMP_HEADER + at, files[i], sizes[i]);
		at += (sizes[i] + 3) & ~3;
	}

	pages = LoadPages(tex, texSize);

	if (!pages || !LoadCarModelIntoSpecialSlot(0, lump, 0, pages > 1 ? 1 : -1))
		goto done;

	// the model points into the lump from here on
	free(wspcLump);
	wspcLump = lump;
	lump = NULL;

	carTpages[GameLevel][6] = ForeignPageId(0);
	carTpages[GameLevel][7] = ForeignPageId(pages > 1 ? 1 : 0);

	if (!LoadD2SlotCosmetics(WspcDlc_DonorModel(car)))
		goto done;

	FitCosmetics();

	// the converted car knows where its wheels are; without CAR.INFO the donor's wheels stay
	sprintf(path, "%s\\CAR.INFO", folder);
	info = ReadWholeFile(path, &infoSize);

	if (info)
	{
		info[infoSize] = 0;

		if (ReadWheels(info, wheels) == 4)
			FitWheelsAndLamps(wheels);
	}

	D1BuildDamageZones(SPECIAL_CAR_SLOT);
	ok = 1;

done:
	for (i = 0; i < 3; i++)
		free(files[i]);

	free(tex);
	free(lump);
	free(info);
	return ok;
}

#endif // !PSX
