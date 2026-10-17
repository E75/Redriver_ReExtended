#include "driver2.h"
#include "models.h"
#include "system.h"
#include "mission.h"
#include "draw.h"
#include "texture.h"
#include "tile.h"
#include "camera.h"
#include "dr2roads.h"
#include "overmap.h"

#include <stdlib.h>

#include "ASM/rndrasm.h"

#define TILE_UNDERSIDE_OFSE	133		// road tiles (grass and water are 229)
#define TILE_UNDERSIDE_GREY	88		// concrete, modulated by the tile colour like the lane markings (128 = unchanged)

// [A] Driver 1 road tiles are one sided: from below (boat under a causeway, flying car, road under a viaduct) the deck was see through.
// Draws the face that points away from the camera as flat concrete grey. Corners are v0, v1, v2, v3 of the tile polygon.
static void DrawTileUnderside(const SVECTOR* verts, int v0, int v1, int v2, int v3, int ofse)
{
	POLY_F4* prim = (POLY_F4*)plotContext.primptr;
	int z0, z1, z2, z3, Z, r, g, b;

	gte_ldv3(&verts[v0], &verts[v1], &verts[v3]);
	gte_rtpt();
	gte_stsz3(&z0, &z1, &z2);
	gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

	gte_ldv0(&verts[v2]);
	gte_rtps();
	gte_stsz(&z3);
	gte_stsxy(&prim->x3);

	// a corner behind the camera plane would stretch the quad over the screen
	if (z0 <= 5 || z1 <= 5 || z2 <= 5 || z3 <= 5)
		return;

	Z = (z0 + z1 + z2 + z3) >> 2;

	setPolyF4(prim);

	r = TILE_UNDERSIDE_GREY * (plotContext.colour & 255) >> 7;
	g = TILE_UNDERSIDE_GREY * (plotContext.colour >> 8 & 255) >> 7;
	b = TILE_UNDERSIDE_GREY * (plotContext.colour >> 16 & 255) >> 7;

	prim->r0 = MIN(r, 255);
	prim->g0 = MIN(g, 255);
	prim->b0 = MIN(b, 255);

	addPrim(plotContext.ot + (Z >> 1) + ofse, prim);
	plotContext.primptr += sizeof(POLY_F4);
}

#define COARSE_WATER_TEXELS	16		// water texture patches up to this many texels wide are too coarse (Los Angeles)
#define COARSE_WATER_INSET	3		// texels dropped at every edge of the patch: the black rim and its filtering

// [A] Driver 1 Los Angeles: the whole sea is one 14 x 14 texel patch (dark purple noise with a black rim) stretched over a 1590 unit tile,
// so it looks like gravel with a grid. Such a coarse water tile is drawn with the patch pulled in (no rim) and tinted blue.
static int CoarseWater(const MODEL* model)
{
	const POLYFT4* p;
	int du, dv;

	if (!IS_DRIVER1_CITY(GameLevel) || !(model->shape_flags & SHAPE_FLAG_WATER) || model->num_polys < 1)
		return 0;

	p = GET_MODEL_DATA(POLYFT4, model, poly_block);
	du = MAX(MAX(p->uv0.u, p->uv1.u), MAX(p->uv2.u, p->uv3.u)) - MIN(MIN(p->uv0.u, p->uv1.u), MIN(p->uv2.u, p->uv3.u));
	dv = MAX(MAX(p->uv0.v, p->uv1.v), MAX(p->uv2.v, p->uv3.v)) - MIN(MIN(p->uv0.v, p->uv1.v), MIN(p->uv2.v, p->uv3.v));

	return du <= COARSE_WATER_TEXELS && dv <= COARSE_WATER_TEXELS;
}

static u_int CoarseWaterTint(u_int colour)
{
	u_int r = (colour & 255) * 5 / 8, g = (colour >> 8 & 255) * 7 / 8, b = MIN((colour >> 16 & 255) * 3 / 2, 255);

	return (colour & 0xFF000000) | b << 16 | g << 8 | r;
}

static int sCoarseFlip = 0;		// bit 0: mirror u, bit 1: mirror v (see CoarseWaterFlip)

// Neighbouring water tiles are mirrored, so the dark edges of the patch meet edge to edge instead of drawing a grid.
// The tile cell comes from the position DrawTILES (and the water backdrop) put in plotContext.scribble.
static int CoarseWaterFlip(MODEL* model)
{
	const POLYFT4* p = GET_MODEL_DATA(POLYFT4, model, poly_block);
	const SVECTOR* verts = GET_MODEL_DATA(SVECTOR, model, vertices);
	const int size = MAX(ABS(verts[p->v0].vx - verts[p->v1].vx) + ABS(verts[p->v0].vz - verts[p->v1].vz), 1);
	const int cellX = (int)floorf((float)plotContext.scribble[0] / size);
	const int cellZ = (int)floorf((float)plotContext.scribble[2] / size);

	return (cellX & 1) | (cellZ & 1) << 1;
}

// uv of a corner moved COARSE_WATER_INSET texels towards the middle of the patch; corners is uv0..uv3 of the polygon
static ushort CoarseWaterUV(const UV_INFO* corners, const UV_INFO* corner)
{
	int minU = 255, maxU = 0, minV = 255, maxV = 0, i;
	int u = corner->u, v = corner->v;

	for (i = 0; i < 4; i++)
	{
		minU = MIN(minU, corners[i].u);
		maxU = MAX(maxU, corners[i].u);
		minV = MIN(minV, corners[i].v);
		maxV = MAX(maxV, corners[i].v);
	}

	u += ((u == minU) - (u == maxU)) * COARSE_WATER_INSET;
	v += ((v == minV) - (v == maxV)) * COARSE_WATER_INSET;

	if (sCoarseFlip & 1)
		u = minU + maxU - u;

	if (sCoarseFlip & 2)
		v = minV + maxV - v;

	return (ushort)(u | v << 8);
}

static int sCoarseWater = 0;		// set by TileNxN for SubdivNxM

#ifdef DYNAMIC_LIGHTING
void Tile1x1Lit(MODEL* model)
{
	int opz, Z;
	int ofse;
	PL_POLYFT4* polys;
	int i;
	u_char ptype;
	POLY_GT4* prims;
	SVECTOR* srcVerts;

	srcVerts = GET_MODEL_DATA(SVECTOR, model, vertices);
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);

	const u_int normalColour = plotContext.colour;
	const int coarse = CoarseWater(model);

	if (coarse)
	{
		plotContext.colour = CoarseWaterTint(normalColour);
		sCoarseFlip = CoarseWaterFlip(model);
	}

	// grass should be under pavements and other things
	if ((model->shape_flags & SHAPE_FLAG_WATER) || (model->flags2 & MODEL_FLAG_GRASS))
		ofse = 229;
	else
		ofse = 133;

