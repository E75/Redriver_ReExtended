# Driver 1 car physics in Redriver (2026-10-18)

Descriptions only; the analysis and the captures stay outside the repo.

## Result in one line

The D2 car code (`StepOneCar`, `AddWheelForcesDriver1`, `GetFrictionScalesDriver1`, `ConvertTorqueToAngularAcceleration`,
`GlobalTimeStep`, `ControlCar`, `GetEngineRevs`) is the D1 car code with every speed, length and force divided by 1.4648 (D1 cell
3000 against 2048), plus a few D2 additions. Porting D1 handling therefore means: use the D1 sizes in D1 cities, and undo the few
model differences listed below. No new physics code is needed.

## Call chain

- Per frame `GlobalTimeStep`: runs the queued car steps, then integrates and does car to car collisions in 4
  sub steps of 2 RK passes, exactly like the D2 function (position += velocity / 256 in 1/16 units, quaternion update and
  renormalise, angular velocity clamp, a car below a small velocity sum is put to rest).
- A first function only queues a car (record pointer, "driven" flag); `StepCars` runs `StepOneCar` + `AddWheelForcesDriver1` (one
  function in D1) and then `ControlCarRevs` (revs from the gear table through `GetEngineRevs`). `ConvertTorqueToAngularAcceleration`
  follows. A surface lookup returns the friction factor: full on roads, half on grass, where it also adds the grass wobble.
- Input (`ControlCar`): digital steering in steps up to a lock, faster steering with a second bit, analogue steering
  as a cube of the stick, return to centre per frame; throttle, brake (constant forward, speed dependent when reversing), hand brake,
  burnout up to a wheel speed limit, gear change cuts the throttle.
- Car record: stride `0x18c` (car 0 = player). Offsets used here: `+0x18` position, `+0x24`
  fixed point position, `+0x30` quaternion, `+0x40` velocity, `+0x4c` angular velocity, `+0x58` acceleration, `+0x68` angular
  acceleration, `+0x78` four wheel bytes (compression, locked, on grass), `+0xf0` thrust, `+0xf4` wheel angle, `+0xf8` heading,
  `+0xfc` hand brake, `+0x100` burnout, `+0x104` wheel speed, `+0x108` speed, `+0x17e` auto brake counter, `+0x180` revs,
  `+0x184` gear, `+0x185` changing gear.

## The model (same structure in D1 and D2)

- Body contact: 12 points (4 when upright) against the ground; a point below the ground gives an impulse along the ground normal
  plus a clamped friction per axis, the car is lifted out of the ground and gets extra angular damping.
- Wheels: per wheel the suspension depth (ground height minus wheel height plus a rest offset, clamped) gives a spring force from
  the new and the old depth. Lateral grip = slide speed across the wheel times a speed dependent grip curve (high at low speed,
  falling with speed), clamped, times the front or rear friction share. The rear wheels push with the thrust (front wheels too for
  cops), every wheel drags with a fixed factor times the velocity. Locked wheels (hand brake, hard brake above a wheel speed limit)
  push against their own sliding direction. The sum is scaled by the surface friction (wetness lowers it) and by how upright the
  car is. Torque = wheel lever x force.
- Front/rear friction share: a base by throttle state (accelerate, roll, brake), plus the auto brake while steering at lock, the
  hand brake and burnout terms, a grass term, and a per class factor (cop difficulty, civilian). D2 stores the class factors in
  `handlingType`, they are the same numbers.
- Car to car: contact point a fixed height above the contact, strike speed from the depth plus the closing speed, capped; push
  along the normal, torque from a fixed factor; cops push the player less by difficulty; damage from the closing speed.

## Differences D1 against Redriver in a D1 city (before this change)

1. Scale (the main one): the converted D1 levels use 2048 unit cells too, so `WORLD_FORCE` (`MAP_CELL_SIZE / 2048`) was 1 in
   D1 cities and every force stayed D2 sized in a 1.4648 times bigger world: throttle, brakes and gravity 0.68 of D1. Measured top
   speed 204 against about 300 in D1 (the throttle against drag balance of both).
2. Speed and length limits D2 sized while speeds and depths are D1 sized: the speed fed into the grip curve (D2 multiplies by 3/2),
   the wheel lock limit, the slide clamp threshold, the suspension rest offset and travel (D1 also takes the plain height difference,
   D2 multiplies it by the normal), the burnout limit, the reverse brake speed scale, the gear change wheel speeds (the D2 gear table
   is the D1 one with wheel speeds x 2/3 and ratios x 3/2).
3. Roll lever: D1 scales the height of every wheel lever (cops a bit lower than others), D2 only for cops. D1 cars roll much less.
4. D1 applies the body contact impulse of every point below the ground and lifts straight up; D2 only the deepest point, along the
   normal. D1 has one global contact point table and one twist pair for all cars, no mass ratio in car to car hits, no per car
   power or traction (the D1 player cars already get neutral values, `D1_CAR_*` in dr2limits.h).
5. D2 additions not in D1: traction per car, friction loss on steep slopes, water drag, locked wheel normal reset at low speed,
   vertical velocity damping, the "civ car must die" threshold.

## Validation against the captured D1 positions

Captures (outside the repo): per frame the first 64 bytes of the player and the chased car record during a Pursuit run.
Velocity is the fixed point position step times 256.

