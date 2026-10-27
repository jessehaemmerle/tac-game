// Procedural aircraft. One airframe part in two-tone camouflage (upper colour on everything that faces
// the sky, lower colour beneath), a propeller part per engine and the weapon load as separate parts so
// the renderer can spin the airscrews and drop the torpedo or bombs.
#include "procgen/AircraftModel.h"

#include "procgen/ShipModelInternal.h"

namespace it::procgen {

namespace {
using namespace detail;

struct Frame {
    float L = 10, S = 12, R = 0.65f;      // length, span, fuselage radius
    float xNose = 3, xTail = -7;          // fuselage ends
    float xLE = 0.6f, chord = 2.5f, taper = 0.5f;
    float zWing = 0;                      // wing root height
    float dihedral = 0.5f;                // tip rise of each wing panel (m)
    std::vector<Vec3f> engines;           // propeller hub positions
    float propR = 1.5f;
};

Vec3f rgb255(const Vec3& c) { return srgb(int(c.x), int(c.y), int(c.z)); }

/// Fuselage radius along its length (t: 0 tail .. 1 nose).
std::vector<Vec2f> fuselageProfile(const sim::AircraftModelDesc& m, float L, float R, bool singleRadial, bool singleInline) {
    std::vector<std::pair<float, float>> pts;  // (t, r/R)
    if (singleRadial)
        pts = {{0.0f, 0.1f}, {0.12f, 0.3f}, {0.3f, 0.6f}, {0.5f, 0.88f}, {0.66f, 1.0f}, {0.86f, 1.0f}, {0.93f, 1.04f}, {0.975f, 0.98f}, {1.0f, 0.6f}};
    else if (singleInline)
        pts = {{0.0f, 0.1f}, {0.12f, 0.3f}, {0.3f, 0.6f}, {0.5f, 0.9f}, {0.68f, 1.0f}, {0.82f, 0.95f}, {0.92f, 0.75f}, {0.98f, 0.45f}, {1.0f, 0.18f}};
    else
        pts = {{0.0f, 0.12f}, {0.1f, 0.3f}, {0.25f, 0.6f}, {0.45f, 0.9f}, {0.6f, 1.0f}, {0.8f, 1.0f}, {0.9f, 0.9f}, {0.96f, 0.7f}, {0.99f, 0.4f}, {1.0f, 0.12f}};
    (void)m;
    std::vector<Vec2f> out;
    for (auto& [t, r] : pts) out.push_back({r * R, t * L});
    return out;
}

/// Radial engine cowling and hub; returns the hub position.
Vec3f cowling(MeshBuilder& mb, const PaintScheme& p, const Material& cowlMat, Vec3f front, float r, float len) {
    mb.setMaterial(cowlMat);
    mb.lathe(front - Vec3f{len, 0, 0}, {1, 0, 0}, {{r * 0.75f, 0}, {r, len * 0.35f}, {r * 1.02f, len * 0.8f}, {r * 0.86f, len}}, 16, false);
    mb.setMaterial(p.black);
    mb.disc(front - Vec3f{0.02f, 0, 0}, {1, 0, 0}, r * 0.86f, 16);  // the cylinders behind the cowling ring
    return front + Vec3f{0.12f, 0, 0};
}

/// Engine nacelle on the wing, ending behind the trailing edge.
void nacelle(MeshBuilder& mb, const PaintScheme& p, const Material& skin, const Material& cowlMat, Vec3f front, float r, float len,
             bool radial) {
    mb.setMaterial(skin);
    mb.lathe(front - Vec3f{len, 0, 0}, {1, 0, 0}, {{0.05f * r, 0}, {0.6f * r, len * 0.35f}, {r, len * 0.75f}, {r, len * 0.85f}}, 14, false);
    if (radial) {
        cowling(mb, p, cowlMat, front, r * 1.05f, len * 0.18f);
    } else {
        mb.setMaterial(skin);
        mb.lathe(front - Vec3f{len * 0.15f, 0, 0}, {1, 0, 0}, {{r, 0}, {r * 0.7f, len * 0.12f}, {r * 0.3f, len * 0.15f}}, 12, false);
    }
}

void propeller(Model& model, const PaintScheme& p, Vec3f hub, float radius, int blades, int index) {
    model.parts.push_back({});
    MeshPart& part = model.parts.back();
    part.name = "propeller";
    part.kind = PartKind::Propeller;
    part.sub = index % 2;
    part.pivot = hub;
    part.axis = {1, 0, 0};
    MeshBuilder mb(part);
    mb.setMaterial(p.black);
    mb.lathe(hub - Vec3f{0.05f, 0, 0}, {1, 0, 0}, {{radius * 0.13f, 0}, {radius * 0.1f, 0.25f}, {0.0f, 0.45f}}, 10, false);
    for (int b = 0; b < blades; ++b) {
        float a = float(b) / blades * float(kTwoPi) + 0.4f;
        Vec3f d{0, std::cos(a), std::sin(a)};
        // Blade along d, its chord across, thin along the shaft.
        mb.orientedBox(hub + d * (radius * 0.55f), {radius * 0.45f, radius * 0.08f, 0.035f}, d, {1, 0, 0});
    }
}

void tailSurfaces(MeshBuilder& mb, const Frame& f, const sim::AircraftModelDesc& m, bool twin) {
    float ct = std::max(0.9f, f.chord * 0.5f);
    float zt = f.R * 0.15f;
    float xRoot = f.xTail + ct * 1.25f;
    float stab = f.S * (twin ? 0.2f : 0.17f);
    for (int s : {1, -1}) wing(mb, {xRoot, 0, zt}, ct, ct * 0.6f, s * stab, ct * 0.25f, 0.05f, 0.1f);
    // Fins: a lofted airfoil between root and tip.
    float h = std::max(1.2f, f.L * 0.15f);
    float cRoot = std::max(1.0f, f.L * 0.13f), cTip = cRoot * 0.5f;
    auto fin = [&](float y, float z0) {
        std::vector<Vec3f> a, b;
        const int n = 10;
        for (int k = 0; k < n; ++k) {
            float ang = float(k) / n * float(kTwoPi);
            float t = 0.5f + 0.5f * std::cos(ang);
            float th = 0.06f * 4.0f * t * (1.0f - t) * cRoot * 0.25f + 0.02f;
            a.push_back({f.xTail + cRoot + 0.1f - (1.0f - t) * cRoot, y + (std::sin(ang) >= 0 ? th : -th), z0});
            b.push_back({f.xTail + cTip * 0.6f + 0.1f - (1.0f - t) * cTip, y + (std::sin(ang) >= 0 ? th * 0.6f : -th * 0.6f), z0 + h});
        }
        mb.loft(a, b, true, false);
    };
    if (twin) {
        for (int s : {1, -1}) fin(s * stab * 0.95f, zt - h * 0.35f);
    } else {
        fin(0.0f, f.R * 0.4f);
    }
    (void)m;
}

/// Bomb: a body with a pointed nose and a finned tail along x, centred on c.
void bomb(MeshBuilder& mb, const PaintScheme& p, Vec3f c, float dia, float len) {
    float r = dia * 0.5f;
    mb.setMaterial(p.dark);
    mb.lathe(c - Vec3f{len * 0.5f, 0, 0}, {1, 0, 0},
             {{r * 0.35f, 0}, {r * 0.8f, len * 0.22f}, {r, len * 0.4f}, {r, len * 0.75f}, {r * 0.6f, len * 0.95f}, {0.0f, len}}, 10, false);
    for (int k = 0; k < 4; ++k) {
        float a = float(k) * float(kPi) * 0.5f + float(kPi) * 0.25f;
        Vec3f d{0, std::cos(a), std::sin(a)};
        mb.orientedBox(c - Vec3f{len * 0.42f, 0, 0} + d * (r * 1.1f), {len * 0.08f, r * 0.7f, 0.01f}, {1, 0, 0}, d);
    }
}

/// Recolour the airframe's camouflage: everything facing downwards gets the lower colour.
void twoTone(MeshPart& part, const Material& upper, const Material& lower) {
    uint32_t up = upper.packColor(), lo = lower.packColor();
    for (auto& v : part.vertices) {
        uint32_t id = (v.params >> 16) & 0xFF;
        if (id != kAircraft) continue;
        v.color = v.normal.z < -0.3f ? lo : up;
    }
}

}  // namespace

Model buildAircraftModel(const sim::AircraftDef& def) {
    const sim::AircraftModelDesc& m = def.model;
    Model model;
    model.name = def.id;
    model.parts.reserve(16);
    PaintScheme p = paintScheme("grey", "teak");
    Material upper = mat(rgb255(m.upper), 0.55f, 0.05f, kAircraft);
    Material lower = mat(rgb255(m.lower), 0.55f, 0.05f, kAircraft);
    Material cowlMat = m.cowl.x >= 0 ? mat(rgb255(m.cowl), 0.5f, 0.2f, kPlain) : upper;
    p.aircraft = upper;
    model.parts.push_back({});
    model.parts.back().name = "airframe";
    MeshBuilder mb(model.parts.back());

    if (m.layout == "float") {
        // The ship's floatplane builder, re-centred on the fuselage.
        float len = float(m.length);
        mb.setTransform(Mat4f::translation({len * 0.08f, 0, -2.3f}));
        floatplane(mb, p, m.style, def.nation, false);
        mb.clearTransform();
        twoTone(model.parts[0], upper, lower);
        // Hub positions as laid out by the floatplane builder (tractor cowling, or the Walrus pusher).
        bool walrus = m.style == "walrus";
        Vec3f hub = walrus ? Vec3f{len * 0.16f - 0.6f - 1.5f + len * 0.08f, 0, 0.5f} : Vec3f{len * 0.37f + 0.62f + len * 0.08f, 0, 0.05f};
        propeller(model, p, hub, walrus ? 1.6f : 1.35f, walrus ? 4 : 2, 0);
        model.computeBounds();
        return model;
    }

    Frame f;
    f.L = float(m.length);
    f.S = float(m.span);
    f.R = float(m.fuselage) * 0.5f;
    f.chord = float(m.chord);
    f.taper = float(m.taper);
    bool single = m.engines == 1;
    bool radial = m.engine != "inline";
    bool boat = m.layout == "boat";
    bool biplane = m.layout == "bi";
    bool singleRadial = single && radial, singleInline = single && !radial;
    float noseLen = single ? f.L * 0.3f : f.L * 0.33f;
    f.xLE = f.chord * 0.25f;
    f.xNose = f.xLE + noseLen;
    f.xTail = f.xNose - f.L;
    if (m.wing == "low") f.zWing = -f.R * 0.6f;
    else if (m.wing == "high") f.zWing = f.R * 0.65f;
    else if (m.wing == "parasol") f.zWing = f.R * (boat ? 1.35f : 1.0f) + 1.0f;
    else if (m.wing == "invgull") f.zWing = -f.R * 0.45f;
    else f.zWing = 0.0f;
    f.dihedral = f.S * 0.5f * (m.wing == "parasol" ? 0.02f : 0.07f);
    f.propR = clamp(f.S * 0.065f, 1.3f, 2.1f);

    // ---- fuselage (a flying boat's is a deep hull with a planing bottom)
    mb.setMaterial(upper);
    auto prof = fuselageProfile(m, f.L, f.R, singleRadial, singleInline);
    if (boat) {
        mb.setTransform(Mat4f::scale({1.0f, 1.0f, 1.45f}));
        mb.lathe({f.xTail, 0, 0}, {1, 0, 0}, prof, 18, true);
        mb.clearTransform();
        mb.setMaterial(lower);
        mb.box({f.xTail + f.L * 0.35f, -f.R * 0.8f, -f.R * 1.5f}, {f.xTail + f.L * 0.62f, f.R * 0.8f, -f.R * 1.2f});  // planing step
        mb.setMaterial(upper);
    } else {
        mb.lathe({f.xTail, 0, 0}, {1, 0, 0}, prof, 18, true);
    }
    bool glazedNose = m.nose == "glazed" || m.canopy == "none";
    if (glazedNose && !single) {
        mb.setMaterial(p.glass);
        mb.sphere({f.xNose - f.R * 0.55f, 0, 0}, f.R * 0.7f, f.R * 0.75f, f.R * 0.75f, 14, 7);
        mb.setMaterial(p.black);
        for (int k = -2; k <= 2; ++k) mb.bar({f.xNose - f.R * 1.2f, 0, k * f.R * 0.25f}, {f.xNose + 0.05f, 0, k * f.R * 0.22f}, 0.03f, 0.03f);
    }

    // ---- engines and propellers
    if (single) {
        Vec3f hub;
        if (radial) {
            hub = cowling(mb, p, cowlMat, {f.xNose - 0.05f, 0, 0}, f.R * 1.04f, f.L * 0.1f);
        } else {
            hub = {f.xNose + 0.1f, 0, 0};
            mb.setMaterial(p.black);
            for (int s : {1, -1})
                for (int k = 0; k < 3; ++k) mb.box({f.xNose - 1.6f + k * 0.4f, s * f.R * 0.8f, f.R * 0.1f}, {f.xNose - 1.45f + k * 0.4f, s * f.R * 0.95f, f.R * 0.3f});  // exhaust stubs
        }
        f.engines.push_back(hub);
    } else {
        std::vector<float> ys;
        if (m.engines == 2) ys = {f.S * 0.17f, -f.S * 0.17f};
        else if (m.engines == 3) ys = {0.0f, f.S * 0.19f, -f.S * 0.19f};
        else ys = {f.S * 0.15f, -f.S * 0.15f, f.S * 0.3f, -f.S * 0.3f};
        float nr = std::max(0.55f, f.R * 0.62f);
        float nlen = f.chord * 1.15f + 1.2f;
        for (float y : ys) {
            if (std::fabs(y) < 0.1f) {
                Vec3f hub = cowling(mb, p, cowlMat, {f.xNose - 0.05f, 0, 0}, f.R * 0.95f, f.L * 0.08f);
                f.engines.push_back(hub);
                continue;
            }
            float rise = f.dihedral * std::fabs(y) / (f.S * 0.5f);
            Vec3f front{f.xLE + 1.2f, y, f.zWing + rise + (m.wing == "low" ? 0.15f : 0.0f)};
            nacelle(mb, p, upper, cowlMat, front, nr, nlen, radial);
            f.engines.push_back(front + Vec3f{0.15f, 0, 0});
        }
    }

    // ---- wings
    mb.setMaterial(upper);
    float sweep = f.chord * (1.0f - f.taper) * 0.3f;
    if (biplane) {
        float zLow = -f.R * 0.75f, zUp = f.R + 1.15f;
        for (int s : {1, -1}) {
            wing(mb, {f.xLE, 0, zLow}, f.chord, f.chord * f.taper, s * f.S * 0.48f, 0.05f, 0.25f, 0.11f);
            wing(mb, {f.xLE + 0.35f, 0, zUp}, f.chord, f.chord * f.taper, s * f.S * 0.5f, 0.15f, 0.12f, 0.11f);
        }
        // Interplane struts, cabane and bracing wires.
        for (int s : {1, -1}) {
            for (float yy : {f.S * 0.18f, f.S * 0.4f}) {
                float y = s * yy;
                mb.bar({f.xLE - 0.2f, y, zLow + 0.1f}, {f.xLE + 0.05f, y, zUp}, 0.06f, 0.1f, {1, 0, 0});
                mb.bar({f.xLE - f.chord * 0.75f, y, zLow + 0.1f}, {f.xLE - f.chord * 0.55f, y, zUp}, 0.06f, 0.1f, {1, 0, 0});
            }
            mb.bar({f.xLE - 0.1f, s * 0.4f, f.R * 0.6f}, {f.xLE + 0.2f, s * 0.7f, zUp}, 0.05f, 0.08f, {1, 0, 0});
        }
        mb.setMaterial(p.rope);
        for (int s : {1, -1}) mb.polyline({{f.xLE - 0.3f, s * f.S * 0.18f, zLow + 0.1f}, {f.xLE - 0.2f, s * f.S * 0.4f, zUp}}, 0.012f, 3);
        f.zWing = zUp;
        f.dihedral = 0.12f;
    } else if (m.wing == "invgull") {
        // Ju 87: the inner panels slope down to the undercarriage legs, the outer panels rise.
        float yk = f.S * 0.17f, zk = f.zWing - 0.55f;
        for (int s : {1, -1}) {
            wing(mb, {f.xLE, 0, f.zWing}, f.chord, f.chord * 0.95f, s * yk, 0.05f, zk - f.zWing, 0.13f);
            wing(mb, {f.xLE - 0.05f, s * yk, zk}, f.chord * 0.95f, f.chord * f.taper, s * (f.S * 0.5f - yk), sweep, f.dihedral * 1.3f, 0.12f);
        }
    } else {
        for (int s : {1, -1}) wing(mb, {f.xLE, 0, f.zWing}, f.chord, f.chord * f.taper, s * f.S * 0.5f, sweep, f.dihedral, 0.12f);
        if (m.wing == "parasol") {
            // The wing sits on a pylon above the hull, braced by struts.
            mb.setMaterial(upper);
            mb.taperedBox({f.xLE - f.chord * 0.85f, -f.R * 0.35f, f.R * (boat ? 1.2f : 0.8f)}, {f.xLE - f.chord * 0.1f, f.R * 0.35f, f.zWing}, 0.1f, 0.3f);
            for (int s : {1, -1}) {
                mb.bar({f.xLE - f.chord * 0.4f, s * f.R * 0.8f, 0.0f}, {f.xLE - f.chord * 0.45f, s * f.S * 0.2f, f.zWing}, 0.08f, 0.2f, {1, 0, 0});
                mb.bar({f.xLE - f.chord * 0.7f, s * f.R * 0.8f, 0.0f}, {f.xLE - f.chord * 0.7f, s * f.S * 0.2f, f.zWing}, 0.08f, 0.2f, {1, 0, 0});
            }
        }
    }

    // ---- tail
    mb.setMaterial(upper);
    tailSurfaces(mb, f, m, m.tail == "twin");

    // ---- canopies and gun positions
    mb.setMaterial(p.glass);
    float cz = f.R * 0.78f;
    if (m.canopy == "fighter") {
        mb.sphere({f.xLE - f.chord * 0.45f, 0, cz}, 0.75f, f.R * 0.42f, f.R * 0.45f, 12, 6);
    } else if (m.canopy == "long") {
        mb.sphere({f.xLE - f.chord * 0.55f, 0, cz}, std::max(1.4f, f.L * 0.17f), f.R * 0.48f, f.R * 0.42f, 14, 6);
        mb.setMaterial(p.black);
        for (int k = -2; k <= 2; ++k) {
            float x = f.xLE - f.chord * 0.55f + k * f.L * 0.06f;
            mb.bar({x, -f.R * 0.45f, cz}, {x, f.R * 0.45f, cz}, 0.04f, 0.04f, {1, 0, 0});  // canopy frames
        }
    } else if (m.canopy == "cockpit") {
        float x = single ? f.xLE - f.chord * 0.4f : f.xNose - f.L * 0.2f;
        mb.sphere({x, 0, cz}, std::max(0.9f, f.L * 0.07f), f.R * 0.6f, f.R * 0.4f, 12, 6);
    } else if (m.canopy == "open") {
        mb.setMaterial(p.dark);
        for (float x : {f.xLE - f.chord * 0.45f, f.xLE - f.chord * 1.15f}) mb.disc({x, 0, f.R * 0.92f}, {0, 0, 1}, f.R * 0.45f, 10);
        mb.setMaterial(p.glass);
        mb.flatQuad({f.xLE - f.chord * 0.3f, -0.25f, f.R * 0.9f}, {f.xLE - f.chord * 0.3f, 0.25f, f.R * 0.9f}, {f.xLE - f.chord * 0.38f, 0.22f, f.R * 1.25f},
                    {f.xLE - f.chord * 0.38f, -0.22f, f.R * 1.25f});
    }
    if (m.dorsal) {
        mb.setMaterial(p.glass);
        mb.sphere({f.xTail + f.L * 0.45f, 0, f.R * 0.95f}, 0.6f, f.R * 0.38f, f.R * 0.35f, 10, 5);
        mb.setMaterial(p.black);
        mb.tube({f.xTail + f.L * 0.45f, 0, f.R * 1.1f}, {f.xTail + f.L * 0.45f - 1.1f, 0, f.R * 1.2f}, 0.035f, 0.03f, 5);
    }
    if (m.tailGun) {
        mb.setMaterial(p.glass);
        mb.sphere({f.xTail + 0.1f, 0, 0.0f}, 0.45f, f.R * 0.25f, f.R * 0.28f, 10, 5);
        mb.setMaterial(p.black);
        mb.tube({f.xTail, 0, 0}, {f.xTail - 0.9f, 0, -0.05f}, 0.04f, 0.035f, 5);
    }

    // ---- undercarriage and floats
    mb.setMaterial(upper);
    if (m.gear == "fixed" || m.gear == "spats") {
        for (int s : {1, -1}) {
            float y = m.gear == "spats" ? s * f.S * 0.17f : s * 1.1f;
            float zTop = m.wing == "invgull" ? f.zWing - 0.55f : (m.gear == "spats" ? f.zWing : -f.R * 0.6f);
            Vec3f wheel{f.xLE - 0.3f, y, zTop - 1.5f};
            if (m.gear == "spats") {
                mb.lathe(wheel - Vec3f{0.9f, 0, 0}, {1, 0, 0}, {{0.05f, 0}, {0.3f, 0.5f}, {0.32f, 1.1f}, {0.1f, 1.6f}}, 10, false);
                mb.bar(wheel + Vec3f{0.1f, 0, 0.2f}, {wheel.x + 0.1f, y, zTop}, 0.18f, 0.35f, {1, 0, 0});
            } else {
                mb.setMaterial(p.black);
                mb.tube(wheel - Vec3f{0, 0.08f, 0}, wheel + Vec3f{0, 0.08f, 0}, 0.38f, 0.38f, 12);
                mb.setMaterial(upper);
                mb.bar(wheel, {wheel.x + 0.2f, s * 0.35f, -f.R * 0.6f}, 0.06f, 0.1f, {1, 0, 0});
                mb.bar(wheel, {wheel.x - 0.6f, s * 0.35f, -f.R * 0.7f}, 0.06f, 0.1f, {1, 0, 0});
            }
        }
    }
    auto pontoon = [&](Vec3f c, float l, float r) {
        mb.lathe(c + Vec3f{-l * 0.5f, 0, 0}, {1, 0, 0}, {{0.02f, 0}, {r * 0.6f, l * 0.25f}, {r, l * 0.55f}, {r * 0.95f, l * 0.8f}, {r * 0.4f, l * 0.97f}, {0.0f, l}}, 10, false);
    };
    if (m.floats == "twin") {
        for (int s : {1, -1}) {
            Vec3f fc{f.xLE - 0.6f, s * 1.75f, -f.R - 1.35f};
            pontoon(fc, f.L * 0.68f, 0.42f);
            mb.bar(fc + Vec3f{0.9f, 0, 0.3f}, {fc.x + 0.8f, s * 0.5f, f.zWing}, 0.07f, 0.22f, {1, 0, 0});
            mb.bar(fc + Vec3f{-0.9f, 0, 0.3f}, {fc.x - 1.0f, s * 0.5f, -f.R * 0.4f}, 0.07f, 0.22f, {1, 0, 0});
        }
    } else if (m.floats == "struts" || m.floats == "tips") {
        for (int s : {1, -1}) {
            Vec3f tc{f.xLE - f.chord * 0.4f, s * f.S * 0.42f, f.zWing + f.dihedral * 0.84f - (m.floats == "struts" ? 2.2f : 0.5f)};
            pontoon(tc, 2.4f, 0.32f);
            mb.bar(tc + Vec3f{0, 0, 0.2f}, {tc.x, tc.y, f.zWing + f.dihedral * 0.84f}, 0.06f, 0.3f, {1, 0, 0});
        }
    }

    // ---- national markings: upper wing tips, under the wings, fuselage sides
    float r = clamp(f.S * 0.045f, 0.45f, 1.2f);
    for (int s : {1, -1}) {
        // On the wing surface at 72 % of the half span: local chord, sweep, dihedral and airfoil thickness.
        float yy = s * f.S * 0.36f;
        float rise = f.dihedral * 0.72f;
        float chordAt = f.chord * (1.0f - 0.72f * (1.0f - f.taper));
        float xm = f.xLE - sweep * 0.72f - chordAt * 0.4f;
        float th = 0.12f * chordAt * 0.84f;
        marking(mb, def.nation, {xm, yy, f.zWing + rise + th * 0.72f + 0.03f}, {0, 0, 1}, std::min(r, chordAt * 0.42f));
        marking(mb, def.nation, {xm, yy, f.zWing + rise - th * 0.32f - 0.03f}, {0, 0, -1}, std::min(r, chordAt * 0.42f));
        marking(mb, def.nation, {f.xTail + f.L * 0.3f, s * f.R * 0.96f, 0.0f}, {0, float(s), 0}, std::min(r, f.R * 0.8f));
    }
    twoTone(model.parts[0], upper, lower);

    // ---- weapon load: torpedo (sub 0) and bombs (sub 1)
    const sim::TorpedoDef* torpedo = nullptr;
    const sim::BombDef* bombDef = nullptr;
    int bombCount = 0;
    for (const auto& l : def.loadouts) {
        if (l.torpedo && !torpedo) torpedo = l.torpedo;
        if (l.bomb && !bombDef) {
            bombDef = l.bomb;
            bombCount = l.count;
        }
    }
    bool bay = (m.nose == "glazed" || m.canopy == "none") && m.engines >= 2;  // internal bomb bay
    if (torpedo) {
        model.parts.push_back({});
        MeshPart& tp = model.parts.back();
        tp.name = "torpedo";
        tp.kind = PartKind::Ordnance;
        tp.sub = 0;
        MeshBuilder tb(tp);
        float tr = float(torpedo->diameterMm) / 2000.0f;
        float tl = clamp(float(torpedo->massKg) / 160.0f, 4.0f, 5.6f);
        Vec3f c{f.xLE - f.chord * 0.3f, single ? 0.0f : f.R * 0.3f, -f.R - tr - 0.05f};
        tb.setMaterial(p.metal);
        tb.lathe(c - Vec3f{tl * 0.5f, 0, 0}, {1, 0, 0}, {{0.02f, 0}, {tr, tl * 0.08f}, {tr, tl * 0.92f}, {tr * 0.7f, tl * 0.98f}, {0.0f, tl}}, 10, false);
        tb.setMaterial(p.black);
        tb.box(c - Vec3f{tl * 0.5f + 0.1f, 0.25f, 0.01f}, c - Vec3f{tl * 0.5f - 0.15f, -0.25f, -0.01f});  // tail fins
        tp.pivot = c;
    }
    if (bombDef && !bay) {
        model.parts.push_back({});
        MeshPart& bp = model.parts.back();
        bp.name = "bombs";
        bp.kind = PartKind::Ordnance;
        bp.sub = 1;
        MeshBuilder bb(bp);
        float dia = float(bombDef->caliberMm) / 1000.0f, len = dia * 5.2f;
        int n = std::min(bombCount, 6);
        for (int k = 0; k < n; ++k) {
            float y = n == 1 ? 0.0f : (k % 2 ? -1.0f : 1.0f) * (single ? 1.4f + 0.6f * float(k / 2) : f.R * 0.35f + 0.5f * float(k / 2));
            float z = n == 1 || !single ? -f.R - dia * 0.6f : f.zWing - dia * 0.7f;
            bomb(bb, p, {f.xLE - f.chord * 0.35f, y, z}, dia, len);
        }
        bp.pivot = {f.xLE - f.chord * 0.35f, 0, -f.R};
    }

    // ---- propellers
    for (size_t i = 0; i < f.engines.size(); ++i) propeller(model, p, f.engines[i], single ? f.propR : f.propR * 0.95f, 3, int(i));
    model.computeBounds();
    return model;
}

}  // namespace it::procgen
