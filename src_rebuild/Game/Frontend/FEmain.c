#include "driver2.h"

#include "FEmain.h"

#include "C/cd_icon.h"

#include "C/system.h"
#include "C/glaunch.h"
#include "C/mission.h"
#include "C/time.h"
#include "C/loadsave.h"
#include "C/pad.h"
#include "C/loadview.h"
#include "C/pres.h"
#include "C/sound.h"
#include "C/debris.h"
#include "C/E3stuff.h"
#include "C/fmvplay.h"
#include "C/gamesnd.h"
#include "C/scores.h"
#include "C/loadsave.h"
#include "C/spool.h"
#include "C/state.h"
#include "C/camera.h"
#include "C/allcars.h"
#include "C/draw.h"

#ifndef PSX

#include "PsyX/PsyX_render.h"
#include "../utils/targa.h"
#include "../utils/hqfont.h"

#define HIRES_FONTS

struct FEFONT_QUAD
{
	float x0, y0, s0, t0; // top-left
	float x1, y1, s1, t1; // bottom-right
};

TextureID gHiresFEFontTexture = 0;
OUT_FN2RANGE gHiresFEFontRanges[4];
OUT_FN2INFO gHiresFEFontCharData[4][224];
int gHiresFEFontRangeCount = 0;

void InitHiresFEFont()
{
	char namebuffer[64];
	u_char* data;

	// init font2
	if (!gHiresFEFontTexture)
	{
		gHiresFEFontRangeCount = 0;

		int width, height, bpp;
		int x, y;
		int size;
		FILE* fp;
		sprintf(namebuffer, "%s%s", gDataFolder, "GFX\\HQ\\fefont.fn2");
		FS_FixPathSlashes(namebuffer);

		fp = fopen(namebuffer, "rb");
		if (fp)
		{
			int i;

			// read fn2 step by step
			OUT_FN2HEADER fn2hdr;
			fread(&fn2hdr, sizeof(fn2hdr), 1, fp);

			gHiresFEFontRangeCount = fn2hdr.range_count;
			for (i = 0; i < fn2hdr.range_count; ++i)
			{
				fread(&gHiresFEFontRanges[i], sizeof(gHiresFEFontRanges[i]), 1, fp);
				fread(gHiresFEFontCharData[i], sizeof(OUT_FN2INFO), gHiresFEFontRanges[i].count, fp);
			}

			fclose(fp);
		}

		// load TGA file
		sprintf(namebuffer, "%s%s", gDataFolder, "GFX\\HQ\\fefont.tga");
		FS_FixPathSlashes(namebuffer);
		
		if (LoadTGAImage(namebuffer, &data, width, height, bpp))
		{
			if (bpp == 32)
			{
				gHiresFEFontTexture = GR_CreateRGBATexture(HIRES_FONT_SIZE_W, HIRES_FONT_SIZE_H, data);
			}
			free(data);
		}
	}
}

int FEGetHiresBakedQuad(int char_index, float scale, float* xpos, float* ypos, FEFONT_QUAD* q)
{
	float ipw = 1.0f / (float)HIRES_FONT_SIZE_W;
	float iph = 1.0f / (float)HIRES_FONT_SIZE_H;

	const int idx = char_index - gHiresFEFontRanges[0].start;
	if (idx < 0)
		return 0;
	const OUT_FN2INFO* b = gHiresFEFontCharData[0] + idx;

	float fscale = 0.5f * scale;

	float s_x = b->x1 - b->x0;
	float s_y = b->y1 - b->y0;

	q->x0 = *xpos + b->xoff * fscale;
	q->y0 = *ypos + b->yoff * fscale;
	q->x1 = (b->xoff2 - b->xoff) * fscale;
	q->y1 = (b->yoff2 - b->yoff) * fscale;

	q->s0 = b->x0 * 255.0f * ipw;
	q->t0 = b->y0 * 255.0f * iph;
	q->s1 = s_x * 255.0f * ipw;
	q->t1 = s_y * 255.0f * iph;

	q->y0 += 32.0f;

	*xpos += b->xadvance * fscale;

	return 1;
}

void SetHiresFEFontTexture(int enabled)
{
	if (gHiresFEFontTexture == 0)
	{
		return;
	}

	DR_PSYX_TEX* tex = (DR_PSYX_TEX*)current->primptr;
	if (enabled)
		SetPsyXTexture(tex, gHiresFEFontTexture, 255, 255);
	else
		SetPsyXTexture(tex, 0, 0, 0);

	addPrim(current->ot + 1, tex);
	current->primptr += sizeof(DR_PSYX_TEX);
}

int FEStringWidthHires(char* string)
{
	char* pString = string;
	u_char c = 0;

	int w = 0;

	while ((c = *pString++) != 0)
	{
		float fx, fy;
		fx = 0;
		fy = 0;
		FEFONT_QUAD q;
		if (!FEGetHiresBakedQuad(c, 1.0f, &fx, &fy, &q))
			continue;

		w += fx;
	}

	return w;
}

int FEPrintStringSizedHires(char* string, int x, int y, int scale, int transparent, int r, int g, int b)
{
	if (current == NULL || string == NULL)
		return -1;

	POLY_FT4* shadow;
	POLY_FT4* font;
	u_char let;
	int w;
	int h;

	SetHiresFEFontTexture(0);
	font = (POLY_FT4*)current->primptr;

	while ((let = *string++) != 0)
	{
		if (let == '\n')
			continue;

		float fx, fy;
		fx = x;
		fy = y;
		FEFONT_QUAD q;
		if (!FEGetHiresBakedQuad(let, scale / 4096.0f, &fx, &fy, &q))
			continue;

		setPolyFT4(font);
		setSemiTrans(font, 1);

		setRGB0(font, r, g, b);
		setUVWH(font, q.s0, q.t0, q.s1, q.t1);
		setXYWH(font, q.x0, q.y0, q.x1, q.y1);

		font->clut = 0;
		font->tpage = 0;

		addPrim(current->ot + 1, font);
		shadow = font + 1;

		// add shadow poly
		memcpy(shadow, font, sizeof(POLY_FT4));
		setRGB0(shadow, 10, 10, 10);
		setXYWH(shadow, q.x0 + 1.0f, q.y0 + 1.0f, q.x1, q.y1);

		addPrim(current->ot + 1, shadow);
		font += 2;

		// make room for next character
		x += fx - x;
	}

	// set tail
	current->primptr = (char*)font;
	SetHiresFEFontTexture(1);

	return x;
}

int FEPrintStringHires(char* string, float x, float y, int justification, int r, int g, int b)
{
	POLY_FT4* shadow;
	POLY_FT4* font;
	u_char let;

	if (justification & 4)
	{
		x -= FEStringWidthHires(string);
	}

	SetHiresFEFontTexture(0);
	font = (POLY_FT4*)current->primptr;

	int counter = 0;

	while ((let = *string++) != 0)
	{
		float fx, fy;
		fx = x;
		fy = y;
		FEFONT_QUAD q;
		FEGetHiresBakedQuad(let, 1.0f, &fx, &fy, &q);

		setPolyFT4(font);
		setSemiTrans(font, 1);

		setRGB0(font, r, g, b);
		setUVWH(font, q.s0, q.t0, q.s1, q.t1);
		setXYWH(font, q.x0, q.y0, q.x1, q.y1);

		font->clut = 0;
		font->tpage = 0;

		addPrim(current->ot + 1, font);
		shadow = font + 1;

		// add shadow poly
		memcpy(shadow, font, sizeof(POLY_FT4));
		setRGB0(shadow, 10, 10, 10);
		setXYWH(shadow, q.x0 + 1.0f, q.y0 + 1.0f, q.x1, q.y1);

		addPrim(current->ot + 1, shadow);
		font += 2;

		// add space for next character
		x += fx - x;

		if (++counter >= 32)
			break;
	}

	// set tail
	current->primptr = (char *)font;

	SetHiresFEFontTexture(1);

	return x;
}

#endif // PSX

struct PSXBUTTON
{
	short x, y, w, h;
	u_char l, r;		// left, right next item id
	u_char u, d;		// up, down next item id
	int userDrawFunctionNum;
	short s_x, s_y;
	int action, var;
	char Name[32];
};

struct PSXSCREEN
{
	u_char index;
	u_char numButtons;
	u_char userFunctionNum;
	PSXBUTTON buttons[8];
};

// #define USE_EMBEDDED_FRONTEND_SCREENS

enum FEButtonAction
{
	BTN_NEXT_SCREEN = 1,
	BTN_START_GAME = 2,
	BTN_DISABLED = 3,
	BTN_PREVIOUS_SCREEN = 4,
	BTN_HIDDEN = 5
};

#define FE_MAKEVAR(code, value)		((code & 0xffff) << 8 | (value & 0xff))

#ifdef USE_EMBEDDED_FRONTEND_SCREENS
#include "FEscreens.inc"
#else
PSXSCREEN PsxScreens[54];	// 0-39 SCRS.BIN, 40 mini cars, 41 Driver 1 cities, 42 camera orbit, 43 extended settings, 44 draw distance, 45 crowd, 46 camera distance, 47 all cities cars, 48 sky, 49 Driver 1 mission cities, 50 Driver 1 missions, 51 day night cycle, 52 volumetric effects, 53 Driver 1 DLC (40-53 are built in code)
#endif

#define FE_OTSIZE 16

struct FE_CHARDATA
{
	u_char u;
	u_char v;
	u_char w;
	u_char h;
};

struct FE_FONT
{
	int NumFonts;
	FE_CHARDATA CharInfo[256];
};

struct SCREEN_LIMITS
{
	short minx, miny, maxx, maxy;
};

struct BOTCH
{
	int missNum;
	char** name;
};

char* NullStr = "\0";

typedef int(*screenFunc)(int bSetup);

extern ACTIVE_CHEATS AvailableCheats;
extern ACTIVE_CHEATS ActiveCheats;

int CentreScreen(int bSetup); // 0x001C31FC
int CarSelectScreen(int bSetup); // 0x001C3430
int CopDiffLevelScreen(int bSetup); // 0x001C6404
int VibroOnOffScreen(int bSetup); // 0x001C3A60
int MissionSelectScreen(int bSetup); // 0x001C3B1C
int MissionCityScreen(int bSetup); // 0x001C4338
int CutSceneSelectScreen(int bSetup); // 0x001C4600
int CutSceneCitySelectScreen(int bSetup); // 0x001C4B30
int SetVolumeScreen(int bSetup); // 0x001C50B0
int ScoreScreen(int bSetup); // 0x001C59A0
int SubtitlesOnOffScreen(int bSetup); // 0x001C64B0
int CityCutOffScreen(int bSetup); // 0x001C64FC
int ControllerScreen(int bSetup); // 0x001C6548
int MainScreen(int bSetup); // 0x001C6614
int CheatScreen(int bSetup); // 0x001C5B4C
int ImmunityOnOffScreen(int bSetup); // 0x001C6654
int InvincibleOnOffScreen(int bSetup); // 0x001C66A0
int GamePlayScreen(int bSetup); // 0x001C66EC
int GameNameScreen(int bSetup); // 0x001C60A0
int CheatNumlayerSelect(int bSetup); // 0x001C6724
int UserReplaySelectScreen(int bSetup);
int TimeOfDaySelectScreen(int bSetup);
int DemoScreen(int bSetup);
int MiniCarsOnOffScreen(int bSetup);
int Driver1CitySelectScreen(int bSetup);
int CameraOrbitOnOffScreen(int bSetup);
int ExtendedOptionsScreen(int bSetup);
extern void ShowSavingWaitMessage(char* message, int height);
int DrawDistanceScreen(int bSetup);
int CrowdScreen(int bSetup);
int CameraZoomScreen(int bSetup);
int AllCarsScreen(int bSetup);
int SkyScreen(int bSetup);
int DayCycleScreen(int bSetup);
int VolumetricEffectsScreen(int bSetup);
int Driver1DlcScreen(int bSetup);
int StoryScreen(int bSetup);
int Driver1MissionCityScreen(int bSetup);
int Driver1MissionListScreen(int bSetup);

#define D1MISSION_CITY_SCREEN	49		// built in code: Story - Driver 1, the cities with converted missions
#define D1MISSION_LIST_SCREEN	50		// built in code: the missions of one city, paged
#define FEVAR_D1MISSION			22		// button var: value = Driver 1 story mission (0..43), selects it as the game to start
#define DRIVER1_CITY_SCREEN		41		// built in code, not part of SCRS.BIN
#define CAMERA_ORBIT_SCREEN		42		// built in code, On/Off screen opened from the secret list
#define FEVAR_CAMERA_ORBIT		16		// button var code, see the switch in the button action handler
#define EXTENDED_SCREEN			43		// built in code: list of the extended settings (opened from the secret list)
#define DRAW_DISTANCE_SCREEN	44		// built in code: Original / 5x / 10x
#define CROWD_SCREEN			45		// built in code: Default / 3x / 5x
#define FEVAR_DRAW_DISTANCE		17
#define FEVAR_CROWD				18
static const int crowdChoices[5] = { 1, 2, 3, 5, 10 };	// traffic and people multiplier per menu row
#define CAMERA_ZOOM_SCREEN		46		// built in code: follow camera distance 1.0 / 1.2 / 1.5 / 1.8 / 2.0
#define FEVAR_CAMERA_ZOOM		19
#define ALLCARS_SCREEN			47		// built in code: Off / On
#define FEVAR_ALLCARS			20
#define SKY_SCREEN				48		// built in code: Off / Smooth / HD
#define FEVAR_SKY				21
#define CYCLE_SCREEN				51		// built in code: day night cycle Off / On
#define FEVAR_CYCLE				23
extern int gDayCycle;						// daycycle.c
#define VOLUMETRIC_SCREEN		52		// built in code: volumetric effects Off / On (sun shafts, lamp light and atmospheric fog together)
#define FEVAR_VOLUMETRIC		24
#define D1DLC_SCREEN			53		// built in code: Driver 1 DLC Off / On (Extended Settings)
#define FEVAR_D1DLC				25
extern int gSunShafts;					// sky.c
extern int gVolumeLights;				// draw.c
extern int gAtmosphere;					// draw.c
extern int gSkyHd;						// sky.c: 0 original, 1 smooth, 2 HD

screenFunc fpUserFunctions[] = {
	CentreScreen,
	CarSelectScreen,
	CopDiffLevelScreen,
	VibroOnOffScreen,
	MissionSelectScreen,
	MissionCityScreen,
	CutSceneCitySelectScreen,
	CutSceneSelectScreen,
	SetVolumeScreen,
	ScoreScreen,
	SubtitlesOnOffScreen,
	CityCutOffScreen,
	ControllerScreen,
	MainScreen,
	CheatScreen,
	ImmunityOnOffScreen,
	InvincibleOnOffScreen,
	GamePlayScreen,
	GameNameScreen,
	CheatNumlayerSelect,
	UserReplaySelectScreen,
	TimeOfDaySelectScreen,
	DemoScreen,
	MiniCarsOnOffScreen,
	Driver1CitySelectScreen,
	CameraOrbitOnOffScreen,
	ExtendedOptionsScreen,
	DrawDistanceScreen,
	CrowdScreen,
	CameraZoomScreen,
	AllCarsScreen,
	SkyScreen,
	StoryScreen,
	Driver1MissionCityScreen,
	Driver1MissionListScreen,
	DayCycleScreen,
	VolumetricEffectsScreen,
	Driver1DlcScreen
};

char* gfxNames[4] = {
	"DATA\\CARS\\CCARS.RAW",
	"DATA\\CARS\\HCARS.RAW",
	"DATA\\CARS\\VCARS.RAW",
	"DATA\\CARS\\RCARS.RAW",
};

char* cutGfxNames[4] = {
	"DATA\\CUTS\\CCUTS.RAW",
	"DATA\\CUTS\\HCUTS.RAW",
	"DATA\\CUTS\\VCUTS.RAW",
	"DATA\\CUTS\\RCUTS.RAW",
};

int CutAmounts[5] = {
	0, 8, 7, 5, 8
};

int CutAmountsTotal[5] = {
	0, 8, 15, 20, 40
};

char* CutSceneNames[28] =
{
	M_LTXT_ID(MTXT_RedRiver),
	M_LTXT_ID(MTXT_Themorgue),
	M_LTXT_ID(MTXT_TheWitness),
	M_LTXT_ID(MTXT_Lennysapartment),
	M_LTXT_ID(MTXT_TheCubaConnection),
	M_LTXT_ID(MTXT_TheIntruder),
	M_LTXT_ID(MTXT_MeetingCaine),
	M_LTXT_ID(MTXT_LeavingTown),

	M_LTXT_ID(MTXT_Lookingforalead),
	M_LTXT_ID(MTXT_Movingout),
	M_LTXT_ID(MTXT_Watchingthetruck),
	M_LTXT_ID(MTXT_RosannaSotoClue),
	M_LTXT_ID(MTXT_TheDockyard),
	M_LTXT_ID(MTXT_TheHit),
	M_LTXT_ID(MTXT_SeizingJericho),

	M_LTXT_ID(MTXT_VasquezinVegas),
	M_LTXT_ID(MTXT_TradingJericho),
	M_LTXT_ID(MTXT_CBankjob),
	M_LTXT_ID(MTXT_ThePoolHall),
	M_LTXT_ID(MTXT_CainesWarpath),

	M_LTXT_ID(MTXT_CaineinRio),
	M_LTXT_ID(MTXT_WarningJones),
	M_LTXT_ID(MTXT_TheShootout),
	M_LTXT_ID(MTXT_Lennysgetaway),
	M_LTXT_ID(MTXT_Lennygetsit),
	M_LTXT_ID(MTXT_BackinChicago),
	M_LTXT_ID(MTXT_VasquezmeetsCaine),
	M_LTXT_ID(MTXT_Credits),
};

