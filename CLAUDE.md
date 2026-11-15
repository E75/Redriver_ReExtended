# Redriver ReExtended - notes for coding agents

## Rules

- Chased cars in Driver 1 missions (`d1chase.c`, `civCornerBrake` cars in `civ_ai.c`) never stand still on their own: they keep driving until the damage bar is full (100 %), and only then stop for good.
- One ram of the player fills at most a sixth of a chased car's damage bar (`D1ChaseCar_LimitHit`): at least 6 rams before the mission is done.
- A chased car always looks for a way through: it runs red lights, gives no way, overtakes and slips past slow cars, weaves away from the player close behind, turns off at junctions away from the player, and gets going again within 1 s when stuck: forward when its front sensors are free, backing up only when the way ahead is shut. It always prefers the road: off the road (pavement, grass) it heads for the nearest lane or the last road spot, lines up with the lane at low speed and only then speeds up again. Every change to the civilian AI, the mission code or the chase code must keep this.
- It drives fast but with sense: full speed on straights, slowing before bends (`CivCornerSpeed`), never full throttle into a corner; on sharp bends it slides round with the handbrake.
- Details and test method: `docs/dev-notes/d1-chase-paths.md`, traffic AI plan: `docs/dev-notes/smart-traffic-plan.md`.

## What this is

Fork of REDRIVER2 (Driver 2 as PC C code, graphics layer PsyCross = PSX API on SDL2/OpenGL, `src_rebuild/PsyCross` submodule `E75/PsyCross`). ReExtended adds: Driver 1 cities and missions converted at runtime from the player's own disc files, free camera, on-foot play, weather, day/night cycle, 10x draw distance, impact dents, lost wheels, car spawner, map editor. No game data is ever committed (see Repo rules). Windows 32 bit (`Release_dev`) is the only tested target.

## Build and run (Windows)

