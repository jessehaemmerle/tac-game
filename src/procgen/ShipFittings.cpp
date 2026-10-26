// Fittings: railings and ladders, Carley floats, catapults with their floatplanes (wings, floats,
// struts and national markings), ship's boats on davits or chocks, bollards, fairleads, ventilators
// and hatches on the free deck, depth-charge racks and throwers, and the ensign.
#include "procgen/ShipModelInternal.h"

namespace it::procgen::detail {

void railing(MeshBuilder& mb, const PaintScheme& p, const std::vector<Vec3f>& pts, float h, int rails, bool closed) {
    if (pts.size() < 2) return;
    mb.setMaterial(p.superstructure);
    std::vector<Vec3f> path = pts;
    if (closed) path.push_back(pts.front());
    // Stanchions about every 1.4 m along the path.
    float carry = 0.0f;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        Vec3f a = path[i], b = path[i + 1];
        float len = length(b - a);
        float t = carry;
        while (t < len) {
            Vec3f s = lerp(a, b, len > 1e-4f ? t / len : 0.0f);
            mb.tube(s, s + Vec3f{0, 0, h}, 0.025f, 0.022f, 4, false);
            t += 1.4f;
        }
        carry = t - len;
    }
    if (!closed) mb.tube(path.back(), path.back() + Vec3f{0, 0, h}, 0.025f, 0.022f, 4, false);
    for (int r = 1; r <= rails; ++r) {
        float zr = h * float(r) / float(rails);
        std::vector<Vec3f> rail;
        for (auto& q : pts) rail.push_back(q + Vec3f{0, 0, zr});
        mb.polyline(rail, r == rails ? 0.024f : 0.014f, 4, closed);
    }
}

void ladder(MeshBuilder& mb, const PaintScheme& p, Vec3f lo, Vec3f hi, float width) {
    mb.setMaterial(p.superstructure);
    Vec3f run = hi - lo;
    Vec3f flat = normalize(Vec3f{run.x, run.y, 0.0f});
    Vec3f side = normalize(cross(Vec3f{0, 0, 1}, flat)) * (width * 0.5f);
    for (int s : {1, -1}) {
        mb.bar(lo + side * float(s), hi + side * float(s), 0.04f, 0.18f, {0, 0, 1});
        mb.polyline({lo + side * float(s) + Vec3f{0, 0, 0.9f}, hi + side * float(s) + Vec3f{0, 0, 0.9f}}, 0.022f, 4);
        mb.tube(lo + side * float(s), lo + side * float(s) + Vec3f{0, 0, 0.9f}, 0.02f, 0.02f, 4, false);
    }
    int steps = std::max(2, int(run.z / 0.25f));
    for (int k = 1; k < steps; ++k) {
        Vec3f c = lo + run * (float(k) / steps);
        mb.bar(c - side, c + side, 0.05f, 0.18f, flat);
    }
}

void carleyFloat(MeshBuilder& mb, const PaintScheme& p, float len, float wid) {
    auto o = roundedRect(0, 0, len, wid, wid * 0.42f, 3);
    std::vector<Vec3f> ringPts;
    for (auto& q : o) ringPts.push_back({q.x, q.y, 0.0f});
    mb.setMaterial(p.raft);
    mb.polyline(ringPts, 0.16f, 6, true);
    Material grating = p.raft;
    grating.albedo = grating.albedo * 0.7f;
    mb.setMaterial(grating);
    for (float yy = -wid * 0.3f; yy <= wid * 0.31f; yy += wid * 0.2f) mb.box({-len * 0.42f, yy - 0.04f, -0.05f}, {len * 0.42f, yy + 0.04f, 0.0f});
    for (float xx = -len * 0.35f; xx <= len * 0.36f; xx += len * 0.23f) mb.box({xx - 0.04f, -wid * 0.38f, -0.1f}, {xx + 0.04f, wid * 0.38f, -0.05f});
    // Grab line looped around the float.
    mb.setMaterial(p.rope);
    std::vector<Vec3f> loop;
    for (size_t i = 0; i < o.size(); ++i) loop.push_back({o[i].x * 1.08f, o[i].y * 1.12f, (i & 1) ? -0.12f : 0.0f});
    mb.polyline(loop, 0.015f, 3, true);
}

// ---------------------------------------------------------------------------------------- catapult
void catapult(Ctx& cx, const sim::ModelPart& pt) {
    double xx = cx.x(pt.f), z0 = cx.surfaceZ(xx, pt.y) + pt.z;
    float L = float(pt.len), x = float(xx), y = float(pt.y), z = float(z0);
    // Turntable pedestal (static).
    MeshBuilder sb = cx.at(0);
    sb.setMaterial(cx.paint.superstructure);
    sb.lathe(v3(x, y, z0 - 0.05), {0, 0, 1}, {{1.3f, 0}, {1.3f, 0.3f}, {0.9f, 0.5f}, {0.9f, 0.95f}}, 16, false);
    cx.take(x - L * 0.45f, x + L * 0.55f, y - 1.0f, y + 1.0f);
    int ci = cx.part("catapult", PartKind::Catapult, 0);
    MeshPart& p = cx.P(ci);
    p.pivot = v3(x, y, z0);
    if (pt.style == "fixed") p.mount = 1;  // on the forecastle, pointing over the bow (Zara class)
    p.sub = int(cx.catapults.size());
    cx.catapults.push_back({v3(x, y, z0), ci});
    MeshBuilder mb(p);
    mb.setMaterial(cx.paint.superstructure);
    mb.ellipticCylinder(v3(x, y, z + 0.95f), 1.2f, 1.2f, 0.2f, 16, 0.0f, 1.0f, true, false);
    // Lattice girder: top rails for the carriage, bottom chords and diagonal bracing.
    float x0 = x - L * 0.42f, x1 = x + L * 0.58f;
    float zb = z + 1.15f, zt = z + 2.05f, hw = 0.45f;
    MeshBuilder fb(p);
    fb.setMaterial(cx.paint.superstructure);
    for (int s : {1, -1}) {
        fb.bar(v3(x0, y + s * hw, zt), v3(x1, y + s * hw, zt), 0.16f, 0.2f);
        fb.bar(v3(x0 + 0.6f, y + s * hw * 0.7f, zb), v3(x1 - 1.4f, y + s * hw * 0.7f, zt - 0.45f), 0.12f, 0.12f);
    }
    int bays = int(L / 1.2f);
    for (int k = 0; k <= bays; ++k) {
        float t = float(k) / bays;
        float bx = lerp(x0 + 0.6f, x1 - 1.4f, t);
        float bz = lerp(zb, zt - 0.45f, t);
        for (int s : {1, -1}) {
            fb.bar(v3(bx, y + s * hw * 0.7f, bz), v3(bx, y + s * hw, zt), 0.06f, 0.06f, {1, 0, 0});
            if (k < bays) {
                float nx = lerp(x0 + 0.6f, x1 - 1.4f, float(k + 1) / bays);
                fb.bar(v3(bx, y + s * hw * 0.7f, bz), v3(nx, y + s * hw, zt), 0.05f, 0.05f, {0, 1, 0});
            }
        }
        fb.bar(v3(bx, y - hw, zt - 0.05f), v3(bx, y + hw, zt - 0.05f), 0.05f, 0.05f);
    }
    // Launching carriage (sled) at the after end and the buffer at the head.
    mb.setMaterial(cx.paint.metal);
    mb.box(v3(x0 + 0.5f, y - 0.6f, zt + 0.1f), v3(x0 + 2.3f, y + 0.6f, zt + 0.35f));
    mb.box(v3(x1 - 0.4f, y - 0.55f, zt - 0.3f), v3(x1, y + 0.55f, zt + 0.25f));
    // The explosive-charge breech (USN powder catapult) under the girder.
    mb.tube(v3(x - 1.0f, y, zb), v3(x + 2.5f, y, zb), 0.25f, 0.25f, 10, true);
}

