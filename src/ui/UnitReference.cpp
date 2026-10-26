#include "ui/UnitReference.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>

#include "core/Localization.h"
#include "sim/Ballistics.h"
#include "ui/Format.h"
#include "ui/GameUi.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace it::ui {

namespace {

bool containsI(const std::string& hay, const char* needle) {
    if (!needle || !*needle) return true;
    std::string h = hay, n = needle;
    std::transform(h.begin(), h.end(), h.begin(), ::tolower);
    std::transform(n.begin(), n.end(), n.begin(), ::tolower);
    return h.find(n) != std::string::npos;
}

void kv(const char* k, const std::string& v) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    ImGui::TextDisabled("%s", k);
    ImGui::TableNextColumn();
    ImGui::TextUnformatted(v.c_str());
}

std::string mm(double v) { return v > 0 ? std::to_string(int(std::lround(v))) + " mm" : std::string("-"); }

const sim::ShellDef* apShell(const sim::GunDef& g) {
    if (const sim::ShellDef* s = g.shell(sim::ShellKind::AP)) return s;
    if (const sim::ShellDef* s = g.shell(sim::ShellKind::Common)) return s;
    return g.shells.empty() ? nullptr : &g.shells.front();
}

}  // namespace

void UnitReference::draw(bool overlay) {
    ImGuiIO& io = ImGui::GetIO();
    const float W = io.DisplaySize.x, H = io.DisplaySize.y;
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({W, H});
    ImGui::SetNextWindowBgAlpha(overlay ? 0.97f : 1.0f);
    ImGui::Begin("##unitref", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    {
        FontScope f(fonts().heading, 2.0f);
        ImGui::TextColored(col::kBrass, "%s", tr("unitref.title"));
    }
    ImGui::SameLine(W - 200);
    if (ImGui::Button(overlay ? tr("ui.close") : tr("ui.back"), {170, 0})) {
        if (overlay) ui_.showUnitRefInBattle_ = false;
        else ui_.go(GameUi::Screen::MainMenu);
    }
    ImGui::Separator();
    const auto& classes = ui_.db().shipClasses();
    if (selected_.empty() && !classes.empty()) selected_ = classes.front()->id;
    const sim::ShipClassDef* c = ui_.db().shipClass(selected_);
    float avail = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("list", {W * 0.18f, avail}, ImGuiChildFlags_Borders);
    drawList(avail);
    ImGui::EndChild();
    ImGui::SameLine();
    if (c) {
        ImGui::BeginChild("main", {0, avail});
        float pw = ImGui::GetContentRegionAvail().x;
        // Header.
        ImVec2 p = ImGui::GetCursorScreenPos();
        float fh = ImGui::GetFontSize();
        drawFlag(ImGui::GetWindowDrawList(), {p.x, p.y + 6}, {fh * 2.2f, fh * 1.4f}, c->nation);
        ImGui::Dummy({fh * 2.4f, fh * 1.5f});
        ImGui::SameLine();
        ImGui::BeginGroup();
        {
            FontScope f(fonts().heading, 1.9f);
            ImGui::Text("%s", c->name.c_str());
        }
        ImGui::TextDisabled("%s  |  %s  |  %d", nationName(c->nation), shipTypeName(c->type), c->year);
        ImGui::EndGroup();
        if (large_) {
            drawPreview(*c, pw, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 1.3f);
        } else {
            float previewH = std::min(avail * 0.5f, pw * 0.42f);
            drawPreview(*c, pw * 0.6f, previewH);
            ImGui::SameLine();
            ImGui::BeginChild("arcs", {0, previewH + ImGui::GetFrameHeightWithSpacing()}, ImGuiChildFlags_Borders);
            sectionHeader(tr("unitref.fire_arcs"));
            drawFireArcs(*c, std::min(ImGui::GetContentRegionAvail().x, ImGui::GetContentRegionAvail().y));
            ImGui::EndChild();
            drawDetails(*c);
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

void UnitReference::drawList(float) {
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", tr("unitref.search"), filter_, sizeof(filter_));
    const char* sides[] = {tr("unitref.all"), sideName(sim::Side::Allies), sideName(sim::Side::Axis)};
    ImGui::SetNextItemWidth(-1);
    ImGui::Combo("##side", &nationFilter_, sides, 3);
    // Group: nation -> type.
    std::map<std::pair<int, int>, std::vector<const sim::ShipClassDef*>> groups;
    for (const auto& c : ui_.db().shipClasses()) {
        sim::Side s = sim::defaultSide(c->nation);
        if (nationFilter_ == 1 && s != sim::Side::Allies) continue;
        if (nationFilter_ == 2 && s != sim::Side::Axis) continue;
        bool match = containsI(c->name, filter_);
        for (const auto& n : c->ships) match |= containsI(n, filter_);
        if (!match) continue;
        groups[{int(c->nation), int(c->type)}].push_back(c.get());
    }
    int lastNation = -1;
    for (auto& [key, list] : groups) {
        if (key.first != lastNation) {
            lastNation = key.first;
            ImGui::Spacing();
            ImGui::TextColored(col::kBrass, "%s", nationName(sim::Nation(key.first)));
        }
        ImGui::TextDisabled("  %s", shipTypeName(sim::ShipType(key.second)));
        std::sort(list.begin(), list.end(), [](auto* a, auto* b) { return a->year < b->year; });
        for (const sim::ShipClassDef* c : list) {
            ImGui::PushID(c->id.c_str());
            if (ImGui::Selectable(("    " + c->name).c_str(), selected_ == c->id)) selected_ = c->id;
            ImGui::PopID();
        }
    }
}

void UnitReference::drawPreview(const sim::ShipClassDef& c, float width, float height) {
    ImGui::BeginGroup();
    uint64_t tex = ui_.svc.shipPreview(c.id, view_, uint32_t(std::max(64.0f, width)), uint32_t(std::max(64.0f, height)));
    ImVec2 p = ImGui::GetCursorScreenPos();
    if (tex) ImGui::Image(ImTextureRef(ImTextureID(tex)), {width, height});
    else {
        ImGui::Dummy({width, height});
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + width, p.y + height}, IM_COL32(20, 30, 38, 255));
        ImGui::GetWindowDrawList()->AddText({p.x + 12, p.y + 12}, toU32(col::kTextDim), tr("unitref.no_preview"));
    }
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            view_.yaw = float(wrap360(view_.yaw + io.MouseDelta.x * 0.4f));
            view_.pitch = std::clamp(view_.pitch + io.MouseDelta.y * 0.25f, -5.0f, 85.0f);
        }
        // Right drag moves the look-at point along the hull and up and down.
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
            // Grab-style: the model follows the mouse. Screen right is (cos yaw, -sin yaw) in (east, north),
            // the hull runs north-south in the preview and the whole length spans about the view width.
            float along = std::sin(view_.yaw * float(kDegToRad));
            view_.focus = std::clamp(view_.focus + io.MouseDelta.x * along * 1.1f / (width * view_.zoom), -0.6f, 0.6f);
            view_.lift = std::clamp(view_.lift + io.MouseDelta.y * 60.0f / (height * view_.zoom), -10.0f, 40.0f);
        }
        if (io.MouseWheel != 0.0f) view_.zoom = std::clamp(view_.zoom * std::pow(1.15f, io.MouseWheel), 1.0f, 25.0f);
    }
    struct Preset {
        const char* key;
        float yaw, pitch;
    };
    static const Preset presets[] = {{"unitref.view_quarter", 135, 10}, {"unitref.view_side", 90, 1}, {"unitref.view_bow", 0, 3},
                                     {"unitref.view_stern", 180, 4},    {"unitref.view_top", 90, 84}};
    for (int i = 0; i < 5; ++i) {
        if (i) ImGui::SameLine();
        if (ImGui::Button(tr(presets[i].key))) {
            view_.yaw = presets[i].yaw;
            view_.pitch = presets[i].pitch;
            view_.zoom = 1.0f;
            view_.focus = view_.lift = 0.0f;
        }
    }
    ImGui::SameLine();
    ImGui::Checkbox(tr("unitref.night"), &view_.night);
    ImGui::SameLine();
    ImGui::Checkbox(tr("unitref.animate"), &view_.animate);
    ImGui::SameLine();
    ImGui::Checkbox(tr("unitref.large"), &large_);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", tr("unitref.view_hint"));
    ImGui::EndGroup();
}

