#pragma once
// Scenario description (historical missions and editor-made battles), stored as JSON in
// data/scenarios/. Positions may be given geographically (lat/lon) or as local metres.
#include <optional>
#include <string>
#include <vector>

#include "ai/AiController.h"
#include "core/Json.h"
#include "sim/Environment.h"
#include "sim/Group.h"

namespace it::game {

struct ScenarioShip {
    std::string cls;
    std::string name;
    std::optional<sim::CrewQuality> crew;   // overrides group crew
    std::vector<std::string> sensors;       // overrides class sensors (period refits)
    bool fcRadar = true;                    // false: directors without fire-control radar
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
    // raw geographic data kept for saving
    bool geo = false;
    double lat = 0, lon = 0;
};

enum class ObjectiveType : uint8_t { DestroyTonnage, DestroyGroup, ProtectGroup, Preserve, ExitArea, Survive };

struct Objective {
    std::string id;
    sim::Side side = sim::Side::Allies;
    ObjectiveType type = ObjectiveType::DestroyTonnage;
    double value = 0;          // tonnage / ratio
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
