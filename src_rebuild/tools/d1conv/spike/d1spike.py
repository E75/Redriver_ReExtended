"""Driver 1 / Driver 2 LEV format spike (Part 0 of plans/driver1-cities.md).

Prints map, model, texture and road statistics of a LEV file; optionally writes a road-id image
(1 pixel = 1500 units, Driver 1 only). Reads the player's own game data, never copies it. Stdlib only.
"""
import argparse
import struct
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

SECTOR = 2048
D1_ROADMAP_UNIT = 1500
D1_REGION_ROADMAP_SIDE = 60
D1_REGION_ROADMAP_CELLS = D1_REGION_ROADMAP_SIDE * D1_REGION_ROADMAP_SIDE
LUMP_NAMES = {0: 'PERMPAGES', 1: 'MODELS', 2: 'MAP', 5: 'TEXNAMES', 7: 'ROADMAP', 8: 'ROADS', 9: 'JUNCTIONS',
              10: 'ROADSURF', 12: 'MODELNAMES', 16: 'ROADBOUNDS', 17: 'JUNCBOUNDS', 20: 'SUBDIV',
              21: 'LOWDETAIL', 22: 'MOTIONCAP', 24: 'OVERLAYMAP', 25: 'PALLET', 26: 'SPOOLINFO',
              28: 'CAR_MODELS', 33: 'CHAIR', 34: 'TEXINFO', 40: 'STRAIGHTS2', 41: 'CURVES2', 43: 'JUNC2_NEW'}
# Game/C/draw.c PolySizes, first 32 entries; 0 = unknown type
POLY_SIZES = [8, 12, 16, 24, 20, 20, 28, 32, 8, 12, 16, 16, 0, 0, 0, 0, 12, 12, 12, 16, 20, 20, 24, 24,
              0, 0, 0, 0, 0, 0, 0, 0]
MODEL_FORMAT = '<hhhBBhhhhiiiii'


class Lev:
    def __init__(self, path: Path) -> None:
        with path.open('rb') as f:
            self.data = f.read()
        lump_type, _ = self.u('<ii', 0)
        if lump_type != 37:
            raise ValueError(f'{path} is not a LEV file')
        (self.lt_ofs, self.lt_size, self.tp_ofs, self.tp_size,
         self.im_ofs, self.im_size, self.sp_ofs, self.sp_size) = self.u('<8i', 8)
        # Driver 1 sub-lump lists start with a count; Driver 2 lists start directly with TEXNAMES (5)
        self.d1 = self.u('<i', self.lt_ofs + 8)[0] != 5
        self.lumps: dict[int, list[tuple[int, int]]] = {}
        self.walk(self.lt_ofs + 8)
        self.walk(self.im_ofs + 8)

    def u(self, fmt: str, ofs: int) -> tuple:
        return struct.unpack_from(fmt, self.data, ofs)

    def walk(self, ofs: int) -> None:
        count = 255
        if self.d1:
            count = self.u('<i', ofs)[0]
            ofs += 4
        for _ in range(count):
            lump_type, size = self.u('<ii', ofs)
            if lump_type == 255:
                break
            self.lumps.setdefault(lump_type, []).append((ofs + 8, size))
            ofs = (ofs + 8 + size + 3) & ~3

    def lump_offset(self, lump_type: int) -> int:
        return self.lumps[lump_type][0][0]


@dataclass
class MapInfo:
    cells_across: int
    cells_down: int
    cell_size: int
    num_regions: int
    region_size: int
    num_cell_objects: int
    num_straddlers: int


@dataclass
class SpoolInfo:
    num_areas: int
    slots: tuple
    objects: tuple
    pvs: tuple
    offsets: tuple
    spool_base: int


@dataclass
class RegionSpool:
    offset: int
    roadm_size: int
    roadh_size: int


