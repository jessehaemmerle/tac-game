# ADR-0001: Eigene C++20-Engine auf Direct3D 12

**Status:** Accepted
**Datum:** 2026-10-26
**Entscheider:** Projektleitung (Jesse Hämmerle), Entwicklung

## Kontext

*Iron Tide: Naval Command 1939–45* soll ein natives Windows-Spiel auf DirectX 12 sein, angelehnt an
*Sea Power – Naval Combat in the Missile Age*. Schwerpunkte: Gegner-KI, Grafik, Taktik-Ansicht,
3D-Modelle, Einheiten-Referenz. Die Entwicklung erfolgt zu großen Teilen textbasiert (Code, Daten,
Shader) in einer Linux-Umgebung ohne GPU; visuelle Prüfung erfolgt per Software-Rendering
(Wine + vkd3d + lavapipe) und auf Windows-Hardware beim Spieler.

## Entscheidung

Eigene Engine in C++20 mit direktem Direct3D-12-Renderer (Mindest-Feature-Level 11_0, Ziel 12_0,
kein Raytracing – „maximale Kompatibilität“). Alles, was nicht zwingend Windows braucht
(Simulation, KI, Daten, prozedurale Modelle, UI-Logik), ist plattformunabhängig (siehe ADR-0002).

## Optionen

### A: Eigene C++/D3D12-Engine (gewählt)
| Dimension | Bewertung |
|---|---|
| Komplexität | Hoch (Renderer, Plattform, Asset-Pipeline selbst gebaut) |
| Kosten | Keine Lizenzkosten |
| Skalierung | Volle Kontrolle über Datenlayout und Performance |
| Werkzeuge | Vollständig textbasiert → automatisierbar, testbar, reviewbar |

**Pro:** Kein Editor-Zwang; jede Zeile im Repo; deterministische Simulation; headless testbar.
**Contra:** Grafik-Features müssen selbst implementiert werden.

### B: Unreal Engine 5
**Pro:** Sofort hochwertige Grafik. **Contra:** Levels, Blueprints, Assets sind Binärdateien, die
nur im Editor entstehen – nicht textbasiert entwickelbar, nicht headless testbar.

### C: Unity 6 HDRP
**Pro/Contra** analog zu B.

## Konsequenzen
- Einfacher: Tests, Reviews, CI, Reproduzierbarkeit, KI-Batch-Simulationen.
- Schwerer: Jedes Grafik-Feature (Ozean, Himmel, Effekte, Post-Processing) muss gebaut werden.
- Später prüfen: DXIL/Shader Model 6 als Primärpfad, sobald die Testumgebung es unterstützt (ADR-0003).
