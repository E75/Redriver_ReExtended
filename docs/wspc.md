# WSPC data formats and DLC

WSPC uses its own file formats (no `.LEV`/`.CAR` files). Goal: use its cars and map in Redriver2_ReExtended as an optional DLC.

All offsets below are in little endian. Nothing of the game data is committed; point the tools at your own copy of the files.

## Status

| Part | State |
|---|---|
| Archive (`POLICE.TOC` + `POLICE.IMG`) | decoded, `tools/wspc/wspc_extract.py` |
| `.MRG` containers | decoded (same tool) |
| `.DOB` chunk tree, textures (several pages), meshes | decoded, `tools/wspc/dob2glb.py` (GLB with vertex colours + textures) |
| Police car | looks right in the three.js viewer (body, window glass, light bar, stripe, wheels) |
| Other cars (`VEHICLES\*.DOB`) | same format, not checked one by one yet |
| Damage states, `.DAT` vehicle values, node rotations | not done |
| World props (`MISC.DOB`: 142 nodes, signs, arrows, lamps) | converts and shows in the viewer, positions absolute |
| **City terrain / roads** | streamed from `DAY.STR` etc. (53 MB), described by `CITY.HDR`; **not decoded** |
| Minimap | no ready made image found (see below) |
| Import into Redriver | not started |

## Archive layout

- `POLICE.TOC` (8248 bytes): entries of `name\0`, `u32 offset`, `u32 size`, back to back. 304 files, offsets into `POLICE.IMG` (266 MB). Directory separator is `\`.
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

Axes: the source is x right, y down, z forward, which is a right handed system, so glTF is a **rotation** `(x, -y, -z)` and the triangle order stays as in the file (the first try with `(x, -y, z)` mirrored the car: text on the doors read backwards). Vertex order in the file is counter clockwise when seen from outside (90 of 93 body triangles have an outward normal).

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
n x 4             vertex: u8 r, g, b (0x80 = 1.0), u8 vertex index      (n = 3 triangle, 4 quad)
triangle: u16 material offset (index * 16), u16 flags
quad:     u16 4th vertex offset (index * 8, repeats the vertex above), u16 material offset (index * 16), (type 15: u32 0x10)
```

Record size by group type: type 3 triangle 24 bytes, type 7 quad 28, type 11 triangle 24, type 15 quad 32 (verified over all cars from the distance between the group pointers). A group with `flags2 == 128` was 36 bytes (seen once, not checked). Quads use the order `v0 v1 v2 v3` as a strip: triangles `(0,1,2)` and `(1,3,2)`; tested with a planarity check, all 77 quads of the police body are flat in this order and none in the fan order.

The material number of a quad is the **second** u16 after the vertices. Taking the first one (the repeated vertex offset) gave textures on the wrong places. Check after the fix: in 113 of 115 meshes of `MISC.DOB` the largest material index is exactly `count - 1` (before: more than half went beyond the list).

## City

- `MISC.DOB` (in every `.MRG`) holds only props: 142 nodes, ≈ 1200 triangles (arrows, signs, lamp posts, cones, boards). Positions span x -6879..11731, z -3647..9377 in the same units as the cars. No roads or buildings. Two nodes (53 and the mesh at pool offset 0x341) are not normal meshes (skipped by the converter).
- The terrain and roads are streamed from `DAY.STR`, `DAWN.STR`, `NIGHT.STR`, `RAIN.STR`, described by `CITY.HDR`. Every `.STR` starts with a copy of `CITY.HDR` (with a leading size dword `0xC2D4`) and has 25846 sectors of 0x800 bytes. Sector 1.. holds u16 lists, later sectors look like 4 bit texture data (`cccccccc`, ...) and packed or compressed data. Only 12 sectors look like a DOB chunk start, so the streamed geometry is another format.
- `CITY.HDR` (words, offsets must be reduced by 4 for the in-memory copy): `w0 = 677` entries, `w1` table of 677 x 12 bytes (`u32 packed, u32 pointer to a u16 list, u32 packed`), `w2 = 3473` (count of the u16 list at `w3`), `w4/w5` index lists, `w6 = 677`, `w8` table of pointer arrays. Looks like a spatial grid with 677 cells and per cell vertex index lists, the packed values probably hold the sector of the cell in the `.STR`. Not decoded.
- `_NOTINUSE_MAP\_CITYMAP.MAP` (551 936 bytes = 1024 x 539, mostly nibbles `0x11`, `0x44`, `0x22`) is not a plain bitmap in 4 or 8 bit at width 512, 1024 or 2048. Not decoded; the name says it is not used by the game.
- Minimap: no minimap image was found in the data. A minimap in the Redriver style has to be rendered from the city geometry, so it depends on decoding the stream above.

## WSPC DLC in Redriver (decisions)

Same idea as the Driver 1 DLC (`src_rebuild/d1dlc`): optional, the player supplies their own data files, nothing of the game is committed, the base game is not affected when the DLC is off or missing.

- **Engine and gameplay stay Redriver 2.** Only the map and the cars come from WSPC.
- **Modular:** own library `src_rebuild/wspcdlc/` (detect a `WSPC\` data folder, convert in game, `config.ini` section `[wspcdlc]`). The menu code in `FEmain.c` stays a thin hook.
- **Menu:** Options → Gameplay → Secret → Extended Settings → Volumetric Effects → D1 DLC → **WSPC DLC**, with items `ON`, `CHECK DLC Folder`, `Convert DLC`.
- **Driving behaviour:** every WSPC car gets the Redriver car type that fits it best (taxi → taxi, police → cop car, ...). This mapping lives in its own table/file, not in the converter.
- **Car selection:**
  - WSPC cars are selectable in every city.
  - On the WSPC map, Redriver cars (Driver 1 and Driver 2) are selectable too.
  - Driver 1 and Driver 2 stay strictly separate. **In the Driver 1 map no Driver 2 cars are selectable, for now.** Mixing them may come later.
- **Work split:** cars first (this document, `tools/wspc/dob2glb.py`); map research, import and conversion scripts run in a separate session.
- All WSPC work goes on branch `feature/wspc-import` (git worktree next to the main checkout).

## Tools

- `tools/wspc/wspc_extract.py <game folder> <out folder>`: all files from `POLICE.IMG` and every `.MRG` unpacked.
- `tools/wspc/dob2glb.py <file.DOB> <out.glb> [node,node,...]`: GLB with the listed nodes (default all), positions scaled by 1/250, axes rotated to glTF, vertex colours and one PNG per used (page, CLUT). Police car: nodes `2,3,6,7,8,9,10,11,12,13,14`. Check it in the viewer first (`bk view` of a folder with the `model.glb`, three.js previewer of the blender skill) before using it in Redriver.

## Learned

- Quad primitive records are not all the same size, so a wrong size gives plausible vertices with wrong faces (spiky body). Check the distance between group pointers and the planarity of the quads.
- A field that is "plausible" is not proof: the quad material index was wrong for a long time because the first u16 after the vertices looks like an offset too (it is the 4th vertex). Check that every index stays below the list size (min 0, max count - 1).
- The `DOB` header pairs `(size, tag)` at the file start look like a table of three entries but are the first chunks of the tree; sizes include the 8 header bytes.
- A first guess "the header gives the vertex count" was wrong: the sub-meshes have their own header with `nVerts` at `+4`.
- Source coordinates to glTF is a rotation, not a flip of one axis (a flip mirrors the model and the text on it).
- Python helpers that write a file they read: read into a variable first, else the file is truncated.
