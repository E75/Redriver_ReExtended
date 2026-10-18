# World's Scariest Police Chases (PS1, Europe) — findings

Game: "World's Scariest Police Chases" (EU disc, boot file `SLES_034.25`, executable `POLICEE.EXE`, internal title "Deadly Pursuit - Worlds scariest police chases", build "May 9 2001"). Developer Unique Development Studios, publisher Activision / Fox Interactive. It is **not** the Driver engine: own file formats, no `.LEV`/`.CAR` files. Goal here: import its cars (and maybe its minimap and maps) into Redriver2_ReExtended.

All offsets below are in little endian. Nothing of the game is committed; point the tools at your own copy of the disc files.

## Status

| Part | State |
|---|---|
| Disc files (`POLICE.TOC` + `POLICE.IMG`) | decoded, `tools/wspc/wspc_extract.py` |
| `.MRG` containers | decoded (same tool) |
| `.DOB` chunk tree, textures, vehicle meshes | decoded, `tools/wspc/dob2glb.py` (GLB with vertex colours + textures) |
| Wheels, damage states, `.DAT` vehicle values, node rotations | not done |
| City (`MISC.DOB`, `CITY.HDR`, `GAME.GPH`), minimap | not looked at yet (only container level) |
| Import into Redriver | not started |

## Disc layout

- `POLICE.TOC` (8248 bytes): entries of `name\0`, `u32 offset`, `u32 size`, back to back. 304 files, offsets into `POLICE.IMG` (266 MB). Directory separator is `\`.
- Other disc files: `*.STR` (PS1 video, jpsxdec reads them), `POLICE.XA` (audio), `SLES_034.25` (boot stub), `POLICEE.EXE` (the game, PS-X EXE: load address `0x80010000`, text size `0xDE000`, entry `0x800C7F34`).
- Main groups in the TOC: `VEHICLES\` (78 files), `SCRIPT\` (46, `.SCB` mission scripts, `SCOB`/`SCEV` blocks with readable text), `MENU\` (33), `BACKDROP\` (32, sky per hour and weather), `MERGED\` (16 level archives), `DEBRIEF\`, `CAMPATH\`, `SOUND\`. `_NOTINUSE_*` and `VEHICLES\OLD\` hold leftovers. The developer also left tool names in the TOC (`MERGE.EXE`, `CLUSTER.EXE`).
- Github has only [RooMan-GBM/WSAE](https://github.com/RooMan-GBM/WSAE) (an extractor for the TOC/IMG). No decompilation project exists as of 2026-10-18.

## `.MRG` (level archive, `MERGED\<time>_<kind>.MRG`)

Times DAWN, DAY, NIGH, RAIN; kinds STD, FLW (flowers), MON (monsters), WAT (water). Format:

```
u32 count
count x { u32 nameOffset, u32 fileOffset, u32 size }   // names are \0 terminated, offsets from file start
```

Content, in all 16 files: `MISC.DOB` (≈230 KB, the world objects + textures), `SMLLFONT.TXT`, `BIGFONT.TXT`, `ALLA.AIX` (AI), `GAME.GPH` (36 KB, graph, probably roads/AI), `BREAKABL.BRK`, `CITY.HDR` (49 KB, city header).

## `.DOB` (3D objects and textures)

Used for vehicles (`VEHICLES\*.DOB`) and the world (`MISC.DOB`). `.DOX` = `.DAT` followed by `.DOB` (same bytes), so use the `.DOB`.

### Chunk tree

A chunk is `u32 size` (including these 8 bytes) + `u32 tag`, payload after. Tags 1, 2 and 4 contain further chunks. Seen in a car:

```
1  file
  2  textures
    3   texture page (see below)
    14, 15  name / table chunks (not decoded)
  4  objects
    5   mesh pool (all meshes, referenced by offset)
    7   per node name strings (16 bytes each, not decoded)
    6   node table, 0x38 bytes per node
    21  (0x1428 bytes, not decoded)  23, 22  (not decoded)
