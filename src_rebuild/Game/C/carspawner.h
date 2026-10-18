#ifndef CARSPAWNER_H
#define CARSPAWNER_H

#ifndef PSX

// [A] Debug menu "Car Spawner": the player's car is put at a spot on the road next to the player and every car of the loaded city
// can be shown on it without a level load (left / right change the car, Enter ends the mode and the player drives it).
// Driver 2 cities offer the Driver 2 cars (also the specials of the other cities), Driver 1 cities the Driver 1 cars.

extern int gCarSpawner;				// 0 off, 1 showroom: the car is locked, the player cannot get out

void CarSpawnerStart(void);			// from the menu: the showroom starts at the next game step
void CarSpawnerCommandLine(int index, int auto_mode);	// -carspawner <index> [autoright|autostart]: tests
int CarSpawnerBlocksExit(void);	// 1 while the player may not get out of the car
int CarSpawnerUpdate(int pressed);	// every game step from CheckForPause; returns 1 when the pad press was used
int LoadD2SlotCosmetics(int model);	// cosmetics of Driver 2 car 'model' of the loaded city into the special slot (also used by wspccar.c)

#endif // !PSX

#endif // CARSPAWNER_H
