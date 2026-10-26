#include "procgen/ShipModel.h"

#include <algorithm>
#include <map>

namespace it::procgen {
namespace {

Vec3f srgb(int r, int g, int b) {
    auto lin = [](int c) {
        float v = c / 255.0f;
        return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return {lin(r), lin(g), lin(b)};
}

Material mat(Vec3f albedo, float rough, float metal, uint8_t id, float emissive = 0.0f) {
    Material m;
    m.albedo = albedo;
    m.roughness = rough;
    m.metalness = metal;
    m.id = id;
    m.emissive = emissive;
    return m;
}

// Material ids select the texture layer and special shading in data/shaders/ship.hlsl.
enum MatId : uint8_t {
    kHull = 0, kBottom, kBoot, kDeck, kSuper, kMetal, kGlass, kCanvas, kLens, kAircraft, kStrip,
    kDeckLino, kDeckSteel, kGrating, kWood
};

struct Ctx {
    const sim::ShipClassDef& c;
    PaintScheme paint;
    double L, B, T;
    Model model;

    explicit Ctx(const sim::ShipClassDef& cls)
        : c(cls), paint(paintScheme(cls.model.hullPaint, cls.model.deckPaint, cls.type != sim::ShipType::DD && cls.type != sim::ShipType::DE)),
          L(cls.length), B(cls.beam), T(cls.draft) {}
    double x(double u) const { return (0.5 - u) * L; }
    double uOf(double xx) const { return clamp(0.5 - xx / L, 0.0, 1.0); }
    double deck(double u) const { return c.model.deckHeightAt(u); }
    MeshPart& part(const std::string& name, PartKind kind = PartKind::Static) {
        model.parts.push_back({});
        model.parts.back().name = name;
        model.parts.back().kind = kind;
        return model.parts.back();
    }
};

double wlHalf(const sim::ShipClassDef& c, double u) {
    double B = c.beam * 0.5;
    double entrance = clamp(0.30 + (0.55 - c.model.fineness) * 0.6, 0.18, 0.42);
    double run = 0.36;
    if (u < entrance) {
        double k = u / entrance;
        return B * std::pow(1.0 - std::pow(1.0 - k, 2.2), 0.8);
    }
    if (u > 1.0 - run) {
        double k = (u - (1.0 - run)) / run;
        if (c.model.sternShape > 0.5) return B * (1.0 - 0.38 * k * k);
        return B * std::pow(std::max(0.0, 1.0 - std::pow(k, 2.4)), 0.55);
    }
    return B;
}

double bowRakeOffset(const Ctx& cx, double u, double z) {
    double keel = keelDepth(cx.c, u), top = cx.deck(u);
    double t = clamp((z + keel) / std::max(0.1, top + keel), 0.0, 1.0);
    return cx.c.model.bowRake * cx.L * std::pow(t, 1.5) * std::max(0.0, 1.0 - u / 0.07);
}

// ------------------------------------------------------------------------------------------- hull
void buildHull(Ctx& cx) {
    MeshPart& hull = cx.part("hull");
    MeshBuilder mb(hull);
    // Stations: cosine spacing for detail at bow and stern, plus the forecastle break.
    std::vector<double> us;
    int n = 56;
    for (int i = 0; i <= n; ++i) us.push_back(0.5 - 0.5 * std::cos(kPi * i / n));
    double fe = cx.c.model.forecastleEnd;
    if (fe > 0.0) {
        us.push_back(fe - 0.004);
        us.push_back(fe + 0.004);
    }
    std::sort(us.begin(), us.end());
    us.erase(std::unique(us.begin(), us.end(), [](double a, double b) { return std::fabs(a - b) < 1e-4; }), us.end());

    struct Band {
        double lo, hi;
        const Material* m;
    } bands[] = {{-100.0, -0.35, &cx.paint.bottom}, {-0.35, 0.45, &cx.paint.boot}, {0.45, 100.0, &cx.paint.hull}};
    const int K = 6;
    for (auto& band : bands) {
        mb.setMaterial(*band.m);
        for (int side : {1, -1}) {
            uint32_t v0 = uint32_t(hull.vertices.size()), i0 = uint32_t(hull.indices.size());
            for (double u : us) {
                double keel = keelDepth(cx.c, u), top = cx.deck(u);
                double z0 = clamp(band.lo, -keel, top), z1 = clamp(band.hi, -keel, top);
                for (int j = 0; j < K; ++j) {
                    double z = lerp(z0, z1, double(j) / (K - 1));
                    double xx = cx.x(u) + bowRakeOffset(cx, u, z);
                    mb.vertex({float(xx), float(side * hullHalfWidth(cx.c, u, z)), float(z)}, {0, 0, 1});
                }
            }
            for (size_t i = 0; i + 1 < us.size(); ++i)
                for (int j = 0; j + 1 < K; ++j) {
                    uint32_t a = v0 + uint32_t(i * K + j), b = v0 + uint32_t((i + 1) * K + j);
                    uint32_t c = b + 1, d = a + 1;
                    if (side > 0)
                        mb.quad(a, b, c, d);
                    else
                        mb.quad(a, d, c, b);
                }
            computeSmoothNormals(hull, v0, i0);
        }
    }
    // Weather deck with camber.
    mb.setMaterial(cx.paint.deck);
    uint32_t v0 = uint32_t(hull.vertices.size()), i0 = uint32_t(hull.indices.size());
    for (double u : us) {
        double z = cx.deck(u);
        double hw = hullHalfWidth(cx.c, u, z);
        double xx = cx.x(u) + bowRakeOffset(cx, u, z);
        mb.vertex({float(xx), float(-hw), float(z)}, {0, 0, 1});
        mb.vertex({float(xx), 0.0f, float(z + std::min(0.35, cx.B * 0.015))}, {0, 0, 1});
        mb.vertex({float(xx), float(hw), float(z)}, {0, 0, 1});
    }
    for (size_t i = 0; i + 1 < us.size(); ++i) {
        uint32_t a = v0 + uint32_t(i * 3), b = v0 + uint32_t((i + 1) * 3);
        mb.quad(a, b, b + 1, a + 1);
        mb.quad(a + 1, b + 1, b + 2, a + 2);
    }
    computeSmoothNormals(hull, v0, i0);
    // Forecastle break bulkhead (hull colour, facing aft).
    if (fe > 0.0) {
        mb.setMaterial(cx.paint.hull);
        double zl = cx.deck(fe + 0.01), zh = cx.deck(fe - 0.01);
        double hw = hullHalfWidth(cx.c, fe, zl), xx = cx.x(fe);
        mb.flatQuad({float(xx), float(-hw), float(zl)}, {float(xx), float(hw), float(zl)}, {float(xx), float(hw), float(zh)},
                    {float(xx), float(-hw), float(zh)});
    }
    // Transom.
    if (cx.c.model.sternShape > 0.5) {
        double u = 1.0, keel = keelDepth(cx.c, u), top = cx.deck(u);
        mb.setMaterial(cx.paint.hull);
        Vec3f center{float(cx.x(u)), 0.0f, float(top * 0.5 - keel * 0.5)};
        uint32_t cIdx = mb.vertex(center, {-1, 0, 0});
        std::vector<uint32_t> ring;
        for (int j = 0; j <= 16; ++j) {
            double z = lerp(-keel, top, double(j) / 16);
            ring.push_back(mb.vertex({float(cx.x(u)), float(hullHalfWidth(cx.c, u, z)), float(z)}, {-1, 0, 0}));
        }
        for (int j = 16; j >= 0; --j) {
            double z = lerp(-keel, top, double(j) / 16);
            ring.push_back(mb.vertex({float(cx.x(u)), float(-hullHalfWidth(cx.c, u, z)), float(z)}, {-1, 0, 0}));
        }
        for (size_t k = 0; k + 1 < ring.size(); ++k) mb.tri(cIdx, ring[k + 1], ring[k]);
    }
}

// ---------------------------------------------------------------------------------- superstructure
void deckhouse(MeshBuilder& mb, const Ctx& cx, double xx, double y, double z0, double len, double wid, double h, int levels,
               const PaintScheme& p, bool windows) {
    double zz = z0;
    for (int l = 0; l < std::max(1, levels); ++l) {
        double s = 1.0 - 0.1 * l;
        double ll = len * s, ww = wid * (1.0 - 0.12 * l);
        mb.setMaterial(p.superstructure);
        mb.taperedBox({float(xx - ll * 0.5), float(y - ww * 0.5), float(zz)}, {float(xx + ll * 0.5), float(y + ww * 0.5), float(zz + h)}, 0.0f,
                      0.0f, float(windows ? 0.4 : 0.0));
        if (windows) {
            mb.setMaterial(p.glass);
            float wz0 = float(zz + h * 0.55), wz1 = float(zz + h * 0.85);
            float fx = float(xx + ll * 0.5 - 0.4 * (0.55 + 0.15)) + 0.03f;
            mb.flatQuad({fx, float(y + ww * 0.45), wz0}, {fx, float(y - ww * 0.45), wz0}, {fx - 0.12f, float(y - ww * 0.45), wz1},
                        {fx - 0.12f, float(y + ww * 0.45), wz1});
        }
        zz += h;
    }
    (void)cx;
}

void pagoda(MeshBuilder& mb, double xx, double z0, double len, double wid, double h, int levels, double taper, const PaintScheme& p,
            bool massive) {
    double zz = z0;
    for (int l = 0; l < levels; ++l) {
        double t = double(l) / std::max(1, levels - 1);
        double s = 1.0 - taper * t;
        double ll = len * s * (massive ? 1.0 : 0.9), ww = wid * s;
        // IJN bridges step back as they rise.
        double cxx = xx - len * 0.12 * t;
        mb.setMaterial(p.superstructure);
        mb.box({float(cxx - ll * 0.5), float(-ww * 0.5), float(zz)}, {float(cxx + ll * 0.5), float(ww * 0.5), float(zz + h * 0.88)});
        // Overhanging platform at each level.
        mb.box({float(cxx - ll * 0.55), float(-ww * 0.58), float(zz + h * 0.88)}, {float(cxx + ll * 0.58), float(ww * 0.58), float(zz + h)});
        if (l >= levels - 3) {
            mb.setMaterial(p.glass);
            float fx = float(cxx + ll * 0.5) + 0.02f;
            mb.flatQuad({fx, float(ww * 0.42), float(zz + h * 0.4)}, {fx, float(-ww * 0.42), float(zz + h * 0.4)},
                        {fx, float(-ww * 0.42), float(zz + h * 0.75)}, {fx, float(ww * 0.42), float(zz + h * 0.75)});
        }
        zz += h;
    }
}

void funnel(Ctx& cx, MeshBuilder& mb, const sim::ModelPart& pt) {
    double u = pt.f, z0 = cx.deck(u) + pt.z;
    double xx = cx.x(u);
    double rx = pt.len * 0.5, ry = pt.wid * 0.5;
    double rakeOff = pt.h * std::tan(pt.rake * kDegToRad);
    MeshPart& part = cx.model.parts[0];
    (void)part;
    if (pt.style == "trunked" && pt.f2 > 0.0) {
        // Two uptakes merged into one strongly raked casing (IJN): long base narrowing to a single stack.
        double x2 = cx.x(pt.f2);
        double baseX = 0.5 * (xx + x2), baseRx = 0.5 * std::fabs(xx - x2) + rx;
        double topRx = rx, topX = xx - rakeOff;
        int segs = 18;
        uint32_t start = 0;
        mb.setMaterial(cx.paint.superstructure);
        std::vector<uint32_t> idx;
        (void)start;
        for (int i = 0; i <= segs; ++i) {
            double a = double(i) / segs * kTwoPi;
            double c = std::cos(a), s = std::sin(a);
            Vec3f nb = normalize(Vec3f{float(c / baseRx), float(s / ry), 0.25f});
            idx.push_back(mb.vertex({float(baseX + c * baseRx), float(s * ry), float(z0)}, nb));
            idx.push_back(mb.vertex({float(topX + c * topRx), float(s * ry * 0.95), float(z0 + pt.h * 0.88)}, nb));
        }
        for (int i = 0; i < segs; ++i) {
            uint32_t a = idx[size_t(i) * 2], b = a + 1, c = idx[size_t(i + 1) * 2], d = c + 1;
            mb.tri(a, c, d);
            mb.tri(a, d, b);
        }
        mb.setMaterial(cx.paint.metal);
        mb.ellipticCylinder({float(topX), 0.0f, float(z0 + pt.h * 0.88)}, float(topRx * 1.02), float(ry * 0.97), float(pt.h * 0.12), 18,
                            float(rakeOff * 0.12), 1.0f, true, false);
        return;
    }
    mb.setMaterial(cx.paint.superstructure);
    if (pt.style == "flat") {
        // Flat-sided (Benson class) funnel.
        mb.taperedBox({float(xx - rx), float(-ry), float(z0)}, {float(xx + rx), float(ry), float(z0 + pt.h * 0.9)}, 0.05f, 0.0f, 0.0f);
        mb.setMaterial(cx.paint.metal);
        mb.box({float(xx - rx * 0.95), float(-ry * 0.95), float(z0 + pt.h * 0.9)}, {float(xx + rx * 0.95), float(ry * 0.95), float(z0 + pt.h)});
        return;
    }
    mb.ellipticCylinder({float(xx), 0.0f, float(z0)}, float(rx), float(ry), float(pt.h * 0.88), 18, float(rakeOff * 0.88), 1.0f, false, false);
    mb.setMaterial(cx.paint.metal);
    mb.ellipticCylinder({float(xx - rakeOff * 0.88), 0.0f, float(z0 + pt.h * 0.88)}, float(rx * 1.02), float(ry * 1.02), float(pt.h * 0.12), 18,
                        float(rakeOff * 0.12), 1.0f, true, false);
}

void mast(Ctx& cx, MeshBuilder& mb, const sim::ModelPart& pt, bool tripod) {
    double u = pt.f, z0 = cx.deck(u) + pt.z, xx = cx.x(u);
    double rake = std::tan(pt.rake * kDegToRad);
    mb.setMaterial(cx.paint.superstructure);
    Vec3f top{float(xx - pt.h * rake), 0.0f, float(z0 + pt.h)};
    if (tripod) {
        Vec3f knee{float(xx - pt.h * 0.62 * rake), 0.0f, float(z0 + pt.h * 0.62)};
        mb.tube({float(xx), 0, float(z0)}, knee, 0.45f, 0.35f, 8, false);
        double spread = std::max(2.0, pt.h * 0.11);
        mb.tube({float(xx - spread * 1.3), float(spread), float(z0)}, knee, 0.32f, 0.3f, 6, false);
        mb.tube({float(xx - spread * 1.3), float(-spread), float(z0)}, knee, 0.32f, 0.3f, 6, false);
        mb.box({float(knee.x - 1.8f), -1.6f, knee.z - 0.25f}, {float(knee.x + 1.8f), 1.6f, knee.z + 0.6f});  // top platform
        mb.tube(knee, top, 0.25f, 0.12f, 6, false);
    } else if (pt.style == "derrick") {
        mb.tube({float(xx), 0, float(z0)}, top, 0.4f, 0.25f, 8, false);
        mb.tube({float(xx + 0.6), 0, float(z0 + 2.0)}, {float(xx + pt.h * 0.55), 0.0f, float(z0 + pt.h * 0.45)}, 0.18f, 0.12f, 6, false);
        mb.tube({float(xx - 0.6), 0, float(z0 + 2.0)}, {float(xx - pt.h * 0.55), 0.0f, float(z0 + pt.h * 0.45)}, 0.18f, 0.12f, 6, false);
        mb.box({float(xx - 1.2), -1.2f, float(z0)}, {float(xx + 1.2), 1.2f, float(z0 + 2.5)});
    } else {
        mb.tube({float(xx), 0, float(z0)}, top, 0.38f, 0.14f, 8, false);
    }
    // Yardarm.
    Vec3f yard{float(xx - pt.h * 0.82 * rake), 0.0f, float(z0 + pt.h * 0.82)};
    mb.tube(yard + Vec3f{0, -std::max(2.5f, float(cx.B * 0.2)), 0}, yard + Vec3f{0, std::max(2.5f, float(cx.B * 0.2)), 0}, 0.1f, 0.1f, 5, false);
}

void tower(Ctx& cx, MeshBuilder& mb, const sim::ModelPart& pt) {
    double u = pt.f, z0 = cx.deck(u) + pt.z, xx = cx.x(u);
    mb.setMaterial(cx.paint.superstructure);
    if (pt.style == "usn_pole") {
        mb.tube({float(xx), 0, float(z0)}, {float(xx), 0, float(z0 + pt.h)}, 0.5f, 0.25f, 10, false);
        mb.box({float(xx - 1.5), -1.5f, float(z0 + pt.h * 0.7)}, {float(xx + 1.5), 1.5f, float(z0 + pt.h * 0.7 + 0.6)});
        return;
    }
    mb.taperedBox({float(xx - pt.len * 0.5), float(-pt.wid * 0.5), float(z0)}, {float(xx + pt.len * 0.5), float(pt.wid * 0.5), float(z0 + pt.h * 0.8)},
                  0.3f, 0.3f);
    mb.box({float(xx - 2.2), -2.2f, float(z0 + pt.h * 0.8)}, {float(xx + 2.2), 2.2f, float(z0 + pt.h * 0.8 + 1.6)});
    mb.tube({float(xx), 0, float(z0 + pt.h * 0.8)}, {float(xx), 0, float(z0 + pt.h)}, 0.25f, 0.12f, 6, false);
}

void aircraft(MeshBuilder& mb, const PaintScheme& p, Vec3f at, const std::string& style) {
    mb.setMaterial(p.aircraft);
    bool biplane = style == "soc" || style == "pete" || style == "walrus";
    float len = style == "walrus" ? 11.4f : (style == "jake" ? 11.3f : 9.5f);
    float span = style == "walrus" ? 13.97f : (style == "jake" ? 14.5f : 11.0f);
    float h = 1.6f;
    Vec3f nose = at + Vec3f{len * 0.5f, 0, h}, tail = at + Vec3f{-len * 0.5f, 0, h + 0.4f};
    mb.tube(tail, nose, 0.25f, 0.6f, 8, true);
    mb.box(at + Vec3f{len * 0.1f, -span * 0.5f, h + 0.55f}, at + Vec3f{len * 0.28f, span * 0.5f, h + 0.68f});
    if (biplane) mb.box(at + Vec3f{len * 0.12f, -span * 0.45f, h + 2.0f}, at + Vec3f{len * 0.28f, span * 0.45f, h + 2.12f});
    mb.box(at + Vec3f{-len * 0.5f, -2.0f, h + 0.45f}, at + Vec3f{-len * 0.38f, 2.0f, h + 0.52f});  // tailplane
    mb.box(at + Vec3f{-len * 0.5f, -0.06f, h + 0.45f}, at + Vec3f{-len * 0.36f, 0.06f, h + 2.0f});  // fin
    mb.setMaterial(p.canvas);
    mb.tube(at + Vec3f{-len * 0.15f, 0, 0.3f}, at + Vec3f{len * 0.42f, 0, 0.35f}, 0.35f, 0.25f, 8, true);  // main float
    if (style != "jake") {
        mb.tube(at + Vec3f{len * 0.0f, span * 0.42f, 0.9f}, at + Vec3f{len * 0.2f, span * 0.42f, 0.9f}, 0.15f, 0.1f, 6, true);
        mb.tube(at + Vec3f{len * 0.0f, -span * 0.42f, 0.9f}, at + Vec3f{len * 0.2f, -span * 0.42f, 0.9f}, 0.15f, 0.1f, 6, true);
    }
}

// ------------------------------------------------------------------------------------- weapons
struct TurretDims {
    double len, wid, h, barrelR, spacing, frontOffset;
};

TurretDims turretDims(const sim::MountDef& m) {
    double cal = m.gun ? m.gun->caliberMm : 127.0;
    double k = cal / 203.0;
    TurretDims d{};
    d.barrelR = float(cal / 1000.0 * 0.9);
    if (m.style == "turret3") {
        d = {9.5 * std::max(0.85, k), 9.0 * std::max(0.8, k), 3.0 * std::max(0.85, k), cal / 1000.0 * 0.85, 1.7 * std::max(0.8, k), 3.0};
    } else if (m.style == "turret2") {
        d = {8.5 * std::max(0.7, k), 6.8 * std::max(0.7, k), 2.9 * std::max(0.8, k), cal / 1000.0 * 0.85, 1.25 * std::max(0.8, k), 2.8};
    } else if (m.style == "dp_twin" || m.style == "dd_turret") {
        d = {6.0, 4.6, 2.5, cal / 1000.0 * 0.8, 1.0, 2.0};
    } else if (m.style == "dp_single" || m.style == "dd_turret1") {
        d = {4.6, 3.3, 2.3, cal / 1000.0 * 0.8, 0.0, 1.6};
    } else if (m.style == "shield") {
        d = {3.0, 2.6, 2.2, cal / 1000.0 * 0.8, 0.0, 0.6};
    } else {
        d = {2.2, 1.6, 1.4, cal / 1000.0 * 0.8, 0.0, 0.3};
    }
    return d;
}

void buildMount(Ctx& cx, size_t mi) {
    const sim::MountDef& m = cx.c.mounts[mi];
    TurretDims d = turretDims(m);
    double u = cx.uOf(m.x);
    double deckZ = cx.deck(u);
    double gz0 = m.z - d.h * 0.45;  // gunhouse base
    // Barbette / pedestal (static, part of the hull)
    {
        MeshBuilder mb(cx.model.parts[0]);
        mb.setMaterial(cx.paint.superstructure);
        if (gz0 > deckZ + 0.2) {
            double r = std::max(d.wid, d.len) * 0.42;
            if (m.style == "open" || m.style == "shield") r = 0.8;
            mb.ellipticCylinder({float(m.x), float(m.y), float(deckZ)}, float(r), float(r), float(gz0 - deckZ), 16, 0.0f, 1.0f, true, false);
        }
    }
    MeshPart& tp = cx.part("mount_" + m.id, PartKind::Turret);
    int turretIdx = int(cx.model.parts.size()) - 1;
    tp.mount = int(mi);
    tp.pivot = {float(m.x), float(m.y), float(gz0)};
    MeshBuilder mb(tp);
    mb.setMaterial(cx.paint.superstructure);
    float x0 = float(m.x - d.len * 0.45), x1 = float(m.x + d.len * 0.55);
    float y0 = float(m.y - d.wid * 0.5), y1 = float(m.y + d.wid * 0.5);
    if (m.style == "open") {
        mb.ellipticCylinder({float(m.x), float(m.y), float(gz0)}, 0.9f, 0.9f, 0.8f, 10, 0.0f, 0.8f, true, false);
        mb.box({float(m.x - 0.6), float(m.y - 0.9), float(gz0 + 0.6)}, {float(m.x + 0.8), float(m.y + 0.9), float(gz0 + 1.4)});
    } else if (m.style == "shield") {
        mb.ellipticCylinder({float(m.x), float(m.y), float(gz0)}, 0.9f, 0.9f, 0.9f, 10, 0.0f, 0.8f, true, false);
        mb.taperedBox({float(m.x + 0.2), float(m.y - d.wid * 0.5), float(gz0 + 0.2)}, {float(m.x + 0.6), float(m.y + d.wid * 0.5), float(gz0 + d.h)},
                      0.0f, 0.15f, 0.2f);
    } else {
        // Enclosed gunhouse: sloped front, flat roof, slight side taper.
        mb.taperedBox({x0, y0, float(gz0)}, {x1, y1, float(gz0 + d.h)}, 0.08f, 0.1f, float(d.h * 0.45));
        if (m.style == "turret3" || m.style == "turret2") {
            // Rangefinder hood / periscope housings on the roof.
            mb.box({float(m.x - d.len * 0.35), float(m.y - d.wid * 0.48), float(gz0 + d.h * 0.85)},
                   {float(m.x - d.len * 0.15), float(m.y + d.wid * 0.48), float(gz0 + d.h * 1.12)});
        }
    }
    // Barrels (one part, elevates about the trunnion axis).
    MeshPart& bp = cx.part("guns_" + m.id, PartKind::Barrel);
    bp.mount = int(mi);
    bp.parent = turretIdx;
    double trunX = m.x + d.len * 0.55 - d.frontOffset - 0.3;
    if (m.style == "open" || m.style == "shield") trunX = m.x + 0.2;
    bp.pivot = {float(trunX), float(m.y), float(m.z)};
    MeshBuilder bb(bp);
    bb.setMaterial(cx.paint.metal);
    double barrelLen = m.gun ? m.gun->caliberMm / 1000.0 * m.gun->calibres : 5.0;
    for (int b = 0; b < m.barrels; ++b) {
        double off = (b - (m.barrels - 1) * 0.5) * (d.spacing > 0 ? d.spacing : 0.6);
        Vec3f a{float(trunX - 0.8), float(m.y + off), float(m.z)}, e{float(trunX + barrelLen), float(m.y + off), float(m.z)};
        bb.tube(a, e, float(d.barrelR * 1.5), float(d.barrelR * 0.95), 10, true);
        // Muzzle swell.
        bb.tube(e - Vec3f{0.35f, 0, 0}, e, float(d.barrelR * 1.15), float(d.barrelR * 1.15), 10, true);
    }
}

void buildTorpedoMount(Ctx& cx, size_t ti) {
    const sim::TorpedoMountDef& t = cx.c.torpedoMounts[ti];
    double u = cx.uOf(t.x), deckZ = cx.deck(u);
    MeshPart& p = cx.part("torpedo_" + t.id, PartKind::TorpedoMount);
    p.mount = int(ti);
    p.pivot = {float(t.x), float(t.y), float(deckZ + 0.6)};
    MeshBuilder mb(p);
    double r = t.torpedo && t.torpedo->diameterMm > 600 ? 0.36 : 0.31;
    double len = t.torpedo && t.torpedo->diameterMm > 600 ? 8.5 : 7.2;
    mb.setMaterial(cx.paint.superstructure);
    mb.ellipticCylinder({float(t.x), float(t.y), float(deckZ)}, 1.3f, 1.3f, 0.6f, 12, 0.0f, 1.0f, true, false);
    mb.setMaterial(cx.paint.metal);
    for (int k = 0; k < t.tubes; ++k) {
        double off = (k - (t.tubes - 1) * 0.5) * (r * 2.15);
        mb.tube({float(t.x - len * 0.45), float(t.y + off), float(deckZ + 1.0)}, {float(t.x + len * 0.55), float(t.y + off), float(deckZ + 1.0)},
                float(r), float(r), 8, true);
    }
    if (cx.c.nation == sim::Nation::IJN && t.tubes >= 3) {
        mb.setMaterial(cx.paint.superstructure);  // splinter shield of IJN quadruple mounts
        double w = t.tubes * r * 2.15 + 0.5;
        mb.taperedBox({float(t.x - len * 0.25), float(t.y - w * 0.5), float(deckZ + 0.6)}, {float(t.x + len * 0.15), float(t.y + w * 0.5), float(deckZ + 2.0)},
                      0.0f, 0.1f, 0.6f);
    }
}

}  // namespace

PaintScheme paintScheme(const std::string& hullPaint, const std::string& deckPaint, bool plankedDeck) {
    PaintScheme p;
    Vec3f hull = srgb(110, 112, 112), bottom = srgb(110, 40, 32);
    if (hullPaint == "usn_ms21") hull = srgb(66, 74, 86);          // Navy Blue 5-N (Measure 21)
    else if (hullPaint == "usn_ms1") hull = srgb(80, 84, 88);       // Dark Gray 5-D
    else if (hullPaint == "ijn_kure_grey") hull = srgb(112, 114, 114);
    else if (hullPaint == "ijn_sasebo_grey") hull = srgb(98, 100, 98);
    else if (hullPaint == "rn_507a") hull = srgb(92, 95, 98);
    Vec3f deck = srgb(150, 120, 85);
    if (deckPaint == "usn_deck_blue") deck = srgb(56, 62, 72);  // Deck Blue 20-B
    else if (deckPaint == "ijn_linoleum") deck = srgb(104, 70, 54);
    p.hull = mat(hull, 0.62f, 0.05f, kHull);
    p.superstructure = mat(hull * 1.05f, 0.6f, 0.05f, kSuper);
    p.bottom = mat(bottom, 0.8f, 0.0f, kBottom);
    p.boot = mat(srgb(28, 28, 30), 0.7f, 0.0f, kBoot);
    // Cruisers kept their wooden planking under the Deck Blue paint; destroyers had bare steel decks.
    // The IJN laid linoleum on steel, held down by brass strips.
    uint8_t deckId = kDeck;
    if (deckPaint == "ijn_linoleum") deckId = kDeckLino;
    else if (!plankedDeck) deckId = kDeckSteel;
    p.deck = mat(deck, deckId == kDeck && deckPaint == "teak" ? 0.8f : 0.85f, 0.0f, deckId);
    p.metal = mat(srgb(32, 32, 33), 0.45f, 0.6f, kMetal);
    p.glass = mat(srgb(18, 24, 30), 0.12f, 0.0f, kGlass);
    p.canvas = mat(srgb(168, 166, 156), 0.9f, 0.0f, kCanvas);
    p.lens = mat(srgb(235, 232, 210), 0.2f, 0.0f, kLens);
    p.aircraft = mat(hullPaint.rfind("ijn", 0) == 0 ? srgb(150, 155, 140) : srgb(120, 130, 145), 0.55f, 0.05f, kAircraft);
    p.linoleumStrip = mat(srgb(170, 140, 70), 0.4f, 0.7f, kStrip);
    return p;
}

double keelDepth(const sim::ShipClassDef& c, double u) {
    return c.draft * (0.15 + 0.85 * smoothstep(0.0, 0.14, u)) * (1.0 - 0.55 * smoothstep(0.82, 1.0, u));
}

double hullHalfWidth(const sim::ShipClassDef& c, double u, double z) {
    double w = wlHalf(c, u);
    double keel = keelDepth(c, u);
    double mid = 1.0 - std::fabs(u - 0.47) / 0.47;
    if (z <= 0.0) {
        double t = std::min(1.0, -z / std::max(0.1, keel));
        double m = lerp(1.6, 3.6, clamp(mid * 1.3, 0.0, 1.0));
        return w * std::pow(std::max(0.0, 1.0 - std::pow(t, m)), 1.0 / m);
    }
    double fb = std::max(0.5, c.model.deckHeightAt(u));
    double zr = clamp(z / fb, 0.0, 1.5);
    double bowness = 1.0 - smoothstep(0.0, 0.35, u);
    double y = w * (1.0 + c.model.flare * bowness * zr);
    // Flared bow: width at deck level near the stem even though the waterline narrows to nothing.
    if (u < 0.15) y += c.model.flare * c.beam * 0.3 * zr * std::sqrt(u / 0.15) * (1.0 - u / 0.15);
    y *= 1.0 - c.model.tumblehome * zr;
    return y;
}

Model buildShipModel(const sim::ShipClassDef& cls, const ShipModelOptions& opt) {
    Ctx cx(cls);
    cx.model.name = cls.id;
    buildHull(cx);
    auto& P = cx.paint;
    for (auto& pt : cls.model.parts) {
        double u = pt.f, xx = cx.x(u), z0 = cx.deck(u) + pt.z;
        if (pt.type == "superstructure" || pt.type == "deckhouse" || pt.type == "hangar") {
            MeshBuilder mb(cx.model.parts[0]);
            deckhouse(mb, cx, xx, pt.y, z0, pt.len, pt.wid, pt.h, pt.levels, P, false);
        } else if (pt.type == "bridge") {
            MeshBuilder mb(cx.model.parts[0]);
            deckhouse(mb, cx, xx, pt.y, z0, pt.len, pt.wid, pt.h, pt.levels, P, true);
            mb.setMaterial(P.superstructure);  // bridge wings
            double wz = z0 + pt.h * std::max(1, pt.levels - 1);
            mb.box({float(xx + pt.len * 0.1), float(-pt.wid * 0.75), float(wz - 0.3)}, {float(xx + pt.len * 0.42), float(pt.wid * 0.75), float(wz)});
        } else if (pt.type == "pagoda") {
            MeshBuilder mb(cx.model.parts[0]);
            pagoda(mb, xx, z0, pt.len, pt.wid, pt.h, pt.levels, pt.taper, P, pt.style == "ijn_takao");
        } else if (pt.type == "funnel") {
            MeshBuilder mb(cx.model.parts[0]);
            funnel(cx, mb, pt);
        } else if (pt.type == "mast" || pt.type == "tripod") {
            MeshBuilder mb(cx.model.parts[0]);
            mast(cx, mb, pt, pt.type == "tripod");
        } else if (pt.type == "tower") {
            MeshBuilder mb(cx.model.parts[0]);
            tower(cx, mb, pt);
        } else if (pt.type == "director") {
            MeshPart& p = cx.part("director", PartKind::Director);
            p.pivot = {float(xx), 0.0f, float(z0)};
            MeshBuilder mb(p);
            mb.setMaterial(P.superstructure);
            mb.box({float(xx - pt.len * 0.5), float(-pt.len * 0.45), float(z0)}, {float(xx + pt.len * 0.5), float(pt.len * 0.45), float(z0 + pt.h)});
            mb.setMaterial(P.metal);
            mb.tube({float(xx - 0.3), float(-pt.wid * 0.5), float(z0 + pt.h * 0.6)}, {float(xx - 0.3), float(pt.wid * 0.5), float(z0 + pt.h * 0.6)},
                    0.22f, 0.22f, 8, true);
        } else if (pt.type == "radar" && opt.details) {
            MeshPart& p = cx.part("radar_" + pt.style, PartKind::Radar);
            p.pivot = {float(xx), 0.0f, float(z0)};
            MeshBuilder mb(p);
            mb.setMaterial(P.metal);
            if (pt.style == "sg") {
                mb.sphere({float(xx + 0.3), 0.0f, float(z0 + pt.h * 0.5)}, 0.25f, float(pt.wid * 0.5), float(pt.h * 0.5), 10, 6);
            } else {
                // Bedspring (SC) antenna: a frame with a mesh.
                mb.box({float(xx - 0.05), float(-pt.wid * 0.5), float(z0)}, {float(xx + 0.05), float(pt.wid * 0.5), float(z0 + pt.h)});
                mb.setMaterial(P.superstructure);
                mb.tube({float(xx - 0.4), 0.0f, float(z0 - 1.2)}, {float(xx - 0.4), 0.0f, float(z0 + pt.h * 0.5)}, 0.15f, 0.15f, 6, false);
            }
        } else if (pt.type == "searchlight") {
            MeshPart& p = cx.part("searchlight", PartKind::Searchlight);
            p.pivot = {float(xx), float(pt.y), float(z0)};
            MeshBuilder mb(p);
            double r = cls.searchlightCm / 200.0 + 0.1;
            mb.setMaterial(P.superstructure);
            mb.tube({float(xx), float(pt.y), float(z0 - 0.8)}, {float(xx), float(pt.y), float(z0)}, 0.15f, 0.15f, 6, false);
            if (pt.z > 2.0) {
                // Searchlight platform on a lattice post.
                MeshBuilder hb(cx.model.parts[0]);
                hb.setMaterial(P.superstructure);
                double dz = cx.deck(u);
                hb.tube({float(xx), float(pt.y * 0.6), float(dz)}, {float(xx), float(pt.y), float(z0 - 0.9)}, 0.35f, 0.3f, 8, false);
                hb.box({float(xx - 1.6), float(pt.y - 1.6), float(z0 - 1.0)}, {float(xx + 1.6), float(pt.y + 1.6), float(z0 - 0.8)});
            }
            mb.tube({float(xx - r), float(pt.y), float(z0 + r)}, {float(xx + r * 0.6), float(pt.y), float(z0 + r)}, float(r), float(r), 12, true);
            mb.setMaterial(P.lens);
            mb.tube({float(xx + r * 0.6), float(pt.y), float(z0 + r)}, {float(xx + r * 0.64), float(pt.y), float(z0 + r)}, float(r * 0.92),
                    float(r * 0.92), 12, true);
        } else if (pt.type == "catapult") {
            MeshPart& p = cx.part("catapult", PartKind::Catapult);
            p.pivot = {float(xx), float(pt.y), float(z0)};
            MeshBuilder mb(p);
            mb.setMaterial(P.metal);
            mb.ellipticCylinder({float(xx), float(pt.y), float(z0)}, 0.9f, 0.9f, 1.0f, 10, 0.0f, 1.0f, true, false);
            mb.box({float(xx - pt.len * 0.5), float(pt.y - 0.5), float(z0 + 1.0)}, {float(xx + pt.len * 0.5), float(pt.y + 0.5), float(z0 + 1.6)});
        } else if (pt.type == "aircraft" && opt.details) {
            MeshPart& p = cx.part("aircraft", PartKind::Aircraft);
            MeshBuilder mb(p);
            aircraft(mb, P, {float(xx), float(pt.y), float(z0 + 0.6)}, pt.style);
        } else if (pt.type == "crane") {
            MeshBuilder mb(cx.model.parts[0]);
            mb.setMaterial(P.superstructure);
            mb.tube({float(xx), float(pt.y), float(z0)}, {float(xx), float(pt.y), float(z0 + pt.h)}, 0.45f, 0.3f, 8, false);
            mb.tube({float(xx), float(pt.y), float(z0 + 1.5)}, {float(xx - pt.len * 0.8), float(pt.y), float(z0 + pt.h * 0.9)}, 0.2f, 0.12f, 6, false);
        } else if (pt.type == "boat" && opt.details) {
            MeshBuilder mb(cx.model.parts[0]);
            mb.setMaterial(P.canvas);
            mb.taperedBox({float(xx - pt.len * 0.5), float(pt.y - 1.1), float(z0)}, {float(xx + pt.len * 0.5), float(pt.y + 1.1), float(z0 + 1.1)}, 0.15f,
                          -0.1f, float(pt.len * 0.15));
        }
    }
    for (size_t i = 0; i < cls.mounts.size(); ++i) buildMount(cx, i);
    for (size_t i = 0; i < cls.torpedoMounts.size(); ++i) buildTorpedoMount(cx, i);
    cx.model.computeBounds();
    return cx.model;
}

}  // namespace it::procgen