#if USE_PGXP
	PGXP_SetZOffsetScale(0.0f, ofse > 200 ? 1.005f : 0.995f);
#endif

	i = model->num_polys;
	while (i-- > 0)
	{
		// iterate through polygons
		// with skipping
		ptype = polys->id & 0x1f;

		// Driver 1 flat quads are only drawn in the near detail tiles
		if (ptype == 17)
		{
			polys = (PL_POLYFT4*)((char*)polys + plotContext.polySizes[ptype]);
			continue;
		}

		// perform transform
		gte_ldv3(&srcVerts[polys->v0], &srcVerts[polys->v1], &srcVerts[polys->v3]);
		gte_rtpt();

		// get culling value
		gte_nclip();
		gte_stopz(&opz);

		if (opz < 0 && ofse == TILE_UNDERSIDE_OFSE && IS_DRIVER1_CITY(GameLevel))
			DrawTileUnderside(srcVerts, polys->v0, polys->v1, polys->v2, polys->v3, ofse);

		if (opz > 0)
		{
			prims = (POLY_GT4*)plotContext.primptr;

			*(uint*)&prims->r0 = plotContext.colour;
			*(uint*)&prims->r1 = plotContext.colour;
			*(uint*)&prims->r2 = plotContext.colour;
			*(uint*)&prims->r3 = plotContext.colour;
			setPolyGT4(prims);

			// retrieve first three verts
			gte_stsxy3(&prims->x0, &prims->x1, &prims->x2);

			// translate 4th vert and get OT Z value
			gte_ldv0(&srcVerts[polys->v2]);
			gte_rtps();
			gte_avsz4();

			gte_stotz(&Z);

			gte_stsxy(&prims->x3);

			prims->tpage = (*plotContext.ptexture_pages)[polys->texture_set];
			prims->clut = (*plotContext.ptexture_cluts)[polys->texture_set][polys->texture_id];

			*(ushort*)&prims->u0 = coarse ? CoarseWaterUV(&polys->uv0, &polys->uv0) : *(ushort*)&polys->uv0;
			*(ushort*)&prims->u1 = coarse ? CoarseWaterUV(&polys->uv0, &polys->uv1) : *(ushort*)&polys->uv1;
			*(ushort*)&prims->u2 = coarse ? CoarseWaterUV(&polys->uv0, &polys->uv3) : *(ushort*)&polys->uv3;
			*(ushort*)&prims->u3 = coarse ? CoarseWaterUV(&polys->uv0, &polys->uv2) : *(ushort*)&polys->uv2;

			SVECTOR tmpPos;
			gte_ldv0(&srcVerts[polys->v0]);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prims->r0);

			gte_ldv0(&srcVerts[polys->v1]);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prims->r1);

			gte_ldv0(&srcVerts[polys->v3]);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prims->r2);

			gte_ldv0(&srcVerts[polys->v2]);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prims->r3);

			addPrim(plotContext.ot + (Z >> 1) + ofse, prims);

			plotContext.primptr += sizeof(POLY_GT4);
		}

		polys = (PL_POLYFT4*)((char*)polys + plotContext.polySizes[ptype]);
	}

#if USE_PGXP
	PGXP_SetZOffsetScale(0.0f, 1.0f);
#endif

	plotContext.colour = normalColour;

	// done
	plotContext.current->primptr = plotContext.primptr;
}
#endif

// [D] [T] [A]
void Tile1x1(MODEL *model)
{
	int opz, Z;
	int ofse;
	PL_POLYFT4* polys;
	int i;
	u_char ptype;
	POLY_FT4* prims;
	SVECTOR* srcVerts;

	srcVerts = GET_MODEL_DATA(SVECTOR, model, vertices);
	polys = GET_MODEL_DATA(PL_POLYFT4, model, poly_block);

	const u_int normalColour = plotContext.colour;
	const int coarse = CoarseWater(model);

	if (coarse)
	{
		plotContext.colour = CoarseWaterTint(normalColour);
		sCoarseFlip = CoarseWaterFlip(model);
	}

	// grass should be under pavements and other things
	if ((model->shape_flags & SHAPE_FLAG_WATER) || (model->flags2 & MODEL_FLAG_GRASS))
		ofse = 229;
	else
		ofse = 133;

#if USE_PGXP
	PGXP_SetZOffsetScale(0.0f, ofse > 200 ? 1.005f : 0.995f);
#endif

	i = model->num_polys;
	while (i-- > 0)
	{
		// iterate through polygons
		// with skipping
		ptype = polys->id & 0x1f;

		// Driver 1 flat quads are only drawn in the near detail tiles
		if (ptype == 17)
		{
			polys = (PL_POLYFT4*)((char*)polys + plotContext.polySizes[ptype]);
			continue;
		}

		// perform transform
		gte_ldv3(&srcVerts[polys->v0], &srcVerts[polys->v1], &srcVerts[polys->v3]);
		gte_rtpt();

		// get culling value
		gte_nclip();
		gte_stopz(&opz);

		if (opz < 0 && ofse == TILE_UNDERSIDE_OFSE && IS_DRIVER1_CITY(GameLevel))
			DrawTileUnderside(srcVerts, polys->v0, polys->v1, polys->v2, polys->v3, ofse);

		if (opz > 0)
		{
			prims = (POLY_FT4*)plotContext.primptr;

			*(uint*)&prims->r0 = plotContext.colour;
			setPolyFT4(prims);
			
			// retrieve first three verts
			gte_stsxy3(&prims->x0, &prims->x1, &prims->x2);

			// translate 4th vert and get OT Z value
			gte_ldv0(&srcVerts[polys->v2]);
			gte_rtps();
			gte_avsz4();

			gte_stotz(&Z);

			gte_stsxy(&prims->x3);

			prims->tpage = (*plotContext.ptexture_pages)[polys->texture_set];
			prims->clut = (*plotContext.ptexture_cluts)[polys->texture_set][polys->texture_id];

			*(ushort*)&prims->u0 = coarse ? CoarseWaterUV(&polys->uv0, &polys->uv0) : *(ushort*)&polys->uv0;
			*(ushort*)&prims->u1 = coarse ? CoarseWaterUV(&polys->uv0, &polys->uv1) : *(ushort*)&polys->uv1;
			*(ushort*)&prims->u2 = coarse ? CoarseWaterUV(&polys->uv0, &polys->uv3) : *(ushort*)&polys->uv3;
			*(ushort*)&prims->u3 = coarse ? CoarseWaterUV(&polys->uv0, &polys->uv2) : *(ushort*)&polys->uv2;

			addPrim(plotContext.ot + (Z >> 1) + ofse, prims);

			plotContext.primptr += sizeof(POLY_FT4);
		}

		polys = (PL_POLYFT4*)((char*)polys + plotContext.polySizes[ptype]);
	}

#if USE_PGXP
	PGXP_SetZOffsetScale(0.0f, 1.0f);
#endif

	plotContext.colour = normalColour;

	// done
	plotContext.current->primptr = plotContext.primptr;
}

