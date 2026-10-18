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

## Making the files

`src_rebuild/tools/d1conv/d1chase_pack.py <routes folder> <install>\DRIVER1\CONVERTED` packs one JSON per variant (`group`, `entry`, `frames` with x, y, z, yaw, `reliable_frames`) into the files above. The routes come from an offline run of the D1 car step on the replay input of `REPLAY.BLK` (see `d1-car-physics.md`); the run matches the real ones exactly up to the first contact with traffic, so only those frames (300 to 471 per variant) are stored.

## Not yet verified

The route start is a block ahead of the player start in every ram mission (7 to 10 thousand units), like the original. The car model comes from the mission target (the script sets it per mission; scripts 6 and 40 use a different model than the one assumed by the routes, so their routes drift earlier). The look in game of the hand-over and the match with the real run beyond the stored frames are not checked.
