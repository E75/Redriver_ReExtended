#include "d1_convert.h"
#include "d1_map.h"
#include "d1_roads.h"
#include "d1_util.h"

#include <map>

static_assert(sizeof(TEXINF) == 8, "TEXINF");
static_assert(sizeof(TEXPAGE_POS) == 8, "TEXPAGE_POS");

namespace
{

// Driver 2 donor content every converted city needs (Tanner, pedestrians, wheels, effect textures).
// Page numbers are those of Driver 2 CHICAGO.LEV: 1 pedestrian head + SEA, 2 effects (FLARE, LIGHT, SKYMOON, ...),
// 50 wheels + hubcap, 58 + 63 Tanner body parts. Page 51 (OVERHEAD, the overlay map) is added in Part 7.
const int kDonorPages[] = { 1, 2, 50, 58, 63 };

// Newcastle (CREDITS) and Training (TRAIN) keep their whole level on 12 permanent pages: all five donor pages would exceed the
// engine limit of 16. Such a "lean" city drops the donor effects page (LIGHT, LENSFLR, SKYMOON become aliases of D1 textures)
// and the D1 player car pages when permanent + special pages exceed the texture slots (Newcastle: the city then plays with
// the civilian cars, like a D1 city with 0 player cars).
const int kLeanSkippedDonorPage = 2;
const int kPermPageLimit = 16;
const int kTextureSlotLimit = 20;		// numTpageSlots of a Driver 1 city (texture.c)

const char* const kDonorModels[] = {
	"HEAD", "NECK", "TORSO", "HIPS", "U_ARM_LEFT", "U_ARM_RIGHT", "L_ARM_LEFT", "L_ARM_RIGHT", "HAND_LEFT", "HAND_RIGHT",
	"THIGH_LEFT", "THIGH_RIGHT", "CALF_LEFT", "CALF_RIGHT", "FOOT_LEFT", "FOOT_RIGHT",
	"PEDHEAD1", "GREENCONE", "CLEANWHEEL", "FASTWHEEL", "DAMWHEEL", "HUBCAP1",
};

// textures the engine looks up by name (GetTextureDetails / GetTextureInfoName). D1 names its flare FLARE1 and has no
// Tanner shadow: TSHADOW becomes an alias of CARSHAD.
const char* const kRequiredTextures[] = {
	"FLARE", "HEAD1", "LENSFLR", "LIGHT", "SKYMOON", "SEA", "TSHADOW", "SMOKE", "DEBRIS", "LITTER", "SKID", "SKYSUN",
	"DROP", "DIGIT", "JEANS1", "ARM1", "FOREARM1", "CHEST1", "LOWHEAD", "LIGHTREF", "SWBIRD1", "SWBIRD2",
};

const char* const kRequiredModels[] = {
	"HEAD", "NECK", "TORSO", "HIPS", "U_ARM_LEFT", "U_ARM_RIGHT", "L_ARM_LEFT", "L_ARM_RIGHT", "HAND_LEFT", "HAND_RIGHT",
	"THIGH_LEFT", "THIGH_RIGHT", "CALF_LEFT", "CALF_RIGHT", "FOOT_LEFT", "FOOT_RIGHT",
	"PEDHEAD1", "GREENCONE", "CLEANWHEEL", "FASTWHEEL", "DAMWHEEL", "HUBCAP1",
};

const bool kReplaceWheels = true;			// test switch for the wheel models
const int kMaxPages = 128;
const int kMaxModels = 1536;				// MAX_MODEL_SLOTS
const int kMaxStoredCluts = 320;			// palette cache of ProcessPalletLump
const int kClutBytes = 32;					// 16 colours
const int kMaxPalettes = 5;					// civ_clut[][][6]: the base colour + 5 variants
const int kMaxTexturesPerPage = 32;			// civ_clut[][32][]
const int kD2LastCivilianCar = 4;			// car model numbers of the D2 engine: 0 cop, 1..4 civilians, 8..12 special cars
const int kD2FirstSpecialCar = 8;
const int kD1FirstPlayerCar = 5;			// D1: 0 cop, 1..2 civilians, 5.. player cars
const int kD1OverlayOffsets = 164;			// D1 overlay map: offset table of 164 shorts, then the CLUT at 328
const int kD1OverlayClut = 328;
const int kD2OverlayClut = 512;				// D2: offset table of 256 shorts
const int kOverlayClutSize = 32;

struct XYPair
{
	int32_t x, y;
};

struct TexPage
{
	TEXPAGE_POS			pos;
	std::vector<TEXINF>	tex;
};

// LUMP_TEXTUREINFO
struct TexInfo
{
	std::vector<TexPage>	pages;
	int						numPerm{ 0 };
	XYPair					perm[16] = {};		// x = page, y = size of the page data in bytes
	int						numSpec{ 0 };
	XYPair					spec[16] = {};
};

bool IsLeanCity(const TexInfo& d1Tex)
{
	return d1Tex.numPerm + (int)(sizeof(kDonorPages) / sizeof(kDonorPages[0])) > kPermPageLimit;
}

bool ParseTexInfo(const LevFile& lev, const LevLump& lump, TexInfo& out)
{
	Reader r(lev.Data(lump.offset), lump.size);

	int pageCount = r.Get<int>();
	r.Get<int>();	// total texture count, recomputed on write

	if (pageCount <= 0 || pageCount > kMaxPages)
		return false;

	std::vector<TEXPAGE_POS> positions(pageCount + 1);
	r.Read(positions.data(), (pageCount + 1) * sizeof(TEXPAGE_POS));

	out.pages.resize(pageCount);
	for (int i = 0; i < pageCount; i++)
	{
		int texCount = r.Get<int>();
		if (texCount < 0 || texCount > 256)
			return false;

		out.pages[i].pos = positions[i];
		out.pages[i].tex.resize(texCount);
		r.Read(out.pages[i].tex.data(), texCount * sizeof(TEXINF));
	}

	out.numPerm = r.Get<int>();
	r.Read(out.perm, sizeof(out.perm));
	out.numSpec = r.Get<int>();
	r.Read(out.spec, sizeof(out.spec));

	return r.Ok() && out.numPerm >= 0 && out.numPerm <= 16 && out.numSpec >= 0 && out.numSpec <= 16;
}

std::vector<char> WriteTexInfo(const TexInfo& info)
{
	std::vector<char> out;
	int total = 0;
	for (const TexPage& page : info.pages)
		total += (int)page.tex.size();

	Put<int>(out, (int)info.pages.size());
	Put<int>(out, total);

	for (const TexPage& page : info.pages)
		Put(out, page.pos);

	Put(out, TEXPAGE_POS{ 0, 0 });		// terminating entry, never read

	for (const TexPage& page : info.pages)
	{
		Put<int>(out, (int)page.tex.size());
		Put(out, page.tex.data(), page.tex.size() * sizeof(TEXINF));
	}

	Put<int>(out, info.numPerm);
	Put(out, info.perm, sizeof(info.perm));
	Put<int>(out, info.numSpec);
	Put(out, info.spec, sizeof(info.spec));

	return out;
}

// texture names: null terminated strings, TEXINF.nameoffset indexes into the blob
const char* NameAt(const std::vector<char>& names, int offset)
{
	return (offset >= 0 && offset < (int)names.size()) ? names.data() + offset : "";
}

int FindName(const std::vector<char>& names, const char* name)
{
	for (int ofs = 0; ofs < (int)names.size(); ofs += (int)strlen(names.data() + ofs) + 1)
	{
		if (!strcmp(names.data() + ofs, name))
			return ofs;
	}

	return -1;
}

int AddName(std::vector<char>& names, const char* name)
{
	int found = FindName(names, name);
	if (found >= 0)
		return found;

	int ofs = (int)names.size();
	Put(names, name, strlen(name) + 1);
	return ofs;
}

std::vector<char> LumpBytes(const LevFile& lev, const LevLump* lump)
{
	if (!lump)
		return std::vector<char>();

	return std::vector<char>(lev.Data(lump->offset), lev.Data(lump->offset + lump->size));
}

// ---------------------------------------------------------------- permanent texture pages

int AlignedPageSize(int size)
{
	return (size + LEV_SECTOR_SIZE - 1) & ~(LEV_SECTOR_SIZE - 1);
}

// byte range of permanent page number n (index in the perm list) inside the tpage section
bool PermPageRange(const LevFile& lev, const TexInfo& info, int n, int& start, int& size)
{
	start = 0;
	for (int i = 0; i < n; i++)
		start += AlignedPageSize(info.perm[i].y);

	size = AlignedPageSize(info.perm[n].y);
	return start + size <= (int)lev.GetLumpInfo().tpage_size;
}

int FindPermIndex(const TexInfo& info, int page)
{
	for (int i = 0; i < info.numPerm; i++)
	{
		if (info.perm[i].x == page)
			return i;
	}

	return -1;
}

// ---------------------------------------------------------------- models

struct ModelSet
{
	std::vector<std::vector<char>>	models;		// empty = not resident
	std::vector<std::string>		names;		// one per model
};

bool ParseModels(const LevFile& lev, ModelSet& out)
{
	const LevLump* lump = lev.FindLump(LUMP_MODELS);
	const LevLump* nameLump = lev.FindLump(LUMP_MODELNAMES);
	if (!lump || !nameLump)
		return false;

	Reader r(lev.Data(lump->offset), lump->size);
	int count = r.Get<int>();
	if (count < 0 || count > kMaxModels)
		return false;

	out.models.resize(count);
	for (int i = 0; i < count; i++)
	{
		int size = r.Get<int>();
		if (size < 0 || size > lump->size)
			return false;

		out.models[i].resize(size);
		r.Read(out.models[i].data(), size);
	}

	// one string per model
	const char* p = lev.Data(nameLump->offset);
	const char* end = p + nameLump->size;
	out.names.resize(count);
	for (int i = 0; i < count && p < end; i++)
	{
		out.names[i] = p;
		p += strlen(p) + 1;
	}

	return r.Ok();
}

int FindModel(const ModelSet& set, const char* name)
{
	for (size_t i = 0; i < set.names.size(); i++)
	{
		if (set.names[i] == name)
			return (int)i;
	}

	return -1;
}

// walks the polygon list of a model, calls fn(polyPointer, type) for each one
template<typename Fn>
bool ForEachPoly(std::vector<char>& model, Fn fn)
{
	if (model.size() < sizeof(MODEL))
		return false;

	const MODEL& header = *(const MODEL*)model.data();
	int ofs = header.poly_block;

	for (int i = 0; i < header.num_polys; i++)
	{
		if (ofs < 0 || ofs >= (int)model.size())
			return false;

		int type = (unsigned char)model[ofs] & 31;
		if (kPolySizes[type] == 0 || ofs + kPolySizes[type] > (int)model.size())
			return false;

		fn((unsigned char*)&model[ofs], type);
		ofs += kPolySizes[type];
	}

	return true;
}

// points the texture_set of every textured polygon to the new page numbers; returns false if a page is unknown
// A lean city has no donor effects page: its polygons use texture fallbackTex of page fallbackPage instead (fallbackPage -1 = none).
bool RemapModelPages(std::vector<char>& model, const std::map<int, int>& pageMap, int fallbackPage, int fallbackTex)
{
	bool ok = true;

	bool walked = ForEachPoly(model, [&](unsigned char* poly, int type)
	{
		if (!IsTexturedPoly(type) || poly[1] == 0xFF)
			return;

		std::map<int, int>::const_iterator it = pageMap.find(poly[1]);
		if (it == pageMap.end() && fallbackPage >= 0 && poly[1] == kLeanSkippedDonorPage)
		{
			poly[1] = (unsigned char)fallbackPage;
			poly[2] = (unsigned char)fallbackTex;
		}
		else if (it == pageMap.end())
			ok = false;
		else
			poly[1] = (unsigned char)it->second;
	});

	return walked && ok;
}

// flags2 bit 0 is a runtime flag of the Driver 2 engine (animated object), Driver 1 uses it for something else
void ClearRuntimeFlags(std::vector<char>& model)
{
	if (model.size() >= sizeof(MODEL))
		((MODEL*)model.data())->flags2 &= ~1;
}

std::vector<char> WriteModels(const ModelSet& set)
{
	std::vector<char> out;
	Put<int>(out, (int)set.models.size());

	for (const std::vector<char>& model : set.models)
	{
		Put<int>(out, (int)model.size());
		Put(out, model.data(), model.size());
	}

	return out;
}

std::vector<char> WriteModelNames(const ModelSet& set)
{
	std::vector<char> out;
	for (const std::string& name : set.names)
		Put(out, name.c_str(), name.size() + 1);

	return out;
}

// LOWDETAIL: two arrays of one ushort per model (high detail, low detail), 0xFFFF = none
std::vector<char> ExtendLowDetail(const LevFile& lev, int oldCount, int newCount)
{
	std::vector<char> out;
	const LevLump* lump = lev.FindLump(LUMP_LOWDETAILTABLE);

	for (int table = 0; table < 2; table++)
	{
		for (int i = 0; i < newCount; i++)
		{
			uint16_t value = 0xFFFF;
			if (lump && i < oldCount && lump->size >= (table + 1) * oldCount * 2)
				memcpy(&value, lev.Data(lump->offset + (table * oldCount + i) * 2), 2);

			Put(out, value);
		}
	}

	return out;
}

// ---------------------------------------------------------------- overlay map

// D1: offsets[164], CLUT at 328. D2: offsets[256], CLUT at 512. The compressed map tiles are copied unchanged.
std::vector<char> ConvertOverlayMap(const LevFile& lev, const LevLump* lump)
{
	std::vector<char> out;
	if (!lump || lump->size < kD1OverlayClut + kOverlayClutSize)
		return out;

	const char* src = lev.Data(lump->offset);
	const int shift = kD2OverlayClut - kD1OverlayClut;

	uint16_t firstTile;
	memcpy(&firstTile, src, 2);

	for (int i = 0; i < 256; i++)
	{
		uint16_t ofs = firstTile;
		if (i < kD1OverlayOffsets)
			memcpy(&ofs, src + i * 2, 2);

		// the end of the D1 offset table holds leftovers, point those at the first tile
		if (ofs < kD1OverlayClut + kOverlayClutSize || ofs >= lump->size)
			ofs = firstTile;

		ofs = (uint16_t)(ofs + shift);
		Put(out, ofs);
	}

	Put(out, src + kD1OverlayClut, kOverlayClutSize);
	Put(out, src + kD1OverlayClut + kOverlayClutSize, lump->size - kD1OverlayClut - kOverlayClutSize);
	return out;
}

// ---------------------------------------------------------------- conversion steps

struct ConvertContext
{
	const LevFile&	d1;
	const LevFile&	donor;
	LevWriter&		out;
	std::string&	log;

