# Uitvoerplan deel 3: god rays (zonnestralen)

Zelfstandig plan voor een aparte sessie. Achtergrond: `plans/volumetric-lighting.md` (lees sectie 0, 0a, 1 en 2; dit bestand gaat voor bij verschil). Lees ook `CLAUDE.md` in de repo-root (build, test, regels).

## Doel

Als de zon in beeld of net buiten beeld staat, schijnen zichtbare lichtstralen langs gebouwen en bomen (screen-space radial blur, "Volumetric Light Scattering as a Post-Process", GPU Gems 3 hoofdstuk 13). Overdag, bij dawn en dusk, helder weer. Met `config.ini [render] sunShafts=0` ziet het spel er pixel voor pixel uit als nu.

## Wat er al is (gecontroleerd 2026-10-14)

- Alles wordt in één ordering table (OT) in de backbuffer van het venster getekend, de HUD ook. De OT wordt van hoge naar lage index gelezen: lucht op `OTSIZE - 1` (eerst), HUD op `ot + 1` (`overlay.c`), fade op `ot + 8`, lens flare op `ot + 10` en de zon-sample (`DR_MOVE`) op `ot + 0x20` (`sky.c DrawLensFlare`, aangeroepen in `main.c` 2729).
- PsyCross eigen OT-pakketten: `DR_PSYX_TEX` (code `0xB1`, `SetPsyXTexture`) en `DR_PSYX_DBGMARKER` (`0xB2`), definitie `include/psx/libgpu.h` ~690, maker `src/psx/LIBGPU.C` ~800, verwerking `src/gpu/PsyX_GPU.cpp ProcessPsyXPrims` (~1535). Splits: `struct GPUDrawSplit` (`PsyX_GPU.cpp` 35), nieuwe split in de functie rond regel 700 (vergelijkt blendmode, texture, drawenv, debugText), tekenen in `DrawSplit` (~754) en `DrawAllSplits` (~799).
- Luchtmasker (spike, sectie 0a): de lucht schrijft depth 0.9996-0.9998, de rest van de wereld blijft eronder. Masker = `depth >= 0.9996`. Onder de horizon tekent `PlotHorizonMDL` (`sky.c`) de lucht gespiegeld (tweede lus, halve helderheid): die telt ook als lucht en moet eruit (anders stralen uit het water).
- Framebuffer-kopieën bestaan al als voorbeeld: `GR_StoreFrameBuffer` (`PsyX_render.cpp` ~1772, `glBlitFramebuffer`), FBO's `g_glBlitFramebuffer`, `g_glOffscreenFramebuffer`. Venstergrootte `g_windowWidth`/`g_windowHeight`.
- Render-state wordt gecachet in globals bovenaan `PsyX_render.cpp` (`g_PreviousBlendMode`, `g_PreviousDepthMode`, `g_PreviousStencilMode`, `g_PreviousScissorState`, `g_PreviousOffscreenState`, `g_lastBoundTexture`) en de shader via `GR_SetShader`. Na je eigen GL-pass moet die state kloppen (zelf terugzetten of de caches ongeldig maken).
- Zon: `sun_position[4]` (`sky.c` 58, één per stad, NIET per tijdstip), schermpositie via `gte_rtps` in `DrawLensFlare` (`sun_pers_conv_position`, PSX-schermcoördinaten rond 160/128). Zichtbaarheid: `flare_col` (aantal witte pixels in een 12x10 sample rond de zon). `DrawLensFlare` stopt bij regen/sneeuw (`gWeather - 1U <= 1`), `SkyOvercast()`, dawn en dusk.
- Premake pakt `PsyCross/**.cpp` automatisch: een nieuw bestand vraagt alleen een nieuwe `premake5.exe vs2022`.

## Bestanden (alleen deze aanraken)

- PsyCross (submodule, branch `feature/driver1-cities` van `E75/PsyCross`): NIEUW `src/render/PsyX_postfx.cpp` + `src/render/PsyX_postfx.h` (alle GL-code en shaders van deze pass), `src/gpu/PsyX_GPU.cpp` (pakket + split-veld + aanroep), `include/psx/libgpu.h` en `src/psx/LIBGPU.C` (pakket).
- Game: `Game/C/sky.c` (`DrawLensFlare` en een zonpositie voor dawn/dusk), `redriver2_psxpc.cpp` (één `ini_sget`-regel onder `distanceFog`, regel ~615).
- NIET aanraken: `PsyX_render.cpp` en `PsyX_public.h` (deel 2 werkt daar tegelijk; heb je één regel in `PsyX_render.cpp` echt nodig, overleg dan via de gebruiker), `draw.c`, `debris.c`, `objanim.c`, `main.c`, de sneeuwcode.

## Contract

