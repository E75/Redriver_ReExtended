"""WSPC .DOB vehicle -> GLB (geometry, vertex colours, CLUT textures, flat node list).

Format notes: docs/wspc.md.
"""
import argparse
import json
import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

SCALE = 1 / 250.0
CONTAINER_TAGS = (1, 2, 4)
RECORD_SIZE = {3: 24, 7: 28, 11: 24, 15: 32}   # primitive bytes per group type
FLAGS2_BIG_QUAD = 128                          # group flag with 36 byte records (seen once, unchecked)
PNG_SIGNATURE = b'\x89PNG\r\n\x1a\n'


@dataclass
class Node:
    pool_offset: int
    position: tuple[int, int, int]
    parent: int


@dataclass
class Prim:
    indices: list[int]
    colours: list[tuple[int, int, int]]
    material: int


@dataclass
class Material:
    page: int
    clut: int
    uv: list[tuple[int, int]]


def parse_chunks(data: bytes, off: int, end: int, out: list[tuple[int, int, int]]) -> None:
    while off + 8 <= end:
        size, tag = struct.unpack_from('<II', data, off)
        if size < 8 or off + size > end:
            break
        out.append((off, size, tag))
        if tag in CONTAINER_TAGS:
            parse_chunks(data, off + 8, off + size, out)
        off += size


def load(path: Path) -> tuple[bytes, dict[int, list[tuple[int, int]]], int, list[Node]]:
    data = path.read_bytes()
    chunks: list[tuple[int, int, int]] = []
    parse_chunks(data, 0, len(data), chunks)
    by_tag: dict[int, list[tuple[int, int]]] = {}
    for off, size, tag in chunks:
        by_tag.setdefault(tag, []).append((off, size))
    pool = by_tag[5][0][0] + 8
    nodes_off, nodes_size = by_tag[6][0]
    nodes = []
    for i in range((nodes_size - 8) // 0x38):
        w = struct.unpack_from('<14i', data, nodes_off + 8 + i * 0x38)
        nodes.append(Node(w[1], (w[2], w[3], w[4]), w[12]))
    return data, by_tag, pool, nodes


def png(width: int, height: int, rgba: bytes) -> bytes:
    raw = b''.join(b'\0' + rgba[y * width * 4:(y + 1) * width * 4] for y in range(height))

    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)

    header = struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)
    return PNG_SIGNATURE + chunk(b'IHDR', header) + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b'')


def texture_pages(data: bytes, by_tag: dict[int, list[tuple[int, int]]]) -> list[tuple[bytearray, bytes]]:
    """Every tag 3 chunk: u32 header, 256x256 4bpp pixels, then CLUTs of 16 RGBA32 colours -> (pixel indices, palette)."""
    pages = []
    for off, size in by_tag[3]:
        start = off + 8
        indices = bytearray(0x10000)
        for i, byte in enumerate(data[start + 4:start + 4 + 0x8000]):
            indices[2 * i] = byte & 15
            indices[2 * i + 1] = byte >> 4
        pages.append((indices, data[start + 4 + 0x8000:off + size]))
    return pages


def page_png(pages: list[tuple[bytearray, bytes]], page: int, clut: int) -> bytes:
    """One page drawn with one CLUT (colour 0,0,0,0 is transparent); out-of-range numbers fall back to 0."""
    indices, palette = pages[page if page < len(pages) else 0]
    colours = palette[clut * 64:(clut + 1) * 64] if clut * 64 < len(palette) else palette[:64]
    rgba = bytearray()
    for index in indices:
        r, g, b, a = colours[4 * index:4 * index + 4]
        rgba += bytes((r, g, b, 0 if (r, g, b, a) == (0, 0, 0, 0) else 255))
    return png(256, 256, bytes(rgba))


