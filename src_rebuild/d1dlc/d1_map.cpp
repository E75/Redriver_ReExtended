#include "d1_height.h"
#include "d1_map.h"
#include "d1_roads.h"
#include "d1_util.h"

#include <algorithm>
#include <math.h>
#include <map>
#include <set>
#include <tuple>

namespace
{

const int kSector = LEV_SECTOR_SIZE;
const int kMaxCellIndex = 16383;					// CELL_DATA.num has 14 bits
const int kMaxRadius = 6144;						// cell coverage radius of an object is its bounding sphere, capped
const int kDefaultRadius = 1024;
const int kCellsPerRegion = D2_REGION_CELLS * D2_REGION_CELLS;
const int kEmptyOffset = 0xFFFF;
const int kLocalFlag = 1 << 24;						// cell reference to an object of the region itself (not a straddler)

int FloorDiv(int a, int b)
{
	return a >= 0 ? a / b : -((-a + b - 1) / b);
}

int SectorsFor(size_t bytes)
{
	return std::max(1, (int)((bytes + kSector - 1) / kSector));
}

// ---------------------------------------------------------------- Driver 1 input

struct D1Spool
{
	int								modelBufferSectors{ 0 };
	std::vector<AreaDataStr>		areas;
	std::vector<std::vector<uint8_t>> tpages;		// 16 per area
	int32_t							slots[4]{}, objects[4]{}, pvs[4]{};
	std::vector<uint16_t>			offsets;		// per region: byte offset into regionInfo, 0xFFFF = none
	std::vector<char>				regionInfo;		// Spool structs

