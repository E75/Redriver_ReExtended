# WSPC — findings

WSPC (EU disc, boot stub `SLES_034.25`). It is **not** the Driver engine: own file formats, no `.LEV`/`.CAR` files. Goal here: import its cars, map and minimap into Redriver2_ReExtended.

All offsets below are in little endian. Nothing of the game is committed; point the tools at your own copy of the disc files.

## Status

| Part | State |
|---|---|
| Disc files (`POLICE.TOC` + `POLICE.IMG`) | decoded, `tools/wspc/wspc_extract.py` |
| `.MRG` containers | decoded (same tool) |
| `.DOB` chunk tree, textures (several pages), meshes | decoded, `tools/wspc/dob2glb.py` (GLB with vertex colours + textures) |
| Police car | looks right in the three.js viewer (body, window glass, light bar, stripe, wheels) |
| Other cars (`VEHICLES\*.DOB`) | same format, not checked one by one yet |
| Damage states, `.DAT` vehicle values, node rotations | not done |
| World props (`MISC.DOB`: 142 nodes, signs, arrows, lamps) | converts and shows in the viewer, positions absolute |
| **City terrain, buildings, textures** | decoded (`CITY.HDR` + `DAY.STR` etc.), `tools/wspc/city2glb.py`, `tools/wspc/city2data.py`; checked in the viewer |
| Road graph (`GAME.GPH`) | decoded, 1180 nodes with links, lines up with the road meshes; script names table not linked to nodes yet |
| Minimap | no image on the disc; `tools/wspc/wcity_map.py` renders a top-down map from the city data |
| Import into Redriver | not started (the converter side) |

## Disc layout

- `POLICE.TOC` (8248 bytes): entries of `name\0`, `u32 offset`, `u32 size`, back to back. 304 files, offsets into `POLICE.IMG` (266 MB). Directory separator is `\`.
- Other disc files: `*.STR` (video), `POLICE.XA` (audio), `SLES_034.25` (boot stub), `POLICEE.EXE` (the game executable).
- Main groups in the TOC: `VEHICLES\` (78 files), `SCRIPT\` (46, `.SCB` mission scripts, `SCOB`/`SCEV` blocks with readable text), `MENU\` (33), `BACKDROP\` (32, sky per hour and weather), `MERGED\` (16 level archives), `DEBRIEF\`, `CAMPATH\`, `SOUND\`. `_NOTINUSE_*` and `VEHICLES\OLD\` hold leftovers. The developer also left tool names in the TOC (`MERGE.EXE`, `CLUSTER.EXE`).
- `DAWN.STR`, `DAY.STR`, `NIGHT.STR`, `RAIN.STR` in the TOC are **not video**: each is ≈ 53 MB of streamed city data (see City).

## `.MRG` (level archive, `MERGED\<time>_<kind>.MRG`)

Times DAWN, DAY, NIGH, RAIN; kinds STD, FLW (flowers), MON (monsters), WAT (water). Format:

```
u32 count
count x { u32 nameOffset, u32 fileOffset, u32 size }   // names are \0 terminated, offsets from file start
```

Content, in all 16 files: `MISC.DOB` (≈230 KB, props + textures, see World props), `SMLLFONT.TXT`, `BIGFONT.TXT`, `ALLA.AIX` (AI), `GAME.GPH` (36 KB, not decoded, probably road graph / AI), `BREAKABL.BRK`, `CITY.HDR` (49 KB, city header).

## `.DOB` (3D objects and textures)

Used for vehicles (`VEHICLES\*.DOB`) and the world props (`MISC.DOB`). `.DOX` = `.DAT` followed by `.DOB` (same bytes), so use the `.DOB`.

### Chunk tree

A chunk is `u32 size` (including these 8 bytes) + `u32 tag`, payload after. Tags 1, 2 and 4 contain further chunks. Seen in a car:

```
1  file
  2  textures
    3   texture page (one per page, MISC.DOB has 5)
    14, 15  name / table chunks (not decoded)
  4  objects
    5   mesh pool (all meshes, referenced by offset)
    7   per node name strings (16 bytes each, not decoded)
    6   node table, 0x38 bytes per node
    21  (0x1428 bytes, not decoded)  23, 22  (not decoded)
