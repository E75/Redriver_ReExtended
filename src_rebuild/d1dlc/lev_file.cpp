#include "lev_file.h"

#include <stdio.h>

bool LevFile::Load(const char* path)
{
	m_data.clear();
	m_lumps.clear();
	m_format = LEV_FORMAT_INVALID;

	FILE* fp = fopen(path, "rb");
	if (!fp)
		return false;

	fseek(fp, 0, SEEK_END);
	long size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	m_data.resize(size);
	size_t read = fread(m_data.data(), 1, size, fp);
	fclose(fp);

	if (read != (size_t)size || size < (long)(sizeof(LUMP) + sizeof(OUT_CITYLUMP_INFO)))
		return false;

	if (At<LUMP>(0).type != LUMP_LUMPDESC)
		return false;

	m_lumpInfo = At<OUT_CITYLUMP_INFO>(sizeof(LUMP));

	const OUT_CITYLUMP_INFO& li = m_lumpInfo;
	if (li.loadtime_offset + 12 > size || li.inmem_offset + 12 > size)
		return false;

	// Driver 2 sub-lump lists start directly with LUMP_TEXTURENAMES; Driver 1 lists start with a lump count
	m_format = At<int>(li.loadtime_offset + sizeof(LUMP)) == LUMP_TEXTURENAMES ? LEV_FORMAT_DRIVER2 : LEV_FORMAT_DRIVER1;

	if (!WalkLumps(li.loadtime_offset + sizeof(LUMP), li.loadtime_offset + li.loadtime_size, LEV_SECTION_LOADTIME) ||
		!WalkLumps(li.inmem_offset + sizeof(LUMP), li.inmem_offset + li.inmem_size, LEV_SECTION_INMEM))
	{
		m_format = LEV_FORMAT_INVALID;
		return false;
	}

	return true;
}

bool LevFile::WalkLumps(int offset, int end, int section)
{
	int count = 255;

	if (m_format == LEV_FORMAT_DRIVER1)
	{
		count = At<int>(offset);
		offset += sizeof(int);
	}

	for (int i = 0; i < count; i++)
	{
		if (offset + (int)sizeof(LUMP) > end)
			return false;

		const LUMP& lump = At<LUMP>(offset);
		if (lump.type == LUMP_END)
			break;

		offset += sizeof(LUMP);

		if (lump.size < 0 || offset + lump.size > end)
			return false;

		m_lumps.push_back({ lump.type, offset, lump.size, section });

		offset = (offset + lump.size + 3) & ~3;
	}

	return true;
}

const LevLump* LevFile::FindLump(int type, int index) const
{
	for (const LevLump& lump : m_lumps)
	{
		if (lump.type == type && index-- == 0)
			return &lump;
	}

	return nullptr;
}

const char* LevLumpName(int type)
{
	switch (type)
	{
		case LUMP_PERMANENTPAGES: return "PERMANENTPAGES";
		case LUMP_MODELS: return "MODELS";
		case LUMP_MAP: return "MAP";
		case LUMP_TEXTURENAMES: return "TEXTURENAMES";
		case LUMP_ROADMAP: return "ROADMAP";
		case LUMP_ROADS: return "ROADS";
		case LUMP_JUNCTIONS: return "JUNCTIONS";
		case LUMP_ROADSURF: return "ROADSURF";
		case LUMP_MODELNAMES: return "MODELNAMES";
		case LUMP_ROADBOUNDS: return "ROADBOUNDS";
		case LUMP_JUNCBOUNDS: return "JUNCBOUNDS";
		case LUMP_SUBDIVISION: return "SUBDIVISION";
		case LUMP_LOWDETAILTABLE: return "LOWDETAILTABLE";
		case LUMP_MOTIONCAPTURE: return "MOTIONCAPTURE";
		case LUMP_OVERLAYMAP: return "OVERLAYMAP";
		case LUMP_PALLET: return "PALLET";
		case LUMP_SPOOLINFO: return "SPOOLINFO";
		case LUMP_CAR_MODELS: return "CAR_MODELS";
		case LUMP_CHAIR: return "CHAIR";
		case LUMP_TEXTUREINFO: return "TEXTUREINFO";
		case LUMP_STRAIGHTS2: return "STRAIGHTS2";
		case LUMP_CURVES2: return "CURVES2";
		case LUMP_JUNCTIONS2: return "JUNCTIONS2";
		case LUMP_JUNCTIONS2_NEW: return "JUNCTIONS2_NEW";
	}

	return "UNKNOWN";
}