	const Spool*	RegionSpool(int region) const
	{
		if (region < 0 || region >= (int)offsets.size() || offsets[region] == kEmptyOffset)
			return nullptr;

		if ((size_t)offsets[region] + sizeof(Spool) > regionInfo.size())
			return nullptr;

		return (const Spool*)&regionInfo[offsets[region]];
	}
};

// LUMP_SPOOLINFO has the same layout in Driver 1 and Driver 2
bool ParseSpoolInfo(const LevFile& lev, D1Spool& out)
{
	const LevLump* lump = lev.FindLump(LUMP_SPOOLINFO);
	if (!lump)
		return false;

	Reader r(lev.Data(lump->offset), lump->size);

	out.modelBufferSectors = r.Get<int>();
	r.Skip(r.Get<int>());							// music and ambient offsets (unused)

	int numAreas = r.Get<int>();
	if (!r.Ok() || numAreas < 0 || numAreas > 64)
		return false;

	out.areas.resize(numAreas);
	r.Read(out.areas.data(), numAreas * (int)sizeof(AreaDataStr));

	out.tpages.assign(numAreas, std::vector<uint8_t>(16));
	for (std::vector<uint8_t>& list : out.tpages)
		r.Read(list.data(), 16);

	// unused area slots hold junk (Miami area 3: 164 texture pages): keep the index, drop the data
	for (AreaDataStr& area : out.areas)
	{
		if (area.num_tpages > 16)
			area = AreaDataStr{};
	}

	r.Read(out.slots, sizeof(out.slots));
	r.Read(out.objects, sizeof(out.objects));
	r.Read(out.pvs, sizeof(out.pvs));

	int numOffsets = r.Get<int>();
	if (!r.Ok() || numOffsets < 0 || numOffsets > 65536)
		return false;

	out.offsets.resize(numOffsets);
	r.Read(out.offsets.data(), numOffsets * 2);

	int infoSize = r.Get<int>();
	if (!r.Ok() || infoSize < 0)
		return false;

	out.regionInfo.resize(infoSize);
	r.Read(out.regionInfo.data(), infoSize);
	return r.Ok();
}

struct D1Map
{
	OUT_CELL_FILE_HEADER			hdr{};
	std::vector<CELL_OBJECT_D1>		straddlers;
};

bool ParseMapLump(const LevFile& lev, bool driver1, D1Map& out)
{
	const LevLump* lump = lev.FindLump(LUMP_MAP);
	if (!lump)
		return false;

	Reader r(lev.Data(lump->offset), lump->size);
	r.Read(&out.hdr, sizeof(out.hdr));

	int count = r.Get<int>();
	if (!r.Ok() || count < 0 || count > 100000)
		return false;

	if (driver1)
	{
		out.straddlers.resize(count);
		r.Read(out.straddlers.data(), count * (int)sizeof(CELL_OBJECT_D1));
	}

	return r.Ok();
}

struct D1Models
{
	std::vector<int>				bounding;		// bounding sphere per model number, 0 = unknown
	std::vector<char>				resident;		// 1 = in the MODELS lump
	std::vector<int>				shape, flags2;	// shape_flags and flags2 per model number
	std::vector<int>				topY;			// highest vertex per model number (ModelTopY)
	std::vector<ModelStats>			stats;			// extent + horizontal polygon area per model number
	std::vector<std::set<int>>		areaModels;		// spooled model numbers per area
	std::vector<std::set<int>>		pages;			// texture pages used by the polygons of each model number
	std::vector<int>				boxReach;		// farthest collision box corner from the model origin, 0 = no boxes
};

// farthest corner of the model's collision boxes from its origin (block starts at the MODEL header)
int BoxReach(const char* block, int size)
{
	const MODEL& model = *(const MODEL*)block;
	if (model.collision_block <= 0 || model.collision_block + 4 > size)
		return 0;

	int count = *(const int*)(block + model.collision_block), reach = 0;
	const COLLISION_PACKET* p = (const COLLISION_PACKET*)(block + model.collision_block + 4);

	for (int i = 0; i < count && model.collision_block + 4 + (i + 1) * (int)sizeof(COLLISION_PACKET) <= size; i++)
	{
		double half = sqrt((double)p[i].xsize * p[i].xsize + (double)p[i].zsize * p[i].zsize) / 2;
		reach = std::max(reach, (int)(sqrt((double)p[i].xpos * p[i].xpos + (double)p[i].zpos * p[i].zpos) + half));
	}

	return reach;
}

// adds the texture pages of the textured polygons of one model (block starts at the MODEL header)
void CollectModelPages(const char* block, int size, std::set<int>& pages)
{
	const MODEL& model = *(const MODEL*)block;
	int ofs = model.poly_block;

	for (int i = 0; i < model.num_polys; i++)
	{
		if (ofs < 0 || ofs + 3 > size)
			return;

		int type = (unsigned char)block[ofs] & 31;
		if (kPolySizes[type] == 0)
			return;

		if (IsTexturedPoly(type) && (unsigned char)block[ofs + 1] != 0xFF)
			pages.insert((unsigned char)block[ofs + 1]);

		ofs += kPolySizes[type];
	}
}

// page numbers of the permanent pages (perm list of LUMP_TEXTUREINFO)
std::set<int> ReadPermanentPages(const LevFile& d1)
{
	std::set<int> perm;
	const LevLump* lump = d1.FindLump(LUMP_TEXTUREINFO);
	if (!lump)
		return perm;

	Reader r(d1.Data(lump->offset), lump->size);
	int pageCount = r.Get<int>();
	r.Get<int>();
	if (pageCount <= 0 || pageCount > 128)
		return perm;

	r.Skip((pageCount + 1) * (int)sizeof(TEXPAGE_POS));
	for (int i = 0; i < pageCount; i++)
		r.Skip(r.Get<int>() * (int)sizeof(TEXINF));

	int numPerm = r.Get<int>();
	for (int i = 0; i < numPerm && i < 16; i++)
	{
		perm.insert(r.Get<int>());
		r.Get<int>();
	}

	return perm;
}

bool ParseModelInfo(const LevFile& d1, const D1Spool& si, D1Models& out)
{
	const LevLump* lump = d1.FindLump(LUMP_MODELS);
	if (!lump)
		return false;

	Reader r(d1.Data(lump->offset), lump->size);
	int count = r.Get<int>();
	if (!r.Ok() || count < 0 || count > 1536)
		return false;

	out.bounding.assign(count, 0);
	out.resident.assign(count, 0);
	out.shape.assign(count, 0);
	out.flags2.assign(count, 0);
	out.topY.assign(count, 0);
	out.stats.assign(count, ModelStats());
	std::vector<std::pair<const char*, int>> blocks(count, { nullptr, 0 });
	out.pages.assign(count, std::set<int>());
	out.boxReach.assign(count, 0);

	for (int i = 0; i < count; i++)
	{
		int size = r.Get<int>();
		if (size >= (int)sizeof(MODEL))
		{
			if (r.Pos() + size <= lump->size)
			{
				CollectModelPages(r.Here(), size, out.pages[i]);
				out.topY[i] = ModelTopY(r.Here(), size);
				blocks[i] = { r.Here(), size };
			}

			MODEL model;
			r.Read(&model, sizeof(model));
			r.Skip(size - (int)sizeof(MODEL));
			out.bounding[i] = model.bounding_sphere;
			out.resident[i] = 1;
			out.shape[i] = model.shape_flags;
			out.flags2[i] = model.flags2;
		}
		else
			r.Skip(size);
	}

	// spooled models: per area a block of [int size][MODEL...] ending with a sector holding the model numbers
	const OUT_CITYLUMP_INFO& li = d1.GetLumpInfo();
	out.areaModels.resize(si.areas.size());

	for (size_t a = 0; a < si.areas.size(); a++)
	{
		const AreaDataStr& area = si.areas[a];
		int64_t base = (int64_t)li.spooled_offset + (int64_t)area.model_offset * kSector;

		if (area.model_size == 0 || base + (int64_t)area.model_size * kSector > d1.Size())
			continue;

		const char* index = d1.Data((int)base + (area.model_size - 1) * kSector);
		int numModels = *(const uint16_t*)index;
		Reader mr(d1.Data((int)base), (area.model_size - 1) * kSector);

		for (int i = 0; i < numModels; i++)
		{
			int number = ((const uint16_t*)index)[1 + i];
			int size = mr.Get<int>();
			if (number < count && size >= (int)sizeof(MODEL) && mr.Pos() + size <= (area.model_size - 1) * kSector)
			{
				CollectModelPages(mr.Here(), size, out.pages[number]);
				out.topY[number] = ModelTopY(mr.Here(), size);
				blocks[number] = { mr.Here(), size };
			}

			MODEL model;
			mr.Read(&model, sizeof(model));
			mr.Skip(size - (int)sizeof(MODEL));

			if (!mr.Ok() || number >= count)
				return false;

			out.bounding[number] = model.bounding_sphere;
			out.shape[number] = model.shape_flags;
			out.flags2[number] = model.flags2;
			out.areaModels[a].insert(number);
		}
	}

	// extent + roof statistics; an instance model takes its vertices from the model it refers to
	for (int i = 0; i < count; i++)
	{
		if (!blocks[i].first)
			continue;

		out.boxReach[i] = BoxReach(blocks[i].first, blocks[i].second);

		const int owner = ((const MODEL*)blocks[i].first)->instance_number;
		const std::pair<const char*, int> vb = owner == -1 ? blocks[i] : (owner >= 0 && owner < count ? blocks[owner] : std::pair<const char*, int>(nullptr, 0));

		if (vb.first)
			out.stats[i] = AnalyseModel(blocks[i].first, blocks[i].second, vb.first, vb.second);
	}

	return true;
}

struct D1Object
{
	int32_t		x, y, z;
	int			yang, type;
};

// cell pointer table: int size, int packtype (0 empty, 1 plain, 2 bit coded), then the entries
bool DecodeCellPointers(const char* data, int size, int count, std::vector<uint16_t>& out)
{
	Reader r(data, size);
	r.Skip(4);
	int packtype = r.Get<int>();

	out.assign(count, 0xFFFF);

	if (packtype == 1)
		r.Read(out.data(), count * 2);
	else if (packtype == 2)
	{
		unsigned code = r.Get<uint16_t>();
		unsigned bit = 0x8000;

		for (int i = 0; i < count; i++)
		{
			if (code & bit)
				out[i] = r.Get<uint16_t>();

			bit >>= 1;
			if (bit == 0)
			{
				bit = 0x8000;
				code = r.Get<uint16_t>();
			}
		}
	}
	else if (packtype != 0)
		return false;

	return r.Ok();
}

bool ReadD1Region(const LevFile& d1, const D1Map& map, const D1Spool& si, int region, std::vector<D1Object>& out, std::string& log)
{
	const Spool& sp = *si.RegionSpool(region);
	const OUT_CELL_FILE_HEADER& h = map.hdr;
	const int regionsAcross = h.cells_across / h.region_size;
	const int barrel = (region % regionsAcross & 1) + ((region / regionsAcross) & 1) * 2;

	int objectsAdd = 0, slotsAdd = 0;
	for (int b = 0; b < barrel; b++)
	{
		objectsAdd += si.objects[b];
		slotsAdd += si.slots[b];
	}

	const int64_t base = (int64_t)d1.GetLumpInfo().spooled_offset + (int64_t)sp.offset * kSector;
	const int64_t ptrOfs = base + (sp.roadm_size + sp.roadh_size) * kSector;
	const int64_t cellOfs = ptrOfs + sp.cell_data_size[1] * kSector;
	const int64_t objOfs = cellOfs + sp.cell_data_size[0] * kSector;
	const int64_t end = objOfs + sp.cell_data_size[2] * kSector;

	if (end > d1.Size())
	{
		Log(log, "ERROR: D1 region %d lies outside the file", region);
		return false;
	}

	const int numCells = h.region_size * h.region_size;
	std::vector<uint16_t> ptrs;
	if (!DecodeCellPointers(d1.Data((int)ptrOfs), sp.cell_data_size[1] * kSector, numCells, ptrs))
	{
		Log(log, "ERROR: D1 region %d has bad cell pointers", region);
		return false;
	}

	const int numCellData = sp.cell_data_size[0] * kSector / (int)sizeof(CELL_DATA_D1);
	const int numObjects = sp.cell_data_size[2] * kSector / (int)sizeof(CELL_OBJECT_D1);
	const int numStraddlers = (int)map.straddlers.size();

	std::vector<char> seen(kMaxCellIndex + 1, 0);

	for (int c = 0; c < numCells; c++)
	{
		int index = ptrs[c];

		for (int guard = 0; index != 0xFFFF; guard++)
		{
			if (index >= numCellData || guard > 4096)
			{
				Log(log, "ERROR: D1 region %d cell %d has a broken cell data chain", region, c);
				return false;
			}

			CELL_DATA_D1 cd;
			memcpy(&cd, d1.Data((int)cellOfs + index * (int)sizeof(CELL_DATA_D1)), sizeof(cd));
			seen[cd.num & kMaxCellIndex] = 1;

			if (cd.next_ptr == 0xFFFF)
				break;

			index = (int)cd.next_ptr - slotsAdd;
			if (index < 0)
			{
				Log(log, "ERROR: D1 region %d cell %d chain points before its barrel", region, c);
				return false;
			}
		}
	}

	for (int n = 0; n <= kMaxCellIndex; n++)
	{
		if (!seen[n])
			continue;

		CELL_OBJECT_D1 co;
		if (n < numStraddlers)
			co = map.straddlers[n];
		else
		{
			int local = n - numStraddlers - objectsAdd;
			if (local < 0 || local >= numObjects)
			{
				Log(log, "ERROR: D1 region %d references object %d outside its object list", region, n);
				return false;
			}

			memcpy(&co, d1.Data((int)objOfs + local * (int)sizeof(co)), sizeof(co));
		}

		// removed objects are marked with an all ones position and type
		if (co.type == 0xFFFF && co.pos.vx == -1 && co.pos.vy == -1 && co.pos.vz == -1)
			continue;

		out.push_back({ co.pos.vx, co.pos.vy, co.pos.vz, co.yang & 63, co.type });
	}

	return true;
}

// road map of the city: per D1 region 60x60 cells, road ids (RLE short length + ushort id, -1 ends) and values
// (RLE uint length + uint value). Region (rx, rz) holds the global cells x = 60 * rx + i, z = 60 * (regionsDown - 1 - rz) + j.
bool LoadRoadMap(const LevFile& d1, const D1Map& map, const D1Spool& si, D1RoadMap& out, std::string& log)
{
	const LevLump* lump = d1.FindLump(LUMP_ROADMAP);
	if (!lump || lump->size < 8)
	{
		Log(log, "ERROR: ROADMAP lump missing");
		return false;
	}

	out.width = *(const int*)d1.Data(lump->offset);
	out.height = *(const int*)d1.Data(lump->offset + 4);
	out.unitXMid = (out.width + 1) * 750;
	out.unitZMid = out.height * 750;

	const OUT_CELL_FILE_HEADER& h = map.hdr;
	const int side = h.cell_size / 1500 * h.region_size;				// 60
	const int regionsAcross = h.cells_across / h.region_size;
	const int regionsDown = h.cells_down / h.region_size;

	if (out.width <= 0 || out.height <= 0 || out.width > side * regionsAcross || out.height > side * regionsDown)
	{
		Log(log, "ERROR: road map %dx%d does not fit the %dx%d regions", out.width, out.height, regionsAcross, regionsDown);
		return false;
	}

	out.ids.assign((size_t)out.width * out.height, 0xFFFF);
	out.values.assign((size_t)out.width * out.height, 0);
	out.present.assign((size_t)out.width * out.height, 0);

	const int numCells = side * side;
	std::vector<uint16_t> ids(numCells);
	std::vector<uint32_t> values(numCells);

	for (int r = 0; r < (int)si.offsets.size(); r++)
	{
		const Spool* sp = si.RegionSpool(r);
		if (!sp)
			continue;

		const int rx = r % regionsAcross, rz = r / regionsAcross;
		const int64_t base = (int64_t)d1.GetLumpInfo().spooled_offset + (int64_t)sp->offset * kSector;

		if (base + (int64_t)(sp->roadm_size + sp->roadh_size) * kSector > d1.Size())
		{
			Log(log, "ERROR: D1 region %d road data lies outside the file", r);
			return false;
		}

		Reader idReader(d1.Data((int)base), sp->roadm_size * kSector);
		std::fill(ids.begin(), ids.end(), (uint16_t)0xFFFF);

		for (int filled = 0; filled < numCells;)
		{
			short length = idReader.Get<short>();
			if (!idReader.Ok() || length < 0)
				break;

			uint16_t id = idReader.Get<uint16_t>();
			for (int i = 0; i < length && filled < numCells; i++)
				ids[filled++] = id;
		}

		Reader valueReader(d1.Data((int)base + sp->roadm_size * kSector), sp->roadh_size * kSector);
		std::fill(values.begin(), values.end(), 0u);

		for (int filled = 0; filled < numCells;)
		{
			uint32_t length = valueReader.Get<uint32_t>();
			uint32_t value = valueReader.Get<uint32_t>();
			if (!valueReader.Ok() || length == 0)
				break;

			for (uint32_t i = 0; i < length && filled < numCells; i++)
				values[filled++] = value;
		}

		for (int j = 0; j < side; j++)
		{
			const int gz = side * (regionsDown - 1 - rz) + j;
			if (gz >= out.height)
				continue;

			for (int i = 0; i < side; i++)
			{
				const int gx = side * rx + i;
				if (gx >= out.width)
					continue;

				const size_t dst = (size_t)gx + (size_t)gz * out.width;
				out.ids[dst] = ids[i + j * side];
				out.values[dst] = values[i + j * side];
				out.present[dst] = 1;
			}
		}
	}

	return true;
}

// ---------------------------------------------------------------- Driver 2 output

struct Grid
{
	int across, down;								// cells
	int regionsAcross, regionsDown;
	int halfX, halfZ;								// world units between the grid corner and the origin

