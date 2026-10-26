// Menus and full-screen pages: main menu, mission selection, briefing, settings, credits, debrief.
#include <algorithm>
#include <cmath>

#include "core/Localization.h"
#include "core/Random.h"
#include "ui/Format.h"
#include "ui/GameUi.h"
#include "ui/ScenarioEditor.h"
#include "ui/Theme.h"
#include "ui/UnitReference.h"
#include "ui/Widgets.h"

namespace it::ui {

namespace {

constexpr ImGuiWindowFlags kPage = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;

bool bigButton(const char* label, float width) {
    FontScope f(fonts().heading, 1.35f);
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0.0f, 0.5f});
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4{0.12f, 0.17f, 0.20f, 0.85f});
    bool r = ImGui::Button(label, {width, ImGui::GetFontSize() * 1.65f});
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    return r;
}

void title(const char* text, float rel = 2.2f) {
    FontScope f(fonts().heading, rel);
    ImGui::TextColored(col::kBrass, "%s", text);
}

void wrapped(const char* text) {
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextUnformatted(text);
    ImGui::PopTextWrapPos();
}

std::string dateText(const sim::DateTime& d) {
    char b[64];
    std::snprintf(b, sizeof(b), "%02d.%02d.%d  %s", d.day, d.month, d.year, fmtClock(d.hour).c_str());
    return b;
}

const char* moonPhaseKey(double illum) {
    if (illum < 0.05) return "env.moon_new";
    if (illum < 0.35) return "env.moon_crescent";
    if (illum < 0.65) return "env.moon_quarter";
    if (illum < 0.95) return "env.moon_gibbous";
    return "env.moon_full";
}

const char* lightKey(double ambient) {
    if (ambient > 0.6) return "env.light_day";
    if (ambient > 0.25) return "env.light_twilight";
    if (ambient > 0.08) return "env.light_moonlit";
    return "env.light_dark";
}

}  // namespace

// ------------------------------------------------------------------------------------------- main menu
void GameUi::drawMainMenu() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    // Darkened gradient on the left for legibility over the live backdrop.
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    bg->AddRectFilledMultiColor({0, 0}, {W * 0.42f, H}, IM_COL32(4, 8, 12, 225), IM_COL32(4, 8, 12, 0), IM_COL32(4, 8, 12, 0), IM_COL32(4, 8, 12, 225));
    ImGui::SetNextWindowPos({W * 0.06f, H * 0.12f});
    ImGui::SetNextWindowSize({std::max(380.0f, W * 0.28f), H * 0.8f});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4{0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4{0, 0, 0, 0});
    ImGui::Begin("##mainmenu", nullptr, kPage);
    {
        FontScope f(fonts().heading, 4.2f);
        ImGui::TextColored(col::kBrass, "IRON TIDE");
    }
    {
        FontScope f(fonts().heading, 1.6f);
        ImGui::TextColored(col::kTextDim, "%s", tr("menu.subtitle"));
    }
    ImGui::Dummy({0, H * 0.06f});
    float bw = ImGui::GetContentRegionAvail().x * 0.85f;
    if (bigButton(tr("menu.missions"), bw)) go(Screen::Missions);
    if (bigButton(tr("menu.editor"), bw)) {
        editor_->newScenario();
        go(Screen::Editor);
    }
    if (bigButton(tr("menu.unitref"), bw)) go(Screen::UnitReference);
    if (bigButton(tr("menu.settings"), bw)) {
        settingsReturn_ = Screen::MainMenu;
        go(Screen::Settings);
    }
    if (bigButton(tr("menu.credits"), bw)) go(Screen::Credits);
    if (bigButton(tr("menu.quit"), bw)) svc.quit();
    ImGui::End();
    ImGui::PopStyleColor(2);

    // Footer.
    ImGui::SetNextWindowPos({W - 12.0f, H - 10.0f}, ImGuiCond_Always, {1.0f, 1.0f});
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##footer", nullptr, kPage | ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextDisabled("%s  |  %s", tr("menu.backdrop_caption"), "v0.1");
    ImGui::End();
}