// performs division by 3 or by 5 depending on t
inline int fst_div_lut_3_5(int x, int t)
{
	static int mul[] = { 171, 205 };
	return x * mul[t] >> (9 + t);
}

inline int fst_div_3(int x)
{
	return x * 171 >> 9;
}


// [D] [T]
void DrawTILES(PACKED_CELL_OBJECT** tiles, int tile_amount)
{
	MODEL* pModel;
	PACKED_CELL_OBJECT *ppco;
	PACKED_CELL_OBJECT** tilePointers;
	int previous_matrix, yang, dofse, Z;
	int model_number;

	if (gTimeOfDay > -1) 
	{
		int combo = combointensity;
		if (gTimeOfDay < TIME_NIGHT)
		{
			plotContext.colour = combo & 0xffffffU | 0x2C000000;
		}
		else if (gTimeOfDay == TIME_NIGHT)
		{
#ifdef DYNAMIC_LIGHTING
			int t;
			t = GameLevel == 2 && gEnableDlights == 1;
			plotContext.colour = fst_div_lut_3_5(combo >> 16 & 255, t) << 16 | fst_div_lut_3_5(combo >> 8 & 255, t) << 8 | fst_div_lut_3_5(combo & 255, t) | 0x2C000000U;
#else
			plotContext.colour = fst_div_3(combo >> 16 & 255) << 16 | fst_div_3(combo >> 8 & 255) << 8 | fst_div_3(combo & 255) | 0x2C000000U;
#endif
		}
	}

	previous_matrix = -1;

	if (gWeather - 1U < 2)
	{
		u_int col;
		col = plotContext.colour >> 2 & 63;
		plotContext.colour = col * 0x30000 | col * 0x300 | col * 3 | 0x2C000000;
	}

	plotContext.ot = current->ot;
	plotContext.current = current;
	plotContext.primptr = current->primptr;
#ifndef PSX
	// ground tiles carry the weather snow to the end of the view, also up a hill
	static u_short groundTexturePages[128];
	for (int i = 0; i < 128; i++)
		groundTexturePages[i] = GROUND_SNOW_TPAGE(texture_pages[i]);

	plotContext.ptexture_pages = &groundTexturePages;
#else
	plotContext.ptexture_pages = (ushort(*)[128])texture_pages;
#endif
	plotContext.ptexture_cluts = (ushort(*)[128][32])texture_cluts;
	plotContext.flags = 0;
	plotContext.polySizes = PolySizes;

	tilePointers = (PACKED_CELL_OBJECT **)tiles;

	while (tile_amount--)
	{
		ppco = *tilePointers++;

#ifdef PSX
		plotContext.scribble[0] = ppco->pos.vx;
		plotContext.scribble[2] = ppco->pos.vz;
#else
		{
			const XZPAIR& nearCell = model_tile_near[tilePointers - (PACKED_CELL_OBJECT**)tiles - 1];
			plotContext.scribble[0] = nearCell.x + (short)(ppco->pos.vx - nearCell.x);
			plotContext.scribble[2] = nearCell.z + (short)(ppco->pos.vz - nearCell.z);
		}
#endif
		plotContext.scribble[1] = (ppco->pos.vy << 0x10) >> 0x11;

		yang = ppco->value & 0x3f;
		model_number = (ppco->value >> 6) | (ppco->pos.vy & 1) << 10;

		if (previous_matrix == yang)
		{
			Z = Apply_InvCameraMatrixSetTrans((VECTOR_NOPAD *)plotContext.scribble);
		}
		else
		{
			Z = Apply_InvCameraMatrixAndSetMatrix((VECTOR_NOPAD *)plotContext.scribble, &CompoundMatrix[previous_matrix = yang]);
		}

		// [A] Driver 1: lane markings and zebra crossings are flat quads that only the near detail tile has, the far tile
		// shows them as grey texture. The near detail tile is used 3 times as far, without subdividing it
		if (Z <= (IS_DRIVER1_CITY(GameLevel) ? DRAW_LOD_DIST_HIGH * 3 : DRAW_LOD_DIST_HIGH))
		{
			if (Low2HighDetailTable[model_number] != 0xffff)
				model_number = Low2HighDetailTable[model_number];

			pModel = modelpointers[model_number];
#ifndef PSX
			plotContext.ptexture_pages = (pModel->shape_flags & SHAPE_FLAG_WATER) ? (ushort(*)[128])texture_pages : &groundTexturePages;
#endif

			if (Z < 2000)
				TileNxN(pModel, 4, 75);
			else if (Z <= DRAW_LOD_DIST_HIGH)
				TileNxN(pModel, 2, 35);
			else
				TileNxN(pModel, 1, 35);
		}
		else
		{
			pModel = Z > DRAW_LOD_DIST_LOW ? pLodModels[model_number] : modelpointers[model_number];
#ifndef PSX
			plotContext.ptexture_pages = (pModel->shape_flags & SHAPE_FLAG_WATER) ? (ushort(*)[128])texture_pages : &groundTexturePages;
#endif
			
#ifdef DYNAMIC_LIGHTING
			(gEnableDlights ? Tile1x1Lit : Tile1x1)(pModel);
#else
			Tile1x1(pModel);
#endif // DYNAMIC_LIGHTING
		}
	}
	current->primptr = plotContext.primptr;

#ifndef PSX
	plotContext.ptexture_pages = (ushort(*)[128])texture_pages;
	DrawWaterBackdrop();
	DrawGroundBackdrop();
#endif
}

#ifndef PSX
// [A] Driver 1 water backdrop (plans/ground-fill.md): where the overview map says water, a water tile is drawn a little
// below the sea level so an open hole in the sea shows water instead of the grey void. Uses the plotContext of DrawTILES.
#define WATER_BACKDROP_RANGE	36000	// units around the camera
#define WATER_BACKDROP_DEPTH	25		// below the real sea tiles (cell objects: negative y is up)
#define WATER_BACKDROP_BRIDGE_HEIGHT	250	// a closed water cell needs a road this high above the sea (a bridge deck, not a ramp)

