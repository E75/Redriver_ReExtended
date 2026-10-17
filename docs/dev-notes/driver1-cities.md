# Plan: Driver 1 cities DLC for REDRIVER2

Branch: `feature/driver1-cities` (from `E75/REDRIVER2` master via remote `e75`). Push ONLY to remote `origin` (= `E75/Redriver2_Extended`): `git push origin HEAD`. Never push to `e75`.
Goal: Miami, San Francisco, Los Angeles, New York, Newcastle (`CREDITS.LEV`) and Training (`TRAIN.LEV`) are selectable in REDRIVER2 and play with REDRIVER2's own mechanics: civ traffic, cops + pursuit AI, pedestrians, on-foot Tanner, damage, physics. Driver 1 cars drive in those cities.

**Strategy (user, 2026-10-11):** Parts 2-5 are done for ONE city first, Miami, end to end (loads, drives, civ traffic, cops, peds, on foot). Every finding goes into `plans/driver1-d2-differences.md`; only then Frisco, LA, New York, Newcastle and Training are run through the same converter, with per-city fixes in a table, not in new code.

Read this whole file before starting a part. One part per session, one green commit per part. Update section 0 at the end of every part.

---

## 0. Status

| Part | State | Commit |
|---|---|---|
| Architecture skeleton (`d1dlc` library, `D1Conv` CLI, startup detection, README) | done 2026-10-11 | see `git log` "Driver 1 DLC: architecture" |
| 0 Format spike + risk analysis | done 2026-10-11 (findings in section 2b) | same commit |
| 1 Engine: 10-city tables + safe defaults | done 2026-10-11, frontend button `⚠ niet getest` (see 0a) | see `git log` "Driver 1 DLC: engine" |
| 2 Converter: container, textures, models, donor merge | done 2026-10-11 (all six cities except Newcastle/Training: donor pages do not fit, see differences doc section 7) | see `git log` "Driver 1 DLC: converter part 2" |
| 3 Converter: cells, regions, spool | done 2026-10-11: Miami, Frisco, LA, New York load and render textured in the game (conversion 0.12 s each, run at first use); flat heights, no roads yet (differences doc section 8) | see `git log` "converter part 3" |
| 4 Converter: heights + surfaces | done 2026-10-11: height maps of all four cities match the D1 oracle (roads max error 10 to 33 units, `d1conv heights`); D1 has no multi level cells, so bridges are one layer plus collision of the model boxes (not drive tested yet) | see `git log` "converter part 4" |
| 5 Converter: road graph | done 2026-10-11: Miami, Frisco, New York 12-13 civ cars drive 90 s without confusion, LA 10-11 while driving, roadblocks + pursuit cops spawn, 15 pedestrians on pavements, Tanner leaves and re-enters the car; traffic drives on the right (differences doc section 10) | see `git log` "converter part 5" |
| 6 Driver 1 car park | in progress 2026-10-11: step 1 done (palettes, car texture pages, lighting fallback, denting off; the cop and civilian cars show their D1 textures and colours in all four cities, see CLAUDE.md). Step 2 done: cosmetics scaled to the D1 model size (camera distance, wheels, lights, collision box), residents and car select list limited to the cop car and two civilian cars. Open: player cars (VRAM slot budget), car select pictures, damage, pedestrian scale | see `git log` "car park" |
| 7 Take a Ride, overlay map, frontend polish | done 2026-10-11 for Miami, Frisco, LA, New York: overlay map, "Preparing Driver 1 cities" message, `d1TestUseChicago` removed, README. Newcastle/Training stay hidden (converter: out of texture pages, no overview map data in TRAIN.LEV) | see `git log` "overlay map", "Part 7" |
| 8-11 extras requested by the user (2026-10-11) | open, see Parts 8 to 11; every part must work in D2 (original) AND D1 (DLC) | |
| 8 Draw distance + crowd density (2x-5x, secrets menu), from the far-view branch; last step | open | |

### 0a. Part 1 result (read before Part 2)
- `NUM_CITIES 10`, `IS_DRIVER1_CITY` in `dr2limits.h`. `CityBase[]` (mission.c, macro `BASE_CITY`) = the D2 city whose sound, music, sky, sun, light colours, debris colours, cosmetics (LCF) and DEN a city borrows: Miami->Havana, LA->Vegas, others->Chicago (deviation: LA light colours are Vegas, not Chicago). Tables that belong to the LEV (citylumps, `LevelNames/Files`, carTpages, specTpages, SpecialByRegion, overlaidmaps, objanim) are indexed by the real `GameLevel`; D1 rows copy Chicago (stand-in) and are TODO for Part 6 (car/spool tables) and Part 7 (overlay map).
- Events, env sounds, tunnels, mission sounds, props stay on real `GameLevel` (D1 = none). `InitTunnels(0)` for D1.
- `SetCityType`: D1 city -> Chicago LEV (incl. night via NLEVELS) when `d1TestUseChicago=1` (config.ini `[game]`), else `D1Dlc_ConvertCity` path (currently only an existing, version-stamped `DRIVER1\CONVERTED\<NAME>.LEV`; converter = Part 2). `D1Dlc_CityAvailable(level)` gates the frontend.
- Take a Ride: `LoadMission` keeps `GameLevel` for D1 (the Chicago mission 50/51 is played, header city ignored); `glaunch.c` maps D1 to Chicago mission numbers. Start position = mission header (Chicago) for now (Part 7).
- Frontend: Take a Ride city screen (screen 1) gets a 5th "Driver 1" button -> code-built screen 41 (`Driver1CitySelectScreen`, user function 25) listing available D1 cities -> time of day. Car select uses the Chicago list (`carCity`). `& 3` city arithmetic in `CityCutOffScreen` replaced by button index. Not tested by hand: needs a visual check.
- Part 2 (see `plans/driver1-d2-differences.md` section 7): `d1conv repack|check|convert`; Chicago repack is byte identical (except pad bytes) and runs in the game; Miami, Frisco, LA and New York convert and pass `d1conv check`. Output has no MAP/SPOOLINFO/road lumps yet, so the game cannot load it before Part 3. `D1Dlc_ConvertCity` is still a stub that only accepts an existing LEV.
- Dev: `-city N` command line option (DEBUG_OPTIONS) + `-mission 50` starts a D1 city directly; log line `SetCityType: city N (NAME) -> path`. With the stand-in every D1 city looks like Chicago by design.
- Tested: cities 0,1,2,3,5,7,9 load and run 15 s without crash; D2 cities log their own LEV.

