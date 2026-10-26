#include "ui/ScenarioEditor.h"

#include <algorithm>
#include <cstring>

#include "core/FileSystem.h"
#include "core/Localization.h"
#include "ui/Format.h"
#include "ui/GameUi.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace it::ui {

namespace {

void copyStr(char* dst, size_t n, const std::string& s) {
    std::strncpy(dst, s.c_str(), n - 1);
    dst[n - 1] = 0;
}

}  // namespace

void ScenarioEditor::newScenario() {
    game::Scenario sc;
    sc.id = "custom_battle";
    sc.title = "Custom battle";
    sc.terrain = "terrain/ironbottom_sound.json";
    sc.date = {1942, 10, 12, 0.5, 11.0};
    sc.weather.cloudCover = 0.5;
    sc.weather.visibilityKm = 12;
    sc.weather.seaState = 2;
    sc.timeLimit = 3 * 3600;
    sc.playableSides = {sim::Side::Allies, sim::Side::Axis};
    load(sc);
}

void ScenarioEditor::load(const game::Scenario& sc) {
    sc_ = sc;
    selGroup_ = sc_.groups.empty() ? -1 : 0;
    fitted_ = false;
    status_.clear();
    copyStr(idBuf_, sizeof(idBuf_), sc_.id);
    copyStr(titleBuf_, sizeof(titleBuf_), loc::has(sc_.title) ? tr(sc_.title.c_str()) : sc_.title);
    ensureTerrain();
}

void ScenarioEditor::ensureTerrain() {
    if (loadedTerrain_ == sc_.terrain) return;
    loadedTerrain_ = sc_.terrain;
    terrain_ = sim::Terrain();
    if (!sc_.terrain.empty()) terrain_.load(fs::dataPath(sc_.terrain).string());
}

void ScenarioEditor::addGroup(sim::Side side, const Vec2& pos) {
    game::ScenarioGroup g;
    g.id = (side == sim::Side::Allies ? "allied_" : "axis_") + std::to_string(sc_.groups.size() + 1);
    g.name = std::string(side == sim::Side::Allies ? "Task Group " : "Raiding Force ") + std::to_string(sc_.groups.size() + 1);
    g.side = side;
    g.pos = pos;
    g.course = side == sim::Side::Allies ? 300 : 120;
    g.speedKn = 20;
    g.doctrine = side == sim::Side::Allies ? "usn_1942" : "ijn_1942";
    g.mission.type = ai::MissionType::Patrol;
    g.mission.speedKn = 20;
    for (const auto& c : ui_.db().shipClasses())
        if (sim::defaultSide(c->nation) == side && (c->type == sim::ShipType::CA || c->type == sim::ShipType::DD) && g.ships.size() < 2) {
            game::ScenarioShip s;
            s.cls = c->id;
            s.name = c->ships.empty() ? c->name : c->ships[g.ships.size() % c->ships.size()];
            g.ships.push_back(s);
        }
    sc_.groups.push_back(g);
    selGroup_ = int(sc_.groups.size()) - 1;
}

bool ScenarioEditor::save() {
    sc_.id = idBuf_;
    sc_.title = titleBuf_;
    if (sc_.id.empty()) {
        status_ = tr("editor.need_id");
        return false;
    }
    auto dir = fs::userDataDir() / "scenarios";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    auto path = dir / (sc_.id + ".json");
    bool ok = game::saveScenario(path.string(), sc_);
    status_ = ok ? loc::format("editor.saved", {path.string()}) : tr("editor.save_failed");
    return ok;
}

