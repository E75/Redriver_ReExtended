"""Driver 2 LEV reader for the gap-fill analysis (read only, stdlib only).

Reads the map header, the region spool table, the cell grid (cell pointers, cell data, packed cell
objects, straddlers) and the models (permanent pack + per area models) of a Driver 2 retail LEV.
The player's own game data is read, nothing is copied or written.
"""
import math
import struct
from dataclasses import dataclass, field
from pathlib import Path

SECTOR = 2048
# vertex index positions inside a polygon record by (type & 31); None = unknown layout
POLY_SIZES = [8, 12, 16, 24, 20, 20, 28, 32, 8, 12, 16, 16, 0, 0, 0, 0, 12, 12, 12, 16, 20, 20, 24, 24,
              0, 0, 0, 0, 0, 0, 0, 0]
POLY_VERTS = {21: (4, 5, 6, 7), 23: (4, 5, 6, 7), 0: (1, 2, 3), 1: (4, 5, 6, 7), 2: (4, 5, 6), 3: (4, 5, 6, 7), 4: (4, 5, 6), 5: (4, 5, 6, 7),
              6: (4, 5, 6), 7: (4, 5, 6, 7), 8: (1, 2, 3), 9: (4, 5, 6, 7), 10: (4, 5, 6), 11: (4, 5, 6, 7)}
POLY_TEXTURED = {2, 3, 6, 7, 10, 11, 21, 23}

SHAPE_TILE, SHAPE_ROAD, SHAPE_WATER = 0x400, 0x2000, 0x80
# roof rule, mirrored in Game/C/gapfill.c: models excluded by flags, size window, ring limits (model units)
EXCLUDED_SHAPE = 0x4000 | 0x2000 | 0x400 | 0x80 | 0x8 | 0x1000      # sprite, road, tile, water, transparent, alpha
EXCLUDED_FLAGS2 = 0x1 | 0x20 | 0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x4000 | 0x8000
ROOF_MIN_SIDE, ROOF_MAX_SIDE, ROOF_MIN_HEIGHT, ROOF_MAX_HEIGHT, ROOF_MAX_RING, TOP_TOL = 256, 6000, 700, 12000, 8, 8
ROOF_MIN_FILL = 0.3   # ring area / bounding box area: rejects facade plates whose top is a line
F2_ALLEY, F2_TREE, F2_GRASS, F2_PATH, F2_JUNC = 0x80, 0x2000, 0x4000, 0x8000, 0x40


@dataclass
class Model:
    index: int
    name: str
    shape: int
    flags2: int
    num_verts: int
    num_polys: int
    bbox: tuple          # xmin, ymin, zmin, xmax, ymax, zmax (y negative = up)
    roof_area: float = 0.0
    roof_textured: float = 0.0
    top_polys: int = 0
    kind: str = ''
    roof_tex: list = field(default_factory=list)   # (texture set, texture id, uv bytes, area) of textured top polygons
    instance: int = -1
    ring: list = field(default_factory=list)       # top vertex ring (x, z) in angle order when the model can get a roof
    has_top_poly: bool = False                     # a drawn polygon already lies in the top plane


@dataclass
class Obj:
    x: int
    y: int
    z: int
    yang: int
    model: int
    straddler: bool = False