def map_info(lev: Lev) -> MapInfo:
    o = lev.lump_offset(2)
    v = lev.u('<7i', o)
    return MapInfo(v[0], v[1], v[2], v[3], v[4], v[5], lev.u('<i', o + 44)[0])


def spool_info(lev: Lev) -> SpoolInfo:
    p = lev.lump_offset(26)
    _, music_size = lev.u('<ii', p)
    p += 8 + music_size
    num_areas = lev.u('<i', p)[0]
    p += 4 + 32 * num_areas  # AreaDataStr (16) + tpage list (16) per area
    slots, objects, pvs = lev.u('<4i', p), lev.u('<4i', p + 16), lev.u('<4i', p + 32)
    p += 48
    num_offsets = lev.u('<i', p)[0]
    offsets = lev.u(f'<{num_offsets}H', p + 4)
    p += 4 + 2 * num_offsets + 4  # + region info size
    return SpoolInfo(num_areas, slots, objects, pvs, offsets, p)


def region_spool(lev: Lev, si: SpoolInfo, region: int) -> RegionSpool | None:
    ofs = si.offsets[region]
    if ofs == 0xFFFF:
        return None
    v = lev.u('<HBBBBBBBBBB', si.spool_base + ofs)
    return RegionSpool(v[0], v[9], v[10])


def d1_region_roadmap(lev: Lev, sp: RegionSpool) -> tuple[list[int], list[int]]:
    """Road ids and road map values of one Driver 1 region (60 x 60 cells of 1500 units)."""
    base = lev.sp_ofs + sp.offset * SECTOR
    ids: list[int] = []
    p = base
    while len(ids) < D1_REGION_ROADMAP_CELLS:
        length, value = lev.u('<hH', p)
        if length == -1:
            break
        ids += [value] * length
        p += 4
    ids += [0xFFFF] * (D1_REGION_ROADMAP_CELLS - len(ids))
    values: list[int] = []
    p = base + sp.roadm_size * SECTOR
    while len(values) < D1_REGION_ROADMAP_CELLS:
        length, value = lev.u('<II', p)
        values += [value] * length
        p += 8
    return ids[:D1_REGION_ROADMAP_CELLS], values[:D1_REGION_ROADMAP_CELLS]


def poly_types(lev: Lev, p: int, num_polys: int) -> list[int | str]:
    out: list[int | str] = []
    for _ in range(num_polys):
        poly_type = lev.data[p] & 31
        out.append(poly_type)
        if POLY_SIZES[poly_type] == 0:
            out.append('unknown')
            break
        p += POLY_SIZES[poly_type]
    return out


def report_models(lev: Lev) -> None:
    p = lev.lump_offset(1)
    count = lev.u('<i', p)[0]
    p += 4
    shape_bits, flag2_bits, types = Counter(), Counter(), Counter()
    with_collision = 0
    for _ in range(count):
        size = lev.u('<i', p)[0]
        p += 4
        if size <= 0:
            continue
        m = lev.u(MODEL_FORMAT, p)
        for bit in range(16):
            if m[0] & (1 << bit):
                shape_bits[1 << bit] += 1
            if m[1] & (1 << bit):
                flag2_bits[1 << bit] += 1
        if m[13] > 0 and lev.u('<i', p + m[13])[0] > 0:
            with_collision += 1
        types.update(poly_types(lev, p + m[10], m[8]))
        p += size
    print(f'models: {count}, with collision boxes {with_collision}')
    print(f'  shape_flags bits: {dict(sorted(shape_bits.items()))}')
    print(f'  flags2 bits: {dict(sorted(flag2_bits.items()))}')
    print(f'  poly types: {dict(sorted(types.items(), key=str))}')

    o = lev.lump_offset(28)
    entries = [lev.u('<iii', o + 4 + i * 12) for i in range(13)]
    base = o + 4 + 13 * 12
    car_types = Counter()
    present = 0
    for clean, _, _ in entries:
        if clean == -1:
            continue
        present += 1
        m = lev.u(MODEL_FORMAT, base + clean + 4)
        car_types.update(poly_types(lev, base + clean + 4 + m[10], m[8]))
    print(f'car models: count {lev.u("<i", o)[0]} present {present} '
          f'poly types {dict(sorted(car_types.items(), key=str))}')


