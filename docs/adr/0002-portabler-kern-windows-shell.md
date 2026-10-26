# ADR-0002: Portabler Simulationskern, Windows-Schale

**Status:** Accepted
**Datum:** 2026-10-26

## Kontext
Simulation und KI sind der Kern des Spiels und müssen intensiv getestet werden (Ballistik,
Sensorik, Schadensmodell, KI-Entscheidungen). Die Entwicklungsumgebung ist Linux; das Spiel
läuft auf Windows.

## Entscheidung
Strikte Modulgrenzen:

| Modul | Plattform | Abhängigkeiten |
|---|---|---|
| `core` (Mathe, RNG, JSON, Lokalisierung, Log) | portabel | – |
| `sim` (Welt, Schiffe, Ballistik, Torpedos, Sensoren, Lagebild, Schaden, Umwelt) | portabel | core |
| `ai` (Befehlshaber/Verband/Kommandant, Doktrin, Utility-Bewertung) | portabel | core, sim |
| `game` (Szenarien, Ziele, Sitzung, Spielerbefehle) | portabel | core, sim, ai |
| `procgen` (prozedurale Schiffs-/Terrain-Meshes, glTF-Import) | portabel | core, sim (Klassendaten) |
| `ui` (ImGui-Bildschirme: Taktikkarte, Referenz, Editor, HUD) | portabel (nur ImGui-API) | game, procgen |
| `render` (D3D12), `audio` (XAudio2), `platform/win32` | Windows | alles |

Die Simulation ist deterministisch (fester Zeitschritt 50 ms, eigener PCG32-RNG pro Welt), damit
KI-Batchläufe und Fehler reproduzierbar sind. Kommunikation Sim → Präsentation ausschließlich
über einen Ereignisstrom (`sim::Event`) und lesenden Zugriff auf den Weltzustand.

## Konsequenzen
- Tests (doctest) und das Headless-Werkzeug `irontide_headless` laufen auf Linux und in CI.
- UI-Bildschirme lassen sich ohne GPU als Rauchtest durchlaufen (ImGui ohne Backend).
- Der Renderer darf niemals Simulationszustand verändern.
