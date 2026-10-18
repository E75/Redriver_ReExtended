"""WSPC city streaming data (CITY.HDR + DAY/DAWN/NIGHT/RAIN.STR) reader.

Format notes: docs/wspc.md (section City). Used by city2glb.py.
"""
import struct
from dataclasses import dataclass, field
from pathlib import Path

from wspc_extract import mrg_files, read_entry, toc_entries
from wspc_prims import Prim, read_prims

SECTOR = 0x800


@dataclass
class Material:
    page: int
    clut: int
    uv: list[tuple[int, int]]
    texture: int = -1    # global texture id, -1 when the lookup failed


@dataclass
class Mesh:
    verts: list[tuple[int, int, int]]
    materials: list[Material]
    prims: list[Prim]


@dataclass
class Node:
    kind: int
    position: tuple[int, int, int]
    rotation: tuple[int, int, int]
    scale: int
    name: str
    mesh_offset: int     # -1 when the node has no mesh (group node)
    parent: str          # name of the parent node ('' for the first); positions are absolute, the game subtracts the parent's


@dataclass
class Texture:
    width: int
    height: int
    pixels: bytes        # one palette index per byte
    palette: list[tuple[int, int, int, int]]


@dataclass
class Cell:
    index: int
    grid_x: int
    grid_z: int
    tile_set: int
    header: bytes
    texture_ids: list[int]
    textures: dict[int, Texture] = field(default_factory=dict)


def level_file(game: Path, time: str, name: str) -> bytes:
    """One file (CITY.HDR, GAME.GPH, ...) of the standard level archive of a time of day; the night archive is NIGH_STD.MRG."""
    return mrg_files(read_entry(game, f'MERGED\\{time[:4]}_STD.MRG'))[name]


def signed(value: int, bits: int) -> int:
    value &= (1 << bits) - 1
    return value - (1 << bits) if value >> (bits - 1) else value


