# Teststrategie

Iron Tide wird auf vier Ebenen geprüft – von schnellen Einheitentests bis zu Bildschirmfotos des
echten Windows-Programms.

```
  Bildprüfung     Spiel unter Windows/Wine, automatisiert per Startparametern, Screenshots
  KI-Batches      ganze Missionen KI gegen KI, viele Seeds, Statistik und Ereignisprotokoll
  Integration     Gefechtssitzungen, headless gebaute Oberfläche, Lokalisierung
  Einheiten       Mathematik, Datenbank, Ballistik, Sensoren, Schäden, Torpedos, KI-Bausteine
```

## 1. Test-Suite (doctest)

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug
ctest --test-dir build/linux-debug --output-on-failure
# einzelne Fälle:
build/linux-debug/irontide_tests -tc="torpedo*"
```

| Datei | Prüft |
|---|---|
| `test_core.cpp` | Winkel und Peilungen, Abfanglösung, Matrizen, deterministischer Zufall, Lokalisierung mit Rückfallsprache |
| `test_database.cpp` | Datenbank lädt und validiert, Kalibrierung der Ballistik gegen historische Höchstschussweiten, Schusstafeln, Durchschlag über Entfernung und Winkel, Bestreichungswinkel, Type 93 übertrifft Mark 15 |
| `test_procgen.cpp` | Prozedurale Modelle sind gültig und treffen die Klassenmaße, Rumpfform |
| `test_sim.cpp` | Astronomie (Savo-Nacht war dunkel), Schiffsführung, Sichtentdeckung nach Licht und Optik, SG vs. SC und Funkschatten, Artillerieduell bei Tag, Type-93-Fächer treffen, Schadenswirkung, Determinismus, Kontaktereignisse |
| `test_ai.cpp` | Fairness (keine Reaktion auf unentdeckte Gegner), IJN-Doktrin (Torpedos zuerst, Geschütze dunkel), Kämmen gemeldeter Laufbahnen, Rückzug eines einzelnen Zerstörers vor Kreuzern, Befehlsquittungen, Torpedofächer gegen eigene Schiffe, Savo Island KI gegen KI deterministisch |
| `test_ui.cpp` | Jeder Bildschirm baut headless (ohne Grafik), Schiedsrichter-Gefecht mit allen Panels, Sprachtabellen vollständig und deckungsgleich, jeder im Code benutzte Schlüssel existiert |

Die Tests laufen unter Linux (GCC) und Windows (MSVC) gleichermaßen; die Oberfläche wird dabei
über `NullServices` ohne Renderer betrieben.

## 2. KI-gegen-KI-Batches

`irontide_headless` spielt eine Mission ohne Grafik mit beiden Seiten unter KI-Führung:

```sh
build/linux-release/irontide_headless --data data --scenario tassafaronga --runs 20 --seed 1
  --log      Entscheidungsprotokoll aller Ebenen (mit Alternativen der Befehlshaber)
  --events   Kontakte, erste Salve je Schiff, Torpedoabschüsse, Treffer mit Entfernung,
             Beleuchtung und Brandzustand des Ziels
  --trace 60 Positionsliste aller Schiffe alle 60 s
  --skill 0.7  KI-Können
```

Die Zusammenfassung zeigt je Seite Tonnageverluste, Punkte, Torpedo- und Granattrefferquoten,
versenkte Schiffe über alle Läufe und die Häufigkeit jeder Entscheidung. Damit werden
KI-Änderungen bewertet: Ein Lauf vorher und nachher über dieselben Seeds zeigt, ob sich
Verhalten und Ergebnis in die gewünschte Richtung bewegen, und `--events` erklärt Ausreißer
(so wurden z. B. Zerstörer gefunden, die ihre eigenen Kreuzer torpedierten).

## 3. Bildprüfung des Windows-Programms

Das Spiel lässt sich vollständig über Startparameter steuern, rendert eine Anzahl Bilder,
speichert ein Bildschirmfoto und beendet sich:

```
IronTide.exe --screen battle --scenario savo_island --side axis --advance 2400 --focus Chokai \
             --yaw 300 --pitch -8 --distance 400 --insight --frames 4 --size 1600x900 \
             --screenshot shot.png --log run.log --no-audio
```

| Parameter | Bedeutung |
|---|---|
| `--screen` | `menu`, `missions`, `briefing`, `settings`, `credits`, `unitref`, `editor`, `battle`, `map`, `split`, `debrief` |
| `--scenario`, `--side` | Mission und Seite (`allies`, `axis`, `none` = Schiedsrichter) |
| `--advance` | Sekunden Gefechtszeit, die vorher simuliert werden |
| `--focus`, `--class` | gewähltes Schiff im Gefecht, Klasse im Handbuch |
| `--yaw`, `--pitch`, `--distance` | Kamera |
| `--insight`, `--insight-filter` | KI-Einblick öffnen; 0 eigene, 1 gegnerische, 2 alle (gegnerische nur als Schiedsrichter) |
| `--frames`, `--size`, `--msaa`, `--lang` | Bilder vor dem Foto, Auflösung, Kantenglättung, Sprache |

Unter Linux läuft das mit dem MinGW-Build unter **Wine mit vkd3d** (D3D12 → Vulkan) und
**lavapipe** (Vulkan in Software) auf einem virtuellen X-Server. Benötigt werden
`VKD3D_CONFIG=virtual_heaps` (sonst zu viele Deskriptorsätze für lavapipe) und die echte
`d3dcompiler_47.dll` neben dem Programm (`WINEDLLOVERRIDES=d3dcompiler_47=n`).

## 4. Continuous Integration

[`.github/workflows/ci.yml`](../.github/workflows/ci.yml) läuft bei jedem Push auf `main` und bei
Pull Requests:

- **Linux** (Ubuntu 24.04, GCC): Build, Test-Suite, kurze KI-Batches aller Missionen.
- **Windows** (Windows Server 2022, MSVC): Build von Spiel, Tests und Werkzeugen, Test-Suite,
  Rauchtest des Spiels auf dem WARP-Software-Renderer (Hauptmenü-Screenshot als Artefakt) und das
  fertige Spielpaket `IronTide-windows-x64` als Artefakt.

## Was bewusst nicht automatisiert ist

- Spielgefühl, Lesbarkeit der Nachtszenen und Klang werden über die Screenshots und von Hand
  bewertet; die Audio-Ausgabe (XAudio2) lässt sich in CI nicht abhören.
- Leistung auf echter Grafikhardware: die CI-Rechner haben keine GPU.