	int CellX(int x) const { return FloorDiv(x + halfX, D2_CELL_SIZE); }
	int CellZ(int z) const { return FloorDiv(z + halfZ, D2_CELL_SIZE); }
};

// same world rectangle as the Driver 1 map, centred on the origin, rounded up to whole regions
Grid MakeGrid(const OUT_CELL_FILE_HEADER& h)
{
	const int regionUnits = D2_REGION_CELLS * D2_CELL_SIZE;
	auto regions = [&](int cells) { return (int)(((int64_t)cells * h.cell_size + regionUnits - 1) / regionUnits); };

	Grid g;
	g.regionsAcross = (regions(h.cells_across) + 1) & ~1;		// even, the origin must sit on a region corner
	g.regionsDown = (regions(h.cells_down) + 1) & ~1;
	g.across = g.regionsAcross * D2_REGION_CELLS;
	g.down = g.regionsDown * D2_REGION_CELLS;
	g.halfX = g.across / 2 * D2_CELL_SIZE;
	g.halfZ = g.down / 2 * D2_CELL_SIZE;
	return g;
}

// all objects of all D1 regions, duplicates (straddlers listed by several regions) merged; present = regions with data
bool ReadAllObjects(const LevFile& d1, const D1Map& map, const D1Spool& si, std::vector<D1Object>& uniq, int& present, std::string& log)
{
	std::map<std::tuple<int, int, int, int, int>, int> uniqIndex;
	present = 0;

	for (int r = 0; r < (int)si.offsets.size(); r++)
	{
		if (!si.RegionSpool(r))
			continue;

		std::vector<D1Object> objs;
		if (!ReadD1Region(d1, map, si, r, objs, log))
			return false;

		present++;
		for (const D1Object& o : objs)
		{
			if (uniqIndex.emplace(std::make_tuple(o.x, o.y, o.z, o.yang, o.type), (int)uniq.size()).second)
				uniq.push_back(o);
		}
	}

	return true;
}

// Frisco marks its water in the road map; Miami and New York draw the sea with flat water models (cell objects) over road map
// cells that are plain ground, so cars float on it. Every road map cell whose centre lies under such a model becomes sea
// (D1HeightField::Eval keeps tiles, roads and bridges on top). Returns the number of cells marked.
int MarkSeaCells(const std::vector<D1Object>& uniq, const D1Models& models, D1HeightField& field)
{
	const int kCell = 1500;
	const D1RoadMap& rm = field.RoadMap();

	if (rm.width <= 0 || rm.height <= 0)
		return 0;

	std::vector<uint8_t> sea((size_t)rm.width * rm.height, 0);
	int marked = 0;

	for (const D1Object& o : uniq)
	{
		if (o.type >= (int)models.stats.size() || !models.stats[o.type].valid || !(models.shape[o.type] & 0x80))
			continue;

		const ModelStats& s = models.stats[o.type];
		if (s.maxY - s.minY > 60)
			continue;

		const double a = o.yang * 6.283185307 / 64, c = cos(a), sn = sin(a);
		const double cx[4] = { (double)s.minX, (double)s.maxX, (double)s.minX, (double)s.maxX }, cz[4] = { (double)s.minZ, (double)s.minZ, (double)s.maxZ, (double)s.maxZ };
		double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;

		for (int k = 0; k < 4; k++)
		{
			const double wx = o.x + cx[k] * c + cz[k] * sn, wz = o.z - cx[k] * sn + cz[k] * c;
			x0 = std::min(x0, wx); x1 = std::max(x1, wx); z0 = std::min(z0, wz); z1 = std::max(z1, wz);
		}

		// road map cell (cx, cz) has its centre at x = cx * kCell - unitXMid + kCell / 2, z = unitZMid - cz * kCell
		const int ix0 = std::max(0, FloorDiv(rm.unitXMid + (int)x0, kCell) - 1), ix1 = std::min(rm.width - 1, FloorDiv(rm.unitXMid + (int)x1, kCell) + 1);
		const int iz0 = std::max(0, FloorDiv(rm.unitZMid - (int)z1, kCell) - 1), iz1 = std::min(rm.height - 1, FloorDiv(rm.unitZMid - (int)z0, kCell) + 1);

		for (int iz = iz0; iz <= iz1; iz++)
		{
			for (int ix = ix0; ix <= ix1; ix++)
			{
				const double dx = ix * kCell - rm.unitXMid + kCell / 2 - o.x, dz = rm.unitZMid - iz * kCell - o.z;
				const double lx = dx * c - dz * sn, lz = dx * sn + dz * c;
				uint8_t& cell = sea[(size_t)ix + (size_t)iz * rm.width];

				if (!cell && lx >= s.minX && lx <= s.maxX && lz >= s.minZ && lz <= s.maxZ)
				{
					cell = 1;
					marked++;
				}
			}
		}
	}

	field.SetSeaCells(std::move(sea));
	return marked;
}

// Driver 1 only draws ground where something stands on it: the holes between and under buildings show the sky when seen
// from above. Every hole within kMaxReach tiles of existing ground gets a copy of the nearest flat ground tile (grass, sand,
// water; never a road tile), at the height of the road map. Returns the number of tiles added.
int FillGroundGaps(std::vector<D1Object>& uniq, const D1Models& models, const D1Spool& si, const OUT_CELL_FILE_HEADER& h, const D1HeightField& field, std::string& log)
{
	const int kStep = 750, kTile = 1500, kMaxSpread = 250, kMaxReach = 24, kObjectIndexSpace = 16384, kStraddlerGuess = 10000;
	const int halfX = h.cells_across / 2 * h.cell_size, halfZ = h.cells_down / 2 * h.cell_size;
	const int w = h.cells_across * h.cell_size / kStep, hh = h.cells_down * h.cell_size / kStep;
	const int regionsAcross = h.cells_across / h.region_size;
	const int regionUnits = h.region_size * h.cell_size;

	auto isFloor = [&](int type)
	{
		return type < (int)models.shape.size() && ((models.shape[type] & (0x400 | 0x80 | 0x2000)) || (models.flags2[type] & (0x4000 | 0x8000 | 0x40 | 0x80 | 0x20)));
	};

	// the grass tile to use: the most used flat square resident grass model
	std::map<int, int> grassCount;
	std::map<std::pair<int, int>, int> phase;
	for (const D1Object& o : uniq)
	{
		if (o.type >= (int)models.stats.size() || !models.stats[o.type].valid || !models.resident[o.type])
			continue;

		const ModelStats& s = models.stats[o.type];
		const int dx = s.maxX - s.minX, dz = s.maxZ - s.minZ;
		if ((models.flags2[o.type] & 0x4000) && !(models.flags2[o.type] & 0x8000) && !(models.shape[o.type] & 0x80) && s.maxY - s.minY <= 40 && dx >= 1400 && dx <= 1600 && dz >= 1400 && dz <= 1600)
		{
			grassCount[o.type]++;
			phase[{ o.x - FloorDiv(o.x, kTile) * kTile, o.z - FloorDiv(o.z, kTile) * kTile }]++;
		}
	}

	int grass = -1, best = 0;
	for (const auto& e : grassCount)
	{
		if (e.second > best)
		{
			best = e.second;
			grass = e.first;
		}
	}

	if (grass < 0)
	{
		Log(log, "fill: no resident grass tile model, ground gaps stay open");
		return 0;
	}

	int phaseX = 0, phaseZ = 0, phaseBest = 0;
	for (const auto& e : phase)
	{
		if (e.second > phaseBest)
		{
			phaseBest = e.second;
			phaseX = e.first.first;
			phaseZ = e.first.second;
		}
	}

	// ground coverage at 750 units: footprint of every floor model (rotated box)
	std::vector<uint8_t> cover((size_t)w * hh, 0);
	for (const D1Object& o : uniq)
	{
		if (o.type >= (int)models.stats.size() || !models.stats[o.type].valid || !isFloor(o.type) || models.stats[o.type].maxY - models.stats[o.type].minY > 450)
			continue;

		const ModelStats& s = models.stats[o.type];
		const double a = o.yang * 6.283185307 / 64, c = cos(a), sn = sin(a);
		const double cx[4] = { (double)s.minX, (double)s.maxX, (double)s.minX, (double)s.maxX }, cz[4] = { (double)s.minZ, (double)s.minZ, (double)s.maxZ, (double)s.maxZ };
		double x0 = 1e18, x1 = -1e18, z0 = 1e18, z1 = -1e18;

		for (int k = 0; k < 4; k++)
		{
			const double wx = o.x + cx[k] * c + cz[k] * sn, wz = o.z - cx[k] * sn + cz[k] * c;
			x0 = std::min(x0, wx); x1 = std::max(x1, wx); z0 = std::min(z0, wz); z1 = std::max(z1, wz);
		}

		const int ix0 = std::max(0, (int)floor((x0 + halfX) / kStep)), ix1 = std::min(w - 1, (int)floor((x1 + halfX) / kStep));
		const int iz0 = std::max(0, (int)floor((z0 + halfZ) / kStep)), iz1 = std::min(hh - 1, (int)floor((z1 + halfZ) / kStep));

		for (int iz = iz0; iz <= iz1; iz++)
		{
			for (int ix = ix0; ix <= ix1; ix++)
			{
				const double dx = ix * kStep + kStep / 2 - halfX - o.x, dz = iz * kStep + kStep / 2 - halfZ - o.z;
				const double lx = dx * c - dz * sn, lz = dx * sn + dz * c;
				if (lx >= s.minX && lx <= s.maxX && lz >= s.minZ && lz <= s.maxZ)
					cover[(size_t)ix + (size_t)iz * w] = 1;
			}
		}
	}

	std::vector<uint8_t> present((size_t)w * hh, 0);
	for (int iz = 0; iz < hh; iz++)
		for (int ix = 0; ix < w; ix++)
			present[(size_t)ix + (size_t)iz * w] = 1;	// regions without data get ground too (the nearest tile reach limits it)

	// nearest flat ground tile: multi source breadth first search over the tile grid from every flat square floor object
	const int gx0 = FloorDiv(-halfX - phaseX, kTile), gz0 = FloorDiv(-halfZ - phaseZ, kTile);
	const int gw = (halfX * 2) / kTile + 3, gh = (halfZ * 2) / kTile + 3;
	std::vector<int> nearType((size_t)gw * gh, -1), nearY((size_t)gw * gh, 0);
	std::vector<uint8_t> nearDist((size_t)gw * gh, 255);
	std::vector<int> frontier;
	auto gridIndex = [&](int x, int z)
	{
		const int ix = FloorDiv(x - phaseX + kTile / 2, kTile) - gx0, iz = FloorDiv(z - phaseZ + kTile / 2, kTile) - gz0;
		return (ix < 0 || iz < 0 || ix >= gw || iz >= gh) ? -1 : ix + iz * gw;
	};

	for (const D1Object& o : uniq)
	{
		if (o.type >= (int)models.stats.size() || !models.stats[o.type].valid || !isFloor(o.type))
			continue;

		const ModelStats& s = models.stats[o.type];
		const int dx = s.maxX - s.minX, dz = s.maxZ - s.minZ;
		if (s.maxY - s.minY > 60 || dx < 1400 || dx > 1600 || dz < 1400 || dz > 1600 || (models.shape[o.type] & 0x400))
			continue;

		D1Ground rg;
		if (field.Eval(o.x, o.z, rg) && rg.surface >= 32)
			continue;

		const int gi = gridIndex(o.x, o.z);
		if (gi >= 0 && nearDist[gi] != 0)
		{
			nearDist[gi] = 0;
			nearType[gi] = o.type;
			nearY[gi] = o.y;
			frontier.push_back(gi);
		}
	}

	for (size_t q = 0; q < frontier.size(); q++)
	{
		const int gi = frontier[q], ix = gi % gw, iz = gi / gw;
		if (nearDist[gi] >= kMaxReach)
			continue;

		const int nx[4] = { ix - 1, ix + 1, ix, ix }, nz[4] = { iz, iz, iz - 1, iz + 1 };
		for (int k = 0; k < 4; k++)
		{
			if (nx[k] < 0 || nz[k] < 0 || nx[k] >= gw || nz[k] >= gh)
				continue;

			const int n = nx[k] + nz[k] * gw;
			if (nearDist[n] == 255)
			{
				nearDist[n] = nearDist[gi] + 1;
				nearType[n] = nearType[gi];
				nearY[n] = nearY[gi];
				frontier.push_back(n);
			}
		}
	}

	// connected holes (4 neighbours); a hole that reaches the border or a region without data is open sea / outside
	std::vector<int> label((size_t)w * hh, -1);
	std::vector<char> fillable;
	std::vector<int> queue;
	int holes = 0, holeCells = 0, openCells = 0;

	for (size_t start = 0; start < label.size(); start++)
	{
		if (label[start] != -1 || cover[start] || !present[start])
			continue;

		const int id = (int)fillable.size();
		bool open = false;
		int size = 0;
		queue.assign(1, (int)start);
		label[start] = id;

		for (size_t q = 0; q < queue.size(); q++)
		{
			const int ix = queue[q] % w, iz = queue[q] / w;
			size++;
			const int nx[4] = { ix - 1, ix + 1, ix, ix }, nz[4] = { iz, iz, iz - 1, iz + 1 };

			for (int k = 0; k < 4; k++)
			{
				if (nx[k] < 0 || nz[k] < 0 || nx[k] >= w || nz[k] >= hh || !present[(size_t)nx[k] + (size_t)nz[k] * w])
				{
					open = true;
					continue;
				}

				const size_t n = (size_t)nx[k] + (size_t)nz[k] * w;
				if (!cover[n] && label[n] == -1)
				{
					label[n] = id;
					queue.push_back((int)n);
				}
			}
		}

		const bool fill = true;
		(void)open;
		fillable.push_back(fill);
		holes += fill ? 1 : 0;
		holeCells += fill ? size : 0;
		openCells += fill ? 0 : size;
	}

	auto holeAt = [&](int x, int z)
	{
		const int ix = FloorDiv(x + halfX, kStep), iz = FloorDiv(z + halfZ, kStep);
		if (ix < 0 || iz < 0 || ix >= w || iz >= hh)
			return false;

		const int id = label[(size_t)ix + (size_t)iz * w];
		return id >= 0 && fillable[id] != 0;
	};

	// the cell object index has 14 bits (shared with the straddlers) and every barrel (the 4 region parities) reserves room for
	// its fullest region, so no region may grow beyond a fixed share. Nearest tiles first.
	struct Candidate { int dist, region; D1Object obj; };
	std::vector<Candidate> candidates;
	std::vector<int> regionCount((size_t)regionsAcross * (h.cells_down / h.region_size), 0);
	auto regionOf = [&](int x, int z)
	{
		const int rx = std::max(0, std::min(regionsAcross - 1, FloorDiv(x + halfX, regionUnits)));
		const int rz = std::max(0, std::min(h.cells_down / h.region_size - 1, FloorDiv(z + halfZ, regionUnits)));
		return rx + rz * regionsAcross;
	};
	for (const D1Object& o : uniq)
		regionCount[regionOf(o.x, o.z)]++;

	// every barrel gets an equal share of the index space the straddlers leave (they are counted in the regions below too, which
	// only makes the share safe)
	auto barrelOf = [&](int region) { return (region % regionsAcross & 1) + ((region / regionsAcross) & 1) * 2; };
	const int regionBudget = (kObjectIndexSpace - kStraddlerGuess) / 4 / 256 * 256;
	int barrelBudget[4] = { regionBudget, regionBudget, regionBudget, regionBudget };

	int added = 0, steep = 0;
	for (int cz = FloorDiv(-halfZ - phaseZ, kTile) * kTile + phaseZ; cz < halfZ; cz += kTile)
	{
		for (int cx = FloorDiv(-halfX - phaseX, kTile) * kTile + phaseX; cx < halfX; cx += kTile)
		{
			if (!holeAt(cx - 375, cz - 375) || !holeAt(cx + 375, cz - 375) || !holeAt(cx - 375, cz + 375) || !holeAt(cx + 375, cz + 375))
				continue;

			const int gi = gridIndex(cx, cz);
			if (gi < 0 || nearType[gi] < 0)
				continue;

			// outside the height field (regions without data) the tile takes the height of its source tile
			D1Ground g;
			if (!field.Eval(cx, cz, g))
			{
				candidates.push_back({ nearDist[gi], regionOf(cx, cz), { cx, nearY[gi], cz, 0, nearType[gi] } });
				continue;
			}

			int lo = g.y, hi = g.y;
			for (int k = 0; k < 4; k++)
			{
				D1Ground c;
				if (field.Eval(cx + (k & 1 ? 750 : -750), cz + (k & 2 ? 750 : -750), c))
				{
					lo = std::min(lo, c.y);
					hi = std::max(hi, c.y);
				}
			}

			// hills and bridge decks: a flat tile there would lie across the road, the runtime backdrop closes the gap instead
			if (hi - lo > kMaxSpread || std::abs(-g.y - nearY[gi]) > 400)
			{
				steep++;
				continue;
			}

			candidates.push_back({ nearDist[gi], regionOf(cx, cz), { cx, -g.y, cz, 0, nearType[gi] } });
		}
	}

	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) { return a.dist < b.dist; });
	int overBudget = 0;
	for (const Candidate& c : candidates)
	{
		if (regionCount[c.region] >= barrelBudget[barrelOf(c.region)])
		{
			overBudget++;
			continue;
		}

		regionCount[c.region]++;
		uniq.push_back(c.obj);
		added++;
	}

	Log(log, "fill: nearest tile fill (grid grass model %d), %d holes (%d cells of %d units), %d tiles added, %d skipped (hill or bridge), %d over the region budget", grass, holes, holeCells, kStep, added, steep, overBudget);
	(void)openCells;
	return added;
}

