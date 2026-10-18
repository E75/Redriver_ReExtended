#ifndef WSPCCAR_H
#define WSPCCAR_H

#ifndef PSX

// WSPC DLC cars in the game (library: src_rebuild/wspcdlc). All engine side work for them lives in wspccar.c; the other
// files only call these functions when a WSPC car is chosen, so the normal flow does not change.

// Driver 2 cities: puts the converted WSPC car 'car' (index in g_WspcCars) into the special car slot, with its two texture
// pages and palettes, and gives it the cosmetics and damage zones of its Driver 2 donor car. 0 when it cannot be loaded.
int WspcCar_LoadIntoSlot(int car);

#endif // !PSX

#endif // WSPCCAR_H
