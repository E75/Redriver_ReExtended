# Plan: GTA-stijl camerabesturing (rechter stick, terugkeer) voor REDRIVER2

Branch: `feature/driver1-cities` (of een eigen branch `feature/camera-orbit` vanaf `origin/feature/driver1-cities`; keuze van de gebruiker, zie sectie 5). Push alleen naar `origin`.
Achtergrond en bronnen: `plans/gta-camera-controls-analysis.md` (lees sectie 1, 3 en 6 eerst). Dit plan is de vertaling naar onze code.

Doel: in de auto (en later in de cabine) stuurt de rechter stick de camera rond de auto; na loslaten wacht de camera kort en glijdt dan vloeiend terug achter de auto, zonder sprong en zonder overshoot. Bestaand gedrag blijft ongewijzigd zolang de rechter stick niet wordt aangeraakt.

Eén deel per sessie, één groene commit per deel. Update sectie 0 aan het eind van elk deel.

---

## 0. Status

| Deel | State |
|---|---|
| 0 Aannames verifiëren | klaar 2026-10-11 (bevindingen in 0a) |
| 1 Test-harnas + kern (yaw-offset, terugkeer) | klaar 2026-10-12: `-camtest` tegen het echte spel (Chicago en Miami) ALL PASS |
| 2 Pitch (omhoog / omlaag kijken) | klaar 2026-10-12: -120 .. 380 eenheden, stick omhoog = camera omlaag, checks P14-P20 PASS, op het oog bekeken |
| 3 Look-knoppen als doel-offset (links / rechts / achter) | achterom als `camLook`-offset klaar (P21-P25 PASS); links/rechts blijven op `headPos` (al snel, bewust niet aangeraakt) |
| 4 Aan/uit in het menu (Options - Gameplay - Secret, "Stick Camera") + `config.ini` | code + build groen; `config.ini`-opties `cameraAutoCenter`, `cameraReturnDelayMs`, `cameraStickSensitivity`, `cameraInvertY` klaar; menu en echte pad nog niet door de gebruiker gezien |
| 5 Cabine-camera (view 2) vrij rondkijken | optioneel |
| 6 Drift-bewuste auto-richting | optioneel |
| 7 Muis | optioneel |
| 8 Te voet | optioneel, eigen beslissing |

### 0a. Bevindingen deel 0 en stand deel 1

- **Analoge modus staat standaard uit**: PsyCross zet het pad op `id 0x41` (digitaal, `type 2`); pas na Select+Start wordt het `0x73` (`type 4`), en het toetsenbord dwingt digitaal af (`PsyX_pad.cpp:215-257`). `analog[]` wordt in beide modi gevuld en `MapPad` kopieert `mapanalog` ook voor `type 2`. **Afwijking van sectie 2.2:** `UpdateCameraOrbit` leest de stick bij `type 2` én `4` (niet alleen 4), anders werkt de rechter stick pas na het inschakelen van analoog. Toetsenbord-only geeft 0 (midden), dus veilig.
- **Tekenconventie** (afgeleid, nog te bevestigen in het spel): `hd.direction = ratan2(m[0][2], m[2][2])` is de hoek van +z naar +x, kloksgewijs gezien van boven; de camera staat op `dir + 2048 + camYaw`. `camYaw > 0` zet de camera links van de auto = kijkrichting naar rechts = stick rechts. De check P3 (`lateral < 0`) meet dit in het log; een visuele controle blijft nodig.
- **Eén update per tick**: niet bewezen, check P1 (`fc`-stap == 1) bewijst het zodra het harnas draait.
- **Resets nodig op twee plekken** (`ChangeCarPlayerToPed`, `ChangePedPlayerToCar` in `players.c`); `InitPlayer` doet `ClearMem` en begint dus op 0.
- **Stick-klik**: R3 is uitstappen, L3 achterom (zoals in sectie 1).
- **Deel 1 geïmplementeerd**: `dr2types.h` (`camYaw`, `camIdle`), `players.c` (resets), `camera.c` (`UpdateCameraOrbit`, constanten, `-camtest` script en logregel), `main.c` (`-camtest` optie). `plans/camera-orbit-check.py` bevat de PASS/FAIL-checks P0-P13.
- **Gevalideerd zonder spel**: de checks zijn gedraaid tegen een Python-port van `UpdateCameraOrbit` (scratchpad); alles PASS, terugkeer naar 0 duurt 96 ticks na loslaten (3,2 s). Dat bewijst alleen dat plan, rekenwerk en checkscript elkaar kloppen, niet het spel.
- **Blokkade opgelost (2026-10-12)**: het harnas draait in de testinstall `GameTestInstall\REDRIVER2_D1` (`-city 0 -mission 50 -camtest` zet de speler in de auto; daarna `python -I plans/camera-orbit-check.py <testmap>\REDRIVER2.log`). Uitkomst: één camera-update per tick, `camYaw > 0` zet de camera links van de auto (stick rechts), de stick levert 21 eenheden per tick (`SquareRoot0(10000)` is 99, niet 100), terugkeer 95 ticks na loslaten. Niet gedekt: een echte pad.
- Op de branch staan ook niet-gecommitte wijzigingen van een andere sessie (`src_rebuild/d1dlc/*`); die horen niet bij dit werk en worden niet gestaged.

