#ifndef LEV_FORMAT_H
#define LEV_FORMAT_H

// Driver 1 and Driver 2 PSX LEV structures used by the D1 -> D2 level converter.
// Layouts follow OpenDriverEngine routines/d2_types.h and REDRIVER2 Game/dr2types.h.

#include <algorithm>
#include <math.h>
#include <string.h>
#include <stdint.h>
#include <string>
#include <vector>

#define LEV_SECTOR_SIZE			2048
#define LEV_MAX_CAR_MODELS		13

#define D1_CELL_SIZE			3000
#define D1_REGION_CELLS			30
#define D1_ROADMAP_UNIT			1500
#define D1_ROADMAP_REGION_CELLS	3600		// 60 x 60 road map units per region

#define D2_CELL_SIZE			2048
#define D2_REGION_CELLS			32
#define D2_SURFACE_GRID			64			// sdHeightmap: 64 x 64 cells of 1024 units per region

enum LevLumpType
{
	LUMP_PERMANENTPAGES		= 0,
	LUMP_MODELS				= 1,
	LUMP_MAP				= 2,
	LUMP_TEXTURENAMES		= 5,
	LUMP_ROADMAP			= 7,		// D1: road map width/height in 1500-unit cells
	LUMP_ROADS				= 8,		// D1
	LUMP_JUNCTIONS			= 9,		// D1
	LUMP_ROADSURF			= 10,		// D1: SURFACEINFO + SIPOLY slope polygons
	LUMP_MODELNAMES			= 12,
	LUMP_ROADBOUNDS			= 16,		// D1
	LUMP_JUNCBOUNDS			= 17,		// D1
	LUMP_SUBDIVISION		= 20,
	LUMP_LOWDETAILTABLE		= 21,
	LUMP_MOTIONCAPTURE		= 22,
	LUMP_OVERLAYMAP			= 24,
	LUMP_PALLET				= 25,
	LUMP_SPOOLINFO			= 26,
	LUMP_CAR_MODELS			= 28,
	LUMP_CHAIR				= 33,
	LUMP_TEXTUREINFO		= 34,
	LUMP_LOADTIME_DATA		= 35,
	LUMP_INMEMORY_DATA		= 36,
	LUMP_LUMPDESC			= 37,
	LUMP_STRAIGHTS2			= 40,		// D2
	LUMP_CURVES2			= 41,		// D2
	LUMP_JUNCTIONS2			= 42,		// D2 alpha 1.6
	LUMP_JUNCTIONS2_NEW		= 43,		// D2 retail
	LUMP_END				= 255,
};

#pragma pack(push, 1)

struct LUMP
{
	int32_t type;
	int32_t size;
};

struct OUT_CITYLUMP_INFO
{
	int32_t loadtime_offset, loadtime_size;
	int32_t tpage_offset, tpage_size;
	int32_t inmem_offset, inmem_size;
	int32_t spooled_offset, spooled_size;
};

struct LEV_VECTOR
{
	int32_t vx, vy, vz;
};

struct LEV_SVECTOR
{
	int16_t vx, vy, vz, pad;
};

struct LEV_XZPAIR
{
	int32_t x, z;
};

struct OUT_CELL_FILE_HEADER
{
	int32_t cells_across;
	int32_t cells_down;
	int32_t cell_size;
	int32_t num_regions;
	int32_t region_size;
	int32_t num_cell_objects;
	int32_t num_cell_data;
	int32_t ambient_light_level;
	LEV_VECTOR light_source;
};

struct TEXPAGE_POS
{
	uint32_t flags;
	uint32_t offset;
};

struct TEXINF
{
	uint16_t id;
	uint16_t nameoffset;
	uint8_t x, y, width, height;
};

struct AreaDataStr
{
	uint16_t gfx_offset;
	uint16_t model_offset;
	uint16_t music_offset;
	uint16_t ambient_offset;
	uint8_t model_size;
	uint8_t pad;
	uint8_t num_tpages;
	uint8_t ambient_size;
	uint8_t music_size;
	uint8_t music_samples_size;
	uint8_t music_id;
	uint8_t ambient_id;
};

