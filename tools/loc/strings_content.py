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
"ai.c.dispersion": ("Own formation dispersed", "Eigener Verband zerstreut"), "ai.c.enemy_close": ("Enemy within reach", "Gegner in Reichweite"), "ai.c.doctrine": ("Doctrine preference", "Doktrin-Vorgabe"),
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
# --- scenario: Cape Esperance
"scenario.cape_esperance.title": ("Battle of Cape Esperance", "Seeschlacht bei Kap Esperance"),
"scenario.cape_esperance.briefing": (
 "Night of 11/12 October 1942, off Cape Esperance, the north-western tip of Guadalcanal. To cover a convoy bringing Army reinforcements to the Marines, Rear Admiral Norman Scott's Task Group 64.2 - the heavy cruisers San Francisco and Salt Lake City, the light cruisers Boise and Helena and five destroyers - waits between Savo Island and the cape. Rear Admiral Aritomo Gotō is coming down 'the Slot' with the heavy cruisers Aoba, Furutaka and Kinugasa and two destroyers to shell Henderson Field, while a separate Japanese group lands troops and artillery further along the coast. The moon has set, the sky is overcast, the sea calm.",
 "Nacht vom 11. auf den 12. Oktober 1942 vor Kap Esperance, der Nordwestspitze von Guadalcanal. Zum Schutz eines Geleitzugs mit Heeresverstärkungen für die Marines wartet Konteradmiral Norman Scotts Task Group 64.2 – die Schweren Kreuzer San Francisco und Salt Lake City, die Leichten Kreuzer Boise und Helena sowie fünf Zerstörer – zwischen der Insel Savo und dem Kap. Konteradmiral Aritomo Gotō kommt mit den Schweren Kreuzern Aoba, Furutaka und Kinugasa und zwei Zerstörern den „Slot“ herunter, um Henderson Field zu beschießen, während eine zweite japanische Gruppe weiter östlich Truppen und Artillerie anlandet. Der Mond ist untergegangen, der Himmel bedeckt, die See ruhig."),
"scenario.cape_esperance.orders_allies": (
 "Intercept the Japanese force west of Savo Island before it can shell Henderson Field. Your crews have trained for exactly this night action and are at battle stations. Helena and Boise carry the new SG search radar - use it, but keep the column together: identification at night is uncertain and your own destroyers steam ahead of the cruisers.",
 "Fange den japanischen Verband westlich der Insel Savo ab, bevor er Henderson Field beschießen kann. Deine Besatzungen haben genau dieses Nachtgefecht geübt und sind auf Gefechtsstation. Helena und Boise tragen das neue SG-Suchradar – nutze es, aber halte die Kiellinie zusammen: Die Identifizierung bei Nacht ist unsicher, und die eigenen Zerstörer laufen vor den Kreuzern."),
"scenario.cape_esperance.orders_axis": (
 "Take the bombardment group past Savo Island, shell Henderson Field and withdraw to the north-west before dawn. No American surface ships are expected - they have not contested these waters at night since August. Your crews are at cruising stations, the guns trained fore and aft.",
 "Führe die Beschießungsgruppe an der Insel Savo vorbei, beschieße Henderson Field und ziehe dich vor Tagesanbruch nach Nordwesten zurück. Amerikanische Überwasserschiffe werden nicht erwartet – seit August haben sie diese Gewässer nachts nicht mehr umkämpft. Die Besatzungen gehen Marschwache, die Geschütze stehen in Längsrichtung."),
"scenario.cape_esperance.intel_allies": (
 "Search aircraft reported Japanese cruisers and destroyers coming down the Slot at high speed this afternoon. Expect them off Savo Island around midnight. Against cruisers the SG radar reaches about 25 km.",
 "Aufklärungsflugzeuge meldeten am Nachmittag japanische Kreuzer und Zerstörer, die mit hoher Fahrt den Slot herunterlaufen. Rechne gegen Mitternacht mit ihnen vor Savo. Gegen Kreuzer reicht das SG-Radar etwa 25 km."),
"scenario.cape_esperance.intel_axis": (
 "Air reconnaissance found only American cruisers and destroyers far to the south of Guadalcanal. Our own reinforcement group is unloading off the coast tonight - ships sighted near Savo may well be friendly.",
 "Die Luftaufklärung fand amerikanische Kreuzer und Zerstörer nur weit südlich von Guadalcanal. Unsere eigene Verstärkungsgruppe löscht heute Nacht vor der Küste – Schiffe in der Nähe von Savo können durchaus eigene sein."),
