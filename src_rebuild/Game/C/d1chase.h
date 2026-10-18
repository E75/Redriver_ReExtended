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

#endif
