# Driver 1 desert and garage missions (plan, 2026-10-19)

Goal: the Driver 1 desert missions (dirt track races, practice laps, free drives) and the garage driving test play like the original, with the desert's own car (spoke wheels). Branch `feature/d1-desert-races`, one green commit per part.

## Findings

- City id 6 of the mission scripts is the training level `TRAIN.LEV` (720000 units square, all desert start spots inside it). `FLAT.LEV` is not used by any script and does not parse as a level.
- Scripts 106-116 use city 6, op 110 (`CFG_96264`) picks the track: 0-3 dirt tracks, 4-6 free drive areas.
  - 106-109: race on track 2, 3, 0, 1 with 6, 5, 4, 3 laps (var 62 = laps, var 63 = lap counter), 5 to 12 checkpoints (TARGET0 + WAIT_STATE) per lap, count-up timer, `SPLIT_TIME` per lap, `BEST_TIME` at the end.
  - 110-113: the same four tracks as endless practice (var 62 = 0x7FFFFFFF).
  - 114-116: free drives (cops off, no target), like the garage scripts.
- Scripts 117 / 118 (city id 2, `IVIEW.LEV`): the garage test. The script only waits; the test itself (manoeuvres, time limit) is code in the original executable, not in the script.
- `TRAIN.LEV` holds one player car (car model slot 5), the cities hold four: that is the spoke wheel car.

## Parts

1. Done: city 6 plays in the Training slot (`TRAIN.LEV`), scripts 114-116 convert as free drives ("Desert Drive 1-3"), the menu button "Desert, Garage" lists the desert and garage missions.
2. Done: races and practice laps (106-113, "Desert Race 1-4", "Desert Practice 1-4"). `ConvertRace` turns the checkpoints into a ProcessTarget chain; at the end of a lap the engine function `0x4000021` (mission.c, Driver 1 missions only) clears the lap's checkpoints and returns 1 after the last lap (laps 0 = practice, endless). Practice resets the clock every lap like the original. Not done: the 3-2-1 countdown (D2 scripts cannot wait, only "GO" is shown), split and best times. Tested with a hook that moves the car to the active checkpoint: race 107 completes after 5 laps, practice 110 keeps going.
3. Done: the desert already starts with the only player car of `TRAIN.LEV` (Training now lists 1 player car, not 2). The spoke wheels were missing in every D1 city: the converter swaps the D1 wheel models for the D2 ones (layout of `MangleWheelModels`); `CopyD1Rim` now gives the D2 hub faces (polys 2 and 3) the D1 rim texture of the same wheel model (clean, blurred, hubcap off). Checked from the side (`REDRIVER2_CAMYAW=1024`) in desert drive 114 and Miami mission 1.
4. Garage test (117 / 118) like the original: the scripts only wait, the test lives in the car park code overlay of the original game (demonstration run, follow the leader, cone and pole course with score and penalties, time limit, car damage and paintwork fails). Next: work out its rules locally and rebuild them in the engine.
