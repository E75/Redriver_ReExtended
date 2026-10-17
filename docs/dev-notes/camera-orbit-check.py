#!/usr/bin/env python3
"""Checks the CAMTEST lines that `REDRIVER2_dev.exe -camtest` writes to REDRIVER2.log.

Prints PASS <n> / FAIL <n>: <actual> != <expected> and exits 1 on any failure.

The expected values follow the scenario table `camTestScript` in camera.c (ticks, stick x):
  A  t 0-44 hold +100, t 45-204 rest
  B  t 205-249 hold +100, 250-269 rest, 270-289 hold -100, 290-449 rest
  C  t 450-509 stick 20 (inside the deadzone), 510-539 rest
  D  t 540-589 stick up (camera goes down), 590-749 rest
  E  t 750-799 stick down (camera goes up), 800-959 rest
  F  t 960-989 look back held, 990-1049 rest
  G  t 1050-1069 stick right, 1070-1109 look back held, 1110-1269 rest
Expected numbers are derived by hand from the constants in plans/camera-orbit.md, not read from the code:
  stick y 100 -> -100: the same 63 after the ease, 63*24/128 = 11 units per tick, limits -120 .. 380.
  look back: camLook moves a third of the way to 2048 per tick (2048/3 = 682 on the first tick).
  stick 100 -> the engine SquareRoot0(10000) is 99 -> radial 71/100*128 = 90 -> eased 90*90/128 = 63 -> 63*44/128 = 21 units per tick.
"""
import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

PER_TICK = 21
DELAY = 30
HOLD = 45
TICKS = 1270
PITCH_STEP = 11
PITCH_MIN = -120
PITCH_MAX = 380
LINE = re.compile(r"CAMTEST t=(-?\d+) fc=(\d+) yaw=(-?\d+) idle=(\d+) w=(\d+) lateral=(-?\d+) pitch=(-?\d+) look=(\d+) above=(-?\d+)")


@dataclass
class Row:
    fc: int
    yaw: int
    idle: int
    w: int
    lateral: int
    pitch: int
    look: int
    above: int


class Checker:
    def __init__(self) -> None:
        self.failures = 0

    def check(self, name: str, ok: bool, actual: object, expected: object) -> None:
        if ok:
            print(f"PASS {name}")
        else:
            self.failures += 1
            print(f"FAIL {name}: {actual} != {expected}")


def load(path: Path) -> dict[int, Row]:
    rows: dict[int, Row] = {}
    with path.open(encoding="utf-8", errors="replace") as log:
        for line in log:
            match = LINE.search(line)
            if match:
                t, fc, yaw, idle, w, lateral, pitch, look, above = (int(g) for g in match.groups())
                rows[t] = Row(fc, yaw, idle, w, lateral, pitch, look, above)
    return rows


def decay_end(rows: dict[int, Row], start: int, end: int, field: str = "yaw") -> tuple[int | None, str]:
    """Walks start..end: the field must shrink without changing sign. Returns the tick where it hits 0, or why it failed."""
    prev = getattr(rows[start - 1], field)
    sign = 1 if prev > 0 else -1
    for t in range(start, end + 1):
        yaw = getattr(rows[t], field)
        if yaw * sign < 0 or abs(yaw) > abs(prev):
            return None, f"t={t} yaw={yaw} prev={prev}"
        prev = yaw
        if yaw == 0:
            return t, ""
    return None, f"still {prev} at t={end}"


