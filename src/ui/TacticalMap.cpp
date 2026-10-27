#include "ui/TacticalMap.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Localization.h"
#include "ui/Format.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace it::ui {

namespace {

constexpr float kPickRadius = 13.0f;

}  // namespace

struct Pick {
    ImVec2 s;
    sim::EntityId ship = sim::kNoEntity;
    uint32_t track = 0;
    int marker = -1;
    int flight = -1;
    uint32_t airTrack = 0;
};

namespace {

double niceStep(double raw) {
    double e = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (raw <= m * e) return m * e;
    return 10.0 * e;
}

void dashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float dash, float th = 1.0f) {
    float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1.0f) return;
    int n = int(len / dash);
    for (int i = 0; i < n; i += 2) {
        float t0 = i * dash / len, t1 = std::min(1.0f, (i + 1) * dash / len);
        dl->AddLine({a.x + dx * t0, a.y + dy * t0}, {a.x + dx * t1, a.y + dy * t1}, col, th);
    }
}

void dashedCircle(ImDrawList* dl, ImVec2 c, float r, ImU32 col, int segs, float th = 1.0f) {
    for (int i = 0; i < segs; i += 2) {
        float a0 = float(kTwoPi * i / segs), a1 = float(kTwoPi * (i + 1) / segs);
        dl->AddLine({c.x + std::cos(a0) * r, c.y + std::sin(a0) * r}, {c.x + std::cos(a1) * r, c.y + std::sin(a1) * r}, col, th);
    }
}

const char* trackLabel(const sim::Track& t, char* buf, size_t n) {
    const char* what = "?";
    if (t.level >= sim::ClassLevel::TypeKnown) what = shipTypeAbbrev(t.typeGuess);
    else if (t.level >= sim::ClassLevel::SizeKnown)
        what = t.size == sim::SizeClass::Large ? tr("map.size_large") : t.size == sim::SizeClass::Medium ? tr("map.size_medium") : tr("map.size_small");
    std::snprintf(buf, n, "T%u %s", t.id, what);
    return buf;
}

bool sideKnows(const sim::Track& t, const sim::World& w, sim::Side side) {
    if (t.sharedAt <= w.time()) return true;
    for (const auto& h : t.holders)
        if (w.time() - h.lastSeen < 30.0)
            if (const sim::Ship* s = w.ship(h.ship); s && s->side == side) return true;
    return false;
}

bool sideKnowsAir(const sim::AirTrack& t, const sim::World& w, sim::Side side) {
    if (t.sharedAt <= w.time()) return true;
    for (const auto& h : t.holders)
        if (w.time() - h.lastSeen < 30.0)
            if (const sim::Ship* s = w.ship(h.ship); s && s->side == side) return true;
    return false;
}

/// Where a flight's order takes it (for the order line on the plot).
std::vector<Vec2> orderPath(const sim::World& w, const sim::Flight& f, sim::Side perspective) {
    using T = sim::AirOrderType;
    std::vector<Vec2> out{f.pos};
    const auto& o = f.order;
    auto trackPos = [&](uint32_t id, Vec2& p) {
        if (const sim::Track* t = w.picture(perspective == sim::Side::Neutral ? f.side : perspective).find(id)) {
            p = t->lost ? t->pos : t->predict(w.time());
            return true;
        }
        return false;
    };
    Vec2 p;
    switch (o.type) {
        case T::Patrol:
            for (size_t i = std::min(f.leg, o.route.size()); i < o.route.size(); ++i) out.push_back(o.route[i]);
            if (o.route.empty()) out.push_back(o.point);
            break;
        case T::Attack:
        case T::Flares:
        case T::Spot:
            if (o.track && trackPos(o.track, p)) out.push_back(p);
            else out.push_back(o.point);
            break;
        case T::Cap:
            if (const sim::Group* g = w.group(o.group))
                for (auto id : g->members)
                    if (const sim::Ship* s = w.ship(id); s && s->operational()) {
                        out.push_back(s->pos);
                        break;
                    }
            if (out.size() == 1) out.push_back(o.point);
            break;
        case T::Escort:
            if (const sim::Flight* e = w.flight(o.escort)) out.push_back(e->pos);
            break;
        case T::Return:
            if (const sim::AirBase* b = w.airBase(f.base)) {
                if (const sim::Ship* s = w.ship(b->ship)) out.push_back(s->pos);
                else out.push_back(b->pos);
            }
            break;
        default: break;
    }
    return out;
}

}  // namespace

