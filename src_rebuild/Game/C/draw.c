#include "driver2.h"

#include "draw.h"
#include "main.h"
#include "map.h"
#include "models.h"
#include "system.h"
#include "camera.h"
#include "mission.h"
#include "cell.h"
#include "tile.h"
#include "objanim.h"
#include "texture.h"
#include "cars.h"
#include "debris.h"
#include "cosmetic.h"
#include "players.h"
#include "dr2roads.h"
#include "ASM/rndrasm.h"
#include "event.h"
#include "spool.h"
#include "chopper.h"
#include "sky.h"
#include "daycycle.h"

#ifndef PSX
#include "PsyX/PsyX_public.h"
#include "PsyX/PsyX_globals.h"
#endif

MATRIX aspect =
{
	{
		{ 6553, 0, 0 },
		{ 0, 4096, 0 },
		{ 0, 0, 4096 }
	},
	{ 0, 0, 0 }
};

MATRIX identity =
{
	{
		{ 4096, 0, 0 },
		{ 0, 4096, 0 },
		{ 0, 0, 4096 }
	},
	{ 0, 0, 0 }
};

int PolySizes[56] = {
	8,  12, 16, 24,
	20, 20, 28, 32,
	8,  12, 16, 16,
	0,  0,  0,  0,
	12, 12, 12, 16,
	20, 20, 24, 24,
	0,  0,  0,  0,
	0,  0,  0,  0,
	8,  12, 16, 24,
	20, 24, 28, 32,
	0,  0,  0,  0,
	0,  0,  0,  0,
	12, 12, 12, 16,
	20, 20, 24, 24,
};

SVECTOR day_vectors[4] =
{
  { -3132, 2520, 783, 0 },
  { -3132, 2520, 783, 0 },
  { -2364, 2364, 2364, 0 },
  { -3132, 2520, 783, 0 }
};

SVECTOR night_vectors[4] =
{
  { 3132, 2520, 783, 0 },
  { 3132, 2520, 783, 0 },
  { -2364, 2364, -2364, 0 },
  { 3132, 2520, 783, 0 }
};

SVECTOR day_colours[4] =
{
  { 3200, 3200, 3200, 0 },
  { 3200, 3200, 3200, 0 },
  { 3200, 3200, 3200, 0 },
  { 3200, 3200, 3200, 0 }
};

SVECTOR night_colours[4] =
{
  { 880, 880, 905, 0 },
  { 880, 880, 905, 0 },
  { 880, 880, 905, 0 },
  { 880, 880, 905, 0 }
};

void* model_object_ptrs[MAX_DRAWN_BUILDINGS];
void* model_tile_ptrs[MAX_DRAWN_TILES];
void* anim_obj_buffer[MAX_DRAWN_ANIMATING];
void* spriteList[MAX_DRAWN_SPRITES];

#ifndef PSX
XZPAIR model_tile_near[MAX_DRAWN_TILES];
XZPAIR spriteListNear[MAX_DRAWN_SPRITES];
#endif

MATRIX inv_camera_matrix;
MATRIX face_camera;
MATRIX2 CompoundMatrix[64];

u_int farClip2Player = 36000;

#ifdef PSX
int goFaster = 1;
#else
int goFaster = 0;	// [A] was 1
#endif
int fasterToggle = 0;

int combointensity;

char CurrentPVS[PVS_CELL_COUNT * PVS_CELL_COUNT + 3]; // 20*20+4
MATRIX2 matrixtable[64];
int setupYet = 0;

int gDrawDistance = PVS_CELL_COUNT * PVS_CELL_COUNT;

#ifndef PSX
// [A] draw distance option (secret menu): 0 = original view, 1 = 5x and 2 = "10x", the widest view: everything the 3x3 loaded
// regions hold (radius 63, 40x the 441 cells of the original view), far objects thinned out. 10x is the default.
int gDrawDistanceMode = 1;
int gForceCarFade;	// -carfade <1..99>: every car is drawn with this fade (percent), to check the look
int gMapPrimBytes;
int gDrawStats[4];		// objects of the last frame: tiles, buildings, sprites, animated (test log, see main.c)

static int DrawDistanceCells(void)
{
	static const int multiplier[3] = { 1, 5, 40 };	// 40: radius 63, everything the 3x3 loaded regions can hold

	return PVS_CELL_COUNT * PVS_CELL_COUNT * multiplier[gDrawDistanceMode];
}

// square radius in cells that fits the cells of the draw distance
static int DrawRadiusOfCells(int cells)
{
	int radius = 0;

	while (radius < 63 && (radius * 2 + 3) * (radius * 2 + 3) <= cells)
		radius++;

	return radius;
}

#define FAR_THIN_RING 16	// widest draw distance: from this ring (cells from the player) on, small objects are left out
#define FAR_SPRITE_RING 24	// trees and lamp posts are drawn up to here
#define FAR_LOD_RING 5		// beyond this ring the low detail model is drawn

int gFarBudget = 10500;		// polygons per frame in the widest mode: dense Chicago draws 10400, about 10 ms; 65536 vertices is the hard limit
int gMapEstPolys;			// polygons the map walk accepted last frame (test log)
int gMapFarPolys;			// ... of which beyond ring 31 (cars and pedestrians stop there)

// Widest draw distance: far away only objects big enough to be seen are drawn. Ground (road, grass, water) stays, else the far view has holes.
static int FarObjectCulled(MODEL* model, int ring)
{
	if (model->shape_flags & SHAPE_FLAG_SPRITE)
		return ring > FAR_SPRITE_RING;

	if (model->flags2 & (MODEL_FLAG_PATH | MODEL_FLAG_ALLEY))
		return 1;

	if ((model->shape_flags & (SHAPE_FLAG_WATER | SHAPE_FLAG_TILE)) || (model->flags2 & MODEL_FLAG_GRASS))
		return 0;

	return model->bounding_sphere < (ring - FAR_THIN_RING) * MAP_CELL_SIZE / 48;
}

// Counts the polygons of an object the map walk accepts. In the widest mode far objects are thinned out (FarObjectCulled) and stop at
// gFarBudget: the walk goes from near to far, so the budget ends the view at the ring where it runs out. Returns 0 to skip the object.
static int ObjectAccepted(MODEL* model, int modelNum, int ring, int* estPolys, int* fullRing)
{
	int polys = (ring > FAR_LOD_RING ? pLodModels[modelNum] : model)->num_polys;

	if (gDrawDistanceMode == 2 && ring > FAR_THIN_RING)
	{
		if (FarObjectCulled(model, ring))
			return 0;

		if (*estPolys + polys > gFarBudget)
		{
			if (*fullRing < 0)
				*fullRing = ring;

			return 0;
		}
	}

	*estPolys += polys;

	if (ring > 31)
		gMapFarPolys += polys;

	return 1;
}

// how far a car or pedestrian can be seen: the original 22000 units reach about 10 cells, farther draw distances scale it
int ViewDrawDistance(void)
{
	// cars and pedestrians stop at radius 31 also in the widest mode: farther they are specks, and the traffic ring stays near the player
	return VIEW_DRAW_DISTANCE * (MIN(DrawRadiusOfCells(DrawDistanceCells()), 31) + 1) / (PVS_CELL_COUNT / 2 + 1) * MAP_CELL_SIZE / 2048;
}

// nearest distance a car or pedestrian may appear in view: the original pop in distance, not the (up to 3x farther) draw distance,
// else the road ahead stays empty at the big draw distances; cars that appear fade in (see DrawAllTheCars)
int SpawnViewDistance(void)
{
	return MIN(ViewDrawDistance(), VIEW_DRAW_DISTANCE * MAP_CELL_SIZE / 2048);
}

// Traffic lives within this radius: the cars of the crowd option stay near the player instead of spreading over the whole
// (up to 64000) draw distance, where they are specks. A car fades out before it pings out, its lamps a little later.
int TrafficRadius(void)
{
	return MIN(ViewDrawDistance() + 6000, 36000 * MAP_CELL_SIZE / 2048);
}

int CarDrawDistance(void)
{
	return TrafficRadius() - 6000;
}

// pedestrians farther than this are specks: they fade out there and ping out a little beyond it; the pedestrian drawing
// code keeps positions relative to the camera in 16 bit, beyond 30000 they would wrap round to a wrong place
int PedDrawDistance(void)
{
	return MIN(ViewDrawDistance(), 20000 * MAP_CELL_SIZE / 2048);
}

// true when a world position is on the screen and closer than spawnDist: a car or pedestrian may not appear there.
// Farther in view it may, it fades in (ObjectFade).
int PositionInViewCone(int x, int z, int spawnDist)
{
	int dx = x - camera_position.vx;
	int dz = z - camera_position.vz;
	int viewX = (inv_camera_matrix.m[0][0] * dx + inv_camera_matrix.m[0][2] * dz) >> 12;
	int viewZ = (inv_camera_matrix.m[2][0] * dx + inv_camera_matrix.m[2][2] * dz) >> 12;

	// the cone is wider than the real field of view (wide screens, camera swings)
	long long view = spawnDist;

	return viewZ > -1500 && ABS(viewX) < viewZ * 2 + 3000 && (long long)viewZ * viewZ + (long long)viewX * viewX < view * view;
}

// a spawn angle (0..4095, sin = x) 90 to 270 degrees off the camera heading: behind or beside the player, out of view
int SpawnAngleOutOfView(u_int rnd)
{
	return (ratan2(inv_camera_matrix.m[2][0], inv_camera_matrix.m[2][2]) + 1024 + (int)(rnd % 2048)) & 4095;
}

#define OBJECT_FADE_IN_FRAMES	45		// 1.5 seconds, longer when the player drives fast (FadeInFrames)

static int FadeInFrames(void)
{
	int frames = OBJECT_FADE_IN_FRAMES;

	if (MainPlayer.playerType == PLAYER_TYPE_CAR && MainPlayer.playerCarId >= 0)
		frames += MIN(ABS(car_data[MainPlayer.playerCarId].hd.speed), 160) * 30 / 160;

	return frames;
}
#define CAR_LAMPS_EXTRA_DISTANCE	5000	// lamps of a car show this far beyond its draw distance (it pings out at +6000)

// Fade of a car or pedestrian, 0 (gone) .. ONE (opaque): it fades out over the last fifth of its draw distance and fades in
// after it appeared (appearedFrame = FrameCnt then, -1 = no fade in), so nothing pops up or vanishes at full strength
int ObjectFade(int dist, int drawDist, int appearedFrame)
{
	int band = MAX(drawDist / 5, 1);
	int fade;

	if (dist >= drawDist)
		return 0;

	fade = (int)((long long)(drawDist - dist) * ONE / band);

	// a spawn frame in the future is stale (frame counter restarted): no fade-in, else the car stays invisible
	if (appearedFrame >= 0 && appearedFrame <= FrameCnt)
		fade = MIN(fade, (FrameCnt - appearedFrame) * ONE / FadeInFrames());

	return MAX(0, MIN(fade, ONE));
}

int LampDrawDistance(void)
{
	int rings = DrawRadiusOfCells(DrawDistanceCells());

	if (gDrawDistanceMode == 2)
		rings = MIN(rings, FAR_SPRITE_RING);

	return rings * MAP_CELL_SIZE;
}