### 0b. Menu-optie (opdracht gebruiker: Options - Gameplay - Secret, in- en uitschakelbaar)

- **Waar**: Options > Gameplay > Secret is `PsxScreens[29]` met `CheatScreen` (`FEmain.c`). De rijen staan nu in één tabel `rows[]` in `CheatScreen`; een extra geheim of schakelaar is één regel meer (max 8 knoppen; vanaf 7 rijen krimpt de afstand, `step`). De oude if-ladder per aantal (1/2/3/4+) is vervangen door één lus met dezelfde u/d-koppelingen en dezelfde posities voor 1-6 rijen.
- **Nieuwe rij "Stick Camera"** staat altijd in de lijst (niet afhankelijk van vrijgespeelde geheimen), want het is een besturingsoptie. Daardoor is het bericht "Sorry no secrets" (`cheatText[0]`) onbereikbaar geworden; de tekst blijft in de taaltabel staan.
- **On/Off-scherm**: `PsxScreens[42]` (`CAMERA_ORBIT_SCREEN`), in code gekloond van scherm 31 zoals het mini-cars-scherm; `CameraOrbitOnOffScreen` (user function 26) zet de selectie op de huidige stand; knop-var `FE_MAKEVAR(FEVAR_CAMERA_ORBIT = 16, 0|1)` zet `gCameraOrbit`. `PsxScreens` is 42 → 43 groot.
- **Opslaan**: in het profiel (`config.dat`) in `CONFIG_SAVE_HEADER.reserved[5]` = 1 als UIT (0 = aan, dus oude profielen blijven aan). Standaard aan. Opgeslagen via Options > Save Settings (of het bestaande opslaan bij afsluiten).
- **Uit** = `UpdateCameraOrbit` zet `camYaw`/`camIdle` op 0 en doet niets: het spel gedraagt zich dan exact als vóór deze wijziging. `-camtest` forceert aan.
- **Voor andere sessies**: wie een tweede schakelaar toevoegt neemt een volgend scherm (43), een volgende `FEVAR_`-code (17+) en een volgend `reserved[]`-slot (4 of lager, 5 is bezet); gebruik de bestaande rij-tabel.
- **Niet getest in het spel** (geen testinstallatie): het scherm moet visueel gecontroleerd worden: (1) Secret toont de lijst met "Stick Camera" onderaan, ook zonder vrijgespeelde geheimen, (2) selectie loopt omhoog/omlaag rond, (3) On/Off toont de huidige stand, (4) terug naar Gameplay en Save Settings bewaart de keuze, (5) bij 6 rijen (alles ontgrendeld) + Stick Camera = 7 rijen past de lijst nog op het scherm (afstand 30).
- Bestaande bug gezien, niet aangeraakt: in de actiehandler loopt `case 14` (bonus gallery) zonder `break` door in `case 15` (mini cars uit).

---

## 1. Wat de code nu doet (gelezen, regelnummers kunnen een paar regels opschuiven)

