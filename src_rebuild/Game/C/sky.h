#ifndef SKY_H
#define SKY_H

extern SVECTOR sun_position[4];
extern SVECTOR moon_position[4];

extern int gTunnelNum;
extern int gSkyHd;		// 0 original, 1 smooth, 2 HD sky (sky.c)
extern int gSunShafts;	// god rays from the sun: 0 off, 1 on (sky.c)

extern void LoadSky(); // 0x000775C8

extern void DrawSkyDome(); // 0x0007795C
extern void DrawLensFlare(); // 0x00077A8C

extern void TunnelSkyFade(); // 0x000787B0

// Day night cycle: draw the HD sky of time of day 'from' faded into that of 'to' by t (0..1), each with its own
// brightness (0..128). from < 0 = off, the sky of gTimeOfDay as usual.
extern void SkyCycleSet(int from, int to, float t, int lightFrom, int lightTo);
extern int SkyCycleReady(void);		// 1 when the HD skies of the cycle are built

#endif
