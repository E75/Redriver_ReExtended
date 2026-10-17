#ifndef DRAW_H
#define DRAW_H

enum PlotFlags
{
	PLOT_TRANSPARENT = (1 << 0),
	PLOT_INV_CULL = (1 << 1),
	PLOT_NO_CULL = (1 << 2),
	PLOT_NO_SHADE = (1 << 3),
	PLOT_CUSTOM_PALETTE = (1 << 4),
};

// Primitive plot context used in scratchpad
struct _pct
{
	int f4colourTable[32];
	u_int planeColours[8];
	int scribble[8];
	u_short(*ptexture_pages)[128];
	u_short(*ptexture_cluts)[128][32];
	struct DB* current;
	SVECTOR* verts;
	int* polySizes;
	char* primptr;
	OTTYPE* ot;
	u_int colour;
	u_int flags;
	u_short clut;
	u_short tpage;
};

#ifndef PSX
// tpage bits 13-15 tell the PC renderer how weather snow lies on that polygon (PsyX_render.cpp applySnow); without a mode
// a model only gets snow on up facing faces near the road level under the camera
#define NO_SNOW_TPAGE(tpage) ((tpage) | 0x8000)			// sky, pedestrians
#define GROUND_SNOW_TPAGE(tpage) ((tpage) | 0x4000)		// ground tiles: up facing faces at any height
#define SPRITE_SNOW_TPAGE(tpage) ((tpage) | 0xA000)		// trees: top edges of the leaves
#define CAR_SNOW_TPAGE(tpage) ((tpage) | 0xC000)		// a light dusting on roof, bonnet and boot
#else
#define NO_SNOW_TPAGE(tpage) (tpage)
#define GROUND_SNOW_TPAGE(tpage) (tpage)
#define SPRITE_SNOW_TPAGE(tpage) (tpage)
#define CAR_SNOW_TPAGE(tpage) (tpage)
#endif

#ifdef DYNAMIC_LIGHTING
#ifndef PSX
extern int gFarBudget;
extern int gMapEstPolys;
extern int gMapFarPolys;
extern int gMapPrimBytes;			// primitive buffer bytes used by the map (test log)
extern int gDrawDistanceMode;		// 0 original view, 1 = 5x the cells of the original view, 2 = "10x": all loaded regions (radius 63)
extern int ViewDrawDistance(void);	// how far cars and pedestrians are drawn (world units)
extern int SpawnViewDistance(void);	// the original pop in distance
extern int TrafficRadius(void);		// traffic pings out beyond this
extern int CarDrawDistance(void);	// cars fade out up to here (TrafficRadius - 6000)
extern int PedDrawDistance(void);	// pedestrians fade out up to here
extern int SpawnAngleOutOfView(u_int rnd);	// spawn angle behind or beside the player
extern int PositionInViewCone(int x, int z, int spawnDist);	// a world position is on the screen and closer than spawnDist
#define CAR_VIEW_SPAWN_DISTANCE	(CarDrawDistance() * 4 / 5)	// nearest a car may appear in view: where its distance fade (last fifth) hides it, a spawn stream far ahead
#define PED_VIEW_SPAWN_DISTANCE	(SpawnViewDistance() / 2)		// nearest a pedestrian may appear in view
extern int ObjectFade(int dist, int drawDist, int appearedFrame);	// 0..ONE, fade in after appearing, out at the draw distance
extern void SetObjectFade(char* start, char* end, int fade);	// primitives written to [start, end) get this fade
extern int LampDrawDistance(void);	// how far lamp posts (sprites) and their glows are drawn
extern int gVolumeLights;			// 1 = lamps and headlights light the air (night, fog, rain)
extern void UpdateVolumeLights(void);
extern int gAtmosphere;				// 1 = height fog in the low streets, fog glows towards the sun
#endif

extern int gEnableDlights;
extern int gNumDlights;

#define DLIGHT_SIREN	1	// pad of a zero dir: the light also lights the road and walls fully (cop car siren)

extern void AddDlight(VECTOR* position, CVECTOR* color, int radius, SVECTOR* dir);
extern void GetDLightLevel(SVECTOR* position, u_int* inOutColor);
#endif

extern SVECTOR day_vectors[4];
extern SVECTOR night_vectors[4];
extern SVECTOR day_colours[4];
extern SVECTOR night_colours[4];

extern MATRIX aspect;
extern MATRIX identity;
extern MATRIX inv_camera_matrix;
extern MATRIX face_camera;

extern MATRIX2 matrixtable[64];
extern MATRIX2 CompoundMatrix[64];

#ifdef PSX

#define MAX_DRAWN_BUILDINGS		192
#define MAX_DRAWN_TILES			320
#define MAX_DRAWN_ANIMATING		20
#define MAX_DRAWN_SPRITES		75

#define DRAW_LOD_DIST_HIGH		4000
#define DRAW_LOD_DIST_LOW		7000

#define plotContext (*(_pct*)((u_char*)getScratchAddr(0) + 1024 - sizeof(_pct)))

#else

#define MAX_DRAWN_BUILDINGS		1024
#define MAX_DRAWN_TILES			4000
#define MAX_DRAWN_ANIMATING		128
#define MAX_DRAWN_SPRITES		512

#define DRAW_LOD_DIST_HIGH		7000
#define DRAW_LOD_DIST_LOW		10000

extern _pct& plotContext;

#endif

extern void* model_tile_ptrs[MAX_DRAWN_TILES];

#ifndef PSX
// cell origin of each drawn tile/sprite: packed x/z are 16 bit, this restores the full position
extern XZPAIR model_tile_near[MAX_DRAWN_TILES];
extern XZPAIR spriteListNear[MAX_DRAWN_SPRITES];
#endif

extern int units_across_halved;
extern int units_down_halved;
extern int pvs_square;
extern int pvs_square_sq;
extern int PolySizes[56];

extern int setupYet;

extern int combointensity;

extern int gForceLowDetailCars;
extern int num_cars_drawn;

extern int PositionVisible(VECTOR* pos); // 0x0005D560

extern void DrawMapPSX(int *comp_val); // 0x0003F6B0

#ifndef PSX
extern int gSnowTarget;
extern int gFogTarget;
extern void DrawSnowFlakes(void);
#endif

extern void SetupPlaneColours(u_int ambient); // 0x00040218
extern void SetupDrawMapPSX(); // 0x00040408

extern void InitFrustrumMatrix(); // 0x00040534
extern void SetFrustrumMatrix(); // 0x000417A4
extern void Set_Inv_CameraMatrix(); // 0x000417DC

extern void CalcObjectRotationMatrices(); // 0x00041814

extern void PlotMDL_less_than_128(MODEL *model); // 0x000418BC

extern void DrawAllTheCars(int view); // 0x000407D8

extern void RenderModel(MODEL *model, MATRIX *matrix, VECTOR *pos, int zBias, int flags, int subdiv, int nrot); // 0x0004143C
extern int gRenderNoSnow;	// 1 = RenderModel draws without weather snow (Tanner's body)


#endif
