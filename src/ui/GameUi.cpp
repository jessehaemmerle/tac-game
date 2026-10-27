// GameUi core: data loading, screen flow, sessions (battle + menu backdrop), camera, input, automation.
#include "ui/GameUi.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "core/FileSystem.h"
#include "core/Localization.h"
#include "core/Log.h"
#include "ui/Format.h"
#include "ui/ScenarioEditor.h"
#include "ui/Theme.h"
#include "ui/UnitReference.h"
#include "ui/Widgets.h"

namespace it::ui {

GameUi::GameUi(UiServices& s, Settings& st) : svc(s), settings(st) {}
GameUi::~GameUi() = default;

bool GameUi::init(std::string* error) {
    loc::load(settings.language);
    if (!db_.loadFromDirectory(fs::dataPath("db").string(), error)) return false;
    auto problems = db_.validate();
    for (auto& p : problems) IT_LOG_WARN("Database: %s", p.c_str());
    doctrines_.load(fs::dataPath("doctrine").string());
    auto list = game::listScenarios(fs::dataPath("scenarios").string());
    auto user = game::listScenarios((fs::userDataDir() / "scenarios").string());
    list.insert(list.end(), user.begin(), user.end());
    for (auto& sc : list)
        if (!sc.hidden) scenarios_.push_back(sc);
    unitRef_ = std::make_unique<UnitReference>(*this);
    editor_ = std::make_unique<ScenarioEditor>(*this);
    ensureBackdrop();
    IT_LOG_INFO("UI ready: %zu ship classes, %zu scenarios", db_.shipClasses().size(), scenarios_.size());
    return true;
}

// ------------------------------------------------------------------------------------------------ flow
void GameUi::go(Screen s) {
    if (s == screen_) return;
    Screen prev = screen_;
    screen_ = s;
    pauseMenu_ = false;
    bool battleWorld = s == Screen::Battle || s == Screen::Debrief;
    bool prevBattleWorld = prev == Screen::Battle || prev == Screen::Debrief;
    if (battleWorld != prevBattleWorld) worldChanged_ = true;
    if (!battleWorld) ensureBackdrop();
    svc.playUiSound("click");
}

void GameUi::ensureBackdrop() {
    if (backdrop_ && !backdrop_->finished()) return;
    game::Scenario sc;
    std::string err;
    if (!game::loadScenario(fs::dataPath("scenarios/menu_backdrop.json").string(), sc, &err)) {
        IT_LOG_WARN("Menu backdrop: %s", err.c_str());
        backdrop_.reset();
        return;
    }
    backdrop_ = std::make_unique<game::Session>(db_, doctrines_);
    game::SessionOptions opt;
    opt.player = sim::Side::Neutral;
    opt.seed = 7;
    if (!backdrop_->start(sc, opt, &err)) {
        backdrop_.reset();
        return;
    }
    // Let the column settle into formation before it is shown.
    for (int i = 0; i < 400; ++i) backdrop_->step();
    backdrop_->drainEvents();
    worldChanged_ = true;
    ctl_.mode = render::CameraMode::Orbit;
    ctl_.distance = 520;
    ctl_.orbitPitch = -7;
    ctl_.orbitYaw = 300;
}

void GameUi::startBattle(const game::Scenario& sc, sim::Side player) {
    session_ = std::make_unique<game::Session>(db_, doctrines_);
    game::SessionOptions opt;
    opt.player = player;
    opt.aiSkill = settings.aiSkill;
    opt.enemyCrewShift = settings.enemyCrewShift;
    opt.autoEvade = settings.autoEvade;
    opt.seed = autoActive_ ? 1 : uint64_t(std::time(nullptr)) & 0xffffff;  // scripted captures are reproducible
    std::string err;
    if (!session_->start(sc, opt, &err)) {
        IT_LOG_ERROR("Cannot start scenario %s: %s", sc.id.c_str(), err.c_str());
        session_.reset();
        return;
    }
    log_.clear();
    plot_.clear();
    plotIndex_.clear();
    lastPlotSample_ = -1e9;
    map_.resetHistory();
    miniMap_.resetHistory();
    mapFitted_ = false;
    selShip_ = sim::kNoEntity;
    selTrack_ = 0;
    selFlight_ = -1;
    selAirTrack_ = 0;
    mapOrder_ = MapOrder::None;
    lastAirMsg_.clear();
    bannerUntil_ = -1;
    lastAlertSim_ = -1e9;
    attackLogged_.clear();
    battleRealTime_ = 0;
    debriefSaved_ = false;
    view_ = View::Scene3D;
    // Select the first own ship (flagship of the first playable group).
    for (auto& s : session_->world().ships())
        if (s->side == player) {
            selectShip(s->id);
            break;
        }
    if (player == sim::Side::Neutral && !session_->world().ships().empty()) selectShip(session_->world().ships().front()->id);
    // A side fighting with aircraft alone starts with its first flight in the air.
    if (selShip_ == sim::kNoEntity)
        for (const auto& f : session_->world().flights())
            if (f.side == player && f.airborne()) {
                selectFlight(f.id);
                break;
            }
    ctl_.mode = render::CameraMode::Orbit;
    ctl_.distance = 420;
    ctl_.orbitPitch = -9;
    ctl_.orbitYaw = 210;
    follow_ = true;
    session_->paused = player != sim::Side::Neutral;  // start paused so the player can look around
    addLog(loc::format("msg.battle_start", {tr(sc.title.c_str())}), col::kBrass);
    if (session_->paused) addLog(tr("msg.paused_hint"), col::kTextDim);
    screen_ = Screen::Battle;
    worldChanged_ = true;
}

void GameUi::endBattle() {
    if (!session_) return;
    sim::Side p = playerSide() == sim::Side::Neutral ? sim::Side::Allies : playerSide();
    resultPlayer_ = session_->result(p);
    resultEnemy_ = session_->result(sim::enemyOf(p));
    recordPlot();
    session_->paused = true;
    go(Screen::Debrief);
}

const sim::World* GameUi::world() const {
    if ((screen_ == Screen::Battle || screen_ == Screen::Debrief) && session_) return &session_->world();
    if (screen_ == Screen::Battle || screen_ == Screen::Debrief) return nullptr;
    return backdrop_ ? &backdrop_->world() : nullptr;
}

bool GameUi::consumeWorldChanged() {
    bool c = worldChanged_;
    worldChanged_ = false;
    return c;
}

std::vector<sim::Event> GameUi::takeEvents() { return std::move(frameEvents_); }

bool GameUi::wants3D() const {
    switch (screen_) {
        case Screen::Battle: return view_ != View::Map && !showUnitRefInBattle_;
        case Screen::MainMenu:
        case Screen::Missions:
        case Screen::Briefing:
        case Screen::Settings:
        case Screen::Credits: return backdrop_ != nullptr;
        default: return false;
    }
}

sim::Side GameUi::playerSide() const { return session_ ? session_->player() : sim::Side::Neutral; }
bool GameUi::paused() const { return screen_ != Screen::Battle || !session_ || session_->paused; }

// --------------------------------------------------------------------------------------------- frame
void GameUi::updateSession(double dt) {
    simDt_ = 0;
    game::Session* s = nullptr;
    if (screen_ == Screen::Battle) s = session_.get();
    else if (screen_ != Screen::Debrief && screen_ != Screen::UnitReference && screen_ != Screen::Editor) s = backdrop_.get();
    if (!s) return;
    if (s == session_.get() && (pauseMenu_ || showUnitRefInBattle_ || showSettingsInBattle_)) return;
    double t0 = s->world().time();
    s->update(dt);
    simDt_ = s->world().time() - t0;
    auto events = s->drainEvents();
    if (s == session_.get()) {
        battleRealTime_ += dt;
        processEvents(events);
        if (s->world().time() - lastPlotSample_ > 20.0) recordPlot();
        if (s->finished() && screen_ == Screen::Battle) {
            addLog(loc::format("msg.battle_over", {tr(s->endReason().c_str())}), col::kBrass);
            endBattle();
        }
    } else if (s->finished()) {
        backdrop_.reset();
        ensureBackdrop();
        return;
    }
    frameEvents_.insert(frameEvents_.end(), std::make_move_iterator(events.begin()), std::make_move_iterator(events.end()));
}

void GameUi::frame(double dt) {
    ImGuiIO& io = ImGui::GetIO();
    if (autoActive_) runAutomation();
    updateSession(dt);

    // Global shortcuts.
    if (!io.WantTextInput) {
        if (screen_ == Screen::Battle && session_) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                if (mapOrder_ != MapOrder::None) mapOrder_ = MapOrder::None;
                else if (showUnitRefInBattle_) showUnitRefInBattle_ = false;
                else if (showSettingsInBattle_) showSettingsInBattle_ = false;
                else pauseMenu_ = !pauseMenu_;
            }
            if (!pauseMenu_ && !showUnitRefInBattle_ && !showSettingsInBattle_) {
                if (ImGui::IsKeyPressed(ImGuiKey_Space)) session_->paused = !session_->paused;
                static const double scales[] = {1, 2, 4, 8, 15, 30};
                for (int i = 0; i < 6; ++i)
                    if (ImGui::IsKeyPressed(ImGuiKey(ImGuiKey_1 + i))) {
                        session_->timeScale = scales[i];
                        session_->paused = false;
                    }
                if (ImGui::IsKeyPressed(ImGuiKey_M)) view_ = view_ == View::Map ? View::Scene3D : View::Map;
                if (ImGui::IsKeyPressed(ImGuiKey_F1)) view_ = View::Scene3D;
                if (ImGui::IsKeyPressed(ImGuiKey_F2)) view_ = View::Map;
                if (ImGui::IsKeyPressed(ImGuiKey_F3)) view_ = View::Split;
                if (ImGui::IsKeyPressed(ImGuiKey_Tab)) cycleShip(io.KeyShift ? -1 : 1);
                if (ImGui::IsKeyPressed(ImGuiKey_I)) showInsight_ = !showInsight_;
                if (ImGui::IsKeyPressed(ImGuiKey_U)) showUnitRefInBattle_ = !showUnitRefInBattle_;
                if (ImGui::IsKeyPressed(ImGuiKey_F)) follow_ = !follow_;
                if (ImGui::IsKeyPressed(ImGuiKey_B))
                    ctl_.mode = ctl_.mode == render::CameraMode::Bridge ? render::CameraMode::Orbit : render::CameraMode::Bridge;
                if (ImGui::IsKeyPressed(ImGuiKey_V))
                    ctl_.mode = ctl_.mode == render::CameraMode::Binoculars ? render::CameraMode::Bridge : render::CameraMode::Binoculars;
                handleBattleKeys();
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) && screen_ != Screen::MainMenu && screen_ != Screen::Battle) {
            go(screen_ == Screen::Briefing ? Screen::Missions : screen_ == Screen::Settings ? settingsReturn_ : Screen::MainMenu);
        }
    }

    switch (screen_) {
        case Screen::MainMenu: drawMainMenu(); break;
        case Screen::Missions: drawMissions(); break;
        case Screen::Briefing: drawBriefing(); break;
        case Screen::Battle: drawBattle(); break;
        case Screen::Debrief: drawDebrief(); break;
        case Screen::UnitReference: unitRef_->draw(false); break;
        case Screen::Editor: editor_->draw(); break;
        case Screen::Settings: drawSettings(false); break;
        case Screen::Credits: drawCredits(); break;
    }
    updateCamera(dt);
    if (autoActive_ && autoStage_ == 1 && --autoFramesLeft_ <= 0) autoStage_ = 2;
}