struct RegionBuild
{
	std::vector<int>				localObjects;	// unique object numbers living in this region
	std::vector<std::vector<int>>	cellRefs;		// per cell: unique object numbers
	bool							present{ false };
	int								d1Region{ -1 };	// Driver 1 region whose area data this one inherits

	std::vector<char>				pointers;		// packed cell pointer block
	std::vector<char>				roadBlock;		// PVS + height map
	int								numSlots{ 0 };
	int								sectors[4]{};	// cell data, pointers, objects, roads
};

PACKED_CELL_OBJECT PackObject(const D1Object& o)
{
	int y = std::max(-16384, std::min(16383, o.y));

	PACKED_CELL_OBJECT p;
	p.pos_vx = (uint16_t)o.x;
	p.pos_vz = (uint16_t)o.z;
	p.pos_vy = (uint16_t)((y * 2) | ((o.type >> 10) & 1));
	p.value = (uint16_t)(((o.type & 1023) << 6) | (o.yang & 63));
	return p;
}

std::vector<char> PackPointers(const std::vector<uint16_t>& ptrs)
{
	std::vector<char> b;
	Put<int>(b, 0);
	Put<int>(b, 2);

	// a code word with one bit per cell, then the pointers of the cells that have one; repeated per 16 cells
	for (size_t g = 0; g < ptrs.size(); g += 16)
	{
		uint16_t code = 0;
		std::vector<uint16_t> values;

		for (size_t i = 0; i < 16; i++)
		{
			if (ptrs[g + i] != 0xFFFF)
			{
				code |= (uint16_t)(0x8000 >> i);
				values.push_back(ptrs[g + i]);
			}
		}

		Put<uint16_t>(b, code);
		Put(b, values.data(), values.size() * 2);
	}

	Put<uint16_t>(b, 0);							// the decoder reads one code word past the last cell
	*(int*)b.data() = (int)b.size() - 4;
	return b;
}

