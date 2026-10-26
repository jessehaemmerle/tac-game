#pragma once
// The game's interface and flow: main menu (with a live 3D backdrop), mission selection, briefing,
// battle (3D view, tactical plot, command panels, AI insight), debrief, unit reference, scenario
// editor and settings. Portable: everything platform specific goes through UiServices.
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "ai/Doctrine.h"
#include "game/Session.h"
#include "render/Camera.h"
#include "sim/Database.h"
#include "ui/Settings.h"
#include "ui/TacticalMap.h"
#include "ui/UiServices.h"

namespace it::ui {

class UnitReference;
class ScenarioEditor;

/// Scripted start-up for automated screenshots and smoke tests.
struct Automation {
    std::string screen;            // menu, missions, briefing, battle, map, split, unitref, editor, settings, debrief, credits
    std::string scenario = "savo_island";
    std::string side = "allies";   // allies, axis, none
    std::string unitClass;         // unit reference selection
    std::string focus;             // ship to select and follow
    double advance = 0;            // simulated seconds to run before the capture
    int frames = 30;               // frames to render before the capture
    double camYaw = -1, camPitch = -100, camDistance = -1;
    bool insight = false;          // open the AI insight window
};

struct LogLine {
    double time = 0;
    std::string text;
    ImVec4 color;
};

class GameUi {
public:
    enum class Screen { MainMenu, Missions, Briefing, Battle, Debrief, UnitReference, Editor, Settings, Credits };
    enum class View { Scene3D, Map, Split };

    GameUi(UiServices& svc, Settings& settings);
    ~GameUi();
    bool init(std::string* error);
    /// Advance the simulation and build this frame's interface (call between ImGui::NewFrame and Render).
    void frame(double realDt);

    // ---- platform hooks
    const sim::World* world() const;
    bool consumeWorldChanged();
    std::vector<sim::Event> takeEvents();
    double simDtLastFrame() const { return simDt_; }
    bool wants3D() const;
    const render::Camera& camera() const { return cam_; }
    sim::EntityId selectedShip() const { return selShip_; }
    sim::Side playerSide() const;
    bool paused() const;
    void startAutomation(const Automation& a);
    bool automationReady() const { return autoActive_ && autoFramesLeft_ <= 0 && autoStage_ >= 2; }
    void finishAutomation() { autoActive_ = false; }
    const sim::Database& db() const { return db_; }
    Screen screen() const { return screen_; }

    // ---- shared with the screen implementations
    UiServices& svc;
    Settings& settings;
    std::string settingsPath;

private:
    friend class UnitReference;
    friend class ScenarioEditor;
    // Flow
    void go(Screen s);
    void startBattle(const game::Scenario& sc, sim::Side player);
    void endBattle();
    void ensureBackdrop();
    void updateSession(double realDt);
    void updateCamera(double realDt);
    void handleSceneInput();
    void processEvents(const std::vector<sim::Event>& events);
    void runAutomation();
    // Screens
    void drawMainMenu();
    void drawMissions();
    void drawBriefing();
    void drawBattle();
    void drawDebrief();
    void drawSettings(bool inGame);
    void drawCredits();
    void drawPauseMenu();
    // Battle panels
    void drawTopBar();
    void drawFleetPanel(ImVec2 pos, ImVec2 size);
    void drawShipPanel(ImVec2 pos, ImVec2 size);
    void drawContactPanel();
    void drawMessageLog(ImVec2 pos, ImVec2 size);
    void drawContactsList(ImVec2 pos, ImVec2 size);
    void drawMapView(ImVec2 pos, ImVec2 size, bool compact);
    void drawAiInsight();
    void drawHelmTab(sim::Ship& s);
    void drawWeaponsTab(sim::Ship& s);
    void drawDamageTab(sim::Ship& s);
    void drawSensorsTab(sim::Ship& s);
    void drawSceneLabels();
    // Helpers
    sim::Ship* selected();
    void selectShip(sim::EntityId id, bool follow = true);
    void cycleShip(int dir);
    std::vector<const sim::Track*> knownHostiles() const;
    std::string trackName(const sim::Track& t) const;
    void addLog(const std::string& text, const ImVec4& color);
    void recordPlot();
    void backgroundPanel(const char* id, ImVec2 pos, ImVec2 size);

    // Data
    sim::Database db_;
    ai::DoctrineLibrary doctrines_;
    std::vector<game::Scenario> scenarios_;
    int selectedScenario_ = 0;
    sim::Side chosenSide_ = sim::Side::Allies;
    std::string chosenSideFor_;  // scenario id the side choice belongs to (a new mission starts from its default side)

    // Sessions
    std::unique_ptr<game::Session> session_;   // battle
    std::unique_ptr<game::Session> backdrop_;  // menu background
    bool worldChanged_ = false;
    std::vector<sim::Event> frameEvents_;
    double simDt_ = 0;
    double lastSimTime_ = 0;

    // State
    Screen screen_ = Screen::MainMenu;
    Screen settingsReturn_ = Screen::MainMenu;
    View view_ = View::Scene3D;
    bool pauseMenu_ = false;
    bool showInsight_ = false;
    bool showUnitRefInBattle_ = false;
    bool showSettingsInBattle_ = false;
    sim::EntityId selShip_ = sim::kNoEntity;
    uint32_t selTrack_ = 0;
    bool follow_ = true;
    int helmMode_ = 0;  // 0 = orders to the group, 1 = this ship only
    bool hideUi_ = false;
    int torpedoCount_ = 4, torpedoSetting_ = 0;
    float torpedoSpread_ = 6.0f;
    std::string lastTorpedoMsg_;
    std::deque<LogLine> log_;
    std::vector<MapTrack> plot_;
    std::unordered_map<sim::EntityId, size_t> plotIndex_;
    double lastPlotSample_ = -1e9;
    double battleRealTime_ = 0;
    bool debriefSaved_ = false;
    game::SideResult resultPlayer_, resultEnemy_;
    int insightFilter_ = 0;  // 0 own AI, 1 enemy AI (debrief / spoiler), 2 all
    int insightLevel_ = -1;

    // 3D camera
    render::Camera cam_;
    render::CameraController ctl_;
    double backdropTime_ = 0;
    Vec3 freeVel_;

    // Map
    TacticalMap map_, miniMap_, debriefMap_;
    bool mapFitted_ = false;

    // Sub-screens
    std::unique_ptr<UnitReference> unitRef_;
    std::unique_ptr<ScenarioEditor> editor_;

    // Automation
    Automation auto_;
    bool autoActive_ = false;
    int autoStage_ = 0;
    int autoFramesLeft_ = 0;
};

}  // namespace it::ui