// the primitives written to [start, end) belong to one object with this fade (see ObjectFade)
void SetObjectFade(char* start, char* end, int fade)
{
	if (fade < ONE)
		PsyX_SetPrimFade(start, end, (float)fade / ONE);
}
#endif

#ifndef PSX
_pct& plotContext = *(_pct*)((u_char*)getScratchAddr(0) + 1024 - sizeof(_pct));	// orig offset: 0x1f800020
#endif

struct MVERTEX5x5
{
	MVERTEX verts[5][5];
};

// [D] [T] [A]
void addSubdivSpriteShadow(POLYFT4* src, SVECTOR* verts, int z)
{
	int m;

	m = 4;

#if 0 //def PSX
	MVERTEX5x5& subdiVerts = *(MVERTEX5x5*)(u_char*)getScratchAddr(0);
#else
	MVERTEX5x5 subdiVerts;
#endif

	plotContext.colour = 0x2E000000;
	plotContext.flags = PLOT_INV_CULL;
	plotContext.clut = texture_cluts[src->texture_set][src->texture_id];
	plotContext.tpage = texture_pages[src->texture_set];

	if (z > 3200)
		m = 1; //2;

	plotContext.ot += 28;

	copyVector(&subdiVerts.verts[0][0], &verts[src->v0]);
	subdiVerts.verts[0][0].uv.val = *(ushort*)&src->uv0;

	copyVector(&subdiVerts.verts[0][1], &verts[src->v1]);
	subdiVerts.verts[0][1].uv.val = *(ushort*)&src->uv1;

	copyVector(&subdiVerts.verts[0][2], &verts[src->v3]);
	subdiVerts.verts[0][2].uv.val = *(ushort*)&src->uv3;

	copyVector(&subdiVerts.verts[0][3], &verts[src->v2]);
	subdiVerts.verts[0][3].uv.val = *(ushort*)&src->uv2;

	makeMesh((MVERTEX(*)[5][5])subdiVerts.verts, m, m);
	drawMesh((MVERTEX(*)[5][5])subdiVerts.verts, m, m, &plotContext);

	plotContext.ot -= 28;
}

// [D] [T] [A]
void DrawSprites(PACKED_CELL_OBJECT** sprites, int numFound)
{
	int i;
	int z;
	u_int spriteColour, lightdd;
	u_char lightLevel;
	MODEL* model;
	PACKED_CELL_OBJECT* pco;
	PACKED_CELL_OBJECT** list;
	int numShadows;

#if 0 //def PSX
	MVERTEX5x5& subdiVerts = *(MVERTEX5x5*)(u_char*)getScratchAddr(0);
	MATRIX shadowMatrix = *(MATRIX*)((u_char*)getScratchAddr(0) + sizeof(MVERTEX5x5));
#else
	MVERTEX5x5 subdiVerts;
	MATRIX shadowMatrix;
#endif
	SVECTOR* lightVec;

	lightVec = (gTimeOfDay == TIME_NIGHT) ? night_vectors : day_vectors;

	lightdd =	FIXEDH(camera_matrix.m[2][0] * lightVec[BASE_CITY].vx) +
				FIXEDH(camera_matrix.m[2][1] * lightVec[BASE_CITY].vy) +
				FIXEDH(camera_matrix.m[2][2] * lightVec[BASE_CITY].vz) + ONE * 3072;

	lightLevel = (lightdd >> 18) + 32 & 255;

	if (gWeather > WEATHER_NONE && gTimeOfDay == TIME_DAY || (M_BIT(gTimeOfDay) & (M_BIT(TIME_DAWN) | M_BIT(TIME_DUSK))))
	{
		lightLevel = (lightLevel * 2 * NightAmbient) >> 8;
	}
	if (gTimeOfDay == TIME_NIGHT)
	{
		if (GameLevel == 0)
			lightLevel *= 2;	// [A] level bug - Chicago trees lit wrong
		else
			lightLevel /= 3;
	}

	spriteColour = lightLevel << 0x10 | lightLevel << 8 | lightLevel | 0x2c000000;

	for (i = 0; i < 3; i++) 
	{
		shadowMatrix.m[i][0] = inv_camera_matrix.m[i][2];
		shadowMatrix.m[i][1] = -inv_camera_matrix.m[i][0];
		shadowMatrix.m[i][2] = inv_camera_matrix.m[i][0];
	}

	plotContext.primptr = current->primptr;
#ifndef PSX
	// sprites (trees) are billboards: snow only on the top edges of their leaves
	static u_short spriteTexturePages[128];
	for (i = 0; i < 128; i++)
		spriteTexturePages[i] = SPRITE_SNOW_TPAGE(texture_pages[i]);

	plotContext.ptexture_pages = &spriteTexturePages;
#else
	plotContext.ptexture_pages = (ushort(*)[128])texture_pages;
#endif
	plotContext.ptexture_cluts = (ushort(*)[128][32])texture_cluts;
	plotContext.polySizes = PolySizes;
	plotContext.ot = current->ot;
	plotContext.colour = spriteColour;
	plotContext.current = current;

	list = sprites;
	numShadows = 0;

	while (numFound--)
	{
		int modelnumber;
		pco = *list;
		list++;

		modelnumber = (pco->value >> 6) | (pco->pos.vy & 1) << 10;
		model = modelpointers[modelnumber];

		if ((pco->value & 63) == 63 || litSprites[modelnumber >> 5] & 1 << (modelnumber & 31))  // [A] multiple sprites lighting fixes
		{
			plotContext.colour = 0x2c808080;
		}
		else
		{
			plotContext.colour = spriteColour;
		}

#ifdef PSX
		plotContext.scribble[0] = pco->pos.vx;
		plotContext.scribble[2] = pco->pos.vz;
#else
		{
			const XZPAIR& nearCell = spriteListNear[list - sprites - 1];
			plotContext.scribble[0] = nearCell.x + (short)(pco->pos.vx - nearCell.x);
			plotContext.scribble[2] = nearCell.z + (short)(pco->pos.vz - nearCell.z);
		}
#endif
		plotContext.scribble[1] = (pco->pos.vy << 0x10) >> 0x11;

		z = Apply_InvCameraMatrixAndSetMatrix((VECTOR_NOPAD*)plotContext.scribble, (MATRIX2*)&face_camera);

		if (z < 1000)
		{
			POLYFT4* src;
			SVECTOR* verts;
			int numPolys;

			numPolys = (u_int)model->num_polys;
			src = GET_MODEL_DATA(POLYFT4, model, poly_block);
			verts = GET_MODEL_DATA(SVECTOR, model, vertices);

			plotContext.flags |= PLOT_NO_CULL;

			while (numPolys--, numPolys >= 0)
			{
				plotContext.clut = texture_cluts[src->texture_set][src->texture_id];
				plotContext.tpage = (*plotContext.ptexture_pages)[src->texture_set];

				copyVector(&subdiVerts.verts[0][0], &verts[src->v0]);
				subdiVerts.verts[0][0].uv.val = *(ushort*)&src->uv0;

				copyVector(&subdiVerts.verts[0][1], &verts[src->v1]);
				subdiVerts.verts[0][1].uv.val = *(ushort*)&src->uv1;

				copyVector(&subdiVerts.verts[0][2], &verts[src->v3]);
				subdiVerts.verts[0][2].uv.val = *(ushort*)&src->uv3;

				copyVector(&subdiVerts.verts[0][3], &verts[src->v2]);
				subdiVerts.verts[0][3].uv.val = *(ushort*)&src->uv2;

				makeMesh((MVERTEX(*)[5][5])subdiVerts.verts, 4, 4);
				drawMesh((MVERTEX(*)[5][5])subdiVerts.verts, 4, 4, &plotContext);

				src++;
			}

			plotContext.flags &= ~PLOT_NO_CULL;
		}
		else
		{
			plotContext.ot -= 133;

			Tile1x1(model);

			plotContext.ot += 133;
		}

#ifdef PSX
#define MAX_TREE_SHADOW_DISTANCE 7000
#else
#define MAX_TREE_SHADOW_DISTANCE 14000
#endif
		
		if (wetness == 0 && gTimeOfDay != TIME_NIGHT &&
			(pco->value & 32) == 0 && 
			z < MAX_TREE_SHADOW_DISTANCE &&
			numShadows < 40)
		{
			gte_SetRotMatrix(&shadowMatrix);

			// the shadow lies on the road tile: pull it in front like the other decals (z-fight flicker)
			PGXP_SetZOffsetScale(0.0f, 0.991f);

			addSubdivSpriteShadow(GET_MODEL_DATA(POLYFT4, model, poly_block), GET_MODEL_DATA(SVECTOR, model, vertices), z);

			if (model->num_polys == 2)
				addSubdivSpriteShadow(GET_MODEL_DATA(POLYFT4, model, poly_block) + 1, GET_MODEL_DATA(SVECTOR, model, vertices), z);

			PGXP_SetZOffsetScale(0.0f, 1.0f);
			gte_SetRotMatrix(&face_camera);

			numShadows++;
		}
	}
	current->primptr = plotContext.primptr;
}

// [D] [T]
void SetupPlaneColours(u_int ambient)
{
	u_int r, g, b;

	if (gWeather == 0 && (M_BIT(gTimeOfDay) & (M_BIT(TIME_DAWN) | M_BIT(TIME_DUSK))) == 0)
	{
		if (gTimeOfDay == TIME_DAY)
		{
			b = ambient & 255;
			g = ambient >> 8 & 255;
			r = ambient >> 16 & 255;
			
			plotContext.planeColours[1] = (r * 120 >> 7) << 16 | (g * 120 >> 7) << 8 | b * 120 >> 7;
			plotContext.planeColours[2] = (r * 103 >> 7) << 16 | (g * 103 >> 7) << 8 | b * 103 >> 7;
			plotContext.planeColours[3] = (r * 13 >> 5) << 16 | (g * 13 >> 5) << 8 | b * 13 >> 5;
			plotContext.planeColours[0] = r << 16 | g << 8 | b;
			plotContext.planeColours[4] = (r * 3 >> 3) << 16 | (g * 3 >> 3) << 8 | b * 3 >> 3;
			plotContext.planeColours[5] = plotContext.planeColours[3];
			plotContext.planeColours[6] = plotContext.planeColours[2];
			plotContext.planeColours[7] = plotContext.planeColours[1];
			return;
		}

		plotContext.planeColours[0] = ambient;
		plotContext.planeColours[1] = ambient;
		plotContext.planeColours[2] = ambient;
		plotContext.planeColours[3] = ambient;
		plotContext.planeColours[4] = ambient;
		plotContext.planeColours[5] = ambient;
		plotContext.planeColours[6] = ambient;
		plotContext.planeColours[7] = ambient;
		return;
	}

	plotContext.planeColours[0] = ambient;
	plotContext.planeColours[1] = ambient + 0x10101;
	plotContext.planeColours[2] = ambient + 0x30303;
	plotContext.planeColours[3] = ambient + 0x80808;
	plotContext.planeColours[4] = ambient + 0xa0a0a;
	plotContext.planeColours[5] = ambient + 0x80808;
	plotContext.planeColours[6] = ambient + 0x30303;
	plotContext.planeColours[7] = ambient + 0x10101;
}