void AppendBlock(std::vector<char>& spool, const std::vector<char>& block, int sectors)
{
	size_t start = spool.size();
	spool.insert(spool.end(), block.begin(), block.end());
	spool.resize(start + (size_t)sectors * kSector, 0);
}

} // namespace

// world positions of the traffic light models (names TLIGHT*) among the objects, for the junction light flags
static std::vector<std::pair<int, int>> CollectTrafficLights(const LevFile& d1, const std::vector<D1Object>& objects)
{
	std::vector<std::pair<int, int>> lights;
	const LevLump* nameLump = d1.FindLump(LUMP_MODELNAMES);

	if (!nameLump)
		return lights;

	std::vector<char> isLight;
	const char* p = d1.Data(nameLump->offset);
	const char* end = p + nameLump->size;

	while (p < end)
	{
		isLight.push_back(!strncmp(p, "TLIGHT", 6));
		p += strlen(p) + 1;
	}

	for (const D1Object& o : objects)
	{
		if (o.type >= 0 && o.type < (int)isLight.size() && isLight[o.type])
			lights.push_back({ o.x, o.z });
	}

	return lights;
}

bool D1Dlc_ConvertMap(const LevFile& d1, LevWriter& out, std::string& log)
{
	D1Map map;
	D1Spool si;
	D1Models models;

	if (!ParseMapLump(d1, true, map))
	{
		Log(log, "ERROR: MAP lump missing or invalid");
		return false;
	}

	if (!ParseSpoolInfo(d1, si))
	{
		Log(log, "ERROR: SPOOLINFO lump missing or invalid");
		return false;
	}

	if (!ParseModelInfo(d1, si, models))
	{
		Log(log, "ERROR: MODELS lump or the area model blocks are invalid");
		return false;
	}

	const OUT_CELL_FILE_HEADER& h = map.hdr;
	if (h.cell_size <= 0 || h.region_size <= 0 || h.cells_across % h.region_size || h.cells_down % h.region_size)
	{
		Log(log, "ERROR: unexpected D1 map header");
		return false;
	}

	const Grid g = MakeGrid(h);
	const int d1RegionsAcross = h.cells_across / h.region_size;
	const int d1RegionsDown = h.cells_down / h.region_size;
	const int d1RegionUnits = h.region_size * h.cell_size;
	const int d1HalfX = h.cells_across / 2 * h.cell_size;
	const int d1HalfZ = h.cells_down / 2 * h.cell_size;

	// 1. all objects of all D1 regions
	std::vector<D1Object> uniq;
	int d1Present = 0;

	if (!ReadAllObjects(d1, map, si, uniq, d1Present, log))
		return false;

	// height field first: the ground fill needs it
	D1RoadMap roadMap;
	D1HeightField field;
	if (!LoadRoadMap(d1, map, si, roadMap, log) ||
		!field.Init(d1, std::move(roadMap), D1ModelFlags{ models.shape, models.flags2, models.topY }))
	{
		Log(log, "ERROR: road map or ROADSURF lump is invalid");
		return false;
	}

	D1RoadTable roadTable;
	roadTable.SetLights(CollectTrafficLights(d1, uniq));
	if (!roadTable.Build(d1, field.RoadMap(), log))
		return false;

	field.SetRoads(&roadTable);

	Log(log, "sea: %d road map cells under water models marked as sea", MarkSeaCells(uniq, models, field));

	FillGroundGaps(uniq, models, si, h, field, log);

	// 2. D2 cells of every object: its bounding sphere square. One region only -> local object, else straddler.
	std::vector<RegionBuild> regions(g.regionsAcross * g.regionsDown);
	for (RegionBuild& rb : regions)
		rb.cellRefs.resize(kCellsPerRegion);

	std::vector<int> localIndex(uniq.size(), -1);
	std::vector<int> straddlerIndex(uniq.size(), -1);
	std::vector<int> straddlers;
	int totalRefs = 0, outside = 0;

	for (size_t u = 0; u < uniq.size(); u++)
	{
		const D1Object& o = uniq[u];
		int radius = o.type < (int)models.bounding.size() && models.bounding[o.type] > 0 ? models.bounding[o.type] : kDefaultRadius;
		radius = std::min(radius, kMaxRadius);

		// collision is only checked against objects listed in the cells around a car: long walls (the car park's) need every
		// cell their boxes reach, beyond the cap
		if (o.type < (int)models.boxReach.size())
			radius = std::max(radius, models.boxReach[o.type]);

		int cx0 = std::max(0, g.CellX(o.x - radius)), cx1 = std::min(g.across - 1, g.CellX(o.x + radius));
		int cz0 = std::max(0, g.CellZ(o.z - radius)), cz1 = std::min(g.down - 1, g.CellZ(o.z + radius));

		if (cx0 > cx1 || cz0 > cz1)
		{
			outside++;
			continue;
		}

		if (cx0 / D2_REGION_CELLS == cx1 / D2_REGION_CELLS && cz0 / D2_REGION_CELLS == cz1 / D2_REGION_CELLS)
		{
			RegionBuild& home = regions[cx0 / D2_REGION_CELLS + cz0 / D2_REGION_CELLS * g.regionsAcross];
			localIndex[u] = (int)home.localObjects.size();
			home.localObjects.push_back((int)u);
		}
		else
		{
			straddlerIndex[u] = (int)straddlers.size();
			straddlers.push_back((int)u);
		}

		for (int cz = cz0; cz <= cz1; cz++)
		{
			for (int cx = cx0; cx <= cx1; cx++)
			{
				RegionBuild& rb = regions[cx / D2_REGION_CELLS + cz / D2_REGION_CELLS * g.regionsAcross];
				rb.cellRefs[cz % D2_REGION_CELLS * D2_REGION_CELLS + cx % D2_REGION_CELLS].push_back((int)u);
				totalRefs++;
			}
		}
	}

	// 3. present regions (objects or overlapping a D1 region) and the D1 region they inherit areas from
	int presentRegions = 0;
	for (int rz = 0; rz < g.regionsDown; rz++)
	{
		for (int rx = 0; rx < g.regionsAcross; rx++)
		{
			RegionBuild& rb = regions[rx + rz * g.regionsAcross];
			// the height map window of a region starts 512 units after the region (half a height cell)
			const int x0 = rx * D2_REGION_CELLS * D2_CELL_SIZE - g.halfX + 512;
			const int z0 = rz * D2_REGION_CELLS * D2_CELL_SIZE - g.halfZ + 512;
			const int regionUnits = D2_REGION_CELLS * D2_CELL_SIZE;

			auto d1RegionAt = [&](int x, int z)
			{
				int gx = FloorDiv(x + d1HalfX, d1RegionUnits), gz = FloorDiv(z + d1HalfZ, d1RegionUnits);
				if (gx < 0 || gz < 0 || gx >= d1RegionsAcross || gz >= d1RegionsDown)
					return -1;

				int index = gx + gz * d1RegionsAcross;
				return si.RegionSpool(index) ? index : -1;
			};

			rb.d1Region = d1RegionAt(x0 + regionUnits / 2, z0 + regionUnits / 2);

			// otherwise any D1 region the rectangle overlaps (each D2 region spans at most 2x2 of them)
			for (int dz = 0; dz < 2 && rb.d1Region < 0; dz++)
			{
				for (int dx = 0; dx < 2 && rb.d1Region < 0; dx++)
					rb.d1Region = d1RegionAt(x0 + dx * (regionUnits - 1), z0 + dz * (regionUnits - 1));
			}

			for (int gz = FloorDiv(z0 + d1HalfZ, d1RegionUnits); gz <= FloorDiv(z0 + regionUnits - 1 + d1HalfZ, d1RegionUnits) && rb.d1Region < 0; gz++)
			{
				for (int gx = FloorDiv(x0 + d1HalfX, d1RegionUnits); gx <= FloorDiv(x0 + regionUnits - 1 + d1HalfX, d1RegionUnits) && rb.d1Region < 0; gx++)
				{
					if (gx >= 0 && gz >= 0 && gx < d1RegionsAcross && gz < d1RegionsDown && si.RegionSpool(gx + gz * d1RegionsAcross))
						rb.d1Region = gx + gz * d1RegionsAcross;
				}
			}

			bool hasObjects = false;
			for (const std::vector<int>& cell : rb.cellRefs)
				hasObjects |= !cell.empty();

			rb.present = hasObjects || rb.d1Region >= 0;
			presentRegions += rb.present ? 1 : 0;
		}
	}

	// 4. region blocks: cell pointers, height maps, sizes per block, per barrel maxima
	HeightMapStats heightStats;
	int barrelRoad[4] = { 0, 0, 0, 0 };
	int barrelSlots[4] = { 1024, 1024, 1024, 1024 }, barrelObjects[4] = { 256, 256, 256, 256 };
	int totalSlots = 0, totalLocal = 0;

	for (int i = 0; i < (int)regions.size(); i++)
	{
		RegionBuild& rb = regions[i];
		if (!rb.present)
			continue;

		std::vector<uint16_t> ptrs(kCellsPerRegion, 0xFFFF);
		rb.numSlots = 0;

		for (int c = 0; c < kCellsPerRegion; c++)
		{
			if (rb.cellRefs[c].empty())
				continue;

			ptrs[c] = (uint16_t)rb.numSlots;
			rb.numSlots += (int)rb.cellRefs[c].size();
		}

		rb.pointers = PackPointers(ptrs);
		rb.sectors[0] = SectorsFor(rb.numSlots * 2);
		rb.sectors[1] = SectorsFor(rb.pointers.size());
		rb.sectors[2] = SectorsFor(rb.localObjects.size() * sizeof(PACKED_CELL_OBJECT));
		rb.roadBlock = BuildRoadBlock(field, i % g.regionsAcross, i / g.regionsAcross, g.regionsAcross, g.regionsDown, heightStats);
		if (rb.roadBlock.empty())
		{
			Log(log, "ERROR: region %d needs %d height planes (limit about 2040)", i, -heightStats.planes);
			return false;
		}

		rb.sectors[3] = SectorsFor(rb.roadBlock.size());

		const int barrel = (i % g.regionsAcross & 1) + ((i / g.regionsAcross) & 1) * 2;
		barrelSlots[barrel] = std::max(barrelSlots[barrel], rb.sectors[0] * (kSector / 2));
		barrelObjects[barrel] = std::max(barrelObjects[barrel], rb.sectors[2] * (kSector / (int)sizeof(PACKED_CELL_OBJECT)));

		barrelRoad[barrel] = std::max(barrelRoad[barrel], rb.sectors[3]);
		totalSlots += rb.numSlots;
		totalLocal += (int)rb.localObjects.size();
	}

	int objectBase[4];
	int objectTotal = (int)straddlers.size();
	for (int b = 0; b < 4; b++)
	{
		objectBase[b] = objectTotal;
		objectTotal += barrelObjects[b];
	}

	if (objectTotal > kMaxCellIndex + 1)
	{
		Log(log, "ERROR: %d cell objects (%d straddlers + barrel space) exceed the 14 bit cell object index", objectTotal, (int)straddlers.size());
		return false;
	}

	// 5. spooled section: area textures + models (copied), then the regions
	std::vector<char> spool;
	std::vector<AreaDataStr> areas = si.areas;
	const OUT_CITYLUMP_INFO& li = d1.GetLumpInfo();
	int maxModelSectors = 0;
	int roofs = 0, kerbBoxes = 0;

	for (AreaDataStr& area : areas)
	{
		const int gfxSectors = area.num_tpages * 17;
		const int64_t gfx = (int64_t)li.spooled_offset + (int64_t)area.gfx_offset * kSector;
		const int64_t mdl = (int64_t)li.spooled_offset + (int64_t)area.model_offset * kSector;

		if (gfx + (int64_t)gfxSectors * kSector > d1.Size() || mdl + (int64_t)area.model_size * kSector > d1.Size())
		{
			Log(log, "ERROR: area data lies outside the file");
			return false;
		}

		area.gfx_offset = (uint16_t)(spool.size() / kSector);
		spool.insert(spool.end(), d1.Data((int)gfx), d1.Data((int)gfx) + (size_t)gfxSectors * kSector);

		area.model_offset = (uint16_t)(spool.size() / kSector);

		// models of the area: flags2 bit 0 is the animated object flag of the D2 engine (same fix as for the resident
		// models), grass is lowered and roofless buildings get a roof, so the block is rebuilt (sector padded, index sector last)
		if (area.model_size > 0)
		{
			const char* src = d1.Data((int)mdl);
			const int dataBytes = (area.model_size - 1) * kSector;
			const int numModels = *(const uint16_t*)(src + dataBytes);
			std::vector<char> block;
			int ofs = 0, parsed = 0;

			for (; parsed < numModels && ofs + 4 + (int)sizeof(MODEL) <= dataBytes; parsed++)
			{
				const int size = *(const int*)(src + ofs);
				if (size < (int)sizeof(MODEL) || ofs + 4 + size > dataBytes)
					break;

				std::vector<char> model(src + ofs + 4, src + ofs + 4 + size);
				((MODEL*)model.data())->flags2 &= ~1;
				LowerGrassModel(model.data(), size);
				ClampBoxHeights(model);
				kerbBoxes += RemoveKerbWallBoxes(model, std::string());	// spooled models carry no name here
				roofs += kRoofCaps ? AddRoofCap(model) : 0;
				Put<int>(block, (int)model.size());
				Put(block, model.data(), model.size());
				ofs += 4 + size;
			}

			if (parsed == numModels)
			{
				block.resize((block.size() + kSector - 1) / kSector * kSector, 0);
				Put(block, src + dataBytes, kSector);
				area.model_size = (uint16_t)(block.size() / kSector);
				spool.insert(spool.end(), block.begin(), block.end());
			}
			else
				spool.insert(spool.end(), src, src + (size_t)area.model_size * kSector);
		}

		area.music_offset = area.ambient_offset = 0;
		area.ambient_size = area.music_size = area.music_samples_size = area.music_id = area.ambient_id = 0;
		maxModelSectors = std::max(maxModelSectors, (int)area.model_size);
	}

	Log(log, "map: %d spooled buildings got a roof, %d kerb wall boxes removed", roofs, kerbBoxes);

	std::vector<char> regionInfo;
	std::vector<uint16_t> regionOffsets(regions.size(), kEmptyOffset);

	for (int i = 0; i < (int)regions.size(); i++)
	{
		RegionBuild& rb = regions[i];
		if (!rb.present)
			continue;

		const int barrel = (i % g.regionsAcross & 1) + ((i / g.regionsAcross) & 1) * 2;

		std::vector<char> cells;
		for (const std::vector<int>& cell : rb.cellRefs)
		{
			for (size_t k = 0; k < cell.size(); k++)
			{
				const int u = cell[k];
				int ref = straddlerIndex[u] >= 0 ? straddlerIndex[u] : objectBase[barrel] + localIndex[u];

				if (ref > kMaxCellIndex)
				{
					Log(log, "ERROR: cell object reference %d does not fit", ref);
					return false;
				}

				Put<uint16_t>(cells, (uint16_t)(ref | (k + 1 == cell.size() ? 0x8000 : 0)));
			}
		}

		std::vector<char> objects;
		for (int u : rb.localObjects)
		{
			PACKED_CELL_OBJECT p = PackObject(uniq[u]);
			Put(objects, p);
		}

		Spool sp{};
		sp.offset = (uint16_t)(spool.size() / kSector);
		sp.pvs_size = 0;
		sp.cell_data_size[0] = (uint8_t)rb.sectors[0];
		sp.cell_data_size[1] = (uint8_t)rb.sectors[1];
		sp.cell_data_size[2] = (uint8_t)rb.sectors[2];
		sp.roadm_size = (uint8_t)rb.sectors[3];
		sp.roadh_size = 0;
		sp.super_region = 0xFF;

		if (const Spool* src = rb.d1Region >= 0 ? si.RegionSpool(rb.d1Region) : nullptr)
		{
			sp.super_region = src->super_region;
			sp.num_connected_areas = src->num_connected_areas;
			memcpy(sp.connected_areas, src->connected_areas, sizeof(sp.connected_areas));
		}

		if (spool.size() / kSector + rb.sectors[0] + rb.sectors[1] + rb.sectors[2] + rb.sectors[3] > 65535)
		{
			Log(log, "ERROR: spooled section is too large for 16 bit sector offsets");
			return false;
		}

		AppendBlock(spool, rb.pointers, rb.sectors[1]);
		AppendBlock(spool, cells, rb.sectors[0]);
		AppendBlock(spool, objects, rb.sectors[2]);
		AppendBlock(spool, rb.roadBlock, rb.sectors[3]);

		regionOffsets[i] = (uint16_t)regionInfo.size();
		Put(regionInfo, sp);
	}

	// 6. SPOOLINFO lump
	std::vector<char> info;
	Put<int>(info, std::max({ si.modelBufferSectors, maxModelSectors, 32 }));
	Put<int>(info, 0);
	Put<int>(info, (int)areas.size());
	Put(info, areas.data(), areas.size() * sizeof(AreaDataStr));
	for (const std::vector<uint8_t>& list : si.tpages)
		Put(info, list.data(), 16);

	Put(info, barrelSlots, sizeof(barrelSlots));
	Put(info, barrelObjects, sizeof(barrelObjects));
	int pvsSizes[4];
	for (int b = 0; b < 4; b++)
		pvsSizes[b] = std::max(1, barrelRoad[b]) * kSector;
	Put(info, pvsSizes, sizeof(pvsSizes));

	Put<int>(info, (int)regionOffsets.size());
	Put(info, regionOffsets.data(), regionOffsets.size() * 2);
	Put<int>(info, (int)regionInfo.size());
	Put(info, regionInfo.data(), regionInfo.size());

	// 7. MAP lump: header, then the straddler objects
	OUT_CELL_FILE_HEADER hdr = h;
	hdr.cells_across = g.across;
	hdr.cells_down = g.down;
	hdr.cell_size = D2_CELL_SIZE;
	hdr.num_regions = g.regionsAcross * g.regionsDown;
	hdr.region_size = D2_REGION_CELLS;
	hdr.num_cell_objects = (int)straddlers.size() + totalLocal;
	hdr.num_cell_data = totalSlots;

	std::vector<char> mapLump;
	Put(mapLump, hdr);
	Put<int>(mapLump, (int)straddlers.size());
	for (int u : straddlers)
	{
		PACKED_CELL_OBJECT p = PackObject(uniq[u]);
		Put(mapLump, p);
	}

	out.AddLump(LEV_SECTION_LOADTIME, LUMP_MAP, mapLump.data(), (int)mapLump.size());
	out.AddLump(LEV_SECTION_INMEM, LUMP_SPOOLINFO, info.data(), (int)info.size());

	// D1 roads are axis aligned, so there are no curves
	const std::vector<char> straightsLump = roadTable.StraightsLump(), junctionsLump = roadTable.JunctionsLump();
	const int noCurves = 0;
	out.AddLump(LEV_SECTION_INMEM, LUMP_STRAIGHTS2, straightsLump.data(), (int)straightsLump.size());
	out.AddLump(LEV_SECTION_INMEM, LUMP_CURVES2, &noCurves, sizeof(noCurves));
	out.AddLump(LEV_SECTION_INMEM, LUMP_JUNCTIONS2_NEW, junctionsLump.data(), (int)junctionsLump.size());

	out.spool = std::move(spool);

	// 8. area coverage: spooled models the objects of a region need but the area of that region does not hold
	int checked = 0, missingRegions = 0, missingPairs = 0;
	std::set<int> missingModels;
	const std::set<int> permPages = ReadPermanentPages(d1);
	int pageRegions = 0, pagePairs = 0;
	std::map<int, int> pageMissingCount;

	for (int i = 0; i < (int)regions.size(); i++)
	{
		const RegionBuild& rb = regions[i];
		if (!rb.present || rb.d1Region < 0)
			continue;

		const Spool& sp = *si.RegionSpool(rb.d1Region);
		int area = sp.super_region != 0xFF ? sp.super_region : (sp.num_connected_areas ? (sp.connected_areas[0] & 0x3F) : -1);
		if (area < 0 || area >= (int)models.areaModels.size())
			continue;

		std::set<int> needed, usedPages;
		for (const std::vector<int>& cell : rb.cellRefs)
		{
			for (int u : cell)
			{
				int type = uniq[u].type;
				if (type < (int)models.resident.size() && !models.resident[type])
					needed.insert(type);

				if (type < (int)models.pages.size())
					usedPages.insert(models.pages[type].begin(), models.pages[type].end());
			}
		}

		int pagesMissing = 0;
		for (int page : usedPages)
		{
			bool inArea = false;
			for (int t = 0; t < si.areas[area].num_tpages; t++)
				inArea |= si.tpages[area][t] == page;

			if (!permPages.count(page) && !inArea)
			{
				pagesMissing++;
				pageMissingCount[page]++;
			}
		}

		pageRegions += pagesMissing ? 1 : 0;
		pagePairs += pagesMissing;

		int missing = 0;
		for (int type : needed)
		{
			if (!models.areaModels[area].count(type))
			{
				missing++;
				missingModels.insert(type);
			}
		}

		checked++;
		missingRegions += missing ? 1 : 0;
		missingPairs += missing;
	}

	Log(log, "map: D1 %dx%d cells (%d regions, %d present) -> D2 %dx%d cells (%dx%d regions, %d present)", h.cells_across, h.cells_down,
		d1RegionsAcross * d1RegionsDown, d1Present, g.across, g.down, g.regionsAcross, g.regionsDown, presentRegions);
	Log(log, "map: %d unique objects, %d cell references, %d straddlers, %d outside the grid, %d cell slots", (int)uniq.size(), totalRefs,
		(int)straddlers.size(), outside, totalSlots);
	Log(log, "map: cell object index space %d of %d, spooled section %d sectors", objectTotal, kMaxCellIndex + 1, (int)(out.spool.size() / kSector));
	Log(log, "heights: %d cells, %d with split planes, %d planes, %d BSP nodes, %d overflow, max fit error %d, %d steep planes, %d regions with coarse planes", heightStats.cells,
		heightStats.splitCells, heightStats.planes, heightStats.nodes, heightStats.overflowCells, heightStats.maxFitError, heightStats.steepPlanes, heightStats.coarseRegions);
	Log(log, "map: area coverage: %d regions checked, %d with missing models (%d pairs, %d distinct models)", checked, missingRegions, missingPairs,
		(int)missingModels.size());
	Log(log, "map: texture pages: %d of %d regions use pages that are neither permanent nor in their area (%d pairs)", pageRegions, checked, pagePairs);
	for (const std::pair<const int, int>& entry : pageMissingCount)
		Log(log, "map:   page %d missing in %d regions", entry.first, entry.second);
	return true;
}