// -------------------------------------------------------------------------------------------- camera
void GameUi::handleSceneInput() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) return;
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        if (ctl_.mode == render::CameraMode::Bridge || ctl_.mode == render::CameraMode::Binoculars) {
            double fovK = ctl_.mode == render::CameraMode::Binoculars ? 0.15 : 1.0;
            ctl_.orbitYaw = wrap360(ctl_.orbitYaw + io.MouseDelta.x * 0.2 * fovK);
            ctl_.orbitPitch = clamp(ctl_.orbitPitch - io.MouseDelta.y * 0.15 * fovK, -30.0, 30.0);
        } else {
            ctl_.orbit(io.MouseDelta.x, io.MouseDelta.y);
        }
    }
    if (io.MouseWheel != 0.0f && ctl_.mode == render::CameraMode::Orbit) ctl_.zoom(io.MouseWheel);
    ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.0f);
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && drag.x * drag.x + drag.y * drag.y < 25.0f) {
        sim::EntityId id = svc.pick3D(io.MousePos.x, io.MousePos.y);
        if (const sim::World* w = world(); w && id != sim::kNoEntity) {
            if (const sim::Ship* s = w->ship(id); s && (playerSide() == sim::Side::Neutral || s->side == playerSide())) selectShip(id);
            else if (const sim::Aircraft* a = w->plane(id); a && (playerSide() == sim::Side::Neutral || a->side == playerSide()))
                selectFlight(a->flight);
        }
    }
    // Free camera movement (WASD) when not following a ship.
    if (!io.WantCaptureKeyboard && !follow_ && ctl_.mode == render::CameraMode::Orbit) {
        double sp = std::max(20.0, ctl_.distance * 0.9) * io.DeltaTime * (io.KeyShift ? 3.0 : 1.0);
        double yaw = ctl_.orbitYaw * kDegToRad;
        Vec3 fwd{std::sin(yaw), 0, std::cos(yaw)}, right{std::cos(yaw), 0, -std::sin(yaw)};
        if (ImGui::IsKeyDown(ImGuiKey_W)) ctl_.target += fwd * sp;
        if (ImGui::IsKeyDown(ImGuiKey_S)) ctl_.target -= fwd * sp;
        if (ImGui::IsKeyDown(ImGuiKey_D)) ctl_.target += right * sp;
        if (ImGui::IsKeyDown(ImGuiKey_A)) ctl_.target -= right * sp;
    }
}

