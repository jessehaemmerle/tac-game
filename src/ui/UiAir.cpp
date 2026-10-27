// Air command: the flights list in the fleet panel, the flight panel (status and orders), orders given
// on the plot (search, reconnaissance, fighter cover, flares at a point) and the plot's context menu for a
// selected flight. Everything the player sees of the enemy comes from the side's air and surface picture.
#include <algorithm>
#include <cmath>

#include "core/Localization.h"
#include "ui/Format.h"
#include "ui/GameUi.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace it::ui {

namespace {

ImVec4 stateColor(sim::FlightState s) {
    switch (s) {
        case sim::FlightState::Attack:
        case sim::FlightState::Intercept: return col::kBad;
        case sim::FlightState::Ready: return col::kGood;
        case sim::FlightState::Rearming:
        case sim::FlightState::Returning:
        case sim::FlightState::Rally: return col::kTextDim;
        case sim::FlightState::Lost: return col::kBad;
        default: return col::kText;
    }
}

const double kAltitudes[] = {-1.0, 300.0, 1500.0, 4000.0};

bool fighterLike(const sim::Flight& f) { return f.def->role == sim::AirRole::Fighter || f.def->firepower >= 0.5; }

}  // namespace

// ------------------------------------------------------------------------------------------- selection
sim::Flight* GameUi::selectedFlight() {
    if (!session_ || selFlight_ < 0) return nullptr;
    sim::Flight* f = session_->world().flight(selFlight_);
    if (!f || (playerSide() != sim::Side::Neutral && f->side != playerSide())) return nullptr;
    return f;
}

void GameUi::selectFlight(int id, bool follow) {
    selFlight_ = id;
    mapOrder_ = MapOrder::None;
    lastAirMsg_.clear();
    if (follow) follow_ = true;
}

void GameUi::cycleFlight(int dir) {
    if (!session_) return;
    std::vector<int> ids;
    for (const auto& f : session_->world().flights())
        if ((playerSide() == sim::Side::Neutral || f.side == playerSide()) && f.state != sim::FlightState::Lost && f.alive(session_->world()) > 0)
            ids.push_back(f.id);
    if (ids.empty()) return;
    auto it = std::find(ids.begin(), ids.end(), selFlight_);
    int i = it == ids.end() ? (dir > 0 ? -1 : 0) : int(it - ids.begin());
    i = (i + dir + int(ids.size())) % int(ids.size());
    selectFlight(ids[size_t(i)]);
}

std::vector<const sim::AirTrack*> GameUi::knownAirTracks() const {
    std::vector<const sim::AirTrack*> out;
    if (!session_ || playerSide() == sim::Side::Neutral) return out;
    const sim::World& w = session_->world();
    for (const auto& t : w.picture(playerSide()).airTracks()) {
        if (t.lost) continue;
        bool known = t.sharedAt <= w.time();
        for (const auto& h : t.holders)
            if (const sim::Ship* s = w.ship(h.ship); s && s->side == playerSide() && w.time() - h.lastSeen < 30.0) known = true;
        if (known) out.push_back(&t);
    }
    return out;
}

// ------------------------------------------------------------------------------------------- orders
bool GameUi::issueFlightOrder(int id, sim::AirOrder o) {
    sim::World& w = session_->world();
    sim::Flight* f = w.flight(id);
    if (!f) return false;
    if (o.altitude < 0 && airAltitude_ > 0) o.altitude = kAltitudes[airAltitude_];
    if (!f->airborne()) o.startTime = w.time() + launchDelayMin_ * 60.0;
    std::string why;
    bool ok = session_->orderFlight(id, o, &why);
    std::string name = locName(f->name);
    if (ok) {
        lastAirMsg_ = loc::format("air.msg.ordered", {name, tr(sim::airOrderKey(o.type))});
        if (!f->airborne() && launchDelayMin_ > 0) lastAirMsg_ += "  " + loc::format("air.msg.launch_in", {std::to_string(launchDelayMin_)});
    } else {
        lastAirMsg_ = name + ": " + (why.empty() ? std::string(tr("air.why.unknown")) : std::string(tr(why.c_str())));
    }
    LogLine l;
    l.text = lastAirMsg_;
    l.color = ok ? col::kText : col::kWarn;
    l.flight = id;
    addLog(l);
    svc.playUiSound("click");
    return ok;
}