char *areaNames[4][8] = {
	{
		M_LTXT_ID(MTXT_Downtown),
		M_LTXT_ID(MTXT_Wrigleyville),
		M_LTXT_ID(MTXT_Greektown),
		M_LTXT_ID(MTXT_GrantPark),
		M_LTXT_ID(MTXT_MeigsField),
		M_LTXT_ID(MTXT_UkrainianVillage),
		M_LTXT_ID(MTXT_RiverNorth),
		M_LTXT_ID(MTXT_CabriniGreen),
	},
	{
		M_LTXT_ID(MTXT_NecropolisdeColon),
		M_LTXT_ID(MTXT_Capitolio),
		M_LTXT_ID(MTXT_OldHavana),
		M_LTXT_ID(MTXT_TheDocks),
		M_LTXT_ID(MTXT_Vedado),
		M_LTXT_ID(MTXT_Plaza),
		M_LTXT_ID(MTXT_PlazadelaRevolucion),
		NULL,
	},
	{
		M_LTXT_ID(MTXT_Downtown),
		M_LTXT_ID(MTXT_UpperStrip),
		M_LTXT_ID(MTXT_Lakeside),
		M_LTXT_ID(MTXT_MidStrip),
		M_LTXT_ID(MTXT_NorthVegas),
		M_LTXT_ID(MTXT_Lakes),
		M_LTXT_ID(MTXT_GhostTown),
		NULL,
	},
	{
		M_LTXT_ID(MTXT_Centro),
		M_LTXT_ID(MTXT_Copacabana),
		M_LTXT_ID(MTXT_SantaTereza),
		M_LTXT_ID(MTXT_LagoaRodrigodeFreitas),
		M_LTXT_ID(MTXT_PracadaBandeira),
		M_LTXT_ID(MTXT_Leblon),
		M_LTXT_ID(MTXT_Flamengo),
		NULL,
	},
};

static char gameAreas[64] = {
	// Getaway (0-6)
	0, 1, // Downtown, Wrigleyville
	0, 1, // Necropolis De Colon, Capitolio
	0, 1, // Downtown, Upper Strip
	0, 1, // Centro, Copacabana

	// Gate race (8-14)
	2, 3, // Greektown, Grant Park
	0, 2, // Necropolis de Colon, Old Havana
	2, 3, // Lakeside, Mid Strip
	1, 2, // Copacabana, Santa Tereza

	// Checkpoint (16-22)
	0, 4, // Downtown, Meigs Field
	3, 2, // The Docks, Old Havana
	4, 2, // North Vegas, Lakeside
	3, 4, // Lagoa Rodrigo de Freitas, Praca da Bandeira

	// Trailblazer (24-30)
	3, 0, // Grant Park, Downtown
	2, 4, // Old Havana, Vedado
	0, 1, // Downtown, Upper Strip
	5, 4, // Leblon, Praca da Bandeira

	// [MP] Cops 'n Robbers (32-38)
	5, 0, // Ukrainian Village, Downtown
	4, 0, // Vedado, Necropolis de Colon
	3, 0, // Mid Strip, Downtown
	1, 0, // Copacabana, Centro

	// [MP] Capture the Flag (40-46)
	7, 6, // Cabrini Green, River North
	2, 5, // Old Havana, Plaza
	5, 6, // Lakes, Ghost Town
	6, 0, // Flamengo, Centro

	// [MP] Take a Ride (48-54)
	6, 7, // River North, Cabrini Green
	5, 2, // Plaza, Old Havana
	0, 5, // Downtown, Lakes
	0, 6, // Centro, Flamengo

	// [MP] Checkpoint (56-62)
	6, 7, // River North, Cabrini Green
	2, 6, // Old Havana, Plaza de la Revolucion
	6, 4, // Ghost Town, North Vegas
	0, 6, // Centro, Flamengo
};

#define AREA_NAME(level, index) GET_MISSION_TXT(areaNames[level][index])

#define GAMEMODE_AREA(level, offset, index) gameAreas[offset + level * 2 + index]
#define GAMEMODE_AREA_NAME(level, offset, index) AREA_NAME(level, GAMEMODE_AREA(level, offset, index))

char cutUnlock[] = {
	2, 2, 2, 4, 5,
	6, 7, 7, 7, 9,
	11, 11, 11, 11, 12,
	12, 13, 13, 14, 15,
	17, 17, 17, 17, 18,
	18, 19, 19, 19, 19,
	21, 21, 21, 21, 22,
	22, 22, 23, 23, 23,
	28
};

int CarAvailability[4][10] = {
	{1,1,1,1,0,0,0,0,0,0},
	{1,1,1,1,0,0,0,0,0,0},
	{1,1,1,1,0,0,0,0,0,0},
	{1,1,1,1,0,0,0,0,0,0},
};

char carNumLookup[4][10] = {
	{1, 2, 3, 4, 0, 8, 9, 10, 11, 12},
	{1, 2, 3, 4, 0, 8, 9, 10, 11, 12},
	{1, 2, 3, 4, 0, 8, 9, 10, 11, 12},
	{1, 2, 3, 4, 0, 8, 9, 10, 11, 12},
};

int minmaxSelections[4][2] = {

	{ 0, 8 },
	{ 8, 18 },
	{ 18, 28 },
	{ 28, 37 }
};

BOTCH botch[38] = {
	{ 1, &MissionName[0]},
	{ 2, &MissionName[1]},
	{ 3, &MissionName[2]},
	{ 4, &MissionName[3]},
	{ 5, &MissionName[4]},
	{ 6, &MissionName[5]},
	{ 7, &MissionName[6]},
	{ 9, &MissionName[7]},
	{ 10, &MissionName[8]},
	{ 11, &MissionName[9]},
	{ 13, &MissionName[10]},
	{ 14, &MissionName[11]},
	{ 15, &MissionName[12]},
	{ 16, &MissionName[13]},
	{ 17, &MissionName[14]},
	{ 18, &MissionName[15]},
	{ 19, &MissionName[16]},
	{ 20, &MissionName[17]},
	{ 21, &MissionName[18]},
	{ 22, &MissionName[19]},
	{ 23, &MissionName[20]},
	{ 24, &MissionName[21]},
	{ 25, &MissionName[22]},
	{ 26, &MissionName[23]},
	{ 27, &MissionName[24]},
	{ 28, &MissionName[25]},
	{ 29, &MissionName[26]},
	{ 30, &MissionName[27]},
	{ 31, &MissionName[28]},
	{ 32, &MissionName[29]},
	{ 33, &MissionName[30]},
	{ 34, &MissionName[31]},
	{ 35, &MissionName[32]},
	{ 37, &MissionName[33]},
	{ 38, &MissionName[34]},
	{ 39, &MissionName[35]},
	{ 40, &MissionName[36]},
	{ 0,NULL}
};

char ScreenTitle[128];
char ScoreName[128];

int cutSelection = 0;
int currCity = 0;

enum MenuSelectScreens
{
	SCREEN_NONE = 0,

	SCREEN_MISSION,
	SCREEN_CAR,
	SCREEN_CUTCITY,		// really not used, but keep it here
	SCREEN_CUTSCENE,
	SCREEN_SCORES,
	SCREEN_TIMEOFDAY,
};

MenuSelectScreens iScreenSelect = SCREEN_NONE;

int bRedrawFrontend = 1;
int bReturnToMain = 1;

int idle_timer = 0;
int currPlayer = 1;
int feNewPad = 0;
int fePad = 0;
int ScreenDepth = 0;

int gIdleReplay = 0;

int padsConnected[2] = { 1, 0 };
int feVariableSave[4] = { -1 };

int bDrawExtra = 0;
int mainScreenLoaded = 1;
int bDoneAllready = 0;
int allowVibration = 1;
int bQuitToSystem = 0;
int bQuitToSystemSel = 0;

int carSelection = 0;
int currSelIndex = 0;
int lastCutCity = -1;
int lastCity = -1;
int currMission = -1;
int missionSetup = 0;

char* ScreenNames[12] = { 0 };

char loaded[3] = {
	-1,	// City
	-1, // Cars
	-1  // Cuts
};

PSXSCREEN* pCurrScreen = NULL;
PSXSCREEN* pNewScreen = NULL;

PSXBUTTON* pNewButton = NULL;
PSXBUTTON* pCurrButton = NULL;

PSXSCREEN* pScreenStack[10] = { 0 };
PSXBUTTON* pButtonStack[10] = { 0 };

POLY_FT4 BackgroundPolys[6];
POLY_F4 BackgroundBlack;
FE_FONT feFont;

RECT16 extraRect = { 896, 256, 64, 219 };

SPRT extraSprt;
POLY_FT3 extraDummy;

int BuildButtonsVertical(int count, int xStart, int yStart);

int FEStringWidth(char* string);
int FEPrintStringSized(char* string, int x, int y, int scale, int transparent, int r, int g, int b);
int FEPrintString(char* string, int x, int y, int justification, int r, int g, int b);

void LoadFrontendScreens(int full);
void NewSelection(short dir);
void EndFrame(void);

// [D] [T]
void SetVariable(int var)
{
	int code = (var >> 8);
	int value = (var & 0xff);

	switch (code)
	{
		case 1:
			GameLevel = value;
			break;
		case 2:
			GameType = (GAMETYPE)value;

			if (value != GAME_COPSANDROBBERS)
				gWantNight = 0;

			break;
		case 3:
			NumPlayers = (value + 1);
			break;
		case 4:
			gWantNight = value > 0;

			break;
		case 5:
			gCurrentMissionNumber = 1;
			GameType = GAME_MISSION;
			break;
		case 6:
			pScreenStack[ScreenDepth] = pCurrScreen;
			pButtonStack[ScreenDepth] = pCurrButton;

			if (value == 1) 
			{
#ifdef PSX
#else
				// [A] temporary
				// TODO: Do menu with the replays
				if(LoadReplayFromFile("Replays/CHASE.D2RP"))
				{
					StoredGameType = GameType;
					GameType = GAME_LOADEDREPLAY;
					SetState(STATE_GAMESTART);
				}
				else
#endif
				{
					SetState(STATE_INITFRONTEND);
				}
			}
			else
			{
				// [A] load configuration
				LoadCurrentProfile(0);

				//SetState(STATE_INITFRONTEND);
				SetMasterVolume(gMasterVolume);
				SetXMVolume(gMusicVolume);
			}

			break;
		case 7:
			pScreenStack[ScreenDepth] = pCurrScreen;
			pButtonStack[ScreenDepth] = pCurrButton;

#ifndef PSX
			if (value == 0) 
			{
				// [A] save configuration
				SaveCurrentProfile(1);
			}
			else 
			{
				// [A] load progress
				if(LoadCurrentGame())
				{
					GameType = GAME_CONTINUEMISSION;
					SetState(STATE_GAMESTART);
				}
			}
#endif

			break;
		case 8:
			gSubGameNumber = value;
			break;
		case 9:
			gSubtitles = (value == 0) ? 0 : 1;
			break;
		case 10:
			gInvincibleCar = value;
			ActiveCheats.cheat3 = value;
			break;
		case 11:
			gPlayerImmune = value;
			ActiveCheats.cheat4 = value;
			break;
		case 12:
			GameLevel = 3;
			gSubGameNumber = (value == 0) ? 2 : 0;
			break;
		case 13:
			GameType = GAME_IDLEDEMO;
			gCurrentMissionNumber = (value + 400);
			break;
		case FEVAR_CAMERA_ORBIT:
			gCameraOrbit = value;
			break;
#ifndef PSX
		case FEVAR_DRAW_DISTANCE:
			gDrawDistanceMode = value;
			break;
		case FEVAR_CROWD:
			gCrowdMul = crowdChoices[MAX(0, MIN(value, 4))];
			break;
		case FEVAR_CAMERA_ZOOM:
			gCameraZoom = value;
			break;
		case FEVAR_ALLCARS:
			gAllCityCars = value;
			break;
		case FEVAR_SKY:
			gSkyHd = value;
			break;
		case FEVAR_CYCLE:
			gDayCycle = value;
			break;
		case FEVAR_VOLUMETRIC:
			gSunShafts = gVolumeLights = gAtmosphere = value;
			break;
		case FEVAR_D1DLC:
			if (value == 2)
			{
				ShowSavingWaitMessage("Converting the Driver 1 cities, please wait", 0);
				D1Dlc_CheckData();
			}							// looks for the Driver 1 data once and stores the answer in config.ini
			else
				gD1DlcEnabled = value && gD1DlcCityMask != 0;	// On needs the Driver 1 data

			gD1DlcEnabled = gD1DlcEnabled && gD1DlcCityMask != 0;
			D1Dlc_SaveConfig();
			break;
		case FEVAR_D1MISSION:
			GameType = GAME_DRIVER1MISSION;
			gCurrentMissionNumber = D1_MISSION_BASE + value;
			gWantNight = 0;
			break;
#endif
#if ENABLE_BONUS_CONTENT
		case 14: // [A]
		{
			ShowBonusGallery();
			LoadFrontendScreens(0);
		}
		case 15: // [A] mini cars cheat
		{
			ActiveCheats.cheat13 = value;
		}
#endif
	}

#ifndef PSX
	// the Extended Settings are remembered at once, not only when the game is closed normally
	if (code >= FEVAR_CAMERA_ORBIT && code <= FEVAR_D1DLC && code != FEVAR_D1MISSION)
		SaveCurrentProfile(0);
#endif
}

// [D] [T]
void SetupBackgroundPolys(void)
{
	POLY_FT4 *poly;
	POLY_F4* bg;
	int i;

	for (i = 0; i < 6; i++)
	{
		poly = &BackgroundPolys[i];

		setPolyFT4(poly);
		setRGB0(poly, 128, 128, 128);
	}

	poly = &BackgroundPolys[0];
	setXYWH(poly, 0, 0, 256, 256);
	setUVWH(poly, 0, 0, 255, 255);
	setClut(poly, 960, 256);
	setTPage(poly, 0, 0, 640, 0);

	poly = &BackgroundPolys[1];
	setXYWH(poly, 256, 0, 256, 256);
	setUVWH(poly, 0, 1, 255, 254);
	setClut(poly, 960, 256);
	setTPage(poly, 0, 0, 704, 0);

	poly = &BackgroundPolys[2];
	setXYWH(poly, 512, 0, 128, 256);
	setUVWH(poly, 0, 1, 128, 254);
	setClut(poly, 960, 256);
	setTPage(poly, 0, 0, 768, 0);

	poly = &BackgroundPolys[3];
	setXYWH(poly, 0, 256, 256, 256);
	setUVWH(poly, 0, 0, 255, 255);
	setClut(poly, 960, 256);
	setTPage(poly, 0, 0, 832, 0);

	poly = &BackgroundPolys[4];
	setXYWH(poly, 256, 256, 256, 256);
	setUVWH(poly, 0, 0, 255, 255);
	setClut(poly, 960, 256);
	setTPage(poly, 0, 0, 896, 0);

	poly = &BackgroundPolys[5];
	setXYWH(poly, 512, 256, 128, 256);
	setUVWH(poly, 0, 0, 128, 255);
	setClut(poly, 960, 256);
	setTPage(poly, 0, 0, 960, 0);

	bg = &BackgroundBlack;
	setPolyF4(bg);
	setXYWH(bg, -1000, 0, 2000, 512);
	setRGB0(bg, 0, 0, 0);
}

SPRT HighlightSprt;
POLY_FT4 HighlightDummy;

// [D] [T]
void SetupScreenSprts(PSXSCREEN *pScr)
{
	setSprt(&HighlightSprt);
	setRGB0(&HighlightSprt, 128, 128, 128);
	setXY0(&HighlightSprt, 364, 198);
	setUV0(&HighlightSprt, 0, 0);
	setWH(&HighlightSprt, 256, 36);
	setClut(&HighlightSprt, 960, 258);

	setPolyFT4(&HighlightDummy);
	setXY3(&HighlightDummy, -1, -1, -1, -1, -1, -1);
	setTPage(&HighlightDummy, 0, 0, 704, 256);

	pNewScreen = NULL;
	pCurrScreen = pScr;

	if (pScr->userFunctionNum == 0 || pScr->userFunctionNum == 128)
	{
		if (pNewButton != NULL) 
		{
			pCurrButton = pNewButton;
			pNewButton = NULL;
		}
		else 
			pCurrButton = pScr->buttons;
	}
	else
	{
		if ((fpUserFunctions[pScr->userFunctionNum - 1])(1) == 0)
		{
			if (pNewButton != NULL) 
			{
				pCurrButton = pNewButton;
				pNewButton = NULL;
			}
			else
				pCurrButton = pCurrScreen->buttons;
		}
	}
}

