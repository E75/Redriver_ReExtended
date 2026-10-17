# 24 hour day/night cycle (extends plans/driver1-cities.md Part 10)

Status: plan only, nothing implemented. Written 2026-10-13 from a read-only research pass over the working tree. Line numbers drift: re-grep before editing.

## 1. Findings
- **Discrete time only.** `gTimeOfDay` is DAWN 0, DAY 1, DUSK 2, NIGHT 3 (dr2types.h:1364). Set at level load in mission.c:584-602 from `MissionHeader->time`, overridden by `wantedTimeOfDay` (FEmain.c:4240-4300, pause.c:220-250), LA forced to NIGHT at mission.c:596. `gNight` is derived there too.
- **Dusk and dawn are already smooth in the original**, driven by `DawnCount` (main.c:870). `NightAmbient` and `gLightsOn` are set in main.c:1336-1388, `combointensity` at main.c:1398, `calc_sky_brightness` at sky.c:1110. Dusk and dawn use the same sky image with a darkening vertex colour. Day/night is otherwise a hard switch.
- **The level file is chosen at load.** `LoadGameLevel` (main.c:394-407) calls `SetCityType(DAY or NIGHT)`, so night loads `CityNN.LEV`. D1 cities always use one converted LEV (system.c:896-902). The debug menu already switches `gTimeOfDay` and calls `LoadSky()` mid-drive (pause.c:220-250); LA already renders night on the day LEV. So a night look on a day LEV is proven; no mid-drive reload is needed.
- **Sky.** `LoadSky` (sky.c ~507-633) loads one 64 KB variant from `DATA\SKY%d.RAW` at offset day 0, night 0x10000, overcast 0x20000, dawn 0x30000, dusk 0x40000, then calls `SkyHdBuild` (sky.c:329). HD sky builds 2 RGBA textures per sky (`skyHdTexture[2]`, 1024x1024, one per half of the 512 px image). `DrawSkyDome` and `PlotSkyPoly` (sky.c:1174-1233, 1316) bind them per quad with `DR_PSYX_TEX`. Only one variant is alive at a time. `DisplaySun` (sky.c ~840-1031) returns early at dawn, dusk and overcast; moon at night (`col.r` 128), sun by day (254).
- **Lighting is switched on `gTimeOfDay == TIME_NIGHT`.**
  - `day_vectors/night_vectors/day_colours/night_colours` (draw.c:65-95) used in `DrawSprites` (draw.c:305-323) and `ComputeCarLightingLevels` (cars.c:494-503). `setupLightingMatrices` back colour 48 at night, 140 otherwise (cars.c:458-465).
  - `SetupPlaneColours` (draw.c:459-501): day branch (per-face multipliers 120/103/13/3 over 128) versus flat ambient plus offsets at dawn, dusk, night.
  - `DrawTILES` (tile.c:236-250) divides by 3 at night. Wheels do the same (cars.c:620-635).
  - Cars cache their shading: `doLight` (cars.c:521-525) recomputes only on turning, or 1 in 16 frames at dawn and dusk.
  - Tree shadows skipped at night (draw.c:438). Fog colour picked in `UpdateSnow` (draw.c:1467-1474). Peds and Tanner use brightness 64 or 12 at night (motion_c.c:787, 1664, 1712, 1767, 2043).
- **Sim-visible readers of the discrete value:** felony.c:394, event.c:3284, targets.c:350, gamesnd.c:334/626 (env sounds chosen at load), overmap.c:580/1112/1414, debris.c:738/1265/2628/2745, denting.c:388, models.c:67 (`litSprites` decided at model load), objanim.c:195 (`InitCyclingPals` only at night). `gNight` is read in about 20 places.
- **Lights.** `gLightsOn` is binary: cars.c:1532/1754, objanim.c:505-694 (street lamps via `AddLightEffect`, debris.c:1573), debris.c:2231, draw.c:1525. Lamp glow (`ShowLight`/`ShowLight1` debris.c ~1558, `AddLightEffect`, `AddTrafficLight` ~1915) draws additive sprites, so scaling `col` is an exact fade. `ShowCarlight` (debris.c:2014) scales `flareCol` by `CarLightFade`. `lightsOnDelay[i] = i*11` (main.c:721) staggers cars at dusk. The headlight beam `UpdateHeadlights` (draw.c:1514-1560) already eases `beam` by 1/30 per frame.
- **D1 cities.** `Level_AnimatingObjectPtrs`/`Level_NumAnimatedObjects` and `Lev_CycleObjPtrs`/`Num_LevCycleObjs` (objanim.c:76-136) are zero for D1: no street-lamp glow, animated traffic lights or neon cycling today. A D1 night is sky, tint and car lights only, unless lamp positions are added.
- **Save and config.** `CONFIG_SAVE_HEADER.reserved[6]` (loadsave.c:52) is full (slots 0-5 used; sky agent took 0). Pack the cycle mode into the free upper bits of `reserved[1]` (`gAllCityCars` 0/1) as `mode << 4`; old profiles read 0 = Off. Menu: Extended Settings list (`ExtendedOptionsScreen`), `BuildOptionButtons(n, names, FEVAR_x, current)` like `SkyScreen`. Use the next free `FEVAR` and `PsxScreens` index at implementation time.
- **Replays:** `ReplayParameterPtr->timeofday` is stored (ai.c:16) and never re-read.
- **Not verified:** no game data on the research machine, so day and night LEVs were not diffed (decides the window strategy, Part 1).