void GameUi::updateCamera(double dt) {
    const sim::World* w = world();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    if (!w) return;
    const sim::Ship* s = nullptr;
    if (screen_ == Screen::Battle) {
        s = w->ship(selShip_);
        // A selected flight in the air: the camera rides with its leader.
        if (const sim::Flight* f = selFlight_ >= 0 ? w->flight(selFlight_) : nullptr; f && f->airborne() && follow_) {
            for (sim::EntityId pid : f->planes)
                if (const sim::Aircraft* a = w->plane(pid); a && (a->flying() || a->fate == sim::Aircraft::Fate::Falling)) {
                    Vec3 target = render::toRender(a->pos.xy(), a->pos.z);
                    double k = 1.0 - std::exp(-dt * 6.0);
                    if (length(ctl_.target - target) > 3000.0) k = 1.0;
                    ctl_.target = ctl_.target + (target - ctl_.target) * k;
                    if (ctl_.mode != render::CameraMode::Orbit) ctl_.mode = render::CameraMode::Orbit;
                    ctl_.apply(cam_, 0.0);
                    cam_.nearZ = std::clamp(ctl_.distance * 0.002, 0.5, 5.0);
                    return;
                }
        }
    } else {
        // Menu backdrop: slow orbit around the leading cruiser.
        for (auto& sp : w->ships())
            if (sp->side == sim::Side::Axis && sp->alive()) {
                s = sp.get();
                break;
            }
        ctl_.mode = render::CameraMode::Orbit;
        ctl_.orbitYaw = wrap360(ctl_.orbitYaw + dt * 2.5);
        follow_ = true;
    }
    if (s && s->cls && (follow_ || ctl_.mode != render::CameraMode::Orbit)) {
        Vec3 target = render::toRender(s->pos, s->cls->superstructureHeight * 0.6);
        // Smoothly follow (avoids jumps when switching ships).
        double k = 1.0 - std::exp(-dt * 6.0);
        if (length(ctl_.target - target) > 3000.0) k = 1.0;
        ctl_.target = ctl_.target + (target - ctl_.target) * k;
    }
    if ((ctl_.mode == render::CameraMode::Bridge || ctl_.mode == render::CameraMode::Binoculars) && s && s->cls) {
        Vec2 bridge = s->localToWorld(s->cls->length * 0.18, 0.0);
        cam_.pos = render::toRender(bridge, s->cls->lookoutHeight);
        cam_.yaw = wrap360(s->heading + ctl_.orbitYaw);
        cam_.pitch = ctl_.orbitPitch;
        cam_.fovY = ctl_.mode == render::CameraMode::Binoculars ? 7.0 : 55.0;
        cam_.nearZ = 0.5;
        return;
    }
    if (ctl_.mode != render::CameraMode::Orbit) ctl_.mode = render::CameraMode::Orbit;
    ctl_.apply(cam_, 0.0);
    cam_.nearZ = std::clamp(ctl_.distance * 0.002, 0.5, 5.0);
}

