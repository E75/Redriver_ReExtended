"""Primitive lists of a WSPC mesh (shared by .DOB objects and city cells). Format: docs/wspc.md."""
import struct
from dataclasses import dataclass

RECORD_SIZE = {3: 24, 7: 28, 11: 24, 15: 32}   # primitive bytes per group type


@dataclass
class Prim:
    indices: list[int]
    colours: list[tuple[int, int, int]]
    material: int
    list_index: int      # 0 = first primitive list of the sub mesh, 1 = second


def read_prims(data: bytes, origin: int, mesh_at: int) -> list[Prim]:
    """All primitives of the mesh at mesh_at. Pointers inside the mesh are relative to origin
    (the mesh itself in a .DOB, the start of the node list in a city cell)."""

    def u32(at: int) -> int:
        return struct.unpack_from('<I', data, at)[0]

    prims: list[Prim] = []
    sub_at = origin + u32(mesh_at + 0xc)
    for i in range(data[mesh_at]):
        sub = sub_at + 0x14 * i
        for list_index, list_off in enumerate((0xc, 0x10)):
            pointer = u32(sub + list_off)
            if not pointer:
                continue
            head = origin + pointer
            table = origin + u32(head + 4)
            for j in range(struct.unpack_from('<h', data, head)[0]):
                entry = table + 8 * j
                kind, count = data[entry], data[entry + 3]
                per_prim = 4 if kind & 4 else 3
                size = RECORD_SIZE.get(kind, 8 + per_prim * 4 + 4)
                first = origin + u32(entry + 4)
                for k in range(count):
                    rec = first + k * size
                    # the u8 in a vertex record is only the low byte of the index: the full ones are the offsets (index * 8) in the header
                    indices = [x // 8 for x in struct.unpack_from('<3H', data, rec + 2)]
                    colours = [tuple(data[rec + 8 + 4 * v:rec + 11 + 4 * v]) for v in range(per_prim)]
                    if per_prim == 4:
                        indices.append(struct.unpack_from('<H', data, rec + 24)[0] // 8)
                    # quads repeat their 4th vertex (offset * 8) first, so the material offset is the second u16
                    mat_at = rec + 8 + per_prim * 4 + (2 if per_prim == 4 else 0)
                    prims.append(Prim(indices, colours, struct.unpack_from('<H', data, mat_at)[0] // 16, list_index))
    return prims
