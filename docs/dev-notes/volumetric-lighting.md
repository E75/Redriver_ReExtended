# Plan: volumetrisch licht, god rays en atmosferische fog

Branch: eigen branch `feature/volumetric-lighting` vanaf `main` (keuze bij de start van deel 1). Renderer-wijzigingen gaan in de PsyCross-fork (`E75/PsyCross`, branch `feature/driver1-cities`), game-wijzigingen in `src_rebuild/Game/C`.
Doel: (1) fog met hoogte en zonkleur, (2) lichthalo's en lichtkegels van lampen en koplampen in fog, regen en nacht, (3) zonnestralen (god rays) langs gebouwen. Met alle opties uit ziet het spel er precies zo uit als nu.

Eén deel per sessie, één groene commit per deel. Werk sectie 0 bij aan het eind van elk deel.

---

## 0. Status

| Deel | Stand |
|---|---|
| S Spike luchtmasker (schrijft de lucht depth, kan de renderer de lucht herkennen) | klaar 2026-10-14 (bevindingen in 0a) |
| 1 Atmosferische fog (hoogte, zon-in-scattering, horizon) | klaar 2026-10-14 (zie 0d), horizonmenging niet gedaan |
| 2 Volumetrische lampen (lichtenlijst + analytische in-scattering) | klaar 2026-10-14, inclusief koplampbundel-fix (zie 0c) |
| 3 God rays (post-pass radial blur richting zon) | klaar 2026-10-14, alleen overdag (dawn/dusk open, zie 0b) |

### 0a. Bevindingen spike

Gemeten met een tijdelijke dump in een private build (`_cleanbuild_vol`, niet in de repo): `PsyX_TakeScreenshot` schreef de depth buffer (`DEPTH.RAW`) en `DrawSplit` een kopie van de kleurbuffer direct na de eerste RGBA-splits van de frame = de HD-lucht (`SKY.RAW`). Testinstallatie `GameTestInstall\REDRIVER2_Vol`, `-shot 240`, standaard draw distance (10x).

- De lucht schrijft WEL depth: `PlotSkyPoly` is een gewone ondoorzichtige `POLY_FT4` (depth test en write aan), PGXP-z maal 256 (`PlotHorizonMDL`). Luchtpixels liggen op 0.9996 - 0.9998, nooit op 1.0 (0% pixels op 1.0: koepel + spiegeling dekken het hele scherm).
- Twee maskers vergeleken: A = kleur ongewijzigd sinds de lucht getekend is, B = depth >= 0.9996. A is in alle shots een deelverzameling van B (A zonder B: 0%).
  - Chicago `-flyalt 3000`: A 43.9%, B 48.2%. Miami `117000 -21000`: A 21.9%, B 24.1%. Frisco Nob Hill: A 4.4%, B 4.4%.
  - B zonder A = (1) HUD-tekst en balken (2D, geen depth write; bovenste banden) en (2) halfdoorzichtige lagen over de luchtspiegeling (Chicago rivier, banden 3-6). Geen verre gebouwen in B: verre geometrie blijft onder 0.9996.
- Valkuil: onder de horizon tekent `PlotHorizonMDL` de lucht nog eens gespiegeld (halve helderheid). Waar de wereld gaten heeft of water doorzichtig is (Chicago rivier), telt die spiegeling in beide maskers als lucht. God rays moeten die uitsluiten.
- Keuze voor deel 3: masker = depth >= drempel, genomen in de shafts-pass (vóór de HUD, dus geen HUD-fout), zonder extra kleurkopie. Drempel niet als magisch getal: afleiden uit de sky-z-schaal (256) en `perspectiveZFar` (1000), of de lucht-depth per frame meten op een pixel boven de horizon. Spiegeling uitsluiten: de spiegel-quads (tweede lus van `PlotHorizonMDL`) in een eigen split met een markering (bijvoorbeeld schrijven op depth 1.0 via `gl_FragDepth`, dan masker = depth in [drempel, 1.0)), of pixels onder de geprojecteerde horizonlijn negeren. Terugval als de depth-route faalt: masker A (kleurkopie na de lucht-splits, kost één blit per frame).
- Gebruikersopmerking: de sneeuwcode is niet goed genoeg; deel 1 gebruikt daarom eigen uniforms (zie deel 1).

### 0b. Resultaat deel 3 (god rays)

