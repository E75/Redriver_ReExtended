#include "d1_roads.h"
#include "d1_util.h"

#include <algorithm>
#include <limits.h>

namespace
{

const int kRoadCell = 1500;							// D1 road map cell
const int kMaxPieceCells = 40;						// D2 straight length is a ushort: 40 cells = 60000 units
const int kJunctionFlags = 2;						// DRIVER2_JUNCTION.flags: 1 = traffic lights, 2 = priority rule
const int kLightRadius = 4500;						// a TLIGHT model belongs to the junction square whose centre is nearest, within this distance
const int kMinLights = 1;							// TLIGHT models needed near a junction for traffic lights: in Miami the D1 junction type 1 (148) is exactly the junctions with 1 or more (146), none without; 3 or more found only 68
const int kLeadExitWeights = 0x02020200;				// DRIVER2_JUNCTION.flags bytes 1..3: weight of exits 1..3 for the lead car AI (leadai.c SelectExit; byte 0 is shared with the traffic flags, weight 1 or 2); civ cars only read bits 0 and 1
const int kJunctionLights = 1;						// DRIVER2_JUNCTION.flags: civ cars wait for the global light phases (civ_ai.c TrafficLightCycle)
const int kJunctionSurface = 0x2000;				// ConnectIdx / surface id offset of junctions

struct CellBox
{
	int x0{ INT_MAX }, x1{ -1 }, z0{ INT_MAX }, z1{ -1 };

	bool Valid() const { return x1 >= 0; }
};

template<typename T>
bool ReadTable(const LevFile& d1, int lumpType, std::vector<T>& out)
{
	const LevLump* lump = d1.FindLump(lumpType);
	if (!lump || lump->size < 4)
		return false;

	Reader r(d1.Data(lump->offset), lump->size);
	int count = r.Get<int>();
	if (count < 0 || count > 8000 || lump->size < 4 + count * (int)sizeof(T))
		return false;

	out.resize(count);
	r.Read(out.data(), count * (int)sizeof(T));
	return r.Ok();
}

// D2 lane j of the straight is lane lanes - 1 - j of the D1 road (lane 0 = right of the D1 forward direction, which is
// the direction of falling world x and z: both D2 angles, 1024 and 0, point against it, so order and direction bits reverse.
// Checked on the one way carriageway pairs of the four cities: with forward = rising x every pair along x was left hand traffic.
int MapLaneDirs(int d1Dirs, int lanes)
{
	int dirs = 0;

	for (int j = 0; j < lanes; j++)
	{
		int source = lanes - 1 - j;
		int bit = (d1Dirs >> source & 1) ^ 1;
		dirs |= bit << j;
	}

	return dirs;
}

} // namespace

int D1RoadTable::PieceOf(const Road& road, int k) const
{
	const int bigCells = road.extra * (road.base + 1);

	if (k < bigCells)
		return k / (road.base + 1);

	return road.extra + (k - bigCells) / road.base;
}

int D1RoadTable::StraightIndex(int roadId, int cellX, int cellZ) const
{
	if (roadId < 0 || roadId >= (int)m_roads.size())
		return -1;

	const Road& road = m_roads[roadId];
	int k = road.axisX ? cellX - road.lo : road.hi - cellZ;				// along the D2 angle: +x, or +z = falling road map z
	k = std::max(0, std::min(road.hi - road.lo, k));

	return road.first + PieceOf(road, k);
}

