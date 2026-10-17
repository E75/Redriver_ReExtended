# Uitvoerplan deel 2: volumetrische lampen (halo's en lichtkegels in de lucht)

Zelfstandig plan voor een aparte sessie. Achtergrond: `plans/volumetric-lighting.md` (lees sectie 0, 1 en 2; dit bestand gaat voor bij verschil). Lees ook `CLAUDE.md` in de repo-root (build, test, regels).

## Doel

Lampen, verkeerslichten en koplampen geven 's nachts, in fog, in regen en bij dawn/dusk een zichtbare gloed IN DE LUCHT (licht dat op deeltjes in de lucht verstrooit), ook naast en voor muren, niet alleen een sprite op de lamp. Met `config.ini [render] volumeLights=0` ziet het spel er pixel voor pixel uit als nu.

## Wat er al is (gecontroleerd 2026-10-14)

- Lichtenlijst: `Game/C/draw.c` 2017-2084 (`#ifdef DYNAMIC_LIGHTING`, aan in `dr2limits.h`): `AddDlight(VECTOR* position, CVECTOR* color, int radius)` vult `gLights[MAX_DLIGHTS]` (32) met `SVECTOR position` (pad = radius) en `CVECTOR color`. Positie is VIEW SPACE (na `Apply_Inv_CameraMatrix`, GTE-assen, y omlaag).
- Wie vult de lijst: `debris.c AddLightEffect` (1692, straatlantaarns), `debris.c` 1566 (`AddSmallStreetLight`), `objanim.c` 453/473/518 (D1-lampen en verkeerslichten). Koplampen van auto's (`ShowCarlight`, debris.c 2014) roepen `AddDlight` NIET aan: controleer dat en voeg het toe (positie zoals `ShowCarlight` hem al berekent, na `Apply_Inv_CameraMatrix`).
- Lijst wordt geleegd aan het eind van `DrawGame` (`main.c` 2794: `gNumDlights = 0;`). De OT wordt pas daarna door de GPU getekend, dus de lichten moeten VOOR die regel naar de renderer.
- Shader: `src_rebuild/PsyCross/src/render/PsyX_render.cpp`, string `gpu_shader_snow` (~regel 659-725): `applySnow`, `applyFog`, `beamLight`. Hoofdprogramma (`GPU_FRAGMENT_SAMPLE_SHADER` en `gpu_shader_32_rgba`): `lit = base * v_color; lit.rgb += base.rgb * beamLight(); fragColor = dither(applyFog(lit));`. `v_view.xyz * 128.0` = positie van de pixel in view space; `v_view.w < 0.5` = 2D (HUD), dan niets doen.
- Uniform-pad om na te volgen: `PsyX_SetBeam` (`PsyX_render.cpp` ~1370) bewaart waarden, `GR_UploadSnow` (~1379) zet ze per shader, locaties in `GR_CompilePSXShader` (~1038-1045). `UpdateHeadlights` (`draw.c` 1519) geeft `u_beamPos` door ZONDER y-flip, in dezelfde ruimte als `AddDlight`-posities.

## Bestanden (alleen deze aanraken)

- PsyCross (submodule, branch `feature/driver1-cities` van `E75/PsyCross`): `src/render/PsyX_render.cpp`, `include/PsyX/PsyX_public.h` (één nieuw declaratieblok direct onder `PsyX_SetBeam`).
- Game: `Game/C/draw.c` (nieuwe functie `UpdateVolumeLights`, variabele `gVolumeLights`), `Game/C/main.c` (één aanroep), `Game/C/debris.c` (alleen `ShowCarlight`: `AddDlight`-aanroep), `Game/C/draw.h` (extern), `redriver2_psxpc.cpp` (één `ini_sget`-regel onder `distanceFog`, regel ~615).
- NIET aanraken: `sky.c`, `PsyX_GPU.cpp`, `libgpu.h`, `LIBGPU.C`, `src/render/PsyX_postfx.*` (deel 3 werkt daar tegelijk), `applySnow`/`UpdateSnow` (sneeuwcode, gebruiker wil die niet als basis), `applyFog` (deel 1).

## Contract

```c
// PsyX_public.h
#define PSYX_MAX_VOLUME_LIGHTS 16
extern void PsyX_SetVolumeLights(int count, const float* pos /* count*3, view space as u_beamPos */, const float* color /* count*4: rgb 0..1, radius */, float density);
extern void PsyX_SetVolumeBeam(float amount);   // in-scattering of the player's headlight cone (u_beam)
```
Shader-uniforms: `u_lightCount` (int), `u_lightPos[16]` (vec3), `u_lightCol[16]` (vec4, w = radius), `u_volDensity` (float), `u_beamVolume` (float).
Game: `int gVolumeLights = 1;` in `draw.c`, `void UpdateVolumeLights(void)` in `draw.c` (declaratie in `draw.h`), aangeroepen in `main.c` direct VOOR `gNumDlights = 0;` (regel ~2795).