// ---------------------------------------------------------------------------------------- aircraft
namespace {

/// Wing panel from root to tip (y), airfoil chord along x, thin, with dihedral (z rise at the tip).
void wing(MeshBuilder& mb, Vec3f root, float rootChord, float tipChord, float span, float sweep, float dihedral, float thick) {
    auto airfoil = [&](Vec3f lead, float chord) {
        std::vector<Vec3f> r;
        const int n = 10;
        for (int k = 0; k < n; ++k) {
            float a = float(k) / n * float(kTwoPi);
            float t = 0.5f + 0.5f * std::cos(a);                   // 1 leading edge .. 0 trailing edge
            float th = thick * chord * 4.0f * t * (1.0f - t) * (0.6f + 0.4f * t);
            r.push_back(lead + Vec3f{-(1.0f - t) * chord, 0, std::sin(a) >= 0 ? th * 0.7f : -th * 0.3f});
        }
        return r;
    };
    Vec3f tipLead = root + Vec3f{-sweep, span, dihedral};
    auto a = airfoil(root, rootChord), b = airfoil(tipLead, tipChord);
    if (span < 0) std::swap(a, b);
    mb.loft(a, b, true, true);
}

void marking(MeshBuilder& mb, sim::Nation nation, Vec3f c, Vec3f n, float r) {
    Vec3f w = normalize(n);
    Vec3f helper = std::fabs(w.z) < 0.9f ? Vec3f{0, 0, 1} : Vec3f{1, 0, 0};
    Vec3f u = normalize(cross(helper, w)), v = cross(w, u);
    // A flat rectangle in the marking plane, lifted by `lift` along the normal.
    auto rect = [&](float u0, float v0, float u1, float v1, float lift) {
        Vec3f o = c + w * lift;
        mb.flatQuad(o + u * u0 + v * v0, o + u * u1 + v * v0, o + u * u1 + v * v1, o + u * u0 + v * v1);
    };
    switch (nation) {
        case sim::Nation::IJN:
            mb.setMaterial(mat(srgb(235, 232, 225), 0.6f, 0.0f, kPlain));
            mb.disc(c, n, r * 1.12f, 16);
            mb.setMaterial(mat(srgb(185, 28, 30), 0.6f, 0.0f, kPlain));
            mb.disc(c + w * 0.004f, n, r, 16);
            return;
        case sim::Nation::RN:
        case sim::Nation::RAN:
            // Type B roundel of the upper wing, with the white ring of the Type A on the fuselage.
            mb.setMaterial(mat(srgb(34, 44, 92), 0.6f, 0.0f, kPlain));
            mb.disc(c, n, r, 16);
            mb.setMaterial(mat(srgb(225, 225, 220), 0.6f, 0.0f, kPlain));
            mb.disc(c + w * 0.003f, n, r * 0.62f, 16);
            mb.setMaterial(mat(srgb(150, 30, 34), 0.6f, 0.0f, kPlain));
            mb.disc(c + w * 0.006f, n, r * 0.38f, 16);
            return;
        case sim::Nation::RM:
            // Fasces roundel: three black fasces on a white disc with a black rim.
            mb.setMaterial(mat(srgb(20, 20, 22), 0.6f, 0.0f, kPlain));
            mb.disc(c, n, r, 16);
            mb.setMaterial(mat(srgb(232, 232, 226), 0.6f, 0.0f, kPlain));
            mb.disc(c + w * 0.003f, n, r * 0.88f, 16);
            mb.setMaterial(mat(srgb(20, 20, 22), 0.6f, 0.0f, kPlain));
            for (int k = -1; k <= 1; ++k) {
                float a = float(k) * 0.42f;
                float cu = std::sin(a) * r * 0.45f, cv = -std::cos(a) * r * 0.1f;
                rect(cu - r * 0.07f, cv - r * 0.5f, cu + r * 0.07f, cv + r * 0.5f, 0.006f);
                rect(cu - r * 0.11f, cv + r * 0.32f, cu + r * 0.11f, cv + r * 0.5f, 0.007f);  // axe head
            }
            return;
        case sim::Nation::KM:
            // Balkenkreuz: a black cross with a white outline.
            mb.setMaterial(mat(srgb(232, 232, 226), 0.6f, 0.0f, kPlain));
            rect(-r * 0.95f, -r * 0.3f, r * 0.95f, r * 0.3f, 0.0f);
            rect(-r * 0.3f, -r * 0.95f, r * 0.3f, r * 0.95f, 0.001f);
            mb.setMaterial(mat(srgb(20, 20, 22), 0.6f, 0.0f, kPlain));
            rect(-r * 0.8f, -r * 0.16f, r * 0.8f, r * 0.16f, 0.004f);
            rect(-r * 0.16f, -r * 0.8f, r * 0.16f, r * 0.8f, 0.005f);
            return;
        default: break;
    }
    mb.setMaterial(mat(srgb(30, 52, 110), 0.6f, 0.0f, kPlain));
    mb.disc(c, n, r, 16);
    // Five-pointed white star (fan from the centre: star-shaped about it).
    mb.setMaterial(mat(srgb(235, 235, 235), 0.6f, 0.0f, kPlain));
    Vec3f cc = c + w * 0.004f;
    uint32_t centre = mb.vertex(cc, w);
    std::vector<uint32_t> rim;
    for (int k = 0; k <= 10; ++k) {
        float a = float(kPi) * 0.5f + float(k) / 10.0f * float(kTwoPi);
        float rr = (k & 1) ? r * 0.38f : r * 0.98f;
        rim.push_back(mb.vertex(cc + (u * std::cos(a) + v * std::sin(a)) * rr, w));
    }
    for (size_t k = 0; k + 1 < rim.size(); ++k) mb.tri(centre, rim[k], rim[k + 1]);
}

/// Floatplane in a local frame: x forward, origin under the main float, z up.
void floatplane(MeshBuilder& mb, const PaintScheme& p, const std::string& style, sim::Nation nation) {
    bool biplane = style == "soc" || style == "pete" || style == "walrus";
    bool walrus = style == "walrus";
    bool kingfisher = style == "kingfisher";  // Vought OS2U: monoplane, central float, long canopy
    float len = walrus ? 11.4f : (style == "jake" ? 11.3f : (kingfisher ? 10.3f : 9.5f));
    float span = walrus ? 13.97f : (style == "jake" ? 14.5f : (style == "pete" ? 11.0f : (kingfisher ? 10.95f : 10.97f)));
    float fz = walrus ? 0.9f : 2.1f;  // fuselage axis height
    Material skin = p.aircraft;
    Material dark = p.dark;
    mb.setMaterial(skin);
    // Fuselage (the Walrus: a boat hull).
    float tail = -len * 0.58f;
    if (walrus) {
        mb.lathe({tail, 0, fz}, {1, 0, 0}, {{0.1f, 0}, {0.45f, len * 0.35f}, {0.85f, len * 0.7f}, {0.8f, len * 0.9f}, {0.3f, len * 0.99f}, {0.0f, len}}, 12, false);
    } else {
        mb.lathe({tail, 0, fz + 0.25f}, {1, 0, 0},
                 {{0.05f, 0}, {0.18f, len * 0.15f}, {0.42f, len * 0.55f}, {0.58f, len * 0.8f}, {0.62f, len * 0.9f}, {0.6f, len * 0.95f}}, 12, false);
        // Radial engine cowling with the propeller hub.
        mb.setMaterial(p.black);
        Vec3f cowl{tail + len * 0.95f, 0, fz + 0.25f};
        mb.lathe(cowl, {1, 0, 0}, {{0.6f, 0}, {0.66f, 0.35f}, {0.55f, 0.55f}, {0.3f, 0.6f}}, 14, false);
        mb.setMaterial(skin);
        mb.lathe(cowl + Vec3f{0.55f, 0, 0}, {1, 0, 0}, {{0.16f, 0}, {0.12f, 0.2f}, {0.0f, 0.35f}}, 8, false);
        mb.setMaterial(p.black);
        for (int b = 0; b < 2; ++b) {
            float a = float(b) * float(kPi) + 0.6f;
            Vec3f d{0, std::cos(a), std::sin(a)};
            mb.orientedBox(cowl + Vec3f{0.62f, 0, 0} + d * 0.7f, {0.03f, 0.08f, 0.68f}, {1, 0, 0}, d);
        }
        // Canopy: a long glazed greenhouse on the Jake, open cockpits with windscreens on the biplanes.
        mb.setMaterial(p.glass);
        if (style == "jake" || kingfisher) {
            mb.sphere({tail + len * 0.62f, 0, fz + 0.78f}, len * 0.17f, 0.38f, 0.33f, 12, 6);
        } else {
            for (float cxp : {0.66f, 0.5f}) mb.sphere({tail + len * cxp, 0, fz + 0.82f}, 0.45f, 0.33f, 0.25f, 10, 5);
        }
    }
    mb.setMaterial(skin);
    // Wings.
    float chord = walrus ? 2.0f : (style == "jake" ? 2.3f : 1.8f);
    float wingX = tail + len * (style == "jake" ? 0.68f : 0.74f);
    float wz = biplane ? fz - 0.35f : fz + 0.05f;
    if (walrus) wz = fz + 1.0f;
    for (int s : {1, -1}) wing(mb, {wingX, 0, wz}, chord, chord * 0.82f, s * span * 0.5f, 0.15f, 0.25f, 0.12f);
    if (biplane) {
        float uz = wz + 1.75f;
        for (int s : {1, -1}) wing(mb, {wingX + 0.35f, 0, uz}, chord * 1.05f, chord * 0.9f, s * span * 0.5f, 0.25f, 0.15f, 0.12f);
        // Interplane N-struts and the centre-section cabane.
        mb.setMaterial(skin);
        for (int s : {1, -1}) {
            float yy = s * span * 0.36f;
            mb.bar({wingX - 0.25f, yy, wz + 0.1f}, {wingX - 0.05f, yy, uz}, 0.06f, 0.1f, {1, 0, 0});
            mb.bar({wingX - chord * 0.75f, yy, wz + 0.1f}, {wingX - chord * 0.55f, yy, uz}, 0.06f, 0.1f, {1, 0, 0});
            mb.bar({wingX - 0.2f, s * 0.4f, fz + 0.6f}, {wingX + 0.1f, s * 0.6f, uz}, 0.05f, 0.08f, {1, 0, 0});
        }
        mb.setMaterial(p.rope);
        for (int s : {1, -1}) mb.polyline({{wingX - 0.2f, s * 0.6f, wz + 0.1f}, {wingX - 0.1f, s * span * 0.36f, uz}}, 0.012f, 3);
    }
    if (walrus) {
        // Pusher engine nacelle between the wings.
        mb.setMaterial(skin);
        Vec3f nac{wingX - chord * 0.3f, 0, wz + 0.9f};
        mb.lathe(nac + Vec3f{-1.4f, 0, 0}, {1, 0, 0}, {{0.0f, 0}, {0.45f, 0.4f}, {0.5f, 1.2f}, {0.3f, 1.8f}, {0.0f, 2.0f}}, 12, false);
        mb.setMaterial(p.black);
        for (int b = 0; b < 4; ++b) {
            float a = float(b) * float(kPi) * 0.5f;
            Vec3f d{0, std::cos(a), std::sin(a)};
            mb.orientedBox(nac + Vec3f{-1.5f, 0, 0} + d * 0.8f, {0.03f, 0.1f, 0.78f}, {1, 0, 0}, d);
        }
    }
    // Tailplane and fin.
    mb.setMaterial(skin);
    for (int s : {1, -1}) wing(mb, {tail + 1.3f, 0, fz + 0.3f}, 1.3f, 0.8f, s * span * 0.17f, 0.3f, 0.0f, 0.1f);
    {
        std::vector<Vec3f> a, b;
        const int n = 8;
        for (int k = 0; k < n; ++k) {
            float ang = float(k) / n * float(kTwoPi);
            float t = 0.5f + 0.5f * std::cos(ang);
            float th = 0.06f * 4.0f * t * (1.0f - t) + 0.01f;
            a.push_back({tail + 1.5f - (1.0f - t) * 1.6f, std::sin(ang) >= 0 ? th : -th, fz + 0.35f});
            b.push_back({tail + 0.6f - (1.0f - t) * 0.9f, std::sin(ang) >= 0 ? th * 0.6f : -th * 0.6f, fz + 1.9f});
        }
        mb.loft(a, b, true, false);
    }
    // Floats: one central float with wingtip floats, or twin floats (Jake).
    mb.setMaterial(skin);
    auto pontoon = [&](Vec3f c, float l, float r) {
        mb.lathe(c + Vec3f{-l * 0.5f, 0, 0}, {1, 0, 0}, {{0.02f, 0}, {r * 0.6f, l * 0.25f}, {r, l * 0.55f}, {r * 0.95f, l * 0.8f}, {r * 0.4f, l * 0.97f}, {0.0f, l}}, 10, false);
    };
    if (walrus) {
        for (int s : {1, -1}) {
            pontoon({wingX - 0.6f, s * span * 0.38f, wz - 0.7f}, 1.6f, 0.22f);
            mb.bar({wingX - 0.6f, s * span * 0.38f, wz - 0.55f}, {wingX - 0.6f, s * span * 0.38f, wz}, 0.05f, 0.25f, {1, 0, 0});
        }
    } else if (style == "jake") {
        for (int s : {1, -1}) {
            Vec3f fc{tail + len * 0.62f, s * 1.75f, 0.45f};
            pontoon(fc, len * 0.62f, 0.42f);
            mb.bar(fc + Vec3f{0.8f, 0, 0.3f}, {fc.x + 0.6f, s * 0.4f, fz}, 0.07f, 0.25f, {1, 0, 0});
            mb.bar(fc + Vec3f{-0.9f, 0, 0.3f}, {fc.x - 1.1f, s * 0.4f, fz}, 0.07f, 0.25f, {1, 0, 0});
        }
    } else {
        Vec3f fc{tail + len * 0.65f, 0, 0.45f};
        pontoon(fc, len * 0.7f, 0.5f);
        mb.bar(fc + Vec3f{0.9f, 0, 0.3f}, {fc.x + 0.7f, 0, fz - 0.3f}, 0.08f, 0.3f, {1, 0, 0});
        mb.bar(fc + Vec3f{-0.8f, 0, 0.3f}, {fc.x - 1.0f, 0, fz - 0.3f}, 0.08f, 0.3f, {1, 0, 0});
        for (int s : {1, -1}) {
            Vec3f tc{wingX - 0.6f, s * span * 0.42f, wz - 0.85f};
            pontoon(tc, 1.7f, 0.24f);
            mb.bar(tc + Vec3f{0, 0, 0.15f}, tc + Vec3f{0, 0, 0.85f}, 0.05f, 0.3f, {1, 0, 0});
        }
    }
    // National markings on the upper wing surfaces and the fuselage sides.
    float topZ = biplane ? wz + 1.75f : wz;
    float mx = wingX - chord * 0.45f;
    float r = nation == sim::Nation::IJN ? 0.5f : 0.55f;
    for (int s : {1, -1}) {
        float rise = (biplane ? 0.15f : 0.25f) * 0.72f;  // dihedral at 72 % of the half span
        marking(mb, nation, {mx, s * span * 0.36f, topZ + rise + 0.15f}, {0, 0, 1}, r);
        if (!walrus) marking(mb, nation, {tail + len * 0.32f, s * 0.36f, fz + 0.28f}, {0, float(s), 0.1f}, 0.32f);
    }
    (void)dark;
}

}  // namespace

