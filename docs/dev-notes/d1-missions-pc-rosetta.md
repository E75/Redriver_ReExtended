# Driver 1 missions: PC script Rosetta stone (2026-10-18)

Research note. The Driver 1 PC version (`Game.exe`, 1999) keeps every mission as a plain text script `Scripts\Missions\missionN.dms` (165 files, comments included, named commands). The PS1 `MISSION.BLK` holds the same missions as anonymous opcodes. Use the PC scripts to name and understand the PS1 opcodes. No PC data is stored in the repo.

## Mapping story missions

- PS1 story mission k (0..43) = PC `mission(k+1).dms` (checked by the draft header titles, e.g. PC 27 "Lucky to the Doc" = PS1 26, PC 44 "President Run" = PS1 43).
- PC 45..49 Survival / Take a Ride, 59..63 + 91..94 Trail Blazer, 66..69 + 87..90 Getaway, 80..86 Survival, 95..101 Pursuit: the PS1 scripts 44..123 (other modes).

## Coordinates: PC maps are NOT the PS1 maps

Target and start positions were compared per story mission (PS1 `TARGET0` / `START_POS` against PC `SetTarget` / `SetPlayerPosition`):

- Miami: PC positions are shifted by about (-9000, +9000) (already visible in the first mission). Targets differ too.
- Los Angeles: totally different (start positions are far apart).
- San Francisco and New York: start positions equal in most missions, targets equal in about a third.
- The converted cities are built from the PS1 LEVs. Therefore: take behaviour, texts and rules from the PC scripts, but ALWAYS keep the PS1 positions. A PC position in a PS1 city can land off the road.

## Opcode meanings found through the PC scripts

| PS1 op | PC command | Evidence |
|---|---|---|
| 20 `TEST_POSITIONS [n, (rot,x,z)*n]` | `SpawnCops n, rot,x,z ...` | PC `SpawnCops n, rot,x,z ...` has the same count and triplet layout as the PS1 record of mission 16, with nearly equal positions. Cop car spawn spots (ambush); safe to ignore, D2 cops spawn from the felony value. Blocks missions 16, 31, 38. |
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
3. ~~Take weather / time of day per mission from the PC scripts into the D2MS header~~ done: `d1mission_to_d2ms.py --pc-scripts <Scripts\Missions>` sets `time` (Night > Dusk > Dawn, else day) and `weather` (Raining) from PC `mission(k+1).dms`; Snowing, Foggy and Lightning have no header field and are only noted.

### Corrections and fog facts (2026-10-18, Game.exe)

- `DATA\REFLECT.BMP` is NOT a car environment map: it is a 640x480 splash image (Reflections logo) loaded by the start-up screen function at `0x4127f0` next to `COPYRIGHT.BMP` and `GT.BMP`. The PC `Shiny Cars` option (Readme: environment mapping nearly doubles the car polygon count) is a geometry pass, not a texture file. The existing sky gloss on car bodies is the equivalent; no env-map texture to import.
- Fog exists only on the Glide path (Readme: visible in some undercover missions, option `Fog`). The table is built at `0x42626e`: exponential fog (`guFogGenerateExp`) with density 1.2e-4, or 1.0e-4 once a script flag (`DAT_00b87e88`) was raised; fog colour is a run-time value, black on the first call. Script `Foggy` is commented out in all but a few missions. Redriver's fog scales with draw distance and D1 cell size already; there are no PC values worth copying.
- Script keywords live in a table of 16 byte headers (flag, id, argument bytes) in front of 0x110 byte name slots (`NIGHT`, `RAINING`, `LIGHTNING`, `FOGGY`, `NOLENSFLARE`, `LAMPPOSITION/COLOR/TYPE/RANGE`, `SNOWING`, `DUSK`, `DAWN`); the ids in that table do not equal the switch index of the script compiler at `0x4f6b..`, so the keyword to handler mapping is not resolved.
- `Lamp*` commands occur in one mission (4 lamps, type 2, range 3000, red/blue = police light placement); `NoLensFlare` in 29 scripts (day missions without sun glare).

## Chase replay format (REPLAY.BLK, retail US exe, 2026-10-18)

Descriptions only; the analysis files live outside the repo.

- Loader: function at `0x8005c318` (mission number -> group through the byte table at `0x8008b6d8`, entry = group * 20 + variant, variant random 0..14 and never twice in a row; state 8 always variant 0). It reads one 5220 byte entry into the replay buffer that starts at RAM `0x1fc32c`; the buffer is split by the stream set-up at `0x80058814`.
- Entry layout (offsets inside the 5220 bytes): `0..3000` pad stream (1000 records of 3 bytes: byte 0 high pad byte, byte 1 low pad byte, byte 2 repeat count; word = (b0 << 8 | b1) AND `0xa0fc`; low nibble of b0 selects the steering value from the byte table at `0x8008b6c8`), `3000..4600` traffic ping block, `4600..4620` start state (int frame count, int x, y, z, short rotation, short flags), `4620..5220` more tables. Entries of the same group share the start state, only the pad stream and frame count differ.
- The chased car is car index 23 (record stride `0x18c`, struct base `0x9b0a4`). Per frame the game fetches the next pad word (`0x8005bf2c`), applies it with `0x8001ff24` (control) and steps the car with `0x8001d608` (physics); car 23 is created at the start state by `0x8001af2c`.
- Consequence: start pose and inputs are fully known. The route itself needs the D1 car physics. Plan: either capture the route from a real PS1 run in the emulator (every frame position of car 23) or port `0x8001d608`; the emulator capture is exact.
