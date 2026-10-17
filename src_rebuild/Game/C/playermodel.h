#ifndef PLAYERMODEL_H
#define PLAYERMODEL_H

#include "glaunch.h"

// [A] debug menu "Player Model": the player walks as Tanner, as Jericho or as a pedestrian (sprite model with a palette)

extern int gPlayerModel;		// PED_MODEL_TYPES the player gets when leaving a car: TANNER_MODEL, OTHER_MODEL (Jericho), OTHER_SPRITE
extern int gPlayerPallet;		// palette of the pedestrian look, -1 = random

extern void SetPlayerModel(int model, int pallet);	// also changes the player at once when on foot
extern int PlayerExitModel(void);					// the model for the next getting out of a car (Jericho only when it could be loaded)

// the pedestrian that is the player (the checks of Tanner in pedest.c)
#define IS_PLAYER_PED(p)	((p)->pedType == TANNER_MODEL || ((p)->pedType == OTHER_MODEL && ActiveCheats.cheat12) || ((p)->pedType == OTHER_SPRITE && gPlayerModel == OTHER_SPRITE))

#endif
