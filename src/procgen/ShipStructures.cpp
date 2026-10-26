// Superstructure: deckhouses with doors, portholes and ladders; navigating bridges and IJN pagodas
// with window bands, wings and splinter shields; funnels with lips, caps and steam pipes; masts with
// platforms, yards and signal lamps; fire-control directors, radar antennas, searchlights and cranes.
#include "procgen/ShipModelInternal.h"

namespace it::procgen::detail {
namespace {

// Ring of a lofted section: an outline in the x/y plane at height z.
std::vector<Vec3f> ring(const std::vector<Vec2f>& outline, float z, Vec2f offset = {}, float scale = 1.0f) {
    std::vector<Vec3f> r;
    Vec2f c{};
    for (auto& p : outline) c += p;
    c = c / float(outline.size());
    for (auto& p : outline) {
        Vec2f q = c + (p - c) * scale + offset;
        r.push_back({q.x, q.y, z});
    }
    return r;
}

// Outward normal of an outline edge (counter-clockwise outline seen from above: x fwd, y stbd).
Vec2f edgeNormal(Vec2f a, Vec2f b, Vec2f centre) {
    Vec2f d = b - a;
    Vec2f n = normalize(Vec2f{d.y, -d.x});
    if (dot(n, (a + b) * 0.5f - centre) < 0) n = -n;
    return n;
}

Vec2f centroid(const std::vector<Vec2f>& o) {
    Vec2f c{};
    for (auto& p : o) c += p;
    return c / float(o.size());
}

/// Structure walls without a cap, then a roof in the deck material.
void house(Ctx& cx, MeshBuilder& mb, const std::vector<Vec2f>& outline, float z0, float z1, const Material& wall) {
    mb.setMaterial(wall);
    mb.loft(ring(outline, z0), ring(outline, z1), false, false);
    Material roof = cx.paint.deck;
    if (roof.id == kDeck) roof.id = kDeckSteel;  // superstructure decks were steel, not planked
    mb.setMaterial(roof);
    mb.polygon(ring(outline, z1));
    cx.addRoof(outline, z1);
}

/// Window band along an outline: dark panes between frames, from z0 to z1, on edges whose outward
/// normal faces forward of `minFacing` (cos of the angle to +x); frames every `pane` metres.
void windowBand(Ctx& cx, const std::vector<Vec2f>& outline, float z0, float z1, float minFacing, float pane, float inset = -0.03f) {
    MeshBuilder gb = cx.at(0);
    MeshBuilder fb = cx.at(1);
    Vec2f c = centroid(outline);
    for (size_t i = 0; i < outline.size(); ++i) {
        Vec2f a = outline[i], b = outline[(i + 1) % outline.size()];
        Vec2f n = edgeNormal(a, b, c);
        if (n.x < minFacing) continue;
        float len = length(b - a);
        if (len < 0.25f) continue;
        Vec2f o = n * -inset;
        gb.setMaterial(cx.paint.glass);
        gb.flatQuad(v3(a.x + o.x, a.y + o.y, z0), v3(b.x + o.x, b.y + o.y, z0), v3(b.x + o.x, b.y + o.y, z1), v3(a.x + o.x, a.y + o.y, z1));
        // Mullions and the sill/lintel.
        fb.setMaterial(cx.paint.superstructure);
        int panes = std::max(1, int(len / pane + 0.5f));
        for (int k = 0; k <= panes; ++k) {
            Vec2f p = a + (b - a) * (float(k) / panes) + n * 0.03f;
            fb.box(v3(p.x - 0.05, p.y - 0.05, z0), v3(p.x + 0.05, p.y + 0.05, z1));
        }
        Vec2f a2 = a + n * 0.05f, b2 = b + n * 0.05f;
        fb.bar(v3(a2.x, a2.y, z1 + 0.04), v3(b2.x, b2.y, z1 + 0.04), 0.12f, 0.08f);
        // Visor (eyebrow) above the windows.
        Vec2f a3 = a + n * 0.32f, b3 = b + n * 0.32f;
        fb.flatQuad(v3(a.x, a.y, z1 + 0.12), v3(b.x, b.y, z1 + 0.12), v3(b3.x, b3.y, z1 + 0.06), v3(a3.x, a3.y, z1 + 0.06));
    }
}

/// Portholes along the long sides of an outline (close-ups).
void portholes(Ctx& cx, const std::vector<Vec2f>& outline, float z, float spacing) {
    MeshBuilder fb = cx.at(2);
    Vec2f c = centroid(outline);
    for (size_t i = 0; i < outline.size(); ++i) {
        Vec2f a = outline[i], b = outline[(i + 1) % outline.size()];
        float len = length(b - a);
        if (len < spacing * 1.5f) continue;
        Vec2f n = edgeNormal(a, b, c);
        int count = int(len / spacing);
        for (int k = 1; k < count; ++k) {
            Vec2f p = a + (b - a) * (float(k) / count) + n * 0.02f;
            fb.setMaterial(cx.paint.dark);
            fb.disc(v3(p.x, p.y, z), v3(n.x, n.y, 0), 0.17f, 10);
            fb.setMaterial(cx.paint.brass);
            fb.torus(v3(p.x, p.y, z), v3(n.x, n.y, 0), 0.19f, 0.03f, 10, 4);
        }
    }
}

/// Watertight door with dogs on a wall (centre on the wall, outward normal n).
void door(Ctx& cx, Vec3f c, Vec3f n, float w = 0.75f, float h = 1.75f) {
    MeshBuilder fb = cx.at(1);
    Vec3f s = normalize(cross(Vec3f{0, 0, 1}, n));
    fb.setMaterial(cx.paint.superstructure);
    fb.orientedBox(c + n * 0.04f, {0.04f, w * 0.5f, h * 0.5f}, n);
    MeshBuilder kb = cx.at(2);
    kb.setMaterial(cx.paint.metal);
    for (int k = 0; k < 3; ++k) {
        float zz = (float(k) - 1.0f) * h * 0.35f;
        kb.orientedBox(c + n * 0.1f + s * (w * 0.42f) + Vec3f{0, 0, zz}, {0.03f, 0.06f, 0.03f}, n);
    }
    // Coaming at the foot.
    fb.orientedBox(c + n * 0.06f - Vec3f{0, 0, h * 0.5f + 0.12f}, {0.06f, w * 0.55f, 0.12f}, n);
}

/// Splinter shield along a polyline (thin plate, rolled top edge).
void splinterShield(Ctx& cx, const std::vector<Vec3f>& pts, float h, bool closed) {
    MeshBuilder mb = cx.at(0);
    mb.setMaterial(cx.paint.superstructure);
    size_t n = pts.size();
    size_t segs = closed ? n : n - 1;
    for (size_t i = 0; i < segs; ++i) {
        Vec3f a = pts[i], b = pts[(i + 1) % n];
        mb.flatQuad(a, b, b + Vec3f{0, 0, h}, a + Vec3f{0, 0, h});
    }
    MeshBuilder fb = cx.at(1);
    fb.setMaterial(cx.paint.superstructure);
    std::vector<Vec3f> top;
    for (auto& p : pts) top.push_back(p + Vec3f{0, 0, h});
    fb.polyline(top, 0.04f, 4, closed);
}

/// Runs of consecutive outline edges (closed loop) whose midpoints pass `keep`.
template <typename Pred> std::vector<std::vector<Vec3f>> openRuns(const std::vector<Vec3f>& loop, Pred keep) {
    std::vector<std::vector<Vec3f>> runs;
    size_t n = loop.size();
    auto kept = [&](size_t i) { return keep((loop[i % n] + loop[(i + 1) % n]) * 0.5f); };
    size_t start = 0;
    while (start < n && kept((start + n - 1) % n)) ++start;
    if (start == n) {
        runs.push_back(loop);
        runs.back().push_back(loop.front());
        return runs;
    }
    std::vector<Vec3f> cur;
    for (size_t k = 0; k < n; ++k) {
        size_t i = (start + k) % n;
        if (kept(i)) {
            if (cur.empty()) cur.push_back(loop[i]);
            cur.push_back(loop[(i + 1) % n]);
        } else if (!cur.empty()) {
            runs.push_back(cur);
            cur.clear();
        }
    }
    if (!cur.empty()) runs.push_back(cur);
    return runs;
}

/// Outline points lifted to height z, offset outward by `out` metres.
std::vector<Vec3f> lifted(const std::vector<Vec2f>& o, float z, float out = 0.0f) {
    Vec2f c = centroid(o);
    std::vector<Vec3f> r;
    for (auto& p : o) {
        Vec2f d = p - c;
        float l = length(d);
        Vec2f q = l > 1e-3f ? p + d / l * out : p;
        r.push_back({q.x, q.y, z});
    }
    return r;
}

/// Thin platform (deck plate) with brackets underneath along its rim.
void platform(Ctx& cx, const std::vector<Vec2f>& outline, float z, float thick, bool brackets) {
    MeshBuilder mb = cx.at(0);
    Material deck = cx.paint.deck;
    if (deck.id == kDeck) deck.id = kDeckSteel;
    mb.setMaterial(cx.paint.superstructure);
    mb.loft(ring(outline, z - thick), ring(outline, z), false, false);
    mb.setMaterial(deck);
    mb.polygon(ring(outline, z));
    cx.addRoof(outline, z);
    mb.setMaterial(cx.paint.superstructure);
    std::vector<Vec3f> bottom = ring(outline, z - thick);
    std::reverse(bottom.begin(), bottom.end());
    mb.polygon(bottom);
    if (!brackets) return;
    MeshBuilder fb = cx.at(1);
    fb.setMaterial(cx.paint.superstructure);
    Vec2f c = centroid(outline);
    for (size_t i = 0; i < outline.size(); i += 2) {
        Vec2f p = outline[i];
        Vec2f d = c - p;
        float l = length(d);
        if (l < 1.0f) continue;
        Vec2f q = p + d / l * std::min(1.4f, l * 0.6f);
        fb.flatTri(v3(p.x, p.y, z - thick), v3(q.x, q.y, z - thick), v3(q.x, q.y, z - thick - 1.0f));
    }
}

/// Signal searchlight (12"/24" blinker) on a small pedestal.
void signalLamp(Ctx& cx, Vec3f base, float r, float yaw) {
    MeshBuilder fb = cx.at(1);
    fb.setMaterial(cx.paint.superstructure);
    fb.tube(base, base + Vec3f{0, 0, 0.6f}, 0.06f, 0.06f, 6, false);
    Vec3f c = base + Vec3f{0, 0, 0.6f + r};
    Vec3f d{std::cos(yaw), std::sin(yaw), 0};
    fb.tube(c - d * (r * 1.1f), c + d * (r * 0.8f), r, r, 10, true);
    fb.setMaterial(cx.paint.lens);
    fb.disc(c + d * (r * 0.82f), d, r * 0.85f, 10);
}

void mushroomVent(MeshBuilder& mb, const PaintScheme& p, Vec3f base, float r) {
    mb.setMaterial(p.superstructure);
    mb.lathe(base, {0, 0, 1}, {{r, 0}, {r, 0.55f}, {r * 1.7f, 0.6f}, {r * 1.6f, 0.75f}, {r * 0.4f, 0.85f}, {0.0f, 0.86f}}, 10, false);
}

}  // namespace

// ---------------------------------------------------------------------------------------- deckhouse
void deckhouse(Ctx& cx, const sim::ModelPart& pt, bool hangar) {
    double xx = cx.x(pt.f), z0 = cx.deck(pt.f) + pt.z;
    float zz = float(z0) - 0.05f;
    int levels = std::max(1, pt.levels);
    MeshBuilder mb = cx.at(0);
    cx.take(xx - pt.len * 0.5, xx + pt.len * 0.5, pt.y - pt.wid * 0.5, pt.y + pt.wid * 0.5);
    for (int l = 0; l < levels; ++l) {
        float s = 1.0f - 0.1f * l;
        float ll = float(pt.len) * s, ww = float(pt.wid) * (1.0f - 0.12f * l);
        float h = float(pt.h);
        // Keep the house inside the deck edge.
        float hw = float(std::min(cx.edgeY(xx + ll * 0.5) , cx.edgeY(xx - ll * 0.5)));
        ww = std::min(ww, std::max(1.0f, (hw - float(std::fabs(pt.y))) * 2.0f - 0.6f));
        float rad = hangar ? 0.3f : std::min(1.2f, ww * 0.18f);
        auto outline = roundedRect(float(xx), float(pt.y), ll, ww, rad, 3);
        house(cx, mb, outline, zz, zz + h, cx.paint.superstructure);
        if (cx.opt.details) {
            if (!hangar) portholes(cx, outline, zz + h * 0.62f, 1.9f);
            // Doors on both sides near the after end and one on the after face.
            for (int side : {1, -1})
                door(cx, v3(xx - ll * 0.32, pt.y + side * ww * 0.5, zz + 0.95), v3(0, side, 0));
            if (!hangar) door(cx, v3(xx - ll * 0.5, pt.y + ww * 0.18, zz + 0.95), v3(-1, 0, 0));
            // Ventilation louvres.
            MeshBuilder kb = cx.at(2);
            kb.setMaterial(cx.paint.superstructure);
            for (int side : {1, -1})
                for (float fx : {0.12f, 0.3f}) kb.box(v3(xx + ll * fx - 0.45, pt.y + side * ww * 0.5 - 0.12, zz + h * 0.55), v3(xx + ll * fx + 0.45, pt.y + side * ww * 0.5 + 0.12, zz + h * 0.85));
            // Railing round the roof edge (top level) or the exposed ledge.
            MeshBuilder rb = cx.at(2);
            auto edge = roundedRect(float(xx), float(pt.y), ll - 0.3f, ww - 0.3f, rad, 3);
            railing(rb, cx.paint, lifted(edge, zz + h), 1.0f, 2, true);
            // Ladder from the deck (or the level below) on the after face.
            MeshBuilder lb = cx.at(1);
            float ly = float(pt.y) + ((l & 1) ? -1.0f : 1.0f) * ww * 0.3f;
            ladder(lb, cx.paint, v3(xx - ll * 0.5 - 1.6, ly, zz), v3(xx - ll * 0.5 - 0.05, ly, zz + h), 0.6f);
            // Carley floats against the side walls (USN/RN).
            if (!cx.ijn && ll > 6.0f && !hangar) {
                for (int side : {1, -1}) {
                    MeshBuilder fb = cx.at(1);
                    float fx = float(xx) + ll * 0.05f;
                    // Stowed on edge against the wall, leaning in.
                    fb.setTransform(frame(v3(fx, pt.y + side * (ww * 0.5 + 0.3), zz + 0.85), side > 0 ? 0.0f : float(kPi), 0.0f, 1.75f));
                    carleyFloat(fb, cx.paint, 2.6f, 1.3f);
                    fb.clearTransform();
                }
            }
            // Roof ventilators.
            MeshBuilder vb = cx.at(1);
            if (l == levels - 1 && ll > 5.0f) {
                mushroomVent(vb, cx.paint, v3(xx - ll * 0.3, pt.y + ww * 0.25, zz + h), 0.22f);
                mushroomVent(vb, cx.paint, v3(xx + ll * 0.2, pt.y - ww * 0.25, zz + h), 0.18f);
            }
        }
        if (hangar && cx.opt.details) {
            // Rolling door on the after face with horizontal stiffeners.
            MeshBuilder fb = cx.at(1);
            fb.setMaterial(cx.paint.superstructure);
            float dx = float(xx) - ll * 0.5f - 0.06f;
            fb.box(v3(dx - 0.04, pt.y - ww * 0.42, zz + 0.05), v3(dx + 0.06, pt.y + ww * 0.42, zz + h - 0.4));
            for (int k = 1; k < 6; ++k) {
                float hz = zz + (h - 0.4f) * float(k) / 6.0f;
                fb.box(v3(dx - 0.12, pt.y - ww * 0.42, hz - 0.05), v3(dx, pt.y + ww * 0.42, hz + 0.05));
            }
            // Vertical stiffeners on the side walls.
            for (int side : {1, -1})
                for (int k = 0; k < 7; ++k) {
                    float sx = float(xx) - ll * 0.42f + ll * 0.84f * float(k) / 6.0f;
                    fb.box(v3(sx - 0.06, pt.y + side * ww * 0.5 - 0.08, zz), v3(sx + 0.06, pt.y + side * ww * 0.5 + 0.08, zz + h));
                }
        }
        zz += h;
    }
}

// ------------------------------------------------------------------------------------------- bridge
void bridge(Ctx& cx, const sim::ModelPart& pt) {
    double xx = cx.x(pt.f), z0 = cx.deck(pt.f) + pt.z;
    int levels = std::max(1, pt.levels);
    float zz = float(z0) - 0.05f;
    float h = float(pt.h);
    float len = float(pt.len), wid = float(pt.wid);
    MeshBuilder mb = cx.at(0);
    const std::string& st = pt.style;
    bool dd = st == "usn_dd" || st == "ijn_dd";
    bool ijnStyle = st == "ijn_dd" || st == "ijn_old";
    cx.take(xx - len * 0.5, xx + len * 0.5, -wid * 0.5, wid * 0.5);
    float edge = float(cx.edgeY(xx));
    // Lower levels: houses that step back aft and narrow slightly.
    for (int l = 0; l < levels - 1; ++l) {
        float s = 1.0f - 0.08f * l;
        float ll = len * s, ww = std::min(wid * (1.0f - 0.06f * l), edge * 2.0f - 0.8f);
        float cxl = float(xx) - len * 0.04f * l;
        std::vector<Vec2f> o;
        if (dd || st == "usn") o = gunhouseOutline(cxl, 0, ll, ww, ww * 0.55f, ww * 0.35f, 0.6f);
        else o = roundedRect(cxl, 0, ll, ww, 0.6f, 3);
        house(cx, mb, o, zz, zz + h, cx.paint.superstructure);
        if (cx.opt.details) {
            portholes(cx, o, zz + h * 0.62f, 1.7f);
            door(cx, v3(cxl - ll * 0.3f, ww * 0.5f, zz + 0.95f), {0, 1, 0});
            door(cx, v3(cxl - ll * 0.3f, -ww * 0.5f, zz + 0.95f), {0, -1, 0});
            MeshBuilder lb = cx.at(1);
            float ly = (l & 1) ? -ww * 0.32f : ww * 0.32f;
            ladder(lb, cx.paint, v3(cxl - ll * 0.5f - 1.6f, ly, zz), v3(cxl - ll * 0.5f - 0.05f, ly, zz + h), 0.6f);
        }
        zz += h;
    }
    // Navigating bridge (pilothouse) with its window band.
    float pl = len * (dd ? 0.7f : 0.62f), pw = std::min(wid * 0.82f, edge * 2.0f - 1.0f);
    float px = float(xx) + len * 0.5f - pl * 0.5f - len * 0.04f * float(levels - 1);
    std::vector<Vec2f> pilot;
    if (st == "usn_dd" || st == "ijn_dd") {
        // Rounded front.
        for (int k = 0; k <= 10; ++k) {
            float a = -float(kPi) * 0.5f + float(kPi) * float(k) / 10.0f;
            pilot.push_back({px + pl * 0.5f - pw * 0.45f + std::cos(a) * pw * 0.45f, std::sin(a) * pw * 0.5f});
        }
        pilot.push_back({px - pl * 0.5f, pw * 0.5f});
        pilot.push_back({px - pl * 0.5f, -pw * 0.5f});
    } else if (st == "merchant") {
        pilot = roundedRect(px, 0, pl, pw, 0.3f, 2);
    } else {
        pilot = gunhouseOutline(px, 0, pl, pw, pw * 0.7f, pw * 0.2f, 0.4f);
    }
    house(cx, mb, pilot, zz, zz + h, cx.paint.superstructure);
    windowBand(cx, pilot, zz + h * 0.5f, zz + h * 0.85f, -0.2f, 0.9f);
    float top = zz + h;
    // Bridge wings out to the ship's side, on brackets.
    float wingZ = zz + 0.05f;
    if (st != "ijn_old") {
        float wy = std::max(pw * 0.5f + 0.6f, edge - 0.3f);
        float wx0 = px + pl * 0.15f, wx1 = px + pl * 0.45f;
        if (st == "merchant") wx0 = px - pl * 0.4f;
        for (int side : {1, -1}) {
            std::vector<Vec2f> wing = {{wx0, side * pw * 0.45f}, {wx1, side * pw * 0.45f}, {wx1, side * wy}, {wx0, side * wy}};
            if (side < 0) std::reverse(wing.begin(), wing.end());
            platform(cx, wing, wingZ, 0.2f, true);
            std::vector<Vec3f> shield = {v3(wx1, side * pw * 0.5f, wingZ), v3(wx1, side * wy, wingZ), v3(wx0, side * wy, wingZ)};
            splinterShield(cx, shield, 1.15f, false);
            if (cx.opt.details) signalLamp(cx, v3(wx1 - 0.6f, side * (wy - 0.6f), wingZ), 0.28f, side * 0.6f);
        }
    }
    // Open bridge on the pilothouse roof, protected by a splinter shield (USN) or canvas dodgers.
    if (st == "usn" || st == "usn_dd" || st == "rn" || st == "merchant") {
        // The shield wraps the front and sides; the after end stays open.
        auto runs = openRuns(lifted(pilot, top), [&](const Vec3f& m) { return m.x > px - pl * 0.3f; });
        for (auto& sh : runs) {
            splinterShield(cx, sh, st == "rn" ? 1.35f : 1.2f, false);
            if (st == "rn") {
                // Glass windscreen above the shield.
                MeshBuilder gb = cx.at(1);
                gb.setMaterial(cx.paint.glass);
                for (size_t i = 0; i + 1 < sh.size(); ++i)
                    gb.flatQuad(sh[i] + Vec3f{0, 0, 1.35f}, sh[i + 1] + Vec3f{0, 0, 1.35f}, sh[i + 1] + Vec3f{-0.15f, 0, 1.75f},
                                sh[i] + Vec3f{-0.15f, 0, 1.75f});
            }
        }
        if (cx.opt.details) {
            // Binocular stands and the gyro repeater pelorus.
            MeshBuilder kb = cx.at(2);
            kb.setMaterial(cx.paint.superstructure);
            for (int side : {1, -1}) {
                Vec3f b = v3(px + pl * 0.3f, side * pw * 0.3f, top);
                kb.tube(b, b + Vec3f{0, 0, 1.1f}, 0.06f, 0.06f, 6, false);
                kb.setMaterial(cx.paint.black);
                kb.box(b + Vec3f{-0.05f, -0.15f, 1.1f}, b + Vec3f{0.25f, 0.15f, 1.25f});
                kb.setMaterial(cx.paint.superstructure);
            }
            kb.lathe(v3(px + pl * 0.15f, 0, top), {0, 0, 1}, {{0.2f, 0}, {0.12f, 0.9f}, {0.25f, 1.0f}, {0.25f, 1.15f}, {0.0f, 1.2f}}, 10, false);
        }
        if (st == "merchant") {
            MeshBuilder rb = cx.at(2);
            railing(rb, cx.paint, lifted(pilot, top, -0.1f), 1.0f, 2, true);
        }
    } else if (ijnStyle) {
        // IJN compass bridge: a second window band under a canvas-covered roof, rangefinder on top.
        float cl = pl * 0.75f, cw = pw * 0.85f;
        float cxp = px - pl * 0.05f;
        std::vector<Vec2f> comp;
        for (int k = 0; k <= 10; ++k) {
            float a = -float(kPi) * 0.5f + float(kPi) * float(k) / 10.0f;
            comp.push_back({cxp + cl * 0.5f - cw * 0.45f + std::cos(a) * cw * 0.45f, std::sin(a) * cw * 0.5f});
        }
        comp.push_back({cxp - cl * 0.5f, cw * 0.5f});
        comp.push_back({cxp - cl * 0.5f, -cw * 0.5f});
        house(cx, mb, comp, top, top + 2.2f, cx.paint.superstructure);
        windowBand(cx, comp, top + 1.0f, top + 1.9f, -0.3f, 0.8f);
        MeshBuilder cb = cx.at(0);
        cb.setMaterial(cx.paint.canvas);
        cb.polygon(lifted(comp, top + 2.25f, 0.15f));
        // Director / rangefinder on the compass bridge roof (trains with the main director).
        int di = cx.part("director_bridge", PartKind::Director, 0);
        MeshPart& dp = cx.P(di);
        dp.sub = 0;
        dp.pivot = v3(cxp - cl * 0.15f, 0, top + 2.25f);
        MeshBuilder db(dp);
        db.setMaterial(cx.paint.superstructure);
        Vec3f b = dp.pivot;
        db.lathe(b, {0, 0, 1}, {{1.0f, 0}, {1.0f, 0.9f}, {0.85f, 1.25f}, {0.4f, 1.45f}, {0.0f, 1.5f}}, 14, false);
        float rf = 3.0f;
        db.tube(b + Vec3f{-0.2f, -rf * 0.5f, 0.75f}, b + Vec3f{-0.2f, rf * 0.5f, 0.75f}, 0.16f, 0.16f, 8, true);
        for (int side : {1, -1}) db.box(b + Vec3f{-0.45f, side * rf * 0.5f - 0.25f, 0.5f}, b + Vec3f{0.05f, side * rf * 0.5f + 0.25f, 1.0f});
    }
    // Lookout and fire-control platform at the after end of the top level.
    if (st == "usn" || st == "rn") {
        float ax = float(xx) - len * 0.3f;
        auto pf = roundedRect(ax, 0, len * 0.35f, pw * 0.7f, 0.5f, 3);
        platform(cx, pf, top + 0.05f, 0.2f, false);
        MeshBuilder rb = cx.at(2);
        railing(rb, cx.paint, lifted(pf, top + 0.05f, -0.1f), 1.0f, 2, true);
    }
}

// ------------------------------------------------------------------------------------------- pagoda
void pagoda(Ctx& cx, const sim::ModelPart& pt) {
    double xx = cx.x(pt.f), z0 = cx.deck(pt.f) + pt.z;
    bool massive = pt.style == "ijn_takao";
    int levels = std::max(2, pt.levels);
    float zz = float(z0) - 0.05f;
    float h = float(pt.h);
    MeshBuilder mb = cx.at(0);
    cx.take(xx - pt.len * 0.5, xx + pt.len * 0.5, -pt.wid * 0.5, pt.wid * 0.5);
    float edge = float(cx.edgeY(xx));
    for (int l = 0; l < levels; ++l) {
        float t = float(l) / float(std::max(1, levels - 1));
        float s = 1.0f - float(pt.taper) * t;
        float ll = float(pt.len) * s * (massive ? 1.0f : 0.9f), ww = std::min(float(pt.wid) * s, edge * 2.0f - 0.8f);
        // IJN bridges step back as they rise.
        float cxl = float(xx) - float(pt.len) * 0.12f * t;
        float hh = h * 0.88f;
        std::vector<Vec2f> o;
        if (massive && l < levels - 2) o = gunhouseOutline(cxl, 0, ll, ww, ww * 0.8f, ww * 0.12f, 1.0f);
        else o = gunhouseOutline(cxl, 0, ll, ww, ww * 0.62f, ww * 0.25f, 0.8f);
        house(cx, mb, o, zz, zz + hh, cx.paint.superstructure);
        // Overhanging platform at each level with railings and brackets.
        auto pf = gunhouseOutline(cxl + ll * 0.04f, 0, ll * 1.12f, std::min(ww * 1.16f + 0.6f, edge * 2.0f + 1.0f), ww * 0.8f, ww * 0.2f, 1.0f);
        platform(cx, pf, zz + h, h - hh, true);
        if (cx.opt.details) {
            MeshBuilder rb = cx.at(2);
            railing(rb, cx.paint, lifted(pf, zz + h, -0.1f), 1.0f, 2, true);
            if (l < levels - 3) portholes(cx, o, zz + hh * 0.6f, 1.6f);
            MeshBuilder lb = cx.at(1);
            float ly = (l & 1) ? -ww * 0.35f : ww * 0.35f;
            ladder(lb, cx.paint, v3(cxl - ll * 0.5f - 1.4f, ly, zz), v3(cxl - ll * 0.5f - 0.05f, ly, zz + h), 0.55f);
        }
        // Window bands on the navigation and compass bridges at the top.
        if (l >= levels - 2) windowBand(cx, o, zz + hh * 0.42f, zz + hh * 0.85f, -0.35f, 0.75f);
        // Sponsons for the high-angle directors and lookouts on the upper levels of large pagodas.
        if (massive && l == levels - 3) {
            for (int side : {1, -1}) {
                auto sp = ellipseOutline(cxl - ll * 0.1f, side * (ww * 0.5f + 1.2f), 1.6f, 1.4f, 12);
                platform(cx, sp, zz + h, 0.25f, true);
                MeshBuilder hb = cx.at(1);
                hb.setMaterial(cx.paint.superstructure);
                Vec3f c = v3(cxl - ll * 0.1f, side * (ww * 0.5f + 1.2f), zz + h);
                hb.lathe(c, {0, 0, 1}, {{0.9f, 0}, {0.9f, 1.2f}, {0.6f, 1.6f}, {0.0f, 1.65f}}, 12, false);
                hb.tube(c + Vec3f{-0.1f, -1.3f, 1.0f}, c + Vec3f{-0.1f, 1.3f, 1.0f}, 0.12f, 0.12f, 8, true);
            }
        }
        zz += h;
    }
    // 6 m rangefinder tower behind the director on the top level.
    float rx = float(xx) - float(pt.len) * 0.22f;
    float rf = cx.c.type == sim::ShipType::CA ? 6.0f : 4.5f;
    MeshBuilder fb = cx.at(0);
    fb.setMaterial(cx.paint.superstructure);
    Vec3f rb = v3(rx, 0, zz);
    fb.lathe(rb, {0, 0, 1}, {{1.3f, 0}, {1.3f, 1.4f}, {1.1f, 1.8f}, {0.0f, 1.85f}}, 14, false);
    fb.tube(rb + Vec3f{0, -rf * 0.5f, 1.2f}, rb + Vec3f{0, rf * 0.5f, 1.2f}, 0.22f, 0.22f, 10, true);
    for (int side : {1, -1}) fb.box(rb + Vec3f{-0.35f, side * rf * 0.5f - 0.3f, 0.85f}, rb + Vec3f{0.35f, side * rf * 0.5f + 0.3f, 1.55f});
}

// ------------------------------------------------------------------------------------------- funnel
void funnel(Ctx& cx, const sim::ModelPart& pt) {
    double u = pt.f, z0 = cx.deck(u) + pt.z - 0.1;
    double xx = cx.x(u);
    float rx = float(pt.len * 0.5), ry = float(pt.wid * 0.5);
    float H = float(pt.h);
    float rake = float(std::tan(pt.rake * kDegToRad));
    MeshBuilder mb = cx.at(0);
    const int N = 28;
    bool flat = pt.style == "flat";
    bool trunked = pt.style == "trunked" && pt.f2 > 0.0;
    auto section = [&](float cxs, float a, float b) {
        std::vector<Vec2f> o;
        if (flat) {
            o = roundedRect(cxs, 0, a * 2.0f, b * 2.0f, std::min(a, b) * 0.35f, 6);  // 28 points
        } else {
            o = ellipseOutline(cxs, 0, a, b, N);
        }
        return o;
    };
    // Rings from the base to the top: casing apron, the trunked sweep, then a straight raked stack.
    std::vector<std::vector<Vec3f>> rings;
    double x2 = trunked ? cx.x(pt.f2) : xx;
    float baseX = float(0.5 * (xx + x2)), baseRx = float(0.5 * std::fabs(xx - x2)) + rx;
    // Trunked: the uptakes sweep up and aft into one stack whose after edge stays near the base's.
    float stackX = baseX - baseRx + rx - H * 0.9f * rake * 0.25f;
    const int K = trunked ? 10 : 4;
    for (int k = 0; k <= K; ++k) {
        float t = float(k) / K;
        float z = float(z0) + H * 0.9f * t;
        float cxs, a, b;
        if (trunked) {
            cxs = lerp(baseX, stackX, t);
            a = lerp(baseRx, rx, std::sqrt(t));
            b = ry * (1.0f - 0.05f * t);
        } else {
            cxs = float(xx) - H * 0.9f * t * rake;
            a = rx;
            b = ry;
        }
        float apron = k == 0 ? 1.12f : 1.0f;
        rings.push_back(ring(section(cxs, a * apron, b * apron), z));
    }
    mb.setMaterial(cx.paint.superstructure);
    for (size_t k = 0; k + 1 < rings.size(); ++k) mb.loft(rings[k], rings[k + 1], false, false);
    // Cap band (black on IJN funnels), the lip and the dark opening.
    Vec3f topC{};
    for (auto& p : rings.back()) topC += p;
    topC = topC / float(rings.back().size());
    float capH = H * 0.1f;
    auto shift = [&](const std::vector<Vec3f>& r, float dz, float scale) {
        std::vector<Vec3f> o;
        for (auto& p : r) {
            Vec3f d = p - topC;
            o.push_back(topC + Vec3f{d.x * scale - rake * dz, d.y * scale, dz});
        }
        return o;
    };
    auto capTop = shift(rings.back(), capH, 1.0f);
    mb.setMaterial(cx.ijn ? cx.paint.black : cx.paint.superstructure);
    mb.loft(rings.back(), capTop, false, false);
    auto lip = shift(rings.back(), capH, 1.04f);
    auto lipTop = shift(rings.back(), capH + 0.12f, 1.04f);
    mb.setMaterial(cx.ijn ? cx.paint.black : cx.paint.metal);
    mb.loft(lip, lipTop, false, false);
    auto inner = shift(rings.back(), capH + 0.12f, 0.9f);
    mb.loft(lipTop, inner, false, false);
    mb.setMaterial(cx.paint.dark);
    auto deep = shift(rings.back(), capH - 1.2f, 0.9f);
    mb.loft(deep, inner, false, false);
    mb.polygon(deep);
    cx.take(std::min(double(baseX - baseRx), xx - rx) - 0.3, std::max(double(baseX + baseRx), xx + rx) + 0.3, -ry - 0.3, ry + 0.3);
    if (!cx.opt.details) return;
    float topZ = topC.z + capH;
    float topX = topC.x - rake * capH;
    float stackRake = trunked ? (topC.x - (baseX - baseRx + rx)) / (H * 0.9f) * -1.0f : rake;
    // Clinker screen: a grating across the opening.
    MeshBuilder gb = cx.at(2);
    gb.setMaterial(cx.paint.black);
    for (int k = -3; k <= 3; ++k) {
        float yy = ry * 0.85f * float(k) / 3.5f;
        float half = rx * 0.88f * std::sqrt(std::max(0.0f, 1.0f - (yy / (ry * 0.9f)) * (yy / (ry * 0.9f))));
        gb.bar(v3(topX - half, yy, topZ + 0.05f), v3(topX + half, yy, topZ + 0.05f), 0.05f, 0.08f);
    }
    // Steam pipes up the after side (forward side on IJN funnels) and the siren platform.
    MeshBuilder fb = cx.at(1);
    fb.setMaterial(cx.paint.superstructure);
    int pipes = cx.destroyer ? 2 : 3;
    float side = cx.ijn ? 1.0f : -1.0f;
    float stackBaseX = trunked ? baseX - baseRx + rx : float(xx);
    for (int p = 0; p < pipes; ++p) {
        float yy = (float(p) - (pipes - 1) * 0.5f) * 0.55f;
        float baseXp = stackBaseX + side * (rx + 0.25f);
        if (trunked && side > 0) baseXp = baseX + baseRx + 0.25f;
        Vec3f b = v3(topX + side * (rx + 0.25f), yy, topZ - 0.6f);
        Vec3f a = v3(baseXp, yy, z0 + 0.1);
        if (trunked && side > 0) a = lerp(a, b, 0.0f);
        Vec3f c = b + Vec3f{-stackRake * 1.2f, 0, 1.2f + 0.4f * float(p)};
        fb.polyline({a, b, c}, 0.11f - 0.02f * p, 6);
    }
    Vec3f siren = v3(topX + rx * 1.05f + stackRake * H * 0.2f, 0, topZ - H * 0.2f);
    fb.box(siren + Vec3f{-0.1f, -0.6f, -0.05f}, siren + Vec3f{0.6f, 0.6f, 0.08f});
    fb.setMaterial(cx.paint.brass);
    fb.lathe(siren + Vec3f{0.3f, 0, 0.08f}, {0, 0, 1}, {{0.06f, 0}, {0.06f, 0.4f}, {0.14f, 0.55f}, {0.14f, 0.6f}}, 8, false);
    // Ladder rungs up the side.
    MeshBuilder kb = cx.at(2);
    kb.setMaterial(cx.paint.superstructure);
    for (float zr = float(z0) + 1.0f; zr < topZ - 0.3f; zr += 0.35f) {
        float yy = -ry - 0.12f;
        float xr = stackBaseX - (zr - float(z0)) * stackRake;
        kb.bar(v3(xr - 0.25f, yy, zr), v3(xr + 0.25f, yy, zr), 0.04f, 0.04f);
    }
}

// --------------------------------------------------------------------------------------------- masts
void mast(Ctx& cx, const sim::ModelPart& pt, bool tripod) {
    double u = pt.f, z0 = cx.deck(u) + pt.z, xx = cx.x(u);
    float rake = float(std::tan(pt.rake * kDegToRad));
    float H = float(pt.h);
    MeshBuilder mb = cx.at(0);
    mb.setMaterial(cx.paint.superstructure);
    auto onMast = [&](float h) { return v3(xx - h * rake, 0.0, z0 + h); };
    Vec3f top = onMast(H);
    float kneeH = tripod ? H * 0.62f : H * 0.7f;
    Vec3f knee = onMast(kneeH);
    float legR = cx.destroyer ? 0.28f : 0.45f;
    if (tripod) {
        mb.tube(v3(xx, 0, z0), knee, legR, legR * 0.8f, 10, false);
        float spread = std::max(2.0f, H * 0.11f);
        for (int side : {1, -1}) {
            Vec3f foot = v3(xx - spread * 1.3f, side * spread, cx.deckZ(xx - spread * 1.3f, side * spread) + pt.z);
            mb.tube(foot, knee, legR * 0.7f, legR * 0.62f, 8, false);
            if (cx.opt.details) {
                MeshBuilder fb = cx.at(1);
                fb.setMaterial(cx.paint.superstructure);
                fb.ellipticCylinder(foot - Vec3f{0, 0, 0.05f}, legR * 1.4f, legR * 1.4f, 0.25f, 10, 0.0f, 0.8f, true, false);
            }
        }
        // Starfish platform at the knee.
        auto pf = ellipseOutline(knee.x, 0, 2.0f, 1.7f, 14);
        platform(cx, pf, knee.z, 0.25f, true);
        if (cx.opt.details) {
            MeshBuilder rb = cx.at(2);
            railing(rb, cx.paint, lifted(pf, knee.z, -0.1f), 1.0f, 2, true);
        }
    } else if (pt.style == "derrick") {
        mb.tube(v3(xx, 0, z0), top, 0.42f, 0.25f, 10, false);
        mb.box(v3(xx - 1.3, -1.3, z0), v3(xx + 1.3, 1.3, z0 + 2.6));  // mast house with the winches
        // Cargo booms and their topping lifts.
        MeshBuilder fb = cx.at(1);
        for (int dir : {1, -1}) {
            Vec3f heel = v3(xx + dir * 1.0, 0, z0 + 2.9);
            Vec3f head = v3(xx + dir * H * 0.5f, 0, z0 + H * 0.42f);
            fb.setMaterial(cx.paint.superstructure);
            fb.tube(heel, head, 0.18f, 0.12f, 8, true);
            fb.setMaterial(cx.paint.rope);
            fb.tube(head, onMast(H * 0.75f), 0.025f, 0.025f, 4, false);
            fb.tube(head, head - Vec3f{0, 0, 5.0f}, 0.02f, 0.02f, 4, false);
            fb.setMaterial(cx.paint.black);
            fb.box(head - Vec3f{0.12f, 0.12f, 5.3f}, head - Vec3f{-0.12f, -0.12f, 5.0f});
            // Winches on the mast house.
            fb.setMaterial(cx.paint.metal);
            fb.tube(v3(xx + dir * 0.7, -0.6, z0 + 2.9), v3(xx + dir * 0.7, 0.6, z0 + 2.9), 0.3f, 0.3f, 10, true);
        }
        // Crosstree.
        mb.box(onMast(H * 0.75f) + Vec3f{-0.3f, -1.6f, -0.2f}, onMast(H * 0.75f) + Vec3f{0.3f, 1.6f, 0.2f});
    } else {
        mb.tube(v3(xx, 0, z0), knee, legR * 0.85f, legR * 0.65f, 10, false);
    }
    // Topmast.
    if (pt.style != "derrick") mb.tube(knee, top, legR * 0.45f, legR * 0.25f, 8, false);
    if (!cx.opt.details) return;
    MeshBuilder fb = cx.at(1);
    fb.setMaterial(cx.paint.superstructure);
    fb.sphere(top + Vec3f{0, 0, 0.1f}, 0.14f, 0.14f, 0.14f, 8, 5);  // truck
    cx.mastTops.push_back(top);
    // Yardarm with foot ropes, signal lamps (blinkers) at the ends and halyard blocks.
    float span = std::max(2.5f, float(cx.B) * (cx.destroyer ? 0.22f : 0.2f));
    Vec3f yard = onMast(H * (tripod ? 0.8f : 0.82f));
    fb.tube(yard + Vec3f{0, -span, 0}, yard + Vec3f{0, span, 0}, 0.07f, 0.07f, 6, true);
    fb.box(yard + Vec3f{-0.25f, -0.4f, -0.12f}, yard + Vec3f{0.25f, 0.4f, 0.12f});
    MeshBuilder kb = cx.at(2);
    for (int side : {1, -1}) {
        Vec3f e = yard + Vec3f{0, side * span, 0};
        cx.yardEnds.push_back(e);
        kb.setMaterial(cx.paint.black);
        kb.tube(e + Vec3f{0, -side * 0.3f, 0.0f}, e + Vec3f{0, -side * 0.3f, 0.35f}, 0.09f, 0.09f, 8, true);
        kb.setMaterial(cx.paint.lens);
        kb.tube(e + Vec3f{0, -side * 0.3f, 0.36f}, e + Vec3f{0, -side * 0.3f, 0.5f}, 0.07f, 0.07f, 8, true);
        kb.setMaterial(cx.paint.rope);
        for (int k = 1; k <= 3; ++k) {
            float t = float(k) / 3.5f;
            kb.box(yard + Vec3f{-0.04f, side * span * t - 0.04f, -0.35f}, yard + Vec3f{0.04f, side * span * t + 0.04f, -0.12f});
        }
        std::vector<Vec3f> foot;
        for (int k = 0; k <= 6; ++k) {
            float t = float(k) / 6.0f;
            foot.push_back(yard + Vec3f{0.12f, side * span * (0.1f + 0.85f * t), -0.6f - 0.2f * std::sin(t * float(kPi))});
        }
        kb.polyline(foot, 0.015f, 4);
    }
    // A second, shorter yard on tall foremasts (radio aerial spreader).
    if (H > 18.0f && (tripod || pt.style != "derrick")) {
        Vec3f y2 = onMast(H * 0.92f);
        fb.tube(y2 + Vec3f{0, -span * 0.55f, 0}, y2 + Vec3f{0, span * 0.55f, 0}, 0.05f, 0.05f, 6, true);
    }
    // Gaff for the battle ensign on the mainmast (the aftermost mast).
    if (pt.f > 0.5) {
        Vec3f g0 = onMast(H * 0.66f);
        Vec3f g1 = g0 + Vec3f{-3.6f, 0, 2.0f};
        fb.tube(g0, g1, 0.07f, 0.05f, 6, true);
        cx.gaffEnd = g1;
    }
}

void tower(Ctx& cx, const sim::ModelPart& pt) {
    double u = pt.f, z0 = cx.deck(u) + pt.z, xx = cx.x(u);
    MeshBuilder mb = cx.at(0);
    mb.setMaterial(cx.paint.superstructure);
    float H = float(pt.h);
    if (pt.style == "usn_pole") {
        mb.tube(v3(xx, 0, z0), v3(xx, 0, z0 + H), 0.5f, 0.25f, 10, false);
        auto pf = roundedRect(float(xx), 0, 3.2f, 3.2f, 0.6f, 3);
        platform(cx, pf, float(z0) + H * 0.7f, 0.25f, true);
        if (cx.opt.details) {
            MeshBuilder rb = cx.at(2);
            railing(rb, cx.paint, lifted(pf, float(z0) + H * 0.7f, -0.1f), 1.0f, 2, true);
            MeshBuilder fb = cx.at(1);
            fb.setMaterial(cx.paint.superstructure);
            Vec3f yard = v3(xx, 0, z0 + H * 0.84f);
            fb.tube(yard + Vec3f{0, -3.0f, 0}, yard + Vec3f{0, 3.0f, 0}, 0.07f, 0.07f, 6, true);
            cx.yardEnds.push_back(yard + Vec3f{0, -3.0f, 0});
            cx.yardEnds.push_back(yard + Vec3f{0, 3.0f, 0});
            cx.mastTops.push_back(v3(xx, 0, z0 + H));
        }
        return;
    }
    // Enclosed tower foremast (New Orleans): tapering, with platforms and a pole on top.
    float l0 = float(pt.len), w0 = float(pt.wid);
    std::vector<std::vector<Vec3f>> rings;
    for (int k = 0; k <= 4; ++k) {
        float t = float(k) / 4.0f;
        float s = 1.0f - 0.35f * t;
        rings.push_back(ring(roundedRect(float(xx), 0, l0 * s, w0 * s, 0.6f * s, 3), float(z0) + H * 0.8f * t));
    }
    for (size_t k = 0; k + 1 < rings.size(); ++k) mb.loft(rings[k], rings[k + 1], false, false);
    float tz = float(z0) + H * 0.8f;
    auto pf = roundedRect(float(xx), 0, 4.6f, 4.6f, 0.8f, 3);
    platform(cx, pf, tz, 0.3f, true);
    house(cx, mb, roundedRect(float(xx), 0, 2.6f, 2.6f, 0.5f, 3), tz, tz + 1.6f, cx.paint.superstructure);
    windowBand(cx, roundedRect(float(xx), 0, 2.6f, 2.6f, 0.5f, 3), tz + 0.8f, tz + 1.3f, -1.1f, 0.6f);
    mb.tube(v3(xx, 0, tz + 1.6f), v3(xx, 0, z0 + H), 0.22f, 0.12f, 8, false);
    auto mid = roundedRect(float(xx), 0, l0 * 1.25f, w0 * 1.3f, 0.6f, 3);
    platform(cx, mid, float(z0) + H * 0.45f, 0.25f, true);
    if (cx.opt.details) {
        MeshBuilder rb = cx.at(2);
        railing(rb, cx.paint, lifted(pf, tz, -0.1f), 1.0f, 2, true);
        railing(rb, cx.paint, lifted(mid, float(z0) + H * 0.45f, -0.1f), 1.0f, 2, true);
        MeshBuilder lb = cx.at(1);
        ladder(lb, cx.paint, v3(xx - l0 * 0.5f - 0.3f, 0.6f, z0), v3(xx - l0 * 0.35f, 0.6f, z0 + H * 0.45f), 0.5f);
        cx.mastTops.push_back(v3(xx, 0, z0 + H));
    }
}

// ----------------------------------------------------------------------------------------- directors
namespace {

const sim::DirectorDef* matchDirector(const Ctx& cx, const sim::ModelPart& pt) {
    const sim::DirectorDef* best = nullptr;
    double bestD = 1e9;
    double xx = cx.x(pt.f), z = cx.deck(pt.f) + pt.z;
    for (const auto& d : cx.c.directors) {
        double dist = std::fabs(d.x - xx) + std::fabs(d.height - z) * 0.5;
        if (!pt.ref.empty() && d.radar == pt.ref) dist -= 50.0;
        if (dist < bestD) {
            bestD = dist;
            best = &d;
        }
    }
    return best;
}

/// Rangefinder tube across the director with hooded end windows ("ears").
void rangefinder(MeshBuilder& mb, const PaintScheme& p, Vec3f c, float base, float r) {
    mb.setMaterial(p.superstructure);
    mb.tube(c - Vec3f{0, base * 0.5f, 0}, c + Vec3f{0, base * 0.5f, 0}, r, r, 10, true);
    for (int side : {1, -1}) {
        Vec3f e = c + Vec3f{0, side * base * 0.5f, 0};
        mb.box(e + Vec3f{-r * 1.6f, -r * 1.4f, -r * 1.3f}, e + Vec3f{r * 1.4f, r * 1.4f, r * 1.5f});
        mb.setMaterial(p.dark);
        mb.box(e + Vec3f{r * 1.4f, -r * 0.9f, -r * 0.4f}, e + Vec3f{r * 1.45f, r * 0.9f, r * 0.6f});
        mb.setMaterial(p.superstructure);
    }
}

/// Mk 4 fire-control antenna: a framed "mattress" of dipoles before a slatted reflector.
void mk4Antenna(MeshBuilder& mb, const PaintScheme& p, Vec3f c, float w, float h) {
    mb.setMaterial(p.superstructure);
    mb.bar(c + Vec3f{0, -w * 0.5f, -h * 0.5f}, c + Vec3f{0, w * 0.5f, -h * 0.5f}, 0.08f, 0.08f);
    mb.bar(c + Vec3f{0, -w * 0.5f, h * 0.5f}, c + Vec3f{0, w * 0.5f, h * 0.5f}, 0.08f, 0.08f);
    for (int k = 0; k <= 6; ++k) {
        float yy = -w * 0.5f + w * float(k) / 6.0f;
        mb.bar(c + Vec3f{0, yy, -h * 0.5f}, c + Vec3f{0, yy, h * 0.5f}, 0.04f, 0.04f, {1, 0, 0});
    }
    for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 6; ++k) {
            Vec3f d = c + Vec3f{0.25f, -w * 0.5f + w * (float(k) + 0.5f) / 6.0f, -h * 0.5f + h * (float(r) + 0.5f) / 4.0f};
            mb.bar(d - Vec3f{0, 0.16f, 0}, d + Vec3f{0, 0.16f, 0}, 0.03f, 0.03f);
            mb.bar(d - Vec3f{0.25f, 0, 0}, d, 0.02f, 0.02f);
        }
    mb.bar(c + Vec3f{-0.6f, 0, -h * 0.5f}, c + Vec3f{0, 0, -h * 0.3f}, 0.08f, 0.08f);
}

/// Mk 3 fire-control antenna: two parabolic-cylinder troughs side by side.
void mk3Antenna(MeshBuilder& mb, const PaintScheme& p, Vec3f c, float w, float h) {
    mb.setMaterial(p.superstructure);
    for (int side : {1, -1}) {
        Vec3f o = c + Vec3f{0, side * w * 0.26f, 0};
        const int n = 8;
        std::vector<Vec3f> lo, hi;
        for (int k = 0; k <= n; ++k) {
            float t = -1.0f + 2.0f * float(k) / n;
            float zz = t * h * 0.5f;
            float xb = -0.35f * t * t;
            lo.push_back(o + Vec3f{xb, -w * 0.24f, zz});
            hi.push_back(o + Vec3f{xb, w * 0.24f, zz});
        }
        for (int k = 0; k < n; ++k) mb.flatQuad(lo[size_t(k)], hi[size_t(k)], hi[size_t(k + 1)], lo[size_t(k + 1)]);
        mb.bar(o + Vec3f{0.0f, -w * 0.24f, 0}, o + Vec3f{0.0f, w * 0.24f, 0}, 0.05f, 0.05f, {1, 0, 0});
        mb.bar(o + Vec3f{-0.3f, 0, 0}, o + Vec3f{0.05f, 0, 0}, 0.06f, 0.06f);
    }
    mb.bar(c + Vec3f{-0.4f, -w * 0.5f, -h * 0.5f - 0.1f}, c + Vec3f{-0.4f, w * 0.5f, -h * 0.5f - 0.1f}, 0.1f, 0.1f);
}

}  // namespace