// The water tile of the city: flat square with four vertices, the one closest to 3000 units (Miami has 1530, 3060 and 6120).
// Returns the model slot or -1, size is the tile width.
static int FindBackdropModel(int* size, int* centreX, int* centreZ)
{
	int i, k, best = -1, bestDiff = 1 << 30;

	for (i = 0; i < MAX_MODEL_SLOTS; i++)
	{
		MODEL* model = modelpointers[i];
		SVECTOR* verts;
		int minX = 32767, maxX = -32767, minY = 32767, maxY = -32767, minZ = 32767, maxZ = -32767;

		if (!model || !(model->shape_flags & SHAPE_FLAG_WATER) || model->num_vertices != 4 || model->num_polys < 1)
			continue;

		verts = GET_MODEL_DATA(SVECTOR, model, vertices);
		for (k = 0; k < 4; k++)
		{
			minX = MIN(minX, verts[k].vx);
			maxX = MAX(maxX, verts[k].vx);
			minY = MIN(minY, verts[k].vy);
			maxY = MAX(maxY, verts[k].vy);
			minZ = MIN(minZ, verts[k].vz);
			maxZ = MAX(maxZ, verts[k].vz);
		}

		if (maxX - minX < 1400 || maxX - minX > 6500 || ABS((maxX - minX) - (maxZ - minZ)) > 100 || maxY - minY > 60)
			continue;

		if (ABS((maxX - minX) - 3000) < bestDiff)
		{
			bestDiff = ABS((maxX - minX) - 3000);
			best = i;
			*size = maxX - minX;
			*centreX = (minX + maxX) / 2;
			*centreZ = (minZ + maxZ) / 2;
		}
	}

	return best;
}

// A city without a water model of its own (Chicago): the cell is one flat coloured quad. The camera matrix and translation are set like for Tile1x1.
// A corner behind the camera would stretch the quad into a wedge across the sky (seen at an open bridge): a quad that
// crosses the camera plane is split in four, down to 256 units, and the pieces still crossing it are skipped.
static int sQuadRGB[3] = { 60, 100, 135 };	// colour scale of the flat quad: water by default, DrawGroundBackdrop sets its own

static void DrawFlatWaterQuad(int x0, int z0, int size, int depth)
{
	static SVECTOR quad[4];
	POLY_F4* prim;
	int z, r, g, b, gz[4];

	quad[0].vx = x0;        quad[0].vz = z0;
	quad[1].vx = x0 + size; quad[1].vz = z0;
	quad[2].vx = x0 + size; quad[2].vz = z0 + size;
	quad[3].vx = x0;        quad[3].vz = z0 + size;

	gte_ldv3(&quad[0], &quad[1], &quad[3]);
	gte_rtpt();
	gte_stsz3(&gz[0], &gz[1], &gz[3]);

	gte_ldv0(&quad[2]);
	gte_rtps();
	gte_stsz(&gz[2]);

	if (MIN(MIN(gz[0], gz[1]), MIN(gz[2], gz[3])) <= 40)
	{
		if (depth < 3 && MAX(MAX(gz[0], gz[1]), MAX(gz[2], gz[3])) > 40)
		{
			const int half = size / 2;

			DrawFlatWaterQuad(x0, z0, half, depth + 1);
			DrawFlatWaterQuad(x0 + half, z0, half, depth + 1);
			DrawFlatWaterQuad(x0, z0 + half, half, depth + 1);
			DrawFlatWaterQuad(x0 + half, z0 + half, half, depth + 1);
		}

		return;
	}

	z = (gz[0] + gz[1] + gz[2] + gz[3]) / 4;

	// the 4th corner is projected last, so the screen positions of the first three are reloaded
	gte_ldv3(&quad[0], &quad[1], &quad[3]);
	gte_rtpt();

	prim = (POLY_F4*)plotContext.primptr;
	setPolyF4(prim);

	gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

	gte_ldv0(&quad[2]);
	gte_rtps();
	gte_stsxy(&prim->x3);

	r = sQuadRGB[0] * (plotContext.colour & 255) >> 7;
	g = sQuadRGB[1] * (plotContext.colour >> 8 & 255) >> 7;
	b = sQuadRGB[2] * (plotContext.colour >> 16 & 255) >> 7;

	prim->r0 = MIN(r, 255);
	prim->g0 = MIN(g, 255);
	prim->b0 = MIN(b, 255);

	addPrim(plotContext.ot + 229 + (z >> 1), prim);
	plotContext.primptr += sizeof(POLY_F4);
}

