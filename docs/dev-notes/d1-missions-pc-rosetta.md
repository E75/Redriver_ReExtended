# Driver 1 missions: PC script Rosetta stone (2026-10-18)

Research note. The Driver 1 PC version (`Game.exe`, 1999) keeps every mission as a plain text script `Scripts\Missions\missionN.dms` (165 files, comments included, named commands). The PS1 `MISSION.BLK` holds the same missions as anonymous opcodes. Use the PC scripts to name and understand the PS1 opcodes. No PC data is stored in the repo.

## Mapping story missions

- PS1 story mission k (0..43) = PC `mission(k+1).dms` (checked by the draft header titles, e.g. PC 27 "Lucky to the Doc" = PS1 26, PC 44 "President Run" = PS1 43).
- PC 45..49 Survival / Take a Ride, 59..63 + 91..94 Trail Blazer, 66..69 + 87..90 Getaway, 80..86 Survival, 95..101 Pursuit: the PS1 scripts 44..123 (other modes).

## Coordinates: PC maps are NOT the PS1 maps

Target and start positions were compared per story mission (PS1 `TARGET0` / `START_POS` against PC `SetTarget` / `SetPlayerPosition`):

- Miami: PC positions are shifted by about (-9000, +9000) (bank job start 284813,-50302 on PC against 293839,-59343 on PS1). Targets differ too.
- Los Angeles: totally different (start -42993,54760 on PC against -21163,-130012 on PS1).
- San Francisco and New York: start positions equal in most missions, targets equal in about a third.
- The converted cities are built from the PS1 LEVs. Therefore: take behaviour, texts and rules from the PC scripts, but ALWAYS keep the PS1 positions. A PC position in a PS1 city can land off the road.

## Opcode meanings found through the PC scripts

| PS1 op | PC command | Evidence |
|---|---|---|
| 20 `TEST_POSITIONS [n, (rot,x,z)*n]` | `SpawnCops n, rot,x,z ...` | PC 17 `SpawnCops 3,3302,46483,-85610,538,...` against PS1 16 `[3, 697,46456,-85685,-711,...]`. Cop car spawn spots (ambush); safe to ignore, D2 cops spawn from the felony value. Blocks missions 16, 31, 38. |
| 39 `CFG_GP101C` | probably `CHOPPER` set-up | in PS1 19 and 43 whose PC scripts (20, 44) are the helicopter missions (`CHOPPER x,z,rot`, `CHOPPER_START`, `CHOPPER_FORCE`). Unverified for 43. |
| 102 `WAIT_SPAWN` | `CreateGangCar 8192` (+ `DisableTargetArrow/Pulse/Tracer`) | PS1 39 against PC 40 (Stop Granger's Gang). Plausible, not proven. |
| 65 `DAMAGE_LIMIT [2500]` | unknown | PS1 34 against PC 35 has no matching command (PC has `CopWipeOutDamage`, `DamageScale`). Open. |
| 103 `MARKER_PROPS`, 40, 63, 4 `TRIGGER3` | not matched yet | PS1 2, 3, 18, 30 against PC 3, 4, 19, 31. PC 4 (boat chase) has no D2 equivalent (speed boat). |

Other PC commands usable for exact rules: `Cop_Respawn n`, `Cops_SpeedScale` / `Cops_PowerScale`, `MustBeHidden` / `NeedToLoseTail` / `OuterRadius` (lose-the-tail rule), `ForceFelonyRating`, `Countdown`, `MaxSpeed`, `Night` / `Dusk` / `Dawn` / `Raining` / `Foggy` / `Lightning` / `Snowing` (time of day and weather per mission), `CreateGangCar`, `AddCivCar model,?,colour,?,rot,x,z`.

## Beta PS1 build (D:\driver march 9th 99, DRIVER.EXE 9 March 1999)

- No debug symbols. The 85 `missions\N\mN.cdm` files use an older record layout than `MISSION.BLK` (string variable records carry no length, other lengths differ); only 6 of 70 parse with the final tables. The interpreter must be read in Ghidra first. Strings in the EXE: `DMS_COM_INCREASE/DECREASE/ADD/SUB: DMS variable %d not yet working`.

## PC graphics findings (Game.exe, Ghidra, `D:\Driver-project\ghidra`)

- Renderer = DirectDraw/Direct3D 6 plus a Glide 2.x path. `SetRenderState` init at `0x421521`: Z buffer on, Gouraud, texture mag/min POINT, dither, no culling, 16-bit 256x256 pages. So the PC version has no better filtering than PS1; its richer look comes from data (`Levels\*.d3d`, `*.cos`, `*.den`) and script features (weather, night, lamps, lens flare, chopper, `REFLECT.BMP`).
- Redriver is the modern side: the wanted upgrades (fog, reflections, draw distance) are done in the Redriver renderer (PsyCross / OpenGL), using the PC data as reference for look and values, not by porting the DX6 code.
- Redriver already has what the user wants modernised (checked 2026-10-18 in `draw.c` / `cars.c` / `debris.c`): shader fog (`PsyX_SetFog`, distance haze `gDistanceFog`, exponential weather fog, height fog and sun glow `gAtmosphere`), draw distance modes (`gDrawDistanceMode`, `ViewDrawDistance`, ring based `DrawRadiusOfCells`), sky gloss on car bodies (`CAR_GLOSS_SKY`), wet road lamp reflections (`DisplayLightReflections`), headlight beam. The DX6 path (point sampling, fixed 256x256 pages, Glide fog table) is behind that, so there is nothing to port.
- What the PC data can still add: per-mission time of day and weather from the scripts (`Night`, `Dusk`, `Dawn`, `Raining`, `Foggy`, `Lightning`, `Snowing`, `NoLensFlare`), the `REFLECT.BMP` car environment map as a texture for the existing gloss, and lamp / lens flare placement (`LampPosition`, `LampColor`, `LampRange`). Tune fog and draw distance for the larger D1 cities (cell scale `MAP_CELL_SIZE / 2048`) instead.

## Next steps (not done)

1. Ignore op 20 with a note and re-run the converter to see which blockers remain (it only reports the first blocker per mission).
2. Decode 39, 65, 102 with a Ghidra pass over the PC script parser (command name table) instead of guessing.
3. Take weather / time of day per mission from the PC scripts into the D2MS header (`time`, `weather`).