- Code: `PsyCross/src/render/PsyX_postfx.cpp` (pass + shaders), pakket `DR_PSYX_SUNSHAFTS` (`libgpu.h`, `LIBGPU.C`, eigen split in `PsyX_GPU.cpp`), `sky.c AddSunShafts` (vanuit `DrawLensFlare`, OT `ot + 0x21`), `config.ini [render] sunShafts` (`gSunShafts`, standaard 1). Menu: Options - Gameplay - Secret - Volumetric Options - Sun Shafts (Off / On; `VOLUMETRIC_SCREEN` 52, `SUNSHAFTS_SCREEN` 53, `FEVAR_SUNSHAFTS` 24); deel 2 kan zijn rij in `VolumetricOptionsScreen` zetten. Profiel: `reserved[1]` bits 5-6 (0 nooit opgeslagen = config.ini, 1 uit, 2 aan).
- Pass: kleur + depth van de backbuffer op halve breedte en hoogte (blit; depth-formaat wordt van de backbuffer afgelezen, hier D24S8), 48 samples richting zon (decay 0.96, density 0.9, gedeeld door de som van de gewichten), additief terug. Bron = luchtpixels (depth >= 0.9996, boven de geprojecteerde horizon) met helderheid > 0.55, alleen dicht bij de zon: een brede bron gaf een witte waas in plaats van stralen. Op de zon zelf komt niets bij (de zon-ster blijft en de flare-sample van `DrawLensFlare` verandert niet).
- Zichtbaarheid = 128 + `flare_col`-deel (een zon achter een gebouw moet stralen langs de rand geven, het masker doet de afscherming), uitdoven tot 1.5 schermbreedte van het midden. Geen pakket 's nachts, bij regen/sneeuw, mist (`SkyOvercast`) en bij dawn/dusk.
- Gemeten (1280x800, Release_dev, vsync uit): GPU-tijd van de pass 0.60-0.68 ms (timer query); CPU build/submit binnen ruis. `sunShafts=0` en nacht/regen/mist: 0% pixelverschil met een build van `HEAD` (3 views; let op: dezelfde `-shot` kan tussen twee runs van één exe soms 58% verschillen, herhaal dan).
- Coördinaten: PSX-scherm -> textuur `u = 0.5 + (x/320 - 0.5) * H / (0.75 W)`, `v = 0.5 - (y/256 - 0.5)` (gecontroleerd met een tijdelijk kruisje op de zon).
- Open (deel 3): dawn/dusk. De lucht-textuur van Chicago toont bij dawn en dusk geen zonvlek (8 richtingen gemeten vanaf 2500 hoog), dus er is geen plek om een lage zon op te zetten; stralen zonder zichtbare bron zijn weggelaten. Mogelijk vervolg: een eigen lage zon-sprite bij dusk. `PsyX_PostFx_Shutdown` wordt nog niet aangeroepen (hoort in `GR_Shutdown`, `PsyX_render.cpp` was van deel 2). Het effect is subtiel (gloed + waaier langs gebouwranden, duidelijkst in Miami); sterkte is `c_shaftExposure` (0.6) in `PsyX_postfx.cpp`.

### 0c. Resultaat deel 2 (volumetrische lampen)

