#pragma once
// Scenario description (historical missions and editor-made battles), stored as JSON in
// data/scenarios/. Positions may be given geographically (lat/lon) or as local metres.
#include <optional>
#include <string>
#include <vector>

#include "ai/AiController.h"
#include "core/Json.h"
#include "sim/Environment.h"
#include "sim/Air.h"
#include "sim/Group.h"

namespace it::game {

struct ScenarioShip {
    std::string cls;
    std::string name;
    std::optional<sim::CrewQuality> crew;   // overrides group crew
    std::vector<std::string> sensors;       // overrides class sensors (period refits)
    bool fcRadar = true;                    // false: directors without fire-control radar
    bool torpedoReloads = true;             // false: reload torpedoes landed (deck cargo, e.g. supply drums)
    // Damage the ship starts with (an earlier action): health of propulsion and electrical power
    // (1 = intact) and water already in the hull (t).
    double propulsion = 1.0, power = 1.0, flooding = 0.0;
};

struct ScenarioGroup {
    std::string id, name;
    sim::Side side = sim::Side::Allies;
    sim::Formation formation = sim::Formation::Column;
    double spacing = 900;
    std::string doctrine;
    Vec2 pos;                  // local metres (resolved from lat/lon on load)
    double course = 0, speedKn = 15;
    sim::CrewQuality crew = sim::CrewQuality::Regular;
    double readiness = 1.0;    // 1 = battle stations; 0.3..0.5 = cruising watch (Condition II/III)
    std::vector<ScenarioShip> ships;
    ai::Mission mission;
    bool playable = true;      // can the player command it when playing this side
    double appearTime = 0;     // reinforcements (s after start)
    bool knownToEnemy = false; // the enemy knows these ships from intelligence: identified on first detection
    // raw geographic data kept for saving
    bool geo = false;
    double lat = 0, lon = 0;
};

/// An airfield (or seaplane base) the scenario's flights operate from; it may lie far off the map.
struct ScenarioAirBase {
    std::string id, name;
    sim::Side side = sim::Side::Allies;
    Vec2 pos;
    double turnaround = 2700;    // s to refuel and rearm
    double launchInterval = 25;  // s between take-offs
};

/// A flight of aircraft: parked at its base (launched by its order's start time, by the AI or by the
/// player) or already in the air at the start / at its appearance time.
struct ScenarioAirGroup {
    std::string id, name;
    sim::Side side = sim::Side::Allies;
    std::string base, aircraft, loadout;
    int count = 3;
    sim::CrewQuality crew = sim::CrewQuality::Regular;
    bool playable = true;
    bool airborne = false;
    Vec2 pos;
    double altitude = -1, heading = 0;
    double appearTime = 0;
    bool hasOrder = false;
    sim::AirOrder order;
    std::string coverGroup;      // CAP / spotting over this scenario group
    std::string escortFlight;    // escort this air group
};

/// Bombard: ships of `group` spend `value` seconds within `radius` of `area` (a shore bombardment);
/// PreventBombard: the enemy's `group` does not manage that.
enum class ObjectiveType : uint8_t { DestroyTonnage, DestroyGroup, ProtectGroup, Preserve, ExitArea, Survive, Bombard, PreventBombard };

struct Objective {
    std::string id;
    sim::Side side = sim::Side::Allies;
    ObjectiveType type = ObjectiveType::DestroyTonnage;
    double value = 0;          // tonnage / ratio / seconds
    std::string group;         // group id for group objectives
    Vec2 area;
    double radius = 5000;
    double points = 100;
    std::string text;          // localisation key
};

struct Scenario {
    std::string id;
    std::string title, briefing, history;  // localisation keys
    std::string theater = "pacific";
    double lat = -9.15, lon = 159.85;
    std::string terrain;                    // path relative to data/
    sim::DateTime date;
    sim::WeatherConfig weather;
    std::vector<sim::Side> playableSides{sim::Side::Allies, sim::Side::Axis};
    sim::Side defaultPlayer = sim::Side::Allies;
    double timeLimit = 7200;
    std::vector<ScenarioGroup> groups;
    std::vector<ScenarioAirBase> airBases;
    std::vector<ScenarioAirGroup> airGroups;
    std::vector<Objective> objectives;
    std::string sourceFile;
    bool hidden = false;  // internal scenarios (menu backdrop) are not listed

    const ScenarioGroup* group(const std::string& id) const;
};

bool loadScenario(const std::string& path, Scenario& out, std::string* error = nullptr);
bool saveScenario(const std::string& path, const Scenario& sc);
Json scenarioToJson(const Scenario& sc);
/// Scenario summaries for the mission list (sorted by date).
std::vector<Scenario> listScenarios(const std::string& dir);

}  // namespace it::game
