#include "d1_height.h"
#include "d1_roads.h"
#include "d1_util.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace
{

const int kRoadCell = 1500;							// D1 road map cell
const int kGridCells = 64;							// D2 height map: 64x64 cells of 1024 units per region
const int kGridUnit = 1024;
const int kRegionUnits = kGridCells * kGridUnit;
const int kGridOffset = 512;						// cell grid is shifted by half a cell against the object cells
const int kPvsHeaderSize = 0x804;
const int kHeaderBytes = (4 + kGridCells * kGridCells) * 2;
const int kMaxNodes = 16383;
const int kMaxPlanes = 2046;						// planes + nodes offsets are 16 bit byte offsets in the header
const int kSurfaceWater = 9;						// SURF_DEEPWATER: a car or Tanner in the sea sinks like in Driver 2 (SURF_WATER is shallow water, the car floats)
const int kWaterDepth = 2048;						// the ground of a water tile lies this far below the water surface (D2 sea plane: d = 2048)
const int kSurfaceGrass = 4;						// SURF_GRASS
const int kSurfacePavement = 12;					// pedestrians ping in on surface 12 next to a road (pedest.c IsPavement)
const int kFlagWater = 0x80;						// SHAPE_FLAG_WATER
const int kFlagGrass = 0x4000;						// MODEL_FLAG_GRASS
const int kFlagPath = 0x8000;						// MODEL_FLAG_PATH
const int kMaxKerb = 64;							// taller path models are walls or ramps, not kerbs

int FloorDiv(int a, int b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int FloorMod(int a, int b)
{
	return a - FloorDiv(a, b) * b;
}

int PointInTri(int tx, int ty, const int v[4][2])
{
	if ((v[1][1] - v[0][1]) * (tx - v[0][0]) - (v[1][0] - v[0][0]) * (ty - v[0][1]) < 0)
		return 0;

	if ((v[2][1] - v[1][1]) * (tx - v[1][0]) - (v[2][0] - v[1][0]) * (ty - v[1][1]) >= 0)
		return (v[0][1] - v[2][1]) * (tx - v[2][0]) - (v[0][0] - v[2][0]) * (ty - v[2][1]) >= 0;

	return 0;
}

int PointInQuad(int tx, int ty, const int v[4][2])
{
	for (int i = 0; i < 4; i++)
	{
		const int* p = v[i];
		const int* q = v[(i + 1) & 3];

		if ((tx - p[0]) * (q[1] - p[1]) - (ty - p[1]) * (q[0] - p[0]) < 0)
			return 0;
	}

	return 1;
}

struct Plane
{
	int16_t		surface, a, b, c;
	int32_t		d;

	bool operator<(const Plane& o) const
	{
		return std::make_tuple(surface, a, b, c, d) < std::make_tuple(o.surface, o.a, o.b, o.c, o.d);
	}

	bool operator==(const Plane& o) const
	{
		return !(*this < o) && !(o < *this);
	}
};

static_assert(sizeof(Plane) == 12, "sdPlane");

// the engine's plane height: Game/C/dr2roads.c sdHeightOnPlane (x, z relative to the region window start)
int PlaneHeight(const Plane& p, int x, int z)
{
	if ((p.d >> 1 ^ p.d) & 0x40000000)
		return p.d ^ 0x40000000;

	if (p.b == 0x4000)
		return -p.d;

	if (p.b != 0)
		return -p.d - (p.a * x + p.c * z) / p.b;

	return 0;
}

} // namespace

// ---------------------------------------------------------------- Driver 1 oracle

bool D1HeightField::Init(const LevFile& d1, D1RoadMap&& roadMap, const D1ModelFlags& flags)
{
	m_map = std::move(roadMap);
	m_flags = flags;

	const LevLump* lump = d1.FindLump(LUMP_ROADSURF);
	if (!lump)
		return true;								// no slopes in this city

	Reader r(d1.Data(lump->offset), lump->size);
	int count = r.Get<int>();
	if (!r.Ok() || count < 0 || count > 4096)
		return false;

	m_surfaces.resize(1536);

	for (int i = 0; i < count; i++)
	{
		short type = r.Get<short>();
		r.Skip(4);									// info, heading
		short numPolys = r.Get<short>();

		if (!r.Ok() || type < 0 || type >= (int)m_surfaces.size() || numPolys < 0)
			return false;

		std::vector<D1SurfacePoly>& polys = m_surfaces[type];
		polys.resize(numPolys);

		for (D1SurfacePoly& poly : polys)
		{
			r.Read(poly.xz, sizeof(poly.xz));
			poly.a = r.Get<int>();
			poly.b = r.Get<int>();
			poly.c = r.Get<int>();
			poly.d = r.Get<int>();
			poly.numVertices = r.Get<short>();
			r.Skip(2);								// natural alignment of the original struct (52 bytes)
		}
	}

	return r.Ok();
}

// A pavement cell lies a kerb higher than its road neighbour and the map steps up at the cell edge: a wall the wheels cannot climb
// (the car stops at the kerb, "invisible wall"). Next to a road cell the pavement height is brought down to the road over kKerbRamp.
// y is in cell object sign (negative = up); a step of kMinKerb..kMaxKerb is a kerb, higher ones are walls or ramps.
static int KerbRamp(const D1RoadMap& map, int cx, int cz, int vx, int vz, int y)
{
	const int kKerbRamp = 150, kMinKerb = 4;
	const int lx = FloorMod(map.unitXMid + vx, kRoadCell);
	const int lz = FloorMod(map.unitZMid - (vz - 750), kRoadCell);
	const int nx[4] = { -1, 1, 0, 0 }, nz[4] = { 0, 0, -1, 1 };
	const int dist[4] = { lx, kRoadCell - lx, lz, kRoadCell - lz };
	int nearest = kKerbRamp, roadY = y;

	for (int i = 0; i < 4; i++)
	{
		const int x = cx + nx[i], z = cz + nz[i];

		if (x < 0 || z < 0 || x >= map.width || z >= map.height || dist[i] >= nearest)
			continue;

		const size_t index = (size_t)x + (size_t)z * map.width;
		if (!map.present[index] || map.ids[index] >= 0x8000)
			continue;

		const int neighbourY = (int16_t)(map.values[index] & 0xFFFF);
		if (y - neighbourY <= -kMinKerb && y - neighbourY >= -kMaxKerb)
		{
			nearest = dist[i];
			roadY = neighbourY;
		}
	}

	return roadY + (y - roadY) * nearest / kKerbRamp;
}

bool D1HeightField::Eval(int vx, int vz, D1Ground& out) const
{
	const int cx = FloorDiv(m_map.unitXMid + vx, kRoadCell);
	const int cz = FloorDiv(m_map.unitZMid - (vz - 750), kRoadCell);

	if (cx < 0 || cz < 0 || cx >= m_map.width || cz >= m_map.height)
		return false;

	const size_t index = (size_t)cx + (size_t)cz * m_map.width;
	if (!m_map.present[index])
		return false;

	const uint32_t value = m_map.values[index];
	const int height = (int16_t)(value & 0xFFFF);
	const int type = (value >> 16) & 1023;
	const int angle = (int)(value >> 30) * 1024;

	// the road map heights use the same sign as the cell object positions (negative = up): checked against the tile meshes
	out.y = height;
	out.surface = 0;
	out.slope = false;

	if (type < 900)
	{
		if (type < (int)m_surfaces.size() && !m_surfaces[type].empty())
		{
			out.slope = true;
			int py = FloorMod(m_map.unitXMid + vx, kRoadCell) - 750;
			int px = FloorMod(m_map.unitZMid - (vz - 750), kRoadCell) - 750;

			if ((angle & 0x400) == 0)
			{
				int tmp = -px;
				px = py;
				py = tmp;
			}

			if (angle & 0x800)
			{
				px = -px;
				py = -py;
			}

			for (const D1SurfacePoly& poly : m_surfaces[type])
			{
				int inside = poly.numVertices == 4 ? PointInQuad(px, py, poly.xz) : PointInTri(px, py, poly.xz);
				if (inside)
					out.y = height + poly.d - (px * poly.a + py * poly.c) / 4096;
			}
		}

		if (type < (int)m_flags.shape.size())
		{
			if (m_flags.shape[type] & kFlagWater)
				out.surface = kSurfaceWater;
			else if (m_flags.flags2[type] & kFlagGrass)
				out.surface = kSurfaceGrass;

			// pavement tiles stand above the road map height; the car must ride on the visible top, not sink into it
			if ((m_flags.flags2[type] & kFlagPath) && m_flags.topY[type] >= -kMaxKerb)
				out.y += m_flags.topY[type];
		}
	}

	// D1 road ids: 0x0xxx road, 0x4xxx junction, 0x8000 pavement. D2 surface = 32 + straight index, 32 + 0x2000 + junction id
	const unsigned id = m_map.ids[index];

	// sea drawn by water models only (Miami, New York): no tile, no road; the map heights under it are ramp heights of nearby bridges
	if (!m_sea.empty() && m_sea[index] && type == 0 && id >= 0x8000)
	{
		out.y = 0;
		out.surface = kSurfaceWater;
		out.slope = false;
	}

	if (id < 0x4000)
		out.surface = 32 + (m_roads ? std::max(0, m_roads->StraightIndex(id, cx, cz)) : (int)id);
	else if ((id & 0xC000) == 0x4000)
		out.surface = 32 + 0x2000 + (id & 0x3FFF);
	else if (id == 0x8000 && out.surface != kSurfaceWater)
	{
		out.surface = kSurfacePavement;
		out.y = KerbRamp(m_map, cx, cz, vx, vz, out.y);
	}

	// the engine's MapHeight is positive = up (pedest.c: position.vy = -98 - MapHeight); the code above works in cell object sign
	out.y = -out.y;

	if (out.surface == kSurfaceWater)
		out.y -= kWaterDepth;

	return true;
}

// ---------------------------------------------------------------- Driver 2 height map writer

namespace
{

// planes are quantised so regions need fewer of them (16 bit offsets: about 2040 planes); coarser steps are tried per region
const int kQuantSteps[][2] = { { 4, 32 }, { 8, 64 }, { 16, 128 }, { 32, 256 } };
const int kTolerance = 20;							// roads (surface >= 32); other ground may be coarser
const int kTerrainTolerance = 48;

int ToleranceFor(int surface, int scale = 1)
{
	return (surface >= 32 || surface == kSurfacePavement ? kTolerance : kTerrainTolerance) * scale;
}
							// a leaf is split further while its fit error is above this
const int kMinSplit = 256;							// ... and wider than this
const int kMaxDepth = 6;							// the 8 bit node offset allows 126 nodes in the first subtree

struct Leaf
{
	Plane	plane;
	int		error;
	bool	uniform;							// all samples have the same surface
};

// fits one rectangle (world coordinates, hi exclusive) of a D2 height cell with a plane
Leaf FitLeaf(const D1HeightField& field, int regionX0, int regionZ0, int xLo, int xHi, int zLo, int zHi, int heightStep, int slopeStep, HeightMapStats& stats)
{
	int sx[25], sz[25], sy[25], ss[25];
	int surface = -1, count = 0;

	// samples stay 24 units inside the rectangle: the D1 tile edges carry a sliver of wrong heights
	const int xIn = xLo + std::min(24, (xHi - xLo) / 4), zIn = zLo + std::min(24, (zHi - zLo) / 4);

	// 3x3 samples, 5x5 when a sample lies on a tile with slope polygons
	for (int n = 3; n <= 5; n += 2)
	{
		count = 0;
		surface = -1;
		bool slope = false;

		for (int j = 0; j < n; j++)
		{
			for (int i = 0; i < n; i++)
			{
				const int x = xIn + (xHi - 1 - xIn - xIn + xLo) * i / (n - 1);
				const int z = zIn + (zHi - 1 - zIn - zIn + zLo) * j / (n - 1);

				D1Ground g;
				if (!field.Eval(x, z, g))
					continue;

				sx[count] = x - regionX0;
				sz[count] = z - regionZ0;
				sy[count] = g.y;
				ss[count] = g.surface;
				slope |= g.slope;

				if (surface < 0 || (i == n / 2 && j == n / 2))
					surface = g.surface;

				count++;
			}
		}

		if (!slope)
			break;
	}

	Leaf leaf{};
	leaf.plane.b = 0x4000;
	leaf.uniform = true;

	if (count == 0)
		return leaf;								// outside the road map: flat ground at 0

	leaf.plane.surface = (int16_t)surface;
	for (int i = 0; i < count; i++)
		leaf.uniform &= ss[i] == surface;

	int minY = sy[0], maxY = sy[0];
	double meanX = 0, meanZ = 0, meanY = 0;
	for (int i = 0; i < count; i++)
	{
		minY = std::min(minY, sy[i]);
		maxY = std::max(maxY, sy[i]);
		meanX += sx[i];
		meanZ += sz[i];
		meanY += sy[i];
	}

	meanX /= count;
	meanZ /= count;
	meanY /= count;

	if (surface < 32)
	{
		heightStep *= 2;
		slopeStep *= 2;
	}

	if (maxY - minY <= std::max(2, heightStep))
	{
		const int flat = (int)std::lround(meanY / heightStep) * heightStep;
		leaf.plane.d = -flat;
		leaf.error = std::max(std::abs(maxY - flat), std::abs(minY - flat));
		return leaf;
	}

	// least squares slopes
	double sxx = 0, szz = 0, sxz = 0, sxy = 0, szy = 0;
	for (int i = 0; i < count; i++)
	{
		double dx = sx[i] - meanX, dz = sz[i] - meanZ, dy = sy[i] - meanY;
		sxx += dx * dx;
		szz += dz * dz;
		sxz += dx * dz;
		sxy += dx * dy;
		szy += dz * dy;
	}

	double det = sxx * szz - sxz * sxz;
	double slopeX = 0, slopeZ = 0;

	if (det > 1e-6)
	{
		slopeX = (sxy * szz - szy * sxz) / det;
		slopeZ = (szy * sxx - sxy * sxz) / det;
	}
	else if (sxx > 1e-6)
		slopeX = sxy / sxx;
	else if (szz > 1e-6)
		slopeZ = szy / szz;

	// the engine uses (a, b, c) >> 2 as the surface normal, so it must have length 16384 (a smaller one weakens the wheel suspension);
	// b == 0x4000 means a flat plane to the engine, so a slope keeps b below it.
	// The slope is quantised first so equal slopes give equal planes (the plane budget per region is limited).
	const double step = slopeStep / 4096.0;
	slopeX = std::round(slopeX / step) * step;
	slopeZ = std::round(slopeZ / step) * step;

	const double norm = std::sqrt(1 + slopeX * slopeX + slopeZ * slopeZ);
	const int planeB = std::max(1, std::min(16383, (int)std::lround(16384 / norm)));

	if (norm > 8)
		stats.steepPlanes++;

	leaf.plane.a = (int16_t)std::lround(-slopeX * planeB);
	leaf.plane.b = planeB;
	leaf.plane.c = (int16_t)std::lround(-slopeZ * planeB);

	// y = -d - (a x + c z) / b: choose d so the mean residual is zero
	double dSum = 0;
	for (int i = 0; i < count; i++)
		dSum += -sy[i] - (leaf.plane.a * sx[i] + leaf.plane.c * sz[i]) / (double)planeB;

	leaf.plane.d = (int32_t)std::lround(dSum / count / heightStep) * heightStep;

	for (int i = 0; i < count; i++)
		leaf.error = std::max(leaf.error, std::abs(PlaneHeight(leaf.plane, sx[i], sz[i]) - sy[i]));

	return leaf;
}

int32_t SplitNode(bool axisX, int dist, int offset)
{
	// angle -1024 splits on x (dot = x * 4096), angle 0 on z (dot = z * 4096); first child = lower side
	uint32_t angle = axisX ? (uint32_t)(-1024) & 0x7FF : 0;
	return (int32_t)(angle | ((uint32_t)dist & 0xFFF) << 11 | ((uint32_t)offset & 0xFF) << 23 | 0x80000000u);
}

class PlaneTable
{
public:
	int Add(const Plane& plane)
	{
		auto it = m_index.find(plane);
		if (it != m_index.end())
			return it->second;

		m_planes.push_back(plane);
		m_index[plane] = (int)m_planes.size() - 1;
		return (int)m_planes.size() - 1;
	}

	const std::vector<Plane>& Planes() const { return m_planes; }

private:
	std::vector<Plane>			m_planes;
	std::map<Plane, int>		m_index;
};

// BSP of one height cell: a leaf (plane index) or an axis split with a lower and an upper child
struct TreeNode
{
	int		plane{ -1 };							// leaf when >= 0
	bool	axisX{ false };
	int		dist{ 0 };
	int		child[2]{ -1, -1 };
};

struct CellBuilder
{
	const D1HeightField&	field;
	PlaneTable&				planes;
	HeightMapStats&			stats;
	int						regionX0, regionZ0;		// world position of the region window
	int						cellX, cellZ;			// world position of the cell origin
	int						splitX, splitZ;			// local D1 road map lines inside the cell, -1 = none
	int						heightStep, slopeStep;
	int						tolScale{ 1 };			// raised when the node index space is almost full
	std::vector<TreeNode>	pool;

	// the plane must also match the ground right next to the D1 tile lines (3x3 samples can miss a narrow road strip)
	bool Covers(const Leaf& leaf, int xLo, int xHi, int zLo, int zHi) const
	{
		int xs[4] = { xLo + 24, xHi - 25, xLo + 24, xHi - 25 }, zs[4] = { zLo + 24, zHi - 25, zLo + 24, zHi - 25 };
		int nx = 2, nz = 2;

		if (splitX > xLo && splitX < xHi)
		{
			xs[nx++] = std::max(xLo, splitX - 24);
			xs[nx++] = std::min(xHi - 1, splitX + 24);
		}

		if (splitZ > zLo && splitZ < zHi)
		{
			zs[nz++] = std::max(zLo, splitZ - 24);
			zs[nz++] = std::min(zHi - 1, splitZ + 24);
		}

		for (int j = 0; j < nz; j++)
		{
			for (int i = 0; i < nx; i++)
			{
				D1Ground g;
				if (!field.Eval(cellX + xs[i], cellZ + zs[j], g))
					continue;

				if (g.surface != leaf.plane.surface ||
					std::abs(PlaneHeight(leaf.plane, cellX + xs[i] - regionX0, cellZ + zs[j] - regionZ0) - g.y) > ToleranceFor(g.surface, tolScale))
					return false;
			}
		}

		return true;
	}

	int NewLeaf(const Leaf& leaf)
	{
		stats.maxFitError = std::max(stats.maxFitError, leaf.error);
		TreeNode node;
		node.plane = planes.Add(leaf.plane);
		pool.push_back(node);
		return (int)pool.size() - 1;
	}

	int Build(int xLo, int xHi, int zLo, int zHi, int depth)
	{
		bool axisX;
		int dist;

		// one plane for the whole rectangle when it fits and has one surface (even across D1 road map cells)
		const bool crosses = (splitX > xLo && splitX < xHi) || (splitZ > zLo && splitZ < zHi);
		if (crosses)
		{
			Leaf whole = FitLeaf(field, regionX0, regionZ0, cellX + xLo, cellX + xHi, cellZ + zLo, cellZ + zHi, heightStep, slopeStep, stats);
			if (whole.uniform && whole.error <= ToleranceFor(whole.plane.surface, tolScale) && Covers(whole, xLo, xHi, zLo, zHi))
				return NewLeaf(whole);
		}

		if (splitX > xLo && splitX < xHi)
		{
			axisX = true;
			dist = splitX;
		}
		else if (splitZ > zLo && splitZ < zHi)
		{
			axisX = false;
			dist = splitZ;
		}
		else
		{
			Leaf leaf = FitLeaf(field, regionX0, regionZ0, cellX + xLo, cellX + xHi, cellZ + zLo, cellZ + zHi, heightStep, slopeStep, stats);

			if (leaf.error <= ToleranceFor(leaf.plane.surface, tolScale) || depth >= kMaxDepth || std::max(xHi - xLo, zHi - zLo) < kMinSplit)
				return NewLeaf(leaf);

			axisX = xHi - xLo >= zHi - zLo;
			dist = axisX ? (xLo + xHi) / 2 : (zLo + zHi) / 2;
		}

		const int low = axisX ? Build(xLo, dist, zLo, zHi, depth + 1) : Build(xLo, xHi, zLo, dist, depth + 1);
		const int high = axisX ? Build(dist, xHi, zLo, zHi, depth + 1) : Build(xLo, xHi, dist, zHi, depth + 1);

		// two leaves with the same plane need no split
		if (pool[low].plane >= 0 && pool[high].plane >= 0 && pool[low].plane == pool[high].plane)
			return low;

		TreeNode node;
		node.axisX = axisX;
		node.dist = dist;
		node.child[0] = low;
		node.child[1] = high;
		pool.push_back(node);
		return (int)pool.size() - 1;
	}

	int Count(int index) const
	{
		const TreeNode& node = pool[index];
		return node.plane >= 0 ? 1 : 1 + Count(node.child[0]) + Count(node.child[1]);
	}

	void Emit(int index, std::vector<int32_t>& nodes) const
	{
		const TreeNode& node = pool[index];

		if (node.plane >= 0)
		{
			nodes.push_back(node.plane);
			return;
		}

		const size_t at = nodes.size();
		nodes.push_back(0);
		Emit(node.child[0], nodes);
		nodes[at] = SplitNode(node.axisX, node.dist, (int)(nodes.size() - at));
		Emit(node.child[1], nodes);
	}
};

} // namespace

namespace
{

std::vector<char> BuildRoadBlockQ(const D1HeightField& field, int regionX, int regionZ, int regionsAcross, int regionsDown, int heightStep, int slopeStep, HeightMapStats& stats)
{
	const int x0 = (regionX - regionsAcross / 2) * kRegionUnits;
	const int z0 = (regionZ - regionsDown / 2) * kRegionUnits;
	const D1RoadMap& map = field.RoadMap();

	PlaneTable planes;
	std::vector<int16_t> grid(kGridCells * kGridCells, 0);
	std::vector<int32_t> nodes;

	for (int cz = 0; cz < kGridCells; cz++)
	{
		for (int cx = 0; cx < kGridCells; cx++)
		{
			CellBuilder b{ field, planes, stats, x0, z0, x0 + kGridOffset + cx * kGridUnit, z0 + kGridOffset + cz * kGridUnit, -1, -1, heightStep, slopeStep, 1, {} };

			// lines where the D1 road map cell changes inside this height cell
			const int splitX = kRoadCell - FloorMod(map.unitXMid + b.cellX, kRoadCell);
			const int splitZ = FloorMod(map.unitZMid + 750 - b.cellZ, kRoadCell) + 1;
			b.splitX = splitX < kGridUnit ? splitX : -1;
			b.splitZ = splitZ < kGridUnit ? splitZ : -1;

			int root = b.Build(0, kGridUnit, 0, kGridUnit, 0);
			stats.cells++;

			// node index space almost full: retry the cell with a looser tolerance, finally with a single plane
			for (int scale = 2; scale <= 16 && b.pool[root].plane < 0 && nodes.size() + b.Count(root) > (size_t)kMaxNodes; scale *= 2)
			{
				b.pool.clear();
				b.tolScale = scale;
				root = b.Build(0, kGridUnit, 0, kGridUnit, 0);
				stats.overflowCells++;
			}

			int16_t& entry = grid[cx + cz * kGridCells];

			if (b.pool[root].plane >= 0)
			{
				entry = (int16_t)b.pool[root].plane;
				continue;
			}

			stats.splitCells++;
			const int count = b.Count(root);

			if (nodes.size() + count > (size_t)kMaxNodes)
			{
				// still too big: one plane for the whole cell
				b.pool.clear();
				b.splitX = b.splitZ = -1;
				b.tolScale = 1 << 20;
				root = b.Build(0, kGridUnit, 0, kGridUnit, 0);
				entry = (int16_t)b.pool[root].plane;
				continue;
			}

			entry = (int16_t)(0x4000 | nodes.size());
			b.Emit(root, nodes);
		}
	}

	if ((int)planes.Planes().size() > kMaxPlanes)
	{
		stats.planes = -(int)planes.Planes().size();
		return std::vector<char>();					// caller reports the error
	}

	stats.planes += (int)planes.Planes().size();
	stats.nodes += (int)nodes.size();

	std::vector<char> block;
	Put<int>(block, kPvsHeaderSize);
	block.resize(block.size() + kPvsHeaderSize, 0);

	// header: 2, byte offsets of the planes, the BSP lists (unused) and the nodes, then the 64x64 grid
	const int planeOfs = kHeaderBytes;
	const int listOfs = planeOfs + (int)planes.Planes().size() * (int)sizeof(Plane);
	const int nodeOfs = listOfs + 4;

	std::vector<int16_t> head(4, 0);
	head[0] = 2;
	head[1] = (int16_t)planeOfs;
	head[2] = (int16_t)listOfs;
	head[3] = (int16_t)nodeOfs;
	Put(block, head.data(), head.size() * 2);
	Put(block, grid.data(), grid.size() * 2);

	for (const Plane& plane : planes.Planes())
		Put(block, plane);

	Put<int>(block, 0);
	Put(block, nodes.data(), nodes.size() * 4);
	return block;
}

} // namespace

std::vector<char> BuildRoadBlock(const D1HeightField& field, int regionX, int regionZ, int regionsAcross, int regionsDown, HeightMapStats& stats)
{
	int needed = 0;

	for (const int* step : kQuantSteps)
	{
		HeightMapStats attempt;
		std::vector<char> block = BuildRoadBlockQ(field, regionX, regionZ, regionsAcross, regionsDown, step[0], step[1], attempt);

		if (!block.empty())
		{
			stats.cells += attempt.cells;
			stats.splitCells += attempt.splitCells;
			stats.planes += attempt.planes;
			stats.nodes += attempt.nodes;
			stats.overflowCells += attempt.overflowCells;
			stats.steepPlanes += attempt.steepPlanes;
			stats.maxFitError = std::max(stats.maxFitError, attempt.maxFitError);
			stats.coarseRegions += step != kQuantSteps[0];
			return block;
		}

		needed = -attempt.planes;
	}

	stats.planes = -needed;
	return std::vector<char>();
}

// ---------------------------------------------------------------- engine side reader (verification)

bool SampleRoadBlock(const char* block, int blockSize, int localX, int localZ, D1Ground& out)
{
	if (blockSize < 4 + kPvsHeaderSize + kHeaderBytes)
		return false;

	const char* buffer = block + 4 + *(const int*)block;
	const int16_t* head = (const int16_t*)buffer;

	if (head[0] != 2)
		return false;

	const Plane* planes = (const Plane*)(buffer + head[1]);
	const int32_t* nodes = (const int32_t*)(buffer + head[3]);

	const int px = localX - kGridOffset, pz = localZ - kGridOffset;
	int surface = head[4 + (px >> 10 & 63) + (pz >> 10 & 63) * kGridCells];
	const int cellX = px & 1023, cellZ = pz & 1023;

	if (surface == -1 || (surface & 0x6000) == 0x2000)
		return false;

	int planeIndex = surface;

	if (surface & 0x4000)
	{
		const int32_t* node = nodes + (surface & 0x3FFF);
		int guard = 0;

		while (*node < 0 && guard++ < 16)
		{
			int angle = (*node << 21) >> 21;				// 11 bit signed
			int dist = (*node << 9) >> 20;					// 12 bit signed
			int offset = (*node << 1) >> 24;				// 8 bit signed
			int cos = 0, sin = 0;

			switch (angle & 4095)
			{
			case 0: cos = 4096; break;
			case 1024: sin = 4096; break;
			case 2048: cos = -4096; break;
			case 3072: sin = -4096; break;
			default: return false;
			}

			int dot = cellZ * cos - cellX * sin;
			node += dot < dist * 4096 ? 1 : offset;
		}

		planeIndex = *node & 0xFFFF;
	}

	const Plane& plane = planes[planeIndex];
	out.y = PlaneHeight(plane, ((px & 0xFFFF) + kGridOffset), ((pz & 0xFFFF) + kGridOffset));
	out.surface = plane.surface;
	return true;
}
