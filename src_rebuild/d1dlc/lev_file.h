#ifndef LEV_FILE_H
#define LEV_FILE_H

#include "lev_format.h"

#include <vector>

enum ELevFormat
{
	LEV_FORMAT_INVALID = 0,
	LEV_FORMAT_DRIVER1,
	LEV_FORMAT_DRIVER2,
};

enum ELevSection
{
	LEV_SECTION_LOADTIME = 0,
	LEV_SECTION_INMEM = 1,
};

struct LevLump
{
	int type;
	int offset;		// file offset of the lump data (after the LUMP header)
	int size;
	int section;	// ELevSection
};

// Whole LEV file in memory plus the lump directory of its load-time and in-memory blocks.
class LevFile
{
public:
	bool					Load(const char* path);

	ELevFormat				GetFormat() const { return m_format; }
	const OUT_CITYLUMP_INFO& GetLumpInfo() const { return m_lumpInfo; }
	const std::vector<LevLump>& GetLumps() const { return m_lumps; }

	// index = n-th lump of that type (MOTIONCAPTURE occurs several times); NULL if missing
	const LevLump*			FindLump(int type, int index = 0) const;

	const char*				Data(int offset) const { return m_data.data() + offset; }
	int						Size() const { return (int)m_data.size(); }

	template<typename T>
	const T&				At(int offset) const { return *(const T*)Data(offset); }

private:
	bool					WalkLumps(int offset, int end, int section);

	std::vector<char>		m_data;
	std::vector<LevLump>	m_lumps;
	OUT_CITYLUMP_INFO		m_lumpInfo{};
	ELevFormat				m_format{ LEV_FORMAT_INVALID };
};

const char* LevLumpName(int type);

#endif // LEV_FILE_H
