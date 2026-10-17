"""Driver 1 traffic analysis: lane directions, junction data and traffic lights of a Driver 1 city.

Usage: python -I analyze.py <D1 LEV> [--converted <converted LEV>] [--dump N]
Reads the player's own game data, never copies it. Stdlib only.

Right-hand rule in Driver 2 terms: bit 0 of LaneDirs (the pair on the right of the straight angle) is 0 (with the angle),
the last used bit is 1 (against the angle). Driver 1: lane 0 is on the right of the rising road map direction.
"""
import argparse
import sys
from collections import Counter
from pathlib import Path

SPIKE = Path(__file__).resolve().parent.parent / 'd1conv' / 'spike'
GAPFILL = Path(__file__).resolve().parent.parent / 'gapfill'
sys.path.insert(0, str(SPIKE))
sys.path.insert(0, str(GAPFILL))
import d1spike  # noqa: E402  (paths are set up above)
import lev2  # noqa: E402

CELL = 1500


def table(lev: d1spike.Lev, lump_type: int, fmt: str, size: int) -> list[tuple]:
    offset = lev.lump_offset(lump_type)
    return [lev.u(fmt, offset + 4 + i * size) for i in range(lev.u('<i', offset)[0])]


def classify(lanes: int, dirs: int) -> str:
    """Pattern of a road with `lanes` lane units (bit k = unit k from the right, 1 = against)."""
    bits = [dirs >> k & 1 for k in range(lanes)]
    if all(b == 0 for b in bits):
        return 'one-way with'
    if all(b == 1 for b in bits):
        return 'one-way against'
    if bits[0] == 0 and bits[-1] == 1:
        return 'two-way right-hand'
    if bits[0] == 1 and bits[-1] == 0:
        return 'two-way LEFT-hand'
    return 'mixed'


def road_ids(lev: d1spike.Lev) -> tuple[int, int, dict[int, int]]:
    """Road map ids of all cells: (width, height, {cell index: id}) with the cell index cx + cz * width."""
    m = d1spike.map_info(lev)
    si = d1spike.spool_info(lev)
    rw, rh = lev.u('<ii', lev.lump_offset(7))
    unit_x_mid, unit_z_mid = (rw + 1) * 750, rh * 750
    regions_across = m.cells_across // m.region_size
    regions_down = m.cells_down // m.region_size
    half_x = m.cells_across // 2 * m.cell_size
    half_z = m.cells_down // 2 * m.cell_size
    cache: dict = {}
    ids: dict[int, int] = {}
    for cz in range(rh):
        for cx in range(rw):
            px = cx * CELL - unit_x_mid + 1500
            pz = unit_z_mid - cz * CELL - 750
            rx = (px + half_x) // m.cell_size // m.region_size
            rz = (pz + half_z) // m.cell_size // m.region_size
            if not (0 <= rx < regions_across and 0 <= rz < regions_down):
                continue
            region = rx + rz * regions_across
            if region not in cache:
                sp = d1spike.region_spool(lev, si, region)
                cache[region] = d1spike.d1_region_roadmap(lev, sp)[0] if sp and sp.roadm_size else None
            if cache[region] is not None:
                ids[cx + cz * rw] = cache[region][cx % 60 + cz % 60 * 60]
    return rw, rh, ids


def junction_centres(lev: d1spike.Lev) -> dict[int, tuple[int, int]]:
    """World centre (x, z) of every junction square, from the 0x4000 | id cells of the road map."""
    rw, rh, ids = road_ids(lev)
    unit_x_mid, unit_z_mid = (rw + 1) * 750, rh * 750
    boxes: dict[int, list[int]] = {}
    for index, value in ids.items():
        if value & 0xC000 != 0x4000:
            continue
        cx, cz = index % rw, index // rw
        b = boxes.setdefault(value & 0x3FFF, [cx, cx, cz, cz])
        b[0], b[1], b[2], b[3] = min(b[0], cx), max(b[1], cx), min(b[2], cz), max(b[3], cz)
    return {j: ((b[0] + b[1] + 1) * 750 - unit_x_mid, unit_z_mid - (b[2] + b[3]) * 750) for j, b in boxes.items()}