- **Tijdstap**: het spel loopt vast op 30 FPS (`main.c:808` "set to 30 FPS VSync", `StepGame` `main.c:1280`). Alle tijden hieronder zijn **ticks** (1 tick = 1/30 s). `RenderGame2` (`main.c:2276`) roept per speler `InitCamera` aan → `PlaceCameraFollowCar` ([camera.c:421](../src_rebuild/Game/C/camera.c:421)), dus de camera-update draait één keer per getekend frame per speler; dat is het bestaande patroon (`lp->cameraAngle` wordt daar ook bijgewerkt, bewaakt door `pauseflag == 0`).
- **Volgcamera** (`camera.c:470-487`): `camAngle = lp->cameraAngle - (lp->headPos >> 16)`; `lp->cameraAngle += angleDelta >> 3` richting `baseDir + gCameraAngle` (`gCameraAngle` default 2048 = achter de auto). `baseDir` = `car_data[cameraCarId].hd.direction` (`camera.c:112`) of bij een voetganger `lp->dir`. Eenheden: 4096 = 360°. Dit is een enkelvoudige exponentiële volger: factor 1/8 per tick, τ ≈ 8 ticks ≈ 0,27 s.
- **Camera-positie** uit `camAngle`: `cameraPos = basePos + (sin, cos)(camAngle) * cameraDist`, hoogte `carheight` met kleine neerwaartse hoek `camera_angle.vx = 25`, daarna **botsing** via `jcam` + `CheckScenaryCollisions` (`camera.c:510-538`). `jcam->hd.direction = camAngle & 0xfff`, dus een extra offset in `camAngle` wordt automatisch meegenomen in de botsing.
- **Look-knoppen** (`TurnHead`, `camera.c:363`): digitaal. `CAMERA_PAD_LOOK_LEFT = MPAD_L2`, `RIGHT = MPAD_R2`, `BACK = L2+R2` of `MPAD_L3` (`pad.h:29-33`). `headPos += (headTarget - headPos) / 2` per tick; achteromkijken zet `camAngle = baseDir` direct. `headPos` wordt ook door `PlaceCameraInCar` (`camera.c:594, 612`) en `pedest.c:2754` gebruikt → **niet aanpassen**.
- **Rechter stick bestaat al in `Pads`**: PsyCross zet `analog[0..3] = RX, RY, LX, LY` (`PsyX_pad.cpp:358-361`); `MapPad` (`pad.c:184-219`) geeft `mapanalog[0]=RX, [1]=RY, [2]=LX, [3]=LY` (bij `swap_analog == 0`, het default) in het bereik −128..127. In de auto wordt alleen **LX** gebruikt (sturen, `handling.c` via `t1`); toetsenbord stuurt met een bestaande ramp (`+32/tick`, max 352; snelstuur +64, max 511; terugcentreren −64/tick, `handling.c:1189-1267`). **Toetsenbordsturen hoeft dus niet nieuw.**
- RX/RY worden alleen door Tanner gebruikt voor hoofdrichting te voet (`pedest.c:1296-1315`, alleen als `Pads[].type == 4`). In de auto zijn RX/RY dus vrij.
- **R3 is al "uitstappen"** (`CAR_PAD_LEAVECAR_DED = MPAD_R3`), **L3 is "achterom kijken"**. Dus de stick-klik van GTA werkt hier niet 1-op-1: houd L3 aan voor achterom.
- Muis: er is alleen een debug-handler voor de free camera (`redriver2_psxpc.cpp:59`, `g_dbg_gameDebugMouse`); geen gewone muisinvoer in het spel.
- Configuratie: `ini_sget(config, "game", "...", "%d", &var)` in `redriver2_psxpc.cpp:577-589`.
- Debug-opties: `-shot`, `-city`, `-mission` enz. in het `#ifdef DEBUG_OPTIONS`-blok `main.c:1951-2064`; `gShotFrame` wordt in `DrawGame` (`main.c:1643`) per frame gecontroleerd.
- `PLAYER` struct: `dr2types.h:1264` (velden `cameraAngle`, `headPos`, `headTarget`, `headTimer`). Alle plekken die `headPos` resetten: `players.c:78-81, 102-108, 193-199` en `pedest.c:2909`.

---

## 2. Ontwerp

