#include "driver2.h"
#include "texture.h"
#include "system.h"
#include "mission.h"
#include "draw.h"
#include "cars.h"
#include "objanim.h"
#include "ASM/compres.h"

SXYPAIR tpagepos[MAX_TPAGE_SLOTS + 1] =
{
	{ 640, 0 },
	{ 704, 0 },
	{ 768, 0 },
	{ 832, 0 },
	{ 896, 0 },
	{ 960, 0 },
	{ 512, 256 },
	{ 576, 256 },
	{ 640, 256 },
	{ 704, 256 },
	{ 768, 256 },
	{ 832, 256 },
	{ 896, 256 },
	{ 448, 0 },
	{ 512, 0 },
	{ 576, 0 },
	{ 320, 256 },
	{ 384, 256 },
	{ 448, 256 },

	// [A] Driver 1 cities (the player cars keep two pages in slots): free VRAM next to the display buffer, the last one is
	// reserved for the Tanner shadow window (slot 20)
	{ 384, 0 },
	{ 320, 0 },

	{ -1, -1 }
};

// slots the permanent and area pages may use: 19, Driver 1 cities have one more
int numTpageSlots = 19;

// Driver 1 cities (4..9) copy the Chicago rows of specTpages and carTpages until the converter generates theirs (Part 6)
char specTpages[NUM_CITIES][12] = {
	{
		54, 55, 
		66, 67, 
		56, 57,
		68, 69,
		61, 64, 
		61, 64 
	},
	{ 
		38, 39, 
		38, 39, 
		42, 43, 
		44, 45,
		48, 49, 
		48, 49 
	},
	{ 
		18, 19, 
		65, 66, 
		67, 68, 
		11, 12, 
		63, 64, 
		63, 64
	},
	{ 
		66, 67,
		77, 78,
		73, 74,
		75, 76,
		69, 70,
		71, 72
	},
	{ 54, 55, 66, 67, 56, 57, 68, 69, 61, 64, 61, 64 },
	{ 54, 55, 66, 67, 56, 57, 68, 69, 61, 64, 61, 64 },
	{ 54, 55, 66, 67, 56, 57, 68, 69, 61, 64, 61, 64 },
	{ 54, 55, 66, 67, 56, 57, 68, 69, 61, 64, 61, 64 },
	{ 54, 55, 66, 67, 56, 57, 68, 69, 61, 64, 61, 64 },
	{ 54, 55, 66, 67, 56, 57, 68, 69, 61, 64, 61, 64 }
};

char carTpages[NUM_CITIES][8] = {
	{ 
		01, 58, 65, 62, 50, 63,
		54, 55
	},
	{ 
		10, 36, 35, 20, 37, 51,
		38, 39
	},
	{
		41, 59, 54, 62, 17, 32,
		18, 19 
	},
	{ 
		55, 59, 57, 68, 58, 60,
		66, 67
	},
	{ 1, 58, 65, 62, 50, 63, 54, 55 },
	{ 1, 58, 65, 62, 50, 63, 54, 55 },
	{ 1, 58, 65, 62, 50, 63, 54, 55 },
	{ 1, 58, 65, 62, 50, 63, 54, 55 },
	{ 1, 58, 65, 62, 50, 63, 54, 55 },
	{ 1, 58, 65, 62, 50, 63, 54, 55 }
};

char *palette_lump;

char* texturename_buffer = NULL;
int NoTextureMemory = 0;

u_short texture_pages[128];
u_short texture_cluts[128][32];
u_char tpageloaded[128];

int MaxSpecCluts;
int slotsused;

RECT16 clutpos;
RECT16 fontclutpos;
RECT16 mapclutpos;
DVECTOR slot_clutpos[MAX_TPAGE_SLOTS];
DVECTOR slot_tpagepos[MAX_TPAGE_SLOTS];
u_char tpageslots[MAX_TPAGE_SLOTS];

TP *tpage_position = NULL;
TEXINF* tpage_ids[128] = { 0 };
int texamount = 0;
int tpage_amount = 0;
int tpage_texamts[128];

int nspecpages = 0;
int nperms = 0;
XYPAIR *speclist = NULL;
XYPAIR *permlist = NULL;

RECT16 tpage; // stupid naming, absolute ass

short specialSlot;

// [D] [T]
void IncrementClutNum(RECT16 *clut)
{
	clut->x += 16;

	if (clut->x >= 1024) 
	{
		clut->x = 960;
		clut->y += 1;
	}
}