def report_d1(lev: d1spike.Lev, dump: int) -> tuple[list[tuple], list[tuple]]:
    roads = table(lev, 8, '<hhhbbhbbhh', 16)
    juncs = table(lev, 9, '<hbbhbb' + 'hhhh' * 4 + 'hh', 44)
    bounds = table(lev, 16, '<iiiiB3x', 20)
    print(f'D1 roads {len(roads)} junctions {len(juncs)}')
    print('  (lanes, dirs) hist:', sorted(Counter((r[3], r[4] & 0xFF) for r in roads).items()))
    for axis in (0, 1):
        pats = Counter(classify(r[3], r[4] & 0xFF) for r, b in zip(roads, bounds) if b[4] == axis and 1 <= r[3] <= 7)
        print(f'  axis {"x" if axis else "z"} patterns (D1 lane 0 = right of rising map direction):', dict(pats))
    print('  road flags hist:', dict(Counter(r[6] for r in roads).most_common(6)))
    print('  junction type hist:', dict(Counter(j[1] for j in juncs).most_common(6)))
    print('  timer/NS/WE hist:', dict(Counter((j[3], j[4], j[5]) for j in juncs).most_common(5)))
    print('  entry flags hist:', dict(Counter(j[8 + k * 4] for j in juncs for k in range(4) if j[7 + k * 4] >= 0).most_common(8)))
    for r in roads[:dump]:
        print(f'  road {r[0]} conn {r[1:3]} lanes {r[3]} dirs {r[4] & 0xFF:#x} len {r[5]} flags {r[6]}')
    for j in juncs[:dump]:
        print(f'  junc {j[0]} type {j[1]} entries {[j[6 + k * 4:10 + k * 4] for k in range(4)]}')
    return roads, juncs


def report_converted(path: Path) -> None:
    lev = d1spike.Lev(path)
    straights = table(lev, 40, '<iiHhh4hbbbb', 28)
    juncs = table(lev, 43, '<4hI', 12)
    pats = Counter(classify(s[9] & 15, s[10] & 0xFF) for s in straights if 1 <= (s[9] & 15) <= 7)
    print(f'converted straights {len(straights)} junctions {len(juncs)}')
    print('  D2 straight patterns:', dict(pats))
    print('  junction flags hist:', dict(Counter(j[4] for j in juncs)))


def report_lights(d1: d1spike.Lev, conv_path: Path, juncs: list[tuple]) -> None:
    """Traffic light models of the converted LEV against the nearest junction square."""
    centres = junction_centres(d1)
    conv = lev2.Lev(conv_path)
    lights: list[tuple] = []
    for rz in range(conv.regions_down):
        for rx in range(conv.regions_across):
            region = conv.read_region(rx, rz)
            if region is None:
                continue
            cells, area = region
            for objs in cells.values():
                for o in objs:
                    name = conv.model_names[o.model] if o.model < len(conv.model_names) else ''
                    if name.startswith('TLIGHT'):
                        lights.append((o.x, o.z, o.yang, name))
    print(f'traffic light objects: {len(lights)}, junction squares {len(centres)}')
    near = Counter()
    dist_hist = Counter()
    for x, z, yang, name in lights:
        best = min(centres.items(), key=lambda kv: (kv[1][0] - x) ** 2 + (kv[1][1] - z) ** 2)
        d = ((best[1][0] - x) ** 2 + (best[1][1] - z) ** 2) ** 0.5
        dist_hist[int(d // 1000)] += 1
        if d < 4500:
            near[best[0]] += 1
    print('  distance to nearest junction centre (km-units of 1000):', dict(sorted(dist_hist.items())[:8]))
    print(f'  junctions with a light within 4500 units: {len(near)}; lights per junction hist: {dict(Counter(near.values()))}')
    with_light = Counter()
    without = Counter()
    for j in juncs:
        key = (j[1], tuple(j[8 + k * 4] if j[7 + k * 4] >= 0 else -9 for k in range(4)))
        (with_light if j[0] in near else without)[j[1]] += 1
        del key
    print('  junction type with light:', dict(with_light), ' without:', dict(without))
    exits = Counter()
    for j in juncs:
        n = sum(1 for k in range(4) if j[7 + k * 4] >= 0)
        exits[(n, j[0] in near)] += 1
    print('  (exits, has light) hist:', dict(sorted(exits.items())))
    flag_stats: dict = {}
    for j in juncs:
        for k in range(4):
            if j[7 + k * 4] >= 0:
                flag_stats.setdefault(j[8 + k * 4], Counter())[j[0] in near] += 1
    print('  entry flags -> (has light: count):', {k: dict(v) for k, v in sorted(flag_stats.items())})


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('lev', type=Path)
    parser.add_argument('--converted', type=Path)
    parser.add_argument('--dump', type=int, default=0)
    args = parser.parse_args()

    lev = d1spike.Lev(args.lev)
    print(f'== {args.lev.name}')
    _, juncs = report_d1(lev, args.dump)
    if args.converted:
        report_converted(args.converted)
        report_lights(lev, args.converted, juncs)


if __name__ == '__main__':
    main()