// ---------------------------------------------------------------------------------------- selection
sim::Ship* GameUi::selected() { return session_ ? session_->world().ship(selShip_) : nullptr; }

void GameUi::selectShip(sim::EntityId id, bool follow) {
    selShip_ = id;
    selFlight_ = -1;
    if (mapOrder_ != MapOrder::Waypoints) mapOrder_ = MapOrder::None;
    if (follow) follow_ = true;
}

void GameUi::cycleShip(int dir) {
    if (!session_) return;
    std::vector<sim::EntityId> ids;
    for (auto& s : session_->world().ships())
        if (s->alive() && (playerSide() == sim::Side::Neutral || s->side == playerSide())) ids.push_back(s->id);
    if (ids.empty()) return;
    auto it = std::find(ids.begin(), ids.end(), selShip_);
    int i = it == ids.end() ? 0 : int(it - ids.begin());
    i = (i + dir + int(ids.size())) % int(ids.size());
    selectShip(ids[size_t(i)]);
}

std::vector<const sim::Track*> GameUi::knownHostiles() const {
    std::vector<const sim::Track*> out;
    if (!session_ || playerSide() == sim::Side::Neutral) return out;
    const sim::World& w = session_->world();
    for (const auto& t : w.picture(playerSide()).tracks()) {
        if (t.lost) continue;
        bool known = t.sharedAt <= w.time();
        for (const auto& h : t.holders)
            if (const sim::Ship* s = w.ship(h.ship); s && s->side == playerSide() && w.time() - h.lastSeen < 30.0) known = true;
        if (known) out.push_back(&t);
    }
    return out;
}

std::string GameUi::trackName(const sim::Track& t) const {
    std::string s = "T" + std::to_string(t.id);
    if (t.level >= sim::ClassLevel::ClassKnown && !t.classGuess.empty()) {
        if (const sim::ShipClassDef* c = db_.shipClass(t.classGuess)) s += " " + c->name;
    } else if (t.level >= sim::ClassLevel::TypeKnown) {
        s += std::string(" ") + shipTypeAbbrev(t.typeGuess);
    } else if (t.level >= sim::ClassLevel::SizeKnown) {
        s += std::string(" ") + (t.size == sim::SizeClass::Large ? tr("map.size_large") : t.size == sim::SizeClass::Medium ? tr("map.size_medium")
                                                                                                                            : tr("map.size_small"));
    }
    return s;
}

void GameUi::addLog(const std::string& text, const ImVec4& color) {
    LogLine l;
    l.text = text;
    l.color = color;
    addLog(l);
}

void GameUi::addLog(const LogLine& line) {
    LogLine l = line;
    l.time = session_ ? session_->world().time() : 0.0;
    log_.push_back(std::move(l));
    while (log_.size() > 400) log_.pop_front();
}