int current_pvs_cell;


// [D] [T]
void SetupDrawMapPSX(void)
{
	int cell_x, cell_z;
	int pvs_cell;
	static int pvsRegionReady = 0;	// [A] the PVS of the camera cell was read from a loaded region

	if (setupYet != 0)
	{
		return;
	}

	cell_x = (camera_position.vx + units_across_halved) / MAP_CELL_SIZE;
	cell_z = (camera_position.vz + units_down_halved) / MAP_CELL_SIZE;

	current_cell_x = cell_x;
	current_cell_z = cell_z;

	pvs_cell = (cell_z % MAP_REGION_SIZE) * MAP_REGION_SIZE + (cell_x % MAP_REGION_SIZE);
	// [A] a region that is still loading gives an empty set (nothing near is drawn, no car shows): read it again every frame until it is there
	if (pvs_cell != current_pvs_cell || !pvsRegionReady)
	{
		int region_x1, region_z1;

		region_x1 = cell_x / MAP_REGION_SIZE;
		region_z1 = cell_z / MAP_REGION_SIZE;

		current_pvs_cell = pvs_cell;
		pvsRegionReady = PVSRegionReady(REGION_SLOT(region_x1, region_z1), region_x1 + region_z1 * regions_across);
		GetPVSRegionCell2(
			REGION_SLOT(region_x1, region_z1),
			region_x1 + region_z1 * regions_across,
			pvs_cell, CurrentPVS);
	}

	InitFrustrumMatrix();
	SetFrustrumMatrix();

	setupYet = 1;
}

MATRIX frustrum_matrix;

// [D] [T]
void InitFrustrumMatrix(void)
{
	int a, t;

	a = -camera_angle.vy;

	frustrum_matrix.m[0][1] = 0;
	frustrum_matrix.m[0][0] = RSIN(a);
	frustrum_matrix.m[0][2] = RCOS(a);

	a = (FrAng - 1024) - camera_angle.vy;

	frustrum_matrix.m[1][1] = 0;
	frustrum_matrix.m[1][0] = RSIN(a);
	frustrum_matrix.m[1][2] = RCOS(a);

	a = -(FrAng - 1024) - camera_angle.vy;

	frustrum_matrix.m[2][1] = 0;
	frustrum_matrix.m[2][0] = RSIN(a);
	frustrum_matrix.m[2][2] = RCOS(a);
}

// [D] [T]
void SetFrustrumMatrix(void)
{
	gte_SetLightMatrix(&frustrum_matrix);
}

// [D] [T]
void Set_Inv_CameraMatrix(void)
{
	gte_SetColorMatrix(&inv_camera_matrix);
}


// [D] [T] [A]
void CalcObjectRotationMatrices(void)
{
	int i;
	int angle;
	MATRIX* m;

	angle = 0;

	for (i = 0; i < 64; i++)
	{
		// simpler and faster method
		m = (MATRIX*)&matrixtable[i];

		InitMatrix(*m);
		RotMatrixY(angle, m);

		angle += 64;
	}
}

// [D] [T]
void PlotMDL_less_than_128(MODEL* model)
{
	RenderModel(model, NULL, NULL, 0, 0, 0, 0);
}

// [D] [T]
int PositionVisible(VECTOR* pos)
{
	return newPositionVisible(pos, CurrentPVS, current_cell_x, current_cell_z);
}

int gForceLowDetailCars = 0;
int num_cars_drawn = 0;

// [D] [T]
void DrawAllTheCars(int view)
{
	static int car_distance[MAX_CARS]; // offset 0x0
	CAR_DATA* cars_to_draw[MAX_CARS];

	int dx, dz;
	int dist;
	int i, j;
	CAR_DATA* cp;
	int num_cars_to_draw;
	int spacefree;
	

	num_cars_drawn = 0;
	num_cars_to_draw = 0;

	cp = &car_data[MAX_CARS - 1];
	do {
		if (cp->controlType != CONTROL_TYPE_NONE && !ChopperHidesCar(cp) &&
			PositionVisible((VECTOR*)cp->hd.where.t))
		{
			// XZ distance estimation
			dx = ABS(camera_position.vx - cp->hd.where.t[0]);
			dz = ABS(camera_position.vz - cp->hd.where.t[2]);

			if (dx < dz)
				dist = dz + dx / 2;
			else
				dist = dx + dz / 2;

#ifndef PSX
			if (dist < CarDrawDistance())
#else
			if (dist < VIEW_DRAW_DISTANCE)
#endif
			{
				car_distance[num_cars_to_draw] = dx + dz;
				cars_to_draw[num_cars_to_draw] = cp;
				num_cars_to_draw++;
			}
#ifndef PSX
			else if (dist < CarDrawDistance() + CAR_LAMPS_EXTRA_DISTANCE)
			{
				char* fadeStart = current->primptr;

				extern int gCivSpawnFrame[MAX_CARS];

				DrawCarLamps(cp);
				SetObjectFade(fadeStart, current->primptr, ObjectFade(dist, CarDrawDistance() + CAR_LAMPS_EXTRA_DISTANCE, (cp->controlType == CONTROL_TYPE_CIV_AI && !(cp->controlFlags & CONTROL_FLAG_PLAYER_LEFT)) ? gCivSpawnFrame[cp->id] : -1));
			}
#endif
		}
#ifdef DYNAMIC_LIGHTING
		else if (cp->controlType != CONTROL_TYPE_NONE && !ChopperHidesCar(cp))
			AddHiddenCarLights(cp);
#endif

		cp--;
	} while (cp >= car_data);

	if (num_cars_to_draw != 0)
	{
		gForceLowDetailCars = 0;

		// insertion sort of cars by distance
		for (i = 1; i < num_cars_to_draw; i++)
		{
			cp = cars_to_draw[i];
			dist = car_distance[i];

			j = i - 1;

			while (j >= 0 && dist < car_distance[j])
			{
				car_distance[j + 1] = car_distance[j];	// [A] the shift wrote to slot i, so the cars were not sorted nearest first
				cars_to_draw[j + 1] = cars_to_draw[j];
				j--;
			}
			
			cars_to_draw[j+1] = cp;
			car_distance[j+1] = dist;
		}

		spacefree = (num_cars_to_draw - 1) * 2000;

		for (i = 0; i < num_cars_to_draw; i++)
		{
			// Don't exceed draw buffers
			if ((int)(current->primtab + (-3000 - (int)(current->primptr - PRIMTAB_SIZE))) < 5800)
				return;

			// make cars look uglier
			if ((int)(current->primtab + (-3000 - (int)(current->primptr - PRIMTAB_SIZE)) - spacefree) < 5800)
				gForceLowDetailCars = 1;

			if (cars_to_draw[i]->controlType == CONTROL_TYPE_PLAYER)
				gForceLowDetailCars = 0;

#ifndef PSX
			// [A] a car fades in after it appeared and fades out over the last fifth of the draw distance, with its wheels
			// and shadow, so it never pops up or vanishes at full strength; its lamps reach farther (DrawCarLamps)
			{
				CAR_DATA* carp = cars_to_draw[i];
				int carDx = ABS(camera_position.vx - carp->hd.where.t[0]);
				int carDz = ABS(camera_position.vz - carp->hd.where.t[2]);
				int carDist = carDx < carDz ? carDz + carDx / 2 : carDx + carDz / 2;
				int appeared, fade, lampFade, drawnBefore;
				char* fadeStart;

				extern int gCivSpawnFrame[MAX_CARS];

				// a car the player left is a civilian car again, but it was not pinged in: no fade-in
				appeared = (carp->controlType == CONTROL_TYPE_CIV_AI && !(carp->controlFlags & CONTROL_FLAG_PLAYER_LEFT)) ? gCivSpawnFrame[carp->id] : -1;
				fade = ObjectFade(carDist, CarDrawDistance(), appeared);
				lampFade = ObjectFade(carDist, CarDrawDistance() + CAR_LAMPS_EXTRA_DISTANCE, appeared);

#ifdef DEBUG_OPTIONS
				if (gForceCarFade)
					fade = lampFade = gForceCarFade * ONE / 100;
#endif

				fadeStart = current->primptr;
				gCarLampsStart = NULL;
				drawnBefore = num_cars_drawn;

				DrawCar(carp, view);

#ifdef DYNAMIC_LIGHTING
				// culled by DrawCar (beside or behind the camera): its lamps go on lighting the player's car
				if (num_cars_drawn == drawnBefore)
					AddHiddenCarLights(carp);
#endif

				if (gCarLampsStart >= fadeStart)
				{
					SetObjectFade(fadeStart, gCarLampsStart, fade);
					SetObjectFade(gCarLampsStart, current->primptr, lampFade);
				}
				else
					SetObjectFade(fadeStart, current->primptr, fade);
			}
#else
			DrawCar(cars_to_draw[i], view);
#endif

			spacefree -= 2000;
		}
	}
}


// [D] [T]
u_int normalIndex(SVECTOR* verts, u_int vidx)
{
	SVECTOR* v0, *v1, *v2;
	int x, y;
	int th23;

	int nx, ny, nz;

	SVECTOR p, q;

	v0 = verts + (vidx & 255);
	v1 = verts + (vidx >> 8 & 255);
	v2 = verts + (vidx >> 16 & 255);

	p.vz = v1->vz - v0->vz;
	q.vz = v2->vz - v0->vz;

	p.vy = v1->vy - v0->vy;
	q.vy = v2->vy - v0->vy;

	p.vx = v1->vx - v0->vx;
	q.vx = v2->vx - v0->vx;

	nx = p.vy * q.vz - p.vz * q.vy;
	nz = p.vx * q.vy - p.vy * q.vx;

	x = nx + (nz >> 3);
	y = nz - (x >> 2);

	x += (y >> 3);

	if (x < y)
	{
		if (x + y < 1)
			th23 = y < 0 ? 5 : 4;
		else
			th23 = x < 0 ? 3 : 2;
	}
	else
	{
		if (x + y < 1)
			th23 = x < 0 ? 6 : 7;
		else
			th23 = y > 0 ? 1 : 0;
	}

	th23 *= 4;

	ny = p.vz * q.vx - p.vx * q.vz;

	nx = ABS(nx);
	nz = ABS(nz);

	if (nx + nz < ny)
		th23 += 1;

	if (nx + nz < -ny)
		th23 = th23 & 31 | 2;
	else
		th23 = th23 & 31;

	return th23 | 0x80;
}

// [A]
void ConvertPolygonTypes(MODEL* model, _pct* pc)
{
	SVECTOR* srcVerts;
	PL_POLYFT4* polys;
	u_char temp, ptype;
	int i;

	// [A] we are storing the processing flag here
	if (model->tri_verts & 0x80)
	{
		return;
	}
	
	model->tri_verts |= 0x80;

	srcVerts = GET_MODEL_DATA(SVECTOR, model, vertices);
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);
	i = model->num_polys;

	// pre-process vertices
	while (i-- > 0)
	{
		ptype = polys->id & 31;

		// convert poly types
		if ((ptype & 1) == 0 && ptype != 8) // is FT3 triangle?
		{
			temp = polys->uv2.v;
			polys->uv3.u = polys->uv2.u;
			polys->uv3.v = temp;

			polys->v3 = polys->v2;

			polys->id |= 1;
			ptype |= 1;
		}

		if (ptype != 11 && ptype != 21 && ptype != 23)
		{
			polys = (PL_POLYFT4*)((char*)polys + pc->polySizes[ptype]);
			continue;
		}

		// also precalculate normal index
		if (ptype != 21 && (polys->th & 0x80) == 0)
		{
			polys->th = normalIndex(srcVerts, *(u_int*)&polys->v0);
		}
		polys = (PL_POLYFT4*)((char*)polys + pc->polySizes[ptype]);
	}
}

