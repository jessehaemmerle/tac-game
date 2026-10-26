# ADR-0005: Assets nur unter freien Lizenzen, Rest prozedural

**Status:** Accepted
**Datum:** 2026-10-26

## Kontext
Das Repository steht unter AGPL-3.0. 3D-Modelle sollen aus Asset-Quellen kommen, soweit
brauchbar; der Rest wird prozedural erzeugt.

## Entscheidung
- Zulässig im Repository: **CC0** und **CC-BY** (inkl. CC-BY-SA) – jeweils mit Eintrag in
  `CREDITS.md` und in den Spiel-Credits. Keine NC-/ND-Lizenzen, keine Store-EULAs.
- Jede Schiffsklasse besitzt ein **prozedurales Modell** aus Klassendaten (Rumpflinien,
  Aufbauten, Türme, Schornsteine, Masten). Ein glTF-2.0-Modell kann es pro Klasse ersetzen
  (`model.gltf` in den Klassendaten), wenn Lizenz und Qualität passen.
- Audio: Stimmen und Effekte per ElevenLabs (Nutzungsrechte des Accounts), Quellen in `CREDITS.md`.
- Gelände: Mapzen/Tilezen Terrain Tiles (SRTM/GMTED/ETOPO1 u. a.) mit Namensnennung.

## Konsequenzen
- Das Spiel ist vollständig ohne externe Downloads spielbar.
- Einheitlicher Stil durch prozedurale Basis; einzelne Klassen können hochwertiger ersetzt werden.
