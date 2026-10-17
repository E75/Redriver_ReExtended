#ifndef D1_HEIGHT_H
#define D1_HEIGHT_H

#include "lev_file.h"

#include <stdint.h>
#include <vector>

// Driver 1 ground height + surface oracle (port of OpenDriverEngine CDriver1LevelMap::FindSurface) and the
// Driver 2 height map writer / reader. See plans/driver1-d2-differences.md section 9.

// road map of the whole city, 1500 unit cells, assembled from the 60x60 cell blocks of the D1 regions
struct D1RoadMap
{
	int						width{ 0 }, height{ 0 };
	int						unitXMid{ 0 }, unitZMid{ 0 };
	std::vector<uint16_t>	ids;				// road id per cell, 0xFFFF = none
	std::vector<uint32_t>	values;				// low 16 bits height, bits 16..25 road tile model, bits 30..31 rotation
	std::vector<uint8_t>	present;			// 1 where a D1 region delivered the cell
};

// slope polygons of the ROADSURF lump
struct D1SurfacePoly
{
	int		xz[4][2];
	int		a, b, c, d;
	int		numVertices;
};

struct D1ModelFlags
{
	std::vector<int>	shape;					// shape_flags per model number
	std::vector<int>	flags2;
	std::vector<int>	topY;					// highest vertex per model number (ModelTopY)
};

struct D1Ground
{
	int		y;									// engine MapHeight convention: positive = up
	int		surface;							// sdPlane.surface: SurfaceType < 32, or 32 + road / junction surface id
	bool	slope{ false };						// the road tile has slope polygons (height varies inside it)
};

class D1RoadTable;

class D1HeightField
{
public:
	bool	Init(const LevFile& d1, D1RoadMap&& roadMap, const D1ModelFlags& flags);

	// maps D1 road ids to D2 straight indices (long roads are split); without it the road id is the straight index
	void	SetRoads(const D1RoadTable* roads) { m_roads = roads; }

	// false where the city has no road map data
	bool	Eval(int vx, int vz, D1Ground& out) const;

	const D1RoadMap& RoadMap() const { return m_map; }

	// road map cells (same index as D1RoadMap) that lie under a flat water model: sea without a water marker in the road map
	void	SetSeaCells(std::vector<uint8_t>&& cells) { m_sea = std::move(cells); }

private:
	std::vector<uint8_t>				m_sea;
	D1RoadMap							m_map;
	D1ModelFlags						m_flags;
	const D1RoadTable*					m_roads{ nullptr };
	std::vector<std::vector<D1SurfacePoly>> m_surfaces;		// by road tile model number
};

struct HeightMapStats
{
	int		cells{ 0 }, splitCells{ 0 }, planes{ 0 }, nodes{ 0 }, overflowCells{ 0 };
	int		maxFitError{ 0 };
	int		steepPlanes{ 0 };
	int		coarseRegions{ 0 };					// regions that needed coarser plane quantisation
};

// Road block of one D2 object region (regionX, regionZ counted from the grid corner): int offset, PVS (everything
// visible), height map. regionsAcross / regionsDown are the grid size in regions (even).
std::vector<char> BuildRoadBlock(const D1HeightField& field, int regionX, int regionZ, int regionsAcross, int regionsDown, HeightMapStats& stats);

// engine side lookup (port of sdGetCell + sdHeightOnPlane) on a road block, for the verification
bool SampleRoadBlock(const char* block, int blockSize, int localX, int localZ, D1Ground& out);

#endif // D1_HEIGHT_H
