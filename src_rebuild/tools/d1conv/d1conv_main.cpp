// d1conv - developer CLI around the d1dlc library (the game itself converts at startup).
//   d1conv info <file.LEV>        lump directory and format of a Driver 1 or Driver 2 LEV
//   d1conv detect <install root>  which Driver 1 cities REDRIVER2 would enable
//   d1conv repack <in.LEV> <out.LEV>  rewrite a Driver 2 LEV through the writer (must be byte identical)
//   d1conv check <file.LEV>       structure check of a Driver 2 format LEV (retail or converted)
//   d1conv convert <D1.LEV> <donor D2.LEV> <out.LEV>  convert a Driver 1 city, then check the result
//   d1conv heights <D1.LEV> <converted.LEV> [step]    compare D1 ground heights with the converted D2 height maps
//   d1conv probe <D1.LEV> <converted.LEV> <x> <z>      D1 and D2 ground height / surface at one world position

#include "d1dlc.h"
#include "lev_file.h"
#include "lev_writer.h"
#include "d1_convert.h"
#include "d1_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int CmdInfo(const char* path)
{
	LevFile lev;
	if (!lev.Load(path))
	{
		printf("ERROR: '%s' is not a valid LEV file\n", path);
		return 1;
	}

	printf("%s: %s\n", path, lev.GetFormat() == LEV_FORMAT_DRIVER1 ? "Driver 1" : "Driver 2");

	for (const LevLump& lump : lev.GetLumps())
		printf("  %-16s %3d  offset %8d  size %8d\n", LevLumpName(lump.type), lump.type, lump.offset, lump.size);

	if (const LevLump* map = lev.FindLump(LUMP_MAP))
	{
		const OUT_CELL_FILE_HEADER& h = lev.At<OUT_CELL_FILE_HEADER>(map->offset);
		printf("  map: %d x %d cells of %d, region %d cells, %d regions\n",
			h.cells_across, h.cells_down, h.cell_size, h.region_size, h.num_regions);
	}

	return 0;
}

static int CmdDetect(const char* root)
{
	int mask = D1Dlc_Detect(root);

	for (int i = 0; i < D1DLC_NUM_CITIES; i++)
		printf("  %-10s %s\n", g_D1DlcCities[i].name, (mask & (1 << i)) ? "found" : "missing");

	return mask ? 0 : 1;
}

// repack a Driver 2 LEV through LevWriter and report whether the result is byte identical
static int CmdRepack(const char* inPath, const char* outPath)
{
	LevFile lev;
	LevWriter writer;
	std::vector<char> built;

	if (!lev.Load(inPath) || !LevRepack(lev, writer) || !writer.Build(built))
	{
		printf("ERROR: cannot repack '%s' (Driver 2 LEV expected)\n", inPath);
		return 1;
	}

	if (!writer.Write(outPath))
	{
		printf("ERROR: cannot write '%s'\n", outPath);
		return 1;
	}

	if (built.size() != (size_t)lev.Size())
	{
		printf("repack: DIFFERENT size (in %d bytes, out %zu bytes)\n", lev.Size(), built.size());
		return 1;
	}

	// the retail files hold stack garbage in the 1-3 alignment bytes after a lump, those are not copied
	std::vector<bool> isPad(built.size(), false);
	for (const LevLump& lump : lev.GetLumps())
		for (int i = lump.offset + lump.size; i < ((lump.offset + lump.size + 3) & ~3); i++)
			isPad[i] = true;

	size_t padDiffs = 0;
	for (size_t i = 0; i < built.size(); i++)
	{
		if (built[i] == *lev.Data((int)i))
			continue;

		if (!isPad[i])
		{
			printf("repack: DIFFERENT at offset %zu (not alignment padding)\n", i);
			return 1;
		}

		padDiffs++;
	}

	printf("repack: identical, %zu bytes (%zu alignment pad bytes differ)\n", built.size(), padDiffs);
	return 0;
}

static int CmdCheck(const char* path)
{
	LevFile lev;
	if (!lev.Load(path))
	{
		printf("ERROR: '%s' is not a valid LEV file\n", path);
		return 1;
	}

	std::string log;
	bool ok = D1Dlc_CheckLev(lev, log);
	printf("%s", log.c_str());
	return ok ? 0 : 1;
}

static int CmdHeights(const char* d1Path, const char* d2Path, int step)
{
	LevFile d1, d2;
	if (!d1.Load(d1Path) || !d2.Load(d2Path))
	{
		printf("ERROR: cannot load '%s' or '%s'\n", d1Path, d2Path);
		return 1;
	}

	std::string log;
	bool ok = D1Dlc_VerifyHeights(d1, d2, step, log);
	printf("%s%s\n", log.c_str(), ok ? "heights: ok" : "heights: PROBLEMS");
	return ok ? 0 : 1;
}

static int CmdProbe(const char* d1Path, const char* d2Path, int x, int z)
{
	LevFile d1, d2;
	if (!d1.Load(d1Path) || !d2.Load(d2Path))
	{
		printf("ERROR: cannot load '%s' or '%s'\n", d1Path, d2Path);
		return 1;
	}

	std::string log;
	bool ok = D1Dlc_ProbeGround(d1, d2, x, z, log);
	printf("%s\n", log.c_str());
	return ok ? 0 : 1;
}

static int CmdConvert(const char* d1Path, const char* donorPath, const char* outPath)
{
	LevFile d1, donor;
	if (!d1.Load(d1Path) || !donor.Load(donorPath))
	{
		printf("ERROR: cannot load '%s' or '%s'\n", d1Path, donorPath);
		return 1;
	}

	LevWriter writer;
	std::string log;
	bool ok = D1Dlc_ConvertLev(d1, donor, writer, log);
	printf("%s", log.c_str());

	if (!ok || !writer.Write(outPath))
	{
		printf("ERROR: conversion failed\n");
		return 1;
	}

	printf("wrote %s\n", outPath);
	int checked = CmdCheck(outPath);
	return CmdHeights(d1Path, outPath, 1000) || checked;
}

int main(int argc, char** argv)
{
	if (argc == 3 && !strcmp(argv[1], "info"))
		return CmdInfo(argv[2]);

	if (argc == 3 && !strcmp(argv[1], "check"))
		return CmdCheck(argv[2]);

	if (argc == 5 && !strcmp(argv[1], "convert"))
		return CmdConvert(argv[2], argv[3], argv[4]);

	if ((argc == 4 || argc == 5) && !strcmp(argv[1], "heights"))
		return CmdHeights(argv[2], argv[3], argc == 5 ? atoi(argv[4]) : 500);

	if (argc == 6 && !strcmp(argv[1], "probe"))
		return CmdProbe(argv[2], argv[3], atoi(argv[4]), atoi(argv[5]));

	if (argc == 4 && !strcmp(argv[1], "repack"))
		return CmdRepack(argv[2], argv[3]);

	if (argc == 3 && !strcmp(argv[1], "detect"))
		return CmdDetect(argv[2]);

	printf("usage:\n  d1conv info <file.LEV>\n  d1conv detect <install root with trailing backslash>\n");
	return 2;
}