void director(Ctx& cx, const sim::ModelPart& pt) {
    double xx = cx.x(pt.f), z0 = cx.deck(pt.f) + pt.z;
    const sim::DirectorDef* dd = matchDirector(cx, pt);
    std::string radarId = !pt.ref.empty() ? pt.ref : (dd ? dd->radar : std::string());
    float base = dd ? float(dd->rangefinderBase) : 3.0f;
    if (cx.ijn) base = cx.c.type == sim::ShipType::CA ? 6.0f : (cx.destroyer ? 3.0f : 4.5f);
    // Pedestal (static) down to the structure below.
    MeshBuilder sb = cx.at(0);
    sb.setMaterial(cx.paint.superstructure);
    double foot = std::min(z0 - 0.4, cx.surfaceZ(xx, 0.0, z0 - 0.2));
    if (z0 - foot > 6.0) foot = z0 - 1.2;  // nothing close below: a short trunk
    sb.ellipticCylinder(v3(xx, 0, foot - 0.05), float(pt.len) * 0.42f, float(pt.len) * 0.42f, float(z0 - foot + 0.1), 16, 0.0f, 1.0f, true, false);
    int di = cx.part("director", PartKind::Director, 0);
    MeshPart& p = cx.P(di);
    p.pivot = v3(xx, 0, z0);
    p.sub = dd ? int(dd - cx.c.directors.data()) : 0;  // trains with this director's target
    MeshBuilder mb(p);
    mb.setMaterial(cx.paint.superstructure);
    float L = float(pt.len), W = float(pt.wid), H = float(pt.h);
    float x = float(xx), z = float(z0);
    if (cx.ijn) {
        // Type 94 director: domed cupola with observation hoods.
        mb.lathe(v3(x, 0, z), {0, 0, 1}, {{L * 0.5f, 0}, {L * 0.5f, H * 0.55f}, {L * 0.42f, H * 0.8f}, {L * 0.2f, H * 0.98f}, {0.0f, H}}, 16, false);
        for (int side : {1, -1}) mb.box(v3(x + L * 0.1f, side * L * 0.25f - 0.3f, z + H * 0.6f), v3(x + L * 0.5f + 0.2f, side * L * 0.25f + 0.3f, z + H * 0.9f));
        rangefinder(mb, cx.paint, v3(x - L * 0.15f, 0, z + H * 0.45f), base, 0.2f);
    } else if (cx.rn) {
        // Director control tower: squat house with a sloped face and a hood.
        auto o = gunhouseOutline(x, 0, L, W * 0.75f, W * 0.55f, L * 0.2f, 0.5f);
        mb.loft(ring(o, z), ring(o, z + H, {}, 0.92f), true, false);
        mb.box(v3(x - L * 0.2f, -W * 0.2f, z + H), v3(x + L * 0.25f, W * 0.2f, z + H + 0.5f));
        rangefinder(mb, cx.paint, v3(x - L * 0.25f, 0, z + H * 0.7f), base, 0.18f);
    } else {
        // USN Mk 33/34/37: sloped front and sides, rangefinder ears at the rear.
        bool mk37 = radarId == "usn_fd_mk4";
        auto o = gunhouseOutline(x, 0, L, W * (mk37 ? 0.72f : 0.78f), W * 0.5f, L * 0.28f, 0.35f);
        std::vector<Vec3f> bot = ring(o, z), top;
        for (auto& q : ring(o, z + H, {}, 0.88f)) top.push_back(q + Vec3f{-L * 0.06f, 0, 0});
        mb.loft(bot, top, true, false);
        rangefinder(mb, cx.paint, v3(x - L * 0.22f, 0, z + H * 0.72f), std::max(base, W * 0.95f), 0.17f);
        // Roof hatches and the trainer's sight hoods.
        MeshBuilder kb(p);
        kb.setMaterial(cx.paint.superstructure);
        for (int side : {1, -1}) kb.box(v3(x + L * 0.05f, side * W * 0.2f - 0.25f, z + H), v3(x + L * 0.3f, side * W * 0.2f + 0.25f, z + H + 0.35f));
        kb.ellipticCylinder(v3(x - L * 0.25f, 0, z + H), 0.35f, 0.35f, 0.12f, 10, 0.0f, 1.0f, true, false);
        if (mk37) mk4Antenna(mb, cx.paint, v3(x + L * 0.2f, 0, z + H + 1.25f), 2.4f, 1.6f);
        else if (radarId == "usn_fc_mk3") mk3Antenna(mb, cx.paint, v3(x + L * 0.1f, 0, z + H + 0.75f), 3.2f, 0.9f);
    }
}

