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