- Code: `PsyX_render.cpp` `volumeLight()` (na `applyFog` opgeteld, alleen bij blendmode none), `PsyX_SetVolumeLights`/`PsyX_SetVolumeBeam` (`PsyX_public.h`); `draw.c UpdateVolumeLights` (16 dichtstbijzijnde van `gLights`, aangeroepen in `main.c` vóór `gNumDlights = 0`), `debris.c ShowCarlight` voegt koplampen (radius 3000) en achterlichten (400) toe; `config.ini [render] volumeLights` (`gVolumeLights`, standaard 1).
- VALKUIL: `v_view.xy * 128` is GEEN view space. `ScreenCoordsToEmulator` (`PsyX_GPU.cpp`) maakt van x, y `px / tekenbreedte - 0.5`; alleen `v_view.z * 128` is view-z. Echte positie: `((v_view.xy + 0.5) * tekengrootte * 128, v_view.z * 128)`, met uniform `u_viewScale` (uit `activeDrawEnv`/`activeDispEnv`). Ruimte = GTE view space, y OMLAAG (geen flip; lampen boven de camera hebben y < 0, gelogd). `beamLight` gebruikt nu ook de echte positie; `applySnow` nog de foute `v_view.xyz * 128` (niet aangeraakt, de sneeuw werkt toevallig redelijk).
- Model: puntlicht `1 / (d^2 + k^2)` gesloten geïntegreerd over de straal 0..t, kern `k` = 0.15 x halo, halo = 0.12 x de `AddDlight`-radius (lantaarn 7000 -> 840, koplamp 3000 -> 360, achterlicht 400 -> 48), gaussische rand `exp(-miss^2 / halo^2)`, uitdoven als de camera binnen 2-6x de halo zit (anders kleurt een eigen achterlicht het hele beeld) en tussen 12000 en 24000 eenheden. Kleur = `CVECTOR` x 4 / 255, schaal 0.05. (Eerst halo 0.5x met harde rand en schaal 0.08: de gebruiker vond lantaarns en achterlichten "grote gloed, te overdreven"; aangepast 2026-10-14 tijdens deel 1.) Dichtheid: nacht 1, dawn/dusk 0.5, regen 1.5, fog 2; dag zonder fog en regen: uit.
- `AddDlight`: bij een volle lijst (32) vervangt een dichter licht het verste, posities buiten 16 bit worden overgeslagen; beide alleen met `volumeLights=1`, zodat de lijst met `0` gelijk blijft (ook voor `dynamicLights=1`). Met `dynamicLights=1` en `volumeLights=1` verlichten autolichten nu ook de geometrie (`GetDLightLevel`).
- Gemeten: `volumeLights=0` (Chicago nacht, Miami nacht + fog) en dag zonder fog met `volumeLights=1` (Chicago, Miami): 0% pixelverschil met een build van `HEAD` (een `-shot`-run kan soms afwijken door een simulatiehik: herhalen). CPU build/submit binnen ruis; de frameperiode blijft 33.5 ms (spel loopt vast op 30 Hz, ook met vsync uit), GPU-tijd niet apart gemeten (geen timer query gedaan).
- Gezien: gloed rond lantaarns en koplampen van tegenliggers (Miami, NY, LA, Havana nacht), grote halo's in fog (Chicago, Frisco), rode gloed van eigen achterlichten (in fog fors).
- Koplampbundel-fix (2026-10-14, op verzoek): `UpdateHeadlights` gaf `u_beamDir` achterstevoren (gelogd: auto rijdt van de camera af, dir z = -0.98; auto-voorkant is -z in auto-ruimte), dus de bundel lichtte de eigen auto op in plaats van de weg. Nu `-` kolom 2 van `drawCarMat`, `beamLight` met de echte positie en afval lineair in plaats van kwadratisch, maal 1.5 (de weg wordt scherend belicht). Gezien: lichtvlek en kegel op de weg in Frisco en Chicago (nacht, fog). Dit verandert het nachtbeeld ook met `volumeLights=0` (bedoeld); overdag 0% verschil met `HEAD` (Chicago, Miami, Havana).
- Open:
  - Verkeerslichten (`AddTrafficLight`, ook de D1-signalen van `objanim.c`) staan niet in de lichtenlijst: geen gloed. Straatlantaarns (`AddLightEffect`, `AddSmallStreetLight`) wel.
  - `PsyX_PostFx_Shutdown` hoort nog in `GR_Shutdown` (open punt van deel 3, niet gedaan).
  - Lichten worden per frame één keer gezet: bij twee views (split screen) wint de laatste.

### 0d. Resultaat deel 1 (atmosferische fog)

