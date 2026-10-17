#ifndef LEV_WRITER_H
#define LEV_WRITER_H

#include "lev_file.h"

#include <vector>

struct LevBlob
{
	int					type;
	std::vector<char>	data;
};

// Builds a Driver 2 format LEV: header, load-time lumps, permanent tpages, in-memory lumps, spooled data.
class LevWriter
{
public:
	std::vector<char>		headerFiller;	// bytes 40..2047 of the file; empty = the 'DES!' filler of the retail files
	std::vector<LevBlob>	loadtime;
	std::vector<LevBlob>	inmem;
	std::vector<char>		tpages;			// permanent + special pages, every page padded to a sector
	std::vector<char>		spool;			// sector aligned
	std::vector<char>		tail;			// blocks after the spooled section (special cars), sector aligned

	void	AddLump(int section, int type, const void* data, int size);

	bool	Build(std::vector<char>& out) const;
	bool	Write(const char* path) const;
};

// Copies a Driver 2 LEV lump by lump (no conversion). Used to verify the writer.
bool LevRepack(const LevFile& src, LevWriter& out);

// Pads to the next multiple of LEV_SECTOR_SIZE with zeros.
void LevPadToSector(std::vector<char>& data);

#endif // LEV_WRITER_H