// --------------------------------------------------------------------------------------------- radar
void radar(Ctx& cx, const sim::ModelPart& pt) {
    double xx = cx.x(pt.f), z0 = cx.deck(pt.f) + pt.z;
    int ri = cx.part("radar_" + pt.style, PartKind::Radar, 0);
    MeshPart& p = cx.P(ri);
    p.pivot = v3(xx, 0, z0);
    p.sub = pt.style == "sg" ? 1 : 0;  // the SG turns faster than the air-search SC
    MeshBuilder mb(p);
    mb.setMaterial(cx.paint.superstructure);
    float x = float(xx), z = float(z0);
    float W = float(pt.wid), H = float(pt.h);
    if (pt.style == "sg") {
        // "Orange peel": a truncated paraboloid facing forward, the feed horn at its focus.
        float f = W * 0.42f;
        const int nu = 10, nv = 6;
        std::vector<Vec3f> g;
        for (int j = 0; j <= nv; ++j)
            for (int i = 0; i <= nu; ++i) {
                float yy = (-0.5f + float(i) / nu) * W, zz = (-0.5f + float(j) / nv) * H;
                g.push_back(v3(x + (yy * yy + zz * zz) / (4.0f * f), yy, z + H * 0.5f + 0.3f + zz));
            }
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                auto at = [&](int ii, int jj) { return g[size_t(jj * (nu + 1) + ii)]; };
                mb.flatQuad(at(i, j), at(i + 1, j), at(i + 1, j + 1), at(i, j + 1));
            }
        Vec3f focus = v3(x + f, 0, z + H * 0.5f + 0.3f);
        mb.setMaterial(cx.paint.metal);
        mb.bar(v3(x, 0, z + 0.3f + H * 0.5f), focus, 0.06f, 0.06f);
        mb.box(focus - Vec3f{0.1f, 0.12f, 0.1f}, focus + Vec3f{0.1f, 0.12f, 0.1f});
        mb.setMaterial(cx.paint.superstructure);
        mb.lathe(v3(x - 0.3f, 0, z - 0.6f), {0, 0, 1}, {{0.35f, 0}, {0.35f, 0.7f}, {0.2f, 0.9f}, {0.0f, 0.92f}}, 10, false);
    } else {
        // SC "bedspring": a frame of dipoles in front of a wire-mesh reflector.
        float bz = z + 0.4f;
        mb.bar(v3(x, -W * 0.5f, bz), v3(x, W * 0.5f, bz), 0.1f, 0.12f);
        mb.bar(v3(x, -W * 0.5f, bz + H), v3(x, W * 0.5f, bz + H), 0.1f, 0.12f);
        for (int k = 0; k <= 8; ++k) {
            float yy = -W * 0.5f + W * float(k) / 8.0f;
            mb.bar(v3(x, yy, bz), v3(x, yy, bz + H), k % 4 == 0 ? 0.09f : 0.035f, k % 4 == 0 ? 0.09f : 0.035f, {1, 0, 0});
        }
        for (int r = 1; r < 6; ++r) {
            float zz = bz + H * float(r) / 6.0f;
            mb.bar(v3(x, -W * 0.5f, zz), v3(x, W * 0.5f, zz), 0.03f, 0.03f);
        }
        mb.setMaterial(cx.paint.metal);
        for (int r = 0; r < 3; ++r)
            for (int k = 0; k < 6; ++k) {
                Vec3f d = v3(x + 0.35f, -W * 0.5f + W * (float(k) + 0.5f) / 6.0f, bz + H * (float(r) + 0.5f) / 3.0f);
                mb.bar(d - Vec3f{0, 0.3f, 0}, d + Vec3f{0, 0.3f, 0}, 0.035f, 0.035f);
                mb.bar(d - Vec3f{0.35f, 0, 0}, d, 0.025f, 0.025f);
            }
        mb.setMaterial(cx.paint.superstructure);
        mb.tube(v3(x - 0.3f, 0, z - 1.0f), v3(x - 0.3f, 0, bz + H * 0.5f), 0.15f, 0.12f, 8, false);
        mb.bar(v3(x - 0.3f, 0, bz + H * 0.5f), v3(x, 0, bz + H * 0.5f), 0.12f, 0.12f);
    }
    // Static mount: platform under the antenna, carried on a pole down to the mast head or the
    // structure below when the antenna stands above them.
    MeshBuilder sb = cx.at(0);
    sb.setMaterial(cx.paint.superstructure);
    sb.ellipticCylinder(v3(xx - 0.3, 0, z0 - 1.25), 0.6f, 0.6f, 0.25f, 12, 0.0f, 1.0f, true, true);
    double below = cx.surfaceZ(xx - 0.3, 0.0);
    for (const Vec3f& t : cx.mastTops)
        if (std::fabs(t.x - (xx - 0.3)) < 3.0 && t.z < z0 - 1.25) below = std::max(below, double(t.z));
    bool throughMast = false;
    for (const Vec3f& t : cx.mastTops)
        if (std::fabs(t.x - (xx - 0.3)) < 3.0 && t.z >= z0 - 1.25) throughMast = true;
    if (!throughMast && below < z0 - 1.3) sb.tube(v3(xx - 0.3, 0, below), v3(xx - 0.3, 0, z0 - 1.25), 0.28f, 0.2f, 8, false);
}

