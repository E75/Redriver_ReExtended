# Ground fill: no holes, no missing textures when seen from the air (plan)

Status 2026-10-13: measured, nothing implemented. Goal: flying a helicopter / flying car over any of the 8 cities (D2 0-3, D1 4-7) shows no void, no black or sky coloured gaps under and between buildings, no missing texture page, no z-fighting. Collisions are not needed. Read `CLAUDE.md` first. Branch: new from `main` (`feature/ground-fill`). Do NOT edit `texture.c`/`d1_convert.cpp` while the Part B session (D1 player cars) is open.

## What exists
- `gapfill.c` (commit `b43b4f59`): roofs for open topped boxy D2 buildings (two quads per model, pavement texture per city, `sRoofTextures`). D2 only, no ground, no D1.
- `d1_map.cpp FillGroundGaps` (about 567, converter): D1 holes enclosed by ground (not open, at most 3000 sub cells of 750 units) get a grass tile at road map height. Open holes (sea, outside the map) stay open on purpose.
- D1 converter already counts missing pages: `D1Conv convert` prints `map: area coverage` (regions with missing models: Miami 2/108, Frisco 2/202, LA 5/90, NY 5/155) and `map: texture pages ... missing` (Miami 4 regions, Frisco 6, LA 3, NY 3).

## Measurement (2026-10-13, read only, scripts in the session scratchpad `gaps\`)
- Holes = present region cells with no floor model (rule of `isFloor`, `d1_map.cpp` 575) and no building box. Share of present area: Chicago 61%, Havana 75%, Vegas 67%, Rio 75%, Miami 42%, Frisco 77%, LA 77%, New York 73%. D2 shares are an upper bound: many D2 buildings are zero depth facade planes (bounding box has no area), so block interiors count as holes.
- Largest holes are open: Miami bay 23110 tiles (100% water per minimap), Chicago lake (40% water), Havana and Rio about 50% water, NY harbour 28% water, Vegas desert (green, no water), Frisco and LA the whole outside. Components 2-5 are 1.4k-10k tiles, surrounded by road, path and grass.
- Minimap as source (decoded for all 8, RNC2 4 bit tiles, CLUT at 512 D2 / 328 D1): reliable for water vs land (water recall Miami 0.76, Frisco 0.76, NY 0.73, LA 0.46, Rio 0.28; D2 maps show almost no water that tiles can verify), but it does NOT separate grass, concrete and road inside land (LA grass is 93% "urban" on the map). Use it for: water / land / outside. Use neighbouring tiles or the city default for grass vs concrete. Map pixel = 2145 units (D2), 2812 (D1), coarser than a D1 tile. Alignment offsets: 0 px Chicago, Havana, Miami, LA, NY; +1 Frisco; (-1,-2) Rio; (+1,+1) Vegas.
- Aerial look (private install copy, `-flyalt 20000 -shot 600`): see the sheet in the session; the chase camera looks horizontally, so a test option that pitches it down is step 0.