void aircraft(Ctx& cx, const sim::ModelPart& pt) {
    double xx = cx.x(pt.f);
    Vec3f at = v3(xx, pt.y, cx.deckZ(xx, pt.y) + pt.z);
    int parent = -1;
    for (const auto& c : cx.catapults)
        if (std::fabs(c.pos.x - at.x) < 3.0f && std::fabs(c.pos.y - at.y) < 1.5f) parent = c.part;
    int ai = cx.part("aircraft", PartKind::Aircraft, 0, parent);
    MeshPart& p = cx.P(ai);
    int ordinal = 0;
    for (size_t i = 0; i + 1 < cx.model.parts.size(); ++i) ordinal += cx.model.parts[i].kind == PartKind::Aircraft;
    p.sub = ordinal;
    if (parent >= 0) at.z = cx.P(parent).pivot.z + 2.45f;  // on the catapult carriage
    else cx.takeCircle(at.x, at.y, 3.0);
    p.pivot = at;  // the aircraft's own reference point (under the float) for drawing it in flight
    MeshBuilder mb(p);
    mb.setTransform(Mat4f::translation(at));
    floatplane(mb, cx.paint, pt.style, cx.c.nation);
    mb.clearTransform();
    if (parent < 0) {
        // Parked on deck: a trolley under the float.
        MeshBuilder fb = cx.at(1);
        fb.setMaterial(cx.paint.metal);
        fb.box(at + Vec3f{-2.0f, -0.6f, -0.05f}, at + Vec3f{2.0f, 0.6f, 0.25f});
    }
}

