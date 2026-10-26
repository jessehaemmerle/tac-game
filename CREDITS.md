# Credits / Quellen

Iron Tide: Naval Command 1939–45 steht unter der AGPL-3.0 (siehe `LICENSE`). Alle mitgelieferten
Inhalte sind entweder selbst erzeugt oder stehen unter freien Lizenzen (ADR-0005).

## Audio

Alle Soundeffekte und Brückenmeldungen wurden für dieses Projekt mit **ElevenLabs** erzeugt
(Nutzungsrechte des erzeugenden Accounts) und mit `tools/audio/build_bank.py` aufbereitet
(Trimmen, Normalisieren, Loop-Überblendung, Telefon-Bandpass für Sprache).

| Inhalt | Modell | Umfang |
|---|---|---|
| Soundeffekte (Geschütze, Einschläge, Explosionen, Torpedos, Leuchtgranaten, Scheinwerfer, Alarmglocke, Maschinentelegraf, Sinkgeräusche, Interface) | Sound Effects v2 | 18 Effekte × bis zu 3 Varianten |
| Ambience-Loops (See, Wind, Brand an Deck) | Sound Effects v2, nahtlose Loops | 3 |
| Brückenmeldungen Deutsch – Stimme „Ernst" | Eleven v4 | 31 Meldungen × 2 Takes |
| Bridge reports English – voice "Vincent" | Eleven v4 | 31 reports × 2 takes |

Die Rohdateien werden nicht eingecheckt; `tools/audio/fetch_generations.py` lädt sie aus den
gespeicherten Generierungs-Status-Dateien, `build_bank.py` erzeugt daraus `data/audio`.

## Schriften

| Schrift | Autor | Lizenz |
|---|---|---|
| Barlow, Barlow Condensed | The Barlow Project Authors (Jeremy Tribby) | SIL Open Font License 1.1 (`data/fonts/OFL-Barlow*.txt`) |
| IBM Plex Mono | IBM Corp. | SIL Open Font License 1.1 (`data/fonts/OFL-IBMPlexMono.txt`) |

## Gelände

Höhendaten Ironbottom Sound / Guadalcanal / Savo / Florida Islands aus **Terrain Tiles**
(Mapzen/Tilezen, AWS Open Data) – Quellen u. a. SRTM, GMTED2010, ETOPO1; Namensnennung siehe
<https://github.com/tilezen/joerd/blob/master/docs/attribution.md>. Aufbereitet mit
`tools/terrain/fetch_terrain.py`.

## 3D-Modelle

Alle Schiffe werden **prozedural aus den Klassendaten** erzeugt (`src/procgen/ShipModel.cpp`:
Rumpflinien, Aufbauten, Türme, Torpedosätze, Schornsteine, Masten, Radar, Tarnanstrich).

Recherche nach frei lizenzierten Modellen (Stand Oktober 2026): Für die konkreten Klassen der
Pazifik-Kampagne (u. a. Takao, Aoba, Furutaka, Tenryū, Yūbari, Fubuki, Kagerō, New Orleans,
Northampton, Pensacola, Brooklyn, Bagley, Mahan, Fletcher, Kent) wurden keine historisch passenden Modelle unter
CC0 oder CC-BY gefunden. Gefunden wurden nur stilisierte Low-Poly-Sammlungen
(z. B. Quaternius „LowPoly Ship Pack", CC0, OpenGameArt) oder Modelle unter Store-Lizenzen
(CGTrader, RenderHub, Unity Asset Store, 3DCADBrowser) bzw. mit ND-Klausel – beides nach
ADR-0005 nicht zulässig oder stilistisch unpassend.

Eigene oder frei lizenzierte glTF-2.0-Modelle lassen sich pro Klasse einbinden: Datei nach
`data/models/` legen und in den Klassendaten `"model": {"gltf": "<datei>.glb", "gltf_scale": 1.0}`
setzen. Knoten mit den Namen `mount_<mount-id>` (Turm, dreht in der Seite) und
`guns_<mount-id>` (Rohre, heben sich) werden den Geschützaufstellungen der Klasse zugeordnet und
wie beim prozeduralen Modell animiert.

## Bibliotheken

| Bibliothek | Lizenz |
|---|---|
| Dear ImGui | MIT |
| nlohmann/json | MIT |
| cgltf | MIT |
| stb_image / stb_image_write | MIT / Public Domain |
| doctest | MIT |

## Historische Grundlagen

Klassendaten (Abmessungen, Verdrängung, Geschwindigkeit, Bewaffnung, Panzerung, Sensoren) und
Gefechtsabläufe folgen allgemein publizierten Eckdaten. Sie sind für das Spiel gerundet und
vereinfacht; Abweichungen von einzelnen Quellen sind möglich.

Weiterführende Literatur: NavWeaps (navweaps.com); Conway's All the World's Fighting Ships
1922–1946; S. E. Morison, *History of United States Naval Operations in World War II*, Bd. V;
R. Lundgren, *The World Wonder'd*; J. D. Hornfischer, *Neptune's Inferno*.