// [D] [T]
void IncrementTPageNum(RECT16 *tpage)
{
	int i = 0;

	while (++i)
	{
		// proper tpage position?
		if ((tpage->x == tpagepos[i - 1].x) && 
			(tpage->y == tpagepos[i - 1].y))
		{
			if (tpagepos[i].x == -1)
			{
				// out of tpages
				NoTextureMemory = 100;
			}
			else
			{
				// increment the tpage
				tpage->x = tpagepos[i].x;
				tpage->y = tpagepos[i].y;
			}

			// bust 'outta here, real fly
			break;
		}
		else
		{
			// last tpage?
			if (tpagepos[i].x == -1)
				break;
		}
	}
}

#ifndef PSX
// [A] - loads TIM files as level textures
void LoadTPageFromTIMs(int tpage2send)
{
	int i, j;
	RECT16 tmptpage;
	RECT16 tmpclut;
	SXYPAIR tpage;
	int tpn;
	
	char filename[64];
	TEXINF* details = tpage_ids[tpage2send];

	tpn = texture_pages[tpage2send];

	tpage.x = tpn << 6 & 0x3c0;
	tpage.y = (tpn << 4 & 0x100) + (tpn >> 2 & 0x200);
	
	// try loading TIMs directly
	for(i = 0; i < tpage_texamts[tpage2send]; i++)
	{
		TIMIMAGEHDR* timClut;
		TIMIMAGEHDR* timData;
		char* textureName;
		char* citytypeStr;
		int j;

		switch (GetCityType())
		{
			case CITYTYPE_NIGHT:
				citytypeStr = "N";
				break;
			case CITYTYPE_MULTI_DAY:
				citytypeStr = "M";
				break;
			case CITYTYPE_MULTI_NIGHT:
				citytypeStr = "MN";
				break;
			default:
				citytypeStr = "D";
				break;
			}

		textureName = texturename_buffer + details[i].nameoffset;

		sprintf(filename, "LEVELS\\%s\\%sPAGE_%d\\%s_%d.TIM", LevelNames[GameLevel], citytypeStr, tpage2send, textureName, i);

		if (!FileExists(filename))
			sprintf(filename, "LEVELS\\%s\\PAGE_%d\\%s_%d.TIM", LevelNames[GameLevel], tpage2send, textureName, i);

		if(!FileExists(filename))
			continue;
		
		Loadfile(filename, (char*)_other_buffer);

		// get TIM data
		timClut = (TIMIMAGEHDR*)(_other_buffer + sizeof(TIMHDR));
		timData = (TIMIMAGEHDR*)((char*)timClut + timClut->len);

		// replace tpage
		// upload it to ram
		tmptpage.x = tpage.x + (details[i].x >> 2);
		tmptpage.y = tpage.y + details[i].y;
		tmptpage.w = timData->width;
		tmptpage.h = timData->height;

		LoadImage(&tmptpage, (u_long*)((char*)timData + sizeof(TIMIMAGEHDR)));

		// get through all it's CLUTs
		// and replace
		for (j = 0; j < timClut->height; j++)
		{
			int cpal = GetCarPalIndex(tpage2send);
			int clutN;

			if (j > 0 && cpal > 0)
				clutN = civ_clut[cpal][i][j];
			else
				clutN = texture_cluts[tpage2send][i];

#if 0
			// FIXME:
			// this is a wasteful way handling multiple palettes
			// we just allocate new palettes to ensure that it would not glitch
			if(clutN == 0 || j > 0 && cpal > 0)
			{
				// add new CLUT
				clutN = GetClut(clutpos.x, clutpos.y);
				IncrementClutNum(&clutpos);
				civ_clut[cpal][i][j] = clutN;
			}
#endif
			
			tmpclut.x = (clutN & 0x3f) << 4;
			tmpclut.y = (clutN >> 6);
			tmpclut.w = 16;
			tmpclut.h = 1;

			LoadImage(&tmpclut, (u_long*)((char*)timClut + sizeof(TIMIMAGEHDR) + j * 32));
		}
	}
}
#endif

