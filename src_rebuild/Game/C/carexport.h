#ifndef CAREXPORT_H
#define CAREXPORT_H

#ifndef PSX

// [A] Car export (-carexport <dir>, run from carspawner.c): car.json + car.glb of a resident car, for the car editor (tools/careditor).
// Returns 1 when the files are written. 'slot' is the resident model slot that holds the car.
// 'comp' = the suspension compression of the four wheels at rest
int CarExportWrite(const char* dir, const char* id, const char* name, int city, int model, int slot, const int* comp);

#endif // !PSX

#endif // CAREXPORT_H
