#ifndef D1GARAGE_H
#define D1GARAGE_H

// Driver 1 garage driving test (docs/dev-notes/d1-desert-garage.md, part 4). Its numbers and texts are read from the player's
// own DRIVER1\IVIEW.BIN; without that file the garage mission stays a free drive.

// new mission: the test starts again
extern void D1Garage_Reset(void);

// one frame of the test (engine function 0x4000022 of the converted script); ends the mission itself
extern void D1Garage_Step(CAR_DATA* cp);

// task list on the right while the test runs
extern void D1Garage_Draw(void);

#endif // D1GARAGE_H
