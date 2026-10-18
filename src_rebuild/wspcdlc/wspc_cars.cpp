#include "wspcdlc.h"

// WSPC car -> Redriver driving behaviour. Kept apart from the converter so it can be tuned without touching it.
const WspcCar g_WspcCars[WSPCDLC_NUM_CARS] = {
	{ "POLICE",		"Police Car",		WSPC_COP },
	{ "AIPOLICE",	"Patrol Car",		WSPC_COP },
	{ "COPJEEP",	"Police Jeep",		WSPC_COP },
	{ "TAXI",		"Taxi",				WSPC_TAXI },
	{ "AMB",		"Ambulance",		WSPC_AMBULANCE },
	{ "BUS",		"Bus",				WSPC_BUS },
	{ "APC",		"Armoured Truck",	WSPC_HEAVY },
	{ "BVAN",		"Black Van",		WSPC_VAN },
	{ "NEWSVAN",	"News Van",			WSPC_VAN },
	{ "VAN",		"Van",				WSPC_VAN },
	{ "BMW",		"Sports Saloon",	WSPC_SPORTS },
	{ "VIPER",		"Viper",			WSPC_SPORTS },
	{ "CADILLAC",	"Cadillac",			WSPC_SEDAN },
	{ "LINCOLN",	"Lincoln",			WSPC_SEDAN },
	{ "LIMO_W",		"Limousine",		WSPC_LIMO },
	{ "PIZZACAR",	"Pizza Car",		WSPC_COMPACT },
};

const char* WspcDlc_CarTitle(int car)
{
	return g_WspcCars[car].title;
}

// Driver 2 car model (0 = cop car, 1..4 = the traffic cars of the city) whose handling, wheels and lights a WSPC car borrows.
// First rule: cops drive like the cop car, everything else like the first traffic car; refine per behaviour after driving them.
int WspcDlc_DonorModel(int car)
{
	return g_WspcCars[car].behaviour == WSPC_COP ? 0 : 1;
}