def report_textures(lev: Lev) -> None:
    o = lev.lump_offset(34)
    pages, textures = lev.u('<ii', o)
    p = o + 8 + (pages + 1) * 8  # TEXPAGE_POS = 8 bytes
    for _ in range(pages):
        p += 4 + lev.u('<i', p)[0] * 8  # TEXINF = 8 bytes
    permanent = lev.u('<i', p)[0]
    special = lev.u('<i', p + 4 + 64)[0]
    print(f'textures: pages {pages} textures {textures} permanent {permanent} special {special}')


def report_d1_roads(lev: Lev, dump: int) -> None:
    def table(lump_type: int, fmt: str, size: int) -> list[tuple]:
        o = lev.lump_offset(lump_type)
        return [lev.u(fmt, o + 4 + i * size) for i in range(lev.u('<i', o)[0])]

    roads = table(8, '<hhhbbhbbhh', 16)
    juncs = table(9, '<hbbhbb' + 'hhhh' * 4 + 'hh', 44)
    bounds = table(16, '<iiiiB3x', 20)
    print(f'd1 roads {len(roads)} junctions {len(juncs)} roadbounds {len(bounds)}')
    print(f'  road lanes: {dict(Counter(r[3] for r in roads))} lanedirs: {dict(Counter(r[4] & 0xFF for r in roads))}')
    print(f'  road flags: {dict(Counter(r[6] for r in roads))} bounds dir: {dict(Counter(b[4] for b in bounds))}')
    print(f'  junction types: {dict(Counter(j[1] for j in juncs))}')
    widths = Counter(min(abs(b[2] - b[0]), abs(b[3] - b[1])) for b in bounds)
    print(f'  road bound narrow side (top 8): {widths.most_common(8)}')
    for r in roads[:dump]:
        print(f'  road id {r[0]} connect {r[1:3]} lanes {r[3]} dirs {r[4]} len {r[5]} flags {r[6]} x {r[8]} z {r[9]}')
    for j in juncs[:dump]:
        entries = [j[6 + k * 4:10 + k * 4] for k in range(4)]
        print(f'  junc id {j[0]} type {j[1]} timer {j[3]} NS {j[4]} WE {j[5]} entries {entries} x {j[22]} z {j[23]}')
    for b in bounds[:dump]:
        print(f'  bounds {b}')


