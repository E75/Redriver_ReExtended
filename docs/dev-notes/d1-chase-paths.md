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
- With `chaseInfiniteMass` the player cannot push the car off the route; it contradicts 1:1 and can be switched off.
- `config.ini [d1dlc] chasePaths=1` (default) uses the files, `0` = civilian AI only. Without a file nothing changes.

## Making the files

`src_rebuild/tools/d1conv/d1chase_pack.py <routes folder> <install>\DRIVER1\CONVERTED` packs one JSON per variant (`group`, `entry`, `frames` with x, y, z, yaw, `reliable_frames`) into the files above. The routes come from an offline run of the D1 car step on the replay input of `REPLAY.BLK` (see `d1-car-physics.md`); the run matches the real ones exactly up to the first contact with traffic, so only those frames (300 to 471 per variant) are stored.

## Not yet verified

The route start is a block ahead of the player start in every ram mission (7 to 10 thousand units), like the original. The look in game of the hand-over to the AI and the match with the real run beyond the stored frames are not checked.
