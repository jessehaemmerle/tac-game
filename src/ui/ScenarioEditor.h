#pragma once
// Scenario editor: place and configure groups (ships, formation, crew, readiness, doctrine, mission
// and waypoints), set date/time, weather and terrain, then save to the user folder or play-test.
#include <string>

#include "game/Scenario.h"
#include "sim/Terrain.h"
#include "ui/TacticalMap.h"

namespace it::ui {

class GameUi;

class ScenarioEditor {
public:
    explicit ScenarioEditor(GameUi& ui) : ui_(ui) {}
    void draw();
    void newScenario();
    void load(const game::Scenario& sc);

private:
    void ensureTerrain();
    void drawProperties();
    void drawGroup(game::ScenarioGroup& g);
    void addGroup(sim::Side side, const Vec2& pos);
    bool save();

    GameUi& ui_;
    game::Scenario sc_;
    int selGroup_ = -1;
    int selWaypointMode_ = 0;  // 0 none, 1 add waypoints with right click
    TacticalMap map_;
    sim::Terrain terrain_;
    std::string loadedTerrain_ = "<none>";
    bool fitted_ = false;
    std::string status_;
    char idBuf_[64] = {}, titleBuf_[128] = {};
};

}  // namespace it::ui