- Code: `applyFog` in `PsyX_render.cpp` (hoogtefog + zon-tint, uniforms van sectie 2), `PsyX_SetAtmosphere` (`PsyX_public.h`); `draw.c UpdateAtmosphere` (na `UpdateHeadlights` in `DrawMapPSX`), `config.ini [render] atmosphere` (`gAtmosphere`, standaard 1). Menu: Options - Gameplay - Secret - Volumetric Options - Fog (`FOG_SCREEN` 55, `FEVAR_FOG` 26), profiel `reserved[1]` bits 9-10 (0 nooit opgeslagen = config.ini, 1 uit, 2 aan).
- Ruimte: echte view-positie zoals deel 2 (`(v_view.xy + 0.5) * u_viewScale`, y omlaag); `u_worldUp` = `(0, -4096, 0)` door `Apply_Inv_CameraMatrix`, `u_camHeight` = `-camera_position.vy` (zeespiegel 0). Geen grondprobe, sneeuwcode niet aangeraakt.
- Waarden: falloff 1 / (3000 x celschaal), dichtheid 1e-5 / celschaal per eenheid op zeespiegel; dawn/dusk x1.5, regen/sneeuw x2, fog x4. Zon-tint (HG, g 0.6, genormaliseerd op 1 in de zon) 0.7 overdag, 0.9 dawn/dusk (zon zelfde richting, elevatie / 4, oranje), 0 bij nacht en slecht weer. De lucht (straal > 400000 door PGXP-z x 256) krijgt niets.
- Gemeten: `atmosphere=0` tegen een build van `HEAD`: 0,00% pixelverschil (Chicago dag, dusk, fog, tegen de zon in). Aan: Chicago 43%, Havana 9%, Vegas 38%, Miami 31%, LA 7%, NY 52% pixels anders, vooral de verre banden; Frisco Nob Hill 0% (camera op 8268 hoog, de laag ligt in de dalen).
- Gezien: waas over de verre skyline, warm bij dusk; fog dichter in de straat. Menu-rij niet in het spel gezien.

---

## 1. Uitgangssituatie (gemeten in de code, 2026-10-14)

- Renderer: `src_rebuild/PsyCross/src/render/PsyX_render.cpp`. Eén ordering table (OT) tekent alles, ook HUD, rechtstreeks in de backbuffer van het venster. Geen post-process pass.
- Fragment shader (`GPU_FRAGMENT_SAMPLE_SHADER`, `gpu_shader_32_rgba`): `applySnow` -> `* v_color` -> `+ beamLight()` -> `applyFog` -> `dither`. `v_view.xyz * 128` = view-space positie (y omhoog), `v_view.w` = 1 voor 3D (PGXP), 2 = 3D zonder sneeuw, 0 = 2D (HUD, lucht?).
- Fog: `applyFog`, `f = 1 - exp(-(d * u_fog * 0.000108)^2)`, kleur `u_fogColor`. Gevoed door `draw.c UpdateSnow` (weerfog `gFogTarget`, afstandsfog `DistanceFogAmount`, kleur per tijdstip).
- Koplampbundel speler: `beamLight` (`u_beam`, `u_beamPos`, `u_beamDir` in view space), gevoed door `draw.c UpdateHeadlights`. Het enige echte per-pixel licht.
- Depth: `GR_EnableDepth`; depth test aan bij `BM_NONE`, uit bij elke blend mode (halfdoorzichtige prims schrijven en testen geen depth). `g_cfg_pgxpZBuffer` kan alles uitzetten. `GR_BeginScene` wist depth naar 1.0.
- Framebuffer-kopieën bestaan al: `GR_StoreFrameBuffer` (`glBlitFramebuffer` naar `g_glBlitFramebuffer`), offscreen FBO `g_glOffscreenFramebuffer`.
- Eigen OT-commando: `DR_PSYX_TEX` (`libgpu.h`, `PsyX_GPU.cpp` ~1544), gebruikt door de HD-lucht. Zelfde mechanisme kan een post-pass midden in de OT starten.
- Lichtbronnen (allemaal additieve sprites, geen belichting van de wereld):
  - zon/maan: `sky.c sun_position[4]`, `moon_position[4]` (één per stad, NIET per tijdstip), `DrawLensFlare` (`sky.c` 820, aangeroepen in `main.c` 2729 na `DrawMission`). Meet zonzichtbaarheid via `StoreImage` van 12x10 pixels (`flare_col`). Slaat over bij regen/sneeuw, bewolkt, dawn en dusk.
  - lantaarns: `debris.c AddLightEffect` (1573), `AddSmallStreetLight` (1448), D1 `objanim.c AnimateD1Light`.
  - verkeerslichten: `debris.c AddTrafficLight` (1895).
  - autolichten: `ShowCarlight` (2014), `ShowLight`/`ShowLight1`, `ShowFlare` (1814), sirene `AddCopCarLight`.
  - grondreflectie: `ShowGroundLight` (2367), `DisplayLightReflections` (1341).
  - Alle functies rekenen een positie relatief aan de camera uit (`dpos = cop->pos - camera_position`): daar wordt de lichtenlijst van deel 2 gevuld.

