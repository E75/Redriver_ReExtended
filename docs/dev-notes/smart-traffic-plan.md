# Smarter traffic and pedestrians - plan (2026-10-18)

Goal: civilian cars and pedestrians in Take a Ride (Driver 1 and Driver 2 cities) and in Driver 1 missions choose roads sensibly, drive faster on fast roads, overtake, merge, get themselves out of jams (back up, go round a blocker) and peds behave more alive. Branch `feature/smart-traffic`, one green commit per part. Each part can be resumed in a fresh session from this file alone.

## Pinned contract (all parts)

- PC only: every new line sits under `#ifndef PSX`. The PSX build keeps the original code path.
- Switch: `int gSmartTraffic = 1;` in `Game/C/civ_ai.c`, `extern` in `civ_ai.h`, read in `redriver2_psxpc.cpp` with `ini_sget(config, "ai", "smartTraffic", "%d", &gSmartTraffic)`; `data/config.ini` gets an `[ai]` section with `smartTraffic=1  # 1 = smarter traffic and peds (0 = behaviour before this work)`.
- Every new behaviour is gated by `SmartTraffic()` in `civ_ai.c` = `gSmartTraffic && !OldFollow()` (the env test switch `REDRIVER2_OLDFOLLOW` still gives the original code).
- Civ AI code is shared by Driver 1 and Driver 2 cities (`d1dlc/d1_roads.cpp` builds normal straights/curves/junctions). Lengths scale with `MAP_CELL_SIZE` where a constant is in world units of the road (see `WorldSpeedLimit`, `civ_ai.c`).
- Units: one step = 1/30 s; speed = `FIXEDH(cp->hd.wheel_speed)` units per step; wheel angle + = right; car axes: forward `(m[0][2], m[2][2])`, right `(m[0][0], m[2][0])`, 4096 = 1.
- Per-car state lives in `static` arrays `[MAX_CARS]` in `civ_ai.c`, reset in `InitCivState` next to `civStuckSteps` (`civ_ai.c` ~204).
- The D1 chase car (`d1chase.c`, `IsChaseCar`) keeps its own avoid/recover code; new civ behaviour must not fight it: skip cars with `cp->inform != NULL` like `CivJamBreaker` does.
- Never despawn or ghost a car the player can see (`PositionInViewCone`).

## Part 1 - jam ladder (status: done 2026-10-19)

Result (`REDRIVER2_JAMTEST=1`, Take a Ride, 80 s, player standing; `standing steps` = steps of civ cars in view standing still without a red light, lower is better; runs are deterministic per setting): Chicago crowd 2 / 3 / 5: 4183 -> 1172, 1630 -> 861, 2748 -> 1740; Vegas 15989 -> 18502 (32 go-rounds without gain, open); Frisco 8130 -> 8784. `near contacts` rises because a car creeping past the blocker counts.

Finding: `CIV_AI_CTRL_WAIT_TRAFFIC_LIGHT` is set as soon as the route reaches a junction with lights, so most cars in a city with many lights drive in that state; the old jam breaker skipped all of them. `CivWaitsForLight` (stop node pending and light not green) is the real wait test, used when `smartTraffic=1`.

Extends `CivJamBreaker` / `CivControl` / `CivSteerAngle` / `SetUpCivCollFlags` in `civ_ai.c`.

- `SetUpCivCollFlags` also stores `brakeLeadCar[id]` = index of the car that sets `brakeLength` (-1 for Tanner/none).
- Blocker test `CivBlockerStationary(cp)`: the lead car is slower than 3 units/step and is not itself queuing (its own `brakeLength` is 0 or it is not a civ car) and not waiting at a light/yield. A queue behind a red light never counts as a blocker.
- Rung 1 (2 s stuck behind a stationary blocker): go round it. Pick the side whose two probe points (beside the blocker and one car length past it) are free of cars (`CivPointFree`) and buildings (`CellAtPositionEmpty`). Set `civSwerveOfs[id]` = signed lateral offset (blocker half width + own half width + margin), `civSwerveLeft[id]` = 90 steps, `civSwerveCar[id]` = blocker. `CivSteerAngle` shifts its aim point sideways by the offset; `CivAccel` ignores `brakeLength` while the lead car is that blocker and caps the speed at a crawl (`SWERVE_MAX_SPEED`). The offset fades out over the last 30 steps.
- Rung 2 (2 s stuck with no car in front, i.e. a wall or object; again at 5 s and 8 s): back up 30 steps with the wheel turned opposite to the wanted steer, so the nose swings back to the lane (replaces the straight 20-step reverse).
- Rung 3 (5 s face to face with an oncoming stuck car): the car with the lower index backs up (rung 2 move) and then goes round (rung 1) the other car.
- Existing rungs stay: 15 s off-screen ping out, 30 s barge.
- Test counters: `gCivSwerveCount`, existing `gCivReverseCount`.
- Done when: a car parked across a lane (player car left in a lane) is passed after ~2-3 s instead of a queue for 30 s; cars that hit a wall back out and rejoin; no car ever swerves out of a queue at a red light.

## Parts 2-4 - result (status: done 2026-10-19, one commit)

Take a Ride, `crowdMul` 3, 80 s, player standing, no jam hook; `standing steps` smart 0 -> 1 (lower is better), average speed, lane changes by the new code. Runs are deterministic per build and setting (3 repeats gave identical numbers).

