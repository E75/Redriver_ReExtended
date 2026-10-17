#include "driver2.h"
#include "allcars.h"
#include "carspawner.h"
#include "models.h"
#include "mission.h"
#include "spool.h"
#include "texture.h"
#include "system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef PSX

// "All cities cars" (Driver 2 cities only): a special car of another Driver 2 city shows up in the traffic. It is
// spooled into the special car slot by the normal special car spool (spool.c), but from the level file of the other
// city: the 42 sectors of its special block are read here once at level start, the texture pages get numbers that
// no city uses and the polygons of its models are pointed at them.

#define FOREIGN_PAGE1		124		// texture page numbers of the imported car, above the pages of any city (the chopper has 126 and 127)
#define FOREIGN_PAGE2		125
#define FOREIGN_BLOCK_SECTORS	46	// a special car block is 42 sectors, the last read may run a few sectors further
#define SPECIAL_BLOCK_SECTORS	42
#define SPECIAL_BLOCK_START		0x1400	// first special car block in a level file, in sectors

// lump types of a level file (see chopper.c)
#define LUMP_TEXTUREINFO		34

extern char* LevelFiles[];
extern int SpecialByRegion[NUM_CITIES][20];
extern int PolySizes[56];
extern char* ReadFilePart(FILE* f, int offset, int size);
extern char* FindLevelLump(char* area, int areaSize, int type);

int gAllCityCars = 0;
int gForeignSpool = 0;
int gForeignLoaded = 0;
int gForeignForceCity = -1;
int gForeignForceSpecial = 0;

static int foreignCity = -1;
static int foreignSpecial = 0;
static int foreignSrc[2];
static char* foreignBlock = NULL;

// texture counts of the two pages of a special car, kept per (city, special): reading them needs the data areas of the level file
static int texAmtCache[4][5][2];
static char texAmtKnown[4][5];

static int ReadForeignBlock(int city, int special)
{
	char path[64];
	FILE* f;
	XYPAIR lumps[4];
	char* area[2] = { NULL, NULL };
	char* texInfo;
	char* ptr;
	int areaSize[2];
	int pageAmount, i, j;
	int ok = 0;

	if (city < 0 || city > 3 || special < 1 || special > 5)
		return 0;

	sprintf(path, "%s%s", gDataFolder, LevelFiles[city]);
	FS_FixPathSlashes(path);

	f = fopen(path, "rb");

	if (!f)
		return 0;

	foreignSrc[0] = specTpages[city][(special - 1) * 2];
	foreignSrc[1] = specTpages[city][(special - 1) * 2 + 1];

	if (!texAmtKnown[city][special - 1])
	{
		fseek(f, 8, SEEK_SET);

		if (fread(lumps, sizeof(lumps), 1, f) != 1)
			goto done;

		areaSize[0] = lumps[CITYLUMP_DATA1].y;
		areaSize[1] = lumps[CITYLUMP_DATA2].y;
		area[0] = ReadFilePart(f, lumps[CITYLUMP_DATA1].x, areaSize[0]);
		area[1] = ReadFilePart(f, lumps[CITYLUMP_DATA2].x, areaSize[1]);

		if (!area[0] || !area[1])
			goto done;

		texInfo = FindLevelLump(area[0], areaSize[0], LUMP_TEXTUREINFO);

		if (!texInfo)
			texInfo = FindLevelLump(area[1], areaSize[1], LUMP_TEXTUREINFO);

		if (!texInfo)
			goto done;

		// the texture lump (see ProcessTextureInfo): the palette count of a page is its texture count
		pageAmount = *(int*)texInfo;
		ptr = texInfo + 8 + (pageAmount + 1) * sizeof(TP);

		for (i = 0; i < pageAmount; i++)
		{
			int texAmount = *(int*)ptr;

			for (j = 0; j < 2; j++)
			{
				if (i == foreignSrc[j])
					texAmtCache[city][special - 1][j] = texAmount;
			}

			ptr += 4 + texAmount * sizeof(TEXINF);
		}

		texAmtKnown[city][special - 1] = 1;
	}

	tpage_texamts[FOREIGN_PAGE1] = texAmtCache[city][special - 1][0];
	tpage_texamts[FOREIGN_PAGE1 + 1] = texAmtCache[city][special - 1][1];

	free(foreignBlock);
	foreignBlock = ReadFilePart(f, (SPECIAL_BLOCK_START + (special - 1) * SPECIAL_BLOCK_SECTORS) * CDSECTOR_SIZE, FOREIGN_BLOCK_SECTORS * CDSECTOR_SIZE);
	ok = foreignBlock != NULL;

done:
	free(area[0]);
	free(area[1]);
	fclose(f);
	return ok;
}

