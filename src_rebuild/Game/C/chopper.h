#ifndef CHOPPER_H
#define CHOPPER_H

// [A] pilotable helicopter (Fun Cheats): flying car handling, the mission helicopter model with a spinning rotor and its own sound

extern int gChopper;						// 1 while the player flies the chopper

extern void ChopperToggle(int direction);	// Fun Cheats menu: take off / land
extern void ChopperStep(void);				// once per game step, before the sound update
extern void ChopperDrawShadow(void);		// after DrawEvents
extern int ChopperHidesCar(CAR_DATA* cp);	// the player car is replaced by the chopper while flying

#endif