struct Spool
{
	uint16_t offset;					// in sectors from spooled_offset
	uint8_t connected_areas[2];
	uint8_t pvs_size;
	uint8_t cell_data_size[3];			// [0] cell data, [1] cell pointers, [2] cell objects (sectors)
	uint8_t super_region;
	uint8_t num_connected_areas;
	uint8_t roadm_size;					// D1 only: road id RLE (sectors)
	uint8_t roadh_size;					// D1 only: road map value RLE (sectors)
};

struct COLLISION_PACKET
{
	int16_t type;
	int16_t xpos, ypos, zpos;
	int16_t flags;
	int16_t yang;
	int16_t empty;
	int16_t xsize, ysize, zsize;
};

struct MODEL
{
	int16_t shape_flags;
	int16_t flags2;
	int16_t instance_number;
	uint8_t tri_verts;
	uint8_t zBias;
	int16_t bounding_sphere;
	int16_t num_point_normals;
	int16_t num_vertices;
	int16_t num_polys;
	int32_t vertices;					// byte offsets from the MODEL start
	int32_t poly_block;
	int32_t normals;
	int32_t point_normals;
	int32_t collision_block;
};

// Game/C/draw.c PolySizes (types 0..31), 0 = unknown type
const int kPolySizes[32] = {
	8, 12, 16, 24, 20, 20, 28, 32, 8, 12, 16, 16, 0, 0, 0, 0,
	12, 12, 12, 16, 20, 20, 24, 24, 0, 0, 0, 0, 0, 0, 0, 0,
};

// polygon types with texture_set / texture_id at bytes 1 and 2
inline bool IsTexturedPoly(int type)
{
	return (type >= 4 && type <= 7) || type == 10 || type == 11 || (type >= 20 && type <= 23);
}

// Driver 1 puts grass and water tiles under the road tiles at the same height; lowering them avoids z-fighting
// (same hack as OpenDriverEngine render_model.cpp). Only models that own their vertices, `model` points at the MODEL.
inline void LowerGrassModel(char* model, int size)
{
	MODEL* m = (MODEL*)model;
	const int kGrassFlag = 0x4000, kWaterFlag = 0x80, kLowerBy = 8;

	if (m->instance_number != -1 || !((m->flags2 & kGrassFlag) || (m->shape_flags & kWaterFlag)))
		return;

	if (m->vertices < 0 || m->vertices + m->num_vertices * 8 > size)
		return;

	int16_t* v = (int16_t*)(model + m->vertices);
	for (int i = 0; i < m->num_vertices; i++)
		v[i * 4 + 1] += kLowerBy;		// SVECTOR {x, y, z, pad}; +y is down
}

// Highest vertex of a model (lowest y, <= 0) relative to its origin; 0 for models without own vertices.
// D1 path (pavement) tiles stand this far above the ground height of the road map.
inline int ModelTopY(const char* model, int size)
{
	const MODEL* m = (const MODEL*)model;

	if (size < (int)sizeof(MODEL) || m->instance_number != -1 || m->vertices < 0 || m->vertices + m->num_vertices * 8 > size)
		return 0;

	const int16_t* v = (const int16_t*)(model + m->vertices);
	int top = 0;
	for (int i = 0; i < m->num_vertices; i++)
		top = top < v[i * 4 + 1] ? top : v[i * 4 + 1];

	return top;
}


// Extent and horizontal polygon area of a model (see D1Dlc_ConvertMap gap handling). Areas are split by winding:
// which sign faces up is decided by the caller from a ground tile.
struct ModelStats
{
	int		minX{ 0 }, maxX{ 0 }, minY{ 0 }, maxY{ 0 }, minZ{ 0 }, maxZ{ 0 };
	int		numPolys{ 0 }, numTextured{ 0 };
	double	topPos{ 0 }, topNeg{ 0 };			// area of horizontal polygons in the upper fifth of the model height, by winding
	double	allPos{ 0 }, allNeg{ 0 };			// same for the whole model
	bool	valid{ false };
};