class Lev:
    def __init__(self, path: Path) -> None:
        self.path = path
        self.data = path.read_bytes()
        if self.u('<i', 0)[0] != 37:
            raise ValueError('not a LEV file')
        (self.lt_ofs, self.lt_size, self.tp_ofs, self.tp_size,
         self.im_ofs, self.im_size, self.sp_ofs, self.sp_size) = self.u('<8i', 8)
        if self.u('<i', self.lt_ofs + 8)[0] != 5:
            raise ValueError('Driver 1 LEV: use the D1 tools')
        self.lumps = {}
        self._walk(self.lt_ofs + 8)
        self._walk(self.im_ofs + 8)
        self._read_map()
        self._read_spool()
        self._read_models()

    def u(self, fmt, ofs):
        return struct.unpack_from(fmt, self.data, ofs)

    def _walk(self, ofs):
        for _ in range(255):
            t, size = self.u('<ii', ofs)
            if t == 255:
                break
            self.lumps.setdefault(t, []).append((ofs + 8, size))
            ofs = (ofs + 8 + size + 3) & ~3

    def lump(self, t):
        return self.lumps[t][0]

    def _read_map(self):
        o, _ = self.lump(2)
        (self.cells_across, self.cells_down, self.cell_size, self.num_regions, self.region_size,
         self.num_cell_objects, self.num_cell_data) = self.u('<7i', o)
        self.num_straddlers = self.u('<i', o + 44)[0]
        self.straddlers = [self._pco(o + 48 + 8 * i) for i in range(self.num_straddlers)]
        self.regions_across = self.cells_across // self.region_size
        self.regions_down = self.cells_down // self.region_size

    def _pco(self, ofs):
        vx, vy, vz, value = self.u('<HHHH', ofs)
        return vx, vy, vz, value

    def _read_spool(self):
        p, _ = self.lump(26)
        _, music_size = self.u('<ii', p)
        p += 8 + music_size
        self.num_areas = self.u('<i', p)[0]
        p += 4
        self.areas = [self.u('<HHHHBBBBBBBB', p + 16 * i) for i in range(self.num_areas)]  # AreaDataStr
        p += 32 * self.num_areas
        self.slot_counts, self.object_counts = self.u('<4i', p), self.u('<4i', p + 16)
        self.objects_add = [sum(self.object_counts[:i]) for i in range(4)]
        p += 48
        n = self.u('<i', p)[0]
        self.offsets = self.u(f'<{n}H', p + 4)
        p += 4 + 2 * n
        self.spool_base = p + 4

    def region_spool(self, region):
        ofs = self.offsets[region]
        if ofs == 0xFFFF:
            return None
        v = self.u('<HBBBBBBBBBB', self.spool_base + ofs)
        return dict(offset=v[0], pvs=v[3], cds=v[4:7], super=v[7], roadm=v[9], roadh=v[10])

    def read_region(self, rx, rz):
        """Returns {local cell number: [Obj]} of one region, or None if the region is not in the LEV."""
        sp = self.region_spool(rx + rz * self.regions_across)
        if sp is None:
            return None
        base = self.sp_ofs + sp['offset'] * SECTOR
        n_ptr, n_cell, n_obj = sp['cds'][1], sp['cds'][0], sp['cds'][2]
        d = self.data
        pk = base
        packtype = self.u('<i', pk + 4)[0]
        ptrs = [0xFFFF] * 1024
        if packtype == 1:
            ptrs = list(self.u('<1024H', pk + 8))
        elif packtype == 2:
            q = pk + 8
            bit, code = 0x8000, self.u('<H', q)[0]
            q += 2
            for i in range(1024):
                if code & bit:
                    ptrs[i] = self.u('<H', q)[0]
                    q += 2
                bit >>= 1
                if bit == 0:
                    bit = 0x8000
                    code = self.u('<H', q)[0]
                    q += 2
        cell_ofs = base + n_ptr * SECTOR
        obj_ofs = cell_ofs + n_cell * SECTOR
        n_cells_u16 = n_cell * SECTOR // 2
        cells = self.u(f'<{n_cells_u16}H', cell_ofs)
        barrel = (rx & 1) + (rz & 1) * 2
        add = self.objects_add[barrel]
        n_pco = n_obj * SECTOR // 8
        half = self.cells_across // 2
        out = {}
        for i in range(1024):
            c = ptrs[i]
            if c == 0xFFFF:
                continue
            cx = rx * self.region_size + i % self.region_size
            cz = rz * self.region_size + i // self.region_size
            near_x = (cx - half) * self.cell_size
            near_z = (cz - self.cells_down // 2) * self.cell_size
            lst = []
            while c < n_cells_u16:
                e = cells[c]
                if e & 0x4000:
                    c += 1
                    e = cells[c]
                idx = e & 16383
                if idx >= self.num_straddlers:
                    k = idx - add - self.num_straddlers
                    pco = self._pco(obj_ofs + 8 * k) if 0 <= k < n_pco else None
                    strad = False
                else:
                    pco = self.straddlers[idx]
                    strad = True
                if pco is not None and not (pco[3] == 0xFFFF and pco[1] & 1):
                    vx, vy, vz, value = pco
                    x = near_x + _s16(vx - near_x)
                    z = near_z + _s16(vz - near_z)
                    y = _s16(vy) >> 1
                    lst.append(Obj(x, y, z, value & 0x3F, (value >> 6) | ((vy & 1) << 10), strad))
                if e & 0x8000:
                    break
                c += 1
            out[i] = lst
        return out, sp['super']

    # ---- models -------------------------------------------------------------------------------
    def _read_models(self):
        o, size = self.lump(12)
        raw = self.data[o:o + size]
        self.model_names = [s.decode('ascii', 'replace') for s in raw.split(b'\0')]
        o, _ = self.lump(1)
        count = self.u('<i', o)[0]
        p = o + 4
        self.perm = {}
        for i in range(count):
            sz = self.u('<i', p)[0]
            p += 4
            if sz > 0:
                self.perm[i] = self._parse_model(i, p)
                p += sz
        self.area_models = {}

    def permanent_pages(self):
        """Number of permanent texture pages: texture sets below it are loaded in every area."""
        o, _ = self.lump(34)
        pages = self.u('<i', o)[0]
        p = o + 8 + (pages + 1) * 8
        for _ in range(pages):
            p += 4 + self.u('<i', p)[0] * 8
        return self.u('<i', p)[0]

    def roof_texture(self):
        """(texture set, texture id, umin, vmin, umax, vmax) of the widest pavement polygon on a permanent page."""
        perm = self.permanent_pages()
        best = None
        for m in self.perm.values():
            if m.flags2 & F2_PATH:
                for ts, ti, uv, area in m.roof_tex:
                    if ts < perm and (best is None or area > best[0]):
                        best = (area, ts, ti, min(uv[0::2]), min(uv[1::2]), max(uv[0::2]), max(uv[1::2]))
        return best[1:] if best else None

    def models_of_area(self, area):
        if area in self.area_models:
            return self.area_models[area]
        res = {}
        if area != 255 and area < self.num_areas:
            a = self.areas[area]
            model_offset, model_size = a[1], a[4]
            end = self.sp_ofs + (model_offset + model_size - 1) * SECTOR
            if model_size and end + 2 < len(self.data):
                n = self.u('<H', end)[0]
                idxs = self.u(f'<{n}H', end + 2)
                p = self.sp_ofs + model_offset * SECTOR
                for i in range(n):
                    sz = self.u('<i', p)[0]
                    p += 4
                    if sz <= 0 or p + sz > end:   # damaged table (some Vegas areas): keep what was read
                        break
                    res[idxs[i]] = self._parse_model(idxs[i], p)
                    p += sz
        self.area_models[area] = res
        return res

    def model(self, area, idx):
        return self.models_of_area(area).get(idx) or self.perm.get(idx)

    def _parse_model(self, idx, p):
        (shape, flags2, _inst, _tri, _zb, _bs, _npn, nv, npoly, voff, poff, _no, _pn, _cb) = self.u('<hhhBBhhhhiiiii', p)
        name = self.model_names[idx] if idx < len(self.model_names) else ''
        shape &= 0xFFFF
        flags2 &= 0xFFFF
        verts = [self.u('<hhh', p + voff + 8 * i) for i in range(nv)] if 0 < nv < 4000 else []
        if verts:
            xs, ys, zs = zip(*verts)
            bbox = (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))
        else:
            bbox = (0, 0, 0, 0, 0, 0)
        m = Model(idx, name, shape, flags2, nv, npoly, bbox, instance=_inst)
        self._roof_stats(m, p, poff, verts)
        m.kind = classify(m)
        return m

    def _roof_stats(self, m, p, poff, verts):
        if not verts:
            return
        ymin, ymax = m.bbox[1], m.bbox[4]
        height = ymax - ymin
        limit = ymin + max(40, height * 0.15)
        q = p + poff
        for _ in range(m.num_polys):
            t = self.data[q] & 31
            size = POLY_SIZES[t]
            if size == 0:
                break
            ix = POLY_VERTS.get(t)
            if ix:
                vs = [verts[self.data[q + k]] for k in ix if self.data[q + k] < len(verts)]
                if len(vs) == len(ix) and all(v[1] <= limit for v in vs):
                    a = _up_area(vs)
                    if a > 0:
                        m.roof_area += a
                        m.top_polys += 1
                        if t in POLY_TEXTURED:
                            m.roof_textured += a
                            m.roof_tex.append((self.data[q + 1], self.data[q + 2], tuple(self.data[q + 8:q + 16]), a))
            if t in (11, 21, 23) and ix and all(self.data[q + k] < len(verts) and verts[self.data[q + k]][1] <= ymin + TOP_TOL for k in ix):
                m.has_top_poly = True
            q += size
        m.ring = roof_ring(m, verts)


