# Car editor: planned extensions (not built)

State: the editor (tools/careditor) edits lamp and wheel positions; the game reads them from CARS/<city>_<model>/car.json (carovr.c).
Wishes of the user for later: sizes, ride height, damage textures, mesh shaping, a texture per face group, a texture and mesh import
into the game, so cars can be replaced 1:1 by nicer models.

## Engine limits (from the code)

- A polygon points at a vertex with one byte: at most 256 vertices per car.
- All triangles of all resident cars share one buffer (cars.c MAX_CAR_POLYS = 200 * 2 per slot): about 400 triangles per car.
- Two 256x256 texture pages per car, 4 bit, 16 colours per palette: a new texture has to be quantised on import.
- Damage: a per polygon uv offset (damageLevel, steps of 64 in u), the damage variants sit as strips next to the clean texture in the
  same page. The damaged model has the same vertex count as the clean one. The dent volume is in the denting zones.
- Existing: wheelSize (one value per car), lamp positions, ride height through the cog y (the older cars already use a `lower` value).
  Missing: a size per lamp, a wheel radius per axle (new fields in car.json and carovr.c).

## Steps (each ends green and is checked in the game)

1. Sizes and height: lamp size, wheel radius per axle, ride height in car.json, editor fields, override in the game.
2. Show damage: the export writes the damage uv's and strips, the editor gets a damage slider (0 to 3).
3. Face groups: the export marks groups (bonnet, roof, windows, front, rear, side) as glb materials (heuristic on normal and position).
4. Mesh shaping in the editor: move vertices, scale a group, a texture per group, live count against 256 vertices / 400 triangles.
   Splitting and extruding is better done in Blender.
5. Mesh and texture into the game: car.json + glb + png loader (the content override path of the car models exists), converter
   (4 bit quantising, polygon lists, winding reversed; same direction as the WSPC converter).

Risks: step 5 is the heaviest; damage for a new mesh needs a rule for the damaged model (same vertices, moved).
