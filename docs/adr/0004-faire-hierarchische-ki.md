# ADR-0004: Faire, hierarchische Gegner-KI mit Doktrin und Utility-Bewertung

**Status:** Accepted
**Datum:** 2026-10-26

## Kontext
Die Gegner-KI ist ein Schwerpunkt. Vorgabe: „fair“ – die KI sieht nur ihr eigenes Sensorbild;
Schwierigkeit entsteht über Ausbildung, Reaktionszeit, Doktrin und Risikobereitschaft.

## Entscheidung
Drei Ebenen, jede arbeitet ausschließlich auf dem **Lagebild der eigenen Seite**
(`sim::ContactPicture`: Kontakte mit Positionsunsicherheit, Klassifizierungsstufe, Alter):

1. **Befehlshaber (Force Commander)** – Lagebeurteilung (geschätzte Kräfteverhältnisse inkl.
   Unsicherheit, Zeit bis Morgengrauen, Munition/Torpedos, Schäden), wählt per Utility-Bewertung
   eine Absicht: *Aufklären, Annähern, Gefecht, Torpedoangriff, Abfangen, Geleitschutz, Ausweichen,
   Absetzen, Sammeln*.
2. **Verband (Division/Gruppe)** – Formation, Manöver (T-Kreuzen, Torpedoangriff mit Abdrehen,
   Flankieren), Zielverteilung (Feuerkonzentration ohne Overkill), Beleuchtungsrollen.
3. **Kommandant (Einheit)** – Waffeneinsatz (Feuereröffnung vs. Tarnung, Torpedo-Lösung mit
   Fächerwinkel), Ausweichen (Torpedolaufbahnen kämmen, Salvenjagen), Schadenskontrolle,
   Navigation (Kollisions-/Landvermeidung).

Entscheidungen erfolgen per **Utility-Bewertung** (gewichtete Faktoren mit Antwortkurven) plus
**Plänen** (Schrittfolgen mit Abbruchbedingungen, z. B. Torpedoangriff). Jede Entscheidung wird mit
allen Faktoren protokolliert (`ai::DecisionLog`) – sichtbar im KI-Einblick-Fenster und in der
Nachbesprechung.

**Doktrinen** (JSON) je Marine und Jahr, z. B. IJN 1942: Nachtkampf, Torpedos vor Geschützen,
Scheinwerfer/Leuchtgranaten; USN 1942: Radar-gestütztes Artilleriegefecht, Linienformation.
**Besatzungsqualität** (Grün/Ausgebildet/Veteran/Elite) beeinflusst Ausguck, Feuerleitung,
Reaktionszeit, Schadenskontrolle.

## Konsequenzen
- KI-Verhalten ist nachvollziehbar und per Daten justierbar.
- Headless-Batchläufe (`irontide_headless`) messen KI-Qualität statistisch (z. B. historische
  Plausibilität von Savo Island).
- Mehr Aufwand als „allwissende“ KI, dafür glaubwürdiges Verhalten (Verwechslungen, Eigenbeschuss-
  risiko, Überraschung).