DR_MOVE In;
DR_MOVE Out;
RECT16 storeRect = { 768, 475, 255, 36 };



// [D] [T]
void DisplayOnScreenText(void)
{
	char* text;

	sprintf(ScreenTitle, NullStr);

	if (padsConnected[0] == 0) {
		int transparent = 0;

		if (Pads[0].type == 0) {
			text = "Please insert controller into Port 1";
			transparent = 1;
		}
		else {
			if (Pads[0].type != 1)
				return;

			text = "Incompatible controller in Port 1";
		}

		FEPrintStringSized(text, 40, 400, 3072, transparent, 64, 64, 64);
	}
	else
	{
		if (iScreenSelect != SCREEN_SCORES && iScreenSelect != SCREEN_CAR)
		{
			for (int i = 0; i < ScreenDepth; i++)
			{
				if (i > 0)
					strcat(ScreenTitle, " - ");

				strcat(ScreenTitle, ScreenNames[i]);
			}

			FEPrintStringSized(ScreenTitle, 40, 400, 3072, 1, 64, 64, 64);
		}

		if (iScreenSelect == SCREEN_CUTSCENE)
		{
			text = GET_MISSION_TXT(CutSceneNames[cutSelection + CutAmountsTotal[currCity]]);

			FEPrintStringSized(text, 100, 226, 3072, 1, 64, 64, 64);
		}

		// [A] a Driver 1 car without a picture is shown by its number
		if (iScreenSelect == SCREEN_CAR && IS_DRIVER1_CITY(GameLevel) && !bDrawExtra)
		{
			char carName[32];

			sprintf(carName, "Car %d of %d", carSelection + 1, MAX(1, D1Dlc_PlayerCars(GameLevel)));
			FEPrintStringSized(carName, 100, 226, 3072, 1, 64, 64, 64);
		}
	}
}

void DrawButton(PSXBUTTON* pBtn, int i)
{
	int status = pBtn->action >> 8;

	int draw = (status != BTN_HIDDEN);

#ifndef PSX
	if (pBtn == pCurrButton)
	{
		SPRT* hghltSprt;
		POLY_FT4* hghltDummy;
		RECT16 rect;

		rect.x = pCurrButton->s_x;
		rect.y = pCurrButton->s_y;

		hghltSprt = (SPRT*)current->primptr;
		current->primptr += sizeof(SPRT);
		*hghltSprt = HighlightSprt;

		hghltDummy = (POLY_FT4*)current->primptr;
		current->primptr += sizeof(POLY_FT4);
		*hghltDummy = HighlightDummy;

		setXY0(hghltSprt, rect.x, rect.y);

		addPrim(current->ot + 6, hghltSprt);
		addPrim(current->ot + 6, hghltDummy);

		draw = 1;
	}
#endif

	if (draw)
	{
		if (status == BTN_DISABLED)
		{
			FEPrintString(pBtn->Name, pBtn->x * 2 + pBtn->w, pBtn->y, 4, 32, 32, 32);
		}
		else
		{
			if (iScreenSelect == SCREEN_MISSION && (i == 0 || i == 5) ||
				iScreenSelect == SCREEN_CAR && (i == 0 || i == 2) ||
				iScreenSelect == SCREEN_CUTSCENE && (i == 0 || i == 2) ||
				iScreenSelect == SCREEN_TIMEOFDAY && i == 2)				// [A]
			{
				FEPrintString(pBtn->Name, pBtn->x * 2 + pBtn->w, pBtn->y, 4, 124, 108, 40);
			}
			else
			{
				FEPrintString(pBtn->Name, pBtn->x * 2 + pBtn->w, pBtn->y, 4, 128, 128, 128);
			}
		}
	}
}

// [D] [T]
void DrawScreen(PSXSCREEN *pScr)
{
	char version_info[32];
	int numBtnsToDraw;
	int i;

	for (i = 0; i < 6; i++)
		addPrim(current->ot + 11, &BackgroundPolys[i]);
	addPrim(current->ot + 11, &BackgroundBlack);
	
	if (pScr)
	{
		GetTimeStamp(version_info);

		if (iScreenSelect == SCREEN_CUTCITY && (gFurthestMission < 39))
		{
			// hide the Director's Cut button
			numBtnsToDraw = pScr->numButtons - 1;
		}
		else 
		{
			numBtnsToDraw = pScr->numButtons;
		}

#ifdef PSX
		for (i = 0; i < numBtnsToDraw; i++)
			DrawButton(&pScr->buttons[i], i);
#else
		if(bQuitToSystem)
		{
			int len;
			int quitLen;

			quitLen = FEStringWidth(G_LTXT(GTXT_QuitToSystem));
			FEPrintString(G_LTXT(GTXT_QuitToSystem), 320 - quitLen / 2, 256, 0, 128, 128, 128);

			if (quitLen < 100)
				quitLen = 100;
			
			len = FEStringWidth(G_LTXT(GTXT_YES));
			FEPrintString(G_LTXT(GTXT_YES), 320 - quitLen / 4 - len / 2, 288, 0, 128, 128, bQuitToSystemSel ? 0 : 128);

			len = FEStringWidth(G_LTXT(GTXT_NO));
			FEPrintString(G_LTXT(GTXT_NO), 320 + quitLen / 4 - len/2, 288, 0, 128, 128, bQuitToSystemSel ? 128 : 0);
		}
		else
		{
			NewSelection(0);
			
			for (i = 0; i < numBtnsToDraw; i++)
				DrawButton(&pScr->buttons[i], i);
		}
#endif

		// debug version text
#if defined(_DEBUG) || defined(DEBUG_OPTIONS)
		FEPrintString(version_info, 40, 16, 0, 128, 128, 0);
		FEPrintString("--- " GAME_VERSION " ---", 320, 16, 0, 128, 128, 0);
#endif

		SetTextColour(128, 0, 0);

		DisplayOnScreenText();
		
		if (bDrawExtra) 
		{
#ifdef PSX
			addPrim(current->ot + 2, &extraSprt);
			addPrim(current->ot + 2, &extraDummy);
#else
			SPRT* extrSprt;
			POLY_FT3* extrDummy;
			
			extrSprt = (SPRT*)current->primptr;
			current->primptr += sizeof(SPRT);
			*extrSprt = extraSprt;

			extrDummy = (POLY_FT3*)current->primptr;
			current->primptr += sizeof(POLY_FT3);
			*extrDummy = extraDummy;

			addPrim(current->ot + 2, extrSprt);
			addPrim(current->ot + 2, extrDummy);
#endif
		}
	}

#ifdef PSX
	else 
	{
		EndFrame();
	}
#else

	EndFrame();

#endif
}

extern POLY_FT4 cd_sprite;

// [D] [T]
void FEDrawCDicon(void)
{
	ushort* palette;
	int i;
	RECT16 dest;

	if(!bCdIconSetup)
		return;

	cd_icon[23] = cd_icon[11];

	palette = &cd_icon[10];

	for (i = 0; i < 12; i++)
	{
		palette[1] = palette[2];
		palette++;
	}

	dest.x = 960;
	dest.y = 433;
	dest.w = 16;
	dest.h = 1;

	LoadImage(&dest, (u_long*)(cd_icon + 10));

	DrawPrim(&cd_sprite);
	DrawSync(0);
}

// [D] [T]
void SetupExtraPoly(char *fileName, int offset, int offset2)
{
	int i;
	RECT16 rect = extraRect;

	FEDrawCDicon();
	Loadfile(fileName, (char*)_frontend_buffer + offset2);

	rect.x = 896;
	rect.y = 256;
	rect.w = 64;
	rect.h = 219;
	
	LoadImage(&rect, (u_long*)(_frontend_buffer + offset2 + offset * 0x8000));
	
	DrawSync(0);
	VSync(0);

	if(bDrawExtra == 0)
	{
		setSprt(&extraSprt);
		setPolyFT3(&extraDummy);

		setXY0(&extraSprt, 100, 226);
		setRGB0(&extraSprt, 128, 128, 128);
		setUV0(&extraSprt, 0, 0);
		setWH(&extraSprt, 255, 219);
		setClut(&extraSprt, 960, 256);

		setXY3(&extraDummy, -1, -1, -1, -1, -1, -1);
		setTPage(&extraDummy, 0, 0, 896, 256);
	}

	bDrawExtra = 1;

	if (strcmp(fileName, "DATA\\CITY.RAW") == 0) 
	{
		loaded[0] = 1;
		loaded[1] = -1;
	}

	for (i = 0; i < 4; i++)
	{
		if (strcmp(fileName, gfxNames[i]) == 0)
		{
			loaded[0] = -1;
			loaded[1] = i;
			loaded[2] = -1;
		}
	}

	for (i = 0; i < 4; i++)
	{
		if (strcmp(fileName, cutGfxNames[i]) == 0) 
		{
			loaded[1] = -1;
			loaded[2] = i;
		}
	}
}

// [D] [T]
void LoadBackgroundFile(char* name)
{
	int iTpage;
	RECT16 rect;
	int i;

	iTpage = 11;

	mainScreenLoaded = (strcmp(name, "DATA\\GFX.RAW") == 0);

	rect.w = 64;
	rect.h = 256;

	for (i = 0; i < 6; i++)
	{
		FEDrawCDicon();

		LoadfileSeg(name, (char*)_other_buffer, i * 0x8000, 0x8000);
		FEDrawCDicon();

		rect.y = (i / 6);
		rect.x = (i - rect.y * 6) * 64 + 640;
		rect.y *= 256;

		LoadImage(&rect, (u_long*)_other_buffer);
		FEDrawCDicon();
	}

	LoadfileSeg(name, (char*)_other_buffer, iTpage * 0x8000, 512);
	FEDrawCDicon();

	rect.h = 1;
	rect.y = (iTpage / 6);
	rect.x = (iTpage - rect.y * 6) * 64 + 640;
	rect.y *= 256;

	LoadImage(&rect, (u_long*)_other_buffer);
	DrawSync(0);

	SetupBackgroundPolys();

	bRedrawFrontend = 1;
}

// [D] [T]
void LoadFrontendScreens(int full)
{
	int size;
	int iNumScreens;
	char* ptr;
	RECT16 rect;

	ShowLoadingScreen("GFX\\FELOAD.TIM", 1, 12);
	ShowLoading();

#ifndef USE_EMBEDDED_FRONTEND_SCREENS
	if (full)
	{
		Loadfile("DATA\\SCRS.BIN", (char*)_other_buffer);

		ptr = (char*)_other_buffer + 20; // skip header and number of screens
		iNumScreens = (int)_other_buffer[16];

		for (int i = 0; i < iNumScreens; i++)
		{
			PsxScreens[i].numButtons = *ptr++;
			PsxScreens[i].userFunctionNum = *ptr++;

			for (int j = 0; j < PsxScreens[i].numButtons; j++)
			{
				memcpy((u_char*)&PsxScreens[i].buttons[j], (u_char*)ptr, sizeof(PSXBUTTON));
				ptr += sizeof(PSXBUTTON);
			}
		}

#ifndef PSX
		// [A] SCREEN HACKS
		// Time of day extended screen
		PsxScreens[3].userFunctionNum = 22;			// TimeOfDaySelectScreen
#endif
		// for web demo content (or empty SCRS.BIN)
		if (PsxScreens[0].userFunctionNum == 128)
			PsxScreens[0].userFunctionNum = 23;		// DemoScreen

#ifndef PSX
		// replay theater
		PsxScreens[4].buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 39);
		PsxScreens[4].buttons[0].var = -1;
		PsxScreens[39].userFunctionNum = 21;		// UserReplaySelectScreen

		// Driver 1 city list (buttons are built when the screen opens)
		PsxScreens[DRIVER1_CITY_SCREEN].userFunctionNum = 25;	// Driver1CitySelectScreen

		// Driver 1 missions: the Story screen gets a Driver 1 button, then a city list and a mission list (buttons are built when a screen opens)
		PsxScreens[2].userFunctionNum = 33;						// StoryScreen
		PsxScreens[D1MISSION_CITY_SCREEN].userFunctionNum = 34;	// Driver1MissionCityScreen
		PsxScreens[D1MISSION_LIST_SCREEN].userFunctionNum = 35;	// Driver1MissionListScreen
		PsxScreens[CYCLE_SCREEN] = PsxScreens[31];
		PsxScreens[CYCLE_SCREEN].userFunctionNum = 36;			// DayCycleScreen

		// camera orbit On/Off, same layout as the Immunity and Mini Cars screens
		PsxScreens[CAMERA_ORBIT_SCREEN] = PsxScreens[31];
		PsxScreens[CAMERA_ORBIT_SCREEN].userFunctionNum = 26;	// CameraOrbitOnOffScreen
		PsxScreens[CAMERA_ORBIT_SCREEN].buttons[0].var = FE_MAKEVAR(FEVAR_CAMERA_ORBIT, 1);
		PsxScreens[CAMERA_ORBIT_SCREEN].buttons[1].var = FE_MAKEVAR(FEVAR_CAMERA_ORBIT, 0);

		// extended settings: the list and its three way option screens (buttons are built when a screen opens)
		PsxScreens[EXTENDED_SCREEN] = PsxScreens[31];
		PsxScreens[EXTENDED_SCREEN].userFunctionNum = 27;		// ExtendedOptionsScreen
		PsxScreens[DRAW_DISTANCE_SCREEN] = PsxScreens[31];
		PsxScreens[DRAW_DISTANCE_SCREEN].userFunctionNum = 28;	// DrawDistanceScreen
		PsxScreens[CROWD_SCREEN] = PsxScreens[31];
		PsxScreens[CROWD_SCREEN].userFunctionNum = 29;			// CrowdScreen
		PsxScreens[CAMERA_ZOOM_SCREEN] = PsxScreens[31];
		PsxScreens[CAMERA_ZOOM_SCREEN].userFunctionNum = 30;	// CameraZoomScreen
		PsxScreens[ALLCARS_SCREEN] = PsxScreens[31];
		PsxScreens[ALLCARS_SCREEN].userFunctionNum = 31;		// AllCarsScreen
		PsxScreens[SKY_SCREEN] = PsxScreens[31];
		PsxScreens[SKY_SCREEN].userFunctionNum = 32;			// SkyScreen

		PsxScreens[VOLUMETRIC_SCREEN] = PsxScreens[31];
		PsxScreens[VOLUMETRIC_SCREEN].userFunctionNum = 37;	// VolumetricEffectsScreen

		PsxScreens[D1DLC_SCREEN] = PsxScreens[31];
		PsxScreens[D1DLC_SCREEN].userFunctionNum = 38;		// Driver1DlcScreen
#endif // PSX

#if ENABLE_BONUS_CONTENT
		 // make mini cars cheat screen
		PsxScreens[40] = PsxScreens[31];
		PsxScreens[40].userFunctionNum = 24;		// MiniCarsOnOffScreen
		PsxScreens[40].buttons[0].var = FE_MAKEVAR(15, 1);
		PsxScreens[40].buttons[1].var = FE_MAKEVAR(15, 0);
#endif
	}
#endif

	rect.w = 64;
	rect.h = 256;

	ShowLoading();
	LoadBackgroundFile("DATA\\GFX.RAW");

	for (int i = 0; i < 2; i++)
	{
		// [A] optimized. Before it was to wasteful to load 16 sectors
		int loadSize = i == 0 ? 0x8000 : (36 * 128);
		rect.h = i == 0 ? 256 : 36;
		
		ShowLoading();
		LoadfileSeg("DATA\\GFX.RAW", (char*)_other_buffer, 0x30000 + (i * 0x8000), loadSize);

		rect.x = 640 + (i * 64);
		rect.y = 256;

		LoadImage(&rect, (u_long*)_other_buffer);
		DrawSync(0);
	}

	// Load clut
	ShowLoading();
	LoadfileSeg("DATA\\GFX.RAW", (char*)_other_buffer, 0x58000, /*0x8000*/ 512);

	rect.x = 960;
	rect.y = 256;
	rect.h = 4;

	LoadImage(&rect, (u_long*)_other_buffer);
	DrawSync(0);

	if (full)
	{
		Loadfile("DATA\\FEFONT.BNK", (char*)_other_buffer);
		memcpy((u_char*)&feFont, (u_char*)_other_buffer, sizeof(feFont));
	}
}

