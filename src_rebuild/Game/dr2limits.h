#ifndef DRLIMITS_H
#define DRLIMITS_H

// DRIVER 2 game engine limits
// please populate this file only with engine limits during refactoring


#ifndef PSX
#define DYNAMIC_LIGHTING
#define MAX_DLIGHTS				32
#endif

#ifndef PSX
#define MAX_PLAYERS				16			// used for replay streams mostly
#else
#define MAX_PLAYERS				8			// used for replay streams mostly
#endif

// cities: 0..3 are Driver 2, 4..9 are the optional Driver 1 cities (keep in sync with d1dlc/d1dlc.h)
#define NUM_CITIES				10
#define FIRST_DRIVER1_CITY		4
#define IS_DRIVER1_CITY(lvl)	((lvl) >= FIRST_DRIVER1_CITY)
#define D1_TRAINING_LEVEL		(FIRST_DRIVER1_CITY + 5)	// Driver 1 training ground: no cop or civilian car textures, so no traffic
#define D1_NEWCASTLE_LEVEL		(FIRST_DRIVER1_CITY + 4)	// Driver 1 Newcastle: traffic, but no cops
#define D1_MISSION_BASE			1000	// mission numbers from here on are converted Driver 1 missions (keep in sync with d1dlc/d1_missions.h)
#define IS_DRIVER1_MISSION(n)	((n) >= D1_MISSION_BASE)
#define IS_D1_GARAGE_MISSION(n)	((n) == D1_MISSION_BASE + 117 || (n) == D1_MISSION_BASE + 118)	// car park level (IVIEW.LEV) in the Training slot, see d1dlc.h gD1DlcGarage
#define FIRST_D1_PLAYER_CAR		8		// the converter moves the D1 player cars (D1 models 5..9) to 8..12
#define D1_CAR_SIZE_PERCENT		120		// D1 cars are this big in percent of the D2 cars (D2 = 100), one setting for every D1 car (cosmetic.c)
#define D1_PED_SCALE_PERCENT	120		// Tanner and pedestrians are drawn this much bigger in D1 cities (drawing only)
// D1 player car handling values (the D1 EXE has no per car table, all cars share them; the D2 code is the D1 code with lengths / 1.4648):
// neutral power / traction / mass; twist rates 110 / 550 of D1 times 1.4648^2 for the D2 sized world (cosmetic.c)
#define D1_CAR_MASS				4096
#define D1_CAR_POWER_RATIO		4096
#define D1_CAR_TRACTION			4096
#define D1_CAR_TWIST_Y			236
#define D1_CAR_TWIST_Z			1180
#define D1_CAMERA_ZOOM_PERCENT	120		// follow camera distance in D1 cities, on top of the Extended Settings choice
#define D1_CAMERA_HEIGHT_PERCENT	110		// follow camera height in D1 cities

// map region slots ("barrel"): the level data is made for 2x2, we keep 3x3 loaded so the view reaches 32+ cells in every direction
// a region lives in slot (x mod 3, z mod 3); object indices of cells are rebased on load, see RebaseCellData
#define REGION_SLOTS_ACROSS		3
#define NUM_REGION_SLOTS		(REGION_SLOTS_ACROSS * REGION_SLOTS_ACROSS)
#define REGION_SLOT(rx, rz)		(((((rx) % REGION_SLOTS_ACROSS) + REGION_SLOTS_ACROSS) % REGION_SLOTS_ACROSS) + \
								 ((((rz) % REGION_SLOTS_ACROSS) + REGION_SLOTS_ACROSS) % REGION_SLOTS_ACROSS) * REGION_SLOTS_ACROSS)

#define MAX_MODEL_SLOTS			1536		// DO NOT CHANGE. No effect in upping it - limited by cell types
#ifndef PSX
#define MAX_CARS				120			// room for the 10x crowd option (gCrowdMul); collision pairs are a bit set, see HANDLING_DATA. Car ids are a signed char (max 127, camera box = MAX_CARS + 1)
#else
#define MAX_CARS				20
#endif

#ifndef PSX
#define MAX_CAR_RESIDENT_MODELS	6			// use all permanently loaded car models available
#else
#define MAX_CAR_RESIDENT_MODELS	5
#endif

#define SPECIAL_CAR_SLOT        (MAX_CAR_RESIDENT_MODELS-1)

// the limits of the original game: the PC build multiplies them by gCrowdMul (1..5, secret menu "crowd")
#define MAX_PEDESTRIANS_BASE	28
#define MAX_SEATED_PEDS_BASE	20
#define MAX_PLACED_PEDS_BASE	15

#ifndef PSX
#define CROWD_MUL_MAX			10
extern int gCrowdMul;
#define MAX_PEDESTRIANS			(MAX_PEDESTRIANS_BASE * CROWD_MUL_MAX)
#define MAX_SEATED_PEDS			(MAX_SEATED_PEDS_BASE * gCrowdMul)
#define MAX_PLACED_PEDS			(MAX_PLACED_PEDS_BASE * gCrowdMul)
#else
#define MAX_PEDESTRIANS			MAX_PEDESTRIANS_BASE
#define MAX_SEATED_PEDS			MAX_SEATED_PEDS_BASE
#define MAX_PLACED_PEDS			MAX_PLACED_PEDS_BASE
#endif
#define MAX_EXPLOSION_OBJECTS	5
#define MAX_THROWN_BOMBS		5
#define MAX_MOTION_CAPTURE		24

// denting
#define MAX_DAMAGE_ZONE_VERTS	50
#define MAX_DAMAGE_ZONE_POLYS	70
#define MAX_DAMAGE_LEVELS		256

#ifndef PSX
#define MAX_DENTING_VERTS		256
#define MAX_DENTING_UVS			256
#define MAX_DENTING_LOD_UVS		256
#else
#define MAX_DENTING_VERTS		132
#define MAX_DENTING_UVS			255
#define MAX_DENTING_LOD_UVS		134
#endif

// Tyre track visuals
#define MAX_TYRE_PLAYERS		4
#ifndef PSX
#define MAX_TYRE_TRACK_WHEELS	4
#define MAX_TYRE_TRACK_PLAYERS	4
#else
#define MAX_TYRE_TRACK_WHEELS	2
#define MAX_TYRE_TRACK_PLAYERS	2
#endif

// Mission limits
// DO NOT EDIT, breaks compatibility!
#define MAX_MISSION_THREADS		16
#define MAX_MISSION_TARGETS		16

// replay definitions.
// DO NOT EDIT, breaks compatibility!
#define MAX_REPLAY_STREAMS		MAX_PLAYERS
#define MAX_REPLAY_CAMERAS		60
#define MAX_REPLAY_WAYPOINTS	150
#define MAX_REPLAY_PINGS		400

// debris limits
#define MAX_SMOKE				80
#define MAX_LEAVES				50
#define MAX_BIRDS				7
#define MAX_DEBRIS				60
#define MAX_GROUND_DEBRIS		16
#define MAX_DAMAGED_LAMPS		5
#define MAX_SMASHED_OBJECTS		8
#define MAX_LAMP_STREAKS		21
#define MAX_RAIN_DROPS			180

// driving games limit
#define MAX_SMASHED_CONES		6

// sound limits
#define MAX_SFX_CHANNELS		16		// DON'T CHANGE unless you have more than 24 channels (no PSX code)
#define MAX_LEVEL_ENVSOUNDS		32
#define MAX_SIREN_NOISES		2
#define MAX_CAR_NOISES			4


#endif // DRLIMITS_H