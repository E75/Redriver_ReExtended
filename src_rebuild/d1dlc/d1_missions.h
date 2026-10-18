#ifndef D1_MISSIONS_H
#define D1_MISSIONS_H

// Driver 1 story missions as standalone Driver 2 missions (plan: plans/d1-missions.md).
// <install>\DRIVER1\MISSIONS\MISSION.BLK (the player's own copy) is converted at runtime into
// <install>\DRIVER1\CONVERTED\M<1000 + n>.D2MS plus a list MISSIONS.LST for the menu. A mission the
// converter cannot map exactly is skipped with a log line; nothing here touches the city conversion.

#include <string>
#include <vector>

#define D1MISSION_BASE			1000	// mission numbers of converted missions: D1MISSION_BASE + D1 story mission
#define D1MISSION_VERSION		8	// bump when the converter output changes -> forces reconversion
#define D1MISSION_STORY_COUNT	44		// the first 44 scripts of MISSION.BLK are the story missions
#define D1MISSION_FREE_FIRST	117		// scripts 117 (Garage) and 118 (Test Level): free drives in the Training level
#define D1MISSION_FREE_COUNT	2

// Converts one MISSION.BLK script into a D2MS file. false (with the reason in why) when it needs something
// the engine cannot do yet. title/city are filled on success (city = GameLevel of the converted city).
bool D1Missions_ConvertScript(const std::vector<char>& script, std::vector<char>& d2ms, std::string& title, int& city, std::string& why);

// Kill switch, config.ini [d1dlc] missions=0 turns the whole feature off.
extern int gD1MissionsEnabled;

// true when MISSION.BLK is present and at least one mission is converted. The first call converts (or reads the list of an
// earlier conversion) and never fails the game: any problem only means "no missions".
bool D1Missions_Available();

// Converted missions of one city (GameLevel), in story order.
int D1Missions_CityCount(int gameLevel);
bool D1Missions_Entry(int gameLevel, int index, int* number, const char** title);

// Menu title of a converted mission number, NULL when there is none.
const char* D1Missions_Title(int number);

// Converter messages of the last conversion (one line per skipped mission).
const char* D1Missions_LastLog();

#endif // D1_MISSIONS_H