void PlotBuildingModel(MODEL* model, int rot, _pct* pc)
{
	int opz, Z, r;
	PL_POLYFT4* polys;
	int i;
	u_char ptype;
	POLY_FT4* prims;
	SVECTOR* srcVerts;
	int combo;

	srcVerts = GET_MODEL_DATA(SVECTOR, model, vertices);
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);

	combo = combointensity;

	ConvertPolygonTypes(model, pc);

	r = (rot >> 3) * 4;

	i = model->num_polys;
	while (i-- > 0)
	{
		ptype = polys->id & 31;

		// skip certain polygons
		// texture page 0xFF is an untextured Driver 1 polygon
		if ((ptype != 11 && ptype != 21 && ptype != 23) || polys->texture_set == 0xFF)
		{
			polys = (PL_POLYFT4*)((char*)polys + pc->polySizes[ptype]);
			continue;
		}

		// perform transform
		gte_ldv3(&srcVerts[polys->v0], &srcVerts[polys->v1], &srcVerts[polys->v3]);
		gte_rtpt();

		// get culling value
		gte_nclip();
		gte_stopz(&opz);

		if (ptype == 21)
		{
			pc->colour = combo & 0x2ffffff | 0x2c000000;
		}
		else
		{
			pc->colour = pc->f4colourTable[r - polys->th & 31];
		}

		if (opz > 0)
		{
			pc->tpage = (*pc->ptexture_pages)[polys->texture_set];
			pc->clut = (*pc->ptexture_cluts)[polys->texture_set][polys->texture_id];

			prims = (POLY_FT4*)pc->primptr;

			setPolyFT4(prims);
			*(u_int*)&prims->r0 = pc->colour;

			// retrieve first three verts
			gte_stsxy3(&prims->x0, &prims->x1, &prims->x2);

			// translate 4th vert and get OT Z value
			gte_ldv0(&srcVerts[polys->v2]);
			gte_rtps();
			gte_avsz4();

			gte_stotz(&Z);

			gte_stsxy(&prims->x3);

			prims->tpage = pc->tpage;
			prims->clut = pc->clut;

			*(ushort*)&prims->u0 = *(ushort*)&polys->uv0;
			*(ushort*)&prims->u1 = *(ushort*)&polys->uv1;
			*(ushort*)&prims->u2 = *(ushort*)&polys->uv3;
			*(ushort*)&prims->u3 = *(ushort*)&polys->uv2;

			addPrim(pc->ot + (Z >> 1), prims);

			pc->primptr += sizeof(POLY_FT4);
		}

		polys = (PL_POLYFT4*)((char*)polys + pc->polySizes[ptype]);
	}
}

// [D] [T] [A] custom
void PlotBuildingModelSubdivNxN(MODEL* model, int rot, _pct* pc, int n)
{
	int opz, Z, r;
	int diff, minZ, maxZ;
	PL_POLYFT4* polys;
	int i;
	u_char ptype;
	POLY_FT4* prims;
	SVECTOR* srcVerts;
	int combo;

#if 0//def PSX
	MVERTEX5x5& subdiVerts = *(MVERTEX5x5*)(u_char*)getScratchAddr(0);
#else
	MVERTEX5x5 subdiVerts;
#endif

	srcVerts = GET_MODEL_DATA(SVECTOR, model, vertices);
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);

	combo = combointensity;

	ConvertPolygonTypes(model, pc);

	r = (rot >> 3) * 4;

	i = model->num_polys;
	while (i-- > 0)
	{
		// iterate through polygons
		// with skipping
		ptype = polys->id & 31;

		// texture page 0xFF is an untextured Driver 1 polygon
		if ((ptype != 11 && ptype != 21 && ptype != 23) || polys->texture_set == 0xFF)
		{
			polys = (PL_POLYFT4*)((char*)polys + pc->polySizes[ptype]);
			continue;
		}

		// perform transform
		gte_ldv3(&srcVerts[polys->v0], &srcVerts[polys->v1], &srcVerts[polys->v3]);
		gte_rtpt();

		// get culling value
		gte_nclip();
		gte_stopz(&opz);

		if (ptype == 21)
		{
			pc->colour = combo & 0x2ffffffU | 0x2c000000;
		}
		else
		{
			pc->colour = pc->f4colourTable[r - polys->th & 31];
		}

		if (opz > 0)
		{
			gte_stsz3(&pc->scribble[0], &pc->scribble[1], &pc->scribble[2]);

			pc->tpage = (*pc->ptexture_pages)[polys->texture_set];
			pc->clut = (*pc->ptexture_cluts)[polys->texture_set][polys->texture_id];

			minZ = pc->scribble[2];
			if (pc->scribble[1] < minZ)
				minZ = pc->scribble[1];

			if (pc->scribble[0] < minZ)
				minZ = pc->scribble[0];

			maxZ = pc->scribble[2];
			if (maxZ < pc->scribble[1])
				maxZ = pc->scribble[1];

			diff = maxZ - minZ;
			if (maxZ < pc->scribble[0])
				diff = pc->scribble[0] - minZ;

			ushort uv0, uv1, uv2, uv3;

			// [A] special case
			if(ptype == 23)
			{
				POLYGT4* pgt4 = (POLYGT4*)polys;
				uv0 = *(ushort*)&pgt4->uv0;
				uv1 = *(ushort*)&pgt4->uv1;
				uv2 = *(ushort*)&pgt4->uv2;
				uv3 = *(ushort*)&pgt4->uv3;
			}
			else
			{
				uv0 = *(ushort*)&polys->uv0;
				uv1 = *(ushort*)&polys->uv1;
				uv2 = *(ushort*)&polys->uv2;
				uv3 = *(ushort*)&polys->uv3;
			}

			if (n == 0 || diff << 2 <= minZ - 350)
			{
				prims = (POLY_FT4*)pc->primptr;

				setPolyFT4(prims);
				*(u_int*)&prims->r0 = pc->colour;

				// retrieve first three verts
				gte_stsxy3(&prims->x0, &prims->x1, &prims->x2);

				// translate 4th vert and get OT Z value
				gte_ldv0(&srcVerts[polys->v2]);
				gte_rtps();
				gte_avsz4();

				gte_stotz(&Z);

				gte_stsxy(&prims->x3);

				prims->tpage = pc->tpage;
				prims->clut = pc->clut;

				*(ushort*)&prims->u0 = uv0;
				*(ushort*)&prims->u1 = uv1;
				*(ushort*)&prims->u2 = uv3;
				*(ushort*)&prims->u3 = uv2;

				addPrim(pc->ot + (Z >> 1), prims);

				pc->primptr += sizeof(POLY_FT4);
			}
			else
			{
				int sub;
				sub = n;
				if (n == 1)
				{
					if (minZ - 150 < (diff << 1))
						sub = 4;
					else
						sub = 2;
				}

				copyVector(&subdiVerts.verts[0][0], &srcVerts[polys->v0]);
				subdiVerts.verts[0][0].uv.val = uv0;

				copyVector(&subdiVerts.verts[0][1], &srcVerts[polys->v1]);
				subdiVerts.verts[0][1].uv.val = uv1;

				copyVector(&subdiVerts.verts[0][2], &srcVerts[polys->v3]);
				subdiVerts.verts[0][2].uv.val = uv3;

				copyVector(&subdiVerts.verts[0][3], &srcVerts[polys->v2]);
				subdiVerts.verts[0][3].uv.val = uv2;

				makeMesh((MVERTEX(*)[5][5])subdiVerts.verts, sub, sub);
				drawMesh((MVERTEX(*)[5][5])subdiVerts.verts, sub, sub, pc);
			}
		}

		polys = (PL_POLYFT4*)((char*)polys + pc->polySizes[ptype]);
	}
}


// [D] [T]
int DrawAllBuildings(CELL_OBJECT** objects, int num_buildings)
{
	int i, mat, prev_mat;
	int Z, zbias;
	int drawlimit;
	MODEL* model;
	OTTYPE* ot;
	CELL_OBJECT* cop;

	prev_mat = -1;
	
	for (i = 0; i < 8; i++)
	{
		plotContext.f4colourTable[i * 4 + 0] = plotContext.planeColours[i] | 0x2C000000;
		plotContext.f4colourTable[i * 4 + 1] = plotContext.planeColours[0] | 0x2C000000;
		plotContext.f4colourTable[i * 4 + 2] = plotContext.planeColours[5] | 0x2C000000;
		plotContext.f4colourTable[i * 4 + 3] = plotContext.planeColours[0] | 0x2C000000; // default: 0x2C00F0F0
	}

	plotContext.current = current;
	plotContext.ptexture_pages = &texture_pages;
	plotContext.ptexture_cluts = &texture_cluts;
	plotContext.polySizes = PolySizes;
	plotContext.flags = 0;
	plotContext.primptr = plotContext.current->primptr;

	ot = plotContext.current->ot + 8;

	while (num_buildings--)
	{
		cop = (CELL_OBJECT*)*objects;
		mat = cop->yang;

		if (prev_mat == mat)
		{
			Z = Apply_InvCameraMatrixSetTrans(&cop->pos);
		}
		else
		{
			Z = Apply_InvCameraMatrixAndSetMatrix(&cop->pos, &CompoundMatrix[prev_mat = mat]);
		}

		model = Z > DRAW_LOD_DIST_LOW ? pLodModels[cop->type] : modelpointers[cop->type];

		zbias = model->zBias - 64;

		if (zbias < 0)
			zbias = 0;

		plotContext.ot = ot + zbias * 4;

		// Driver 1 grass lies under the road tiles at almost the same depth: push it back to stop the Z-fighting
		if (IS_DRIVER1_CITY(GameLevel))
			PGXP_SetZOffsetScale(0.0f, (model->flags2 & 0x4000) ? 1.01f : 1.0f);

		if(Z <= DRAW_LOD_DIST_HIGH)
			PlotBuildingModelSubdivNxN(model, cop->yang, &plotContext, 1);
		else
			PlotBuildingModel(model, cop->yang, &plotContext);

		drawlimit = (int)(current->primptr - current->primtab);

		if (PRIMTAB_SIZE - drawlimit < 60000)
			break;
		objects++;
	}

	if (IS_DRIVER1_CITY(GameLevel))
		PGXP_SetZOffsetScale(0.0f, 1.0f);

	// advance primitive buffer
	current->primptr = plotContext.primptr;

	return 0;
}


