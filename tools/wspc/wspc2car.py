"""WSPC .DOB vehicle -> Redriver car files (MODEL for the clean, damaged and low detail car, texture pages, info).

Output folder per car: CLEAN.MDL, DAMAGED.MDL, LOW.MDL, TEXTURE.BIN, CAR.INFO. Format notes: docs/wspc.md, docs/dev-notes/wspc-dlc.md.
TEXTURE.BIN: u32 pages, per page u32 palettes, palettes * 32 bytes (15 bit colours), 0x8000 bytes of 4 bit pixels.
"""
import argparse
import struct
from pathlib import Path

import dob2glb as dob

MODEL_HEADER = 36
POLY_FT3, POLY_FT4 = 20, 21     # Redriver polygon ids, 20 bytes each
MAX_VERTS = 256                 # polygon vertex numbers are one byte
MAX_PAGES = 2                   # a car owns two texture pages
MAX_PALETTES = 64               # palette room of the special car slot
WHEEL_SHAPE = (16, 4)           # (vertices, polygons) of a wheel box
HULL_SHAPE = (16, 8)            # (polygons, materials) of the collision / shadow box next to the body
SMALL_BOX_SHAPE = (6, 3)        # (vertices, polygons) of a second box, also left out


def classify(data: bytes, pool: int, nodes: list[dob.Node]) -> tuple[tuple[int, ...], tuple[int, ...]]:
    """Body and wheel node numbers: wheels are the 16 vertex boxes, hull boxes and the damage copies of the body are left out."""
    shapes: dict[int, tuple[int, int]] = {}
    hulls = set()
    for number, node in enumerate(nodes):
        try:
            verts, materials, prims = dob.read_mesh(data, pool, node.pool_offset)
        except struct.error:
            continue
        shapes[number] = (len(verts), len(prims))
        if (len(prims), len(materials)) == HULL_SHAPE or shapes[number] == SMALL_BOX_SHAPE:
            hulls.add(number)
    wheels = tuple(n for n, s in shapes.items() if s == WHEEL_SHAPE)
    main = max(shapes, key=lambda n: (shapes[n][1], -n))
    body = tuple(n for n, s in shapes.items() if n not in wheels and n not in hulls and (s != shapes[main] or n == main))
    return body, wheels


def bgr555(rgba: bytes) -> int:
    """PS1 15 bit colour; all zero = transparent (0), opaque black gets the semi transparency bit."""
    r, g, b, a = rgba
    if (r, g, b, a) == (0, 0, 0, 0):
        return 0
    return (r >> 3) | (g >> 3) << 5 | (b >> 3) << 10 or 0x8000


