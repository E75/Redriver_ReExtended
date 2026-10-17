#!/usr/bin/env python3
"""Checks the FOOTTEST lines that `REDRIVER2_dev.exe -city 0 -mission 50 -onfoot -camtest` writes to REDRIVER2.log.

Prints PASS <n> / FAIL <n>: <actual> != <expected> and exits 1 on any failure.

The script is `camTestFootScript` in camera.c: camera turns right (60 ticks), then the pad holds D_UP, D_RIGHT, rest, D_DOWN, rest.
Walking is relative to the camera: the movement direction must be camera heading + 0 / 1024 / 2048 (heading units, 4096 = 360 degrees),
within TOLERANCE (the dead zone of the steering in TannerPadCameraRelative is 64), and the camera must not drift while walking.
"""
import argparse
import math
import re
import sys
from pathlib import Path

TOLERANCE = 120
LINE = re.compile(r"FOOTTEST fc=(\d+) pad=([0-9a-f]+) x=(-?\d+) z=(-?\d+) dir=(\d+) cam=(\d+) yaw=(-?\d+) speed=(-?\d+)")
PHASES = [("P1 forward", 0x1000, 0), ("P2 right", 0x2000, 1024), ("P3 back", 0x4000, 2048)]


def wrap(a: int) -> int:
    return (a + 2048) % 4096 - 2048


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("log", type=Path)
    args = parser.parse_args()

    rows = [tuple(int(g, 16) if i == 1 else int(g) for i, g in enumerate(m.groups())) for m in map(LINE.search, args.log.read_text(encoding="utf-8", errors="replace").splitlines()) if m]
    failures = 0

    def check(name: str, ok: bool, actual, expected) -> None:
        nonlocal failures
        print(f"PASS {name}" if ok else f"FAIL {name}: {actual} != {expected}")
        failures += 0 if ok else 1

    for name, pad, offset in PHASES:
        phase = [r for r in rows if r[1] == pad]
        check(f"{name}: phase found", len(phase) >= 100, len(phase), ">= 100 ticks")

        if len(phase) < 100:
            continue

        last = phase[-60:]
        dx = last[-1][2] - last[0][2]
        dz = last[-1][3] - last[0][3]
        moved = math.hypot(dx, dz)
        heading = round(math.atan2(dx, dz) * 4096 / (2 * math.pi)) % 4096
        cam = phase[-1][5]
        check(f"{name}: Tanner runs", moved > 1000, round(moved), "> 1000 units")
        check(f"{name}: direction is camera + {offset}", abs(wrap(heading - (cam + offset))) <= TOLERANCE, heading, (cam + offset) % 4096)
        check(f"{name}: camera holds still", len({r[5] for r in phase[-60:]}) == 1, sorted({r[5] for r in phase[-60:]}), "one value")

    check("camera stays turned (no return on foot)", rows[-1][6] != 0, rows[-1][6], "!= 0")
    print("ALL PASS" if failures == 0 else f"{failures} FAILED")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
