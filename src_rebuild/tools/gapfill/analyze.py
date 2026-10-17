"""Gap analysis of Driver 2 city LEVs: holes between objects, roofless buildings, ragged city edge.

usage: python -I analyze.py <DRIVER2\\LEVELS dir> <out dir> [CITY ...]

Writes <out>/<CITY>_occupancy.png and <CITY>_report.txt (never into the repo). Stdlib only.
"""
import argparse
import math
import struct
import sys
import zlib
from collections import Counter, defaultdict, deque
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lev2 import Lev  # noqa: E402

CITIES = ['CHICAGO', 'HAVANA', 'VEGAS', 'RIO']
ROOF_MIN_COVER = 0.5   # a building whose roof polygons cover less of its footprint is roofless
# colours of the occupancy map; priority order of the cell classes (first match wins)
COLORS = {'roofless': (255, 0, 0), 'building': (255, 150, 30), 'tree': (20, 110, 20), 'prop': (240, 240, 240),
          'path': (170, 170, 170), 'grass': (60, 190, 60), 'alley': (120, 90, 60), 'junction': (80, 120, 220),
          'road': (50, 70, 130), 'water': (30, 160, 220), 'hole': (255, 0, 255), 'empty': (0, 0, 0)}
PRIORITY = ['roofless', 'building', 'tree', 'prop', 'path', 'grass', 'alley', 'junction', 'road', 'water']