Answered questions (user, 2026-10-11): cities = Miami, Frisco, LA, NY + Newcastle and Training (added after the first commit); D1 reader written fresh (no new dependency); D1 cars only in D1 cities; Take a Ride only in v1; test install copy allowed.

---

## 1. Decisions

### 1a. DLC model (user requirement)
- REDRIVER2 ships **no Driver 1 asset** (licensed game). The player copies `LEVELS` + `DATA` of their own Driver 1 disc into `<install>\DRIVER1\`, exactly like `DRIVER2\` for Driver 2 data (see README section "Driver 1 cities").
- At startup REDRIVER2 detects the folder (`D1Dlc_Detect`, `gD1DlcCityMask`) and enables only the cities whose LEV is present. No folder -> game unchanged.
- Conversion runs **inside the game** (first start, or when `D1DLC_CONVERTER_VERSION` changed) and writes D2-format LEVs to `<install>\DRIVER1\CONVERTED\` (+ a version stamp). Show a "Preparing Driver 1 cities" message while it runs. `D1Conv.exe` is only a developer CLI around the same library.
- Nothing the converter needs may come from outside the player's files: **no `cosmetics.lcf` from OpenDriverEngine** (it holds data extracted from the Driver 1 executable). Car cosmetics are derived from the car models themselves (Part 6). Driver 2 donor content (Tanner, pedestrians, wheels) comes from the player's own `DRIVER2\LEVELS\CHICAGO.LEV`.
- Take-a-Ride missions for D1 cities are built in code (no D2MS files shipped).

### 1b. Converter (D1 LEV -> D2-format LEV) instead of a runtime D1 backend or OpenDriverEngine

| Option | Verdict |
|---|---|
| A. Convert D1 LEV to D2 format, REDRIVER2 loads it as city 4..9 | **Chosen.** AI/peds/on-foot/cops code stays untouched, it only ever sees D2 data. Converter is deterministic and testable (`D1Conv`). |
| B. Runtime D1 backend in REDRIVER2 | Rejected: `draw.c`, `cell.c`, `dr2roads.c sdGetCell`, `civ_ai.c` assume 2048-unit cells, packed cell objects, 1024-unit sdPlane grid and the D2 road graph (hundreds of call sites). |
| C. Port REDRIVER2 into OpenDriverEngine | Rejected: ODE has no traffic AI, cops, pedestrians, on-foot, missions or frontend; its D1 heightmap is marked "IN PROGRESS, some heights invalid". |

ODE / OpenDriver2Tools are **format references only**: clones in `<parent folder>/_ref\OpenDriverEngine` and `...\_ref\OpenDriver2Tools` (outside the repo). Key files: `OpenDriverEngine/routines/d2_types.h` (structs), `routines/regions_d1.cpp` (D1 spool, `GetRoadInfo`, `FindSurface` incl. Frisco hack), `routines/level.cpp` (lump walk), `routines/textures.cpp:406-560` (TEXINFO, D1 palettes), `routines/models.cpp:87-160,224-270` (car model + model lumps), `routines/models.cpp:380-470` (poly type decode), `game/scripts/driver1_cities.lua` (overlay map params, sky choice), `game/scripts/driver1_cosmetics.lua` (D1 car physics defaults: these are numbers, not assets — OK to reuse as constants).

---

## 2. Pinned facts

### 2a. Formats (measured 2026-10-11)
Player data on this PC: `E:\Games\Driver Series\OpenDriver2Alpha27\DRIVER\{LEVELS,DATA}`; copied into the test install as `E:\Games\Driver Series\REDRIVER2_D1\DRIVER1\{LEVELS,DATA}`.

| Level | cells (D1, 3000 units) | regions (30 cells) | models | car models | D1 roads | D1 junctions | road map (1500 units) | height range |
|---|---|---|---|---|---|---|---|---|
| MIAMI | 300x420 | 140 | 653 | 7 present | 365 | 234 | 600x840 | -1000..28 |
| FRISCO | 540x540 | 324 | 534 | 17 lump | 358 | 223 | 1080x1080 | -15032..1500 |
| LA | 540x540 | 324 | 633 | 18 lump | 259 | 167 | 1080x1080 | |
| NEWYORK | 480x480 | 256 | 670 | 19 lump | 410 | 258 | 960x960 | |
| CREDITS (Newcastle) | 150x150 | 25 | 175 | 11 lump | 16 | 15 | 300x300 | |
| TRAIN (Training) | 240x240 | 64 | 100 | 11 lump | 1 | 1 | 480x480 | |
| CHICAGO (D2) | 512x768 (2048) | 384 (32 cells) | 1205 | 9 present | 779 straights, 151 curves, 252 junctions | | - | |

- LEV container identical (LUMP_LUMPDESC + `OUT_CITYLUMP_INFO`). D1 sub-lump lists start with an `int` count; D2 runs until type 255. Implemented in `src_rebuild/d1dlc/lev_file.cpp`.
- D1 lumps: TEXNAMES, TEXINFO, MODELNAMES, ROADMAP (w,h), PALLET (`PALLET_INFO_D1`), MAP (16-byte `CELL_OBJECT` straddlers), MOTIONCAP x4, CAR_MODELS, MODELS, SPOOLINFO, ROADS, JUNCTIONS, ROADBOUNDS, JUNCBOUNDS, SUBDIV, ROADSURF, OVERLAYMAP, CHAIR, LOWDETAIL. D2 needs STRAIGHTS2 40, CURVES2 41, JUNCTIONS2_NEW 43 and 17 MOTIONCAP lumps instead of the D1 road lumps.
- D1 spooled region: roadm (RLE `short len, ushort id`, 60x60 per region, -1 ends), roadh (RLE `uint len, uint value`: low 16 = height, bits 16-25 = model/surface type, bits 30-31 = rotation), cell pointers, `CELL_DATA_D1`, unpacked `CELL_OBJECT_D1`. Height = road map value + optional `SURFACEINFO`/`SIPOLY` polygons (ROADSURF), see `regions_d1.cpp FindSurface`.
- D2 heightmap per region (`Game/C/dr2roads.c sdGetCell:323`): `short buffer[]`: [0]=2, [1]=byte ofs `sdPlane[]`, [2]=byte ofs BSP lists, [3]=byte ofs `sdNode[]`, [4..4100)=64x64 grid of 1024-unit cells; value -1 sea, `0x4000|n` BSP node, `0x2000|n` multi-level list, else plane index. `sdPlane.surface` = road id + 32 (0x0000 straight, 0x2000 junction, 0x4000 curve) or `SurfaceType` < 32 (CONCRETE 0, GRASS 4, WATER 6, ALLEY 8, DEEPWATER 9, SAND 11). Height: `sdHeightOnPlane` dr2roads.c:261.
- Converter output must use D2 grid: 2048-unit cells, 32-cell regions (`RoadMapDataRegions` 2x2 barrel, `>> 11`/`>> 10` shifts). Miami -> 448x640 cells, Frisco/LA -> 800x800, NY -> 704x704, Newcastle -> 224x224, Training -> 352x352.
- Newcastle and Training are small: Training has 1 road + 1 junction (car park / test ground) -> traffic may be near zero there; that is accepted, they must still load, drive and allow on-foot play.
- Structs with sizes asserted: `src_rebuild/d1dlc/lev_format.h` (note `DRIVER2_STRAIGHT` is 28 bytes: natural alignment pad at the end).
- Engine hooks that already exist: `mission.c:420` (loose `MISSIONS\M%d.D2MS`), `cars.c:971` (`LEVELS\<city>\CARMODEL_<n>_{CLEAN,DAMAGED,LOW}.MDL`), `cosmetic.c:36` (`LEVELS\<city>\CARMODEL_<n>.COS`), `system.c:19` `gDataFolder` = `DRIVER2\`. `ROADS_GetRouteData` (`roadbits.c:4`) is a stub: D1 route data has no consumer.
- Named models the engine looks up (take from donor CHICAGO.LEV): HEAD, NECK, TORSO, HIPS, U_ARM_*, L_ARM_*, HAND_*, THIGH_*, CALF_*, FOOT_*, JERI_*, PEDHEAD1, CLEANWHEEL, FASTWHEEL, DAMWHEEL, HUBCAP1, HUB, GREENCONE, CRATE, BOMB, BAG, DETONATOR, FOAM. City props (TRAIN, ELTRAIN, MONORAIL, FERRY, BOAT01, LORRY, LIFT, ROTOR, CHOPPER*) not needed (events off for D1 cities).
- City coupling: 215 `GameLevel` uses in 29 files. 4-entry tables: `LevelNames`/`LevelFiles`/`LoadingScreenNames` (system.c:130-149), `citylumps[8][4]` (system.c:168, must grow to `[NUM_CITIES]` = 10), `CosmeticFiles` (cosmetic.c:13), DEN names (denting.c:40), day/night vectors + colours (draw.c:56-80), `debris_colour[4][31]` (debris.c:143), `CarAvailability[4][10]` + `carNumLookup` (FEmain.c:577), frontend wrap `GameLevel 0..3` (FEmain.c:3351-3399; `& 3` at 2792, 3139, 3518), mission numbers (glaunch.c:169-269), music (main.c:536-578), sound banks (gamesnd.c:177-257, 389-396, 475-479), sky (sky.c, 20), events (event.c, 22), score tables (overlay.c:549-625, scores.c:159-175), Chicago/Vegas road patches (dr2roads.c:175-257, keep gated), VEGAS spool fix (spool.c:943), objanim.c:462-642, motion_c.c:1774, pedest.c:2535/2762, shadow.c:420, handling.c:1127, models.c:69-90, mc_snd.c:387-413, texture.c:194-218, overmap.c (20). `RoadMapDataRegions[4]` (dr2roads.c:27) is the region barrel, not cities — leave it.

### 2b. Risk analysis results (Part 0, `src_rebuild/tools/d1conv/spike/d1spike.py`)
Run: `python -I src_rebuild/tools/d1conv/spike/d1spike.py <LEV> [--ppm out.ppm] [--dump-roads N]`.

| # | Risk | Finding | Consequence / mitigation |
|---|---|---|---|
| R1 | D1 road layout (curves?) | All D1 roads are **axis-aligned** (`ROADBOUNDS.dir` 0/1 only), bounds in 1500-unit road-map cells, narrow side 1-2 cells. Road graph rendered for Miami: a pure grid of straights + junction tiles. | No curves needed (CURVES2 count 0). Straights: Mid = bounds centre * 1500 (convert road-map cells to world), angle 0 or 1024. **Low risk.** |
| R2 | Road id encoding in roadm | High bits: `0x0xxx` road (Miami 15487 cells), `0x4xxx` junction (1247), `0x8xxx` unknown (4357), `0xFFFF` none. Junction entries = `{fromJunc, road, flags, extraFlags}` x4, type 0/1. | Part 5 must decode `0x8000` first (hypothesis: pavement/side tile of a road; check against ROADBOUNDS). **Medium.** |
| R3 | Building collision | Models with collision boxes: Miami 116/653, Frisco 141/534, Chicago 215/1205 (similar share). | D1 uses the same box mechanism; no generated boxes needed. **Low.** |
| R4 | World poly types | D1 world: 10, 11, 20, 21 (as D2) **plus 16/17 (F3/F4 flat colour), 22/23 (GT3/GT4), 5, 8, 9, 1**. REDRIVER2 `draw.c` world plotter only draws 11/21/23 after promoting even types; flat-colour types are skipped. | Part 2 converts 16/17/8/1 to textured types with a white texel (or adds a flat path), 5/9 checked against `PolySizes`. Miami 65, Frisco 287 flat polys would otherwise be invisible. **Medium.** |
| R5 | Car poly types | D1 cars: 0, 1, 20, 21, 22, 23; D2 cars only 0, 20, 22 (triangles). | Part 6 splits quads (21 -> 2x20, 23 -> 2x22, 1 -> 2x0). **Medium.** |
| R6 | Textures / VRAM | Miami 82 pages (7 permanent, 0 special), Frisco 72 (9 perm, 68 special?), Chicago 70 (11 perm, 62 special). | Page counts are close to D2; donor pages (Tanner/peds/wheels) must fit: Part 2 measures free VRAM slots. Spike's "special" count for D1 needs a re-check (layout may differ). **Medium.** |
| R7 | Heights | Miami flat (-1000..28); Frisco -15032..1500 (hills); ODE marks D1 heights partly invalid. | Part 4 error report vs. oracle; fallback to plain road-map height. **High** for Frisco. |
| R8 | Car cosmetics without LCF | `cosmetics.lcf` is not the player's file -> not allowed. | Part 6 derives wheel positions/lights from model geometry + ODE D1 defaults (cPoints, colBox 185/130/520, wheel height -165). Optional later: read table from the player's own D1 executable if they copy it. **Medium.** |
| R9 | Conversion time at startup | Measure in Part 3/4 (C++). Target < 10 s per city; cached afterwards. | If slower: convert one city on first selection instead of all at startup. **Low.** |
| R10 | Build environment | This PC had no deps; `windows_dev_prepare.ps1` downloaded premake/SDL2/OpenAL/jpeg into `src_rebuild/dependencies` + `premake5.exe` (git-ignored). Its last step (open .sln) fails harmlessly. | Documented in CLAUDE.md. |

---

## 3. Architecture (in place)

```
<install>\DRIVER1\LEVELS\*.LEV  (player)          <install>\DRIVER2\LEVELS\CHICAGO.LEV (player, donor)
            │                                                  │
            └──────── d1dlc library (src_rebuild/d1dlc) ───────┘
                       D1Dlc_Detect      (startup, done)  -> gD1DlcCityMask
                       D1Dlc_Convert*    (Parts 2-6)      -> <install>\DRIVER1\CONVERTED\<CITY>.LEV + CARMODEL files
                                 │