// [D] [T]
int LoadTPageAndCluts(RECT16 *tpage, RECT16 *cluts, int tpage2send, char *tpageaddress)
{
	int npalettes;
	int i;
	RECT16 temptpage;

	char* tempBuf;

	npalettes = *(int *)tpageaddress;
	tpageaddress += 4;

	for (i = 0; i < npalettes; i++)
	{
		LoadImage(cluts, (u_long*)tpageaddress);
		tpageaddress += 32;

		texture_cluts[tpage2send][i] = GetClut(cluts->x, cluts->y);
		
		IncrementClutNum(cluts);
	}

	temptpage.x = tpage->x;
	temptpage.y = tpage->y;
	temptpage.w = tpage->w;
	temptpage.h = 256;

	decomp_asm((char*)_other_buffer, tpageaddress);
	LoadImage(&temptpage, (u_long*)_other_buffer);

	texture_pages[tpage2send] = GetTPage(0, 0, tpage->x, tpage->y);
	IncrementTPageNum(tpage);

	return 1;
}

// UNUSED
int Find_TexID(MODEL *model, int t_id)
{
	char *polylist;
	polylist = GET_MODEL_DATA(char, model, poly_block);

	for (int i = 0; i < model->num_polys; i++)
	{
		switch (*polylist & 0x1F)
		{
			case 4:
			case 5:
			case 6:
			case 7:
			case 20:
			case 21:
			case 22:
			case 23:
				if (polylist[2] == t_id)
					return 1;
		}

		polylist += PolySizes[*polylist];
	}
	
	return 0;
}

// [D] [T]
TEXINF* GetTEXINFName(char *name, int *tpagenum, int *texturenum)
{
	char *nametable;
	int i, j;
	nametable = texturename_buffer;

	for (i = 0; i < tpage_amount; i++)
	{
		int texamt = tpage_texamts[i];
		TEXINF *texinf = tpage_ids[i];

		for (j = 0; j < texamt; j++)
		{
			if (!strcmp(nametable + texinf->nameoffset, name))
			{
				*tpagenum = i;
				*texturenum = j;

				return texinf;
			}

			texinf++;
		}
	}

	return NULL;
}

// [D] [T]
TEXINF* GetTextureInfoName(char *name, TPAN *result)
{
	TEXINF *tex;
	int tpagenum;
	int texturenum;

	tex = GetTEXINFName(name, &tpagenum, &texturenum);

	result->texture_page = tpagenum;
	result->texture_number = texturenum;

	return tex;
}

// [D] [T]
void update_slotinfo(int tpage, int slot, RECT16 *pos)
{
	tpageslots[slot] = tpage;
	tpageloaded[tpage] = slot;
	slot_tpagepos[slot].vx = pos->x;
	slot_tpagepos[slot].vy = pos->y;
}

// [D] [T]
void ProcessTextureInfo(char *lump_ptr)
{
	int i;
	char* ptr;
	tpage_amount =  *(int *)lump_ptr;
	texamount = *(int *)(lump_ptr + 4);
	tpage_position = (TP *)(lump_ptr + 8);

	ptr = (char *)&tpage_position[tpage_amount + 1];

	for (i = 0; i < tpage_amount; i++)
	{
		texamount = *(int *)ptr;
		ptr += 4;

#ifndef PSX
		tpage_ids[i] = (TEXINF *)D_MALLOC(sizeof(TEXINF) * texamount);
		memcpy(tpage_ids[i], ptr, sizeof(TEXINF) * texamount);
#else
		tpage_ids[i] = (TEXINF *)ptr;
#endif

		ptr += (texamount * sizeof(TEXINF));

		tpage_texamts[i] = texamount;
	}

	nperms = *(int *)ptr;
	permlist = (XYPAIR *)(ptr + 4);

	ptr = (char *)&permlist[16];

	nspecpages = *(int *)ptr;
	speclist = (XYPAIR *)(ptr + 4);

	// initialize here on PSX
	InitCyclingPals();
}

// [A] 1 when the level file keeps this texture page in its special (player car) list
int IsSpecialPage(int page)
{
	int i;

	for (i = 0; i < nspecpages && i < 32; i++)
	{
		if (speclist[i].x == page)
			return 1;
	}

	return 0;
}

#ifndef PSX
extern char g_CurrentLevelFileName[64];

// [A] one-shot texture replacement
void LoadPermanentTPagesFromTIM()
{
	int slot;

	for (slot = 0; slot < numTpageSlots; slot++)
	{
		if(tpageslots[slot] != 0xFF)
		{
			int tpage = tpageslots[slot];
			LoadTPageFromTIMs(tpage);

#if 0
			// initialize ALL texture palettes
			// this makes damaged textures appear properly
			int pal = GetCarPalIndex(tpage);
			
			if (pal)
			{
				int carpal = GetCarPalIndex(tpage);

				if(carpal > 0)
				{
					for (int i = 0; i < 32; i++)
						civ_clut[carpal][i][0] = texture_cluts[tpage][i];
				}

			}
#endif
		}
	}
}