def write_png(path: Path, w: int, h: int, rows: list[bytes]) -> None:
    raw = b''.join(b'\0' + r for r in rows)

    def chunk(tag: bytes, data: bytes) -> bytes:
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)

    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def footprint_cells(o, m, cell: int, half_x: int, half_z: int):
    """Cells covered by the rotated bounding box of a building (world x/z -> cell index)."""
    cx, cz = (m.bbox[0] + m.bbox[3]) / 2, (m.bbox[2] + m.bbox[5]) / 2
    hw, hd = (m.bbox[3] - m.bbox[0]) / 2, (m.bbox[5] - m.bbox[2]) / 2
    a = o.yang * 64 * math.pi / 2048
    s, c = math.sin(a), math.cos(a)
    ex, ez = abs(hw * c) + abs(hd * s), abs(hw * s) + abs(hd * c)
    wx, wz = o.x + cx * c + cz * s, o.z - cx * s + cz * c
    x0, x1 = int((wx - ex) // cell) + half_x, int((wx + ex - 1) // cell) + half_x
    z0, z1 = int((wz - ez) // cell) + half_z, int((wz + ez - 1) // cell) + half_z
    return [(x, z) for z in range(z0, z1 + 1) for x in range(x0, x1 + 1)]


def analyze(lev: Lev, name: str, out: Path) -> list[str]:
    w, h = lev.cells_across, lev.cells_down
    half_x, half_z = w // 2, h // 2
    kinds: dict = defaultdict(set)          # (cx, cz) -> classes of objects in the cell
    roofless_inst: Counter = Counter()      # model name -> instances without roof
    building_inst: Counter = Counter()
    untextured_roof: Counter = Counter()
    roofable_inst: Counter = Counter()      # models that get a roof from the runtime rule (lev2.roof_ring)
    roofless_after = 0
    regions = 0
    for rz in range(lev.regions_down):
        for rx in range(lev.regions_across):
            r = lev.read_region(rx, rz)
            if r is None:
                continue
            regions += 1
            cells, area = r
            for i, objs in cells.items():
                cx, cz = rx * lev.region_size + i % lev.region_size, rz * lev.region_size + i // lev.region_size
                kinds[(cx, cz)].add('seen')
                for o in objs:
                    m = lev.model(area, o.model)
                    if m is None:
                        kinds[(cx, cz)].add('prop')
                        continue
                    k = m.kind
                    if m.ring:
                        roofable_inst[m.name or str(m.index)] += 1
                    if k == 'building':
                        fp = max(1, (m.bbox[3] - m.bbox[0]) * (m.bbox[5] - m.bbox[2]))
                        cover = m.roof_area / fp
                        building_inst[m.name] += 1
                        if cover < ROOF_MIN_COVER:
                            k = 'roofless'
                            roofless_inst[m.name or str(m.index)] += 1
                            if not m.ring:
                                roofless_after += 1
                        elif m.roof_textured < 0.5 * m.roof_area:
                            untextured_roof[m.name] += 1
                        for c in footprint_cells(o, m, lev.cell_size, half_x, half_z):
                            if 0 <= c[0] < w and 0 <= c[1] < h:
                                kinds[c].add(k)
                    kinds[(cx, cz)].add(k)
    filled = {c for c, k in kinds.items() if k - {'seen'}}
    # scanline hull: inside both the row span and the column span
    row_span, col_span = {}, {}
    for x, z in filled:
        a = row_span.get(z, (x, x))
        row_span[z] = (min(a[0], x), max(a[1], x))
        b = col_span.get(x, (z, z))
        col_span[x] = (min(b[0], z), max(b[1], z))
    holes = set()
    for z, (a, b) in row_span.items():
        for x in range(a, b + 1):
            if (x, z) not in filled and x in col_span and col_span[x][0] <= z <= col_span[x][1]:
                holes.add((x, z))
    comps = components(holes)
    xs = [c[0] for c in filled]
    zs = [c[1] for c in filled]
    bx0, bx1, bz0, bz1 = min(xs), max(xs), min(zs), max(zs)
    bbox_cells = (bx1 - bx0 + 1) * (bz1 - bz0 + 1)
    hull_cells = sum(b - a + 1 for a, b in row_span.values())
    # edge raggedness: distance of every row end / column end to the bounding square
    left = [a - bx0 for a, _ in row_span.values()]
    right = [bx1 - b for _, b in row_span.values()]
    top = [a - bz0 for a, _ in col_span.values()]
    bottom = [bz1 - b for _, b in col_span.values()]
    class_cells = Counter()
    for c in filled:
        k = kinds[c]
        class_cells[next(p for p in PRIORITY if p in k) if k & set(PRIORITY) else 'prop'] += 1
    bare = [c for c in filled if kinds[c] - {'seen'} <= {'grass', 'path', 'alley'}]
    lines = [f'== {name}: map {w}x{h} cells of {lev.cell_size}, regions present {regions}/{lev.num_regions}',
             f'filled cells {len(filled)}, bounding box x {bx0}..{bx1} z {bz0}..{bz1} = {bx1 - bx0 + 1}x{bz1 - bz0 + 1}'
             f' ({bbox_cells} cells), fill of bbox {len(filled) / bbox_cells:.1%}, scanline hull {hull_cells}',
             f'cell classes (top priority): {dict(class_cells.most_common())}',
             f'holes (empty cells inside the hull): {len(holes)} in {len(comps)} areas; largest {sorted((len(c) for c in comps), reverse=True)[:8]}',
             f'bare ground cells (only grass/path/alley objects): {len(bare)}',
             f'edge depth (cells between city and bounding square): left mean {mean(left):.1f} max {max(left)}, right {mean(right):.1f} max {max(right)}, '
             f'top {mean(top):.1f} max {max(top)}, bottom {mean(bottom):.1f} max {max(bottom)}',
             f'buildings: {sum(building_inst.values())} instances of {len(building_inst)} models; roofless (roof cover < {ROOF_MIN_COVER:.0%}): '
             f'{sum(roofless_inst.values())} instances of {len(roofless_inst)} models; untextured roof: {sum(untextured_roof.values())} instances',
             f'roof rule: {sum(roofable_inst.values())} object instances of {len(roofable_inst)} models get a roof; roofless buildings left: {roofless_after}',
             f'roof texture (set, id, umin, vmin, umax, vmax): {lev.roof_texture()}',
             'top roofless models: ' + ', '.join(f'{n}x{c}' for n, c in roofless_inst.most_common(12)),
             'top untextured-roof models: ' + ', '.join(f'{n}x{c}' for n, c in untextured_roof.most_common(8))]
    # picture: 2x2 pixels per cell, rows top to bottom = z
    rows = []
    for z in range(h):
        row = bytearray()
        for x in range(w):
            c = (x, z)
            if c in holes:
                col = COLORS['hole']
            elif c in kinds and (kinds[c] - {'seen'}):
                k = kinds[c]
                col = COLORS[next((p for p in PRIORITY if p in k), 'prop')]
            else:
                col = COLORS['empty']
            row += bytes(col) * 2
        rows += [bytes(row)] * 2
    write_png(out / f'{name}_occupancy.png', w * 2, h * 2, rows)
    (out / f'{name}_holes.txt').write_text('\n'.join(f'{x} {z}' for x, z in sorted(holes, key=lambda c: (c[1], c[0]))), encoding='utf-8')
    return lines


def mean(v: list[int]) -> float:
    return sum(v) / len(v) if v else 0.0


def components(cells: set) -> list[list]:
    seen, out = set(), []
    for s in cells:
        if s in seen:
            continue
        q, comp = deque([s]), []
        seen.add(s)
        while q:
            x, z = q.popleft()
            comp.append((x, z))
            for n in ((x + 1, z), (x - 1, z), (x, z + 1), (x, z - 1)):
                if n in cells and n not in seen:
                    seen.add(n)
                    q.append(n)
        out.append(comp)
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('levels', type=Path, help='DRIVER2\\LEVELS folder')
    ap.add_argument('out', type=Path, help='output folder (outside the repo)')
    ap.add_argument('cities', nargs='*', default=CITIES)
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    for city in a.cities:
        lines = analyze(Lev(a.levels / f'{city}.LEV'), city, a.out)
        (a.out / f'{city}_report.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        print('\n'.join(lines))


if __name__ == '__main__':
    main()
