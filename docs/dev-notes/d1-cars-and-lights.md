# Driver 1 cities: street lamps, traffic light sprites, player cars (plan)

Status 2026-10-13: research done (two read-only audits of the D1 LEV data), nothing implemented. Branch: `feature/d1-cars-and-lights` (from `main`). Read `CLAUDE.md` (Driver 1 DLC findings) first. Parts A and B are independent (A: `objanim.c`, `debris.c`; B: converter, `texture.c`, `mission.c`, `cosmetic.c`, `denting.c`, `FEmain.c`): one session per part, never two sessions on one file, never two builds into the same build folder at once.

Rules that hold for both parts:
- D2 cities must look and behave exactly as before. Gate every change with `IS_DRIVER1_CITY(GameLevel)`. After each step run `-shot` in Chicago, Havana, Vegas and in the four D1 cities (`test_scripts\shot_safe2.ps1`).
- Never commit game data or data derived from it. All tables are derived at runtime or by the converter from the player's own `DRIVER1\` folder. Bump `D1DLC_CONVERTER_VERSION` (now 23) after any converter output change.
- Each step ends on a green build (Release_dev) plus a `-shot` check; one commit per step.

## Part A: lamps and traffic light sprites

Findings (D1 data, unique objects per city):
- Lamps: Miami SLAMP2 (20 v), SLAMP3 (double arm), SLAMP1 (sprite lamp) = 776 objects; Frisco SLIGHT00/01 + sprite SLIGHT02/03 = 108; LA SLIGHT00/01/02 = 984; New York SLIGHT00/01/02 = 1397.
- Traffic lights: Miami TLIGHT01/02 (+ TLIGHT03..18 are instance copies of id 89 with garbage own vertices) = 377; Frisco TLIGHT00/01 = 154; LA TLIGHT00..10 = 229; New York TLIGHT00/01 = 359.
- D1 flags2 already has `MODEL_FLAG_LAMP` (0x1000) on most lamp models (`bcollide.c:915` uses it at night).
- Lamp head rule: centroid of the connected cluster of white (255,255,255) flat polys (poly type 8/9). Verified on D2: Chicago LOW_SLIGHT01 cluster (431,-1244,0) against the hand tuned `AddLightEffect` value (429,-1234,0).
  - Local offsets (y negative = up): Miami SLAMP2 (-540,-1491,0), SLAMP3 (-579,-1517,1) and (564,-1517,1); Frisco SLIGHT00 (-712,-2005,-61), SLIGHT01 (-268,-1760,0); New York SLIGHT00 (-758,-2300,0), SLIGHT01 (+-1450,-3868,1).
  - LA has no white polys: estimate SLIGHT00 (-811,-2330,0), SLIGHT01 (+-1080,-2290,0) from the arm end vertices; LA SLIGHT02 unknown.
  - Sprite lamps (shape 0x4001): head at (0, ymin + 20), e.g. Miami (0,-730,0).
  - Model units are NOT scaled: D1 lamps are 1500-3900 high (D2 1234-2249).