// -------------------------------------------------------------------------------------------- missions
void GameUi::drawMissions() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImGui::SetNextWindowPos({W * 0.05f, H * 0.06f});
    ImGui::SetNextWindowSize({W * 0.9f, H * 0.88f});
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::Begin("##missions", nullptr, kPage);
    title(tr("missions.title"));
    ImGui::Separator();
    float listW = ImGui::GetContentRegionAvail().x * 0.36f;
    ImGui::BeginChild("list", {listW, -ImGui::GetFrameHeightWithSpacing() * 1.4f}, ImGuiChildFlags_Borders);
    for (size_t i = 0; i < scenarios_.size(); ++i) {
        const auto& sc = scenarios_[i];
        ImGui::PushID(int(i));
        bool sel = int(i) == selectedScenario_;
        if (ImGui::Selectable("##s", sel, ImGuiSelectableFlags_AllowDoubleClick, {0, ImGui::GetFontSize() * 2.6f})) {
            selectedScenario_ = int(i);
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) go(Screen::Briefing);
        }
        ImGui::SameLine(ImGui::GetStyle().ItemSpacing.x * 2);
        ImGui::BeginGroup();
        {
            FontScope f(fonts().bold, 1.1f);
            ImGui::TextUnformatted(tr(sc.title.c_str()));
        }
        ImGui::TextDisabled("%s  -  %s", dateText(sc.date).c_str(), tr(("theater." + sc.theater).c_str()));
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (scenarios_.empty()) ImGui::TextDisabled("%s", tr("missions.none"));
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("detail", {0, -ImGui::GetFrameHeightWithSpacing() * 1.4f}, ImGuiChildFlags_Borders);
    if (selectedScenario_ >= 0 && size_t(selectedScenario_) < scenarios_.size()) {
        const auto& sc = scenarios_[size_t(selectedScenario_)];
        title(tr(sc.title.c_str()), 1.7f);
        ImGui::TextDisabled("%s", dateText(sc.date).c_str());
        ImGui::Separator();
        wrapped(tr(sc.briefing.c_str()));
        ImGui::Spacing();
        sectionHeader(tr("briefing.forces"));
        for (sim::Side side : {sim::Side::Allies, sim::Side::Axis}) {
            int ships = 0;
            double tons = 0;
            for (const auto& g : sc.groups)
                if (g.side == side)
                    for (const auto& s : g.ships) {
                        ++ships;
                        if (const auto* c = db_.shipClass(s.cls)) tons += c->displacementStd;
                    }
            ImGui::TextColored(sideColor(side), "%s:", sideName(side));
            ImGui::SameLine();
            ImGui::Text("%d %s, %s", ships, tr("briefing.ships"), fmtTonnes(tons).c_str());
        }
    }
    ImGui::EndChild();
    if (ImGui::Button(tr("ui.back"), {160, 0})) go(Screen::MainMenu);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 220);
    ImGui::BeginDisabled(scenarios_.empty());
    if (ImGui::Button(tr("missions.to_briefing"), {220, 0})) go(Screen::Briefing);
    ImGui::EndDisabled();
    ImGui::End();
}