// ---------------------------------------------------------------------------------------- searchlight
void searchlight(Ctx& cx, const sim::ModelPart& pt) {
    double u = pt.f, xx = cx.x(u), z0 = cx.deck(u) + pt.z;
    float r = float(cx.c.searchlightCm / 200.0 + 0.1);
    if (pt.z > 2.0) {
        // Searchlight platform on a lattice post, with a railing.
        MeshBuilder hb = cx.at(0);
        hb.setMaterial(cx.paint.superstructure);
        double dz = cx.deckZ(xx, pt.y * 0.6);
        hb.tube(v3(xx, pt.y * 0.6, dz), v3(xx, pt.y, z0 - 0.9), 0.35f, 0.3f, 8, false);
        auto pf = ellipseOutline(float(xx), float(pt.y), 1.7f, 1.7f, 14);
        platform(cx, pf, float(z0) - 0.8f, 0.2f, true);
        if (cx.opt.details) {
            MeshBuilder rb = cx.at(2);
            railing(rb, cx.paint, lifted(pf, float(z0) - 0.8f, -0.1f), 1.0f, 2, true);
        }
    }
    int si = cx.part("searchlight", PartKind::Searchlight, 0);
    MeshPart& sp = cx.P(si);
    sp.pivot = v3(xx, pt.y, z0);
    {
        MeshBuilder mb(sp);
        mb.setMaterial(cx.paint.superstructure);
        mb.lathe(v3(xx, pt.y, z0 - 0.8), {0, 0, 1}, {{0.35f, 0}, {0.3f, 0.15f}, {0.15f, 0.25f}, {0.15f, 0.8f}}, 10, false);
        // Yoke.
        for (int side : {1, -1}) mb.bar(v3(xx, pt.y + side * (r + 0.08f), z0), v3(xx, pt.y + side * (r + 0.08f), z0 + r), 0.1f, 0.12f, {1, 0, 0});
        mb.bar(v3(xx, pt.y - r - 0.08f, z0), v3(xx, pt.y + r + 0.08f, z0), 0.14f, 0.1f, {1, 0, 0});
    }
    int li = cx.part("searchlight_lamp", PartKind::SearchlightLamp, 0, si);
    MeshPart& lp = cx.P(li);
    lp.pivot = v3(xx, pt.y, z0 + r);
    MeshBuilder mb(lp);
    Vec3f c = lp.pivot;
    mb.setMaterial(cx.paint.superstructure);
    // Drum with the rear dome, the ventilated lantern on top and the front ring.
    mb.lathe(c + Vec3f{-r * 0.9f, 0, 0}, {1, 0, 0}, {{0.0f, -r * 0.35f}, {r * 0.75f, -r * 0.15f}, {r, 0.0f}, {r, r * 1.5f}, {r * 1.08f, r * 1.52f}, {r * 1.08f, r * 1.65f}, {r * 0.95f, r * 1.65f}}, 16, false);
    mb.lathe(c + Vec3f{-r * 0.2f, 0, r * 0.95f}, {0, 0, 1}, {{r * 0.3f, 0}, {r * 0.3f, r * 0.25f}, {r * 0.42f, r * 0.3f}, {r * 0.42f, r * 0.38f}, {0.0f, r * 0.42f}}, 10, false);
    for (int side : {1, -1}) mb.tube(c + Vec3f{0, side * r, 0}, c + Vec3f{0, side * (r + 0.1f), 0}, 0.07f, 0.07f, 8, true);  // trunnions
    mb.bar(c + Vec3f{-r * 0.9f, 0, -r * 0.2f}, c + Vec3f{-r * 1.5f, 0, -r * 0.5f}, 0.05f, 0.05f);  // training handle
    mb.setMaterial(cx.paint.lens);
    mb.disc(c + Vec3f{r * 0.7f, 0, 0}, {1, 0, 0}, r * 0.93f, 16);
    if (cx.opt.details) {
        // Iris shutter vanes across the lens.
        MeshBuilder kb(lp);
        kb.setMaterial(cx.paint.metal);
        for (int k = -3; k <= 3; ++k) {
            float zz = r * 0.85f * float(k) / 3.5f;
            float hw = r * 0.95f * std::sqrt(std::max(0.0f, 1.0f - (zz / r) * (zz / r)));
            kb.bar(c + Vec3f{r * 0.74f, -hw, zz}, c + Vec3f{r * 0.74f, hw, zz}, 0.02f, 0.05f, {1, 0, 0});
        }
    }
}