class City:
    def __init__(self, game: Path, time: str = 'DAY') -> None:
        hdr = level_file(game, time, 'CITY.HDR')
        self.hdr = hdr
        count, cells_at, tex_count, tex_at, _, _, _, _, sets_at = struct.unpack_from('<9I', hdr, 0)
        self.str_offset, self.str_size = toc_entries(game)[f'{time}.STR']
        self.cell_table = [struct.unpack_from('<III', hdr, cells_at - 4 + 12 * i) for i in range(count)]
        self.tex_table = [(hdr[tex_at - 4 + 2 * i], hdr[tex_at - 3 + 2 * i]) for i in range(tex_count)]
        self.sets = self._read_sets(sets_at - 4)
        self._img = (game / 'POLICE.IMG').open('rb')

    def close(self) -> None:
        self._img.close()

    def _read_sets(self, table: int) -> list[list[list[int]]]:
        """13 tile sets: set -> page -> clut -> global texture id. A page array ends where the next array or table starts."""
        hdr = self.hdr
        words = struct.unpack_from('<9I', hdr, 0)
        bounds = sorted({words[1] - 4, words[3] - 4, words[4] - 4, words[7] - 4, len(hdr)})
        sets = []
        set_count = (struct.unpack_from('<I', hdr, table)[0] - 4 - table) // 4
        starts = [struct.unpack_from('<I', hdr, table + 4 * s)[0] - 4 for s in range(set_count)]
        for s, at in enumerate(starts):
            page_count = (struct.unpack_from('<I', hdr, at)[0] - 4 - at) // 4
            page_at = [struct.unpack_from('<I', hdr, at + 4 * k)[0] - 4 for k in range(page_count)]
            set_end = starts[s + 1] if s + 1 < set_count else min(b for b in bounds if b > page_at[-1])
            pages = []
            for k, start in enumerate(page_at):
                end = page_at[k + 1] if k + 1 < page_count else set_end
                pages.append(list(struct.unpack_from(f'<{(end - start) // 4}i', hdr, start)))
            sets.append(pages)
        return sets

    def centre(self) -> tuple[int, int, int]:
        """Middle of the city grid in game units (a grid step is 1024 units), the origin the exporters subtract."""
        xs = [signed(word >> 9, 12) for word, _, _ in self.cell_table]
        zs = [signed(word >> 21, 11) for word, _, _ in self.cell_table]
        return ((min(xs) + max(xs)) // 2 * 1024, 0, (min(zs) + max(zs)) // 2 * 1024)

    def cell_sectors(self, index: int) -> tuple[int, int, int]:
        """(first sector, header sectors, total sectors) of a cell in the .STR."""
        word = self.cell_table[index][2]
        first = (word & 0x7ffffff) >> 11
        end = (self.cell_table[index + 1][2] & 0x7ffffff) >> 11 if index + 1 < len(self.cell_table) else self.str_size // SECTOR
        return first, word >> 27, end - first

    def read_cell(self, index: int) -> Cell:
        first, header_sectors, total = self.cell_sectors(index)
        self._img.seek(self.str_offset + first * SECTOR)
        data = self._img.read(total * SECTOR)
        header, body = data[:header_sectors * SECTOR], data[header_sectors * SECTOR:]
        word = self.cell_table[index][0]
        list_at = struct.unpack_from('<I', header, len(header) - 8)[0]
        count = struct.unpack_from('<H', header, list_at)[0]
        ids = list(struct.unpack_from(f'<{count}h', header, list_at + 2))
        cell = Cell(index, signed(word >> 9, 12), signed(word >> 21, 11), struct.unpack_from('<I', header, 0)[0], header, ids)
        pos = 0
        for tid in ids:
            size, dim = self.tex_table[tid]
            width, height = (dim & 15) * 16, dim & 0xf0
            raw = body[pos:pos + width * height // 2]
            pixels = bytearray(width * height)
            for i, byte in enumerate(raw):
                pixels[2 * i] = byte & 15
                pixels[2 * i + 1] = byte >> 4
            pal_at = pos + width * height // 2
            palette = [tuple(body[pal_at + 4 * i:pal_at + 4 * i + 4]) for i in range(16)]
            cell.textures[tid] = Texture(width, height, bytes(pixels), palette)
            pos += size * 0x40
        return cell

    def nodes(self, cell: Cell) -> list[Node]:
        header = cell.header
        out = []
        at = 4
        while True:
            w = struct.unpack_from('<13i', header, at)
            name_at = 4 + w[10]
            name = header[name_at:header.index(b'\0', name_at)].decode('latin1')
            parent = header[4 + w[12]:header.index(b'\0', 4 + w[12])].decode('latin1') if w[12] else ''
            out.append(Node(w[0], (w[2], w[3], w[4]), (w[5], w[6], w[7]), w[8], name, w[1], parent))
            if w[11] == 0:
                return out
            at = 4 + w[11]

    def mesh(self, cell: Cell, mesh_offset: int) -> Mesh:
        header = cell.header
        base = 4

        def u32(at: int) -> int:
            return struct.unpack_from('<I', header, at)[0]

        mesh_at = base + mesh_offset
        vertex_count = u32(mesh_at + 4)
        count_a, count_b = struct.unpack_from('<hh', header, mesh_at + 8)
        verts = [struct.unpack_from('<3h', header, base + u32(mesh_at + 0x18) + 8 * i) for i in range(vertex_count)]
        materials = []
        for list_ptr, count in ((0x10, count_a), (0x14, count_b)):
            if not u32(mesh_at + list_ptr):
                continue
            for j in range(count):
                m = header[base + u32(mesh_at + list_ptr) + 16 * j:][:16]
                page, clut = struct.unpack_from('<HH', m, 0)
                pages = self.sets[cell.tile_set]
                tex = pages[page][clut] if page < len(pages) and clut < len(pages[page]) else -1
                materials.append(Material(page, clut, [(m[4 + 2 * k], m[5 + 2 * k]) for k in range(4)], tex))
        return Mesh(verts, materials, read_prims(header, base, mesh_at))
