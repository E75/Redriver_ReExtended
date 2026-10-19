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

1. City 6 plays in the Training slot (`TRAIN.LEV`), scripts 114-116 convert as free drives, menu button of the Training slot lists desert and garage missions. Done when `-d1mission 114` starts in the desert and the menu shows the three drives.
2. Dirt track races and practice (106-113): checkpoints per lap in order, lap counter and "Lap" / "Finished!" messages, count-up timer with lap times. Needs a way to repeat checkpoints for N laps (D2 targets run once, 16 at most).
3. Spoke wheel car: every desert mission uses the player car of `TRAIN.LEV`.
4. Garage test (117 / 118) like the original: needs the test logic of the original executable (not in the repo or this PC; ask the user for it).
