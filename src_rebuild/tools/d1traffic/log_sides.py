"""Counts civ cars that drive on the wrong side of a straight, from the TRAF telemetry lines of REDRIVER2.log.

Usage: python -I log_sides.py <REDRIVER2.log> <converted LEV> [--worst N]
A TRAF line (civ_ai.c TrafficTelemetry, temporary) is one car on one straight: lat > 0 is the side of lane 0 (right of the
straight angle), dot is +1 for a car driving with the angle. wrong = the car is on the left of the divider between the
two directions, seen from its own heading.
"""
import argparse
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'd1conv' / 'spike'))
import d1spike  # noqa: E402  (path is set up above)

LINE = re.compile(r'TRAF car (\d+) road (\d+) n (\d+) dirs (\d+) lane (\d+) laneDir (\d+) lat (-?\d+) along (-?\d+)/(\d+) dot (-?\d+) wrong (\d+) dirmis (\d+) state (\d+)')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('lev', type=Path)
    parser.add_argument('--worst', type=int, default=8)
    args = parser.parse_args()

    lev = d1spike.Lev(args.lev)
    offset = lev.lump_offset(40)
    straights = [lev.u('<iiHhh4hbbbb', offset + 4 + i * 28) for i in range(lev.u('<i', offset)[0])]

    per_road: dict[int, list[int]] = defaultdict(lambda: [0, 0, 0])
    by_n: Counter = Counter()
    wrong_by_n: Counter = Counter()
    total = wrong = mismatch = one_way = off_road = 0
    for text in args.log.read_text(encoding='utf-8', errors='replace').splitlines():
        m = LINE.search(text)
        if not m:
            continue
        v = [int(g) for g in m.groups()]
        road, n, bad, dirmis = v[1], v[2], v[10], v[11]
        dirs = straights[road][10] & 0xFF if road < len(straights) else 0
        if (dirs & 1) == (dirs >> (n - 1) & 1):
            one_way += 1				# one-way roads: a car in the left lane is not on the wrong side
            continue
        # the divider between the two directions lies after the units with bit 0; a car must keep right of it
        divider = n * 512 - (next(k for k in range(n) if dirs >> k & 1)) * 1024
        side = v[6] - divider
        bad = 1 if (abs(side) > 100 and ((v[9] > 0) == (side < 0))) else 0
        if abs(v[6]) > n * 512 + 300:
            off_road += 1				# the car is not on this straight (turning, entering): not a lane question
            continue
        total += 1
        wrong += bad
        mismatch += dirmis
        by_n[n] += 1
        wrong_by_n[n] += bad
        row = per_road[road]
        row[0] += 1
        row[1] += bad
        row[2] += dirmis
    print(f'one-way samples skipped {one_way}, off-road samples skipped {off_road}')
    print(f'two-way samples {total}, wrong side {wrong} ({100 * wrong / max(1, total):.1f}%), heading against lane direction {mismatch}')
    print('  by lane units n:', {n: f'{wrong_by_n[n]}/{by_n[n]}' for n in sorted(by_n)})
    worst = sorted(per_road.items(), key=lambda kv: -kv[1][1])[:args.worst]
    for road, (count, bad, dirmis) in worst:
        if bad == 0:
            break
        s = straights[road] if road < len(straights) else None
        info = f'angle {s[4]} mid {s[0]},{s[1]} len {s[2]} n {s[9] & 15} dirs {s[10] & 0xFF:#x}' if s else ''
        print(f'  road {road}: {bad}/{count} wrong, {dirmis} dirmis; {info}')


if __name__ == '__main__':
    main()