	TexInfo				d1Tex, donorTex;
	std::vector<char>	d1Names, donorNames;
	std::map<int, int>	pageMap;		// donor page -> page in the converted LEV
	int					leanFallbackPage{ -1 }, leanFallbackTex{ 0 };		// lean city: stand-in for the skipped donor page
};

// adds the donor pages: texture entries, names and the permanent page data
bool MergeDonorTextures(ConvertContext& c, TexInfo& tex, std::vector<char>& names)
{
	const int d1PageCount = (int)c.d1Tex.pages.size();

	for (int donorPage : kDonorPages)
	{
		// lean city: the effects page is replaced by aliases of D1 textures (AddLeanEffectAliases)
		if (IsLeanCity(c.d1Tex) && donorPage == kLeanSkippedDonorPage)
			continue;

		int permIndex = FindPermIndex(c.donorTex, donorPage);
		int start, size;

		if (donorPage >= (int)c.donorTex.pages.size() || permIndex < 0 || !PermPageRange(c.donor, c.donorTex, permIndex, start, size))
		{
			Log(c.log, "ERROR: donor page %d is not a permanent page of the donor LEV", donorPage);
			return false;
		}

		if (tex.numPerm >= 16 || (int)tex.pages.size() >= kMaxPages)
		{
			Log(c.log, "ERROR: out of texture pages (permanent %d, pages %d)", tex.numPerm, (int)tex.pages.size());
			return false;
		}

		TexPage page = c.donorTex.pages[donorPage];
		for (TEXINF& t : page.tex)
			t.nameoffset = (uint16_t)AddName(names, NameAt(c.donorNames, t.nameoffset));

		c.pageMap[donorPage] = (int)tex.pages.size();

		tex.perm[tex.numPerm].x = (int)tex.pages.size();
		tex.perm[tex.numPerm].y = c.donorTex.perm[permIndex].y;
		tex.numPerm++;
		tex.pages.push_back(page);

		const char* data = c.donor.Data(c.donor.GetLumpInfo().tpage_offset + start);
		c.out.tpages.insert(c.out.tpages.end(), data, data + size);
	}

	Log(c.log, "textures: %d D1 pages + %d donor pages, %d permanent pages", d1PageCount, (int)(tex.pages.size() - d1PageCount), tex.numPerm);
	return true;
}

// Driver 1 has no Tanner shadow: TSHADOW is the car shadow texture on a permanent page
bool AddTextureAlias(ConvertContext& c, TexInfo& tex, std::vector<char>& names, const char* alias, const char* target)
{
	int targetOfs = FindName(names, target);

	for (int p = 0; targetOfs >= 0 && p < (int)c.d1Tex.pages.size(); p++)
	{
		if (FindPermIndex(c.d1Tex, p) < 0)
			continue;

		for (size_t j = 0; j < tex.pages[p].tex.size(); j++)
		{
			if (tex.pages[p].tex[j].nameoffset != targetOfs)
				continue;

			TEXINF copy = tex.pages[p].tex[j];
			copy.nameoffset = (uint16_t)AddName(names, alias);
			tex.pages[p].tex.push_back(copy);
			return true;
		}
	}

	Log(c.log, "ERROR: cannot alias texture %s to %s (not on a permanent page)", alias, target);
	return false;
}

bool ConvertTextures(ConvertContext& c)
{
	const LevLump* d1InfoLump = c.d1.FindLump(LUMP_TEXTUREINFO);
	const LevLump* d1NameLump = c.d1.FindLump(LUMP_TEXTURENAMES);
	const LevLump* donorInfoLump = c.donor.FindLump(LUMP_TEXTUREINFO);
	const LevLump* donorNameLump = c.donor.FindLump(LUMP_TEXTURENAMES);

	if (!d1InfoLump || !d1NameLump || !donorInfoLump || !donorNameLump ||
		!ParseTexInfo(c.d1, *d1InfoLump, c.d1Tex) || !ParseTexInfo(c.donor, *donorInfoLump, c.donorTex))
	{
		Log(c.log, "ERROR: texture lumps missing or invalid");
		return false;
	}

	c.d1Names = LumpBytes(c.d1, d1NameLump);
	c.donorNames = LumpBytes(c.donor, donorNameLump);

	TexInfo tex = c.d1Tex;
	std::vector<char> names = c.d1Names;

	// permanent pages of the city, in perm list order. The special (player car) pages follow the donor pages below.
	for (int i = 0; i < tex.numPerm; i++)
	{
		int start, size;
		if (!PermPageRange(c.d1, c.d1Tex, i, start, size))
		{
			Log(c.log, "ERROR: permanent page %d lies outside the tpage section", c.d1Tex.perm[i].x);
			return false;
		}

		const char* data = c.d1.Data(c.d1.GetLumpInfo().tpage_offset + start);
		c.out.tpages.insert(c.out.tpages.end(), data, data + size);
	}

	if (!MergeDonorTextures(c, tex, names) || !AddTextureAlias(c, tex, names, "TSHADOW", "CARSHAD"))
		return false;

	if (IsLeanCity(c.d1Tex))
	{
		if (!AddTextureAlias(c, tex, names, "FLARE", "FLARE1") || !AddTextureAlias(c, tex, names, "LENSFLR", "FLARE1") ||
			!AddTextureAlias(c, tex, names, "LIGHT", "FLARE1") || !AddTextureAlias(c, tex, names, "SKYMOON", "SKYSUN"))
			return false;

		int flareOfs = FindName(names, "FLARE1");
		for (int p = 0; p < (int)tex.pages.size(); p++)
		{
			for (int j = 0; j < (int)tex.pages[p].tex.size() && c.leanFallbackPage < 0; j++)
			{
				if (tex.pages[p].tex[j].nameoffset == flareOfs)
				{
					c.leanFallbackPage = p;
					c.leanFallbackTex = j;
				}
			}
		}

		// player car pages that do not fit into the texture slots next to the permanent pages are dropped: their data is not
		// copied and the special list stays empty (the city plays with its civilian cars)
		if (tex.numPerm + tex.numSpec > kTextureSlotLimit)
		{
			tex.numSpec = 0;
			memset(tex.spec, 0, sizeof(tex.spec));
		}

		Log(c.log, "textures: lean city, donor effects page replaced by aliases, %d player car pages kept", tex.numSpec);
	}

	// special pages (two per player car, in the D1 spec list order) sit behind the permanent pages of the D1 tpage section
	int specStart = 0;
	for (int i = 0; i < c.d1Tex.numPerm; i++)
		specStart += AlignedPageSize(c.d1Tex.perm[i].y);

	for (int i = 0; i < tex.numSpec; i++)
	{
		int size = AlignedPageSize(tex.spec[i].y);
		if (tex.spec[i].x < 0 || tex.spec[i].x >= (int)tex.pages.size() || specStart + size > (int)c.d1.GetLumpInfo().tpage_size)
		{
			Log(c.log, "ERROR: special page entry %d lies outside the tpage section", i);
			return false;
		}

		const char* data = c.d1.Data(c.d1.GetLumpInfo().tpage_offset + specStart);
		c.out.tpages.insert(c.out.tpages.end(), data, data + size);
		specStart += size;
	}

	Log(c.log, "textures: %d special pages", tex.numSpec);

	c.out.AddLump(LEV_SECTION_LOADTIME, LUMP_TEXTURENAMES, names.data(), (int)names.size());
	std::vector<char> info = WriteTexInfo(tex);
	c.out.AddLump(LEV_SECTION_LOADTIME, LUMP_TEXTUREINFO, info.data(), (int)info.size());
	return true;
}

// D1 wheels have spoke rims: the D2 wheel keeps its layout and takes the rim texture of the D1 hub faces (polys 2 and 3 in
// both); wheels are drawn everywhere, so the rim page must be permanent.
void CopyD1Rim(std::vector<char>& model, std::vector<char>& d1Model, const TexInfo& d1Tex)
{
	const int kPolyFT4 = 21, kUvOffset = 8, kUvBytes = 8;
	unsigned char rim[2][2 + kUvBytes];
	int found = 0, n = 0;

	ForEachPoly(d1Model, [&](unsigned char* poly, int type)
	{
		if ((n == 2 || n == 3) && type == kPolyFT4 && FindPermIndex(d1Tex, poly[1]) >= 0)
		{
			memcpy(rim[n - 2], poly + 1, 2);
			memcpy(rim[n - 2] + 2, poly + kUvOffset, kUvBytes);
			found++;
		}
		n++;
	});

	if (found != 2)
		return;

	n = 0;
	ForEachPoly(model, [&](unsigned char* poly, int type)
	{
		if ((n == 2 || n == 3) && type == kPolyFT4)
		{
			memcpy(poly + 1, rim[n - 2], 2);
			memcpy(poly + kUvOffset, rim[n - 2] + 2, kUvBytes);
		}
		n++;
	});
}

bool ConvertModels(ConvertContext& c)
{
	ModelSet city, donor;
	if (!ParseModels(c.d1, city) || !ParseModels(c.donor, donor))
	{
		Log(c.log, "ERROR: model lumps missing or invalid");
		return false;
	}

	const int d1Count = (int)city.models.size();
	int roofs = 0, kerbBoxes = 0, modelIndex = 0;
	for (std::vector<char>& model : city.models)
	{
		ClearRuntimeFlags(model);
		ClampBoxHeights(model);
		kerbBoxes += RemoveKerbWallBoxes(model, modelIndex < (int)city.names.size() ? city.names[modelIndex] : std::string());
		modelIndex++;

		if (model.size() >= sizeof(MODEL))
		{
			LowerGrassModel(model.data(), (int)model.size());
			roofs += kRoofCaps ? AddRoofCap(model) : 0;
		}
	}

	Log(c.log, "models: %d resident buildings got a roof, %d kerb wall boxes removed", roofs, kerbBoxes);

	// models 0..3 are the sky dome: the D2 renderer needs its vertex layout (30 vertices, 20 quads), D1's SKY01..04 differ.
	// The sky quads only use the vertex indices, their textures come from the sky files (LoadSky), so the page is cleared.
	for (int i = 0; i < 4; i++)
	{
		char name[16];
		snprintf(name, sizeof(name), "BASE%d", i + 1);

		int index = FindModel(donor, name);
		if (index < 0 || donor.models[index].empty())
		{
			Log(c.log, "ERROR: donor sky model %s not found", name);
			return false;
		}

		std::vector<char> model = donor.models[index];
		ForEachPoly(model, [](unsigned char* poly, int type)
		{
			if (IsTexturedPoly(type))
				poly[1] = 0xFF;
		});

		ClearRuntimeFlags(model);
		city.models[i] = model;
		city.names[i] = name;
	}

	int added = 0;
	for (const char* name : kDonorModels)
	{
		// D1 has its own PEDHEAD1 (other origin, floats above the D2 neck, other texture layout): the D2 head replaces it
		int existing = FindModel(city, name);
		// the wheel models of D1 are laid out differently from what MangleWheelModels (cars.c) expects: the D2 wheels replace them, with the D1 rims
		bool replaceHead = existing >= 0 && (!strcmp(name, "PEDHEAD1") || (kReplaceWheels && (!strcmp(name, "CLEANWHEEL") || !strcmp(name, "FASTWHEEL") || !strcmp(name, "DAMWHEEL"))));

		if (existing >= 0 && !replaceHead)
			continue;

		int index = FindModel(donor, name);
		if (index < 0 || donor.models[index].empty())
		{
			Log(c.log, "ERROR: donor model %s not found", name);
			return false;
		}

		std::vector<char> model = donor.models[index];
		if (((const MODEL*)model.data())->instance_number != -1 || !RemapModelPages(model, c.pageMap, c.leanFallbackPage, c.leanFallbackTex))
		{
			Log(c.log, "ERROR: donor model %s uses a page that is not merged", name);
			return false;
		}

		ClearRuntimeFlags(model);

		if (replaceHead)
		{
			if (strcmp(name, "PEDHEAD1") != 0)
				CopyD1Rim(model, city.models[existing], c.d1Tex);

			city.models[existing] = model;
			continue;
		}

		city.models.push_back(model);
		city.names.push_back(name);
		added++;
	}

	if ((int)city.models.size() > kMaxModels)
	{
		Log(c.log, "ERROR: %d models exceed the engine limit of %d", (int)city.models.size(), kMaxModels);
		return false;
	}

	std::vector<char> models = WriteModels(city);
	std::vector<char> names = WriteModelNames(city);
	std::vector<char> lowDetail = ExtendLowDetail(c.d1, d1Count, (int)city.models.size());

	c.out.AddLump(LEV_SECTION_LOADTIME, LUMP_MODELNAMES, names.data(), (int)names.size());
	c.out.AddLump(LEV_SECTION_INMEM, LUMP_MODELS, models.data(), (int)models.size());
	c.out.AddLump(LEV_SECTION_INMEM, LUMP_LOWDETAILTABLE, lowDetail.data(), (int)lowDetail.size());

	Log(c.log, "models: %d from the city + %d from the donor", d1Count, added);
	return true;
}

// D1 colour variants of the car and pedestrian textures (PALLET_INFO_D1: palette, texnum, tpage + 16 colours) in the
// D2 layout (palette, texnum, tpage, clut_number = -1 + 16 colours, closed by -1). D1 page numbers stay as they are.
std::vector<char> ConvertPalettes(ConvertContext& c)
{
	std::vector<char> out;
	int count = 0;

	if (const LevLump* lump = c.d1.FindLump(LUMP_PALLET))
	{
		Reader r(c.d1.Data(lump->offset), lump->size);
		count = r.Get<int>();

		if (!r.Ok() || count < 0 || count > kMaxStoredCluts)
		{
			Log(c.log, "palettes: invalid count %d, dropped", count);
			count = 0;
		}

		Put<int>(out, count);

		for (int i = 0; i < count; i++)
		{
			int info[3];
			char colours[kClutBytes];
			r.Read(info, sizeof(info));
			r.Read(colours, sizeof(colours));

			if (!r.Ok() || info[0] < 0 || info[0] >= kMaxPalettes || info[1] < 0 || info[1] >= kMaxTexturesPerPage || info[2] < 0 || info[2] >= kMaxPages)
			{
				Log(c.log, "palettes: entry %d is invalid, the rest is dropped", i);
				count = i;
				memcpy(out.data(), &count, sizeof(count));
				break;
			}

			Put(out, info, sizeof(info));
			Put<int>(out, -1);
			Put(out, colours, sizeof(colours));
		}
	}
	else
		Put<int>(out, 0);

	Put<int>(out, -1);
	Log(c.log, "palettes: %d colour variants", count);
	return out;
}

// Driver 2 numbers its cars 0 = cop car, 1..4 civilians, 8..12 "special" cars. Driver 1 has the cop car at 0, civilians at 1
// and 2 and its player cars at 5 and up: they move to 8 and up, the model data stays as it is (the D2 car renderer handles
// the D1 polygon types).
std::vector<char> ConvertCarModels(ConvertContext& c)
{
	const LevLump* lump = c.d1.FindLump(LUMP_CAR_MODELS);
	if (!lump)
		return {};

	std::vector<char> out = LumpBytes(c.d1, lump);
	if (out.size() < sizeof(int) + LEV_MAX_CAR_MODELS * 3 * sizeof(int))
		return out;

	int* table = (int*)(out.data() + sizeof(int));
	int moved[LEV_MAX_CAR_MODELS * 3];

	for (int model = 0; model < LEV_MAX_CAR_MODELS; model++)
	{
		const int from = model <= kD2LastCivilianCar ? model : (model >= kD2FirstSpecialCar ? model - (kD2FirstSpecialCar - kD1FirstPlayerCar) : -1);

		for (int part = 0; part < 3; part++)
			moved[model * 3 + part] = from >= 0 ? table[from * 3 + part] : -1;
	}

	memcpy(table, moved, sizeof(moved));
	return out;
}

void CopyPlainLumps(ConvertContext& c)
{
	std::vector<char> palettes = ConvertPalettes(c);
	c.out.AddLump(LEV_SECTION_LOADTIME, LUMP_PALLET, palettes.data(), (int)palettes.size());

	// Tanner and the pedestrians animate from the donor motion capture data (14 lumps)
	for (int i = 0; const LevLump* lump = c.donor.FindLump(LUMP_MOTIONCAPTURE, i); i++)
		c.out.AddLump(LEV_SECTION_LOADTIME, LUMP_MOTIONCAPTURE, c.donor.Data(lump->offset), lump->size);

	std::vector<char> cars = ConvertCarModels(c);
	if (!cars.empty())
		c.out.AddLump(LEV_SECTION_LOADTIME, LUMP_CAR_MODELS, cars.data(), (int)cars.size());

	std::vector<char> overlay = ConvertOverlayMap(c.d1, c.d1.FindLump(LUMP_OVERLAYMAP));
	if (!overlay.empty())
		c.out.AddLump(LEV_SECTION_INMEM, LUMP_OVERLAYMAP, overlay.data(), (int)overlay.size());

	if (const LevLump* lump = c.d1.FindLump(LUMP_CHAIR))
		c.out.AddLump(LEV_SECTION_INMEM, LUMP_CHAIR, c.d1.Data(lump->offset), lump->size);
}

} // namespace