def roof_ring(m, verts):
    """Top vertex ring of a model that qualifies for a roof (same rule as Game/C/gapfill.c), else []."""
    if m.instance != -1 or m.num_verts > 255 or m.has_top_poly:
        return []
    if m.shape & EXCLUDED_SHAPE or m.flags2 & EXCLUDED_FLAGS2:
        return []
    w, h, d = m.bbox[3] - m.bbox[0], m.bbox[4] - m.bbox[1], m.bbox[5] - m.bbox[2]
    if not (ROOF_MIN_SIDE <= w <= ROOF_MAX_SIDE and ROOF_MIN_SIDE <= d <= ROOF_MAX_SIDE and ROOF_MIN_HEIGHT <= h <= ROOF_MAX_HEIGHT):
        return []
    pts = sorted({(v[0], v[2]) for v in verts if v[1] <= m.bbox[1] + TOP_TOL})
    if not 3 <= len(pts) <= ROOF_MAX_RING:
        return []
    cx = sum(p[0] for p in pts) / len(pts)
    cz = sum(p[1] for p in pts) / len(pts)
    pts.sort(key=lambda p: math.atan2(p[1] - cz, p[0] - cx))
    sign = 0
    for i in range(len(pts)):
        a, b, c = pts[i], pts[(i + 1) % len(pts)], pts[(i + 2) % len(pts)]
        cross = (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0])
        if cross:
            if sign and (cross > 0) != (sign > 0):
                return []   # not convex: a fan would cover the notch
            sign = 1 if cross > 0 else -1
    if not sign:
        return []
    area = abs(sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))) / 2
    return pts if area >= ROOF_MIN_FILL * w * d else []