def read_mesh(data: bytes, pool: int, offset: int) -> tuple[list[tuple[int, int, int]], list[Material], list[Prim]]:
    base = pool + offset

    def u32(at: int) -> int:
        return struct.unpack_from('<I', data, at)[0]

    vertex_count = u32(base + 4)
    material_count = struct.unpack_from('<h', data, base + 8)[0]
    vertex_at = base + u32(base + 0x18)
    material_at = base + u32(base + 0x10)
    verts = [struct.unpack_from('<3h', data, vertex_at + 8 * i) for i in range(vertex_count)]
    materials = []
    for j in range(material_count):
        m = data[material_at + 16 * j:material_at + 16 * j + 16]
        page, clut = struct.unpack_from('<HH', m, 0)
        materials.append(Material(page, clut, [(m[4 + 2 * k], m[5 + 2 * k]) for k in range(4)]))
    prims: list[Prim] = []
    sub_at = base + u32(base + 0xc)
    for i in range(data[base]):
        sub = sub_at + 0x14 * i
        for list_off in (0xc, 0x10):
            pointer = u32(sub + list_off)
            if not pointer:
                continue
            head = base + pointer
            table = base + u32(head + 4)
            for j in range(struct.unpack_from('<h', data, head)[0]):
                entry = table + 8 * j
                kind, count = data[entry], data[entry + 3]
                verts_per_prim = 4 if kind & 4 else 3
                size = RECORD_SIZE.get(kind, 8 + verts_per_prim * 4 + 4)
                if data[entry + 2] == FLAGS2_BIG_QUAD:
                    size = 36
                first = base + u32(entry + 4)
                for k in range(count):
                    rec = first + k * size
                    indices, colours = [], []
                    for v in range(verts_per_prim):
                        r, g, b, index = data[rec + 8 + 4 * v:rec + 12 + 4 * v]
                        indices.append(index)
                        colours.append((r, g, b))
                    # quads repeat their 4th vertex (offset * 8) first, so the material offset is the second u16
                    material_at = rec + 8 + verts_per_prim * 4 + (2 if verts_per_prim == 4 else 0)
                    material = struct.unpack_from('<H', data, material_at)[0] // 16
                    prims.append(Prim(indices, colours, material))
    return verts, materials, prims


class GlbBuilder:
    def __init__(self, pages: list[tuple[bytearray, bytes]]) -> None:
        self.pages = pages
        self.blob = bytearray()
        self.views: list[dict] = []
        self.accessors: list[dict] = []
        self.images: list[bytes] = []
        self.textures: list[dict] = []
        self.materials: list[dict] = []
        self.clut_material: dict[tuple[int, int], int] = {}

    def add(self, values: list[float], count: int, kind: str, bounds: tuple[list[float], list[float]] | None = None) -> int:
        self.blob.extend(b'\0' * (-len(self.blob) % 4))
        body = struct.pack('<%df' % len(values), *values)
        self.views.append(dict(buffer=0, byteOffset=len(self.blob), byteLength=len(body), target=34962))
        self.blob.extend(body)
        accessor = dict(bufferView=len(self.views) - 1, componentType=5126, count=count, type=kind)
        if bounds:
            accessor['min'], accessor['max'] = bounds
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def material(self, page: int, clut: int) -> int:
        key = (page, clut)
        if key not in self.clut_material:
            self.images.append(page_png(self.pages, page, clut))
            self.textures.append(dict(source=len(self.images) - 1, sampler=0))
            self.materials.append(dict(
                name=f'page{page}_clut{clut}', alphaMode='MASK', alphaCutoff=0.5, doubleSided=True,
                pbrMetallicRoughness=dict(baseColorTexture=dict(index=len(self.textures) - 1), metallicFactor=0, roughnessFactor=0.9)))
            self.clut_material[key] = len(self.materials) - 1
        return self.clut_material[key]

    def finish(self, nodes: list[dict], meshes: list[dict]) -> bytes:
        image_defs = []
        for body in self.images:
            self.blob.extend(b'\0' * (-len(self.blob) % 4))
            self.views.append(dict(buffer=0, byteOffset=len(self.blob), byteLength=len(body)))
            self.blob.extend(body)
            image_defs.append(dict(bufferView=len(self.views) - 1, mimeType='image/png'))
        self.blob.extend(b'\0' * (-len(self.blob) % 4))
        gltf = dict(
            asset=dict(version='2.0', generator='wspc dob2glb'), scene=0, scenes=[dict(nodes=list(range(len(nodes))))],
            nodes=nodes, meshes=meshes, accessors=self.accessors, bufferViews=self.views, materials=self.materials,
            images=image_defs, textures=self.textures,
            samplers=[dict(magFilter=9728, minFilter=9728, wrapS=33071, wrapT=33071)], buffers=[dict(byteLength=len(self.blob))])
        text = json.dumps(gltf).encode()
        text += b' ' * (-len(text) % 4)
        total = 12 + 8 + len(text) + 8 + len(self.blob)
        return (struct.pack('<III', 0x46546C67, 2, total) + struct.pack('<II', len(text), 0x4E4F534A) + text
                + struct.pack('<II', len(self.blob), 0x004E4942) + bytes(self.blob))


