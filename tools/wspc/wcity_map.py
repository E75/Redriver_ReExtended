"""Top-down map of the WSPC city (minimap source) from city.wcity / graph.wgph made by city2data.py.

usage: wcity_map.py <data folder> <out.png> [--units-per-pixel 512] [--buildings] [--graph]
Roads and ground only by default (mesh class GFS, faces that look up); --buildings adds GHS/INST roofs,
--graph draws the road graph on top. The world bounds are printed so the image can be placed in the game.
"""
import argparse
import struct
from pathlib import Path

from dob2glb import png
from wcity_io import CLASS_GFS, CLASS_GHS, CLASS_INST, HEADER, TRI, read_city

BACKGROUND = (16, 16, 20, 255)


def average_colours(textures: dict[int, tuple[int, int, bytes]]) -> dict[int, tuple[int, int, int]]:
    """Mean colour of the opaque texels of every texture."""
    out = {}
    for tid, (width, height, rgba) in textures.items():
        px = [rgba[i:i + 4] for i in range(0, len(rgba), 4) if rgba[i + 3]]
        n = max(len(px), 1)
        out[tid] = (sum(p[0] for p in px) // n, sum(p[1] for p in px) // n, sum(p[2] for p in px) // n)
    return out


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('data', type=Path)
    parser.add_argument('png', type=Path)
    parser.add_argument('--units-per-pixel', type=int, default=512)
    parser.add_argument('--buildings', action='store_true')
    parser.add_argument('--graph', action='store_true')
    args = parser.parse_args()
    textures, meshes = read_city(args.data / 'city.wcity')
    colours = average_colours(textures)
    classes = {CLASS_GFS} | ({CLASS_GHS, CLASS_INST} if args.buildings else set())
    tris = []   # (height, [(x, z)] * 3, colour)
    for _, _, kind, tri_bytes in meshes:
        if kind not in classes:
            continue
        for rec in TRI.iter_unpack(tri_bytes):
            tid = rec[0]
            pts = [rec[1 + 9 * k:10 + 9 * k] for k in range(3)]
            (ax, ay, az), (bx, by, bz), (cx, cy, cz) = (p[:3] for p in pts)
            nx = (by - ay) * (cz - az) - (bz - az) * (cy - ay)
            ny = (bz - az) * (cx - ax) - (bx - ax) * (cz - az)
            nz = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax)
            if abs(ny) < 0.5 * (nx * nx + ny * ny + nz * nz) ** 0.5:   # only faces that look up or down
                continue
            base = colours.get(tid, (128, 128, 128))
            colour = tuple(min(255, int(c * sum(p[3 + k] for p in pts) / 384.0)) for k, c in enumerate(base))
            tris.append(((ay + by + cy) / 3, [(ax, az), (bx, bz), (cx, cz)], colour))
    xs = [p[0] for _, tri, _ in tris for p in tri]
    zs = [p[1] for _, tri, _ in tris for p in tri]
    x0, z0 = min(xs), min(zs)
    scale = 1.0 / args.units_per_pixel
    width, height = int((max(xs) - x0) * scale) + 1, int((max(zs) - z0) * scale) + 1
    print('world bounds x', x0, max(xs), 'z', z0, max(zs), 'image', width, 'x', height)
    img = bytearray(bytes(BACKGROUND) * width * height)
    tris.sort(key=lambda t: -t[0])   # y is down: lowest first, higher faces overwrite
    # limit: pure Python triangle fill (about 25 s for the whole city at 512 units per pixel), upgrade: numpy if finer maps are needed
    for _, tri, colour in tris:
        pts = [((x - x0) * scale, (z - z0) * scale) for x, z in tri]
        min_x, max_x = int(min(p[0] for p in pts)), int(max(p[0] for p in pts))
        min_y, max_y = int(min(p[1] for p in pts)), int(max(p[1] for p in pts))
        (ax, ay), (bx, by), (cx, cy) = pts
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        for py in range(min_y, max_y + 1):
            for px in range(min_x, max_x + 1):
                if den == 0 or (min_x == max_x and min_y == max_y):
                    inside = True
                else:
                    w1 = ((by - cy) * (px + 0.5 - cx) + (cx - bx) * (py + 0.5 - cy)) / den
                    w2 = ((cy - ay) * (px + 0.5 - cx) + (ax - cx) * (py + 0.5 - cy)) / den
                    inside = w1 >= 0 and w2 >= 0 and w1 + w2 <= 1
                if inside and 0 <= px < width and 0 <= py < height:
                    at = (py * width + px) * 4
                    img[at:at + 3] = bytes(colour)
    if args.graph:
        draw_graph(img, width, height, args.data / 'graph.wgph', x0, z0, scale)
    args.png.write_bytes(png(width, height, bytes(img)))
    print('wrote', args.png)


def draw_graph(img: bytearray, width: int, height: int, path: Path, x0: int, z0: int, scale: float) -> None:
    data = path.read_bytes()
    count = struct.unpack_from('<I', data, 8)[0]
    nodes = [struct.unpack_from('<8h', data, HEADER.size + 26 * i) for i in range(count)]   # x, z, 2 bytes, 1 word, 4 links
    for i, node in enumerate(nodes):
        for link in node[4:8]:
            if 0 <= link < count:
                ax, ay = (node[0] * 64 - x0) * scale, (node[1] * 64 - z0) * scale
                bx, by = (nodes[link][0] * 64 - x0) * scale, (nodes[link][1] * 64 - z0) * scale
                steps = int(max(abs(bx - ax), abs(by - ay))) + 1
                for s in range(steps + 1):
                    px, py = int(ax + (bx - ax) * s / steps), int(ay + (by - ay) * s / steps)
                    if 0 <= px < width and 0 <= py < height:
                        img[(py * width + px) * 4:(py * width + px) * 4 + 3] = bytes((255, 220, 0))


if __name__ == '__main__':
    main()