void GameUi::issueMapOrder(const Vec2& p) {
    MapOrder mode = mapOrder_;
    if (mode == MapOrder::Waypoints) {
        addWaypoint(p, true);
        return;  // stays active: each click adds a leg until right click / Esc
    }
    mapOrder_ = MapOrder::None;
    sim::Flight* f = selectedFlight();
    if (!f) return;
    sim::AirOrder o;
    o.point = p;
    o.route = {p};
    switch (mode) {
        case MapOrder::Search:
            o.type = sim::AirOrderType::Patrol;
            o.strike = true;
            break;
        case MapOrder::Recon: o.type = sim::AirOrderType::Patrol; break;
        case MapOrder::Cap: o.type = sim::AirOrderType::Cap; break;
        case MapOrder::Flares:
            o.type = sim::AirOrderType::Flares;
            o.radius = 3000;
            break;
        default: return;
    }
    issueFlightOrder(f->id, o);
}

// ------------------------------------------------------------------------------------------- fleet list
void GameUi::drawAirList() {
    const sim::World& w = session_->world();
    sim::Side me = playerSide();
    bool any = false;
    for (const auto& f : w.flights())
        if ((me == sim::Side::Neutral || f.side == me) && f.alive(w) > 0) any = true;
    if (!any) return;
    ImGui::Spacing();
    sectionHeader(tr("air.title"));
    for (const auto& f : w.flights()) {
        if (me != sim::Side::Neutral && f.side != me) continue;
        int alive = f.alive(w);
        if (alive == 0 && f.state == sim::FlightState::Lost) continue;
        ImGui::PushID(1000000 + f.id);
        bool sel = f.id == selFlight_;
        float fh = ImGui::GetFontSize();
        ImVec4 c = alive == 0 ? col::kTextDim : me == sim::Side::Neutral ? sideColor(f.side) : col::kText;
        // Two lines: symbol and name; state, aircraft and fuel against the way home.
        ImVec2 top = ImGui::GetCursorScreenPos();
        float rowH = fh * 2.25f;
        if (ImGui::Selectable("##flight", sel, ImGuiSelectableFlags_SpanAllColumns, {0, rowH})) selectFlight(f.id);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && f.airborne()) map_.focus(f.pos);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
        drawAircraftSymbol(dl, {top.x + fh * 0.45f, top.y + fh * 0.55f}, fh * 0.38f, 0.0f, toU32(c), 1.3f);
        std::string name = locName(f.name);
        if (!f.playerControlled && me != sim::Side::Neutral) name += "  (" + std::string(tr("air.ai_short")) + ")";
        dl->PushClipRect(top, {right, top.y + rowH}, true);
        dl->AddText({top.x + fh * 1.1f, top.y}, toU32(c), name.c_str());
        char line[96];
        std::snprintf(line, sizeof(line), "%d  %s", alive, tr(sim::flightStateKey(f.state)));
        dl->AddText({top.x + fh * 1.1f, top.y + fh * 1.1f}, toU32(stateColor(f.state), 0.85f), line);
        dl->PopClipRect();
        float fuel = float(std::clamp(f.fuel / std::max(1.0, f.def->enduranceH * 3600.0), 0.0, 1.0));
        float bw = fh * 2.2f, bh = fh * 0.28f;
        ImVec2 q{right - bw, top.y + fh * 1.45f};
        dl->AddRectFilled(q, {q.x + bw, q.y + bh}, IM_COL32(40, 50, 55, 255));
        dl->AddRectFilled(q, {q.x + bw * fuel, q.y + bh}, toU32(fuel > 0.4f ? col::kBrassDim : col::kWarn));
        ImGui::PopID();
    }
}