// [D] [T] [A] custom
void PlotModelSubdivNxN(MODEL* model, int rot, _pct* pc, int n)
{
	int opz, Z, r, pr, nr;
	int diff, minZ, maxZ;
	PL_POLYFT4* polys;
	int i;
	u_char ptype;
	POLY_FT4* prims;
	SVECTOR* srcVerts;
	int combo;

#if 0//def PSX
	MVERTEX5x5& subdiVerts = *(MVERTEX5x5*)(u_char*)getScratchAddr(0);
#else
	MVERTEX5x5 subdiVerts;
#endif

	ConvertPolygonTypes(model, pc);

	srcVerts = GET_MODEL_DATA(SVECTOR, model, vertices);
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);

	combo = combointensity;

	pr = (rot >> 3) * 4;
	nr = ((rot + 32 & 63) >> 3) * 4;

	// transparent object flag
	if (pc->flags & PLOT_TRANSPARENT)
		combo |= 0x2000000;

	i = model->num_polys;
	while (i > 0)
	{
		// iterate through polygons
		// with skipping
		ptype = polys->id & 31;

		// texture page 0xFF is an untextured Driver 1 polygon
		if ((ptype != 11 && ptype != 21 && ptype != 23) || polys->texture_set == 0xFF)
		{
			polys = (PL_POLYFT4*)((char*)polys + pc->polySizes[ptype]);
			i--;
			continue;
		}

		// perform transform
		gte_ldv3(&srcVerts[polys->v0], &srcVerts[polys->v1], &srcVerts[polys->v3]);
		gte_rtpt();

		// get culling value
		gte_nclip();
		gte_stopz(&opz);

		r = pr;

		if (pc->flags & (PLOT_INV_CULL | PLOT_NO_CULL))
		{
			if (opz < 0)
				r = nr;

			if (pc->flags & PLOT_NO_CULL)
				opz = 1;		// no culling
			else // PLOT_FRONT_CULL
				opz = -opz;		// front face
		}

		if (ptype == 21 || (pc->flags & PLOT_NO_SHADE))
		{
			pc->colour = combo & 0x2ffffffU | 0x2c000000;
		}
		else
		{
			pc->colour = pc->f4colourTable[r - polys->th & 31];
		}

		if (opz > 0)
		{
			gte_stsz3(&pc->scribble[0], &pc->scribble[1], &pc->scribble[2]);

			pc->tpage = (*pc->ptexture_pages)[polys->texture_set];

			if ((pc->flags & PLOT_CUSTOM_PALETTE) == 0) // [A] custom palette flag - for pedestrian heads
				pc->clut = (*pc->ptexture_cluts)[polys->texture_set][polys->texture_id];

			minZ = pc->scribble[2];
			if (pc->scribble[1] < minZ)
				minZ = pc->scribble[1];

			if (pc->scribble[0] < minZ)
				minZ = pc->scribble[0];

			maxZ = pc->scribble[2];
			if (maxZ < pc->scribble[1])
				maxZ = pc->scribble[1];

			diff = maxZ - minZ;
			if (maxZ < pc->scribble[0])
				diff = pc->scribble[0] - minZ;

			ushort uv0, uv1, uv2, uv3;

			// [A] special case
			if (ptype == 23)
			{
				POLYGT4* pgt4 = (POLYGT4*)polys;
				uv0 = *(ushort*)&pgt4->uv0;
				uv1 = *(ushort*)&pgt4->uv1;
				uv2 = *(ushort*)&pgt4->uv2;
				uv3 = *(ushort*)&pgt4->uv3;
			}
			else
			{
				uv0 = *(ushort*)&polys->uv0;
				uv1 = *(ushort*)&polys->uv1;
				uv2 = *(ushort*)&polys->uv2;
				uv3 = *(ushort*)&polys->uv3;
			}

			if (n == 0 || diff << 2 <= minZ - 350)
			{
				prims = (POLY_FT4*)pc->primptr;

				setPolyFT4(prims);
				*(u_int*)&prims->r0 = pc->colour;

				// retrieve first three verts
				gte_stsxy3(&prims->x0, &prims->x1, &prims->x2);

				// translate 4th vert and get OT Z value
				gte_ldv0(&srcVerts[polys->v2]);
				gte_rtps();
				gte_avsz4();

				gte_stotz(&Z);

				gte_stsxy(&prims->x3);

				prims->tpage = pc->tpage;
				prims->clut = pc->clut;

				*(ushort*)&prims->u0 = uv0;
				*(ushort*)&prims->u1 = uv1;
				*(ushort*)&prims->u2 = uv3;
				*(ushort*)&prims->u3 = uv2;

				addPrim(pc->ot + (Z >> 1), prims);

				pc->primptr += sizeof(POLY_FT4);
			}
			else
			{
				r = n;
				if (n == 1)
				{
					if (minZ - 150 < (diff << 1))
						r = 4;
					else
						r = 2;
				}

				copyVector(&subdiVerts.verts[0][0], &srcVerts[polys->v0]);
				subdiVerts.verts[0][0].uv.val = uv0;

				copyVector(&subdiVerts.verts[0][1], &srcVerts[polys->v1]);
				subdiVerts.verts[0][1].uv.val = uv1;

				copyVector(&subdiVerts.verts[0][2], &srcVerts[polys->v3]);
				subdiVerts.verts[0][2].uv.val = uv3;

				copyVector(&subdiVerts.verts[0][3], &srcVerts[polys->v2]);
				subdiVerts.verts[0][3].uv.val = uv2;

				makeMesh((MVERTEX(*)[5][5])subdiVerts.verts, r, r);
				drawMesh((MVERTEX(*)[5][5])subdiVerts.verts, r, r, pc);
			}
		}

		polys = (PL_POLYFT4*)((char*)polys + pc->polySizes[ptype]);
		i--;
	}
}


// [D] [T]
int gRenderNoSnow = 0;		// 1 = RenderModel draws without weather snow

void RenderModel(MODEL* model, MATRIX* matrix, VECTOR* pos, int zBias, int flags, int subdiv, int nrot)
{
	int i;

	if (matrix != NULL)
	{
		MATRIX comb;
		gte_SetTransVector(pos);

		MulMatrix0(&inv_camera_matrix, matrix, &comb);
		SetRotMatrix(&comb);
	}

	zBias /= 8;
	zBias += (model->zBias - 64);

	plotContext.ot = current->ot;

	if (zBias > 0)
		plotContext.ot += (zBias * 4);

	for (i = 0; i < 8; i++)
	{
		plotContext.f4colourTable[i * 4 + 0] = plotContext.planeColours[i] | 0x2C000000;
		plotContext.f4colourTable[i * 4 + 1] = plotContext.planeColours[0] | 0x2C000000;
		plotContext.f4colourTable[i * 4 + 2] = plotContext.planeColours[5] | 0x2C000000;
		plotContext.f4colourTable[i * 4 + 3] = plotContext.planeColours[0] | 0x2C000000; // default: 0x2C00F0F0
	}

	plotContext.ptexture_pages = &texture_pages;
#ifndef PSX
	// Tanner's body: no weather snow on shoulders, hands and shoes (the renderer snows models near the road level)
	static u_short noSnowTexturePages[128];
	if (gRenderNoSnow)
	{
		for (i = 0; i < 128; i++)
			noSnowTexturePages[i] = NO_SNOW_TPAGE(texture_pages[i]);

		plotContext.ptexture_pages = &noSnowTexturePages;
	}
#endif
	plotContext.ptexture_cluts = &texture_cluts;
	plotContext.polySizes = PolySizes;
	plotContext.flags = flags;
	plotContext.current = current;

	plotContext.primptr = plotContext.current->primptr;

	if ((current->primtab - (current->primptr - PRIMTAB_SIZE)) > 56000)
	{
		PlotModelSubdivNxN(model, nrot, &plotContext, subdiv);
	}

	// advance primitive buffer
	current->primptr = plotContext.primptr;
}

#ifndef PSX
int gSnowTarget = 0;				// 1 = snow wanted
int gFogTarget = 0;					// 1 = fog wanted
static float gSnowAmount = 0.0f;	// snow layer 0 or 1, also drives the number of snowflakes

static float Approach(float value, float target, float step)
{
	// plain comparisons: MIN/MAX in this code base are integer helpers
	if (value < target)
		return (value + step < target) ? value + step : target;

	return (value - step > target) ? value - step : target;
}

int gDistanceFog = 1;				// config.ini [render] distanceFog: a haze that hides the end of the draw distance

// fog amount that turns the last ring of the walk to 3/4 fog colour (the shader falls off with the square of the distance),
// so far buildings fade in instead of popping up
static float DistanceFogAmount(void)
{
	float range = (float)((DrawRadiusOfCells(DrawDistanceCells()) + 1) * MAP_CELL_SIZE);

	return gDistanceFog ? 1.2f / (range * 0.000108f) : 0.0f;
}

// hands the weather state and the camera to the renderer, which does the actual shading (PsyX_render.cpp, applySnow/applyFog)
static void UpdateSnow(void)
{
	static float fog = 0.0f;
	float groundPos[3];
	float groundUp[3];
	float len;
	VECTOR probe, ground, up;
	float fogColor[3] = { 0.66f, 0.69f, 0.73f };

	static int lastSnowTarget = -1, lastFogTarget = -1;

	if (lastSnowTarget != gSnowTarget || lastFogTarget != gFogTarget)
	{
		lastSnowTarget = gSnowTarget;
		lastFogTarget = gFogTarget;
		PsyX_Log_Info("Weather: snow %d, fog %d\n", gSnowTarget, gFogTarget);
	}

	// everything turns white at once, and clear again at once; falling snow brings a light haze of its own
	gSnowAmount = (float)gSnowTarget;
	fog = Approach(fog, gFogTarget ? 1.0f : gSnowTarget ? 0.35f : 0.0f, 1.0f / 120.0f);

	if (gTimeOfDay == TIME_NIGHT)
	{
		fogColor[0] = 0.06f; fogColor[1] = 0.07f; fogColor[2] = 0.10f;
	}
	else if (gTimeOfDay == TIME_DAWN || gTimeOfDay == TIME_DUSK)
	{
		fogColor[0] = 0.58f; fogColor[1] = 0.50f; fogColor[2] = 0.48f;
	}

	// weather fog is exponential (95% at 18000 units in a D2 city): the old squared falloff left near trees and cars
	// crisp in front of a wall of fog
	PsyX_SetFog(DistanceFogAmount(), fog * 3.0f / (18000.0f * MAP_CELL_SIZE / 2048.0f), fogColor);

	// The road level: the ground point under the camera and the way up, both in view space like the vertices the shader
	// gets. Only faces that lie there and face up get snow, so roofs and walls stay clear.
	probe.vx = camera_position.vx;
	probe.vy = -camera_position.vy;
	probe.vz = camera_position.vz;

	ground.vx = 0;
	ground.vy = -camera_position.vy - MapHeight(&probe);
	ground.vz = 0;
	Apply_Inv_CameraMatrix(&ground);

	up.vx = 0;
	up.vy = -4096;
	up.vz = 0;
	Apply_Inv_CameraMatrix(&up);

	len = sqrtf((float)up.vx * up.vx + (float)up.vy * up.vy + (float)up.vz * up.vz);

	if (len < 1.0f)
		len = 1.0f;

	groundPos[0] = (float)ground.vx; groundPos[1] = (float)ground.vy; groundPos[2] = (float)ground.vz;
	groundUp[0] = up.vx / len; groundUp[1] = up.vy / len; groundUp[2] = up.vz / len;

	PsyX_SetSnow(gSnowAmount, groundPos, groundUp);
}