// -------------------------------------------------------------------------------------------- briefing
void GameUi::drawBriefing() {
    if (selectedScenario_ < 0 || size_t(selectedScenario_) >= scenarios_.size()) {
        go(Screen::Missions);
        return;
    }
    const game::Scenario& sc = scenarios_[size_t(selectedScenario_)];
    if (chosenSideFor_ != sc.id) {
        chosenSide_ = sc.defaultPlayer;
        chosenSideFor_ = sc.id;
    }
    if (std::find(sc.playableSides.begin(), sc.playableSides.end(), chosenSide_) == sc.playableSides.end()) chosenSide_ = sc.defaultPlayer;
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImGui::SetNextWindowPos({W * 0.05f, H * 0.05f});
    ImGui::SetNextWindowSize({W * 0.9f, H * 0.9f});
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::Begin("##briefing", nullptr, kPage);
    title(tr(sc.title.c_str()));
    ImGui::TextDisabled("%s  |  %s", dateText(sc.date).c_str(), tr(("theater." + sc.theater).c_str()));
    ImGui::Separator();
    float colW = ImGui::GetContentRegionAvail().x * 0.58f;
    ImGui::BeginChild("text", {colW, -ImGui::GetFrameHeightWithSpacing() * 1.5f});
    sectionHeader(tr("briefing.situation"));
    wrapped(tr(sc.briefing.c_str()));
    std::string orders = "scenario." + sc.id + ".orders_" + std::string(chosenSide_ == sim::Side::Axis ? "axis" : "allies");
    if (loc::has(orders)) {
        sectionHeader(tr("briefing.orders"));
        wrapped(tr(orders.c_str()));
    }
    std::string intel = "scenario." + sc.id + ".intel_" + std::string(chosenSide_ == sim::Side::Axis ? "axis" : "allies");
    if (loc::has(intel)) {
        sectionHeader(tr("briefing.intel"));
        wrapped(tr(intel.c_str()));
    }
    if (loc::has(sc.history)) {
        sectionHeader(tr("briefing.history"));
        ImGui::PushStyleColor(ImGuiCol_Text, col::kTextDim);
        wrapped(tr(sc.history.c_str()));
        ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("side", {0, -ImGui::GetFrameHeightWithSpacing() * 1.5f}, ImGuiChildFlags_Borders);
    // Conditions (computed from the actual astronomy for the scenario date and position).
    {
        sim::Environment env;
        Rng rng(1);
        env.configure(sc.lat, sc.lon, sc.date, sc.weather, {0, 0}, 40000.0, rng);
        env.update(0.0, 0.0);
        sectionHeader(tr("briefing.conditions"));
        if (ImGui::BeginTable("cond", 2, ImGuiTableFlags_SizingStretchProp)) {
            auto row = [](const char* k, const std::string& v) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", k);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v.c_str());
            };
            row(tr("env.light"), tr(lightKey(env.ambientLight())));
            row(tr("env.moon"), std::string(tr(moonPhaseKey(env.moonIllumination()))) + (env.moonAltitudeDeg() > 0 ? "" : std::string(" (") + tr("env.moon_down") + ")"));
            row(tr("env.sun"), fmtClock(sc.date.hour + env.secondsUntilSunAbove(-6.0) / 3600.0) + " " + tr("env.dawn"));
            row(tr("env.clouds"), fmtPercent(sc.weather.cloudCover));
            row(tr("env.visibility"), fmtRange(sc.weather.visibilityKm * 1000.0, settings.metric));
            row(tr("env.sea_state"), std::to_string(int(sc.weather.seaState)));
            row(tr("env.wind"), fmtBearing(sc.weather.windDirDeg) + " " + fmtKnots(sc.weather.windSpeedKn));
            row(tr("env.squalls"), sc.weather.squalls > 0 ? tr("ui.yes") : tr("ui.no"));
            row(tr("briefing.time_limit"), fmtDuration(sc.timeLimit));
            ImGui::EndTable();
        }
    }
    sectionHeader(tr("briefing.your_forces"));
    for (const auto& g : sc.groups) {
        if (g.side != chosenSide_) continue;
        ImGui::PushID(g.id.c_str());
        if (ImGui::TreeNodeEx("##g", ImGuiTreeNodeFlags_DefaultOpen, "%s", tr(g.name.c_str()))) {
            for (const auto& s : g.ships) {
                const sim::ShipClassDef* c = db_.shipClass(s.cls);
                ImGui::Bullet();
                ImGui::Text("%s", s.name.c_str());
                if (c) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s %s", c->name.c_str(), shipTypeAbbrev(c->type));
                    ImGui::SameLine();
                    ImGui::PushID(s.name.c_str());
                    if (ImGui::SmallButton(tr("ui.info"))) {
                        unitRef_->select(c->id);
                        go(Screen::UnitReference);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    sectionHeader(tr("briefing.command"));
    if (sc.playableSides.size() > 1) {
        for (sim::Side s : sc.playableSides) {
            if (ImGui::RadioButton(sideName(s), chosenSide_ == s)) chosenSide_ = s;
            ImGui::SameLine();
        }
        ImGui::NewLine();
    }
    ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("(?)").x + ImGui::GetStyle().ItemSpacing.x));  // leave room for the help marker
    ImGui::SliderFloat("##skill", &settings.aiSkill, 0.3f, 1.0f, tr("briefing.ai_skill_fmt"));
    helpMarker(tr("briefing.ai_skill_help"));
    const char* crewOpts[] = {tr("briefing.crew_weaker"), tr("briefing.crew_historical"), tr("briefing.crew_stronger")};
    int crewIdx = settings.enemyCrewShift + 1;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##crew", &crewIdx, crewOpts, 3)) settings.enemyCrewShift = crewIdx - 1;
    ImGui::Checkbox(tr("settings.auto_evade"), &settings.autoEvade);
    ImGui::EndChild();
    if (ImGui::Button(tr("ui.back"), {160, 0})) go(Screen::Missions);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 260);
    {
        FontScope f(fonts().bold, 1.15f);
        if (ImGui::Button(tr("briefing.start"), {260, 0})) {
            game::Scenario copy = sc;
            startBattle(copy, chosenSide_);
        }
    }
    ImGui::End();
}

// -------------------------------------------------------------------------------------------- settings
void GameUi::drawSettings(bool inGame) {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImGui::SetNextWindowPos({W * 0.5f, H * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(W * 0.9f, 820.0f), std::min(H * 0.9f, 640.0f)});
    ImGui::SetNextWindowBgAlpha(0.96f);
    ImGui::Begin("##settings", nullptr, kPage & ~ImGuiWindowFlags_NoBringToFrontOnFocus);
    title(tr("settings.title"), 1.8f);
    bool changed = false;
    if (ImGui::BeginTabBar("tabs")) {
        if (ImGui::BeginTabItem(tr("settings.general"))) {
            const char* langs[] = {"Deutsch", "English"};
            int li = settings.language == "en" ? 1 : 0;
            if (ImGui::Combo(tr("settings.language"), &li, langs, 2)) {
                settings.language = li == 1 ? "en" : "de";
                loc::load(settings.language);
                changed = true;
            }
            int units = settings.metric ? 1 : 0;
            const char* unitOpts[] = {tr("settings.units_nautical"), tr("settings.units_metric")};
            if (ImGui::Combo(tr("settings.units"), &units, unitOpts, 2)) {
                settings.metric = units == 1;
                changed = true;
            }
            changed |= ImGui::SliderFloat(tr("settings.ui_scale"), &settings.uiScale, 0.75f, 1.75f, "%.2f");
            changed |= ImGui::Checkbox(tr("settings.hints"), &settings.showTutorialHints);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("settings.graphics"))) {
            ImGui::TextDisabled("%s: %s", tr("settings.gpu"), svc.gpuDescription().c_str());
            changed |= ImGui::Checkbox(tr("settings.fullscreen"), &settings.fullscreen);
            changed |= ImGui::Checkbox(tr("settings.vsync"), &settings.vsync);
            const char* msaaOpts[] = {"1x", "2x", "4x", "8x"};
            int mi = settings.msaa >= 8 ? 3 : settings.msaa >= 4 ? 2 : settings.msaa >= 2 ? 1 : 0;
            if (ImGui::Combo(tr("settings.msaa"), &mi, msaaOpts, 4)) {
                settings.msaa = 1 << mi;
                changed = true;
            }
            const char* shadowOpts[] = {"1024", "2048", "4096"};
            int si = settings.shadowSize >= 4096 ? 2 : settings.shadowSize >= 2048 ? 1 : 0;
            if (ImGui::Combo(tr("settings.shadows"), &si, shadowOpts, 3)) {
                settings.shadowSize = 1024 << si;
                changed = true;
            }
            changed |= ImGui::SliderFloat(tr("settings.exposure"), &settings.exposureBias, -2.0f, 2.0f, "%+.1f EV");
            changed |= ImGui::SliderFloat(tr("settings.bloom"), &settings.bloom, 0.0f, 0.2f, "%.2f");
            changed |= ImGui::Checkbox(tr("settings.film_grain"), &settings.filmGrain);
            ImGui::TextDisabled("%s", tr("settings.restart_hint"));
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("settings.audio"))) {
            changed |= ImGui::SliderFloat(tr("settings.master"), &settings.masterVolume, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat(tr("settings.sfx"), &settings.sfxVolume, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat(tr("settings.voice"), &settings.voiceVolume, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::SliderFloat(tr("settings.ambience"), &settings.ambienceVolume, 0.0f, 1.0f, "%.2f");
            changed |= ImGui::Checkbox(tr("settings.voice_reports"), &settings.voiceReports);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("settings.gameplay"))) {
            changed |= ImGui::SliderFloat(tr("settings.ai_skill"), &settings.aiSkill, 0.3f, 1.0f, "%.2f");
            helpMarker(tr("briefing.ai_skill_help"));
            changed |= ImGui::SliderInt(tr("settings.enemy_crew"), &settings.enemyCrewShift, -1, 1);
            changed |= ImGui::Checkbox(tr("settings.auto_evade"), &settings.autoEvade);
            changed |= ImGui::Checkbox(tr("settings.pause_on_contact"), &settings.pauseOnContact);
            changed |= ImGui::Checkbox(tr("settings.reveal_ai"), &settings.revealEnemyAi);
            helpMarker(tr("settings.reveal_ai_help"));
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("settings.controls"))) {
            static const char* keys[][2] = {{"Space", "keys.pause"},     {"1 - 6", "keys.timescale"}, {"M / F1 / F2 / F3", "keys.views"},
                                            {"Tab", "keys.next_ship"},   {"F", "keys.follow"},        {"B / V", "keys.bridge"},
                                            {"I", "keys.insight"},       {"U", "keys.unitref"},       {"W A S D", "keys.free_cam"},
                                            {"RMB", "keys.orbit"},       {"Ctrl + LMB", "keys.measure"}, {"Esc", "keys.menu"}};
            if (ImGui::BeginTable("keys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                for (auto& k : keys) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextColored(col::kBrass, "%s", k[0]);
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(tr(k[1]));
                }
                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (changed) {
        svc.settingsChanged(settings);
        if (!settingsPath.empty()) settings.save(settingsPath);
    }
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().WindowPadding.y);
    if (ImGui::Button(tr("ui.close"), {160, 0})) {
        if (inGame) showSettingsInBattle_ = false;
        else go(settingsReturn_);
    }
    ImGui::End();
}

// --------------------------------------------------------------------------------------------- credits
void GameUi::drawCredits() {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImGui::SetNextWindowPos({W * 0.5f, H * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({std::min(W * 0.9f, 900.0f), H * 0.85f});
    ImGui::SetNextWindowBgAlpha(0.95f);
    ImGui::Begin("##credits", nullptr, kPage);
    title(tr("menu.credits"), 1.8f);
    ImGui::BeginChild("c", {0, -ImGui::GetFrameHeightWithSpacing() * 1.4f});
    sectionHeader(tr("credits.game"));
    wrapped(tr("credits.game_text"));
    sectionHeader(tr("credits.data"));
    wrapped("Terrain Tiles (Mapzen / Tilezen, AWS Open Data Registry): SRTM, GMTED2010, ETOPO1 and others. "
            "https://github.com/tilezen/joerd/blob/master/docs/attribution.md");
    sectionHeader(tr("credits.libraries"));
    wrapped("Dear ImGui (MIT) - Omar Cornut and contributors\ncgltf (MIT) - Johannes Kuhlmann\nstb_image / stb_image_write (Public Domain / MIT) - "
            "Sean Barrett\nnlohmann/json (MIT) - Niels Lohmann\ndoctest (MIT) - Viktor Kirilov");
    sectionHeader(tr("credits.fonts"));
    wrapped("Barlow, Barlow Condensed - The Barlow Project Authors (SIL Open Font License 1.1)\nIBM Plex Mono - IBM Corp. (SIL Open Font License 1.1)");
    sectionHeader(tr("credits.audio"));
    wrapped(tr("credits.audio_text"));
    sectionHeader(tr("credits.history"));
    wrapped(tr("credits.history_text"));
    ImGui::EndChild();
    if (ImGui::Button(tr("ui.back"), {160, 0})) go(Screen::MainMenu);
    ImGui::End();
}

// --------------------------------------------------------------------------------------------- debrief
void GameUi::drawDebrief() {
    if (!session_) {
        go(Screen::MainMenu);
        return;
    }
    const sim::World& w = session_->world();
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({W, H});
    ImGui::Begin("##debrief", nullptr, kPage);
    sim::Side me = playerSide() == sim::Side::Neutral ? sim::Side::Allies : playerSide();
    title(tr("debrief.title"));
    ImGui::SameLine();
    double diff = resultPlayer_.points - resultEnemy_.points;
    const char* verdict = diff > 150 ? "debrief.decisive_victory" : diff > 25 ? "debrief.victory" : diff > -25 ? "debrief.draw" : diff > -150 ? "debrief.defeat"
                                                                                                                                   : "debrief.decisive_defeat";
    {
        FontScope f(fonts().heading, 2.0f);
        ImGui::TextColored(diff > 25 ? col::kGood : diff < -25 ? col::kBad : col::kWarn, "  %s", tr(verdict));
    }
    ImGui::TextDisabled("%s  |  %s %s  |  %s", tr(session_->scenario().title.c_str()), tr("debrief.duration"), fmtDuration(w.time()).c_str(),
                        tr(session_->endReason().empty() ? "end.player" : session_->endReason().c_str()));
    ImGui::Separator();
    float leftW = W * 0.42f;
    ImGui::BeginChild("left", {leftW, -ImGui::GetFrameHeightWithSpacing() * 1.4f});
    if (ImGui::BeginTable("res", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("");
        ImGui::TableSetupColumn(sideName(me));
        ImGui::TableSetupColumn(sideName(sim::enemyOf(me)));
        ImGui::TableHeadersRow();
        auto row = [&](const char* label, const std::string& a, const std::string& b) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", label);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(a.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(b.c_str());
        };
        row(tr("debrief.ships_lost"), std::to_string(resultPlayer_.shipsLost), std::to_string(resultEnemy_.shipsLost));
        row(tr("debrief.tonnage_lost"), fmtTonnes(resultPlayer_.tonnageLost), fmtTonnes(resultEnemy_.tonnageLost));
        row(tr("debrief.damage"), fmtPercent(resultPlayer_.damageTaken), fmtPercent(resultEnemy_.damageTaken));
        row(tr("debrief.points"), std::to_string(int(resultPlayer_.points)), std::to_string(int(resultEnemy_.points)));
        ImGui::EndTable();
    }
    if (!resultPlayer_.objectivesMet.empty() || !resultPlayer_.objectivesFailed.empty()) {
        sectionHeader(tr("debrief.objectives"));
        for (auto& o : resultPlayer_.objectivesMet) textColored(col::kGood, "+ %s", tr(o.c_str()));
        for (auto& o : resultPlayer_.objectivesFailed) textColored(col::kBad, "- %s", tr(o.c_str()));
    }
    sectionHeader(tr("debrief.ships"));
    if (ImGui::BeginTable("ships", 7, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                          {0, ImGui::GetContentRegionAvail().y * 0.98f})) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("debrief.ship"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(tr("debrief.status"));
        ImGui::TableSetupColumn(tr("debrief.shells"));
        ImGui::TableSetupColumn(tr("debrief.hits"));
        ImGui::TableSetupColumn(tr("debrief.torps"));
        ImGui::TableSetupColumn(tr("debrief.torp_hits"));
        ImGui::TableSetupColumn(tr("debrief.hits_taken"));
        ImGui::TableHeadersRow();
        for (const auto& s : w.ships()) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextColored(sideColor(s->side), "%s", s->name.c_str());
            if (s->cls) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", shipTypeAbbrev(s->cls->type));
            }
            ImGui::TableNextColumn();
            if (s->dmg.sunk || s->dmg.sinking) textColored(col::kBad, "%s", tr("debrief.sunk"));
            else if (s->dmg.hullIntegrity < 0.5 || s->dmg.propulsion < 0.3) textColored(col::kWarn, "%s", tr("debrief.crippled"));
            else if (s->dmg.hitsTaken > 0) textColored(col::kWarn, "%s", tr("debrief.damaged"));
            else textColored(col::kGood, "%s", tr("debrief.intact"));
            ImGui::TableNextColumn();
            ImGui::Text("%d", s->stats.shellsFired);
            ImGui::TableNextColumn();
            ImGui::Text("%d", s->stats.hitsScored);
            ImGui::TableNextColumn();
            ImGui::Text("%d", s->stats.torpedoesFired);
            ImGui::TableNextColumn();
            ImGui::Text("%d", s->stats.torpedoHits);
            ImGui::TableNextColumn();
            ImGui::Text("%d / %d", s->dmg.hitsTaken, s->dmg.torpedoHitsTaken);
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("right", {0, -ImGui::GetFrameHeightWithSpacing() * 1.4f});
    if (ImGui::BeginTabBar("dtabs")) {
        if (ImGui::BeginTabItem(tr("debrief.plot"))) {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            if (!mapFitted_) {
                Vec2 lo{1e18, 1e18}, hi{-1e18, -1e18};
                for (const auto& t : plot_)
                    for (const auto& p : t.points) {
                        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y)};
                        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y)};
                    }
                if (lo.x < hi.x) debriefMap_.fit(lo - Vec2{3000, 3000}, hi + Vec2{3000, 3000}, avail);
                mapFitted_ = true;
            }
            MapInput in;
            in.world = &w;
            in.terrain = &w.terrain();
            in.chartTexture = svc.chartTexture();
            in.side = sim::Side::Neutral;
            in.metric = settings.metric;
            in.plot = &plot_;
            debriefMap_.draw("plot", avail, in);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("debrief.ai_log"))) {
            insightFilter_ = 2;
            drawAiInsight();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("debrief.log"))) {
            ImGui::BeginChild("log");
            for (const auto& l : log_) {
                ImGui::TextDisabled("%s", fmtDuration(l.time).c_str());
                ImGui::SameLine();
                ImGui::TextColored(l.color, "%s", l.text.c_str());
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::EndChild();
    if (ImGui::Button(tr("debrief.to_missions"), {220, 0})) {
        session_.reset();
        go(Screen::Missions);
    }
    ImGui::SameLine();
    if (ImGui::Button(tr("debrief.replay"), {220, 0})) {
        game::Scenario sc = session_->scenario();
        sim::Side side = session_->player();
        startBattle(sc, side);
    }
    ImGui::End();
}

// ------------------------------------------------------------------------------------------ pause menu
void GameUi::drawPauseMenu() {
    ImGuiIO& io = ImGui::GetIO();
    ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, io.DisplaySize, IM_COL32(0, 0, 0, 110));
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f}, ImGuiCond_Always, {0.5f, 0.5f});
    ImGui::SetNextWindowSize({360, 0});
    ImGui::Begin("##pause", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    title(tr("pause.title"), 1.6f);
    float bw = ImGui::GetContentRegionAvail().x;
    if (bigButton(tr("pause.resume"), bw)) pauseMenu_ = false;
    if (bigButton(tr("menu.unitref"), bw)) {
        showUnitRefInBattle_ = true;
        pauseMenu_ = false;
    }
    if (bigButton(tr("menu.settings"), bw)) {
        showSettingsInBattle_ = true;
        pauseMenu_ = false;
    }
    if (bigButton(tr("pause.end_battle"), bw)) endBattle();
    if (bigButton(tr("pause.abandon"), bw)) {
        session_.reset();
        go(Screen::MainMenu);
    }
    ImGui::End();
}

}  // namespace it::ui
