#ifndef D1DLC_H
#define D1DLC_H

// Driver 1 cities "DLC": the player copies their own Driver 1 data into <install>/DRIVER1/
// (LEVELS\*.LEV, DATA\). REDRIVER2 detects it at startup and converts each city once into
// Driver 2 format under <install>\DRIVER1\CONVERTED\. No Driver 1 asset is shipped.

#define D1DLC_DATA_FOLDER		"DRIVER1\\"
#define D1DLC_CONVERTED_FOLDER	"DRIVER1\\CONVERTED\\"
#define D1DLC_CONVERTER_VERSION	38// bump when the output format changes -> forces reconversion

#define D1DLC_FIRST_CITY		4				// GameLevel of the first Driver 1 city
#define D1DLC_NUM_CITIES		7
#define D1DLC_GARAGE			6				// index of the car park level: it plays in the GameLevel of Training, no menu lists it

struct D1DlcCity
{
	int			gameLevel;						// REDRIVER2 GameLevel index
	const char*	name;							// LevelNames entry, also the converted LEV name
	const char*	sourceLev;						// file in DRIVER1/LEVELS
	int			startX, startZ, startRotation;	// Take a Ride start: middle of a long flat 2+ lane road in the centre, facing along it
	int			playerCars;						// player cars of the city (converted car models 8 and up), 0 = none
};

extern const D1DlcCity g_D1DlcCities[D1DLC_NUM_CITIES];

// Result of D1Dlc_Detect at startup (bit i = g_D1DlcCities[i] playable)
extern int gD1DlcCityMask;

// Options - Gameplay - Secret - Extended Settings - Driver 1 DLC: the menus list the Driver 1 cities only when this is on (and the data is there)
extern int gD1DlcEnabled;

// 1 while the Driver 1 Garage / Test Level mission is loaded: the Training slot (GameLevel D1DLC_FIRST_CITY + 5) then uses the car park LEV
extern int gD1DlcGarage;

// Index in g_D1DlcCities of a GameLevel (the Training level gives the garage while gD1DlcGarage is set), -1 when it has none
int D1Dlc_CityIndex(int gameLevel);

// The Driver 1 data is looked for once, from the menu (Extended Settings - Driver 1 DLC - Check Data), and the answer is kept in
// config.ini [d1dlc] checked / mask / enabled so it is not scanned again at every start. Until it was checked the mask is 0.
extern int gD1DlcChecked;
// converter version the cities were converted for by the last check (0 = not prepared): with the current version the
// game does not look at the converted files again
extern int gD1DlcPrepared;
void D1Dlc_SetConfig(const char* configPath, int checked, int mask, int enabled, int prepared);
int D1Dlc_CheckData();			// scans DRIVER1\LEVELS and converts every city found (about a second each): show a wait message first			// scans DRIVER1\LEVELS, stores and saves the result, returns the mask
void D1Dlc_SaveConfig();		// writes the [d1dlc] section of config.ini (checked, mask, enabled)

// Take a Ride start position and rotation of a Driver 1 city; false when the city has none (the Chicago start is used).
bool D1Dlc_StartPosition(int gameLevel, int* x, int* z, int* rotation);

// Number of player cars the player can choose in a Driver 1 city (0 for any other level).
int D1Dlc_PlayerCars(int gameLevel);

// A city is selectable when its source LEV is present and it can be converted (the first call converts the city).
bool D1Dlc_CityAvailable(int gameLevel);

// true when a present city still has to be converted: the next D1Dlc_CityAvailable call blocks for about a second per city.
bool D1Dlc_ConversionPending();

// Converts city dlcIndex (index in g_D1DlcCities) when its converted LEV is missing or its stamp <NAME>.VER is outdated.
// Always writes the path of the converted LEV (<dataRoot>DRIVER1\CONVERTED\<NAME>.LEV) to outPath; returns false when it
// cannot be used. The donor is <dataRoot>DRIVER2\LEVELS\CHICAGO.LEV.
bool D1Dlc_ConvertCity(int dlcIndex, const char* dataRoot, char* outPath, int outPathSize);

// Converter messages of the last D1Dlc_ConvertCity call (empty when nothing was converted).
const char* D1Dlc_LastLog();

// Checks <dataRoot>DRIVER1\LEVELS\ for each city LEV. Returns a bit mask, bit i = g_D1DlcCities[i] present.
int D1Dlc_Detect(const char* dataRoot);

#endif // D1DLC_H
