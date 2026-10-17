"""D1 ground height oracle (python port of D1HeightField::Eval) against the world heights of the tile / path / grass
model vertices of all cell objects: prints the share of vertices within 30 units per kind. Usage: d1_mesh_check.py <D1.LEV>.
Expect ~99% for path and flat tiles; a low value means the oracle (sign, slope rotation) is wrong. Reads the player's own data."""
import math
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, r'<parent folder>/Redriver2_Extended\src_rebuild\tools\d1conv\spike')
import d1spike as s  # noqa: E402

K = 1500
SG, SA, SD = -1, -1, 1  # y = -SG * height; slope term SA * P + SD * d (see d1_height.cpp)


def floor_mod(a: int, b: int) -> int:
    return a % b


def point_in_quad(tx, ty, v) -> bool:
    for i in range(4):
        p, q = v[i], v[(i + 1) & 3]
        if (tx - p[0]) * (q[1] - p[1]) - (ty - p[1]) * (q[0] - p[0]) < 0:
            return False
    return True


def point_in_tri(tx, ty, v) -> bool:
    if (v[1][1] - v[0][1]) * (tx - v[0][0]) - (v[1][0] - v[0][0]) * (ty - v[0][1]) < 0:
        return False
    if (v[2][1] - v[1][1]) * (tx - v[1][0]) - (v[2][0] - v[1][0]) * (ty - v[1][1]) >= 0:
        return (v[0][1] - v[2][1]) * (tx - v[2][0]) - (v[0][0] - v[2][0]) * (ty - v[2][1]) >= 0
    return False