## 2. Vastgelegd contract (namen, niet meer wijzigen zonder dit plan bij te werken)

PsyCross (`PsyX_public.h`, implementatie `PsyX_render.cpp`):
- `void PsyX_SetAtmosphere(float heightFalloff, float heightDensity, const float* worldUpView, float camHeight, const float* sunDirView, const float* sunColor, float sunScatter);` deel 1. Aangeroepen vanuit een eigen `UpdateAtmosphere` (draw.c), niet vanuit `UpdateSnow`.
- `#define PSYX_MAX_VOLUME_LIGHTS 16`
- `void PsyX_ClearVolumeLights(void);` en `void PsyX_AddVolumeLight(const float* posView, const float* color, float radius);` deel 2 (posities in view space, y omhoog, zelfde eenheden als `beamLight`).
- `void PsyX_SetVolumeBeam(float amount);` deel 2: de bestaande `u_beam`-kegel krijgt in-scattering.
- `void PsyX_SetSunShafts(float amount, float screenX, float screenY, const float* color);` deel 3, scherm in PSX-coördinaten (0..320, 0..256, zoals `DrawLensFlare`).
- OT-commando voor deel 3: `DR_PSYX_SUNSHAFTS` (zelfde opbouw als `DR_PSYX_TEX`), ingevoegd door `DrawLensFlare` op een OT-plek vóór de HUD.

Game (`config.ini [render]`, gelezen in `redriver2_psxpc.cpp` naast `distanceFog`/`skyHd`):
- `atmosphere=1` (deel 1), `volumeLights=1` (deel 2), `sunShafts=1` (deel 3). 0 = huidig beeld.
- Variabelen `gAtmosphere`, `gVolumeLights`, `gSunShafts` (draw.c / sky.c). Menu pas later: de `reserved[]`-slots zijn vol, alleen bits in een bestaand slot.

Shader-uniforms: `u_hfFalloff`, `u_hfDensity`, `u_worldUp`, `u_camHeight`, `u_sunDir`, `u_sunColor`, `u_sunScatter`, `u_lightCount`, `u_lightPos[16]`, `u_lightCol[16]` (rgb + radius in w), `u_beamVolume`.

## 3. Delen

### Deel S: spike luchtmasker (vóór alles, want deel 3 hangt ervan af)

Vraag: kan de renderer na de 3D-scène per pixel zien "hier is lucht"?
- Lees `sky.c DrawSkyDome`/`PlotSkyPoly` en de 2D/3D-tak in `GTE_PERSPECTIVE_CORRECTION`: welke z en welke blend mode krijgt de lucht, wordt depth geschreven?
- Meet in het spel: tijdelijke dump van de depth buffer (`glReadPixels(GL_DEPTH_COMPONENT)`) bij `-shot`, telt pixels met depth 1.0 en schrijft een PGM; vergelijk met `SCREENSHOT.BMP` (lucht = 1.0?). Dump weer verwijderen.
- Uitkomst A: lucht = depth 1.0 (ongeschreven) -> masker = `depth >= 0.9999`.
- Uitkomst B: lucht schrijft depth -> markeer de lucht met de stencil (bit vrij in `GR_SetStencilMode`?) of met `v_view.w == 0` via een tweede render target, of val terug op helderheidsmasker rond de zon.
- Klaar als: sectie 0a beschrijft het masker dat deel 3 gebruikt, met een getal (percentage luchtpixels in Chicago-dag en Miami-dag).

### Deel 1: atmosferische fog (alleen shader + `UpdateSnow`)