REDRIVER2 (cities 0..3 D2, 4..9 D1 when gD1DlcCityMask bit set)
D1Conv.exe (src_rebuild/tools/d1conv) = developer CLI: info / detect (later: convert, check)
```

Files (all CRLF, no BOM, like the siblings):
- `src_rebuild/d1dlc/lev_format.h` — D1 + D2 structs, lump ids, size asserts.
- `src_rebuild/d1dlc/lev_file.h/.cpp` — `LevFile`: loads a whole LEV, detects D1/D2, lump directory (`FindLump(type, index)`, `At<T>(offset)`).
- `src_rebuild/d1dlc/d1dlc.h/.cpp` — city table `g_D1DlcCities` (GameLevel 4..9: MIAMI, FRISCO, LA, NEWYORK, NEWCASTLE = CREDITS.LEV, TRAINING = TRAIN.LEV), `D1Dlc_Detect`, `gD1DlcCityMask`, `D1DLC_CONVERTER_VERSION`.
- `src_rebuild/tools/d1conv/d1conv_main.cpp` — CLI. `src_rebuild/premake5_d1conv.lua` — project `D1Conv` (included from `premake5.lua`); `premake5.lua` also adds `d1dlc/**` to project REDRIVER2.
- `src_rebuild/redriver2_psxpc.cpp` — calls `D1Dlc_Detect("")` after `PsyX_Initialise`, logs `Driver 1 cities: mask 0x..`.

Contract for the next parts (pin, do not rename):
- `bool D1Dlc_ConvertCity(int dlcIndex, const char* dataRoot, char* outPath, int outPathSize)` — converts if `DRIVER1\CONVERTED\<NAME>.LEV` is missing or its stamp `<NAME>.VER` differs from `D1DLC_CONVERTER_VERSION`; returns the converted LEV path. Called from `SetCityType` (system.c) for `GameLevel >= D1DLC_FIRST_CITY`.
- Converted LEV + car files are opened with an explicit path (`DRIVER1\CONVERTED\...`) instead of `gDataFolder`; every other file (sounds, frontend, missions) still comes from `DRIVER2\`.
- `IS_DRIVER1_CITY(lvl)` = `(lvl >= D1DLC_FIRST_CITY)`; `NUM_CITIES` = 10.

---

## 4. Parts (one session each, one commit each)

Build (this PC): in `src_rebuild`: `set SDL2_DIR=.\dependencies\SDL2-2.30.2`, `OPENAL_DIR=.\dependencies\openal-soft-1.23.1-bin`, `JPEG_DIR=.\dependencies\jpeg-9d`, `.\premake5.exe vs2022` (only after adding files), then
`"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe" build\REDRIVER2.sln -p:Configuration=Release_dev -p:Platform=Win32 -p:PlatformToolset=v145 -m -nologo -v:m`.
Game into the test install: build `build\REDRIVER2.vcxproj` with `"-p:OutDir=E:\Games\Driver Series\REDRIVER2_D1\\"`. Run: `Start-Process .\REDRIVER2_dev.exe -WorkingDirectory "E:\Games\Driver Series\REDRIVER2_D1"`, close with `CloseMainWindow()` (log only flushes on a graceful exit), log `REDRIVER2.log`.

### Part 1 — Engine: 8-city tables + safe defaults
- Add `NUM_CITIES 10` + `IS_DRIVER1_CITY` (include `d1dlc/d1dlc.h` where needed or mirror the constants in `dr2limits.h`).
- Extend every 4-entry table in 2a to 10 (D1: names MIAMI, FRISCO, LA, NEWYORK, NEWCASTLE, TRAINING; DEN: none; colours: Havana for Miami, Chicago for the others).
- Each `GameLevel` switch/if site: D1 default (no events/props; sky + music + sound bank mapping Miami->Havana, Frisco->Chicago, LA->Vegas, NY->Chicago, Newcastle->Chicago, Training->Chicago). Prefer one `CityProfile` table (music id, sound bank city, sky set, colour set, overlay params) over scattered `if`s.
- `SetCityType`: D1 city -> `D1Dlc_ConvertCity` path for every CITYTYPE (no night LEV).
- Frontend: city cycling over the cities that are available (D2 always, D1 only if bit set in `gD1DlcCityMask`) in Take a Ride (FEmain.c:3351-3399), `& 3` -> lookup; city names; `CarAvailability[NUM_CITIES][10]`, `carNumLookup`.
- Missions for D1 cities: build the Take-a-Ride mission in code (copy of the Chicago one with city + start position) instead of a D2MS file; other modes hidden for D1 cities.
- Test without converter: dev-only config `d1TestUseChicago=1` makes `D1Dlc_ConvertCity` return `DRIVER2\LEVELS\CHICAGO.LEV` -> city 4 must load, drive, have traffic, peds, on foot. Remove in Part 7.
- Done when: game builds, D2 cities unchanged, city 4 selectable only with `DRIVER1\` present, plays with the Chicago stand-in.

### Part 2 — Converter: container, textures, models, donor merge
- `D1Conv convert <D1.LEV> <donor D2.LEV> <out.LEV>` and `D1Conv repack <D2.LEV> <out.LEV>` (same library code as the game).
- Write D2 container (LUMPDESC, loadtime, tpages, inmem, spooled; 2048-byte sector alignment).
- Convert: TEXNAMES, TEXINFO, tpages, PALLET (`PALLET_INFO_D1` -> D2), MODELNAMES, MODELS (flags2 remap per spike histogram; poly types per R4), LOWDETAIL, OVERLAYMAP, CHAIR, SUBDIV.
- Donor merge: named models from 2a + their tpages/cluts + all MOTIONCAP lumps from CHICAGO.LEV; D1 model indices unchanged, donor models appended (MAX_MODEL_SLOTS 1536).
- Done when: `repack` of Chicago plays identically; D1 output passes a self-check (offsets inside file, lump sizes).

### Part 3 — Converter: cells, regions, spool
- Re-grid every D1 cell object (spooled + straddlers) into 2048-unit cells / 32-cell regions; packed cell objects; cell pointers; PVS all visible; flat heightmap placeholder (one plane per cell `{surface 0, a 0, b 0x4000, c 0, d 0}`); SPOOLINFO areas + tpage lists rebuilt.
- Done when: Miami loads, free camera (F7, `freeCamera=1`) shows the city textured; conversion time measured (R9).

### Part 4 — Converter: heights + surfaces
- User requirement (2026-10-11): collision of the cities must work, cars must not drive through buildings or bridges. Building boxes come from the model collision blocks (same mechanism as D2, should work from Part 3 cell objects); bridges need the multi level height cells (`0x2000` lists, `sdLevel`) of Part 4 and a drive test under and over a bridge.
- Port D1 `GetRoadInfo` + `FindSurface` (+ Frisco hack) as height oracle; per 1024 cell sample on 64 units, quadtree split (axis sdNodes) until one surface id + plane error < 16; dedupe planes; surface = road id + 32 (junction bit, R2) or SurfaceType from model flags.
- Done when: error report max < 32 units on roads; Frisco hills drivable.

### Part 5 — Converter: road graph
- D1 roads -> `DRIVER2_STRAIGHT` (R1), junctions -> `DRIVER2_JUNCTION` (ExitIdx order learned from Chicago dump: `--dump-roads`), lanes/dirs/AILanes/parking/speed id; CURVES2 empty. Graph self-check (back-references, lane counts).
- Done when: Miami + NY: ≥ 20 civ cars drive 3 min on the roads, cop chase works, peds on pavements, Tanner walks and enters a car.

### Part 6 — Driver 1 car park
- Converted `CARMODEL_<n>_{CLEAN,DAMAGED,LOW}.MDL` (quads split, R5; D1 palettes) + `CARMODEL_<n>.COS` derived from model geometry + D1 defaults (R8); residents 5 civ + cop per D1 city; car select list per D1 city.
- Done when: every D1 car selectable in Take a Ride, damage works or is cleanly disabled, cops use the D1 cop car.

### Known issues found by the user / during tests (2026-10-11), to fix in Part 7 or earlier
- **Grass strips on roads and zebra crossings** (user screenshot, Miami): FIXED 2026-10-11. Cause: z-fighting between the D1 grass ground tiles and the road tiles under/over them (a vertical shift of 8 or even 120 units does not help at a low camera). Fix: `DrawAllBuildings` (draw.c) pushes the depth of grass models back with `PGXP_SetZOffsetScale(0, 1.01)` in D1 cities (the same trick tile.c uses); the converter still lowers grass 8 units (`LowerGrassModel`).
- **Garbage coloured dashes in the road centre** (user screenshot, Frisco): FIXED. D1 has textured polygon types with texture page 0xFF (= untextured polygon); the D2 renderer indexed `texture_pages[255]` out of bounds. The three building plotters in draw.c now skip polygons with `texture_set == 0xFF`. Open: draw them flat coloured like OpenDriverEngine (`models.cpp:523`).
- **Mini map does not render in D1 cities** (user): `overlaidmaps[4..9]` have toptile 0 (Part 1 stop gap), the overlay map comes with Part 7. The `OVERLAYMAP` lump is converted but the tile table of D1 is not.
- **Car sank into the road / pavement** (user screenshots, Frisco, Miami): FIXED 2026-10-11, three causes. (1) The D1 ground oracle had the wrong sign: road map heights and cell object y use the same convention (negative = up), so `D1HeightField::Eval` returns `height` (not `-height`) and the slope plane as `height + d - (a x + c z) / 4096`. Found by comparing the oracle with the vertices of the road/path tile models of all objects (98-100% of the path vertices agree in LA, Miami, Frisco, New York with the new sign, 28-63% before). (2) D1 pavement (`MODEL_FLAG_PATH`) tiles stand 25 to 57 units above the road map height; `Eval` adds the model's highest vertex (`ModelTopY`, only when <= 64 high). (3) `sdGetCell` returns the sea plane when the road block is not 4 byte aligned; the PVS buffer allocation in `spool.c` is now aligned (New York had it unaligned: every start was "in the water", Game Over). Open: steep Frisco hills (camera ends up 18000 units below the car, world not drawn), see below.
- **Frisco steep hill, camera below the ground: FIXED** (start -11250 240000 rot 1024). Cause was the mirrored height sign (see "Height sign" below), not the camera.
- LA civ cars only ping in once the player drives (ring of 16384 units around the player, the roads of LA are far apart); not a bug.

### Conversion QA for player supplied data (the game converts the player's own Driver 1 files at first start, it must be watertight)
- `d1conv convert <D1.LEV> <Chicago.LEV> <out.LEV>` must end with `0 problems` and `heights: ok` for every city; the log also lists regions without all models / texture pages (area border misses, known 2-6%).
- `d1conv probe <D1.LEV> <converted.LEV> <x> <z>` shows the D1 oracle against the converted D2 height at one point.
- Test the real engine without window tricks: `REDRIVER2_dev.exe -nofmv -city <n> -mission 50 -startpos <x> <z> -startrot <r> -shot <frame>` writes `SCREENSHOT.BMP` (bottom-up), `VRAM.TGA` and logs player/ground/camera height + the plane under the car (`shot:` lines) at that frame. Script: `test_scripts\shot_safe.ps1` (minimized window, retries with a normal window when the frame is black).
- Rules learned: never trust an oracle only against its own output, compare with the rendered mesh (tile vertices); an engine pointer that must be aligned (planes) must be checked in the loader; D1 data uses texture page 0xFF and polygon types the D2 renderer does not draw (1, 5, 8, 9, 16, 17).
- Height sign: the engine's `MapHeight` is positive = up (pedestrians: `position.vy = -98 - MapHeight`), cell objects are negative = up. `D1HeightField::Eval` computes in cell object sign and flips at the end; planes store the engine sign. Flat ground (height 0) hides a wrong sign, so always test on a hill: `-city 5 -startpos -11250 240000 -startrot 1024 -shot 300` must show the world and `ground height` positive (about 8600), camera above the car. (Found late: before the flip every height <> 0 was mirrored: sunk cars on hills/pavements, empty world.)
- Lane markings and zebra crossings are D1 polygon type 17 (flat coloured quad: id, 3 pad, v0..v3, r, g, b, code), only in the near detail tiles. The D2 tile code (`tile.c` `SubdivNxM`, `Tile1x1*`) read every tile polygon as a textured quad, so Miami sampled palm tree / grass texels (green dashes; LA/NY looked right by luck). Now `DrawFlatTileQuad` draws them as `POLY_F4` with the lit colour, and the far tile plotters skip them. Found by a read-only analysis agent on the D1 LEV, not by rendering.
- Plane normal: the engine uses `(a, b, c) >> 2` as surface normal in the wheel forces (`wheelforces.c`: `force.vy = susForce * surfaceNormal[1]`), so a slope plane needs a normal of length 16384 (`b = 16384 / sqrt(1 + sx² + sz²)`, `a = -sx * b`, `c = -sz * b`). `b == 0x4000` means "flat" to `sdHeightOnPlane`, so slopes keep `b <= 16383`. With `b = 4096` the car lost 4x spring force on every slope and sank 200 units into hills. Quantise the slope before building a, b, c (equal slopes must give equal planes, the plane budget per region is about 2040). Check: `-shot` logs `shot: car N above the ground`, expect about 30 on flat ground and on the Frisco hill.
- Start positions (Part 7 item, done): `g_D1DlcCities` start x/z/rotation = middle of a long flat 2+ lane straight near the road network centre (script logic in the session notes: STRAIGHTS2 lump entries after a 4 byte count, 28 bytes each, NumLanes at +22, rotation = straight angle). Newcastle/Training have none (Chicago start).

### Open check: car lamp positions of D1 cars (user request 2026-10-11, do before the final release)
- Fixed by eye per city and model (`sD1LightFixes`, cosmetic.c, notes in CLAUDE.md): head/brake/reverse lamps of cop, civ 1, civ 2 in Miami, Frisco, LA, New York. Still to double check: every city/model front and rear with `-lightson -camangle 4096|2048 -playercar 0|1|2`, also in motion (braking, reversing, indicators, siren), at night, and the D1 player cars (models 8+) once they exist (Part 6). New York civ 2 (taxi) and the police light bar were not calibrated. LA is forced to night (sky, lights on): check it once more with Part 10 (24 hour cycle), which must keep LA night.

### Part 7 — Take a Ride polish
- Done 2026-10-11: start positions, overlay map, loading screen (D1 cities reuse `LoadingScreenNames` entries of D2 cities, no D1 picture yet), "Preparing Driver 1 cities" (`ShowSavingWaitMessage` in `SetupDriver1CityButton`, only when `D1Dlc_ConversionPending()`), `d1TestUseChicago` removed, README.
- Known limit: Newcastle (`CREDITS.LEV`) and Training (`TRAIN.LEV`) fail in the converter ("out of texture pages (permanent 16)": D1 already marks 16 pages permanent, the donor pages do not fit) so they are not offered. Fix would be demoting unused D1 permanent pages to area pages. TRAIN.LEV has no OVERLAYMAP and no road network, so no minimap there anyway. Low priority; ask the user before spending time.
- Done when: four D1 cities selectable day and night, 10-minute play test each without crash (night: converted cities use the day LEV; see Part 10).

### Part 8 — Draw distance, crowd density, fog, safe spawning (user request 2026-10-11, updated 2026-10-12)
- Goal: far view + denser traffic/pedestrians from `feature/far-view-weather-cars` in ALL cities (D2 and D1), without FPS loss. Menu: Options > Gameplay > Secrets (rows next to the other session's "Stick Camera" row, see CLAUDE.md "Conventions").
- **Options (user 2026-10-12):** Draw distance = Default / 5x / 10x. Vehicles + people density = Default / 3x / 5x. Fog Off/On. Saved in the profile.
- **Meaning of the draw distance multiplier** (the far-view branch has none, only `gDrawDistance` = number of cells walked, default `PVS_CELL_COUNT^2` = 441 = radius 10 cells): Default = 441 cells, 5x = 2205 cells (radius 22), 10x = 4410 cells (radius 31 = cap, a region is 32 cells wide). World distance: D2 cell 2048, D1 cell 3000 (a D1 city shows more world per cell: tune per `MAP_CELL_SIZE`, `farClipLimit` 125000 on PC).
- **Far-view mechanics (analysis of cb2b3b5d):** square spiral walk with `drawRadius` instead of the diamond; cells outside the 21x21 PVS table skip the PVS lookup (frustum + `farClipLimit` only); 3x3 region slots (`REGION_SLOTS_ACROSS`, 9 regions loaded around the player, `CheckUnpackNewRegions` loads all nearest first); slot sizing for the biggest region (`ProcessSpoolInfoLump`, cell pointers are 16 bit: max 0xffff cells); `RebaseCellData` (cell object indices count from the 2x2 block start, rewritten region relative, `CellObjectIndex` adds the slot start); 32 bit frustum (`FrustrumCheck16` + `nearCell`); bigger draw lists (`MAX_DRAWN_*`: buildings 1024, tiles 2000, animated 128, sprites 512); `CellInLoadedArea` (cells of another `super_region` are skipped: wrong textures otherwise); fog = `PsyX_SetFog` (PsyCross shader, opaque prims only, fixed distance units).
- **Order (riskiest first, one green build per step):** (A) port the region/draw core to this branch (merge of the far-view files, `git merge-file` clean except `dr2limits.h`) and make every D1 city load at 1x/5x/10x; (B) draw distance rows + config + per-frame cost measurement (below); (C) density rows Default/3x/5x (existing `gCrowdMul`, map Default=1, 3x=3, 5x=5); (D) fog row; (E) spawn rings tied to the draw distance, nothing pops up in view.
- **Performance rule (user):** FPS must not drop. Measure at 1x, 5x, 10x in Chicago and a D1 city (frame time ms + cells walked + objects drawn from the `-shot` log / a dev counter), note the numbers in this plan, and when a setting is slow find the cause (draw lists, per frame cell walk, prim buffer, OT depth) before accepting it. Possible levers: LOD distances (`LOD_HIGH`/`LOD_LOW`), skipping tiny far objects, cull far cells earlier, cap per-frame primitives.
- Warning: the earlier uncommitted far-view experiment crashed Frisco, LA and New York at load: D1 regions are bigger (check `maxCells`, `maxObjects`, `maxRoadMap` slot sizes, 16 bit cell pointers) and the D1 converter must supply the same spool/region lumps.
- Done when: all cities load and play 10 minutes at Default and at 10x/5x without crash, FPS numbers noted here, spawn points never visible, rows work and are saved.
- **Status 2026-10-12:** (A) done: far-view core merged, all 8 cities load and render at Default / 5x / 10x (`OTSIZE` 0x4000 for D1 distances). (B) done: `gDrawDistanceMode`, `-drawdist`, menu Extended Settings > Draw Distance, profile `reserved[4]`; frame cost log in `-shot` (`step/build/submit ms`). Numbers (this PC, Release_dev, vsync off, ms per frame spent building the primitives, the simulation adds about 0.3 and the GL submit about 1.5; budget 33): Chicago 4.3 -> 11.5 (10x), Vegas 6 -> 11, Miami 3.3 -> 4.6, Frisco 2.5 -> 4.8, LA 2.9 -> 6.8, New York 3.5 -> 11.4. No optimisation needed on this PC; check on a slower machine before raising the cap. (C) done: menu Traffic And People Default / 3x / 5x (`gCrowdMul` 1/3/5, `reserved[3]`). (E) done: no pop-in of cars (0 pop-ins in view at 1x and 10x against 45 to 114 with the old ring, `-oldspawn`), pedestrians use the same rule, cars fade half transparent over the last 20% of the draw distance, ping-out distance follows the draw distance. (D) fog open: needs the PsyCross fork (decision of the user needed: `PsyX_SetFog` lives in the far-view fork of the PsyCross submodule). Spawn ring per city (2026-10-12): in D1 cities and at crowd > 1 the spot is a random point on a ring (radius 0.75 to 1.0 x 16384 world scaled, up to the draw distance + 3000) instead of the 44 fixed cookie spots, and the final lane position is checked against the view cone too. Cars at 5x (of 68): Chicago 67, New York 67, Miami 59, LA 24, Frisco 18 (stationary test player; Frisco/LA have few roads near the start); pop-ins in view 0 to 3 (was 45 to 114); the `-shot` log has `ping in results` (reasons for a refused spawn, `gPingFail`). Pedestrian spawn band (same day): pedestrians at 5x Chicago 46, NY 29, Miami 23, Frisco 12, LA 10 (were 31, 18, 25-37, 5, 7), D2 crowd 1 unchanged. Car counts fixed (same day): the min gap to the nearest car was the limit (95% "too close"), it shrinks to 4 car lengths at 5x; cars at 5x: Chicago 67, NY 67, Miami 66, LA 67, Frisco 43 at its default start (map edge) and 67 mid map. Open: peds fade (not needed for pop-ups, only for looks), the three new screens not seen in the game (no automatic test), shadow-without-car report of the user not reproduced, Miami water (see CLAUDE.md), commit/push.

### Part 9 — "All cities cars" option (user request 2026-10-11)
- Secrets row "All cities cars" Off/On: any car model of any city can spawn as traffic in every city, D2 and D1 (default Off = original per-city car sets).
- Investigate first: how `civ_ai.c` picks the model (`residentCarModels`, `MAX_RESIDENT_CARS`, `SetupResidentModels` in `mission.c`, `CarAvailability`/`carNumLookup`), how many car texture pages fit VRAM at once (D1 cities: 19 slots, 12-14 already used, see CLAUDE.md "Still open for cars"), and the per-city `civ_clut`/`carTpages` setup. Probably only a subset can be resident at a time: pick a random subset per level load (or rotate on respawn) and load the model + page lazily; do not exceed the slot budget.
- D1 player cars (model 8+) need their special pages (2 each): that is the same VRAM problem as "player cars" in Part 6; solve both together or limit this option to the cars whose pages fit.
- Done when: Chicago, Havana, Vegas, Rio, Miami, Frisco, LA, New York show a mix of foreign cars with correct textures/colours (no black cars, no wrong palette), cops stay the city cop car, 10 minutes without crash.

### Part 10 — "24 hour day/night cycle" option (user request 2026-10-11)
- Secrets row "24h day/night cycle" Off/On: time of day advances continuously (day -> dusk -> night -> dawn -> day) instead of the fixed day or night of the mission, in D2 cities and D1 cities.
- Investigate first: `gTimeOfDay` / `gWantNight` usage (`TIME_DAY`, `TIME_DUSK`, `TIME_NIGHT`, `TIME_DAWN`), sky colours and light colours per time (`sky.c`, `draw.c`, `lightcol`), the night LEV variants (`CityNN.LEV` via `SetCityType`: night uses a second LEV with different lighting/lamp textures; D1 cities have only one converted LEV), headlights/street lamps/building window lights (`CityType`, `lightsOn`), the dusk handling D2 already has for some missions.
- Rules: lamps, headlights, brake lights, windows and neon switch on and off smoothly at fixed times (not all at one frame); cars turn headlights on at dusk and off at dawn; no night LEV swap in the middle of a drive (a LEV swap would reload the level): the day LEV must be shaded to look like night (palette/ambient tint) or the cycle only runs on the level that is loaded. Decide this in the investigation and note it here before coding.
- Done when: a full cycle in Chicago and in Miami (accelerated clock for testing) shows correct sky, lighting and lights on/off without pop, and the setting Off leaves the original behaviour.

### Part 11 — Free camera controls option (user request 2026-10-11)
- Secrets row for the free camera controls described in `plans/gta-camera-controls-analysis.md` (right stick / mouse look around the car with smooth return behind the car). Reuse the camera orbit work of the other session (`plans/camera-orbit.md`, row "camera orbit", `reserved[5]` in `loadsave.c`): do NOT start before that session has committed its camera changes (`camera.c/.h`, `players.c`, `loadsave.c`, `dr2types.h` have uncommitted work of it). Then add the row/option on top, default Off.
- **Status 2026-10-12:** the camera session work is finished and verified with `-camtest` (see `plans/camera-orbit.md`): stick yaw and pitch, eased look back, return, menu row "Stick Camera" and `config.ini` keys. Open: menu and a real pad seen by the user.
- Done when: option Off = original camera in D2 and D1 cities; On = free look with smooth return, no clipping into buildings, works with keyboard+mouse and pad, saved in the profile.

### Order and rules for Parts 8 to 11
- Order: 8 (draw distance, density, fog, safe spawn), 9 (all cities cars), 10 (24 hour cycle), 11 (free camera, after the other session's camera commit). One commit per part, ask "commit & push?" after each green part.
- Every part: gated option (default = original behaviour), tested in a D2 city (`-StartX ""`) and a D1 city (Miami, Frisco, LA, New York), found problems and facts go into `CLAUDE.md` section "Driver 1 DLC findings".

---

## 5. Test install
- `E:\Games\Driver Series\REDRIVER2_D1` = copy of `E:\Games\Driver Series\REDRIVER2` (user's install, never write there). Contains `DRIVER1\LEVELS` + `DRIVER1\DATA` (player-flow test). Rename `DRIVER1` temporarily to test "no DLC" (mask 0x0).