### 2.1 Nieuwe state (per `PLAYER`, naast `headPos`)
```
short camYaw;     // handmatige offset t.o.v. de auto-richting, 4096 = 360°, genormaliseerd -2048..2047
short camPitch;   // handmatige hoogte-offset, 0 = default (deel 2)
u_char camIdle;   // ticks sinds de laatste camera-input, verzadigd op 255
```
Wordt op 0 gezet op dezelfde plekken als `headPos` (zie boven), en bij wisselen van camerastand in `ModifyCamera`.

### 2.2 Eén functie, aangeroepen uit `PlaceCameraFollowCar`
`static void UpdateCameraOrbit(PLAYER* lp)`, direct na `TurnHead(lp)` (`camera.c:468`), alleen als `pauseflag == 0`.

Wanneer **niet** actief: `NoPlayerControl`, `gInGameCutsceneActive`, `events.cameraEvent`, `lp->padid < 0` (cutscene-stream), `Pads[padid].type != 4` (geen analoge pad) of `lp->cameraCarId < 0` (voetganger/event) → `camYaw = camPitch = camIdle = 0` en return. (Zo blijft replay/director/cutscene onaangetast.)

Per tick:
1. **Invoer**: `rx = mapanalog[0]`, `ry = mapanalog[1]`. Radiale deadzone `DZ = 28` (zelfde orde als de bestaande `> 32` in `pedest.c`): `m = sqrt(rx²+ry²)`; `m < DZ` → geen input, anders schaal naar 0..128: `f = (m - DZ) * 128 / (128 - DZ)`, `rx = rx*f/m`, `ry = ry*f/m`. Responscurve: `v = v * |v| / 128` (zachte ease-in, kleine uitslag nauwkeurig).
2. **Input actief** (`f > 0`) of een look-knop vast (`paddCamera` L2/R2/L3): `camIdle = 0`; bij stick: `camYaw += rx * YAW_RATE / 128` (YAW_RATE = 44 eenheden/tick ≈ 130°/s bij vol), tekens in deel 1 vaststellen. Normaliseer `camYaw` naar −2048..2047.
3. **Geen input**: `camIdle = min(camIdle + 1, 255)`.
4. **Terugkeer**: `w16 = clamp((camIdle - RETURN_DELAY) * 16 / RETURN_RAMP, 0, 16)` met `RETURN_DELAY = 30` (1,0 s), `RETURN_RAMP = 12`. Dan `camYaw -= camYaw * w16 / (16 * RETURN_TAU)` met `RETURN_TAU = 12` ticks (≈ 0,4 s). Omdat integer-deling naar 0 afrondt: als `0 < |camYaw| <= 8` en `w16 > 0` → `camYaw = 0` (snap onder ~0,7°; voorkomt eindeloos sluipen).
5. **Gebruik**: in `PlaceCameraFollowCar` wordt `camAngle = lp->cameraAngle - (lp->headPos >> 16) + lp->camYaw` (let op het teken; zie deel 1).
6. **Achterom kijken** (L2+R2 / L3) blijft `camAngle = baseDir` zoals nu, zonder `camYaw`; zodra losgelaten blijft `camYaw` zoals hij was en loopt de normale terugkeer na `RETURN_DELAY`.

Waarom dit vloeiend is: de offset is **relatief** aan de auto-richting (`lp->cameraAngle` volgt `baseDir` onafhankelijk), de terugkeer is een exponentiële afname met zachte start (`w16`-ramp) en wachttijd, en stopt direct bij nieuwe input. Geen overshoot mogelijk want `camYaw` verandert alleen richting 0.

### 2.3 Constanten (startwaarden, in één blok bovenaan `camera.c`, alles in ticks/eenheden)
| Naam | Waarde | Betekenis |
|---|---|---|
| `CAMORBIT_DEADZONE` | 28 | van 128 |
| `CAMORBIT_YAW_RATE` | 44 | eenheden/tick bij vol uitslag |
| `CAMORBIT_PITCH_RATE` | 24 | deel 2 |
| `CAMORBIT_RETURN_DELAY` | 30 | ticks (1,0 s) |
| `CAMORBIT_RETURN_RAMP` | 12 | ticks |
| `CAMORBIT_RETURN_TAU` | 12 | ticks (≈ 0,4 s) |
| `CAMORBIT_SNAP` | 8 | eenheden |

