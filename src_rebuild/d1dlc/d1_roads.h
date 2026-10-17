#ifndef D1_ROADS_H
#define D1_ROADS_H

#include "d1_height.h"
#include "lev_file.h"

#include <string>
#include <vector>

// Driver 1 road graph -> Driver 2 straights + junctions. D1 roads are axis aligned rectangles of road map cells between
// junction squares; every road becomes one or more straights (a straight is at most 61440 units long), every junction one
// DRIVER2_JUNCTION whose ExitIdx order (0 = +z, 1 = +x, 2 = -z, 3 = -x) equals the D1 entry order.
// See plans/driver1-d2-differences.md section 10.
class D1RoadTable
{
public:
	// world x / z of the traffic light models (TLIGHT*) of the city: a junction square with several of them nearby gets
	// traffic lights (D1 has no light flag the cities agree on). Call before Build.
	void SetLights(std::vector<std::pair<int, int>> lights) { m_lights = std::move(lights); }

	bool Build(const LevFile& d1, const D1RoadMap& map, std::string& log);

	// straight index of the piece of D1 road `roadId` that covers road map cell (cellX, cellZ); -1 for unknown roads
	int StraightIndex(int roadId, int cellX, int cellZ) const;

	const std::vector<DRIVER2_STRAIGHT>&	Straights() const { return m_straights; }
	const std::vector<DRIVER2_JUNCTION>&	Junctions() const { return m_junctions; }

	// lump bodies: int count + records
	std::vector<char>	StraightsLump() const;
	std::vector<char>	JunctionsLump() const;

private:
	struct Road
	{
		bool	axisX{ true };				// long side along the world x axis
		int		lo{ 0 }, hi{ 0 };			// road map cells along the long axis (inclusive)
		int		crossLo{ 0 }, crossHi{ 0 };	// road map cells across
		int		first{ 0 }, pieces{ 0 };	// first straight index, number of straights
		int		base{ 0 }, extra{ 0 };		// cells per piece (the first `extra` pieces have one more)
		int		ahead{ -1 }, behind{ -1 };	// junction ids at the end the D2 angle points to / away from
	};

	int PieceOf(const Road& road, int cellAlong) const;

	std::vector<Road>				m_roads;
	std::vector<DRIVER2_STRAIGHT>	m_straights;
	std::vector<DRIVER2_JUNCTION>	m_junctions;
	std::vector<std::pair<int, int>>	m_lights;
};

// Structure check of the STRAIGHTS2 / CURVES2 / JUNCTIONS2_NEW lumps of a Driver 2 format LEV (indices in range, links
// reciprocal). Returns the number of problems.
int D1Dlc_CheckRoads(const LevFile& lev, std::string& log);

#endif // D1_ROADS_H