// ---------------------------------------------------------------------------------------------- crane
void crane(Ctx& cx, const sim::ModelPart& pt) {
    double u = pt.f, xx = cx.x(u), z0 = cx.deck(u) + pt.z;
    float H = float(pt.h), J = float(pt.len);
    MeshBuilder mb = cx.at(0);
    mb.setMaterial(cx.paint.superstructure);
    Vec3f base = v3(xx, pt.y, z0);
    mb.lathe(base, {0, 0, 1}, {{0.7f, 0}, {0.7f, 0.3f}, {0.45f, 0.5f}, {0.4f, H * 0.6f}, {0.3f, H}, {0.0f, H + 0.05f}}, 12, false);
    mb.box(base + Vec3f{-1.6f, -0.9f, 0}, base + Vec3f{-0.4f, 0.9f, 1.8f});  // winch house
    cx.takeCircle(xx, pt.y, 1.2);
    // Lattice jib from the post to the head, raised and pointing aft.
    Vec3f heel = base + Vec3f{-0.5f, 0, 1.6f};
    Vec3f head = base + Vec3f{-J * 0.8f, 0, H * 0.92f};
    Vec3f d = normalize(head - heel);
    Vec3f side{0, 1, 0};
    Vec3f up = normalize(cross(d, side));
    MeshBuilder fb = cx.at(cx.opt.details ? 1 : 0);
    fb.setMaterial(cx.paint.superstructure);
    float w0 = 0.45f, w1 = 0.15f;
    Vec3f corners[4] = {side + up, side - up, -side - up, -side + up};
    float len = length(head - heel);
    for (auto& cr : corners) fb.tube(heel + cr * w0, head + cr * w1, 0.06f, 0.05f, 5, false);
    int bays = int(len / 1.0f);
    for (int k = 0; k < bays; ++k) {
        float t0 = float(k) / bays, t1 = float(k + 1) / bays;
        float wa = lerp(w0, w1, t0), wb = lerp(w0, w1, t1);
        Vec3f a = heel + (head - heel) * t0, b = heel + (head - heel) * t1;
        for (int s = 0; s < 4; ++s) {
            Vec3f ca = corners[s] * wa, cb = corners[(s + 1) % 4] * wb;
            fb.tube(a + ca, b + cb, 0.03f, 0.03f, 4, false);
        }
    }
    // Hoist wire and the hook block.
    fb.setMaterial(cx.paint.rope);
    fb.tube(head, head - Vec3f{0, 0, 4.0f}, 0.025f, 0.025f, 4, false);
    fb.tube(head, base + Vec3f{0, 0, H}, 0.025f, 0.025f, 4, false);
    fb.setMaterial(cx.paint.black);
    fb.box(head - Vec3f{0.18f, 0.12f, 4.4f}, head - Vec3f{-0.18f, -0.12f, 4.0f});
}

