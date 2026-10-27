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
  Sichtbarkeit. Schwere See (ab Stärke 5) kostet kurze Schiffe Fahrt – ein Zerstörer bei
  Stärke 6 läuft gut ein Fünftel langsamer, ein Schlachtschiff nicht –, streut die Salven und
  verlangsamt das Laden.
- **Sensoren**: Ausguck und Optik (Silhouette, Aspekt, Licht, Gegenlicht, Inselhintergrund,
  Bugwelle, Brände, Scheinwerfer), Radar je Gerät mit Reichweiten gegen große und kleine Ziele,
  Peil- und Entfernungsgenauigkeit, Landechos, Funkschatten von Inseln, Ausfällen; Mündungsfeuer
  über große Entfernung; Torpedolaufbahnen nach Kielwassersichtbarkeit des Torpedos.
- **Lagebild** (`ContactPicture`) je Seite: Spuren mit Positions-/Geschwindigkeitsunsicherheit,
  Klassifizierungsstufe (entdeckt → Größe → Typ → Klasse → identifiziert), Haltern und
  Meldeverzug. Neue Spuren und Erstidentifizierungen werden als Ereignisse gemeldet. Ein Verband
  mit `known_to_enemy` im Szenario (am Nordkap die von Ultra entzifferte Scharnhorst) wird beim
  ersten Kontakt sofort identifiziert – die Entdeckung selbst bleibt Sache der Sensoren.
- **Feuerleitung**: Jeder Richtungsgeber verfolgt sein Ziel selbst (Messungen per Radar oder
  Optik, Alpha-Beta-Filter), rechnet Vorhalt und Flugzeit, schießt Salven, beobachtet die
  Aufschläge und korrigiert (Gabelbildung, Deckung). Streuung hängt von Geschütz, Besatzung,
  Schäden, Bewegung des eigenen Schiffs und Licht ab. Blindes Radarschießen (Ziel nur auf dem
  Schirm, nicht im Glas) streut mit wachsender Entfernung stärker, und die Aufschläge sind auf dem
  Radarschirm seltener und ungenauer zu beobachten – so trafen 1942 Radarsalven auf 15 km bei
  Nacht kaum, auf 8 km mit Sicht dagegen verheerend.
- **Ballistik**: numerisch integrierte Flugbahnen mit Luftwiderstand (G7-Näherung) und
  Luftdichte nach Höhe; Durchschlag gegen Gürtel-, Deck- und Turmpanzer abhängig von
  Auftreffwinkel und Geschwindigkeit; Wirkung nach Granatart und Sprengladung.
- **Schäden**: Strukturfestigkeit, Wassereinbruch und Leckwehr, Krängung und Trimm, Brände mit
  Ausbreitung in Nachbarabschnitte (größere Schiffe haben mehr Brandabwehrtrupps), Munitionskammern (Explosion oder Fluten), Ausfall von Antrieb, Ruder, Brücke,
  Stromversorgung, Scheinwerfern, Radar; Sinken, Auseinanderbrechen, Kentern. Großschiffe mit
  vier Wellen und getrennten Maschinenräumen verlieren durch einen Treffer nur einen Teil ihrer
  Antriebsleistung; Granaten, die über dem Gürtel die dünne Bordwand durchschlagen, erreichen
  die Munitionskammern eines gepanzerten Schiffs nicht (das Panzerdeck liegt dazwischen), und
  sie treffen auch Maschinen, Kessel und Ruder darunter nicht, sondern nur Aufbauten, Flak und
  Besatzung. Die Strukturfestigkeit je Klasse (`hull_strength`) bestimmt, wie viel Sprengwirkung
  der Rumpf aufnimmt; nicht durchschlagene schwere Panzerung lässt fast nichts durch.
- **Torpedos**: Laufstufen (Geschwindigkeit/Reichweite), Tiefenlauf, Versager, Fächer;
  Ausweichen durch Kämmen der Laufbahnen. Lufttorpedos tauchen zu schnell oder zu hoch
  abgeworfen tief weg, laufen unregelmäßig oder brechen auseinander.