def _s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def _up_area(vs):
    """Area of a polygon (triangle or quad as two triangles) if it faces up (-y), else 0."""
    total = 0.0
    for a, b, c in ([(0, 1, 2)] if len(vs) == 3 else [(0, 1, 2), (1, 3, 2)]):
        ux, uy, uz = (vs[b][i] - vs[a][i] for i in range(3))
        wx, wy, wz = (vs[c][i] - vs[a][i] for i in range(3))
        nx, ny, nz = uy * wz - uz * wy, uz * wx - ux * wz, ux * wy - uy * wx
        ln = (nx * nx + ny * ny + nz * nz) ** 0.5
        if ln > 0 and abs(ny) / ln > 0.9:  # faces up or down; both count as a closed top (winding unknown)
            total += ln / 2
    return total


def classify(m):
    """Model class: road, junction, grass, path, alley, water, tree, building, prop."""
    if m.shape & SHAPE_WATER:
        return 'water'
    if m.flags2 & F2_GRASS:
        return 'grass'
    if m.flags2 & F2_PATH:
        return 'path'
    if m.flags2 & F2_ALLEY:
        return 'alley'
    if m.flags2 & F2_JUNC:
        return 'junction'
    if m.shape & (SHAPE_ROAD | SHAPE_TILE):
        return 'road'
    if m.flags2 & F2_TREE:
        return 'tree'
    w = m.bbox[3] - m.bbox[0]
    d = m.bbox[5] - m.bbox[2]
    h = m.bbox[4] - m.bbox[1]
    if h >= 500 and w * d >= 600 * 600:
        return 'building'
    return 'prop'
