#include "driver2.h"
#include "playermodel.h"
#include "jericho.h"
#include "glaunch.h"
#include "players.h"
#include "pedest.h"

int gPlayerModel = TANNER_MODEL;
int gPlayerPallet = -1;

void SetPlayerModel(int model, int pallet)
{
	if (model == OTHER_MODEL && !JerichoEnsureModels())
	{
		printWarning("Player model: Jericho could not be loaded here\n");
		model = TANNER_MODEL;
	}

	ActiveCheats.cheat12 = (model == OTHER_MODEL);
	gPlayerModel = model;
	gPlayerPallet = pallet;

	// on foot: change the walking player now, in a car it takes effect when getting out
	if (player[0].playerType == PLAYER_TYPE_PEDESTRIAN && player[0].pPed != NULL)
	{
		player[0].pPed->pedType = model;

		if (model == OTHER_SPRITE && pallet >= 0)
			player[0].pPed->pallet = pallet;
	}
}

int PlayerExitModel(void)
{
	if (gPlayerModel == OTHER_MODEL && !JerichoEnsureModels())
		return TANNER_MODEL;

	return gPlayerModel;
}