// [D] [T]
void ReInitScreens(int returnToMain)
{
#ifndef PSX
	bReturnToMain = returnToMain;

	if(bReturnToMain)
	{
		bCdIconSetup = 0;
		iScreenSelect = SCREEN_NONE;

		cutSelection = 0;
		currCity = 0;

		bRedrawFrontend = 0;

		idle_timer = 0;
		currPlayer = 1;
		feNewPad = 0;
		fePad = 0;
		ScreenDepth = 0;

		loaded[0] = -1;
		loaded[1] = -1;
		loaded[2] = -1;
	
		padsConnected[0] = 1;
		padsConnected[1] = 0;

		bDrawExtra = 0;
		mainScreenLoaded = 1;
		bDoneAllready = 0;
		allowVibration = 1;

		ScreenNames[0] = 0;
		gInFrontend = 1;
		currMission = -1;
		missionSetup = 0;
	}

	extern void SwitchMappings(int menu);
	SwitchMappings(1);
#endif // !PSX

	bCdIconSetup = 0;
	InitCdIcon();

	if (bReturnToMain)
	{
		ScreenDepth = 0;
		NumPlayers = 1;
		gWantNight = 0;
		gNight = 0;
		gTimeOfDay = TIME_DAY;
		gSubGameNumber = 0;

		feVariableSave[3] = -1;
		feVariableSave[2] = -1;
		feVariableSave[1] = -1;
		feVariableSave[0] = -1;

		if (!mainScreenLoaded)
		{
			bDrawExtra = 0;
			LoadBackgroundFile("DATA\\GFX.RAW");
		}
	}
	else 
	{
		bReturnToMain = 1;
	}

	pCurrScreen = pScreenStack[ScreenDepth];
	pNewButton = pButtonStack[ScreenDepth];

	// [A] state hack for demo
	if (pCurrScreen == NULL)
		pCurrScreen = &PsxScreens[0];

	SetupScreenSprts(pCurrScreen);
	SetupBackgroundPolys();

	idle_timer = VSync(-1);
}

// [D] [T]
void NewSelection(short dir)
{
	PSXBUTTON *pNewB;
	RECT16 rect;

	if (pCurrScreen->numButtons == 0)
		return;

	pNewB = pCurrButton;
	int btn = 0;

	// any buttons pressed?
	if (dir != 0)
	{
#ifdef PSX
		SetDrawMove(&In, &storeRect, pCurrButton->s_x, pCurrButton->s_y);
		addPrim(current->ot + 9, &In);
#endif

		if ((dir & MPAD_D_UP) != 0)
		{
			btn = pCurrButton->u;
		}
		else if ((dir & MPAD_D_DOWN) != 0)
		{
			btn = pCurrButton->d;
		}
		else if ((dir & MPAD_D_LEFT) != 0)
		{
			btn = pCurrButton->l;
		}
		else if ((dir & MPAD_D_RIGHT) != 0)
		{
			btn = pCurrButton->r;
		}

		if (btn != 0)
		{
			FESound(3);
			pNewB = &pCurrScreen->buttons[btn - 1];
		}
	}

	pCurrButton = pNewB;

	// show who has control
	if (NumPlayers == 2 && currPlayer != 1)
		FEPrintStringSized(G_LTXT(GTXT_Player2), 400, 260, 3072, 0, 128, 128, 128);

#ifdef PSX
	rect.x = pCurrButton->s_x;
	rect.y = pCurrButton->s_y;
	rect.w = 255;
	rect.h = 36;

	SetDrawMove(&Out, &rect, storeRect.x, storeRect.y);
	addPrim(current->ot + 8, &Out);

	setXY0(&HighlightSprt, rect.x, rect.y);

	addPrim(current->ot + 6, &HighlightSprt);
	addPrim(current->ot + 6, &HighlightDummy);

	DrawButton(pNewB, pNewB - pCurrScreen->buttons);

	EndFrame();
#endif
}

// [D] [T]
int HandleKeyPress(void)
{
	if (pCurrScreen == NULL || pCurrButton == NULL)
		return 0;

	if (pCurrScreen->userFunctionNum != 0)
	{
		// notify the user function first
		if ((fpUserFunctions[pCurrScreen->userFunctionNum - 1])(0) != 0)
		{
			// user function handled the key press
			feNewPad = 0;
		}
	}

	if (feNewPad & MPAD_CROSS)
	{	
		int action = pCurrButton->action >> 8;

		if (action != BTN_DISABLED)
		{
			FESound(2);

			if (pCurrButton->var != -1)
				SetVariable(pCurrButton->var);

			switch (action)
			{
				case BTN_NEXT_SCREEN:
					pScreenStack[ScreenDepth] = pCurrScreen;
					pButtonStack[ScreenDepth] = pCurrButton;

					ScreenNames[ScreenDepth] = pCurrButton->Name;

					pNewScreen = &PsxScreens[pCurrButton->action & 0xFF];

					if (ScreenDepth < 10)
						ScreenDepth++;

					break;
				case BTN_START_GAME:
					if (NumPlayers == 2 && iScreenSelect == SCREEN_CAR && (currPlayer == 2))
					{
						(fpUserFunctions[pCurrScreen->userFunctionNum - 1])(1);
						bRedrawFrontend = 1;
					}
					else
					{
						pScreenStack[ScreenDepth] = pCurrScreen;
						pButtonStack[ScreenDepth] = pCurrButton;

						ScreenNames[ScreenDepth] = pCurrButton->Name;

						SetState(STATE_GAMESTART);
					}
					break;
				case BTN_PREVIOUS_SCREEN:
					if (ScreenDepth > 0)
					{
						if (--ScreenDepth == 0)
							NumPlayers = 1;

						pNewScreen = pScreenStack[ScreenDepth];
						pNewButton = pButtonStack[ScreenDepth];
					}
					break;
			}
		}
	}
	else if (feNewPad & MPAD_TRIANGLE)
	{
		if (ScreenDepth > 0)
		{
			if (!bDoneAllready)
				FESound(0);
			else 
				bDoneAllready = 0;

			if (--ScreenDepth == 0)
			{
				gWantNight = 0;
				gSubGameNumber = 0;
				NumPlayers = 1;
			}

			pNewScreen = pScreenStack[ScreenDepth];
			pNewButton = pButtonStack[ScreenDepth];
		}
	}
	else
	{
		// any d-pad buttons pressed?
		if ((feNewPad & (MPAD_D_UP | MPAD_D_RIGHT | MPAD_D_DOWN | MPAD_D_LEFT)) != 0)
		{
			NewSelection(feNewPad);
		}
		else
		{
			// button pressed, but we didn't handle it
			return 1;
		}
	}

	idle_timer = VSync(-1);

	return 1;
}


#if defined(DEBUG_OPTIONS) && !defined(PSX)
extern void PsyX_TakeScreenshot();
char gFeSteps[64];	// -festeps "EDDDEESS": scripted frontend input for tests. E select, D down, U up, T back, S screenshot FESHOT<n>.BMP

static void FeScriptedInput(void)
{
	static int frame = 0, pos = 0, shots = 0;
	char name[32];

	if (!gFeSteps[pos] || ++frame % 120 != 0)
		return;

	switch (gFeSteps[pos++])
	{
		case 'E': feNewPad |= MPAD_CROSS; break;
		case 'D': feNewPad |= MPAD_D_DOWN; break;
		case 'U': feNewPad |= MPAD_D_UP; break;
		case 'T': feNewPad |= MPAD_TRIANGLE; break;
		case 'S':
			PsyX_TakeScreenshot();
			sprintf(name, "FESHOT%d.BMP", shots++);
			remove(name);
			rename("SCREENSHOT.BMP", name);
			break;
	}
}
#endif

// [D] [T]
void PadChecks(void)
{
	int i;
	// TODO: null check for pCurrScreen

	int oldnum = numPadsConnected;

	ReadControllers();

	numPadsConnected = 0;
	feNewPad = Pads[0].mapnew;
	fePad = Pads[0].mapped;

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	FeScriptedInput();
#endif

	for (int i = 0; i < 2; i++) 
	{
		if (Pads[i].type < 2) 
		{
			padsConnected[i] = 0;
		}
		else 
		{
			padsConnected[i] = 1;
			numPadsConnected++;
		}
	}

#ifndef PSX
	// [A] quit to system
	if(feNewPad & MPAD_TRIANGLE)
	{
		if(ScreenDepth == 0)
		{
			bQuitToSystem ^= 1;
			FESound(0);
		}
	}

	if(bQuitToSystem)
	{
		if (feNewPad & MPAD_CROSS)
		{
			if(bQuitToSystemSel == 1)
				bQuitToSystem = 2;
			else
				bQuitToSystem = 0;

			FESound(2);
		}
		else if ((feNewPad & MPAD_D_LEFT) || (feNewPad & MPAD_D_RIGHT))
		{
			bQuitToSystemSel += 1;
			bQuitToSystemSel &= 1;

			FESound(3);
		}

		feNewPad = 0;
	}
#endif

	if (oldnum != numPadsConnected && (oldnum == 2 || numPadsConnected == 2) && ScreenDepth == 0) 
	{
		bRedrawFrontend = 1;
		MainScreen(1);
	}

	if ((Pads[0].dualshock == 0 || padsConnected[0] == 0) &&
		(Pads[1].dualshock == 0 || padsConnected[1] == 0)) 
	{
		if(allowVibration == 1)
		{
			allowVibration = 0;

			if (pCurrScreen->userFunctionNum == 18)
			{
				bRedrawFrontend = 1;
				GamePlayScreen(1);
			}
		}
	}
	else 
	{
		if (allowVibration == 0)
		{
			allowVibration = 1;

			if (pCurrScreen->userFunctionNum == 18)
			{
				bRedrawFrontend = 1;
				GamePlayScreen(1);
			}
		}
	}

	if (oldnum != numPadsConnected && NumPlayers == 2 && numPadsConnected != NumPlayers || 
		numPadsConnected == 0 || 
		padsConnected[0] == 0) 
	{
		bReturnToMain = 1;
		bRedrawFrontend = 1;
		feNewPad = 0x10;

		if (pCurrScreen->userFunctionNum != 0) 
		{
			(fpUserFunctions[pCurrScreen->userFunctionNum - 1])(0);
		}

		feNewPad = 0;

		if (ScreenDepth != 0) 
		{
			ReInitScreens(bReturnToMain);
		}
	}
	if (bRedrawFrontend == 0 && numPadsConnected != oldnum &&
		(gInFrontend != 0 && (pCurrScreen != NULL && pCurrScreen->userFunctionNum != 0)))
	{
		(fpUserFunctions[pCurrScreen->userFunctionNum - 1])(1);
		bRedrawFrontend = 1;
	}
}

// [D] [T]
void SetFEDrawMode(void)
{
	SetVideoMode(video_mode);

	SetDefDrawEnv(&MPBuff[0][0].draw, draw_mode.x1, draw_mode.y1, 640, 512);
	SetDefDispEnv(&MPBuff[0][0].disp, draw_mode.x1, draw_mode.y1, 640, 512);
	SetDefDrawEnv(&MPBuff[0][1].draw, draw_mode.x1, draw_mode.y1, 640, 512);
	SetDefDispEnv(&MPBuff[0][1].disp, draw_mode.x1, draw_mode.y1, 640, 512);

	MPBuff[0][0].draw.isbg = 0;
#ifdef PSX
	MPBuff[0][0].disp.isinter = 1;
#endif
	MPBuff[0][0].draw.dfe = 1;
	MPBuff[0][0].disp.screen.h = 256;
	MPBuff[0][0].primtab = (char*)_primTab1;
	MPBuff[0][0].primptr = (char*)_primTab1;
	MPBuff[0][0].ot = (OTTYPE*)_OT1;

	MPBuff[0][1].draw.isbg = 0;
#ifdef PSX
	MPBuff[0][1].disp.isinter = 1;
#endif
	MPBuff[0][1].draw.dfe = 1;
	MPBuff[0][1].disp.screen.h = 256;
	MPBuff[0][1].primtab = (char*)_primTab2;
	MPBuff[0][1].primptr = (char*)_primTab2;
	MPBuff[0][1].ot = (OTTYPE*)_OT2;

	last = &MPBuff[0][1];
	current = &MPBuff[0][0];

	MPBuff[0][0].disp.screen.x = draw_mode.framex * 2;
	MPBuff[0][0].disp.screen.y = draw_mode.framey;
	MPBuff[0][1].disp.screen.y = draw_mode.framey;
	MPBuff[0][1].disp.screen.x = MPBuff[0][0].disp.screen.x;

	PutDispEnv(&MPBuff[0][0].disp);
	PutDrawEnv(&current->draw);
}

// [A] - was inlined in State_FrontEnd
void InitFrontend(void)
{
	InitCdIcon();
#ifdef HIRES_FONTS
	InitHiresFEFont();
#endif

	ResetGraph(1);
	SetDispMask(0);

	bRedrawFrontend = 0;
	gInFrontend = 1;

	idle_timer = VSync(-1);

	LoadFrontendScreens(1);

	SetupBackgroundPolys();
	SetupScreenSprts(&PsxScreens[0]);

	// load frontend bank
	LoadBankFromLump(SOUND_BANK_SFX, SBK_ID_MENU);
}

// [A] - was inlined in State_FrontEnd
void InitFrontendDisplay(void)
{
	SetDispMask(0);
	ResetGraph(0);

	SetFEDrawMode();

	EnableDisplay();

#ifdef PSX
	DrawScreen(pCurrScreen);
	EndFrame();

	NewSelection(0);

	// REALLY make sure the screen is cleared
	EndFrame();
	EndFrame();
	EndFrame();
	EndFrame();
	EndFrame();
	EndFrame();
#endif

	SetDispMask(1);
}

// [D] [T]
void State_FrontEnd(void* param)
{
	PadChecks();

	if (currPlayer == 2)
	{
		if (Pads[1].type < 2)
		{
			feNewPad = ((feNewPad & MPAD_TRIANGLE) != 0) ? MPAD_TRIANGLE : 0;
		}
		else 
		{
			feNewPad = Pads[1].mapnew;
		}
	}

	if (HandleKeyPress())
	{
		if (pNewScreen != NULL)
		{
			SetupScreenSprts(pNewScreen);
			bRedrawFrontend = 1;
		}
	}

#ifndef PSX
	DrawScreen(pCurrScreen);
#else
	if (bRedrawFrontend)
	{
		DrawSync(0);
		EndFrame();

		DrawScreen(pCurrScreen);

		EndFrame();

		NewSelection(0);

		bRedrawFrontend = 0;
	}
#endif

	if ((VSync(-1) - idle_timer) > 1800)
	{
		if (ScreenDepth == 0)
		{
			GameType = GAME_IDLEDEMO;

			gCurrentMissionNumber = gIdleReplay + 400;
			
			if (++gIdleReplay == 4)
				gIdleReplay = 0;

			pScreenStack[0] = pCurrScreen;
			pButtonStack[0] = pCurrButton;
			ScreenNames[0] = pCurrButton->Name;

			SetState(STATE_GAMESTART);

			pCurrScreen = pScreenStack[0];

			bRedrawFrontend = 1;
			ScreenDepth = 0; // fail-safe?
		}

		idle_timer = VSync(-1);
	}

	if(bQuitToSystem == 2)
		SetState(STATE_NONE);
}

// [D] [T]
void EndFrame(void)
{
	DB *db_hold;

	DrawSync(0);
	VSync(0);

	PutDispEnv(&current->disp);
	PutDrawEnv(&current->draw);

	db_hold = current;
	current = last;

	last->primptr = last->primtab;
	last = db_hold;

	DrawOTag((u_long*)(db_hold->ot + FE_OTSIZE - 1));

	ClearOTagR((u_long*)current->ot, FE_OTSIZE);
	VSync(0);

#ifndef PSX
	PsyX_EndScene();
#endif
}

// [A]
int FEStringWidth(char* string)
{
	char* pString = string;
	u_char c = 0;
	int w = 0;

#ifdef HIRES_FONTS
	if (gHiresFEFontTexture)
	{
		return FEStringWidthHires(string);
	}
#endif

	while ((c = *pString++) != 0)
	{
		if (c == ' ')
			w += 4;
		else
			w += feFont.CharInfo[c].w;
	}

	return w;
}

// [D] [T]
int FEPrintString(char *string, int x, int y, int justification, int r, int g, int b)
{
	if (current == NULL || string == NULL)
		return -1;

#ifdef HIRES_FONTS
	if (gHiresFEFontTexture)
	{
		return FEPrintStringHires(string, x, y, justification, r, g, b);
	}
#endif

	FE_CHARDATA *pFontInfo;
	SPRT *font;
	u_char let;

	if (justification & 4)
	{
		x -= FEStringWidth(string);
	}

	font = (SPRT*)current->primptr;

	int counter = 0;

	while ((let = *string++) != 0)
	{
		if (let == ' ')
		{
			x += 4;
		}
		else
		{
			pFontInfo = &feFont.CharInfo[let];

			setSprt(font);
			setSemiTrans(font, 1);

			setRGB0(font, r, g, b);
			setXY0(font, x, y);
			setUV0(font, pFontInfo->u, pFontInfo->v);
			setWH(font, pFontInfo->w, pFontInfo->h);
			// setTPage(font, 0, 0, 640, 256); // [A]
			setClut(font, 960, 257); // [A] seems clut has a transparency bit; width is 256

			addPrim(current->ot + 1, font);
			font++;

			// add space for next character
			x += pFontInfo->w;
		}

		if (++counter >= 32)
			break;
	}

	// set tail
	current->primptr = (char *)font;

	POLY_FT3 *null = (POLY_FT3 *)current->primptr;
	setPolyFT3(null);
	setSemiTrans(null, 1);
	setXY3(null, -1, -1, -1, -1, -1, -1);
	setTPage(null, 0, 0, 640, 256);

	addPrim(current->ot + 1, null);
	null++;

	current->primptr = (char *)null;

	return x;
}


