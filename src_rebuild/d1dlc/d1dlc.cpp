#include "d1dlc.h"
#include "d1_convert.h"
#include "lev_file.h"
#include "lev_writer.h"
#include "d1_util.h"

#include <stdio.h>
#include <string>

const D1DlcCity g_D1DlcCities[D1DLC_NUM_CITIES] = {
	// start positions: straight roads of the converted STRAIGHTS2 lump nearest to the road network centre with flat ground
	// (D1 map height 0 along 6000 units) and 2+ lane pairs; rotation = straight angle. Re-derive when the converter geometry changes.
	{ D1DLC_FIRST_CITY + 0, "MIAMI",	"MIAMI.LEV",	64500,   -22500,  0,    4 },
	{ D1DLC_FIRST_CITY + 1, "FRISCO",	"FRISCO.LEV",	-255000, 203250,  1024, 4 },
	{ D1DLC_FIRST_CITY + 2, "LA",		"LA.LEV",		-159000, -169500, 1024, 4 },
	{ D1DLC_FIRST_CITY + 3, "NEWYORK",	"NEWYORK.LEV",	-87000,  56250,   0,    5 },
	{ D1DLC_FIRST_CITY + 4, "NEWCASTLE",	"CREDITS.LEV",	-8250, 48750, 0, 0 },
	{ D1DLC_FIRST_CITY + 5, "TRAINING",	"TRAIN.LEV",	-6750, -37500, 0, 2 },
};

int D1Dlc_PlayerCars(int gameLevel)
{
	for (const D1DlcCity& city : g_D1DlcCities)
	{
		if (city.gameLevel == gameLevel)
			return city.playerCars;
	}

	return 0;
}

bool D1Dlc_StartPosition(int gameLevel, int* x, int* z, int* rotation)
{
	for (const D1DlcCity& city : g_D1DlcCities)
	{
		if (city.gameLevel != gameLevel || (city.startX == 0 && city.startZ == 0))
			continue;

		*x = city.startX;
		*z = city.startZ;
		*rotation = city.startRotation;
		return true;
	}

	return false;
}

int gD1DlcCityMask = 0;
int gD1DlcEnabled = 0;
int gD1DlcChecked = 0;
int gD1DlcPrepared = 0;
static std::string g_configPath = "config.ini";

static std::string g_lastLog;
static signed char g_cityState[D1DLC_NUM_CITIES];		// 0 not tried, 1 usable, -1 conversion failed

const char* D1Dlc_LastLog()
{
	return g_lastLog.c_str();
}

static void StampPath(const char* dataRoot, int dlcIndex, char* path, int size)
{
	snprintf(path, size, "%s%s%s.VER", dataRoot, D1DLC_CONVERTED_FOLDER, g_D1DlcCities[dlcIndex].name);
}

// true when the stamp <NAME>.VER of the converted city holds the current converter version
static bool ConvertedStampIsCurrent(const char* dataRoot, int dlcIndex)
{
	char path[256];
	StampPath(dataRoot, dlcIndex, path, sizeof(path));

	FILE* fp = fopen(path, "rb");
	if (!fp)
		return false;

	int version = 0;
	bool ok = fscanf(fp, "%d", &version) == 1 && version == D1DLC_CONVERTER_VERSION;
	fclose(fp);
	return ok;
}

static bool ConvertSourceLev(const char* dataRoot, int dlcIndex, const char* outPath)
{
	char src[256], donor[256];
	snprintf(src, sizeof(src), "%s%sLEVELS\\%s", dataRoot, D1DLC_DATA_FOLDER, g_D1DlcCities[dlcIndex].sourceLev);
	snprintf(donor, sizeof(donor), "%sDRIVER2\\LEVELS\\CHICAGO.LEV", dataRoot);

	LevFile d1, donorLev;
	if (!d1.Load(src) || !donorLev.Load(donor))
	{
		g_lastLog += "ERROR: cannot load the Driver 1 LEV or the Driver 2 donor LEV\n";
		return false;
	}

	LevWriter writer;
	if (!D1Dlc_ConvertLev(d1, donorLev, writer, g_lastLog))
		return false;

	char folder[256];
	snprintf(folder, sizeof(folder), "%sDRIVER1", dataRoot);
	MakeFolder(folder);
	snprintf(folder, sizeof(folder), "%s%s", dataRoot, D1DLC_CONVERTED_FOLDER);
	MakeFolder(folder);

	if (!writer.Write(outPath))
	{
		g_lastLog += "ERROR: cannot write the converted LEV\n";
		return false;
	}

	// the stamp is written last: a half written LEV is converted again
	char stamp[256];
	StampPath(dataRoot, dlcIndex, stamp, sizeof(stamp));

	FILE* fp = fopen(stamp, "wb");
	if (!fp)
		return false;

	fprintf(fp, "%d", D1DLC_CONVERTER_VERSION);
	fclose(fp);
	return true;
}