void ScenarioEditor::draw() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({W, H});
    ImGui::Begin("##editor", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    {
        FontScope f(fonts().heading, 2.0f);
        ImGui::TextColored(col::kBrass, "%s", tr("editor.title"));
    }
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", tr("editor.hint"));
    ImGui::Separator();
    float bottom = ImGui::GetFrameHeightWithSpacing() * 1.5f;
    float leftW = W * 0.2f, rightW = W * 0.24f;
    float h = ImGui::GetContentRegionAvail().y - bottom;
    ImGui::BeginChild("props", {leftW, h}, ImGuiChildFlags_Borders);
    drawProperties();
    ImGui::EndChild();
    ImGui::SameLine();
    // Map.
    ImGui::BeginChild("mapc", {W - leftW - rightW - ImGui::GetStyle().ItemSpacing.x * 4, h}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (!fitted_) {
        if (!terrain_.empty()) map_.fit(terrain_.minCorner(), terrain_.maxCorner(), avail);
        else map_.focus({0, 0}, 80.0);
        fitted_ = true;
    }
    std::vector<MapMarker> markers;
    for (size_t i = 0; i < sc_.groups.size(); ++i) {
        const auto& g = sc_.groups[i];
        MapMarker m;
        m.id = int(i);
        m.pos = g.pos;
        m.heading = g.course;
        m.color = sideColor(g.side);
        m.label = (loc::has(g.name) ? std::string(tr(g.name.c_str())) : g.name) + " (" + std::to_string(g.ships.size()) + ")";
        m.selected = int(i) == selGroup_;
        m.route = g.mission.waypoints;
        markers.push_back(m);
    }
    MapInput in;
    in.terrain = &terrain_;
    if (const sim::World* w = ui_.world(); w && w->terrain().info().name == terrain_.info().name) in.chartTexture = ui_.svc.chartTexture();
    in.metric = ui_.settings.metric;
    in.markers = &markers;
    MapOutput out = map_.draw("editormap", avail, in);
    if (out.clickedMarker >= 0) selGroup_ = out.clickedMarker;
    if (out.draggedMarker >= 0 && size_t(out.draggedMarker) < sc_.groups.size()) {
        selGroup_ = out.draggedMarker;
        sc_.groups[size_t(out.draggedMarker)].pos = out.dragPos;
    }
    static Vec2 ctx;
    if (out.rightClicked) {
        ctx = out.rightClickPos;
        ImGui::OpenPopup("ctx");
    }
    if (ImGui::BeginPopup("ctx")) {
        if (ImGui::MenuItem(tr("editor.add_allied"))) addGroup(sim::Side::Allies, ctx);
        if (ImGui::MenuItem(tr("editor.add_axis"))) addGroup(sim::Side::Axis, ctx);
        if (selGroup_ >= 0 && size_t(selGroup_) < sc_.groups.size()) {
            ImGui::Separator();
            auto& g = sc_.groups[size_t(selGroup_)];
            if (ImGui::MenuItem(tr("editor.add_waypoint"))) g.mission.waypoints.push_back(ctx);
            if (ImGui::MenuItem(tr("editor.point_course"))) g.course = bearingTo(g.pos, ctx);
            if (!g.mission.waypoints.empty() && ImGui::MenuItem(tr("editor.clear_waypoints"))) g.mission.waypoints.clear();
        }
        ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("group", {0, h}, ImGuiChildFlags_Borders);
    if (selGroup_ >= 0 && size_t(selGroup_) < sc_.groups.size()) drawGroup(sc_.groups[size_t(selGroup_)]);
    else ImGui::TextDisabled("%s", tr("editor.no_group"));
    ImGui::EndChild();
    // Bottom bar.
    if (ImGui::Button(tr("ui.back"), {150, 0})) ui_.go(GameUi::Screen::MainMenu);
    ImGui::SameLine();
    if (ImGui::Button(tr("editor.new"), {150, 0})) newScenario();
    ImGui::SameLine();
    if (ImGui::Button(tr("editor.load"), {150, 0})) ImGui::OpenPopup("load");
    if (ImGui::BeginPopup("load")) {
        for (const auto& sc : ui_.scenarios_)
            if (ImGui::MenuItem(tr(sc.title.c_str()))) load(sc);
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("editor.save"), {150, 0})) save();
    ImGui::SameLine();
    bool canPlay = false;
    for (auto& g : sc_.groups)
        if (!g.ships.empty()) canPlay = true;
    ImGui::BeginDisabled(!canPlay);
    if (ImGui::Button(tr("editor.test"), {180, 0})) {
        sc_.id = idBuf_;
        sc_.title = titleBuf_;
        game::Scenario copy = sc_;
        ui_.startBattle(copy, sc_.playableSides.empty() ? sim::Side::Allies : sc_.playableSides.front());
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("%s", status_.c_str());
    ImGui::End();
}

void ScenarioEditor::drawProperties() {
    sectionHeader(tr("editor.scenario"));
    ImGui::InputText(tr("editor.id"), idBuf_, sizeof(idBuf_));
    ImGui::InputText(tr("editor.title_field"), titleBuf_, sizeof(titleBuf_));
    // Terrain.
    auto terrains = fs::listFiles(fs::dataPath("terrain"), ".json");
    std::string curT = sc_.terrain.empty() ? tr("editor.open_sea") : sc_.terrain;
    if (ImGui::BeginCombo(tr("editor.terrain"), curT.c_str())) {
        if (ImGui::Selectable(tr("editor.open_sea"), sc_.terrain.empty())) sc_.terrain.clear();
        for (auto& p : terrains) {
            std::string rel = "terrain/" + p.filename().string();
            if (ImGui::Selectable(rel.c_str(), rel == sc_.terrain)) sc_.terrain = rel;
        }
        ImGui::EndCombo();
    }
    ensureTerrain();
    sectionHeader(tr("editor.date_time"));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.3f);
    ImGui::InputInt("##y", &sc_.date.year, 0);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
    ImGui::SliderInt("##m", &sc_.date.month, 1, 12, "M %d");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderInt("##d", &sc_.date.day, 1, 31, "D %d");
    float hour = float(sc_.date.hour);
    if (ImGui::SliderFloat(tr("editor.local_time"), &hour, 0.0f, 23.99f, fmtClock(hour).c_str())) sc_.date.hour = hour;
    float limit = float(sc_.timeLimit / 3600.0);
    if (ImGui::SliderFloat(tr("briefing.time_limit"), &limit, 0.5f, 8.0f, "%.1f h")) sc_.timeLimit = limit * 3600.0;
    sectionHeader(tr("editor.weather"));
    float cloud = float(sc_.weather.cloudCover), vis = float(sc_.weather.visibilityKm), sea = float(sc_.weather.seaState);
    float windDir = float(sc_.weather.windDirDeg), windKn = float(sc_.weather.windSpeedKn);
    if (ImGui::SliderFloat(tr("env.clouds"), &cloud, 0.0f, 1.0f, "%.2f")) sc_.weather.cloudCover = cloud;
    if (ImGui::SliderFloat(tr("env.visibility"), &vis, 2.0f, 40.0f, "%.0f km")) sc_.weather.visibilityKm = vis;
    if (ImGui::SliderFloat(tr("env.sea_state"), &sea, 0.0f, 6.0f, "%.0f")) sc_.weather.seaState = std::round(sea);
    if (ImGui::SliderFloat(tr("editor.wind_dir"), &windDir, 0.0f, 359.0f, "%.0f\xC2\xB0")) sc_.weather.windDirDeg = windDir;
    if (ImGui::SliderFloat(tr("editor.wind_speed"), &windKn, 0.0f, 40.0f, "%.0f kn")) sc_.weather.windSpeedKn = windKn;
    ImGui::SliderInt(tr("env.squalls"), &sc_.weather.squalls, 0, 8);
    sectionHeader(tr("editor.sides"));
    for (sim::Side s : {sim::Side::Allies, sim::Side::Axis}) {
        bool on = std::find(sc_.playableSides.begin(), sc_.playableSides.end(), s) != sc_.playableSides.end();
        if (ImGui::Checkbox(sideName(s), &on)) {
            sc_.playableSides.erase(std::remove(sc_.playableSides.begin(), sc_.playableSides.end(), s), sc_.playableSides.end());
            if (on) sc_.playableSides.push_back(s);
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    sectionHeader(tr("editor.groups"));
    for (size_t i = 0; i < sc_.groups.size(); ++i) {
        auto& g = sc_.groups[i];
        ImGui::PushID(int(i));
        ImGui::PushStyleColor(ImGuiCol_Text, sideColor(g.side));
        if (ImGui::Selectable((loc::has(g.name) ? std::string(tr(g.name.c_str())) : g.name).c_str(), int(i) == selGroup_)) selGroup_ = int(i);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (ImGui::Button(tr("editor.add_allied"))) addGroup(sim::Side::Allies, map_.center);
    ImGui::SameLine();
    if (ImGui::Button(tr("editor.add_axis"))) addGroup(sim::Side::Axis, map_.center);
}

void ScenarioEditor::drawGroup(game::ScenarioGroup& g) {
    sectionHeader(tr("editor.group"));
    char name[128];
    copyStr(name, sizeof(name), loc::has(g.name) ? tr(g.name.c_str()) : g.name);
    if (ImGui::InputText(tr("editor.name"), name, sizeof(name))) g.name = name;
    int side = g.side == sim::Side::Axis ? 1 : 0;
    const char* sides[] = {sideName(sim::Side::Allies), sideName(sim::Side::Axis)};
    if (ImGui::Combo(tr("editor.side"), &side, sides, 2)) g.side = side ? sim::Side::Axis : sim::Side::Allies;
    static const sim::Formation forms[] = {sim::Formation::Column, sim::Formation::LineAbreast, sim::Formation::EchelonRight, sim::Formation::EchelonLeft,
                                           sim::Formation::Screen};
    int fi = 0;
    for (int i = 0; i < 5; ++i)
        if (forms[i] == g.formation) fi = i;
    const char* fnames[5];
    for (int i = 0; i < 5; ++i) fnames[i] = tr(sim::formationKey(forms[i]));
    if (ImGui::Combo(tr("ship.formation"), &fi, fnames, 5)) g.formation = forms[fi];
    float spacing = float(g.spacing), course = float(g.course), speed = float(g.speedKn), ready = float(g.readiness), appear = float(g.appearTime / 60.0);
    if (ImGui::SliderFloat(tr("editor.spacing"), &spacing, 300, 3000, "%.0f m")) g.spacing = spacing;
    if (ImGui::SliderFloat(tr("ui.course"), &course, 0, 359, "%.0f\xC2\xB0")) g.course = course;
    if (ImGui::SliderFloat(tr("ui.speed"), &speed, 0, 35, "%.0f kn")) g.speedKn = speed;
    int crew = int(g.crew);
    const char* crews[] = {crewName(sim::CrewQuality::Green), crewName(sim::CrewQuality::Regular), crewName(sim::CrewQuality::Veteran),
                           crewName(sim::CrewQuality::Elite)};
    if (ImGui::Combo(tr("editor.crew"), &crew, crews, 4)) g.crew = sim::CrewQuality(crew);
    if (ImGui::SliderFloat(tr("editor.readiness"), &ready, 0.2f, 1.0f, "%.2f")) g.readiness = ready;
    helpMarker(tr("editor.readiness_help"));
    if (ImGui::SliderFloat(tr("editor.appear"), &appear, 0, 120, "%.0f min")) g.appearTime = appear * 60.0;
    ImGui::Checkbox(tr("editor.playable"), &g.playable);
    if (ImGui::BeginCombo(tr("editor.doctrine"), g.doctrine.c_str())) {
        for (const auto& d : ui_.doctrines_.all())
            if (ImGui::Selectable(d.id.c_str(), d.id == g.doctrine)) g.doctrine = d.id;
        ImGui::EndCombo();
    }
    sectionHeader(tr("editor.mission"));
    int mt = int(g.mission.type);
    const char* missions[7];
    for (int i = 0; i < 7; ++i) missions[i] = tr(ai::missionKey(ai::MissionType(i)));
    if (ImGui::Combo(tr("editor.mission_type"), &mt, missions, 7)) g.mission.type = ai::MissionType(mt);
    float ms = float(g.mission.speedKn), as = float(g.mission.attackSpeedKn);
    if (ImGui::SliderFloat(tr("editor.mission_speed"), &ms, 0, 35, "%.0f kn")) g.mission.speedKn = ms;
    if (ImGui::SliderFloat(tr("editor.attack_speed"), &as, 10, 38, "%.0f kn")) g.mission.attackSpeedKn = as;
    ImGui::Checkbox(tr("editor.loop"), &g.mission.loop);
    ImGui::TextDisabled("%s: %zu  (%s)", tr("editor.waypoints"), g.mission.waypoints.size(), tr("editor.waypoint_hint"));
    sectionHeader(tr("editor.ships"));
    for (size_t i = 0; i < g.ships.size(); ++i) {
        auto& s = g.ships[i];
        ImGui::PushID(int(i));
        char nb[64];
        copyStr(nb, sizeof(nb), s.name);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.42f);
        if (ImGui::InputText("##n", nb, sizeof(nb))) s.name = nb;
        ImGui::SameLine();
        const sim::ShipClassDef* c = ui_.db().shipClass(s.cls);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() * 1.4f);
        if (ImGui::BeginCombo("##c", c ? c->name.c_str() : s.cls.c_str())) {
            for (const auto& cc : ui_.db().shipClasses())
                if (sim::defaultSide(cc->nation) == g.side)
                    if (ImGui::Selectable((cc->name + " (" + shipTypeAbbrev(cc->type) + ")").c_str(), cc->id == s.cls)) s.cls = cc->id;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            g.ships.erase(g.ships.begin() + long(i));
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (ImGui::Button(tr("editor.add_ship"))) {
        game::ScenarioShip s;
        for (const auto& cc : ui_.db().shipClasses())
            if (sim::defaultSide(cc->nation) == g.side) {
                s.cls = cc->id;
                s.name = cc->ships.empty() ? cc->name : cc->ships[g.ships.size() % cc->ships.size()];
                break;
            }
        g.ships.push_back(s);
    }
    ImGui::Spacing();
    if (ImGui::Button(tr("editor.delete_group"))) {
        sc_.groups.erase(sc_.groups.begin() + selGroup_);
        selGroup_ = -1;
    }
}

}  // namespace it::ui