// How much a front lamp of the car still burns (0..1), the same limits as AddNightLights: wrecked from damage 1000, the second lamp
// of a double lamp pair is gone from 500
static float HeadlampStrength(CAR_DATA* cp, int side)
{
	int damage = cp->ap.damage[side];

	if (damage >= 1000)
		return 0.0f;

	return (damage < 500 || !(cp->ap.carCos->extraInfo & 0x8000)) ? 1.0f : 0.5f;
}

// Headlight cone of the player car for the renderer (PsyX_render.cpp beamLight). It burns at night, in fog and in rain, with
// the lamps that are left: it starts at the better lamp, half the cone with one of two lamps gone, none with both gone.
static void UpdateHeadlights(void)
{
	static float beam = 0.0f;
	float pos[3] = { 0.0f, 0.0f, 0.0f };
	float dir[3] = { 0.0f, 0.0f, 1.0f };
	CAR_DATA* cp = NULL;
	float wanted = 0.0f;
	int found = 0;

	if (MainPlayer.playerType == PLAYER_TYPE_CAR)
		cp = &car_data[MainPlayer.playerCarId];

	// the cone also follows the car while it fades out: without a car it would shine from the camera
	if (cp && cp->ap.carCos)
	{
		SVECTOR* hl = &cp->ap.carCos->headLight;
		SVECTOR* cog = &cp->ap.carCos->cog;
		MATRIX* m = &cp->hd.drawCarMat;
		float lampLeft = HeadlampStrength(cp, 0), lampRight = HeadlampStrength(cp, 1);
		int side = lampRight > lampLeft;
		int lx = side ? cog->vx - hl->vx : hl->vx + cog->vx;
		int ly = hl->vy + cog->vy;
		int lz = hl->vz + cog->vz + (cp->ap.damage[side] >> 6);
		VECTOR p, d;
		float len;

		// car space to camera relative world space (as InitFXPos), then to view space
		p.vx = FIXED(m->m[0][0] * lx + m->m[0][1] * ly + m->m[0][2] * lz) + cp->hd.where.t[0] - camera_position.vx;
		p.vy = FIXED(m->m[1][0] * lx + m->m[1][1] * ly + m->m[1][2] * lz) - camera_position.vy - cp->hd.where.t[1];
		p.vz = FIXED(m->m[2][0] * lx + m->m[2][1] * ly + m->m[2][2] * lz) + cp->hd.where.t[2] - camera_position.vz;
		Apply_Inv_CameraMatrix(&p);

		// forward (the car front is -z in car space), tilted slightly down towards the road (y points down here)
		d.vx = -m->m[0][2];
		d.vy = -m->m[1][2] + 500;
		d.vz = -m->m[2][2];
		Apply_Inv_CameraMatrix(&d);

		len = sqrtf((float)d.vx * d.vx + (float)d.vy * d.vy + (float)d.vz * d.vz);

		if (len > 1.0f)
		{
			pos[0] = (float)p.vx; pos[1] = (float)p.vy; pos[2] = (float)p.vz;
			dir[0] = d.vx / len; dir[1] = d.vy / len; dir[2] = d.vz / len;
			found = 1;
			wanted = CarLightsOn(cp) ? (lampLeft + lampRight) * 0.5f : 0.0f;
		}
	}

	beam = found ? Approach(beam, wanted, 1.0f / 30.0f) : 0.0f;
	PsyX_SetBeam(beam * 1.05f, pos, dir);
}

int gAtmosphere = 1;		// config.ini [render] atmosphere: height fog that lies in the low streets, fog glows towards the sun

static void NormaliseView(VECTOR* v, float* out)
{
	float len = sqrtf((float)v->vx * v->vx + (float)v->vy * v->vy + (float)v->vz * v->vz);

	if (len < 1.0f)
		len = 1.0f;

	out[0] = v->vx / len; out[1] = v->vy / len; out[2] = v->vz / len;
}

// Height fog and sun glow for the renderer (PsyX_render.cpp applyFog). Own uniforms: the snow ground probe is not used.
static void UpdateAtmosphere(void)
{
	float worldUp[3], sunDir[3];
	float sunColor[3] = { 1.0f, 0.92f, 0.75f };
	float density = 0.0f, scatter = 0.0f;
	float cellScale = MAP_CELL_SIZE / 2048.0f;	// D1 cities are bigger, the fog layer too
	VECTOR up, sun;

	up.vx = 0;
	up.vy = -4096;		// world up is -y
	up.vz = 0;
	Apply_Inv_CameraMatrix(&up);
	NormaliseView(&up, worldUp);

	sun.vx = sun_position[BASE_CITY].vx;
	sun.vy = sun_position[BASE_CITY].vy;
	sun.vz = sun_position[BASE_CITY].vz;

	// the day night cycle moves the sun itself; 'low' is 1 for a sun at the horizon, 0 from 25 degrees up
	float low = DayCycleActive() ? 1.0f - SmoothStepF(5.0f, 25.0f, gSunElevation) : 0.0f;

	if (DayCycleActive())
	{
		sunColor[1] = 0.92f - 0.30f * low;
		sunColor[2] = 0.75f - 0.37f * low;
	}
	else if (gTimeOfDay == TIME_DAWN || gTimeOfDay == TIME_DUSK)
	{
		sun.vy /= 4;	// low sun, same direction
		sunColor[0] = 1.0f; sunColor[1] = 0.62f; sunColor[2] = 0.38f;
	}

	Apply_Inv_CameraMatrix(&sun);
	NormaliseView(&sun, sunDir);

	if (gAtmosphere)
	{
		density = 1.0f / 100000.0f;

		// morning and evening mist
		if (DayCycleActive())
			density *= 1.0f + 0.5f * low * SmoothStepF(-14.0f, -6.0f, gSunElevation);
		else if (gTimeOfDay == TIME_DAWN || gTimeOfDay == TIME_DUSK)
			density *= 1.5f;

		if (gWeather == WEATHER_RAIN || gSnowTarget)
			density *= 2.0f;

		if (gFogTarget)
			density *= 4.0f;

		// the sun lights the haze only when it is out
		if (gWeather != WEATHER_RAIN && !gSnowTarget && !gFogTarget)
		{
			if (DayCycleActive())
				scatter = (0.7f + 0.2f * low) * SmoothStepF(-3.0f, 2.0f, gSunElevation);
			else if (gTimeOfDay != TIME_NIGHT)
				scatter = (gTimeOfDay == TIME_DAY) ? 0.7f : 0.9f;
		}
	}

	PsyX_SetAtmosphere(1.0f / (3000.0f * cellScale), density / cellScale, worldUp, (float)-camera_position.vy, sunDir, sunColor, scatter);
}

#define SNOWFLAKES_MAX		700
#define SNOW_BOX_SIZE		6144	// flakes live in a box of this width/depth around the camera
#define SNOW_BOX_HEIGHT		3072
#define SNOW_FALL_SPEED		7		// about 1.3 m/s at 30 fps, real flakes drift down at 1 to 1.5

static u_int SnowHash(u_int n)
{
	n *= 0x9E3779B1u;
	n ^= n >> 15;
	n *= 0x85EBCA77u;
	n ^= n >> 13;
	return n;
}

// position of a flake relative to the camera: the box wraps around the camera, so the flakes stay put in the world
static int SnowWrap(int value, int size)
{
	value %= size;

	if (value < 0)
		value += size;

	return value - size / 2;
}

// Snowflakes are stateless: every flake has a fixed place in the box and only falls with the frame counter.
void DrawSnowFlakes(void)
{
	int i, count, size, z;
	u_int screenXY;
	u_char bright;
	VECTOR zero = { 0 };
	SVECTOR v;
	DVECTOR sxy;
	TILE* tile;

	count = (int)(gSnowAmount * SNOWFLAKES_MAX);

	if (count <= 0)
		return;

	bright = (gTimeOfDay == TIME_NIGHT) ? 170 : 235;

	gte_SetRotMatrix(&inv_camera_matrix);
	gte_SetTransVector(&zero);

	tile = (TILE*)current->primptr;

	for (i = 0; i < count; i++)
	{
		int hx = SnowHash(i * 3) % SNOW_BOX_SIZE;
		int hy = SnowHash(i * 3 + 1) % SNOW_BOX_HEIGHT;
		int hz = SnowHash(i * 3 + 2) % SNOW_BOX_SIZE;
		int t = CameraCnt;

		v.vx = SnowWrap(hx + (RSIN(t * 12 + hx) * 90 >> 12) - camera_position.vx, SNOW_BOX_SIZE);
		v.vy = SnowWrap(hy + t * SNOW_FALL_SPEED - camera_position.vy, SNOW_BOX_HEIGHT);
		v.vz = SnowWrap(hz + (RCOS(t * 10 + hz) * 90 >> 12) - camera_position.vz, SNOW_BOX_SIZE);

		gte_ldv0(&v);
		gte_rtps();
		gte_stsz(&z);

		// gte_stsxy hands out PGXP float bits, we need the plain screen position
		screenXY = MFC2(14);
		sxy.vx = (short)screenXY;
		sxy.vy = (short)(screenXY >> 16);

		if (z < 150 || z > 3200 || sxy.vx < -100 || sxy.vx > 420 || sxy.vy < -50 || sxy.vy > 257)
			continue;

		size = 1 + 600 / z;

		setTile(tile);
		tile->r0 = tile->g0 = tile->b0 = bright;
		tile->x0 = sxy.vx;
		tile->y0 = sxy.vy;
		tile->w = size;
		tile->h = size;

		addPrim(current->ot + (z >> 1), tile);
		tile++;
	}

	current->primptr = (char*)tile;
}
#endif

struct DrawMapData
{
	int rightPlane;
	int leftPlane;
	int backPlane;

	int farClipLimit;

	int current_object_computed_value;
	int cellLevel;

	int cellxpos, cellzpos;

	short backAng;
	short leftAng;
	short rightAng;

	short rightcos;
	short rightsin;
	short leftcos;
	short leftsin;
	short backcos;
	short backsin;

	u_short tiles_found;
	u_short other_models_found;
	u_short sprites_found;
	u_short anim_objs_found;
};