```

Found in the loader code of the game executable.

### Texture page (tag 3)

`u32 header` (the car has `0x1C0000`; the palette byte count below gives 28 CLUTs, so the high half is probably that count), then `256 x 256` pixels at 4 bits per pixel (`0x8000` bytes, low nibble first), then `N` palettes of 16 colours stored as **RGBA32** (`r, g, b, a` bytes; `a` is 1, a colour of all zero is transparent). A material picks the page (`u16` at +0) and the palette (`u16` at +2). `MISC.DOB` has 5 pages with 1, 1, 36, 52 and 38 CLUTs.

### Node table (tag 6), 0x38 bytes per node

Seen as `i32` words:

| word | meaning |
|---|---|
| 0 | node type (`0x23` for meshes) |
| 1 | offset of the mesh in the mesh pool (tag 5 payload) |
| 2, 3, 4 | position x, y, z, **absolute** (the game subtracts the parent position to get the local one) |
| 5, 6, 7 | rotation (12 bit angles, all 0 in the car) |
| 8 | `0x1000` (scale 1.0 in 12 bit) |
| 12 | parent node index, `-1` for the root |

Axes: the source data is x right, y down, z forward, which is a right handed system, so glTF is a **rotation** `(x, -y, -z)` and the triangle order stays as in the file (the first try with `(x, -y, z)` mirrored the car: text on the doors read backwards). Vertex order in the file is counter clockwise when seen from outside (90 of 93 body triangles have an outward normal).

Police car: node 3 is the body (root), nodes 4 and 5 are two more copies of the body (probably damage states) and are left out. Nodes 0, 1 are low-detail boxes (hull / shadow?) and left out, nodes 2, 6..10 are small body parts (light bar, mirrors, plates, ...), nodes 11..14 are the four wheels: a box ±48 x ±92 x ±92 with a round tyre texture on the two big faces (very dark vertex colour 25/128).

### Mesh (in the pool at the node's offset)

Header, offsets are relative to the start of the mesh:

| offset | meaning |
|---|---|
| `+0x00` u8 | number of sub meshes |
| `+0x04` u32 | number of vertices |
| `+0x08` s16 | number of materials in list A (`+0x0A` s16: list B, always 0 in the files looked at) |
| `+0x0C` | sub mesh array (0x14 bytes each, usually one) |
| `+0x10` | materials (list A), 16 bytes each |
| `+0x18` | vertices, 8 bytes each: `s16 x, y, z, pad` (units ≈ 1/250 m, Y down) |

Material (16 bytes): `u16 page`, `u16 clut`, 4 x `(u8 u, u8 v)` texture coordinates in the 256 x 256 page (one per vertex of the polygon, same order as the vertices), `u16 ?`, `u16 flags` (`0x8000` = untextured colour only?, wheel materials 128).

Sub mesh (0x14 bytes): two pointers at `+0x0C` and `+0x10` (two lists). Each points to `s16 count` and (at `+4`) a table of groups, 8 bytes each: `u8 type, u8 flags1, u8 flags2, u8 count, u32 pointer` to the primitives.

Primitive record (`count` of them per group):

```
u8, u8, u16 x 3   header: 0x14/0x15, a small number, then the offsets (vertex index * 8) of the first 3 vertices
n x 4             vertex: u8 r, g, b (0x80 = 1.0), u8 low byte of the vertex index  (n = 3 triangle, 4 quad)
triangle: u16 material offset (index * 16), u16 flags
quad:     u16 4th vertex offset (index * 8, repeats the vertex above), u16 material offset (index * 16), (type 15: u32 0x10)
```

Record size by group type: type 3 triangle 24 bytes, type 7 quad 28, type 11 triangle 24, type 15 quad 32 (verified over all cars from the distance between the group pointers). `flags2 == 128` does **not** change the record size (an earlier guess of 36 bytes was wrong; 28 byte quads with `flags2 = 128` exist in the city). Quads use the order `v0 v1 v2 v3` as a strip: triangles `(0,1,2)` and `(1,3,2)`; tested with a planarity check, all 77 quads of the police body are flat in this order and none in the fan order.

The vertex index of a primitive is the **full** index from the header offsets (`u16 / 8`; the 4th vertex of a quad is the u16 after the vertices); the `u8` in each vertex record is only the low byte and breaks above 255 vertices (city meshes have up to 350+). The material number of a quad is the **second** u16 after the vertices. Taking the first one (the repeated vertex offset) gave textures on the wrong places. Check after the fix: in 113 of 115 meshes of `MISC.DOB` the largest material index is exactly `count - 1` (before: more than half went beyond the list).

## City

All of this is checked: the city exports show roads, bridges, skyscrapers and the water in the three.js viewer, the road graph lines up with the road meshes, and the textures a cell's meshes use are exactly the textures the cell carries.

- `MISC.DOB` (in every `.MRG`) holds only props: 142 nodes, ≈ 1200 triangles (arrows, signs, lamp posts, cones, boards). Positions span x -6879..11731, z -3647..9377 in the same units as the cars. No roads or buildings. Two nodes (53 and the mesh at pool offset 0x341) are not normal meshes (skipped by the converter).
- Four times of day: `DAY.STR`, `DAWN.STR`, `NIGHT.STR`, `RAIN.STR` (in the TOC, 25846 sectors of 0x800 each, ≈ 53 MB) with `MERGED\DAY_STD.MRG`, `DAWN_STD`, `NIGH_STD`, `RAIN_STD`. The geometry is the same, the textures differ. Every `.STR` starts with a copy of `CITY.HDR` (a leading dword `0xC2D4`, 25 sectors) and then 677 cells back to back.
- World: 677 cells on a 1024 unit grid, x -625..1032, z -808..587 grid steps (x -654400..1065322, z -828821..614232 game units, y down). Same units as the cars, so 1 unit = 1/250 m (a car tyre is 0.74 m) and the city is about 6.9 x 5.8 km. A few cells (the four in the north) are detached strips.

### `CITY.HDR`

Words (offsets inside the file are 4 too high, a file in memory has the size dword in front):

| word | meaning |
|---|---|
| w0 = 677 | cell count |
| w1 | table of `w0` x 12 bytes: `u32 grid`, `u32 pointer to a u16 neighbour list`, `u32 stream` |
| w2 = 3473 | number of textures |
| w3 | texture table, `w2` x `u8 size, u8 dim` |
| w4, w5 | the u16 lists of the neighbours (the pointers of w1 go there) |
| w6 = 677, w7 | per cell data not needed here |
| w8 | table of 13 tile sets: set -> page -> clut -> `i32` texture id |

- `grid`: low 3 bits = number of neighbours, bits 9..20 = x (signed, grid steps of 1024 game units), bits 21..31 = z (signed). Cell origin `x * 1024`, `z * 1024` (the first node of a cell lies close to it).
- `stream`: bits 0..26 = byte offset of the cell in the `.STR` (multiple of 0x800), bits 27..31 = number of header sectors. The cell runs to the offset of the next cell (the last one to the end of the file).
- Texture table: `size * 64` = bytes of the texture in a cell body; `dim`: width = `(dim & 15) * 16`, height = `dim & 0xF0`. Pixels are 4 bit (low nibble first, `w * h / 2` bytes), then 16 palette entries of RGBA32 (`r, g, b, a`; all zero = transparent). Checked: 64 x 64 = 2048 + 64 = 2112 = 33 * 64 and the other sizes in the table match too.
- Tile sets: the first word of a cell header is its tile set number (0..12). A material of a mesh gives `page` (`u16 at +0`) and `clut` (`u16 at +2`): global texture id = `sets[tile set][page][clut]`. The texture UV in the material are relative to the texture itself (the game adds the position in VRAM at load time).

### Cell (in the `.STR`)

`stream` header sectors, then the body.

- **Header part** (offsets are relative to byte 4, the word at 0 is the tile set): a chain of nodes, 13 words each, the next one at `w11` (0 = last):

  | word | meaning |
  |---|---|
  | 0 | kind: 11 group (`STR_`), 7 mesh (`GFS_`, `GHS_`, `INST_`), 36 null (`NUL_`), 35 `GFX_` |
  | 1 | offset of the mesh, -1 for none |
  | 2, 3, 4 | position x, y, z **absolute** (game units) |
  | 5, 6, 7 | rotation (all 0 in the cells looked at), 8 = `0x1000` scale |
  | 10 | offset of the node name (`\0` terminated) |
  | 11 | offset of the next node |
  | 12 | offset of the name of the parent node (the game subtracts its position to get the local one), 0 for none |

  A mesh has **the same layout as a `.DOB` mesh** (see Mesh above), but every pointer inside it is relative to byte 4 of the cell header and not to the mesh. The `u32` at 8 bytes before the end of the header part is the offset of the texture list (`u16 count`, then `i16` texture ids).
- **Body**: the textures of the list, one after another, each `size * 64` bytes.

Node names: `STR_<tile>` group per cell, `GFS_...` ground, roads, bridges, buildings of the cell (≈ 80 % of the triangles), `GHS_...` more buildings (placed in the middle of the city, mostly in the downtown), `INST_`/`GFX_` props, `GFS_...FYSIK` a small physics mesh, `NUL_...` helper points (no mesh; `NUL_Z1604_...` probably zones). Positions are all absolute, so the cells simply add up to the city: 4270 meshes, 558 362 triangles (460 664 GFS, 95 034 GHS) and 3472 of the 3473 textures.

### `GAME.GPH` (road graph)

Header: `u32` (= 1), `u16 node count` (1180 in all four times), `u16` (0), then the data from byte 8:

- 1180 nodes of 26 bytes: `s16 x`, `s16 z` (both **`<< 6`** gives game units), `u8, u8` (probably distance to the previous and next node), `s16` (small number), four `s16` links to other nodes (`-1` for none), `u8 0xFF`, `u8 flags` (0xC0 / 0x8C ...), `u16 flags`, `s16`, `s16 -1`, `u8, u8` (0xFF). The nodes of a road chain link to their predecessor and successor, junction nodes have 3 or 4 links.
- Behind them (aligned to 4): `s16 n` (357), `n x u32` offsets and the data: a list of `\0` terminated names such as `16_BUS_08`, `14_POLICE_START`, `03_PEG` (mission objects of the `SCRIPT\` files). How a node points to a name is not decoded.
- `ALLA.AIX` and `BREAKABL.BRK` (AI and breakable objects) are not decoded.

### Export format of `city2data.py`

`city.wcity` (little endian):

```
char magic[4] = "WCTY"; u32 version = 1; u32 textureCount; u32 meshCount; u32 reserved
texture:  u32 id; u16 width; u16 height; u8 rgba[width * height * 4]          (palette already applied, alpha 0 = transparent)
mesh:     u16 cell; char name[32]; u8 class; u8 reserved; u32 triangleCount; triangle[triangleCount]
class:    0 GFS (ground, roads, buildings), 1 GHS, 2 INST, 3 GFX, 4 physics (FYSIK), 255 other
triangle: u16 texture id; 3 x { i32 x, y, z (absolute game units, y down); u8 r, g, b (0x80 = 1.0), u8 0; u8 u, v (texels) }
```

`graph.wgph`: `char magic[4] = "WGPH"; u32 version = 1; u32 nodeCount; u32 listCount; u32 listBytes`, then `nodeCount` x 26 byte nodes as above, `listCount` x `u32` offsets into the list data, then the list data. The files carry game data of the player's own disc: never commit them.

### Minimap

The game has no minimap image on the disc. `_NOTINUSE_MAP\_CITYMAP.MAP` (551 936 bytes = 1024 x 539, mostly nibbles `0x11`, `0x44`, `0x22`) is not a plain bitmap in 4 or 8 bit at width 512, 1024 or 2048 and the name says it is not used. `wcity_map.py` draws the up-facing ground triangles with the mean colour of their texture; the result at 512 game units per pixel (3386 x 2864) is a recognisable city map (roads, water, bridges, the park); `--buildings` adds roofs, `--graph` draws the road graph in yellow. The game units of the image corner are printed so the image can be placed.

## Tools

- `tools/wspc/wspc_extract.py <game folder> <out folder>`: all files from `POLICE.IMG` and every `.MRG` unpacked.
- `tools/wspc/dob2glb.py <file.DOB> <out.glb> [node,node,...]`: GLB with the listed nodes (default all), positions scaled by 1/250, axes rotated to glTF, vertex colours and one PNG per used (page, CLUT). Police car: nodes `2,3,6,7,8,9,10,11,12,13,14`. Check it in the viewer first (`bk view` of a folder with the `model.glb`, three.js previewer of the blender skill) before using it in Redriver.
- `tools/wspc/city2glb.py <game folder> <out.glb> --cells 1,2,3 | --all [--time DAY]`: the city as GLB straight from the disc files (`--all` is 80 MB). The origin is moved to the middle of the city grid.
- `tools/wspc/city2data.py <game folder> <out folder> [--time DAY]`: `city.wcity`, `graph.wgph` and `textures/*.png` (see Export format).
- `tools/wspc/wcity_map.py <data folder> <out.png> [--units-per-pixel 512] [--buildings] [--graph]`: top-down map from the exported data.
- `wspc_city.py`, `wspc_prims.py`, `wcity_io.py`: the readers the scripts share (cells, primitives, the `.wcity` file); `wspc_extract.py` has `toc_entries`, `read_entry`, `mrg_files` to read single files from the disc without unpacking all of it. The scripts import each other, so run them from their folder or with the folder on `PYTHONPATH` (not with `python -I`).
- The record layouts above come from reading the loader code of the executable and checking the result on the data; the polygon drawing itself is hand written assembly and was not needed.

## Learned

- Quad primitive records are not all the same size, so a wrong size gives plausible vertices with wrong faces (spiky body). Check the distance between group pointers and the planarity of the quads.
- A field that is "plausible" is not proof: the quad material index was wrong for a long time because the first u16 after the vertices looks like an offset too (it is the 4th vertex). Check that every index stays below the list size (min 0, max count - 1).
- The `DOB` header pairs `(size, tag)` at the file start look like a table of three entries but are the first chunks of the tree; sizes include the 8 header bytes.
- A first guess "the header gives the vertex count" was wrong: the sub-meshes have their own header with `nVerts` at `+4`.
- Source coordinates to glTF is a rotation, not a flip of one axis (a flip mirrors the model and the text on it).
- Python helpers that write a file they read: read into a variable first, else the file is truncated.
- A table that has a `u8` index next to the full value (vertex record vs. header offsets) needs a test over more than 255: the byte was enough for the cars and broke every city mesh (spiky roads). Draw a top-down raster of one cell before trusting a face list.
- A size guess from one file ("`flags2 == 128` = 36 bytes") can be wrong: check it on another group before building a rule on it.
- A texture size that does not fit `w * h / 2` plus a palette of `32` bytes is a hint for another palette format: here it was `64` bytes (RGBA32).
- The relocation loop of the cell loader (the code that adds the base to every pointer) gave the whole cell layout in one read; prefer that to guessing from hex dumps.
