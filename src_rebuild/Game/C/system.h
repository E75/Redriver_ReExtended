#ifndef SYSTEM_H
#define SYSTEM_H

struct DRAW_MODE
{
	short x1, y1;
	short x2, y2;
	short width, height;
	short framex, framey;
};

enum CDTYPE
{
	CDTYPE_NODISC = 0,
	CDTYPE_SHELLOPEN = 1,
	CDTYPE_DISCERROR = 2,
	CDTYPE_WRONGDISC = 3,
	CDTYPE_CORRECTDISC = 4,
};

enum CITYTYPE
{
	CITYTYPE_DAY = 0,
	CITYTYPE_NIGHT = 1,
	CITYTYPE_MULTI_DAY = 2,
	CITYTYPE_MULTI_NIGHT = 3,
};


extern volatile char* _overlay_buffer;		// 0x1C0000
extern volatile char* _frontend_buffer;		// 0xFB400
extern volatile char* _other_buffer;		// 0xF3000
extern volatile char* _other_buffer2;		// 0xE7000

extern volatile OTTYPE* _OT1;				// 0xF3000
extern volatile OTTYPE* _OT2;				// 0xF7200

extern volatile char* _primTab1;			// 0xFB400
extern volatile char* _primTab2;			// 0x119400
extern volatile char* _sbank_buffer;		// 0x180000
extern volatile char* _replay_buffer;		// 0x1FABBC

extern char gDataFolder[32];

#define PSX_MALLOC_SIZE 870332

extern volatile char* malloctab;
extern volatile char* mallocptr;

#if USE_CRT_MALLOC

extern char* sys_malloc(int size, char* funcname, int line);
extern char* sys_tempalloc(int size);
extern void sys_tempfree();

extern void sys_freeall();

#define D_TEMPFREE()		sys_tempfree()
#define D_TEMPALLOC(size)	sys_tempalloc(size)

#ifdef _DEBUG
#define D_MALLOC(size)		sys_malloc(size, FUNCNAME, __LINE__)

#else
#define D_MALLOC(size)		sys_malloc(size, NULL, 0)
#endif

#else
#define D_MALLOC(size)		(char*)mallocptr; mallocptr += (((size) + 3) & -4);
#define D_TEMPALLOC(size)	(char*)mallocptr
#define D_TEMPFREE()
#endif

#ifdef _DEBUG
#define DMalloc_DebugPrint printWarning
#else
#define DMalloc_DebugPrint (void)
#endif

#define D_MALLOC_BEGIN() \
	{ \
		volatile const char* _oldmalloc = mallocptr;

#ifdef __GNUC__
#define D_MALLOC_END() \
		D_TEMPFREE();\
		if(mallocptr > _oldmalloc)\
			DMalloc_DebugPrint("malloc(%d) in %s, line %d. Malloc usage: %d\n", mallocptr-_oldmalloc, FUNCNAME, __LINE__, (mallocptr-malloctab));\
	} // D_MALLOC_BEGIN block
#else
#define D_MALLOC_END() \
		D_TEMPFREE();\
		if(mallocptr > _oldmalloc)\
			DMalloc_DebugPrint("malloc(%d) in " FUNCNAME ", line %d. Malloc usage: %d\n", mallocptr-_oldmalloc, __LINE__, (mallocptr-malloctab));\
	} // D_MALLOC_BEGIN block
#endif

extern int leadAIRequired;
extern int leadAILoaded;
extern int pathAILoaded;

extern char* LevelNames[];
extern char* LoadingScreenNames[];

struct DB
{
	char* primptr;
	OTTYPE* ot;
	char* primtab;
	int id;
	DRAWENV draw;
	DISPENV disp;
};

extern DB MPBuff[2][2];
extern DB* last;
extern DB* current;

extern DRAW_MODE draw_mode_pal;
extern DRAW_MODE draw_mode_ntsc;

#define SCREEN_FB	512
#define SCREEN_FB_H	256

#ifdef PAL_VERSION
#define SCREEN_H	256
#define draw_mode draw_mode_pal
#define video_mode MODE_PAL
#else
#define SCREEN_H	240
#define draw_mode draw_mode_ntsc
#define video_mode MODE_NTSC
#endif // PAL