// ------------------------------------------------------------------------------------------- flight panel
void GameUi::drawFlightPanel(sim::Flight& f) {
    sim::World& w = session_->world();
    const sim::AircraftDef& d = *f.def;
    sim::Side me = playerSide();
    float fh = ImGui::GetFontSize();
    ImVec2 p = ImGui::GetCursorScreenPos();
    drawFlag(ImGui::GetWindowDrawList(), {p.x, p.y + 4}, {fh * 1.9f, fh * 1.2f}, f.nation);
    ImGui::Dummy({fh * 2.1f, fh * 1.3f});
    ImGui::SameLine();
    ImGui::BeginGroup();
    {
        FontScope fs(fonts().heading, 1.5f);
        ImGui::TextUnformatted(locName(f.name).c_str());
    }
    ImGui::EndGroup();
    sim::AirRole role = d.role;
    if (f.loadout && f.loadout->torpedo) role = sim::AirRole::Torpedo;
    else if (f.loadout && !f.loadout->armed() && role != sim::AirRole::Fighter) role = sim::AirRole::Recon;
    ImGui::PushStyleColor(ImGuiCol_Text, col::kTextDim);
    ImGui::TextWrapped("%d x %s  |  %s  |  %s", int(f.planes.size()), d.name.c_str(), tr(sim::airRoleKey(role)), crewName(f.crew));
    ImGui::PopStyleColor();
    const sim::AirBase* base = w.airBase(f.base);
    if (base) ImGui::TextDisabled("%s: %s", tr("air.base"), locName(base->name).c_str());

    // ---- Status
    int alive = f.alive(w), flying = 0, armed = 0, torps = 0, bombs = 0;
    for (sim::EntityId pid : f.planes)
        if (const sim::Aircraft* a = w.plane(pid)) {
            if (a->flying()) ++flying;
            if ((a->flying() || a->fate == sim::Aircraft::Fate::Parked) && a->armed() && !a->aborted) ++armed;
            torps += a->torpedoes;
            bombs += a->bombs + a->bombs2;
        }
    ImGui::TextColored(stateColor(f.state), "%s", tr(sim::flightStateKey(f.state)));
    ImGui::SameLine();
    ImGui::TextDisabled("-  %s", tr(sim::airOrderKey(f.order.type)));
    if (f.order.type == sim::AirOrderType::Patrol && f.order.strike) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%s)", tr("air.armed_search"));
    }
    if (f.state == sim::FlightState::Ready && f.order.type != sim::AirOrderType::None && f.order.startTime > w.time()) {
        ImGui::TextColored(col::kWarn, "%s", loc::format("air.msg.launch_in", {std::to_string(int(std::ceil((f.order.startTime - w.time()) / 60.0)))}).c_str());
    }
    ImGui::Text("%s: %d / %d", tr("air.aircraft"), alive, int(f.planes.size()));
    if (f.airborne()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%d %s)", flying, tr("air.in_the_air"));
    }
    if (f.losses > 0) {
        ImGui::SameLine();
        ImGui::TextColored(col::kBad, "-%d", f.losses);
    }
    if (f.loadout && f.loadout->armed()) {
        std::string load;
        if (f.loadout->torpedo) load = std::to_string(torps) + " x " + f.loadout->torpedo->name;
        else if (f.loadout->bomb) load = std::to_string(bombs) + " x " + std::to_string(int(f.loadout->bomb->massKg)) + " kg";
        ImGui::Text("%s: %s", tr("air.weapons"), load.c_str());
        if (armed == 0 && alive > 0) {
            ImGui::SameLine();
            ImGui::TextColored(col::kWarn, "(%s)", tr("air.expended"));
        }
    } else if (f.flaresLeft > 0 && d.flares > 0) {
        ImGui::Text("%s: %d", tr("air.flares"), f.flaresLeft);
    }
    if (f.airborne()) {
        double endurance = std::max(1.0, d.enduranceH * 3600.0);
        double home = sim::air::timeToBase(w, f);
        char ov[64];
        std::snprintf(ov, sizeof(ov), "%s  (%s %s)", fmtDuration(f.fuel).c_str(), tr("air.home"), fmtDuration(home).c_str());
        statusBar(tr("air.fuel"), float(f.fuel / endurance), f.fuel < home * 1.3 ? col::kWarn : col::kBrassDim, ov);
        ImGui::Text("%s %s   %s   %.0f m", tr("ui.course"), fmtBearing(f.heading).c_str(), fmtKnots(f.speed / 0.514444).c_str(), f.altitude);
        if (const sim::Ship* s = w.ship(selShip_))
            ImGui::TextDisabled("%s %s  %s", tr("ui.range"), fmtRange(distance(s->pos, f.pos), settings.metric).c_str(),
                                fmtBearing(bearingTo(s->pos, f.pos)).c_str());
    }
    if (f.kills > 0 || f.hits > 0) ImGui::TextDisabled("%s: %d   %s: %d", tr("air.kills"), f.kills, tr("air.hits"), f.hits);
    if (ImGui::Checkbox(tr("ship.follow"), &follow_)) {
    }

    // ---- Command
    bool own = me != sim::Side::Neutral && f.side == me;
    if (!own) return;
    if (!f.playerControlled) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", tr("air.ai_controlled_hint"));
        if (ImGui::Button(tr("air.take_command"), {-1, 0})) session_->commandFlight(f.id, true);
        return;
    }
    if (alive == 0) return;
    bool canOrder = f.state != sim::FlightState::Rearming;
    if (!canOrder) {
        ImGui::TextColored(col::kWarn, "%s", tr("air.rearming_hint"));
        return;
    }
    double now = w.time();
    sectionHeader(tr("air.orders"));
    // Altitude and launch time apply to the next order.
    const char* alts[] = {tr("air.alt.auto"), tr("air.alt.low"), tr("air.alt.medium"), tr("air.alt.high")};
    ImGui::SetNextItemWidth(fh * 7.0f);
    ImGui::Combo(tr("air.altitude"), &airAltitude_, alts, 4);
    if (!f.airborne()) {
        ImGui::SetNextItemWidth(fh * 7.0f);
        ImGui::SliderInt(tr("air.launch_delay"), &launchDelayMin_, 0, 180, tr("air.minutes_fmt"));
    }

    // Targets: the side's ship contacts, nearest the flight first.
    auto hostiles = knownHostiles();
    Vec2 from = f.airborne() ? f.pos : (base ? base->pos : f.pos);
    std::sort(hostiles.begin(), hostiles.end(), [&](auto* a, auto* b) { return distance(a->predict(now), from) < distance(b->predict(now), from); });
    static uint32_t chosen = 0;
    if (selTrack_) chosen = selTrack_;
    const sim::Track* tgt = nullptr;
    for (auto* t : hostiles)
        if (t->id == chosen) tgt = t;
    if (!tgt && !hostiles.empty()) tgt = hostiles.front();
    float half = ImGui::GetContentRegionAvail().x * 0.5f - 3.0f;
    bool armedLoad = f.loadout && f.loadout->armed() && armed > 0;
    if (armedLoad || d.flares > 0 || d.role == sim::AirRole::Recon || d.role == sim::AirRole::Patrol) {
        std::string cur = tgt ? trackName(*tgt) + "  " + fmtRange(distance(tgt->predict(now), from), settings.metric) : tr("air.no_target");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##airtarget", cur.c_str())) {
            for (auto* t : hostiles) {
                std::string lbl = trackName(*t) + "  " + fmtRange(distance(t->predict(now), from), settings.metric);
                if (ImGui::Selectable(lbl.c_str(), tgt == t)) {
                    chosen = t->id;
                    selTrack_ = t->id;
                }
            }
            ImGui::EndCombo();
        }
    }
    auto orderOn = [&](sim::AirOrderType type) {
        if (!tgt) return;
        sim::AirOrder o;
        o.type = type;
        o.track = tgt->id;
        o.point = tgt->predict(now);
        o.radius = 3000;
        issueFlightOrder(f.id, o);
    };
    auto mapButton = [&](const char* label, MapOrder m, float width) {
        bool active = mapOrder_ == m;
        if (toggleButton(label, active, {width, 0})) {
            mapOrder_ = active ? MapOrder::None : m;
            if (mapOrder_ != MapOrder::None && view_ == View::Scene3D) view_ = View::Split;
        }
    };
    if (armedLoad) {
        ImGui::BeginDisabled(!tgt);
        if (ImGui::Button(tr("air.cmd.attack"), {half, 0})) orderOn(sim::AirOrderType::Attack);
        ImGui::EndDisabled();
        ImGui::SameLine();
        mapButton(tr("air.cmd.search"), MapOrder::Search, half);
    }
    if (!armedLoad || d.role == sim::AirRole::Recon || d.role == sim::AirRole::Patrol) {
        mapButton(tr("air.cmd.recon"), MapOrder::Recon, half);
        ImGui::SameLine();
        ImGui::BeginDisabled(!tgt);
        if (ImGui::Button(tr("air.cmd.shadow"), {half, 0})) orderOn(sim::AirOrderType::Spot);
        ImGui::EndDisabled();
    }
    if (d.flares > 0 && f.flaresLeft > 0) {
        ImGui::BeginDisabled(!tgt);
        if (ImGui::Button(tr("air.cmd.flares_contact"), {half, 0})) orderOn(sim::AirOrderType::Flares);
        ImGui::EndDisabled();
        ImGui::SameLine();
        mapButton(tr("air.cmd.flares_point"), MapOrder::Flares, half);
    }
    if (fighterLike(f)) {
        // Fighter cover over an own group, at a point, or escort for another flight.
        static int coverGroup = -1;
        std::vector<const sim::Group*> groups;
        for (auto& g : w.groups())
            if (g.side == me && w.guideOf(g)) groups.push_back(&g);
        if (coverGroup < 0 || !w.group(coverGroup) || w.group(coverGroup)->side != me) coverGroup = groups.empty() ? -1 : groups.front()->id;
        const sim::Group* cg = w.group(coverGroup);
        ImGui::SetNextItemWidth(half);
        if (ImGui::BeginCombo("##cover", cg ? locName(cg->name).c_str() : "-")) {
            for (auto* g : groups)
                if (ImGui::Selectable(locName(g->name).c_str(), g->id == coverGroup)) coverGroup = g->id;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!cg);
        if (ImGui::Button(tr("air.cmd.cap_group"), {half, 0})) {
            sim::AirOrder o;
            o.type = sim::AirOrderType::Cap;
            o.group = coverGroup;
            if (sim::Ship* gs = w.guideOf(*w.group(coverGroup))) o.point = gs->pos;
            issueFlightOrder(f.id, o);
        }
        ImGui::EndDisabled();
        mapButton(tr("air.cmd.cap_point"), MapOrder::Cap, half);
        ImGui::SameLine();
        static int escortOf = -1;
        std::vector<const sim::Flight*> others;
        for (const auto& o : w.flights())
            if (o.side == me && o.id != f.id && o.alive(w) > 0 && o.state != sim::FlightState::Lost && !fighterLike(o)) others.push_back(&o);
        if (ImGui::Button(tr("air.cmd.escort"), {half, 0}) && !others.empty()) ImGui::OpenPopup("escortpick");
        if (others.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("air.no_escort"));
        if (ImGui::BeginPopup("escortpick")) {
            for (auto* o : others)
                if (ImGui::MenuItem(locName(o->name).c_str())) {
                    escortOf = o->id;
                    sim::AirOrder ord;
                    ord.type = sim::AirOrderType::Escort;
                    ord.escort = escortOf;
                    ord.point = o->pos;
                    issueFlightOrder(f.id, ord);
                }
            ImGui::EndPopup();
        }
        // Intercept the selected raid: fighters go for the point ahead of it.
        if (const sim::AirTrack* at = selAirTrack_ ? w.picture(me).findAir(selAirTrack_) : nullptr; at && !at->lost) {
            std::string lbl = loc::format("air.cmd.intercept", {"A" + std::to_string(at->id)});
            if (ImGui::Button(lbl.c_str(), {-1, 0})) {
                sim::AirOrder o;
                o.type = sim::AirOrderType::Cap;
                o.point = at->predict(now) + at->vel * 120.0;
                o.radius = 20000;
                issueFlightOrder(f.id, o);
            }
        }
    }
    if (f.airborne()) {
        if (ImGui::Button(tr("air.cmd.return"), {-1, 0})) {
            sim::AirOrder o;
            o.type = sim::AirOrderType::Return;
            issueFlightOrder(f.id, o);
        }
    } else if (f.order.type != sim::AirOrderType::None && f.order.startTime >= 0) {
        if (ImGui::Button(tr("air.cmd.cancel_launch"), {-1, 0})) {
            f.order = sim::AirOrder{};
            lastAirMsg_ = tr("air.msg.launch_cancelled");
        }
    }
    if (mapOrder_ != MapOrder::None && mapOrder_ != MapOrder::Waypoints) ImGui::TextColored(col::kBrass, "%s", tr("air.pick_point_hint"));
    if (!lastAirMsg_.empty()) ImGui::TextWrapped("%s", lastAirMsg_.c_str());
    ImGui::Spacing();
    if (ImGui::SmallButton(tr("air.leave_to_ai"))) session_->commandFlight(f.id, false);
    helpMarker(tr("air.leave_to_ai_help"));
}

