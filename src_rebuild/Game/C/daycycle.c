#include "driver2.h"
#include "daycycle.h"
#include "mission.h"
#include "main.h"
#include "sky.h"
#include "cars.h"
#include "debris.h"
#include "cutscene.h"
#include "glaunch.h"
#include "camera.h"
#include "system.h"
#include "spool.h"
#include "models.h"

#include <math.h>

int gDayCycle = 0;
int gDayCycleMinutes = 48;
float gDayCycleStartHour = -1.0f;
float gDayHour = 12.0f;
float gSunElevation = 0.0f;

#define STEPS_PER_SECOND	30
#define CYCLE_PI			3.14159265f
#define SUN_RISE			8.0f
#define SUN_SET				20.0f
#define SUN_TOP_ELEVATION	55.0f	// degrees at 14:00
#define SUN_DISTANCE		32000.0f
#define TWILIGHT_HOURS		2.0f	// dawn and dusk each last two hours around sunrise and sunset
#define TWILIGHT_RAMP		9000	// DawnCount the original dawn and dusk ramps need to run through (lights, sky colour)
#define SKY_FADE_HOURS		1.0f	// the sky of one time of day fades into the next one over this time

// The time of day switches at these hours; the sky fades an hour around each switch
static const float sPhaseHour[4] = { 7.0f, 9.0f, 19.0f, 21.0f };
static const int sPhaseTime[4] = { TIME_DAWN, TIME_DAY, TIME_DUSK, TIME_NIGHT };

static int sActive = 0;
static SVECTOR sSunOriginal;

int DayCycleActive(void)
{
	return sActive;
}

static int TimeOfDayAt(float hour)
{
	int i, time = TIME_NIGHT;

	for (i = 0; i < 4; i++)
	{
		if (hour >= sPhaseHour[i])
			time = sPhaseTime[i];
	}

	return time;
}

static float PhaseStartHour(int time)
{
	int i;

	for (i = 0; i < 4; i++)
	{
		if (sPhaseTime[i] == time)
			return sPhaseHour[i];
	}

	return 0.0f;
}

// The sun turns half a circle from sunrise to sunset and sets where the original fixed sun stands, so the evening
// light comes from the side the dusk sky glows on. Below the horizon it keeps going round.
static void PlaceSun(void)
{
	float day = (gDayHour - SUN_RISE) / (SUN_SET - SUN_RISE);
	float elevation = SUN_TOP_ELEVATION * sinf(CYCLE_PI * day) * (CYCLE_PI / 180.0f);
	float azimuth = atan2f(sSunOriginal.vz, sSunOriginal.vx) - CYCLE_PI * (1.0f - day);

	sun_position[BASE_CITY].vx = (short)(SUN_DISTANCE * cosf(elevation) * cosf(azimuth));
	sun_position[BASE_CITY].vy = (short)(-SUN_DISTANCE * sinf(elevation));
	sun_position[BASE_CITY].vz = (short)(SUN_DISTANCE * cosf(elevation) * sinf(azimuth));

	gSunElevation = elevation * (180.0f / CYCLE_PI);
}

// Sky brightness of a time of day: the dawn and dusk skies brighten and darken with the sun, the night sky is dark itself
static int SkyLight(int time)
{
	if (time == TIME_NIGHT)
		return 128;

	return 50 + (int)(78.0f * SmoothStepF(-12.0f, 10.0f, gSunElevation));
}

static void PlaceSky(void)
{
	int i, from, to;
	float t = 0.0f;

	from = to = gTimeOfDay;

	for (i = 0; i < 4; i++)
	{
		float d = gDayHour - sPhaseHour[i];

		if (d > -SKY_FADE_HOURS * 0.5f && d < SKY_FADE_HOURS * 0.5f)
		{
			from = sPhaseTime[(i + 3) & 3];
			to = sPhaseTime[i];
			t = SmoothStepF(-SKY_FADE_HOURS * 0.5f, SKY_FADE_HOURS * 0.5f, d);
		}
	}

	SkyCycleSet(from, to, t, SkyLight(from), SkyLight(to));
}

