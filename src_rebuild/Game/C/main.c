#include "driver2.h"
#include "main.h"
#include "daycycle.h"

#include "ASM/rndrasm.h"
#include "ASM/d2mapasm.h"

#include "system.h"
#include "pad.h"
#include "sound.h"
#include "fmvplay.h"
#include "E3stuff.h"
#include "gamesnd.h"
#include "scores.h"
#include "glaunch.h"
#include "loadview.h"
#include "replays.h"
#include "event.h"
#include "mission.h"
#include "cutscene.h"
#include "texture.h"
#include "sky.h"
#include "pres.h"
#include "draw.h"
#include "denting.h"
#include "bcollide.h"
#include "playermodel.h"
#include "cosmetic.h"
#include "pedest.h"
#include "spool.h"
#include "map.h"
#include "objanim.h"
#include "handling.h"
#include "drivinggames.h"
#include "bomberman.h"
#include "players.h"
#include "ai.h"
#include "civ_ai.h"
#include "cop_ai.h"
#include "camera.h"
#include "overlay.h"
#include "debris.h"
#include "job_fx.h"
#include "director.h"
#include "convert.h"
#include "tile.h"
#include "overmap.h"
#include "motion_c.h"
#include "dr2roads.h"
#include "models.h"
#include "cars.h"
#include "objcoll.h"
#include "mc_snd.h"
#include "felony.h"
#include "leadai.h"
#include "loadsave.h"
#include "Frontend/FEmain.h"
#include "xaplay.h"
#include "shadow.h"
#include "pause.h"
#include "sysclock.h"
#include "platform.h"
#include "state.h"
#include "cutrecorder.h"
#include "chopper.h"
#include "carspawner.h"
#include "carovr.h"

int levelstartpos[8][4] = {
	{ 4785, -1024, -223340, 0},
	{ -223276, 2048, -235167, 0},
	{ 230347, -1024, 704030, 0},
	{ 91631, -1024, -347175, 0},

	// what?
	{ 148808, 6163, -112000, 0},
	{ -170000, 6163, 361000, 0},
	{ -10500, -6163, -22144, 0},
	{ -8995, -6163, 63655, 0},
};

XZPAIR gStartPos = { 0 };
int gNightSwapSteps = 0;		// -nightswap <steps>: swap the day and night LEV spool every that many steps (test)
int gStartRot = 0;

enum LevLumpType
{
	// known lumps indexes
	LUMP_MODELS = 1,			// level models
	LUMP_MAP = 2,				// map info

	LUMP_TEXTURENAMES = 5,		// texture name strings

	LUMP_ROADMAP = 7,			// unused lump in Driver 2
	LUMP_ROADS = 8,				// unused lump in Driver 2
	LUMP_JUNCTIONS = 9,			// unused lump in Driver 2
	LUMP_ROADSURF = 10,			// unused lump in Driver 2

	LUMP_MODELNAMES = 12,		// model name strings

	LUMP_ROADBOUNDS = 16,		// unused lump in Driver 2
	LUMP_JUNCBOUNDS = 17,		// unused lump in Driver 2

	LUMP_SUBDIVISION = 20,
	LUMP_LOWDETAILTABLE = 21,	// LOD tables for models
	LUMP_MOTIONCAPTURE = 22,	// motion capture/animation data for peds and Tanner
	LUMP_OVERLAYMAP = 24,		// overlay map
	LUMP_PALLET = 25,			// car palettes
	LUMP_SPOOLINFO = 26,		// level region spooling
	LUMP_CAR_MODELS = 28,		// car models

	LUMP_CHAIR = 33,
	LUMP_TEXTUREINFO = 34,		// texture page info and details (atlases)

	LUMP_LEVELDESC = 35,
	LUMP_LEVELDATA = 36,
	LUMP_LUMPDESC = 37,

	LUMP_STRAIGHTS2 = 40,		// road straights (AI)
	LUMP_CURVES2 = 41,

	LUMP_JUNCTIONS2 = 42,		// previously LUMP_JUNCTIONS2
	LUMP_JUNCTIONS2_NEW = 43,	// Only appear in release Driver2
};

int gStopPadReads = 0;
int gDieWithFade = 0;

int game_over = 0;
int saved_counter = 0;
int saved_leadcar_pos = 0;

int DawnCount = 0;
int current_camera_angle = 2048;

int scr_z = 0;
int FrameCnt = 0;

static int WantPause = 0;
static PAUSEMODE PauseMode = PAUSEMODE_PAUSE;

u_char defaultPlayerModel[2] = { 0 }; // offset 0xAA604
u_char defaultPlayerPalette = 0; // offset 0xAA606

u_int* transparent_buffer;

// system?
int gameinit = 0;
int xa_timeout = 0;

int IconsLoaded = 0;

// OVERLAY
int gLoadedOverlay = 0;

// MOTION_C
int gLoadedMotionCapture = 0;

// DRAWGAME.C ???
int FrAng = 0;
int wetness = 0;

extern SPEECH_QUEUE gSpeechQueue;

// [D] [T]
void InitModelNames(void)
{
	gHubcapModelPtr = FindModelPtrWithName("HUBCAP1");
	gCleanWheelModelPtr = FindModelPtrWithName("CLEANWHEEL");
	gFastWheelModelPtr = FindModelPtrWithName("FASTWHEEL");
	gDamWheelModelPtr = FindModelPtrWithName("DAMWHEEL");
	gPed1HeadModelPtr = FindModelPtrWithName("PEDHEAD1");
	gPed2HeadModelPtr = FindModelPtrWithName("HEAD");
	gPed3HeadModelPtr = FindModelPtrWithName("HEAD");
	gPed4HeadModelPtr = FindModelPtrWithName("HEAD");

	//gBoxModelPtr = FindModelPtrWithName("CRATE");
	//gRotorPtr = FindModelPtrWithName("ROTOR");
	gTrailblazerConeModel = FindModelPtrWithName("GREENCONE");
	gBombModel = FindModelPtrWithName("BOMB");

	MangleWheelModels();
	InitTanner();
	InitAnimatingObjects();
}

int gDemoLevel = 0;