// level start (after the special spool is set up): picks the foreign car and reads its data
void AllCarsLevelStart(void)
{
	int candidates[4 * 20][2];
	int count = 0;
	int city, region, pick;

	foreignCity = -1;
	gForeignSpool = 0;
	gForeignLoaded = 0;

	if (!gAllCityCars || GameLevel > 3 || !allowSpecSpooling || gCarSpawner)
		return;

	if (gForeignForceCity >= 0)
	{
		city = gForeignForceCity;
		pick = gForeignForceSpecial;
	}
	else
	{
		// the specials a city uses itself, without the secret car (5): it never shows up in traffic
		for (city = 0; city < 4; city++)
		{
			if (city == GameLevel)
				continue;

			for (region = 0; region < 20; region++)
			{
				int special = SpecialByRegion[city][region];

				if (special >= 1 && special <= 4 && count < 80)
				{
					candidates[count][0] = city;
					candidates[count][1] = special;
					count++;
				}
			}
		}

		if (count == 0)
			return;

		pick = rand() % count;
		city = candidates[pick][0];
		pick = candidates[pick][1];
	}

	if (city < 0 || city > 3 || pick < 1 || pick > 4 || !ReadForeignBlock(city, pick))
	{
		printWarning("All cities cars: could not read special %d of city %d\n", pick, city);
		return;
	}

	foreignCity = city;
	foreignSpecial = pick;
	printInfo("All cities cars: special %d of %s\n", pick, LevelNames[city]);
}

// Car Spawner: the special car 'special' of Driver 2 city 'city' becomes the car of the special slot at the next special spool
int ForeignCarSelect(int city, int special)
{
	if (!ReadForeignBlock(city, special))
		return 0;

	foreignCity = city;
	foreignSpecial = special;
	gForeignSpool = 1;
	gForeignLoaded = 1;
	return 1;
}

// Car Spawner: the special slot holds a car of this city again
void ForeignCarClear(void)
{
	foreignCity = -1;
	gForeignSpool = 0;
	gForeignLoaded = 0;
}

int ForeignCarMode(void)
{
	return foreignCity >= 0;
}

int ForeignSpecialIndex(void)
{
	return foreignSpecial;
}

int ForeignCarCity(void)
{
	return foreignCity;
}

char* ForeignSpoolPtr(int sector)
{
	return foreignBlock + (sector - FOREIGN_SPOOL_BASE) * CDSECTOR_SIZE;
}

int ForeignPageId(int i)
{
	return FOREIGN_PAGE1 + i;
}

// the polygons of a model of the foreign city name its two car pages by that city's numbers
// the polygons of a car model name its two car pages by src1 / src2: they get the page numbers dest1 / dest2
void RemapModelPages(MODEL* polySrc, int src1, int src2, int dest1, int dest2)
{
	PL_POLYFT4* polys;
	int i, ptype;

	if (polySrc == NULL)
		return;

	polys = GET_RELOC_MODEL_DATA(PL_POLYFT4, polySrc, poly_block);

	for (i = 0; i < polySrc->num_polys; i++)
	{
		ptype = polys->id & 31;

		if (ptype == 10 || ptype == 11 || ptype == 20 || ptype == 21 || ptype == 22 || ptype == 23)
		{
			if (polys->texture_set == src1)
				polys->texture_set = dest1;
			else if (polys->texture_set == src2)
				polys->texture_set = dest2;
		}

		polys = (PL_POLYFT4*)((char*)polys + PolySizes[ptype]);
	}
}

// the polygons of a model of the foreign city name its two car pages by that city's numbers
void ForeignRemapModel(MODEL* polySrc)
{
	if (gForeignSpool)
		RemapModelPages(polySrc, foreignSrc[0], foreignSrc[1], FOREIGN_PAGE1, FOREIGN_PAGE2);
}

#endif // !PSX