bool D1RoadTable::Build(const LevFile& d1, const D1RoadMap& map, std::string& log)
{
	std::vector<DRIVER1_ROAD> d1Roads;
	std::vector<DRIVER1_JUNCTION> d1Juncs;
	std::vector<DRIVER1_ROADBOUNDS> bounds;

	if (!ReadTable(d1, LUMP_ROADS, d1Roads) || !ReadTable(d1, LUMP_JUNCTIONS, d1Juncs) || !ReadTable(d1, LUMP_ROADBOUNDS, bounds) ||
		bounds.size() != d1Roads.size())
	{
		Log(log, "ERROR: ROADS / JUNCTIONS / ROADBOUNDS lumps are missing or inconsistent");
		return false;
	}

	// junction squares as cell boxes, taken from the road map
	std::vector<CellBox> boxes(d1Juncs.size());

	for (int cz = 0; cz < map.height; cz++)
	{
		for (int cx = 0; cx < map.width; cx++)
		{
			unsigned id = map.ids[(size_t)cx + (size_t)cz * map.width];
			if ((id & 0xC000) != 0x4000 || (int)(id & 0x3FFF) >= (int)boxes.size())
				continue;

			CellBox& box = boxes[id & 0x3FFF];
			box.x0 = std::min(box.x0, cx);
			box.x1 = std::max(box.x1, cx);
			box.z0 = std::min(box.z0, cz);
			box.z1 = std::max(box.z1, cz);
		}
	}

	// traffic light models per junction square: every light counts for the nearest junction centre within kLightRadius
	std::vector<int> lightCount(boxes.size(), 0);
	int assignedLights = 0;

	for (const std::pair<int, int>& light : m_lights)
	{
		int best = -1;
		long long bestDist = (long long)kLightRadius * kLightRadius;

		for (int j = 0; j < (int)boxes.size(); j++)
		{
			if (!boxes[j].Valid())
				continue;

			const long long dx = (boxes[j].x0 + boxes[j].x1 + 1) * 750 - map.unitXMid - light.first;
			const long long dz = map.unitZMid - (boxes[j].z0 + boxes[j].z1) * 750 - light.second;

			if (dx * dx + dz * dz < bestDist)
			{
				bestDist = dx * dx + dz * dz;
				best = j;
			}
		}

		if (best >= 0)
		{
			lightCount[best]++;
			assignedLights++;
		}
	}

	int splitRoads = 0, sideConflicts = 0, noJunctionBox = 0, leftHandRoads = 0, parkingRoads = 0;
	m_roads.assign(d1Roads.size(), Road());
	m_straights.clear();

	for (int i = 0; i < (int)d1Roads.size(); i++)
	{
		const DRIVER1_ROAD& src = d1Roads[i];
		const DRIVER1_ROADBOUNDS& b = bounds[i];
		Road& road = m_roads[i];

		// ROADBOUNDS are relative to the middle of the road map
		const int x0 = std::min(b.x1, b.x2) + map.width / 2, x1 = std::max(b.x1, b.x2) + map.width / 2;
		const int z0 = std::min(b.y1, b.y2) + map.height / 2, z1 = std::max(b.y1, b.y2) + map.height / 2;

		road.axisX = b.dir == 1;
		road.lo = road.axisX ? x0 : z0;
		road.hi = road.axisX ? x1 : z1;
		road.crossLo = road.axisX ? z0 : x0;
		road.crossHi = road.axisX ? z1 : x1;

		// which junction sits at the low / high end (rising road map coordinate)
		int side[2] = { 0, 0 };

		for (int c = 0; c < 2; c++)
		{
			int j = src.ConnectIdx[c];
			if (j < 0 || j >= (int)boxes.size())
				continue;

			if (!boxes[j].Valid())
			{
				noJunctionBox++;
				continue;
			}

			int junctionMid = road.axisX ? boxes[j].x0 + boxes[j].x1 : boxes[j].z0 + boxes[j].z1;
			side[c] = junctionMid < road.lo + road.hi ? -1 : 1;
		}

		if (side[0] == 0 && side[1] == 0)
		{
			side[0] = -1;
			side[1] = 1;
		}
		else if (side[0] == 0)
		{
			side[0] = -side[1];
		}
		else if (side[1] == 0)
		{
			side[1] = -side[0];
		}
		else if (side[0] == side[1])
		{
			sideConflicts++;
			side[0] = -1;
			side[1] = 1;
		}

		const int lowJunction = side[0] < 0 ? src.ConnectIdx[0] : (side[1] < 0 ? src.ConnectIdx[1] : -1);
		const int highJunction = side[0] > 0 ? src.ConnectIdx[0] : (side[1] > 0 ? src.ConnectIdx[1] : -1);

		// the D2 angle is 1024 (+x) or 0 (+z); +z is falling road map z
		road.ahead = road.axisX ? highJunction : lowJunction;
		road.behind = road.axisX ? lowJunction : highJunction;

		const int cells = road.hi - road.lo + 1;
		road.pieces = (cells + kMaxPieceCells - 1) / kMaxPieceCells;
		road.base = cells / road.pieces;
		road.extra = cells % road.pieces;
		road.first = (int)m_straights.size();
		splitRoads += road.pieces > 1 ? 1 : 0;

		const int lanes = std::max(1, std::min(15, (int)src.NumLanes));
		// a two-way road whose right lane runs against the direction is left hand traffic: right hand is the default
		int d1Dirs = src.LaneDirs & 0xFF;

		if (lanes >= 2 && (d1Dirs & 1) && !(d1Dirs >> (lanes - 1) & 1))
		{
			d1Dirs ^= (1 << lanes) - 1;
			leftHandRoads++;
		}

		const int dirs = MapLaneDirs(d1Dirs, lanes);

		for (int p = 0; p < road.pieces; p++)
		{
			const int k0 = p < road.extra ? p * (road.base + 1) : road.extra * (road.base + 1) + (p - road.extra) * road.base;
			const int k1 = k0 + (p < road.extra ? road.base + 1 : road.base);

			DRIVER2_STRAIGHT s = {};
			int low, high;

			if (road.axisX)
			{
				low = (road.lo + k0) * kRoadCell - map.unitXMid;
				high = (road.lo + k1) * kRoadCell - map.unitXMid;
				s.Midx = (low + high) / 2;
				s.Midz = map.unitZMid - (road.crossLo + road.crossHi) * (kRoadCell / 2);
				s.angle = 1024;
			}
			else
			{
				low = map.unitZMid - (road.hi - k0) * kRoadCell - kRoadCell / 2;
				high = map.unitZMid - (road.hi - k1) * kRoadCell - kRoadCell / 2;
				s.Midz = (low + high) / 2;
				s.Midx = (road.crossLo + road.crossHi + 1) * (kRoadCell / 2) - map.unitXMid;
				s.angle = 0;
			}

			s.length = (uint16_t)(high - low);
			s.bing = (int16_t)(lanes * 1024);

			const int aheadId = p + 1 < road.pieces ? road.first + p + 1 : (road.ahead >= 0 ? kJunctionSurface + road.ahead : -1);
			const int behindId = p > 0 ? road.first + p - 1 : (road.behind >= 0 ? kJunctionSurface + road.behind : -1);
			s.ConnectIdx[0] = s.ConnectIdx[1] = (int16_t)aheadId;
			s.ConnectIdx[2] = s.ConnectIdx[3] = (int16_t)behindId;

			// D1 has no parking lanes; the D2 civilian spawner needs them for parked cars (civ_ai.c PingInCivCar): 0x40 = the
			// leftmost lane parks, 0x80 = the rightmost. Roads of 4 or more lanes keep two driving lanes, 3 lanes park on one side
			// only, narrower roads would be blocked by a parked car
			const int parking = lanes >= 4 ? 0xC0 : (lanes == 3 ? 0x80 : 0);
			if (parking)
				parkingRoads++;

			s.NumLanes = (int8_t)(lanes | parking);
			s.LaneDirs = (int8_t)dirs;
			s.AILanes = (int8_t)((1 << lanes) - 1);
			m_straights.push_back(s);
		}
	}

	// junctions: ExitIdx[k] = straight next to the junction in direction k (0 = +z, 1 = +x, 2 = -z, 3 = -x)
	int badExits = 0, wrongDirection = 0, signalled = 0;
	m_junctions.assign(d1Juncs.size(), DRIVER2_JUNCTION());

	for (int j = 0; j < (int)d1Juncs.size(); j++)
	{
		DRIVER2_JUNCTION& out = m_junctions[j];
		out.flags = kJunctionFlags;

		if (lightCount[j] >= kMinLights)
		{
			out.flags = kJunctionLights;
			signalled++;
		}

		out.flags |= kLeadExitWeights;

		for (int k = 0; k < 4; k++)
		{
			out.ExitIdx[k] = -1;

			const int roadId = d1Juncs[j].entries[k].road;
			if (roadId < 0)
				continue;

			if (roadId >= (int)m_roads.size())
			{
				badExits++;
				continue;
			}

			const Road& road = m_roads[roadId];

			if (road.ahead == j)
				out.ExitIdx[k] = (int16_t)(road.first + road.pieces - 1);
			else if (road.behind == j)
				out.ExitIdx[k] = (int16_t)road.first;
			else
			{
				badExits++;
				continue;
			}

			if (boxes[j].Valid())
			{
				const int roadX2 = road.axisX ? road.lo + road.hi : road.crossLo + road.crossHi;
				const int roadZ2 = road.axisX ? road.crossLo + road.crossHi : road.lo + road.hi;
				const int dx = roadX2 - (boxes[j].x0 + boxes[j].x1), dz = roadZ2 - (boxes[j].z0 + boxes[j].z1);
				const bool ok = k == 0 ? dz < 0 : k == 1 ? dx > 0 : k == 2 ? dz > 0 : dx < 0;
				wrongDirection += ok ? 0 : 1;
			}
		}
	}

	if ((int)m_straights.size() > 8000)
	{
		Log(log, "ERROR: %d straights do not fit the 13 bit road ids", (int)m_straights.size());
		return false;
	}

	Log(log, "roads: %d D1 roads -> %d straights (%d roads split), %d junctions; %d junction side conflicts, %d without map box, "
		"%d exits not touching their junction, %d exits in the wrong direction; %d junctions with traffic lights (%d TLIGHT objects, %d assigned to a junction), %d left hand roads flipped, %d with parking lanes",
		(int)d1Roads.size(), (int)m_straights.size(), splitRoads, (int)m_junctions.size(), sideConflicts, noJunctionBox, badExits, wrongDirection,
		signalled, (int)m_lights.size(), assignedLights, leftHandRoads, parkingRoads);
	return true;
}