"scenario.cape_esperance.history": (
 "Helena's SG radar found Gotō's force at about 23:25, but the report reached Scott only as his column was countermarching, and the van destroyers ended up between the two forces. Even so the Americans crossed the Japanese 'T'. At 23:46 Helena opened fire without waiting for orders and the others followed. Gotō, convinced he was being fired on by friendly ships, turned away too late and was mortally wounded on Aoba's bridge. Furutaka and the destroyer Fubuki were sunk and Aoba badly damaged. The Americans lost Duncan, caught between both lines; Farenholt was hit by friendly shells, and Boise survived a magazine fire only because the sea flooding in through a shell hole put it out. It was the US Navy's first victory in a night surface action - yet the Japanese reinforcements were landed all the same.",
 "Helenas SG-Radar erfasste Gotōs Verband gegen 23:25 Uhr, doch die Meldung erreichte Scott erst, als seine Kiellinie gerade wendete, und die vorausgehenden Zerstörer gerieten zwischen die Verbände. Trotzdem überquerten die Amerikaner das japanische „T“. Um 23:46 Uhr eröffnete die Helena ohne Befehl das Feuer, die anderen folgten. Gotō, überzeugt, von eigenen Schiffen beschossen zu werden, drehte zu spät ab und wurde auf der Brücke der Aoba tödlich verwundet. Die Furutaka und der Zerstörer Fubuki sanken, die Aoba wurde schwer beschädigt. Die Amerikaner verloren die Duncan, die zwischen beide Linien geraten war; die Farenholt wurde von eigenen Granaten getroffen, und die Boise überstand einen Munitionskammerbrand nur, weil das durch ein Granatloch eindringende Wasser ihn löschte. Es war der erste Sieg der US Navy in einem nächtlichen Überwassergefecht – die japanischen Verstärkungen wurden dennoch angelandet."),
"objective.cape_esperance.stop": ("Stop the bombardment group", "Beschießungsgruppe stoppen"),
"objective.cape_esperance.sink": ("Sink enemy tonnage", "Gegnerische Tonnage versenken"),
"objective.cape_esperance.preserve": ("Preserve your own force", "Eigenen Verband erhalten"),
"group.esperance.scott": ("TG 64.2 cruisers (Scott)", "TG 64.2 Kreuzer (Scott)"),
"group.esperance.scott_van": ("Van destroyers", "Vorausgehende Zerstörer"),
"group.esperance.scott_rear": ("Rear destroyers", "Nachfolgende Zerstörer"),
"group.esperance.goto": ("Bombardment Group (Gotō)", "Beschießungsgruppe (Gotō)"),
"group.esperance.goto_screen": ("Destroyer screen", "Zerstörersicherung"),
# --- scenario: Tassafaronga
"scenario.tassafaronga.title": ("Battle of Tassafaronga", "Seeschlacht bei Tassafaronga"),
"scenario.tassafaronga.briefing": (
 "Night of 30 November 1942, off Tassafaronga Point on the north coast of Guadalcanal. Rear Admiral Raizō Tanaka's Destroyer Squadron 2 - eight destroyers - is running the 'Tokyo Express': six of them carry drums of food and supplies lashed on deck, to be dropped off the coast and hauled ashore by the starving garrison. To make room, the drum ships left their reload torpedoes behind. Rear Admiral Carleton Wright, in command of Task Force 67 for only two days, is coming in through Lengo Channel with four heavy cruisers, a light cruiser and six destroyers to stop them. The night is very dark and the sea calm; against the jungle-covered shore the Japanese ships are almost invisible.",
 "Nacht vom 30. November 1942 vor Tassafaronga Point an der Nordküste von Guadalcanal. Konteradmiral Raizō Tanakas 2. Zerstörergeschwader – acht Zerstörer – fährt den „Tokyo-Express“: Sechs Schiffe tragen an Deck gezurrte Fässer mit Lebensmitteln und Nachschub, die vor der Küste abgeworfen und von der hungernden Garnison an Land gezogen werden sollen. Um Platz zu schaffen, blieben die Reservetorpedos der Fass-Schiffe zurück. Konteradmiral Carleton Wright, erst seit zwei Tagen Befehlshaber der Task Force 67, läuft mit vier Schweren Kreuzern, einem Leichten Kreuzer und sechs Zerstörern durch den Lengo-Kanal heran, um sie aufzuhalten. Die Nacht ist sehr dunkel, die See ruhig; vor der dschungelbedeckten Küste sind die japanischen Schiffe kaum zu sehen."),
"scenario.tassafaronga.orders_axis": (
 "Reach the drop point off Tassafaronga, release the supply drums and withdraw to the north-west. Avoid action if you can - but if enemy cruisers appear, your crews know what to do: turn away, launch the Type 93s unseen and let the torpedoes do the work.",
 "Erreiche den Abwurfpunkt vor Tassafaronga, wirf die Nachschubfässer ab und ziehe dich nach Nordwesten zurück. Vermeide ein Gefecht, wenn möglich – doch wenn feindliche Kreuzer auftauchen, wissen deine Besatzungen, was zu tun ist: abdrehen, die Type 93 ungesehen ausstoßen und die Torpedos arbeiten lassen."),