// [D] [T]
int FEPrintStringSized(char *string, int x, int y, int scale, int transparent, int r, int g, int b)
{
	if (current == NULL || string == NULL)
		return -1;

#ifdef HIRES_FONTS
	if (gHiresFEFontTexture)
	{
		return FEPrintStringSizedHires(string, x, y, scale, transparent, r, g, b);
	}
#endif

	POLY_FT4 *font;
	FE_CHARDATA *pFontInfo;
	u_char let;
	int w;
	int h;

	font = (POLY_FT4 *)current->primptr;

	while ((let = *string++) != 0)
	{
		if (let == '\n')
			continue;

		if (let == ' ')
		{
			// add space
			x += 4;
		}
		else
		{
			pFontInfo = &feFont.CharInfo[let];

			w = (pFontInfo->w * scale) / 4096;
			h = (pFontInfo->h * scale) / 4096;

			setPolyFT4(font);
			setSemiTrans(font, transparent);

			setRGB0(font, 128, 128, 128);
			setUVWH(font, pFontInfo->u, pFontInfo->v, pFontInfo->w - 1, pFontInfo->h - 1);
			setXYWH(font, x, y, w, h);
			setRGB0(font, r, g, b);
			setTPage(font, 0, 0, 640, 256);
			setClut(font, 960, 257); // [A] seems clut has a transparency bit; width is 256

			addPrim(current->ot + 1, font);
			font++;

			// make room for next character
			x += w;
		}
	}

	// set tail
	current->primptr = (char *)font;

	return x;
}


SCREEN_LIMITS screen_limits[2] = {
	// min(x/y), max(x/y)
	{ -27,   5, 35, 37 },
	{ -32, -11, 32, 21 },
};

int bScreenSetup = 0;

// [D] [T]
int CentreScreen(int bSetup)
{
	if (bSetup)
		return 0;

#if defined(DEBUG) && !defined(PSX)
	char text[32];

	sprintf(text, "X1: %d, Y1: %d", current->disp.screen.x, current->disp.screen.y);
	FEPrintStringSized(text, 25, 50, 3072, 0, 128, 0, 0);

	sprintf(text, "X2: %d, Y2: %d", last->disp.screen.x, last->disp.screen.y);
	FEPrintStringSized(text, 25, 75, 3072, 0, 128, 0, 0);
#endif

	if (feNewPad & MPAD_CROSS)
	{
		draw_mode_pal.framex = current->disp.screen.x / 2;
		draw_mode_pal.framey = current->disp.screen.y;
		draw_mode_ntsc.framex = current->disp.screen.x / 2;
		draw_mode_ntsc.framey = current->disp.screen.y;
	}
	else if (feNewPad & MPAD_TRIANGLE)
	{
		current->disp.screen.x = draw_mode.framex * 2;
		current->disp.screen.y = draw_mode.framey;
		last->disp.screen.x = draw_mode.framex * 2;
		last->disp.screen.y = draw_mode.framey;
	}
	else
	{
		bool done = false;

		if (feNewPad & MPAD_D_UP)
		{
			if (current->disp.screen.y >= screen_limits[video_mode].miny)
			{
				current->disp.screen.y--;
				last->disp.screen.y--;
				done = true;
			}
		}
		else if (feNewPad & MPAD_D_DOWN)
		{
			if (current->disp.screen.y <= screen_limits[video_mode].maxy)
			{
				current->disp.screen.y++;
				last->disp.screen.y++;
				done = true;
			}
		}
		else if (feNewPad & MPAD_D_LEFT)
		{
			if (current->disp.screen.x >= screen_limits[video_mode].minx)
			{
				current->disp.screen.x--;
				last->disp.screen.x--;
				done = true;
			}
		}
		else if (feNewPad & MPAD_D_RIGHT)
		{
			if (current->disp.screen.x <= screen_limits[video_mode].maxx)
			{
				current->disp.screen.x++;
				last->disp.screen.x++;
				done = true;
			}
		}
		else
		{
			// unhandled button press
			return 0;
		}

		FESound((done) ? 3 : 1);
	}

	return 0;
}


// [A] a Driver 1 city offers its own player cars (selection 0 is the first one), no pictures yet: the screen stays empty
static int CarSelectable(int carCity, int sel)
{
	if (IS_DRIVER1_CITY(GameLevel))
		return sel < MAX(1, D1Dlc_PlayerCars(GameLevel));		// a city without player cars offers one civilian car

	return CarAvailability[carCity][sel] != 0;
}

static int CarSelectionModel(int carCity, int sel)
{
	if (IS_DRIVER1_CITY(GameLevel))
		return D1Dlc_PlayerCars(GameLevel) > 0 ? FIRST_D1_PLAYER_CAR + sel : 1;

	return carNumLookup[carCity][sel];
}

// [D] [T]
int CarSelectScreen(int bSetup)
{
	int newSel;
	RECT16 rect;

	// Driver 1 cities offer the Chicago cars until their own car park exists (Part 6)
	int carCity = IS_DRIVER1_CITY(GameLevel) ? 0 : GameLevel;

	newSel = carSelection;
	rect = extraRect;

	if (bSetup)
	{
		iScreenSelect = SCREEN_CAR;

		// setup secret cars
		if (NumPlayers == 1)
		{
			CarAvailability[0][9] = AvailableCheats.cheat5;
			CarAvailability[1][9] = AvailableCheats.cheat6;
			CarAvailability[2][9] = AvailableCheats.cheat7;
			CarAvailability[3][9] = AvailableCheats.cheat8;
		}

		// setup unlockable cars
		if (gFurthestMission == 40 && NumPlayers == 1)
		{
			for (int i = 4; i < 9; i++)
			{
#if USE_PC_FILESYSTEM
				// [A] minimal requirement for this car to be selectable
				extern int gContentOverride;
				if (i == 8)
					CarAvailability[0][i] = gContentOverride && FileExists("LEVELS\\CHICAGO\\CARMODEL_11_clean.MDL");	// remove truck
				else
					CarAvailability[0][i] = 1;
#else
				if (i != 8)
					CarAvailability[0][i] = 1;	// remove truck
#endif
				CarAvailability[1][i] = 1;
				CarAvailability[2][i] = 1;
				CarAvailability[3][i] = 1;
			}
		}
		else
		{
			for (int i = 4; i < 9; i++)
			{
				// unlock the cop car only
				int unlocked = (i == 4) && gFurthestMission == 40;

				CarAvailability[0][i] = unlocked; 
				CarAvailability[1][i] = unlocked;
				CarAvailability[2][i] = unlocked;
				CarAvailability[3][i] = unlocked;
			}
		}

#ifdef PSX
		if (currPlayer != 1) 
			return 0;
#endif

		LoadBackgroundFile("DATA\\CARS\\CARBACK.RAW");

		if (IS_DRIVER1_CITY(GameLevel))
		{
			carSelection = (feVariableSave[0] >= 0 && feVariableSave[0] < D1Dlc_PlayerCars(GameLevel)) ? feVariableSave[0] : 0;
			bDrawExtra = 0;
		}
		else if (feVariableSave[0] != -1)
		{
			carSelection = feVariableSave[0];
			SetupExtraPoly(gfxNames[carCity], carSelection, 0);
		}
		else
		{
			carSelection = 0;

			if (loaded[1] == carCity)
			{
				bDrawExtra = 1;

				LoadImage(&rect, (u_long*)_frontend_buffer);
				DrawSync(0);
			}
			else
			{
				SetupExtraPoly(gfxNames[carCity], carSelection, 0);
				lastCity = carCity;
			}
		}

		feVariableSave[0] = -1;
		feVariableSave[1] = -1;
		feVariableSave[2] = -1;
		feVariableSave[3] = -1;

		lastCutCity = -1;
		currSelIndex = 1;

		pCurrButton = &pCurrScreen->buttons[1];

		return 1;
	}

	if (feNewPad & MPAD_TRIANGLE)
	{
		FESound(0);
		bDoneAllready = 1;
#ifdef PSX
		LoadBackgroundFile("DATA\\GFX.RAW");
		bDrawExtra = 0;
#else
		LoadBackgroundFile("DATA\\CITYBACK.RAW");

		if (loaded[0] == -1)
		{
			SetupExtraPoly("DATA\\CITY.RAW", currCity, 0);
		}
		else
		{
			bDrawExtra = 1;

			RECT16 rect = extraRect;
			LoadImage(&rect, (u_long*)(_frontend_buffer + currCity * 0x8000));
			DrawSync(0);
		}
#endif
		currPlayer = 1;
		iScreenSelect = SCREEN_NONE;
	}

	else if (feNewPad & MPAD_CROSS)
	{
		if (currSelIndex == 0)
		{
			// find best-fit for previous vehicle
			do
			{
				newSel--;
				if (newSel < 0)
					newSel = 9;

			} while (!CarSelectable(carCity, newSel));

			carSelection = newSel;
		}
		else if (currSelIndex == 2)
		{
			// find best-fit for next vehicle
			do
			{
				newSel++;
				if (newSel > 9)
					newSel = 0;

			} while (!CarSelectable(carCity, newSel));

			carSelection = newSel;
		}
		else
		{
			// select the vehicle
			if (currPlayer == 1)
			{
				feVariableSave[0] = carSelection;
				wantedCar[0] = CarSelectionModel(carCity, carSelection);
			}
			else {
				wantedCar[1] = CarSelectionModel(carCity, carSelection);
			}

			// time for player 2 to select their vehicle?
			if (NumPlayers == 2)
				currPlayer++;

			return 0;
		}
		
		rect = extraRect;
		if (!IS_DRIVER1_CITY(GameLevel))
			LoadImage(&rect, (u_long*)(_frontend_buffer + carSelection * 0x8000));
		DrawSync(0);

#ifdef PSX
		DisplayOnScreenText();

		addPrim(current->ot + 2, &extraSprt);
		addPrim(current->ot + 2, &extraDummy);

		EndFrame();
#endif
	}
	else if (feNewPad & MPAD_D_UP)
	{
		currSelIndex = pCurrButton->u - 1;
	}
	else if (feNewPad & MPAD_D_DOWN)
	{
		currSelIndex = pCurrButton->d - 1;
	}

	return 0;
}

// [D] [T]
int CopDiffLevelScreen(int bSetup)
{
	if (bSetup) 
	{
		pCurrButton = &pCurrScreen->buttons[gCopDifficultyLevel];
		return 1;
	}

	if (feNewPad & MPAD_CROSS)
	{
		gCopDifficultyLevel = currSelIndex;
	}
	else if (feNewPad & MPAD_D_UP)
	{
		currSelIndex = pCurrButton->u - 1;
	}
	else if (feNewPad & MPAD_D_DOWN)
	{
		currSelIndex = pCurrButton->d - 1;
	}

	return 0;
}

// [D] [T]
int VibroOnOffScreen(int bSetup)
{
	if (bSetup) 
	{
		currSelIndex = (gVibration ^ 1);
		pCurrButton = &pCurrScreen->buttons[currSelIndex];
		return 1;
	}

	if (feNewPad & MPAD_CROSS)
	{
		gVibration = (currSelIndex ^ 1);
	}
	else if (feNewPad & MPAD_D_UP)
	{
		currSelIndex = pCurrButton->u - 1;
	}
	else if (feNewPad & MPAD_D_DOWN)
	{
		currSelIndex = pCurrButton->d - 1;
	}

	return 0;
}


// [D] [T]
int MissionSelectScreen(int bSetup)
{
	int i;

	if (bSetup) 
	{
		iScreenSelect = SCREEN_MISSION;

		if (!missionSetup)
		{
			currMission = minmaxSelections[currCity][0];
			currSelIndex = 0;

			if (GameType == GAME_REPLAYMISSION)
				LoadBackgroundFile("DATA\\CITYBACK.RAW");
		}

		if (feVariableSave[0] != -1) 
		{
			currMission = feVariableSave[0];
			currSelIndex = feVariableSave[1];
			currCity = feVariableSave[2];
		}

		int usedB = 0;

		bool done = false;
		bool bP = false; // 'Previous' button is visible?
		bool bN = false; // 'Next' button is visible?

		for (i = 0; (i < 4) && !done; i++)
		{
			if (botch[currMission + i].missNum > gFurthestMission ||
				currMission + i >= minmaxSelections[currCity][1] ||
				currMission + i > 36) 
			{
				done = true;
			}
			else
			{
				usedB++;
			}
		}

		switch (usedB)
		{
		case 1:
			pCurrScreen->buttons[1].u = 2;
			pCurrScreen->buttons[1].d = 2;
			sprintf(pCurrScreen->buttons[1].Name, GET_MISSION_TXT(MissionName[currMission]));
			sprintf(pCurrScreen->buttons[2].Name, NullStr);
			sprintf(pCurrScreen->buttons[3].Name, NullStr);
			sprintf(pCurrScreen->buttons[4].Name, NullStr);
			break;
		case 2:
			pCurrScreen->buttons[1].u = 3;
			pCurrScreen->buttons[1].d = 3;
			pCurrScreen->buttons[2].u = 2;
			pCurrScreen->buttons[2].d = 2;
			sprintf(pCurrScreen->buttons[1].Name, GET_MISSION_TXT(MissionName[currMission]));
			sprintf(pCurrScreen->buttons[2].Name, GET_MISSION_TXT(MissionName[currMission + 1]));
			sprintf(pCurrScreen->buttons[3].Name, NullStr);
			sprintf(pCurrScreen->buttons[4].Name, NullStr);
			break;
		case 3:
			pCurrScreen->buttons[1].u = 4;
			pCurrScreen->buttons[1].d = 3;
			pCurrScreen->buttons[2].u = 2;
			pCurrScreen->buttons[2].d = 4;
			pCurrScreen->buttons[3].u = 3;
			pCurrScreen->buttons[3].d = 2;
			sprintf(pCurrScreen->buttons[1].Name, GET_MISSION_TXT(MissionName[currMission]));
			sprintf(pCurrScreen->buttons[2].Name, GET_MISSION_TXT(MissionName[currMission + 1]));
			sprintf(pCurrScreen->buttons[3].Name, GET_MISSION_TXT(MissionName[currMission + 2]));
			sprintf(pCurrScreen->buttons[4].Name, NullStr);
			break;
		case 4:
			pCurrScreen->buttons[1].u = 5;
			pCurrScreen->buttons[1].d = 3;
			pCurrScreen->buttons[2].u = 2;
			pCurrScreen->buttons[2].d = 4;
			pCurrScreen->buttons[3].u = 3;
			pCurrScreen->buttons[3].d = 5;
			pCurrScreen->buttons[4].u = 4;
			pCurrScreen->buttons[4].d = 2;
			sprintf(pCurrScreen->buttons[1].Name, GET_MISSION_TXT(MissionName[currMission]));
			sprintf(pCurrScreen->buttons[2].Name, GET_MISSION_TXT(MissionName[currMission + 1]));
			sprintf(pCurrScreen->buttons[3].Name, GET_MISSION_TXT(MissionName[currMission + 2]));
			sprintf(pCurrScreen->buttons[4].Name, GET_MISSION_TXT(MissionName[currMission + 3]));
			break;
		}

		if (usedB == 4 &&
			botch[currMission + 4].missNum <= gFurthestMission &&
			currMission + 4 != minmaxSelections[currCity][1])
		{
			bN = true;
		}

		if (bN)
		{
			if (currMission != minmaxSelections[currCity][0])
			{
				bP = true;

				pCurrScreen->buttons[0].u = 6;
				pCurrScreen->buttons[0].d = 2;
				pCurrScreen->buttons[5].u = 5;
				pCurrScreen->buttons[5].d = 1;
				pCurrScreen->buttons[1].u = 1;
				pCurrScreen->buttons[4].d = 6;
				pCurrScreen->buttons[0].action = 0;
				pCurrScreen->buttons[5].action = 0;
			}
			else
			{
				pCurrScreen->buttons[1].u = 6;
				pCurrScreen->buttons[4].d = 6;
				pCurrScreen->buttons[5].u = 5;
				pCurrScreen->buttons[5].d = 2;
				pCurrScreen->buttons[5].action = 0;
				pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_HIDDEN, 0);
			}
		}
		else
		{
			if (currMission != minmaxSelections[currCity][0])
			{
				bP = true;

				pCurrScreen->buttons[0].u = usedB + 1;
				pCurrScreen->buttons[0].d = 2;
				pCurrScreen->buttons[1].u = 1;
				pCurrScreen->buttons[usedB].d = 1;
				pCurrScreen->buttons[0].action = 0;
				pCurrScreen->buttons[5].action = FE_MAKEVAR(BTN_HIDDEN, 0);
			}
			else
			{
				// list missions only
				pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_HIDDEN, 0);
				pCurrScreen->buttons[5].action = FE_MAKEVAR(BTN_HIDDEN, 0);
			}
		}

		if (bP)
		{
			pCurrButton = pCurrScreen->buttons;
			currSelIndex = 0;
		}
		else
		{
			pCurrButton = pCurrScreen->buttons + 1;
			currSelIndex = 1;
		}

		if (loaded[0] == -1)
		{
			SetupExtraPoly("DATA\\CITY.RAW", currCity, 0);
		}
		else
		{
			bDrawExtra = 1;
		}

		feVariableSave[0] = -1;
		feVariableSave[1] = -1;
		feVariableSave[2] = -1;
		feVariableSave[3] = -1;

		missionSetup = 1;

		return 1;
	}

	if (feNewPad & MPAD_CROSS)
	{
		i = currMission;

		if (currSelIndex == 0)
		{
			i -= 4;

			if (i < minmaxSelections[currCity][0])
				return 0;
		}
		else if (currSelIndex == 5)
		{
			i += 4;

			if (i > minmaxSelections[currCity][1] || i > gFurthestMission)
				return 0;
		}
		else
		{
			bReturnToMain = 0;
			GameType = GAME_REPLAYMISSION;
			feVariableSave[0] = currMission;
			feVariableSave[1] = currSelIndex;
			feVariableSave[2] = currCity;
			gCurrentMissionNumber = botch[currMission + (currSelIndex - 1)].missNum;

			return 0;
		}

		currMission = i;
		FESound(3);
		fpUserFunctions[pCurrScreen->userFunctionNum - 1](1);
		bRedrawFrontend = 1;

		return 1;
	}
	else if (feNewPad & MPAD_TRIANGLE)
	{
		missionSetup = 0;
		iScreenSelect = SCREEN_NONE;
	}
	else if (feNewPad & MPAD_D_UP)
	{
		currSelIndex = pCurrButton->u - 1;
	}
	else if (feNewPad & MPAD_D_DOWN)
	{
		currSelIndex = pCurrButton->d - 1;
	}

	return 0;
}