## Design (runtime only, no game data written or committed)
1. Backdrop ground grid, drawn under everything, all 8 cities. Per city a coarse class grid (water / land / outside) from the minimap, built when the level loads (`LoadSky`-style, `sky.c` shows how to build a texture at load). Each frame, for the cells inside the draw radius, emit flat quads with the class texture, merged in rows (primitive budget: count against `gFarBudget`, cap about 1500 quads). Land height = `MapHeight` sampled per region once (cache), water height = the city's sea level, outside = fade to the fog / sky colour at the horizon. The backdrop lies 20-30 units BELOW the real ground and uses `PGXP_SetZOffsetScale(0, 1.01)` so every real tile wins: no coverage grid, no z-fighting. A hole shows backdrop instead of sky.
2. Textures per class and city: water = the city water texture (D2 water models, D1 SEA/WATER models), land = grass tile of the city (`FillGroundGaps` grass rule for D1, grass/pavement of `analyze.py` for D2). Table `sGroundTextures[8]` like `sRoofTextures`.
3. Missing texture pages (D1: 3-6 regions per city): DONE, nothing to fix (2026-10-13). A converter test (every textured polygon of every spooled area model against permanent + area pages) found 0 polygons with an unloaded page in Miami, Frisco, LA and New York. The converter's "pages missing" count comes from model NUMBERS that are used in a region but absent from its area spool (`models.pages` merges all copies); those objects are simply not drawn. The real gap is therefore missing MODELS (Miami 2/108 regions, Frisco 2/202, LA 5/90, New York 5/155), only fixable by adding the model plus its page to the area (slot limit), not worth it for about 3% of the regions.
4. D1 roofs: the converter already caps roofless single box buildings (`AddRoofCap`, `lev_format.h`, 38/34/40/92 resident and 257/40/268/182 spooled buildings per city, textured like the topmost wall quad); what is left are buildings of more than one box or with a stepped top. Option: extend `gapfill.c` (rule: top ring of boxy buildings) to D1 with the D1 pavement texture; only after the converter session is done if it needs converter data.
5. Z-fight audit from the air: bridges, tunnels, tile borders, decals (compare `-flyalt` shots before/after).

## Steps (one commit each, green Release_dev build, D2 regression `-shot` in Chicago, Havana, Vegas, Rio and the four D1 cities)
0. Test option `-camdown N` (pitch the chase camera down, `UpdateCameraOrbit` `camPitch` is -120..380 units; forced in `-shot` runs only) + an aerial sheet script (private install copy: junctions to `DRIVER1`, `DRIVER2`, copy of exe, dlls and `config.ini`, so other sessions' logs stay intact; black frames: retry with `-WindowStyle Normal`). Baseline sheets of all 8 cities.
1. Class grids from the minimap (`overmap.c` already knows the offsets and rotation) + backdrop quads in one city (Miami: a 100% water hole, easy to see), measure `prim` bytes and build ms.
2. All 8 cities, the texture table, horizon fade.
3. Missing page fallback: not needed (see Design 3).
4. D1 roofs, z-fight audit.
Done when: the 8 aerial sheets show no black or sky gap inside the map, `build` stays within +2 ms of the baseline at 10x, D2 and D1 ground tiles look unchanged when seen from the street.

## Risks / open
- Backdrop height on hills (Frisco): per vertex height needed, cost of `MapHeight` sampling; cache per region.
- The minimap is coarse: coast lines will be off by up to a map pixel; the backdrop is below the tiles so real coast tiles hide the error.
- D2 OVERHEAD page is not resident in D1 cities (the D1 map sits in the wheel page): build the class grid at level load from the lump, not from VRAM.
- Water that moves/reflects in D2 (animated water models) is not replaced: the backdrop is only the fallback below.

## Status 2026-10-14 (parked on purpose)
- Done: water backdrop (all 4 D1 cities), step 3 not needed, z-fight audit (8 cities, 3 altitudes, nothing found), D1 bridge undersides (`DrawTileUnderside`), blue void at the screen bottom (near plane check only for shadows), LA water (`CoarseWater`). Details in `CLAUDE.md`.
- Open, parked by the user: (1) ground holes: cells the map does not mark as water stay open (Frisco black hole and grey mirror strip left of the bay; Frisco default start lies at the map edge). (2) D1 roofs of multi box or stepped buildings: converter diagnostic says single boxes are done (Miami 35 of 40 buildings, Frisco 31 of 81, LA 37 of 54, New York 89 of 98 got a cap, most of the rest already has a roof or is a path/alley); nothing open was seen in aerial shots (New York, Miami clear; Frisco and LA could not be judged: camera too close or night). Wait for a play report (F9 MARK) before building a per box roof generator. (3) Black beam on the Frisco hill: shadow near plane check kept, not looked at again. (4) A free camera would make aerial checks much easier: `-camdown 1000` (straight down) smears, 450 to 650 works, `-flyalt` is absolute height and D1 cities are huge (use 9000 or more).
