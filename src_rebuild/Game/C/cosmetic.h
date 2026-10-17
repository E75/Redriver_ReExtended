#ifndef COSMETIC_H
#define COSMETIC_H

extern CAR_COSMETICS car_cosmetics[MAX_CAR_RESIDENT_MODELS];
extern CAR_COSMETICS dummyCosmetics;

extern int gcar_num;

extern void LoadCosmetics(int level); // 0x00031160
extern void SetupSpecCosmetics(char *loadbuffer); // 0x00031360

// [A] Driver 1 cities: scales the Driver 2 cosmetics of the resident cars to the size of the Driver 1 car models
extern void D1ScaleCarCosmetics(void);

// [A] where a Driver 1 player car's wheels are drawn (the physics keeps car_cosmetics), valid when the flag of the slot is set
extern SVECTOR gD1WheelVisual[MAX_CAR_RESIDENT_MODELS][4];
extern int gD1WheelSizeVisual[MAX_CAR_RESIDENT_MODELS];
extern char gD1WheelVisualOn[MAX_CAR_RESIDENT_MODELS];

extern void AddReverseLight(CAR_DATA* cp); // 0x0002F994
extern void AddIndicatorLight(CAR_DATA *cp, int Type); // 0x0002FAEC
extern void AddBrakeLight(CAR_DATA *cp); // 0x0002FDE4
extern void AddCopCarLight(CAR_DATA *cp); // 0x00030148
extern void AddNightLights(CAR_DATA *cp); // 0x00030544
#ifdef DYNAMIC_LIGHTING
extern void AddHiddenCarLights(CAR_DATA* cp);
#endif

extern void AddSmokingEngine(CAR_DATA *cp, int black_smoke, int WheelSpeed); // 0x00030D9C
extern void AddExhaustSmoke(CAR_DATA *cp, int black_smoke, int WheelSpeed);
extern void AddFlamingEngine(CAR_DATA *cp); // 0x00030FAC


#endif
