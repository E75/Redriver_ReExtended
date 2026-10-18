"""Reader and constants of the flat city file (.wcity) written by city2data.py. Format: docs/wspc.md, section Export format."""
import struct
from pathlib import Path

CITY_MAGIC = b'WCTY'
GRAPH_MAGIC = b'WGPH'
VERSION = 1
HEADER = struct.Struct('<4sIIII')          # magic, version, texture count, mesh count, reserved
MESH_HEAD = struct.Struct('<H32sBBI')      # cell, name, class, reserved, triangle count
TRI = struct.Struct('<H' + 'iiiBBBBBB' * 3)  # texture id, 3 x (x, y, z, r, g, b, pad, u, v)
CLASS_GFS, CLASS_GHS, CLASS_INST, CLASS_GFX, CLASS_FYSIK, CLASS_OTHER = 0, 1, 2, 3, 4, 255
CLASSES = {'GFS': CLASS_GFS, 'GHS': CLASS_GHS, 'INST': CLASS_INST, 'GFX': CLASS_GFX}


def read_city(path: Path) -> tuple[dict[int, tuple[int, int, bytes]], list[tuple[int, str, int, bytes]]]:
    """-> ({texture id: (width, height, RGBA bytes)}, [(cell, name, class, triangle records)])."""
    data = path.read_bytes()
    magic, version, tex_count, mesh_count, _ = HEADER.unpack_from(data, 0)
    if magic != CITY_MAGIC or version != VERSION:
        raise ValueError(f'{path}: not a version {VERSION} city file')
    pos = HEADER.size
    textures = {}
    for _ in range(tex_count):
        tid, width, height = struct.unpack_from('<IHH', data, pos)
        pos += 8
        textures[tid] = (width, height, data[pos:pos + width * height * 4])
        pos += width * height * 4
    meshes = []
    for _ in range(mesh_count):
        cell, name, kind, _, count = MESH_HEAD.unpack_from(data, pos)
        pos += MESH_HEAD.size
        meshes.append((cell, name.rstrip(b'\0').decode('latin1'), kind, data[pos:pos + count * TRI.size]))
        pos += count * TRI.size
    return textures, meshes
