// Battle interface: top bar, fleet list, ship command panel (helm, weapons, damage, sensors), contact
// details, message log, contact list, tactical plot / mini-map, 3D labels and the AI insight window.
#include <algorithm>
#include <cmath>

#include "core/Localization.h"
#include "ui/Format.h"
#include "ui/GameUi.h"
#include "ui/Theme.h"
#include "ui/UnitReference.h"
#include "ui/Widgets.h"

namespace it::ui {

namespace {

constexpr ImGuiWindowFlags kPanel = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
                                    ImGuiWindowFlags_NoTitleBar;

struct Layout {
    float W, H, top, leftW, rightW, bottomH;
    ImVec2 centerMin() const { return {leftW, top}; }
    ImVec2 centerMax() const { return {W - rightW, H - bottomH}; }
};

Layout layout() {
    ImGuiIO& io = ImGui::GetIO();
    float f = ImGui::GetFontSize();
    Layout l;
    l.W = io.DisplaySize.x;
    l.H = io.DisplaySize.y;
    l.top = f * 2.6f;
    l.leftW = std::clamp(l.W * 0.17f, f * 15.0f, f * 21.0f);
    l.rightW = std::clamp(l.W * 0.22f, f * 21.0f, f * 27.0f);
    l.bottomH = std::clamp(l.H * 0.2f, f * 9.0f, f * 14.0f);
    return l;
}

const char* weaponsKey(sim::WeaponsStatus w) {
    switch (w) {
        case sim::WeaponsStatus::Hold: return "ui.weapons.hold";
        case sim::WeaponsStatus::Tight: return "ui.weapons.tight";
        default: return "ui.weapons.free";
    }
}

const char* levelKey(sim::ClassLevel l) {
    switch (l) {
        case sim::ClassLevel::Detected: return "contact.level.detected";
        case sim::ClassLevel::SizeKnown: return "contact.level.size";
        case sim::ClassLevel::TypeKnown: return "contact.level.type";
        case sim::ClassLevel::ClassKnown: return "contact.level.class";
        default: return "contact.level.identified";
    }
}

std::string senseText(uint32_t s) {
    std::string out;
    auto add = [&](uint32_t bit, const char* key) {
        if (s & bit) {
            if (!out.empty()) out += ", ";
            out += tr(key);
        }
    };
    add(sim::kSenseVisual, "sense.visual");
    add(sim::kSenseRadar, "sense.radar");
    add(sim::kSenseFlash, "sense.flash");
    add(sim::kSenseAircraft, "sense.aircraft");
    add(sim::kSenseReport, "sense.report");
    add(sim::kSenseHydrophone, "sense.hydrophone");
    return out.empty() ? std::string("-") : out;
}

/// Human-readable form of the structured detail strings the AI attaches to its decisions.
std::string describeDetail(const ai::DecisionRecord& r, bool metric) {
    if (loc::has(r.detail)) return tr(r.detail.c_str());
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t bar = r.detail.find('|', start);
        parts.push_back(r.detail.substr(start, bar == std::string::npos ? std::string::npos : bar - start));
        if (bar == std::string::npos) break;
        start = bar + 1;
    }
    if (r.decision.rfind("ai.o.", 0) == 0 && parts.size() == 3)
        return loc::format("insight.detail_target", {shipTypeName(sim::shipTypeFromString(parts[0])), fmtBearing(std::atof(parts[1].c_str())),
                                                     fmtRange(std::atof(parts[2].c_str()), metric)});
    if (r.decision == "ai.d.torpedoes_launched" && parts.size() == 3)
        return loc::format("insight.detail_torpedoes", {parts[0], parts[1], fmtRange(std::atof(parts[2].c_str()), metric)});
    if ((r.decision == "ai.d.comb_torpedo" || r.decision == "ai.d.evade_torpedo") && parts.size() == 1)
        return loc::format("insight.detail_course", {fmtBearing(std::atof(parts[0].c_str()))});
    return r.detail;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ battle
void GameUi::drawBattle() {
    if (!session_) {
        go(Screen::MainMenu);
        return;
    }
    if (showUnitRefInBattle_) {
        unitRef_->draw(true);
        return;
    }
    if (showSettingsInBattle_) {
        drawSettings(true);
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_H) && !ImGui::GetIO().WantTextInput) hideUi_ = !hideUi_;
    Layout L = layout();
    if (!mapFitted_) {
        const sim::World& w = session_->world();
        Vec2 lo{1e18, 1e18}, hi{-1e18, -1e18};
        for (const auto& s : w.ships())
            if (s->side == playerSide() || playerSide() == sim::Side::Neutral) {
                lo = {std::min(lo.x, s->pos.x), std::min(lo.y, s->pos.y)};
                hi = {std::max(hi.x, s->pos.x), std::max(hi.y, s->pos.y)};
            }
        if (lo.x <= hi.x) {
            map_.fit(lo - Vec2{15000, 15000}, hi + Vec2{15000, 15000}, {L.W - L.leftW - L.rightW, L.H - L.top - L.bottomH});
            miniMap_.focus((lo + hi) * 0.5, 120.0);
        }
        mapFitted_ = true;
    }
    if (view_ != View::Map) drawSceneLabels();
    if (!hideUi_) {
        drawTopBar();
        drawFleetPanel({0, L.top}, {L.leftW, L.H - L.top - L.bottomH});
        drawShipPanel({L.W - L.rightW, L.top}, {L.rightW, L.H - L.top});
        float logW = (L.W - L.rightW) * 0.55f;
        drawMessageLog({0, L.H - L.bottomH}, {logW, L.bottomH});
        drawContactsList({logW, L.H - L.bottomH}, {L.W - L.rightW - logW, L.bottomH});
        if (selTrack_) drawContactPanel();
    }
    ImVec2 cmin = L.centerMin(), cmax = L.centerMax();
    if (hideUi_) cmin = {0, 0}, cmax = {L.W, L.H};
    if (view_ == View::Map) {
        drawMapView(cmin, {cmax.x - cmin.x, cmax.y - cmin.y}, false);
    } else if (view_ == View::Split) {
        float mid = (cmin.x + cmax.x) * 0.5f;
        drawMapView({mid, cmin.y}, {cmax.x - mid, cmax.y - cmin.y}, false);
    } else if (!hideUi_) {
        float s = std::min(L.W, L.H) * 0.24f;
        drawMapView({cmax.x - s - 8, cmin.y + 8}, {s, s}, true);
    }
    if (showInsight_) {
        ImGui::SetNextWindowSize({L.W * 0.36f, L.H * 0.55f}, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos({L.leftW + 20, L.top + 40}, ImGuiCond_FirstUseEver);
        if (ImGui::Begin(tr("insight.title"), &showInsight_, ImGuiWindowFlags_NoSavedSettings)) {
            if (!settings.revealEnemyAi && insightFilter_ != 0) insightFilter_ = 0;
            drawAiInsight();
        }
        ImGui::End();
    }
    // 3D view interaction when the mouse is over no window.
    if (view_ != View::Map) handleSceneInput();
    if (pauseMenu_) drawPauseMenu();
    // Paused banner.
    if (session_->paused && !pauseMenu_) {
        const char* t = tr("ui.paused");
        FontScope f(fonts().heading, 1.6f);
        ImVec2 ts = ImGui::CalcTextSize(t);
        ImGui::GetForegroundDrawList()->AddText({(L.W - ts.x) * 0.5f, L.top + 10}, toU32(col::kBrass, 0.9f), t);
    }
}

void GameUi::drawTopBar() {
    Layout L = layout();
    const sim::World& w = session_->world();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({L.W, L.top});
    ImGui::Begin("##topbar", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar);
    {
        FontScope f(fonts().heading, 1.3f);
        ImGui::TextColored(col::kBrass, "%s", tr(session_->scenario().title.c_str()));
    }
    ImGui::SameLine();
    const auto& env = w.env();
    {
        FontScope f(fonts().mono, 1.1f);
        ImGui::Text("  %s", fmtClock(env.localHour()).c_str());
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%s  |  %s %s", tr(env.ambientLight() > 0.6 ? "env.light_day" : env.ambientLight() > 0.25 ? "env.light_twilight"
                                                                         : env.ambientLight() > 0.08 ? "env.light_moonlit"
                                                                                                     : "env.light_dark"),
                        tr("env.elapsed"), fmtDuration(w.time()).c_str());
    // Time controls, then the view toggles right-aligned (widths measured so nothing overlaps).
    static const double scales[] = {1, 2, 4, 8, 15, 30};
    const ImGuiStyle& st = ImGui::GetStyle();
    float bw = ImGui::GetFontSize() * 2.4f;
    auto btnW = [&](const char* t) { return ImGui::CalcTextSize(t).x + st.FramePadding.x * 2.0f; };
    const char* rightLabels[] = {tr("view.3d"), tr("view.map"), tr("view.split"), tr("insight.short"), tr("menu.unitref_short"), tr("ui.menu")};
    float rightW = 0;
    for (auto* l : rightLabels) rightW += btnW(l) + st.ItemSpacing.x;
    float timeW = (bw + st.ItemSpacing.x) * 7.0f;
    float timeX = std::max(ImGui::GetCursorPosX() + st.ItemSpacing.x * 3.0f, std::min(L.W * 0.5f - timeW * 0.5f, L.W - rightW - timeW - 20.0f));
    ImGui::SameLine(timeX);
    if (toggleButton(session_->paused ? ">" : "||", session_->paused, {bw, 0})) session_->paused = !session_->paused;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("keys.pause"));
    for (double sc : scales) {
        ImGui::SameLine();
        char b[16];
        std::snprintf(b, sizeof(b), "%gx", sc);
        if (toggleButton(b, !session_->paused && std::abs(session_->timeScale - sc) < 0.01, {bw, 0})) {
            session_->timeScale = sc;
            session_->paused = false;
        }
    }
    ImGui::SameLine(L.W - rightW - st.WindowPadding.x);
    if (toggleButton(rightLabels[0], view_ == View::Scene3D)) view_ = View::Scene3D;
    ImGui::SameLine();
    if (toggleButton(rightLabels[1], view_ == View::Map)) view_ = View::Map;
    ImGui::SameLine();
    if (toggleButton(rightLabels[2], view_ == View::Split)) view_ = View::Split;
    ImGui::SameLine();
    if (toggleButton(rightLabels[3], showInsight_)) showInsight_ = !showInsight_;
    ImGui::SameLine();
    if (ImGui::Button(rightLabels[4])) showUnitRefInBattle_ = true;
    ImGui::SameLine();
    if (ImGui::Button(rightLabels[5])) pauseMenu_ = true;
    ImGui::End();
}