"scenario.tassafaronga.orders_allies": (
 "Intercept the Tokyo Express off Tassafaronga and destroy it. The plan: the van destroyers attack first with radar-aimed torpedoes, then the cruisers open fire at about 11 km. Your SG radars see the enemy long before any lookout - the Japanese have no radar, but they have the most dangerous torpedoes afloat.",
 "Fange den Tokyo-Express vor Tassafaronga ab und vernichte ihn. Der Plan: Die vorausgehenden Zerstörer greifen zuerst mit radargeführten Torpedos an, dann eröffnen die Kreuzer auf etwa 11 km das Feuer. Deine SG-Radare sehen den Gegner lange vor jedem Ausguck – die Japaner haben kein Radar, aber die gefährlichsten Torpedos auf See."),
"scenario.tassafaronga.intel_allies": (
 "Coast watchers and radio intelligence predict a run of eight destroyers, possibly with transports, to Guadalcanal tonight. Expect them along the coast off Tassafaronga around 23:00.",
 "Küstenbeobachter und Funkaufklärung erwarten für heute Nacht einen Lauf von acht Zerstörern, möglicherweise mit Transportern, nach Guadalcanal. Rechne gegen 23:00 Uhr entlang der Küste vor Tassafaronga mit ihnen."),
"scenario.tassafaronga.intel_axis": (
 "American cruisers and destroyers have been seen in the area during the last days; their position tonight is unknown. Expect to be opposed. None of our ships carries radar.",
 "In den letzten Tagen wurden amerikanische Kreuzer und Zerstörer im Gebiet gesichtet; ihre Position heute Nacht ist unbekannt. Rechne mit Widerstand. Keines unserer Schiffe hat Radar."),
"scenario.tassafaronga.history": (
 "American radar picked up the Japanese at about 23:06, but Wright hesitated before releasing his destroyers' torpedoes; when they were finally fired the range had opened and none hit. The cruisers' gunfire fell mostly on Takanami, the nearest Japanese ship, which was wrecked and sunk. Tanaka's other destroyers turned away and launched more than forty torpedoes without firing a gun. Minutes later Minneapolis, New Orleans, Pensacola and Northampton were hit: Minneapolis and New Orleans lost their bows, Pensacola burned for hours, and Northampton sank after midnight. One Japanese destroyer lost against one heavy cruiser sunk and three crippled - Tassafaronga became a textbook example of the torpedo tactics of the IJN, even though no supplies were landed that night.",
 "Amerikanisches Radar erfasste die Japaner gegen 23:06 Uhr, doch Wright zögerte, die Torpedos seiner Zerstörer freizugeben; als sie schließlich liefen, war die Entfernung gewachsen, und keiner traf. Das Feuer der Kreuzer lag vor allem auf der Takanami, dem nächsten japanischen Schiff, das zusammengeschossen wurde und sank. Tanakas übrige Zerstörer drehten ab und stießen über vierzig Torpedos aus, ohne einen Schuss abzugeben. Minuten später wurden Minneapolis, New Orleans, Pensacola und Northampton getroffen: Minneapolis und New Orleans verloren ihren Bug, die Pensacola brannte stundenlang, die Northampton sank nach Mitternacht. Ein verlorener japanischer Zerstörer gegen einen versenkten und drei schwer beschädigte Schwere Kreuzer – Tassafaronga wurde zum Lehrbuchbeispiel japanischer Torpedotaktik, obwohl in dieser Nacht kein Nachschub an Land kam."),