// ordering table size 
#ifdef PSX
#define	OTSIZE	0x1080
#else
#define	OTSIZE	0x8000		// [A] was 0x2000: depth index (z >> 1) must stay inside the table; 0x8000 covers the full 16 bit GTE depth (unlimited draw distance)
#endif

#if USE_EXTENDED_PRIM_POINTERS
#	define PRIMTAB_SIZE 0x80000	// [A] was 0x50000: the unlimited draw distance builds up to about 10000 map polygons
#else
#	define PRIMTAB_SIZE 0x1e000
#endif

#define CITYLUMP_DATA1		0
#define CITYLUMP_TPAGE		1
#define CITYLUMP_DATA2		2
#define CITYLUMP_SPOOL		3

extern int citystart[NUM_CITIES];
extern XYPAIR citylumps[NUM_CITIES][4];

#ifndef PSX
// Driver 1 DLC library (src_rebuild/d1dlc/d1dlc.h)
extern int gD1DlcCityMask;
extern int gD1DlcEnabled;
extern bool D1Dlc_CityAvailable(int gameLevel);
extern bool D1Dlc_ConversionPending();
extern int gD1DlcChecked;
extern int gD1DlcPrepared;
extern int D1Dlc_CheckData();
extern void D1Dlc_SaveConfig();
extern bool D1Dlc_StartPosition(int gameLevel, int* x, int* z, int* rotation);
extern int D1Dlc_PlayerCars(int gameLevel);
extern bool D1Dlc_ConvertCity(int dlcIndex, const char* dataRoot, char* outPath, int outPathSize);
extern const char* D1Dlc_LastLog();

// WSPC DLC library (src_rebuild/wspcdlc/wspcdlc.h)
extern int gWspcDlcEnabled;
extern int gWspcDlcChecked;
extern int gWspcDlcPrepared;
extern int gWspcDlcCarMask;
extern int WspcDlc_CheckData();
extern void WspcDlc_SaveConfig();
extern void WspcDlc_CarFolder(int car, char* path, int size);
extern int WspcDlc_DonorModel(int car);
extern const char* WspcDlc_CarTitle(int car);

// Driver 1 missions (src_rebuild/d1dlc/d1_missions.h)
extern bool D1Missions_Available();
extern int D1Missions_CityCount(int gameLevel);
extern bool D1Missions_Entry(int gameLevel, int index, int* number, const char** title);
extern const char* D1Missions_Title(int number);
#endif

#define CDSECTOR_SIZE 2048

extern void ClearMem(char *mem, int size); // 0x0007F3E8
extern void setMem8(unsigned char *mem, unsigned char val, int size); // 0x0007F47C
extern void setMem16(unsigned short *mem, unsigned short val, int size); // 0x0007F524

extern void Init_FileSystem(); // 0x0007F848

extern int FileExists(char *filename); // 0x0007F5C8
extern int Loadfile(char *name, char *addr); // 0x0007F354
extern int LoadfileSeg(char *name, char *addr, int offset, int loadsize); // 0x0007E9D0

extern void ReportMode(int on); // 0x0007F8B8

extern void loadsectors(char *addr, int sector, int nsectors); // 0x0007F904

extern void EnableDisplay(); // 0x0007F984
extern void DisableDisplay(); // 0x0007F9F0

extern void ClearCurrentDrawBuffers();

extern void SwapDrawBuffers(); // 0x0007F6FC
extern void SwapDrawBuffers2(int player); // 0x0007ECF0

extern void UpdatePadData(); // 0x0007FA10

extern void SetupDrawBuffers(); // 0x0007EDDC
extern void SetupDrawBufferData(int num_players); // 0x0007EF0C

extern void InitaliseDrawEnv(DB *pBuff, int x, int y, int w, int h); // 0x0007F7C0

extern void ResetCityType(); // 0x0007F5B4
extern void SetCityType(CITYTYPE type); // 0x0007F118

extern CITYTYPE GetCityType();
extern int SwapCitySpool(void);		// day night cycle: spool from the other LEV of the city (D2 only)
extern int gCitySpoolNight;
extern CDTYPE DiscSwapped(char *filename); // 0x0007F640


#endif