- Chicago 432 -> 412, speed 50 -> 50, 99 lane changes.
- Frisco 9129 -> 5403, speed 43 -> 41.
- Vegas 17197 -> 10609, speed 40 -> 38, highway speed 51 -> 52.
- Rio 118 -> 371, highway speed 44 -> 41. With the strong exit bias first tried it was 782; without weighted exits 181.
- Havana 5118 -> 5360, Miami 2528 -> 2853 (small rises, open).
- `near contacts` rises 1.3-2x: overtaking cars and hasty drivers run closer.

Findings:
- Lane index = one 512-wide lane; `AILanes`/`LaneDirs` bits are per lane pair (`lane / 2`).
- Only Vegas and Rio have speed limit id 2 roads (all with 4 lanes the car's way); Chicago's wide roads are id 1. Highway = id 2 and 2+ lanes the same way.
- The +25 % highway speed barely shows in the average: cars rarely reach the limit (bends, followers). A free-flow metric (cars without a leader) is needed to tune it further (open).
- `Random2` returns one value per frame; per-car draws use `Random2(0) + id * 40503` split into bit fields.
- The free-road IDM fade `1 - (v/v0)^4` made cars cruise slower (Chicago 50 -> 45); full push up to the limit is used instead.

## Part 2 - speed (done)

- Driver variety: per car `civDriverFactor[id]` 85..115 % from the car id and `Random2`, scales `maxSpeed` and `newAccel`; 3 % of cars are hasty (115-125 %, smaller gap, `CIV_HEADWAY_STEPS` x 0.7).
- Fast roads: speed limit id 2 (top tier of `speedLimits`) gets +25 % when the road has 2+ lanes in the car's direction (highway feel); `WorldSpeedLimit` stays the base.
- Smooth acceleration: replace the fixed accelerate/coast switch in `CivAccelTrafficRules` by the free-road IDM term `a * (1 - (v/v0)^4)`.
- Bend braking for every civ car (today only `civCornerBrake` cars): `CivCornerSpeed` look-ahead, with the limit from curve radius (`v = sqrt(a_lat * r)`), so faster cars slow before bends.
- Done when: cars on a straight multi-lane fast road visibly cruise faster than on side streets, speeds differ per car, no car leaves the road in a bend at the higher speeds.

## Part 3 - overtaking, keep right, merging (done, except merging)

Built: `CivSmartLaneChange` at each new route node (no ctrl/turn node, no indicator, not swerving, not a mission car): overtake left then right when the leader is under 85 % of the wanted speed and the side lane is free (`CivLaneGapFree`: 20 steps of own travel ahead, closing speed x 30 steps behind) and faster; keep right when nothing brakes us. Not built: per-step checks, MOBIL braking test, gap acceptance when entering a road (existing junction yields stay).


- Continuous lane change on straights with 2+ lanes in the same direction (`ROAD_LANES_COUNT`, `ROAD_LANE_DIR`, `ROAD_IS_AI_LANE`, `dr2roads.h`): check every 4th step per car (staggered by id).
- Incentive (simple MOBIL): the leader is slower than 85 % of my wanted speed and closer than 3 s; target lane leader is farther or faster; safety: the new follower's gap is at least 1.0 s x its speed + one car length and it would not have to brake harder than `bSafe` (falls from 4 to 17 m/s^2 equivalent as speed drops).
- Keep right: back to the right lane when that lane is free for 4 s ahead.
- Lane change uses the existing route-node lane switch (`CheckChangeLanes`, `RemapLaneChange`) with the indicator (`changeLaneIndicateCount`) and a 4 s cooldown.
- Merging at junction exits: entering a road, wait for a gap of 2 s in the target lane (gap acceptance), shrinking with waiting time (impatience).
- Done when: on a 2+ lane road a slow car is passed; cars return right; no lane change into a car.

## Part 4 - route choice and junction manners (done: weighted exits only)

Built: `CivExitWeight` = (speed limit id + lanes the same way + 2) x (3 + cos(trip heading - exit heading)), trip heading re-rolled at 1 in 8 junctions. Existing turn-lane rules and junction yields kept. Not built: don't-block-the-box, yield to the bigger road.


- Turn lanes: in `GetNextRoadInfo` the leftmost lane may turn left, rightmost right, middle straight (mirrored for left-hand traffic if a city uses it); lane changes before the junction to reach the right lane.
- Weighted exits instead of `Random2(0) % 3`: weight by road class (lanes x speed limit) plus a per-car "trip" bias (a heading kept for 30-60 s), so cars flow along main roads instead of zig-zagging; dead ends get weight 0 unless nothing else.
- Junctions without lights: yield to the right / to the bigger road; don't enter a junction whose exit lane is full (don't block the box), released after 4 s waiting.
- Done when: traffic visibly follows main roads, fewer cars in side streets, fewer deadlocks in junctions.

## Part 5 - pedestrians (skipped 2026-10-19: peds keep the old logic)

- Predictive dodge: a car that reaches the ped within 1 s makes it step aside (away from the car's path) before it has to jump; horns and fast player cars make nearby peds flee for 2 s.
- Crossing: at junctions with lights, peds cross during the pedestrian part of `TrafficLightCycle`; elsewhere cross only with a 4 s gap in both directions.
- Ped-to-ped: keep apart from the 4 nearest peds (separation), stop in pairs to chat 10-14 s, idle stops.
- Done when: peds cross at junctions on the light, do not walk into each other, side-step cars early.

## Test method

- Build: `src_rebuild` Visual Studio solution (see `docs/DEVELOPER_NOTES.md`), Release x86.
- Take a Ride in a Driver 2 city and a Driver 1 city with `crowdMul` 3; counters `gCivReverseCount`, `gCivSwerveCount`, `gCivNearContact` printed by the play log.
- A/B: `smartTraffic=0` vs `1` on the same route.