#endif // !PSX

void load_civ_palettes(RECT16 *cluts)
{
	return;
}

#ifndef PSX
// [A] the special pages of the level (all special cars of the city, the Rio chopper among them) stay in the level file
static int specPagesSector = -1;
static int specPagesSectors = 0;
static XYPAIR specPageList[32];
static int specPageCount = 0;

// loads two pages from the special pages of a level file (src1/src2 = their page numbers, 'all' = the sectors that
// hold them in the order of 'list') into the special slot as pages dest1/dest2, with palettes
int LoadSpecialSlotPages(char* all, const XYPAIR* list, int count, int src1, int src2, int dest1, int dest2)
{
	char* buffer;
	int i, slot, loaded, palettes, capacity;
	RECT16 pos, cl;

	// the palette area of the slot is as big as the biggest special pair of the city, a Driver 1 city has two spare slots of 32
	capacity = (nspecpages != 0) ? ((MaxSpecCluts + 3) & ~3) : 64;
	palettes = 0;
	buffer = all;

	for (i = 0; i < count; i++)
	{
		if (list[i].x == src1 || list[i].x == src2)
			palettes += *(int*)buffer;

		buffer += (list[i].y + 2047) & -CDSECTOR_SIZE;
	}

	if (palettes > capacity)
	{
		printWarning("LoadSpecialSlotPages: %d palettes do not fit into the special slot (%d)\n", palettes, capacity);
		return 0;
	}

	cl.x = slot_clutpos[specialSlot].vx;
	cl.y = slot_clutpos[specialSlot].vy;
	cl.w = 16;
	cl.h = 1;

	slot = specialSlot;
	loaded = 0;
	buffer = all;

	for (i = 0; i < count; i++)
	{
		int tp = list[i].x;

		if (tp == src1 || tp == src2)
		{
			int dest = (tp == src1) ? dest1 : dest2;

			pos.x = slot_tpagepos[slot].vx;
			pos.y = slot_tpagepos[slot].vy;
			pos.w = 64;
			pos.h = 256;

			update_slotinfo(dest, slot, &pos);
			LoadTPageAndCluts(&pos, &cl, dest, buffer);
			slot++;
			loaded++;
		}

		buffer += (list[i].y + 2047) & -CDSECTOR_SIZE;
	}

	return loaded == 2;
}

// loads the two pages of special car 'model' (8 and up) of this level into the special slot
int LoadSpecialSlotFromLevel(int model)
{
	char* all;
	int page1, page2, ok;

	if (specPagesSector < 0 || specPageCount == 0 || model < 8)
		return 0;

	page1 = specTpages[GameLevel][(model - 8) * 2];
	page2 = specTpages[GameLevel][(model - 8) * 2 + 1];

	all = (char*)malloc(specPagesSectors * CDSECTOR_SIZE);
	loadsectors(all, specPagesSector, specPagesSectors);

	ok = LoadSpecialSlotPages(all, specPageList, specPageCount, page1, page2, page1, page2);
	free(all);

	if (ok)
	{
		carTpages[GameLevel][6] = page1;
		carTpages[GameLevel][7] = page2;
	}

	return ok;
}
#endif

#ifndef PSX
// the page data of page 'page' inside a TPAGE lump, NULL when the lump does not hold it
static char* PageInLump(const LEV_TPAGES* t, int page)
{
	char* p = t->data;
	int i;

	for (i = 0; i < t->nperms; i++)
	{
		if (t->perms[i].x == page)
			return p;

		p += (t->perms[i].y + 2047) & -CDSECTOR_SIZE;
	}

	for (i = 0; i < t->nspec; i++)
	{
		if (t->spec[i].x == page)
			return p;

		p += (t->spec[i].y + 2047) & -CDSECTOR_SIZE;
	}

	return NULL;
}