- **Großkampfschiffe unter Torpedotreffern**: Über 20.000 t wächst der Wassereinbruch je Treffer
  mit der Größe der Abteilungen und streut je nach Trefferort; die zum Meer offenen Räume
  (`holed`) können die Pumpen nicht lenzen. Gegenfluten richtet das Schiff auf, kostet aber
  Auftrieb; liegt es tief (über ein Drittel der Reserveverdrängung), dringt das Wasser über
  Schotte, Lüfter und Wellentunnel weiter vor – ohne Strom verlieren die Leckwehrtrupps diesen
  Kampf. Kalibriert an Force Z: Repulse sinkt nach drei Treffern in etwa der Hälfte der Fälle,
  nach vier sicher; Prince of Wales (King George V) nach sechs meist, nach fünf selten.
- **Luftstreitkräfte** (`Air.cpp`): Ein *Verband* (`Flight`) ist die befehligte Einheit, er
  besteht aus einzelnen *Flugzeugen* (`Aircraft`), die in Formation fliegen, ihre Angriffe selbst
  fliegen, einzeln getroffen und einzeln gezeichnet werden. Verbände starten von *Basen*
  (Flugplätze, auch weit außerhalb der Karte, und Katapultschiffe) und folgen einer
  Zustandsmaschine (startbereit → Start → Anflug → Suche/Station/Abfangen/Angriff → Sammeln →
  Rückflug → Aufmunitionieren). Befehle: Suchflug (bewaffnet: greift an, was er findet), Angriff
  auf einen Kontakt, Jagdschutz über Punkt oder Verband, Begleitschutz, Leuchtbomben,
  Fühlunghalten mit Schussbeobachtung, Rückflug; Startzeit und Flughöhe sind wählbar. Angriffsart
  nach Zuladung: Torpedo im Zangenangriff mit Prüfung des Anlaufwinkels, Horizontalwurf mit
  Bombenzielgerät (Fallzeit und Wurfweite), Sturzangriff; Jäger greifen in Anflügen mit
  Feuerstößen und Abschwung an, Bomber schießen gemeinsam zurück. Treibstoff begrenzt die
  Einsatzzeit; Verbände melden Sichtungen, Angriffe, Treibstoffmangel und Verluste.
- **Flugabwehr** (`AntiAir.cpp`): leichte Flak aus den Modellteilen (`aa.json`: Kadenz,
  Reichweite, Schuss je Abschuss) und Mehrzweckgeschütze mit Flakfeuerleitung; Trefferchance je
  Schuss aus Zielgeschwindigkeit, Kurs, Entfernung, Höhe, Licht, eigener Bewegung und
  nationaler Feuerleitung, Poisson-verteilt je Feuerstoß. Mehrzweckgeschütze bleiben bei
  Seezielen und schießen nur auf angreifende Flugzeuge; nachts halten sie gegen Fühlunghalter
  und Leuchtbombenträger das Feuer (das Mündungsfeuer verriete das Schiff).
- **Luftlage**: Luftsuchradar (Reichweite nach Radarhorizont und Größe des Verbands) und
  Ausguck (Licht, Wolkenuntergrenze) melden Luftkontakte (`AirTrack`: Anzahl, Rolle, Höhe,
  greift an); Flugzeuge entdecken Schiffe (Sicht, ASV-Radar). Ein Luftkontakt alarmiert die
  Schiffe in der Nähe.

## Gegner-KI (`src/ai`)

Drei Ebenen, alle mit demselben Prinzip: Optionen werden mit **Utility-Bewertung** (Produkt
gewichteter Einzelüberlegungen über Antwortkurven, `Utility.h`) verglichen; die Wahl samt aller
Alternativen und Überlegungen landet im **Entscheidungsprotokoll** und ist im KI-Einblick sichtbar.

1. **Befehlshaber** je Verband (alle 12–16 s): Lagebeurteilung (`Assessment`) aus dem eigenen
   Lagebild – Kräfteverhältnis, Nähe, Bedrohung, eigene Schäden, Munition und Torpedos, Licht,
   Morgendämmerung, ob man entdeckt ist. Haltungen: Auftrag, Aufklären, Gefecht, Torpedoangriff,
   Beschatten, Rückzug, Sammeln. Ein Gegner in Waffenreichweite drängt den bloßen Auftrag
   zurück; ein Vorposten, der Treffer erhält, gilt als entdeckt und kehrt nicht auf seine Station
   zurück. Befehle wirken erst nach einem doktrinabhängigen Befehlsverzug.
