#ifndef ALLCARS_H
#define ALLCARS_H

#ifndef PSX

// [A] "All cities cars": a special car of another Driver 2 city in the traffic (allcars.c, hooks in spool.c)

#define FOREIGN_SPOOL_BASE	0x1000000	// spool request sector numbers from here on read the foreign block (UpdateSpoolPC)

extern int gAllCityCars;			// option: 0 = original cars of the city
extern int gForeignSpool;			// the special spool under way reads the foreign block
extern int gForeignLoaded;			// the foreign car was spooled (once per level)
int ForeignCarSelect(int city, int special);	// Car Spawner: live special car of another Driver 2 city (spool it with QuickSpoolSpecial)
void ForeignCarClear(void);
extern int gForeignForceCity;		// -foreigncar <city> <special 1..4>: test option, -1 = random
extern int gForeignForceSpecial;

extern void AllCarsLevelStart(void);
extern int ForeignCarMode(void);			// a foreign car was chosen for this level
extern int ForeignSpecialIndex(void);		// its special car number (1..4) in the other city
extern int ForeignCarCity(void);
extern char* ForeignSpoolPtr(int sector);
extern int ForeignPageId(int i);
extern void ForeignRemapModel(MODEL* polySrc);

#endif

#endif