- Integration (position step = velocity / 4096 per frame) is taken from the original code; the capture holds no velocity, so the
  speeds below rest on it.
- Throttle and drag: on straight frames the forward acceleration of the chased car from standstill falls linearly with speed; its
  intercept matches two driven wheels x throttle x road friction within about 1 %, the slope lies between the drag of 3 and 4 wheels
  on the ground (front wheels unload under acceleration), and the acceleration ends where throttle and drag balance (about 300
  units per frame, also seen as the top speed of the player car).
- Acceleration curve of the player car from standstill (speed after 15 / 30 / 45 / 60 / 90 frames, three starts): D1 about
  63 / 110 / 125 / 156 / 190. Redriver Miami flat out before: 37 / 67 / 81 / 98 / 130; with `d1Handling=1`: 54 / 97 / 122 / 144 / 195.
- Chased car from its REPLAY.BLK pad stream (Pursuit Miami 1, entry 125): an offline re-implementation of the control, force,
  revs and integration steps (scratchpad tool outside the repo, tables read from the own disc), flat ground, no collisions,
  is 9 units off after 30 frames and 31 after 60. At frame 100 the recorded car runs over a crest (35 units up and down) that the
  flat ground does not have; up to frame 300 the error stays about 3 % of the distance driven, then it grows to the route scale.
  Fitted on the way: the chased car uses the 7000 throttle of the chase modes (not 7200), and the stream is one record ahead of
  the first captured frame. Full routes need the map heights (crests, kerbs) and the contacts with other cars.
- With the D1 ground (road map heights, slope polygons and the grass wobble of the surface function, read from the own LEV) and
  full record captures of the chased car, the offline re-implementation is bit
  exact per frame: control, forces, revs and integration. Two details needed for that: in the chase modes the chased car always
  has the "may be colliding" flag, so it integrates in 4 sub steps with 2 passes each; and the engine's own arc tangent table.
  An open loop run from one full state stays exact until the car touches something. From a capture with only position and
  rotation the first unit of error appears after 9 frames (unknown start state) and grows to the route scale after 400 frames.
- From the start state alone (REPLAY.BLK start position and heading, ground height plus the creation offset, the creation
  quaternion with its small tilt, the creation wheel compressions, everything else zero) the full path of the
  chased car is bit exact against two live Pursuit Miami 1 runs: 453 and 471 frames, up to the first contact with traffic (damage
  flag set, player far away). After that the error grows to the route scale within about 150 frames; the traffic is not simulated.
- The chased car is driven by the pad stream only: no distance logic. It brakes with the hand brake and straight
  wheels when the mission ends (game over flag) or the frame count of the start state runs out, which explains the stop with an
  idle player (the mission was lost).
- Apart from its input the chased car is an ordinary physics car, the same in every city: the same step, forces and integration
  as the player, and the normal car collision code (in the chase modes it is always flagged as possibly colliding). Traffic and
  buildings therefore push it (the damage flag of the record is set at the first traffic contact), and after a contact
  the recorded pads simply play on (open loop, no correction back to the route). A 1:1 conversion therefore stores the pad
  stream and the start state per variant and drives the car with the Driver 1 handling and the game's own collisions. Recorded
  positions are only exact up to the first contact. The port is not bit exact, so how long pads alone stay on the route in
  Redriver is not measured yet; a hybrid (positions until the first contact, then the pads from that frame on) needs no exactness.
- Damage of the chased car: its damage total (the value the ram goal compares with the mission's damage limit) only grows in
  hits by the player car; traffic contacts only raise the damage of the hit zone, and world contacts give it no damage at all.
- Pad pack: `src_rebuild/tools/d1conv/d1pads_pack.py` writes the pad stream, start state and chase car model (set by the
  mission script) of every variant to `M<1000 + script>.PADS` next to the `.CHASE` file; the format is in the script.
- Not validated yet: lateral grip and roll (the capture has no pad input; needs a full record capture with known input),
  brakes, car to car collisions.

## Implemented (config.ini `[d1dlc] d1Handling`, default 1)

`D1HandlingActive()` (wheelforces.c) = option on and a D1 city (`IS_DRIVER1_CITY(GameLevel)`). Then: `WORLD_FORCE` scales by
3000 / 2048 (gravity, throttle, brakes, spring, slip clamp, body friction), the twist rates are divided by the same factor so the
turning response per force stays as before (it was already right for the unscaled forces), the limits of point 2 take their D1
value, and the roll lever of point 3 is the D1 one. D2 cities and `d1Handling=0` are unchanged (checked: Chicago flat out run
identical frame by frame with the option on and off).

## Next steps

1. Full record capture with known input (accelerate, steer at lock, brake), and the same script in Redriver: compare
   lateral speed, yaw rate and roll per frame.
2. Body contact as in D1 (all points, straight lift) in D1 cities, and the car to car constants of point 4 (they are D2 sized too).
3. Traffic and cop cars in D1 cities keep the twist and power of their own cosmetics: give them the D1 neutral set as well.
4. Other `MAP_CELL_SIZE / 2048` world scales are 1 in D1 cities for the same reason as point 1 (civilian speed limits
   `WorldSpeedLimit`, spawn and view distances in civ_ai.c and draw.c, fog): check whether they should follow the city instead.