## 2. Design
- **Clock.** New `daycycle.c/.h` in `src_rebuild/Game/C` (rerun premake; CRLF, no BOM). `gCycleMode` 0 Off, 1 = 24 real min per day, 2 = 12, 3 = 48. `gCycleClock` = game minutes in 1/256 units, wraps at 24*60*256. Advances once per `StepGame` (30 Hz) only when `!pauseflag && NoPlayerControl==0 && gInGameCutsceneActive==0`; step = `1440*256 / (realMinutes*60*30)`. Start hour from the chosen or mission time: dawn 6.0, day 12.0, dusk 19.0, night 23.0; `-cyclestart <hour>` overrides.
- **Active only when** option on, `GameType == GAME_TAKEADRIVE` (recommended; story missions keep scripted time), not replay/cutscene, not LA (forced night). Otherwise `gCycleOn = 0` and every existing path runs untouched.
- **Continuous `gCycleSun`** (0..4096, 4096 = full day), smoothstep: rises 5.0-7.5 h, falls 17.5-20.0 h.
- **Derived discrete `gTimeOfDay`** kept for sim readers. Hysteresis +-0.1 h on sun: NIGHT below 0.15, DAWN rising 0.15-0.85, DAY above 0.85, DUSK falling 0.85-0.15. `gNight = (sun < 0.35)`.
- **Helpers:** `CycleLerp(day, night) = (day*sun + night*(4096-sun)) >> 12`, `CycleLampLevel(hash, rising)`.
- **Config:** `config.ini [game] dayNightCycle=0..3`, `dayNightMinutes=<n>`; profile `reserved[1]` bits 4..7; menu row "Day Night Cycle" Off / 24 min / 12 min / 48 min. Test options `-cycle <realMinutes>` (accepts 1) and `-cyclestart <hour>`.

## 3. Per subsystem
1. **Sky.** Contract with the sky work: keep HD textures per variant `skyHdTexture[variant][half]` (0 day, 1 night, 2 overcast, 3 dawn, 4 dusk); split `LoadSky` into `SkyLoadVariant(skyNum, offset, slot)`; add `SkySetBlend(a, b, weight4096)`. Draw the dome twice in `PlotHorizonMDL`: pass A opaque colour `skycolor*(1-w)` texture A; pass B additive (`setSemiTrans`, `tpage|0x20`) colour `skycolor*w` texture B. Result `A*(1-w)+B*w`, no shader change. Keep the `DR_PSYX_TEX` order rule (sky.c:1223-1230) for both passes. Pairs by hour (smoothstep): night-dawn 4.5-6.0, dawn-day 6.0-8.0, day-dusk 17.0-18.5, dusk-night 18.5-20.0. Overcast/rain: replace day/dawn/dusk by overcast. Cycle on forces HD (indexed 4 bit cannot blend). Build all four variants at level load (~32 MB GPU, measure load time). Sun `col = 254*smoothstep(sun,.6,.9)`, moon `128*(1-smoothstep(sun,.1,.35))`, no early return at dawn/dusk when cycling; positions fixed in v1.
2. **Ambient and light vectors.** `NightAmbient = CycleLerp(day 128 (78 rain), night 40)`, warm tint around sun 0.3-0.7 (r+8, b-8), blue at night (r*0.85, b*1.15). `combointensity` from the RGB; replaces the `DawnCount` branches (main.c:1336-1398) when `gCycleOn`. `SetupPlaneColours`: compute day set and flat-plus-offset set, mix each of the 8 entries by sun, call every frame (draw.c:1988). `DrawTILES`, wheels, `DrawSprites`: replace `/3` and `(x*2*NightAmbient)>>8` with `CycleLerp(full, full/3)`. Lerp `day_vectors` toward `night_vectors` and renormalise to ~4086; lerp `day_colours` toward `night_colours` (3200 to 880); back colour `CycleLerp(140, 48)`. Cars: in cycle mode set `doLight` for `(cp->id & 15) == (CameraCnt & 15)` (cars.c:524). Tree shadows: scale `0x2E` by sun (draw.c:438). Peds/Tanner: lerp brightness. `litSprites` (models.c:67): apply night rule at load for every model in cycle mode, lerp colour in `DrawSprites`. Fog colour (draw.c:1467): lerp day (.66 .69 .73), dusk/dawn (.58 .50 .48), night (.06 .07 .10).
3. **Street lamps, traffic lights, car lights.** Replace binary `gLightsOn` checks in objanim.c with per-lamp `level = clamp((tOn - sun)*4096/0.06)`, `tOn = 0.62 - 0.30*h`, `h = hash(cop->pos.vx, cop->pos.vz)` (never pointers). Morning off uses a second hash, `tOff = 0.30 + 0.25*h2`, selected by `gCycleRising` (flips only at noon/midnight). Scale `col` of `AddLightEffect`, the lamp call at debris.c ~1558 and `ShowGroundLight` by level, skip at 0. `gLightsOn` stays as legacy boolean. Traffic lights stay on, colour `0.6 + 0.4*(1-sun)`. Cars: `ShowCarlight` multiplies `flareCol` by per-car level, `PlacePoolForCar` too. Per car `tOn = 0.55 - 0.15*hash(cp->id)`, ramp 0.03. `lightsOnDelay` unused in cycle mode. Beam: `wanted` in `UpdateHeadlights` = player car level (fog/rain keep `max(level,1)`). Cars without lamps (cars.c:1754) evaluate `level > 0`.
4. **Windows and neon** (depends on Part 1). Day/night LEV pages share bitmaps and differ in CLUTs: load day LEV, crossfade CLUT entries of window/neon textures per frame (night CLUTs read from night LEV at load, `LoadImage` one row per changed CLUT, 30-60 rows/frame budget). Neon cycling (objanim.c:215-304) works on the night copy before the blend (`InitCyclingPals` for any cycle start). If bitmaps differ: second VRAM copy of night pages plus additive quad pass, or no window glow in D2. D1: no night art, no window glow.
5. **D1.** LA stays night (`gCycleOn = 0`). Other D1 cities run the same code. Optional later part: lamp lights for D1.
6. **Edge cases.** Pause stops the clock; `DebugTimeOfDay*` sets the clock to the matching hour. Rain ambient 78 stays the day value. Replays/cutscenes: `gCycleOn = 0`. Lightning (debris.c:3915) doubles `NightAmbient` after the cycle, fine. Level restart resets the clock.