// --------------------------------------------------------------------------------------------- boats
namespace {

/// Open boat hull in a local frame: x forward, keel at z = 0, length L, beam B, depth D.
void boatHull(MeshBuilder& mb, const Material& paint, float L, float B, float D, bool transom) {
    const int ns = 12, nk = 7;
    MeshPart& part = mb.part();
    uint32_t v0 = uint32_t(part.vertices.size()), i0 = uint32_t(part.indices.size());
    mb.setMaterial(paint);
    std::vector<Vec3f> sternRing;  // local coordinates (the builder's transform applies on output)
    for (int i = 0; i <= ns; ++i) {
        float u = float(i) / ns;  // 0 stern .. 1 bow
        float x = -L * 0.5f + L * u;
        float w = B * 0.5f * std::sqrt(std::max(0.0f, 1.0f - std::pow(std::fabs(u - 0.45f) / 0.55f, 2.4f)));
        if (transom && u < 0.45f) w = B * 0.5f * (0.72f + 0.28f * u / 0.45f);
        float sheer = D * (1.0f + 0.25f * std::pow(std::fabs(u - 0.45f) / 0.55f, 2.0f));
        for (int k = 0; k <= nk; ++k) {
            float a = float(k) / nk * float(kPi);  // port gunwale .. keel .. starboard gunwale
            float yy = -std::cos(a) * w;
            float zz = sheer - std::sin(a) * sheer * (0.85f + 0.15f * std::sin(a));
            mb.vertex({x, yy, zz}, {0, 0, 1});
            if (i == 0) sternRing.push_back({x, yy, zz});
        }
    }
    uint32_t rowN = uint32_t(nk + 1);
    for (int i = 0; i < ns; ++i)
        for (int k = 0; k < nk; ++k) {
            uint32_t a = v0 + uint32_t(i) * rowN + uint32_t(k), b = a + rowN;
            mb.quad(a, b, b + 1, a + 1);
        }
    computeSmoothNormals(part, v0, i0);
    if (transom) mb.polygon(sternRing);
}

}  // namespace

