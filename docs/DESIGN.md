# Architektur

Überblick über den Aufbau von Iron Tide. Die Begründungen der Grundsatzentscheidungen stehen in
den ADRs unter [`docs/adr/`](adr/):

| ADR | Entscheidung |
|---|---|
| [0001](adr/0001-eigene-cpp-d3d12-engine.md) | Eigene C++20-Engine auf Direct3D 12 |
| [0002](adr/0002-portabler-kern-windows-shell.md) | Portabler Simulationskern, Windows-Schale |
| [0003](adr/0003-shader-sm51-laufzeitkompilierung.md) | HLSL Shader Model 5.1, Laufzeit-Kompilierung mit Cache |
| [0004](adr/0004-faire-hierarchische-ki.md) | Faire, hierarchische Gegner-KI mit Doktrin und Utility-Bewertung |
| [0005](adr/0005-assets-freie-lizenzen.md) | Assets nur unter freien Lizenzen, Rest prozedural |

## Schichten

```
              ┌──────────────── Windows-Schale (nur Win32) ────────────────┐
              │ platform/win32   render (D3D12)   audio (XAudio2)          │
              └───────▲──────────────▲─────────────────▲─────────────────┘
                      │ UiServices    │ Welt + Events    │ Events
              ┌───────┴──────────────┴─────────────────┴─────────────────┐
  portabel    │ ui (Dear ImGui: Menüs, Gefecht, Plot, Handbuch, Editor)    │
  (Linux-     │ game (Szenario, Sitzung, Ziele, Spielerbefehle)            │
  getestet)   │ ai (Befehlshaber / Verband / Kommandant)    procgen        │
              │ sim (Welt, Physik, Sensoren, Lagebilder, Schäden)          │
              │ core (Mathematik, JSON, Dateien, Lokalisierung, Log)       │
              └───────────────────────────────────────────────────────────┘
```

Alles unterhalb der Windows-Schale baut und testet unter Linux (GCC) wie unter Windows (MSVC,
MinGW). Die Oberfläche spricht Renderer, Audio und Fenster nur über die Schnittstelle
`ui::UiServices` an; in Tests ersetzt `NullServices` sie.

## Simulation (`src/sim`)

- **Fester Zeitschritt** `kStep = 0.05 s`, deterministisch bei gleichem Seed (eigener RNG in der
  Welt). Zeitraffer führt mehr Schritte pro Bild aus (`Session::update`).
- **`World`** besitzt Schiffe, Granaten, Salven, Torpedos, Leuchtmittel, Bordflugzeuge, Verbände,
  Gelände und Umwelt und erzeugt einen **Ereignisstrom** (`sim::Event`): Abschüsse, Einschläge,
  Treffer, Brände, Kontakte, Meldungen. Renderer, Audio, Meldungsliste, Nachbesprechung und KI
  lesen nur diesen Strom und den öffentlichen Zustand.
- **Umwelt**: Sonnen- und Mondstand aus Datum, Uhrzeit und Position (Mondphase), Bewölkung,
  Sichtweite, Seegang, Wind, ziehende Regenböen. Daraus folgt die Grundhelligkeit für die
  Sichtbarkeit.
- **Sensoren**: Ausguck und Optik (Silhouette, Aspekt, Licht, Gegenlicht, Inselhintergrund,
  Bugwelle, Brände, Scheinwerfer), Radar je Gerät mit Reichweiten gegen große und kleine Ziele,
  Peil- und Entfernungsgenauigkeit, Landechos, Funkschatten von Inseln, Ausfällen; Mündungsfeuer
  über große Entfernung; Torpedolaufbahnen nach Kielwassersichtbarkeit des Torpedos.
- **Lagebild** (`ContactPicture`) je Seite: Spuren mit Positions-/Geschwindigkeitsunsicherheit,
  Klassifizierungsstufe (entdeckt → Größe → Typ → Klasse → identifiziert), Haltern und
  Meldeverzug. Neue Spuren und Erstidentifizierungen werden als Ereignisse gemeldet.
- **Feuerleitung**: Jeder Richtungsgeber verfolgt sein Ziel selbst (Messungen per Radar oder
  Optik, Alpha-Beta-Filter), rechnet Vorhalt und Flugzeit, schießt Salven, beobachtet die
  Aufschläge und korrigiert (Gabelbildung, Deckung). Streuung hängt von Geschütz, Besatzung,
  Schäden, Bewegung des eigenen Schiffs und Licht ab.
- **Ballistik**: numerisch integrierte Flugbahnen mit Luftwiderstand (G7-Näherung) und
  Luftdichte nach Höhe; Durchschlag gegen Gürtel-, Deck- und Turmpanzer abhängig von
  Auftreffwinkel und Geschwindigkeit; Wirkung nach Granatart und Sprengladung.