// [D] [T]
void ProcessLumps(char* lump_ptr, int lump_size)
{
	int quit;

	int lump_type;
	int seg_size;
	int* ptr;

	int numLumps = -1;

	quit = 0;
	do {
		lump_type = *(int*)lump_ptr;
		seg_size = *(int*)(lump_ptr + 4);
		ptr = (int*)(lump_ptr + 8);

		if (lump_type == LUMP_LOWDETAILTABLE)
		{
			printInfo("LUMP_LOWDETAILTABLE: size: %d\n", seg_size);
			ProcessLowDetailTable((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_CHAIR)
		{
			printInfo("LUMP_CHAIR: size: %d\n", seg_size);
			ProcessChairLump((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_MOTIONCAPTURE)
		{
			printInfo("LUMP_MOTIONCAPTURE: size: %d\n", seg_size);
			ProcessMotionLump((char*)ptr, seg_size);
			gLoadedMotionCapture = 1;
		}
		else if (lump_type == LUMP_OVERLAYMAP)
		{
			printInfo("LUMP_OVERLAYMAP: size: %d\n", seg_size);
			ProcessOverlayLump((char*)ptr, seg_size);
			gLoadedOverlay = 1;
		}
		else if (lump_type == LUMP_MAP)
		{
			map_lump = (char*)ptr;
		}
		else if (lump_type == LUMP_SPOOLINFO)
		{
			printInfo("LUMP_SPOOLINFO: size: %d\n", seg_size);
			ProcessSpoolInfoLump((char*)ptr, lump_size);
			ProcessMapLump(map_lump, 0);

			// [A] only used in alpha 1.6
			region_buffer_xor = (cells_down >> 5 & 2U | cells_across >> 6 & 1U) << 2;
			sdSelfModifyingCode = sdSelfModifyingCode ^ (sdSelfModifyingCode ^ region_buffer_xor) & 12;
		}
		else if (lump_type == LUMP_CURVES2)
		{
			printInfo("LUMP_CURVES2: size: %d\n", seg_size);
			ProcessCurvesDriver2Lump((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_TEXTURENAMES)
		{
			printInfo("LUMP_TEXTURENAMES: size: %d\n", seg_size);
#ifndef PSX
			// we need to copy texture names
			texturename_buffer = D_MALLOC(seg_size);
			memcpy(texturename_buffer, ptr, seg_size);
#else
			// we need to copy texture names
			texturename_buffer = (char*)ptr;
#endif
		}
		else if (lump_type == LUMP_PALLET)
		{
			printInfo("LUMP_PALLET: size: %d\n", seg_size);
			palette_lump = (char*)ptr;
		}
		else if (lump_type == LUMP_TEXTUREINFO)
		{
			printInfo("LUMP_TEXTUREINFO: size: %d\n", seg_size);
			ProcessTextureInfo((char*)ptr);
		}
		else if (lump_type == LUMP_STRAIGHTS2)
		{
			printInfo("LUMP_STRAIGHTS2: size: %d\n", seg_size);
			ProcessStraightsDriver2Lump((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_JUNCTIONS2_NEW)
		{
			int cnt;

			printInfo("LUMP_JUNCTIONS2_NEW: size: %d\n", seg_size);
			ProcessJunctionsDriver2Lump((char*)ptr, seg_size, 0);

			// put junction flags if any
			cnt = 0;

			while (cnt < NumTempJunctions)
			{
				Driver2JunctionsPtr[cnt].flags = Driver2TempJunctionsPtr[cnt];
				cnt++;
			}

			gDemoLevel = 0; // [A]
		}
		else if (lump_type == LUMP_JUNCTIONS2)
		{
			int cnt;

			printInfo("LUMP_JUNCTIONS2: size: %d\n", seg_size);
			ProcessJunctionsDriver2Lump((char*)ptr, seg_size, 1);

			// put junction flags if any
			cnt = 0;

			while (cnt < NumTempJunctions)
			{
				Driver2JunctionsPtr[cnt].flags = Driver2TempJunctionsPtr[cnt];
				cnt++;
			}

			gDemoLevel = 1; // [A]
			gLoadedMotionCapture = 0;
		}
		else if (lump_type == LUMP_JUNCTIONS)
		{
			printInfo("LUMP_JUNCTIONS: size: %d\n", seg_size);
			ProcessJunctionsLump((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_CAR_MODELS)
		{
			printInfo("LUMP_CAR_MODELS: size: %d\n", seg_size);
			car_models_lump = (char*)ptr;
			KeepCarModelsLump((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_MODELS)
		{
			printInfo("LUMP_MODELS: size: %d\n", seg_size);
			ProcessMDSLump((char*)ptr, seg_size);
			ProcessCarModelLump(car_models_lump, 0);

			if (IS_DRIVER1_CITY(GameLevel))
				D1ScaleCarCosmetics();
			
			InitModelNames();

			SetUpEvents(1);
		}
		else if (lump_type == LUMP_ROADMAP)
		{
			printInfo("LUMP_ROADMAP: size: %d\n", seg_size);
			NewProcessRoadMapLump(&roadMapLumpData, (char*)ptr);
		}
		else if (lump_type == LUMP_ROADS)
		{
			printInfo("LUMP_ROADS: size: %d\n", seg_size);
			ProcessRoadsLump((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_ROADBOUNDS)
		{
			printInfo("LUMP_ROADBOUNDS: size: %d\n", seg_size);
			ProcessRoadBoundsLump((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_JUNCBOUNDS)
		{
			printInfo("LUMP_JUNCBOUNDS: size: %d\n", seg_size);
			ProcessJuncBoundsLump((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_SUBDIVISION)
		{
			printInfo("LUMP_SUBDIVISION: size: %d\n", seg_size);
			ProcessSubDivisionLump((char*)ptr, seg_size);
		}
		else if (lump_type == LUMP_ROADSURF)
		{
			printInfo("LUMP_ROADSURF: size: %d\n", seg_size);
		}
		else if (lump_type == LUMP_MODELNAMES)
		{
			printInfo("LUMP_MODELNAMES: size: %d\n", seg_size);
			modelname_buffer = (char*)ptr;

			// the lump buffer is reused after the level load: keep the names for the map editor
			extern char* gModelNamesCopy;
			free(gModelNamesCopy);
			gModelNamesCopy = (char*)malloc(seg_size + 1);
			memcpy(gModelNamesCopy, ptr, seg_size);
			gModelNamesCopy[seg_size] = 0;
		}
		else if (lump_type == 0xff)
		{
			quit = true;
		}
		else
		{
			printInfo("ERROR - unknown lump type %d\n", lump_type);
			numLumps = lump_type;

			lump_ptr += 4;
			continue;
		}

		lump_ptr = (char*)ptr + ((seg_size + 3) & ~0x3); // aligned to 4-byte boundary

		if (quit)
			return;

		numLumps--;
	} while (numLumps != 0);
}

// [D] [T]
void LoadGameLevel(void)
{
	char* malloc_lump;
	int nsectors;
	int sector;

	extern void EditorLoadEdits(void);	// utils/MapEditor.cpp: EDITS/<CITY>.EDIT.json
	EditorLoadEdits();

	seated_pedestrian = NULL;
	LoadCosmetics(BASE_CITY);

	if (gMultiplayerLevels == 0)
	{
		if (gTimeOfDay == TIME_NIGHT)
			SetCityType(CITYTYPE_NIGHT);
		else
			SetCityType(CITYTYPE_DAY);
	}
	else
	{
		if (gTimeOfDay == TIME_NIGHT)
			SetCityType(CITYTYPE_MULTI_NIGHT);
		else
			SetCityType(CITYTYPE_MULTI_DAY);
	}

	ReportMode(0);

	sector = citylumps[GameLevel][CITYLUMP_DATA1].x / CDSECTOR_SIZE;
	nsectors = citylumps[GameLevel][CITYLUMP_DATA1].y / CDSECTOR_SIZE;

	loadsectors((char*)_primTab1, sector, nsectors);

	sector += nsectors;

	// CITYLUMP_DATA1 - load-time lump
	ProcessLumps((char*)_primTab1 + 8, nsectors * CDSECTOR_SIZE);
	
	// [A] the palette lump and the permanent pages need the car texture pages of the city
	if (IS_DRIVER1_CITY(GameLevel))
		D1SetupCarTpages(car_models_lump);

	// CITYLUMP_TPAGE is right next after DATA1
	LoadPermanentTPages(&sector);

	sector = citylumps[GameLevel][CITYLUMP_DATA2].x / CDSECTOR_SIZE;
	nsectors = citylumps[GameLevel][CITYLUMP_DATA2].y / CDSECTOR_SIZE;

	D_MALLOC_BEGIN();
	malloc_lump = D_MALLOC(nsectors * CDSECTOR_SIZE);
	D_MALLOC_END();

	loadsectors(malloc_lump, sector, nsectors);

	sector += nsectors;
	
	// CITYLUMP_DATA2 - in-memory lump
	ProcessLumps(malloc_lump + 8, (nsectors * CDSECTOR_SIZE));	

	SpoolLumpOffset = citylumps[GameLevel][CITYLUMP_SPOOL].x;

	//Init_Reflection_Mapping();	// [A] I know that this is obsolete and used NOWHERE
	InitDebrisNames();
	InitShadow();
	//InitTextureNames();			// [A] I know that this is obsolete and used NOWHERE
	
#if USE_PC_FILESYSTEM
	extern int gContentOverride;

	if (gContentOverride)
	{
		// [A] override textures
		LoadPermanentTPagesFromTIM();
	}
#endif
	
	ReportMode(1);
}

// [D] [T]
void State_GameInit(void* param)
{
	STREAM_SOURCE* plStart;
	int i, musicType;
	char padid;

	if (NewLevel)
	{
#if USE_CRT_MALLOC
		sys_freeall();
		malloctab = D_MALLOC(0x200000);
#endif // USE_CRT_MALLOC
		
		mallocptr = (char*)malloctab;

		// 4 regions, 1024 packed cell pointers
		D_MALLOC_BEGIN();
		packed_cell_pointers = D_MALLOC(1024 * 4);
		D_MALLOC_END();
	}
	else
	{
		SetPleaseWait(NULL);
	}

	gameinit = 1;

	InitGameVariables();
	InitEvents();
	InitPadRecording();
	InitSpeechQueue(&gSpeechQueue);

	if (NewLevel)
	{
		leadAIRequired = 0;
		leadAILoaded = 0;
		pathAILoaded = 0;
	}

	LoadMission(gCurrentMissionNumber);

	if (gCurrentMissionNumber == 38)
		residentCarModels[SPECIAL_CAR_SLOT] = 9;

	if (GameType == GAME_MISSION)
		SetupFadePolys();

	if (NewLevel)
		ShowLoadingScreen(LoadingScreenNames[GameLevel], 1, 36);

	if (AttractMode)
	{
		TriggerInGameCutscene(0);
		NoPlayerControl = 1;
	}

	ResetSound();

	LockChannel(0);
	LockChannel(1);
	LockChannel(2);

	if (NumPlayers == 2)
	{
		LockChannel(3);
		LockChannel(4);
		LockChannel(5);
#ifdef DEBUG
		printInfo("*---LOCKED P2 CHANS---*\n");
#endif
	}

	if (NewLevel != 0)
	{
		ShowLoading();
		LoadLevelSFX(gCurrentMissionNumber);
	}

	PauseSound();
	ThisMotion = 0;

	// [A] Driver 1 music support
#ifndef PSX
	if (gDriver1Music)
	{
		if (GameType == GAME_TAKEADRIVE)
		{
			if(BASE_CITY == 0)
				musicType = 0 + (gCurrentMissionNumber & 1);
			else if (BASE_CITY == 1)
				musicType = 5 + (gCurrentMissionNumber & 1);
			else if (BASE_CITY == 2)
				musicType = 2 + (gCurrentMissionNumber & 1) * 5;
			else if (BASE_CITY == 3)
				musicType = 3 + (gCurrentMissionNumber & 1);
		}
		else
		{
			musicType = gCurrentMissionNumber % 8;
		}
	}
	else 
#endif
	if (BASE_CITY == 1)
	{
		musicType = 1;

		if ((gCurrentMissionNumber & 1U) != 0)
			musicType = 5;
	}
	else if (BASE_CITY == 0)
	{
		musicType = 2;

		if ((gCurrentMissionNumber & 1U) != 0)
			musicType = 6;
	}
	else if (BASE_CITY == 2)
	{
		musicType = 0;

		if ((gCurrentMissionNumber & 1U) == 0)
			musicType = 3;
	}
	else if (BASE_CITY == 3)
	{
		musicType = 4;

		if ((gCurrentMissionNumber & 1U) != 0)
			musicType = 7;
	}

	InitMusic(musicType);

	if (NewLevel == 0)
	{
		if (IconsLoaded == 0)
			ReloadIcons();

		IconsLoaded = 1;
		SetUpEvents(0);
	}
	else
	{
		LoadGameLevel();
		IconsLoaded = 1;
		LoadSky();
		LoadFont(NULL);
	}

	ClearMem((char*)car_data, sizeof(car_data));

	MainPlayer.spoolXZ = (VECTOR*)car_data[0].hd.where.t;
	car_data[0].hd.where.t[0] = PlayerStartInfo[0]->position.vx;
	car_data[0].hd.where.t[2] = PlayerStartInfo[0]->position.vz;

	CalcObjectRotationMatrices();
	InitialiseDenting();

	cameraview = 0;

	if (gLoadedMotionCapture != 0)
		InitPedestrians();

	InitSpooling();
	InitMap();
	InitSpecSpool();

	if (NewLevel == 0 && allowSpecSpooling == 1)
	{
		QuickSpoolSpecial();
	}

	int_garage_door();
	SpoolSYNC();

	InitialiseCarHandling();
	ClearMem((char*)player, sizeof(player));

	InitDrivingGames();
	InitThrownBombs();

	for (i = 0; i < numPlayersToCreate; i++)
	{
		plStart = PlayerStartInfo[i];
		padid = -i;

#if defined(DEBUG_OPTIONS) && !defined(PSX)
		extern int gForceOnFoot;

		if (gForceOnFoot)
		{
			plStart->type = 2;
			plStart->model = 0;
		}
#endif

		gStartOnFoot = (plStart->type == 2);

		if (i < NumPlayers)
			padid = i;

		InitPlayer(&player[i], &car_data[i], plStart->controlType, plStart->rotation, (LONGVECTOR4 *)&plStart->position, plStart->model, plStart->palette, &padid);

		if (!(plStart->type == 2))
		{
			car_data[i].ap.damage[0] = plStart->damage[0];
			car_data[i].ap.damage[1] = plStart->damage[1];
			car_data[i].ap.damage[2] = plStart->damage[2];
			car_data[i].ap.damage[3] = plStart->damage[3];
			car_data[i].ap.damage[4] = plStart->damage[4];
			car_data[i].ap.damage[5] = plStart->damage[5];

			car_data[i].totalDamage = plStart->totaldamage;

			car_data[i].ap.needsDenting = 1;
		}
	}

	if (pathAILoaded)
		InitCops();

	InitCamera(&MainPlayer);

	if (gLoadedOverlay && NoPlayerControl == 0)
	{
		InitOverlays();
		IconsLoaded = 0;
	}

	SetupRain();
	InitExObjects();

	if (NewLevel)
	{
		// alloc pointer list
		// [A] use model_tile_ptrs for this since it is only used for drawing purposes
		coplist = (CELL_OBJECT**)(model_tile_ptrs);
		pcoplist = (PACKED_CELL_OBJECT**)(model_tile_ptrs + 160);
	}

	if (NoPlayerControl == 0)
	{
		if (!_CutRec_IsOn())
			DeleteAllCameras();
	}
	else
	{
		FindNextChange(CameraCnt);
	}

	FrAng = 512;

	if (gWeather == WEATHER_RAIN)
		wetness = 7000;
	else
		wetness = 0;

	if (gTimeOfDay == TIME_DUSK)
	{
		for ( i = 0; i < MAX_CARS; i++)
			lightsOnDelay[i] = (i * 11);
	}

	tracking_car = 1;

	if (NoPlayerControl == 0)
		StoreGameFlags();

	SetReverbState(0);

	for (i = 0; i < 2; i++)
	{
		player[i].horn.request = 0;
		player[i].horn.time = 0;
		player[i].horn.on = 0;
	}

#if defined(DEBUG) || defined(PSX)
	printInfo("malloctab 0x%08x to 0x%08x (%d bytes) (Final: 0x%08x)\n", &malloctab, mallocptr, mallocptr - malloctab, mallocptr);

	if (mallocptr < malloctab + PSX_MALLOC_SIZE)
		printInfo("COOL: Level fits on blue station by %d bytes!\n", malloctab + PSX_MALLOC_SIZE - mallocptr);
	else
		printWarning("WARNING: Level too big to fit on blue station by %d bytes!\n", mallocptr - (malloctab + PSX_MALLOC_SIZE));
#endif

	gShowPlayerDamage = 1;
	InitThunder();
	GetXAData(-1);
	SetXAVolume(0);

	switch (gCurrentMissionNumber)
	{
		case 2:
		case 3:
		case 6:
		case 11:
		case 13:
		case 14:
		case 19:
		case 26:
		case 28:
		case 34:
		case 38:
			FunkUpDaBGMTunez(1);
	}

	xa_timeout = 0;

	//-------------------------
	// GameLoop start part

	if (NewLevel)
	{
#ifdef PSX
		CloseShutters(2, 320, 512);
#else
		CloseShutters(16, 320, 512);
#endif // PSX
	}

	DisableDisplay();
	SetupDrawBuffers();
	EnableDisplay();

	srand(0x1234);

	cameraview = 0;
	FrameCnt = 0;
	NoTextureMemory = 0;
	WantPause = 0;
	gDrawPauseMenus = 0;

	SpoolSYNC();

	if (CurrentGameMode != GAMEMODE_DIRECTOR)
		UnPauseSound();

	StartGameSounds();

	SetMasterVolume(gMasterVolume);
	SetXMVolume(gMusicVolume);

	CloseControllers();
	InitControllers();
	VSync(0);

	for (i = 0; i < 5; i++)
	{
		ReadControllers();
		VSync(0);
	}
	
#ifdef PSX
	inittimer(120);
	Clock_SystemStartUp();
#endif

	// switch to STATE_GAMELOOP
	SetState(STATE_GAMELOOP);

#ifndef PSX
	// set to 30 FPS VSync
	PsyX_SetSwapInterval(2);
	PsyX_EnableSwapInterval(1);
#endif
}

extern short paddp;
extern short padd;
unsigned short controller_bits = 0;

VECTOR lis_pos;

int gLightsOn = 0;
int NightAmbient = 0;

#ifndef PSX
int gPlayerLights = 0;			// the player's own car lamps: 0 = like every car, 1 = on, 2 = off (cars.c CarLightsOn)
int gPlayerLightsToggle = 0;	// set by the L key (redriver2_psxpc.cpp GameKeys)
extern int gSnowTarget;
#endif

char CameraChanged = 0;
char CamerasSaved = 0;
char paused = 0;

char gRightWayUp = 0;	// cheat

int num_active_cars = 0;
u_int lead_pad = 0;

int numInactiveCars = 0;

VECTOR leadcar_pos;

// [D] [T]
void StepSim(void)
{
	static u_int t0; // offset 0x0
	static char t1; // offset 0x4
	static char t2; // offset 0x5

	char padAcc;
	short* playerFelony;
	int stream;
	CAR_DATA* cp;
	PLAYER* pl;
	int i, j;
	int car;
	int timeOfDay;

	DayCycleStep();

	if (gNightSwapSteps && CameraCnt > 0 && CameraCnt % gNightSwapSteps == 0)
		SpoolSwapDayNight();

	if (M_BIT(gTimeOfDay) & (M_BIT(TIME_DAWN) | M_BIT(TIME_DUSK)))
	{
		DawnCount++;
	}

	SetUpTrafficLightPhase();
	MoveSmashable_object();
	//animate_garage_door();
	StepEvents();
	HandleMission();
	HandleInGameCutscene();
	HandleDrivingGames();

	num_active_cars = 0;

	if (NoPlayerControl && ReplayParameterPtr->RecordingEnd - 2 < CameraCnt)
	{
		ReleaseInGameCutscene();
		pauseflag = 1;
	}

	lead_pad = (u_int)controller_bits;

	playerFelony = GetPlayerFelony(&MainPlayer);

	// control cop roadblocks
	if (*playerFelony <= FELONY_ROADBLOCK_MIN_VALUE || numRoadblockCars != 0)
	{
		if (roadblockCount != 0)
		{
			roadblockCount--;
		}
	}
	else
	{
		if (roadblockCount != 0)
		{
			roadblockCount--;
		}
		else
		{
			if (copsAreInPursuit != 0 && residentCarModels[3] == 0 && gCurrentMissionNumber != 26)
				requestRoadblock = 1;

			if (roadblockCount != 0)
				roadblockCount--;
		}
	}

	// control civcars pingin/pingout
	if (requestStationaryCivCar != 1 && requestRoadblock == 0)
	{
		if (gInGameChaseActive || _CutRec_IsPlaying())
		{
			// it will use ping buffer
			// checks are done internally
			for (i = 0; i < 10; i++)
				PingInCivCar(15900);
		}
		else if (numCivCars < MaxCivCars() && (NumPlayers == 1 || (NumPlayers == 2 && GameType == GAME_COPSANDROBBERS)))
		{
			// make 5 tries (crowd option: 5 per extra car multiple)
#ifndef PSX
				UpdateCivDeficit();
#endif
			for (i = 0; i < 5 * gCrowdMul * (gCivDeficit ? 3 : 1); i++)	// three times as many tries while the circle round the player is short of cars
			{
				if (PingInCivCar(15900))
					break;
			}
		}

		SetUpCivCollFlags();
	}
	else
	{
		distFurthestCivCarSq = 0;
	}

	numRoadblockCars = 0;
	numInactiveCars = 0;
	numParkedCars = 0;
	numActiveCops = 0;
	numCopCars = 0;
	numCivCars = 0;

	cp = car_data;

	// count cars
	while (cp < &car_data[MAX_CARS])
	{
		if (cp->controlType == CONTROL_TYPE_CIV_AI)
		{
			numCivCars++;

			if (cp->controlFlags & CONTROL_FLAG_COP)
				numCopCars++;

			if (cp->ai.c.thrustState == CIV_AI_THRUST_STOP && cp->ai.c.ctrlState == CIV_AI_CTRL_PARKED)
				numParkedCars++;

			if (cp->controlFlags & CONTROL_FLAG_COP_SLEEPING)
				numRoadblockCars++;
		}
		else if (cp->controlType == CONTROL_TYPE_PURSUER_AI)
		{
			numCopCars++;

			if (cp->ai.p.dying == 0)
			{
				numActiveCops++;
			}
		}
		else if (cp->controlType == CONTROL_TYPE_NONE)
		{
			numInactiveCars++;
		}

		cp++;
	}

	if (numRoadblockCars != 0)
		roadblockCount = roadblockDelay;

	if (requestStationaryCivCar == 0 && requestRoadblock != 0 && numRoadblockCars == 0 && MaxCivCars() - numCivCars > 4)
	{
		CreateRoadblock();
	}

	if (gInGameCutsceneActive == 0)
	{
		CheckSpecialSpool();
	}

	// update cars
	cp = car_data;
	while (cp < &car_data[MAX_CARS])
	{
		switch (cp->controlType)
		{
			case CONTROL_TYPE_PLAYER:
				t0 = Pads[*cp->ai.padid].mapped;	// [A] padid might be wrong
				t1 = Pads[*cp->ai.padid].mapanalog[2];
				t2 = Pads[*cp->ai.padid].type & 4;

				// [A] handle REDRIVER2 dedicated car exit button
				if(t0 & CAR_PAD_LEAVECAR_DED)
				{
					t0 &= ~CAR_PAD_LEAVECAR_DED;
					t0 |= CAR_PAD_LEAVECAR;
				}

#if defined(DEBUG_OPTIONS) && !defined(PSX)
				if (getenv("REDRIVER2_AUTODRIVE"))
				{
					t0 |= ((FrameCnt / 600) % 4 == 3) ? CAR_PAD_BRAKE : CAR_PAD_ACCEL;
					t1 = (int)(50.0 * sin(FrameCnt * 0.011));
					t2 = 1;
				}
#endif

				if (NoPlayerControl == 0)
				{
					if (gStopPadReads)
					{
						t0 = CAR_PAD_BRAKE;

						if (cp->hd.wheel_speed <= 0x9000)
							t0 = CAR_PAD_HANDBRAKE;

						t1 = 0;
						t2 = 1;
					}

					cjpRecord(*cp->ai.padid, &t0, &t1, &t2);
				}
				else
				{
					cjpPlay(*cp->ai.padid, &t0, &t1, &t2);
				}

				ProcessCarPad(cp, t0, t1, t2);
				break;
			case CONTROL_TYPE_CIV_AI:
				CivControl(cp);
				break;
			case CONTROL_TYPE_PURSUER_AI:
				CopControl(cp);
				break;
			case CONTROL_TYPE_LEAD_AI:
				t2 = 0;
				t1 = 0;
				t0 = 0;

				t0 = FreeRoamer(cp);

				if (t0 == 0)
				{
					cp->handbrake = 1;
					cp->wheel_angle = 0;
				}
				else
				{
					ProcessCarPad(cp, t0, t1, t2);
				}

				break;
			case CONTROL_TYPE_CUTSCENE:
				if (!_CutRec_RecordCarPad(cp, &t0, &t1, &t2))
					cjpPlay(-*cp->ai.padid, &t0, &t1, &t2);
			
				ProcessCarPad(cp, t0, t1, t2);
		}

		StepCarPhysics(cp);
		cp++;
	}

	// Update players
	for (i = 0; i < MAX_PLAYERS; i++)
	{
		pl = &player[i];

		if (pl->playerType != PLAYER_TYPE_PEDESTRIAN)
			continue;

		stream = pl->padid;

		if (stream < 0) // Is cutscene stream?
		{
			if (!_CutRec_RecordPad(pl, &t0, &t1, &t2) && cjpPlay(-stream, &t0, &t1, &t2) != 0)
				ProcessTannerPad(pl->pPed, t0, t1, t2);

			continue;
		}

		if (Pads[stream].type == 4)
		{
			padAcc = Pads[stream].mapanalog[3];

			// walk back
			if (padAcc < -64)
			{
				if(padAcc < -100)
					Pads[stream].mapped |= 0x1000;
				else
					Pads[stream].mapped |= 0x1008;
			}
			else if (padAcc > 32)
			{
				stream = pl->padid;
				Pads[stream].mapped |= 0x4000;
			}
		}

		t0 = Pads[stream].mapped;
		t1 = Pads[stream].mapanalog[2];
		t2 = Pads[stream].type & 4;

		// [A] handle REDRIVER2 dedicated car entry button
		if (t0 & TANNER_PAD_ACTION_DED)
		{
			t0 &= ~TANNER_PAD_ACTION_DED;
			t0 |= TANNER_PAD_ACTION;
		}

		if (NoPlayerControl == 0)
		{
			if (gStopPadReads)
			{
				t2 = 0;
				t1 = 0;
				t0 = 0;
			}

			cjpRecord(stream, &t0, &t1, &t2);
		}
		else
		{
			if (cjpPlay(stream, &t0, &t1, &t2) == 0)
				continue;
		}

		ProcessTannerPad(pl->pPed, t0, t1, t2);
	}

	if (requestStationaryCivCar == 1 && (numCivCars < MaxCivCars() || (PingOutCar(&car_data[furthestCivID]), numCivCars < MaxCivCars())))
	{
		requestStationaryCivCar = 0;
	}

	if (!game_over)
	{
		if(pathAILoaded)
			ControlCops();

		if (gLoadedMotionCapture)
			HandlePedestrians();
	}

	GlobalTimeStep();
	UpdatePlayers();
	DoScenaryCollisions();
	CheckPlayerMiscFelonies();

	CameraCnt++;

	pl = player;

	// deal with car horns
	for (i = 0; i < NumPlayers; i++)
	{
		int playerCarId;

		playerCarId = pl->playerCarId;

		if (playerCarId >= 0)
		{
			if (pl->horn.on == 0)
			{
				if (CarHasSiren(car_data[playerCarId].ap.model) == 0 && pl->pPed == NULL)
				{
					stream = i != 0 ? 5 : 2;
					StopChannel(stream);

					pl->horn.request = 0;
					pl->horn.time = 0;
				}
			}
			else
			{
				int spuKeys;

				spuKeys = SPU_KEYCH(i != 0 ? 5 : 2);

				if (SpuGetKeyStatus(spuKeys) == 0)
				{
					if (CarHasSiren(car_data[pl->playerCarId].ap.model) == 0 && pl->pPed == NULL && pl->horn.request == 0)
						pl->horn.request = 1;

					pl->horn.time = 11;
				}

			}

			DealWithHorn(&pl->horn.request, i);
		}
		pl++;
	}

	ChopperStep();

	SoundTasks();

	static int stupid_logic[4];

#if MAX_TYRE_TRACK_PLAYERS > 2
	if (gInGameCutsceneActive != 0)
	{
		for (i = 0; i < 4; i++)
		{
			stupid_logic[i] = player[i + NumPlayers].playerCarId;
		}
	}
	else
#endif
	{
		// "Car Bomb"?
		if (gInGameCutsceneActive != 0 && gCurrentMissionNumber == 23 && gInGameCutsceneID == 0)
			stupid_logic[0] = 2;
		else
			stupid_logic[0] = MainPlayer.playerCarId;

		stupid_logic[1] = SecondPlayer.playerCarId;
		stupid_logic[2] = gThePlayerCar;
		stupid_logic[3] = MainPlayer.targetCarId; // [A]
	}

	for (i = 0; i < 3; i++)
	{
		for (j = i+1; j < 4; j++)
		{
			if (stupid_logic[i] == stupid_logic[j])
				stupid_logic[j] = -1;
		}
	}

	for (car = 0, i = 0; car < 4; car++)
	{
		if (stupid_logic[car] != -1 && SilenceThisCar(car) == 0)
		{
			CheckCarEffects(&car_data[stupid_logic[car]], i);
			SwirlLeaves(&car_data[stupid_logic[car]]);
			
			i++;
		}
	}

	// save car positions
	if (gStopPadReads == 1 && lead_car != 0)
	{
		saved_counter++;

		if (saved_counter > 20 && saved_leadcar_pos == 0)
		{
			leadcar_pos.vx = car_data[lead_car].hd.where.t[0];
			leadcar_pos.vy = car_data[lead_car].hd.where.t[1];
			leadcar_pos.vz = car_data[lead_car].hd.where.t[2];

			saved_leadcar_pos = 1;
		}
	}

	// XA playback timeout
	if (gInGameCutsceneActive == 0 && XAPrepared())
	{
		xa_timeout--;

		if (xa_timeout == 0)
		{
			StopXA();
			UnprepareXA();
			StartSpooling();
		}
	}
}

// [A] checks VSync if can time step
int FilterFrameTime()
{
	static int frame = 0;

	// always stay 30 FPS (2 vblanks)
	if (VSync(-1) - frame < 2)
		return 0;

	frame = VSync(-1);

	return 1;
}

// [D] [T]
void StepGame(void)
{
	int i;
	PLAYER* pl;

	if (CameraCnt == 3 && !pauseflag)
		StartXM(gDriver1Music);

	if (doSpooling)
	{
		CheckValidSpoolData();
		ControlMap();
	}

	if (gTimeOfDay == TIME_NIGHT)
		PreLampStreak();

	if ((padd & 0x2000U) && (padd & 0x8000U))
		padd &= ~0xA000;

	controller_bits = padd;

	for (i = 0; i < NumPlayers; i++)
	{
		pl = &player[i];
		if (pl->horn.time == 0 || pl->horn.on == 0)
			pl->horn.time = 0;
		else
			pl->horn.time--;
	}

	ModifyCamera();

	lis_pos = camera_position;

	// update colours of ambience
	if (gTimeOfDay == TIME_DAWN)
	{
		NightAmbient = (DawnCount >> 7) + 26;
		gLightsOn = (DawnCount < 4000);

		if (NightAmbient > 96)
			NightAmbient = 96;
	}
	else if (gTimeOfDay == TIME_DAY)
	{
		gLightsOn = 0;

		if (gWeather - 1U > 1)
			NightAmbient = 128;
		else
			NightAmbient = 78;
	}
	else if (gTimeOfDay == TIME_DUSK)
	{
		if (DawnCount < 3000)
		{
			gLightsOn = 0;
		}
		else
		{
			gLightsOn = 1;

			for (i = 0; i < MAX_CARS; i++)
			{
				if (lightsOnDelay[i] > 0)
					lightsOnDelay[i]--;
			}
		}

		if (gWeather - 1U < 2)
			NightAmbient = 78 - (DawnCount >> 7);
		else
			NightAmbient = 96 - (DawnCount >> 5);

		if (NightAmbient < 45)
			NightAmbient = 45;
	}
	else if (gTimeOfDay == TIME_NIGHT)
	{
		gLightsOn = 1;
		NightAmbient = 128;
	}

	DayCycleLight();

#ifndef PSX
	// fog, rain and snow: car lamps and street lights burn at any time of day, so they glow and reflect on the road
	if (gFogTarget || gSnowTarget || gWeather == WEATHER_RAIN)
		gLightsOn = 1;

	// L key or controller D-pad down switches the player's own lamps, the first press against what they show now.
	// The keyboard maps the Down arrow to square and D-pad down together: that is braking, not a switch.
	if (gPlayerLightsToggle || ((Pads[0].mapnew & MPAD_D_DOWN) && !(Pads[0].mapped & MPAD_SQUARE)))
	{
		if (player[0].playerType == PLAYER_TYPE_CAR && player[0].playerCarId >= 0)
			gPlayerLights = CarLightsOn(&car_data[player[0].playerCarId]) ? 2 : 1;

		gPlayerLightsToggle = 0;
	}
#endif

	if (gWeather == WEATHER_RAIN)
	{
		DoLightning();
		DoThunder();
	}

	HandleExplosion();

	combointensity = NightAmbient | NightAmbient << 8 | NightAmbient << 0x10;

	if (NoPlayerControl && AttractMode == 0)
		ShowReplayOptions();

	// process fast forward
	if (FastForward != 0 && CameraCnt < ReplayParameterPtr->RecordingEnd - 1)
	{
		int color;
		color = CameraCnt & 0x1f;

		if (color > 15)
			color = 32 - color;

		SetTextColour((color & 0x1f) << 3, 0, 0);
		PrintStringFeature(G_LTXT(GTXT_Fastforward), 100, 0x1e, 0x1000, 0x1000, 0);
	}

	if (NoPlayerControl == 0)
	{
		if (pad_connected < 1 && FrameCnt > 2 && bMissionTitleFade == 0 && gInGameCutsceneActive == 0)
		{
			pauseflag = 1;
			PauseSound();
		}

		if (NoPlayerControl == 0 && NoTextureMemory != 0)
			NoTextureMemory--;
	}

	CameraChanged = 0;
	old_camera_change = camera_change;

	// do camera changes
	if (gInGameCutsceneActive != 0)
		camera_change = CutsceneCameraChange(CameraCnt);
	else if (pauseflag == 0 && NoPlayerControl != 0)
		camera_change = CheckCameraChange(CameraCnt);
	else
		camera_change = 0;

	// step physics engine
	if (pauseflag)
	{
		if (!NoPlayerControl && !AttractMode && !game_over)
		{
			if (pad_connected < 1)
				EnablePause(PAUSEMODE_PADERROR);
			else
				EnablePause(PAUSEMODE_PAUSE);
		}
		else if(quick_replay && !paused)
		{
			WantPause = 1;
			PauseMode = (PAUSEMODE)gMissionCompletionState;
		}

		paused = 1;
	}
	else
	{
		StepSim();
		UpdatePlayerInformation();

		if (FastForward == 0)
			ColourCycle();

		if (gDieWithFade != 0)
			gDieWithFade++;

		if (paused)
		{
			CamerasSaved = 1;
			paused = 0;
		}
	}

	if (NoPlayerControl && AttractMode == 0)
		ControlReplay();

	// player flip cheat
	if (gRightWayUp)
	{
		TempBuildHandlingMatrix(&car_data[MainPlayer.playerCarId], 0);
		gRightWayUp = 0;
	}

	if (AttractMode && (paddp || ReplayParameterPtr->RecordingEnd <= CameraCnt))
		EndGame(GAMEMODE_QUIT);
}

// [D] [T]
void CheckForPause(void)
{
	int ret;

	if (gDieWithFade == 16 && (quick_replay || !NoPlayerControl))
	{
		PauseMode = PAUSEMODE_GAMEOVER;
		WantPause = 1;

		gDieWithFade = 32;
	}

	// check pads for pause here
	if (AttractMode == 0 && pauseflag == 0)
	{
#ifndef PSX
		// [A] Car Spawner showroom: left / right change the car, A or Enter confirms it
		CarOverrideUpdate();

		if (CarSpawnerUpdate(paddp))
			return;
#endif

		if (FrameCnt > 2)
		{
			if (NumPlayers == 1)
			{
				if (paddp == MPAD_START && bMissionTitleFade == 0) // [A] && gInGameCutsceneActive == 0)		// allow pausing during cutscene
				{
					EnablePause(PAUSEMODE_PAUSE);
				}
			}
			else if (paddp == MPAD_START)
			{
				EnablePause(PAUSEMODE_PAUSEP1);
			}
			else if (NumPlayers == 2 && (Pads[1].dirnew & 0x800))
			{
				EnablePause(PAUSEMODE_PAUSEP2);
			}
		}
	}
	
	if (WantPause)
	{
		WantPause = 0;
		pauseflag = 1;

		PauseSound();
		ShowPauseMenu(PauseMode);
	}

	if (gDrawPauseMenus)
	{
		ret = UpdatePauseMenu(PauseMode);

		switch (ret)
		{
		case MENU_QUIT_CONTINUE:
			pauseflag = 0;
			break;
		case MENU_QUIT_QUIT:
			EndGame(GAMEMODE_QUIT);
			break;
		case MENU_QUIT_RESTART:
			EndGame(GAMEMODE_RESTART);
			break;
		case MENU_QUIT_DIRECTOR:
			EndGame(GAMEMODE_DIRECTOR);
			break;
		case MENU_QUIT_QUICKREPLAY:
			EndGame(GAMEMODE_REPLAY);
			break;
		case MENU_QUIT_NEXTMISSION:
			EndGame(GAMEMODE_NEXTMISSION);
			break;
		}

		if (ret != 0 && !game_over)
		{
			UnPauseSound();
		}
	}
}

int gMultiStep = 0;

// [D] [T]
#if defined(DEBUG_OPTIONS) && !defined(PSX)
// frame cost log (-shot runs): milliseconds per frame spent in the simulation, building the primitives (RenderGame2) and in the GL submission (EndScene)
#include <chrono>
extern int gDrawStats[4];
static double gStepMs, gBuildMs, gSubmitMs;
static int gTimedFrames;

static double NowMs(void)
{
	return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
#endif

void State_GameLoop(void* param)
{
	int cnt;

	if (gSkipInGameCutscene)
	{
		StepGame();
		ClearCurrentDrawBuffers();
		return;
	}

	if (!FilterFrameTime())
		return;

	UpdatePadData();
	CheckForPause();

	// moved from StepGame
	if (FrameCnt == 5)
		SetDispMask(1);

#ifdef PSX
	static int lastTime32Hz = 0;

	int curTime = clock_realTime.time32Hz;
	int numFrames = curTime - lastTime32Hz;

	if (FastForward)
		cnt = 7;
	else
		cnt = gMultiStep ? numFrames : 1;

	if (cnt)
		lastTime32Hz = curTime;

	// don't do more than 3 frames in non-fastforward mode
	if (!FastForward && cnt > 3)
		cnt = 0;

	while (--cnt >= 0)
	{
		if(cnt != 0)
			InitCamera(&MainPlayer);

		StepGame();
	}
#else
	cnt = FastForward ? 7 : 1;

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	double stepStart = NowMs();
#endif

	while (--cnt >= 0)
		StepGame();

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	if (FrameCnt > 40)
		gStepMs += NowMs() - stepStart;
#endif
#endif

	_CutRec_Draw();
	DrawGame();
	
	if (game_over)
		SetState(STATE_GAMECOMPLETE);

	_CutRec_Step();
}

int ObjectDrawnValue = 0;

extern int num_pedestrians;
int gPlayLog = 0;	// config.ini [game] playLog=1: one line per second about the player and the world around it, to find rendering problems
int gPlayLogMark = 0;	// F9 while playLog=1: one extra line right now, marked MARK (press it when you see the glitch)

#if defined(DEBUG_OPTIONS) && !defined(PSX) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
extern void PsyX_TakeScreenshot();
extern "C" void GR_SaveVRAM(const char* outputFileName, int x, int y, int width, int height, int bReadFromFrameBuffer);
int gShotFrame = 0;	// -shot <frame>: saves SCREENSHOT.BMP (bottom-up) and VRAM.TGA at that game frame, for automated tests
#endif

#if defined(DEBUG_OPTIONS) && !defined(PSX)
extern int gCamTest;	// -camtest: scripted right stick input for the camera orbit, see camera.c
#endif

#if defined(DEBUG_OPTIONS) && !defined(PSX)
extern int gCameraAngle;
int gCamAngleOverride = 0;	// -camangle <n>: chase camera angle around the car (2048 = behind it), to look at a car from the side
int gCamDown = 0;			// -camdown <n>: chase camera pitch in 4096ths of a turn (positive looks down, 1024 straight down), for aerial shots
int gForceOnFoot = 0;		// -onfoot: the player starts as Tanner on foot (test)
int gPedTest = 0;			// -pedtest: three pedestrians with different palettes stand in front of the player (test)
int gForceLightsOn = 0;		// -lightson: car lights on in daylight, to check where the lights sit on a car
int gLampDamage = 0;		// -lampdamage <mask>: wreck the front lamps of the player car (bit 0 = left, bit 1 = right), to check that broken lamps stay dark
#endif

#if defined(DEBUG_OPTIONS) && !defined(PSX) && defined(_WIN32)
// REDRIVER2_PERFLOG=<frames>: every that many frames one "perf:" line (average and worst frame time, memory of the process);
// REDRIVER2_AUTODRIVE=1: the player car drives on its own (gas, weaving steering, reverse now and then), for long leak runs
struct PerfProcessMemory { unsigned long cb, pageFaults; size_t peakWs, ws, a, b, c, d, pagefile, peakPagefile, privateUsage; };
extern "C" __declspec(dllimport) void* __stdcall GetCurrentProcess(void);
extern "C" __declspec(dllimport) int __stdcall K32GetProcessMemoryInfo(void*, PerfProcessMemory*, unsigned long);

static void PerfLogFrame(void)
{
	static int interval = -1, count = 0;
	static double sum = 0, worst = 0, last = 0, workWorst = 0, workSum = 0, lastWork = 0;
	double now, dt;
	PerfProcessMemory mem = { sizeof(mem) };

	if (interval < 0)
	{
		const char* e = getenv("REDRIVER2_PERFLOG");
		interval = e ? atoi(e) : 0;
	}

	if (interval <= 0 || FrameCnt <= 40)
		return;

	now = NowMs();
	dt = now - last;
	last = now;

	if (dt > 1000.0)
		return;

	sum += dt;
	worst = MAX(worst, dt);
	workSum = gStepMs + gBuildMs + gSubmitMs;
	workWorst = MAX(workWorst, workSum - lastWork);
	lastWork = workSum;

	if (++count < interval)
		return;

	K32GetProcessMemoryInfo(GetCurrentProcess(), &mem, sizeof(mem));
	printInfo("perf: frame %d avg %.2f ms worst %.2f ms, work worst %.2f avg %.2f ms, player %d %d, working set %d MB, private %d MB\n", FrameCnt, sum / count, worst, workWorst, workSum / MAX(gTimedFrames, 1), player[0].pos[0], player[0].pos[2], (int)(mem.ws >> 20), (int)(mem.privateUsage >> 20));
	count = 0;
	sum = worst = workWorst = 0;
}
#endif

// [D] [T]
void DrawGame(void)
{
#if defined(DEBUG_OPTIONS) && !defined(PSX)
	if (gCamAngleOverride)
		gCameraAngle = gCamAngleOverride;

	if (gForceLightsOn)
		gLightsOn = 1;

	if (gLampDamage && MainPlayer.playerType == PLAYER_TYPE_CAR)
	{
		car_data[MainPlayer.playerCarId].ap.damage[0] = (gLampDamage & 1) ? 1500 : 0;
		car_data[MainPlayer.playerCarId].ap.damage[1] = (gLampDamage & 2) ? 1500 : 0;
	}

	// REDRIVER2_PLAYERMODEL=<0 Tanner, 1 Jericho, 2 pedestrian>: the player gets out as that model at step 40 (test of the player model switch)
	if (getenv("REDRIVER2_PLAYERMODEL") && FrameCnt == 40 && MainPlayer.playerType == PLAYER_TYPE_CAR)
	{
		SetPlayerModel(atoi(getenv("REDRIVER2_PLAYERMODEL")), 17);
		ActivatePlayerPedestrian(&car_data[MainPlayer.playerCarId], NULL, 0, NULL, (PED_MODEL_TYPES)PlayerExitModel());
	}

	// REDRIVER2_DENT=<zone mask>: damage those zones of the player car at step 40 and dent it (test of the damage zones)
	if (getenv("REDRIVER2_DENT") && FrameCnt == 40 && MainPlayer.playerType == PLAYER_TYPE_CAR)
	{
		int z;

		for (z = 0; z < 6; z++)
			car_data[MainPlayer.playerCarId].ap.damage[z] = (atoi(getenv("REDRIVER2_DENT")) >> z & 1) ? 3500 : 0;

		DentCar(&car_data[MainPlayer.playerCarId]);
	}

	// REDRIVER2_IMPACT=x,y,z,value: dent the player car round a point in model space at step 40 (test of the impact dents)
	if (getenv("REDRIVER2_IMPACT") && FrameCnt == 40 && MainPlayer.playerType == PLAYER_TYPE_CAR)
	{
		int a[6] = { 0, 0, 0, 0, -1, 1 };

		if (getenv("REDRIVER2_DAMAGE"))	// percent of the damage bar
			car_data[MainPlayer.playerCarId].totalDamage = MaxPlayerDamage[0] * atoi(getenv("REDRIVER2_DAMAGE")) / 100;
		int n;
		CAR_DATA* tcp = &car_data[MainPlayer.playerCarId];

		sscanf(getenv("REDRIVER2_IMPACT"), "%d,%d,%d,%d,%d,%d", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);

		for (n = 0; n < a[5]; n++)
		{
			if (a[4] >= 0)
			{
				ApplyDamage(tcp, a[4], a[3], 0);
				MirrorCentreDamage(tcp, a[4], a[0], a[3]);
			}

			ImpactDent(tcp, a[0], a[1], a[2], a[3]);
		}
	}

	if (gPedTest && FrameCnt == 30)
	{
		int k;

		for (k = 0; k < 3; k++)
		{
			LONGVECTOR4 pos;

			pos[0] = MainPlayer.pos[0] + (k - 1) * 450;
			pos[2] = MainPlayer.pos[2] + (gCamAngleOverride == 4096 ? -1100 : gCamAngleOverride ? -500 : 1100);	// -camangle: peds behind the player (4096: walk towards the camera, else seen from the side)
			pos[1] = -MapHeight((VECTOR*)pos);

			if (CreatePedAtLocation(&pos, 8))
				pUsedPeds->pallet = (k + 1) * 17;
		}
	}
#endif

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	double buildStart = NowMs();
#endif

	if (NumPlayers == 1 || NoPlayerControl)
	{
		ObjectDrawnValue = FrameCnt;
		DrawPauseMenus();

		RenderGame2(0);
		SwapDrawBuffers();
	}
	else
	{
		ObjectDrawnValue = FrameCnt;
		RenderGame2(0);
		SwapDrawBuffers2(0);

		ObjectDrawnValue += 16;

		DrawPauseMenus();
		
		RenderGame2(1);
		SwapDrawBuffers2(1);
	}

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	double submitStart = NowMs();
#endif

#ifndef PSX
	if (!FadingScreen)
	{
		PsyX_EndScene();
	}
#endif

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	if (FrameCnt > 40)	// the first frames stream regions and textures
	{
		gBuildMs += submitStart - buildStart;
		gSubmitMs += NowMs() - submitStart;
		gTimedFrames++;
	}
#endif

#if defined(DEBUG_OPTIONS) && !defined(PSX) && defined(_WIN32)
	PerfLogFrame();
#endif

#if defined(DEBUG_OPTIONS) && !defined(PSX)
	{
		extern void DumpCameraState(void);

		static int dumpTick;

		if (paused && dumpTick++ % 10 == 0)
			DumpCameraState();
	}

	if (gPlayLog && (FrameCnt % 30 == 0 || gPlayLogMark) && player[0].playerType != PLAYER_TYPE_NONE)
	{
		extern int g_vertexPeak;
		int mark = gPlayLogMark;
		gPlayLogMark = 0;

		VECTOR at = { player[0].pos[0], 0, player[0].pos[2] };
		sdPlane* plane = sdGetCell(&at);
		CAR_DATA* pc = player[0].playerType == PLAYER_TYPE_CAR ? &car_data[player[0].playerCarId] : NULL;

		printInfo("playlog: %sframe %d city %d pos %d %d %d ground %d surface %d plane %d %d %d %d camera %d %d %d speed %d wheels %d %d %d %d cars %d drawn %d tiles %d buildings %d peds %d area %d inarea %d prim %d est %d vpeak %d\n",
			mark ? "MARK " : "", FrameCnt, GameLevel, player[0].pos[0], player[0].pos[1], player[0].pos[2], MapHeight(&at), plane->surface, plane->a, plane->b, plane->c, plane->d,
			camera_position.vx, camera_position.vy, camera_position.vz, pc ? pc->hd.speed : 0,
			pc ? pc->hd.wheel[0].surface : -1, pc ? pc->hd.wheel[1].surface : -1, pc ? pc->hd.wheel[2].surface : -1, pc ? pc->hd.wheel[3].surface : -1,
			numCivCars, num_cars_drawn, gDrawStats[0], gDrawStats[1], num_pedestrians, LoadedArea, CellInLoadedArea(current_cell_x, current_cell_z),
				gMapPrimBytes, gMapEstPolys, g_vertexPeak);
	}
#endif

#if defined(DEBUG_OPTIONS) && !defined(PSX) && !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
	// -foreigncar: the camera follows a traffic car of the imported model for the screenshot
	extern int gForeignForceCity;
	if (gShotFrame && FrameCnt == gShotFrame - 20 && gForeignForceCity != -1 && player[0].playerType == 1)
	{
		for (int c = 0; c < MAX_CARS; c++)
		{
			if (car_data[c].controlType == CONTROL_TYPE_CIV_AI && car_data[c].ap.model == SPECIAL_CAR_SLOT)
			{
				player[0].cameraCarId = c;
				break;
			}
		}
	}

	if (gShotFrame && FrameCnt == gShotFrame)
	{
		printInfo("shot: draw distance mode %d, objects tiles %d buildings %d sprites %d animated %d, step %.2f ms, build %.2f ms, submit %.2f ms (average of %d frames)\n",
			gDrawDistanceMode, gDrawStats[0], gDrawStats[1], gDrawStats[2], gDrawStats[3],
			gTimedFrames ? gStepMs / gTimedFrames : 0.0, gTimedFrames ? gBuildMs / gTimedFrames : 0.0, gTimedFrames ? gSubmitMs / gTimedFrames : 0.0, gTimedFrames);
		extern int g_vertexPeak;
		printInfo("shot: primitive buffer used by the map %d of %d, polygons accepted %d, beyond ring 31: %d (budget %d), vertex peak %d of 65536\n", gMapPrimBytes, PRIMTAB_SIZE, gMapEstPolys, gMapFarPolys, gFarBudget, g_vertexPeak);

		VECTOR at = { player[0].pos[0], 0, player[0].pos[2] };

		PsyX_TakeScreenshot();
		GR_SaveVRAM("VRAM.TGA", 0, 0, 1024, 512, 1);
		sdPlane* plane = sdGetCell(&at);

		printInfo("shot: player %d %d %d, ground height %d, camera %d %d %d\n", player[0].pos[0], player[0].pos[1], player[0].pos[2],
			MapHeight(&at), camera_position.vx, camera_position.vy, camera_position.vz);
		printInfo("shot: plane surface %d a %d b %d c %d d %d, loaded regions %d %d %d %d, regions across %d down %d\n", plane->surface, plane->a, plane->b,
			plane->c, plane->d, RoadMapRegions[0], RoadMapRegions[1], RoadMapRegions[2], RoadMapRegions[3], regions_across, regions_down);
		printInfo("shot: car %d above the ground\n", player[0].pos[1] - MapHeight(&at));
		{
			// traffic check: civilian cars per control state (0 drive, 1 light, 4 yield, 5 parked, 7 empty, 8 stop at node, other = confused) and moving ones
			int states[16] = { 0 }, moving = 0, k;

			for (k = 0; k < MAX_CARS; k++)
			{
				CAR_DATA* tc = &car_data[k];

				if (tc->controlType != CONTROL_TYPE_CIV_AI)
					continue;

				states[tc->ai.c.ctrlState & 15]++;

				if (ABS(tc->hd.wheel_speed) > 20000)
					moving++;
			}

			printInfo("shot: traffic states drive %d light %d yield %d parked %d empty %d stop %d other %d, moving %d\n", states[0], states[1], states[4] + states[6], states[5],
				states[7], states[8], numCivCars - states[0] - states[1] - states[4] - states[6] - states[5] - states[7] - states[8], moving);
		}

		extern int gCarPopIns;
		printInfo("shot: car pop-ins in view %d\n", gCarPopIns);

		extern int gPingFail[16];
		printInfo("shot: ping in results: spawned %d, no road %d, bad road %d, no lane %d, not drivable %d, straight short %d, curve short %d, parked max %d, bad dir %d, crowded %d, too close %d, no seg %d, in view %d, not approaching %d\n",
			gPingFail[0], gPingFail[1], gPingFail[2], gPingFail[3], gPingFail[4], gPingFail[5], gPingFail[6], gPingFail[7], gPingFail[8], gPingFail[9], gPingFail[10], gPingFail[11], gPingFail[12], gPingFail[13]);

		extern int gPedPingFail[4];
		extern int gCivReverseCount, gCivNearContact;
		printInfo("shot: car following: reverse manoeuvres %d, near contacts %d\n", gCivReverseCount, gCivNearContact);

		printInfo("shot: ped ping in results: spawned %d, no pavement %d, too close %d\n", gPedPingFail[0], gPedPingFail[1], gPedPingFail[2]);

		extern int num_pedestrians;
		{
			int standing = 0;	// parked cars: ready to drive off (counted by the game) and empty ones
			for (CAR_DATA* sc = car_data; sc < &car_data[MAX_CARS]; sc++)
				standing += (sc->controlType == CONTROL_TYPE_CIV_AI && sc->ai.c.thrustState == CIV_AI_THRUST_STOP && (sc->ai.c.ctrlState == CIV_AI_CTRL_PARKED || sc->ai.c.ctrlState == CIV_AI_CTRL_EMPTY));
			printInfo("shot: crowd %d, civ cars %d of %d, parked %d of %d (standing %d), pedestrians %d\n", gCrowdMul, numCivCars, MaxCivCars(), numParkedCars, MaxParkedCars(), standing, num_pedestrians);
			printInfo("shot: cars within the density circle %d, target %d\n", CivCarsNearPlayer(), CivDensityTarget());
		}
		{
			extern int ForeignCarCity(void);
			int special = 0;

			for (int c = 0; c < MAX_CARS; c++)
			{
				if (car_data[c].controlType == CONTROL_TYPE_CIV_AI && car_data[c].ap.model == SPECIAL_CAR_SLOT)
					special++;
			}

			printInfo("shot: all cities cars: foreign car from city %d, %d of them in traffic\n", ForeignCarCity(), special);
		}
		printInfo("shot: heading dir %d car direction %d\n", MainPlayer.dir, car_data[player[0].playerCarId].hd.direction);
		printInfo("shot: resident models %d %d %d %d %d %d, player model %d palette %d, car tpages %d %d %d %d %d %d %d %d\n",
			residentCarModels[0], residentCarModels[1], residentCarModels[2], residentCarModels[3], residentCarModels[4], residentCarModels[SPECIAL_CAR_SLOT],
			car_data[player[0].playerCarId].ap.model, car_data[player[0].playerCarId].ap.palette,
			carTpages[GameLevel][0], carTpages[GameLevel][1], carTpages[GameLevel][2], carTpages[GameLevel][3],
			carTpages[GameLevel][4], carTpages[GameLevel][5], carTpages[GameLevel][6], carTpages[GameLevel][7]);
	}
#endif

	FrameCnt++;
}


// [D] [T]
void EndGame(GAMEMODE mode)
{
	WantedGameMode = mode;
	game_over = 1;
}


// [D] [T]
void EnablePause(PAUSEMODE mode)
{
	if (quick_replay == 0 && NoPlayerControl && mode == PAUSEMODE_GAMEOVER)
		return;

	if (!pauseflag)
	{
		WantPause = 1;
		PauseMode = mode;
	}
}

// [D] [T] This is really a Psy-Q function
void SsSetSerialVol(short s_num, short voll, short volr)
{
	SpuCommonAttr attr;

	if ((s_num & 0xffU) == 0)
	{
		attr.mask = 0xc0;
		if (0x7f < voll)
			voll = 0x7f;

		if (0x7f < volr)
			volr = 0x7f;

		attr.cd.volume.left = voll * 0x102;
		attr.cd.volume.right = volr * 0x102;
	}

	if (s_num == 1)
	{
		attr.mask = 0xc00;
		if (0x7f < voll)
			voll = 0x7f;

		if (0x7f < volr)
			volr = 0x7f;

		attr.ext.volume.left = voll * 0x102;
		attr.ext.volume.right = volr * 0x102;
	}

	SpuSetCommonAttr(&attr);
}

//-------------------------------------------

#if !defined(PSX) && !defined(__EMSCRIPTEN__)
#include <SDL_messagebox.h>
void PrintCommandLineArguments()
{
	const char* argumentsMessage =
		"Example: REDRIVER2 <command> [arguments]\n\n"
		"  -ini <filename.ini> : starts game with specific configuration ini\n"
		"  -cdimage <filename.iso> : starts game with specific ISO/BIN image file\n"
#ifdef DEBUG_OPTIONS
		"  -exportxasubtitles: Exports strings from XA WAV files to SBN\n"
		"  -startpos <x> <z>: Set player start position\n"
		"  -startrot <number> : Set player start rotation (with -startpos)\n"
		"  -shot <frame> : save SCREENSHOT.BMP at that game frame\n"
		"  -players <count> : Set player count (1 or 2)\n"
		"  -playercar <number>, -player2car <number> : set player wanted car\n"
		"  -chase <number> : using specified chase number for mission\n"
		"  -mission <number> : starts specified mission\n"
		"  -d1mission <0..43> : starts a converted Driver 1 story mission\n"
		"  -festeps <keys> : scripted frontend input (E select, D down, U up, T back, S screenshot)\n"
#endif // DEBUG_OPTIONS
		"  -replay <filename.d2rp> : starts replay from file\n"
#ifdef CUTSCENE_RECORDER
		"  -recordcutscene <filename> : starts cutscene recording session. Specify INI filename with it\n"
		"  -chaseautotest <filename> : starts chase autotesting. Specify INI filename with it\n"
#endif
		"  -nointro : disable intro screens\n"
		"  -nofmv : disable all FMVs\n";

	SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "REDRIVER 2 command line arguments", argumentsMessage, NULL);
}
#endif

// [D] [T]
#ifdef PSX

extern "C" u_long memTab_org;
extern "C" u_long mallocTab_org;

// TODO: mapping in Linker script
//volatile u_char _memoryTab_org[0x50400]  __attribute__((aligned(0x10)));						// 0xE7000
//volatile u_char _mallocTab_org[0xD47BC /*0xC37BC*/] __attribute__((aligned(0x10)));				// 0x137400

volatile u_char* _memoryTab_org = (u_char*)&memTab_org;
volatile u_char* _mallocTab_org = (u_char*)&mallocTab_org;

volatile char* _frontend_buffer = NULL;
volatile char* _other_buffer = NULL;
volatile char* _other_buffer2 = NULL;
volatile OTTYPE* _OT1 = NULL;
volatile OTTYPE* _OT2 = NULL;
volatile char* _primTab1 = NULL;
volatile char* _primTab2 = NULL;
volatile char* _overlay_buffer = NULL;
volatile char* _replay_buffer = NULL;
volatile char* _sbank_buffer = NULL;
volatile char* malloctab = NULL;
volatile char* mallocptr = NULL;

int main(void)
#else
int redriver2_main(int argc, char** argv)
#endif // PSX
{
	char** ScreenNames;
	
	char* PALScreenNames[4] = {		// [A] don't show publisher logo
	//	"GFX\\SPLASH2.TIM",
	//	"GFX\\SPLASH3.TIM",
		"GFX\\SPLASH1P.TIM",
		NULL
	};

	char* NTSCScreenNames[4] = {		// [A] don't show publisher logo
	//	"GFX\\SPLASH2.TIM",
		//"GFX\\SPLASH3.TIM",
		"GFX\\SPLASH1N.TIM",
		NULL
	};

	char* OPMScreenNames[4] = {		// [A] don't show publisher logo
		//"GFX\\OPM1.TIM",
		"GFX\\OPM2.TIM",
		"GFX\\OPM3.TIM",
		NULL
	};

	//_stacksize = 0x4000;
	//_ramsize = 0x200000;

#ifdef PSX
	printf("------REDRIVER2 STARTUP------\n");

	volatile u_char* _path_org = &_memoryTab_org[0];				// 0xE7000
	volatile u_char* _otag1_org = &_memoryTab_org[0xC000];			// 0xF3000
	volatile u_char* _otag2_org = &_memoryTab_org[0x10200];			// 0xF7200
	volatile u_char* _primTab1_org = &_memoryTab_org[0x14400];		// 0xFB400
	volatile u_char* _primTab2_org = &_memoryTab_org[0x32400];		// 0x119400
	volatile u_char* _sbnk_org = &_mallocTab_org[0x48C00];			// 0x180000
	volatile u_char* _frnt_org = &_mallocTab_org[0x88C00];			// 0x1C0000
	volatile u_char* _repl_org = &_mallocTab_org[0xC47BC];			// 0x1FABBC

	_frontend_buffer = (char*)_otag1_org;
	_other_buffer = (char*)_otag1_org;
	_other_buffer2 = (char*)_path_org;
	_OT1 = (OTTYPE*)_otag1_org;
	_OT2 = (OTTYPE*)_otag2_org;
	_primTab1 = (char*)_primTab1_org;
	_primTab2 = (char*)_primTab2_org;
	_overlay_buffer = (char*)_frnt_org;
	_replay_buffer = (char*)_repl_org;
	_sbank_buffer = (char*)_sbnk_org;
	malloctab = (char*)_mallocTab_org;

	printf("path_org = %x\n", _path_org); // 0xE7000
	printf("otag1_org = %x\n", _otag1_org); // 0xF3000
	printf("otag2_org = %x\n", _otag2_org); // 0xF7200
	printf("primTab1_org = %x\n", _primTab1_org); // 0xFB400
	printf("primTab2_org = %x\n", _primTab2_org); // 0x119400
	printf("sbnk_org = %x\n", _sbnk_org); // 0x180000
	printf("frnt_org = %x\n", _frnt_org); // 0x1C0000
	printf("repl_org = %x\n", _repl_org); // 0x1FABBC

	printf("malloctab = %x\n", malloctab);
#endif

	SetDispMask(0);
	StopCallback();
	ResetCallback();
	ResetGraph(0);
	SsSetSerialVol(0, 0, 0);
	SetDispMask(0);
	SetGraphDebug(0);

	SetVideoMode(video_mode);

	CdInit();
	SsSetSerialVol(0, 0, 0);

	SpuInit();
	InitGeom();
	SetGeomOffset(160, 128);
	scr_z = 256;
	SetGeomScreen(256);
	MemCardInit(1);
	InitControllers();
	Init_FileSystem();
	InitSound();
	
	// [A] REDRIVER 2 version auto-detection
	// this is the only difference between the files on CD
#ifdef DEMO_VERSION
	ScreenNames = OPMScreenNames;
#elif NTSC_VERSION
	ScreenNames = NTSCScreenNames;

	if (!FileExists(ScreenNames[0]))
		ScreenNames[0] = PALScreenNames[0];
#elif PAL_VERSION
	ScreenNames = PALScreenNames;

	if (!FileExists(ScreenNames[0]))
		ScreenNames[0] = NTSCScreenNames[0];
#endif

#if !defined(PSX) && !defined(__EMSCRIPTEN__)
	// verify installation
	if (!FileExists("DATA\\FEFONT.BNK") || !FileExists("GFX\\FONT2.FNT"))
	{
		char str[320];
		sprintf(str, "Cannot initialize REDRIVER2\n\nGame files not found by folder '%s'\n", gDataFolder);
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR!", str, NULL);
		return -1;
	}

	// init language
	if (!InitStringMng())
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ERROR!", "Unable to load language files!\n\nSee console for details", NULL);
		return -1;
	}

	// TODO: divide game by the states, place main loop here.
	
	if (argc <= 1)
#elif !defined(PSX)
	
	InitStringMng();
	
#endif
	{
		//PlayFMV(99);	// [A] don't show publisher logo

		ShowHiresScreens(ScreenNames, 300, 0); // [A]
		PlayFMV(0);		// play intro movie
	}

	CheckForCorrectDisc(0);
	
	// Init frontend
	LOAD_OVERLAY("FRONTEND.BIN", _overlay_buffer);

	SpuSetMute(0);

	// initializes sound system
	LoadSoundBankDynamic(NULL, 0, 0);

	InitialiseScoreTables();

	// by default go to frontend
	SetState(STATE_INITFRONTEND, (void*)2);

	LoadCurrentProfile(1);
	
#ifndef PSX	
	int commandLinePropsShown;
	commandLinePropsShown = 0;

	for (int i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-ini") || 
			!strcmp(argv[i], "-cdimage"))
		{
			i++;
		}
		else if (!strcmp(argv[i], "-nofmv"))
		{
			gNoFMV = 1;
		}
		else if (!strcmp(argv[i], "-nointro"))
		{
			// do nothing. All command line features use it
		}
#ifdef DEBUG_OPTIONS
		else if (!strcmp(argv[i], "-exportxasubtitles"))
		{
			extern void StoreXASubtitles();
			StoreXASubtitles();
		}
		else if (!strcmp(argv[i], "-startpos"))
		{
			if (argc - i < 3)
			{
				printError("-startpos missing two number argument!");
				return -1;
			}

			gStartPos.x = atoi(argv[i + 1]);
			gStartPos.z = atoi(argv[i + 2]);

			i += 2;
		}
		else if (!strcmp(argv[i], "-startrot"))
		{
			if (argc - i < 2)
			{
				printError("-startrot missing number argument!");
				return -1;
			}

			gStartRot = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-shot"))
		{
			if (argc - i < 2)
			{
				printError("-shot missing frame number argument!");
				return -1;
			}

			gShotFrame = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-crowd") && argc - i >= 2)
		{
			gCrowdMul = MAX(1, MIN(atoi(argv[i + 1]), CROWD_MUL_MAX));
			i++;
		}
		else if (!strcmp(argv[i], "-d1dlc") && argc - i >= 2)
		{
			gD1DlcEnabled = atoi(argv[i + 1]) != 0 && gD1DlcCityMask != 0;	// Driver 1 DLC menus on/off without the secret menu
			i++;
		}
		else if (!strcmp(argv[i], "-drawdist") && argc - i >= 2)
		{
			gDrawDistanceMode = MAX(0, MIN(atoi(argv[i + 1]), 2));
			i++;
		}
		else if (!strcmp(argv[i], "-farbudget") && argc - i >= 2)
		{
			gFarBudget = MAX(0, atoi(argv[i + 1]));
			i++;
		}
		else if (!strcmp(argv[i], "-fog"))
		{
			gFogTarget = 1;
		}
		else if (!strcmp(argv[i], "-snow"))
		{
			wantedWeather = WANTED_WEATHER_SNOW;	// the mission load turns it into the snow layer
		}
		else if (!strcmp(argv[i], "-playerlights") && argc - i >= 2)	// 1 on, 2 off: as the L key leaves them
		{
			gPlayerLights = MAX(0, MIN(atoi(argv[i + 1]), 2));
			i++;
		}
		else if (!strcmp(argv[i], "-skyhd") && argc - i >= 2)	// 0 original, 1 smooth, 2 HD sky
		{
			extern int gSkyHd;
			gSkyHd = MAX(0, MIN(atoi(argv[i + 1]), 2));
			i++;
		}
		else if (!strcmp(argv[i], "-allcars"))
		{
			extern int gAllCityCars;
			gAllCityCars = 1;
		}
		else if (!strcmp(argv[i], "-foreigncar") && argc - i >= 3)	// <city 0..3> <special car 1..4> of that city
		{
			extern int gAllCityCars, gForeignForceCity, gForeignForceSpecial;
			gAllCityCars = 1;
			gForeignForceCity = atoi(argv[i + 1]);
			gForeignForceSpecial = atoi(argv[i + 2]);
			i += 2;
		}
		else if (!strcmp(argv[i], "-carfade") && argc - i >= 2)
		{
			extern int gForceCarFade;
			gForceCarFade = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-choppertest"))
		{
			extern int gChopperTest;
			gChopperTest = 1;	// take off in a Rio run (-shot)
		}
		else if (!strcmp(argv[i], "-chopperexit"))
		{
			extern int gChopperTest;
			gChopperTest = 2;	// like -choppertest, then the pilot gets out and walks away (chopper.c)
		}
		else if (!strcmp(argv[i], "-flytest") && argc - i >= 2)
		{
			extern int gFlyingCar, gFlyTestUp;
			gFlyingCar = 1;
			gFlyTestUp = MAX(-128, MIN(atoi(argv[i + 1]), 128));	// forced climb of the flying car
			i++;
		}
		else if (!strcmp(argv[i], "-flyalt") && argc - i >= 2)
		{
			extern int gFlyingCar, gFlyTestAlt;
			gFlyingCar = 1;
			gFlyTestAlt = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-flyfwd"))
		{
			extern int gFlyTestFwd;
			gFlyTestFwd = 1;
		}
		else if (!strcmp(argv[i], "-weather") && argc - i >= 2)
		{
			wantedWeather = MAX(0, MIN(atoi(argv[i + 1]), WANTED_WEATHER_SNOW));	// 0 clear, 1 rainy, 2 wet, 3 snow
			i++;
		}
		else if (!strcmp(argv[i], "-tod") && argc - i >= 2)
		{
			wantedTimeOfDay = MAX(0, MIN(atoi(argv[i + 1]), 3));	// 0 dawn, 1 day, 2 dusk, 3 night
			i++;
		}
		else if (!strcmp(argv[i], "-cycle") && argc - i >= 2)
		{
			gDayCycle = 1;
			gDayCycleMinutes = MAX(1, atoi(argv[i + 1]));	// real minutes of one day (48 is the default, 1 for a quick test)
			i++;
		}
		else if (!strcmp(argv[i], "-nightswap") && argc - i >= 2)
		{
			gNightSwapSteps = MAX(0, atoi(argv[i + 1]));
			i++;
		}
		else if (!strcmp(argv[i], "-hour") && argc - i >= 2)
		{
			gDayCycleStartHour = (float)MAX(0.0, MIN(atof(argv[i + 1]), 23.99));	// start clock of the day night cycle
			i++;
		}
		else if (!strcmp(argv[i], "-oldspawn"))
		{
			extern int gOldSpawn;
			gOldSpawn = 1;
		}
		else if (!strcmp(argv[i], "-pedtest"))
			gPedTest = 1;
		else if (!strcmp(argv[i], "-onfoot"))
			gForceOnFoot = 1;
		else if (!strcmp(argv[i], "-lightson"))
			gForceLightsOn = 1;
		else if (!strcmp(argv[i], "-lampdamage") && argc - i >= 2)
		{
			gLampDamage = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-camangle") && argc - i >= 2)
		{
			gCamAngleOverride = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-camdown") && argc - i >= 2)
		{
			gCamDown = MAX(0, MIN(atoi(argv[i + 1]), 1024));
			i++;
		}
		else if (!strcmp(argv[i], "-camzoom") && argc - i >= 2)
		{
			gCameraZoom = MAX(0, MIN(atoi(argv[i + 1]), 4));	// index of 1.0 / 1.2 / 1.5 / 1.8 / 2.0
			i++;
		}
		else if (!strcmp(argv[i], "-camtest"))
		{
			gCamTest = 1;
		}
		else if (!strcmp(argv[i], "-editor"))
		{
			extern int gEditor;
			gEditor = 1;		// in-game map editor (utils/MapEditor.cpp)
		}
		else if (!strcmp(argv[i], "-editortest"))
		{
			extern int gEditor, gEditorTest;
			gEditor = gEditorTest = 1;
		}
		else if (!strcmp(argv[i], "-d1lower") && argc - i >= 2)
		{
			extern int gD1CarLower;
			gD1CarLower = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-d1wheel") && argc - i >= 2)
		{
			extern int gD1WheelPercent;
			gD1WheelPercent = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-carsdir") && argc - i >= 2)
		{
			strncpy(gCarsDir, argv[i + 1], sizeof(gCarsDir) - 1);
			i++;
		}
		else if (!strcmp(argv[i], "-carexport") && argc - i >= 2)
		{
			CarExportCommandLine(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-carspawner") && argc - i >= 2)
		{
			int mode = argc - i >= 3 ? (!strcmp(argv[i + 2], "autoright") ? 1 : !strcmp(argv[i + 2], "autostart") ? 2 : 0) : 0;

			CarSpawnerCommandLine(atoi(argv[i + 1]), mode);
			i += 1 + (mode != 0);
		}
		else if (!strcmp(argv[i], "-playercar"))
		{
			if (argc - i < 2)
			{
				printError("-playercar missing number argument!");
				return -1;
			}
			wantedCar[0] = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-player2car"))
		{
			if (argc - i < 2)
			{
				printError("-player2car missing number argument!");
				return -1;
			}
			wantedCar[1] = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-players"))
		{
			if (argc - i < 2)
			{
				printError("-players missing number argument!");
				return -1;
			}
			NumPlayers = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-chase"))
		{
			if (argc - i < 2)
			{
				printError("-chase missing number argument!");
				return -1;
			}

			gChaseNumber = atoi(argv[i + 1]);
			i++;
		}
		else if (!strcmp(argv[i], "-mission"))
		{
			if (argc - i < 2)
			{
				printError("-mission missing number argument!");
				return -1;
			}

			SetFEDrawMode();

			gInFrontend = 0;
			AttractMode = 0;

			gCurrentMissionNumber = atoi(argv[i + 1]);
			i++;

			GameType = GAME_TAKEADRIVE;
			SetState(STATE_GAMELAUNCH);
		}
		else if (!strcmp(argv[i], "-festeps") && argc - i >= 2)
		{
			extern char gFeSteps[64];
			strncpy(gFeSteps, argv[i + 1], sizeof(gFeSteps) - 1);
			i++;
		}
		else if (!strcmp(argv[i], "-d1mission") && argc - i >= 2)
		{
			// Driver 1 story mission 0..43 (or the full number 1000 + n), converted from DRIVER1\MISSIONS\MISSION.BLK on first use
			SetFEDrawMode();

			gInFrontend = 0;
			AttractMode = 0;

			gCurrentMissionNumber = atoi(argv[i + 1]);
			if (!IS_DRIVER1_MISSION(gCurrentMissionNumber))
				gCurrentMissionNumber += D1_MISSION_BASE;

			i++;

			GameType = GAME_DRIVER1MISSION;
			D1Missions_Available();
			SetState(STATE_GAMELAUNCH);
		}
		else if (!strcmp(argv[i], "-city"))
		{
			if (argc - i < 2)
			{
				printError("-city missing number argument!");
				return -1;
			}

			// 0..3 Driver 2, 4..9 Driver 1 cities (Take a Ride missions only)
			GameLevel = atoi(argv[i + 1]);
			i++;
		}
#endif // _DEBUG_OPTIONS
		else if (!strcmp(argv[i], "-replay"))
		{
			if (argc - i < 2)
			{
				printError("-replay missing argument!");
				return -1;
			}

			SetFEDrawMode();

			gInFrontend = 0;
			AttractMode = 0;

			char nameStr[512];
			sprintf(nameStr, "%s", argv[i + 1]);

			FILE* fp = fopen(nameStr, "rb");
			if (fp)
			{
				int replay_size = 0;
				fseek(fp, 0, SEEK_END);
				replay_size = ftell(fp);
				fseek(fp, 0, SEEK_SET);

				fread((char*)_other_buffer, replay_size, 1, fp);
				fclose(fp);

				if (LoadReplayFromBuffer((char*)_other_buffer))
				{
					CurrentGameMode = GAMEMODE_REPLAY;
					gLoadedReplay = 1;
					
					SetState(STATE_GAMELAUNCH);
				}
				else
				{
					printError("Error loading replay file '%s'!\n", nameStr);
				}
			}
			else
			{
				printError("Cannot open replay '%s'!\n", nameStr);
				return -1;
			}
			i++;
		}
#ifdef CUTSCENE_RECORDER
		else if (!strcmp(argv[i], "-chaseautotest"))
		{
			SetFEDrawMode();

			gInFrontend = 0;
			AttractMode = 0;

			InitChaseAutoTest(argv[i+1]);
			i++;
		}
		else if (!strcmp(argv[i], "-recordcutscene"))
		{
			SetFEDrawMode();

			gInFrontend = 0;
			AttractMode = 0;

			InitCutsceneRecorder(argv[i+1]);
			i++;
		}
#endif
		else if (!strcmp(argv[i], "-d1detect") || !strcmp(argv[i], "-d1check") || !strcmp(argv[i], "-d1prepare"))
		{
			// read by redriver2_psxpc.cpp at startup, nothing to do here (no usage box)
		}
		else
		{
#if !defined(PSX) && !defined(__EMSCRIPTEN__)
			if (!commandLinePropsShown)
				PrintCommandLineArguments();
#endif
			commandLinePropsShown = 1;
		}
	}
#endif // PSX

	DoStateLoop();

#ifndef PSX
	SaveCurrentProfile(1);
#endif

	return 1;
}

// [D] [T]
void FadeScreen(int end_value)
{
	int tmp2;

	tmp2 = pauseflag;

	pauseflag = 1;
	SetupScreenFade(-32, end_value, gFastLoadingScreens ? 128 : 8);
	FadingScreen = 1;

	do {
		RenderGame();
	} while (FadingScreen);

	DrawSync(0);
	SetDispMask(0);

	pauseflag = tmp2;
}


// [D] [T]
void UpdatePlayerInformation(void)
{
	short* playerFelony;
	WHEEL* wheel;
	int i, j;
	int wheelsInWater;
	int wheelsAboveWaterToDieWithFade;
	CAR_DATA* cp;

	cp = NULL;

	PlayerDamageBar.max = MaxPlayerDamage[0];
	Player2DamageBar.max = MaxPlayerDamage[1];

	playerFelony = GetPlayerFelony(&MainPlayer);

	if (gPlayerImmune != 0)
		*playerFelony = 0;

	FelonyBar.position = *playerFelony;

	for (i = 0; i < NumPlayers; i++)
	{
		if (player[i].playerType == PLAYER_TYPE_CAR)
		{
			cp = &car_data[player[i].playerCarId];

			if (gInvincibleCar != 0)
			{
				cp->totalDamage = 0;
				ClearMem((char*)cp->ap.damage, sizeof(cp->ap.damage));
			}

			wheelsInWater = 0;
			wheelsAboveWaterToDieWithFade = 0;

			wheel = cp->hd.wheel;

			// [A] count the wheels above the water and in the water
			for (j = 0; j < 4; j++)
			{
				if ((wheel->surface & 7) == 1)
				{
					if (wheel->susCompression == 0)
						wheelsAboveWaterToDieWithFade++;
					else
						wheelsInWater++;
				}

				wheel++;
			}

			// [A] if all wheels above the water surface and we are falling down
			// fade out and end the game
			if(wheelsAboveWaterToDieWithFade > 0 && cp->hd.where.t[1] < -1000 && gDieWithFade == 0)
			{
				// fix for Havana tunnels and Chicago freeway
				if (GameLevel <= 1)
				{
					if(wheelsAboveWaterToDieWithFade == 4)
						gDieWithFade = 1;
				}
				else // car drown as usual
				{
					gDieWithFade = 1;
				}
			}

			if (wheelsInWater == 4) // apply water damage
				cp->totalDamage += MaxPlayerDamage[i] / 80;

			if (cp->totalDamage > MaxPlayerDamage[i])
				cp->totalDamage = MaxPlayerDamage[i];

			if (i == 0)
				PlayerDamageBar.position = cp->totalDamage;
			else
				Player2DamageBar.position = cp->totalDamage;
		}
		else
		{
			if (i == 0)
				PlayerDamageBar.position = 0;
			else
				Player2DamageBar.position = 0;
		}

		// die with fade on mountain race track
		if ((gCurrentMissionNumber > 479 && gCurrentMissionNumber < 482 ||
			gCurrentMissionNumber > 483 && gCurrentMissionNumber < 486) &&
			cp->hd.where.t[1] < -750 && gDieWithFade == 0)
		{
			gDieWithFade = 1;
		}
	}
}

int CurrentPlayerView = 0;

// [D] [T]
void RenderGame2(int view)
{
	POLY_F4* poly;
	int fadeColour;
	int i;
	int notInDreaAndStevesEvilLair;

	CurrentPlayerView = view;
	InitCamera(&player[view]);

#ifndef PSX
	int screenW, screenH;
	PsyX_GetScreenSize(&screenW, &screenH);

	float aspectVar = float(screenH) / float(screenW);

	FrAng = ratan2(160, float(scr_z) * aspectVar * 1.35f);

	extern void DoFreeCamera();
	DoFreeCamera();

	// map editor, top-down: the view cone of the map walk and the object checks must cover every direction
	extern int EditorTopDownView(void);
	extern int gFrustumOff;
	gFrustumOff = EditorTopDownView();
#else
	FrAng = ratan2(160, scr_z);
#endif

	Set_Inv_CameraMatrix();
	SetCameraVector();

	SetupDrawMapPSX();
	setupYet = 0;

	if (gLoadedMotionCapture != 0)
		DrawAllPedestrians();

	// do a lot of stuff
	DisplayMissionTitle();
	DrawInGameCutscene();

	// draw events from level itself
	DrawEvents(1);

	Set_Inv_CameraMatrix();
	SetCameraVector();

	SetupDrawMapPSX();
	
	DrawDrivingGames();
	DrawThrownBombs();
	AddGroundDebris();

	// draw events that use cell object
	DrawEvents(0);
	ChopperDrawShadow();

	current->ot += 10;

	DrawSmashable_sprites();
	HandleDebris();

	current->ot -= 10;

	DrawAllExplosions();

	if (AttractMode != 0)
	{
		int colour;
		colour = CameraCnt & 0x1f;

		if (colour > 15)
			colour = 32 - colour;

		SetTextColour((colour & 0x1f) << 3, 0, 0);
		PrintString(G_LTXT(GTXT_DEMO), gOverlayXPos, 15);
	}

	for (i = 0; i < 2; i++)
	{
		if (player[i].playerCarId >= 0 && 
			player[i].horn.on != 0 &&
			CarHasSiren(car_data[player[i].playerCarId].ap.model) != 0)
		{
			AddCopCarLight(&car_data[player[i].playerCarId]);
		}
	}

	if (gLoadedOverlay)
		DisplayOverlays();

	DrawMission();

	if (!FastForward && NumPlayers == 1)
		DrawLensFlare();

	// Retro calls this BSOD...
	if (gDieWithFade)
	{
		fadeColour = (gDieWithFade << 4);

		if (gDieWithFade << 4 > 255)
			fadeColour = 255;

		poly = (POLY_F4*)current->primptr;

		setPolyF4(poly);
		setSemiTrans(poly, 1);

		poly->r0 = fadeColour;
		poly->g0 = fadeColour;
		poly->b0 = fadeColour;

#ifdef PSX
		setXYWH(poly, 0, 0, 320, 256);
#else
		setXYWH(poly, -500, 0, 1200, 256);
#endif

		addPrim(current->ot + 8, poly);

		current->primptr += sizeof(POLY_F4);
		POLY_FT3* null = (POLY_FT3*)current->primptr;

		setPolyFT3(null);
		null->x0 = -1;
		null->y0 = -1;
		null->x1 = -1;
		null->y1 = -1;
		null->x2 = -1;
		null->y2 = -1;
		null->tpage = 0x40;

		addPrim(current->ot + 8, null);
		current->primptr += sizeof(POLY_FT3);
	}

	// Steven Adams and Andreas Tawn of Havana team
	notInDreaAndStevesEvilLair = Havana3DOcclusion(DrawMapPSX, (int*)&ObjectDrawnValue);

	if (notInDreaAndStevesEvilLair)
	{		
		DrawSkyDome();

		if (current->primtab - (current->primptr - PRIMTAB_SIZE) > 40000)
		{
			DoWeather(gWeather);
		}
	}

	if (current->primtab - (current->primptr - PRIMTAB_SIZE) > 37000)
	{
		DrawTyreTracks();
	}

	DrawAllTheCars(view);

#ifndef PSX

#ifdef DYNAMIC_LIGHTING
	UpdateVolumeLights();
	gNumDlights = 0;
#endif

	extern void DrawDebugOverlays();

	DrawDebugOverlays();
#endif
}


// [D] [T]
void RenderGame(void)
{
	DrawGame(); // [A] was inline
	FadeGameScreen(0);
}

int Havana3DLevelDraw = -1;
int Havana3DLevelMode = -1;			// 0 = uses 1.0 LEV file, 1 = uses v1.1 LEV file

// [D] [T]
void InitGameVariables(void)
{
	InitDebris();
	InitTyreTracks();
	TargetCar = 0;

	if (NewLevel)
	{
		gLoadedOverlay = 0;
		gLoadedMotionCapture = 0;
	}

	gRainCount = 0;

	if (!NoPlayerControl || AttractMode || quick_replay)
		pauseflag = 0;
	else
		pauseflag = 1;

	FastForward = 0;
	game_over = 0;
	saved_counter = 0;
	saved_leadcar_pos = 0;
	gStopPadReads = 0;
	DawnCount = 0;
	variable_weather = 0;
	current_camera_angle = 2048;
	gDieWithFade = 0;

	pedestrianFelony = 0;	// [A] reset pedestrian felony and also cop visibility state
	CopsCanSeePlayer = 0;

	Havana3DLevelDraw = -1;

	srand(0x1234);
	RandomInit(0xd431, 0x350b1);
	FrameCnt = 0;
	CameraCnt = 0;

	ClearMem((char*)&lightsOnDelay, sizeof(lightsOnDelay));

	PlayerStartInfo[0] = &ReplayStreams[0].SourceType;

	if (!_CutRec_InitPlayers())
	{
		// Driver 1 cities start at the Chicago position until their converter supplies one
		int startCity = IS_DRIVER1_CITY(GameLevel) ? 0 : GameLevel;

		ClearMem((char*)PlayerStartInfo[0], sizeof(STREAM_SOURCE));

		PlayerStartInfo[0]->type = 1;
		PlayerStartInfo[0]->model = defaultPlayerModel[0];
		PlayerStartInfo[0]->palette = defaultPlayerPalette;
		PlayerStartInfo[0]->controlType = CONTROL_TYPE_PLAYER;
		PlayerStartInfo[0]->flags = 0;

		PlayerStartInfo[0]->rotation = levelstartpos[startCity + (gWantNight * 4)][1];

		PlayerStartInfo[0]->position.vy = 0;
		PlayerStartInfo[0]->position.vx = levelstartpos[startCity + (gWantNight * 4)][0];
		PlayerStartInfo[0]->position.vz = levelstartpos[startCity + (gWantNight * 4)][2];

		numPlayersToCreate = 1;

		if (NumPlayers == 2)
		{
			PlayerStartInfo[1] = &ReplayStreams[1].SourceType;
			ClearMem((char*)PlayerStartInfo[1], sizeof(STREAM_SOURCE));

			PlayerStartInfo[1]->type = 1;
			PlayerStartInfo[1]->model = defaultPlayerModel[1];
			PlayerStartInfo[1]->palette = defaultPlayerPalette;
			PlayerStartInfo[1]->controlType = CONTROL_TYPE_PLAYER;
			PlayerStartInfo[1]->flags = 0;

			PlayerStartInfo[1]->rotation = levelstartpos[startCity][1];

			PlayerStartInfo[1]->position.vy = 0;
			PlayerStartInfo[1]->position.vx = levelstartpos[startCity][0] + 600;
			PlayerStartInfo[1]->position.vz = levelstartpos[startCity][2];

			numPlayersToCreate = NumPlayers;
		}
	}

	InitCivCars();
}

// [D] [T]
void DealWithHorn(char* hr, int i)
{
	int channel;
	int sample;
	CAR_DATA* car;

	car = &car_data[player[i].playerCarId];

	if (*hr == 0)
		return;

	if (*hr == 1)
	{
		channel = i != 0 ? 5 : 2;
		StopChannel(channel);
	}
	else if (*hr == 2)
	{
		sample = GetCarBankSample(car->ap.model);
		channel = i != 0 ? 5 : 2;

		Start3DSoundVolPitch(channel, SOUND_BANK_CARS, sample * 3 + 2,
			car->hd.where.t[0],
			car->hd.where.t[1],
			car->hd.where.t[2], -10000,
			4096);

		if (NumPlayers > 1 && NoPlayerControl == 0)
		{
			channel = i != 0 ? 5 : 2;

			SetPlayerOwnsChannel(channel, i);
		}

		channel = i != 0 ? 5 : 2;

		SetChannelPosition3(channel, 
			(VECTOR*)car->hd.where.t, 
			(LONGVECTOR3*)car->st.n.linearVelocity, 
			-2000, i * 8 + 4096, 0);
	}

	*hr = (*hr + 1) % 3;
}

// [D] [T]
int Havana3DOcclusion(occlFunc func, int* param)
{
	int loop;
	int draw;
	int otAltered;
	int outside;
	VECTOR tempPos;

#ifndef PSX
	if (gDemoLevel)
	{
		(*func)(param);
		return 1;
	}
#endif

	if (GameLevel == 1 && 
		camera_position.vx <= -430044 && camera_position.vx >= -480278 && 
		camera_position.vz <= -112814 && camera_position.vz >= -134323)
	{
		// TODO: Hardcode into different builds for PSX version
		if(Havana3DLevelMode == -1)
		{
			// try autodetecting
			tempPos.vy = -3823;
			tempPos.vx = -461964;
			tempPos.vz = -124831;

			if (GetSurfaceIndex(&tempPos) + 32 == 27)
				Havana3DLevelMode = 1;
			else
				Havana3DLevelMode = 0;
		}

		if(Havana3DLevelMode == 0)
		{
			// v1.0 method
			outside = 1;
			draw = 10;

			if (camera_position.vy < 447)
			{
				if (camera_position.vx > -469500)
					draw = 17;
				else
					draw = 16;
			}
			else
			{
				outside = 0;

				if (camera_position.vx < -453093 && camera_position.vx > -457217 &&
					camera_position.vz < -123393 && camera_position.vz > -127493 &&
					camera_position.vy < 3955)
				{
					if (camera_position.vy < 1245)
						draw = 15;
					else if (camera_position.vy < 2001)
						draw = 14;
					else if (camera_position.vy < 3071)
						draw = 13;
				}
				else
				{
					draw = 15;
					
					if (camera_position.vy >= 1730)
					{
						draw = 14;

						if (camera_position.vy >= 2500)
						{
							draw = 13;

							if(camera_position.vz < -120000)
							{
								draw = 12;

								// check final room
								if (camera_position.vy >= 3300 && camera_position.vx >= -458108)
									draw = 10;
							}
						}
					}
				}
			}

			events.camera = 1;

			loop = draw - 1;

			if (loop < 10)
				loop = 10;
		}
		else if(Havana3DLevelMode == 1)
		{
			// v1.1 method - simpler one
			outside = 0;

			if (camera_position.vy < 447)
			{
				if (camera_position.vx > -468500)
					draw = 17;
				else
					draw = 16;
			}
			else
			{
				tempPos.vy = -camera_position.vy;
				tempPos.vx = camera_position.vx;
				tempPos.vz = camera_position.vz;

				loop = GetSurfaceIndex(&tempPos);

				if (loop + 32 == 17)
				{
					Havana3DLevelDraw = -1;
					draw = 9;
				}
				else
				{
					if (loop + 7 < 7)
					{
						draw = loop + 32 & 15;

						if (Havana3DLevelDraw == -1)
							goto still_inside;

						loop = Havana3DLevelDraw - draw;

						if (loop < 0)
							loop = draw - Havana3DLevelDraw;

						if (loop < 2)
							goto still_inside;
					}

					outside = 1;
					draw = Havana3DLevelDraw;
				}
			}

			// FIXME: please handle this goto for me
		still_inside:
			if (Havana3DLevelDraw != draw)
				Havana3DLevelDraw = draw;

			events.camera = 1;

			loop = draw - 1;

			if (loop < 9)
				loop = 9;
		}
	
		otAltered = 0;

		while (true)
		{
			if (draw + 1 < loop)
			{
				events.camera = 0;
				return outside;
			}
			
			if (loop == 16) 
				break;
			
			if (draw != loop)
			{
				otAltered = 200;
			}
			
			events.draw = loop;
			current->ot += otAltered;
			
			(*func)(param);

			if (otAltered != 0)
			{
				current->ot -= otAltered;
				otAltered = 0;
			}
			
			loop++;
		}

		events.camera = 0;
	
		if (draw == 15 && camera_position.vx > -458001)
		{
			events.camera = 0;
			return outside;
		}
	
		(*func)(param);
		return 1;
	}

	(*func)(param);

	return 1;
}