void boat(Ctx& cx, const sim::ModelPart& pt) {
    double xx = cx.x(pt.f);
    float L = float(pt.len), B = std::max(1.8f, L * 0.26f), D = 0.45f * B + 0.25f;
    float x = float(xx), y = float(pt.y);
    float deckZ = float(cx.deckZ(xx, pt.y));
    float z = deckZ + float(pt.z);
    bool launch = L >= 9.0f;
    float edge = float(cx.edgeY(xx));
    bool davits = std::fabs(y) + B * 0.5f > edge - 1.2f && pt.z > 1.0;
    cx.take(x - L * 0.5f, x + L * 0.5f, y - B * 0.5f, y + B * 0.5f);
    MeshBuilder mb = cx.at(0);
    Material hullPaint = cx.paint.superstructure;
    if (cx.ijn) hullPaint = mat(srgb(150, 152, 150), 0.6f, 0.0f, kSuper);
    mb.setTransform(Mat4f::translation({x, y, z}));
    boatHull(mb, hullPaint, L, B, D, launch);
    mb.clearTransform();
    MeshBuilder fb = cx.at(1);
    fb.setTransform(Mat4f::translation({x, y, z}));
    // Gunwale, thwarts and the canvas cover (launches have a small cabin forward).
    fb.setMaterial(cx.paint.wood);
    std::vector<Vec3f> gun[2];
    for (int i = 0; i <= 12; ++i) {
        float u = float(i) / 12.0f;
        float gx = -L * 0.5f + L * u;
        float w = B * 0.5f * std::sqrt(std::max(0.0f, 1.0f - std::pow(std::fabs(u - 0.45f) / 0.55f, 2.4f)));
        if (launch && u < 0.45f) w = B * 0.5f * (0.72f + 0.28f * u / 0.45f);
        float sheer = D * (1.0f + 0.25f * std::pow(std::fabs(u - 0.45f) / 0.55f, 2.0f));
        gun[0].push_back({gx, -w, sheer + 0.03f});
        gun[1].push_back({gx, w, sheer + 0.03f});
    }
    fb.polyline(gun[0], 0.05f, 4);
    fb.polyline(gun[1], 0.05f, 4);
    if (launch) {
        fb.setMaterial(cx.paint.superstructure);
        fb.box({L * 0.05f, -B * 0.32f, D * 0.4f}, {L * 0.32f, B * 0.32f, D + 0.75f});
        fb.setMaterial(cx.paint.glass);
        fb.box({L * 0.32f, -B * 0.25f, D + 0.25f}, {L * 0.33f, B * 0.25f, D + 0.6f});
        fb.setMaterial(cx.paint.canvas);
        fb.flatQuad({-L * 0.42f, -B * 0.45f, D}, {L * 0.05f, -B * 0.45f, D}, {L * 0.05f, 0, D + 0.5f}, {-L * 0.42f, 0, D + 0.5f});
        fb.flatQuad({-L * 0.42f, 0, D + 0.5f}, {L * 0.05f, 0, D + 0.5f}, {L * 0.05f, B * 0.45f, D}, {-L * 0.42f, B * 0.45f, D});
    } else {
        fb.setMaterial(cx.paint.canvas);
        fb.flatQuad({-L * 0.45f, -B * 0.47f, D}, {L * 0.42f, -B * 0.4f, D}, {L * 0.42f, 0, D + 0.4f}, {-L * 0.45f, 0, D + 0.4f});
        fb.flatQuad({-L * 0.45f, 0, D + 0.4f}, {L * 0.42f, 0, D + 0.4f}, {L * 0.42f, B * 0.4f, D}, {-L * 0.45f, B * 0.47f, D});
    }
    fb.clearTransform();
    if (davits) {
        // Radial davits from the deck edge, curving over the boat, with the falls.
        for (float dxs : {-L * 0.36f, L * 0.36f}) {
            float s = y >= 0 ? 1.0f : -1.0f;
            Vec3f foot = v3(x + dxs, s * (edge - 0.4f), deckZ);
            Vec3f head = v3(x + dxs, y, z + D + 1.6f);
            std::vector<Vec3f> arm;
            for (int k = 0; k <= 8; ++k) {
                float t = float(k) / 8.0f;
                Vec3f p = lerp(foot, head, t);
                p.y = lerp(foot.y, head.y, t * t);
                p.z = foot.z + (head.z - foot.z + 0.6f) * std::sin(t * float(kPi) * 0.5f);
                arm.push_back(p);
            }
            fb.setMaterial(cx.paint.superstructure);
            fb.polyline(arm, 0.11f, 8);
            fb.setMaterial(cx.paint.rope);
            fb.tube(arm.back(), v3(x + dxs, y, z + D + 0.1f), 0.02f, 0.02f, 4, false);
            fb.setMaterial(cx.paint.black);
            fb.box(arm.back() - Vec3f{0.12f, 0.08f, 0.35f}, arm.back() + Vec3f{0.12f, 0.08f, 0.0f});
        }
    } else {
        // Chocks on a pair of skid beams.
        fb.setMaterial(cx.paint.superstructure);
        for (float dxs : {-L * 0.3f, L * 0.25f}) {
            fb.box(v3(x + dxs - 0.15f, y - B * 0.45f, deckZ), v3(x + dxs + 0.15f, y + B * 0.45f, z + 0.25f));
            fb.box(v3(x + dxs - 0.12f, y - B * 0.42f, z + 0.25f), v3(x + dxs + 0.12f, y - B * 0.2f, z + 0.6f));
            fb.box(v3(x + dxs - 0.12f, y + B * 0.2f, z + 0.25f), v3(x + dxs + 0.12f, y + B * 0.42f, z + 0.6f));
        }
    }
}

