#ifndef WSPC_CONVERT_H
#define WSPC_CONVERT_H

#include <string>

// Converts one WSPC vehicle (<name>.DOB) into the files of a Redriver car, written to outFolder:
// CLEAN.MDL, DAMAGED.MDL, LOW.MDL (MODEL structs), TEXTURE.BIN (u32 pages; per page u32 palettes, palettes * 32 bytes of
// 15 bit colours, 0x8000 bytes of 4 bit pixels) and CAR.INFO (counts, bounds, wheel centres). Format: docs/wspc.md.
// Messages are appended to log; false when the vehicle cannot be converted.
bool Wspc_ConvertCar(const char* dobPath, const char* outFolder, std::string& log);

#endif // WSPC_CONVERT_H
