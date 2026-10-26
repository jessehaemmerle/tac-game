// Hull: lofted side plating with the boot topping, armour belt or bulge, cambered weather deck with
// steel waterways, bulwarks and breakwater, anchors in their hawse pipes, degaussing cable, and the
// underwater body (bilge keels, shafts, struts, propellers, rudders).
#include "procgen/ShipModelInternal.h"

namespace it::procgen::detail {
namespace {

std::vector<double> hullStations(const Ctx& cx) {
    std::vector<double> us;
    int n = std::max(24, cx.opt.stations);
    for (int i = 0; i <= n; ++i) us.push_back(0.5 - 0.5 * std::cos(kPi * i / n));
    double fe = cx.c.model.forecastleEnd;
    if (fe > 0.0) {
        us.push_back(fe - 0.004);
        us.push_back(fe + 0.004);
    }
    std::sort(us.begin(), us.end());
    us.erase(std::unique(us.begin(), us.end(), [](double a, double b) { return std::fabs(a - b) < 1e-4; }), us.end());
    return us;
}

double camber(const Ctx& cx) { return std::min(0.35, cx.B * 0.015); }

// Outward hull normal at model x / height z (finite differences of the side plating).
Vec3f sideNormal(const Ctx& cx, double xx, double z, int side) {
    double e = 0.25;
    double y0 = cx.sideY(xx, z), yx = cx.sideY(xx + e, z), yz = cx.sideY(xx, z + e);
    Vec3f tx = v3(e, side * (yx - y0), 0), tz = v3(0, side * (yz - y0), e);
    Vec3f n = normalize(cross(tz, tx));
    if (n.y * side < 0) n = -n;
    return n;
}

// Band of side plating between heights lo and hi (clamped to the hull at each station).
void sideBand(Ctx& cx, MeshBuilder& mb, const std::vector<double>& us, double lo, double hi, int K) {
    MeshPart& part = mb.part();
    for (int side : {1, -1}) {
        uint32_t v0 = uint32_t(part.vertices.size()), i0 = uint32_t(part.indices.size());
        for (double u : us) {
            double keel = keelDepth(cx.c, u), top = cx.deck(u);
            double z0 = clamp(lo, -keel, top), z1 = clamp(hi, -keel, top);
            for (int j = 0; j < K; ++j) {
                double z = lerp(z0, z1, double(j) / (K - 1));
                double xx = cx.x(u) + bowRakeOffset(cx, u, z);
                mb.vertex(v3(xx, side * hullHalfWidth(cx.c, u, z), z), {0, 0, 1});
            }
        }
        for (size_t i = 0; i + 1 < us.size(); ++i)
            for (int j = 0; j + 1 < K; ++j) {
                uint32_t a = v0 + uint32_t(i * K + j), b = v0 + uint32_t((i + 1) * K + j);
                uint32_t c = b + 1, d = a + 1;
                if (side > 0) mb.quad(a, b, c, d);
                else mb.quad(a, d, c, b);
            }
        computeSmoothNormals(part, v0, i0);
    }
}

struct BeltSpec {
    double u0 = 0, u1 = 0, zLo = 0, zHi = 0, offset = 0;
    bool rounded = false;  // bulge: the top curves into the hull
};

BeltSpec beltSpec(const Ctx& cx) {
    BeltSpec b;
    if (cx.capital) {
        // Japanese battleships: deep bulges added in their reconstructions; US fast battleships carried
        // their belt inside the hull.
        if (cx.ijn || cx.rn) b = {0.16, 0.8, -cx.T * 0.8, 1.2, 0.7, true};
        return b;
    }
    if (!cx.cruiser) return b;
    if (cx.ijn) b = {0.2, 0.78, -cx.T * 0.75, 0.9, 0.38, true};          // anti-torpedo bulge
    else if (cx.rn) b = {0.24, 0.74, -cx.T * 0.7, 1.1, 0.3, true};      // County class bulges
    else b = {0.26, 0.72, -1.6, 1.5, std::max(0.06, cx.c.armor.belt / 1000.0 * 0.7), false};  // external belt
    return b;
}

// Armour belt or bulge: a raised strip of plating over the hull with sloped ends and top.
void belt(Ctx& cx, MeshBuilder& mb, const std::vector<double>& usAll) {
    BeltSpec b = beltSpec(cx);
    if (b.offset <= 0) return;
    std::vector<double> us;
    for (double u : usAll)
        if (u > b.u0 && u < b.u1) us.push_back(u);
    us.insert(us.begin(), b.u0);
    us.push_back(b.u1);
    double ramp = 0.03;
    auto off = [&](double u, double z) {
        double e = smoothstep(b.u0, b.u0 + ramp, u) * (1.0 - smoothstep(b.u1 - ramp, b.u1, u));
        double t = clamp((z - b.zLo) / (b.zHi - b.zLo), 0.0, 1.0);
        double prof = b.rounded ? std::sqrt(std::max(0.0, std::sin(t * kPi))) : 1.0;  // a bulge is round in section
        return b.offset * e * prof;
    };
    struct Band {
        double lo, hi;
        const Material* m;
    } bands[] = {{b.zLo, -0.35, &cx.paint.bottom}, {-0.35, 0.45, &cx.paint.boot}, {0.45, b.zHi, &cx.paint.hull}};
    MeshPart& part = mb.part();
    for (auto& band : bands) {
        double lo = std::max(band.lo, b.zLo), hi = std::min(band.hi, b.zHi);
        if (hi <= lo) continue;
        mb.setMaterial(*band.m);
        int K = std::max(2, int((hi - lo) / 0.4) + 2);
        for (int side : {1, -1}) {
            uint32_t v0 = uint32_t(part.vertices.size()), i0 = uint32_t(part.indices.size());
            for (double u : us)
                for (int j = 0; j < K; ++j) {
                    double z = lerp(lo, hi, double(j) / (K - 1));
                    mb.vertex(v3(cx.x(u), side * (hullHalfWidth(cx.c, u, z) + off(u, z)), z), {0, 0, 1});
                }
            for (size_t i = 0; i + 1 < us.size(); ++i)
                for (int j = 0; j + 1 < K; ++j) {
                    uint32_t a = v0 + uint32_t(i * K + j), bb = v0 + uint32_t((i + 1) * K + j);
                    if (side > 0) mb.quad(a, bb, bb + 1, a + 1);
                    else mb.quad(a, a + 1, bb + 1, bb);
                }
            computeSmoothNormals(part, v0, i0);
        }
    }
    if (!b.rounded) {
        // Flat belt: a ledge along its top edge.
        mb.setMaterial(cx.paint.hull);
        for (size_t i = 0; i + 1 < us.size(); ++i)
            for (int side : {1, -1}) {
                double ua = us[i], ub = us[i + 1];
                Vec3f a0 = v3(cx.x(ua), side * hullHalfWidth(cx.c, ua, b.zHi), b.zHi);
                Vec3f a1 = v3(cx.x(ua), side * (hullHalfWidth(cx.c, ua, b.zHi) + off(ua, b.zHi)), b.zHi);
                Vec3f b0 = v3(cx.x(ub), side * hullHalfWidth(cx.c, ub, b.zHi), b.zHi);
                Vec3f b1 = v3(cx.x(ub), side * (hullHalfWidth(cx.c, ub, b.zHi) + off(ub, b.zHi)), b.zHi);
                mb.flatQuad(a0, b0, b1, a1);
            }
    }
}

void weatherDeck(Ctx& cx, MeshBuilder& mb, const std::vector<double>& us) {
    MeshPart& part = mb.part();
    double cam = camber(cx);
    Material water = cx.paint.deck;
    water.id = kDeckSteel;
    water.albedo = water.albedo * 0.92f;
    // Three strips: port waterway, planked/linoleum deck, starboard waterway (own vertices per material).
    for (int strip = 0; strip < 3; ++strip) {
        mb.setMaterial(strip == 1 ? cx.paint.deck : water);
        const int cols = strip == 1 ? 7 : 2;
        uint32_t v0 = uint32_t(part.vertices.size()), i0 = uint32_t(part.indices.size());
        for (double u : us) {
            double z = cx.deck(u);
            double hw = hullHalfWidth(cx.c, u, z);
            double ww = std::min(0.45, hw * 0.25);
            double xx = cx.x(u) + bowRakeOffset(cx, u, z);
            for (int k = 0; k < cols; ++k) {
                double t = double(k) / (cols - 1);
                double y;
                if (strip == 0) y = lerp(-hw, -(hw - ww), t);
                else if (strip == 2) y = lerp(hw - ww, hw, t);
                else y = lerp(-(hw - ww), hw - ww, t);
                double r = hw > 1e-3 ? y / hw : 0.0;
                mb.vertex(v3(xx, y, z + cam * (1.0 - r * r)), {0, 0, 1});
            }
        }
        for (size_t i = 0; i + 1 < us.size(); ++i)
            for (int k = 0; k + 1 < cols; ++k) {
                uint32_t a = v0 + uint32_t(i * cols + k), b = v0 + uint32_t((i + 1) * cols + k);
                mb.quad(a, b, b + 1, a + 1);
            }
        computeSmoothNormals(part, v0, i0);
    }
}

void transom(Ctx& cx, MeshBuilder& mb) {
    if (cx.c.model.sternShape <= 0.5) return;
    double u = 1.0, keel = keelDepth(cx.c, u), top = cx.deck(u);
    mb.setMaterial(cx.paint.hull);
    Vec3f center = v3(cx.x(u), 0.0, top * 0.5 - keel * 0.5);
    uint32_t cIdx = mb.vertex(center, {-1, 0, 0});
    std::vector<uint32_t> ring;
    for (int j = 0; j <= 16; ++j) {
        double z = lerp(-keel, top, double(j) / 16);
        ring.push_back(mb.vertex(v3(cx.x(u), hullHalfWidth(cx.c, u, z), z), {-1, 0, 0}));
    }
    for (int j = 16; j >= 0; --j) {
        double z = lerp(-keel, top, double(j) / 16);
        ring.push_back(mb.vertex(v3(cx.x(u), -hullHalfWidth(cx.c, u, z), z), {-1, 0, 0}));
    }
    for (size_t k = 0; k + 1 < ring.size(); ++k) mb.tri(cIdx, ring[k + 1], ring[k]);
}

void forecastleBreak(Ctx& cx, MeshBuilder& mb) {
    double fe = cx.c.model.forecastleEnd;
    if (fe <= 0.0) return;
    mb.setMaterial(cx.paint.hull);
    double zl = cx.deck(fe + 0.01), zh = cx.deck(fe - 0.01);
    double hw = hullHalfWidth(cx.c, fe, zl), xx = cx.x(fe);
    double cam = camber(cx);
    // Bulkhead following the deck camber.
    const int n = 8;
    for (int k = 0; k < n; ++k) {
        double ya = lerp(-hw, hw, double(k) / n), yb = lerp(-hw, hw, double(k + 1) / n);
        double ca = cam * (1 - (ya / hw) * (ya / hw)), cb = cam * (1 - (yb / hw) * (yb / hw));
        mb.flatQuad(v3(xx, ya, zl + ca), v3(xx, yb, zl + cb), v3(xx, yb, zh + cb), v3(xx, ya, zh + ca));
    }
    // Doors and portholes on the break (close-ups).
    MeshBuilder fb = cx.at(2);
    Material dark = mat(srgb(14, 16, 18), 0.2f, 0.0f, kGlass);
    for (int side : {1, -1}) {
        double yd = side * hw * 0.45;
        fb.setMaterial(cx.paint.superstructure);
        fb.box(v3(xx - 0.06, yd - 0.4, zl + 0.15), v3(xx + 0.02, yd + 0.4, zl + 1.85));
        fb.setMaterial(dark);
        for (double yp : {side * hw * 0.2, side * hw * 0.72})
            fb.disc(v3(xx - 0.07, yp, zl + std::min(1.5, (zh - zl) * 0.7)), {-1, 0, 0}, 0.16f, 10);
    }
}

}  // namespace

double bowRakeOffset(const Ctx& cx, double u, double z) {
    double keel = keelDepth(cx.c, u), top = cx.deck(u);
    double t = clamp((z + keel) / std::max(0.1, top + keel), 0.0, 1.0);
    return cx.c.model.bowRake * cx.L * std::pow(t, 1.5) * std::max(0.0, 1.0 - u / 0.07);
}

void buildHull(Ctx& cx) {
    MeshBuilder mb(cx.P(cx.hull));
    std::vector<double> us = hullStations(cx);
    double top = std::max({cx.c.model.freeboardBow, cx.c.model.freeboardMid, cx.c.model.freeboardStern}) + 4.0;
    mb.setMaterial(cx.paint.bottom);
    sideBand(cx, mb, us, -100.0, -0.35, 10);
    mb.setMaterial(cx.paint.boot);
    sideBand(cx, mb, us, -0.35, 0.45, 3);
    mb.setMaterial(cx.paint.hull);
    sideBand(cx, mb, us, 0.45, top, 9);
    belt(cx, mb, us);
    weatherDeck(cx, mb, us);
    transom(cx, mb);
    forecastleBreak(cx, mb);
    cx.roofs.push_back({float(cx.x(1.0)), float(cx.x(0.0)), float(-cx.B * 0.5), float(cx.B * 0.5), 0.0f});
}

// ------------------------------------------------------------------------------------- hull fittings
namespace {

void gunwale(Ctx& cx) {
    // Rounded deck-edge angle along both sides (crisp silhouette at the sheer line).
    MeshBuilder mb = cx.at(1);
    mb.setMaterial(cx.paint.hull);
    std::vector<double> us = hullStations(cx);
    for (int side : {1, -1}) {
        std::vector<Vec3f> pts;
        double fe = cx.c.model.forecastleEnd;
        auto flush = [&]() {
            if (pts.size() >= 2) mb.polyline(pts, 0.07f, 5, false);
            pts.clear();
        };
        for (double u : us) {
            if (u < 0.004) continue;
            if (fe > 0.0 && std::fabs(u - fe) < 0.0039) {
                flush();
                continue;
            }
            double z = cx.deck(u);
            double xx = cx.x(u) + bowRakeOffset(cx, u, z);
            pts.push_back(v3(xx, side * (hullHalfWidth(cx.c, u, z) + 0.02), z + 0.03));
        }
        flush();
    }
}

void bulwarks(Ctx& cx) {
    // Bow bulwark: the side plating carried up above the forecastle deck, tapering off aft.
    double len = cx.destroyer ? 0.085 : 0.06;
    double h = cx.destroyer ? 0.95 : 1.1;
    cx.bulwarkEndX = cx.x(len);
    MeshBuilder mb(cx.P(cx.hull));
    mb.setMaterial(cx.paint.hull);
    MeshPart& part = mb.part();
    const int n = 16;
    for (int side : {1, -1}) {
        uint32_t v0 = uint32_t(part.vertices.size()), i0 = uint32_t(part.indices.size());
        for (int i = 0; i <= n; ++i) {
            double u = 0.002 + (len - 0.002) * double(i) / n;
            double z = cx.deck(u);
            double hh = h * (1.0 - smoothstep(0.75, 1.0, double(i) / n));
            for (int j = 0; j < 2; ++j) {
                double zz = z + hh * j;
                double xx = cx.x(u) + bowRakeOffset(cx, u, z) + (j ? 0.04 * hh : 0.0);
                double y = hullHalfWidth(cx.c, u, z) * (1.0 + 0.06 * j * hh / h);
                mb.vertex(v3(xx, side * y, zz), {0, 0, 1});
            }
        }
        for (int i = 0; i < n; ++i) {
            uint32_t a = v0 + uint32_t(i * 2), b = a + 2;
            if (side > 0) mb.quad(a, b, b + 1, a + 1);
            else mb.quad(a, a + 1, b + 1, b);
        }
        computeSmoothNormals(part, v0, i0);
        // Capping rail.
        std::vector<Vec3f> cap;
        for (int i = 0; i <= n; ++i) {
            const Vertex& v = part.vertices[v0 + uint32_t(i * 2 + 1)];
            cap.push_back(v.pos);
        }
        MeshBuilder fb = cx.at(1);
        fb.setMaterial(cx.paint.hull);
        fb.polyline(cap, 0.06f, 5);
    }
}

void breakwater(Ctx& cx) {
    // V-shaped breakwater on the forecastle ahead of the first mount.
    const sim::MountDef* first = nullptr;
    for (auto& m : cx.c.mounts)
        if (std::fabs(m.y) < 0.5 && m.x > 0 && (!first || m.x > first->x)) first = &m;
    if (!first || cx.c.type == sim::ShipType::AP) return;
    double r = cx.destroyer ? 3.0 : 5.0;
    double apex = first->x + r + (cx.destroyer ? 2.0 : 3.0);
    double hw = cx.edgeY(apex - 2.0) * 0.82;
    double back = hw * 0.65;
    double h = cx.destroyer ? 0.9 : 1.3;
    MeshBuilder mb(cx.P(cx.hull));
    mb.setMaterial(cx.paint.superstructure);
    MeshBuilder fb = cx.at(1);
    fb.setMaterial(cx.paint.superstructure);
    for (int side : {1, -1}) {
        Vec3f a = v3(apex, 0, cx.deckZ(apex, 0) - 0.05), b = v3(apex - back, side * hw, cx.deckZ(apex - back, side * hw) - 0.05);
        Vec3f at = a + Vec3f{-0.3f, 0, float(h)}, bt = b + Vec3f{-0.3f, 0, float(h * 0.85)};
        mb.flatQuad(a, b, bt, at);
        fb.polyline({at, bt}, 0.05f, 5);
        // Stiffening brackets on the after face.
        for (int k = 1; k < 5; ++k) {
            float t = float(k) / 5.0f;
            Vec3f base = lerp(a, b, t);
            Vec3f d{-1.0f, 0.0f, 0.0f};
            float hh = float(h) * (1.0f - 0.15f * t);
            fb.flatTri(base + Vec3f{-0.02f, 0, 0}, base + d * 0.9f, base + Vec3f{-0.3f, 0, hh * 0.92f});
        }
    }
    cx.take(apex - back - 0.5, apex + 0.5, -hw, hw);
}

// Stockless bower anchor, shank along local z (up), flukes at z = 0, flat in the local x/z plane.
void anchorShape(MeshBuilder& mb, float s) {
    mb.bar({0, 0, 0.15f * s}, {0, 0, 1.0f * s}, 0.16f * s, 0.12f * s, {1, 0, 0});           // shank
    mb.box({-0.32f * s, -0.12f * s, 0.0f}, {0.32f * s, 0.12f * s, 0.2f * s});                 // crown
    for (int side : {1, -1}) {
        float sx = float(side);
        Vec3f root{sx * 0.28f * s, 0, 0.1f * s}, tip{sx * 0.42f * s, 0, 0.62f * s}, inner{sx * 0.12f * s, 0, 0.42f * s};
        Vec3f o{0, 0.06f * s, 0};
        mb.flatTri(root + o, tip + o, inner + o);
        mb.flatTri(root - o, inner - o, tip - o);
        mb.flatQuad(root - o, tip - o, tip + o, root + o);
    }
    mb.torus({0, 0, 1.05f * s}, {0, 1, 0}, 0.12f * s, 0.035f * s, 10, 5);  // shackle
}

void anchors(Ctx& cx) {
    double u = cx.destroyer ? 0.05 : 0.04;
    double xx = cx.x(u);
    double zH = cx.deckX(xx) - (cx.destroyer ? 1.4 : 2.0);
    float s = cx.destroyer ? 1.1f : (cx.c.displacementStd > 8000 ? 1.9f : 1.5f);
    Material black = mat(srgb(30, 30, 31), 0.55f, 0.5f, kMetal);
    Material dark = mat(srgb(6, 6, 7), 0.9f, 0.0f, kPlain);
    for (int side : {1, -1}) {
        double y = cx.sideY(xx, zH);
        Vec3f n = sideNormal(cx, xx, zH, side);
        Vec3f p = v3(xx, side * y, zH);
        MeshBuilder fb = cx.at(1);
        // Hawse pipe: dark opening with a rim.
        fb.setMaterial(dark);
        fb.disc(p + n * 0.03f, n, 0.32f * s * 0.6f, 12);
        fb.setMaterial(cx.paint.hull);
        fb.torus(p + n * 0.04f, n, 0.34f * s * 0.6f, 0.06f, 14, 5);
        // Anchor hanging from the pipe, lying against the flared plating.
        Vec3f up = normalize(Vec3f{0, -n.y * 0.25f, 1.0f});
        Vec3f out = n;
        Vec3f fwd = normalize(cross(up, out)) * float(side);
        Mat4f m;
        m.m[0][0] = fwd.x; m.m[0][1] = fwd.y; m.m[0][2] = fwd.z;
        Vec3f ly = normalize(cross(up, fwd));
        m.m[1][0] = ly.x; m.m[1][1] = ly.y; m.m[1][2] = ly.z;
        m.m[2][0] = up.x; m.m[2][1] = up.y; m.m[2][2] = up.z;
        Vec3f origin = p + n * 0.22f - up * (1.0f * s);
        m.m[3][0] = origin.x; m.m[3][1] = origin.y; m.m[3][2] = origin.z;
        fb.setMaterial(black);
        fb.setTransform(m);
        anchorShape(fb, s);
        fb.clearTransform();
        // Deck end of the hawse pipe and the cable to the windlass.
        double hx = xx - 0.8, hy = side * (cx.edgeY(hx) - 1.0);
        Vec3f deckEnd = v3(hx, hy, cx.deckZ(hx, hy));
        fb.setMaterial(cx.paint.metal);
        fb.ellipticCylinder(deckEnd, 0.45f, 0.38f, 0.22f, 12, 0.0f, 1.0f, false, false);
        fb.setMaterial(dark);
        fb.disc(deckEnd + Vec3f{0, 0, 0.2f}, {0, 0, 1}, 0.32f, 10);
        double wx = cx.x(u + (cx.destroyer ? 0.045 : 0.05));
        Vec3f wl = v3(wx, side * 0.9, cx.deckZ(wx, side * 0.9) + 0.45);
        Vec3f navel = v3(wx - 2.2, side * 0.9, cx.deckZ(wx - 2.2, side * 0.9));
        // Chain cable: alternating links (close-ups).
        MeshBuilder cb = cx.at(2);
        cb.setMaterial(black);
        auto chain = [&](Vec3f a, Vec3f b) {
            Vec3f d = b - a;
            float len = length(d);
            if (len < 0.1f) return;
            Vec3f t = d / len;
            float link = 0.12f * s;
            int nl = int(len / link);
            Vec3f side1 = normalize(cross(t, Vec3f{0, 0, 1}));
            for (int k = 0; k < nl; ++k) {
                Vec3f c = a + t * (link * (k + 0.5f));
                Vec3f ax = (k & 1) ? side1 : normalize(cross(t, side1));
                cb.torus(c, ax, 0.055f * s, 0.018f * s, 8, 4);
            }
        };
        chain(deckEnd + Vec3f{0, 0, 0.18f}, wl);
        chain(wl, navel + Vec3f{0, 0, 0.05f});
        fb.setMaterial(cx.paint.metal);
        fb.ellipticCylinder(navel, 0.35f, 0.35f, 0.15f, 10, 0.0f, 1.0f, true, false);
    }
    // Windlass: two capstans with the cable lifters.
    MeshBuilder fb = cx.at(1);
    double wx = cx.x(u + (cx.destroyer ? 0.045 : 0.05));
    for (int side : {1, -1}) {
        Vec3f base = v3(wx, side * 0.9, cx.deckZ(wx, side * 0.9));
        fb.setMaterial(cx.paint.metal);
        float r = cx.destroyer ? 0.35f : 0.5f;
        fb.lathe(base, {0, 0, 1}, {{r * 1.3f, 0}, {r * 1.3f, 0.08f}, {r * 0.85f, 0.12f}, {r * 0.75f, 0.45f}, {r * 0.85f, 0.7f}, {r * 1.05f, 0.75f}, {r * 1.0f, 0.85f}, {0.0f, 0.9f}}, 14, true);
        // Whelps on the barrel.
        for (int k = 0; k < 8; ++k) {
            float a = float(k) / 8 * float(kTwoPi);
            Vec3f d{std::cos(a), std::sin(a), 0};
            fb.bar(base + d * (r * 0.78f) + Vec3f{0, 0, 0.14f}, base + d * (r * 0.78f) + Vec3f{0, 0, 0.66f}, 0.06f, 0.05f, d);
        }
        cx.takeCircle(base.x, base.y, r * 1.4);
    }
    fb.setMaterial(cx.paint.superstructure);
    fb.box(v3(wx - 0.9, -0.5, cx.deckZ(wx, 0)), v3(wx + 0.2, 0.5, cx.deckZ(wx, 0) + 0.6));  // windlass engine casing
    // Jackstaff at the stem.
    double sx = cx.x(0.0) + bowRakeOffset(cx, 0.0, cx.deck(0.0)) - 0.6;
    fb.setMaterial(cx.paint.superstructure);
    fb.tube(v3(sx, 0, cx.deck(0.0)), v3(sx - 0.3, 0, cx.deck(0.0) + 3.2), 0.06f, 0.03f, 6, true);
}

void degaussingCable(Ctx& cx) {
    if (cx.ijn) return;  // external cables were fitted by the USN and RN
    MeshBuilder fb = cx.at(1);
    fb.setMaterial(cx.paint.hull);
    for (int side : {1, -1}) {
        std::vector<Vec3f> pts;
        for (int i = 0; i <= 80; ++i) {
            double u = 0.03 + 0.94 * double(i) / 80;
            double xx = cx.x(u);
            double z = cx.deckX(xx) - 1.1;
            if (cx.c.model.forecastleEnd > 0 && u > cx.c.model.forecastleEnd) z = std::min(z, cx.deckX(xx) - 0.9);
            pts.push_back(v3(xx, side * (cx.sideY(xx, z) + 0.07), z));
        }
        fb.polyline(pts, 0.07f, 5);
        // Clamps.
        MeshBuilder kb = cx.at(2);
        kb.setMaterial(cx.paint.hull);
        for (size_t i = 2; i + 2 < pts.size(); i += 3) kb.box(pts[i] - Vec3f{0.05f, 0.09f, 0.1f}, pts[i] + Vec3f{0.05f, 0.09f, 0.1f});
    }
}

}  // namespace

void buildHullFittings(Ctx& cx) {
    gunwale(cx);
    bulwarks(cx);
    if (!cx.opt.details) return;
    breakwater(cx);
    anchors(cx);
    degaussingCable(cx);
}

// --------------------------------------------------------------------------------- underwater body
namespace {

void bilgeKeels(Ctx& cx) {
    MeshBuilder fb = cx.at(1);
    fb.setMaterial(cx.paint.bottom);
    for (int side : {1, -1}) {
        std::vector<Vec3f> in, out;
        for (int i = 0; i <= 12; ++i) {
            double u = lerp(0.32, 0.66, double(i) / 12);
            double z = -keelDepth(cx.c, u) * 0.72;
            double y = hullHalfWidth(cx.c, u, z);
            double d = (i == 0 || i == 12) ? 0.05 : 0.55;
            in.push_back(v3(cx.x(u), side * (y - 0.05), z));
            out.push_back(v3(cx.x(u), side * (y + d * 0.7), z - d * 0.7));
        }
        for (size_t i = 0; i + 1 < in.size(); ++i) fb.flatQuad(in[i], in[i + 1], out[i + 1], out[i]);
    }
}

void propeller(Ctx& cx, const Vec3f& hub, float r, int blades, int handed, int index) {
    int pi = cx.part("propeller_" + std::to_string(index), PartKind::Propeller, 1);
    MeshPart& p = cx.P(pi);
    p.pivot = hub;
    p.axis = {1, 0, 0};
    p.sub = handed > 0 ? 0 : 1;
    MeshBuilder mb(p);
    mb.setMaterial(mat(srgb(150, 110, 60), 0.35f, 0.9f, kPlain));  // manganese bronze
    mb.lathe(hub + Vec3f{r * 0.28f, 0, 0}, {-1, 0, 0}, {{0.0f, -0.05f}, {r * 0.16f, 0.0f}, {r * 0.18f, r * 0.28f}, {r * 0.12f, r * 0.42f}, {0.0f, r * 0.5f}}, 12, false);
    for (int b = 0; b < blades; ++b) {
        float a0 = float(b) / blades * float(kTwoPi);
        // Blade: twisted, skewed plate from the hub to the tip.
        const int n = 6;
        std::vector<Vec3f> lead, trail;
        for (int k = 0; k <= n; ++k) {
            float t = float(k) / n;
            float rr = lerp(r * 0.17f, r, t);
            float chord = r * (0.55f * std::sin(0.25f + t * 2.6f) + 0.12f) * (1.0f - 0.6f * t * t);
            float pitch = 0.9f - 0.45f * t;  // blade angle (rad) from the disc plane
            float skew = 0.25f * t * t * float(handed);
            float a = a0 + skew;
            Vec3f radial{0, std::cos(a), std::sin(a)};
            Vec3f tang{0, -std::sin(a), std::cos(a)};
            Vec3f c = hub + radial * rr;
            Vec3f dir = normalize(tang * std::cos(pitch) * float(handed) + Vec3f{1, 0, 0} * std::sin(pitch));
            lead.push_back(c + dir * (chord * 0.5f));
            trail.push_back(c - dir * (chord * 0.5f));
        }
        for (int k = 0; k < n; ++k) mb.flatQuad(trail[size_t(k)], trail[size_t(k + 1)], lead[size_t(k + 1)], lead[size_t(k)]);
    }
}

void rudder(Ctx& cx, double xx, double y, double zTop, double zBot, double chord, int index) {
    int ri = cx.part("rudder_" + std::to_string(index), PartKind::Rudder, 1);
    MeshPart& p = cx.P(ri);
    p.pivot = v3(xx, y, zTop);  // balanced rudder: the stock at a quarter chord
    MeshBuilder mb(p);
    mb.setMaterial(cx.paint.bottom);
    // Streamlined section, slightly tapered towards the sole.
    std::vector<Vec2f> sec;
    for (int k = 0; k < 12; ++k) {
        float a = float(k) / 12 * float(kTwoPi);
        float c = std::cos(a);
        float t = 0.5f - 0.5f * c;  // 0 leading edge .. 1 trailing edge
        float th = 0.12f * float(chord) * std::sqrt(std::max(0.0f, t)) * (1.0f - t) * 2.2f;
        sec.push_back({float(chord) * (0.25f - t), th * (std::sin(a) >= 0 ? 1.0f : -1.0f)});
    }
    std::vector<Vec3f> top, bot;
    for (auto& s : sec) {
        top.push_back(v3(xx + s.x, y + s.y, zTop));
        bot.push_back(v3(xx + s.x * 0.85 + chord * 0.03, y + s.y * 0.85, zBot));
    }
    mb.loft(bot, top, true, true);
    mb.setMaterial(cx.paint.metal);
    mb.tube(v3(xx, y, zTop), v3(xx, y, zTop + 1.2), 0.12f, 0.12f, 8, false);  // stock
}

}  // namespace

void buildUnderwater(Ctx& cx) {
    if (!cx.opt.details) return;
    bilgeKeels(cx);
    int shafts = std::max(1, cx.c.shafts);
    std::vector<double> ys;
    if (shafts >= 4) ys = {-0.32, -0.15, 0.15, 0.32};
    else if (shafts == 3) ys = {-0.26, 0.0, 0.26};
    else if (shafts == 2) ys = {-0.21, 0.21};
    else ys = {0.0};
    double r = std::min(cx.T * 0.42, cx.destroyer ? 1.75 : 2.3);
    int blades = cx.destroyer ? 3 : (cx.c.type == sim::ShipType::AP ? 4 : 3);
    MeshBuilder fb = cx.at(1);
    int index = 0;
    for (double yf : ys) {
        double y = yf * cx.B;
        bool inner = std::fabs(yf) < 0.2 && shafts >= 4;
        double uProp = inner ? 0.9 : 0.94;
        if (std::fabs(yf) < 1e-3) uProp = 0.965;
        double xp = cx.x(uProp);
        double zp = -std::min(cx.T * 0.62, keelDepth(cx.c, uProp) * 0.55 + r * 0.45);
        double uExit = inner ? 0.74 : 0.7;
        double xe = cx.x(uExit), ze = zp + 0.35;
        // Shaft and its stern tube boss.
        fb.setMaterial(cx.paint.metal);
        fb.tube(v3(xe, y * 0.92, ze), v3(xp + r * 0.3, y, zp), 0.22f, 0.2f, 8, false);
        fb.setMaterial(cx.paint.bottom);
        fb.tube(v3(xe + 1.8, y * 0.86, ze + 0.08), v3(xe - 1.2, y * 0.94, ze - 0.02), 0.32f, 0.36f, 8, true);
        // A-bracket strut ahead of the propeller.
        double xs = xp + r * 0.75;
        double hy = hullHalfWidth(cx.c, cx.uOf(xs), zp * 0.3);
        double side = y >= 0 ? 1.0 : -1.0;
        Vec3f boss = v3(xs, y, zp);
        fb.tube(v3(xs + 0.4, y, zp), v3(xs - 0.4, y, zp), 0.3f, 0.3f, 8, true);
        fb.bar(boss, v3(xs, side * std::max(0.3, std::min(std::fabs(y) - 0.6, hy - 0.2)), zp * 0.15), 0.18f, 0.7f, {1, 0, 0});
        fb.bar(boss, v3(xs + 0.3, side * std::max(0.2, std::fabs(y) * 0.35), zp * 0.5), 0.18f, 0.7f, {1, 0, 0});
        propeller(cx, v3(xp, y, zp), float(r), blades, y >= 0 ? 1 : -1, index++);
    }
    // Rudders: twin abaft the propellers on USN destroyers, otherwise one on the centreline.
    double zTop = -keelDepth(cx.c, 0.985) * 0.35;
    double zBot = -std::min(cx.T * 0.92, keelDepth(cx.c, 0.97) + 0.2);
    double chord = std::max(1.6, cx.L * 0.022);
    if (cx.destroyer && cx.usn && shafts == 2) {
        for (int k = 0; k < 2; ++k) rudder(cx, cx.x(0.985), ys[size_t(k)] * cx.B * 0.9, zTop, zBot, chord * 0.8, k);
    } else {
        rudder(cx, cx.x(0.99), 0.0, zTop, zBot, chord, 0);
    }
}

}  // namespace it::procgen::detail