void rafts(Ctx& cx, const sim::ModelPart& pt) {
    double xx = cx.x(pt.f);
    Vec3f base = v3(xx, pt.y, cx.deckZ(xx, pt.y) + pt.z);
    MeshBuilder fb = cx.at(1);
    for (int k = 0; k < std::max(1, pt.levels); ++k) {
        fb.setTransform(Mat4f::translation(base + Vec3f{0, 0, 0.2f + 0.36f * k}));
        carleyFloat(fb, cx.paint, float(pt.len), float(pt.wid));
    }
    fb.clearTransform();
    cx.take(xx - pt.len * 0.5, xx + pt.len * 0.5, pt.y - pt.wid * 0.5, pt.y + pt.wid * 0.5);
}

// ------------------------------------------------------------------------------------- deck fittings
namespace {

void bitts(MeshBuilder& mb, const PaintScheme& p, Vec3f c, float r, float yaw) {
    Vec3f d{std::cos(yaw), std::sin(yaw), 0};
    mb.setMaterial(p.superstructure);
    mb.orientedBox(c + Vec3f{0, 0, 0.04f}, {r * 3.2f, r * 1.4f, 0.04f}, d);
    for (int s : {1, -1}) {
        Vec3f b = c + d * (r * 1.7f * s);
        mb.lathe(b, {0, 0, 1}, {{r, 0}, {r, r * 2.3f}, {r * 1.25f, r * 2.4f}, {r * 1.25f, r * 2.65f}, {0.0f, r * 2.7f}}, 10, false);
    }
}

void fairlead(MeshBuilder& mb, const PaintScheme& p, Vec3f c, float side) {
    mb.setMaterial(p.superstructure);
    mb.box(c + Vec3f{-0.35f, -0.12f, 0}, c + Vec3f{0.35f, 0.12f, 0.35f});
    mb.setMaterial(p.dark);
    mb.box(c + Vec3f{-0.18f, -0.13f, 0.08f}, c + Vec3f{0.18f, 0.13f, 0.26f});
    (void)side;
}

void cowlVent(MeshBuilder& mb, const PaintScheme& p, Vec3f base, float r, float h, float yaw) {
    Vec3f d{std::cos(yaw), std::sin(yaw), 0};
    mb.setMaterial(p.superstructure);
    std::vector<Vec3f> path;
    for (int k = 0; k <= 6; ++k) {
        float t = float(k) / 6.0f;
        float a = t * float(kPi) * 0.5f;
        if (k == 0) path.push_back(base);
        path.push_back(base + Vec3f{0, 0, h} + d * (r * 1.2f * (1.0f - std::cos(a))) + Vec3f{0, 0, r * 1.2f * std::sin(a)});
    }
    mb.polyline(path, r, 10);
    Vec3f mouth = path.back();
    mb.lathe(mouth, d, {{r, 0}, {r * 1.25f, r * 0.4f}, {r * 1.6f, r * 0.7f}, {r * 1.5f, r * 0.75f}}, 12, false);
    mb.setMaterial(p.dark);
    mb.disc(mouth + d * (r * 0.1f), d, r * 0.97f, 10);
}

void hatch(MeshBuilder& mb, const PaintScheme& p, Vec3f c, float l, float w) {
    mb.setMaterial(p.superstructure);
    mb.box(c + Vec3f{-l * 0.5f, -w * 0.5f, 0}, c + Vec3f{l * 0.5f, w * 0.5f, 0.35f});
    mb.box(c + Vec3f{-l * 0.52f, -w * 0.52f, 0.35f}, c + Vec3f{l * 0.52f, w * 0.52f, 0.42f});
    mb.setMaterial(p.metal);
    for (int s : {1, -1}) mb.box(c + Vec3f{-l * 0.55f, s * w * 0.3f - 0.06f, 0.3f}, c + Vec3f{-l * 0.45f, s * w * 0.3f + 0.06f, 0.42f});
}

void lifeRing(MeshBuilder& mb, Vec3f c, Vec3f n) {
    mb.setMaterial(mat(srgb(230, 228, 220), 0.7f, 0.0f, kPlain));
    mb.torus(c, n, 0.3f, 0.07f, 14, 6);
}

}  // namespace