void GameUi::drawFleetPanel(ImVec2 pos, ImVec2 size) {
    const sim::World& w = session_->world();
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##fleet", nullptr, kPanel);
    sectionHeader(tr("fleet.title"));
    sim::Side me = playerSide();
    for (const auto& g : w.groups()) {
        if (me != sim::Side::Neutral && g.side != me) continue;
        bool any = false;
        for (auto id : g.members)
            if (const sim::Ship* s = w.ship(id); s && !s->dmg.sunk) any = true;
        if (!any) continue;
        ImGui::PushID(g.id);
        std::string head = std::string(tr(g.name.c_str()));
        if (!g.playerControlled && me != sim::Side::Neutral) head += std::string("  (") + tr("fleet.ai_controlled") + ")";
        ImGui::PushStyleColor(ImGuiCol_Text, me == sim::Side::Neutral ? sideColor(g.side) : col::kText);
        bool open = ImGui::TreeNodeEx("##grp", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth, "%s", head.c_str());
        ImGui::PopStyleColor();
        if (open) {
            for (auto id : g.members) {
                const sim::Ship* s = w.ship(id);
                if (!s) continue;
                ImGui::PushID(int(id));
                bool sel = id == selShip_;
                ImVec4 c = s->dmg.sunk || s->dmg.sinking ? col::kTextDim : s->dmg.hitsTaken > 0 ? col::kWarn : col::kText;
                ImGui::PushStyleColor(ImGuiCol_Text, c);
                if (ImGui::Selectable("##ship", sel, ImGuiSelectableFlags_SpanAllColumns)) selectShip(id);
                ImGui::SameLine(0, 0);
                ImGui::Text("%s %s", s->cls ? shipTypeAbbrev(s->cls->type) : "", s->name.c_str());
                ImGui::PopStyleColor();
                ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.62f + ImGui::GetCursorStartPos().x);
                if (s->dmg.sunk || s->dmg.sinking) ImGui::TextColored(col::kBad, "%s", tr("debrief.sunk"));
                else {
                    float hp = float(std::clamp(s->dmg.hullIntegrity, 0.0, 1.0));
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    float bw = ImGui::GetFontSize() * 2.6f, bh = ImGui::GetFontSize() * 0.35f;
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRectFilled({p.x, p.y + 6}, {p.x + bw, p.y + 6 + bh}, IM_COL32(40, 50, 55, 255));
                    dl->AddRectFilled({p.x, p.y + 6}, {p.x + bw * hp, p.y + 6 + bh}, toU32(hp > 0.7f ? col::kGood : hp > 0.4f ? col::kWarn : col::kBad));
                    ImGui::Dummy({bw, bh + 6});
                    ImGui::SameLine();
                    ImGui::TextDisabled("%2.0f", s->speedKn());
                    if (!s->dmg.fires.empty()) {
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4{1.0f, 0.5f, 0.15f, 1.0f}, "*");
                    }
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

// ------------------------------------------------------------------------------------------ ship panel
void GameUi::drawShipPanel(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##shippanel", nullptr, kPanel);
    sim::Ship* s = selected();
    if (!s || !s->cls) {
        ImGui::TextDisabled("%s", tr("ship.none_selected"));
        ImGui::End();
        return;
    }
    const sim::World& w = session_->world();
    bool own = playerSide() == sim::Side::Neutral || s->side == playerSide();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float fh = ImGui::GetFontSize();
    drawFlag(ImGui::GetWindowDrawList(), {p.x, p.y + 4}, {fh * 1.9f, fh * 1.2f}, s->nation);
    ImGui::Dummy({fh * 2.1f, fh * 1.3f});
    ImGui::SameLine();
    ImGui::BeginGroup();
    {
        FontScope f(fonts().heading, 1.5f);
        ImGui::TextUnformatted(s->name.c_str());
    }
    ImGui::TextDisabled("%s %s  |  %s", s->cls->name.c_str(), shipTypeName(s->cls->type), crewName(s->crew));
    ImGui::EndGroup();
    ImGui::SameLine(ImGui::GetWindowWidth() - fh * 4.2f);
    if (ImGui::SmallButton(tr("ui.info"))) {
        unitRef_->select(s->cls->id);
        showUnitRefInBattle_ = true;
    }
    if (const sim::Group* g = w.group(s->groupId)) ImGui::TextDisabled("%s: %s", tr("ship.group"), tr(g->name.c_str()));
    ImGui::Text("%s %s   %s", tr("ui.course"), fmtBearing(s->heading).c_str(), fmtKnots(s->speedKn()).c_str());
    ImGui::SameLine();
    if (ImGui::Checkbox(tr("ship.follow"), &follow_)) {
    }
    if (s->dmg.sinking) textColored(col::kBad, "%s", tr("ship.sinking"));
    if (!own) {
        ImGui::End();
        return;
    }
    if (ImGui::BeginTabBar("shiptabs")) {
        if (ImGui::BeginTabItem(tr("ship.tab.helm"))) {
            drawHelmTab(*s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("ship.tab.weapons"))) {
            drawWeaponsTab(*s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("ship.tab.damage"))) {
            drawDamageTab(*s);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(tr("ship.tab.sensors"))) {
            drawSensorsTab(*s);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void GameUi::drawHelmTab(sim::Ship& s) {
    sim::World& w = session_->world();
    sim::Group* g = w.group(s.groupId);
    bool groupCtl = g && g->playerControlled && !s.orders.playerSteering;
    bool canCommand = playerSide() == sim::Side::Neutral || (g && g->playerControlled) || s.orders.playerSteering;
    if (!canCommand) {
        ImGui::TextWrapped("%s", tr("ship.ai_group_hint"));
        if (ImGui::Button(tr("ship.detach"))) session_->orderShipCourse(s.id, s.heading, s.speedKn());
        return;
    }
    if (groupCtl) {
        ImGui::RadioButton(tr("ship.orders_group"), &helmMode_, 0);
        ImGui::SameLine();
        ImGui::RadioButton(tr("ship.orders_ship"), &helmMode_, 1);
    } else {
        helmMode_ = 1;
        if (s.orders.playerSteering && g) {
            ImGui::TextDisabled("%s", tr("ship.detached"));
            ImGui::SameLine();
            if (ImGui::SmallButton(tr("ship.rejoin"))) session_->rejoinGroup(s.id);
        }
    }
    bool toGroup = groupCtl && helmMode_ == 0;
    double course = toGroup ? g->course : s.orders.course;
    double speed = toGroup ? g->speedKn : s.orders.speedKn;
    double maxKn = s.maxSpeedNow() / 0.514444;
    if (toGroup)
        for (auto id : g->members)
            if (const sim::Ship* m = w.ship(id); m && m->operational()) maxKn = std::min(maxKn, m->maxSpeedNow() / 0.514444);
    float r = std::min(ImGui::GetContentRegionAvail().x * 0.27f, ImGui::GetFontSize() * 5.5f);
    bool changed = compassInput("compass", course, s.heading, r);
    ImGui::SameLine();
    ImGui::BeginGroup();
    changed |= engineTelegraph("telegraph", speed, std::max(1.0, std::floor(maxKn)), s.speedKn());
    ImGui::EndGroup();
    // Quick turns.
    const double turns[] = {-90, -45, -10, 10, 45, 90};
    for (int i = 0; i < 6; ++i) {
        if (i) ImGui::SameLine();
        char b[16];
        std::snprintf(b, sizeof(b), "%+.0f", turns[i]);
        if (ImGui::Button(b, {ImGui::GetContentRegionAvail().x / float(6 - i) - 4, 0})) {
            course = wrap360(course + turns[i]);
            changed = true;
        }
    }
    if (changed) {
        if (toGroup) session_->orderGroupCourse(g->id, course, speed);
        else session_->orderShipCourse(s.id, course, speed);
    }
    if (toGroup) {
        sectionHeader(tr("ship.formation"));
        static const sim::Formation forms[] = {sim::Formation::Column, sim::Formation::LineAbreast, sim::Formation::EchelonRight,
                                               sim::Formation::EchelonLeft, sim::Formation::Screen};
        int cur = 0;
        for (int i = 0; i < 5; ++i)
            if (forms[i] == g->formation) cur = i;
        const char* names[5];
        for (int i = 0; i < 5; ++i) names[i] = tr(sim::formationKey(forms[i]));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.55f);
        if (ImGui::Combo("##form", &cur, names, 5)) session_->orderGroupFormation(g->id, forms[cur]);
        ImGui::SameLine();
        float spacing = float(g->spacing);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##spacing", &spacing, 300.0f, 3000.0f, "%.0f m")) session_->orderGroupFormation(g->id, g->formation, spacing);
    }
    ImGui::Spacing();
    ImGui::TextDisabled("%s", tr("ship.helm_hint"));
}

void GameUi::drawWeaponsTab(sim::Ship& s) {
    const sim::World& w = session_->world();
    // Weapons status.
    for (auto ws : {sim::WeaponsStatus::Hold, sim::WeaponsStatus::Tight, sim::WeaponsStatus::Free}) {
        if (ws != sim::WeaponsStatus::Hold) ImGui::SameLine();
        if (toggleButton(tr(weaponsKey(ws)), s.orders.weapons == ws, {ImGui::GetContentRegionAvail().x / (ws == sim::WeaponsStatus::Hold ? 3.0f : ws == sim::WeaponsStatus::Tight ? 2.0f : 1.0f) - 4, 0}))
            session_->orderWeapons(s.id, ws);
    }
    // Target selection.
    auto hostiles = knownHostiles();
    std::sort(hostiles.begin(), hostiles.end(), [&](auto* a, auto* b) { return distance(a->pos, s.pos) < distance(b->pos, s.pos); });
    std::string cur = tr("ui.target.captain");
    if (s.orders.playerTarget)
        if (const sim::Track* t = w.picture(s.side).find(s.orders.playerTarget)) cur = trackName(*t);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##target", (std::string(tr("ui.target")) + ": " + cur).c_str())) {
        if (ImGui::Selectable(tr("ui.target.captain"), s.orders.playerTarget == 0)) session_->orderTarget(s.id, 0);
        for (const sim::Track* t : hostiles) {
            std::string lbl = trackName(*t) + "  " + fmtRange(distance(s.pos, t->predict(w.time())), settings.metric);
            if (ImGui::Selectable(lbl.c_str(), s.orders.playerTarget == t->id)) session_->orderTarget(s.id, t->id);
        }
        ImGui::EndCombo();
    }
    // Directors.
    sectionHeader(tr("weapons.directors"));
    for (size_t i = 0; i < s.directors.size(); ++i) {
        const auto& d = s.directors[i];
        if (!d.def) continue;
        ImGui::PushID(int(i));
        std::string tgt = "-";
        if (d.targetTrack)
            if (const sim::Track* t = w.picture(s.side).find(d.targetTrack)) tgt = trackName(*t);
        ImGui::Text("%s", tr(("director." + d.def->role).c_str()));
        ImGui::SameLine(ImGui::GetFontSize() * 6.0f);
        ImGui::TextColored(d.tracking ? col::kGood : col::kTextDim, "%s", tgt.c_str());
        if (d.tracking) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s%s", d.straddled ? tr("weapons.straddle") : "", d.radarRanging ? " R" : "");
        }
        if (!d.operational()) {
            ImGui::SameLine();
            ImGui::TextColored(col::kBad, "%s", tr("ui.damaged"));
        }
        if (d.tracking) statusBar(tr("weapons.solution"), float(d.quality), col::kBrassDim);
        ImGui::PopID();
    }
    // Mounts.
    sectionHeader(tr("weapons.guns"));
    if (ImGui::BeginTable("mounts", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        for (const auto& m : s.mounts) {
            if (!m.def) continue;
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text("%s", m.def->id.c_str());
            ImGui::TableNextColumn();
            if (m.def->gun) ImGui::Text("%dx %.0f mm", m.def->barrels, m.def->gun->caliberMm);
            ImGui::TableNextColumn();
            if (!m.operational()) ImGui::TextColored(col::kBad, "%s", tr("ui.damaged"));
            else if (m.reload > 0.1) ImGui::TextColored(col::kWarn, "%s %.0fs", tr("weapons.loading"), m.reload);
            else ImGui::TextColored(m.bears ? col::kGood : col::kTextDim, "%s", m.bears ? tr("weapons.bears") : tr("weapons.ready"));
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%d", m.totalAmmo());
        }
        ImGui::EndTable();
    }
    bool star = false;
    for (const auto& d : s.directors)
        if (d.illuminate) star = true;
    if (ImGui::Checkbox(tr("weapons.starshells"), &star)) session_->orderStarshells(s.id, star);
    ImGui::SameLine();
    bool sl = s.orders.searchlight;
    if (ImGui::Checkbox(tr("weapons.searchlight"), &sl)) session_->orderSearchlight(s.id, sl, selTrack_ ? selTrack_ : s.orders.playerTarget);
    // Torpedoes.
    if (!s.torpedoMounts.empty()) {
        sectionHeader(tr("weapons.torpedoes"));
        int ready = s.torpedoesReady();
        ImGui::Text("%s: %d / %d", tr("weapons.tubes_ready"), ready, s.torpedoesTotal());
        const sim::TorpedoDef* td = s.torpedoMounts.front().def ? s.torpedoMounts.front().def->torpedo : nullptr;
        if (td) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", td->name.c_str());
            for (const auto& st : td->settings) ImGui::TextDisabled("  %2.0f kn - %s", st.speedKn, fmtRange(st.range, settings.metric).c_str());
        }
        ImGui::Checkbox(tr("weapons.hold_torpedoes"), &s.orders.playerHoldTorpedoes);
        helpMarker(tr("weapons.hold_torpedoes_help"));
        torpedoCount_ = std::clamp(torpedoCount_, 1, std::max(1, ready));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.48f);
        ImGui::SliderInt("##count", &torpedoCount_, 1, std::max(1, ready), tr("weapons.count_fmt"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        ImGui::SliderFloat("##spread", &torpedoSpread_, 0.0f, 15.0f, tr("weapons.spread_fmt"));
        uint32_t target = selTrack_ ? selTrack_ : s.orders.playerTarget;
        const sim::Track* tt = target ? w.picture(s.side).find(target) : nullptr;
        ImGui::BeginDisabled(!tt || ready == 0);
        std::string fire = std::string(tr("weapons.launch")) + (tt ? " -> " + trackName(*tt) : "");
        if (ImGui::Button(fire.c_str(), {-1, 0})) {
            std::string why;
            int n = session_->orderTorpedoes(s.id, target, torpedoCount_, torpedoSpread_, &why);
            lastTorpedoMsg_ = n > 0 ? loc::format("weapons.launched", {std::to_string(n)}) : (why.empty() ? tr("weapons.no_solution") : tr(why.c_str()));
            addLog(s.name + ": " + lastTorpedoMsg_, n > 0 ? col::kGood : col::kWarn);
        }
        ImGui::EndDisabled();
        if (!tt) ImGui::TextDisabled("%s", tr("weapons.select_target_hint"));
        if (!lastTorpedoMsg_.empty()) ImGui::TextDisabled("%s", lastTorpedoMsg_.c_str());
    }
    if (s.floatplanes > 0) {
        sectionHeader(tr("weapons.aircraft"));
        Vec2 target = s.pos + s.forward() * 15000.0;
        if (selTrack_)
            if (const sim::Track* t = w.picture(s.side).find(selTrack_)) target = t->predict(w.time());
        if (ImGui::Button(tr("weapons.launch_recon"))) session_->orderFloatplane(s.id, false, target);
        ImGui::SameLine();
        if (ImGui::Button(tr("weapons.launch_flares"))) session_->orderFloatplane(s.id, true, target);
        ImGui::TextDisabled("%s: %d", tr("weapons.floatplanes"), s.floatplanes);
    }
}

void GameUi::drawDamageTab(sim::Ship& s) {
    const auto& d = s.dmg;
    auto good = [](double f) { return f > 0.7 ? col::kGood : f > 0.4 ? col::kWarn : col::kBad; };
    statusBar(tr("damage.hull"), float(d.hullIntegrity), good(d.hullIntegrity));
    double reserve = s.cls->displacementFull * s.cls->reserveBuoyancy;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f t (%+.1f t/min)", d.flooding, d.floodRate * 60.0);
    statusBar(tr("damage.flooding"), float(d.flooding / std::max(1.0, reserve)), col::kAllies, buf);
    ImGui::Text("%s %+.1f\xC2\xB0   %s %+.1f\xC2\xB0", tr("damage.list"), d.list, tr("damage.trim"), d.trim);
    ImGui::Text("%s: %zu", tr("damage.fires"), d.fires.size());
    if (!d.fires.empty()) {
        ImGui::SameLine();
        statusBar("", float(std::min(1.0, d.totalFireIntensity())), ImVec4{1.0f, 0.5f, 0.15f, 1.0f});
    }
    sectionHeader(tr("damage.systems"));
    statusBar(tr("damage.propulsion"), float(d.propulsion), good(d.propulsion));
    statusBar(tr("damage.steering"), float(d.steering), good(d.steering));
    if (d.rudderJammed) textColored(col::kBad, "%s", tr("damage.rudder_jammed"));
    statusBar(tr("damage.power"), float(d.power), good(d.power));
    statusBar(tr("damage.bridge"), float(d.bridge), good(d.bridge));
    statusBar(tr("damage.radio"), float(d.radio), good(d.radio));
    statusBar(tr("damage.searchlights"), float(d.searchlights), good(d.searchlights));
    statusBar(tr("damage.crew"), float(1.0 - d.crewLoss), good(1.0 - d.crewLoss));
    sectionHeader(tr("damage.readiness"));
    statusBar(tr("damage.readiness_level"), float(s.readiness), good(s.readiness));
    ImGui::TextDisabled("%s", s.alertedAt >= 0 ? tr("damage.general_quarters") : tr("damage.cruising_watch"));
    ImGui::Text("%s: %d   %s: %d", tr("damage.hits"), d.hitsTaken, tr("damage.torpedo_hits"), d.torpedoHitsTaken);
}

void GameUi::drawSensorsTab(sim::Ship& s) {
    const sim::World& w = session_->world();
    sectionHeader(tr("sensors.title"));
    for (size_t i = 0; i < s.sensors.size(); ++i) {
        auto& sn = s.sensors[i];
        const sim::SensorDef* sd = sn.def ? sn.def->def : nullptr;
        if (!sd) continue;
        ImGui::PushID(int(i));
        bool working = sn.working(w.time());
        ImGui::TextColored(working ? col::kGood : col::kTextDim, "%s", sd->name.c_str());
        if (sd->kind == sim::SensorKind::Radar) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.65f);
            ImGui::Checkbox(tr("sensors.emit"), &sn.on);
            if (sd->rangeLarge > 0) ImGui::TextDisabled("  %s %s", tr("sensors.range_large"), fmtRange(sd->rangeLarge, settings.metric).c_str());
        }
        if (sn.health < 0.3) {
            ImGui::SameLine();
            ImGui::TextColored(col::kBad, "%s", tr("ui.damaged"));
        } else if (w.time() < sn.outageUntil) {
            ImGui::SameLine();
            ImGui::TextColored(col::kWarn, "%s", tr("sensors.outage"));
        }
        ImGui::PopID();
    }
    sectionHeader(tr("sensors.held"));
    for (const auto& t : w.picture(s.side).tracks()) {
        if (t.lost || !t.heldBy(s.id, w.time())) continue;
        ImGui::BulletText("%s  %s  (%s)", trackName(t).c_str(), fmtRange(distance(s.pos, t.pos), settings.metric).c_str(),
                          senseText(t.senseOf(s.id, w.time())).c_str());
    }
    ImGui::TextDisabled("%s", tr("sensors.fair_hint"));
}

// ---------------------------------------------------------------------------------------- contact panel
void GameUi::drawContactPanel() {
    const sim::World& w = session_->world();
    sim::Side me = playerSide();
    if (me == sim::Side::Neutral) return;
    const sim::Track* t = w.picture(me).find(selTrack_);
    if (!t) {
        selTrack_ = 0;
        return;
    }
    Layout L = layout();
    ImGui::SetNextWindowPos({L.leftW + 8, L.H - L.bottomH - 8}, ImGuiCond_Always, {0.0f, 1.0f});
    ImGui::SetNextWindowSize({ImGui::GetFontSize() * 19.0f, 0});
    bool open = true;
    ImGui::Begin(tr("contact.title"), &open, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    {
        FontScope f(fonts().bold, 1.2f);
        ImGui::TextColored(t->hostile ? col::kAxis : col::kUnknown, "%s", trackName(*t).c_str());
    }
    ImGui::TextDisabled("%s", tr(levelKey(t->level)));
    double now = w.time();
    Vec2 tp = t->lost ? t->pos : t->predict(now);
    if (const sim::Ship* s = w.ship(selShip_)) {
        ImGui::Text("%s %s   %s %s", tr("ui.bearing"), fmtBearing(bearingTo(s->pos, tp)).c_str(), tr("ui.range"),
                    fmtRange(distance(s->pos, tp), settings.metric).c_str());
    }
    ImGui::Text("%s ~%s  %s ~%s", tr("ui.course"), fmtBearing(t->courseEstimate()).c_str(), tr("ui.speed"), fmtKnots(t->speedEstimateKn()).c_str());
    ImGui::TextDisabled("%s: +/- %s", tr("contact.uncertainty"), fmtRange(t->posSigma, settings.metric).c_str());
    ImGui::TextDisabled("%s: %s", tr("contact.senses"), senseText(t->sense).c_str());
    if (t->lost) ImGui::TextColored(col::kWarn, "%s %s", tr("ui.contact.lost_since"), fmtDuration(t->age(now)).c_str());
    if (t->firedOnUs) ImGui::TextColored(col::kBad, "%s", tr("contact.fired_on_us"));
    if (t->level >= sim::ClassLevel::ClassKnown && !t->classGuess.empty()) {
        if (const sim::ShipClassDef* c = db_.shipClass(t->classGuess)) {
            ImGui::TextDisabled("%s: %s %s", tr("ui.contact.class_guess"), c->name.c_str(), shipTypeAbbrev(c->type));
            if (ImGui::Button(tr("contact.open_reference"))) {
                unitRef_->select(c->id);
                showUnitRefInBattle_ = true;
            }
        }
    }
    if (sim::Ship* s = selected(); s && s->side == me && !t->lost) {
        if (ImGui::Button(tr("contact.engage"))) session_->orderTarget(s->id, t->id);
        ImGui::SameLine();
        if (ImGui::Button(tr("contact.illuminate"))) session_->orderSearchlight(s->id, true, t->id);
    }
    ImGui::End();
    if (!open) selTrack_ = 0;
}

// ---------------------------------------------------------------------------------- log & contact list
void GameUi::drawMessageLog(ImVec2 pos, ImVec2 size) {
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##log", nullptr, kPanel);
    sectionHeader(tr("log.title"));
    ImGui::BeginChild("lines");
    for (const auto& l : log_) {
        ImGui::TextDisabled("%s", fmtClock(session_->world().env().localHour() - (session_->world().time() - l.time) / 3600.0).c_str());
        ImGui::SameLine();
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(l.color, "%s", l.text.c_str());
        ImGui::PopTextWrapPos();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

void GameUi::drawContactsList(ImVec2 pos, ImVec2 size) {
    const sim::World& w = session_->world();
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##contacts", nullptr, kPanel);
    sectionHeader(tr("contacts.title"));
    sim::Side me = playerSide();
    if (me == sim::Side::Neutral) {
        ImGui::TextDisabled("%s", tr("contacts.umpire"));
        ImGui::End();
        return;
    }
    const sim::Ship* own = w.ship(selShip_);
    if (ImGui::BeginTable("ct", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn(tr("contacts.track"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn(tr("ui.bearing"));
        ImGui::TableSetupColumn(tr("ui.range"));
        ImGui::TableSetupColumn(tr("ui.course"));
        ImGui::TableSetupColumn(tr("ui.speed"));
        ImGui::TableSetupColumn(tr("contacts.via"));
        ImGui::TableHeadersRow();
        std::vector<const sim::Track*> list;
        for (const auto& t : w.picture(me).tracks()) {
            bool known = t.sharedAt <= w.time();
            for (const auto& h : t.holders)
                if (const sim::Ship* s = w.ship(h.ship); s && s->side == me && w.time() - h.lastSeen < 30.0) known = true;
            if (known) list.push_back(&t);
        }
        std::sort(list.begin(), list.end(), [&](auto* a, auto* b) {
            if (a->lost != b->lost) return !a->lost;
            return own ? distance(own->pos, a->pos) < distance(own->pos, b->pos) : a->id < b->id;
        });
        for (const sim::Track* t : list) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(int(t->id));
            ImVec4 c = t->lost ? col::kTextDim : t->hostile ? col::kAxis : col::kUnknown;
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            if (ImGui::Selectable(trackName(*t).c_str(), selTrack_ == t->id, ImGuiSelectableFlags_SpanAllColumns)) selTrack_ = t->id;
            ImGui::PopStyleColor();
            ImGui::PopID();
            Vec2 tp = t->lost ? t->pos : t->predict(w.time());
            ImGui::TableNextColumn();
            if (own) ImGui::TextUnformatted(fmtBearing(bearingTo(own->pos, tp)).c_str());
            ImGui::TableNextColumn();
            if (own) ImGui::TextUnformatted(fmtRange(distance(own->pos, tp), settings.metric).c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmtBearing(t->courseEstimate()).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.0f", t->speedEstimateKn());
            ImGui::TableNextColumn();
            ImGui::TextDisabled("%s", senseText(t->sense).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

// ------------------------------------------------------------------------------------------------- map
void GameUi::drawMapView(ImVec2 pos, ImVec2 size, bool compact) {
    sim::World& w = session_->world();
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin(compact ? "##minimap" : "##map", nullptr, kPanel | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    MapInput in;
    in.world = &w;
    in.terrain = &w.terrain();
    in.side = playerSide();
    in.selectedShip = selShip_;
    in.selectedTrack = selTrack_;
    in.chartTexture = svc.chartTexture();
    in.metric = settings.metric;
    in.compact = compact;
    if (const sim::Ship* s = w.ship(selShip_); s && s->cls) {
        in.selectedGroup = s->groupId;
        double gun = 0;
        for (const auto& m : s->mounts)
            if (m.def && m.def->gun) gun = std::max(gun, m.def->gun->effectiveMaxRange());
        in.gunRange = gun;
        if (!s->torpedoMounts.empty() && s->torpedoMounts.front().def && s->torpedoMounts.front().def->torpedo &&
            !s->torpedoMounts.front().def->torpedo->settings.empty())
            in.torpedoRange = s->torpedoMounts.front().def->torpedo->settings.front().range;
        in.plannedCourse = {s->pos, s->pos + bearingVector(s->orders.playerSteering ? s->orders.course : s->heading) * (s->speed * 600.0 + 500.0)};
        if (const sim::Group* g = w.group(s->groupId); g && g->playerControlled && !s->orders.playerSteering)
            in.plannedCourse = {s->pos, s->pos + bearingVector(g->course) * (g->speedKn * 0.514444 * 600.0 + 500.0)};
    }
    TacticalMap& m = compact ? miniMap_ : map_;
    if (compact)
        if (const sim::Ship* s = w.ship(selShip_)) m.center = s->pos;
    MapOutput out = m.draw("plot", size, in);
    if (out.clickedShip != sim::kNoEntity) selectShip(out.clickedShip, !compact);
    if (out.clickedTrack) selTrack_ = out.clickedTrack;
    if (compact && out.doubleClicked) view_ = View::Map;
    if (out.rightClicked && !compact) ImGui::OpenPopup("mapctx");
    static Vec2 ctxPos;
    if (out.rightClicked) ctxPos = out.rightClickPos;
    if (ImGui::BeginPopup("mapctx")) {
        if (sim::Ship* s = selected(); s && (s->side == playerSide() || playerSide() == sim::Side::Neutral)) {
            double brg = bearingTo(s->pos, ctxPos);
            const sim::Group* g = w.group(s->groupId);
            ImGui::TextDisabled("%s %s  %s", tr("ui.bearing"), fmtBearing(brg).c_str(), fmtRange(distance(s->pos, ctxPos), settings.metric).c_str());
            if (g && g->playerControlled && !s->orders.playerSteering && ImGui::MenuItem(tr("map.ctx.group_steer")))
                session_->orderGroupCourse(g->id, brg, g->speedKn);
            if (ImGui::MenuItem(tr("map.ctx.ship_steer"))) session_->orderShipCourse(s->id, brg, s->orders.playerSteering ? s->orders.speedKn : s->speedKn());
        }
        if (ImGui::MenuItem(tr("map.ctx.center"))) map_.center = ctxPos;
        ImGui::EndPopup();
    }
    if (!compact) {
        ImGui::SetCursorPos({8, 8});
        ImGui::TextDisabled("%s", tr("map.hint"));
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

// ------------------------------------------------------------------------------------------ 3D labels
void GameUi::drawSceneLabels() {
    const sim::World& w = session_->world();
    ImGuiIO& io = ImGui::GetIO();
    if (ctl_.mode == render::CameraMode::Binoculars) {
        // Binocular reticle.
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImVec2 c{io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f};
        float r = io.DisplaySize.y * 0.46f;
        dl->AddCircle({c.x - r * 0.55f, c.y}, r, IM_COL32(0, 0, 0, 200), 96, 6.0f);
        dl->AddCircle({c.x + r * 0.55f, c.y}, r, IM_COL32(0, 0, 0, 200), 96, 6.0f);
        for (int i = -5; i <= 5; ++i) dl->AddLine({c.x + i * r * 0.08f, c.y - 4}, {c.x + i * r * 0.08f, c.y + 4}, IM_COL32(20, 20, 20, 220), 1.5f);
        dl->AddLine({c.x - r * 0.4f, c.y}, {c.x + r * 0.4f, c.y}, IM_COL32(20, 20, 20, 200), 1.0f);
        std::string brg = fmtBearing(cam_.yaw);
        dl->AddText({c.x - 20, c.y + r * 0.85f}, IM_COL32(230, 220, 190, 255), brg.c_str());
    }
    Mat4f vp = cam_.proj(io.DisplaySize.x / std::max(1.0f, io.DisplaySize.y)) * cam_.view();
    auto project = [&](const Vec3& world, ImVec2& out) {
        Vec3 r = world - cam_.pos;
        Vec4f c = vp * Vec4f(float(r.x), float(r.y), float(r.z), 1.0f);
        if (c.w <= 0.1f) return false;
        out = {(c.x / c.w * 0.5f + 0.5f) * io.DisplaySize.x, (0.5f - c.y / c.w * 0.5f) * io.DisplaySize.y};
        return out.x > -50 && out.y > -50 && out.x < io.DisplaySize.x + 50 && out.y < io.DisplaySize.y + 50;
    };
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    sim::Side me = playerSide();
    for (const auto& s : w.ships()) {
        if (!s->cls || s->dmg.sunk || (me != sim::Side::Neutral && s->side != me)) continue;
        double d = length(render::toRender(s->pos, 0.0) - cam_.pos);
        if (d > 12000.0 && s->id != selShip_) continue;
        ImVec2 p;
        if (!project(render::toRender(s->pos, s->cls->mastHeight + 6.0), p)) continue;
        ImU32 c = s->id == selShip_ ? toU32(col::kBrass) : toU32(col::kAllies, 0.75f);
        ImVec2 ts = ImGui::CalcTextSize(s->name.c_str());
        dl->AddText({p.x - ts.x * 0.5f, p.y - ts.y}, c, s->name.c_str());
    }
    if (me != sim::Side::Neutral)
        for (const sim::Track* t : knownHostiles()) {
            ImVec2 p;
            if (!project(render::toRender(t->predict(w.time()), 18.0), p)) continue;
            ImU32 c = toU32(t->hostile ? col::kAxis : col::kUnknown, 0.8f);
            dl->AddRect({p.x - 9, p.y - 9}, {p.x + 9, p.y + 9}, c, 0.0f, 0, 1.2f);
            std::string n = trackName(*t);
            dl->AddText({p.x + 12, p.y - 8}, c, n.c_str());
        }
}

// ------------------------------------------------------------------------------------------ AI insight
void GameUi::drawAiInsight() {
    if (!session_) return;
    sim::Side me = playerSide();
    bool umpire = screen_ == Screen::Debrief || me == sim::Side::Neutral;
    const char* filters[] = {tr("insight.own_ai"), tr("insight.enemy_ai"), tr("insight.all")};
    int maxFilter = (umpire || settings.revealEnemyAi) ? 3 : 1;
    insightFilter_ = std::min(insightFilter_, maxFilter - 1);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 10.0f);
    ImGui::Combo("##filter", &insightFilter_, filters, maxFilter);
    ImGui::SameLine();
    const char* levels[] = {tr("insight.level_all"), tr("insight.level_commander"), tr("insight.level_group"), tr("insight.level_captain")};
    int li = insightLevel_ + 1;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    if (ImGui::Combo("##level", &li, levels, 4)) insightLevel_ = li - 1;
    // Current posture of every AI group (what the commanders intend right now).
    std::vector<sim::Side> sides;
    if (insightFilter_ == 0 || insightFilter_ == 2) sides.push_back(me == sim::Side::Neutral ? sim::Side::Allies : me);
    if (insightFilter_ == 1 || insightFilter_ == 2) sides.push_back(me == sim::Side::Neutral ? sim::Side::Axis : sim::enemyOf(me));
    const sim::World& w = session_->world();
    if (ImGui::CollapsingHeader(tr("insight.postures"), ImGuiTreeNodeFlags_DefaultOpen)) {
        for (sim::Side side : sides) {
            ai::AiController* ai = session_->ai(side);
            if (!ai) continue;
            for (const auto& g : w.groups()) {
                if (g.side != side) continue;
                const ai::GroupBrain* b = ai->brain(g.id);
                if (!b) continue;
                ImGui::TextColored(sideColor(side), "%s", tr(g.name.c_str()));
                ImGui::SameLine(ImGui::GetFontSize() * 12.0f);
                ImGui::TextUnformatted(tr(ai::postureKey(b->posture)));
            }
        }
    }
    ImGui::BeginChild("records");
    std::vector<const ai::DecisionRecord*> recs;
    for (sim::Side side : sides)
        if (ai::AiController* ai = session_->ai(side))
            for (const auto& r : ai->log().records())
                if (insightLevel_ < 0 || int(r.level) == insightLevel_) recs.push_back(&r);
    std::sort(recs.begin(), recs.end(), [](auto* a, auto* b) { return a->time > b->time; });
    int shown = 0;
    for (const ai::DecisionRecord* r : recs) {
        if (++shown > 300) break;
        ImGui::PushID(r);
        std::string head = fmtDuration(r->time) + "  " + r->actor + ": " + loc::format(r->decision, r->args);
        ImGui::PushStyleColor(ImGuiCol_Text, sideColor(r->side));
        bool open = ImGui::TreeNodeEx("##r", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", head.c_str());
        ImGui::PopStyleColor();
        if (open) {
            if (!r->detail.empty()) ImGui::TextDisabled("%s", describeDetail(*r, settings.metric).c_str());
            for (const auto& alt : r->alternatives) {
                bool win = alt.option == r->decision;
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram, win ? col::kBrass : col::kBrassDim);
                char b[32];
                std::snprintf(b, sizeof(b), "%.2f", alt.score);
                ImGui::ProgressBar(float(std::clamp(alt.score, 0.0, 1.0)), {ImGui::GetFontSize() * 6.0f, 0}, b);
                ImGui::PopStyleColor();
                ImGui::SameLine();
                if (win) ImGui::TextColored(col::kBrass, "%s", tr(alt.option.c_str()));
                else ImGui::TextUnformatted(tr(alt.option.c_str()));
                if (ImGui::IsItemHovered() && !alt.considerations.empty()) {
                    ImGui::BeginTooltip();
                    for (const auto& c : alt.considerations) {
                        std::string key = "ai.c." + c.name;
                        ImGui::Text("%s", loc::has(key) ? tr(key.c_str()) : c.name.c_str());
                        ImGui::SameLine(ImGui::GetFontSize() * 14.0f);
                        ImGui::TextDisabled("%.2f", c.input);
                        ImGui::SameLine(ImGui::GetFontSize() * 17.0f);
                        ImGui::ProgressBar(float(std::clamp(c.score, 0.0, 1.0)), {ImGui::GetFontSize() * 6.0f, 0});
                    }
                    ImGui::EndTooltip();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (recs.empty()) ImGui::TextDisabled("%s", tr("insight.empty"));
    ImGui::EndChild();
}

}  // namespace it::ui