Tuning gebeurt in deel 4 via `config.ini`. Eerst alle waarden vast, dan pas instelbaar maken.

---

## 3. Delen

### Deel 0 — Aannames verifiëren (korte sessie, geen code)
Doel: de drie dingen vastleggen die het plan nu veronderstelt.
1. `Pads[padid].type == 4` voor een Xbox/PS-pad onder PsyCross (anders is de test hieronder nooit actief; nu is `id == 115` → `type 4` in `MapPad`; controleren dat PsyX `id 0x73` meegeeft). Als het `1` of `2` is: de conditie wordt `type != 0` plus een analoge-pad-check.
2. Tekenconventie: stick rechts → camera zwenkt zó dat de kijkrichting naar rechts van de auto gaat (GTA-gedrag). Bepalen of dat `camYaw += rx…` of `-=` is in `camAngle` (sinus/cosinus-richting in `camera.c:489-490`).
3. Of `PlaceCameraFollowCar` per tick precies één keer per speler draait (log een teller naast `FrameCnt` een paar seconden).
Uitkomst in sectie 0a van dit plan opschrijven. Commit: alleen dit plan.

### Deel 1 — Test-harnas + kern
1. **Eerst het harnas** (debug-only, `#ifdef DEBUG_OPTIONS`): `-camtest` in het optieblok `main.c:~1981` (naast `-shot`). Het activeert een globaal script `gCamTest` dat in `UpdateCameraOrbit` de stick **overschrijft** met een vast scenario en elke tick een regel logt:
   `CAMTEST t=<tick> rx=<..> yaw=<camYaw> idle=<camIdle> w=<w16>`; aan het eind logt het `CAMTEST done` en sluit het spel netjes af (zelfde afsluitpad als normaal quit, zodat `REDRIVER2.log` flusht, zie CLAUDE.md).
   Scenario (start vanaf `FrameCnt > 90` zodat de auto stilstaat/rijdt gelijk blijft): 
   A. stick rechts vol 45 ticks vast → loslaten → wachten tot `yaw == 0`.
   B. stick rechts 45 ticks → loslaten → na 20 ticks weer 20 ticks stick links → loslaten → wachten tot 0.
   C. stick klein (binnen deadzone, 20) 60 ticks → `yaw` moet 0 blijven.
2. **Kern**: structvelden (2.1), reset op de `headPos`-plekken, `UpdateCameraOrbit` (2.2), gebruik in `camAngle`.
3. **Testscript** `plans/camera-orbit-check.py` (of in de scratchpad, niet incheckbaar nodig): start `REDRIVER2_dev.exe -city 0 -mission 50 -camtest` in de testmap, wacht tot het proces eindigt, parse het log en print `PASS n` / `FAIL n: <actual> ≠ <expected>`:
   - P1: tijdens vasthouden groeit `|yaw|` monotoon en blijft ≤ 2048.
   - P2: na loslaten blijft `yaw` constant voor ≥ `RETURN_DELAY` ticks (toleranties 0).
   - P3: daarna neemt `|yaw|` monotoon af, wisselt nooit van teken (geen overshoot) en bereikt 0 binnen `RETURN_DELAY + RETURN_RAMP + 8·RETURN_TAU` ticks.
   - P4 (scenario B): zodra input terugkomt, stopt de afname direct (`idle == 0` en `yaw` verandert in de richting van de input).
   - P5 (scenario C): `yaw == 0` de hele tijd.
4. **Handmatige check** met echte pad: rijden, rechter stick opzij, loslaten, kijken of de terugkeer vloeiend is. Niet vervangbaar door het script (gevoel). Eén screenshot is niet nodig; beschrijf wat je ziet.
5. Build (zie CLAUDE.md, Release_dev, `OutDir` naar de testmap), QC-pass (`~/.claude/rules/general-qc.md` + eventuele repo-regels), commit `Camera: right stick orbit with smooth return (part 1)`. Geen Co-Authored-By-trailer (repo-regel).

Done als: P1–P5 PASS, handmatige check goed, geen verandering zolang de stick niet wordt aangeraakt (rij 20 s zonder input en vergelijk visueel met eerder gedrag; er mag geen nieuwe beweging zijn omdat `camYaw` 0 blijft).