void deckFittings(Ctx& cx) {
    MeshBuilder fb = cx.at(1);
    MeshBuilder kb = cx.at(2);
    float r = cx.destroyer ? 0.14f : 0.2f;
    // Bitts and fairleads along both sides.
    for (double u : {0.075, 0.14, 0.3, 0.46, 0.62, 0.78, 0.9, 0.955}) {
        double xx = cx.x(u);
        for (int s : {1, -1}) {
            double edge = cx.edgeY(xx);
            if (edge < 1.5) continue;
            double y = s * (edge - 0.9);
            if (!cx.isFree(xx, y, 0.9)) continue;
            Vec3f c = v3(xx, y, cx.deckZ(xx, y));
            bitts(fb, cx.paint, c, r, 0.0f);
            Vec3f f = v3(xx + 0.2, s * (edge - 0.22), cx.deckZ(xx, s * (edge - 0.22)));
            fairlead(kb, cx.paint, f, float(s));
            cx.takeCircle(xx, y, 0.9);
        }
    }
    // Cowl ventilators and hatches on the free deck between the structures.
    int vents = 0, hatches = 0;
    int wantVents = cx.destroyer ? 6 : 12, wantHatches = cx.destroyer ? 3 : 6;
    for (int attempt = 0; attempt < 400 && (vents < wantVents || hatches < wantHatches); ++attempt) {
        double u = cx.rand(0.1f, 0.92f);
        double xx = cx.x(u);
        double edge = cx.edgeY(xx);
        if (edge < 2.0) continue;
        bool vent = vents < wantVents && (attempt & 1);
        double y = cx.rand(-float(edge) * 0.65f, float(edge) * 0.65f);
        if (vent) y = (y >= 0 ? 1 : -1) * std::max(1.2, std::fabs(y));
        double need = vent ? 1.0 : 1.6;
        if (!cx.isFree(xx, y, need)) continue;
        // Keep clear of the line of fire of the mounts' barrels on the centreline.
        Vec3f c = v3(xx, y, cx.deckZ(xx, y));
        if (vent) {
            float vr = cx.destroyer ? cx.rand(0.18f, 0.28f) : cx.rand(0.25f, 0.42f);
            cowlVent(fb, cx.paint, c, vr, cx.rand(1.0f, 1.8f), cx.rand(0.0f, 1.0f) < 0.5f ? 0.0f : float(kPi));
            ++vents;
        } else if (hatches < wantHatches) {
            hatch(fb, cx.paint, c, cx.rand(1.0f, 1.6f), cx.rand(0.8f, 1.2f));
            ++hatches;
        } else {
            continue;
        }
        cx.takeCircle(xx, y, need);
    }
    // Life rings hung on the railings near the bridge and the stern.
    for (double u : {0.3, 0.93}) {
        double xx = cx.x(u);
        for (int s : {1, -1}) {
            double edge = cx.edgeY(xx);
            if (edge < 1.0) continue;
            lifeRing(kb, v3(xx, s * (edge - 0.08), cx.deckZ(xx, s * edge) + 0.65), v3(0, s, 0));
        }
    }
}

void railings(Ctx& cx) {
    MeshBuilder rb = cx.at(2);
    int rails = cx.destroyer ? 2 : 3;
    double fe = cx.c.model.forecastleEnd;
    double startX = std::min(cx.bulwarkEndX, cx.x(0.02));
    // One loop from the port side round the stern to the starboard side (split at the forecastle break).
    auto edgePts = [&](double u0, double u1, int side, bool reverse) {
        std::vector<Vec3f> pts;
        int n = std::max(2, int((u1 - u0) * cx.L / 1.4));
        for (int i = 0; i <= n; ++i) {
            double u = lerp(u0, u1, double(i) / n);
            double xx = cx.x(u);
            double e = cx.edgeY(xx);
            if (u > 0.985 && cx.c.model.sternShape <= 0.5) e = std::max(0.0, e - 0.15);
            pts.push_back(v3(xx, side * std::max(0.0, e - 0.12), cx.deckX(xx)));
        }
        if (reverse) std::reverse(pts.begin(), pts.end());
        return pts;
    };
    double uStart = cx.uOf(startX);
    if (fe > 0.0) {
        // Forecastle: from the bulwark to the break, then across the break.
        for (int s : {1, -1}) railing(rb, cx.paint, edgePts(uStart, fe - 0.006, s, false), 1.0f, rails, false);
        double bx = cx.x(fe) + 0.25;
        double e = cx.edgeY(bx);
        for (int s : {1, -1}) railing(rb, cx.paint, {v3(bx, s * (e - 0.12), cx.deckX(bx)), v3(bx, s * 1.0, cx.deckX(bx))}, 1.0f, rails, false);
        uStart = fe + 0.006;
    }
    std::vector<Vec3f> loop = edgePts(uStart, 0.999, -1, false);
    std::vector<Vec3f> stbd = edgePts(uStart, 0.999, 1, true);
    if (cx.c.model.sternShape > 0.5) {
        // Transom: straight across.
        loop.push_back(v3(cx.x(0.999) - 0.1, 0.0, cx.deck(0.999)));
    }
    loop.insert(loop.end(), stbd.begin(), stbd.end());
    railing(rb, cx.paint, loop, 1.0f, rails, false);
}

void depthCharges(Ctx& cx) {
    if (cx.c.depthCharges <= 0) return;
    MeshBuilder fb = cx.at(1);
    MeshBuilder kb = cx.at(2);
    double sternX = cx.x(0.995);
    Material drum = mat(srgb(60, 62, 64), 0.5f, 0.4f, kMetal);
    // Stern racks: inclined rails holding a row of drums.
    for (int s : {1, -1}) {
        double y = s * std::max(1.0, cx.edgeY(sternX + 3.0) * 0.45);
        double x1 = sternX + 0.3, x0 = sternX + 6.5;
        double z1 = cx.deckX(x1), z0 = cx.deckX(x0);
        fb.setMaterial(cx.paint.superstructure);
        for (int r : {1, -1}) {
            Vec3f a = v3(x0, y + r * 0.45, z0 + 1.0), b = v3(x1, y + r * 0.45, z1 + 0.55);
            fb.bar(a, b, 0.08f, 0.12f);
            fb.tube(v3(x0, y + r * 0.45, z0), a, 0.05f, 0.05f, 4, false);
            fb.tube(v3(x1, y + r * 0.45, z1), b, 0.05f, 0.05f, 4, false);
        }
        fb.setMaterial(drum);
        for (int k = 0; k < 7; ++k) {
            double t = (k + 0.5) / 7.0;
            Vec3f c = lerp(v3(x0, y, z0 + 1.35), v3(x1, y, z1 + 0.9), float(t));
            fb.tube(c - Vec3f{0, 0.35f, 0}, c + Vec3f{0, 0.35f, 0}, 0.3f, 0.3f, 10, true);
        }
        cx.take(x1, x0, y - 0.8, y + 0.8);
    }
    // Throwers (K-guns) along the after deck sides with a charge on the arbor.
    int throwers = cx.usn ? 4 : 2;
    for (int k = 0; k < throwers / 2; ++k) {
        double xx = cx.x(0.8 + 0.06 * k);
        for (int s : {1, -1}) {
            double y = s * (cx.edgeY(xx) - 1.2);
            if (!cx.isFree(xx, y, 0.8)) continue;
            Vec3f c = v3(xx, y, cx.deckZ(xx, y));
            fb.setMaterial(cx.paint.superstructure);
            fb.box(c + Vec3f{-0.4f, -0.4f, 0}, c + Vec3f{0.4f, 0.4f, 0.2f});
            Vec3f d = normalize(Vec3f{0, float(s), 1.0f});
            fb.tube(c + Vec3f{0, 0, 0.2f}, c + Vec3f{0, 0, 0.2f} + d * 0.9f, 0.13f, 0.13f, 8, true);
            fb.setMaterial(drum);
            Vec3f top = c + Vec3f{0, 0, 0.2f} + d * 1.05f;
            fb.tube(top - d * 0.1f, top + d * 0.35f, 0.08f, 0.08f, 6, false);
            fb.tube(top + d * 0.35f - Vec3f{0.35f, 0, 0}, top + d * 0.35f + Vec3f{0.35f, 0, 0}, 0.3f, 0.3f, 10, true);
            // Spare charges in a ready rack beside it.
            kb.setMaterial(drum);
            for (int q = 0; q < 3; ++q) kb.tube(c + Vec3f{-1.6f + 0.65f * q, -s * 0.6f, 0.3f} - Vec3f{0, 0.3f, 0}, c + Vec3f{-1.6f + 0.65f * q, -s * 0.6f, 0.3f} + Vec3f{0, 0.3f, 0}, 0.28f, 0.28f, 8, true);
            cx.takeCircle(xx, y, 1.6);
        }
    }
}