"objective.tassafaronga.stop": ("Stop the Tokyo Express", "Den Tokyo-Express stoppen"),
"objective.tassafaronga.sink": ("Sink enemy tonnage", "Gegnerische Tonnage versenken"),
"objective.tassafaronga.preserve": ("Preserve your own force", "Eigenen Verband erhalten"),
"group.tassafaronga.wright": ("TF 67 cruisers (Wright)", "TF 67 Kreuzer (Wright)"),
"group.tassafaronga.wright_van": ("Van destroyers", "Vorausgehende Zerstörer"),
"group.tassafaronga.wright_rear": ("Rear destroyers", "Nachfolgende Zerstörer"),
"group.tassafaronga.tanaka": ("Destroyer Squadron 2 (Tanaka)", "2. Zerstörergeschwader (Tanaka)"),
"group.tassafaronga.takanami": ("Takanami (screen)", "Takanami (Sicherung)"),
# --- scenario: First Naval Battle of Guadalcanal
"scenario.first_guadalcanal.title": ("First Naval Battle of Guadalcanal", "Erste Seeschlacht von Guadalcanal"),
"scenario.first_guadalcanal.briefing": (
 "Friday, 13 November 1942, 01:25, Ironbottom Sound. Vice Admiral Hiroaki Abe is coming down past Savo Island with the battleships Hiei and Kirishima, the light cruiser Nagara and eleven destroyers. His magazines are topped with incendiary shells: tonight he is to burn Henderson Field, so that a convoy of eleven transports can land 7,000 soldiers tomorrow. In his way stands Rear Admiral Daniel Callaghan with what was left after escorting the day's supply ships: two heavy and three light cruisers and eight destroyers in one long column. There are no American battleships within reach. The night is moonless, rain squalls hang over the sound - and neither side quite knows where the other is.",
 "Freitag, 13. November 1942, 01:25 Uhr, Ironbottom Sound. Vizeadmiral Hiroaki Abe läuft mit den Schlachtschiffen Hiei und Kirishima, dem Leichten Kreuzer Nagara und elf Zerstörern an der Insel Savo vorbei nach Süden. Oben in seinen Munitionskammern liegen Brandgranaten: Heute Nacht soll er Henderson Field in Flammen setzen, damit morgen ein Geleit aus elf Transportern 7.000 Soldaten anlanden kann. Ihm im Weg steht Konteradmiral Daniel Callaghan mit dem, was nach der Sicherung der Nachschubschiffe des Tages übrig ist: zwei Schwere und drei Leichte Kreuzer und acht Zerstörer in einer einzigen langen Kiellinie. Amerikanische Schlachtschiffe sind nicht in Reichweite. Die Nacht ist mondlos, Regenböen hängen über dem Sund – und keine Seite weiß genau, wo die andere steht."),
"scenario.first_guadalcanal.orders_allies": (
 "Stop the bombardment force before it reaches Lunga Point. Henderson Field must not be shelled tonight - whatever it costs. You are outgunned: the enemy battleships carry 14-inch guns, your heaviest are 8-inch. Close the range, fight at night where their guns matter less, and use your destroyers' torpedoes. Helena, O'Bannon and Fletcher carry the new SG radar - the flagship San Francisco does not.",
 "Halte den Beschießungsverband auf, bevor er Lunga Point erreicht. Henderson Field darf heute Nacht nicht beschossen werden – koste es, was es wolle. Du bist unterlegen: Die feindlichen Schlachtschiffe führen 35,6-cm-Geschütze, deine schwersten sind 20,3 cm. Verkürze die Entfernung, kämpfe in der Nacht, in der ihre Geschütze weniger zählen, und setze die Torpedos deiner Zerstörer ein. Helena, O'Bannon und Fletcher tragen das neue SG-Radar – das Flaggschiff San Francisco nicht."),
"scenario.first_guadalcanal.orders_axis": (
 "Take the raiding group past Savo Island to Lunga Point and bombard Henderson Field: the battleships must stay at least ten minutes within 7 km of the airfield. Withdraw to the north-west by 03:00 at the latest. If American warships bar the way, the destroyers attack with torpedoes first, as the doctrine demands.",
 "Führe die Angriffsgruppe an der Insel Savo vorbei nach Lunga Point und beschieße Henderson Field: Die Schlachtschiffe müssen mindestens zehn Minuten innerhalb von 7 km um den Flugplatz bleiben. Ziehe dich spätestens um 03:00 Uhr nach Nordwesten zurück. Versperren amerikanische Kriegsschiffe den Weg, greifen die Zerstörer nach Doktrin zuerst mit Torpedos an."),
"scenario.first_guadalcanal.intel_allies": (
 "Search planes reported two battleships or heavy cruisers, a cruiser and up to a dozen destroyers coming down the Slot this afternoon. Expect them off Savo Island after midnight, heading for Lunga Point.",
 "Aufklärungsflugzeuge meldeten heute Nachmittag zwei Schlachtschiffe oder Schwere Kreuzer, einen Kreuzer und bis zu einem Dutzend Zerstörer, die den „Slot“ herunterlaufen. Rechne nach Mitternacht vor der Insel Savo mit ihnen, Kurs Lunga Point."),
"scenario.first_guadalcanal.intel_axis": (
 "During the day American transports unloaded off Lunga Point, covered by cruisers and destroyers. The transports have left; whether their escort is still in the sound is unknown. Our lookouts will have to find them - none of our ships carries radar.",
 "Tagsüber haben amerikanische Transporter vor Lunga Point gelöscht, gedeckt von Kreuzern und Zerstörern. Die Transporter sind abgelaufen; ob ihre Sicherung noch im Sund steht, ist unbekannt. Unsere Ausgucke müssen sie finden – keines unserer Schiffe hat Radar."),
