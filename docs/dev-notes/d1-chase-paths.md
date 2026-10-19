# Driver 1 chase routes (2026-10-18)

The chased car of a Driver 1 ram mission starts on its recorded route and the civilian AI takes over when the route ends. The route files are the player's own data (made offline from the player's own disc) and are never part of the repository.

## Which missions

Replay group k of the original (0..5) belongs to story script 6, 11, 24, 27, 35, 40. Scripts 11, 24, 27, 35 and 40 convert today as chase car targets; script 6 is still skipped by the converter. Every group has 15 variants, the original picks one at random at each start; so does the engine.

## File `DRIVER1\CONVERTED\M<1000 + script>.CHASE` (little endian)

- 4 bytes `D1CP`, int32 version (1), int32 number of variants (1..64).
- Per variant: int32 frame count, then per frame int32 x, int32 z, int16 y, int16 yaw (4096 = full turn). One frame is one game step (30 per second).
- Only the part of the route that is trustworthy is stored (see below).

## Engine (`Game/C/d1chase.c`)

- `D1ChasePath_Load` (called when the chase car is created, `mission.c`) reads the file and picks a variant; `D1ChasePath_Start` gives the start spot and heading, the car is placed with `CreateDrivingCivCarNear` there.
- Every game step `D1ChasePath_Step` puts the car on the next recorded frame (position, heading, body pitch like `TempBuildHandlingMatrix`, linear velocity and wheel speed from the step to the next frame). When the route is over the civilian AI drives on from that pose and speed.
- Each step first compares where the physics left the car with the recorded frame; more than 40 units off means a contact (traffic, building, the player) and the route is dropped for good, the civilian AI drives on from the pushed pose.
- Pad stream: `M<n>.PADS` (made by `d1pads_pack.py`, same variants as the route) holds the recorded input per step. When present, the chase car stays `CONTROL_TYPE_CIV_AI` but `D1ChasePath_ProcessPad` (called from the car loop in `main.c` instead of `CivControl`) feeds the pad to `ProcessCarPad` (bit 0 of the word = analogue frame, thrust 4779 on throttle), so the normal car physics drive it. Until the first contact the position is also snapped to the recorded frame (the measured drift is a few units over 90 steps); after the contact only the pads and the collisions act, as in the original. After the last pad frame the car holds the handbrake.
- Recovery: when the car stands after a crash (less than 200 units in 2 seconds) or the pads are over, it reverses and drives at the nearest traffic lane (`CivFindLaneNear`, `civ_ai.c`), then the civilian AI takes it again. If the civilian AI then stands 3 seconds without progress (a jam), the car backs off and tries again, until the mission ends.
- The damage bar follows the chased car's damage total (`mission.c`), because the civilian AI does not fill it.
- Damage: a hit on the chased car raises its damage total only when the other car is the player's (`D1ChaseCar_DamageCounts`, `bcollide.c`), so traffic and buildings cannot complete a ram mission; the denting stays visible.
- `chaseInfiniteMass=1` makes the car unpushable; default is 0 because that contradicts 1:1.
- `config.ini [d1dlc] chasePaths=1` (default) uses the files, `0` = civilian AI only. Without a file nothing changes.

## Chased car behaviour (rule, 2026-10-19)

The chased car must make a real chase: it never stops before its damage bar is full, and it always finds a way away from the player.

- Keeps driving: the stall monitor and recovery run with and without a pad stream. The recovery starts after 30 steps without progress, after 15 steps off the road surface (`GetSurfaceIndex`, pavement, grass, trees) and at once when the civilian AI stopped for good (thrust state stop). Once the damage bar is full (`D1ChaseCar_Wrecked`, `mission.c`) the car stops for good.
- Back to the road: the recovery heads for the nearest lane (`CivFindLaneNear`, any lane spot, no spawn rules) or, when none is near or it is much farther, for the last spot where the car drove on the road. It aims a block ahead along that lane so it merges in, drives slowly while it points more than 45 degrees off the lane, and hands back to the civilian AI only on the road surface, near the lane, lined up with it and with the way ahead free; the first 1.5 s after that are capped at town speed.
- Eight body sensors (`SenseRing`, corners, front, back, sides; building, object or car): the recovery backs up (full opposite lock, off a blocked rear corner) only when the front is shut, until the nose points at the road with room ahead; driving on it steers towards the free front corner on the road side.
- Off the road surface is measured on the surface, not with the lane finder: the spawn lane search skips short roads, road ends and spots near cars, so on a road it often finds nothing near and the car would go into recovery on a free road.
- Bends: on a sharp bend at speed the car pulls the handbrake for a few steps (`D1ChaseCar_Avoid`), so it slides round instead of braking hard.
- Damage: one ram of the player fills at most a sixth of the bar (`D1ChaseCar_LimitHit`, `bcollide.c`; contacts within a second are one ram), so it takes at least 6 rams, often 7 or 8.
- Two old traps: a civilian AI car in thrust state stop has all wheels locked (`wheelforces.c`), so the recovery sets it to accelerate while it drives the car; a very hard hit marks car slot 1 `carMustDie` (stops for good), which a chased car ignores.
- Speed: top speed 140 % of the highest road speed limit (`D1_CHASE_TOP_PCT`, `mission.c`), throttle 4779 like the recorded runs (`CIV_CHASE_ACCEL`), braking before bends from the route ahead (`CivCornerSpeed`).
- Traffic (`smartTraffic=1`): runs red lights and gives no way, overtakes any slower car with a shorter gap check, slips past a slow car close ahead at once (`CivChaseSlipPast`, over the pavement if that side is free), keeps any lane, uses alleys (roads without traffic lanes) when the player is not near.
- Player: with the player's car within 4000 units it weaves into a free side lane when the player is right behind, prefers turning off at a junction (3x weight) and never takes an exit towards the player (`CivChaserNear`, `CivExitWeight`).
- Test: `REDRIVER2_CHASEKNOCK=1` pushes the car off the road beside it every 15 s, facing away, and logs how many steps it takes to drive on the road again (3 of 3 back, about 8 s). Damage per ram was checked with the player's car placed right behind the chased car at twice its speed every 5 s.

## Making the files

`src_rebuild/tools/d1conv/d1chase_pack.py <routes folder> <install>\DRIVER1\CONVERTED` packs one JSON per variant (`group`, `entry`, `frames` with x, y, z, yaw, `reliable_frames`) into the files above. The routes come from an offline run of the D1 car step on the replay input of `REPLAY.BLK` (see `d1-car-physics.md`); the run matches the real ones exactly up to the first contact with traffic, so only those frames (300 to 471 per variant) are stored.

## Not yet verified

The route start is a block ahead of the player start in every ram mission (7 to 10 thousand units), like the original. The car model comes from the mission target (the script sets it per mission; scripts 6 and 40 use a different model than the one assumed by the routes, so their routes drift earlier). The look in game of the hand-over and the match with the real run beyond the stored frames are not checked.
