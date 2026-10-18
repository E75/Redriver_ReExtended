"""WSPC city + road graph -> flat binary files for the Redriver DLC converter.

usage: city2data.py <game folder> <out folder> [--time DAY]
Writes in <out folder> (all little endian, format in docs/wspc.md, section Export format):
  city.wcity   textures + triangles of every city mesh, absolute game units
  graph.wgph   road graph (GAME.GPH nodes)
  textures/<id>.png  every texture (only for looking at them)
"""
import argparse
import struct
from pathlib import Path

from dob2glb import png
from wcity_io import CITY_MAGIC, CLASS_FYSIK, CLASS_OTHER, CLASSES, GRAPH_MAGIC, HEADER, MESH_HEAD, TRI, VERSION
from wspc_city import City, Texture, level_file

GRAPH_NODE = struct.Struct('<hhBBhhhhhBBHhhBB')   # x, z (<< 6 = game units), 2 bytes, 4 links, 2 bytes, flags, 2 words, 2 bytes


def mesh_class(name: str) -> int:
    if 'FYSIK' in name:
        return CLASS_FYSIK
    return CLASSES.get(name.split('_')[0].rstrip('0123456789'), CLASS_OTHER)


def tri_record(tid: int, corners: list[tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int]]]) -> bytes:
    values: list[int] = [tid]
    for (x, y, z), (r, g, b), (u, v) in corners:
        values += (x, y, z, r, g, b, 0, u, v)
    return TRI.pack(*values)


def write_city(city: City, out: Path) -> None:
    textures: dict[int, Texture] = {}
    meshes: list[bytes] = []
    for index in range(len(city.cell_table)):
        cell = city.read_cell(index)
        textures.update(cell.textures)
        for node in city.nodes(cell):
            if node.mesh_offset < 0:
                continue
            mesh = city.mesh(cell, node.mesh_offset)
            tris = bytearray()
            count = 0
            px, py, pz = node.position
            for prim in mesh.prims:
                if prim.material >= len(mesh.materials) or any(i >= len(mesh.verts) for i in prim.indices):
                    continue
                material = mesh.materials[prim.material]
                if material.texture not in cell.textures:
                    continue
                corners = [((mesh.verts[i][0] + px, mesh.verts[i][1] + py, mesh.verts[i][2] + pz), prim.colours[k], material.uv[k])
                           for k, i in enumerate(prim.indices)]
                for tri in ([(0, 1, 2)] if len(corners) == 3 else [(0, 1, 2), (1, 3, 2)]):
                    tris += tri_record(material.texture, [corners[k] for k in tri])
                    count += 1
            head = MESH_HEAD.pack(index, node.name.encode('latin1')[:32], mesh_class(node.name), 0, count)
            meshes.append(head + bytes(tris))
    blob = bytearray(HEADER.pack(CITY_MAGIC, VERSION, len(textures), len(meshes), 0))
    (out / 'textures').mkdir(parents=True, exist_ok=True)
    for tid, tex in sorted(textures.items()):
        rgba = b''.join(bytes(tex.palette[i]) for i in tex.pixels)
        blob += struct.pack('<IHH', tid, tex.width, tex.height) + rgba
        colours = [bytes(c[:3]) + bytes([0 if c == (0, 0, 0, 0) else 255]) for c in tex.palette]
        (out / 'textures' / f'{tid}.png').write_bytes(png(tex.width, tex.height, b''.join(colours[i] for i in tex.pixels)))
    for mesh in meshes:
        blob += mesh
    (out / 'city.wcity').write_bytes(blob)
    print(len(textures), 'textures,', len(meshes), 'meshes')


def write_graph(game: Path, time: str, out: Path) -> None:
    data = level_file(game, time, 'GAME.GPH')
    count = struct.unpack_from('<h', data, 4)[0]
    body = data[8:]
    nodes = body[:count * GRAPH_NODE.size]
    tail = (count * GRAPH_NODE.size + 3) & ~3
    list_count = struct.unpack_from('<h', body, tail)[0]
    offsets = struct.unpack_from(f'<{list_count}i', body, tail + 4)
    lists_at = tail + 4 + 4 * list_count
    blob = bytearray(GRAPH_MAGIC + struct.pack('<IIII', VERSION, count, list_count, len(body) - lists_at))
    blob += nodes + struct.pack(f'<{list_count}I', *offsets) + body[lists_at:]
    (out / 'graph.wgph').write_bytes(blob)
    print(count, 'graph nodes,', list_count, 'lists')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('out', type=Path)
    parser.add_argument('--time', default='DAY', choices=['DAY', 'DAWN', 'NIGHT', 'RAIN'])
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    city = City(args.game, args.time)
    try:
        write_city(city, args.out)
    finally:
        city.close()
    write_graph(args.game, args.time, args.out)


if __name__ == '__main__':
    main()