// [D] [T]
int MissionCityScreen(int bSetup)
{
	RECT16 rect;

	if (bSetup)
	{
		GameType = GAME_MISSION;

		if (gFurthestMission == 0)
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
		else if (gFurthestMission < 10)
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
		else if (gFurthestMission < 21)
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
		else if (gFurthestMission < 31)
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
		else
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 19);
		}

		LoadBackgroundFile("DATA\\CITYBACK.RAW");

		if (loaded[0] == -1)
		{
			SetupExtraPoly("DATA\\CITY.RAW", 0, 0);
		}
		else
		{
			bDrawExtra = 1;
		}

		return 0;
	}

	if (feNewPad & MPAD_TRIANGLE)
	{
		// BUGFIX: unload city image
		loaded[0] = -1;

		bDrawExtra = 0;
		FESound(0);
		bDoneAllready = 1;
		LoadBackgroundFile("DATA\\GFX.RAW");
	}
	else
	{
		if (feNewPad & MPAD_D_UP)
		{
			currCity = pCurrButton->u - 1;
		}
		else if (feNewPad & MPAD_D_DOWN)
		{
			currCity = pCurrButton->d - 1;
		}
		else
		{
			currCity = pCurrButton->u & 3;
			return 0;
		}

		rect = extraRect;
		LoadImage(&rect, (u_long*)(_frontend_buffer + currCity * 0x8000));
		DrawSync(0);

#ifdef PSX
		DisplayOnScreenText();

		addPrim(current->ot + 2, &extraSprt);
		addPrim(current->ot + 2, &extraDummy);

		EndFrame();
#endif
	}
	
	return 0;
}

// [D] [T]
int CutSceneSelectScreen(int bSetup)
{
	RECT16 rect;

	rect = extraRect;

	if (bSetup)
	{
		LoadBackgroundFile("DATA\\CUTS\\CUTBACK.RAW");
		currCity = feVariableSave[1];

		if (feVariableSave[0] == -1)
		{
			cutSelection = 0;
			currCity = GameLevel;
		}
		else
		{
			cutSelection = feVariableSave[0];
			feVariableSave[3] = -1;
			feVariableSave[2] = -1;
			feVariableSave[1] = -1;
			feVariableSave[0] = -1;
		}

		if (loaded[2] == GameLevel)
		{
			bDrawExtra = 1;
			LoadImage(&rect, (u_long*)(_frontend_buffer + 0x20000 + cutSelection * 0x8000));
			DrawSync(0);
		}
		else
		{
			SetupExtraPoly(cutGfxNames[GameLevel], cutSelection, 0x20000);
		}

		currSelIndex = 1;

		iScreenSelect = SCREEN_CUTSCENE;

		pCurrButton = pCurrScreen->buttons + 1;

		return 1;
	}

	if (feNewPad & MPAD_TRIANGLE)
	{
		bDrawExtra = 0;
		iScreenSelect = SCREEN_NONE;
	}
	else if (feNewPad & MPAD_CROSS)
	{
		if (currSelIndex == 0)
		{
			if (cutSelection == 0)
			{
				cutSelection = CutAmounts[currCity + 1] - 1;

				if (cutUnlock[gFurthestMission] < CutAmounts[currCity + 1] - 1 + CutAmountsTotal[currCity] + 1)
					cutSelection = cutUnlock[gFurthestMission];
			}
			else
			{
				cutSelection--;
			}
		}
		else
		{
			if (currSelIndex != 2)
			{
				bReturnToMain = 0;
				pCurrButton = pButtonStack[ScreenDepth];
				pScreenStack[ScreenDepth] = pCurrScreen;

				feVariableSave[1] = currCity;
				feVariableSave[0] = cutSelection;

				ScreenNames[ScreenDepth] = pCurrButton->Name;
				SetState(STATE_FMVPLAY, (void*)(feVariableSave[0] + CutAmountsTotal[feVariableSave[1]] + 1));

				return 1;
			}

			if (cutSelection == CutAmounts[currCity + 1] - 1 || cutUnlock[gFurthestMission] <= cutSelection + CutAmountsTotal[currCity] + 1)
			{
				cutSelection = 0;
			}
			else
			{
				cutSelection++;
			}
		}

		rect = extraRect;
		LoadImage(&rect, (u_long*)(_frontend_buffer + 0x20000 + cutSelection * 0x8000));
		DrawSync(0);
		DisplayOnScreenText();

#ifdef PSX
		DisplayOnScreenText();

		addPrim(current->ot + 2, &extraSprt);
		addPrim(current->ot + 2, &extraDummy);

		EndFrame();
#endif

		return 0;
	}
	else if (feNewPad & MPAD_D_UP)
	{
		currSelIndex = pCurrButton->u - 1;
	}
	else if (feNewPad & MPAD_D_DOWN)
	{
		currSelIndex = pCurrButton->d - 1;
	}

	return 0;
}



// [D] [T]
int CutSceneCitySelectScreen(int bSetup)
{
	RECT16 rect;

	rect = extraRect;

	if (bSetup)
	{
		LoadBackgroundFile("DATA\\CITYBACK.RAW");

		if (feVariableSave[0] == -1)
		{
			if (iScreenSelect == SCREEN_NONE)
				iScreenSelect = SCREEN_CUTCITY;
		}
		else
		{
			currCity = feVariableSave[0];

			feVariableSave[3] = -1;
			feVariableSave[2] = -1;
			feVariableSave[1] = -1;
			feVariableSave[0] = -1;
		}

		pCurrScreen->buttons[0].u = 4;
		pCurrScreen->buttons[3].d = 1;

		if (gFurthestMission == 0) 
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
		else if (gFurthestMission < 10)
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
		else if (gFurthestMission < 21)
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
		else if (gFurthestMission < 31) 
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
		else 
		{
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 22);

			if (gFurthestMission == 40)
			{
				pCurrScreen->buttons[0].u = 5;
				pCurrScreen->buttons[3].d = 5;
			}
		}

		if (loaded[0] == -1)
		{
			SetupExtraPoly("DATA\\CITY.RAW", (currCity != 4) ? currCity : 0, 0);
		}
		else
		{
			bDrawExtra = 1;
			
			if (currCity == 4) 
				LoadImage(&rect, (u_long*)_frontend_buffer);
			else 
				LoadImage(&rect, (u_long*)(_frontend_buffer + currCity * 0x8000));

			DrawSync(0);
		}

		return 0;
	}

	if (feNewPad & MPAD_CROSS)
	{
		lastCity = -1;
		lastCutCity = GameLevel;

		if (GameLevel == 4)
		{
			bReturnToMain = 0;

			pScreenStack[ScreenDepth] = pCurrScreen;
			pButtonStack[ScreenDepth] = pCurrButton;

			ScreenNames[ScreenDepth] = pCurrButton->Name;

			feVariableSave[0] = currCity;

			SetState(STATE_FMVPLAY, (void*)96);
			return 1;
		}
		else
		{
			lastCity = -1;
		}

		return 0;
	}
	else if (feNewPad & MPAD_TRIANGLE)
	{
		FESound(0);
		bDoneAllready = 1;
		LoadBackgroundFile("DATA\\GFX.RAW");
		iScreenSelect = SCREEN_NONE;
		bDrawExtra = 0;

		return 0;
	}
	else if (feNewPad & MPAD_D_UP)
	{
		currCity = pCurrButton->u - 1;
	}
	else if (feNewPad & MPAD_D_DOWN)
	{
		currCity = pCurrButton->d - 1;
	}

	GameLevel = currCity;

	rect = extraRect;

	if (GameLevel != 4)
		LoadImage(&rect, (u_long*)(_frontend_buffer + GameLevel * 0x8000));
	else
		LoadImage(&rect, (u_long*)_frontend_buffer);

	DrawSync(0);

#ifdef PSX
	DisplayOnScreenText();

	addPrim(current->ot + 2, &extraSprt);
	addPrim(current->ot + 2, &extraDummy);

	EndFrame();
#endif

	return 0;
}



// [D] [T]
int SetVolumeScreen(int bSetup)
{
	int last;
	int ypos[2] = { 292, 338 };
	char text[4];
	RECT16 rect;

	static int masterVolumeHold = 0;
	static int musicVolumeHold = 0;

	if (bSetup) 
	{
		currSelIndex = 0;

		masterVolumeHold = gMasterVolume;
		musicVolumeHold = gMusicVolume;

		LoadBackgroundFile("DATA\\VOL.RAW");

#ifdef PSX
		sprintf(text, "%d", (int)(10000 + gMasterVolume) / 100);
		FEPrintString(text, 152, ypos[0], 2, 128, 128, 128);

		sprintf(text, "%d", (int)(10000 + gMusicVolume) / 100);
		FEPrintString(text, 152, ypos[1], 2, 128, 128, 128);
#endif

		return 0;
	}

#ifndef PSX
	sprintf(text, "%d", (int)(10000 + gMasterVolume) / 100);
	FEPrintString(text, 152, ypos[0], 2, 128, 128, 128);

	sprintf(text, "%d", (int)(10000 + gMusicVolume) / 100);
	FEPrintString(text, 152, ypos[1], 2, 128, 128, 128);
#endif

	// did we make any changes?
	bool update = false;

	currSelIndex = (pCurrButton->u & 3);

	if (feNewPad & MPAD_TRIANGLE)
	{
		FESound(0);
		bDoneAllready = 1;

		gMasterVolume = masterVolumeHold;
		gMusicVolume = musicVolumeHold;

		LoadBackgroundFile("DATA\\GFX.RAW");

		SetMasterVolume(gMasterVolume);
		SetXMVolume(gMusicVolume);

		return 0;
	}
	else if (feNewPad & MPAD_CROSS)
	{
		if (currSelIndex == 2)
			LoadBackgroundFile("DATA\\GFX.RAW");

		return 0;
	}
	else
	{
		int dir = -1; // -1: no action, 0: limit reached, 1: OK

		if (fePad & MPAD_D_LEFT)
		{
			switch (currSelIndex)
			{
			case 0:
				gMasterVolume -= 100;

				if (gMasterVolume < -10000)
				{
					gMasterVolume = -10000;
					dir = 0;
				}
				else
				{
					dir = 1;
				}

				last = gMasterVolume;
				break;
			case 1:
				gMusicVolume -= 100;

				if (gMusicVolume < -10000)
				{
					gMusicVolume = -10000;
					dir = 0;
				}
				else
				{
					dir = 1;
				}

				last = gMusicVolume;
				break;
			}
		}
		else if (fePad & MPAD_D_RIGHT)
		{
			switch (currSelIndex)
			{
			case 0:
				gMasterVolume += 100;

				if (gMasterVolume > 0)
				{
					gMasterVolume = 0;
					dir = 0;
				}
				else
				{
					dir = 1;
				}

				last = gMasterVolume;
				break;
			case 1:
				gMusicVolume += 100;

				if (gMusicVolume > 0)
				{
					gMusicVolume = 0;
					dir = 0;
				}
				else
				{
					dir = 1;
				}

				last = gMusicVolume;
				break;
			}
		}

		if (dir != -1)
		{
			FESound((dir == 1) ? 3 : 1);
			update = true;
		}
	}

	if (update)
	{
		SetMasterVolume(gMasterVolume);
		SetXMVolume(gMusicVolume);

#ifdef PSX
		sprintf(text, "%d", (int)(10000 + last) / 100);
		FEPrintString(text, 152, ypos[currSelIndex], 2, 128, 128, 128);

		sprintf(text, "%d", (int)(10000 + last) / 100);
		FEPrintString(text, 152, ypos[currSelIndex], 2, 0, 0, 0);

		EndFrame(); // [A] inlined
#else
		// don't flush the screen
		return 1;
#endif
	}

	return 0;
}

int GameNum = 0;
CVECTOR scoreCol = { 120, 120, 120 };
CVECTOR otherCol = {125, 115, 34};

// [D] [T]
void DisplayScoreTable(void)
{
	static char* CityNames[4] = {
		M_LTXT(MTXT_Chicago),
		M_LTXT(MTXT_Havana),
		M_LTXT(MTXT_LasVegas),
		M_LTXT(MTXT_RioDeJaneiro)
	};

	int time;
	int i;
	int offset;
	char text[32];
	SCORE_ENTRY *pSE;

	gSubGameNumber = GameNum;

	OnScoreTable(&pSE);

	sprintf(text, ScreenNames[ScreenDepth - 1]);
	FEPrintString(text, 20, 200, 2, otherCol.r, otherCol.g, otherCol.b);

	sprintf(text, CityNames[GameLevel]);
	FEPrintStringSized(text, 280, 206, 3072, 2, otherCol.r, otherCol.g, otherCol.b);

	if (GameType == GAME_TAKEADRIVE && NumPlayers == 2)
		offset = 48;
	else if (GameType == GAME_COPSANDROBBERS)
		offset = 32;
	else if (GameType == GAME_CAPTURETHEFLAG)
		offset = 40;
	else if (GameType == GAME_CHECKPOINT && NumPlayers == 2)
		offset = 56;
	else
		offset = (GameType - 4U) * 8;

	if (GameType != GAME_PURSUIT && GameType != GAME_SURVIVAL) 
	{
		sprintf(text, "%s", GAMEMODE_AREA_NAME(GameLevel, offset, GameNum));
		FEPrintStringSized(text, 420, 206, 3072, 2, otherCol.r, otherCol.g, otherCol.b);
	}

	offset = 240;
	
	for (i = 0; i < 5; i++)
	{
		sprintf(text, pSE[i].name);
		FEPrintString(text, 20, offset, 2, scoreCol.r, scoreCol.g, scoreCol.b);

		if (GameType == GAME_GATERACE || GameType == GAME_TRAILBLAZER) 
		{
			if (pSE[i].items != -1) 
			{
				sprintf(text, "%d", pSE[i].items);
				FEPrintString(text, 140, offset, 2, scoreCol.r, scoreCol.g, scoreCol.b);
			}
		}

		time = pSE[i].time;

		if (time != -1) 
		{
			sprintf(text, "%d:%02d.%02d", time / 180000, time / 3000 - (time / 180000) * 60, (time % 3000) / 30);
			
			FEPrintString(text, 280, offset, 2, scoreCol.r, scoreCol.g, scoreCol.b);
		}

		offset += 36;
	}
}