bool D1Dlc_ConvertLev(const LevFile& d1, const LevFile& donor, LevWriter& out, std::string& log)
{
	if (d1.GetFormat() != LEV_FORMAT_DRIVER1 || donor.GetFormat() != LEV_FORMAT_DRIVER2)
	{
		Log(log, "ERROR: expected a Driver 1 LEV and a Driver 2 donor LEV");
		return false;
	}

	out = LevWriter();
	ConvertContext c{ d1, donor, out, log };

	return ConvertTextures(c) && ConvertModels(c) && D1Dlc_ConvertMap(d1, out, log) && (CopyPlainLumps(c), true);
}

// ---------------------------------------------------------------- structure check

namespace
{

struct CheckContext
{
	const LevFile&	lev;
	std::string&	log;
	int				problems{ 0 };

	CheckContext(const LevFile& l, std::string& g) : lev(l), log(g) {}

	void Fail(const char* format, ...)
	{
		char line[512];
		va_list args;
		va_start(args, format);
		vsnprintf(line, sizeof(line), format, args);
		va_end(args);

		Log(log, "PROBLEM: %s", line);
		problems++;
	}
};

void CheckTextures(CheckContext& c, TexInfo& tex, std::vector<char>& names)
{
	const LevLump* infoLump = c.lev.FindLump(LUMP_TEXTUREINFO);
	const LevLump* nameLump = c.lev.FindLump(LUMP_TEXTURENAMES);

	if (!infoLump || !nameLump || !ParseTexInfo(c.lev, *infoLump, tex))
	{
		c.Fail("TEXTUREINFO / TEXTURENAMES missing or unparsable");
		return;
	}

	names = LumpBytes(c.lev, nameLump);

	// permanent + special page data fill the tpage section exactly
	int expected = 0;
	for (int i = 0; i < tex.numPerm; i++)
		expected += AlignedPageSize(tex.perm[i].y);
	for (int i = 0; i < tex.numSpec; i++)
		expected += AlignedPageSize(tex.spec[i].y);

	if (expected != (int)c.lev.GetLumpInfo().tpage_size)
		c.Fail("tpage section is %d bytes, the perm + special lists describe %d", (int)c.lev.GetLumpInfo().tpage_size, expected);

	for (int i = 0; i < tex.numPerm; i++)
	{
		int start, size;
		if (tex.perm[i].x < 0 || tex.perm[i].x >= (int)tex.pages.size() || !PermPageRange(c.lev, tex, i, start, size))
		{
			c.Fail("permanent page entry %d is invalid", i);
			continue;
		}

		int palettes;
		memcpy(&palettes, c.lev.Data(c.lev.GetLumpInfo().tpage_offset + start), 4);
		if (palettes < 1 || palettes > 32)
			c.Fail("permanent page %d starts with %d palettes", tex.perm[i].x, palettes);
	}

	for (size_t p = 0; p < tex.pages.size(); p++)
	{
		for (const TEXINF& t : tex.pages[p].tex)
		{
			if (t.nameoffset >= names.size())
			{
				c.Fail("page %d: texture name offset %d outside the name table", (int)p, t.nameoffset);
				break;
			}
		}
	}
}

void CheckModels(CheckContext& c, const TexInfo& tex)
{
	ModelSet set;
	if (!ParseModels(c.lev, set))
	{
		c.Fail("MODELS / MODELNAMES missing or unparsable");
		return;
	}

	int resident = 0;
	for (size_t i = 0; i < set.models.size(); i++)
	{
		std::vector<char>& model = set.models[i];
		if (model.empty())
			continue;

		resident++;
		bool walked = ForEachPoly(model, [&](unsigned char* poly, int type)
		{
			if (IsTexturedPoly(type) && poly[1] != 0xFF && poly[1] >= tex.pages.size())
				c.Fail("model %d: polygon uses page %d, the LEV has %d pages", (int)i, poly[1], (int)tex.pages.size());
			else if (IsTexturedPoly(type) && poly[1] != 0xFF && poly[2] >= tex.pages[poly[1]].tex.size())
				c.Fail("model %d: polygon uses texture %d of page %d (%d textures)", (int)i, poly[2], poly[1], (int)tex.pages[poly[1]].tex.size());
		});

		if (!walked)
			c.Fail("model %d (%s): polygon list is not walkable", (int)i, set.names[i].c_str());
	}

	const LevLump* lod = c.lev.FindLump(LUMP_LOWDETAILTABLE);
	if (!lod || lod->size != (int)set.models.size() * 4)
		c.Fail("LOWDETAIL size does not match %d models", (int)set.models.size());

	for (const char* name : kRequiredModels)
	{
		int index = FindModel(set, name);
		if (index < 0 || set.models[index].empty())
			c.Fail("model %s missing", name);
	}

	Log(c.log, "models: %d slots, %d resident", (int)set.models.size(), resident);
}

void CheckNamesAndSmallLumps(CheckContext& c, const TexInfo& tex, const std::vector<char>& names)
{
	for (const char* name : kRequiredTextures)
	{
		if (FindName(names, name) < 0)
			c.Fail("texture %s missing", name);
	}

	int motion = 0;
	while (c.lev.FindLump(LUMP_MOTIONCAPTURE, motion))
		motion++;
	if (motion != 14)
		c.Fail("%d motion capture lumps, the engine expects 14", motion);

	if (const LevLump* chair = c.lev.FindLump(LUMP_CHAIR))
	{
		int end = -1;
		for (int ofs = 0; ofs + 12 <= chair->size && end < 0; ofs += 12)
		{
			short rotation;
			memcpy(&rotation, c.lev.Data(chair->offset + ofs + 8), 2);
			if (rotation == 9999)
				end = ofs;
		}

		if (end < 0)
			c.Fail("CHAIR list has no terminator");
	}

	if (const LevLump* overlay = c.lev.FindLump(LUMP_OVERLAYMAP))
	{
		for (int i = 0; i < 256 && overlay->size >= 512; i++)
		{
			uint16_t ofs;
			memcpy(&ofs, c.lev.Data(overlay->offset + i * 2), 2);
			if (ofs >= overlay->size)
				c.Fail("overlay map tile %d starts at %d, the lump has %d bytes", i, ofs, overlay->size);
		}
	}

	Log(c.log, "textures: %d pages, %d permanent, %d special", (int)tex.pages.size(), tex.numPerm, tex.numSpec);
}

} // namespace

bool D1Dlc_CheckLev(const LevFile& lev, std::string& log)
{
	CheckContext c{ lev, log };
	TexInfo tex;
	std::vector<char> names;

	if (lev.GetFormat() != LEV_FORMAT_DRIVER2)
	{
		c.Fail("not a Driver 2 format LEV");
		return false;
	}

	CheckTextures(c, tex, names);
	if (!tex.pages.empty())
	{
		CheckModels(c, tex);
		CheckNamesAndSmallLumps(c, tex, names);
	}

	c.problems += D1Dlc_CheckMap(lev, log);
	c.problems += D1Dlc_CheckRoads(lev, log);
	Log(log, c.problems ? "check: %d problems" : "check: ok", c.problems);
	return c.problems == 0;
}
