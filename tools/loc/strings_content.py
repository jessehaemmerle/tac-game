# -*- coding: utf-8 -*-
"""AI decision texts, scenario texts and ship class histories (key: (English, German))."""
S = {
# --- AI options (postures) - used as option names and as commander decisions
"ai.o.mission": ("Continue mission", "Auftrag fortsetzen"), "ai.o.investigate": ("Investigate contact", "Kontakt aufklären"),
"ai.o.engage": ("Engage with guns", "Artilleriegefecht"), "ai.o.torpedo_attack": ("Torpedo attack", "Torpedoangriff"),
"ai.o.shadow": ("Shadow", "Fühlung halten"), "ai.o.withdraw": ("Withdraw", "Absetzen"), "ai.o.regroup": ("Regroup", "Sammeln"),
# --- AI captain / group decisions ({0} = ship or group)
"ai.d.comb_torpedo": ("{0} turns to comb the torpedo tracks", "{0} dreht in die Torpedolaufbahnen"),
"ai.d.evade_torpedo": ("{0} turns away from the torpedoes", "{0} dreht von den Torpedos ab"),
"ai.d.launch_flares": ("{0} sends a floatplane to drop flares", "{0} schickt ein Flugzeug mit Leuchtfallschirmen"),
"ai.d.retire_damaged": ("{0} is badly damaged and retires", "{0} ist schwer beschädigt und läuft ab"),
"ai.d.searchlight_on": ("{0} switches on her searchlight", "{0} schaltet den Scheinwerfer ein"),
"ai.d.searchlight_off": ("{0} switches off her searchlight", "{0} schaltet den Scheinwerfer aus"),
"ai.d.torpedo_blocked": ("{0} cannot fire torpedoes", "{0} kann keine Torpedos schießen"),
"ai.d.torpedo_hold_friendly": ("{0} holds torpedoes: friendly ships in the line of fire", "{0} hält Torpedos zurück: eigene Schiffe in der Schussbahn"),
"ai.d.torpedo_impact_window": ("{0}: torpedoes should have arrived, resuming", "{0}: Torpedolaufzeit verstrichen, weiter"),
"ai.d.torpedo_launch_position": ("{0} reaches the launch position", "{0} erreicht die Abschussposition"),
"ai.d.torpedo_no_solution": ("{0} has no torpedo solution", "{0} hat keine Torpedolösung"),
"ai.d.torpedo_reattack": ("{0} reloads and attacks again", "{0} lädt nach und greift erneut an"),
"ai.d.torpedo_turn_away": ("{0} turns away after launching", "{0} dreht nach dem Abschuss ab"),
"ai.d.torpedoes_launched": ("{0} launches torpedoes", "{0} schießt Torpedos"),
# --- AI considerations
"ai.c.aggression": ("Doctrine: aggressiveness", "Doktrin: Angriffslust"), "ai.c.ammunition": ("Ammunition remaining", "Restmunition"),
"ai.c.caution": ("Doctrine: caution", "Doktrin: Vorsicht"), "ai.c.dawn": ("Time until dawn", "Zeit bis zur Dämmerung"),
"ai.c.dispersion": ("Own formation dispersed", "Eigener Verband zerstreut"), "ai.c.doctrine": ("Doctrine preference", "Doktrin-Vorgabe"),
"ai.c.force_ratio": ("Force ratio", "Kräfteverhältnis"), "ai.c.health": ("Own condition", "Eigener Zustand"),
"ai.c.mission_pending": ("Mission not yet complete", "Auftrag noch offen"), "ai.c.mission_picket": ("Picket mission", "Vorpostenauftrag"),
"ai.c.night": ("Darkness", "Dunkelheit"), "ai.c.no_threat": ("No threat detected", "Keine Bedrohung erkannt"),
"ai.c.picket_duty": ("Picket duty", "Vorpostendienst"), "ai.c.preserve_surprise": ("Preserve surprise", "Überraschung wahren"),
"ai.c.proximity": ("Contact proximity", "Nähe des Kontakts"), "ai.c.range_band": ("Range suits the weapon", "Entfernung passt zur Waffe"),
"ai.c.reason": ("Reason", "Grund"), "ai.c.surprise": ("Enemy unaware", "Gegner ahnungslos"), "ai.c.target_identified": ("Target identified", "Ziel identifiziert"),
"ai.c.target_size": ("Target size", "Zielgröße"), "ai.c.target_value": ("Target value", "Zielwert"), "ai.c.torpedoes_first": ("Doctrine: torpedoes first", "Doktrin: zuerst Torpedos"),
"ai.c.torpedoes_ready": ("Torpedoes ready", "Torpedos bereit"), "ai.c.unidentified": ("Contact unidentified", "Kontakt nicht identifiziert"),
"ai.c.weaker": ("Enemy weaker", "Gegner schwächer"),
# --- scenario: Savo Island
"scenario.savo_island.title": ("Battle of Savo Island", "Schlacht vor der Insel Savo"),
"scenario.savo_island.briefing": (
 "Night of 8/9 August 1942, Ironbottom Sound off Guadalcanal. Two days ago US Marines landed on Guadalcanal and Tulagi; the transports are still unloading off Lunga Point. Vice Admiral Gunichi Mikawa has raced down from Rabaul with five heavy cruisers, two light cruisers and a destroyer. The Allied screen is split around Savo Island: a southern group (HMAS Canberra, USS Chicago and two destroyers), a northern group (Vincennes, Quincy, Astoria and two destroyers) and two radar pickets west of the island. The night is dark and overcast, rain squalls drift over the sound, the moon has set.",
 "Nacht vom 8. auf den 9. August 1942, Ironbottom Sound vor Guadalcanal. Vor zwei Tagen sind US-Marines auf Guadalcanal und Tulagi gelandet; die Transporter löschen noch vor Lunga Point. Vizeadmiral Gunichi Mikawa ist mit fünf Schweren Kreuzern, zwei Leichten Kreuzern und einem Zerstörer von Rabaul heruntergelaufen. Der alliierte Sicherungsschirm ist um die Insel Savo verteilt: eine Südgruppe (HMAS Canberra, USS Chicago und zwei Zerstörer), eine Nordgruppe (Vincennes, Quincy, Astoria und zwei Zerstörer) sowie zwei Radarvorposten westlich der Insel. Die Nacht ist dunkel und bedeckt, Regenböen ziehen über den Sund, der Mond ist untergegangen."),
"scenario.savo_island.orders_axis": (
 "Pass Savo Island undetected, destroy the Allied cruisers in a night action and, if possible, strike the transports off Lunga Point. Break off in time: by daylight you must be out of range of American carrier aircraft. Japanese night training, Type 93 torpedoes and surprise are your weapons.",
 "Passiere die Insel Savo unentdeckt, vernichte die alliierten Kreuzer im Nachtgefecht und greife wenn möglich die Transporter vor Lunga Point an. Brich rechtzeitig ab: Bei Tagesanbruch musst du außer Reichweite amerikanischer Trägerflugzeuge sein. Japanisches Nachtgefechtstraining, Type-93-Torpedos und Überraschung sind deine Waffen."),
"scenario.savo_island.orders_allies": (
 "Guard both entrances to the anchorage around Savo Island and stop any surface force before it reaches the transports. Your crews have been at action stations for two days and are now in Condition II - raising the alarm will cost precious minutes. Radar (SC on the destroyers) is unreliable close to land.",
 "Bewache beide Zugänge zum Ankerplatz um die Insel Savo und stoppe jeden Überwasserverband, bevor er die Transporter erreicht. Deine Besatzungen waren zwei Tage auf Gefechtsstation und gehen jetzt Seewache (Zustand II) – der Alarm kostet wertvolle Minuten. Das Radar (SC auf den Zerstörern) ist in Landnähe unzuverlässig."),
"scenario.savo_island.intel_axis": (
 "Reconnaissance on 7 August reported several cruisers and destroyers covering the landing. Expect patrol lines on both sides of Savo Island; destroyers with radar may be posted to the west.",
 "Die Aufklärung meldete am 7. August mehrere Kreuzer und Zerstörer zum Schutz der Landung. Rechne mit Patrouillenlinien beiderseits der Insel Savo; westlich könnten Zerstörer mit Radar stehen."),
"scenario.savo_island.intel_allies": (
 "A sighting by an Australian Hudson on 8 August reported 'three cruisers, three destroyers and two seaplane tenders' heading south-east - read as a force setting up a seaplane base. The report arrived hours late. No surface attack is expected tonight.",
 "Eine Sichtung durch eine australische Hudson am 8. August meldete „drei Kreuzer, drei Zerstörer und zwei Flugzeugmutterschiffe“ auf Südostkurs – gedeutet als Verband zum Aufbau einer Wasserflugzeugbasis. Die Meldung kam Stunden zu spät. Für diese Nacht wird kein Überwasserangriff erwartet."),
"scenario.savo_island.history": (
 "Historically Mikawa's column slipped past the picket destroyers unseen. Floatplanes dropped flares over the southern group; within minutes Canberra was wrecked by gunfire and Chicago lost her bow to a torpedo, without warning the northern group. Mikawa swept around Savo and destroyed Vincennes, Quincy and Astoria in close-range night action. More than a thousand Allied sailors died. Fearing air attack at dawn, Mikawa withdrew without attacking the transports; on the way home Kako was sunk by the submarine S-44. It remains one of the heaviest defeats ever suffered by the US Navy.",
 "Historisch glitt Mikawas Kiellinie ungesehen an den Vorpostenzerstörern vorbei. Bordflugzeuge warfen Leuchtfallschirme über der Südgruppe ab; binnen Minuten war die Canberra zusammengeschossen und die Chicago verlor ihren Bug durch einen Torpedo – ohne dass die Nordgruppe gewarnt wurde. Mikawa schwenkte um Savo und vernichtete Vincennes, Quincy und Astoria im Nachtgefecht auf kurze Distanz. Über tausend alliierte Seeleute starben. Aus Furcht vor Luftangriffen bei Tagesanbruch zog sich Mikawa zurück, ohne die Transporter anzugreifen; auf dem Heimweg versenkte das U-Boot S-44 die Kako. Es bleibt eine der schwersten Niederlagen der US Navy."),
"objective.savo_island.sink": ("Sink enemy tonnage", "Gegnerische Tonnage versenken"),
"objective.savo_island.transports": ("Destroy the transports off Lunga Point", "Transporter vor Lunga Point vernichten"),
"objective.savo_island.preserve": ("Preserve your own force", "Eigenen Verband erhalten"),
"objective.savo_island.protect": ("Protect the transports", "Transporter schützen"),
"group.savo.mikawa": ("Eighth Fleet (Mikawa)", "8. Flotte (Mikawa)"),
"group.savo.southern": ("Southern Group", "Südgruppe"), "group.savo.southern_screen": ("Southern screen", "Sicherung Süd"),
"group.savo.northern": ("Northern Group", "Nordgruppe"), "group.savo.northern_screen": ("Northern screen", "Sicherung Nord"),
"group.savo.blue": ("Picket Blue", "Vorposten Blue"), "group.savo.talbot": ("Picket Ralph Talbot", "Vorposten Ralph Talbot"),
"group.savo.transports": ("Transport anchorage", "Transporter-Ankerplatz"),
# --- torpedo notes
"torpedo.ijn_type93.note": (
 "The 'Long Lance': a 61 cm oxygen-fuelled torpedo with enormous range and a heavy warhead. It leaves almost no wake - at night it arrives unseen. The Allies did not know of its performance in 1942 and kept mistaking torpedo hits for submarine attacks.",
 "Der „Long Lance“: ein 61-cm-Sauerstofftorpedo mit enormer Reichweite und schwerem Gefechtskopf. Er hinterlässt fast keine Blasenbahn – nachts kommt er ungesehen. Die Alliierten kannten 1942 seine Leistung nicht und hielten Treffer oft für U-Boot-Angriffe."),
"torpedo.usn_mk15.note": (
 "The standard US destroyer torpedo. In 1942 its Mark 6 exploder and depth control were unreliable: many hits failed to explode or the torpedoes ran too deep.",
 "Der Standardtorpedo der US-Zerstörer. 1942 waren Zündpistole Mark 6 und Tiefensteuerung unzuverlässig: Viele Treffer zündeten nicht oder die Torpedos liefen zu tief."),
# --- class histories & tactics
"class.ijn_ca_takao.history": (
 "The Takao class combined ten 20 cm guns with a towering bridge that housed the most elaborate fire-control and command spaces of any cruiser of her time. Chokai was Mikawa's flagship at Savo Island and was hit several times by Quincy's last salvoes.",
 "Die Takao-Klasse vereinte zehn 20-cm-Geschütze mit einem gewaltigen Brückenaufbau, der die aufwendigsten Feuerleit- und Führungsräume aller Kreuzer ihrer Zeit beherbergte. Die Chokai war Mikawas Flaggschiff bei Savo und erhielt mehrere Treffer aus den letzten Salven der Quincy."),
"class.ijn_ca_takao.tactics": (
 "Heavy gun and torpedo armament, excellent night optics. Keep the range short at night where its torpedoes and trained lookouts dominate.",
 "Schwere Artillerie und Torpedobewaffnung, hervorragende Nachtoptik. Nachts die Entfernung kurz halten, dort dominieren Torpedos und geschulte Ausguckposten."),
"class.ijn_ca_aoba.history": (
 "Aoba and Kinugasa were improved Furutakas with twin turrets. Both fought at Savo; at Cape Esperance in October 1942 Aoba was ambushed by radar-directed US cruisers and badly damaged, Kinugasa was sunk at Guadalcanal in November.",
 "Aoba und Kinugasa waren verbesserte Furutakas mit Zwillingstürmen. Beide kämpften bei Savo; bei Kap Esperance im Oktober 1942 geriet die Aoba in einen Hinterhalt radargeleiteter US-Kreuzer und wurde schwer beschädigt, die Kinugasa sank im November vor Guadalcanal."),
"class.ijn_ca_furutaka.history": (
 "Japan's first generation of heavy cruisers, small but well armed. Kako was torpedoed by the submarine S-44 on 10 August 1942 while returning from Savo; Furutaka was sunk at Cape Esperance two months later.",
 "Japans erste Generation Schwerer Kreuzer, klein, aber gut bewaffnet. Die Kako wurde am 10. August 1942 auf dem Rückweg von Savo vom U-Boot S-44 torpediert; die Furutaka sank zwei Monate später bei Kap Esperance."),
"class.ijn_cl_tenryu.history": (
 "Small light cruisers of 1919, designed as destroyer flotilla leaders. Old and lightly armed, they were still useful in night actions thanks to their torpedoes.",
 "Kleine Leichte Kreuzer von 1919, als Führer von Zerstörerflottillen gebaut. Alt und leicht bewaffnet, im Nachtgefecht dank ihrer Torpedos dennoch brauchbar."),
"class.ijn_cl_yubari.history": (
 "An experimental design that packed a full light cruiser armament onto a 3,000-ton hull. Yubari fought at Savo Island at the end of Mikawa's column.",
 "Ein Versuchsentwurf, der die volle Bewaffnung eines Leichten Kreuzers auf 3.000 Tonnen unterbrachte. Die Yubari kämpfte bei Savo am Ende von Mikawas Kiellinie."),
"class.ijn_dd_kamikaze.history": (
 "Destroyers of the early 1920s. Yunagi accompanied Mikawa to Savo Island and covered the withdrawal.",
 "Zerstörer der frühen 1920er-Jahre. Die Yunagi begleitete Mikawa nach Savo und deckte den Rückzug."),
"class.ijn_dd_fubuki.history": (
 "The 'Special Type' destroyers of 1928 shocked other navies with six 12.7 cm guns in enclosed mounts and nine 61 cm torpedo tubes with reloads. Ayanami fought alone against two US battleships off Savo in November 1942.",
 "Die „Spezialtyp“-Zerstörer von 1928 schockierten andere Marinen mit sechs 12,7-cm-Geschützen in geschlossenen Türmen und neun 61-cm-Torpedorohren mit Nachladung. Die Ayanami kämpfte im November 1942 vor Savo allein gegen zwei US-Schlachtschiffe."),
"class.ijn_dd_kagero.history": (
 "The backbone of the 'Tokyo Express' supply runs to Guadalcanal. At Tassafaronga on 30 November 1942 Kagero-class destroyers turned a surprise attack by a superior US cruiser force into a Japanese victory with a mass torpedo launch.",
 "Das Rückgrat des „Tokyo Express“ nach Guadalcanal. Bei Tassafaronga am 30. November 1942 verwandelten Zerstörer der Kagero-Klasse den Überraschungsangriff eines überlegenen US-Kreuzerverbandes mit einem massiven Torpedoangriff in einen japanischen Sieg."),
"class.ijn_dd_kagero.tactics": (
 "Fire torpedoes first, guns second. Reload at sea and attack again. Turn away immediately after launching.",
 "Zuerst Torpedos, dann Artillerie. Auf See nachladen und erneut angreifen. Nach dem Abschuss sofort abdrehen."),
"class.ijn_dd_yugumo.history": (
 "Improved Kageros with higher-angle guns. Takanami was lost at Tassafaronga after drawing the fire of the entire US force.",
 "Verbesserte Kageros mit Steilfeuer-Geschützen. Die Takanami ging bei Tassafaronga verloren, nachdem sie das Feuer des gesamten US-Verbandes auf sich gezogen hatte."),
"class.ijn_dd_shiratsuyu.history": (
 "The first Japanese destroyers with quadruple torpedo mounts. Yudachi caused havoc in the first naval battle of Guadalcanal; Shigure became famous as a lucky ship.",
 "Die ersten japanischen Zerstörer mit Vierlings-Torpedorohrsätzen. Die Yudachi richtete in der ersten Seeschlacht von Guadalcanal ein Chaos an; die Shigure wurde als Glücksschiff berühmt."),
"class.ran_ca_kent.history": (
 "A British 'County' class treaty cruiser with high freeboard and long range, built for imperial trade protection rather than close combat. HMAS Canberra was hit by more than twenty shells and two torpedoes in the first minutes at Savo Island and had to be scuttled.",
 "Ein britischer Vertragskreuzer der „County“-Klasse mit hohem Freibord und großer Reichweite, gebaut für den Handelsschutz statt für den Nahkampf. Die HMAS Canberra erhielt in den ersten Minuten bei Savo über zwanzig Granat- und zwei Torpedotreffer und musste selbst versenkt werden."),
"class.usn_ca_new_orleans.history": (
 "The last US 'treaty' heavy cruisers, with better armour than their predecessors. Astoria, Quincy and Vincennes were all sunk at Savo Island - surprised, illuminated by searchlights and shot to pieces before they could reply effectively.",
 "Die letzten US-„Vertragskreuzer“, besser gepanzert als ihre Vorgänger. Astoria, Quincy und Vincennes sanken alle bei Savo – überrascht, von Scheinwerfern angeleuchtet und zusammengeschossen, bevor sie wirksam antworten konnten."),
"class.usn_ca_new_orleans.tactics": (
 "Strong gun platform. Do not let the enemy close at night unseen: keep crews at battle stations near enemy waters and use radar early.",
 "Starke Artillerieplattform. Den Gegner nachts nicht ungesehen herankommen lassen: in Feindnähe Gefechtsstationen besetzt halten und früh Radar nutzen."),
"class.usn_ca_northampton.history": (
 "Northampton-class cruisers carried nine 8-inch guns in three triple turrets. USS Chicago lost her bow to a torpedo at Savo and failed to warn the northern group; Northampton herself was sunk by Long Lance torpedoes at Tassafaronga.",
 "Die Kreuzer der Northampton-Klasse trugen neun 8-Zoll-Geschütze in drei Drillingstürmen. Die USS Chicago verlor bei Savo ihren Bug durch einen Torpedo und warnte die Nordgruppe nicht; die Northampton selbst sank bei Tassafaronga durch Long-Lance-Torpedos."),
"class.usn_ca_pensacola.history": (
 "The first US treaty cruisers, with ten 8-inch guns but thin armour. Salt Lake City fought at Cape Esperance; Pensacola survived a Long Lance hit at Tassafaronga.",
 "Die ersten US-Vertragskreuzer mit zehn 8-Zoll-Geschützen, aber dünner Panzerung. Die Salt Lake City kämpfte bei Kap Esperance; die Pensacola überstand bei Tassafaronga einen Long-Lance-Treffer."),
"class.usn_cl_brooklyn.history": (
 "Fifteen rapid-firing 6-inch guns gave the Brooklyns a tremendous volume of fire at short and medium range. Boise was badly hit at Cape Esperance after illuminating a target with her searchlight.",
 "Fünfzehn schnellfeuernde 6-Zoll-Geschütze gaben der Brooklyn-Klasse auf kurze und mittlere Distanz eine enorme Feuerkraft. Die Boise wurde bei Kap Esperance schwer getroffen, nachdem sie ein Ziel mit dem Scheinwerfer angeleuchtet hatte."),
"class.usn_cl_brooklyn.tactics": (
 "Volume of fire, not penetration: overwhelm cruisers and destroyers at 8-12 km. Avoid searchlights - they mark you for every enemy gun.",
 "Feuervolumen statt Durchschlag: Kreuzer und Zerstörer auf 8–12 km überwältigen. Scheinwerfer vermeiden – sie markieren das eigene Schiff für jedes gegnerische Geschütz."),
"class.usn_cl_st_louis.history": (
 "A refined Brooklyn with 5-inch/38 dual-purpose guns. USS Helena's SG surface radar gave the first warning at Cape Esperance - the turning point of radar in night actions.",
 "Eine verbesserte Brooklyn mit 5-Zoll/38-Mehrzweckgeschützen. Das SG-Seeradar der USS Helena gab bei Kap Esperance die erste Warnung – der Wendepunkt des Radars im Nachtgefecht."),
"class.usn_dd_bagley.history": (
 "Destroyers with sixteen torpedo tubes. Blue and Ralph Talbot were the radar pickets that failed to detect Mikawa at Savo; Bagley and Patterson screened the southern group.",
 "Zerstörer mit sechzehn Torpedorohren. Blue und Ralph Talbot waren die Radarvorposten, die Mikawa bei Savo nicht entdeckten; Bagley und Patterson sicherten die Südgruppe."),
"class.usn_dd_benson.history": (
 "Benson/Gleaves-class destroyers formed many of the screens in the Solomons. Laffey fought at point-blank range against the battleship Hiei in November 1942.",
 "Zerstörer der Benson/Gleaves-Klasse bildeten viele Sicherungen bei den Salomonen. Die Laffey kämpfte im November 1942 auf kürzeste Distanz gegen das Schlachtschiff Hiei."),
"class.usn_dd_fletcher.history": (
 "The definitive US destroyer of the war: five 5-inch/38 guns, ten torpedo tubes and good radar. Fletcher herself fought at Guadalcanal and Tassafaronga.",
 "Der maßgebliche US-Zerstörer des Krieges: fünf 5-Zoll/38-Geschütze, zehn Torpedorohre und gutes Radar. Die Fletcher selbst kämpfte vor Guadalcanal und bei Tassafaronga."),
"class.usn_dd_mahan.history": (
 "Mid-1930s destroyers with twelve torpedo tubes. Cushing led the US column into the first naval battle of Guadalcanal.",
 "Zerstörer der Mitte der 1930er-Jahre mit zwölf Torpedorohren. Die Cushing führte die US-Kiellinie in die erste Seeschlacht von Guadalcanal."),
"class.usn_ap_transport.history": (
 "Converted liners and freighters that carried the Marines to Guadalcanal. Their protection was the reason the cruisers stood guard around Savo Island.",
 "Umgebaute Passagier- und Frachtschiffe, die die Marines nach Guadalcanal brachten. Ihr Schutz war der Grund, warum die Kreuzer um Savo Wache standen."),
}