- **Schäden**: Strukturfestigkeit, Wassereinbruch und Leckwehr, Krängung und Trimm, Brände mit
  Ausbreitung, Munitionskammern (Explosion oder Fluten), Ausfall von Antrieb, Ruder, Brücke,
  Stromversorgung, Scheinwerfern, Radar; Sinken, Auseinanderbrechen, Kentern.
- **Torpedos**: Laufstufen (Geschwindigkeit/Reichweite), Tiefenlauf, Versager, Fächer;
  Ausweichen durch Kämmen der Laufbahnen.

## Gegner-KI (`src/ai`)

Drei Ebenen, alle mit demselben Prinzip: Optionen werden mit **Utility-Bewertung** (Produkt
gewichteter Einzelüberlegungen über Antwortkurven, `Utility.h`) verglichen; die Wahl samt aller
Alternativen und Überlegungen landet im **Entscheidungsprotokoll** und ist im KI-Einblick sichtbar.

1. **Befehlshaber** je Verband (alle 12–16 s): Lagebeurteilung (`Assessment`) aus dem eigenen
   Lagebild – Kräfteverhältnis, Nähe, Bedrohung, eigene Schäden, Munition und Torpedos, Licht,
   Morgendämmerung, ob man entdeckt ist. Haltungen: Auftrag, Aufklären, Gefecht, Torpedoangriff,
   Beschatten, Rückzug, Sammeln. Befehle wirken erst nach einem doktrinabhängigen
   Befehlsverzug.
2. **Verband**: setzt die Haltung um – Auftrag (Wegpunkte, Patrouille, Geleit mit Station voraus
   oder achteraus, Angriff auf ein Zielgebiet), Gefechtskurs mit Breitseite und Kreuzen des T,
   Torpedoangriff in Phasen (Anlauf → Abschussposition → Abdrehen → Laufzeit abwarten →
   Nachladen und erneut angreifen), Rückzug über eine Abmarschroute. Zielverteilung auf die
   Schiffe, Feuerdisziplin (Geschütze dunkel lassen, solange die Torpedos laufen).
3. **Kommandant** je Schiff: Station halten, Land und eigenen Schiffen ausweichen, Torpedo-
   laufbahnen kämmen, beschädigt abdrehen, Richtungsgeber-Ziele, Scheinwerfer und Leucht-
   granaten, Torpedofächer (mit Prüfung, dass der Fächer über die ganze Laufstrecke kein eigenes
   Schiff gefährdet).

**Fairness**: Die KI liest ausschließlich ihr Lagebild (`SideView`), nie die Wahrheit. Das
Schwierigkeitsniveau ändert Reaktionszeit und Taktikqualität, nie die Informationslage.
**Doktrinen** (`data/doctrine/*.json`) legen Aggressivität, Vorsicht, Nachtvorliebe, bevorzugte
Gefechts- und Torpedoentfernungen, Identifizierungspflicht, Beleuchtungsart (Leuchtgranaten,
Scheinwerfer, Bordflugzeuge), Feuerdisziplin, Rückzug vor Tagesanbruch und Führungsverzug fest.

## Spielsitzung (`src/game`)

`Session` verbindet Szenario, Welt und beide KIs: Verbände erscheinen zeitgesteuert, Spieler-
befehle (Kurs, Fahrt, Formation, Ziel, Feuererlaubnis, Torpedos, Scheinwerfer, Leuchtgranaten,
Bordflugzeuge) werden geprüft und als Quittung (`OrderAcknowledged`) gemeldet. Auf der
Spielerseite laufen nur Kommandanten-Automatismen (Ausweichen, Stationshalten). Das Ende ergibt
sich aus Zeitlimit, Vernichtung oder Lösen vom Gegner; die Wertung aus versenkter und verlorener
Tonnage plus Szenarioziele (Tonnage, Verband vernichten oder schützen, Verband erhalten,
Gebiet erreichen, überleben).

## Daten (`data/`)

| Pfad | Inhalt |
|---|---|
| `db/ships/*.json` | Schiffsklassen: Abmessungen, Verdrängung, Antrieb, Panzerung, Geschütz- und Torpedoaufstellungen mit Bestreichungswinkeln, Richtungsgeber, Sensoren, Scheinwerfer, Bordflugzeuge, Modellparameter, Schiffsnamen (Klassengeschichte in den Sprachdateien) |
| `db/guns.json`, `torpedoes.json`, `sensors.json`, `nations.json` | Geschütze (Ballistik, Granaten, Kadenz), Torpedos (Laufstufen, Kielwasser, Versager), Sensoren, Nationen |
| `doctrine/*.json` | KI-Doktrinen |
| `scenarios/*.json` | Missionen: Zeit, Ort, Wetter, Verbände mit Auftrag, Doktrin, Besatzungsgüte, Bereitschaft, Ziele |
| `terrain/` | Höhenmodell (Int16, 100 m) mit Georeferenz |
| `shaders/` | HLSL (SM 5.1), zur Laufzeit kompiliert und zwischengespeichert |
| `loc/` | Sprachdateien, erzeugt aus `tools/loc/*.py` |
| `audio/` | `bank.json` und 16-Bit-PCM-WAV, erzeugt mit `tools/audio/build_bank.py` |