"scenario.first_guadalcanal.history": (
 "Helena's radar found the Japanese at 01:24, but the American column ran on while its admiral asked for bearings over a crowded radio net. The two forces blundered into each other; at 01:50 Akatsuki switched on her searchlight and the most confused surface action of the war began at ranges of a few hundred metres. Atlanta was shot to pieces and torpedoed, Admiral Scott killed on her bridge; San Francisco took 14-inch shells and Callaghan died with his staff. Hiei, hit by some 85 shells, lost her steering and circled north of Savo - aircraft from Henderson Field sank her the next day. The Americans lost Atlanta, Juneau (torpedoed by a submarine the next morning) and four destroyers; the Japanese lost Hiei, Akatsuki and Yudachi. Abe turned back without firing a shell at the airfield, and the convoy's landing had to be postponed.",
 "Das Radar der Helena erfasste die Japaner um 01:24 Uhr, doch die amerikanische Kiellinie lief weiter, während ihr Admiral über ein überlastetes Funknetz nach Peilungen fragte. Die beiden Verbände liefen ineinander; um 01:50 Uhr schaltete die Akatsuki ihren Scheinwerfer ein, und auf wenige hundert Meter begann das verworrenste Überwassergefecht des Krieges. Die Atlanta wurde zusammengeschossen und torpediert, Admiral Scott fiel auf ihrer Brücke; die San Francisco erhielt 35,6-cm-Treffer, Callaghan starb mit seinem Stab. Die Hiei, von rund 85 Granaten getroffen, verlor ihre Ruderanlage und drehte nördlich von Savo Kreise – Flugzeuge von Henderson Field versenkten sie am nächsten Tag. Die Amerikaner verloren die Atlanta, die Juneau (am nächsten Morgen von einem U-Boot torpediert) und vier Zerstörer; die Japaner die Hiei, die Akatsuki und die Yudachi. Abe drehte ab, ohne eine einzige Granate auf den Flugplatz zu feuern, und die Landung des Geleits musste verschoben werden."),
"objective.first_guadalcanal.airfield": ("Keep the bombardment force away from Henderson Field", "Beschießungsverband von Henderson Field fernhalten"),
"objective.first_guadalcanal.bombard": ("Bombard Henderson Field (10 min within 7 km)", "Henderson Field beschießen (10 min innerhalb 7 km)"),
"objective.first_guadalcanal.sink": ("Sink enemy tonnage", "Gegnerische Tonnage versenken"),
"objective.first_guadalcanal.preserve": ("Preserve your own force", "Eigenen Verband erhalten"),
"group.first_guadalcanal.callaghan": ("TG 67.4 cruisers (Callaghan)", "TG 67.4 Kreuzer (Callaghan)"),
"group.first_guadalcanal.callaghan_van": ("Van destroyers", "Vorausgehende Zerstörer"),
"group.first_guadalcanal.callaghan_rear": ("Rear destroyers", "Nachfolgende Zerstörer"),
"group.first_guadalcanal.abe": ("Raiding group battleships (Abe)", "Schlachtschiffe der Angriffsgruppe (Abe)"),
"group.first_guadalcanal.abe_screen": ("Close screen (Nagara)", "Nahsicherung (Nagara)"),
"group.first_guadalcanal.abe_van": ("Advance destroyers (Yudachi)", "Vorgeschobene Zerstörer (Yudachi)"),
"group.first_guadalcanal.abe_right": ("6th Destroyer Division", "6. Zerstörerdivision"),
"group.first_guadalcanal.abe_rear": ("Rear destroyers (Murasame)", "Nachfolgende Zerstörer (Murasame)"),
# --- scenario: Second Naval Battle of Guadalcanal
"scenario.second_guadalcanal.title": ("Second Naval Battle of Guadalcanal", "Zweite Seeschlacht von Guadalcanal"),
"scenario.second_guadalcanal.briefing": (
 "Night of 14/15 November 1942, 22:55, south of Savo Island. Two nights after Abe's failure, Vice Admiral Nobutake Kondō is bringing the battleship Kirishima, the heavy cruisers Atago and Takao, two light cruisers and nine destroyers to finish the job: Henderson Field is to be shelled so that Tanaka's battered transports can beach at dawn. This time the Americans have battleships of their own. Rear Admiral Willis Lee, the Navy's foremost radar expert, has brought Washington and South Dakota - new, fast and armed with 16-inch guns - and four destroyers scraped together from different units. Lee's ships have rounded Savo and are steaming west, south of the island. A light breeze, a first-quarter moon low in the west, scattered showers.",
 "Nacht vom 14. auf den 15. November 1942, 22:55 Uhr, südlich der Insel Savo. Zwei Nächte nach Abes Misserfolg führt Vizeadmiral Nobutake Kondō das Schlachtschiff Kirishima, die Schweren Kreuzer Atago und Takao, zwei Leichte Kreuzer und neun Zerstörer heran, um die Aufgabe zu Ende zu bringen: Henderson Field soll beschossen werden, damit Tanakas angeschlagene Transporter im Morgengrauen auf den Strand laufen können. Diesmal haben die Amerikaner eigene Schlachtschiffe. Konteradmiral Willis Lee, der führende Radarexperte der Navy, bringt die Washington und die South Dakota – neu, schnell und mit 40,6-cm-Geschützen bewaffnet – und vier aus verschiedenen Verbänden zusammengezogene Zerstörer. Lees Schiffe haben Savo umrundet und laufen südlich der Insel nach Westen. Leichte Brise, ein zunehmender Halbmond tief im Westen, vereinzelte Schauer."),