void DrawWaterBackdrop(void)
{
	static int sModel = -1, sLevel = -1;
	static int sCell, sCentreX, sCentreZ;
	static int sOff = -1, sRed = -1;
	const u_int normalColour = plotContext.colour;
	MODEL* pModel;
	int cx, cz, cx0, cz0, radius, previous;

	if (sOff < 0)
		sOff = getenv("REDRIVER2_NOBACKDROP") != NULL;	// A/B test switch

	// test switch: backdrop cells in a strong red, to see where they show through (2: above the sea tiles)
	if (sRed < 0)
		sRed = getenv("REDRIVER2_BACKDROPRED") ? (getenv("REDRIVER2_BACKDROPRED")[0] == '2' ? 2 : 1) : 0;

	if (sOff)
		return;

	pModel = (sLevel == GameLevel && sModel >= 0) ? modelpointers[sModel] : NULL;

	if (sLevel != GameLevel || (sModel >= 0 && (!pModel || !(pModel->shape_flags & SHAPE_FLAG_WATER) || pModel->num_vertices != 4)))
	{
		sLevel = GameLevel;
		sModel = FindBackdropModel(&sCell, &sCentreX, &sCentreZ);

		if (sModel >= 0)
		{
			printInfo("water backdrop: model %d, tile %d units\n", sModel, sCell);
			pModel = modelpointers[sModel];
		}
		else
		{
			printInfo("water backdrop: no water model, flat quads\n");
			sCell = 2048;
			sCentreX = sCentreZ = 0;
		}
	}

#if USE_PGXP
	if (sModel < 0)
		PGXP_SetZOffsetScale(0.0f, 1.005f);
#endif

	if (sRed)
		plotContext.colour = 0x2C0000FF;

	radius = WATER_BACKDROP_RANGE / sCell;
	cx0 = (camera_position.vx + units_across_halved) / sCell - radius;
	cz0 = (camera_position.vz + units_down_halved) / sCell - radius;
	previous = 0;

	for (cz = cz0; cz <= cz0 + radius * 2; cz++)
	{
		for (cx = cx0; cx <= cx0 + radius * 2; cx++)
		{
			int Z, water;

			water = WaterBackdropCell(cx, cz, sCell);
			if (!water)
				continue;

			// a closed cell (water on both sides) is the water under a bridge: on low ground it is a ramp, a quay or a grass strip
			if (water == 2)
			{
				VECTOR centre;
				centre.vx = -units_across_halved + cx * sCell + sCell / 2;
				centre.vy = 0;
				centre.vz = -units_down_halved + cz * sCell + sCell / 2;

				if (MapHeight(&centre) < WATER_BACKDROP_BRIDGE_HEIGHT)
					continue;
			}

			if (PRIMTAB_SIZE - (int)(plotContext.primptr - current->primtab) < 100000)
			{
				plotContext.colour = normalColour;
				plotContext.current->primptr = plotContext.primptr;
				return;
			}

			plotContext.scribble[0] = -units_across_halved + cx * sCell + sCell / 2 - sCentreX;
			plotContext.scribble[1] = sRed == 2 ? -WATER_BACKDROP_DEPTH * 2 : WATER_BACKDROP_DEPTH;
			plotContext.scribble[2] = -units_down_halved + cz * sCell + sCell / 2 - sCentreZ;

			if (previous)
				Z = Apply_InvCameraMatrixSetTrans((VECTOR_NOPAD*)plotContext.scribble);
			else
				Z = Apply_InvCameraMatrixAndSetMatrix((VECTOR_NOPAD*)plotContext.scribble, &CompoundMatrix[0]);

			previous = 1;

			if (Z > -sCell)	// not behind the camera
			{
				if (sModel >= 0)
					Tile1x1(pModel);
				else
					DrawFlatWaterQuad(-sCell / 2, -sCell / 2, sCell, 0);
			}
		}
	}

	if (sModel < 0)
	{
#if USE_PGXP
		PGXP_SetZOffsetScale(0.0f, 1.0f);
#endif
		plotContext.current->primptr = plotContext.primptr;
	}

	plotContext.colour = normalColour;
}
#endif

// [A] custom implemented function
void makeMesh(MVERTEX(*VSP)[5][5], int m, int n)
{
	/* vertices by index in quad:
		3---------2
		|         |
		|         |
		|         |
		0---------1
	*/

	// 0 3 2 1

	// do subdivision
	MVERTEX v1, v2, v3, v4;
	MVERTEX e1, e2, e3, e4, e5;
	MVERTEX p1, p2, p3, p4, p5;

	if (n < 2)
		return;

#if 0
	// no need to subdivide!
	if (g_pgxpZBuffer)
		return;
#endif

	v1 = (*VSP)[0][0];
	v2 = (*VSP)[0][1];
	v3 = (*VSP)[0][3];
	v4 = (*VSP)[0][2];

	VecSubtract(&e1, &v2, &v1); // plane[1] - plane[0];
	e1.uv.s.u0 = (v2.uv.s.u0 - v1.uv.s.u0) >> 1;
	e1.uv.s.v0 = (v2.uv.s.v0 - v1.uv.s.v0) >> 1;

	VecSubtract(&e2, &v3, &v4); // plane[2] - plane[3];
	e2.uv.s.u0 = (v3.uv.s.u0 - v4.uv.s.u0) >> 1;
	e2.uv.s.v0 = (v3.uv.s.v0 - v4.uv.s.v0) >> 1;

	VecSubtract(&e3, &v4, &v1); // plane[3] - plane[0];
	e3.uv.s.u0 = (v4.uv.s.u0 - v1.uv.s.u0) >> 1;
	e3.uv.s.v0 = (v4.uv.s.v0 - v1.uv.s.v0) >> 1;

	VecSubtract(&e4, &v3, &v2); // plane[2] - plane[1];
	e4.uv.s.u0 = (v3.uv.s.u0 - v2.uv.s.u0) >> 1;
	e4.uv.s.v0 = (v3.uv.s.v0 - v2.uv.s.v0) >> 1;

	//-----------

	// half them all
	SetVec(&e1, e1.vx >> 1, e1.vy >> 1, e1.vz >> 1);

	SetVec(&e2, e2.vx >> 1, e2.vy >> 1, e2.vz >> 1);

	SetVec(&e3, e3.vx >> 1, e3.vy >> 1, e3.vz >> 1);

	SetVec(&e4, e4.vx >> 1, e4.vy >> 1, e4.vz >> 1);

	//-----------

	VecAdd(&p1, &e1, &v1); // e1 * 0.5f + plane[0];
	p1.uv.s.u0 = e1.uv.s.u0 + v1.uv.s.u0;
	p1.uv.s.v0 = e1.uv.s.v0 + v1.uv.s.v0;

	VecAdd(&p2, &e2, &v4); // e2 * 0.5f + plane[3];
	p2.uv.s.u0 = e2.uv.s.u0 + v4.uv.s.u0;
	p2.uv.s.v0 = e2.uv.s.v0 + v4.uv.s.v0;

	VecAdd(&p3, &e3, &v1); // e3 * 0.5f + plane[0];
	p3.uv.s.u0 = e3.uv.s.u0 + v1.uv.s.u0;
	p3.uv.s.v0 = e3.uv.s.v0 + v1.uv.s.v0;

	VecAdd(&p4, &e4, &v2); // e4 * 0.5f + plane[1];
	p4.uv.s.u0 = e4.uv.s.u0 + v2.uv.s.u0;
	p4.uv.s.v0 = e4.uv.s.v0 + v2.uv.s.v0;

	//-----------

	VecSubtract(&e5, &p2, &p1); // p2 - p1;
	e5.uv.s.u0 = (p2.uv.s.u0 - p1.uv.s.u0) >> 1;
	e5.uv.s.v0 = (p2.uv.s.v0 - p1.uv.s.v0) >> 1;

	SetVec(&e5, e5.vx >> 1, e5.vy >> 1, e5.vz >> 1);


	VecAdd(&p5, &e5, &p1); // e5 * 0.5f + p1;
	p5.uv.s.u0 = e5.uv.s.u0 + p1.uv.s.u0;
	p5.uv.s.v0 = e5.uv.s.v0 + p1.uv.s.v0;

	//-----------

	(*VSP)[0][0] = v1; // { plane[0], p1, p5, p3 },
	(*VSP)[0][1] = p1;
	(*VSP)[0][2] = p3;
	(*VSP)[0][3] = p5;

	(*VSP)[1][0] = p1; // { p1, plane[1], p4, p5 },
	(*VSP)[1][1] = v2;
	(*VSP)[1][2] = p5;
	(*VSP)[1][3] = p4;

	(*VSP)[2][0] = p5; // { p5, p4, plane[2], p2 },
	(*VSP)[2][1] = p4;
	(*VSP)[2][2] = p2;
	(*VSP)[2][3] = v3;

	(*VSP)[3][0] = p3; // { p3, p5, p2, plane[3] }
	(*VSP)[3][1] = p5;
	(*VSP)[3][2] = v4;
	(*VSP)[3][3] = p2;
}