// ---------------------------------------------------------------- structure check

int D1Dlc_CheckMap(const LevFile& lev, std::string& log)
{
	int problems = 0;
	auto fail = [&](const char* format, auto... args)
	{
		char line[400];
		snprintf(line, sizeof(line), format, args...);
		Log(log, "PROBLEM: %s", line);
		problems++;
	};

	D1Map map;
	D1Spool si;

	if (!ParseMapLump(lev, false, map) || !ParseSpoolInfo(lev, si))
	{
		fail("MAP or SPOOLINFO lump missing or unreadable");
		return problems;
	}

	const OUT_CELL_FILE_HEADER& h = map.hdr;
	if (h.cell_size != D2_CELL_SIZE || h.region_size != D2_REGION_CELLS || h.cells_across % h.region_size || h.cells_down % h.region_size)
	{
		fail("map header: cells %dx%d, cell size %d, region size %d", h.cells_across, h.cells_down, h.cell_size, h.region_size);
		return problems;
	}

	const int regionsAcross = h.cells_across / h.region_size;
	const int numRegions = regionsAcross * (h.cells_down / h.region_size);
	if (h.num_regions != numRegions || (int)si.offsets.size() != numRegions)
		fail("region count: header %d, spool info %d, expected %d", h.num_regions, (int)si.offsets.size(), numRegions);

	int numStraddlers = 0;
	{
		const LevLump* lump = lev.FindLump(LUMP_MAP);
		numStraddlers = *(const int*)lev.Data(lump->offset + (int)sizeof(OUT_CELL_FILE_HEADER));
	}

	const OUT_CITYLUMP_INFO& li = lev.GetLumpInfo();
	const int64_t spoolEnd = (int64_t)li.spooled_offset + li.spooled_size;
	int objectBase[4];
	int total = numStraddlers;
	for (int b = 0; b < 4; b++)
	{
		objectBase[b] = total;
		total += si.objects[b];
	}

	if (total > kMaxCellIndex + 1)
		fail("cell object index space %d exceeds 14 bits", total);

	for (size_t a = 0; a < si.areas.size(); a++)
	{
		const AreaDataStr& area = si.areas[a];
		if ((int64_t)li.spooled_offset + ((int64_t)area.gfx_offset + area.num_tpages * 17) * kSector > spoolEnd ||
			(int64_t)li.spooled_offset + ((int64_t)area.model_offset + area.model_size) * kSector > spoolEnd)
			fail("area %d lies outside the spooled section", (int)a);

		if (area.num_tpages > 16)
			fail("area %d has %d texture pages", (int)a, area.num_tpages);
	}

	int present = 0, maxSlots = 0;
	for (int r = 0; r < (int)si.offsets.size(); r++)
	{
		const Spool* sp = si.RegionSpool(r);
		if (!sp)
		{
			if (si.offsets[r] != kEmptyOffset)
				fail("region %d: spool info offset %d is outside the region info", r, si.offsets[r]);
			continue;
		}

		present++;
		const int barrel = (r % regionsAcross & 1) + ((r / regionsAcross) & 1) * 2;
		const int64_t base = (int64_t)li.spooled_offset + (int64_t)sp->offset * kSector;
		const int64_t ptrOfs = base;
		const int64_t cellOfs = ptrOfs + sp->cell_data_size[1] * kSector;
		const int64_t objOfs = cellOfs + sp->cell_data_size[0] * kSector;
		const int64_t roadOfs = objOfs + sp->cell_data_size[2] * kSector;
		const int64_t end = roadOfs + sp->roadm_size * kSector;

		if (end > spoolEnd)
		{
			fail("region %d: data ends %d bytes past the spooled section", r, (int)(end - spoolEnd));
			continue;
		}

		if (sp->super_region != 0xFF && sp->super_region >= si.areas.size())
			fail("region %d: super region %d does not exist", r, sp->super_region);

		for (int i = 0; i < sp->num_connected_areas && i < 2; i++)
		{
			if ((sp->connected_areas[i] & 0x3F) >= si.areas.size())
				fail("region %d: connected area %d does not exist", r, sp->connected_areas[i] & 0x3F);
		}

		std::vector<uint16_t> ptrs;
		if (!DecodeCellPointers(lev.Data((int)ptrOfs), sp->cell_data_size[1] * kSector, kCellsPerRegion, ptrs))
		{
			fail("region %d: bad cell pointers", r);
			continue;
		}

		const int numSlots = sp->cell_data_size[0] * kSector / 2;
		maxSlots = std::max(maxSlots, numSlots);
		const uint16_t* cells = (const uint16_t*)lev.Data((int)cellOfs);
		const int objectLimit = barrel < 3 ? objectBase[barrel + 1] : total;

		for (int c = 0; c < kCellsPerRegion; c++)
		{
			int index = ptrs[c];
			if (index == 0xFFFF)
				continue;

			bool ended = false;
			for (; index < numSlots; index++)
			{
				int num = cells[index] & kMaxCellIndex;
				if (num >= numStraddlers && (num < objectBase[barrel] || num >= objectLimit))
				{
					fail("region %d cell %d: object index %d is outside its barrel", r, c, num);
					break;
				}

				if (cells[index] & 0x8000)
				{
					ended = true;
					break;
				}
			}

			if (!ended)
			{
				fail("region %d cell %d: cell list is not terminated", r, c);
				break;
			}
		}

		if (sp->cell_data_size[0] * 1024 > si.slots[barrel] || sp->cell_data_size[2] * (kSector / 8) > si.objects[barrel])
			fail("region %d: cell data does not fit the barrel space", r);

		// road block: int offset of the height map, PVS offsets, height map starting with the value 2
		int cbr = *(const int*)lev.Data((int)roadOfs);
		if (cbr < 0 || cbr + 8200 > sp->roadm_size * kSector || *(const int16_t*)lev.Data((int)roadOfs + 4 + cbr) != 2)
			fail("region %d: road block (offset %d) has no height map", r, cbr);
	}

	Log(log, "map: %d cells across, %d regions (%d present), %d straddlers, %d areas, %d problems", h.cells_across, numRegions, present,
		numStraddlers, (int)si.areas.size(), problems);
	return problems;
}

