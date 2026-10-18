#include "wspcdlc.h"
#include "wspc_convert.h"
#include "../d1dlc/d1_util.h"

#include <stdio.h>
#include <string>

int gWspcDlcEnabled = 0;
int gWspcDlcChecked = 0;
int gWspcDlcPrepared = 0;
int gWspcDlcCarMask = 0;

static std::string g_configPath = "config.ini";
static std::string g_lastLog;

const char* WspcDlc_LastLog()
{
	return g_lastLog.c_str();
}

void WspcDlc_CarFolder(int car, char* path, int size)
{
	snprintf(path, size, "%s%s", WSPCDLC_CONVERTED_FOLDER, g_WspcCars[car].name);
}

static bool FileExists(const char* path)
{
	FILE* fp = fopen(path, "rb");
	if (!fp)
		return false;

	fclose(fp);
	return true;
}

void WspcDlc_SetConfig(const char* configPath, int checked, int mask, int enabled, int prepared)
{
	if (configPath && configPath[0])
		g_configPath = configPath;

	gWspcDlcChecked = checked != 0;
	gWspcDlcCarMask = gWspcDlcChecked ? mask : 0;
	gWspcDlcEnabled = gWspcDlcChecked && gWspcDlcCarMask != 0 && enabled != 0;
	gWspcDlcPrepared = (gWspcDlcChecked && gWspcDlcCarMask != 0 && prepared == WSPCDLC_CONVERTER_VERSION) ? prepared : 0;
}

void WspcDlc_SaveConfig()
{
	char body[120];
	snprintf(body, sizeof(body), "checked=%d\nmask=%d\nenabled=%d\nprepared=%d\n", gWspcDlcChecked, gWspcDlcCarMask, gWspcDlcEnabled, gWspcDlcPrepared);
	WriteIniSection(g_configPath.c_str(), "wspcdlc", body);
}

int WspcDlc_CheckData()
{
	g_lastLog.clear();
	gWspcDlcCarMask = 0;
	gWspcDlcChecked = 1;

	for (int i = 0; i < WSPCDLC_NUM_CARS; i++)
	{
		char source[256], folder[256];
		snprintf(source, sizeof(source), "%s%s.DOB", WSPCDLC_VEHICLES_FOLDER, g_WspcCars[i].name);

		if (!FileExists(source))
			continue;

		MakeFolder("WSPC");
		MakeFolder("WSPC\\CONVERTED");
		WspcDlc_CarFolder(i, folder, sizeof(folder));

		// a car that cannot be converted is only left out, the reason is in the log
		if (Wspc_ConvertCar(source, folder, g_lastLog))
			gWspcDlcCarMask |= 1 << i;
	}

	gWspcDlcEnabled = gWspcDlcEnabled && gWspcDlcCarMask != 0;
	gWspcDlcPrepared = gWspcDlcCarMask ? WSPCDLC_CONVERTER_VERSION : 0;
	WspcDlc_SaveConfig();
	return gWspcDlcCarMask;
}