## Renderer (`src/render`)

Direct3D 12, Feature Level 12_0, eine Root Signature für alles (drei Konstantenpuffer, zwei
SRV-Tabellen, eine UAV-Tabelle, statische Sampler). Pro Bild:

1. Kielwasserkarte (Kielwasser, Torpedolaufbahnen, Schaumstempel) und Schattenkarte
2. FFT-Ozean (drei Kaskaden 600/120/24 m, Pierson-Moskowitz-Spektrum, Schaum aus der Jacobi-
   Determinante) per Compute-Shader
3. HDR-Szene mit MSAA: Himmel mit Sonne, Mond und Sternen, Gelände, Ozean (radiales Gitter bis
   60 km), Schiffe (artikulierte Türme, Rohre, Torpedosätze, Scheinwerfer, Radar; Seegangs-
   bewegung, Sinken), Partikel (Rauch, Feuer, Wassersäulen, Glut, Funken, Lichtkegel)
4. Tiefenauflösung, Augenadaption (Luminanz-Compute), Bloom, ACES-Tonemapping, Filmkorn

Schiffsmodelle entstehen prozedural aus den Klassendaten (`src/procgen`); glTF-Modelle können sie
je Klasse ersetzen (siehe [CREDITS.md](../CREDITS.md)). Der Generator ist in Module geteilt:

| Datei | Inhalt |
|---|---|
| `ShipModel.cpp` | Einstieg, Farbschemata, Rumpfform, Reihenfolge des Aufbaus, Belegung des Decks |
| `ShipHull.cpp` | Außenhaut mit Wasserpass, Gürtel/Wulst, Deck mit Wasserlauf, Schanzkleid, Wellenbrecher, Anker, Unterwasserschiff |
| `ShipStructures.cpp` | Deckshäuser, Brücken, Pagoden, Schornsteine, Masten, Leitstände, Radar, Scheinwerfer, Kräne, Takelage |
| `ShipWeapons.cpp` | Türme je Bauart, Lafetten, Einzelrohre, Torpedorohrsätze mit Torpedos, leichte Flak |
| `ShipFittings.cpp` | Relinge, Niedergänge, Flöße, Katapulte, Bordflugzeuge, Boote, Decksausrüstung, Wasserbomben, Flagge |
| `ShipAnimation.cpp` | Zeitverläufe: Rückstoß, Torpedoausstoß, Katapultstart, Waffenvorführung |

Jedes bewegliche Teil ist ein eigenes Modellteil mit Drehpunkt, Achse und Elternteil (Turm →
Wiege → Rohr; Torpedosatz → Torpedo; Katapult → Flugzeug; Flaklafette → Waffen). Der Renderer
setzt daraus je Bild die Pose zusammen (`RendererAnimation.cpp`): Seiten- und Höhenrichtung aus der
Simulation, Rückstoß aus den Schussereignissen, Torpedoausstoß aus der Beladung, Flak und
Katapultstart aus den Flugzeugeinsätzen. Teile tragen eine Detailstufe (0 Rumpf und Bewaffnung,
1 Ausrüstung und Flak, 2 Relinge, Takelage, Kleinteile), die nach der projizierten Größe eines
Meters auf dem Bildschirm gewählt wird.

## Oberfläche (`src/ui`)

Dear ImGui mit eigenem Thema (Barlow, IBM Plex Mono). Bildschirme: Hauptmenü mit laufender
Hintergrundszene, Missionen, Briefing, Gefecht (3D, Plot, geteilt; Gliederung, Schiffspanel mit
Ruder/Waffen/Schäden/Sensoren, Kontaktliste, Meldungen, Minikarte, KI-Einblick), Nachbesprechung
mit Gefechtsplot, Einheitenhandbuch, Szenario-Editor, Einstellungen, Credits. Alle Texte laufen
über Lokalisierungsschlüssel; ein Test prüft, dass jeder im Code benutzte Schlüssel in beiden
Sprachen existiert.

## Bekannte Grenzen

- Bisher nur der Pazifik 1942 (Ironbottom Sound). Atlantik, Mittelmeer und Kanal sind vorgesehen;
  die Datenbank kennt bereits Royal Navy, Kriegsmarine und Regia Marina als Nationen.
- Keine U-Boote, Flugzeugträger und Landziele im Spiel; Bordflugzeuge nur als Aufklärer und
  Leuchtbombenträger.
- KI-gegen-KI-Gefechte verlaufen oft verlustreicher als die historischen Vorbilder; die
  Abstimmung von Nachtgefechtstreffsicherheit und Rückzugsentscheidungen ist laufende Arbeit
  (Werkzeug: `irontide_headless --events`).
