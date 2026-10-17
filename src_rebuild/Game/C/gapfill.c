#include "driver2.h"
#include "models.h"
#include "draw.h"
#include "mission.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Roofs for the open-topped boxy buildings of the four Driver 2 cities. The analysis behind the rule and the
// roof textures is src_rebuild/tools/gapfill (analyze.py); keep EXCLUDED_*, ROOF_* in lev2.py in sync.
// A model that qualifies gets two extra quads (one per winding) on its top vertex ring; models are copied, the
// game's own data is never touched.

#define ROOF_EXCLUDED_SHAPE		(SHAPE_FLAG_SPRITE | SHAPE_FLAG_ROAD | SHAPE_FLAG_TILE | SHAPE_FLAG_WATER | SHAPE_FLAG_TRANS | SHAPE_FLAG_ALPHA)
#define ROOF_EXCLUDED_FLAGS2	0xFFE1	// animated, median, junction, alley, indoors, chair, barrier, smashable, lamp, tree, grass, path
#define ROOF_MIN_SIDE			256
#define ROOF_MAX_SIDE			6000
#define ROOF_MIN_HEIGHT			700
#define ROOF_MAX_HEIGHT			12000
#define ROOF_MAX_RING			8
#define ROOF_TOP_TOLERANCE		8
#define ROOF_MIN_FILL			0.3f	// ring area / bounding box area: a facade plate has a line as top

#define ROOF_POLY_SIZE			16		// type 11 quad: id, texture set, texture id, shade, 4 vertices, 4 uv pairs
#define ROOF_UV_PATCH			12		// texels: a roof shows this centre patch of the pavement window, not the whole window

struct RoofTexture
{
	u_char set, id, u0, v0, u1, v1;
};

// Pavement texture of each city (permanent page): the most uniform grey 12 x 12 texel square inside the window that
// analyze.py "roof texture" found (lowest variance, no dark or transparent texel)
static const RoofTexture sRoofTextures[4] = {
	{ 0, 4, 1, 130, 13, 142 },		// Chicago
	{ 0, 2, 211, 216, 223, 228 },	// Havana
	{ 3, 2, 222, 48, 234, 60 },		// Vegas
	{ 0, 7, 221, 163, 233, 175 },	// Rio
};

static char* sRoofBuffer[MAX_MODEL_SLOTS];	// one copy per model slot, reused when the slot is spooled again
static int sRoofBufferSize[MAX_MODEL_SLOTS];
static int sRoofPatched = 0;

struct RoofPoint
{
	int x, z;
	int vertex;
	float angle;
};

static int RoofPointCompare(const void* a, const void* b)
{
	float d = ((const RoofPoint*)a)->angle - ((const RoofPoint*)b)->angle;
	return d < 0 ? -1 : d > 0;
}

// fills ring (angle order) with the distinct top vertices, returns their count or 0 when the model gets no roof
static int FindRoofRing(MODEL* model, SVECTOR* verts, int* bbox, RoofPoint* ring)
{
	int i, j, n;
	int w, h, d;
	float cx, cz, area;
	int sign;

	bbox[0] = bbox[2] = 32767;
	bbox[1] = bbox[3] = -32767;
	int ymin = 32767, ymax = -32767;

	for (i = 0; i < model->num_vertices; i++)
	{
		bbox[0] = MIN(bbox[0], verts[i].vx);
		bbox[1] = MAX(bbox[1], verts[i].vx);
		bbox[2] = MIN(bbox[2], verts[i].vz);
		bbox[3] = MAX(bbox[3], verts[i].vz);
		ymin = MIN(ymin, verts[i].vy);
		ymax = MAX(ymax, verts[i].vy);
	}

	w = bbox[1] - bbox[0];
	d = bbox[3] - bbox[2];
	h = ymax - ymin;

	if (w < ROOF_MIN_SIDE || d < ROOF_MIN_SIDE || w > ROOF_MAX_SIDE || d > ROOF_MAX_SIDE || h < ROOF_MIN_HEIGHT || h > ROOF_MAX_HEIGHT)
		return 0;

	n = 0;
	for (i = 0; i < model->num_vertices; i++)
	{
		if (verts[i].vy > ymin + ROOF_TOP_TOLERANCE)
			continue;

		for (j = 0; j < n; j++)
		{
			if (ring[j].x == verts[i].vx && ring[j].z == verts[i].vz)
				break;
		}

		if (j < n)
			continue;

		if (n == ROOF_MAX_RING)
			return 0;

		ring[n].x = verts[i].vx;
		ring[n].z = verts[i].vz;
		ring[n].vertex = i;
		n++;
	}

	if (n < 3)
		return 0;

	cx = cz = 0;
	for (i = 0; i < n; i++)
	{
		cx += ring[i].x;
		cz += ring[i].z;
	}
	cx /= n;
	cz /= n;

	for (i = 0; i < n; i++)
		ring[i].angle = atan2f(ring[i].z - cz, ring[i].x - cx);

	qsort(ring, n, sizeof(RoofPoint), RoofPointCompare);

	sign = 0;
	area = 0;
	for (i = 0; i < n; i++)
	{
		RoofPoint* a = &ring[i];
		RoofPoint* b = &ring[(i + 1) % n];
		RoofPoint* c = &ring[(i + 2) % n];
		float cross = (float)(b->x - a->x) * (c->z - b->z) - (float)(b->z - a->z) * (c->x - b->x);

		area += (float)a->x * b->z - (float)b->x * a->z;

		if (cross != 0)
		{
			if (sign && (cross > 0) != (sign > 0))
				return 0;	// not convex, a fan would cover the notch

			sign = cross > 0 ? 1 : -1;
		}
	}

	if (!sign || fabsf(area) / 2 < ROOF_MIN_FILL * (float)w * d)
		return 0;

	return n;
}