```c
// libgpu.h, naast DR_PSYX_TEX
typedef struct {
	DECLARE_P_ADDR
	u_int  code[4];
} DR_PSYX_SUNSHAFTS;
// code[0] = 0xB3000000 | amount (0..255)
// code[1] = (u_short)sunX | (u_short)sunY << 16   (PSX screen, may lie outside 0..320 / 0..256)
// code[2] = r | g << 8 | b << 16                  (colour of the rays)
// code[3] = (u_short)horizonY                     (PSX screen y of the horizon; pixels below it are never sky)
extern void SetPsyXSunShafts(DR_PSYX_SUNSHAFTS* p, int amount, int sunX, int sunY, int horizonY, int r, int g, int b);
```
- `ProcessPsyXPrims` case `0x03`: forceer een nieuwe split met `split.sunShafts = 1` en de waarden; `DrawSplit` roept voor zo'n split (vóór zijn driehoeken, die er niet zijn) `PsyX_PostFx_SunShafts(...)` aan.
- `PsyX_postfx.h`: `void PsyX_PostFx_SunShafts(int amount, float sunX, float sunY, float horizonY, const float* color);` en `void PsyX_PostFx_Shutdown(void);`.
- Game: `int gSunShafts = 1;` in `sky.c`, gelezen in `redriver2_psxpc.cpp` (`ini_sget(config, "render", "sunShafts", "%d", &gSunShafts);` + `extern int gSunShafts;`).
- OT-plek van het pakket: `ot + 0x21` (na de 3D-scène, vóór zon-sample, lens flare en HUD).

## Stappen

1. **Pakket** (`libgpu.h`, `LIBGPU.C`, `PsyX_GPU.cpp`): struct, `SetPsyXSunShafts`, case `0x03` in `ProcessPsyXPrims` (lengte 4 teruggeven), veld(en) in `GPUDrawSplit`, de split-vergelijking laten splitsen bij een shafts-pakket, aanroep in `DrawSplit`. Eerst met een lege `PsyX_PostFx_SunShafts` die alleen één keer logt: bouwen, `-shot`, controleren dat de aanroep één keer per frame komt.
2. **Coördinaten**: zoek hoe 2D-vertices (PSX-scherm) naar vensterpixels gaan (vertex shader `Projection`, `GR_Ortho2D`, `PsyX_GetPSXWidescreenMappedViewport`, widescreen-offsets) en reken `sunX/sunY/horizonY` op de CPU op dezelfde manier om. Controle: teken in de pass tijdelijk een rood kruisje op de zonpositie en vergelijk met de lens flare in een screenshot.
3. **Pass** (`PsyX_postfx.cpp`), op 1/4 resolutie:
   - Kopieer depth van de backbuffer naar een textuur (`glBlitFramebuffer` met `GL_DEPTH_BUFFER_BIT` naar een FBO met `GL_DEPTH24_STENCIL8`-textuur, formaat moet gelijk zijn aan de backbuffer; lukt dat niet: `glCopyTexSubImage2D` naar een `GL_DEPTH_COMPONENT`-textuur) en kleur naar een RGBA-textuur.
   - Maskerpass (1/4 res): `mask = (depth >= 0.9996 && pixelY boven horizonY) ? helderheid(kleur) : 0` (helderheid zodat wolken minder stralen dan open lucht). Zet de drempel als `const float` met `// limit: sky depth measured 0.9996-0.9998 (sky z x256, zFar 1000), upgrade: if the projection changes`.
   - Radial blur (1/4 res): 48 samples van de pixel richting de zon, `decay 0.96`, `weight 0.5`, `density 0.9` (GPU Gems 3-formule), exposure uit `amount / 255`.
   - Composiet: additief (`GL_ONE, GL_ONE`) over de backbuffer op volle resolutie, maal `color`. Daarna de GL-state terugzetten (framebuffer 0, viewport, programma, VAO via `GR_BindVertexBuffer`, blend, depth, scissor, gebonden textuur) of de cache-globals van `PsyX_render.cpp` via `extern` ongeldig maken (`g_PreviousBlendMode = -1` enz.: kijk welke waarde "onbekend" betekent).
   - Resources maken bij de eerste aanroep en opnieuw bij een andere venstergrootte.
4. **Game** (`sky.c DrawLensFlare`): bereken `amount` = `flare_col` (zichtbaarheid, 0..120) geschaald naar 0..255, maal een factor die afneemt als de zon ver buiten beeld staat (0 vanaf ~1.5 schermbreedte van het midden), maal 0 's nachts, bij regen/sneeuw en `SkyOvercast()`. Horizon: projecteer met `gte_rtps` een punt ver voor de camera op hoogte 0 (horizontale kijkrichting, bijv. `(sin yaw, 0, cos yaw) * 100000` door `inv_camera_matrix`) en neem de y. Voeg het pakket in op `ot + 0x21` ALLEEN als `gSunShafts && amount > 0`, zodat er met de optie uit niets verandert. Kleur: dag warm wit `(255, 240, 210)`, dawn/dusk oranje `(255, 170, 100)`.
5. **Dawn en dusk**: `DrawLensFlare` stopt nu bij dawn/dusk vóór de projectie. Laat de flare zelf zoals hij is, maar reken voor de shafts toch de zonpositie uit met een lage zon: zelfde azimut als `sun_position[BASE_CITY]`, elevatie zo dat de zon op de plek staat waar de lucht-textuur hem toont (meet in een dawn- en dusk-screenshot waar de heldere vlek in de lucht zit en kies de vector die daar projecteert). Zonder `flare_col` (de sample draait niet bij dawn/dusk): laat de sample voor de shafts ook bij dawn/dusk lopen, of gebruik een vaste zichtbaarheid en vertrouw op het masker (het masker doet het afschermen al).
6. **config.ini** regel in `redriver2_psxpc.cpp`.

