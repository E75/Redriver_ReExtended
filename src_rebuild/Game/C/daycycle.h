#ifndef DAYCYCLE_H
#define DAYCYCLE_H

// Day night cycle (Take a Ride): a 24 hour clock runs in 48 real minutes, the sun rises at 8:00, stands highest
// at 14:00 and sets at 20:00. Light, shadows, haze and sky follow the sun smoothly. Off by default, see daycycle.c.

extern int gDayCycle;			// option: 0 off, 1 on (config.ini [game] dayNightCycle, Extended Settings, -cycle)
extern int gDayCycleMinutes;	// real minutes of one whole day, 48 (-cycle <n> for tests)
extern float gDayHour;			// clock of the cycle, 0..24
extern float gDayCycleStartHour;	// -hour <h>: start clock, below 0 = from the chosen time of day
extern float gSunElevation;		// degrees above the horizon, negative at night (only while the cycle runs)

extern void DayCycleInit(void);			// level load, after the time of day is known
extern int DayCycleActive(void);		// 1 when the cycle runs in this level
extern void DayCycleStep(void);			// once per simulation step
extern void DaySetTimeOfDay(int time);	// debug menu: live switch, no level load
extern void DayCycleLight(void);		// after the ambient light of the time of day is set

static inline float SmoothStepF(float edge0, float edge1, float x)
{
	float t = (x - edge0) / (edge1 - edge0);

	t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
	return t * t * (3.0f - 2.0f * t);
}

#endif