### Deel 2 — Pitch
- `camPitch` in eenheden 4096/360°, begrensd (startwaarde −100 … +560: iets onder de standaard tot ~50° omhoog; uitkomst op het oog bepalen). Rate `CAMORBIT_PITCH_RATE`. Zelfde terugkeer als yaw (zelfde `w16`).
- Toepassen in `PlaceCameraFollowCar`: `camera_angle.vx` (nu 25 + wereldhoogte-correctie `camera.c:505`) krijgt `+camPitch` (kijkhoek), en `cameraPos.vy` gaat omhoog met `-FIXEDH(RSIN(camPitch) * lp->cameraDist)` (y is negatief omhoog). De bestaande wereldhoogte-controle (`camera.c:496-508`) moet **na** deze aanpassing blijven draaien zodat de camera niet onder de grond komt; controleren dat `cammapht`-clamp nog klopt.
- Test: harnas uitbreiden met ry-scenario; checks P1–P4 voor `pitch`; extra P6: `pitch` blijft binnen de limieten; P7: cameraPos.vy ligt nooit onder `MapHeight` van die plek (log `camera_vy - MapHeight`).
- Valkuil: omhoog duwen vergroot de afstand tot de auto niet; controleer dat bij +560 de auto nog in beeld is (kijkpunt = `basePos`).

### Deel 3 — Look-knoppen als doel-offset
- L2/R2 (links/rechts) en L3/L2+R2 (achter) werken nu via `headPos` en direct `camAngle = baseDir`. Doel: achterom kijken glijdt met dezelfde easing (nu is het een harde sprong, `camera.c:479`).
- Oplossing zonder `headPos` aan te raken: een tweede, korte offset `camLook` (doel: 0, ±1024, 2048) die per tick met `/2` naar het doel loopt (zelfde snelheid als `headPos`) en samen met `camYaw` wordt opgeteld; bij loslaten terug naar 0 met dezelfde easing. Na de wijziging mag `camAngle = baseDir` bij achteromkijken verdwijnen.
- Risico: `PlaceCameraInCar` en `pedest.c` gebruiken `headPos`; de cabine-camera moet identiek blijven → alleen in `PlaceCameraFollowCar` wijzigen.
- Test: harnas-scenario D: L3 vast 30 ticks, loslaten; P8: `camLook` loopt monotoon naar 2048 en terug naar 0, nooit voorbij.

### Deel 4 — Instellingen
- `config.ini` sectie `[game]`: `cameraAutoCenter` (0/1, default 1), `cameraReturnDelayMs` (default 1000), `cameraStickSensitivity` (percent, default 100), `cameraInvertY` (0/1, default 0). Inlezen bij `redriver2_psxpc.cpp:577-589` met `ini_sget`, zichtbaar maken als `extern int` in `camera.c`.
- `cameraAutoCenter = 0` → geen terugkeer (de offset blijft staan; wel een manier om hem te resetten: stick-klik is bezet door R3 = uitstappen, dus reset op camerastand-wissel `ModifyCamera` of kort L3).
- Test: harnas met andere ini → P3 verandert mee.
- Voor een toekomstige in-game instelling (pauzemenu) apart deel; niet in deze scope.

### Deel 5 — Cabine-camera (optioneel)
`PlaceCameraInCar` (`camera.c:573`) draait nu op `headPos` voor links/rechts kijken. Zelfde `camYaw`/`camPitch` toevoegen aan `camera_angle.vy/vx` + terugkeer. Aparte tuning (cabine: ±120° yaw-limiet, snellere terugkeer). Eigen deel omdat het een andere camera-matrix opbouwt (`camera.c:594-612`).

### Deel 6 — Drift-bewuste auto-richting (optioneel, grootste invloed op feel)
Nu volgt de camera `hd.direction` (neus). Voorstel: `baseDirEff = baseDir + wrap(velDir − baseDir) * mix`, waarbij `velDir = ratan2(linearVelocity[0], linearVelocity[2])` (`car_data[].st.n.linearVelocity`, eenheid eerst controleren) en `mix` loopt van 0 bij < 2 m/s tot ~0,6 bij hoge snelheid; alleen bij voorwaartse beweging (dot > 0, geen achteruit-flip), gefilterd met de bestaande 1/8-volger. Instelbaar `cameraDriftFollow` (default 0 tot de gebruiker tevreden is, want dit verandert het standaardgevoel van iedereen).