// [A] day night cycle: loads the permanent pages and the player car's special pages again from the TPAGE lump of the
// other LEV of this city, into the VRAM place and palette rows they have now. A page whose palette count differs
// from the lump it was loaded from would shift the palettes of the next page: it is skipped.
void ReloadPermanentTPages(const LEV_TPAGES* now, const LEV_TPAGES* before)
{
	int i, n, reloaded = 0;
	int pages[18];

	n = 0;

	for (i = 0; i < now->nperms; i++)
		pages[n++] = now->perms[i].x;

	for (i = 6; i < 8; i++)
	{
		if (carTpages[GameLevel][i] >= 0)
			pages[n++] = carTpages[GameLevel][i];
	}

	for (i = 0; i < n; i++)
	{
		int tp = pages[i];
		char* src = PageInLump(now, tp);
		char* old = PageInLump(before, tp);
		RECT16 tpage, cluts;

		if (!src || !old || *(int*)src != *(int*)old)
		{
			printInfo("day night LEV: page %d not reloaded\n", tp);
			continue;
		}

		cluts.x = (texture_cluts[tp][0] & 0x3f) << 4;
		cluts.y = texture_cluts[tp][0] >> 6;
		cluts.w = 16;
		cluts.h = 1;

		tpage.x = (texture_pages[tp] & 0xf) << 6;
		tpage.y = ((texture_pages[tp] >> 4) & 1) << 8 | ((texture_pages[tp] >> 11) & 1) << 9;
		tpage.w = 64;
		tpage.h = 256;

		LoadTPageAndCluts(&tpage, &cluts, tp, src);
		reloaded++;
	}

	printInfo("day night LEV: %d of %d permanent and special pages reloaded\n", reloaded, n);
}
#endif