def main() -> None:
    lev = s.Lev(Path(sys.argv[1]))
    rot_sign = 1
    m = s.map_info(lev)
    si = s.spool_info(lev)

    # surfaces
    surfaces: dict[int, list] = {}
    o, size = lev.lumps[10][0]
    count = lev.u('<i', o)[0]
    p = o + 4
    for _ in range(count):
        typ, info, heading, npoly = lev.u('<hhhh', p)
        p += 8
        polys = []
        for _ in range(npoly):
            xz = [lev.u('<ii', p + k * 8) for k in range(4)]
            a, b, c, d = lev.u('<4i', p + 32)
            nv = lev.u('<h', p + 48)[0]
            polys.append((xz, a, b, c, d, nv))
            p += 52
        surfaces[typ] = polys

    rw, rh = lev.u('<ii', lev.lump_offset(7))
    unit_x_mid, unit_z_mid = (rw + 1) * 750, rh * 750
    regions_across = m.cells_across // m.region_size
    regions_down = m.cells_down // m.region_size
    uah = m.cells_across // 2 * m.cell_size
    udh = m.cells_down // 2 * m.cell_size
    vals: dict[tuple[int, int], int] = {}
    cache = {}
    for cz in range(rh):
        for cx in range(rw):
            px = cx * K - unit_x_mid + 1500
            pz = unit_z_mid - cz * K - 750
            rx = (px + uah) // m.cell_size // m.region_size
            rz = (pz + udh) // m.cell_size // m.region_size
            if not (0 <= rx < regions_across and 0 <= rz < regions_down):
                continue
            region = rx + rz * regions_across
            if region not in cache:
                sp = s.region_spool(lev, si, region)
                cache[region] = s.d1_region_roadmap(lev, sp) if sp and sp.roadm_size else None
            if cache[region] is None:
                continue
            _, values = cache[region]
            idx = (cx % 60) + (cz % 60) * 60
            vals[cx, cz] = values[idx]

    dbg = []

    def ground(vx: int, vz: int):
        cx = (unit_x_mid + vx) // K
        cz = (unit_z_mid - (vz - 750)) // K
        if (cx, cz) not in vals:
            return None
        v = vals[cx, cz]
        height = struct.unpack('<h', struct.pack('<H', v & 0xFFFF))[0]
        typ = (v >> 16) & 1023
        angle = (v >> 30) * 1024
        y = -SG * height
        dbg.append((typ, height, angle, bool(surfaces.get(typ))))
        if typ < 900 and surfaces.get(typ):
            py = (unit_x_mid + vx) % K - 750
            px = (unit_z_mid - (vz - 750)) % K - 750
            if (angle & 0x400) == 0:
                px, py = py, -px
            if angle & 0x800:
                px, py = -px, -py
            for xz, a, b, c, d, nv in surfaces[typ]:
                inside = point_in_quad(px, py, xz) if nv == 4 else point_in_tri(px, py, xz)
                if inside:
                    y = SA * int((px * a + py * c) / 4096) + SD * d - SG * height
        return y

    # model vertices of ground-like models
    models: dict[int, tuple] = {}

    def add(num: int, mb: int) -> None:
        mm = lev.u(s.MODEL_FORMAT, mb)
        if mm[2] != -1 or not 0 < mm[7] < 400:
            return
        if not (mm[0] & 0x400 or mm[1] & 0x8000 or mm[1] & 0x4000):
            return
        verts = [struct.unpack_from('<hhhh', lev.data, mb + mm[9] + v * 8)[:3] for v in range(mm[7])]
        models[num] = (mm[0], mm[1], verts)

    o = lev.lump_offset(1)
    n = lev.u('<i', o)[0]
    p = o + 4
    for i in range(n):
        sz = lev.u('<i', p)[0]
        if sz >= 36:
            add(i, p + 4)
        p += 4 + sz
    q = lev.lump_offset(26)
    _, ms = lev.u('<ii', q)
    q += 8 + ms
    na = lev.u('<i', q)[0]
    q += 4
    for a in range(na):
        f = lev.u('<HHHHBBBBBBBB', q + a * 16)
        mdl, msize, ntp = f[1], f[4], f[6]
        if msize == 0 or ntp > 16 or lev.sp_ofs + (mdl + msize) * 2048 > len(lev.data):
            continue
        base = lev.sp_ofs + mdl * 2048
        cnt = lev.u('<H', base + (msize - 1) * 2048)[0]
        nums = lev.u(f'<{cnt}H', base + (msize - 1) * 2048 + 2)
        ofs = 0
        for k in range(cnt):
            sz = lev.u('<i', base + ofs)[0]
            add(nums[k], base + ofs + 4)
            ofs += 4 + sz

    # objects per region, compare vertex heights with the oracle
    stats: dict[str, Counter] = {}
    examples = []
    for region in range(m.num_regions):
        ofs = si.offsets[region]
        if ofs == 0xFFFF:
            continue
        v = lev.u('<HBBBBBBBBBB', si.spool_base + ofs)
        if v[6] == 0:
            continue
        obj = lev.sp_ofs + v[0] * 2048 + (v[9] + v[10] + v[5] + v[4]) * 2048
        for i in range(v[6] * 2048 // 16):
            vx, vy, vz, pad, yang, typ = lev.u('<iiiBBH', obj + i * 16)
            if typ not in models or abs(vx) > uah or abs(vz) > udh:
                continue
            shape, flags2, verts = models[typ]
            kind = 'path' if flags2 & 0x8000 else 'grass' if flags2 & 0x4000 else 'tile'
            th = yang * 64 / 4096 * 2 * math.pi
            cs, sn = math.cos(th), math.sin(th)
            top = min(vv[1] for vv in verts)
            for vv in verts:
                if vv[1] > top + 40 and kind != 'tile':
                    continue
                wx = vx + vv[0] * cs + rot_sign * vv[2] * sn
                wz = vz - rot_sign * vv[0] * sn + vv[2] * cs
                g = ground(int(round(wx)), int(round(wz)))
                if g is None:
                    continue
                d = vy + vv[1] - g
                bucket = '<=-30 above' if d < -30 else 'ok' if d <= 30 else '>30 below'
                stats.setdefault(kind, Counter())[bucket] += 1
                if bucket != 'ok' and len(examples) < 14 and kind == 'tile':
                    examples.append((region, typ, vx, vy, vz, vv, g, d, dbg[-1]))
    for kind, c in stats.items():
        t = sum(c.values())
        print(kind, t, {k: f'{100 * v / t:.1f}%' for k, v in c.items()})


if __name__ == '__main__':
    main()