static void WriteRoofQuad(u_char* dest, u_char id, const RoofTexture* tex, const int* bbox, const RoofPoint* a, const RoofPoint* b, const RoofPoint* c, const RoofPoint* e)
{
	const RoofPoint* pts[4] = { a, b, c, e };
	int i;

	// The whole window (up to 63 x 127 texels) stretched over a roof of thousands of units showed as streaks from
	// the air: map a small square patch from its centre instead, it reads as plain concrete.
	const int side = MIN(ROOF_UV_PATCH, MIN(tex->u1 - tex->u0, tex->v1 - tex->v0));
	const int pu0 = (tex->u0 + tex->u1 - side) / 2;
	const int pv0 = (tex->v0 + tex->v1 - side) / 2;

	dest[0] = id;
	dest[1] = tex->set;
	dest[2] = tex->id;
	dest[3] = 0;	// shade index, the renderer computes it on first draw

	for (i = 0; i < 4; i++)
	{
		int u = pu0 + (pts[i]->x - bbox[0]) * side / MAX(1, bbox[1] - bbox[0]);
		int v = pv0 + (pts[i]->z - bbox[2]) * side / MAX(1, bbox[3] - bbox[2]);

		dest[4 + i] = (u_char)pts[i]->vertex;
		dest[8 + i * 2] = (u_char)MAX(0, MIN(u, 255));
		dest[9 + i * 2] = (u_char)MAX(0, MIN(v, 255));
	}
}

// Returns the model itself or a patched copy. size = bytes of the model in its lump / spool buffer.
MODEL* GapFillModel(MODEL* model, int size, int modelNumber)
{
	RoofPoint ring[ROOF_MAX_RING];
	int bbox[4];
	int i, n, quads, add;
	u_char id = 0;
	u_char* polys;
	u_char* polyEnd;
	char* copy;
	MODEL* patched;
	int polyEndOffset;
	const RoofTexture* tex;

	if (GameLevel < 0 || GameLevel > 3 || modelNumber < 0 || modelNumber >= MAX_MODEL_SLOTS || size <= 0)
		return model;

	if (model->instance_number != -1 || model->num_vertices < 4 || model->num_vertices > 255 || model->num_polys < 3)
		return model;

	if ((model->shape_flags & ROOF_EXCLUDED_SHAPE) || (model->flags2 & ROOF_EXCLUDED_FLAGS2))
		return model;

	if (!getenv("REDRIVER2_ROOFS"))	// roofs are off (map editor ground fill replaces them); the variable brings them back
		return model;

	SVECTOR* verts = (SVECTOR*)((char*)model + model->vertices);
	n = FindRoofRing(model, verts, bbox, ring);
	if (n == 0)
		return model;

	// the model must not have a drawn polygon on its top yet
	polys = (u_char*)model + model->poly_block;
	for (i = 0; i < model->num_polys; i++)
	{
		int type = polys[0] & 31;
		int topVerts = 0;

		if (PolySizes[type] == 0)
			return model;

		if (type == 11 || type == 21 || type == 23)
		{
			int k;
			id = id ? id : polys[0];

			for (k = 0; k < 4; k++)
				topVerts += verts[polys[4 + k]].vy <= verts[ring[0].vertex].vy + ROOF_TOP_TOLERANCE;

			if (topVerts == 4)
				return model;
		}

		polys += PolySizes[type];
	}

	if (!id)
		id = 0x2b;

	polyEnd = polys;
	polyEndOffset = (int)(polyEnd - (u_char*)model);
	quads = (n == 4) ? 1 : n - 2;
	add = quads * 2 * ROOF_POLY_SIZE;
	tex = &sRoofTextures[GameLevel];

	if (sRoofBufferSize[modelNumber] < size + add)
	{
		free(sRoofBuffer[modelNumber]);
		sRoofBuffer[modelNumber] = (char*)malloc(size + add);
		sRoofBufferSize[modelNumber] = sRoofBuffer[modelNumber] ? size + add : 0;
	}

	copy = sRoofBuffer[modelNumber];
	if (!copy)
		return model;

	memcpy(copy, model, polyEndOffset);
	memcpy(copy + polyEndOffset + add, (char*)model + polyEndOffset, size - polyEndOffset);

	// both windings: the renderer culls back faces and the roof must show from above
	for (i = 0; i < quads; i++)
	{
		const RoofPoint* a = &ring[0];
		const RoofPoint* b = &ring[n == 4 ? 1 : i + 1];
		const RoofPoint* c = &ring[n == 4 ? 2 : i + 2];
		const RoofPoint* e = n == 4 ? &ring[3] : c;	// triangles are quads with the last vertex repeated

		WriteRoofQuad((u_char*)copy + polyEndOffset + i * 2 * ROOF_POLY_SIZE, id, tex, bbox, a, b, c, e);
		WriteRoofQuad((u_char*)copy + polyEndOffset + (i * 2 + 1) * ROOF_POLY_SIZE, id, tex, bbox, e, c, b, a);
	}

	patched = (MODEL*)copy;
	patched->num_polys += quads * 2;

	// blocks behind the polygons moved
	if (patched->vertices >= polyEndOffset)
		patched->vertices += add;
	if (patched->normals >= polyEndOffset)
		patched->normals += add;
	if (patched->point_normals >= polyEndOffset)
		patched->point_normals += add;
	if (patched->collision_block >= polyEndOffset)
		patched->collision_block += add;

	if (sRoofPatched++ < 12)
		printInfo("gapfill: roof on model %d (%d ring points, %d quads)\n", modelNumber, n, quads * 2);

	return patched;
}
