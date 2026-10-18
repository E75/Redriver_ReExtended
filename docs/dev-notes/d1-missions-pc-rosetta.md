# Driver 1 missions: PC mission scripts as a reference (2026-10-18)

Research note. The PC release of the DLC game keeps every mission as a plain text script `Scripts\Missions\missionN.dms` (165 files, comments included, named commands). The console `MISSION.BLK` holds the same missions as anonymous opcodes. The PC scripts help to name and understand those opcodes. No PC data is stored in the repo.

## Mapping story missions

- Console story mission k (0..43) = PC `mission(k+1).dms` (checked by the draft header titles, e.g. PC 27 "Lucky to the Doc" = 26, PC 44 "President Run" = 43).
- PC 45..49 Survival / Take a Ride, 59..63 + 91..94 Trail Blazer, 66..69 + 87..90 Getaway, 80..86 Survival, 95..101 Pursuit: the console scripts 44..123 (other modes).

## Coordinates: the PC maps are NOT the converted maps

Target and start positions were compared per story mission (`TARGET0` / `START_POS` against PC `SetTarget` / `SetPlayerPosition`):

- Miami: PC positions are shifted by about (-9000, +9000), visible in the first mission already. Targets differ too.
- Los Angeles: totally different (start positions are far apart).
- San Francisco and New York: start positions equal in most missions, targets equal in about a third.
- The converted cities are built from the console level files. Therefore: take behaviour, texts and rules from the PC scripts, but ALWAYS keep the converted positions. A PC position in a converted city can land off the road.

## Opcode meanings found through the PC scripts

| Op | PC command | Evidence |
|---|---|---|
| 20 `TEST_POSITIONS [n, (rot,x,z)*n]` | `SpawnCops n, rot,x,z ...` | Same count and triplet layout as the record of mission 16, with nearly equal positions. Cop car spawn spots (ambush); safe to ignore, D2 cops spawn from the felony value. Blocks missions 16, 31, 38. |
| 39 `CFG_GP101C` | probably `CHOPPER` set-up | In missions 19 and 43 whose PC scripts (20, 44) are the helicopter missions (`CHOPPER x,z,rot`, `CHOPPER_START`, `CHOPPER_FORCE`). Unverified for 43. |
| 102 `WAIT_SPAWN` | `CreateGangCar 8192` (+ `DisableTargetArrow/Pulse/Tracer`) | Mission 39 against PC 40 (Stop Granger's Gang). Plausible, not proven. |
| 65 `DAMAGE_LIMIT [2500]` | unknown | Mission 34 against PC 35 has no matching command (PC has `CopWipeOutDamage`, `DamageScale`). Open. |
| 103 `MARKER_PROPS`, 40, 63, 4 `TRIGGER3` | not matched yet | Missions 2, 3, 18, 30 against PC 3, 4, 19, 31. PC 4 (boat chase) has no D2 equivalent (speed boat). |

Other PC commands usable for exact rules: `Cop_Respawn n`, `Cops_SpeedScale` / `Cops_PowerScale`, `MustBeHidden` / `NeedToLoseTail` / `OuterRadius` (lose-the-tail rule), `ForceFelonyRating`, `Countdown`, `MaxSpeed`, `Night` / `Dusk` / `Dawn` / `Raining` / `Foggy` / `Lightning` / `Snowing` (time of day and weather per mission), `CreateGangCar`, `AddCivCar model,?,colour,?,rot,x,z`.

Time of day and weather: `d1mission_to_d2ms.py --pc-scripts <Scripts\Missions>` sets the D2MS header `time` (Night > Dusk > Dawn, else day) and `weather` (Raining) from PC `mission(k+1).dms`. Snowing, Foggy and Lightning have no header field and are only noted.

## PC graphics findings

- The PC renderer is a plain fixed function pipeline (Z buffer, Gouraud, point sampled textures, 16 bit 256x256 pages, a second path with table fog). It has no better filtering than the console version; its richer look comes from data (`Levels\*.d3d`, `*.cos`, `*.den`) and script features (weather, night, lamps, lens flare, chopper).
- Redriver already covers the wanted upgrades in its own renderer (PsyCross / OpenGL): shader fog (`PsyX_SetFog`, distance haze `gDistanceFog`, exponential weather fog, height fog and sun glow `gAtmosphere`), draw distance modes (`gDrawDistanceMode`, `ViewDrawDistance`, ring based `DrawRadiusOfCells`), sky gloss on car bodies, wet road lamp reflections (`DisplayLightReflections`), headlight beam. Nothing of the PC renderer needs porting; tune fog and draw distance for the larger D1 cities (cell scale `MAP_CELL_SIZE / 2048`).
- `DATA\REFLECT.BMP` is not a car environment map but a 640x480 start-up splash image. The PC option `Shiny Cars` (environment mapping nearly doubles the car polygon count) is an extra geometry pass; the car gloss pass of Redriver is the equivalent.
- Fog exists only on the second render path (option `Fog`, visible in a few missions): exponential fog with a density of about 1.2e-4 (1.0e-4 after a script flag), run-time colour. The `Foggy` command is commented out in nearly all missions.
- `Lamp*` commands occur in one mission (4 lamps, type 2, range 3000, red/blue = police light placement); `NoLensFlare` in 29 scripts (day missions without sun glare).

## Chase replay data (`REPLAY.BLK`)

Description of the format only.

- Mission number -> group through a byte table, entry = group * 20 + variant, variant random 0..14 and never twice in a row; one mission state always uses variant 0. One entry is 5220 bytes.
- Entry layout (offsets inside the 5220 bytes): `0..3000` pad stream (1000 records of 3 bytes: byte 0 high pad byte, byte 1 low pad byte, byte 2 repeat count; word = (b0 << 8 | b1) AND `0xa0fc`; low nibble of b0 selects the steering value from a byte table), `3000..4600` traffic ping block, `4600..4620` start state (int frame count, int x, y, z, short rotation, short flags), `4620..5220` more tables. Entries of the same group share the start state, only the pad stream and frame count differ.
- The chased car is car index 23. Per frame the next pad word is applied to its controls and the car is stepped by the normal car physics, so start pose and inputs are fully known; the route needs the D1 car physics.
- Observation from a recorded run: a plain per frame read of the chased car gives a smooth track (speed 70 to 260 units per frame) when the player follows. With an idle player the leader stops after a few hundred frames, so the leader reacts to the player distance and a pure pad stream replay is not enough to reproduce its speed.