"scenario.second_guadalcanal.orders_allies": (
 "Stop Kondo's bombardment force. Your battleships' radar sees far beyond any Japanese lookout, and your 16-inch guns outrange everything they have - but fire at night by radar alone is still guesswork at long range, and in these narrow waters their destroyers can come at you out of the shadow of Savo Island with torpedoes you cannot see. Keep the destroyers ahead as a screen and do not let the battleships be ambushed.",
 "Halte Kondōs Beschießungsverband auf. Das Radar deiner Schlachtschiffe sieht weit über jeden japanischen Ausguck hinaus, und deine 40,6-cm-Geschütze reichen weiter als alles, was der Gegner hat – doch reines Radarschießen bei Nacht bleibt auf große Entfernung Glückssache, und in diesen engen Gewässern können seine Zerstörer aus dem Schatten der Insel Savo mit Torpedos angreifen, die du nicht siehst. Halte die Zerstörer als Sicherung voraus und lass die Schlachtschiffe nicht in einen Hinterhalt laufen."),
"scenario.second_guadalcanal.orders_axis": (
 "Bombard Henderson Field with the main body: Kirishima, Atago and Takao must stay ten minutes within 7 km of the airfield; withdraw by 01:30. Sendai's sweeping unit scouts east of Savo and reports; Nagara's screening unit goes round the west of the island and clears the way. Any enemy found is attacked with torpedoes first.",
 "Beschieße Henderson Field mit der Hauptgruppe: Kirishima, Atago und Takao müssen zehn Minuten innerhalb von 7 km um den Flugplatz bleiben; Rückzug bis 01:30 Uhr. Die Säuberungsgruppe der Sendai klärt östlich von Savo auf und meldet; die Sicherungsgruppe der Nagara läuft westlich um die Insel herum und bahnt den Weg. Jeder gefundene Gegner wird zuerst mit Torpedos angegriffen."),
"scenario.second_guadalcanal.intel_allies": (
 "Submarines and search planes have tracked a force of battleships or cruisers and many destroyers coming south all afternoon. Expect it off Savo Island before midnight. The transports Tanaka is escorting are still north of the island.",
 "U-Boote und Aufklärungsflugzeuge haben den ganzen Nachmittag einen Verband aus Schlachtschiffen oder Kreuzern und vielen Zerstörern auf Südkurs verfolgt. Rechne vor Mitternacht vor der Insel Savo mit ihm. Die von Tanaka geleiteten Transporter stehen noch nördlich der Insel."),
"scenario.second_guadalcanal.intel_axis": (
 "Air reconnaissance this afternoon reported two cruisers and four destroyers south of Guadalcanal, steaming north. They may be in the sound tonight. (The pilots were wrong: the 'cruisers' were battleships.)",
 "Die Luftaufklärung meldete heute Nachmittag zwei Kreuzer und vier Zerstörer südlich von Guadalcanal auf Nordkurs. Sie könnten heute Nacht im Sund stehen. (Die Flieger irrten sich: Die „Kreuzer“ waren Schlachtschiffe.)"),