// vmodel / vsize: the model that owns the vertices (the model itself unless it is an instance of another one)
inline ModelStats AnalyseModel(const char* model, int size, const char* vmodel, int vsize)
{
	ModelStats s;
	const MODEL* m = (const MODEL*)model;
	const MODEL* vm = (const MODEL*)vmodel;

	if (size < (int)sizeof(MODEL) || vsize < (int)sizeof(MODEL) || vm->vertices < 0 || vm->num_vertices == 0 || vm->vertices + vm->num_vertices * 8 > vsize)
		return s;

	const int16_t* v = (const int16_t*)(vmodel + vm->vertices);
	s.minX = s.maxX = v[0];
	s.minY = s.maxY = v[1];
	s.minZ = s.maxZ = v[2];

	for (int i = 1; i < vm->num_vertices; i++)
	{
		s.minX = s.minX < v[i * 4] ? s.minX : v[i * 4];
		s.maxX = s.maxX > v[i * 4] ? s.maxX : v[i * 4];
		s.minY = s.minY < v[i * 4 + 1] ? s.minY : v[i * 4 + 1];
		s.maxY = s.maxY > v[i * 4 + 1] ? s.maxY : v[i * 4 + 1];
		s.minZ = s.minZ < v[i * 4 + 2] ? s.minZ : v[i * 4 + 2];
		s.maxZ = s.maxZ > v[i * 4 + 2] ? s.maxZ : v[i * 4 + 2];
	}

	s.valid = true;
	const int topLimit = s.minY + (s.maxY - s.minY) / 5;
	int ofs = m->poly_block;

	for (int i = 0; i < m->num_polys; i++)
	{
		if (ofs < 0 || ofs + 8 > size)
			break;

		const int type = (unsigned char)model[ofs] & 31;
		if (kPolySizes[type] == 0 || ofs + kPolySizes[type] > size)
			break;

		s.numPolys++;
		const unsigned char* p = (const unsigned char*)model + ofs;
		ofs += kPolySizes[type];

		if (!IsTexturedPoly(type))
			continue;

		s.numTextured++;
		const int tri[2][3] = { { p[4], p[5], p[6] }, { p[4], p[6], p[7] } };
		for (int t = 0; t < ((type & 1) ? 2 : 1); t++)
		{
			const int a = tri[t][0], b = tri[t][1], c = tri[t][2];
			if (a >= vm->num_vertices || b >= vm->num_vertices || c >= vm->num_vertices)
				continue;

			const double ux = v[b * 4] - v[a * 4], uy = v[b * 4 + 1] - v[a * 4 + 1], uz = v[b * 4 + 2] - v[a * 4 + 2];
			const double wx = v[c * 4] - v[a * 4], wy = v[c * 4 + 1] - v[a * 4 + 1], wz = v[c * 4 + 2] - v[a * 4 + 2];
			const double nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
			const double len = sqrt(nx * nx + ny * ny + nz * nz);

			if (len < 1.0 || fabs(ny) < 0.9 * len)
				continue;

			const double area = len / 2;
			const int cy = (v[a * 4 + 1] + v[b * 4 + 1] + v[c * 4 + 1]) / 3;
			(ny > 0 ? s.allPos : s.allNeg) += area;
			if (cy <= topLimit)
				(ny > 0 ? s.topPos : s.topNeg) += area;
		}
	}

	return s;
}
// Driver 1 buildings have no roof (nobody looks down in the original game). Adds one quad over the box of a roofless
// building model at its top, textured like the topmost wall quad and facing up. The new vertex and polygon lists are
// copies appended to the model, so no offset inside the block moves. Returns 1 when a roof was added.
// D1 collision boxes of low objects (kerbs, barriers, stands) are 2D walls of any height: ysize reaches 3000 and more above a
// model of 140 units. The D2 engine checks the height, so a flying car or helicopter hit an invisible wall. Boxes are cut
// to the model top plus a margin, never below kMinBoxTop so ground cars (jumps) are still stopped.
inline void ClampBoxHeights(std::vector<char>& model)
{
	const int kTopMargin = 150;
	const int kMinBoxTop = 400;

	if (model.size() < sizeof(MODEL))
		return;

	const MODEL& h = *(const MODEL*)model.data();
	if (h.collision_block <= 0 || h.collision_block + 4 > (int)model.size() || h.vertices <= 0 || h.num_vertices == 0 || h.vertices + h.num_vertices * 8 > (int)model.size())
		return;

	int count = *(const int*)&model[h.collision_block];
	const int16_t* v = (const int16_t*)&model[h.vertices];
	int modelTop = 0;
	for (int i = 0; i < h.num_vertices; i++)
		modelTop = v[i * 4 + 1] < modelTop ? v[i * 4 + 1] : modelTop;

	int limit = modelTop - kTopMargin;
	if (limit > -kMinBoxTop)
		limit = -kMinBoxTop;

	COLLISION_PACKET* p = (COLLISION_PACKET*)&model[h.collision_block + 4];
	for (int i = 0; i < count && h.collision_block + 4 + (i + 1) * (int)sizeof(COLLISION_PACKET) <= (int)model.size(); i++)
	{
		int top = p[i].ypos - p[i].ysize / 2;
		int bottom = p[i].ypos + p[i].ysize / 2;
		if (top >= limit || bottom <= limit)
			continue;

		p[i].ysize = (int16_t)(bottom - limit);
		p[i].ypos = (int16_t)((bottom + limit) / 2);
	}
}