- Signals have no flat colour polys. Frisco TLIGHT01: three lens strips at z -62, x about -850, y -1504 / -1404 / -1319 (top / middle / bottom). LA and New York: heads at the arm tip (x -1446 LA TLIGHT01, -1716 NY), y -1527..-1181 (thirds at -1469 / -1354 / -1240), z -77. Miami TLIGHT01/02 and Frisco TLIGHT00 are unclear.
- D2 mechanism: `objanim.c` tables `{internal_id, name, LitPoly}` per city; `InitAnimatingObjects` flags models `MODEL_FLAG_ANIMOBJ`; `DrawAllAnimatingObjects` calls `animate_object(cop, id)` (hard coded `switch` per `GameLevel` 0..3). Lamps call `AddLightEffect(cop, x, y, z, 0, 3)` when `gLightsOn`; signals call `AddTrafficLight(cop, x, y, z, 0x200|0x400|0x800, yang)` (only the active colour is drawn: red top, amber middle, green bottom). D1 is excluded today (`objanim.c InitAnimatingObjects`: `GameLevel < 4`, table 0 for D1). Limits: `aop[tri_verts & 31]` (32 entries max), `MAX_DRAWN_ANIMATING` 128.
- Phase: `junctionLightsPhase[2]` (1 red, 2 amber, 3 green; index 0 green at timeCnt 256..405, index 1 at 0..149); cars use `exitFrom & 1`; in `d1_roads.cpp` ExitIdx 0/2 are +-z, 1/3 are +-x. D1 TLIGHT yang is only 0/16/32/48. Proposed index `((yang * 64 + 128) >> 10) & 1` (D2 formula without Chicago's +1024); whether to flip it, and whether the lens faces local -z, is NOT verified.
- Junction lights in the converter now flag every junction with at least one TLIGHT model within 4500 units (Miami 146, Frisco 43, LA 81, New York 107). Lights further away would still cycle visually but cars ignore them.
- Cost: densest 6000 unit cell has at most 145 (Miami), 67 (LA), 49 (NY) lamp + signal objects within 63000 units before culling, about 2 prims each: below the 128 cap. Cap by distance: `min(ViewDrawDistance(), 30000)` and fade the colour like `AddTrafficLight`.

Design:
- `objanim.c`: add tables for levels 4..7 (Miami about 21 entries, LA 14, New York 8, Frisco 8) registered in `Level_AnimatingObjectPtrs[4..7]` / `Level_NumAnimatedObjects[4..7]`, remove the `GameLevel < 4` guards. Add a `D1_LIGHT_DEF` table by internal id: kind (lamp, double lamp, sprite lamp, signal), x, y, z, y step. `animate_object` gets a `GameLevel >= FIRST_DRIVER1_CITY` branch calling `AddLightEffect` / `AddTrafficLight` from that table. Instance models TLIGHT03..18 get their own name entries with the same id.
- `debris.c`: add a size parameter to `AddTrafficLight` only if the D2 sizes (30 / 150) look too small next to the 1.5x D1 cars.

Steps:
1. Tables + branch, lamps only. Test with `-lightson` in the four cities.
2. Signals, static red.
3. Phase parity (check in game that cars stopped on a red really face a red head), then distance cap and fade.
4. Sprite lamps.
5. Regression: Chicago, Havana, Vegas unchanged.
Calibrate by eye: all offsets (especially Miami TLIGHT01/02, Frisco TLIGHT00, LA SLIGHT*), glow sizes, signal y spacing and z, phase parity flip.

Part A, extra step 0 (user report 2026-10-13, do it FIRST, it is in `debris.c` and fits this part): car headlights in the fog weather (`-fog`, `config.ini [game] fog=1`) reach very long and far, mostly in Frisco: a car standing on a road higher than the player (hill) shows a long light streak (seen as a tall vertical streak over the hill crest, `-city 5 -fog`, default start).
- Suspects, in order: (1) `ShowCarlight` (debris.c about 2014) calls `DisplayLightReflections` when `RoadWetness() > 9`, and `RoadWetness()` (debris.c about 1334) returns 7000 whenever `gFogTarget` is set, so every car in fog gets road reflections, placed at `MapHeight` under the car; on a slope or a higher road the reflection sprite is stretched. (2) `ShowFlare` size/length of the flares. (3) the player beam (`UpdateHeadlights`, draw.c, `PsyX_SetBeam`, cone straight ahead tilted down by +500 on y): on a hill the cone hits far geometry.
- Test: A/B per suspect in Frisco on the hill (`-city 5 -fog`, and `-startpos -11250 240000 -startrot 1024`), plus Chicago and Miami in fog. Fix: no reflections in plain fog (only real rain wetness), cap the streak length by distance and by the height difference to the camera. Keep D2 rain unchanged (D2 weather must look as before).

## Part B: D1 player cars

Findings:
- Cars per city: Miami 4 (D1 models 5-8), Frisco 4, LA 4, New York 5 (5-9). Each car owns 2 full texture pages (A 26-27 textures, B 33). Converted perm pages 12 / 14 / 11 / 13; max pages per area 6 / 4 / 7 / 5; so exactly one spare slot (18, used by the Tanner shadow window). Two player car pages cost 2 slots: area pages come up short by 2 (1 if the Tanner window may overlap slot 18).
- Donor pages cannot be dropped: 58 Tanner head, 63 body, 50 HUBCAP1 + wheels + minimap, 1 PEDHEAD1, 2 GREENCONE (also FLARE / LENSFLR / LIGHT / SMOKE).
- The converter writes `numSpec = 0` and drops the spec page data (`ConvertTextures`); `spool.c` (about line 2474) disables the special spool in D1 cities. A real spool is not needed: `LoadPermanentTPages` already loads the 2 pages of `residentCarModels[5]` when `nspecpages != 0`, and `ProcessCarModelLump` already reads slot 5 (the converter moved D1 models 5+ to 8+).
- Damage: the D1 damaged model has the same vertex count as the clean one in every car (126/126 etc.), vertices only, all vertices differ (median 30 units, max 55-170): exactly what `DentCar` (denting.c) expects. Missing: `gCarDamageZoneVerts`, `gHDCarDamageZonePolys`, `gHDCarDamageLevels`, which `LoadDenting` wipes for D1. Damage textures `*_DM0..DM3` sit at x 0/64/128/192 on one row (D2 `u3 * 64` offset trick works unchanged). Cars have 200-250 polys, fits the u_char poly index.
- Lights: `sD1LightFixes` (cosmetic.c) has 5 entries (civ cars). Player cars borrow cosmetics by number from the Chicago LCF specials 8-12 and are rescaled by `D1ScaleCarCosmetics`: up to 17 cars (4+4+4+5) need an eye check of lamps and wheel track/base; try a derivation rule first (corner vertices, extreme x/z, the "LGT_DM0" textured polys). `SetupSpecCosmetics` / `SetupSpecDenting` only run from the spool: a D1 init (cosmetics + FixCarCos + denting for slot 5) is needed.
- Car select: `CarSelectScreen` uses `carNumLookup[city][sel]`; pictures come from `DATA\CARS\*.RAW` (0x8000 per picture, loaded into `extraRect` (896,256,64,219)); D1 has none. `CarSelectable`, `SetupResidentModels` (mission.c) and the `texture.c` guards need the D1 table.
- Risks: engine sound bank (`gamesnd.c`: model 8-10 mapped to D2 banks); `civ_ai` special slot in traffic (off while the spool is off).

Steps (riskiest first):
1. Converter writes spec list + page data; `D1SetupCarTpages` fills `specTpages[GameLevel]` from the polygon pages of models 8+; `texture.c`: if the slot-5 model is below 8 load nothing (today it falls back to the first car's pair and burns 2 slots). Test `-playercar 8` in each D1 city and MEASURE how many area pages go missing (log it).
2. D1 cosmetics + denting init for slot 5.
3. Converter writes a D1 `.DEN` in the existing layout: zones by clean vertex position (6 zones), a poly belongs to a zone if its vertices do, per-poly level from whether its texture has DMn siblings, caps 50 verts / 70 polys per zone (check the runtime array sizes). Fallback with no geometry: swap to the damaged model above a damage threshold. Read `DamageCar3D` (bcollide.c about 372) for the meaning of zones 2 and 5.
4. Remove the model guards, add the D1 car select table.
5. Lights and wheels by eye per car.
6. Car select pictures (converter writes `*.RAW` per city, rendered offline or placeholders; layout of clut: read `SetupExtraPoly`, FEmain.c about 1244).
7. Only if step 1 shows visible texture starvation: merge pages A and B into one (drop DM2/DM3 from A, move B's DM0/DM1 into x 128-255 / y 128-253 of A, add u + 128 to B's polys), or no Tanner shadow window while a D1 car is chosen, or merge donor pages 1/2/50.
Not confirmed: starvation size (nothing run in game), `ProcessCosmeticsLump` filling `car_cosmetics[5]`, the meaning of damage zones 2 and 5.

## Part B status (2026-10-14) and Part C: car select pictures

Done in Part B (uncommitted at the time of writing, see `CLAUDE.md` "D1 player cars"): special pages + `specTpages`, 21 texture slots (D1 area pages all fit), default car 8, car select list, cars drawn at `D1_CAR_SIZE_PERCENT` 120 of a D2 car (`D1CarScaleFactor`, cosmetic.c), pedestrians and Tanner `D1_PED_SCALE_PERCENT` 120, wheels derived from the arches (85% of the arch fill), per model table `sD1CarFixes` (body height, lamp shifts, vertex count guard), physics from a D2 donor of about the same length (`D1PickDonor`), drawing and physics separated. Still open: denting (steps 2/3), sound bank mapping, Part C.

Part C (user decision 2026-10-14): menu pictures for the D1 car select screen, made from the player's own data, never committed.
- Default: no picture, the screen shows "Car n of m" (done, `DisplayOnScreenText`, FEmain.c).
- Optional picture run: an in-game choice (row in Options - Gameplay - Secret - Extended Settings, off by default) lets the player allow the game to photograph the cars of a city. The run starts the game itself per car (`-city N -mission 50 -playercar M -camangle 3584 -carpic <file>`, hidden/short window, Windows only, nothing on other platforms), takes the frame at about game frame 150, crops 3/4 front view, and stores a 256x219 4 bit picture in `DRIVER1\CONVERTED\` (menu palette, see below). The select screen loads that file when it exists, else the text.
- Menu picture format (checked by decoding `DRIVER2\DATA\CARS\CCARS.RAW`): 0x8000 bytes per picture, 4 bit, 256x219 pixels (64x219 VRAM words at `extraRect` (896,256)), drawn by `extraSprt` with CLUT (960,256) = palette 0 of the frontend palette (`GFX.RAW` 0x58000, 16 colours); the pictures are dithered sepia, 3/4 view from the front left in an oval vignette. Style = luminance mapped onto that palette with ordered dithering.
- `-shot` code is `DEBUG_OPTIONS` only (`PsyX_TakeScreenshot` writes a BMP): the picture run needs a framebuffer read that also exists in release builds (PsyCross fork `E75/PsyCross`, separate repo/commit) or must be limited to dev builds: decide first.
- Reference screenshots (side, rear, front, 3/4) of all 17 cars: `test_scripts\d1cars\` (outside the repo).

- D1 car physics (2026-10-14, research of the D1 EXE `all.c`): the D1 player car step (`FUN_8001bb28` / `FUN_8001d6f8` / input `FUN_8001fe6c`) is the same code as `AddWheelForcesDriver1` in `wheelforces.c`; D2 is D1 with every length and force divided by 1.4648 (D1 cell 3000, D2 cell 2048). The EXE has NO per car mass / power / grip table, only the global constants (throttle 7200, brake -7000, gravity -10922, drag -12 v, spring 230 / -100 with cap 60 against 42, slip clamp 18000 against 12500, hand brake +656 / -375, twist 110 / 550) and per car geometry (100 byte entries at EXE 0x84e2c, 22 of them: wheel x / z). So `D1ScaleCarCosmetics` (cosmetic.c) now gives every D1 player car the same neutral values (`D1_CAR_*` in dr2limits.h: mass, power, traction 4096, twist 236 / 236 / 1180 = D1 110 / 550 x 1.4648^2) instead of those of a D2 donor car (Frisco and New York donors had power 3000, Miami 4096, twist 180 to 250). Measured with a flat out run in Miami and Frisco: identical speed curve (65 after 30 frames, 97 after 60). World scale (2026-10-14, user agreed): the world is MAP_CELL_SIZE / 2048 (3000 / 2048 = 1.4648) bigger than D2, so every FORCE is scaled by it, `WORLD_FORCE` in wheelforces.c (gravity, thrust at its use in AddWheelForcesDriver1, spring force, slip clamp 12500, collision friction table); factor 1 in D2 cities (exact). Not scaled: spring compression cap and ride height (car geometry stays D2 sized), drag per velocity (12), speed thresholds. Road speed limits and the felony limit follow it (`WorldSpeedLimit`, civ_ai.c). Flat out in Miami: top speed 204 (D2 limit 138 x 1.46 = 202), ride height unchanged. Brake is x1.46 too, D1 itself has -7000 against -5000 of D2 (x1.4).