"scenario.second_guadalcanal.history": (
 "Washington's radar found Sendai at 23:00, and at 23:17 both battleships opened fire at 15 km - Sendai turned away behind smoke without a hit. Minutes later the American destroyers met the Japanese screen coming round Savo: against the dark island they were seen first, and Japanese torpedoes and gunfire sank Walke and Preston, crippled Benham and set Gwin on fire. South Dakota lost all electrical power at the worst moment, stumbled towards the Japanese main body and was caught in their searchlights; 27 shells wrecked her upper works. Washington, still unseen, opened fire on Kirishima at 7.7 km around midnight - nine 16-inch hits left the Japanese battleship burning and unmanageable, and she was scuttled at 03:25. Dozens of Long Lances fired at Washington all missed. Kondo withdrew, Henderson Field was not shelled, and the transports that beached at Tassafaronga at dawn were destroyed from the air. It was the last time the Japanese tried to win Guadalcanal with their battleships.",
 "Das Radar der Washington erfasste die Sendai um 23:00 Uhr, um 23:17 Uhr eröffneten beide Schlachtschiffe auf 15 km das Feuer – die Sendai drehte hinter einer Nebelwand ab, ohne getroffen zu werden. Minuten später stießen die amerikanischen Zerstörer auf die japanische Sicherung, die um Savo herumkam: Vor der dunklen Insel wurden sie zuerst gesehen, japanische Torpedos und Granaten versenkten die Walke und die Preston, setzten die Benham außer Gefecht und die Gwin in Brand. Die South Dakota verlor im ungünstigsten Moment die gesamte Stromversorgung, geriet an die japanische Hauptgruppe und in deren Scheinwerfer; 27 Treffer zerschlugen ihre Aufbauten. Die noch unentdeckte Washington eröffnete gegen Mitternacht auf 7,7 km das Feuer auf die Kirishima – neun 40,6-cm-Treffer ließen das japanische Schlachtschiff brennend und manövrierunfähig zurück, um 03:25 Uhr wurde es selbstversenkt. Dutzende auf die Washington abgefeuerte Long Lances verfehlten alle ihr Ziel. Kondō zog sich zurück, Henderson Field wurde nicht beschossen, und die Transporter, die im Morgengrauen bei Tassafaronga auf den Strand liefen, wurden aus der Luft zerstört. Es war der letzte Versuch der Japaner, Guadalcanal mit ihren Schlachtschiffen zu gewinnen."),
"objective.second_guadalcanal.airfield": ("Keep the bombardment force away from Henderson Field", "Beschießungsverband von Henderson Field fernhalten"),
"objective.second_guadalcanal.bombard": ("Bombard Henderson Field (10 min within 7 km)", "Henderson Field beschießen (10 min innerhalb 7 km)"),
"objective.second_guadalcanal.sink": ("Sink enemy tonnage", "Gegnerische Tonnage versenken"),
"objective.second_guadalcanal.preserve": ("Preserve your own force", "Eigenen Verband erhalten"),
"group.second_guadalcanal.lee": ("TF 64 battleships (Lee)", "TF 64 Schlachtschiffe (Lee)"),
"group.second_guadalcanal.lee_van": ("TF 64 destroyers", "TF 64 Zerstörer"),
"group.second_guadalcanal.kondo": ("Bombardment group (Kondo)", "Beschießungsgruppe (Kondō)"),
"group.second_guadalcanal.kondo_screen": ("Main body screen", "Sicherung der Hauptgruppe"),
"group.second_guadalcanal.sendai": ("Sweeping unit (Sendai)", "Säuberungsgruppe (Sendai)"),
"group.second_guadalcanal.nagara": ("Screening unit (Nagara)", "Sicherungsgruppe (Nagara)"),
"group.second_guadalcanal.ayanami": ("Ayanami (advance)", "Ayanami (vorgeschoben)"),
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
"class.usn_bb_north_carolina.history": (
 "The first US battleships built after the naval treaties, armed with nine 16-inch guns and twenty 5-inch/38s. On the night of 14/15 November 1942 Washington, guided by her SG radar, wrecked the battleship Kirishima in seven minutes at about 8,000 yards - the only time a US battleship sank an enemy battleship in a gun duel.",
 "Die ersten nach den Flottenverträgen gebauten US-Schlachtschiffe, bewaffnet mit neun 16-Zoll- und zwanzig 5-Zoll/38-Geschützen. In der Nacht vom 14. auf den 15. November 1942 zerschoss die Washington, geführt von ihrem SG-Radar, das Schlachtschiff Kirishima in sieben Minuten auf etwa 7.300 m – das einzige Mal, dass ein US-Schlachtschiff ein gegnerisches im Artillerieduell versenkte."),
"class.usn_bb_north_carolina.tactics": (
 "Radar-directed main battery outranges anything the enemy can show at night. Keep the destroyers ahead as a screen and open fire before the enemy's searchlights find you.",
 "Die radargeleitete Hauptartillerie übertrifft nachts alles, was der Gegner aufbieten kann. Die Zerstörer als Sicherung vorausschicken und das Feuer eröffnen, bevor feindliche Scheinwerfer das Schiff finden."),
"class.usn_bb_south_dakota.history": (
 "A shorter, more heavily protected design with a single funnel. At the Second Naval Battle of Guadalcanal South Dakota lost all electrical power at the critical moment, was illuminated and hit 26 times - but survived while Washington destroyed Kirishima.",
 "Ein kürzerer, stärker gepanzerter Entwurf mit nur einem Schornstein. In der zweiten Seeschlacht von Guadalcanal fiel auf der South Dakota im entscheidenden Moment der gesamte Strom aus; sie wurde angeleuchtet und 26-mal getroffen – überlebte aber, während die Washington die Kirishima vernichtete."),