def report_d1_roadmap(lev: Lev, m: MapInfo, si: SpoolInfo, ppm: Path | None) -> None:
    rw, rh = lev.u('<ii', lev.lump_offset(7))
    unit_x_mid, unit_z_mid = (rw + 1) * 750, rh * 750
    regions_across = m.cells_across // m.region_size
    regions_down = m.cells_down // m.region_size
    units_across_halved = m.cells_across // 2 * m.cell_size
    units_down_halved = m.cells_down // 2 * m.cell_size
    ids_hist = Counter()
    heights: list[int] = []
    img = bytearray(rw * rh * 3) if ppm else None
    cache: dict[int, tuple[list[int], list[int]] | None] = {}
    for cz in range(rh):
        for cx in range(rw):
            # inverse of OpenDriverEngine regions_d1.cpp GetRoadInfo, incl. its +750/-750 shift
            px = cx * D1_ROADMAP_UNIT - unit_x_mid + 1500
            pz = unit_z_mid - cz * D1_ROADMAP_UNIT - 750
            rx = (px + units_across_halved) // m.cell_size // m.region_size
            rz = (pz + units_down_halved) // m.cell_size // m.region_size
            if not (0 <= rx < regions_across and 0 <= rz < regions_down):
                continue
            region = rx + rz * regions_across
            if region not in cache:
                sp = region_spool(lev, si, region)
                cache[region] = d1_region_roadmap(lev, sp) if sp and sp.roadm_size else None
            if cache[region] is None:
                continue
            ids, values = cache[region]
            idx = (cx % D1_REGION_ROADMAP_SIDE) + (cz % D1_REGION_ROADMAP_SIDE) * D1_REGION_ROADMAP_SIDE
            road_id, value = ids[idx], values[idx]
            ids_hist[road_id >> 12] += 1
            heights.append(struct.unpack('<h', struct.pack('<H', value & 0xFFFF))[0])
            if img is not None:
                i = (cz * rw + cx) * 3
                if road_id >= 0x7FFF:
                    colour = b'\x00\x00\x00'
                elif road_id & 0x8000:
                    colour = b'\x30\xc0\x30'
                elif road_id & 0x4000:
                    colour = b'\xff\x30\x30'
                else:
                    colour = bytes(((road_id * 37) & 0x7F | 0x80, (road_id * 91) & 0x7F | 0x80, 0xFF))
                img[i:i + 3] = colour
    print(f'roadmap {rw}x{rh}: road id high nibble hist {dict(sorted(ids_hist.items()))}')
    print(f'  height min {min(heights)} max {max(heights)}')
    if img is not None and ppm is not None:
        with ppm.open('wb') as f:
            f.write(f'P6 {rw} {rh} 255\n'.encode('ascii'))
            f.write(bytes(img))
        print(f'  wrote {ppm}')


def report_d2_roads(lev: Lev, dump: int) -> None:
    def table(lump_type: int, fmt: str, size: int) -> list[tuple]:
        o = lev.lump_offset(lump_type)
        return [lev.u(fmt, o + 4 + i * size) for i in range(lev.u('<i', o)[0])]

    straights = table(40, '<iiHhh4hbbbb', 28)
    curves = table(41, '<iihh4hhhbbbb', 28)
    juncs = table(43, '<4hI', 12)
    print(f'd2 straights {len(straights)} curves {len(curves)} junctions {len(juncs)}')
    print(f'  straight lanes (low nibble): {dict(Counter(s[9] & 15 for s in straights))}')
    print(f'  straight length (top 6): {Counter(s[2] for s in straights).most_common(6)}')
    for i, s in enumerate(straights[:dump]):
        print(f'  straight {i} mid {s[0]},{s[1]} len {s[2]} bing {s[3]} angle {s[4]} connect {s[5:9]} '
              f'lanes 0x{s[9] & 0xFF:02x} dirs 0x{s[10] & 0xFF:02x} ai 0x{s[11] & 0xFF:02x}')
    for i, j in enumerate(juncs[:dump]):
        print(f'  junction {i} exits {j[:4]} flags 0x{j[4]:x}')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('lev', type=Path, help='LEV file (Driver 1 or Driver 2)')
    parser.add_argument('--ppm', type=Path, help='write the Driver 1 road-id map as a PPM image')
    parser.add_argument('--dump-roads', type=int, default=0, help='print the first N roads/junctions')
    args = parser.parse_args()

    lev = Lev(args.lev)
    print(f'== {args.lev} {"Driver 1" if lev.d1 else "Driver 2"}')
    print('lumps: ' + ' '.join(f'{LUMP_NAMES.get(t, t)} x{len(v)}' for t, v in sorted(lev.lumps.items())))
    m = map_info(lev)
    print(f'map: {m}')
    si = spool_info(lev)
    print(f'spool: areas {si.num_areas} slots {si.slots} objects {si.objects} pvs {si.pvs} regions {len(si.offsets)}')
    report_textures(lev)
    report_models(lev)
    if lev.d1:
        report_d1_roads(lev, args.dump_roads)
        report_d1_roadmap(lev, m, si, args.ppm)
    else:
        report_d2_roads(lev, args.dump_roads)


if __name__ == '__main__':
    main()
