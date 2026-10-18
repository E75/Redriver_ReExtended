#ifndef WHEELFORCES_H
#define WHEELFORCES_H

extern HANDLING_TYPE handlingType[7];

extern void StepCars(); // 0x00082BD0
extern void StepOneCar(CAR_DATA *cp); // 0x00081178

extern int gInfiniteMass;	// cheat: the player car is not pushed by collisions with other cars (F6, Fun Cheats menu)
extern int gD1Handling;		// config.ini [d1dlc] d1Handling (default 0): Driver 1 handling in Driver 1 cities
extern int D1HandlingActive(void);	// gD1Handling and a Driver 1 city is loaded
extern int gD1ChaseInfiniteMass;	// config.ini [d1dlc] chaseInfiniteMass (default 0): Driver 1 chase cars are not pushed by collisions


#endif