// [D] [T]
int ScoreScreen(int bSetup)
{
	if (bSetup)
	{
		GameLevel = 0;

		DisplayScoreTable();

		iScreenSelect = SCREEN_SCORES;
		currSelIndex = 0;

		return 0;
	}

	if (feNewPad & MPAD_CROSS)
	{
		if (currSelIndex == 0)
		{
			if (GameType == GAME_SURVIVAL || GameType == GAME_PURSUIT)
			{
				if (--GameLevel < 0)
					GameLevel = 3;

				GameNum = 0;
			}
			else
			{
				GameNum ^= 1;

				if (GameNum == 1)
				{
					if (--GameLevel < 0)
						GameLevel = 3;
				}
			}
		}
		else
		{
			if (GameType == GAME_SURVIVAL || GameType == GAME_PURSUIT)
			{
				if (++GameLevel > 3)
					GameLevel = 0;

				GameNum = 0;
			}
			else
			{
				GameNum ^= 1;

				if (GameNum == 0)
				{
					if (++GameLevel > 3)
						GameLevel = 0;
				}
			}
		}

#ifdef PSX
		DisplayScoreTable();
		bRedrawFrontend = 1;
#endif
	}
	else if (feNewPad & MPAD_TRIANGLE)
	{
		iScreenSelect = SCREEN_NONE;
		return 0;
	}
	else if ((feNewPad & MPAD_D_UP) || (feNewPad & MPAD_D_DOWN))
	{
		currSelIndex ^= 1;
	}

#ifndef PSX
	DisplayScoreTable();
#endif

	return 0;
}

// [D] [T]
int SubtitlesOnOffScreen(int bSetup)
{
	if (bSetup)
	{
		if (gSubtitles == 0)
		{
			pCurrButton = pCurrScreen->buttons + 1;
		}
		else 
		{
			pCurrButton = pCurrScreen->buttons;
		}

		return 1;
	}
	return 0;
}


#ifndef PSX
static char* Driver1CityNames[NUM_CITIES - FIRST_DRIVER1_CITY] = {
	"Miami", "San Francisco", "Los Angeles", "New York", "Newcastle", "Training"
};

extern void ShowSavingWaitMessage(char* message, int height);

static int sCityButtonCity[8];		// game level of each button of the Take a Ride city screen
static bool sD1CityListButton;		// the screen has one "Driver 1" button that opens the city list

// picture shown next to a button of the Take a Ride city screen: the Driver 1 cities show the picture of the Driver 2 city they borrow from
static int CityPictureOfButton(int idx)
{
	return idx < 4 ? idx : CityBase[sCityButtonCity[idx & 7]] & 3;
}

// [A] Take a Ride city screen: the four Driver 2 cities, followed by the Driver 1 cities the player can play (Driver 1 DLC switched on)
static void SetupDriver1CityButton(void)
{
	PSXSCREEN* scr = &PsxScreens[1];
	static int origY[4], origSY[4];
	static bool saved = false;
	int count = 0;

	if (!saved)
	{
		for (int i = 0; i < 4; i++)
		{
			origY[i] = scr->buttons[i].y;
			origSY[i] = scr->buttons[i].s_y;
		}
		saved = true;
	}

	sD1CityListButton = false;

	if (gD1DlcEnabled)
	{
		// the first visit converts the Driver 1 cities (about a second each): tell the player why the game waits
		if (D1Dlc_ConversionPending())
			ShowSavingWaitMessage("Preparing Driver 1 cities", 0);

		int available[NUM_CITIES];
		int total = 0;

		for (int i = FIRST_DRIVER1_CITY; i < NUM_CITIES; i++)
		{
			if (D1Dlc_CityAvailable(i))
				available[total++] = i;
		}

		// one "Driver 1 Cities" button below the four Driver 2 cities opens the list of all Driver 1 cities
		sD1CityListButton = total > 0;
		count = sD1CityListButton ? 1 : 0;

		if (total > 0)
			sCityButtonCity[4] = available[0];
	}

	const int n = 4 + count;
	const int origStep = (origY[3] - origY[0]) / 3;
	// 8 buttons do not fit at the original spacing
	const int step = n > 5 ? MIN(origStep, (origStep * 3 + 90) / (n - 1)) : origStep;

	scr->numButtons = n;

	for (int i = 0; i < n; i++)
	{
		PSXBUTTON& btn = scr->buttons[i];

		if (i >= 4)
		{
			btn = scr->buttons[3];
			btn.action = FE_MAKEVAR(BTN_NEXT_SCREEN, 3);
			btn.var = FE_MAKEVAR(1, sCityButtonCity[i]);
			strcpy(btn.Name, Driver1CityNames[sCityButtonCity[i] - FIRST_DRIVER1_CITY]);

			if (sD1CityListButton)
			{
				btn.action = FE_MAKEVAR(BTN_NEXT_SCREEN, DRIVER1_CITY_SCREEN);
				btn.var = -1;
				strcpy(btn.Name, "Driver 1 Cities");
			}
		}

		btn.y = origY[0] + i * step;
		btn.s_y = origSY[0] + i * step;

		if (n == 4)
		{
			btn.y = origY[i];
			btn.s_y = origSY[i];
		}

		btn.u = (i == 0) ? n : i;
		btn.d = (i == n - 1) ? 1 : i + 2;
	}
}

// [A] lists the Driver 1 cities the player can play, picking one continues like a Driver 2 city
int Driver1CitySelectScreen(int bSetup)
{
	if (bSetup)
	{
		int cities[NUM_CITIES];
		int count = 0;

		for (int i = FIRST_DRIVER1_CITY; i < NUM_CITIES; i++)
		{
			if (D1Dlc_CityAvailable(i))
				cities[count++] = i;
		}

		if (count == 0)
			return 0;

		currCity = 0;

		BuildButtonsVertical(count, 160, count < 6 ? 208 : 170);

		for (int i = 0; i < count; i++)
		{
			PSXBUTTON& btn = pCurrScreen->buttons[i];

			btn.action = FE_MAKEVAR(BTN_NEXT_SCREEN, 3);
			btn.var = FE_MAKEVAR(1, cities[i]);
			strcpy(btn.Name, Driver1CityNames[cities[i] - FIRST_DRIVER1_CITY]);
		}
	}

	return 0;
}

// [A] cities that have converted Driver 1 missions; convert = also converts the city itself (about a second, only when the list opens)
static int Driver1MissionCities(int* cities, bool convert)
{
	int count = 0;

	for (int i = FIRST_DRIVER1_CITY; i < NUM_CITIES; i++)
	{
		if (D1Missions_CityCount(i) > 0 && (convert ? D1Dlc_CityAvailable(i) : (gD1DlcCityMask & (1 << (i - FIRST_DRIVER1_CITY))) != 0))
			cities[count++] = i;
	}

	return count;
}

// [A] the Story screen (New Game, Load Game, Replay Mission, View Cutscenes) gets a Driver 1 button when converted missions exist
int StoryScreen(int bSetup)
{
	if (bSetup)
	{
		PSXSCREEN* scr = pCurrScreen;
		int cities[NUM_CITIES];

		// restore the Driver 2 layout first, the screen data is reused on every visit
		scr->numButtons = 4;
		scr->buttons[0].u = 4;
		scr->buttons[3].d = 1;

		if (gD1DlcEnabled && D1Missions_Available() && Driver1MissionCities(cities, false) > 0)
		{
			PSXBUTTON& btn = scr->buttons[4];

			btn = scr->buttons[3];
			btn.y += 41;
			btn.s_y += 41;
			btn.u = 4;
			btn.d = 1;
			btn.action = FE_MAKEVAR(BTN_NEXT_SCREEN, D1MISSION_CITY_SCREEN);
			btn.var = -1;
			strcpy(btn.Name, "D1 Missions");

			scr->buttons[0].u = 5;
			scr->buttons[3].d = 5;
			scr->numButtons = 5;
		}
	}

	return 0;
}

// [A] Story - Driver 1: the cities that have missions, picking one opens its mission list
int Driver1MissionCityScreen(int bSetup)
{
	if (bSetup)
	{
		int cities[NUM_CITIES];
		int count;

		if (D1Dlc_ConversionPending())
			ShowSavingWaitMessage("Preparing Driver 1 cities", 0);

		count = Driver1MissionCities(cities, true);

		if (count == 0)
		{
			BuildButtonsVertical(1, 160, 208);
			pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_DISABLED, 0);
			pCurrScreen->buttons[0].var = -1;
			strcpy(pCurrScreen->buttons[0].Name, "Not Available");
			return 0;
		}

		BuildButtonsVertical(count, 160, count < 6 ? 208 : 170);

		for (int i = 0; i < count; i++)
		{
			PSXBUTTON& btn = pCurrScreen->buttons[i];

			btn.action = FE_MAKEVAR(BTN_NEXT_SCREEN, D1MISSION_LIST_SCREEN);
			btn.var = FE_MAKEVAR(1, cities[i]);		// GameLevel: the list shows the missions of this city
			strcpy(btn.Name, Driver1CityNames[cities[i] - FIRST_DRIVER1_CITY]);
		}
	}

	return 0;
}

#define D1_MISSIONS_PER_PAGE	5		// plus Previous and Next: the screen holds 8 buttons

static int sD1MissionPage;
static int sD1PrevButton, sD1NextButton;

static void BuildDriver1MissionPage(void)
{
	int total = D1Missions_CityCount(GameLevel);
	int first = sD1MissionPage * D1_MISSIONS_PER_PAGE;
	int shown = MIN(D1_MISSIONS_PER_PAGE, total - first);
	bool hasPrev = sD1MissionPage > 0;
	bool hasNext = first + D1_MISSIONS_PER_PAGE < total;
	int count = shown + (hasPrev ? 1 : 0) + (hasNext ? 1 : 0);
	int b = 0;

	BuildButtonsVertical(count, 160, count < 6 ? 190 : 150);
	sD1PrevButton = sD1NextButton = -1;

	if (hasPrev)
	{
		sD1PrevButton = b;
		pCurrScreen->buttons[b].var = -1;
		strcpy(pCurrScreen->buttons[b++].Name, "Previous");
	}

	for (int i = 0; i < shown; i++, b++)
	{
		PSXBUTTON& btn = pCurrScreen->buttons[b];
		const char* title = "";
		int number = D1_MISSION_BASE;

		D1Missions_Entry(GameLevel, first + i, &number, &title);

		btn.action = FE_MAKEVAR(BTN_START_GAME, 0);
		btn.var = FE_MAKEVAR(FEVAR_D1MISSION, number - D1_MISSION_BASE);
		strncpy(btn.Name, title, sizeof(btn.Name) - 1);
		btn.Name[sizeof(btn.Name) - 1] = '\0';
	}

	if (hasNext)
	{
		sD1NextButton = b;
		pCurrScreen->buttons[b].var = -1;
		strcpy(pCurrScreen->buttons[b].Name, "Next");
	}

	pCurrButton = pCurrScreen->buttons + (hasPrev ? 1 : 0);
}

// [A] Story - Driver 1 - city: every mission of the city is open, all standalone (no unlocking, no story progress)
int Driver1MissionListScreen(int bSetup)
{
	if (bSetup)
	{
		sD1MissionPage = 0;
		BuildDriver1MissionPage();
		return 1;
	}

	if (feNewPad & MPAD_CROSS)
	{
		int sel = (int)(pCurrButton - pCurrScreen->buttons);

		if (sel == sD1PrevButton || sel == sD1NextButton)
		{
			sD1MissionPage += (sel == sD1NextButton) ? 1 : -1;
			FESound(3);
			BuildDriver1MissionPage();
			bRedrawFrontend = 1;
			return 1;
		}
	}

	return 0;
}

// [A] a list of options: one button per entry, the current one selected; the buttons of screen 31 (On/Off) set the variable and go back
static void BuildOptionButtons(int count, char** names, int varCode, int current)
{
	BuildButtonsVertical(count, 160, 208);

	for (int i = 0; i < count; i++)
	{
		PSXBUTTON& btn = pCurrScreen->buttons[i];

		btn.action = PsxScreens[31].buttons[0].action;
		btn.var = FE_MAKEVAR(varCode, i);
		strcpy(btn.Name, names[i]);
	}

	pCurrButton = pCurrScreen->buttons + current;
}

int ExtendedOptionsScreen(int bSetup)
{
	if (bSetup)
	{
		BuildButtonsVertical(CAMERA_DISTANCE_MENU ? 8 : 7, 160, 170);

		pCurrScreen->buttons[0].action = FE_MAKEVAR(BTN_NEXT_SCREEN, DRAW_DISTANCE_SCREEN);
		pCurrScreen->buttons[0].var = -1;
		strcpy(pCurrScreen->buttons[0].Name, "Draw Distance");

		pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_NEXT_SCREEN, CROWD_SCREEN);
		pCurrScreen->buttons[1].var = -1;
		strcpy(pCurrScreen->buttons[1].Name, "Traffic And People");

		pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_NEXT_SCREEN, CAMERA_ORBIT_SCREEN);
		pCurrScreen->buttons[2].var = -1;
		strcpy(pCurrScreen->buttons[2].Name, "Free Camera");

		int next = 3;

		if (CAMERA_DISTANCE_MENU)
		{
			pCurrScreen->buttons[next].action = FE_MAKEVAR(BTN_NEXT_SCREEN, CAMERA_ZOOM_SCREEN);
			pCurrScreen->buttons[next].var = -1;
			strcpy(pCurrScreen->buttons[next++].Name, "Camera Distance");
		}

		pCurrScreen->buttons[next].action = FE_MAKEVAR(BTN_NEXT_SCREEN, ALLCARS_SCREEN);
		pCurrScreen->buttons[next].var = -1;
		strcpy(pCurrScreen->buttons[next++].Name, "All Cities Cars");

		pCurrScreen->buttons[next].action = FE_MAKEVAR(BTN_NEXT_SCREEN, SKY_SCREEN);
		pCurrScreen->buttons[next].var = -1;
		strcpy(pCurrScreen->buttons[next++].Name, "Sky");

		pCurrScreen->buttons[next].action = FE_MAKEVAR(BTN_NEXT_SCREEN, CYCLE_SCREEN);
		pCurrScreen->buttons[next].var = -1;
		strcpy(pCurrScreen->buttons[next++].Name, "Day Night Cycle");

		pCurrScreen->buttons[next].action = FE_MAKEVAR(BTN_NEXT_SCREEN, D1DLC_SCREEN);
		pCurrScreen->buttons[next].var = -1;
		strcpy(pCurrScreen->buttons[next].Name, "Driver 1 DLC");

		return 0;
	}

	return 0;
}

int DrawDistanceScreen(int bSetup)
{
	if (bSetup)
	{
		static char* names[3] = { "Original", "5x", "10x" };

		BuildOptionButtons(3, names, FEVAR_DRAW_DISTANCE, gDrawDistanceMode);
		return 1;
	}

	return 0;
}

int CrowdScreen(int bSetup)
{
	if (bSetup)
	{
		static char* names[5] = { "Original", "2x", "3x", "5x", "10x" };
		int current = 0;

		for (int i = 0; i < 5; i++)
		{
			if (crowdChoices[i] == gCrowdMul)
				current = i;
		}

		BuildOptionButtons(5, names, FEVAR_CROWD, current);
		return 1;
	}

	return 0;
}

int AllCarsScreen(int bSetup)
{
	if (bSetup)
	{
		static char* names[2] = { "Off", "On" };

		BuildOptionButtons(2, names, FEVAR_ALLCARS, gAllCityCars);
		return 1;
	}

	return 0;
}

int SkyScreen(int bSetup)
{
	if (bSetup)
	{
		static char* names[3] = { "Off", "Smooth", "HD" };

		BuildOptionButtons(3, names, FEVAR_SKY, gSkyHd);
		return 1;
	}

	return 0;
}

int DayCycleScreen(int bSetup)
{
	if (bSetup)
	{
		static char* names[2] = { "Off", "On" };

		BuildOptionButtons(2, names, FEVAR_CYCLE, gDayCycle);
		return 1;
	}

	return 0;
}

int VolumetricEffectsScreen(int bSetup)
{
	if (bSetup)
	{
		static char* names[2] = { "Off", "On" };

		BuildOptionButtons(2, names, FEVAR_VOLUMETRIC, gSunShafts && gVolumeLights && gAtmosphere);
		return 1;
	}

	return 0;
}

int Driver1DlcScreen(int bSetup)
{
	if (bSetup)
	{
		static char checkName[40];
		static char* names[3] = { "Off", "On", checkName };
		static char* namesNoData[3] = { "Off", "On (Check Data First)", checkName };

		if (!gD1DlcChecked)
			strcpy(checkName, "Check Driver 1 Data");
		else if (gD1DlcCityMask && !gD1DlcPrepared)
		{
			// the converter borrows Chicago from the extracted Driver 2 files: without them the conversion cannot succeed
			FILE* donor = fopen("DRIVER2\\LEVELS\\CHICAGO.LEV", "rb");

			if (donor)
			{
				fclose(donor);
				strcpy(checkName, "Update Needed - Convert Again");
			}
			else
				strcpy(checkName, "Driver 2 LEVELS Folder Missing");
		}
		else if (gD1DlcCityMask)
		{
			int found = 0;
			for (int m = gD1DlcCityMask; m; m >>= 1)
				found += m & 1;

			sprintf(checkName, "Found %d Cities - Check Again", found);
		}
		else
			strcpy(checkName, "Not Found - Check Again");

		BuildOptionButtons(3, gD1DlcCityMask ? names : namesNoData, FEVAR_D1DLC, gD1DlcEnabled);
		return 1;
	}

	return 0;
}