## 4. Risks
- D2 with the option off must stay bit-identical: guard every change with `gCycleOn`; after each part `-shot` in Chicago, Havana, Vegas, Rio vs a baseline.
- Sky blend collides with the sky work in `sky.c`: finish and commit that first.
- Per-frame `SetupPlaneColours` and CLUT uploads: measure build ms against the 33 ms budget.
- Car shading caches can step at very fast cycles (`-cycle 1` is test only).
- 32 MB HD sky textures and build time of four variants at load.
- Replays/saves never store the clock (only the `reserved[1]` bits).

## 5. Parts (riskiest first, each ends on a green build)
1. **Spike, no engine change.** Python script compares day and night LEV texture lumps and CLUTs of Chicago, Havana, Vegas, Rio: "same bitmaps, different CLUTs" or "bitmaps differ"; also whether window glow lives in textures or polygon types. Decides 3.4.
2. **Clock, config, menu, test options, derived `gTimeOfDay`, state log.** `-cycle 1 -cyclestart 5` logs `cycle: hour 6.2 sun 0.41 amb 77 sky 3/0 w 0.4 lamps 0.0 cars 0.0` each second. Off by default.
3. **Sky blend.** Hours 4,5,6,7,8,17,18,19,20 (`-cyclestart` + `-shot`) in Chicago and Miami; no band or pop between consecutive hours.
4. **Lighting blend** (ambient, planes, tiles, sprites, cars, peds, fog, shadows). Log max per-frame delta of `combointensity` and sky weight over an accelerated day; `plans/24h-cycle-check.py` must print ALL PASS (ambient step <= 1/frame at `-cycle 1`, sky weight <= 0.02/frame).
5. **Lamps, traffic lights, car lights, beam, sun, moon.** Log lamp counts at level 0 / between / 1 while driving a street; screenshots at 18.5, 19, 19.5, 20.5 of one street; two cars switch headlights at different times.
6. **Windows and neon** (per Part 1). Screenshots at 18, 19, 20, 21 h of a downtown street.
7. **Remaining readers and edge cases:** smoke, debris, overmap, env sounds, LA, rain, pause, replay/cutscene guards, debug menu.
8. **Full verification.** Accelerated day in all D2 cities plus Miami, Frisco, New York; LA night check; Off-mode comparison with baseline. Update CLAUDE.md and plans/driver1-cities.md Part 10.

## 6. Open questions for the user
1. Cycle in Take a Ride only (recommended), or also story missions/chases?
2. Default speed 24 real minutes per day, presets 12 / 24 / 48. OK?
3. LA stays night with the cycle on. OK?
4. If D2 night windows cannot be blended: no window glow mid-cycle acceptable, or build the second VRAM copy with an extra pass?
5. D1 cities (except LA) have no lamp or traffic light glow in the engine. Add street lamp glow from model names in the converter later?
6. Sun and moon move across the sky, or fixed (v1 fixed)?
7. Replays: show the recorded time of day only, or store the start hour too?
