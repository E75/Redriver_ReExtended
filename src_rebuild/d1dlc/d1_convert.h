#ifndef D1_CONVERT_H
#define D1_CONVERT_H

#include "lev_file.h"
#include "lev_writer.h"

#include <string>

// Converts a Driver 1 LEV into a Driver 2 format LEV: textures, models, car models, donor content (Tanner,
// pedestrians, wheels, effect textures from the Driver 2 donor LEV), LOD table, overlay map, chairs.
// Cells, spool, heights and roads are added in later parts (plans/driver1-cities.md).
// Findings about the format differences are collected in plans/driver1-d2-differences.md.
bool D1Dlc_ConvertLev(const LevFile& d1, const LevFile& donor, LevWriter& out, std::string& log);

// Structure check of a Driver 2 format LEV (retail or converted). Returns false when problems were found.
// Every problem and a short summary are appended to log.
bool D1Dlc_CheckLev(const LevFile& lev, std::string& log);

#endif // D1_CONVERT_H