// [D] [T]
void DrawMapPSX(int* comp_val)
{
	CELL_OBJECT* cop;
	PACKED_CELL_OBJECT* ppco;
	MODEL* model;
	int dir;
	int distScale;
	int cellx, cellz;
	int hloop, vloop;

#ifdef PSX
	CELL_ITERATOR& ci = *(CELL_ITERATOR*)(u_char*)getScratchAddr(0);
	MATRIX& mRotStore = *(MATRIX*)((u_char*)getScratchAddr(0) + sizeof(CELL_ITERATOR));
	DrawMapData& drawData = *(DrawMapData*)((u_char*)getScratchAddr(0) + sizeof(CELL_ITERATOR) + sizeof(MATRIX));
	static_assert(sizeof(CELL_ITERATOR) + sizeof(MATRIX) + sizeof(DrawMapData) < 1024 - sizeof(_pct), "scratchpad overflow");
#else
	CELL_ITERATOR ci;
	MATRIX mRotStore;
	DrawMapData drawData;
#endif

	char* PVS_ptr;
	int i;
	int estPolys = 0;
	int fullRing = -1;

	gMapFarPolys = 0;

	static int treecount = 0;
	static int alleycount = 0;

	SetupDrawMapPSX();

#ifndef PSX
	UpdateSnow();
	UpdateHeadlights();
	UpdateAtmosphere();
#endif

	// clean cell cache
	ClearCopUsage();

	drawData.cellLevel = events.camera ? events.draw : -1;

	drawData.backPlane = 6144;
	drawData.rightPlane = -6144;
	drawData.leftPlane = 6144;

	// setup planes
	drawData.rightAng = camera_angle.vy - FrAng;
	drawData.leftAng = camera_angle.vy + FrAng;
	drawData.backAng = camera_angle.vy + 1024;

	drawData.rightcos = RCOS(drawData.rightAng);
	drawData.rightsin = RSIN(drawData.rightAng);

	drawData.leftcos = RCOS(drawData.leftAng);
	drawData.leftsin = RSIN(drawData.leftAng);

	drawData.backcos = RCOS(drawData.backAng);
	drawData.backsin = RSIN(drawData.backAng);

	extern int gFrustumOff;
	if (gFrustumOff)	// map editor, top-down: all planes stay open
	{
		drawData.rightcos = drawData.rightsin = drawData.leftcos = drawData.leftsin = drawData.backcos = drawData.backsin = 0;
	}

#ifdef PSX
	if (NumPlayers == 2)
		drawData.farClipLimit = farClip2Player;
	else
		drawData.farClipLimit = 60000;
#else
	drawData.farClipLimit = gDrawDistanceMode == 2 ? 6144 + 4096 * 66 : 125000;	// plane units: 4096 per cell
#endif

	drawData.tiles_found = 0;
	drawData.sprites_found = 0;
	drawData.current_object_computed_value = (*comp_val & 4095) | drawData.cellLevel << 16;
	drawData.other_models_found = 0;
	drawData.anim_objs_found = 0;
	
	drawData.cellzpos = current_cell_z;
	drawData.cellxpos = current_cell_x;

	PVS_ptr = CurrentPVS + 220;

	vloop = 0;
	hloop = 0;
	dir = 0;

	goFaster ^= fasterToggle;

	if (NumPlayers == 2)
		distScale = goFaster & 31 | 1;
	else
		distScale = goFaster & 31;

#ifndef PSX
	int drawCells = DrawDistanceCells();
#else
	int drawCells = gDrawDistance;
#endif

	i = (drawCells >> distScale);		// [A]

#ifndef PSX
	// square radius that fits drawCells cells; regions are 32 cells wide, so farther cells are never loaded
	int drawRadius = DrawRadiusOfCells(drawCells);
#endif

	// walk through all cells
	do
	{
#ifndef PSX
		// widest draw distance: the far object budget is used up, the rings beyond only hold farther objects
		if (fullRing >= 0 && MAX(ABS(hloop), ABS(vloop)) > fullRing)
			break;
#endif

#ifdef PSX
		if (ABS(hloop) + ABS(vloop) < PVS_CELL_COUNT)
#else
		if (ABS(hloop) <= drawRadius && ABS(vloop) <= drawRadius &&
			(ABS(hloop) + ABS(vloop) < PVS_CELL_COUNT || CellInLoadedArea(drawData.cellxpos + hloop, drawData.cellzpos + vloop)))
#endif
		{
			// clamped vis values
			int vis_h = MIN(MAX(hloop, -9), PVS_CELL_COUNT / 2);
			int vis_v = MIN(MAX(vloop, -9), PVS_CELL_COUNT / 2);
			int outsidePvs = 0;

#ifndef PSX
			// the PVS table only spans the nearest cells; farther ones (clamped to its edge) are decided by the frustum alone
			outsidePvs = hloop != vis_h || vloop != vis_v;
#endif

			cellx = drawData.cellxpos + hloop;
			cellz = drawData.cellzpos + vloop;

			if (drawData.rightPlane < 0 &&
				drawData.leftPlane > 0 &&
				drawData.backPlane < drawData.farClipLimit &&  // check planes
				cellx > -1 && cellx < cells_across &&							// check cell ranges
				cellz > -1 && cellz < cells_down &&
				(outsidePvs || gFrustumOff || PVS_ptr[vis_v * pvs_square + vis_h])) // check PVS table (the map editor sees every cell)
			{
				// walk each cell object in cell
				for (ppco = GetFirstPackedCop(cellx, cellz, &ci, 1, drawData.cellLevel); ppco; ppco = GetNextPackedCop(&ci))
				{
					int modelNum = (ppco->value >> 6) | ((ppco->pos).vy & 1) << 10;

					model = modelpointers[modelNum];

					if (FrustrumCheck16(ppco, model->bounding_sphere, &ci.nearCell) != -1 &&
						ObjectAccepted(model, modelNum, MAX(ABS(hloop), ABS(vloop)), &estPolys, &fullRing))
					{
						// sprity type
						if (model->shape_flags & SHAPE_FLAG_SPRITE)
						{
							if (drawData.sprites_found < MAX_DRAWN_SPRITES)
							{
#ifndef PSX
								spriteListNear[drawData.sprites_found] = ci.nearCell;
#endif
								spriteList[drawData.sprites_found++] = ppco;
							}

							if ((model->flags2 & MODEL_FLAG_ANIMOBJ) && drawData.anim_objs_found < MAX_DRAWN_ANIMATING)
							{
								cop = UnpackCellObject(ppco, &ci.nearCell);
								anim_obj_buffer[drawData.anim_objs_found++] = cop;
							}

							if (model->flags2 & MODEL_FLAG_TREE)
							{
								if (treecount == 0)
								{
									QuickUnpackCellObject(ppco, &ci.nearCell, &ground_debris[groundDebrisIndex]);

									if (groundDebrisIndex < MAX_GROUND_DEBRIS - 1)
										groundDebrisIndex++;
									else
										groundDebrisIndex = 0;
								}

								if (treecount < 15)
									treecount++;
								else
									treecount = 0;
							}
						}
						else
						{
							int yang;
							MATRIX2* cmat;

							yang = ppco->value & 63;
							cmat = &CompoundMatrix[yang];

							if (cmat->computed != drawData.current_object_computed_value)
							{
								cmat->computed = drawData.current_object_computed_value;
								if (yang > 0)
									MulMatrix0(&inv_camera_matrix, (MATRIX*)&matrixtable[yang], (MATRIX*)cmat);
								else
									*(MATRIX*)cmat = inv_camera_matrix;
							}

							if ((model->shape_flags & (SHAPE_FLAG_WATER | SHAPE_FLAG_TILE)) || 
								(model->flags2 & (MODEL_FLAG_PATH | MODEL_FLAG_GRASS)))
							{
								if (model->flags2 & MODEL_FLAG_ALLEY)
								{
									alleycount++;

									if (alleycount == 13)
									{
										QuickUnpackCellObject(ppco, &ci.nearCell, &ground_debris[groundDebrisIndex]);

										if (groundDebrisIndex < MAX_GROUND_DEBRIS - 1)
											groundDebrisIndex++;
										else
											groundDebrisIndex = 0;

										alleycount = 0;
									}
								}

								if (drawData.tiles_found < MAX_DRAWN_TILES)
								{
#ifndef PSX
									model_tile_near[drawData.tiles_found] = ci.nearCell;
#endif
									model_tile_ptrs[drawData.tiles_found++] = ppco;
								}
							}
							else
							{
								// [A] objects over the limit are not unpacked: they would only use up ring buffer slots of the near ones
								if (drawData.other_models_found < MAX_DRAWN_BUILDINGS)
								{
									cop = UnpackCellObject(ppco, &ci.nearCell);
									model_object_ptrs[drawData.other_models_found++] = cop;

									if (drawData.anim_objs_found < MAX_DRAWN_ANIMATING && (model->flags2 & MODEL_FLAG_ANIMOBJ))
										anim_obj_buffer[drawData.anim_objs_found++] = cop;
								}
							}
						}
					}
				}
			}
		}

		if (dir == 0)
		{
			drawData.leftPlane += drawData.leftcos;
			drawData.backPlane += drawData.backcos;
			drawData.rightPlane += drawData.rightcos;
			dir = (++hloop + vloop == 1) ? 1 : dir;
		}
		else if (dir == 1)
		{
			drawData.leftPlane += drawData.leftsin;
			drawData.backPlane += drawData.backsin;
			drawData.rightPlane += drawData.rightsin;
			//PVS_ptr += pvs_square;
			dir = (hloop == ++vloop) ? 2 : dir;
		}
		else if (dir == 2)
		{
			drawData.leftPlane -= drawData.leftcos;
			drawData.backPlane -= drawData.backcos;
			drawData.rightPlane -= drawData.rightcos;
			dir = (--hloop + vloop == 0) ? 3 : dir;
		}
		else
		{
			drawData.leftPlane -= drawData.leftsin;
			drawData.backPlane -= drawData.backsin;
			drawData.rightPlane -= drawData.rightsin;
			//PVS_ptr -= pvs_square;
			dir = (hloop == --vloop) ? 0 : dir;
		}
	}while (i-- > 0);

#ifndef PSX
	gDrawStats[0] = drawData.tiles_found;
	gDrawStats[1] = drawData.other_models_found;
	gDrawStats[2] = drawData.sprites_found;
	gDrawStats[3] = drawData.anim_objs_found;
#endif

#if 0
	char tempBuf[512];

	SetTextColour(255, 255, 0);

	sprintf(tempBuf, "Buildings: %d", drawData.other_models_found);
	PrintString(tempBuf, 10, 60);

	sprintf(tempBuf, "Sprites: %d", drawData.sprites_found);
	PrintString(tempBuf, 10, 75);

	sprintf(tempBuf, "Tiles: %d", drawData.tiles_found);
	PrintString(tempBuf, 10, 90);

	sprintf(tempBuf, "Anims: %d", drawData.anim_objs_found);
	PrintString(tempBuf, 10, 105);

	sprintf(tempBuf, "TOTAL: %d", drawData.other_models_found + drawData.sprites_found + drawData.tiles_found + drawData.anim_objs_found);
	PrintString(tempBuf, 10, 120);
#endif

	SetupPlaneColours(combointensity);

	if (drawData.anim_objs_found)
		DrawAllAnimatingObjects((CELL_OBJECT**)anim_obj_buffer, drawData.anim_objs_found);

	if (drawData.sprites_found)
		DrawSprites((PACKED_CELL_OBJECT**)spriteList, drawData.sprites_found);

	if (drawData.tiles_found)
		DrawTILES((PACKED_CELL_OBJECT**)model_tile_ptrs, drawData.tiles_found);

	if (drawData.other_models_found)
		DrawAllBuildings((CELL_OBJECT**)model_object_ptrs, drawData.other_models_found);

	setupYet = 0;

#ifndef PSX
	gMapPrimBytes = (int)(current->primptr - current->primtab);
	gMapEstPolys = estPolys;
#endif
}