- First time: `src_rebuild\windows_dev_prepare.ps1` (downloads premake5, SDL2 2.30.2, openal-soft 1.23.1, jpeg-9d into `src_rebuild\dependencies`, git-ignored; its last "open .sln" error is harmless).
- In `src_rebuild`: set `SDL2_DIR=.\dependencies\SDL2-2.30.2`, `OPENAL_DIR=.\dependencies\openal-soft-1.23.1-bin`, `JPEG_DIR=.\dependencies\jpeg-9d`, run `.\premake5.exe vs2022` (again only after adding source files).
- Build: `MSBuild build\REDRIVER2.sln -p:Configuration=Release_dev -p:Platform=Win32 -p:PlatformToolset=v145 -m -nologo -v:m` (VS BuildTools 18, `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\MSBuild.exe`). Run it in the background.
- Output is `src_rebuild\bin\Release_dev\REDRIVER2_dev.exe`. Needs `SDL2.dll` and `OpenAL32.dll` (= `soft_oal.dll`) next to it, plus `DRIVER2\` (and optionally `DRIVER1\`) game data. Build straight into a test install with `-p:OutDir=<install>\` on `build\REDRIVER2.vcxproj`.
- Test install of this PC: `..\REDRIVER2-PROJECT\GameTestInstall\REDRIVER2_D1` (`DRIVER2`, `DRIVER1`, `config.ini`). Never write into the user's own install; use a copy.
- Close the game gracefully (`CloseMainWindow()`): `REDRIVER2.log` only flushes on exit.
- Details, per-feature findings, test recipes: `docs/DEVELOPER_NOTES.md`.

## Module map

- `src_rebuild/Game/C/*.c` - game logic (compiled as C++). Entry `main.c` (state machine), `glaunch.c` (game start), `mission.c` (missions, script VM, targets), `civ_ai.c` / `cop_ai.c` / `leadai.c` (AI), `handling.c` / `cars.c` (physics), `pedest.c`, `camera.c`, `draw.c` / `tile.c` / `map.c` (rendering), `denting.c`, `cosmetic.c`.
- Extended features: `daycycle.c`, `sky.c`, `allcars.c`, `carspawner.c`, `objanim.c`, `overmap.c`, `gapfill.c`, `chopper.c`.
- Driver 1 gameplay: `d1chase.c` (chased cars), `d1garage.c` (car park / desert tests), plus `IS_DRIVER1_*` branches in `mission.c` and `civ_ai.c`.
- `src_rebuild/Game/Frontend/FEmain.c` - menus (`PsxScreens[]`, `fpUserFunctions[]`, Extended Settings screens).
- `src_rebuild/redriver2_psxpc.cpp` - PC entry: `config.ini`, command line options, startup (`D1Dlc_Detect`).
- `src_rebuild/d1dlc/` - Driver 1 converter library: `lev_file` / `lev_writer` (level files), `d1_map`, `d1_roads`, `d1_height`, `d1_convert` (cities), `d1_missions` (missions, own stamp `MISSIONS.VER`). Bump `D1DLC_CONVERTER_VERSION` / `D1MISSION_VERSION` after any output change.
- `src_rebuild/tools/d1conv/` - `D1Conv` CLI (`convert`, `probe`) and Python format scripts in `spike/`. `tools/` - `prepare_driver1.bat`, map editor, car editor, box analysis.
- `tools/installer/` - player installer (`Install_ReExtended.bat` -> PowerShell + WinForms, no dependency): player picks Driver 2 / Driver 1 (folder, drive or .iso), it copies into `DRIVER2\` / `DRIVER1\` (never overwrites), runs `-nofmv -d1prepare`, then offers "Start game". Headless test: `-Auto -Driver2 <p> -Driver1 <p> -GameDir <copy>`. Keep its markers (`LEVELS\CHICAGO.LEV`, `LEVELS\MIAMI.LEV`) and the "Driver 1 prepare:" log lines in sync with `redriver2_psxpc.cpp`.
- `docs/dev-notes/` - one note per feature (read section 0 first). Driver 1 missions: `d1-missions.md`, `d1-missions-pc-rosetta.md`; chase: `d1-chase-paths.md`; cities: `driver1-cities.md`; D1 vs D2: `driver1-d2-differences.md`.

## Repo rules

- Never commit or ship game data (Driver 1 or 2) or files derived from it. Driver 1 is converted at runtime into `DRIVER1\CONVERTED\`.
- Every Driver 1 feature is gated (`IS_DRIVER1_CITY(GameLevel)`, `IS_DRIVER1_MISSION(n)`, `GameType == GAME_DRIVER1MISSION`), never changes a Driver 2 code path, has a kill switch or missing-data fallback, and logs instead of failing.
- Repo and commit text stays neutral about where the format knowledge comes from: talk about "the source data", formats, opcodes and meanings; no executable addresses, no code listings, no third-party extractor links.
- Sources are CRLF: keep line endings; check `count(\r\n) == count(\n)` after a scripted edit. Several sessions may share the tree: stage only your own hunks.
- Descriptive names for new code (they say the Driver 1 concept); the top of a new file lists where to find what.
- Bug fixes: find the root cause first. Performance: measure first (`-shot` logs step/build/submit times).

## Test options (no person at the keyboard)

`-d1mission <n>` (Driver 1 story mission), `-city <n> -mission 50`, `-festeps "SESDDDDSE"` (scripted menu input, screenshots `FESHOT<n>.BMP`), `-shot <frame>` (screenshot + log lines), `-startpos x z`, `-startrot r`, `-camangle n`, `-drawdist 0|1|2`, `-crowd 1..5`, `-onfoot`, `-lightson`, `-tod n`, `-fog`, `-editor`. `config.ini [game] playLog=1` logs one line per second (F9 = MARK). Never inject key events with `keybd_event`/SendKeys (they hit whatever window has focus).

## Reuse list (try first)

`D1HeightField::Eval` (ground height), `PositionInViewCone` (draw.c), `ViewDrawDistance()`, `MaxCivCars()` / `MaxParkedCars()` (mission.c), `CivCornerSpeed` (civ_ai.c), `D1ChaseCar_LimitHit` (d1chase.c), `PingInCivCar` / `PingInPedestrians`, `MRProcessTarget` (mission targets).