### Deel 7 — Muis (optioneel)
Nodig: een gewone muisinvoer-hook in PsyCross (nu alleen `g_dbg_gameDebugMouse`). Voorstel: nieuwe callback die `dx,dy` per frame accumuleert in `Pads[0]`-achtige velden `mouseDx/Dy`; `UpdateCameraOrbit` gebruikt die als stick-equivalent (delta, **niet** × dt), met `PsyX_SetCursorRelative(1)` alleen tijdens rijden. Dezelfde `camIdle`/terugkeer. Eerst beslissen of PsyCross-aanpassing ok is (submodule `OpenDriver2/PsyCross`, "untouched" volgens CLAUDE.md).

### Deel 8 — Te voet (optioneel, aparte beslissing)
Niet in v1: Tanner heeft tankbesturing (linker stick draait Tanner), de rechter stick draait al zijn hoofd (`TannerCameraHandler`). GTA-achtige camera-relatieve besturing is een grote ingreep in `ProcessTannerPad`/`pedest.c` en in de aanloop van missies; eerst beslissen of dit gewenst is. Als wel: camera te voet gebruikt dezelfde `PlaceCameraFollowCar` (cameraCarId < 0, `baseDir = lp->dir`), dus de orbit-offset werkt daar vanzelf zodra de conditie "niet voetganger" wordt versoepeld; het conflict met het hoofdbeweging-gebruik van RX/RY moet dan worden opgelost.

---

## 4. Risico's en valkuilen

- **Tekenfouten** in yaw/pitch (4096-hoeken, y negatief omhoog): daarom Deel 0 + het log-harnas; nooit "op het oog" vaststellen.
- **Camera-update per render** i.p.v. per simulatiestap: bestaand patroon, maar controleer in Deel 0 dat het één keer per tick draait; draait het vaker (bijv. bij pause-overlay-redraws), moet de update achter `pauseflag == 0` en een tick-teller (`FrameCnt`) lopen.
- **Wraparound**: `camYaw` in −2048..2047 houden; verschillen altijd via `DIFF_ANGLES` of `((x + 2048) & 4095) − 2048`.
- **Integer afronding** in de terugkeer: zonder snap blijft `camYaw` op 1-8 hangen.
- **Replay/director/cutscene**: `UpdateCameraOrbit` moet daar nul zetten, anders erft de volgende scène de offset.
- **Geen gedragsverandering zonder input**: `camYaw = 0` → formule identiek aan nu. Controleren in Deel 1.
- **L3 vs R3**: R3 = uitstappen, dus niet voor camera gebruiken.
- **Multiplayer** (2 spelers): alle state in `PLAYER`, input via `Pads[lp->padid]`; testen met `-player2car`.
- **CRLF/BOM**: bronbestanden CRLF (soms met BOM, `redriver2_psxpc.cpp`); `//`-commentaar mag niet op een backslash eindigen. Geen nieuwe bestanden nodig in deel 1-4 (dus ook geen premake-regeneratie); nieuw bestand pas als het harnas te groot wordt.

## 5. Open vragen voor de gebruiker (ik raad de eerste optie aan)

1. **Branch**: op `feature/driver1-cities` (camera is gedeeld met alle steden) of eigen branch `feature/camera-orbit`? Aanbeveling: eigen branch, kleinere review, makkelijk terugdraaien.
2. **Standaard aan?** Zonder rechter-stick-input verandert er niets, dus veilig: ja. Aanbeveling: ja, `cameraAutoCenter = 1`.
3. **Te voet** (deel 8): v1 overslaan. Aanbeveling: overslaan.
4. **Muis** (deel 7): later, eerst controller. Aanbeveling: later.
5. **Drift-mix** (deel 6) default uit. Aanbeveling: uit tot jij hem hebt getest.
6. **Volgorde**: deel 0 en 1 meteen, daarna 2-4; daarna beslissen over 5-8.
