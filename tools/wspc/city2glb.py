"""WSPC city -> GLB, from the player's own disc files.

usage: city2glb.py <game folder> <out.glb> [--time DAY] [--cells 1,2,3 | --all]
The game folder holds POLICE.TOC and POLICE.IMG. Format notes: docs/wspc.md.
"""
import argparse
import struct
from pathlib import Path

from dob2glb import SCALE, GlbBuilder, png
from wspc_city import City, Mesh, Node, Texture


class CityBuilder(GlbBuilder):
    """GlbBuilder with one material per global texture id instead of per (page, CLUT)."""

    def __init__(self) -> None:
        super().__init__([])
        self.tex_material: dict[int, int] = {}

    def texture(self, tid: int, tex: Texture) -> int:
        if tid not in self.tex_material:
            colours = [bytes(c[:3]) + bytes([0 if c == (0, 0, 0, 0) else 255]) for c in tex.palette]   # all zero = transparent
            rgba = b''.join(colours[i] for i in tex.pixels)
            self.images.append(png(tex.width, tex.height, rgba))
            self.textures.append(dict(source=len(self.images) - 1, sampler=0))
            self.materials.append(dict(
                name=f'tex{tid}', alphaMode='MASK', alphaCutoff=0.5, doubleSided=True,
                pbrMetallicRoughness=dict(baseColorTexture=dict(index=len(self.textures) - 1), metallicFactor=0, roughnessFactor=0.9)))
            self.tex_material[tid] = len(self.materials) - 1
        return self.tex_material[tid]


def mesh_primitives(builder: CityBuilder, mesh: Mesh, node: Node, textures: dict[int, Texture], origin: tuple[int, int, int]) -> list[dict]:
    """Triangles per texture. World position = node position + vertex; source data x right, y down, z forward -> glTF rotation (x, -y, -z)."""
    groups: dict[int, tuple[list[float], list[float], list[float]]] = {}
    px, py, pz = (node.position[a] - origin[a] for a in range(3))
    for prim in mesh.prims:
        if prim.material >= len(mesh.materials):
            continue
        material = mesh.materials[prim.material]
        tex = textures.get(material.texture)
        if tex is None:
            continue
        triangles = [(0, 1, 2)] if len(prim.indices) == 3 else [(0, 1, 2), (1, 3, 2)]
        for tri in triangles:
            if any(prim.indices[k] >= len(mesh.verts) for k in tri):
                continue
            positions, colours, uvs = groups.setdefault(material.texture, ([], [], []))
            for k in tri:
                x, y, z = mesh.verts[prim.indices[k]]
                positions.extend(((x + px) * SCALE, -(y + py) * SCALE, -(z + pz) * SCALE))
                colours.extend([min(1.0, c / 128.0) for c in prim.colours[k]] + [1.0])
                u, v = material.uv[k]
                uvs.extend(((u + 0.5) / tex.width, (v + 0.5) / tex.height))
    primitives = []
    for tid, (positions, colours, uvs) in groups.items():
        count = len(positions) // 3
        bounds = ([min(positions[a::3]) for a in range(3)], [max(positions[a::3]) for a in range(3)])
        primitives.append(dict(
            attributes=dict(POSITION=builder.add(positions, count, 'VEC3', bounds), COLOR_0=builder.add(colours, count, 'VEC4'),
                            TEXCOORD_0=builder.add(uvs, count, 'VEC2')),
            material=builder.texture(tid, textures[tid])))
    return primitives


def build(game: Path, out: Path, time: str, cells: list[int] | None) -> None:
    city = City(game, time)
    builder = CityBuilder()
    origin = city.centre()
    print('origin (game units):', origin)
    gltf_nodes: list[dict] = []
    meshes: list[dict] = []
    try:
        for index in cells if cells is not None else range(len(city.cell_table)):
            cell = city.read_cell(index)
            nodes = city.nodes(cell)
            for node in nodes:
                if node.mesh_offset < 0:
                    continue
                try:
                    mesh = city.mesh(cell, node.mesh_offset)
                except (struct.error, ValueError, IndexError):
                    print(f'cell {index} node {node.name}: mesh not readable, skipped')
                    continue
                primitives = mesh_primitives(builder, mesh, node, cell.textures, origin)
                if primitives:
                    meshes.append(dict(primitives=primitives))
                    gltf_nodes.append(dict(name=f'c{index}_{node.name}', mesh=len(meshes) - 1))
            print(f'cell {index}: grid ({cell.grid_x}, {cell.grid_z}) done')
    finally:
        city.close()
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(builder.finish(gltf_nodes, meshes))
    print('wrote', out)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('glb', type=Path)
    parser.add_argument('--time', default='DAY', choices=['DAY', 'DAWN', 'NIGHT', 'RAIN'])
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--cells', help='comma separated cell numbers')
    group.add_argument('--all', action='store_true')
    args = parser.parse_args()
    cells = None if args.all else [int(c) for c in args.cells.split(',')]
    build(args.game, args.glb, args.time, cells)


if __name__ == '__main__':
    main()