// Kerbs, steps and stands of Driver 1 carry long thin wall boxes (cut to a top of kMinBoxTop by ClampBoxHeights) along the kerb
// stone: the car stops at the kerb instead of driving up on the pavement ("invisible walls" on pavements). Found with the map
// editor box report: a low model (<= 250 high) with a box that is long (>= 1000), thin (<= 800) and stands at the clamp height.
// Barriers (BARRIER*) keep their boxes. Returns the number of boxes removed.
inline int RemoveKerbWallBoxes(std::vector<char>& model, const std::string& name)
{
	const int kMaxModelHeight = 250, kMinBoxTop = 395, kMinLong = 1000, kMaxShort = 800;

	if (model.size() < sizeof(MODEL) || name.compare(0, 7, "BARRIER") == 0)
		return 0;

	const MODEL& h = *(const MODEL*)model.data();
	if (h.collision_block <= 0 || h.collision_block + 4 > (int)model.size() || h.vertices <= 0 || h.num_vertices == 0 || h.vertices + h.num_vertices * 8 > (int)model.size())
		return 0;

	int* count = (int*)&model[h.collision_block];
	const int16_t* v = (const int16_t*)&model[h.vertices];
	int modelTop = 0;
	for (int i = 0; i < h.num_vertices; i++)
		modelTop = v[i * 4 + 1] < modelTop ? v[i * 4 + 1] : modelTop;

	if (-modelTop > kMaxModelHeight || *count <= 0 || h.collision_block + 4 + *count * (int)sizeof(COLLISION_PACKET) > (int)model.size())
		return 0;

	COLLISION_PACKET* p = (COLLISION_PACKET*)&model[h.collision_block + 4];
	int kept = 0;
	for (int i = 0; i < *count; i++)
	{
		const int top = -(p[i].ypos - p[i].ysize / 2);
		const int longSide = p[i].xsize > p[i].zsize ? p[i].xsize : p[i].zsize;
		const int shortSide = p[i].xsize > p[i].zsize ? p[i].zsize : p[i].xsize;

		if (top >= kMinBoxTop && longSide >= kMinLong && shortSide <= kMaxShort)
			continue;

		p[kept++] = p[i];
	}

	const int removed = *count - kept;
	*count = kept;
	return removed;
}

// roofs on D1 buildings are off: the ground fill (FillGroundGaps) closes the holes instead
constexpr bool kRoofCaps = false;