def node_groups(verts: list[tuple[int, int, int]], materials: list[Material], prims: list[Prim]) -> dict[tuple[int, int], tuple[list[float], list[float], list[float]]]:
    """Triangles per (page, CLUT): positions (source x right, y down, z forward -> glTF by rotating (x, -y, -z), winding kept), vertex colours (0x80 = 1.0), uv."""
    groups: dict[tuple[int, int], tuple[list[float], list[float], list[float]]] = {}
    for prim in prims:
        material = materials[prim.material] if prim.material < len(materials) else Material(0, 0, [(0, 0)] * 4)
        triangles = [(0, 1, 2)] if len(prim.indices) == 3 else [(0, 1, 2), (1, 3, 2)]
        for tri in triangles:
            if any(prim.indices[k] >= len(verts) for k in tri):
                continue
            positions, colours, uvs = groups.setdefault((material.page, material.clut), ([], [], []))
            for k in tri:
                x, y, z = verts[prim.indices[k]]
                positions.extend((x * SCALE, -y * SCALE, -z * SCALE))
                colours.extend([min(1.0, c / 128.0) for c in prim.colours[k]] + [1.0])
                u, v = material.uv[k]
                uvs.extend(((u + 0.5) / 256.0, (v + 0.5) / 256.0))
    return groups


def build(path: Path, out: Path, only: set[int] | None) -> None:
    data, by_tag, pool, nodes = load(path)
    builder = GlbBuilder(texture_pages(data, by_tag))
    gltf_nodes: list[dict] = []
    meshes: list[dict] = []
    for index, node in enumerate(nodes):
        if only is not None and index not in only:
            continue
        try:
            verts, materials, prims = read_mesh(data, pool, node.pool_offset)
        except struct.error:
            print(f'node {index}: offset 0x{node.pool_offset:x} is not a normal mesh, skipped')
            continue
        x, y, z = node.position
        gltf_node = dict(name=f'n{index}', translation=[x * SCALE, -y * SCALE, -z * SCALE])
        primitives = []
        for (page, clut), (positions, colours, uvs) in node_groups(verts, materials, prims).items():
            count = len(positions) // 3
            bounds = ([min(positions[a::3]) for a in range(3)], [max(positions[a::3]) for a in range(3)])
            primitives.append(dict(
                attributes=dict(POSITION=builder.add(positions, count, 'VEC3', bounds), COLOR_0=builder.add(colours, count, 'VEC4'),
                                TEXCOORD_0=builder.add(uvs, count, 'VEC2')),
                material=builder.material(page, clut)))
        print(f'node {index}: offset 0x{node.pool_offset:x} parent {node.parent} verts {len(verts)} prims {len(prims)}')
        if primitives:
            meshes.append(dict(primitives=primitives))
            gltf_node['mesh'] = len(meshes) - 1
        gltf_nodes.append(gltf_node)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(builder.finish(gltf_nodes, meshes))
    print('wrote', out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dob', type=Path)
    parser.add_argument('glb', type=Path)
    parser.add_argument('nodes', nargs='?', help='comma separated node numbers (default all)')
    args = parser.parse_args()
    only = {int(n) for n in args.nodes.split(',')} if args.nodes else None
    build(args.dob, args.glb, only)


if __name__ == '__main__':
    main()