## Build en test (gedeelde working tree: andere sessies werken tegelijk)

- Bouwen in een private kopie, nooit in `src_rebuild/build` van de gedeelde tree:
  1. `git archive HEAD src_rebuild | tar -x -C ../_cleanbuild_shafts`, `git -C src_rebuild/PsyCross archive HEAD | tar -x -C ../_cleanbuild_shafts/src_rebuild/PsyCross`, kopieer `src_rebuild/dependencies` (robocopy /E) en `premake5.exe`.
  2. Kopieer je eigen bestanden uit de werkboom over de kopie heen, behalve `redriver2_psxpc.cpp`: daarvan alleen je eigen hunk (`git diff -U0 -- src_rebuild/redriver2_psxpc.cpp`, eigen hunk eruit, `git apply --unidiff-zero` in de kopie).
  3. In de kopie: `SDL2_DIR=.\dependencies\SDL2-2.30.2`, `OPENAL_DIR=.\dependencies\openal-soft-1.23.1-bin`, `JPEG_DIR=.\dependencies\jpeg-9d`, `premake5.exe vs2022` (opnieuw na het nieuwe bestand), dan MSBuild (CLAUDE.md) op `build\REDRIVER2.vcxproj` met `"-p:OutDir=<parent folder>/GameTestInstall\REDRIVER2_Shafts\\"`, in de achtergrond (eerste build >10 min). Wacht niet op `MSBuild.exe`-processen (node reuse houdt ze open): kijk naar de `-> ...REDRIVER2_dev.exe`-regel.
- Testinstallatie `GameTestInstall\REDRIVER2_Shafts`: junctions `DRIVER1` en `DRIVER2` naar `GameTestInstall\REDRIVER2_D1\DRIVER1` / `\DRIVER2`, kopieer `SDL2.dll`, `OpenAL32.dll`, `config.ini`, `REDRIVER2.CT`, `REDRIVER2_ENG.CT` uit `GameTestInstall\REDRIVER2_Vol`.
- Shots: kopie van `..\test_scripts\shot_safe.ps1` met `$dir` naar je eigen installatie, `-Style Normal` (geminimaliseerd geeft soms een zwart beeld). Naar de zon kijken: bereken de hoek van `sun_position[stad]` (`ratan2(x, z)`, 4096 = rondje) en start met `-startrot` zo dat de auto (en de achtervolgcamera) die kant op kijkt; log de zon-schermpositie in het `-shot`-frame (`shot: sun shafts amount a at x y, horizon h`).
  - Chicago dag tegen de zon in, met gebouwen tussen camera en zon (verplaats met `-startpos` tot de zon achter een gebouwrand staat).
  - Miami dag, Frisco Nob Hill (`-city 5 -startpos -11250 240000`, rot naar de zon).
  - Dusk (`-tod 2`) en dawn (`-tod 0`/`1`, kijk in `mission.c` welke waarde wat is) in Chicago.
  - Rivier in Chicago (`-flyalt 3000`, standaard start): GEEN stralen uit het water (spiegeling buiten het masker).
  - Nacht, regen, bewolkt: geen pakket in de OT (log), beeld gelijk aan de oude exe.
  - `sunShafts=0`: 0% pixelverschil met de oude exe (`GameTestInstall\REDRIVER2_Vol` heeft nog de spike-dumps: neem liever een eigen build van `HEAD` als referentie). Schrijf een klein Python-script dat twee `SCREENSHOT.BMP` vergelijkt.
  - D2-regel uit CLAUDE.md: ook Havana en Vegas (`shot_safe2.ps1 -Mission 52/54`).
- Kosten: `-shot`-log `build/submit` en framerate vóór/na, Chicago 10x dag. Doel: < 1 ms extra.

## Klaar als

- Stralen zichtbaar langs gebouwranden bij dag en dusk (screenshots, crop in het verslag), niet uit water of HUD, niet 's nachts of bij regen.
- `sunShafts=0`: 0% pixelverschil; geen GL-errors (`glGetError` na de pass in een dev-build één keer controleren).
- Build groen, debug-kruisje en logs weg.
- Status in `plans/volumetric-lighting.md` sectie 0 bijgewerkt (deel 3), met gemeten kosten en open punten.
- Commit: eerst in `src_rebuild/PsyCross` (alleen jouw bestanden, branch `feature/driver1-cities`, push naar de fork), dan in de repo op branch `feature/volumetric-lighting`: alleen eigen hunks (`git add -p`), inclusief de nieuwe submodule-pointer. Geen `Co-Authored-By`-regel. Vraag de gebruiker eerst "commit & push?".