// --------------------------------------------------------------------------------------------- rigging
void rigging(Ctx& cx) {
    MeshBuilder kb = cx.at(2);
    kb.setMaterial(cx.paint.rope);
    // Antenna wires between the mast heads (a little below the trucks), with a slight sag.
    std::vector<Vec3f> tops = cx.mastTops;
    std::sort(tops.begin(), tops.end(), [](const Vec3f& a, const Vec3f& b) { return a.x > b.x; });
    for (size_t i = 0; i + 1 < tops.size(); ++i) {
        Vec3f a = tops[i] - Vec3f{0, 0, 0.6f}, b = tops[i + 1] - Vec3f{0, 0, 0.6f};
        if (std::fabs(a.x - b.x) < 8.0f) continue;
        for (int w = -1; w <= 1; w += 2) {
            std::vector<Vec3f> pts;
            for (int k = 0; k <= 16; ++k) {
                float t = float(k) / 16.0f;
                Vec3f p = lerp(a, b, t) + Vec3f{0, w * 0.9f * std::sin(t * float(kPi)), -0.9f * std::sin(t * float(kPi))};
                pts.push_back(p);
            }
            kb.polyline(pts, 0.018f, 3);
        }
    }
    // Signal halyards from the yardarms down towards the bridge wings.
    for (const Vec3f& e : cx.yardEnds) {
        float s = e.y >= 0 ? 1.0f : -1.0f;
        Vec3f foot = v3(e.x + 6.0f, s * std::max(1.0, cx.edgeY(e.x + 6.0) - 1.0), cx.deckX(e.x + 6.0) + 6.0);
        foot.z = std::min(foot.z, e.z - 4.0f);
        kb.polyline({e + Vec3f{0, -s * 0.2f, -0.3f}, foot}, 0.012f, 3);
        kb.polyline({e + Vec3f{0, -s * 0.6f, -0.3f}, foot + Vec3f{-0.6f, 0, 0}}, 0.012f, 3);
    }
}

}  // namespace it::procgen::detail
