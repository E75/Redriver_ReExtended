#ifndef WSPCDLC_H
#define WSPCDLC_H

// WSPC "DLC": the player copies their own WSPC vehicle files into <install>/WSPC/VEHICLES/ (<NAME>.DOB). REDRIVER2 detects them
// from the menu (Extended Settings - WSPC DLC - Check DLC Folder) and converts each car once into Redriver format under
// <install>\WSPC\CONVERTED\<NAME>\. Nothing of WSPC is shipped. Format and plan: docs/wspc.md, docs/dev-notes/wspc-dlc.md.

#define WSPCDLC_DATA_FOLDER			"WSPC\\"
#define WSPCDLC_VEHICLES_FOLDER		"WSPC\\VEHICLES\\"
#define WSPCDLC_CONVERTED_FOLDER	"WSPC\\CONVERTED\\"
#define WSPCDLC_CONVERTER_VERSION	4	// bump when the output format changes -> forces reconversion
#define WSPCDLC_CAR_SCALE			0.55	// source units to Redriver units: a Redriver car is about 750 units long (half length 375)

// Redriver driving behaviour that fits a WSPC car best; the table is in wspc_cars.cpp
enum WspcBehaviour
{
	WSPC_COP,
	WSPC_TAXI,
	WSPC_AMBULANCE,
	WSPC_BUS,
	WSPC_VAN,
	WSPC_SEDAN,
	WSPC_SPORTS,
	WSPC_COMPACT,
	WSPC_LIMO,
	WSPC_HEAVY,
};

struct WspcCar
{
	const char*		name;			// file name without extension, also the converted folder name
	const char*		title;			// shown in the menu
	WspcBehaviour	behaviour;
};

#define WSPCDLC_NUM_CARS	16
extern const WspcCar g_WspcCars[WSPCDLC_NUM_CARS];

// Options - Gameplay - Secret - Extended Settings - WSPC DLC: the car menus list the WSPC cars only when this is on (and the cars are converted)
extern int gWspcDlcEnabled;
extern int gWspcDlcChecked;		// the folder was checked once, the answer is kept in config.ini [wspcdlc]
extern int gWspcDlcPrepared;	// converter version the cars were converted for by the last check (0 = not prepared)
extern int gWspcDlcCarMask;		// bit i = g_WspcCars[i] converted and usable

void WspcDlc_SetConfig(const char* configPath, int checked, int mask, int enabled, int prepared);
int WspcDlc_CheckData();		// looks at WSPC\VEHICLES, converts every car found (a second or two): show a wait message first
void WspcDlc_SaveConfig();		// writes the [wspcdlc] section of config.ini

// Redriver files of a converted car: <install>\WSPC\CONVERTED\<NAME>\ (CLEAN.MDL, DAMAGED.MDL, LOW.MDL, TEXTURE.BIN, CAR.INFO)
void WspcDlc_CarFolder(int car, char* path, int size);

// Driver 2 car model whose handling the WSPC car borrows (wspc_cars.cpp)
int WspcDlc_DonorModel(int car);
const char* WspcDlc_CarTitle(int car);

// Converter messages of the last WspcDlc_CheckData call.
const char* WspcDlc_LastLog();

#endif // WSPCDLC_H
