#pragma once
// The game's interface and flow: main menu (with a live 3D backdrop), mission selection, briefing,
// battle (3D view, tactical plot, command panels, AI insight), debrief, unit reference, scenario
// editor and settings. Portable: everything platform specific goes through UiServices.
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
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
    std::string flight;            // flight to select (name or localisation key, e.g. air.force_z.genzan1)
    double advance = 0;            // simulated seconds to run before the capture
    int frames = 30;               // frames to render before the capture
    double camYaw = -1, camPitch = -100, camDistance = -1;
    bool insight = false;          // open the AI insight window
    int insightFilter = 0;         // 0 own AI, 1 enemy AI, 2 all (enemy views need umpire mode: --side none)
    double pan = 0, lift = 0;      // unit reference: look-at point along the hull (+0.5 bow) and height
    bool large = false, night = false, drill = false;  // unit reference: large view, night lighting, weapons drill
    double drillTime = -1;         // unit reference: show the weapons drill at this time (s)
};

struct LogLine {
    double time = 0;
    std::string text;
    ImVec4 color;
    // What a click on the line brings into view.
    sim::EntityId ship = sim::kNoEntity;
    int flight = -1;
    bool hasPos = false;
    Vec2 pos;
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
    // Air command (UiAir.cpp)
    void drawAirList();
    void drawFlightPanel(sim::Flight& f);
    void flightMapMenu(sim::Flight& f, const MapOutput& ctx);
    bool issueFlightOrder(int flight, sim::AirOrder o);
    void issueMapOrder(const Vec2& p);
    void selectFlight(int id, bool follow = true);
    void cycleFlight(int dir);
    sim::Flight* selectedFlight();
    std::vector<const sim::AirTrack*> knownAirTracks() const;
    // Command helpers
    void shipMapMenu(sim::Ship& s, const MapOutput& ctx);
    void addWaypoint(const Vec2& p, bool append);
    void handleBattleKeys();
    void alert(const char* key, const ImVec4& color, double simTime);
    void drawAlertBanner();
    // Helpers
    sim::Ship* selected();
    void selectShip(sim::EntityId id, bool follow = true);
    void cycleShip(int dir);
    std::vector<const sim::Track*> knownHostiles() const;
    std::string trackName(const sim::Track& t) const;
    void addLog(const std::string& text, const ImVec4& color);
    void addLog(const LogLine& line);
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
    int selFlight_ = -1;          // selected own flight (the right panel shows it instead of the ship)
    uint32_t selAirTrack_ = 0;    // selected enemy air contact
    // An order waiting for a point on the plot.
    enum class MapOrder { None, Search, Recon, Cap, Flares, Waypoints };
    MapOrder mapOrder_ = MapOrder::None;
    int airAltitude_ = 0;         // 0 by role, 1 low, 2 medium, 3 high
    int launchDelayMin_ = 0;      // flights at their base: minutes until take-off
    std::string lastAirMsg_;
    MapOutput mapCtx_;            // what the plot's context menu was opened on
    std::string banner_;
    ImVec4 bannerColor_{1, 1, 1, 1};
    double bannerUntil_ = -1;     // real seconds (battleRealTime_)
    double lastAlertSim_ = -1e9;
    std::unordered_map<uint64_t, double> attackLogged_;  // ship x attack kind -> last log time
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