void UnitReference::drawFireArcs(const sim::ShipClassDef& c, float size) {
    size = std::max(120.0f, size - 8.0f);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy({size, size});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ctr{p.x + size * 0.5f, p.y + size * 0.5f};
    float scale = size * 0.42f / float(c.length * 0.5);
    // Hull outline (bow up).
    std::vector<ImVec2> hull;
    for (int i = 0; i <= 24; ++i) {
        double u = i / 24.0;
        double x = (0.5 - u) * c.length;
        double w = c.beam * 0.5 * std::pow(std::sin(kPi * std::min(1.0, u * 1.25 + 0.02)), 0.6);
        hull.push_back({ctr.x + float(w) * scale, ctr.y - float(x) * scale});
    }
    for (int i = 24; i >= 0; --i) {
        double u = i / 24.0;
        double x = (0.5 - u) * c.length;
        double w = c.beam * 0.5 * std::pow(std::sin(kPi * std::min(1.0, u * 1.25 + 0.02)), 0.6);
        hull.push_back({ctr.x - float(w) * scale, ctr.y - float(x) * scale});
    }
    dl->AddConvexPolyFilled(hull.data(), int(hull.size()), IM_COL32(70, 80, 88, 255));
    // Arcs of the main battery (largest calibre) and torpedo mounts.
    double mainCal = 0;
    for (const auto& m : c.mounts)
        if (m.gun) mainCal = std::max(mainCal, m.gun->caliberMm);
    auto wedge = [&](ImVec2 o, double a0, double a1, float r, ImU32 col) {
        std::vector<ImVec2> pts{o};
        int n = std::max(4, int((a1 - a0) / 5.0));
        for (int i = 0; i <= n; ++i) {
            double a = (a0 + (a1 - a0) * i / n) * kDegToRad;
            pts.push_back({o.x + float(std::sin(a)) * r, o.y - float(std::cos(a)) * r});
        }
        for (size_t i = 1; i + 1 < pts.size(); ++i) dl->AddTriangleFilled(pts[0], pts[i], pts[i + 1], col);
    };
    for (const auto& m : c.mounts) {
        if (!m.gun) continue;
        bool main = m.gun->caliberMm >= mainCal - 1;
        ImVec2 o{ctr.x + float(m.y) * scale, ctr.y - float(m.x) * scale};
        ImU32 colr = main ? IM_COL32(220, 180, 100, 26) : IM_COL32(120, 180, 230, 16);
        float r = size * (main ? 0.48f : 0.36f);
        wedge(o, m.arcMin, m.arcMax, r, colr);
        if (m.mirrored) wedge(o, -m.arcMax, -m.arcMin, r, colr);
        dl->AddCircleFilled(o, main ? 4.0f : 2.5f, main ? toU32(col::kBrass) : IM_COL32(150, 200, 240, 255));
    }
    for (const auto& t : c.torpedoMounts) {
        ImVec2 o{ctr.x + float(t.y) * scale, ctr.y - float(t.x) * scale};
        wedge(o, t.arcMin, t.arcMax, size * 0.3f, IM_COL32(120, 230, 160, 24));
        if (t.mirrored) wedge(o, -t.arcMax, -t.arcMin, size * 0.3f, IM_COL32(120, 230, 160, 24));
        dl->AddRectFilled({o.x - 2, o.y - 2}, {o.x + 2, o.y + 2}, IM_COL32(120, 230, 160, 255));
    }
    // Broadside / end-on counts.
    auto bearing = [&](double rel) {
        int barrels = 0;
        for (const auto& m : c.mounts)
            if (m.gun && m.gun->caliberMm >= mainCal - 1 && sim::inArc(rel, m.arcMin, m.arcMax, m.mirrored)) barrels += m.barrels;
        return barrels;
    };
    char b[128];
    std::snprintf(b, sizeof(b), "%s %d  |  %s %d  |  %s %d", tr("unitref.ahead"), bearing(0.0), tr("unitref.broadside"), bearing(90.0),
                  tr("unitref.astern"), bearing(180.0));
    dl->AddText({p.x + 4, p.y + size - ImGui::GetFontSize() - 2}, toU32(col::kTextDim), b);
}

