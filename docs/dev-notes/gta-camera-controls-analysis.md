# GTA IV / V — camera- en stuurbesturing (3rd person): analyse + implementatiegids

Doel: precies begrijpen hoe de achtervolgcamera, de rechter stick (camera), de linker stick (lopen / sturen) en toetsenbord+muis werken in GTA IV/V, zodat we het in dit spel en in eigen spellen kunnen nabouwen.

## 0. Betrouwbaarheid (eerst lezen)

Rockstar's camera-code is closed source. Ik vond **geen** officiële documentatie, GDC-talk of dump met de echte constanten. Wat hier staat komt uit vier soorten bronnen, en elke bewering is zo gelabeld:

| Label | Betekenis |
|---|---|
| **[MOD]** | Gelezen in de open-source code van de GTA V-mods *Custom Camera V* / *Custom Camera V Plus* (Rbn3D). Dat is een **nabouw** van een racing-camera, niet Rockstar's code. De getallen zijn bruikbaar als beproefd startpunt, niet als "zo doet GTA het". |
| **[COMM]** | Spelersrapporten / mod-beschrijvingen (Steam, gta5-mods, forums). Gedrag klopt waarschijnlijk, getallen zijn vaag. |
| **[KENN]** | Mijn eigen kennis van het spel + game-camera-ontwerp. Niet in deze sessie geverifieerd; de plekken waar het ertoe doet staan in §10 als "nog te verifiëren". |
| **[VOORSTEL]** | Ontwerpkeuze van mij voor onze implementatie, geen claim over GTA. |