std::vector<char> D1RoadTable::StraightsLump() const
{
	std::vector<char> lump;
	Put<int>(lump, (int)m_straights.size());
	Put(lump, m_straights.data(), m_straights.size() * sizeof(DRIVER2_STRAIGHT));
	return lump;
}

std::vector<char> D1RoadTable::JunctionsLump() const
{
	std::vector<char> lump;
	Put<int>(lump, (int)m_junctions.size());
	Put(lump, m_junctions.data(), m_junctions.size() * sizeof(DRIVER2_JUNCTION));
	return lump;
}

// ---------------------------------------------------------------- structure check

namespace
{

template<typename T>
bool LoadTable(const LevFile& lev, int lumpType, std::vector<T>& out)
{
	const LevLump* lump = lev.FindLump(lumpType);
	if (!lump || lump->size < 4)
		return false;

	int count = *(const int*)lev.Data(lump->offset);
	if (count < 0 || lump->size < 4 + count * (int)sizeof(T))
		return false;

	out.resize(count);
	memcpy(out.data(), lev.Data(lump->offset + 4), count * sizeof(T));
	return true;
}

bool Contains(const int16_t* ids, int count, int value)
{
	for (int i = 0; i < count; i++)
	{
		if (ids[i] == value)
			return true;
	}

	return false;
}

} // namespace

