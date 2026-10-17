#include "driver2.h"
#include "jericho.h"
#include "chopper.h"
#include "cars.h"
#include "mission.h"
#include "spool.h"
#include "texture.h"
#include "models.h"
#include "motion_c.h"
#include "pedest.h"
#include "system.h"
#include "civ_ai.h"
#include "map.h"
#include "main.h"
#include "players.h"

// [A] Jericho Mode in every city: only Havana and Vegas have the JERI_* models. Vegas keeps the Jericho texture page
// among its permanent pages, so the six models and that page are read from VEGAS.LEV on first use, like the chopper
// (chopper.c) does with Rio. The page goes into the special slot of the current city as page JERICHO_PAGE.

#ifndef PSX

#define JERICHO_CITY	2		// Vegas
#define JERICHO_PAGE	122		// below the pages of the chopper (126, 127) and of foreign cars (124, 125)

#define LUMP_MODELS			1
#define LUMP_MODELNAMES		12

extern char* LevelFiles[];
extern int allowSpecSpooling;
extern char* ReadFilePart(FILE* f, int offset, int size);
extern char* FindLevelLump(char* area, int areaSize, int type);

static const char* jerichoNames[6] = { "JERI_TORSO", "JERI_HEAD", "JERI_U_ARM_LEFT", "JERI_L_ARM_LEFT", "JERI_U_ARM_RIGHT", "JERI_L_ARM_RIGHT" };
static char* jerichoMem[6];
static int jerichoPageLoaded = 0;

#define IMPORT_FAIL() do { printWarning("Jericho import failed at line %d of jericho.c\n", __LINE__); goto done; } while (0)

// the model with this name in the model pack, copied and made ready for drawing; NULL when it is not there
static MODEL* CopyModel(char* names, char* models, int modelCount, const char* name, char** memory)
{
	char* src;
	MODEL* model;
	int i, j, size;

	for (i = 0; i < modelCount && strcmp(names, name) != 0; i++)
		names += strlen(names) + 1;

	if (i == modelCount)
		return NULL;

	src = models + 4;

	for (j = 0; j < i; j++)
		src += 4 + *(int*)src;

	size = *(int*)src;
	free(*memory);
	*memory = (char*)calloc(size + 256, 1);
	memcpy(*memory, src + 4, size);

	model = (MODEL*)*memory;
	model->tri_verts = 0;

#if MODEL_RELOCATE_POINTERS
	model->poly_block += (int)(char*)model;
	model->vertices += (int)(char*)model;
	model->normals += (int)(char*)model;
	model->point_normals += (int)(char*)model;

	if (model->collision_block != 0)
		model->collision_block += (int)(char*)model;
#endif

	return model;
}

// the textured polygons of a model name a page of Vegas: returns that page. With dest < 0 the texture numbers they use are
// numbered in idMap (-1 = unused so far), with dest >= 0 the polygons are pointed at page dest and those new numbers
static int PageOfModel(MODEL* model, int dest, short* idMap, int* idCount)
{
	PL_POLYFT4* polys = GET_RELOC_MODEL_DATA(PL_POLYFT4, model, poly_block);
	extern int PolySizes[56];
	int i, ptype, page = -1;

	for (i = 0; i < model->num_polys; i++)
	{
		ptype = polys->id & 31;

		if (ptype == 10 || ptype == 11 || ptype == 20 || ptype == 21 || ptype == 22 || ptype == 23)
		{
			if (page < 0)
				page = polys->texture_set;

			if (dest < 0)
			{
				if (idMap[polys->texture_id] < 0)
					idMap[polys->texture_id] = (*idCount)++;
			}
			else
			{
				polys->texture_set = dest;
				polys->texture_id = idMap[polys->texture_id];
			}
		}

		polys = (PL_POLYFT4*)((char*)polys + PolySizes[ptype]);
	}

	return page;
}

