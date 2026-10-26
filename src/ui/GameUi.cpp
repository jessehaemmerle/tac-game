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
    opt.seed = uint64_t(std::time(nullptr)) & 0xffffff;
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
                if (showUnitRefInBattle_) showUnitRefInBattle_ = false;
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
        if (const sim::World* w = world(); w && id != sim::kNoEntity)
            if (const sim::Ship* s = w->ship(id); s && (playerSide() == sim::Side::Neutral || s->side == playerSide())) selectShip(id);
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
    double t = session_ ? session_->world().time() : 0.0;
    log_.push_back({t, text, color});
    while (log_.size() > 400) log_.pop_front();
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
    for (const auto& e : events) {
        switch (e.type) {
            case sim::EventType::Report:
                if (me == sim::Side::Neutral || e.side == me) addLog(loc::format(e.text, e.args), col::kText);
                break;
            case sim::EventType::ContactNew:
                if (e.side == me) {
                    std::string brg;
                    if (const sim::Ship* s = w.ship(selShip_)) brg = fmtBearing(bearingTo(s->pos, {e.pos.x, e.pos.y}));
                    addLog(loc::format("msg.contact_new", {std::to_string(e.track), brg}), col::kUnknown);
                    if (settings.pauseOnContact && session_) session_->paused = true;
                }
                break;
            case sim::EventType::ContactIdentified:
                if (e.side == me) {
                    const sim::ShipClassDef* c = db_.shipClass(e.text);
                    addLog(loc::format("msg.contact_identified", {std::to_string(e.track), c ? c->name : e.text}), col::kWarn);
                }
                break;
            case sim::EventType::TorpedoSighted:
                if (const sim::Ship* s = w.ship(e.a); s && (me == sim::Side::Neutral || s->side == me))
                    addLog(loc::format("msg.torpedo_sighted", {s->name}), col::kBad);
                break;
            case sim::EventType::ShellHit:
                if (const sim::Ship* t = w.ship(e.b)) {
                    if (me == sim::Side::Neutral || t->side == me)
                        addLog(loc::format(e.value2 > 0.5 ? "msg.hit_taken_pen" : "msg.hit_taken", {t->name, std::to_string(int(e.value))}), col::kBad);
                    else if (const sim::Ship* f = w.ship(e.a); f && f->side == me)
                        addLog(loc::format("msg.hit_scored", {f->name}), col::kGood);
                }
                break;
            case sim::EventType::TorpedoHit:
                if (const sim::Ship* t = w.ship(e.b)) {
                    if (me == sim::Side::Neutral || t->side == me) addLog(loc::format("msg.torpedo_hit_taken", {t->name}), col::kBad);
                    else if (const sim::Ship* f = w.ship(e.a); f && f->side == me) addLog(loc::format("msg.torpedo_hit_scored", {f->name}), col::kGood);
                }
                break;
            case sim::EventType::ShipSunk:
                if (const sim::Ship* s = w.ship(e.a)) {
                    if (me == sim::Side::Neutral || s->side == me) addLog(loc::format("msg.ship_lost", {s->name}), col::kBad);
                    else addLog(tr("msg.enemy_sinking"), col::kGood);
                }
                break;
            case sim::EventType::MagazineExplosion:
                if (const sim::Ship* s = w.ship(e.a); s && (me == sim::Side::Neutral || s->side == me))
                    addLog(loc::format("msg.magazine", {s->name}), col::kBad);
                break;
            case sim::EventType::FireStarted:
                if (const sim::Ship* s = w.ship(e.a); s && (me == sim::Side::Neutral || s->side == me))
                    addLog(loc::format("msg.fire", {s->name}), col::kWarn);
                break;
            case sim::EventType::StarshellBurst:
                if (const sim::Ship* s = w.ship(e.a); s && s->side != me && me != sim::Side::Neutral) addLog(tr("msg.starshell_enemy"), col::kWarn);
                break;
            case sim::EventType::SearchlightOn:
                if (const sim::Ship* s = w.ship(e.a); s && s->side != me && me != sim::Side::Neutral) addLog(tr("msg.searchlight_enemy"), col::kWarn);
                break;
            case sim::EventType::FlareDropped:
                if (const sim::Ship* s = w.ship(e.a); s && s->side != me && me != sim::Side::Neutral) addLog(tr("msg.flare_enemy"), col::kWarn);
                break;
            case sim::EventType::ShipGrounded:
                if (me == sim::Side::Neutral || e.side == me) addLog(loc::format("msg.grounded", {shipName(e.a)}), col::kBad);
                break;
            case sim::EventType::Collision:
                if (const sim::Ship* s = w.ship(e.a); s && (me == sim::Side::Neutral || s->side == me))
                    addLog(loc::format("msg.collision", {s->name, shipName(e.b)}), col::kBad);
                break;
            case sim::EventType::ScenarioMessage: addLog(loc::has(e.text) ? tr(e.text) : e.text, col::kBrass); break;
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