#ifdef DYNAMIC_LIGHTING
struct DLIGHT
{
	SVECTOR position;
	CVECTOR color;
	SVECTOR dir;		// view space, length 4096: a headlight that lights what is in front of it; 0 = lamp, pad DLIGHT_SIREN
};

int gEnableDlights = 0;

int gNumDlights = 0;
DLIGHT gLights[MAX_DLIGHTS];

int gVolumeLights = 1;		// config.ini [render] volumeLights: lamps and headlights light the air at night, in fog and rain

// squared view space distance in units of the light's reach; a headlight counts from the middle of its beam, so a car
// lighting the player's car wins over cars lighting the road away from the camera
static float DlightRank(int x, int y, int z, int radius, SVECTOR* dir)
{
	float fx = x, fy = y, fz = z;
	float reach = MAX(radius * 0.48f, 2000.0f);

	if (dir && (dir->vx || dir->vy || dir->vz))
	{
		fx += dir->vx * (2000.0f / ONE);
		fy += dir->vy * (2000.0f / ONE);
		fz += dir->vz * (2000.0f / ONE);
		reach = 7000.0f;
	}

	return (fx * fx + fy * fy + fz * fz) / (reach * reach);
}

static float DlightRankOf(DLIGHT* l)
{
	return DlightRank(l->position.vx, l->position.vy, l->position.vz, l->position.pad, &l->dir);
}

void AddDlight(VECTOR* position, CVECTOR* color, int radius, SVECTOR* dir)
{
	DLIGHT* pLight;
	VECTOR lightPos;

	// positions are stored in 16 bit: a far light would wrap to a wrong place (off: the list stays as it was)
	if (gVolumeLights && (ABS(position->vx) > 32767 || ABS(position->vy) > 32767 || ABS(position->vz) > 32767))
		return;

	if (gNumDlights + 1 >= MAX_DLIGHTS)
	{
		// full (many car lights): replace the farthest light when this one is nearer
		int i, farthest = 0;
		float farDist;

		if (!gVolumeLights)
			return;

		farDist = DlightRankOf(&gLights[0]);

		for (i = 1; i < gNumDlights; i++)
		{
			float d = DlightRankOf(&gLights[i]);

			if (d > farDist)
			{
				farDist = d;
				farthest = i;
			}
		}

		if (DlightRank(position->vx, position->vy, position->vz, ABS(radius), dir) >= farDist)
			return;

		pLight = &gLights[farthest];
	}
	else
		pLight = &gLights[gNumDlights++];

	lightPos = *position;
	VecCopy(&pLight->position, &lightPos);
	pLight->position.pad = ABS(radius);

	pLight->color = *color;

	if (dir)
		pLight->dir = *dir;
	else
		pLight->dir.vx = pLight->dir.vy = pLight->dir.vz = pLight->dir.pad = 0;
}

void GetDLightLevel(SVECTOR* position, u_int* inOutColor)
{
	DLIGHT* pLight;
	int dx, dy, dz, dist, light;
	u_int lightR, lightG, lightB;

	lightR = (*inOutColor & 255);
	lightG = (*inOutColor >> 8 & 255);
	lightB = (*inOutColor >> 16 & 255);

	for (int i = 0; i < gNumDlights; i++)
	{
		int radius;
		pLight = &gLights[i];

		dx = (int)position->vx - (int)pLight->position.vx;
		dy = (int)position->vy - (int)pLight->position.vy;
		dz = (int)position->vz - (int)pLight->position.vz;

		dist = SquareRoot0(dx * dx + dy * dy + dz * dz);

		radius = pLight->position.pad;

		if (dist > radius) {
			continue;
		}

		light = radius - dist;

		lightR += pLight->color.r * light >> 13;
		lightG += pLight->color.g * light >> 13;
		lightB += pLight->color.b * light >> 13;
	}

	*inOutColor = MIN(lightB, 255) << 16 | MIN(lightG, 255) << 8 | MIN(lightR, 255) | (*inOutColor & 0xFF000000);
}

#define VOLUME_LIGHT_MATCH_DIST	1500.0f	// a light of this frame is the same lamp as one of the last frame when this close (view space)
#define VOLUME_LIGHT_FADE_STEP	0.07f	// about half a second at 30 Hz

struct VOLUME_SLOT
{
	float pos[3], col[4], dir[3];
	float fade;
	int used;
};

static VOLUME_SLOT gVolumeSlots[PSYX_MAX_VOLUME_LIGHTS];

// A light that enters the nearest set fades in, one that leaves fades out where it was, so lamps and headlights never
// switch on or off in one frame. n = lights of this frame in pos/col/dir (-1: none, reset); returns the count to draw.
static int SmoothVolumeLights(int n, float* pos, float* col, float* dir)
{
	char matched[PSYX_MAX_VOLUME_LIGHTS] = { 0 };
	int i, j, count = 0;

	if (n < 0)
	{
		memset(gVolumeSlots, 0, sizeof(gVolumeSlots));
		return 0;
	}

	for (i = 0; i < n; i++)
	{
		int best = -1;
		float bestDist = VOLUME_LIGHT_MATCH_DIST * VOLUME_LIGHT_MATCH_DIST;

		for (j = 0; j < PSYX_MAX_VOLUME_LIGHTS; j++)
		{
			float dx, dy, dz, d;

			if (!gVolumeSlots[j].used || matched[j])
				continue;

			dx = gVolumeSlots[j].pos[0] - pos[i * 3 + 0];
			dy = gVolumeSlots[j].pos[1] - pos[i * 3 + 1];
			dz = gVolumeSlots[j].pos[2] - pos[i * 3 + 2];
			d = dx * dx + dy * dy + dz * dz;

			if (d < bestDist)
			{
				bestDist = d;
				best = j;
			}
		}

		if (best < 0)
		{
			// a new light takes a free slot, else the dimmest one that is fading out
			float lowest = 2.0f;

			for (j = 0; j < PSYX_MAX_VOLUME_LIGHTS; j++)
			{
				if (matched[j])
					continue;

				if (!gVolumeSlots[j].used)
				{
					best = j;
					break;
				}

				if (gVolumeSlots[j].fade < lowest)
				{
					lowest = gVolumeSlots[j].fade;
					best = j;
				}
			}

			if (best < 0)
				continue;

			memset(&gVolumeSlots[best], 0, sizeof(VOLUME_SLOT));
			gVolumeSlots[best].used = 1;
		}

		matched[best] = 1;
		memcpy(gVolumeSlots[best].pos, &pos[i * 3], sizeof(float) * 3);
		memcpy(gVolumeSlots[best].col, &col[i * 4], sizeof(float) * 4);
		memcpy(gVolumeSlots[best].dir, &dir[i * 3], sizeof(float) * 3);
		gVolumeSlots[best].fade = MIN(gVolumeSlots[best].fade + VOLUME_LIGHT_FADE_STEP, 1.0f);
	}

	for (j = 0; j < PSYX_MAX_VOLUME_LIGHTS; j++)
	{
		VOLUME_SLOT* s = &gVolumeSlots[j];

		if (!s->used)
			continue;

		if (!matched[j])
		{
			s->fade -= VOLUME_LIGHT_FADE_STEP;

			if (s->fade <= 0.0f)
			{
				s->used = 0;
				continue;
			}
		}

		memcpy(&pos[count * 3], s->pos, sizeof(float) * 3);
		memcpy(&dir[count * 3], s->dir, sizeof(float) * 3);
		col[count * 4 + 0] = s->col[0] * s->fade;
		col[count * 4 + 1] = s->col[1] * s->fade;
		col[count * 4 + 2] = s->col[2] * s->fade;
		col[count * 4 + 3] = s->col[3];
		count++;
	}

	return count;
}

// Hands the nearest lights of this frame to the renderer (PsyX_render.cpp volumeLight). Called before the list is cleared.
void UpdateVolumeLights(void)
{
	float pos[PSYX_MAX_VOLUME_LIGHTS * 3];
	float col[PSYX_MAX_VOLUME_LIGHTS * 4];
	float dir[PSYX_MAX_VOLUME_LIGHTS * 3];
	char used[MAX_DLIGHTS] = { 0 };
	float density = 0.0f;
	float gain = 4.0f / 255.0f;		// lamp colours are low (40 - 85)
	int i, n;

	if (gVolumeLights && (gTimeOfDay != TIME_DAY || DayCycleActive() || gFogTarget || gWeather == WEATHER_RAIN))
	{
		if (DayCycleActive())
			density = 1.0f - SmoothStepF(-12.0f, 12.0f, gSunElevation);
		else if (gTimeOfDay == TIME_NIGHT)
			density = 1.0f;
		else if (gTimeOfDay == TIME_DAWN || gTimeOfDay == TIME_DUSK)
			density = 0.5f;

		if (gWeather == WEATHER_RAIN && density < 1.5f)
			density = 1.5f;

		if (gFogTarget && density < 2.0f)
			density = 2.0f;
	}

	PsyX_SetVolumeBeam(density);

	if (density <= 0.0f)
	{
		SmoothVolumeLights(-1, pos, col, dir);
		PsyX_SetVolumeLights(0, pos, col, dir, 0.0f);
		return;
	}

	for (n = 0; n < PSYX_MAX_VOLUME_LIGHTS && n < gNumDlights; n++)
	{
		int best = -1;
		float bestDist = 0.0f;

		for (i = 0; i < gNumDlights; i++)
		{
			float d = DlightRankOf(&gLights[i]);

			if (!used[i] && (best == -1 || d < bestDist))
			{
				best = i;
				bestDist = d;
			}
		}

		used[best] = 1;

		pos[n * 3 + 0] = gLights[best].position.vx;
		pos[n * 3 + 1] = gLights[best].position.vy;
		pos[n * 3 + 2] = gLights[best].position.vz;

		{
			// headlights come in at up to 255: without the cap their halo was a white ball in fog
			float scale = gain;
			int peak = MAX(gLights[best].color.r, MAX(gLights[best].color.g, gLights[best].color.b));

			if (peak * gain > 1.0f)
				scale = 1.0f / peak;

			col[n * 4 + 0] = gLights[best].color.r * scale;
			col[n * 4 + 1] = gLights[best].color.g * scale;
			col[n * 4 + 2] = gLights[best].color.b * scale;
		}
		col[n * 4 + 3] = gLights[best].position.pad * 0.12f;	// halo: a street lamp lights 7000 units, its glow in the air is ~800

		dir[n * 3 + 0] = gLights[best].dir.vx / 4096.0f;
		dir[n * 3 + 1] = gLights[best].dir.vy / 4096.0f;
		dir[n * 3 + 2] = gLights[best].dir.vz / 4096.0f;

		// the shader tells a siren from a lamp by a short direction (a cone has length 1)
		if (gLights[best].dir.pad == DLIGHT_SIREN && !gLights[best].dir.vx && !gLights[best].dir.vy && !gLights[best].dir.vz)
			dir[n * 3 + 0] = 0.4f;
	}

	n = SmoothVolumeLights(n, pos, col, dir);

	PsyX_SetVolumeLights(n, pos, col, dir, density);
}

#endif // DYNAMIC_LIGHTING