void TacticalMap::drawAir(ImDrawList* dl, const MapInput& in, std::vector<Pick>& picks) {
    const sim::World& w = *in.world;
    const double now = w.time();
    const bool umpire = in.side == sim::Side::Neutral;
    // Air bases (catapult ships are their own symbol).
    for (const auto& b : w.airBases()) {
        if (b.ship != sim::kNoEntity || (!umpire && b.side != in.side)) continue;
        ImVec2 c = toScreen(b.pos);
        ImU32 colr = toU32(umpire ? sideColor(b.side) : col::kAllies, 0.85f);
        drawAirBaseSymbol(dl, c, 6.0f, colr);
        if (!in.compact) dl->AddText({c.x + 10, c.y - 7}, colr, locName(b.name).c_str());
    }
    // Own flights.
    for (const auto& f : w.flights()) {
        if (!umpire && f.side != in.side) continue;
        if (!f.airborne() || f.alive(w) == 0) continue;
        bool sel = f.id == in.selectedFlight;
        ImVec4 base = umpire ? sideColor(f.side) : ImVec4{0.55f, 0.85f, 1.0f, 1.0f};
        ImU32 colr = toU32(base, f.playerControlled || umpire ? 1.0f : 0.7f);
        ImVec2 c = toScreen(f.pos);
        // Order line: full for the selected flight, a faint hint for the others.
        if (!in.compact && (sel || mpp < 120)) {
            auto path = orderPath(w, f, in.side);
            for (size_t i = 0; i + 1 < path.size(); ++i)
                dashedLine(dl, toScreen(path[i]), toScreen(path[i + 1]), toU32(base, sel ? 0.8f : 0.3f), sel ? 6.0f : 4.0f, sel ? 1.5f : 1.0f);
            if (sel && path.size() > 1) dl->AddCircle(toScreen(path.back()), 5.0f, toU32(base, 0.8f), 12, 1.5f);
        }
        if (mpp < 9.0) {
            // Close in: every aircraft of the formation.
            for (sim::EntityId pid : f.planes)
                if (const sim::Aircraft* a = w.plane(pid); a && a->flying())
                    drawAircraftSymbol(dl, toScreen(a->pos.xy()), 4.5f, float(a->heading * kDegToRad), colr, 1.2f);
        } else {
            drawAircraftSymbol(dl, c, 7.5f, float(f.heading * kDegToRad), colr);
        }
        if (sel) dl->AddCircle(c, 14.0f, toU32(col::kBrass), 24, 2.0f);
        if (!in.compact && (mpp < 150 || sel)) {
            char b[96];
            std::snprintf(b, sizeof(b), "%s  %d", locName(f.name).c_str(), f.alive(w));
            dl->AddText({c.x + 11, c.y - 16}, toU32(base, 0.9f), b);
        }
        Pick p;
        p.s = c;
        p.flight = f.id;
        picks.push_back(p);
    }
    // The side's air picture.
    if (umpire) return;
    for (const auto& t : w.picture(in.side).airTracks()) {
        if (!sideKnowsAir(t, w, in.side)) continue;
        if (t.lost && t.age(now) > 120.0) continue;
        Vec2 pos = t.lost ? t.pos : t.predict(now);
        ImVec2 c = toScreen(pos);
        ImU32 colr = t.lost ? IM_COL32(150, 150, 150, 200) : t.attacking ? toU32(col::kAxis) : toU32(col::kAxis, 0.85f);
        if (!t.lost && length(t.vel) > 5.0) dl->AddLine(c, toScreen(pos + t.vel * 120.0), colr, 1.3f);
        float hdg = length(t.vel) > 5.0 ? float(bearingOf(t.vel) * kDegToRad) : 0.0f;
        drawAircraftSymbol(dl, c, 7.0f, hdg, colr);
        dl->AddCircle(c, 10.0f, (colr & 0x00ffffff) | 0x60000000, 16, 1.0f);
        if (now - t.attackSeen < 8.0) {
            float k = float(1.0 - (now - t.attackSeen) / 8.0);
            dl->AddCircle(c, 13.0f + 6.0f * (1.0f - k), IM_COL32(255, 120, 90, int(230 * k)), 16, 2.0f);
        }
        if (t.id == in.selectedAirTrack) dl->AddRect({c.x - 13, c.y - 13}, {c.x + 13, c.y + 13}, toU32(col::kBrass), 0.0f, 0, 2.0f);
        if (!in.compact && mpp < 150) {
            std::string lbl = "A" + std::to_string(t.id);
            if (t.count > 0) lbl += "  " + std::to_string(t.count) + "x";
            if (t.roleKnown) lbl += std::string(" ") + tr(sim::airRoleKey(t.role));
            if (t.altitude >= 0) lbl += "  " + std::to_string(int(std::round(t.altitude / 100.0) * 100)) + " m";
            dl->AddText({c.x + 12, c.y - 7}, (colr & 0x00ffffff) | 0xd0000000, lbl.c_str());
        }
        Pick p;
        p.s = c;
        p.airTrack = t.id;
        picks.push_back(p);
    }
}