void GameUi::alert(const char* key, const ImVec4& color, double simTime) {
    banner_ = tr(key);
    bannerColor_ = color;
    bannerUntil_ = battleRealTime_ + 4.0;
    lastAlertSim_ = simTime;
    // Time compression drops back so the player can react (the battle is not paused).
    if (settings.slowOnAlert && session_ && session_->timeScale > 1.0) {
        session_->timeScale = 1.0;
        addLog(tr("msg.time_slowed"), col::kTextDim);
    }
}

void GameUi::drawAlertBanner() {
    if (battleRealTime_ >= bannerUntil_ || banner_.empty()) return;
    float k = float(std::clamp((bannerUntil_ - battleRealTime_) / 1.0, 0.0, 1.0));
    ImGuiIO& io = ImGui::GetIO();
    FontScope f(fonts().heading, 1.7f);
    ImVec2 ts = ImGui::CalcTextSize(banner_.c_str());
    ImVec2 p{(io.DisplaySize.x - ts.x) * 0.5f, ImGui::GetFontSize() * 2.6f + 10.0f};
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddRectFilled({p.x - 18, p.y - 6}, {p.x + ts.x + 18, p.y + ts.y + 6}, IM_COL32(10, 14, 18, int(200 * k)), 4.0f);
    dl->AddRect({p.x - 18, p.y - 6}, {p.x + ts.x + 18, p.y + ts.y + 6}, toU32(bannerColor_, 0.8f * k), 4.0f, 0, 1.5f);
    dl->AddText(p, toU32(bannerColor_, k), banner_.c_str());
}

void GameUi::handleBattleKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || !session_) return;
    sim::World& w = session_->world();
    if (ImGui::IsKeyPressed(ImGuiKey_Q)) cycleFlight(io.KeyShift ? -1 : 1);
    if (sim::Flight* f = selectedFlight()) {
        if (ImGui::IsKeyPressed(ImGuiKey_R) && f->airborne() && f->playerControlled) {
            sim::AirOrder o;
            o.type = sim::AirOrderType::Return;
            issueFlightOrder(f->id, o);
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Home) && f->airborne()) map_.focus(f->pos);
        return;
    }
    sim::Ship* s = selected();
    if (!s || !session_->playerShip(s->id)) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) map_.focus(s->pos);
    sim::Group* g = w.group(s->groupId);
    bool groupCtl = g && g->playerControlled && !s->orders.playerSteering;
    if (!groupCtl && !s->orders.playerSteering && !(g && g->playerControlled)) return;  // the AI has this ship's group
    if (ImGui::IsKeyPressed(ImGuiKey_G) && groupCtl) helmMode_ ^= 1;
    bool toGroup = groupCtl && helmMode_ == 0;
    double course = toGroup ? g->course : s->orders.playerSteering ? s->orders.course : s->heading;
    double speed = toGroup ? g->speedKn : s->orders.playerSteering ? s->orders.speedKn : s->speedKn();
    double maxKn = s->maxSpeedNow() / 0.514444;
    double dc = 0, dv = 0;
    double step = io.KeyShift ? 45.0 : io.KeyCtrl ? 1.0 : 10.0;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) dc = -step;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) dc = step;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) dv = io.KeyShift ? 5.0 : 2.0;
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) dv = io.KeyShift ? -5.0 : -2.0;
    if (dc != 0 || dv != 0) {
        course = wrap360(course + dc);
        speed = std::clamp(std::round(speed + dv), 0.0, std::floor(maxKn));
        if (toGroup) session_->orderGroupCourse(g->id, course, speed);
        else session_->orderShipCourse(s->id, course, speed);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_X)) {
        sim::WeaponsStatus next = s->orders.weapons == sim::WeaponsStatus::Hold ? sim::WeaponsStatus::Tight
                                  : s->orders.weapons == sim::WeaponsStatus::Tight ? sim::WeaponsStatus::Free
                                                                                    : sim::WeaponsStatus::Hold;
        if (toGroup) {
            for (auto id : g->members) session_->orderWeapons(id, next);
        } else {
            session_->orderWeapons(s->id, next);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_T)) {
        uint32_t target = selTrack_ ? selTrack_ : s->orders.playerTarget;
        if (target && s->torpedoesReady() > 0) {
            std::string why;
            int n = session_->orderTorpedoes(s->id, target, std::min(torpedoCount_, s->torpedoesReady()), torpedoSpread_, &why);
            lastTorpedoMsg_ = n > 0 ? loc::format("weapons.launched", {std::to_string(n)}) : (why.empty() ? tr("weapons.no_solution") : tr(why.c_str()));
            addLog(s->name + ": " + lastTorpedoMsg_, n > 0 ? col::kGood : col::kWarn);
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
        if (toGroup) session_->orderGroupRoute(g->id, {}, false);
        else if (s->orders.playerSteering) session_->orderShipRoute(s->id, {}, false);
    }
}