// [A] a polygon corner behind the camera plane stretches the polygon over the screen (dark beams from shadows of passing cars).
// Only for semi transparent meshes (shadows, 0x2E): ground and walls that cross the camera plane must stay, else holes at the screen edge
static int MeshIsShadow(_pct* pc)
{
	return (pc->colour >> 24) == 0x2E;
}

static int MeshCornersInFront(void)
{
	int z0, z1, z2;
	gte_stsz3(&z0, &z1, &z2);
	return z0 > 5 && z1 > 5 && z2 > 5;
}

static int MeshLastCornerBehind(void)
{
	int z;
	gte_stsz(&z);
	return z <= 5;
}

// GTE flag after a projection: a corner close to the camera overflowed the divide or the screen range. The clamped corner
// bent the polygon into a dark slab with a straight edge over the cars beyond it (low camera beside a car).
#define GTE_FLAG_SCREEN_CLAMP	((1 << 17) | (1 << 14) | (1 << 13))

// [A] custom implemented function
void drawMesh(MVERTEX(*VSP)[5][5], int m, int n, _pct *pc)
{
	POLY_FT4* prim;
	int z, opz;
	u_int flag;

	prim = (POLY_FT4*)pc->primptr;

	int numPolys = 4;

	if (n < 2)
		numPolys = 1;

#if 0
	// no need to subdivide!
	if (g_pgxpZBuffer)
		numPolys = 1;
#endif

	for (int index = 0; index < numPolys; index++)
	{
		setPolyFT4(prim);
		*(uint*)&prim->r0 = pc->colour; // FIXME: semiTransparency support

		// test
		gte_ldv3(&(*VSP)[index][0], &(*VSP)[index][1], &(*VSP)[index][2]);
		gte_rtpt();
		gte_stflg(&flag);
		gte_nclip();
		gte_stopz(&opz);

		gte_avsz3();
		
		gte_stotz(&z);

		if (pc->flags & (PLOT_NO_CULL | PLOT_INV_CULL))
		{
			if (pc->flags & PLOT_NO_CULL)
				opz = 1;		// no culling
			else // PLOT_FRONT_CULL
				opz = -opz;		// front face
		}

		if (opz > 0 && z > 5 && (!MeshIsShadow(pc) || MeshCornersInFront()))
		{
			int z0, z1, z2, z3;

			gte_stsz3(&z0, &z1, &z2);
			gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

			gte_ldv0(&(*VSP)[index][3]);
			gte_rtps();

			gte_stsxy(&prim->x3);

			if (MeshIsShadow(pc))
			{
				u_int flag3;
				gte_stflg(&flag3);

				if (MeshLastCornerBehind() || ((flag | flag3) & GTE_FLAG_SCREEN_CLAMP))
					continue;

				// [A] blended prims skip the depth test: a shadow cell reaching to a close low camera, sorted by its average
				// depth, drew over the cars standing on it. Its farthest corner sorts it behind them, still in front of the road.
				gte_stsz(&z3);
				z = z * 3 * MAX(MAX(z0, z1), MAX(z2, z3)) / (z0 + z1 + z2);
			}

			*(ushort*)&prim->u0 = (*VSP)[index][0].uv.val;
			*(ushort*)&prim->u1 = (*VSP)[index][1].uv.val;
			*(ushort*)&prim->u2 = (*VSP)[index][2].uv.val;
			*(ushort*)&prim->u3 = (*VSP)[index][3].uv.val;

			prim->clut = pc->clut;
			prim->tpage = pc->tpage;

			addPrim(pc->ot + (z >> 1), prim);

			prim++;
		}
	}

	pc->primptr = (char*)prim;
}

#ifdef DYNAMIC_LIGHTING
void drawMeshLit(MVERTEX(*VSP)[5][5], int m, int n, _pct* pc)
{
	POLY_GT4* prim;
	int z, opz;

	prim = (POLY_GT4*)pc->primptr;

	int numPolys = 4;

	if (n < 2)
		numPolys = 1;

#if 0
	// no need to subdivide!
	if (g_pgxpZBuffer)
		numPolys = 1;
#endif

	for (int index = 0; index < numPolys; index++)
	{
		setPolyGT4(prim);

		// test
		gte_ldv3(&(*VSP)[index][0], &(*VSP)[index][1], &(*VSP)[index][2]);
		gte_rtpt();
		gte_nclip();
		gte_stopz(&opz);

		gte_avsz3();

		gte_stotz(&z);

		if (pc->flags & (PLOT_NO_CULL | PLOT_INV_CULL))
		{
			if (pc->flags & PLOT_NO_CULL)
				opz = 1;		// no culling
			else // PLOT_FRONT_CULL
				opz = -opz;		// front face
		}

		if (opz > 0 && z > 5 && (!MeshIsShadow(pc) || MeshCornersInFront()))
		{
			gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

			gte_ldv0(&(*VSP)[index][3]);
			gte_rtps();

			gte_stsxy(&prim->x3);

			if (MeshIsShadow(pc) && MeshLastCornerBehind())
				continue;

			*(uint*)&prim->r0 = plotContext.colour;
			*(uint*)&prim->r1 = plotContext.colour;
			*(uint*)&prim->r2 = plotContext.colour;
			*(uint*)&prim->r3 = plotContext.colour;

			SVECTOR tmpPos;
			gte_ldv0(&(*VSP)[index][0]);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prim->r0);

			gte_ldv0(&(*VSP)[index][1]);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prim->r1);

			gte_ldv0(&(*VSP)[index][2]);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prim->r2);

			gte_ldv0(&(*VSP)[index][3]);
			gte_rtps();
			gte_stsv(&tmpPos);
			GetDLightLevel(&tmpPos, (u_int*)&prim->r3);

			setPolyGT4(prim);

			* (ushort*)&prim->u0 = (*VSP)[index][0].uv.val;
			*(ushort*)&prim->u1 = (*VSP)[index][1].uv.val;
			*(ushort*)&prim->u2 = (*VSP)[index][2].uv.val;
			*(ushort*)&prim->u3 = (*VSP)[index][3].uv.val;

			prim->clut = pc->clut;
			prim->tpage = pc->tpage;

			addPrim(pc->ot + (z >> 1), prim);

			prim++;
		}
	}

	pc->primptr = (char*)prim;
}
#endif // DYNAMIC_LIGHTING