static const float startHour[4] = { 7.5f, 12.0f, 19.5f, 23.0f };	// dawn, day, dusk, night

// Take a Ride only (missions keep the time of day they script), not in the replay of the level, not in Driver 1 Los Angeles (always night)
void DayCycleInit(void)
{
	static int sunSaved = 0;

	if (!sunSaved)
	{
		sSunOriginal = sun_position[BASE_CITY];
		sunSaved = 1;
	}

	sActive = gDayCycle && GameType == GAME_TAKEADRIVE && !NoPlayerControl && GameLevel != FIRST_DRIVER1_CITY + 2;

	// sun_position is shared by every level: a level without the cycle gets the fixed sun back
	sun_position[BASE_CITY] = sSunOriginal;
	SkyCycleSet(-1, -1, 0.0f, 0, 0);

	if (!sActive)
		return;

	gDayHour = gDayCycleStartHour >= 0.0f ? gDayCycleStartHour : startHour[gTimeOfDay & 3];
	gTimeOfDay = TimeOfDayAt(gDayHour);
	gNight = (gTimeOfDay >= TIME_NIGHT);

	PlaceSun();
	PlaceSky();

	printInfo("day night cycle: %d minutes a day, start at %.2f h, time of day %d\n", gDayCycleMinutes, gDayHour, gTimeOfDay);
}

static void EnterTimeOfDay(int time)
{
	int i;

	gTimeOfDay = time;
	gNight = (gTimeOfDay >= TIME_NIGHT);

	// Driver 2 cities have a night LEV (lit windows, night models): switch to it at night and back at dawn
	if ((gTimeOfDay == TIME_NIGHT) != gCitySpoolNight)
	{
		SpoolSwapDayNight();
		UpdatePermanentSpriteLights();
	}

	if (gTimeOfDay == TIME_DUSK)
	{
		for (i = 0; i < MAX_CARS; i++)
			lightsOnDelay[i] = i * 11;
	}

	// the HD sky fades between prebuilt images; the original sky can only switch
	if (!SkyCycleReady())
		LoadSky();

	printInfo("day night cycle: time of day %d at %.2f h, camera count %d\n", gTimeOfDay, gDayHour, CameraCnt);
}

// Debug menu: switch to a time of day without a level load; a running cycle continues from the start hour of that time
void DaySetTimeOfDay(int time)
{
	if (sActive)
		gDayHour = startHour[time & 3];

	EnterTimeOfDay(time);
	DawnCount = 0;

	if (sActive)
	{
		PlaceSun();
		PlaceSky();
	}
}

void DayCycleStep(void)
{
	int time;

	if (!sActive || gInGameCutsceneActive || NoPlayerControl || CameraCnt < 90)
		return;

	gDayHour += 24.0f / (gDayCycleMinutes * 60.0f * STEPS_PER_SECOND);

	if (gDayHour >= 24.0f)
		gDayHour -= 24.0f;

	time = TimeOfDayAt(gDayHour);

	if (time != gTimeOfDay)
		EnterTimeOfDay(time);

	// the dawn and dusk code of the game (lights, sky colour) runs on DawnCount: drive it by the clock
	if (gTimeOfDay == TIME_DAWN || gTimeOfDay == TIME_DUSK)
		DawnCount = (int)((gDayHour - PhaseStartHour(gTimeOfDay)) / TWILIGHT_HOURS * TWILIGHT_RAMP);

	PlaceSun();
	PlaceSky();
}

// Ambient light follows the sun height. Twelve degrees under the horizon it reaches 43, what the night look draws
// (a third of 128), so dusk runs into night and night into dawn without a step.
void DayCycleLight(void)
{
	int top;

	if (!sActive || gTimeOfDay == TIME_NIGHT)
		return;

	top = (gWeather - 1U > 1) ? 128 : 78;
	NightAmbient = 43 + (int)((top - 43) * SmoothStepF(-12.0f, 12.0f, gSunElevation));
}