def sub(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return a[0] - b[0], a[1] - b[1], a[2] - b[2]


def cross(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]


def unit(v: tuple[float, float, float]) -> tuple[int, int, int]:
    """Direction as 4096 = 1.0."""
    length = (v[0] ** 2 + v[1] ** 2 + v[2] ** 2) ** 0.5 or 1.0
    return tuple(round(4096 * c / length) for c in v)


def collect(data: bytes, pool: int, nodes: list[dob.Node], wanted: tuple[int, ...], scale: float):
    """Merged vertices (node position added) and polygons (vertex numbers, material, colour) of the wanted nodes."""
    verts: list[tuple[int, int, int]] = []
    polys = []
    materials: list[dob.Material] = []
    for number in wanted:
        node = nodes[number]
        node_verts, node_materials, prims = dob.read_mesh(data, pool, node.pool_offset)
        first = len(verts)
        material_base = len(materials)
        materials.extend(node_materials)
        px, py, pz = node.position
        verts.extend((round((x + px) * scale), round((y + py) * scale), round((z + pz) * scale)) for x, y, z in node_verts)
        for prim in prims:
            if prim.material < len(node_materials) and all(i < len(node_verts) for i in prim.indices):
                colour = tuple(sum(c[k] for c in prim.colours) // len(prim.colours) for k in range(3))
                polys.append(([first + i for i in prim.indices], material_base + prim.material, colour))
    used = sorted({i for indices, _, _ in polys for i in indices})
    renumber = {old: new for new, old in enumerate(used)}
    polys = [([renumber[i] for i in indices], material, colour) for indices, material, colour in polys]
    return [verts[i] for i in used], polys, materials


def build_model(verts, polys, texture_of) -> bytes:
    """MODEL file: header, vertices, point normals, polygons (FT3 / FT4), face normals."""
    count = len(verts)
    poly_bytes = bytearray()
    face_normals = bytearray()
    point_sum = [(0, 0, 0)] * count
    for indices, material, colour in polys:
        page, palette, uv = texture_of(material)
        if len(indices) == 3:
            order = (0, 1, 2)
            poly_bytes += struct.pack('<4B3BB', POLY_FT3, page, palette, 0, indices[0], indices[1], indices[2], 0)
            poly_bytes += bytes(sum(([uv[k][0], uv[k][1]] for k in order), [])) + b'\0\0'
        else:
            order = (0, 1, 3, 2)   # source quads are strips, Redriver quads go round the edge
            poly_bytes += struct.pack('<4B4B', POLY_FT4, page, palette, 0, *(indices[k] for k in order))
            poly_bytes += bytes(sum(([uv[k][0], uv[k][1]] for k in order), []))
        poly_bytes += bytes((*colour, 0))
        a, b, c = (verts[i] for i in indices[:3])
        normal = cross(sub(b, a), sub(c, a))
        face_normals += struct.pack('<4h', *unit(normal), 0)
        for i in indices:
            point_sum[i] = tuple(p + n for p, n in zip(point_sum[i], normal))
    vertex_at = MODEL_HEADER
    point_at = vertex_at + 8 * count
    poly_at = point_at + 8 * count
    normals_at = poly_at + len(poly_bytes)
    radius = int(max((x * x + y * y + z * z) ** 0.5 for x, y, z in verts))
    header = struct.pack('<HHhBBhHHHiiiii', 0, 0, -1, 0, 0, radius, count, count, len(polys), vertex_at, poly_at, normals_at, point_at, 0)
    body = b''.join(struct.pack('<4h', x, y, z, 0) for x, y, z in verts)
    body += b''.join(struct.pack('<4h', *unit(n), 0) for n in point_sum)
    return header + body + bytes(poly_bytes) + bytes(face_normals)


def build_textures(data: bytes, by_tag, used: list[tuple[int, int]]):
    """Source (page, palette) pairs -> output (page, palette number); returns the lookup and the TEXTURE.BIN content."""
    pages = dob.texture_pages(data, by_tag)
    page_order = sorted({p for p, _ in used})
    if len(page_order) > MAX_PAGES:
        raise SystemExit(f'{len(page_order)} source texture pages used, a car owns {MAX_PAGES}')
    palettes: list[list[tuple[int, int]]] = [[] for _ in page_order]
    lookup: dict[tuple[int, int], tuple[int, int]] = {}
    for page, clut in used:
        out_page = page_order.index(page)
        if (page, clut) not in lookup:
            lookup[(page, clut)] = (out_page, len(palettes[out_page]))
            palettes[out_page].append((page, clut))
    if sum(len(p) for p in palettes) > MAX_PALETTES:
        raise SystemExit(f'{sum(len(p) for p in palettes)} palettes, the special car slot holds {MAX_PALETTES}')
    blob = struct.pack('<I', len(page_order))
    for out_page, page in enumerate(page_order):
        pixels, colours = pages[page]
        blob += struct.pack('<I', len(palettes[out_page]))
        for _, clut in palettes[out_page]:
            entry = colours[clut * 64:(clut + 1) * 64]
            blob += b''.join(struct.pack('<H', bgr555(entry[4 * k:4 * k + 4])) for k in range(16))
        blob += bytes(pixels[2 * i] | pixels[2 * i + 1] << 4 for i in range(0x8000))
    return lookup, blob


def convert(source: Path, out: Path, body: tuple[int, ...] | None, wheels: tuple[int, ...] | None, scale: float) -> None:
    data, by_tag, pool, nodes = dob.load(source)
    found_body, found_wheels = classify(data, pool, nodes)
    body, wheels = body or found_body, wheels or found_wheels
    print('body nodes', body, 'wheel nodes', wheels)
    verts, polys, materials = collect(data, pool, nodes, body, scale)
    if len(verts) > MAX_VERTS:
        raise SystemExit(f'{len(verts)} vertices, polygons address at most {MAX_VERTS}')
    used = sorted({(materials[m].page, materials[m].clut) for _, m, _ in polys})
    lookup, blob = build_textures(data, by_tag, used)

    def texture_of(material: int):
        m = materials[material]
        page, palette = lookup[(m.page, m.clut)]
        return page, palette, m.uv

    model = build_model(verts, polys, texture_of)
    out.mkdir(parents=True, exist_ok=True)
    for name in ('CLEAN', 'DAMAGED', 'LOW'):
        (out / f'{name}.MDL').write_bytes(model)
    (out / 'TEXTURE.BIN').write_bytes(blob)
    wheel_centres = []
    for number in wheels:
        node_verts, _, _ = dob.read_mesh(data, pool, nodes[number].pool_offset)
        px, py, pz = nodes[number].position
        xs, ys, zs = ([(v[k] + p) * scale for v in node_verts] for k, p in enumerate((px, py, pz)))
        wheel_centres.append((round((min(xs) + max(xs)) / 2), round((min(ys) + max(ys)) / 2), round((min(zs) + max(zs)) / 2), round((max(ys) - min(ys)) / 2)))
    lines = [f'vertices={len(verts)}', f'polys={len(polys)}', f'pages={len({p for p, _ in used})}', f'palettes={len(used)}',
             f'bounds={[min(v[k] for v in verts) for k in range(3)]}..{[max(v[k] for v in verts) for k in range(3)]}']
    lines += [f'wheel{i}=x{w[0]} y{w[1]} z{w[2]} radius{w[3]}' for i, w in enumerate(wheel_centres)]
    (out / 'CAR.INFO').write_text('\n'.join(lines) + '\n')
    print('\n'.join(lines))


def numbers(text: str) -> tuple[int, ...]:
    return tuple(int(n) for n in text.split(','))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dob', type=Path)
    parser.add_argument('out', type=Path)
    parser.add_argument('--body', type=numbers, help='body node numbers (default: guessed from the node shapes)')
    parser.add_argument('--wheels', type=numbers, help='wheel node numbers, only measured (default: guessed)')
    parser.add_argument('--scale', type=float, default=1.0, help='source units to Redriver units')
    args = parser.parse_args()
    convert(args.dob, args.out, args.body, args.wheels, args.scale)


if __name__ == '__main__':
    main()
