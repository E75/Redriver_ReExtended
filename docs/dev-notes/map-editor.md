# Map editor (in-game) with command port for the AI

## 0. Status
- 2026-10-16: user decision: build the editor INSIDE the game (not three.js): no export/texture decoding, edits are tested at once in the real engine. Text HUD + hotkeys first (no new dependency, no ImGui). A command port lets Claude do the same operations. Edits live in `<CITY>.EDIT.json` (source LEV untouched), applied at level load. Start with Miami (D1), then all cities.
- 2026-10-16: part 2 done (move/rotate/copy/paste/add model + undo; extras per cell via EditorNextExtra hook in cell.c). Tested scripted in Miami (-editortest); keys/mouse by hand not yet.
- 2026-10-16: part 3 (view + remove) done: K shows collision boxes of props in front of the camera (red, selected prop yellow, J picks a box white), H removes that box from the model (all instances), Z undoes. -editortest writes MAPEDITOR_BOXES.txt (box vs model size per model). Miami: 298 boxes in 205 models; kerb models (KERB3, DETAIL_KERB, LOW_KERB, IROADB...) have boxes 400-495 high on a 138 high kerb = suspected pavement walls, not yet confirmed by driving. Per-instance removal and hole scan still open.
- 2026-10-16: part 4 done: F5 saves EDITS/<CITY>.EDIT.json (working dir of the game; remove = raw packed words of deleted originals, add = type x y z yang, boxRemove = model + collision packet). Loaded at every level load (main.c LoadGameLevel, also without -editor): deleted markers are re-applied each time a region is unpacked (spool.c RebaseCellData, so streaming no longer undoes a delete), removed boxes at the end of ProcessMDSLump, added props appear through the cell iterator. Tested Miami round trip (applied 1 removed + 1 box), Chicago unchanged. Not yet: straddling objects (index below num_straddlers), loaded deletions cannot be undone in the editor, night LEV swap of D2 cities, Release builds without the debug overlay never call the editor draw (edits still load).
- 2026-10-16: editor UX: top-down camera by default (WASD pan, Q/E zoom, TAB = free look), free mouse pointer picks (ray through the pointer, checked against a projected cross at 60%/30% of the window), LMB selects, pressing on the selected prop and dragging moves it (one undo step on release), grey left panel with keys and selection (ot slot 1 behind the text), pad and keys cleared while the editor is on (pad.c ReadControllers), game HUD bars hidden (overlay.c). Keys/mouse driving not tried by hand.
- 2026-10-16: part 5 done as a FILE port instead of TCP (same commands, no socket code): EDITCMD.txt in, EDITRES.txt out, see CLAUDE.md "Map editor". Next: clean up the invisible walls of the D1 cities (analysis of boxes vs model size, then cutbox/cutmodel + F5), hole scan and the roof/ground fill question.
- 2026-10-16: always 2D (user): top-down at 1200..5000 units, void cleared to dark grey, view cone opened (gFrustumOff), narrow panel (F1 = all keys), TAB = free look only on request. Found while doing it: the cell at the start of Miami (117000,-21000) shows a hole in the ground = candidate for the hole scan.
- 2026-10-16: zoom out to 60000, fast travel (car follows the view, tested 170000 units away on the Miami causeway), BACK button, Debug Options - Map Editor. D1 kerb walls: rule = model height <= 250, box top at the 400 clamp (or higher), strip >= 1000 long and <= 800 thick, not BARRIER*; cut via `cutbox` + `save` (Miami 14 boxes, Frisco 6, LA 11, New York 7) into EDITS/<CITY>.EDIT.json of the test install (not committed: derived from game data). Not driven yet.

## 1. Goal
- Fly a free camera through any city, pick props/tiles with the mouse, move / copy / delete them, add props from a model list.
- See collision boxes (invisible walls) of objects and remove them per model or per instance.
- Find holes (cells without ground tile) and fill them quickly by copying a nearby tile.
- Command port: Claude sends the same operations (JSON lines) and reads state, plus a hole scan.

## 2. Design
- `editor.c/.h` (PC only, option `-editor`, key toggle): free camera (WASD + mouse look, shift = fast), cursor ray, HUD text, selection highlight. Gated by `gEditor`; off = no code path changes.
- Picking: walk the cells along the cursor ray (`GetFirstPackedCop`), test the ray against the model bounding box/sphere (`UnpackCellObject`, `modelpointers`), nearest wins.
- Delete = existing "deleted object" marker (`value == 0xffff && (pos.vy & 1)`, `cell.c`). Move = delete + add. Add = per-cell extra list yielded by `GetNextPackedCop` after the normal list (so draw and collision both see it), objects packed like `PACKED_CELL_OBJECT`.
- Collision: boxes from `COLLISION_PACKET` of `model->collision_block` drawn as lines; edit file removes a box of a model or turns collision off for one instance (hooks in the `objcoll.c` loops).
- Edit file `<CITY>.EDIT.json`: `{version, add: [{model,x,y,z,yang}], remove: [{cellX,cellZ,model,x,y,z,yang}], moveIsRemoveAdd, noCollisionObjects: [...], modelBoxesOff: {model: [boxIndex]}}`; identity of an object = model + position + yang (survives reconversion). Applied after region unpack for editor and normal play alike (no file = unchanged).
- Command port: TCP localhost (PsyCross/SDL_net? check for an existing socket lib first, else plain Winsock under `#ifdef _WIN32`), JSON lines: `pick`, `list objects in box`, `move`, `copy`, `delete`, `add`, `box off`, `scan holes`, `save`, `goto x z`. Same code path as the mouse.

## 3. Parts (one green commit each)
1. Editor mode: `-editor`, free camera, mouse ray, picking + selection box, HUD, delete (marker). Test in Miami: select and delete a prop.
2. Move / rotate / copy / add with a model list; per-cell extra list; undo.
3. Collision boxes view + removal; hole scan (ground missing) with markers.
4. EDIT.json save/load and load-time apply (Miami hole filled, wall removed, Chicago unchanged via `-shot`).
5. Command port for Claude + docs in CLAUDE.md.
6. Other cities, polish.

## 4. Open
- Cause of invisible walls on D1 pavement: model collision boxes or steep slope planes; part 3 shows both.
