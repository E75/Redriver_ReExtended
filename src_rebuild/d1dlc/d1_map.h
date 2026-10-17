#ifndef D1_MAP_H
#define D1_MAP_H

#include "lev_file.h"
#include "lev_writer.h"

#include <string>

// Re-grids the Driver 1 cell objects (3000 unit cells, 30 cell regions) into Driver 2 cells (2048 units, 32 cells per
// region) and writes LUMP_MAP, LUMP_SPOOLINFO, the spooled section (area textures + models, regions with cell pointers,
// cells, packed cell objects and a flat height map) and empty road lumps. See plans/driver1-d2-differences.md section 8.
bool D1Dlc_ConvertMap(const LevFile& d1, LevWriter& out, std::string& log);

// Structure check of the map lumps and the spooled regions of a Driver 2 format LEV. Returns the number of problems.
int D1Dlc_CheckMap(const LevFile& lev, std::string& log);

// Compares the D1 ground height / surface with the converted D2 height maps on a grid of `step` units (a developer check
// for d1conv). Returns false when samples are missing or a road sample is off by more than 32 units.
bool D1Dlc_VerifyHeights(const LevFile& d1, const LevFile& d2, int step, std::string& log);

// Logs the D1 ground height / surface and the converted D2 height map value at one world position.
bool D1Dlc_ProbeGround(const LevFile& d1, const LevFile& d2, int x, int z, std::string& log);

#endif // D1_MAP_H
