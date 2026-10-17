# Live day / night LEV switch for the day night cycle

## 0. Status

- 2026-10-15: research done (section 2). Part 1 done: `SwapCitySpool` (system.c, reads the other LEV's spool lump once into a second 16 MB buffer, swaps buffer + `SpoolLumpOffset`, `gCitySpoolNight`) and `SpoolSwapDayNight` (spool.c, flushes queued requests, frees the area texture slots, reloads `LoadInAreaTSets` / `LoadInAreaModels` of `LoadedArea`); test option `-nightswap <steps>` (main.c). Seen: Havana buildings switch to night pages live; 61 swaps in 1800 steps without a crash; Vegas start area shows no visible change (its night look is in the models and permanent pages, Part 2/3).
- 2026-10-15: Part 2 done. Measured (`levtexinfo.py`, `levspoolinfo.py`): the TEXINF tables, permanent page ids and special page ids are equal day and night, only the page bytes (sizes) differ; Chicago's SPOOLINFO differs only in unused area rows (night file has garbage there), so SPOOLINFO is NOT swapped. `ReloadPermanentTPages` (texture.c) loads the permanent pages and the player car's special pages from the other TPAGE lump into the VRAM place and palette rows they have (decoded from `texture_pages` / `texture_cluts`, skipped when the palette count differs); `LoadOtherCityLev` (system.c) reads both TPAGE lumps and page lists once. The minimap tiles live on the permanent OVERHEAD page: `ReloadOverheadMap` (overmap.c) puts them and the map palette back after a swap. Verified in Chicago, Havana, Vegas, Rio: night start, swap to day and back is pixel identical to a plain night start (minimap too), 61 swaps in Chicago without a crash.
- 2026-10-15: Part 3 done for MODELS. `ReadModelPatches` / `BuildModelPatches` (system.c) diff the MODELS sub lump of both DATA2 lumps once and keep one entry per differing byte (address in the loaded model, day and night value); `SwapCitySpool` writes the values of the new side. All differences lie before the polygon end (vertices and polygons), so models copied by `GapFillModel` (roofs appended there) patch at the same offsets; `tri_verts` (runtime flags) is skipped. Log `day night LEV: N models differ (B bytes)`: Chicago 12 (160), Havana 46 (699), Vegas 198 (24578), Rio 35 (1188), as measured. Day start, swap to night and back is pixel identical to a plain day start in all four cities; one swap changes ~21% of the picture. `SetCityType` now always reads the level again after a swap (restart of a mission: the buffers and patch addresses belong to the old load); not tested in a real restart. CAR_MODELS NOT patched: every city has a few differing car polygon bytes (night lamp textures), but car polygons are rebuilt into `CAR_MODEL` lists from the DATA1 buffer, which is gone after load; cars keep the look of the file the level started with. Part 4 note: `ProcessModel` sets `litSprites` only when the level loads at night (Havana / Vegas sprites). Next: Part 4.
- 2026-10-15: Part 4 done. `EnterTimeOfDay` (daycycle.c) calls `SpoolSwapDayNight` when the night state differs from `gCitySpoolNight` (21:00 to night, 7:00 to dawn) and `UpdatePermanentSpriteLights` (models.c, same rule as `ProcessModel`: `SpriteLitAtNight`); `LoadGameLevel` loads the night file whenever the level starts at night, also with the cycle. `SpoolSwapDayNight` logs its time: first swap (reads the other LEV, warm file cache) 4.0 to 6.8 ms in Chicago, Havana, Vegas. Seen: Chicago `-cycle 1 -hour 20` swaps at 21:00 and the picture is 1.3% off a plain night start (centre only); `-hour 6` starts in the night file and swaps to day at 7:00; `-cycle 24 -hour 23` is pixel identical to `-tod 3`; Miami (D1) passes 21:00 without a swap. Not measured: a cold disk cache.
- Goal: while the day night cycle runs (`daycycle.c`, Take a Ride), the city switches from `LEVELS\<CITY>.LEV` to `NLEVELS\<CITY>.LEV` at 21:00 and back at 7:00, with no black screen and no level reload. Lit windows at night, the night look of trees and models. D2 cities only (D1 cities have no night LEV).
- User decision (2026-10-15): live switch, not a reload behind a fade.

## 1. Where things live now

- `LoadGameLevel` (main.c): `SetCityType` picks the file (`CITYTYPE_NIGHT` = `NLEVELS`); the cycle always loads the day file (`!DayCycleActive()`).
- City lumps (`citylumps[GameLevel]`, `CITYLUMP_*` system.h): 0 DATA1 (load time: MAP, TEXTURENAMES, MODELNAMES, MOTIONCAPTURE, PALLET, CAR_MODELS, TEXTUREINFO), 1 TPAGE (permanent texture pages, `LoadPermanentTPages`), 2 DATA2 (in memory: MODELS, SUBDIVISION, LOWDETAILTABLE, OVERLAYMAP, SPOOLINFO, CHAIR, STRAIGHTS2, CURVES2, JUNCTIONS2_NEW), 3 SPOOL (regions, area texture pages and area models; read as one 16 MB buffer `g_CurrentLevelSpoolData` by `UpdateSpoolPC`, spool.c, addressed by absolute sector minus `SpoolLumpOffset`).
- Area data: `ProcessSpoolInfoLump` (spool.c) -> `AreaData[]` (gfx_offset, num_tpages, model_offset, model_size), `AreaTPages` (16 ids per area). `LoadInAreaTSets(area)` requests the area pages (`SendTPage`, slots `tpageslots[]`, skips slots that already hold the id), `LoadInAreaModels(area)` respools the area models (`SetupModels`).
- Texture info: `ProcessTextureInfo` (texture.c) copies `TEXINF` arrays per page into `tpage_ids[i]` (malloc on PC).
- Models: `ProcessMDSLump` (models.c) keeps the MODELS blob in place, `modelpointers[i]`; `GapFillModel` (gapfill.c) may return a new pointer for D2 buildings (roof quads appended).

## 2. Measured differences, day against night (`levsub.py`, `levmodels.py` in the session scratchpad)

| | Chicago | Havana | Vegas | Rio |
|---|---|---|---|---|
| file size | equal (10915840) | equal | equal | equal |
| DATA1 differing sub lumps | TEXTUREINFO | TEXTUREINFO | TEXTUREINFO, CAR_MODELS | TEXTUREINFO |
| TPAGE lump size day / night | 565248 / 555008 | | 499712 / 477184 | |
| MODELS: models that differ (same size, same header) | 12 | 46 | 198 | 35 (2 bounding spheres) |
| MODELS: what differs | byte 16 of poly types 20/21 | bytes 16 of types 20/21/5 | bytes 4..15 of type 11 | bytes 17/19 of 20/21/5 |
| SPOOLINFO differing bytes | 93 (from offset 100: AreaData / AreaTPages) | 0 | 0 | 2 |
| SPOOL lump | same size, ~20% of sectors differ (area pages, area models) | | | |

- Palettes (PALLET) are equal. Model counts, model sizes and model headers are equal: night models are byte patches of the day models (colours / texture refs inside polygons).
- The TPAGE lump has a different size: the permanent page set (`nperms`, `permlist` in TEXTUREINFO) differs. The SPOOL lump starts 5 sectors earlier in the night file (`SpoolLumpOffset`).

## 3. Approach

At level start (cycle active, D2 city) read the night file's DATA1, TPAGE, DATA2 and SPOOL lumps into memory next to the day ones and build patch lists; the switch then swaps data in place, without reallocations:

- MODELS: per model index, the byte ranges that differ (raw day vs raw night blob), applied to `modelpointers[i]` (also when `GapFillModel` moved the model: the patch offsets are inside the original part). Store both values, so the switch back restores the day bytes.
- TEXTUREINFO: swap the `tpage_ids[i]` contents (same `texamount` per page, check) and `nperms` / `permlist`.
- SPOOLINFO: patch the differing bytes of `AreaData` / `AreaTPages` (same layout).
- SPOOL: second 16 MB buffer for the night spool lump; switch `g_CurrentLevelSpoolData` and `SpoolLumpOffset` together.
- Permanent pages: reload from the other file's TPAGE lump (`LoadPermanentTPages` reads from a sector cursor: give it the night lump).
- Current area: mark the area slots free (`tpageslots[slot] = 0xff` for the area slots) and call `LoadInAreaTSets(area)` and `LoadInAreaModels(area)` for the loaded area so the visible city changes at once.
- CAR_MODELS (Vegas): patch like MODELS, after `ProcessCarModelLump` (check whether it changes the data in place first).

## 4. Parts (one green commit each, all gated by `DayCycleActive()` and `!IS_DRIVER1_CITY(GameLevel)`)

1. Live re-spool (riskiest): load the night SPOOL lump into a second buffer, switch buffer + `SpoolLumpOffset`, force the current area to reload its pages and models. Test: a debug key / `-nightswap <step>` that swaps at a given simulation step; `-shot` before and after; no crash over 2 minutes of driving with swaps every 10 s (`playLog`).
2. Texture info + permanent pages + SPOOLINFO patches. Test: windows lit in Chicago, Havana, Vegas, Rio night shots after a live swap equal (by eye, crop) to a normal night start (`-tod 3` without the cycle).
3. MODELS + CAR_MODELS byte patches. Test: count of patched models logged (`night lev: patched N models`) equals section 2; Vegas neon models.
4. Hook into the cycle: `EnterTimeOfDay` swaps at 21:00 (night) and 7:00 (day); remove the `!DayCycleActive()` special case from `LoadGameLevel` (start in the night file when the cycle starts at night). Remove the Chicago tree hack question (draw.c `lightLevel *= 2` is right again with the night file). Measure the hitch of a swap (ms, `-shot` frame cost log).

## 5. Open questions

- Does `SendTPage` / PsyCross pick up a re-uploaded VRAM page at once (texture cache)? Part 1 shows it.
- Night SPOOLINFO area offsets in Chicago differ: verify the night area offsets point inside the night spool buffer only (no mixing with the day buffer).
- Memory: a second 16 MB spool buffer and the night DATA lumps (~1 MB): fine on PC.