int D1Dlc_CheckRoads(const LevFile& lev, std::string& log)
{
	std::vector<DRIVER2_STRAIGHT> straights;
	std::vector<DRIVER2_JUNCTION> junctions;
	std::vector<DRIVER2_CURVE> curves;

	if (!LoadTable(lev, LUMP_STRAIGHTS2, straights) || !LoadTable(lev, LUMP_JUNCTIONS2_NEW, junctions) || !LoadTable(lev, LUMP_CURVES2, curves))
	{
		Log(log, "check: road lumps (STRAIGHTS2 / CURVES2 / JUNCTIONS2_NEW) missing or truncated");
		return 1;
	}

	const int numS = (int)straights.size(), numJ = (int)junctions.size();
	int problems = 0, reported = 0;

	auto report = [&](const char* what, int index, int value)
	{
		problems++;

		if (reported++ < 8)
			Log(log, "check: roads: %s (index %d, value %d)", what, index, value);
	};

	for (int s = 0; s < numS; s++)
	{
		const DRIVER2_STRAIGHT& st = straights[s];

		if ((st.NumLanes & 15) == 0 || st.AILanes == 0)
			report("straight without lanes", s, st.NumLanes);

		if (st.angle != 0 && st.angle != 1024)
			report("straight angle not 0 / 1024", s, st.angle);

		if (st.length == 0)
			report("straight of length 0", s, st.length);

		for (int c = 0; c < 4; c++)
		{
			const int v = st.ConnectIdx[c];

			if (v == -1)
				continue;

			if ((v & 0xE000) == kJunctionSurface)
			{
				const int j = v & 0x1FFF;

				if (j >= numJ)
					report("straight links to a missing junction", s, v);
				else if (!Contains(junctions[j].ExitIdx, 4, s))
					report("junction does not list the straight that links to it", s, v);
			}
			else if (v >= 0 && v < numS)
			{
				// [0] and [1] point ahead, [2] and [3] behind: the neighbour must point back with the other pair
				const DRIVER2_STRAIGHT& other = straights[v];
				const int16_t* back = c < 2 ? &other.ConnectIdx[2] : &other.ConnectIdx[0];

				if (!Contains(back, 2, s))
					report("straight link is not reciprocal", s, v);
			}
			else
			{
				report("straight link out of range", s, v);
			}
		}
	}

	for (int j = 0; j < numJ; j++)
	{
		for (int k = 0; k < 4; k++)
		{
			const int v = junctions[j].ExitIdx[k];

			if (v == -1)
				continue;

			if (v < 0 || v >= numS)
				report("junction exit out of range", j, v);
			else if (!Contains(straights[v].ConnectIdx, 4, kJunctionSurface + j))
				report("straight does not link back to the junction", j, v);
		}
	}

	if (problems > reported)
		Log(log, "check: roads: %d more problems not listed", problems - reported);

	return problems;
}