GTA IV: er is bijna geen technisch materiaal; alleen spelersgedrag. De meeste details hieronder gaan over GTA V (RAGE-camera's); GTA IV gedraagt zich in de basis hetzelfde (rechter stick = rondkijken, terugkeren achter de auto, stick-klik = achterom kijken) maar met meer lag [COMM, zwak].

---

## 1. Het kernmodel (dit is de sleutel tot "netjes en vloeiend terugkeren")

Alle goede 3rd-person-camera's in deze stijl zijn dezelfde machine:

```
cameraYaw   = autoYaw(target)  +  manualOffsetYaw
cameraPitch = autoPitch(speed, accel, terrain) + manualOffsetPitch
```

1. **Auto-component** — volgt het doel (auto/speler). Altijd actief, altijd vloeiend gefilterd.
2. **Manual-offset** — wat de speler met rechter stick / muis / look-knoppen *erbovenop* draait. Opgeslagen **relatief** aan de auto-richting, niet in wereldcoördinaten.
3. **Terugkeer** — als er een tijdje geen camera-input is, vervalt de offset exponentieel naar 0. De auto-component blijft intussen de auto volgen, dus er is nooit iets om "naar toe te snappen": de camera glijdt gewoon terug achter de auto.

Waarom relatief opslaan het hele probleem oplost: draait de auto terwijl je opzij kijkt, dan blijft je kijkhoek t.o.v. de auto gelijk; laat je los, dan kruipt de offset naar 0 en ligt je camera vanzelf op de plek waar de auto nú naartoe wijst. In wereldcoördinaten opslaan geeft een grote sprong op het moment van terugkeren.

Drie regels die de "GTA-feel" bepalen [COMM + MOD + KENN]:
- **Terugkeer start pas na een korte rust** (vertraging), en zodra de speler weer input geeft stopt de terugkeer direct (anders "vecht" je tegen de camera).
- **De auto-richting is niet de neus van de auto maar een mix van neus en bewegingsrichting** (zie §2.2) — daardoor zwaait de camera niet wild mee bij slippen.
- **Alles is tijd-gebaseerd (dt) en frame-rate onafhankelijk**.

---

## 2. Auto in de camera — detail

### 2.1 Positie en kijkpunt
- Pivot = autopositie + omhoog-offset (afhankelijk van autohoogte). [MOD: `height = model.Z * 0.8333 + heightOffset(0.3) + 0.15…`]
- Camera = pivot − richting(yaw, pitch) × afstand. Basis-hellingshoek camera naar beneden: **~3,5°** (instelbaar 0–20°). [MOD ini `cameraAngle = 3.5`]
- Afstand schaalt met autogrootte: achterkant gemeten aan botten (bumper/spoiler/achterruit), grote voertuigen + aanhangers vergroten de afstand. [MOD] → in ons spel: afstand uit de collision-box van de auto.
- Kijkpunt = pivot (iets voor/boven de auto), niet het midden van de wielen.
- **Collision**: raycast van pivot naar gewenste cameraplek; bij een hit de camera naar de hitplek + kleine marge naar voren. [MOD: ray, `endCoords + forward*0.1`]

### 2.2 Auto-richting (belangrijkste onderdeel)
De camera kijkt langs een gefilterde richting, niet langs de auto-neus [MOD]:

```
snelheid > 2 m/s  → smoothVelocity = lerp(smoothVelocity, velocity,     7.5 * dt)
anders            → smoothVelocity = lerp(smoothVelocity, vehForward,   7.5 * dt)
```
- Voertuigrotatie voor de camera: `slerp(camRot, vehRot, 3 * (1.39 + 0.017*speed) * dt)` — dus iets stijver bij hoge snelheid. [MOD]
- Bij **driften** is velocity ≠ neus: de camera ligt dan tussen de twee, daardoor "zie je meer van de bocht" en zwaait het beeld niet mee met elke achterkant-uitbreking. Een speler klaagt over de stock-camera die "zwaait bij driften" [COMM]; dat past bij een gefilterde heading, maar bewijst het niet.
- **Rijden zonder rotatie door vering**: gebruik de richting tussen de gemiddelde voor- en achterwielposities i.p.v. de chassis-rotatie, zolang alle wielen op de grond staan. [MOD: `getRotationFromAverageWheelPos`] Voorkomt dat de camera op en neer wiebelt op hobbels.
- **In de lucht / op z'n kop**: richting bevriezen op de huidige camerarichting en met een "isInAir"-gewicht (lerp 12/s) uit het velocity-model weghalen. [MOD]
- **Achteruit rijden**: de camera blijft achter de auto (draait niet 180°); de speler gebruikt "achterom kijken". [KENN + COMM]
- **Stilstaand / draaien op de plek**: de camera mag niet meezwaaien met kleine bewegingen. Fade de auto-heading-filter in met snelheid. [COMM: vergelijkbare trailing-camera mods doen dit expliciet; VOORSTEL]

### 2.3 Afstand, FOV en gevoel van snelheid [MOD]
- Afstand +1,0…1,9 m bij hoge snelheid (`maxHighSpeed 80 m/s`, ease-out kubisch).
- Extra afstand door **acceleratie** (multiplier 2,38, ease-out kwadratisch): camera zakt weg bij optrekken, komt terug bij remmen.
- Dynamische FOV: **75° → 80°** van stilstaand tot topsnelheid, basis-FOV ~77,5°.
- Met een uitgebreide camera hoort er ook pitch bij remmen/optrekken (camera kijkt iets omlaag bij hard remmen) [MOD optie; alleen cosmetisch].

### 2.4 Handmatige invoer (rechter stick, muis, knoppen)
- Rechter stick horizontaal/vertikaal → `manualYaw/manualPitch`. **Rate-gebaseerd** (hoek per seconde ∝ stick), niet "stick = absolute hoek".
- Pitch-limiet in de mod: **−62° … +40°** (naar beneden kijken gaat verder dan omhoog). [MOD]
- **Look behind** (hold): doel-yaw = 180° t.o.v. de auto-richting; loslaten → terugkeer. [MOD + KENN]
- **Look left / right** (hold, in de mod B/N): doel-yaw ±90°, met easing in en uit. [MOD ini `lookLeftAngle = 90`]
- Op stick-klik (R3) op controller = achterom kijken (PS3: "Right Stick Button – Look behind") [COMM].
- Terugkeer-vertraging: 
  - Mod: `timerResetLook = 2.0 s` na laatste input. [MOD]
  - Stock GTA V: spelers melden dat de camera na **ongeveer 1 s** zonder muisbeweging terugvalt; er is geen gewone instelling om het uit te zetten, maar mods bestaan ("no auto rotation camera") [COMM]. Pak als startwaarde **1,0–1,5 s** en maak het instelbaar.
- Stock gedrag bij muis: terugkeer is gekoppeld aan **input-rust**, niet aan een vaste timer: zodra de muis stilstaat keert hij terug [COMM].

---

## 3. Terugkeer vloeiend maken (het deel dat jij als eerste noemt)

Wat je als speler ziet in GTA: stick loslaten → even niets → camera begint langzaam, versnelt, remt af en komt zonder overshoot achter de auto tot rust.

Zo bouw je dat [VOORSTEL, consistent met het mod-gedrag en met standaard camera-ontwerp]:

```
// per frame, offsets in radialen/graden, dt in seconden
if (hasCameraInput) {                       // stick/muis/knop actief
    idleTimer = 0
    manualYaw   += input.x * yawRate   * dt // stick; muis: delta * gevoeligheid
    manualPitch  = clamp(manualPitch + input.y * pitchRate * dt, pitchMin, pitchMax)
} else {
    idleTimer += dt
}

// terugkeer-gewicht: 0 tijdens de wachttijd, daarna zacht oplopend naar 1
float w = smoothstep(returnDelay, returnDelay + returnRamp, idleTimer)   // bv. 1.0 s, ramp 0.4 s
float k = 1 - exp(-dt * w / returnTau)                                   // returnTau ≈ 0.35–0.5 s
manualYaw   *= (1 - k)
manualPitch *= (1 - k)
if (|manualYaw| < 0.05°) manualYaw = 0                                    // anders blijft het eeuwig sluipen
```

Waarom dit vloeiend voelt:
- `smoothstep`-gewicht = zachte start (geen schok bij het afgaan van de timer).
- `exp(-dt/τ)` = frame-rate onafhankelijk, remt vanzelf af naar 0, geen overshoot.
- Offset is relatief, dus ook tijdens het terugdraaien blijft de auto-camera de auto volgen.
- Zonder `returnDelay` voelt het nerveus; zonder `ramp` voelt het als een tik; zonder snap-to-zero (`<0,05°`) lijkt de camera nooit helemaal te stoppen.

Aanvullende regels:
- **Alleen terugkeren als de auto rijdt** (snelheid > ~1–2 m/s of speler geeft gas); bij stilstaan mag de camera blijven waar de speler hem zette. [VOORSTEL; trailing-camera mods voor andere spellen doen dit ook]
- **Terwijl er een knop-modus actief is** (look behind/left/right): gewicht = 0, offset volgt het doel met een korte ease (±0,1–0,15 s).
- **Tijdens het schakelen** (instappen, cutscene-uitgang): initialiseer de camera met de huidige gameplay-richting zodat er geen sprong is. [MOD: `lookQuat = getGameplayCameraDirection()` bij setup]
- Optie voor de speler: terugkeer **aan / uit / vertraagd** (Forza-achtig). GTA IV had volgens een gids een "Auto-Center Camera"-optie [COMM, zwakke bron]; GTA V-spelers vragen vaak om uitzetten.

---

## 4. Rechter stick, linker stick: gewenste invoer-verwerking

### 4.1 Controller [MOD + KENN]
- **Radiale deadzone** (niet per as): `if |v| < dz → 0 else v = normalize(v) * (|v|-dz)/(1-dz)`. Rockstar heeft in GTA V zelf een "deadzone"-slider voor richten toegevoegd [COMM], dus de stock-pijplijn heeft er al één; stack niet dubbel.
- **Responscurve** op de camera: milde ease-in (de mod gebruikt sinus-ease op |stick|) zodat kleine uitslagen nauwkeurig zijn en vol uitslag snel draait. [MOD `easeInSineInput`]
- Camera-rate = curve(stick) × gevoeligheid × dt. Aparte gevoeligheid voor richten (lager) en rondkijken in de auto.
- Verticale invoer: omkeerbaar (invert look).
- **Linker stick**:
  - *Te voet*: camera-relatieve beweging (zie §5). Stick-magnitude → loop/jog/sprint-drempels. [KENN]
  - *In de auto*: horizontaal = sturen (analoog, volledige schaal), triggers = gas/rem/achteruit, face-knop = handrem. In de lucht stuurt de stick de voertuigrotatie. [KENN]
  - **Speed-sensitive steering lock**: max stuuruitslag neemt af met snelheid, anders is hoge snelheid onbestuurbaar. [KENN/VOORSTEL: `lock = lerp(1.0, 0.35, saturate(speed/ vTop))`]

### 4.2 Toetsenbord + muis
Muis [COMM + MOD]:
- Muis = absolute delta per frame × gevoeligheid, **geen** snelheidsintegratie (geen dt-vermenigvuldiging op de delta; wel op de terugkeer).
- Zit je in de auto, dan kijkt de muis rond; GTA V heeft een aparte optie "mouse steering" waarbij de muis ook stuurt, dat verstoort het camerabeeld (spelers melden crashen daardoor) [COMM].
- Stock terugkeer: muis stil → camera keert na ~1 s terug [COMM]. Mod-gebruikers vragen expliciet om een instelbare vertraging [COMM] → bied het aan.
- Dezelfde `idleTimer`-logica als stick; input-bron mag per frame wisselen (controller ↔ muis detecteren, zoals de mod doet).

Toetsen (alleen gedrag, exacte default-toetsen nog te verifiëren, §10):
- WASD: lopen / gas-rem-achteruit en sturen (A/D digitaal).
- Aparte toetsen voor achterom kijken, links/rechts kijken, camerastand wisselen, cinematic camera. [KENN]
- **Digitaal sturen moet analoog gemaakt worden** [KENN/VOORSTEL]. Stuurwaarde `steer` eased naar doel (−1/0/+1):
  ```
  rate = (target == 0) ? returnRate : (sign(target) != sign(steer) ? crossRate : pressRate)
  steer = moveToward(steer, target * steerLock(speed), rate * dt)
  // pressRate ≈ 3–4 /s, returnRate ≈ 6–8 /s, crossRate ≈ 10 /s
  ```
  Er bestaan GTA V-mods precies hiervoor ("Smooth Driving V"), wat bevestigt dat stock A/D te schokkerig is [COMM].

---

## 5. Te voet (on-foot camera)

[KENN voor de basis, COMM voor het auto-centreren]

- Camera-state: yaw/pitch om de speler, afstand ~2,5–3 m, schouder-offset (rechts-boven). Rechter stick/muis draait yaw/pitch; pitch beperkt (ca. −60…+50).
- **Beweging is camera-relatief**: `moveDir = rotateY(leftStick.xy, cameraYaw)`. Stick omhoog = van de camera af. Hierom voelt "de camera bepaalt waar naartoe" natuurlijk.
- **Auto-centreren te voet**: loopt de speler en geeft hij geen camera-input, dan zwenkt de camera na een korte tijd (~1 s [COMM]) naar de bewegingsrichting. Dit is hetzelfde `manualOffset`-model: auto-richting = looprichting van de speler, offset vervalt naar 0.
  - *Alleen als de speler beweegt* en niet richt; staat hij stil dan blijft de camera.
  - *Snelheid van zwenk ∝ zijdelingse stick-uitslag*: wie alleen links/rechts loopt ziet de camera meedraaien; wie recht van de camera af loopt hoeft niets te corrigeren. [VOORSTEL]
- **Richtmodus** (aim): camera schakelt naar schouder-camera, FOV smaller, gevoeligheid lager (aparte multipliers), auto-centreren uit, bij loslaten glijdt de camera terug (zelfde `smoothstep + exp`). De mod laat zien dat aiming de pitch/FOV-blend via een `smoothIsAiming`-gewicht doet. [MOD]
- **Sprinten**: iets grotere afstand + licht FOV-effect. [KENN]
- **Collision** met wereld: zelfde raycast als in de auto.
- Bij in-/uitstappen: crossfade tussen on-foot- en vehicle-camera (en de offset reset naar 0); initialiseer op de huidige richting.

---

## 6. Referentie-ontwerp (engine-onafhankelijk)

### 6.1 Parameters (startwaarden)
| Parameter | Start | Bron |
|---|---|---|
| returnDelay | 1,0–1,5 s (instelbaar 0–3) | COMM; mod 2,0 |
| returnRamp | 0,4 s | VOORSTEL |
| returnTau | 0,35–0,5 s | VOORSTEL |
| pitch min / max | −62° / +40° | MOD |
| basis cameraAngle (neer) | 3,5° | MOD |
| heading-filter (velocity) | 7,5 /s lineair, bij v>2 m/s | MOD |
| rotatiefilter auto | 3·(1,39 + 0,017·v) /s | MOD |
| FOV 75 → 80° over topsnelheid | | MOD |
| afstand +1…1,9 m bij topsnelheid; +accel-term | | MOD |
| look left/right/back | 90° / 90° / 180° | MOD |
| stick deadzone (radiaal) | 0,10–0,15 | KENN |
| stick-curve | sinus ease-in op |stick| | MOD |
| muis / stick gevoeligheid | instelbaar | MOD |
| min. snelheid voor terugkeer | 1–2 m/s | VOORSTEL |

### 6.2 Volgorde per frame (voertuig)
1. Input verzamelen en normaliseren (stick-vorm, muisdelta); bron bijhouden.
2. Voertuig-state: positie, snelheid, forward, in-de-lucht, achteruit.
3. Auto-heading bepalen (velocity/forward-mix, wielpositie-richting, filter).
4. Manual-offset updaten of laten vervallen (§3); knop-modi (achteromkijken e.d.) als doel-offset.
5. Pitch: basis + manual + (optioneel) accel/rem.
6. Afstand + FOV uit snelheid/accel, met ease.
7. Cameraplek berekenen, raycast naar pivot, uitgesmoothde positie.
8. Kijkpunt = pivot + voorwaartse lead; camera-orientatie.
9. Debug-hook: print `autoYaw, manualYaw, idleTimer, w` zodat je het gedrag zonder screenshots kunt beoordelen.

### 6.3 Valkuilen
- **Hoeken wikkelen**: altijd korte pad (`delta = wrap(a-b, ±π)`) en offset opslaan in (−π, π].
- **Geen `lerp(a,b,0.1)` per frame**: gebruik `1 - exp(-dt/τ)`; anders verandert het gedrag met de framerate.
- **Auto-filter niet op stuurinput baseren** maar op de echte beweging; zo zwaait de camera niet als je stilstaand stuurt. [COMM]
- **Terugkeer niet op snelheid 0** laten doorlopen als de speler de camera nét bewust heeft gezet.
- **Muis-delta niet vermenigvuldigen met dt** (anders hangt gevoeligheid van framerate af); stick wel.
- **Terugkeer en handmatige input mogen niet tegelijk draaien**: bij input is `w = 0`.
- **Initialisatie** bij start/omschakeling om sprongen te vermijden.

---

## 7. Hoe het in dit spel (REDRIVER2) nu werkt

`src_rebuild/Game/C/camera.c` (gelezen):

- `PlaceCameraFollowCar` ([camera.c:421](../src_rebuild/Game/C/camera.c:421)): `lp->cameraAngle += angleDelta >> 3` per tick richting `baseDir + gCameraAngle` → een enkelvoudige exponentiële volger, ≈ 1/8 per tick (bij 30 Hz τ ≈ 0,27 s). `baseDir` is de autoheading.
- `TurnHead` ([camera.c:363](../src_rebuild/Game/C/camera.c:363)): look left/right/back zijn **digitale knoppen** (`CAMERA_PAD_LOOK_LEFT/RIGHT/BACK`); `headPos += (headTarget − headPos) / 2` per tick (zeer snel) en achterom kijken zet de hoek direct (`camAngle = baseDir`).
- Er is **geen analoge rechter stick** voor de camera, **geen pitch-input**, **geen wachttijd** en geen snelheids-/velocity-mix in de auto-heading.
- Hoekeenheid: 4096 = 360° (RSIN/RCOS tabellen), vaste-komma; tijd is in ticks, niet in seconden.

Aanbevolen aanpak als we dit hier bouwen [VOORSTEL]:
1. `lp->manualYaw` (4096-eenheden) en `lp->manualPitch` + `idleTimer` (ticks) naast `lp->cameraAngle`; `camAngle = lp->cameraAngle − headPos + manualYaw`.
2. Rechter stick uit de `Pads[]` lezen (de `diranalog`/`mapanalog` indices in `pad.c` eerst verifiëren), radiale deadzone + sinus-curve, rate in 4096-eenheden per tick.
3. Terugkeer in integer: `manualYaw -= (manualYaw * w_fixed) >> shift` met w opgebouwd uit `idleTimer`; snap naar 0 onder een drempel.
4. Look left/right/back als doel-offset i.p.v. directe hoek, zelfde easing.
5. Pitch: `camera_angle.vx` (nu 25 + wereldhoogte) krijgt `manualPitch` erbij, met clamp.
6. Eventueel de velocity-mix voor `baseDir` (nu puur autoheading) — dat is de grootste winst voor drift-gevoel en waarschijnlijk de meeste vraag om tuning.

Dit was het voorstel; het is uitgewerkt en gebouwd volgens `plans/camera-orbit.md` (stand 2026-10-12: stick yaw + pitch, terugkeer, achterom als offset, menu, `config.ini`; cabine, drift-mix, muis en te voet nog niet).

---

## 8. GTA IV vs GTA V (voor zover bekend)

- IV: rechter stick = rondkijken, R3 = achterom, camerahoogte/afstand instelbaar in opties, "Auto-Center Camera" optie volgens een (zwakke) gids [COMM]. Op PC met toetsenbord/muis blijft de camera in bochten recht en spelers snappen hem terug met een toets [COMM].
- V: meer filtering van de heading, geavanceerdere cinematic camera, aparte camera-metadata per camera-type (follow-ped, follow-vehicle, aim) waar mods (Extended/Custom Camera Settings) velden zoals FOV, afstand, horizontale pivot, pivot-schaal en auto-centreren aanpassen [COMM]. De exacte veldnamen kon ik niet verifiëren.

---

## 9. Wat ik niet kon vinden

- De werkelijke Rockstar-constanten (vertraging, snelheid van terugkeer, springparameters).
- Een officiële spec of GDC-talk over de GTA follow-camera.
- Exacte standaard-toetsen voor look-behind / links / rechts / cinematic op PC.
- Of er in GTA V (Legacy of Enhanced) een in-game schakelaar voor het automatisch centreren in voertuigen bestaat (bronnen spreken elkaar tegen).

## 10. Nog te verifiëren (zodra we dit gaan bouwen)

1. Ons gewenste gedrag in de praktijk naast een opname van GTA V leggen (video-analyse: terugkeer-tijd na stick loslaten meten, met één voertuig en vaste snelheid).
2. Rechter-stick-indices in `Pads[]` en bestaande camera-input in `pad.c` / `paddCamera`.
3. Toetsenlay-out voor onze keyboard-optie (muis-look in de auto, look-back toets).
4. Of we de velocity-mix in `baseDir` ook voor Driver 1-steden gebruiken (zelfde camera-code).

## Bronnen

- [Custom Camera V (gta5-mods)](https://www.gta5-mods.com/scripts/custom-camera-v), [broncode Rbn3D/CustomCameraV](https://github.com/Rbn3D/CustomCameraV), [Custom Camera V Plus](https://github.com/Rbn3D/CustomCameraVPlus) — `SmoothCamera.cs`, `script.cpp` (`updateCamThirdPerson3P`, `readLookAroundInput`) en `CustomCameraVPlus.ini`.
- [No auto rotation camera on foot and cars (gta5-mods)](https://www.gta5-mods.com/scripts/no-auto-rotation-camera-on-foot-no-autocentering) — beschrijft het stock auto-centreren (korte vertraging, te voet en in voertuigen).
- [Steam-discussies GTA V](https://steamcommunity.com/app/271590/discussions/0/2253434652392696589) en [Steam GTA V mouse steering](https://steamcommunity.com/app/271590/discussions/0/611704531886710758) — gedrag van muis en camera.
- [Smooth Driving V (gta5-mods)](https://www.gta5-mods.com/ko/scripts/smooth-driving-v-2024-update) — bewijs dat stock keyboard-sturen wordt gladgestreken.
- [Ludo-gids GTA IV opties](https://www.ludo.guide/guide/grand-theft-auto-iv/getting-started/game-settings-options) — zwakke bron (lijkt gegenereerd), alleen gebruikt voor "Auto-Center Camera".
- [SuperCheats GTA IV controls PS3](https://www.supercheats.com/guides/grand-theft-auto-4/controls-playstation-3) — R3 = achterom kijken.
- Eigen code: `src_rebuild/Game/C/camera.c`.
