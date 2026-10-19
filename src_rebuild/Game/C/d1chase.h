#ifndef D1CHASE_H
#define D1CHASE_H

// Recorded route of the chased car of a Driver 1 mission: DRIVER1\CONVERTED\M<1000 + script>.CHASE
// (docs/dev-notes/d1-chase-paths.md). Optional: without the file the chase car is driven by the civilian AI alone.
extern int gD1ChasePaths;	// config.ini [d1dlc] chasePaths (default 1)

// true when a route for this mission was loaded (one random variant of the file, like the original)
extern int D1ChasePath_Load(int missionNumber);

// start spot and heading of the loaded route
extern void D1ChasePath_Start(int* x, int* z, int* direction);

// puts the car on the next recorded frame; false once the route is over (the civilian AI drives on from there)
extern int D1ChasePath_Step(CAR_DATA* cp);

// Driver 1 chase car driven by the recorded pad stream (M<n>.PADS, same variant as the route): feeds the pad to the
// normal car control; false when this car is not the chase car or there is no stream (the civilian AI drives then)
extern int D1ChasePath_ProcessPad(CAR_DATA* cp);

// chase car on the civilian AI: feelers ahead turn it away from buildings and cars and brake it for what it cannot take at speed
extern void D1ChaseCar_Avoid(CAR_DATA* cp);

// the damage bar of the chased car is full: it stops and is no longer brought back to the road
extern void D1ChaseCar_Wrecked(void);

// caps one ram (contacts within a second) at a sixth of the damage bar; before = damage total before the contact
extern void D1ChaseCar_LimitHit(CAR_DATA* cp, int before);

// false when a hit on this car must not raise its damage total: the chased car only takes damage from the player's car
extern int D1ChaseCar_DamageCounts(CAR_DATA* cp, CAR_DATA* other);

#endif
