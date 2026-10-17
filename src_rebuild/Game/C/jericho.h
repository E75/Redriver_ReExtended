#ifndef JERICHO_H
#define JERICHO_H

// [A] Jericho Mode in every city: the models and the texture page are read from VEGAS.LEV when the city has none

extern int JerichoEnsureModels(void);	// 1 when the six Jericho models (with texture) are ready, loads them on first use
extern void JerichoPageLost(void);		// something else took the special texture slot

#endif