inline int AddRoofCap(std::vector<char>& block)
{
	const int kMinHeight = 500, kMinSide = 500;
	const MODEL* m = (const MODEL*)block.data();
	const int size = (int)block.size();

	if (size < (int)sizeof(MODEL) || m->instance_number != -1 || (m->flags2 & 0xFFFE) || (m->shape_flags & (0x4000 | 0x1000 | 0x8 | 0x80 | 0x400 | 0x2000)) ||
		m->num_vertices > 250 || m->num_polys == 0 || m->poly_block < 0 || m->vertices < 0)
		return 0;

	const ModelStats s = AnalyseModel(block.data(), size, block.data(), size);
	const double dx = s.maxX - s.minX, dz = s.maxZ - s.minZ;

	if (!s.valid || s.maxY - s.minY < kMinHeight || dx < kMinSide || dz < kMinSide || std::max(s.topPos, s.topNeg) > 0.3 * dx * dz)
		return 0;

	// source polygon: the textured quad (types 5, 11, 21: no normals) with the smallest mean height
	const int16_t* v = (const int16_t*)(block.data() + m->vertices);
	int ofs = m->poly_block, srcOfs = -1, srcSize = 0, polyBytes = 0;
	double srcY = 1e18;

	for (int i = 0; i < m->num_polys; i++)
	{
		if (ofs < 0 || ofs + 8 > size)
			return 0;

		const int type = (unsigned char)block[ofs] & 31;
		if (kPolySizes[type] == 0 || ofs + kPolySizes[type] > size)
			return 0;

		if (type == 5 || type == 11 || type == 21)
		{
			const unsigned char* p = (const unsigned char*)block.data() + ofs;
			if (p[4] < m->num_vertices && p[5] < m->num_vertices && p[6] < m->num_vertices && p[7] < m->num_vertices)
			{
				const double y = (v[p[4] * 4 + 1] + v[p[5] * 4 + 1] + v[p[6] * 4 + 1] + v[p[7] * 4 + 1]) / 4.0;
				if (y < srcY)
				{
					srcY = y;
					srcOfs = ofs;
					srcSize = kPolySizes[type];
				}
			}
		}

		ofs += kPolySizes[type];
		polyBytes += kPolySizes[type];
	}

	if (srcOfs < 0)
		return 0;

	std::vector<char> poly(block.begin() + srcOfs, block.begin() + srcOfs + srcSize);
	const int nv = m->num_vertices;
	poly[4] = (char)nv; poly[5] = (char)(nv + 1); poly[6] = (char)(nv + 2); poly[7] = (char)(nv + 3);

	const int16_t top = (int16_t)s.minY;
	const int16_t corner[4][3] = { { (int16_t)s.minX, top, (int16_t)s.minZ }, { (int16_t)s.minX, top, (int16_t)s.maxZ }, { (int16_t)s.maxX, top, (int16_t)s.maxZ }, { (int16_t)s.maxX, top, (int16_t)s.minZ } };

	// copies of the vertex and polygon lists at the end (4 byte aligned), the polygon list gets the roof as its last entry
	while (block.size() % 4)
		block.push_back(0);

	const int newVertices = (int)block.size();
	block.resize(block.size() + (nv + 4) * 8);
	memcpy(&block[newVertices], block.data() + ((const MODEL*)block.data())->vertices, nv * 8);
	for (int k = 0; k < 4; k++)
	{
		int16_t* d = (int16_t*)&block[newVertices + (nv + k) * 8];
		d[0] = corner[k][0]; d[1] = corner[k][1]; d[2] = corner[k][2]; d[3] = 0;
	}

	const int newPolys = (int)block.size();
	block.resize(block.size() + polyBytes + srcSize);
	memcpy(&block[newPolys], block.data() + ((const MODEL*)block.data())->poly_block, polyBytes);
	memcpy(&block[newPolys + polyBytes], poly.data(), srcSize);

	MODEL* nm = (MODEL*)block.data();
	nm->vertices = newVertices;
	nm->poly_block = newPolys;
	nm->num_vertices += 4;
	nm->num_polys += 1;
	return 1;
}

struct carmodelentry_t
{
	int32_t cleanOffset;				// -1 if no model
	int32_t damOffset;
	int32_t lowOffset;
};

// ---------------------------------------------------------------- cells

struct CELL_DATA_D1
{
	uint16_t num;						// low 14 bits: cell object index
	uint16_t next_ptr;
};

struct CELL_OBJECT_D1
{
	LEV_VECTOR pos;
	uint8_t pad;
	uint8_t yang;						// 0..63, same as D2
	uint16_t type;						// model index
};

struct PACKED_CELL_OBJECT
{
	uint16_t pos_vx, pos_vy, pos_vz;	// x/z: low 16 bits of the world position
	uint16_t value;						// model index + yang (see REDRIVER2 cell.c UnpackCellObject)
};

// ---------------------------------------------------------------- D1 roads + heights

struct ROAD_MAP_LUMP_DATA
{
	int32_t width, height;
};