## Stappen

0. **Ruimte-check (eerst, ~15 min).** Tijdelijk: log in `UpdateVolumeLights` de eerste `gLights`-positie en `u_beamPos` van de speler (`printInfo`, alleen in het `-shot`-frame). Bevestig dat beide dezelfde as-richting hebben (een lamp vóór de auto heeft z > 0, boven de weg heeft y met hetzelfde teken als `u_beamPos.y` van een koplamp boven de weg). Kijk in de shader of `beamLight` met dezelfde ruimte werkt (`p = v_view.xyz * 128.0`). Als y gespiegeld moet: flip in `UpdateVolumeLights`, niet in de shader. Log daarna verwijderen.
1. **UpdateVolumeLights** (`draw.c`): als `gVolumeLights == 0` of het is dag zonder fog en zonder regen (`gTimeOfDay == TIME_DAY && !gFogTarget && gWeather != WEATHER_RAIN`): `PsyX_SetVolumeLights(0, ...)` en klaar. Anders: kies de 16 dichtstbijzijnde van `gLights[0..gNumDlights)` (afstand = lengte van de positie, de camera staat in de oorsprong), zet kleur = `CVECTOR / 255` maal een helderheidsfactor (begin 4.0, CVECTOR-waarden van lampen zijn laag, 40-85), radius = `position.pad`. `density`: nacht 1.0, fog `max(1.0, fogAmount * 3)`, regen 1.5, dawn/dusk 0.5 (afstemmen op screenshots). Roep ook `PsyX_SetVolumeBeam(gVolumeLights ? density : 0)` aan.
2. **ShowCarlight** (`debris.c` 2014): voeg `AddDlight` toe voor koplampen (alleen de voorlichten, `flag` bekijken) met radius ~3000; achterlichten met kleine radius (800). Controleer of `gEnableDlights` (per-vertex licht, standaard 0) daardoor niets anders gaat doen: `GetDLightLevel` loopt alleen bij `gEnableDlights`.
3. **Shader** (`PsyX_render.cpp`, nieuwe functie `volumeLight()` in hetzelfde `#if USE_PGXP`-blok als `beamLight`, plus een lege variant in het `#else`-blok):
   - Straal: camera (oorsprong) naar pixel `p`, lengte `t = length(p)`, richting `r = p / t`.
   - Per licht `L`: `b = dot(L, r)` (afstand langs de straal tot het dichtste punt), `h2 = max(dot(L, L) - b * b, 1.0)`, `h = sqrt(h2)`. Verstrooiing van een puntlicht met `1 / (d^2 + k)`: `scat = (atan((t - b) / h) - atan(-b / h)) / h` (gesloten vorm van de integraal over het segment 0..t). Vermenigvuldig met `col.rgb * u_volDensity * col.w` en een globale schaal (begin `0.00002`, afstemmen) en laat het boven `col.w * 4` (vier keer de radius) uitdoven met `smoothstep`, anders tellen lampen aan de andere kant van de stad mee.
   - Koplampkegel (`u_beamVolume > 0`): 8 stappen langs het segment `0..min(t, 9000)`, per stap het bestaande `cone * atten`-gewicht van `beamLight` (zonder `ndl` en zonder normaal), gemiddeld, maal `u_beamVolume * 0.15`.
   - Toepassen NA de fog: `fragColor = dither(applyFog(lit) + vec4(volumeLight(), 0.0));` in beide hoofdprogramma's. Zo verdwijnt de gloed niet in de fog maar ligt hij erop (zoals echt licht in mist).
   - Niet bij `v_view.w < 0.5`.
   - Uniforms ophalen in `GR_CompilePSXShader`, uploaden in `GR_UploadSnow` (of een eigen `GR_UploadVolume` direct erna, dezelfde plek in de aanroeper).
4. **config.ini**: in `redriver2_psxpc.cpp` onder `distanceFog`: `ini_sget(config, "render", "volumeLights", "%d", &gVolumeLights);` met `extern int gVolumeLights;` bij de andere externs bovenaan.
5. **Afstemmen en meten** (zie Test). Begin met de schaal te laag; liever subtiel dan een witte waas.

## Bekende beperkingen (zo laten, in het verslag noemen)