// [D] [T]
void LoadPermanentTPages(int *sector)
{
	int nsectors;
	char *tpagebuffer;
	int tloop, tset, i;
	int specmodel;
	int page1, page2;

	// init tpage and cluts
	MaxSpecCluts = 0;

	for (tloop = 0; tloop < 128; tloop++)
		texture_pages[tloop] = GetTPage(0, 0, 960, 0);

	for (tloop = 0; tloop < 128; tloop++)
	{
		for (tset = 0; tset < 32; tset++)
			texture_cluts[tloop][tset] = GetClut(960, 16);
	}

	slotsused = 0;
	numTpageSlots = IS_DRIVER1_CITY(GameLevel) ? 20 : 19;
	memset(tpageloaded, 0, sizeof(tpageloaded));

	clutpos.x = 960;
	clutpos.y = 256;
	clutpos.w = 16;
	clutpos.h = 1;

	mapclutpos.x = 960;
	mapclutpos.y = 256;
	mapclutpos.w = 16;
	mapclutpos.h = 1;

	tpage.x = tpagepos[0].x;
	tpage.y = tpagepos[0].y;
	tpage.w = 64;
	tpage.h = 256;

	IncrementClutNum(&clutpos);
	fontclutpos = clutpos;
	
	IncrementClutNum(&clutpos);
	ProcessPalletLump(palette_lump, 0);

	load_civ_palettes(&clutpos);

	tpagebuffer = (char*)mallocptr;
	nsectors = 0;

	for (i = 0; i < nperms; i++)
		nsectors += (permlist[i].y + 2047) / CDSECTOR_SIZE;

	loadsectors(tpagebuffer, *sector, nsectors);

	*sector += nsectors;

	for (i = 0; i < nperms; i++)
	{
		int tp = permlist[i].x;

		update_slotinfo(tp, slotsused, &tpage);
		LoadTPageAndCluts(&tpage, &clutpos, tp, tpagebuffer);
		slotsused++;

		tpagebuffer += (permlist[i].y + 2047) & -CDSECTOR_SIZE;
	}
	
	tpagebuffer = (char*)mallocptr;

	slot_clutpos[slotsused].vx = clutpos.x;
	slot_clutpos[slotsused].vy = clutpos.y;

	// init special slot texture
	specmodel = (residentCarModels[SPECIAL_CAR_SLOT] - 8) * 2;

	specialSlot = (short)slotsused;

	// [A] Driver 1 cities without a player car (special slot below 8) load no special pages: their two slots stay free
	if (IS_DRIVER1_CITY(GameLevel) && specmodel < 0)
		page1 = page2 = -1;
	else
	{
		page1 = specTpages[GameLevel][specmodel];
		page2 = specTpages[GameLevel][specmodel + 1];
	}

	carTpages[GameLevel][6] = page1;
	carTpages[GameLevel][7] = page2;

	if (nspecpages != 0 && IS_DRIVER1_CITY(GameLevel) && page1 < 0)
	{
		// the special pages stay unread, the next lump starts behind them
		for (i = 0; i < nspecpages; i++)
			*sector += (speclist[i].y + 2047) / CDSECTOR_SIZE;
	}
	else if (nspecpages != 0)
	{
		int temp, clutsloaded;
		
		temp = 0;
		clutsloaded = 0;

		nsectors = 0;

		for (i = 0; i < nspecpages; i++)
			nsectors += (speclist[i].y + 2047) / CDSECTOR_SIZE;

		loadsectors(tpagebuffer, *sector, nsectors);

#ifndef PSX
		// the lump that holds speclist is gone after the level load
		specPagesSector = *sector;
		specPagesSectors = nsectors;
		specPageCount = MIN(nspecpages, 32);
		memcpy(specPageList, speclist, specPageCount * sizeof(XYPAIR));
#endif

		*sector += nsectors;

		for (i = 0; i < nspecpages; i++)
		{
			int tp, npalettes;
			npalettes = *(int *)tpagebuffer;

			temp += npalettes;

			if ((i & 1) != 0)
			{
				if (temp > MaxSpecCluts)
					MaxSpecCluts = temp;

				temp = 0;
			}

			tp = speclist[i].x;

			// find a special car TPAGEs
			if (page1 == tp || page2 == tp)
			{
				update_slotinfo(tp, slotsused, &tpage);
				LoadTPageAndCluts(&tpage, &clutpos, tp, tpagebuffer);
				slotsused++;

				clutsloaded += npalettes;
			}

			tpagebuffer += (speclist[i].y + 2047) & -CDSECTOR_SIZE;
		}

		while (clutsloaded < MaxSpecCluts)
		{
			IncrementClutNum(&clutpos);
			clutsloaded++;
		}
	}

	if (clutpos.x != 960) 
	{
		clutpos.x = 960;
		clutpos.y++;
	}

	// init all slots
	for (i = slotsused; i < numTpageSlots; i++)
	{
		tpageslots[i] = 0xFF;

		slot_clutpos[i].vx = clutpos.x;
		slot_clutpos[i].vy = clutpos.y;

		slot_tpagepos[i].vx = tpage.x;
		slot_tpagepos[i].vy = tpage.y;

		IncrementTPageNum(&tpage);
		clutpos.y += 8;
	}

	if (IS_DRIVER1_CITY(GameLevel))
		printInfo("D1 texture slots: %d used by permanent and special pages, areas get %d, palettes end at row %d\n", slotsused, numTpageSlots - slotsused, clutpos.y);

	// [A] Driver 1 cities: colour variants the palette lump does not hold show the base colour (a clut of 0 is black)
	if (IS_DRIVER1_CITY(GameLevel))
	{
		int car, tex, pal;

		for (car = 0; car < 6; car++)
		{
			int page = carTpages[GameLevel][car];

			if (page < 0)
				continue;

			for (tex = 0; tex < tpage_texamts[page] && tex < 32; tex++)
			{
				for (pal = 0; pal < 6; pal++)
				{
					if (civ_clut[car][tex][pal] == 0)
						civ_clut[car][tex][pal] = texture_cluts[page][tex];
				}
			}
		}
	}
}

// [D] [T]
void ReloadIcons(void)
{
	ReportMode(0);
	ReportMode(1);
}

// [D] [T]
void GetTextureDetails(char *name, TEXTURE_DETAILS *info, int defaultToSea)
{
	int i, j;
	int texamt;
	char *nametable;
	TEXINF *texinf;
	
	nametable = texturename_buffer;

	for (i = 0; i < tpage_amount; i++)
	{
		texamt = tpage_texamts[i];
		texinf = tpage_ids[i];

		for (j = 0; j < texamt; j++)
		{
			if (!strcmp(nametable + texinf->nameoffset, name))
			{
				info->tpageid = texture_pages[i];
				info->clutid = texture_cluts[i][j];
				info->texture_number = j;
				info->texture_page = i;

				setUVWH(&info->coords, texinf->x, texinf->y, texinf->width - 1, texinf->height - 1);

				// bust 'outta here, real fly
				return;
			}

			texinf++;
		}
	}

	info->tpageid = 0;
	info->clutid = 0;
	info->texture_number = 0;
	info->texture_page = 0;
	if (defaultToSea)
		GetTextureDetails("SEA", info);	// weird but ok, ok...
}