- `applyFog` wordt: afstandsfog (zoals nu) + hoogtefog `density * exp(-falloff * h)`, analytisch geïntegreerd langs de straal camera->pixel (formule: `fogAmount = density * exp(-falloff * camH) * (1 - exp(-falloff * rayDir.y * d)) / (falloff * rayDir.y)`, met kleine `rayDir.y` afvangen).
- NIET bouwen op de sneeuwcode (gebruiker 2026-10-14: "sneeuw code was niet goed genoeg"): `u_groundPos`/`u_groundUp` komen uit één `MapHeight`-probe onder de camera (fout op hellingen, bruggen, boven water) en `applySnow` gebruikt `dFdx/dFdy`-normalen. Hoogtefog krijgt eigen uniforms: `u_worldUp` (wereld-omhoog in view space) en `u_camHeight` (camera-hoogte boven een vaste referentie per stad, zeespiegel 0), dus geen grondprobe. De sneeuwcode zelf blijft onaangeroerd; wijzigingen in `applySnow`/`UpdateSnow` alleen als het een eigen opdracht wordt.
- Fogkleur: mix van `u_fogColor` met `u_sunColor` volgens Henyey-Greenstein (`g` ~0.6) op `dot(viewDir, u_sunDir)`: tegen de zon in warm, ervan af koel.
- Zonrichting in view space: `sun_position[BASE_CITY]` door `inv_camera_matrix` (zoals `DrawLensFlare`), doorgegeven in `UpdateAtmosphere`.
- Lucht (2D, `v_view.w == 0`) krijgt geen fog: horizonmenging is een apart klein punt, alleen als het zonder masker kan (anders naar deel 3).
- Test: `-shot` Chicago dag, `-fog`, Frisco Nob Hill (`-startpos -11250 240000 -startrot 1024`), `-tod 2` dusk; `atmosphere=0` geeft pixel-gelijk beeld met de huidige build (A/B diff 0%).

### Deel 2: volumetrische lampen

- Game: `PsyX_ClearVolumeLights` aan het begin van de frame (naast `UpdateSnow`), `PsyX_AddVolumeLight` in `AddLightEffect`, `AddSmallStreetLight`, `AddTrafficLight`, `AnimateD1Light` en `ShowCarlight` (koplampen) met de al berekende camera-relatieve positie door `inv_camera_matrix`. Houd de 16 dichtstbijzijnde (vervang de verste).
- Alleen actief bij `gNight`, `gFogTarget`, regen of dawn/dusk (overdag geen halo's); sterkte schaalt met fog/regen.
- Shader: per licht het analytische integraal van `1/(r^2 + d^2)` langs het segment camera->pixel (`(atan((t + b)/h) - atan(b/h)) / h`), maal kleur en dichtheid; optioneel phase-term. Koplampkegel: 8 stappen langs het segment met de bestaande `cone`/`atten`-functie.
- Beperking: pixels zonder geometrie (lucht) krijgen geen halo.
- Kosten meten: `-shot`-log `build/submit` vóór en na, Chicago 10x nacht met crowd 5 (veel lichten).
- Test: `-tod 3`, `-fog`, Miami SLAMP2 en New York TLIGHT01 (posities in `plans/d1-cars-and-lights.md`), Chicago nacht.

### Deel 3: god rays

- `DrawLensFlare` zet `PsyX_SetSunShafts` (sterkte = `flare_col`-zichtbaarheid x hoek tot schermmidden) en voegt `DR_PSYX_SUNSHAFTS` in de OT vóór de HUD in.
- Renderer bij dat commando: flush de batch, blit kleur + depth naar een FBO, bouw het luchtmasker op 1/4 resolutie (depth-drempel zonder luchtspiegeling, zie 0a), radial blur 48 samples richting zon (decay 0.96, weight, exposure), additief terug over de backbuffer, dan verder met de OT.
- Zon voor dawn/dusk: `sun_position` is één waarde per stad. God rays bij laagstaande zon vragen een zonpositie per tijdstip: kies een vaste lage positie (zelfde azimut, lagere elevatie) voor dawn/dusk, gelijk aan waar de lucht-textuur de zon toont (meten op screenshot). Laat `DrawLensFlare` de flare zelf ongewijzigd overslaan bij dawn/dusk; alleen de shafts lopen dan.
- Uit bij regen/sneeuw/bewolkt en 's nachts.
- Test: Chicago dag tegen de zon in (`-camangle`, `-startrot` zo kiezen dat de zon in beeld staat), Miami, Frisco; `sunShafts=0` A/B diff 0%.

## 4. Regels voor elk deel

- D2 en D1 delen deze renderer: na elke wijziging `-shot` in Chicago, Havana, Vegas (zonder `-startpos`) en de vier D1-steden.
- Testen in een eigen kopie van de testinstallatie (andere sessies gebruiken `config.ini` en `REDRIVER2.log`).
- PsyCross-wijzigingen eerst committen in de fork, daarna de submodule-pointer in deze repo.
- Shader-tekst: `\n` in C-strings; scripts met backslashes via een bestand (zie CLAUDE.md).