void GameUi::recordPlot() {
    if (!session_) return;
    const sim::World& w = session_->world();
    lastPlotSample_ = w.time();
    for (const auto& s : w.ships()) {
        auto it = plotIndex_.find(s->id);
        if (it == plotIndex_.end()) {
            MapTrack t;
            t.color = sideColor(s->side);
            t.label = s->name;
            plotIndex_[s->id] = plot_.size();
            plot_.push_back(t);
            it = plotIndex_.find(s->id);
        }
        MapTrack& t = plot_[it->second];
        if (t.sunk) continue;
        t.points.push_back(s->pos);
        if (!s->alive() || s->dmg.sinking) t.sunk = true;
    }
}

// ------------------------------------------------------------------------------------------- events
void GameUi::processEvents(const std::vector<sim::Event>& events) {
    const sim::Side me = playerSide();
    const sim::World& w = session_->world();
    auto shipName = [&](sim::EntityId id) { const sim::Ship* s = w.ship(id); return s ? s->name : std::string("?"); };
    auto ownShip = [&](sim::EntityId id) {
        const sim::Ship* s = w.ship(id);
        return s && (me == sim::Side::Neutral || s->side == me);
    };
    auto flightOf = [&](sim::EntityId plane) -> const sim::Flight* {
        const sim::Aircraft* a = w.plane(plane);
        return a ? w.flight(a->flight) : nullptr;
    };
    // Log line that focuses a ship / a flight / a place when clicked.
    auto log = [&](const std::string& text, const ImVec4& c, sim::EntityId ship = sim::kNoEntity, int flight = -1, const Vec3* pos = nullptr) {
        LogLine l;
        l.text = text;
        l.color = c;
        l.ship = ship;
        l.flight = flight;
        if (pos) {
            l.hasPos = true;
            l.pos = {pos->x, pos->y};
        }
        addLog(l);
    };
    auto argsOf = [](const sim::Event& e) {
        std::vector<std::string> a;
        for (const auto& x : e.args) a.push_back(locName(x));
        return a;
    };
    for (const auto& e : events) {
        switch (e.type) {
            case sim::EventType::Report:
                if (me == sim::Side::Neutral || e.side == me) log(loc::format(e.text, argsOf(e)), col::kText, e.a);
                break;
            case sim::EventType::ContactNew:
                if (e.side == me) {
                    const sim::Ship* s = w.ship(selShip_);
                    std::string text = s ? loc::format("msg.contact_new", {std::to_string(e.track), fmtBearing(bearingTo(s->pos, {e.pos.x, e.pos.y}))})
                                         : loc::format("msg.contact_new_plain", {std::to_string(e.track)});
                    log(text, col::kUnknown, sim::kNoEntity, -1, &e.pos);
                    if (settings.pauseOnContact && session_) session_->paused = true;
                    else if (e.time - lastAlertSim_ > 30.0) alert("alert.contact", col::kUnknown, e.time);
                }
                break;
            case sim::EventType::ContactIdentified:
                if (e.side == me) {
                    const sim::ShipClassDef* c = db_.shipClass(e.text);
                    log(loc::format("msg.contact_identified", {std::to_string(e.track), c ? c->name : e.text}), col::kWarn, sim::kNoEntity, -1, &e.pos);
                }
                break;
            case sim::EventType::TorpedoSighted:
                if (ownShip(e.a)) {
                    log(loc::format("msg.torpedo_sighted", {shipName(e.a)}), col::kBad, e.a);
                    if (e.time - lastAlertSim_ > 20.0) alert("alert.torpedoes", col::kBad, e.time);
                }
                break;
            case sim::EventType::ShellHit:
                if (const sim::Ship* t = w.ship(e.b)) {
                    if (me == sim::Side::Neutral || t->side == me)
                        log(loc::format(e.value2 > 0.5 ? "msg.hit_taken_pen" : "msg.hit_taken", {t->name, std::to_string(int(e.value))}), col::kBad, t->id);
                    else if (const sim::Ship* f = w.ship(e.a); f && f->side == me)
                        log(loc::format("msg.hit_scored", {f->name}), col::kGood, f->id);
                    else if (const sim::Flight* fl = flightOf(e.a); fl && fl->side == me)
                        log(loc::format("msg.air_bomb_hit", {locName(fl->name)}), col::kGood, sim::kNoEntity, fl->id);
                }
                break;
            case sim::EventType::TorpedoHit:
                if (const sim::Ship* t = w.ship(e.b)) {
                    if (me == sim::Side::Neutral || t->side == me) log(loc::format("msg.torpedo_hit_taken", {t->name}), col::kBad, t->id);
                    else if (const sim::Ship* f = w.ship(e.a); f && f->side == me) log(loc::format("msg.torpedo_hit_scored", {f->name}), col::kGood, f->id);
                    else if (const sim::Flight* fl = flightOf(e.a); fl && fl->side == me)
                        log(loc::format("msg.air_torpedo_hit", {locName(fl->name)}), col::kGood, sim::kNoEntity, fl->id);
                }
                break;
            case sim::EventType::ShipSunk:
                if (const sim::Ship* s = w.ship(e.a)) {
                    if (me == sim::Side::Neutral || s->side == me) log(loc::format("msg.ship_lost", {s->name}), col::kBad, sim::kNoEntity, -1, &e.pos);
                    else log(tr("msg.enemy_sinking"), col::kGood, sim::kNoEntity, -1, &e.pos);
                }
                break;
            case sim::EventType::MagazineExplosion:
                if (ownShip(e.a)) log(loc::format("msg.magazine", {shipName(e.a)}), col::kBad, e.a);
                break;
            case sim::EventType::FireStarted:
                if (ownShip(e.a)) log(loc::format("msg.fire", {shipName(e.a)}), col::kWarn, e.a);
                break;
            case sim::EventType::StarshellBurst:
                if (const sim::Ship* s = w.ship(e.a); s && s->side != me && me != sim::Side::Neutral) log(tr("msg.starshell_enemy"), col::kWarn, sim::kNoEntity, -1, &e.pos);
                break;
            case sim::EventType::SearchlightOn:
                if (const sim::Ship* s = w.ship(e.a); s && s->side != me && me != sim::Side::Neutral) log(tr("msg.searchlight_enemy"), col::kWarn);
                break;
            case sim::EventType::FlareDropped:
                if (e.side != me && me != sim::Side::Neutral) log(tr("msg.flare_enemy"), col::kWarn, sim::kNoEntity, -1, &e.pos);
                break;
            case sim::EventType::ShipGrounded:
                if (me == sim::Side::Neutral || e.side == me) log(loc::format("msg.grounded", {shipName(e.a)}), col::kBad, e.a);
                break;
            case sim::EventType::Collision:
                if (ownShip(e.a)) log(loc::format("msg.collision", {shipName(e.a), shipName(e.b)}), col::kBad, e.a);
                break;
            case sim::EventType::ScenarioMessage: log(locName(e.text), col::kBrass); break;
            // ---- Air
            case sim::EventType::FlightLaunched:
                if (me == sim::Side::Neutral || e.side == me)
                    if (const sim::Flight* f = w.flight(int(e.value))) log(loc::format("msg.flight_launched", {locName(f->name)}), col::kText, sim::kNoEntity, f->id);
                break;
            case sim::EventType::FlightLanded:
                if (me == sim::Side::Neutral || e.side == me)
                    if (const sim::Flight* f = w.flight(int(e.value))) log(loc::format("msg.flight_landed", {locName(f->name)}), col::kTextDim, sim::kNoEntity, f->id);
                break;
            case sim::EventType::FlightReport:
                if (me == sim::Side::Neutral || e.side == me)
                    if (const sim::Flight* f = w.flight(int(e.value))) {
                        std::vector<std::string> args = argsOf(e);
                        std::string key = e.text;
                        if (e.text == "air.report.sighting" && args.size() >= 2) {
                            sim::ShipType st = sim::shipTypeFromString(e.args[0]);
                            if (st == sim::ShipType::Unknown) {
                                key = "air.report.sighting_ships";
                                args = {fmtBearing(std::atof(e.args[1].c_str()))};
                            } else {
                                args = {shipTypeName(st), fmtBearing(std::atof(e.args[1].c_str()))};
                            }
                        }
                        bool bad = e.text == "air.report.lost" || e.text == "air.report.ditched" || e.text == "air.report.fuel";
                        bool hot = e.text == "air.report.sighting" || e.text == "air.report.tallyho" || e.text == "air.report.attacking";
                        log(locName(f->name) + ": " + loc::format(key, args), bad ? col::kBad : hot ? col::kWarn : col::kText, sim::kNoEntity, f->id, &e.pos);
                    }
                break;
            case sim::EventType::AirContactNew:
                if (e.side == me && me != sim::Side::Neutral) {
                    const sim::AirTrack* a = w.picture(me).findAir(e.track);
                    std::string brg = "-";
                    if (const sim::Ship* s = w.ship(selShip_)) brg = fmtBearing(bearingTo(s->pos, {e.pos.x, e.pos.y}));
                    std::string what = a && a->count > 0 ? std::to_string(a->count) : "?";
                    log(loc::format("msg.air_contact", {"A" + std::to_string(e.track), what, brg}), col::kAxis, sim::kNoEntity, -1, &e.pos);
                    bool raid = a && (a->count >= 3 || (a->roleKnown && a->role != sim::AirRole::Recon && a->role != sim::AirRole::Patrol));
                    if (raid && e.time - lastAlertSim_ > 30.0) alert("alert.air_raid", col::kAxis, e.time);
                }
                break;
            case sim::EventType::AirAttack:
                if (ownShip(e.b) && e.side != me) {
                    // One line per ship and kind of attack while the attack lasts (every aircraft reports its run).
                    double& last = attackLogged_[(uint64_t(e.b) << 8) ^ std::hash<std::string>{}(e.text)];
                    if (e.time - last < 60.0 && last > 0.0) break;
                    last = e.time;
                    std::string kind = tr(("air.attack." + e.text).c_str());
                    log(loc::format("msg.air_attack", {shipName(e.b), kind}), col::kBad, e.b);
                    if (e.time - lastAlertSim_ > 45.0) alert("alert.air_attack", col::kBad, e.time);
                }
                break;
            case sim::EventType::AircraftShotDown: {
                const sim::Flight* f = w.flight(int(e.value));
                if (f && (me == sim::Side::Neutral || f->side == me)) log(loc::format("msg.aircraft_lost", {locName(f->name)}), col::kBad, sim::kNoEntity, f->id, &e.pos);
                else if (me != sim::Side::Neutral) {
                    if (const sim::Ship* s = w.ship(e.b); s && s->side == me) log(loc::format("msg.aircraft_downed_aa", {s->name}), col::kGood, s->id);
                    else if (const sim::Flight* k = flightOf(e.b); k && k->side == me)
                        log(loc::format("msg.aircraft_downed_air", {locName(k->name)}), col::kGood, sim::kNoEntity, k->id);
                }
                break;
            }
            default: break;
        }
    }
}

