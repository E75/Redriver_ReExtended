#ifndef CAROVR_H
#define CAROVR_H

#ifndef PSX

// [A] Car override: lamps and wheels of a car from CARS/<city>_<model>/car.json (car editor, tools/careditor). Nothing changes for
// a car without the file.

extern char gCarsDir[200];			// -carsdir <dir>, default CARS

void CarOverrideId(char* out, int city, int model);	// "<city>_<model>": the folder name of a car
void CarOverrideUpdate(void);		// every game step: reads the file when the car of a slot changed, applies the values

#endif // !PSX

#endif // CAROVR_H