```

Found by decompiling the loader at `0x8001D768` (EXE address space, Ghidra project raw MIPS at base `0x80010000`).

### Texture page (tag 3)

`u32 header` (the car has `0x1C0000`; the palette byte count below gives 28 CLUTs, so the high half is probably that count), then `256 x 256` pixels at 4 bits per pixel (`0x8000` bytes, low nibble first), then `N` palettes of 16 colours stored as **RGBA32** (`r, g, b, a` bytes; `a` is 1, a colour of all zero is transparent). A material picks the palette by its CLUT number.

### Node table (tag 6), 0x38 bytes per node

Seen as `i32` words:

| word | meaning |
|---|---|
| 0 | node type (`0x23` for meshes) |
| 1 | offset of the mesh in the mesh pool (tag 5 payload) |
| 2, 3, 4 | position x, y, z, **absolute** (the game subtracts the parent position to get the local one). Y points down (PS1) |
| 5, 6, 7 | rotation (12 bit angles, all 0 in the car) |
| 8 | `0x1000` (scale 1.0 in 12 bit) |
| 12 | parent node index, `-1` for the root |

Police car: node 3 is the body (root), nodes 4 and 5 are two copies of the body (probably damage states, same size) and are left out. Node 0, 1 are low-detail boxes (hull / shadow?) and left out, nodes 2, 6..10 are small body parts (light bar, mirrors, ...), nodes 11..14 are the four wheels (a box ±48 x ±92 x ±92 with 4 textured quads; wheel look is not solved yet).

### Mesh (in the pool at the node's offset)

Header, offsets are relative to the start of the mesh:

| offset | meaning |
|---|---|
| `+0x00` u8 | number of sub meshes |
| `+0x04` u32 | number of vertices |
| `+0x08` s16 | number of materials |
| `+0x0C` | sub mesh array (0x14 bytes each, usually one) |
| `+0x10` | materials, 16 bytes each |
| `+0x18` | vertices, 8 bytes each: `s16 x, y, z, pad` (units ≈ 1/250 m, Y down) |
| `+0x20` | normals (same layout) |

Material (16 bytes): `u16 ?`, `u16 clut`, 4 x `(u8 u, u8 v)` texture coordinates in the 256 x 256 page, `u16 ?`, `u16 flags`. Wheel materials have flags 128 (`0x80`).

Sub mesh (0x14 bytes): two pointers at `+0x0C` and `+0x10` (two lists). Each points to `s16 count` and (at `+4`) a table of groups, 8 bytes each: `u8 type, u8 flags1, u8 flags2, u8 count, u32 pointer` to the primitives.

Primitives in a group, one record each, `count` records:

```
8 bytes   header (unknown; looks like a bounding box / sort data)
n x 4     vertex: u8 r, g, b (0x80 = 1.0), u8 vertex index      (n = 3 for triangles, 4 for quads)
u16       material index x 16 (byte offset in the material list)
u16 ...   rest of the record
```

Record size by group type: type 3 triangle 24 bytes, type 7 quad 28, type 11 triangle 24, type 15 quad 32 (verified over all cars from the distance between the group pointers). A group with `flags2 == 128` was 36 bytes (seen once, not checked). Quads use the order `v0 v1 v2 v3` as a strip: triangles `(0,1,2)` and `(1,3,2)`; tested with a planarity check, all 77 quads of the police body are flat in this order and none in the fan order.

## Tools

- `tools/wspc/wspc_extract.py <game folder> <out folder>`: all files from `POLICE.IMG` and every `.MRG` unpacked.
- `tools/wspc/dob2glb.py <file.DOB> <out.glb> [node,node,...]`: GLB with the listed nodes (default all), positions scaled by 1/250, Y flipped, vertex colours and one PNG per used CLUT. Check it in the viewer first (`bk view` of a folder with the `model.glb`, three.js previewer of the blender skill) before using it in Redriver.
- Ghidra: import `POLICEE.EXE` as raw binary (strip the first `0x800` header bytes), language `MIPS:LE:32:default`, base `0x80010000`. Useful addresses: `0x80033644` loads `misc.dob`, `0x8001D768` parses a DOB, `0x800218BC` finds an object by name, `0x80034354` reads a GFX_* texture rectangle, `0x8002ADBC` uploads a texture page.

## Learned

- Quad primitive records are not all the same size, so a wrong size gives plausible vertices with wrong faces (spiky body). Check the distance between group pointers and the planarity of the quads.
- The `DOB` header pairs `(size, tag)` at the file start look like a table of three entries but are the first chunks of the tree; sizes include the 8 header bytes.
- A first guess "the header gives the vertex count" was wrong: the sub-meshes have their own header with `nVerts` at `+4`.