// Driver 1 lane markings and zebra crossings are flat coloured quads (type 17: id, 3 pad, v0..v3, r, g, b, code), not textured ones
static void DrawFlatTileQuad(const u_char* poly, int ofse)
{
	POLY_F4* prim;
	SVECTOR* verts = plotContext.verts;
	int opz, z, r, g, b;

#if USE_PGXP
	// decals lie on the road tile: pull them in front like the other decal tiles
	PGXP_SetZOffsetScale(0.0f, 0.991f);
#endif

	gte_ldv3(&verts[poly[4]], &verts[poly[5]], &verts[poly[7]]);
	gte_rtpt();
	gte_nclip();
	gte_stopz(&opz);
	gte_avsz3();
	gte_stotz(&z);

	if (opz <= 0 || z <= 5)
		return;

	prim = (POLY_F4*)plotContext.primptr;
	setPolyF4(prim);

	gte_stsxy3(&prim->x0, &prim->x1, &prim->x2);

	gte_ldv0(&verts[poly[6]]);
	gte_rtps();
	gte_stsxy(&prim->x3);

	// the lit colour is a modulation value: 128 = unchanged
	r = poly[8] * (plotContext.colour & 255) >> 7;
	g = poly[9] * (plotContext.colour >> 8 & 255) >> 7;
	b = poly[10] * (plotContext.colour >> 16 & 255) >> 7;

	prim->r0 = r > 255 ? 255 : r;
	prim->g0 = g > 255 ? 255 : g;
	prim->b0 = b > 255 ? 255 : b;

	addPrim(plotContext.ot + ofse + (z >> 1), prim);
	plotContext.primptr += sizeof(POLY_F4);
}

// [A] custom implemented function
void SubdivNxM(char *polys, int n, int m, int ofse)
{
	MVERTEX subdivVerts[5][5];

	SVECTOR* verts = plotContext.verts;

	POLYFT4* pft4 = (POLYFT4*)polys;

	if ((pft4->id & 0x1f) == 17)
	{
		DrawFlatTileQuad((u_char*)polys, ofse);
		return;
	}

	if (ofse == TILE_UNDERSIDE_OFSE && IS_DRIVER1_CITY(GameLevel))
	{
		int opz;

		gte_ldv3(&verts[pft4->v0], &verts[pft4->v1], &verts[pft4->v3]);
		gte_rtpt();
		gte_nclip();
		gte_stopz(&opz);

		if (opz < 0)
			DrawTileUnderside(verts, pft4->v0, pft4->v1, pft4->v2, pft4->v3, ofse);
	}

	plotContext.clut = (u_int)(*plotContext.ptexture_cluts)[pft4->texture_set][pft4->texture_id];
	plotContext.tpage = (u_int)(*plotContext.ptexture_pages)[pft4->texture_set];

	copyVector(&subdivVerts[0][0], &verts[pft4->v0]);
	subdivVerts[0][0].uv.val = sCoarseWater ? CoarseWaterUV(&pft4->uv0, &pft4->uv0) : *(ushort*)&pft4->uv0;

	copyVector(&subdivVerts[0][1], &verts[pft4->v1]);
	subdivVerts[0][1].uv.val = sCoarseWater ? CoarseWaterUV(&pft4->uv0, &pft4->uv1) : *(ushort*)&pft4->uv1;

	copyVector(&subdivVerts[0][2], &verts[pft4->v3]);
	subdivVerts[0][2].uv.val = sCoarseWater ? CoarseWaterUV(&pft4->uv0, &pft4->uv3) : *(ushort*)&pft4->uv3;

	copyVector(&subdivVerts[0][3], &verts[pft4->v2]);
	subdivVerts[0][3].uv.val = sCoarseWater ? CoarseWaterUV(&pft4->uv0, &pft4->uv2) : *(ushort*)&pft4->uv2;

	plotContext.ot += ofse;

	makeMesh((MVERTEX(*)[5][5])subdivVerts, m, n);
#ifdef DYNAMIC_LIGHTING
	(gEnableDlights ? drawMeshLit : drawMesh)((MVERTEX(*)[5][5])subdivVerts, m, n, &plotContext);
#else
	drawMesh((MVERTEX(*)[5][5])subdivVerts, m, n, &plotContext);
#endif

	plotContext.ot -= ofse;
}

// [D] [T]
void TileNxN(MODEL *model, int levels, int Dofse)
{
	u_int ttype, tileTypes;
	u_char *polys;
	int i;
	int ofse;

	polys = GET_MODEL_DATA(u_char, model, poly_block);
	plotContext.verts = GET_MODEL_DATA(SVECTOR, model, vertices);

	// WEIRD: tile types comes right after model header it seems
	tileTypes = *(u_int *)(model + 1) >> 2;

	// grass should be under pavements and other things
	if((model->shape_flags & SHAPE_FLAG_WATER) || (model->flags2 & MODEL_FLAG_GRASS))
		ofse = 229;
	else
		ofse = 133;

	const u_int normalColour = plotContext.colour;

	sCoarseWater = CoarseWater(model);

	if (sCoarseWater)
	{
		plotContext.colour = CoarseWaterTint(normalColour);
		sCoarseFlip = CoarseWaterFlip(model);
	}

	int _m[5] = {
		levels, levels, 0, 1, levels
	};
	int _ofse[5] = {
		ofse, ofse, 0, Dofse, 133
	};

	i = model->num_polys;
	ttype = 0;
	while (i--)
	{
#if USE_PGXP
		switch (ttype)
		{
		case 0:
		case 1:
			PGXP_SetZOffsetScale(0.0f, ofse > 200 ? 1.008f : 0.995f);
			break;
		case 3:
			PGXP_SetZOffsetScale(0.0f, ofse < 40 ? 1.0f : 0.991f);
			break;
		case 4:
			PGXP_SetZOffsetScale(0.0f, 1.0f);
			break;
		}
#endif

		SubdivNxM((char*)polys, levels, _m[ttype], _ofse[ttype]);

		ttype = tileTypes & 7;
		tileTypes >>= 3;

		polys += plotContext.polySizes[*polys];
	}

	plotContext.colour = normalColour;
	sCoarseWater = 0;

#if USE_PGXP
	PGXP_SetZOffsetScale(0.0f, 1.0f);
#endif
}