// ---------------------------------------------------------------- height verification

bool D1Dlc_VerifyHeights(const LevFile& d1, const LevFile& d2, int step, std::string& log)
{
	D1Map d1Map, d2Map;
	D1Spool d1Spool, d2Spool;
	D1Models models;
	D1RoadMap roadMap;
	D1HeightField field;

	if (!ParseMapLump(d1, true, d1Map) || !ParseSpoolInfo(d1, d1Spool) || !ParseModelInfo(d1, d1Spool, models) ||
		!LoadRoadMap(d1, d1Map, d1Spool, roadMap, log) || !field.Init(d1, std::move(roadMap), D1ModelFlags{ models.shape, models.flags2, models.topY }) ||
		!ParseMapLump(d2, false, d2Map) || !ParseSpoolInfo(d2, d2Spool))
	{
		Log(log, "ERROR: cannot read the D1 or D2 map data");
		return false;
	}

	D1RoadTable roadTable;
	if (!roadTable.Build(d1, field.RoadMap(), log))
		return false;

	field.SetRoads(&roadTable);

	std::vector<D1Object> objects;
	int objectRegions = 0;
	if (!ReadAllObjects(d1, d1Map, d1Spool, objects, objectRegions, log))
		return false;

	MarkSeaCells(objects, models, field);

	const int regionsAcross = d2Map.hdr.cells_across / D2_REGION_CELLS;
	const int regionsDown = d2Map.hdr.cells_down / D2_REGION_CELLS;
	const int regionUnits = D2_REGION_CELLS * D2_CELL_SIZE;
	const OUT_CITYLUMP_INFO& li = d2.GetLumpInfo();

	int64_t samples = 0, missing = 0, big = 0, surfaceDiff = 0, roadSamples = 0, roadBig = 0;
	int maxError = 0, maxRoadError = 0, worstX = 0, worstZ = 0;
	std::vector<std::tuple<int, int, int, int, int, int>> worst;	// error, x, z, want, got, surface

	const int minX = -d2Map.hdr.cells_across / 2 * D2_CELL_SIZE, minZ = -d2Map.hdr.cells_down / 2 * D2_CELL_SIZE;

	for (int vz = minZ + 100; vz < -minZ; vz += step)
	{
		for (int vx = minX + 100; vx < -minX; vx += step)
		{
			D1Ground want;
			if (!field.Eval(vx, vz, want))
				continue;

			const int kx = FloorDiv(vx - 512, regionUnits), kz = FloorDiv(vz - 512, regionUnits);
			const int rx = kx + regionsAcross / 2, rz = kz + regionsDown / 2;
			const Spool* sp = rx >= 0 && rz >= 0 && rx < regionsAcross && rz < regionsDown ? d2Spool.RegionSpool(rx + rz * regionsAcross) : nullptr;

			samples++;
			D1Ground got;
			bool ok = false;

			if (sp)
			{
				const int64_t roadOfs = (int64_t)li.spooled_offset + ((int64_t)sp->offset + sp->cell_data_size[0] + sp->cell_data_size[1] + sp->cell_data_size[2]) * kSector;
				ok = roadOfs + sp->roadm_size * kSector <= d2.Size() &&
					SampleRoadBlock(d2.Data((int)roadOfs), sp->roadm_size * kSector, vx - kx * regionUnits, vz - kz * regionUnits, got);
			}

			if (!ok)
			{
				missing++;
				continue;
			}

			// the D1 tile edges carry slivers of wrong heights: accept the value of any point 24 units around
			int error = std::abs(got.y - want.y);
			bool sameSurface = got.surface == want.surface;
			const int around[4][2] = { { 24, 0 }, { -24, 0 }, { 0, 24 }, { 0, -24 } };
			for (const auto& d : around)
			{
				D1Ground other;
				if (error > 32 && field.Eval(vx + d[0], vz + d[1], other))
				{
					error = std::min(error, std::abs(got.y - other.y));
					sameSurface |= got.surface == other.surface;
				}
			}

			const bool road = want.surface >= 32;
			surfaceDiff += !sameSurface;
			roadSamples += road;

			if (error > maxError)
			{
				maxError = error;
				worstX = vx;
				worstZ = vz;
			}

			big += error > 32;
			if (error > 32 && (worst.size() < 400))
				worst.push_back(std::make_tuple(error, vx, vz, want.y, got.y, want.surface));

			if (road)
			{
				maxRoadError = std::max(maxRoadError, error);
				roadBig += error > 32;
			}
		}
	}

	std::sort(worst.rbegin(), worst.rend());
	for (size_t i = 0; i < worst.size() && i < 12; i++)
		Log(log, "  worst: error %d at %d,%d want y %d got y %d surface %d", std::get<0>(worst[i]), std::get<1>(worst[i]), std::get<2>(worst[i]),
			std::get<3>(worst[i]), std::get<4>(worst[i]), std::get<5>(worst[i]));

	if (!worst.empty() && (roadBig || surfaceDiff || missing))
	{
		const int wx = std::get<1>(worst[0]), wz = std::get<2>(worst[0]);
		Log(log, "  grid around %d,%d (d1/d2 y, rows = z, columns = x, step 128):", wx, wz);

		for (int dz = -512; dz <= 512; dz += 128)
		{
			std::string row;
			for (int dx = -512; dx <= 512; dx += 128)
			{
				D1Ground g;
				const bool has = field.Eval(wx + dx, wz + dz, g);
				const int kx = FloorDiv(wx + dx - 512, regionUnits), kz = FloorDiv(wz + dz - 512, regionUnits);
				const Spool* sp = d2Spool.RegionSpool(kx + regionsAcross / 2 + (kz + regionsDown / 2) * regionsAcross);
				D1Ground got{ 0, 0 };
				bool ok = false;
				if (sp)
				{
					const int64_t roadOfs = (int64_t)li.spooled_offset + ((int64_t)sp->offset + sp->cell_data_size[0] + sp->cell_data_size[1] + sp->cell_data_size[2]) * kSector;
					ok = SampleRoadBlock(d2.Data((int)roadOfs), sp->roadm_size * kSector, wx + dx - kx * regionUnits, wz + dz - kz * regionUnits, got);
				}

				char cell[32];
				snprintf(cell, sizeof(cell), "%5d/%-5d", has ? g.y : 9999, ok ? got.y : 9999);
				row += cell;
			}

			Log(log, "  %s", row.c_str());
		}
	}

	Log(log, "heights: %lld samples (%lld on roads), %lld without D2 data, max error %d at %d,%d, %lld above 32 units (roads: max %d, %lld above 32), %lld surface differences",
		(long long)samples, (long long)roadSamples, (long long)missing, maxError, worstX, worstZ, (long long)big, maxRoadError, (long long)roadBig, (long long)surfaceDiff);
	return missing == 0 && maxRoadError <= 48 && surfaceDiff == 0;
}