int CameraZoomScreen(int bSetup)
{
	if (bSetup)
	{
		static char* names[5] = { "1.0", "1.2", "1.5", "1.8", "2.0" };

		BuildOptionButtons(5, names, FEVAR_CAMERA_ZOOM, gCameraZoom);
		return 1;
	}

	return 0;
}
#endif // !PSX

// [D] [T]
int CityCutOffScreen(int bSetup)
{
	if (bSetup)
	{
#ifdef PSX
		if (gFurthestMission < 20)
			pCurrScreen->buttons[2].action = FE_MAKEVAR(BTN_DISABLED, 0);

		if (gFurthestMission < 29)
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
#endif

#ifndef PSX
		// [A] Take A Ride: every city is open from the start (Vegas and Rio no longer wait for the story progress)
		// Driver 1 cities: an extra button opens the list of the cities the player has data for
		if (pCurrScreen == &PsxScreens[1])
			SetupDriver1CityButton();

		LoadBackgroundFile("DATA\\CITYBACK.RAW");

		if (lastCity == -1)
			currCity = 0;
		else
			currCity = lastCity;

		if (loaded[0] == -1)
		{
			SetupExtraPoly("DATA\\CITY.RAW", currCity, 0);
		}
		else
		{
			bDrawExtra = 1;

			RECT16 rect = extraRect;
			LoadImage(&rect, (u_long*)(_frontend_buffer + currCity * 0x8000));
			DrawSync(0);
		}
#endif
		return 0;
	}

#ifndef PSX
	/*
	if ((fePad & MPAD_CROSS) != 0)
	{
		lastCity = currCity;

		bDrawExtra = 0;

		LoadBackgroundFile("DATA\\GFX.RAW");
		loaded[0] = -1;

		return 0;
	}*/

	if (feNewPad & MPAD_TRIANGLE)
	{
		lastCity = -1;

		bDrawExtra = 0;
		FESound(0);
		bDoneAllready = 1;

		LoadBackgroundFile("DATA\\GFX.RAW");
		loaded[0] = -1;

		return 0;
	}
	else if (feNewPad & MPAD_D_UP)
	{
		currCity = CityPictureOfButton(pCurrButton->u - 1);
	}
	else if (feNewPad & MPAD_D_DOWN)
	{
		currCity = CityPictureOfButton(pCurrButton->d - 1);
	}
	else
	{
		currCity = CityPictureOfButton(pCurrButton - pCurrScreen->buttons);
		return 0;
	}

	RECT16 rect = extraRect;
	LoadImage(&rect, (u_long*)(_frontend_buffer + currCity * 0x8000));
	DrawSync(0);
#endif
	return 0;
}


char* contNames[2] = {
	"DATA\\CARCONT.RAW",
	"DATA\\TANCONT.RAW"
};

// [D] [T]
int ControllerScreen(int bSetup)
{
	if (bSetup ) 
	{
		iScreenSelect = SCREEN_SCORES;
		currSelIndex = 0;
		pCurrScreen->numButtons = 0;

		LoadBackgroundFile(contNames[currSelIndex]);
	}
	else
	{
		if (feNewPad & MPAD_CROSS)
		{
			currSelIndex = currSelIndex ^ 1;
			LoadBackgroundFile(contNames[currSelIndex]);

			bRedrawFrontend = 1;
		}
		else if (feNewPad & MPAD_TRIANGLE)
		{
			iScreenSelect = SCREEN_SCORES;
			LoadBackgroundFile("DATA\\GFX.RAW");
		}
	}

	return 0;
}

// [D] [T]
int MainScreen(int bSetup)
{
	if (bSetup) 
	{
		if (numPadsConnected == 2) 
		{
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 6);
		}
		else 
		{
			pCurrScreen->buttons[3].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
	}

	return 0;
}

static char* cheatText[] =
{
	G_LTXT_ID(GTXT_NoSecrets),
	G_LTXT_ID(GTXT_MountainTrack),
	G_LTXT_ID(GTXT_Circuit),
	G_LTXT_ID(GTXT_Invincibility),
	G_LTXT_ID(GTXT_Immunity),
	G_LTXT_ID(GTXT_MiniCars),		// [A]
	G_LTXT_ID(GTXT_BonusGallery)	// [A]
};

// [D] [T]
int CheatScreen(int bSetup)
{
	// one row per secret in display order; a new secret is one more row here
	struct CHEAT_ROW
	{
		char* text;
		int action;
		int var;
		int open;
	};

	int i, n, step;
	int unlockedAll;
	int shown[8];			// rows to show, PSXSCREEN has 8 buttons
	PSXBUTTON* btn;

	if (bSetup == 0)
		return 0;

	unlockedAll = (gFurthestMission == 40);

	CHEAT_ROW rows[] = {
		{ cheatText[1], FE_MAKEVAR(BTN_NEXT_SCREEN, 33), FE_MAKEVAR(12, 1), unlockedAll || AvailableCheats.cheat1 },
		{ cheatText[2], FE_MAKEVAR(BTN_NEXT_SCREEN, 33), FE_MAKEVAR(12, 0), unlockedAll || AvailableCheats.cheat2 },
		{ cheatText[3], FE_MAKEVAR(BTN_NEXT_SCREEN, 30), -1, unlockedAll || AvailableCheats.cheat3 },
		{ cheatText[4], FE_MAKEVAR(BTN_NEXT_SCREEN, 31), -1, unlockedAll || AvailableCheats.cheat4 },
#if ENABLE_BONUS_CONTENT
		{ cheatText[5], FE_MAKEVAR(BTN_NEXT_SCREEN, 40), -1, unlockedAll },
		{ cheatText[6], 0, FE_MAKEVAR(14, 0), unlockedAll },
#endif
#ifndef PSX
		{ "Extended Settings", FE_MAKEVAR(BTN_NEXT_SCREEN, EXTENDED_SCREEN), -1, 1 },	// draw distance, crowd, free camera
		{ "Volumetric Effects", FE_MAKEVAR(BTN_NEXT_SCREEN, VOLUMETRIC_SCREEN), -1, 1 },	// sun shafts, lamp light, fog
#endif
	};

	n = 0;

	for (i = 0; i < (int)(sizeof(rows) / sizeof(rows[0])) && n < 8; i++)
	{
		if (rows[i].open)
			shown[n++] = i;
	}

	pCurrScreen->numButtons = n;

	// the screen has four buttons, the rest are copies of the fourth placed further down
	step = (n <= 6) ? 40 : 90 / (n - 4);

	for (i = 0; i < n; i++)
	{
		btn = &pCurrScreen->buttons[i];

		if (i >= 4)
		{
			*btn = pCurrScreen->buttons[3];
			btn->y += (i - 3) * step;
			btn->s_y += (i - 3) * step;
		}

		btn->action = rows[shown[i]].action;
		btn->var = rows[shown[i]].var;
		btn->u = (i == 0) ? n : i;
		btn->d = (i == n - 1) ? 1 : i + 2;

		sprintf(btn->Name, "%s", GET_GAME_TXT(rows[shown[i]].text));
	}

	currSelIndex = 0;
	return 0;
}


// [D] [T]
int ImmunityOnOffScreen(int bSetup)
{
	if (bSetup)
	{
		if (gPlayerImmune == 0)
			pCurrButton = pCurrScreen->buttons + 1;
		else
			pCurrButton = pCurrScreen->buttons;
		return 1;
	}

	return 0;
}

// [D] [T]
int InvincibleOnOffScreen(int bSetup)
{
	if (bSetup)
	{
		if (gInvincibleCar == 0) 
			pCurrButton = pCurrScreen->buttons + 1;
		else 
			pCurrButton = pCurrScreen->buttons;

		return 1;
	}
	return 0;
}

int MiniCarsOnOffScreen(int bSetup)
{
	if (bSetup)
	{
		if (ActiveCheats.cheat13 == 0)
			pCurrButton = pCurrScreen->buttons + 1;
		else
			pCurrButton = pCurrScreen->buttons;

		return 1;
	}
	return 0;
}

int CameraOrbitOnOffScreen(int bSetup)
{
	if (bSetup)
	{
		pCurrButton = pCurrScreen->buttons + (gCameraOrbit ? 0 : 1);
		return 1;
	}
	return 0;
}

// [D] [T]
int GamePlayScreen(int bSetup)
{
	if (bSetup)
	{
		pCurrScreen->buttons[2].action = (allowVibration == 0) ? FE_MAKEVAR(BTN_DISABLED, 0) : FE_MAKEVAR(BTN_NEXT_SCREEN, 17);
	}

	return 0;
}

// [D] [T]
int GameNameScreen(int bSetup)
{
	int offset;

	if (bSetup) 
	{
		
		if (GameType == GAME_TAKEADRIVE && NumPlayers == 2) 
			offset = 48;
		else if (GameType == GAME_COPSANDROBBERS)
			offset = 32;
		else if (GameType == GAME_CAPTURETHEFLAG)
			offset = 40;
		else if (GameType == GAME_CHECKPOINT && NumPlayers == 2)
			offset = 56;
		else
			offset = (GameType - 4U) * 8;

		strcpy(pCurrScreen->buttons[0].Name, GAMEMODE_AREA_NAME(GameLevel, offset, 0));
		strcpy(pCurrScreen->buttons[1].Name, GAMEMODE_AREA_NAME(GameLevel, offset, 1));
	}

	return 0;
}
// [D] [T]
int CheatNumlayerSelect(int bSetup)
{
	if (bSetup) 
	{
		if (numPadsConnected == 2) 
		{
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_NEXT_SCREEN, 32);
		}
		else 
		{
			pCurrScreen->buttons[1].action = FE_MAKEVAR(BTN_DISABLED, 0);
		}
		return 0;
	}
	return 0;
}

int BuildButtonsVertical(int count, int xStart, int yStart)
{
	int numButtons = count;

	if (numButtons > 8)
		numButtons = 8;

	for (int i = 0; i < numButtons; i++)
	{
		int prevButton = i - 1;
		int nextButton = i + 1;

		if (prevButton < 0)
			prevButton += numButtons;

		if (nextButton >= numButtons)
			nextButton -= numButtons;

		PSXBUTTON& btn = pCurrScreen->buttons[i];

		// copy button 0
		btn = PsxScreens[4].buttons[0];

		btn.u = prevButton + 1;
		btn.d = nextButton + 1;

		btn.l = 0;//i + 1;
		btn.r = 0;//i + 1;

		btn.s_x = xStart + 200;
		btn.s_y = yStart + i * 38;

		btn.x = xStart;
		btn.y = yStart + i * 38;

		btn.action = 0;

		btn.var = i;

		sprintf(btn.Name, "Button %d", i + 1);
	}

	pCurrScreen->numButtons = numButtons;

	return numButtons;
}

#ifndef PSX
#include "../utils/fs.h"
#define MAX_REPLAY_NAME_LENGTH 48
char gFEReplayList[8][MAX_REPLAY_NAME_LENGTH];
int gFEReplayCount = 0;
#endif

int UserReplaySelectScreen(int bSetup)
{
#ifndef PSX
	if (bSetup)
	{
		FS_FINDDATA* fd;
		const char* filename;
		const char* tmp;

		gFEReplayCount = 0;
		fd = NULL;
		filename = FS_FindFirst("Replays/*.D2RP", &fd);

		while (filename)
		{
			strncpy(gFEReplayList[gFEReplayCount], filename, MAX_REPLAY_NAME_LENGTH);
			tmp = strchr(filename, '.');
			gFEReplayList[gFEReplayCount][MAX_REPLAY_NAME_LENGTH-1] = 0;

			gFEReplayCount++;
			filename = FS_FindNext(fd);

			// [A] don't load more than 8
			if (gFEReplayCount >= 8)
				break;
		}

		FS_FindClose(fd);

		if (gFEReplayCount)
		{
			int yOfs = 170;

			if (gFEReplayCount < 6)
				yOfs = 208;

			int numButtons = BuildButtonsVertical(gFEReplayCount, 160, yOfs);

			for (int i = 0; i < numButtons; i++)
			{
				PSXBUTTON& btn = pCurrScreen->buttons[i];
				strncpy(btn.Name, gFEReplayList[i], sizeof(btn.Name));

				tmp = strchr(btn.Name, '.');

				if (tmp)
					btn.Name[tmp - btn.Name] = 0;
				else
					btn.Name[15] = 0;
			}
		}
		else
		{
			BuildButtonsVertical(1, 160, 208);

			PSXBUTTON& btn = pCurrScreen->buttons[0];
			btn.action = FE_MAKEVAR(BTN_PREVIOUS_SCREEN, 0);
			strcpy(btn.Name, G_LTXT(GTXT_NoSavedData));
		}

		return 0;
	}

	if (gFEReplayCount)
	{
		if (feNewPad & MPAD_CROSS)
		{
			char filename[64];
			int selectedReplay = pCurrButton->var;

			sprintf(filename, "Replays/%s", gFEReplayList[selectedReplay]);

			if (LoadReplayFromFile(filename))
			{
				StoredGameType = GameType;
				GameType = GAME_LOADEDREPLAY;
				SetState(STATE_GAMESTART);
			}
			else
			{
				SetState(STATE_INITFRONTEND);
			}
		}
	}
#endif

	return 0;
}

char* TimeOfDayNames[] = {
	G_LTXT_ID(GTXT_Dawn),
	G_LTXT_ID(GTXT_Day),
	G_LTXT_ID(GTXT_Dusk),
	G_LTXT_ID(GTXT_Night)
};

char* WeatherNames[] = {
	G_LTXT_ID(GTXT_Clear),
	G_LTXT_ID(GTXT_Rainy),
	G_LTXT_ID(GTXT_Wet),
#ifndef PSX
	"Snow",		// [A] WANTED_WEATHER_SNOW
#endif
};

char* TimeOfDayItems[] = {
	G_LTXT_ID(GTXT_TimeOfDay),
	G_LTXT_ID(GTXT_Condition),
	G_LTXT_ID(GTXT_Continue),
};

int TimeOfDaySelectScreen(int bSetup)
{
	int numButtons, i;
	int dir;
	char text[64];
	int ypos[2] = { 208, 246 };

	if (bSetup)
	{
		// setup time we want to reset it
		wantedWeather = 0;
		wantedTimeOfDay = TIME_DAY;

		numButtons = BuildButtonsVertical(3, 168, 208);

		for (i = 0; i < numButtons; i++)
		{
			PSXBUTTON& btn = pCurrScreen->buttons[i];
			sprintf(btn.Name, "%s%c", GET_GAME_TXT(TimeOfDayItems[i]), i == 2 ? 0 : ':');

			if (i == 2)
			{
				btn.y += 32;
				btn.s_y += 32;
				btn.action = FE_MAKEVAR(BTN_NEXT_SCREEN, 14);
			}
			else
			{
				btn.x -= 80;
				btn.l = btn.r = i + 1;
			}
		}

		pNewButton = &pCurrScreen->buttons[2];
		iScreenSelect = SCREEN_TIMEOFDAY;

		return 0;
	}

	FEPrintString(GET_GAME_TXT(TimeOfDayNames[wantedTimeOfDay]), 590, ypos[0], 4, 128, 128, 128);
	FEPrintString(GET_GAME_TXT(WeatherNames[wantedWeather]), 590, ypos[1], 4, 128, 128, 128);

	if (feNewPad & MPAD_TRIANGLE)
	{
		// reset back
		wantedWeather = -1;
		wantedTimeOfDay = -1;

		iScreenSelect = SCREEN_NONE;
	}

	dir = 0;

	if (feNewPad & MPAD_D_LEFT)
	{
		dir = -1;
	}
	else if (feNewPad & MPAD_D_RIGHT)
	{
		dir = 1;
	}

	if (dir != 0)
	{
		i = pCurrButton - pCurrScreen->buttons;

		if (i == 0)
		{
			wantedTimeOfDay += dir;
			wantedTimeOfDay &= 3;

			gWantNight = wantedTimeOfDay == TIME_DUSK || wantedTimeOfDay == TIME_NIGHT;
		}
		else if (i == 1)
		{
			const int numWeathers = sizeof(WeatherNames) / sizeof(WeatherNames[0]);

			wantedWeather += dir;
			if (wantedWeather < 0)
				wantedWeather += numWeathers;
			else if (wantedWeather >= numWeathers)
				wantedWeather -= numWeathers;
		}
		else
			dir = 0;
	}

	return 0;
}

int DemoScreen(int bSetup)
{
	if (bSetup)
		return 0;
	
	if (feNewPad & MPAD_CROSS)
	{
		pScreenStack[ScreenDepth] = pCurrScreen;
		pButtonStack[ScreenDepth] = pCurrButton;
		
		FESound(2);
		GameType = GAME_PURSUIT;
	
		GameLevel = 1;
		gWantNight = 0;
		gSubGameNumber = 0;
		
		SetState(STATE_GAMESTART);
		
		return 0;
	}

	if(mainScreenLoaded)
	{
		if (feNewPad & MPAD_CIRCLE)
		{
			LoadBackgroundFile(contNames[0]);
			FESound(2);
		}
	}
	else if(bQuitToSystem)
	{
		bQuitToSystem = 0;
		LoadBackgroundFile("DATA\\GFX.RAW");
	}

	return 0;
}