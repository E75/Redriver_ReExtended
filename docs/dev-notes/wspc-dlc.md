# WSPC DLC: cars (plan)

Branch `feature/wspc-import`. Decisions (menu, car selection, modular layout) and the source data formats are in `docs/wspc.md`; this file is the build plan for the cars. The map has its own session and notes.

Rules: the base game must not change when the DLC is off or missing. Never commit game data or data derived from it. Each step ends on a green build plus a check in game; one commit per step.

## What the engine already offers (read this session)

- Car models are `MODEL` structs (`engine/mdl.h`): header, vertices, polygon block, normals, point normals, collision block. A car has three models: clean, damaged, low detail.
- `models.c ProcessCarModelLump` builds the resident cars from the level lump. With `gContentOverride` it first looks for `LEVELS\<city>\CARMODEL_<n>_<CLEAN|DAMAGED|LOW>.MDL` (`cars.c LoadCarModelFromFile`) and uses that file instead of the lump.
- `models.c LoadCarModelIntoSpecialSlot(model, lump, src1, src2)` puts a model into the special car slot at run time (used by the car spawner and the D1 cars). `carspawner.c PrepareModelInner` shows the three paths: D1 city, foreign car (`model >= 8`), D2 car of the level.
- Textures: a car owns two texture pages plus palettes (`ForeignPageId(0/1)`, `RemapModelPages`). D1 cars show how a foreign page pair is imported (`ImportD1Car`).
- Cosmetics (wheels, lamps, `car_cosmetics`), denting (`DentCar`, damage zones) and driving values come from tables per car slot; D1 needed `D1ScaleCarCosmetics` and a donor car for physics (`D1PickDonor`).

## Plan

1. **Converter (tools first):** `tools/wspc/` script writes, per WSPC car, the three `MODEL` files plus a 2 page texture set from the source `.DOB` (positions to engine units and axes, quads to triangles or quads, 4 bit pages and palettes mapped to the 2 page layout). Check each car in the three.js viewer, then in game via the `CARMODEL_*` override.
2. **Behaviour table:** own file (`wspcdlc/wspc_cars.cpp`): WSPC car name to Redriver car type (taxi to taxi, police to cop car, ...), size/scale factor, wheel positions, lamp positions. Donor for physics, as `D1PickDonor`.
3. **Module `src_rebuild/wspcdlc/`:** `WspcDlc_CheckData`, detect `WSPC\`, convert in game, `config.ini [wspcdlc]`, same shape as `d1dlc.h`.
4. **Menu:** `FEmain.c` screen under Volumetric Effects (ON, CHECK DLC Folder, Convert DLC), thin hook into the module.
5. **Car select:** WSPC cars in all cities; on the WSPC map also Redriver D1 and D2 cars; D1 map gets no D2 cars for now.
6. **Damage and lights:** nodes 4 and 5 of the source are body copies (damage states); denting zones as for D1.

## Step 1 status

`tools/wspc/wspc2car.py <car.DOB> <out folder>` writes `CLEAN.MDL`, `DAMAGED.MDL`, `LOW.MDL` (same model for now), `TEXTURE.BIN` (per page: palette count, 15 bit palettes, 4 bit pixels) and `CAR.INFO` (counts, bounds, wheel centres and radius).
- Model layout (engine order): 36 byte header, vertices (8 bytes), point normals (8 bytes, 4096 = 1.0), polygons, face normals. Polygons are FT3 (id 20) and FT4 (id 21), 20 bytes each, quads go round the edge (source strip order 0,1,3,2), texture set = page 0/1, texture id = palette number of that page, colour = mean of the vertex colours. Source units are used 1:1 (about 250 per metre, like the Redriver lamps).
- Wheels are left out of the body (the engine draws its own wheel model at the wheel positions); the damage copies and the two hull boxes of the source are left out. Node choice is guessed from the node shapes, `--body` / `--wheels` override it.
- Checked: the converted files read back by an independent script (`.MDL` + `TEXTURE.BIN` to GLB) look right in the three.js viewer (police car, 17 palettes, 142 vertices, 123 polygons). 16 of 17 vehicles convert (1 page, at most 19 palettes, at most 176 vertices); the tank has 348 vertices and no wheels (tracks), so it needs its own handling.
- Not done: Gouraud polygons (the source has vertex colours, FT polygons give one colour), damaged model from the source damage copies, low detail model, per car node tables after looking at each car.

## Open

- How many pages a WSPC car needs after mapping to 2 pages (police car uses several pages with different palettes).
- Source `.DAT` vehicle values are not decoded; the behaviour table is by hand until they are.
- Is `gContentOverride` per city folder enough for a WSPC car in every city, or do WSPC cars go through the special slot (spawner path)? Leaning to the special slot: one code path for all cities.
