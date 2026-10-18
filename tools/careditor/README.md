# Car editor: how to fix a car in one go

Read this before touching lamps or wheels of a car. Format of car.json and the game side: `docs/dev-notes/car-editor.md`.

## Tools

| Tool | Use |
|---|---|
| game `-carexport <dir>` | writes `<city>_<model>/car.json + car.glb` of every car of the loaded city (D2: `-mission 50|20|26|32`, D1: `-city 4..9 -mission 50`) |
| `careditor-standalone.html` | open in Chrome / Edge, *Open folder*, move points, Save (rebuild: `npm run build` here, copy `dist/index.html`) |
| `carapi.py` | `get`, `set`, `nudge`, `radius`, `shots` (game screenshots) |
| `careshots.py` | exact check: rear + front, orthographic, 2 px per unit, ruler in engine units |

The game reads the folder with `-carsdir <dir>` (or `CARS/` next to the exe).

## Routine per car (do it in this order, once)

1. `careshots.py --cars <dir> --out <out> <id>`. One picture: rear on top, front below. Engine x on the top ruler (mirrored in the
   rear view, left of the picture is +x), engine y on the left ruler (up is negative).
2. Per lamp: read the centre of its housing off the ruler and `carapi.py set <id> <point> x y z`. A pair moves its mirror image too.
   Keep z (depth), it is only used for the flare depth. Move the markers, not the housing: x and y are all that matters.
   - brake light: the red part of the rear housing; reverse light: next to it (inner); indicator back: outer end of the housing
   - headlight: the lamp glass in the front end (not the grille); indicator front: the amber lens, or the bumper corner when there is none
   - markers of the other end shine through the body (no depth test): a marker in the wrong picture is not a bug
3. Check in the game once: `carapi.py shots <id> --views front,rear`. The flare has to sit on the lamp. Night and fog hide errors, use the
   daylight cities (Miami) with `-lightson` (carapi does).
4. Wheels: the wheel circle has to sit in the arch; the exported wheel values are the ones the game draws, they were right for all cars.

## Pitfalls (all seen)

- The game must be started with a car in the showroom; a second game process or a leftover `SCREENSHOT.BMP` makes `shots` fail: kill
  `REDRIVER2_dev` first.
- Textures of a few cars are wrong in the game itself (the older game's training cars: flat green / missing pages). Lamps of those cannot be
  judged by eye; say so, do not guess.
- One model has no car in it (Newcastle model 0); skip it.
- Colour detection of lamps in the texture (carsuggest idea) does not work: the lamps are painted into the front and back textures
  (CFNT / CBCK), they are not separate polygons. Use the ruler.
- The 50 % screenshot sheets are too coarse for lamps (10 to 25 units off). Always use `careshots.py`.
- D2 cars come with correct lamp data; only the older game's cars (borrowed data) need the routine.
- Do not commit the exported or corrected folders (derived from the game data).
- Never `Write` car.json by hand: `carapi.py` / the editor also rewrite the `raw` block the game's import is checked against.
- A shell here collapses `\n` in inline python: put scripts in a file.