2. **Verband**: setzt die Haltung um – Auftrag (Wegpunkte, Patrouille, Geleit mit Station voraus
   oder achteraus, Angriff auf ein Zielgebiet), Gefechtskurs mit Breitseite und Kreuzen des T,
   Torpedoangriff in Phasen (Anlauf → Abschussposition → Abdrehen → Laufzeit abwarten →
   Nachladen und erneut angreifen), Rückzug über eine Abmarschroute – führt sie auf einen
   Gegner zu, dreht der Verband ab, bis die Gefahr (Summe aller Bedrohungen nach Kampfkraft und
   Entfernung) achterlicher als querab steht, und hält die einmal gewählte Seite. Zielverteilung auf die
   Schiffe, Feuerdisziplin (Geschütze dunkel lassen, solange die Torpedos laufen).
3. **Kommandant** je Schiff: Station halten, Land und eigenen Schiffen ausweichen, Torpedo-
   laufbahnen kämmen, beschädigt abdrehen, Richtungsgeber-Ziele, Scheinwerfer und Leucht-
   granaten, Torpedofächer (mit Prüfung, dass der Fächer über die ganze Laufstrecke kein eigenes
   Schiff gefährdet, und Wahl der Laufstufe nach dem Treffpunkt – Ziele außer Reichweite des
   eigenen Torpedos, etwa des kurzen Type 90, werden gar nicht erst angegriffen). Aus größerer
   Entfernung schießt ein Verband nur, wenn er selbst Treffer erhält, nicht schon, weil der
   Gegner irgendwo feuert.

**Luftkrieg der KI**: Der Luftbefehlshaber verteilt bewaffnete Verbände auf die wertvollsten
bekannten Schiffskontakte in Reichweite (Großkampfschiffe zuerst, Angriffe gestreut), setzt
suchende Verbände auf gemeldete Ziele an, lässt Aufklärer Fühlung halten und schickt Jäger als
Jagdschutz über bedrohte Verbände – unter Funkstille (Doktrin `air_cover_delay_s`, Force Z) erst
nach dem ersten Angriff. Verbände unter Luftbedrohung laufen äußerste Kraft (nicht im
Seegefecht), Kommandanten drehen gegen anlaufende Torpedoflugzeuge.

**Fairness**: Die KI liest ausschließlich ihr Lagebild (`SideView`), nie die Wahrheit. Das
Schwierigkeitsniveau ändert Reaktionszeit und Taktikqualität, nie die Informationslage.
**Doktrinen** (`data/doctrine/*.json`) legen Aggressivität, Vorsicht, Nachtvorliebe, bevorzugte
Gefechts- und Torpedoentfernungen, Identifizierungspflicht, Beleuchtungsart (Leuchtgranaten,
Scheinwerfer, Bordflugzeuge), Feuerdisziplin, Rückzug vor Tagesanbruch und Führungsverzug fest.

## Spielsitzung (`src/game`)

`Session` verbindet Szenario, Welt und beide KIs: Verbände erscheinen zeitgesteuert, Spieler-
befehle (Kurs, Fahrt, Formation, Wegpunkt-Routen für Verband oder Einzelschiff, Ziel,
Feuererlaubnis, Torpedos, Scheinwerfer, Leuchtgranaten, Bordflugzeuge, Befehle an Luftverbände)
werden geprüft und als Quittung (`OrderAcknowledged`) gemeldet. Auf der Spielerseite laufen nur
Kommandanten-Automatismen (Ausweichen, Stationshalten) – es sei denn, der Spieler übergibt einen
Verband an die KI-Führung (`delegateGroup`) oder einen Luftverband an den KI-Luftbefehlshaber
(`commandFlight`); er kann beides jederzeit zurücknehmen. Routen steuert die Sitzung: Der
Leitschiffkurs zeigt auf den nächsten Wegpunkt, erreichte Punkte fallen weg, am Ende kommt eine
Meldung. Das Ende ergibt
sich aus Zeitlimit, Vernichtung oder Lösen vom Gegner; die Wertung aus versenkter und verlorener
Tonnage plus Szenarioziele (Tonnage, Verband vernichten oder schützen, Verband erhalten,
Gebiet erreichen, überleben).

## Daten (`data/`)