def run(rows: dict[int, Row]) -> int:
    c = Checker()
    c.check("P0 log has 1270 CAMTEST ticks", all(t in rows for t in range(TICKS)), len(rows), TICKS)
    if c.failures:
        return 1

    steps = {rows[t + 1].fc - rows[t].fc for t in range(TICKS - 1)}
    c.check("P1 one camera update per tick (fc step == 1)", steps == {1}, sorted(steps), [1])

    held = [rows[t].yaw for t in range(HOLD)]
    c.check("P2 hold: yaw grows by 21 per tick", held == [PER_TICK * (t + 1) for t in range(HOLD)], held[:3] + held[-2:], "21,42,63 .. 945")
    c.check("P3 hold right: camera ends left of the car (lateral < 0)", rows[HOLD - 1].lateral < -50, rows[HOLD - 1].lateral, "< -50")

    peak = rows[HOLD - 1].yaw
    c.check("P4 no return during the delay (t 45..74)", all(rows[t].yaw == peak for t in range(HOLD, HOLD + DELAY)), [rows[45].yaw, rows[74].yaw], peak)
    c.check("P5 return starts at t 75", rows[HOLD + DELAY].yaw < peak, rows[HOLD + DELAY].yaw, f"< {peak}")
    zero_a, why = decay_end(rows, HOLD + DELAY, 204)
    c.check("P6 A: monotone return without overshoot, reaches 0 by t 204", zero_a is not None, why, "0")
    if zero_a is not None:
        print(f"     A: yaw back at 0 {zero_a - HOLD} ticks after release (delay {DELAY})")

    c.check("P7 B: second hold starts from 0", rows[205].yaw == PER_TICK, rows[205].yaw, PER_TICK)
    c.check("P8 B: yaw constant while resting 20 ticks", all(rows[t].yaw == rows[249].yaw for t in range(250, 270)), [rows[250].yaw, rows[269].yaw], rows[249].yaw)
    opposite_ok = all(rows[270 + i].yaw == rows[269].yaw - PER_TICK * (i + 1) for i in range(20))
    c.check("P9 B: opposite stick lowers yaw by 21 per tick", opposite_ok, [rows[270].yaw, rows[289].yaw], "-21 per tick")
    c.check("P10 B: idle resets to 0 on input", rows[289].idle == 0, rows[289].idle, 0)
    c.check("P11 B: no return right after the abort", all(rows[t].yaw == rows[289].yaw for t in range(290, 290 + DELAY - 1)), rows[290 + DELAY - 2].yaw, rows[289].yaw)
    zero_b, why = decay_end(rows, 290 + DELAY, 449)
    c.check("P12 B: smooth return to 0 by t 449", zero_b is not None, why, "0")

    c.check("P13 C: stick inside the deadzone moves nothing", all(rows[t].yaw == 0 for t in range(450, 540)), max(abs(rows[t].yaw) for t in range(450, 540)), 0)

    # D: stick up lowers the camera
    c.check("P14 D: pitch -11 per tick down to -120", [rows[t].pitch for t in range(540, 590)] == [max(PITCH_MIN, -PITCH_STEP * (t - 539)) for t in range(540, 590)],
            [rows[t].pitch for t in (540, 541, 549, 589)], "-11,-22 .. -120")
    c.check("P15 D: pitch constant during the delay (t 590..619)", all(rows[t].pitch == PITCH_MIN for t in range(590, 620)), rows[619].pitch, PITCH_MIN)
    zero_d, why = decay_end(rows, 620, 749, "pitch")
    c.check("P16 D: pitch returns to 0 without overshoot by t 749", zero_d is not None, why, "0")
    c.check("P17 D: camera is lower than at rest, never underground", rows[589].above < rows[539].above and min(r.above for r in rows.values()) > 0, [rows[589].above, rows[539].above], "lower and > 0")

    # E: stick down raises the camera
    c.check("P18 E: pitch +11 per tick up to 380", [rows[t].pitch for t in range(750, 800)] == [min(PITCH_MAX, PITCH_STEP * (t - 749)) for t in range(750, 800)],
            [rows[t].pitch for t in (750, 751, 789, 799)], "11,22 .. 380")
    c.check("P19 E: camera is much higher than at rest", rows[799].above > rows[539].above + 500, [rows[799].above, rows[539].above], "> rest + 500")
    zero_e, why = decay_end(rows, 830, 959, "pitch")
    c.check("P20 E: pitch returns to 0 without overshoot by t 959", zero_e is not None, why, "0")

    # F: look back eases in and out
    looks = [rows[t].look for t in range(960, 990)]
    c.check("P21 F: look starts at 682 and rises monotone to 2048", looks[0] == 682 and looks[-1] == 2048 and looks == sorted(looks) and max(looks) <= 2048, [looks[0], looks[-1]], "682 .. 2048")
    back = [rows[t].look for t in range(990, 1050)]
    c.check("P22 F: look returns to 0 monotone", back[-1] == 0 and back == sorted(back, reverse=True) and rows[990].look < 2048, [back[0], back[-1]], "-> 0")

    # G: looking back while turned eases the yaw to 0
    c.check("P23 G: yaw 420 after the hold", rows[1069].yaw == 20 * PER_TICK, rows[1069].yaw, 20 * PER_TICK)
    yaws = [rows[t].yaw for t in range(1070, 1110)]
    c.check("P24 G: yaw eases to 0 while looking back", yaws[-1] == 0 and yaws == sorted(yaws, reverse=True) and rows[1109].look == 2048, [yaws[0], yaws[-1], rows[1109].look], "420 -> 0, look 2048")
    c.check("P25 G: nothing left over after the release", all(rows[t].yaw == 0 and rows[t].pitch == 0 for t in range(1110, TICKS)) and rows[TICKS - 1].look == 0, [rows[TICKS - 1].yaw, rows[TICKS - 1].look], "0")

    print("ALL PASS" if c.failures == 0 else f"{c.failures} FAILED")
    return 1 if c.failures else 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log", type=Path, help="path to REDRIVER2.log")
    args = parser.parse_args()
    sys.exit(run(load(args.log)))


if __name__ == "__main__":
    main()