void ProcessSubDivisionLump(char *lump_ptr, int lump_size)
{
	// Driver 1 leftover...
	UNIMPLEMENTED();
	/*
	SubDivisionArrays = lump_ptr;
	return;*/
}

// [D] [T]
void ProcessLowDetailTable(char *lump_ptr, int lump_size)
{
	int i;

	Low2HighDetailTable = (ushort *)lump_ptr;
	Low2LowerDetailTable = Low2HighDetailTable + num_models_in_pack;

	for (i = 0; i < num_models_in_pack; i++)
	{
		if (Low2LowerDetailTable[i] != 0xFFFF)
			pLodModels[i] = modelpointers[Low2LowerDetailTable[i]];
		else
			pLodModels[i] = modelpointers[i];
	}
}

// [A] Ground backdrop (map editor request 2026-10-16): the map has holes in its ground (under and between buildings, no roofs any
// more); from above or from a flying car they show the void. A flat dark green quad of GROUND_BACKDROP_CELL units lies below the
// real ground of every land cell around the camera. Its height is the lowest road map height around the cell minus a margin, so
// it never covers a real tile and stays at the water level under a bridge (Chicago) instead of following the deck.
#ifndef PSX
#define GROUND_BACKDROP_CELL	6000
#define GROUND_BACKDROP_RANGE	42000
#define GROUND_BACKDROP_MARGIN	120		// below the lowest sample (world y is down)
#define GROUND_BACKDROP_REFRESH	60		// frames between two height samplings of a cell

void DrawGroundBackdrop(void)
{
	static int sOff = -1;
	static int sLevel = -1, sAcross, sDown, sFrame;
	static int* sHeight = NULL;			// lowest height (engine convention, up is positive) per cell
	static int* sStamp = NULL;			// frame of the last sampling per cell
	const u_int normalColour = plotContext.colour;
	int cx, cz, cx0, cz0, radius, previous = 0;

	if (sOff < 0)
		sOff = getenv("REDRIVER2_NOGROUNDBACKDROP") != NULL;	// A/B test switch

	if (sOff)
		return;

	if (sLevel != GameLevel || !sHeight)
	{
		free(sHeight);
		free(sStamp);
		sLevel = GameLevel;
		sAcross = units_across_halved * 2 / GROUND_BACKDROP_CELL + 2;
		sDown = units_down_halved * 2 / GROUND_BACKDROP_CELL + 2;
		sHeight = (int*)calloc(sAcross * sDown, sizeof(int));
		sStamp = (int*)calloc(sAcross * sDown, sizeof(int));
		if (!sHeight || !sStamp)
			return;
	}

	sFrame++;
	radius = GROUND_BACKDROP_RANGE / GROUND_BACKDROP_CELL;
	cx0 = (camera_position.vx + units_across_halved) / GROUND_BACKDROP_CELL - radius;
	cz0 = (camera_position.vz + units_down_halved) / GROUND_BACKDROP_CELL - radius;

	sQuadRGB[0] = 48;
	sQuadRGB[1] = 62;
	sQuadRGB[2] = 36;

#if USE_PGXP
	PGXP_SetZOffsetScale(0.0f, 1.008f);
#endif

	for (cz = cz0; cz <= cz0 + radius * 2; cz++)
	{
		for (cx = cx0; cx <= cx0 + radius * 2; cx++)
		{
			const int x0 = -units_across_halved + cx * GROUND_BACKDROP_CELL;
			const int z0 = -units_down_halved + cz * GROUND_BACKDROP_CELL;
			int index, Z;

			if (cx < 0 || cz < 0 || cx >= sAcross || cz >= sDown)
				continue;

			index = cx + cz * sAcross;

			// land: the centre and the four corners lie on the overview map and not on water
			if (!MapWorldIsLand(x0 + GROUND_BACKDROP_CELL / 2, z0 + GROUND_BACKDROP_CELL / 2) || !MapWorldIsLand(x0, z0) ||
				!MapWorldIsLand(x0 + GROUND_BACKDROP_CELL - 1, z0) || !MapWorldIsLand(x0, z0 + GROUND_BACKDROP_CELL - 1) ||
				!MapWorldIsLand(x0 + GROUND_BACKDROP_CELL - 1, z0 + GROUND_BACKDROP_CELL - 1))
				continue;

			if (sStamp[index] == 0 || sFrame - sStamp[index] > GROUND_BACKDROP_REFRESH)
			{
				int k, low = 1 << 30;

				// 5 x 5 samples over the cell and half a cell around it: a bridge deck is surrounded by lower ground
				for (k = 0; k < 25; k++)
				{
					VECTOR pos;
					pos.vx = x0 - GROUND_BACKDROP_CELL / 2 + (k % 5) * GROUND_BACKDROP_CELL / 2;
					pos.vz = z0 - GROUND_BACKDROP_CELL / 2 + (k / 5) * GROUND_BACKDROP_CELL / 2;
					pos.vy = 0;
					low = MIN(low, MapHeight(&pos));
				}

				// Chicago is flat at sea level: a lifting bridge must not carry the backdrop up and down with it
				if (GameLevel == 0)
					low = MIN(low, 0);

				sHeight[index] = low;
				sStamp[index] = sFrame;
			}

			if (PRIMTAB_SIZE - (int)(plotContext.primptr - current->primtab) < 100000)
				goto done;

			plotContext.scribble[0] = x0 + GROUND_BACKDROP_CELL / 2;
			plotContext.scribble[1] = -sHeight[index] + GROUND_BACKDROP_MARGIN;
			plotContext.scribble[2] = z0 + GROUND_BACKDROP_CELL / 2;

			if (previous)
				Z = Apply_InvCameraMatrixSetTrans((VECTOR_NOPAD*)plotContext.scribble);
			else
				Z = Apply_InvCameraMatrixAndSetMatrix((VECTOR_NOPAD*)plotContext.scribble, &CompoundMatrix[0]);

			previous = 1;

			if (Z > -GROUND_BACKDROP_CELL)
				DrawFlatWaterQuad(-GROUND_BACKDROP_CELL / 2, -GROUND_BACKDROP_CELL / 2, GROUND_BACKDROP_CELL, 0);
		}
	}

done:
#if USE_PGXP
	PGXP_SetZOffsetScale(0.0f, 1.0f);
#endif
	sQuadRGB[0] = 60;
	sQuadRGB[1] = 100;
	sQuadRGB[2] = 135;
	plotContext.current->primptr = plotContext.primptr;
	plotContext.colour = normalColour;
}
#endif