// ------------------------------------------------------------------------------------------- plot menu
void GameUi::flightMapMenu(sim::Flight& f, const MapOutput& ctx) {
    sim::World& w = session_->world();
    sim::Side me = playerSide();
    double now = w.time();
    ImGui::TextDisabled("%s", locName(f.name).c_str());
    ImGui::Separator();
    bool armed = false;
    for (sim::EntityId pid : f.planes)
        if (const sim::Aircraft* a = w.plane(pid); a && (a->flying() || a->fate == sim::Aircraft::Fate::Parked) && a->armed() && !a->aborted) armed = true;
    sim::AirOrder o;
    o.point = ctx.rightClickPos;
    o.route = {ctx.rightClickPos};
    if (const sim::Track* t = ctx.rightTrack ? w.picture(me).find(ctx.rightTrack) : nullptr) {
        std::string n = trackName(*t);
        o.track = t->id;
        o.point = t->predict(now);
        if (armed && ImGui::MenuItem(loc::format("air.ctx.attack", {n}).c_str())) {
            o.type = sim::AirOrderType::Attack;
            issueFlightOrder(f.id, o);
        }
        if (ImGui::MenuItem(loc::format("air.ctx.shadow", {n}).c_str())) {
            o.type = sim::AirOrderType::Spot;
            issueFlightOrder(f.id, o);
        }
        if (f.flaresLeft > 0 && ImGui::MenuItem(loc::format("air.ctx.flares", {n}).c_str())) {
            o.type = sim::AirOrderType::Flares;
            o.radius = 3000;
            issueFlightOrder(f.id, o);
        }
        ImGui::Separator();
        o.track = 0;
        o.point = ctx.rightClickPos;
    }
    if (const sim::AirTrack* at = ctx.rightAirTrack ? w.picture(me).findAir(ctx.rightAirTrack) : nullptr; at && fighterLike(f)) {
        if (ImGui::MenuItem(loc::format("air.cmd.intercept", {"A" + std::to_string(at->id)}).c_str())) {
            o.type = sim::AirOrderType::Cap;
            o.point = at->predict(now) + at->vel * 120.0;
            o.radius = 20000;
            issueFlightOrder(f.id, o);
        }
        ImGui::Separator();
    }
    if (const sim::Ship* s = w.ship(ctx.rightShip); s && s->side == me && fighterLike(f)) {
        if (const sim::Group* g = w.group(s->groupId); g && ImGui::MenuItem(loc::format("air.ctx.cover", {locName(g->name)}).c_str())) {
            o.type = sim::AirOrderType::Cap;
            o.group = g->id;
            o.point = s->pos;
            issueFlightOrder(f.id, o);
        }
    }
    if (const sim::Flight* e = w.flight(ctx.rightFlight); e && e->id != f.id && e->side == me && fighterLike(f)) {
        if (ImGui::MenuItem(loc::format("air.ctx.escort", {locName(e->name)}).c_str())) {
            o.type = sim::AirOrderType::Escort;
            o.escort = e->id;
            o.point = e->pos;
            issueFlightOrder(f.id, o);
        }
    }
    if (armed && ImGui::MenuItem(tr("air.ctx.search_here"))) {
        o.type = sim::AirOrderType::Patrol;
        o.strike = true;
        issueFlightOrder(f.id, o);
    }
    if (ImGui::MenuItem(tr("air.ctx.recon_here"))) {
        o.type = sim::AirOrderType::Patrol;
        o.strike = false;
        issueFlightOrder(f.id, o);
    }
    if (f.airborne() && f.order.type == sim::AirOrderType::Patrol && ImGui::MenuItem(tr("air.ctx.add_leg"))) {
        sim::AirOrder leg = f.order;
        std::vector<Vec2> rest(leg.route.begin() + std::min(f.leg, leg.route.size()), leg.route.end());
        rest.push_back(ctx.rightClickPos);
        leg.route = rest;
        leg.point = rest.front();
        issueFlightOrder(f.id, leg);
    }
    if (fighterLike(f) && ImGui::MenuItem(tr("air.ctx.cap_here"))) {
        o.type = sim::AirOrderType::Cap;
        issueFlightOrder(f.id, o);
    }
    if (f.flaresLeft > 0 && ImGui::MenuItem(tr("air.ctx.flares_here"))) {
        o.type = sim::AirOrderType::Flares;
        o.radius = 3000;
        issueFlightOrder(f.id, o);
    }
    if (f.airborne()) {
        ImGui::Separator();
        if (ImGui::MenuItem(tr("air.cmd.return"))) {
            sim::AirOrder r;
            r.type = sim::AirOrderType::Return;
            issueFlightOrder(f.id, r);
        }
    }
}

}  // namespace it::ui
