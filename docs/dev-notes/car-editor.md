# Car editor (tools/careditor.html)

One HTML file (three.js r160 from a CDN, no install). Open it in Chrome or Edge, press *Open folder*, pick the export folder.
Other browsers: *Open (read only)* and Save downloads car.json.

## Export

Run the game with `-mission <n> -carexport <dir>` (n: 50 Chicago, 20 Havana, 26 Vegas, 32 Rio). The showroom of the car spawner
steps through every car of the loaded city (the civilian and cop cars of that city, the specials of all four cities, the secret car)
and writes `<dir>/<id>/car.json` + `car.glb`, then the game ends. Run it once per city; the output is derived game data and is not
committed. Code: `Game/C/carexport.c`, the loop in `carspawner.c` (`ExportTick`).

## car.json (schema `redriver-car/1`)

All numbers are engine units in the engine car frame: x sideways (+x = left of the car), y down (up is negative), front is -z,
origin = car position. The body is drawn at the position plus `cog`; the exported mesh already contains that shift.

- `cog`, `wheelRadius`
- `wheels[4]`: `pos` = where the wheel model is drawn (`hubX` = side offset of the wheel model, `compression` = suspension compression
  at rest); order of the game: 0 and 1 on the +hubX side, even = front axle
- `lamps`: `headlight`, `brake`, `indicatorFront`, `indicatorBack`, `reverse` as `{ "L": [x,y,z], "R": [x,y,z] }` (the game mirrors one
  value over x = cog.x), `exhaust` and `police` as single points. All absolute in the car frame.
- `raw`: the values as the game stores them (CAR_COSMETICS: lamp = value + cog, wheel pos = (hubX - disp.x, 14 - compression - radius - disp.y, -disp.z)).
  The editor rewrites `raw` from `lamps` / `wheels` when it saves, so the import can use either.

## car.glb

Standard glTF (y up, front +z): the engine frame turned by (x, -y, -z), triangle corners 1 and 2 swapped (the engine winding is the
reverse of glTF). One material, one atlas texture (every page + palette the polygons use, cut to the used rectangle), `COLOR_0` for the
flat colour polygons. Colour 0 of a palette is transparent (alpha mask).

## Editor

Click a point or pick it in the list, drag the arrows or type x / y / z. *mirror L/R* moves the other side of a lamp pair (or the
other wheel of the axle) along. Views front / side / rear / top, optional orthographic camera, double sided and wireframe toggles
show a wrong winding or a missing texture. *Reset point* goes back to the last saved value.

## Command line API (tools/careditor/carapi.py)

For fixing cars without the editor: `list`, `get <id>`, `set|nudge <id> <point> x y z` (a lamp pair or wheel pair moves its mirror image
too, `--single` not), `radius`, and `shots <id>... --game <install> --out <dir> --views front,side,rear --sheet x.png`, which starts the
game in the car showroom (`-carsdir <dir> -spawnid <id> -spawnangle <n> -lightson -shot 150`) and cuts out the car. The game reads the
same folder (`CARS/` or `-carsdir`), so a fix is: shots, compare the lamp flares with the housings, nudge, shots again.

## Exact lamp check (tools/careditor/careshots.py)

`careshots.py --cars <dir> --out <dir> <id>...` renders the rear and the front of a car from the editor (orthographic, 2 pixels per
unit, ruler in engine units, markers on top; needs Playwright + Edge and `npm run build`). Read the housing of a lamp off the ruler and
fix the marker with `carapi.py set`. The check in the game is `carapi.py shots` (flares against housings). The cars of the older game
borrow their lamp data and needed this; the original cars were already correct.

## Using corrected cars in the game

The game reads `CARS/<city>_<model>/car.json` next to the exe, or the folder of `-carsdir <dir>`. A folder without that file is not
touched. The corrected files are derived from the game data and are not committed.

## Not done yet

- Import: the game reading `car.json` (corrected lamps / wheels) instead of the built-in values.
- Other cars than those of the four cities, new cars from a mesh + texture.