void TacticalMap::fit(const Vec2& lo, const Vec2& hi, ImVec2 size) {
    center = (lo + hi) * 0.5;
    double sx = (hi.x - lo.x) / std::max(50.0f, size.x), sy = (hi.y - lo.y) / std::max(50.0f, size.y);
    mpp = std::max({sx, sy, 2.0}) * 1.08;
}

void TacticalMap::sampleHistory(const MapInput& in) {
    const sim::World& w = *in.world;
    if (w.time() - lastSample_ < 15.0) return;
    lastSample_ = w.time();
    for (const auto& sp : w.ships()) {
        const sim::Ship& s = *sp;
        if (!s.alive()) continue;
        if (in.side != sim::Side::Neutral && s.side != in.side) continue;
        auto& h = history_[s.id];
        h.push_back(s.pos);
        if (h.size() > 240) h.pop_front();
    }
    if (in.side != sim::Side::Neutral)
        for (const auto& t : w.picture(in.side).tracks()) {
            if (t.lost || !sideKnows(t, w, in.side)) continue;
            auto& h = trackHistory_[t.id];
            h.push_back(t.pos);
            if (h.size() > 240) h.pop_front();
        }
}

void TacticalMap::drawGrid(ImDrawList* dl, const MapInput& in) {
    double unit = in.metric ? 1000.0 : 1852.0;
    double step = niceStep(mpp * 110.0 / unit) * unit;
    Vec2 lo = toWorld({canvasMin_.x, canvasMax_.y}), hi = toWorld({canvasMax_.x, canvasMin_.y});
    ImU32 line = IM_COL32(120, 150, 165, 38), label = IM_COL32(150, 170, 180, 150);
    for (double x = std::floor(lo.x / step) * step; x <= hi.x; x += step) {
        float sx = toScreen({x, 0}).x;
        dl->AddLine({sx, canvasMin_.y}, {sx, canvasMax_.y}, line);
        if (!in.compact) {
            char b[32];
            std::snprintf(b, sizeof(b), "%.0f", x / unit);
            dl->AddText({sx + 3, canvasMin_.y + 2}, label, b);
        }
    }
    for (double y = std::floor(lo.y / step) * step; y <= hi.y; y += step) {
        float sy = toScreen({0, y}).y;
        dl->AddLine({canvasMin_.x, sy}, {canvasMax_.x, sy}, line);
        if (!in.compact) {
            char b[32];
            std::snprintf(b, sizeof(b), "%.0f", y / unit);
            dl->AddText({canvasMin_.x + 3, sy + 1}, label, b);
        }
    }
}

void TacticalMap::drawScale(ImDrawList* dl, const MapInput& in) {
    double unit = in.metric ? 1000.0 : 1852.0;
    double len = niceStep(mpp * 140.0 / unit) * unit;
    float px = float(len / mpp);
    ImVec2 a{canvasMin_.x + 14, canvasMax_.y - 18};
    ImU32 c = toU32(col::kText, 0.85f);
    dl->AddLine(a, {a.x + px, a.y}, c, 2.0f);
    dl->AddLine({a.x, a.y - 5}, {a.x, a.y + 3}, c, 2.0f);
    dl->AddLine({a.x + px, a.y - 5}, {a.x + px, a.y + 3}, c, 2.0f);
    char b[32];
    std::snprintf(b, sizeof(b), "%g %s", len / unit, in.metric ? "km" : "nm");
    dl->AddText({a.x + px + 6, a.y - 9}, c, b);
    // North arrow.
    ImVec2 n{canvasMax_.x - 22, canvasMin_.y + 34};
    dl->AddTriangleFilled({n.x, n.y - 16}, {n.x - 6, n.y + 4}, {n.x + 6, n.y + 4}, toU32(col::kBrass, 0.9f));
    dl->AddText({n.x - 4, n.y + 6}, toU32(col::kBrass), "N");
}