| Pfad | Inhalt |
|---|---|
| `db/ships/*.json` | Schiffsklassen: Abmessungen, Verdrängung, Antrieb, Panzerung, Geschütz- und Torpedoaufstellungen mit Bestreichungswinkeln, Richtungsgeber, Sensoren, Scheinwerfer, Bordflugzeuge, Modellparameter, Schiffsnamen (Klassengeschichte in den Sprachdateien) |
| `db/guns.json`, `torpedoes.json`, `sensors.json`, `nations.json` | Geschütze (Ballistik, Granaten, Kadenz, Flakwerte der Mehrzweckgeschütze), Torpedos (Laufstufen, Kielwasser, Versager, Abwurfgrenzen der Lufttorpedos), Sensoren (auch Luftsuchreichweite), Nationen |
| `db/aircraft.json`, `bombs.json`, `aa.json` | Flugzeugtypen (Leistung, Robustheit, Bewaffnung, Zuladungen, Modellparameter), Bomben, leichte Flak |
| `doctrine/*.json` | KI-Doktrinen |
| `scenarios/*.json` | Missionen: Zeit, Ort, Wetter, Verbände mit Auftrag, Doktrin, Besatzungsgüte, Bereitschaft, Ziele; je Schiff Sensor-Umrüstungen und Vorschäden (Antrieb, Strom, Wassereinbruch – z. B. die gestoppte Pola bei Matapan); Flugplätze und Luftverbände (Basis, Typ, Zuladung, Anzahl, Startzeit oder schon in der Luft, Befehl) |
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
   bewegung, Sinken), Flugzeuge (Luftschrauben, Zuladung, Querlage, Rauchfahne, Absturz,
   Wrack), Partikel (Rauch, Feuer, Wassersäulen, Glut, Funken, Lichtkegel, Leuchtspur,
   Flakwolken, Regen und Schnee um die Kamera, Regenvorhänge unter den Böen)
4. Tiefenauflösung, Augenadaption (Luminanz-Compute), Bloom, ACES-Tonemapping, Filmkorn

Schiffs- und Flugzeugmodelle (`AircraftModel.cpp`: Ein- und Doppeldecker, Schwimmer, Flugboote,
ein bis vier Motoren, Tarnanstrich und Hoheitszeichen je Nation) entstehen prozedural aus den
Daten (`src/procgen`); glTF-Modelle können Schiffe je Klasse ersetzen (siehe [CREDITS.md](../CREDITS.md)). Der Generator ist in Module geteilt:

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
Hintergrundszene, Missionen, Briefing, Gefecht (3D, Plot, geteilt; Gliederung mit Luftverbänden,
Schiffspanel mit Ruder/Wegpunkten/Waffen/Schäden/Sensoren oder Befehlspanel des gewählten
Luftverbands (`UiAir.cpp`), Kontaktliste mit Luftkontakten, anklickbare Meldungen, Alarmbanner,
Minikarte, KI-Einblick), Nachbesprechung mit Gefechtsplot, Einheitenhandbuch (Schiffe und
Flugzeuge), Szenario-Editor, Einstellungen, Credits. Der Plot zeigt eigene Luftverbände mit ihren
Befehlslinien, das Luftlagebild der Seite und die Flugplätze; Befehle an Schiffe und Luftverbände
gibt es per Rechtsklick, Punktbefehle (Suchgebiet, Jagdschutzraum, Route) per Klick. Alle Texte laufen
über Lokalisierungsschlüssel; ein Test prüft, dass jeder im Code benutzte Schlüssel in beiden
Sprachen existiert.

## Bekannte Grenzen

- Schauplätze bisher: Pazifik 1942 (Ironbottom Sound), Südchinesisches Meer 1941 (Force Z),
  Mittelmeer 1941 (Matapan) und Nordmeer 1943 (Nordkap). Atlantik, Kanal und Biskaya sind
  vorgesehen.
- Keine U-Boote, Flugzeugträger und Landziele im Spiel; Flugzeuge starten von Flugplätzen und
  Katapulten.
- KI-gegen-KI-Gefechte verlaufen oft verlustreicher als die historischen Vorbilder; die
  Abstimmung von Nachtgefechtstreffsicherheit und Rückzugsentscheidungen ist laufende Arbeit
  (Werkzeug: `irontide_headless --events`).