struct DRIVER1_ROAD
{
	int16_t id;
	int16_t ConnectIdx[2];				// junction ids at both ends
	int8_t NumLanes, LaneDirs;
	int16_t length;						// in road map units (1500)
	int8_t flags;						// bit 0: parking
	int8_t pad;
	int16_t x, z;
};

struct DRIVER1_JUNC_ENTRY
{
	int16_t fromJunc;
	int16_t road;						// -1 = no exit
	int16_t flags;
	int16_t extraFlags;
};

struct DRIVER1_JUNCTION
{
	int16_t id;
	int8_t type;
	int8_t pad;
	int16_t lightsTimer;
	int8_t lightsNS, lightsWE;
	DRIVER1_JUNC_ENTRY entries[4];
	int16_t x, z;
};

struct DRIVER1_ROADBOUNDS
{
	int32_t x1, y1;						// road map units, axis aligned
	int32_t x2, y2;
	uint8_t dir;						// 0 or 1 (axis)
	uint8_t pad[3];
};

struct LONGPLANE
{
	int32_t a, b, c, d;
};

struct SIPOLY
{
	LEV_XZPAIR xz[4];
	LONGPLANE normals;
	int16_t num_vertices;
};

struct SURFACEINFO
{
	int16_t type;
	int16_t info;
	int16_t heading;
	int16_t numpolys;					// followed by numpolys SIPOLY
};

// ---------------------------------------------------------------- D2 roads + surfaces

struct sdPlane
{
	int16_t surface;					// road id + 32 (0x2000 junction, 0x4000 curve) or SurfaceType < 32
	int16_t a, b, c;
	int32_t d;
};

struct DRIVER2_STRAIGHT
{
	int32_t Midx, Midz;
	uint16_t length;
	int16_t bing, angle;
	int16_t ConnectIdx[4];
	int8_t NumLanes, LaneDirs, AILanes, packing;
	int16_t alignPad;					// natural 4-byte alignment in the original data (stride 28)
};

struct DRIVER2_CURVE
{
	int32_t Midx, Midz;
	int16_t start, end;
	int16_t ConnectIdx[4];
	int16_t gradient, height;
	int8_t NumLanes, LaneDirs, inside, AILanes;
};

struct DRIVER2_JUNCTION
{
	int16_t ExitIdx[4];
	uint32_t flags;
};

// ---------------------------------------------------------------- cars

struct CAR_COSMETICS_D1
{
	LEV_SVECTOR headLight;
	LEV_SVECTOR frontInd;
	LEV_SVECTOR backInd;
	LEV_SVECTOR brakeLight;
	LEV_SVECTOR revLight;
	LEV_SVECTOR exhaust;
	LEV_SVECTOR smoke;
	LEV_SVECTOR fire;
	LEV_SVECTOR wheelDisp[4];
	int32_t pad;
};

#pragma pack(pop)

static_assert(sizeof(OUT_CELL_FILE_HEADER) == 44, "OUT_CELL_FILE_HEADER");
static_assert(sizeof(AreaDataStr) == 16, "AreaDataStr");
static_assert(sizeof(Spool) == 12, "Spool");
static_assert(sizeof(MODEL) == 36, "MODEL");
static_assert(sizeof(CELL_OBJECT_D1) == 16, "CELL_OBJECT_D1");
static_assert(sizeof(PACKED_CELL_OBJECT) == 8, "PACKED_CELL_OBJECT");
static_assert(sizeof(DRIVER1_ROAD) == 16, "DRIVER1_ROAD");
static_assert(sizeof(DRIVER1_JUNCTION) == 44, "DRIVER1_JUNCTION");
static_assert(sizeof(DRIVER1_ROADBOUNDS) == 20, "DRIVER1_ROADBOUNDS");
static_assert(sizeof(sdPlane) == 12, "sdPlane");
static_assert(sizeof(DRIVER2_STRAIGHT) == 28, "DRIVER2_STRAIGHT");
static_assert(sizeof(DRIVER2_CURVE) == 28, "DRIVER2_CURVE");
static_assert(sizeof(DRIVER2_JUNCTION) == 12, "DRIVER2_JUNCTION");
static_assert(sizeof(CAR_COSMETICS_D1) == 100, "CAR_COSMETICS_D1");

#endif // LEV_FORMAT_H