void ensign(Ctx& cx) {
    // Battle ensign at the gaff peak, otherwise on the stern flagstaff.
    Vec3f hoist = cx.gaffEnd;
    if (hoist.z <= 0.0f) {
        double sx = cx.x(0.995);
        MeshBuilder fb = cx.at(1);
        fb.setMaterial(cx.paint.superstructure);
        Vec3f foot = v3(sx, 0, cx.deckX(sx));
        fb.tube(foot, foot + Vec3f{-0.4f, 0, 4.0f}, 0.06f, 0.035f, 6, true);
        hoist = foot + Vec3f{-0.45f, 0, 4.0f};
    }
    // Pattern ids (see ship.hlsl): 1 Stars and Stripes, 2 Rising Sun, 3 White Ensign, 4 Italian naval
    // ensign, 5 German war ensign (shown without its prohibited emblem).
    float w = cx.destroyer ? 2.0f : 3.0f, h = w * (cx.ijn || cx.rm ? 0.667f : (cx.km ? 0.6f : 0.53f));
    uint8_t flagId = cx.usn ? 1 : (cx.ijn ? 2 : (cx.rm ? 4 : (cx.km ? 5 : 3)));
    int fi = cx.part("ensign", PartKind::Flag, 1);
    MeshPart& p = cx.P(fi);
    p.pivot = hoist;
    const int nu = 12, nv = 6;
    Material m = mat({1, 1, 1}, 0.8f, 0.0f, kFlag);
    uint32_t params = m.packParams();
    for (int side = 0; side < 2; ++side) {
        uint32_t base = uint32_t(p.vertices.size());
        for (int j = 0; j <= nv; ++j)
            for (int i = 0; i <= nu; ++i) {
                float u = float(i) / nu, v = float(j) / nv;
                Vertex vx;
                vx.pos = hoist + Vec3f{-u * w, side ? -0.002f : 0.002f, -v * h - 0.1f};
                vx.normal = {0, side ? -1.0f : 1.0f, 0};
                // Flag UV and pattern id travel in the vertex colour (see ship.hlsl).
                vx.color = uint32_t(u * 255.0f + 0.5f) | (uint32_t(v * 255.0f + 0.5f) << 8) | (uint32_t(flagId) << 16) | (255u << 24);
                vx.params = params;
                p.vertices.push_back(vx);
            }
        for (int j = 0; j < nv; ++j)
            for (int i = 0; i < nu; ++i) {
                uint32_t a = base + uint32_t(j * (nu + 1) + i), b = a + 1, c = a + uint32_t(nu + 1), d = c + 1;
                if (side) {
                    p.indices.insert(p.indices.end(), {a, c, d, a, d, b});
                } else {
                    p.indices.insert(p.indices.end(), {a, b, d, a, d, c});
                }
            }
    }
    // Halyard from the hoist down to a cleat.
    MeshBuilder kb = cx.at(2);
    kb.setMaterial(cx.paint.rope);
    kb.tube(hoist, hoist - Vec3f{0, 0, h + 0.2f}, 0.012f, 0.012f, 3, false);
}

void recognitionStripes(Ctx& cx) {
    if (!cx.rm) return;
    // From the stem back to the first turret: diagonal bands, all slanting the same way.
    double fwdMount = -1e9;
    for (const auto& m : cx.c.mounts)
        if (std::fabs(m.y) < 1.0) fwdMount = std::max(fwdMount, m.x);
    double xEnd = fwdMount > -1e8 ? fwdMount + (cx.destroyer ? 3.5 : 5.5) : cx.x(0.18);
    double xStart = cx.x(0.012);
    if (xStart - xEnd < 6.0) return;
    float band = cx.destroyer ? 1.6f : 2.4f;
    double half = cx.B * 0.5, step = 0.35;
    MeshBuilder mb = cx.at(1);
    Material red = mat(srgb(152, 32, 30), 0.82f, 0.0f, kPlain), white = mat(srgb(214, 211, 202), 0.82f, 0.0f, kPlain);
    auto inside = [&](double xx, double yy) { return xx >= xEnd && xx <= xStart && std::fabs(yy) < cx.edgeY(xx) - 0.25; };
    auto at = [&](double xx, double yy) { return v3(xx, yy, cx.deckZ(xx, yy) + 0.03); };
    int k = 0;
    for (double d0 = xEnd - half; d0 < xStart + half; d0 += band, ++k) {
        mb.setMaterial(k % 2 ? white : red);
        for (double y0 = -half; y0 < half; y0 += step) {
            double y1 = y0 + step;
            // Band edges: x + y = d0 and x + y = d0 + band.
            double a0 = d0 - y0, a1 = d0 - y1, b0 = d0 + band - y0, b1 = d0 + band - y1;
            if (!inside(a0, y0) || !inside(a1, y1) || !inside(b0, y0) || !inside(b1, y1)) continue;
            mb.flatQuad(at(a0, y0), at(b0, y0), at(b1, y1), at(a1, y1));
        }
    }
}

}  // namespace it::procgen::detail