void UnitReference::drawPenetration(const sim::ShipClassDef& c) {
    const sim::GunDef* gun = nullptr;
    for (const auto& m : c.mounts)
        if (m.gun && (!gun || m.gun->caliberMm > gun->caliberMm)) gun = m.gun;
    if (!gun) {
        ImGui::TextDisabled("-");
        return;
    }
    const sim::ShellDef* shell = apShell(*gun);
    if (!shell) return;
    if (versus_.empty() || !ui_.db().shipClass(versus_)) {
        for (const auto& o : ui_.db().shipClasses())
            if (sim::defaultSide(o->nation) != sim::defaultSide(c.nation) && o->type == c.type) versus_ = o->id;
        if (versus_.empty() && !ui_.db().shipClasses().empty()) versus_ = ui_.db().shipClasses().front()->id;
    }
    const sim::ShipClassDef* vs = ui_.db().shipClass(versus_);
    ImGui::Text("%s  %.0f mm/%.0f  -  %s %.0f kg, %.0f m/s", gun->name.c_str(), gun->caliberMm, gun->calibres, shell->name.c_str(), shell->massKg,
                shell->muzzleVelocity);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
    if (ImGui::BeginCombo(tr("unitref.versus"), vs ? vs->name.c_str() : "-")) {
        for (const auto& o : ui_.db().shipClasses())
            if (ImGui::Selectable((o->name + "  (" + shipTypeAbbrev(o->type) + ", " + nationName(o->nation) + ")").c_str(), o->id == versus_))
                versus_ = o->id;
        ImGui::EndCombo();
    }
    if (vs) {
        ImGui::SameLine();
        ImGui::TextDisabled("%s %s / %s %s", tr("unitref.belt"), mm(vs->armor.belt).c_str(), tr("unitref.deck"), mm(vs->armor.deck).c_str());
    }
    if (ImGui::BeginTable("pen", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn(tr("ui.range"));
        ImGui::TableSetupColumn(tr("unitref.elevation"));
        ImGui::TableSetupColumn(tr("unitref.tof"));
        ImGui::TableSetupColumn(tr("unitref.fall"));
        ImGui::TableSetupColumn(tr("unitref.belt_pen"));
        ImGui::TableSetupColumn(tr("unitref.deck_pen"));
        ImGui::TableHeadersRow();
        double maxR = gun->effectiveMaxRange();
        for (double r = 2000.0; r <= maxR + 1.0; r += r < 10000 ? 2000.0 : 3000.0) {
            sim::BallisticSample bs = gun->sampleAtRange(r);
            double belt = sim::ballistics::beltPenetrationMm(shell->massKg, gun->caliberMm, bs.strikingVelocity, bs.angleOfFallDeg);
            double deck = sim::ballistics::deckPenetrationMm(shell->massKg, gun->caliberMm, bs.strikingVelocity, bs.angleOfFallDeg);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(fmtRange(r, ui_.settings.metric).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.1f\xC2\xB0", bs.elevationDeg);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f s", bs.timeOfFlight);
            ImGui::TableNextColumn();
            ImGui::Text("%.1f\xC2\xB0", bs.angleOfFallDeg);
            ImGui::TableNextColumn();
            bool bp = vs && belt > vs->armor.belt;
            ImGui::TextColored(vs ? (bp ? col::kGood : col::kBad) : col::kText, "%s", mm(belt).c_str());
            ImGui::TableNextColumn();
            bool dp = vs && deck > vs->armor.deck;
            ImGui::TextColored(vs ? (dp ? col::kGood : col::kBad) : col::kText, "%s", mm(deck).c_str());
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("%s", tr("unitref.pen_note"));
}

void UnitReference::drawDetails(const sim::ShipClassDef& c) {
    const bool metric = ui_.settings.metric;
    if (!ImGui::BeginTabBar("details")) return;
    if (ImGui::BeginTabItem(tr("unitref.tab_overview"))) {
        if (ImGui::BeginTable("ov", 4, ImGuiTableFlags_SizingStretchProp)) {
            auto pair = [](const char* k1, const std::string& v1, const char* k2, const std::string& v2) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", k1);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v1.c_str());
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", k2);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(v2.c_str());
            };
            char dims[96];
            std::snprintf(dims, sizeof(dims), "%.1f x %.1f x %.1f m", c.length, c.beam, c.draft);
            pair(tr("unitref.displacement"), fmtTonnes(c.displacementStd) + " / " + fmtTonnes(c.displacementFull), tr("unitref.dimensions"), dims);
            pair(tr("unitref.speed"), fmtKnots(c.maxSpeedKn) + " (" + fmtKnots(c.cruiseSpeedKn) + ")", tr("unitref.power"),
                 fmtInt(std::llround(c.shp)) + " shp, " + std::to_string(c.shafts) + " " + tr("unitref.shafts"));
            pair(tr("unitref.turning"), fmtRange(c.tacticalDiameter, metric), tr("unitref.crew"), fmtInt(c.crew));
            pair(tr("unitref.searchlights"), std::to_string(c.searchlights) + " x " + std::to_string(int(c.searchlightCm)) + " cm", tr("unitref.aircraft"),
                 std::to_string(c.floatplanes));
            pair(tr("unitref.mast"), std::to_string(int(c.mastHeight)) + " m", tr("unitref.rcs"), std::to_string(c.radarCrossSection).substr(0, 4));
            ImGui::EndTable();
        }
        if (!c.ships.empty()) {
            sectionHeader(tr("unitref.ships_in_class"));
            std::string names;
            for (const auto& n : c.ships) names += (names.empty() ? "" : ", ") + n;
            ImGui::TextWrapped("%s", names.c_str());
        }
        std::string hist = "class." + c.id + ".history";
        if (loc::has(hist)) {
            sectionHeader(tr("unitref.history"));
            ImGui::TextWrapped("%s", tr(hist.c_str()));
        }
        std::string tact = "class." + c.id + ".tactics";
        if (loc::has(tact)) {
            sectionHeader(tr("unitref.tactics"));
            ImGui::TextWrapped("%s", tr(tact.c_str()));
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tr("unitref.tab_guns"))) {
        // Aggregate mounts per gun type.
        std::map<const sim::GunDef*, std::pair<int, std::string>> guns;
        for (const auto& m : c.mounts) {
            if (!m.gun) continue;
            auto& g = guns[m.gun];
            g.first += m.barrels;
            g.second += (g.second.empty() ? "" : " ") + m.id;
        }
        if (ImGui::BeginTable("guns", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(tr("unitref.gun"), ImGuiTableColumnFlags_WidthStretch, 2.5f);
            ImGui::TableSetupColumn(tr("unitref.barrels"));
            ImGui::TableSetupColumn(tr("unitref.mounts"));
            ImGui::TableSetupColumn(tr("unitref.max_range"));
            ImGui::TableSetupColumn(tr("unitref.rate"));
            ImGui::TableSetupColumn(tr("unitref.shells"), ImGuiTableColumnFlags_WidthStretch, 2.5f);
            ImGui::TableHeadersRow();
            for (auto& [g, info] : guns) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s%s", g->name.c_str(), g->dualPurpose ? " (DP)" : "");
                ImGui::TableNextColumn();
                ImGui::Text("%d", info.first);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(info.second.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(fmtRange(g->effectiveMaxRange(), metric).c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.1f/min", g->rofRpm);
                ImGui::TableNextColumn();
                std::string shells;
                for (const auto& s : g->shells) {
                    char b[64];
                    std::snprintf(b, sizeof(b), "%s%s %.0f kg", shells.empty() ? "" : ", ", tr(sim::shellKindKey(s.kind)), s.massKg);
                    shells += b;
                }
                ImGui::TextWrapped("%s", shells.c_str());
            }
            ImGui::EndTable();
        }
        sectionHeader(tr("unitref.penetration"));
        drawPenetration(c);
        ImGui::EndTabItem();
    }
    if (!c.torpedoMounts.empty() && ImGui::BeginTabItem(tr("unitref.tab_torpedoes"))) {
        int tubes = 0, reloads = 0;
        for (const auto& t : c.torpedoMounts) {
            tubes += t.tubes;
            reloads += t.reloads;
        }
        ImGui::Text("%d %s (%zu %s), %d %s", tubes, tr("unitref.tubes"), c.torpedoMounts.size(), tr("unitref.mounts"), reloads, tr("unitref.reloads"));
        if (const sim::TorpedoDef* td = c.torpedoMounts.front().torpedo) {
            sectionHeader(td->name.c_str());
            if (ImGui::BeginTable("torp", 2, ImGuiTableFlags_SizingStretchProp)) {
                kv(tr("unitref.diameter"), std::to_string(int(td->diameterMm)) + " mm");
                kv(tr("unitref.warhead"), std::to_string(int(td->warheadKg)) + " kg");
                for (const auto& s : td->settings) kv(tr("unitref.setting"), fmtKnots(s.speedKn) + "  -  " + fmtRange(s.range, metric));
                kv(tr("unitref.wake"), td->wakeVisibility < 0.3 ? tr("unitref.wake_none") : tr("unitref.wake_visible"));
                kv(tr("unitref.reliability"), fmtPercent(1.0 - td->dudChance - td->deepRunChance - td->erraticChance));
                ImGui::EndTable();
            }
            std::string note = "torpedo." + td->id + ".note";
            if (loc::has(note)) ImGui::TextWrapped("%s", tr(note.c_str()));
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tr("unitref.tab_protection"))) {
        if (ImGui::BeginTable("arm", 2, ImGuiTableFlags_SizingStretchProp)) {
            kv(tr("unitref.belt"), mm(c.armor.belt));
            kv(tr("unitref.belt_magazine"), mm(c.armor.beltMagazine));
            kv(tr("unitref.deck"), mm(c.armor.deck));
            kv(tr("unitref.turret_face"), mm(c.armor.turretFace));
            kv(tr("unitref.turret_roof"), mm(c.armor.turretRoof));
            kv(tr("unitref.barbette"), mm(c.armor.barbette));
            kv(tr("unitref.conning_tower"), mm(c.armor.conningTower));
            kv(tr("unitref.torpedo_protection"), fmtPercent(c.armor.torpedoProtection));
            kv(tr("unitref.reserve_buoyancy"), fmtPercent(c.reserveBuoyancy));
            ImGui::EndTable();
        }
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(tr("unitref.tab_sensors"))) {
        if (ImGui::BeginTable("sens", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn(tr("unitref.sensor"), ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn(tr("unitref.height"));
            ImGui::TableSetupColumn(tr("unitref.range_large"));
            ImGui::TableSetupColumn(tr("unitref.range_small"));
            ImGui::TableHeadersRow();
            for (const auto& s : c.sensors) {
                if (!s.def) continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%s%s", s.def->name.c_str(), s.def->fireControl ? " (FC)" : "");
                ImGui::TableNextColumn();
                ImGui::Text("%.0f m", s.height);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s.def->rangeLarge > 0 ? fmtRange(s.def->rangeLarge, metric).c_str() : "-");
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s.def->rangeSmall > 0 ? fmtRange(s.def->rangeSmall, metric).c_str() : "-");
            }
            ImGui::EndTable();
        }
        sectionHeader(tr("unitref.directors"));
        for (const auto& d : c.directors)
            ImGui::BulletText("%s: %s %.1f m%s%s", tr(("director." + d.role).c_str()), tr("unitref.rangefinder"), d.rangefinderBase,
                              d.radarDef ? " + " : "", d.radarDef ? d.radarDef->name.c_str() : "");
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

}  // namespace it::ui