bool D1Dlc_ConvertCity(int dlcIndex, const char* dataRoot, char* outPath, int outPathSize)
{
	if (dlcIndex < 0 || dlcIndex >= D1DLC_NUM_CITIES)
	{
		outPath[0] = '\0';
		return false;
	}

	snprintf(outPath, outPathSize, "%s%s%s.LEV", dataRoot, D1DLC_CONVERTED_FOLDER, g_D1DlcCities[dlcIndex].name);
	g_lastLog.clear();

	FILE* fp = fopen(outPath, "rb");
	if (fp)
	{
		fclose(fp);
		if (ConvertedStampIsCurrent(dataRoot, dlcIndex))
			return true;
	}

	return ConvertSourceLev(dataRoot, dlcIndex, outPath);
}

bool D1Dlc_CityAvailable(int gameLevel)
{
	int i = gameLevel - D1DLC_FIRST_CITY;
	if (i < 0 || i >= D1DLC_NUM_CITIES || !(gD1DlcCityMask & (1 << i)))
		return false;

	if (g_cityState[i] == 0)
	{
		char path[256];
		g_cityState[i] = D1Dlc_ConvertCity(i, "", path, sizeof(path)) ? 1 : -1;
	}

	return g_cityState[i] > 0;
}

bool D1Dlc_ConversionPending()
{
	for (int i = 0; i < D1DLC_NUM_CITIES; i++)
	{
		if ((gD1DlcCityMask & (1 << i)) && g_cityState[i] == 0 && !ConvertedStampIsCurrent("", i))
			return true;
	}

	return false;
}

int D1Dlc_Detect(const char* dataRoot)
{
	int mask = 0;

	for (int i = 0; i < D1DLC_NUM_CITIES; i++)
	{
		char path[256];
		snprintf(path, sizeof(path), "%s%sLEVELS\\%s", dataRoot, D1DLC_DATA_FOLDER, g_D1DlcCities[i].sourceLev);

		// only the header is checked here; a full LevFile::Load happens at conversion time
		FILE* fp = fopen(path, "rb");
		if (!fp)
			continue;

		LUMP lump;
		if (fread(&lump, sizeof(lump), 1, fp) == 1 && lump.type == LUMP_LUMPDESC)
			mask |= 1 << i;

		fclose(fp);
	}

	return mask;
}

void D1Dlc_SetConfig(const char* configPath, int checked, int mask, int enabled, int prepared)
{
	if (configPath && configPath[0])
		g_configPath = configPath;

	gD1DlcChecked = checked != 0;
	gD1DlcCityMask = gD1DlcChecked ? mask : 0;
	gD1DlcEnabled = gD1DlcChecked && gD1DlcCityMask != 0 && enabled != 0;

	// prepared for this converter version: the cities are known to be converted, nothing is checked at the first use
	gD1DlcPrepared = (gD1DlcChecked && gD1DlcCityMask != 0 && prepared == D1DLC_CONVERTER_VERSION) ? prepared : 0;
	if (gD1DlcPrepared)
	{
		for (int i = 0; i < D1DLC_NUM_CITIES; i++)
			g_cityState[i] = (gD1DlcCityMask & (1 << i)) ? 1 : 0;
	}
}

// Replaces the [d1dlc] section of the config file, everything else stays as it is (line endings included).
void D1Dlc_SaveConfig()
{
	std::string text;
	FILE* fp = fopen(g_configPath.c_str(), "rb");
	if (fp)
	{
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
			text.append(buf, n);

		fclose(fp);
	}

	const char* eol = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";

	size_t start = text.find("[d1dlc]");
	if (start != std::string::npos)
	{
		size_t end = text.find("\n[", start + 1);
		text.erase(start, end == std::string::npos ? std::string::npos : end + 1 - start);
	}

	while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
		text.pop_back();

	char section[200];
	snprintf(section, sizeof(section), "%s%s[d1dlc]%schecked=%d%smask=%d%senabled=%d%sprepared=%d%s", text.empty() ? "" : eol, text.empty() ? "" : eol, eol, gD1DlcChecked, eol, gD1DlcCityMask, eol, gD1DlcEnabled, eol, gD1DlcPrepared, eol);
	text += section;

	fp = fopen(g_configPath.c_str(), "wb");
	if (fp)
	{
		fwrite(text.data(), 1, text.size(), fp);
		fclose(fp);
	}
}

int D1Dlc_CheckData()
{
	gD1DlcCityMask = D1Dlc_Detect("");
	gD1DlcChecked = 1;
	gD1DlcEnabled = gD1DlcEnabled && gD1DlcCityMask != 0;
	for (int i = 0; i < D1DLC_NUM_CITIES; i++)
		g_cityState[i] = 0;

	// convert every city now, so the first visit of a Driver 1 menu does not have to
	bool allReady = gD1DlcCityMask != 0;
	for (int i = 0; i < D1DLC_NUM_CITIES; i++)
	{
		if ((gD1DlcCityMask & (1 << i)) && !D1Dlc_CityAvailable(D1DLC_FIRST_CITY + i))
			allReady = false;
	}

	gD1DlcPrepared = allReady ? D1DLC_CONVERTER_VERSION : 0;
	D1Dlc_SaveConfig();
	return gD1DlcCityMask;
}