// --------------------------------------------------------------------------------------- automation
void GameUi::startAutomation(const Automation& a) {
    auto_ = a;
    autoActive_ = true;
    autoStage_ = 0;
    autoFramesLeft_ = std::max(1, a.frames);
}

void GameUi::runAutomation() {
    if (autoStage_ != 0) return;
    autoStage_ = 1;
    const Automation& a = auto_;
    auto findScenario = [&]() -> const game::Scenario* {
        for (size_t i = 0; i < scenarios_.size(); ++i)
            if (scenarios_[i].id == a.scenario) {
                selectedScenario_ = int(i);
                return &scenarios_[i];
            }
        return scenarios_.empty() ? nullptr : &scenarios_[0];
    };
    sim::Side side = a.side == "axis" ? sim::Side::Axis : a.side == "none" ? sim::Side::Neutral : sim::Side::Allies;
    chosenSide_ = side == sim::Side::Neutral ? sim::Side::Allies : side;
    const std::string& s = a.screen;
    if (s == "missions") go(Screen::Missions);
    else if (s == "briefing") {
        if (const game::Scenario* sc = findScenario(); sc && !a.side.empty()) chosenSideFor_ = sc->id;  // explicit --side wins
        go(Screen::Briefing);
    } else if (s == "settings") {
        settingsReturn_ = Screen::MainMenu;
        go(Screen::Settings);
    } else if (s == "credits") go(Screen::Credits);
    else if (s == "unitref") {
        go(Screen::UnitReference);
        if (!a.unitClass.empty()) unitRef_->select(a.unitClass);
        // Close-ups for visual checks: --yaw/--pitch, --distance as zoom factor, --pan along the hull, --large.
        PreviewView& v = unitRef_->view();
        if (a.camYaw >= 0) v.yaw = float(a.camYaw);
        if (a.camPitch > -90) v.pitch = float(a.camPitch);
        if (a.camDistance > 0) v.zoom = float(a.camDistance);
        v.focus = float(a.pan);
        v.lift = float(a.lift);
        v.night = a.night;
        v.animate = a.drill || a.drillTime >= 0;
        v.drillTime = float(a.drillTime);
        unitRef_->setLargeView(a.large);
    } else if (s == "editor") {
        if (const game::Scenario* sc = findScenario()) editor_->load(*sc);
        go(Screen::Editor);
    } else if (s == "battle" || s == "map" || s == "split" || s == "debrief") {
        const game::Scenario* sc = findScenario();
        if (!sc) return;
        startBattle(*sc, side);
        if (!session_) return;
        session_->paused = false;
        while (session_->world().time() < a.advance && !session_->finished()) {
            session_->step();
            auto ev = session_->drainEvents();
            processEvents(ev);
            if (session_->world().time() - lastPlotSample_ > 20.0) recordPlot();
            // Keep only the last seconds of events for the visual effects.
            for (auto& e : ev)
                if (e.time > a.advance - 4.0) frameEvents_.push_back(std::move(e));
        }
        if (!a.focus.empty())
            for (auto& sh : session_->world().ships())
                if (sh->name == a.focus) selectShip(sh->id);
        if (!a.flight.empty())
            for (auto& f : session_->world().flights())
                if (f.name == a.flight || locName(f.name) == a.flight) selectFlight(f.id);
        view_ = s == "map" ? View::Map : s == "split" ? View::Split : View::Scene3D;
        showInsight_ = a.insight;
        insightFilter_ = a.insightFilter;
        if (s == "debrief") endBattle();
    }
    if (a.camYaw >= 0) ctl_.orbitYaw = a.camYaw;
    if (a.camPitch > -90) ctl_.orbitPitch = a.camPitch;
    if (a.camDistance > 0) ctl_.distance = a.camDistance;
}

void GameUi::backgroundPanel(const char* id, ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
}

}  // namespace it::ui