- Pixels zonder geometrie (lucht) krijgen geen gloed: de lucht is wel geometrie (depth 0.9996+), dus dit valt waarschijnlijk mee; controleren.
- Halfdoorzichtige prims (sprites, rook) krijgen de gloed ook: bekijken of de lampsprite zelf daardoor te fel wordt; zo ja `volumeLight()` alleen bij `v_view.w` 1 of 2 en blendmode none (de shader weet de blendmode niet: dan een uniform `u_volOn` per split zetten in `GR_SetBlendMode`).

## Build en test (gedeelde working tree: andere sessies werken tegelijk)

- Bouwen in een private kopie, nooit in `src_rebuild/build` van de gedeelde tree:
  1. `git archive HEAD src_rebuild | tar -x -C ../_cleanbuild_lamps`, `git -C src_rebuild/PsyCross archive HEAD | tar -x -C ../_cleanbuild_lamps/src_rebuild/PsyCross`, kopieer `src_rebuild/dependencies` (robocopy /E) en `premake5.exe`.
  2. Kopieer je eigen bestanden uit de werkboom over de kopie heen, behalve `redriver2_psxpc.cpp`: daarvan alleen je eigen hunk (`git diff -U0 -- src_rebuild/redriver2_psxpc.cpp`, eigen hunk eruit, `git apply --unidiff-zero` in de kopie).
  3. In de kopie: `SDL2_DIR=.\dependencies\SDL2-2.30.2`, `OPENAL_DIR=.\dependencies\openal-soft-1.23.1-bin`, `JPEG_DIR=.\dependencies\jpeg-9d`, `premake5.exe vs2022`, dan MSBuild (CLAUDE.md) op `build\REDRIVER2.vcxproj` met `"-p:OutDir=<parent folder>/GameTestInstall\REDRIVER2_Lamps\\"`, in de achtergrond (eerste build >10 min). Wacht niet op `MSBuild.exe`-processen (node reuse houdt ze open): kijk naar de `-> ...REDRIVER2_dev.exe`-regel.
- Testinstallatie `GameTestInstall\REDRIVER2_Lamps`: junctions `DRIVER1` en `DRIVER2` naar `GameTestInstall\REDRIVER2_D1\DRIVER1` / `\DRIVER2`, kopieer `SDL2.dll`, `OpenAL32.dll`, `config.ini`, `REDRIVER2.CT`, `REDRIVER2_ENG.CT` uit `GameTestInstall\REDRIVER2_Vol`.
- Shots: kopie van `..\test_scripts\shot_safe.ps1` met `$dir` naar je eigen installatie, `-Style Normal` (geminimaliseerd geeft soms een zwart beeld). Gevallen:
  - Chicago nacht: `-city 0 -tod 3` (standaard start), Chicago nacht + `-fog`.
  - Miami nacht bij lantaarns: `-city 4 -startpos 117000 -21000 -tod 3`.
  - New York verkeerslicht: zie `plans/d1-cars-and-lights.md` (TLIGHT01-positie) of scan een paar `-startpos`.
  - Regen als het spel dat per optie toestaat, anders overslaan en noemen.
  - Dag zonder fog: beeld gelijk aan `volumeLights=0` (A/B-pixeldiff 0%: schrijf een klein Python-script dat twee `SCREENSHOT.BMP` vergelijkt).
  - `volumeLights=0` in alle gevallen: pixel-gelijk aan een build zonder jouw wijziging (oude exe uit `GameTestInstall\REDRIVER2_Vol`).
  - D2-regel uit CLAUDE.md: ook Havana en Vegas (`shot_safe2.ps1 -Mission 52/54`) zonder fouten.
- Kosten: `-shot`-log `build/submit` en framerate vóór/na in Chicago 10x nacht crowd 5 (`-crowd 5`). Doel: < 1 ms extra.

## Klaar als

- Gloed rond lampen zichtbaar 's nachts en in fog (screenshots als bewijs, crop in het verslag), koplampkegel zichtbaar in fog.
- `volumeLights=0` en dag zonder fog: 0% pixelverschil.
- Build groen, geen nieuwe warnings in eigen bestanden, debug-logs verwijderd.
- Status in `plans/volumetric-lighting.md` sectie 0 bijgewerkt (deel 2), met gemeten kosten en open punten.
- Commit: eerst in `src_rebuild/PsyCross` (alleen jouw bestanden, branch `feature/driver1-cities`, push naar de fork), dan in de repo op branch `feature/volumetric-lighting`: alleen eigen hunks (`git add -p`), inclusief de nieuwe submodule-pointer. Geen `Co-Authored-By`-regel. Vraag de gebruiker eerst "commit & push?".