"class.ijn_bb_kongo.history": (
 "Designed in Britain as battlecruisers in 1911 and twice rebuilt into fast battleships. On 13 November 1942 Hiei, sent to bombard Henderson Field with incendiary shells, ran into a US cruiser force at point-blank range; crippled, she was sunk by aircraft the next day. Kirishima was lost to Washington two nights later.",
 "1911 in Großbritannien als Schlachtkreuzer entworfen und zweimal zu schnellen Schlachtschiffen umgebaut. Am 13. November 1942 geriet die Hiei, die Henderson Field mit Brandgranaten beschießen sollte, auf kürzeste Distanz in einen US-Kreuzerverband; schwer beschädigt wurde sie am nächsten Tag von Flugzeugen versenkt. Die Kirishima ging zwei Nächte später gegen die Washington verloren."),
"class.ijn_bb_kongo.tactics": (
 "Fast and heavily armed but thinly armoured for a battleship. Bombardment shells loaded for a shore target are of little use against ships - change to armour-piercing early.",
 "Schnell und schwer bewaffnet, für ein Schlachtschiff aber dünn gepanzert. Für ein Landziel geladene Bombardierungsgranaten nützen gegen Schiffe wenig – früh auf Panzersprenggranaten umladen."),
"class.usn_cl_atlanta.history": (
 "Built as anti-aircraft cruisers and destroyer leaders, with sixteen 5-inch/38 guns and torpedo tubes but very light armour. Atlanta was wrecked by a torpedo and by gunfire - partly from San Francisco - at the First Naval Battle of Guadalcanal; Juneau was sunk by a submarine as she withdrew.",
 "Als Flakkreuzer und Zerstörerführer gebaut, mit sechzehn 5-Zoll/38-Geschützen und Torpedorohren, aber sehr leichter Panzerung. Die Atlanta wurde in der ersten Seeschlacht von Guadalcanal von einem Torpedo und Geschützfeuer – teils von der San Francisco – zerstört; die Juneau wurde beim Rückzug von einem U-Boot versenkt."),
"class.usn_ca_portland.history": (
 "Two improved Northamptons. Portland lost her stern steering to a torpedo off Guadalcanal on 13 November 1942, circled helplessly - and still fired six salvos into the crippled Hiei.",
 "Zwei verbesserte Northamptons. Die Portland verlor am 13. November 1942 vor Guadalcanal durch einen Torpedo die Steuerfähigkeit, drehte hilflos im Kreis – und schoss dennoch sechs Salven auf die angeschlagene Hiei."),
"class.ijn_cl_nagara.history": (
 "The 5,500-ton light cruisers led the destroyer squadrons. Nagara led Abe's screen at the First Naval Battle of Guadalcanal and Kondo's at the second.",
 "Die Leichten Kreuzer der 5.500-Tonnen-Klasse führten die Zerstörergeschwader. Die Nagara führte die Sicherung Abes in der ersten und die Kondos in der zweiten Seeschlacht von Guadalcanal."),
"class.ijn_cl_sendai.history": (
 "The last and most refined of the 5,500-ton cruisers, recognisable by their four funnels. Sendai led the sweeping force around Savo Island on 14 November 1942.",
 "Die letzten und ausgereiftesten Kreuzer der 5.500-Tonnen-Klasse, erkennbar an ihren vier Schornsteinen. Die Sendai führte am 14. November 1942 die Vorausgruppe um Savo."),
"class.ijn_dd_akatsuki.history": (
 "The last group of the Special Type destroyers. Akatsuki illuminated Atlanta with her searchlight at the start of the First Naval Battle of Guadalcanal and was sunk within minutes.",
 "Die letzte Gruppe der Spezialtyp-Zerstörer. Die Akatsuki leuchtete zu Beginn der ersten Seeschlacht von Guadalcanal die Atlanta mit ihrem Scheinwerfer an und wurde binnen Minuten versenkt."),
"class.ijn_dd_asashio.history": (
 "The first destroyers built after the treaty limits fell away, forerunners of the Kagero class. Asagumo fought in both naval battles of Guadalcanal in November 1942.",
 "Die ersten Zerstörer nach dem Wegfall der Vertragsgrenzen, Vorläufer der Kagero-Klasse. Die Asagumo kämpfte im November 1942 in beiden Seeschlachten von Guadalcanal."),
"class.ijn_dd_akizuki.history": (
 "Large destroyers built around eight 10 cm high-velocity dual-purpose guns to escort carriers. Teruzuki was brand new at the First Naval Battle of Guadalcanal and was sunk by a PT boat a month later.",
 "Große Zerstörer, gebaut um acht 10-cm-Mehrzweckgeschütze mit hoher Mündungsgeschwindigkeit zum Schutz der Träger. Die Teruzuki war in der ersten Seeschlacht von Guadalcanal fabrikneu und wurde einen Monat später von einem Schnellboot versenkt."),
}
