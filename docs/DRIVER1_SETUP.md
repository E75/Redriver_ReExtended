# Driver 1 cities: setup

This game ships no Driver 1 or Driver 2 data. The Driver 1 cities (Miami, Frisco, Los Angeles, New York) are converted
at runtime from your own Driver 1 files into `DRIVER1\CONVERTED\`. Nothing derived from your files is ever committed or shared.

## What you need

- the game (`REDRIVER2.exe`) with your own Driver 2 data extracted into `DRIVER2\` (as for the normal game): the files of the `DRIVER2` folder of your Driver 2 disc 1, so that `DRIVER2\LEVELS\CHICAGO.LEV` exists. The converter reads Chicago from it. The disc image alone is not enough.
- the `LEVELS` and `DATA` folders of your Driver 1 disc in a folder `DRIVER1\` next to the exe

```
REDRIVER2.exe
config.ini
DRIVER2\...           (your Driver 2 files)
DRIVER1\LEVELS\...    (MIAMI.LEV, FRISCO.LEV, LA.LEV, NEWYORK.LEV, ...)
DRIVER1\DATA\...
```

## Option A: preparation script (all cities at once)

1. Copy `tools\prepare_driver1.bat` next to the exe.
2. Run it. It converts every city it finds (about a minute), writes `[d1dlc]` to `config.ini` and switches the cities on.
3. Start the game: the Driver 1 cities are in Take a Ride and Story - Driver 1.

## Option B: in the game

1. Options - Gameplay - Secret - Extended Settings - Driver 1 DLC.
2. Choose "Check Driver 1 Data". The game looks for the Driver 1 files once, converts every city it finds (a message
   asks you to wait, about a minute) and keeps the answer in `config.ini`; it then shows "Found N Cities" or "Not Found".
3. Choose "On" (only possible when the data was found).

The result is stored in `config.ini`, so nothing is scanned again at the next start:

```
[d1dlc]
checked=1      # the data was looked for
mask=63        # one bit per city that was found (63 = all six Driver 1 level files)
enabled=1      # Driver 1 cities on
prepared=30     # converter version the cities were converted for (a game update asks for a new check)
```

Changed or added Driver 1 files later: choose "Check Again" in the same menu (or delete the `[d1dlc]` section).

## Notes

- Invisible walls: the converter removes the long thin kerb wall boxes of Driver 1 (`RemoveKerbWallBoxes` in
  `src_rebuild\d1dlc\lev_format.h`) so cars can drive up onto the pavements. No extra step is needed for this.
- Ground holes are filled while converting and at runtime (`DrawGroundBackdrop`, `DrawWaterBackdrop`), roofs are off.
- Developer tools: `tools\editor\` (map editor test driver and top-down sweeps), `tools\boxanalysis\` (collision box
  analysis). Test options: `-d1detect` (scan at every start), `-d1check` (menu scan from the command line), `-d1prepare`.