MapOutput TacticalMap::draw(const char* id, ImVec2 size, const MapInput& in) {
    MapOutput out;
    ImGui::PushID(id);
    canvasMin_ = ImGui::GetCursorScreenPos();
    size.x = std::max(size.x, 50.0f);
    size.y = std::max(size.y, 50.0f);
    canvasMax_ = {canvasMin_.x + size.x, canvasMin_.y + size.y};
    origin_ = {canvasMin_.x + size.x * 0.5f, canvasMin_.y + size.y * 0.5f};
    ImGui::InvisibleButton("##map", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(canvasMin_, canvasMax_, true);
    dl->AddRectFilled(canvasMin_, canvasMax_, IM_COL32(12, 26, 40, 255));

    const sim::World* w = in.world;
    const double now = w ? w->time() : 0.0;
    if (w) sampleHistory(in);

    // ---- Chart and coastlines
    if (in.terrain && !in.terrain->empty()) {
        const auto& t = *in.terrain;
        double half = t.info().cellSize * 0.5;
        Vec2 lo = t.minCorner() - Vec2{half, half}, hi = t.maxCorner() + Vec2{half, half};
        if (in.chartTexture) dl->AddImage(ImTextureRef(ImTextureID(in.chartTexture)), toScreen({lo.x, hi.y}), toScreen({hi.x, lo.y}));
        ImU32 coast = in.chartTexture ? IM_COL32(205, 190, 140, 200) : IM_COL32(205, 190, 140, 255);
        std::vector<ImVec2> pts;
        for (const auto& line : t.coastlines()) {
            pts.clear();
            for (const auto& p : line) {
                ImVec2 s = toScreen(p);
                if (pts.empty() || std::abs(s.x - pts.back().x) + std::abs(s.y - pts.back().y) > 1.5f) pts.push_back(s);
            }
            if (pts.size() >= 2) dl->AddPolyline(pts.data(), int(pts.size()), coast, ImDrawFlags_None, 1.3f);
        }
    }
    drawGrid(dl, in);

    // Hit-test candidates.
    std::vector<Pick> picks;

    if (w) {
        // Squalls (rain cells are visible to everybody).
        for (const auto& sq : w->env().squalls()) {
            ImVec2 c = toScreen(sq.pos);
            float r = float(sq.radius / mpp);
            dl->AddCircleFilled(c, r, IM_COL32(120, 140, 160, int(30 + 40 * sq.intensity)), 40);
            dl->AddCircle(c, r, IM_COL32(150, 170, 190, 90), 40, 1.0f);
        }
        // Flares and starshells.
        for (const auto& f : w->flares()) {
            if (!f.alive) continue;
            ImVec2 c = toScreen({f.pos.x, f.pos.y});
            dl->AddCircleFilled(c, 4.0f, IM_COL32(255, 240, 170, 230));
            dl->AddCircle(c, float(1500.0 / mpp), IM_COL32(255, 240, 170, 40), 32);
        }
        // Plot history (dotted).
        for (const auto& [sid, h] : history_) {
            const sim::Ship* s = w->ship(sid);
            ImU32 c = toU32(s && in.side == sim::Side::Neutral ? sideColor(s->side) : col::kAllies, 0.45f);
            for (const auto& p : h) dl->AddCircleFilled(toScreen(p), 1.3f, c);
        }
        for (const auto& [tid, h] : trackHistory_)
            for (const auto& p : h) dl->AddCircleFilled(toScreen(p), 1.3f, IM_COL32(250, 120, 100, 110));

        // Sighted torpedo tracks.
        for (const auto& t : w->torpedoes()) {
            if (!t.alive) continue;
            bool seen = in.side == sim::Side::Neutral;
            for (sim::EntityId o : t.sightedBy)
                if (const sim::Ship* s = w->ship(o); s && s->side == in.side) seen = true;
            if (!seen) continue;
            ImVec2 a = toScreen(t.pos);
            Vec2 ahead = t.pos + bearingVector(t.heading) * (t.speed * 120.0);
            dashedLine(dl, a, toScreen(ahead), IM_COL32(255, 90, 70, 220), 5.0f, 1.6f);
            dl->AddCircleFilled(a, 3.0f, IM_COL32(255, 90, 70, 255));
        }
    }

    // Debrief plot.
    if (in.plot)
        for (const auto& tr : *in.plot) {
            std::vector<ImVec2> pts;
            for (auto& p : tr.points) pts.push_back(toScreen(p));
            if (pts.size() >= 2) dl->AddPolyline(pts.data(), int(pts.size()), toU32(tr.color, 0.85f), ImDrawFlags_None, 1.6f);
            if (!pts.empty()) {
                ImVec2 e = pts.back();
                if (tr.sunk) {
                    dl->AddLine({e.x - 5, e.y - 5}, {e.x + 5, e.y + 5}, toU32(tr.color), 2.0f);
                    dl->AddLine({e.x - 5, e.y + 5}, {e.x + 5, e.y - 5}, toU32(tr.color), 2.0f);
                }
                if (!in.compact && mpp < 120) dl->AddText({e.x + 6, e.y - 6}, toU32(tr.color, 0.9f), tr.label.c_str());
            }
        }

    if (w) {
        // ---- Contacts of the side's picture
        if (in.side != sim::Side::Neutral) {
            for (const auto& t : w->picture(in.side).tracks()) {
                if (!sideKnows(t, *w, in.side)) continue;
                Vec2 pos = t.lost ? t.pos : t.predict(now);
                ImVec2 c = toScreen(pos);
                ImU32 colr = t.lost ? IM_COL32(150, 150, 150, 200) : t.hostile ? toU32(col::kAxis) : toU32(col::kUnknown);
                float sigma = float(t.posSigma / mpp);
                if (!t.lost && sigma > 5.0f) dl->AddCircle(c, sigma, (colr & 0x00ffffff) | 0x50000000, 32);
                if (!t.lost && length(t.vel) > 0.5) dl->AddLine(c, toScreen(pos + t.vel * 180.0), colr, 1.5f);
                drawContactSymbol(dl, c, 7.0f, t.hostile, t.lost, colr);
                if (now - t.lastFlash < 6.0) {
                    float k = float(1.0 - (now - t.lastFlash) / 6.0);
                    dl->AddCircle(c, 11.0f + 6.0f * (1.0f - k), IM_COL32(255, 220, 120, int(220 * k)), 16, 2.0f);
                }
                if (t.id == in.selectedTrack) dl->AddRect({c.x - 12, c.y - 12}, {c.x + 12, c.y + 12}, toU32(col::kBrass), 0.0f, 0, 2.0f);
                if (!in.compact && mpp < 150) {
                    char b[64];
                    trackLabel(t, b, sizeof(b));
                    dl->AddText({c.x + 10, c.y - 7}, (colr & 0x00ffffff) | 0xd0000000, b);
                }
                picks.push_back({c, sim::kNoEntity, t.id, -1});
            }
        }
        // ---- Ships (own side, or all ships in the umpire view)
        for (const auto& sp : w->ships()) {
            const sim::Ship& s = *sp;
            if (in.side != sim::Side::Neutral && s.side != in.side) continue;
            if (s.dmg.sunk) continue;
            ImVec2 c = toScreen(s.pos);
            // Perspective view: own forces always blue (friendly), contacts red/yellow.
            ImVec4 sc = in.side == sim::Side::Neutral ? sideColor(s.side) : col::kAllies;
            if (s.dmg.sinking) sc = {0.5f, 0.5f, 0.5f, 1.0f};
            bool sel = s.id == in.selectedShip;
            bool inGroup = in.selectedGroup >= 0 && s.groupId == in.selectedGroup;
            float sz = s.cls && (s.cls->type == sim::ShipType::DD || s.cls->type == sim::ShipType::DE) ? 5.5f : 7.5f;
            if (s.speed > 0.5) dl->AddLine(c, toScreen(s.pos + s.velocity() * 180.0), toU32(sc, 0.8f), 1.2f);
            drawShipSymbol(dl, c, sz, float(s.heading * kDegToRad), toU32(sc), true);
            if (sel) dl->AddCircle(c, 14.0f, toU32(col::kBrass), 24, 2.0f);
            else if (inGroup && !in.compact) dl->AddCircle(c, 9.0f, toU32(col::kBrassDim, 0.35f), 16, 1.0f);
            if (!s.dmg.fires.empty()) dl->AddCircleFilled({c.x + 8, c.y - 8}, 3.0f, IM_COL32(255, 120, 40, 230));
            if (s.orders.searchlight && s.operational()) {
                double brg = s.heading;
                if (const sim::Ship* t = w->ship(s.searchlightTarget)) brg = bearingTo(s.pos, t->pos);
                dashedLine(dl, c, toScreen(s.pos + bearingVector(brg) * 4000.0), IM_COL32(255, 250, 210, 140), 4.0f);
            }
            if (!in.compact && (mpp < 60 || sel)) dl->AddText({c.x + 10, c.y + 2}, toU32(sc, 0.9f), s.name.c_str());
            picks.push_back({c, s.id, 0, -1});
        }
        drawAir(dl, in, picks);
        // ---- Selected ship overlays: weapon ranges, planned course
        if (const sim::Ship* s = w->ship(in.selectedShip); s && s->alive()) {
            ImVec2 c = toScreen(s->pos);
            if (in.gunRange > 0) {
                dashedCircle(dl, c, float(in.gunRange / mpp), toU32(col::kBrass, 0.55f), 96);
                if (!in.compact) dl->AddText({c.x + 4, c.y - float(in.gunRange / mpp) - 16}, toU32(col::kBrass, 0.7f), tr("map.gun_range"));
            }
            if (in.torpedoRange > 0) {
                dashedCircle(dl, c, float(in.torpedoRange / mpp), IM_COL32(120, 200, 255, 120), 72);
                if (!in.compact)
                    dl->AddText({c.x + 4, c.y + float(in.torpedoRange / mpp) + 2}, IM_COL32(120, 200, 255, 170), tr("map.torpedo_range"));
            }
            for (size_t i = 0; i + 1 < in.plannedCourse.size(); ++i) {
                dashedLine(dl, toScreen(in.plannedCourse[i]), toScreen(in.plannedCourse[i + 1]), toU32(col::kBrass, 0.8f), 6.0f, 1.5f);
                if (in.plannedIsRoute) {
                    ImVec2 v = toScreen(in.plannedCourse[i + 1]);
                    dl->AddCircle(v, 5.0f, toU32(col::kBrass, 0.9f), 12, 1.5f);
                    if (!in.compact) {
                        char b[24];
                        std::snprintf(b, sizeof(b), "%zu", i + 1);
                        dl->AddText({v.x + 7, v.y - 7}, toU32(col::kBrass, 0.8f), b);
                    }
                }
            }
        }
    }

    // ---- Editor markers
    if (in.markers)
        for (const auto& m : *in.markers) {
            ImVec2 c = toScreen(m.pos);
            for (size_t i = 0; i < m.route.size(); ++i) {
                ImVec2 a = toScreen(i == 0 ? m.pos : m.route[i - 1]), b = toScreen(m.route[i]);
                dashedLine(dl, a, b, toU32(m.color, 0.6f), 5.0f);
                dl->AddCircle(b, 4.0f, toU32(m.color, 0.7f));
            }
            drawShipSymbol(dl, c, 9.0f, float(m.heading * kDegToRad), toU32(m.color), true);
            dl->AddLine(c, {c.x + float(std::sin(m.heading * kDegToRad)) * 30.0f, c.y - float(std::cos(m.heading * kDegToRad)) * 30.0f},
                        toU32(m.color, 0.8f), 1.5f);
            if (m.selected) dl->AddCircle(c, 16.0f, toU32(col::kBrass), 24, 2.0f);
            dl->AddText({c.x + 12, c.y - 6}, toU32(m.color), m.label.c_str());
            picks.push_back({c, sim::kNoEntity, 0, m.id});
        }

    // ---- Interaction
    ImVec2 mouse = io.MousePos;
    out.hovered = hovered;
    out.mouseWorld = toWorld(mouse);
    auto nearest = [&]() -> const Pick* {
        const Pick* best = nullptr;
        float bd = kPickRadius * kPickRadius;
        for (const auto& p : picks) {
            float dx = p.s.x - mouse.x, dy = p.s.y - mouse.y, d = dx * dx + dy * dy;
            // Own ships win ties over contacts drawn at the same spot.
            if (p.ship != sim::kNoEntity) d *= 0.8f;
            if (d < bd) {
                bd = d;
                best = &p;
            }
        }
        return best;
    };
    if (hovered && io.MouseWheel != 0.0f) {
        Vec2 before = toWorld(mouse);
        mpp = clamp(mpp * std::pow(0.85, double(io.MouseWheel)), 1.5, 2000.0);
        Vec2 after = toWorld(mouse);
        center += before - after;
    }
    if (ImGui::IsItemActivated()) {
        pressPos_ = mouse;
        if (io.KeyCtrl && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            measuring_ = true;
            measureStart_ = toWorld(mouse);
        } else if (const Pick* p = nearest(); p && p->marker >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            dragMarker_ = p->marker;
        }
    }
    if (ImGui::IsItemActive()) {
        ImVec2 d = io.MouseDelta;
        bool drag = std::abs(mouse.x - pressPos_.x) + std::abs(mouse.y - pressPos_.y) > 4.0f;
        if (measuring_) {
        } else if (dragMarker_ >= 0 && drag) {
            out.draggedMarker = dragMarker_;
            out.dragPos = toWorld(mouse);
        } else if ((ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseDown(ImGuiMouseButton_Middle)) && drag && dragMarker_ < 0) {
            panning_ = true;
            center.x -= d.x * mpp;
            center.y += d.y * mpp;
        }
    }
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !panning_ && !measuring_) {
        bool wasDrag = std::abs(mouse.x - pressPos_.x) + std::abs(mouse.y - pressPos_.y) > 4.0f;
        if (!wasDrag) {
            if (const Pick* p = in.targeting ? nullptr : nearest()) {
                out.clickedShip = p->ship;
                out.clickedTrack = p->track;
                out.clickedMarker = p->marker;
                out.clickedFlight = p->flight;
                out.clickedAirTrack = p->airTrack;
            } else {
                out.clickedEmpty = true;
            }
        }
    }
    if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) out.doubleClicked = true;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
        panning_ = false;
        measuring_ = false;
        dragMarker_ = -1;
    }
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        out.rightClicked = true;
        out.rightShift = io.KeyShift;
        out.rightClickPos = toWorld(mouse);
        if (const Pick* p = nearest()) {
            out.rightShip = p->ship;
            out.rightTrack = p->track;
            out.rightAirTrack = p->airTrack;
            out.rightFlight = p->flight;
        }
    }
    if (hovered && in.targeting) {
        dl->AddCircle(mouse, 9.0f, toU32(col::kBrass), 20, 1.5f);
        dl->AddLine({mouse.x - 14, mouse.y}, {mouse.x + 14, mouse.y}, toU32(col::kBrass), 1.0f);
        dl->AddLine({mouse.x, mouse.y - 14}, {mouse.x, mouse.y + 14}, toU32(col::kBrass), 1.0f);
    }
    if (measuring_) {
        ImVec2 a = toScreen(measureStart_);
        dl->AddLine(a, mouse, toU32(col::kBrass), 1.5f);
        dl->AddCircle(a, 3.0f, toU32(col::kBrass));
        Vec2 wm = toWorld(mouse);
        std::string txt = fmtRange(distance(measureStart_, wm), in.metric) + "  " + fmtBearing(bearingTo(measureStart_, wm));
        dl->AddText({mouse.x + 12, mouse.y - 18}, toU32(col::kBrass), txt.c_str());
    }
    // Hover tooltip.
    if (hovered && !ImGui::IsItemActive() && !in.compact)
        if (const Pick* p = nearest(); p && w) {
            ImGui::BeginTooltip();
            if (const sim::Ship* s = w->ship(p->ship)) {
                ImGui::TextColored(in.side == sim::Side::Neutral ? sideColor(s->side) : col::kAllies, "%s", s->name.c_str());
                if (s->cls) ImGui::TextDisabled("%s %s", s->cls->name.c_str(), shipTypeName(s->cls->type));
                ImGui::Text("%s %s  %s", tr("ui.course"), fmtBearing(s->heading).c_str(), fmtKnots(s->speedKn()).c_str());
            } else if (p->track) {
                if (const sim::Track* t = w->picture(in.side).find(p->track)) {
                    char b[64];
                    ImGui::TextColored(t->hostile ? col::kAxis : col::kUnknown, "%s", trackLabel(*t, b, sizeof(b)));
                    if (t->level >= sim::ClassLevel::ClassKnown && !t->classGuess.empty())
                        if (const sim::ShipClassDef* c = w->db().shipClass(t->classGuess))
                            ImGui::TextDisabled("%s: %s", tr("ui.contact.class_guess"), c->name.c_str());
                    ImGui::Text("%s %s  ~%s", tr("ui.course"), fmtBearing(t->courseEstimate()).c_str(), fmtKnots(t->speedEstimateKn()).c_str());
                    if (const sim::Ship* own = w->ship(in.selectedShip)) {
                        Vec2 tp = t->predict(now);
                        ImGui::Text("%s %s  %s", tr("ui.range"), fmtRange(distance(own->pos, tp), in.metric).c_str(),
                                    fmtBearing(bearingTo(own->pos, tp)).c_str());
                    }
                    if (t->lost) ImGui::TextDisabled("%s %s", tr("ui.contact.lost_since"), fmtDuration(t->age(now)).c_str());
                }
            } else if (const sim::Flight* f = w->flight(p->flight)) {
                ImGui::TextColored(in.side == sim::Side::Neutral ? sideColor(f->side) : col::kAllies, "%s", locName(f->name).c_str());
                ImGui::TextDisabled("%d x %s  |  %s", f->alive(*w), f->def->name.c_str(), tr(sim::flightStateKey(f->state)));
                ImGui::Text("%s  %s %s  %.0f m", tr(sim::airOrderKey(f->order.type)), tr("ui.course"), fmtBearing(f->heading).c_str(), f->altitude);
            } else if (const sim::AirTrack* a = w->picture(in.side).findAir(p->airTrack)) {
                ImGui::TextColored(col::kAxis, "A%u  %s", a->id, a->roleKnown ? tr(sim::airRoleKey(a->role)) : tr("air.unknown_type"));
                if (a->count > 0) ImGui::Text("%s: ~%d", tr("air.count"), a->count);
                if (a->altitude >= 0) ImGui::Text("%s: ~%.0f m", tr("air.altitude"), a->altitude);
                ImGui::Text("%s %s  ~%s", tr("ui.course"), fmtBearing(bearingOf(a->vel)).c_str(), fmtKnots(length(a->vel) / 0.514444).c_str());
                if (const sim::Ship* own = w->ship(in.selectedShip)) {
                    Vec2 ap = a->predict(now);
                    ImGui::Text("%s %s  %s", tr("ui.range"), fmtRange(distance(own->pos, ap), in.metric).c_str(), fmtBearing(bearingTo(own->pos, ap)).c_str());
                }
                if (a->attacking) ImGui::TextColored(col::kBad, "%s", tr("air.attacking"));
            }
            ImGui::EndTooltip();
        }
    drawScale(dl, in);
    // Cursor read-out.
    if (hovered && !in.compact) {
        char b[96];
        if (in.terrain && !in.terrain->empty()) {
            double lat, lon;
            in.terrain->toGeo(out.mouseWorld, lat, lon);
            std::snprintf(b, sizeof(b), "%.3f\xC2\xB0%c  %.3f\xC2\xB0%c", std::abs(lat), lat < 0 ? 'S' : 'N', std::abs(lon), lon < 0 ? 'W' : 'E');
        } else {
            std::snprintf(b, sizeof(b), "%.1f / %.1f km", out.mouseWorld.x / 1000.0, out.mouseWorld.y / 1000.0);
        }
        ImVec2 ts = ImGui::CalcTextSize(b);
        dl->AddText({canvasMax_.x - ts.x - 10, canvasMax_.y - ts.y - 6}, toU32(col::kTextDim), b);
    }
    dl->AddRect(canvasMin_, canvasMax_, IM_COL32(60, 75, 85, 255));
    dl->PopClipRect();
    ImGui::PopID();
    return out;
}

}  // namespace it::ui