static int ImportJericho(void)
{
	char path[64];
	FILE* f;
	XYPAIR lumps[4];
	char* area[2] = { NULL, NULL };
	int areaSize[2];
	char* page = NULL;
	char* names;
	char* models;
	char* texInfo;
	char* ptr;
	MODEL* list[6];
	XYPAIR* permList;
	int pageAmount, nPerm, modelCount, srcPage, idCount = 0, npal, imageBytes, newSize;
	short idMap[256];
	XYPAIR pageEntry;
	char* slim = NULL;
	int areaSlot = 0, firstSlot = -1;
	int i, sector, permIndex = -1, ok = 0;

	sprintf(path, "%s%s", gDataFolder, LevelFiles[JERICHO_CITY]);
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
	texInfo = FindLevelLump(area[0], areaSize[0], 34);

	if (!names) names = FindLevelLump(area[1], areaSize[1], LUMP_MODELNAMES);
	if (!models) models = FindLevelLump(area[1], areaSize[1], LUMP_MODELS);
	if (!texInfo) texInfo = FindLevelLump(area[1], areaSize[1], 34);

	if (!names || !models || !texInfo)
		IMPORT_FAIL();

	modelCount = *(int*)models;

	for (i = 0; i < 6; i++)
	{
		list[i] = CopyModel(names, models, modelCount, jerichoNames[i], &jerichoMem[i]);

		if (list[i] == NULL)
			IMPORT_FAIL();
	}

	memset(idMap, 0xFF, sizeof(idMap));
	srcPage = PageOfModel(list[0], -1, idMap, &idCount);

	for (i = 1; i < 6; i++)
		PageOfModel(list[i], -1, idMap, &idCount);

	// the permanent page list of the texture lump (see ProcessTextureInfo)
	pageAmount = *(int*)texInfo;

	if (pageAmount > 120 || srcPage < 0)
		IMPORT_FAIL();

	ptr = texInfo + 8 + (pageAmount + 1) * sizeof(TP);

	for (i = 0; i < pageAmount; i++)
		ptr += 4 + *(int*)ptr * sizeof(TEXINF);

	nPerm = *(int*)ptr;
	permList = (XYPAIR*)(ptr + 4);

	// the permanent pages follow the first data area one after the other
	sector = (lumps[CITYLUMP_DATA1].x + lumps[CITYLUMP_DATA1].y) / CDSECTOR_SIZE;

	for (i = 0; i < nPerm; i++)
	{
		if (permList[i].x == srcPage)
		{
			permIndex = i;
			break;
		}

		sector += (permList[i].y + 2047) / CDSECTOR_SIZE;
	}

	if (permIndex < 0)
		IMPORT_FAIL();

	page = ReadFilePart(f, sector * CDSECTOR_SIZE, ((permList[permIndex].y + 2047) / CDSECTOR_SIZE) * CDSECTOR_SIZE);

	if (!page)
		IMPORT_FAIL();

	// only the palettes the six models use go to VRAM (the special slot holds a few, the Vegas page has 33)
	npal = *(int*)page;
	imageBytes = permList[permIndex].y - 4 - npal * 32;
	newSize = 4 + idCount * 32 + imageBytes;
	slim = (char*)calloc(((newSize + 2047) / CDSECTOR_SIZE) * CDSECTOR_SIZE, 1);

	if (!slim || imageBytes <= 0)
		IMPORT_FAIL();

	*(int*)slim = idCount;

	for (i = 0; i < 256; i++)
	{
		if (idMap[i] >= 0)
		{
			if (i >= npal)
				IMPORT_FAIL();

			memcpy(slim + 4 + idMap[i] * 32, page + 4 + i * 32, 32);
		}
	}

	memcpy(slim + 4 + idCount * 32, page + 4 + npal * 32, imageBytes);
	pageEntry.x = srcPage;
	pageEntry.y = newSize;

	// a car with special textures (every Driver 1 player car) keeps its slot: Jericho then takes the last area slot
	for (CAR_DATA* cp = car_data; cp < &car_data[MAX_CARS]; cp++)
	{
		if (cp->controlType != CONTROL_TYPE_NONE && residentCarModels[cp->ap.model] > 5)
		{
			if (!IS_DRIVER1_CITY(GameLevel))
			{
				printWarning("Jericho: a car uses the special texture slot, no Jericho in this car\n");
				goto done;
			}

			areaSlot = 1;
		}
	}

	if (areaSlot)
	{
		if (numTpageSlots - slotsused < 3)
			IMPORT_FAIL();

		firstSlot = numTpageSlots - 1;

		if (tpageslots[firstSlot] != 0xFF)
			tpageloaded[tpageslots[firstSlot]] = 0;

		numTpageSlots--;
		LoadedArea = -1;
	}
	else
	{
		// the special slot of this city belongs to Jericho now: no special cars, no chopper pages
		allowSpecSpooling = 0;
		AbortSpecialSpool();
		PingOutAllSpecialCivCars();

		// a Driver 1 city has no special pages: its two spare slots after the permanent ones are taken from the area textures
		if (specialSlot + 2 > slotsused)
		{
			for (i = specialSlot; i < specialSlot + 2; i++)
			{
				if (tpageslots[i] != 0xFF)
					tpageloaded[tpageslots[i]] = 0;
			}

			slotsused = specialSlot + 2;
			LoadedArea = -1;
		}
	}

	if (!LoadSpecialSlotPages(slim, &pageEntry, 1, srcPage, -1, JERICHO_PAGE, JERICHO_PAGE, firstSlot))
		IMPORT_FAIL();

	for (i = 0; i < 6; i++)
	{
		PageOfModel(list[i], JERICHO_PAGE, idMap, &idCount);
		pmJerichoModels[i] = list[i];
	}

	StoreVertexLists();
	jerichoPageLoaded = 1;

	if (!areaSlot)
		ChopperPagesLost();
	ok = 1;

done:
	free(area[0]);
	free(area[1]);
	free(page);
	free(slim);
	fclose(f);
	return ok;
}

// the chopper or a new level took the special slot: the Jericho page has to be loaded again
void JerichoPageLost(void)
{
	jerichoPageLoaded = 0;
}

int JerichoEnsureModels(void)
{
	int i;

	// Vegas keeps the Jericho page among its permanent pages; Havana's page is an area page that is not always in VRAM
	if (pmJerichoModels[0] != NULL && (jerichoPageLoaded || GameLevel == JERICHO_CITY))
		return 1;

	for (i = 0; i < 6; i++)
		pmJerichoModels[i] = NULL;

	return ImportJericho();
}

#else

void JerichoPageLost(void) {}
int JerichoEnsureModels(void) { return 0; }

#endif