bool D1Dlc_ProbeGround(const LevFile& d1, const LevFile& d2, int x, int z, std::string& log)
{
	D1Map d1Map, d2Map;
	D1Spool d1Spool, d2Spool;
	D1Models models;
	D1RoadMap roadMap;
	D1HeightField field;

	if (!ParseMapLump(d1, true, d1Map) || !ParseSpoolInfo(d1, d1Spool) || !ParseModelInfo(d1, d1Spool, models) ||
		!LoadRoadMap(d1, d1Map, d1Spool, roadMap, log) || !field.Init(d1, std::move(roadMap), D1ModelFlags{ models.shape, models.flags2, models.topY }) ||
		!ParseMapLump(d2, false, d2Map) || !ParseSpoolInfo(d2, d2Spool))
	{
		Log(log, "ERROR: cannot read the D1 or D2 map data");
		return false;
	}

	D1RoadTable roadTable;
	if (!roadTable.Build(d1, field.RoadMap(), log))
		return false;

	field.SetRoads(&roadTable);

	std::vector<D1Object> objects;
	int objectRegions = 0;
	if (!ReadAllObjects(d1, d1Map, d1Spool, objects, objectRegions, log))
		return false;

	MarkSeaCells(objects, models, field);

	const int regionsAcross = d2Map.hdr.cells_across / D2_REGION_CELLS;
	const int regionsDown = d2Map.hdr.cells_down / D2_REGION_CELLS;
	const int regionUnits = D2_REGION_CELLS * D2_CELL_SIZE;
	const OUT_CITYLUMP_INFO& li = d2.GetLumpInfo();

	D1Ground want;
	const bool hasWant = field.Eval(x, z, want);

	const int kx = FloorDiv(x - 512, regionUnits), kz = FloorDiv(z - 512, regionUnits);
	const int rx = kx + regionsAcross / 2, rz = kz + regionsDown / 2;
	const Spool* sp = rx >= 0 && rz >= 0 && rx < regionsAcross && rz < regionsDown ? d2Spool.RegionSpool(rx + rz * regionsAcross) : nullptr;

	D1Ground got{ 0, 0 };
	bool hasGot = false;
	if (sp)
	{
		const int64_t roadOfs = (int64_t)li.spooled_offset + ((int64_t)sp->offset + sp->cell_data_size[0] + sp->cell_data_size[1] + sp->cell_data_size[2]) * kSector;
		hasGot = SampleRoadBlock(d2.Data((int)roadOfs), sp->roadm_size * kSector, x - kx * regionUnits, z - kz * regionUnits, got);
	}

	Log(log, "probe %d,%d: D1 %s y %d surface %d, D2 %s y %d surface %d (region %d,%d)", x, z, hasWant ? "ok" : "none", want.y, want.surface,
		hasGot ? "ok" : "none", got.y, got.surface, rx, rz);
	return hasWant && hasGot;
}
