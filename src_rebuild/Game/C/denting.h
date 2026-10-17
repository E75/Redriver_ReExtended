#ifndef DENTING_H
#define DENTING_H


extern void InitialiseDenting(); // 0x0003B1C0
extern void D1BuildDamageZones(int slot);
extern void LoadDenting(int level); // 0x0003B1F0

extern void DentCar(CAR_DATA *cp); // 0x0003A310
extern void ResetImpactDents(int carId);
extern int gImpactDents;								// option: impact dents and lost wheels
extern int WheelIsLost(CAR_DATA* cp, int wheel);
extern void ImpactDent(CAR_DATA* cp, int hx, int hy, int hz, int value);	// dent round a hit point (model space)
extern void CreateDentableCar(CAR_DATA *cp); // 0x0003A6E4

extern void InitHubcap(); // 0x0003A874
extern void HandlePlayerHubcaps(int playerId);
extern void MoveHubcap(); // 0x0003AB4C

extern void SetupSpecDenting(char *loadbuffer); // 0x0003B240


#endif
