// Armament: gunhouses in the style of each navy (sloped faces, sighting hoods, rangefinder ears, blast
// bags), open and shielded mounts with their carriages, every barrel as its own part so it can recoil,
// torpedo tube mounts with the torpedoes in the tubes, and cosmetic light AA in tubs.
#include "procgen/ShipModelInternal.h"

namespace it::procgen::detail {

void gunBarrel(MeshBuilder& mb, const PaintScheme& p, float L, float rB, float rM, bool swell) {
    mb.setMaterial(p.metal);
    std::vector<Vec2f> prof = {{0.0f, 0.0f},          {rB * 1.18f, 0.0f},        {rB * 1.18f, L * 0.1f}, {rB, L * 0.11f},
                               {rB * 0.96f, L * 0.36f}, {rM * 1.25f, L * 0.38f}, {rM * 1.04f, L * 0.9f}};
    if (swell) {
        prof.push_back({rM * 1.2f, L * 0.975f});
        prof.push_back({rM * 1.2f, L});
    } else {
        prof.push_back({rM, L});
    }
    mb.lathe({0, 0, 0}, {1, 0, 0}, prof, 14, false);
    // Muzzle face and the dark bore.
    float rBore = rM * 0.55f;
    float rEnd = prof.back().x;
    std::vector<Vec2f> face = {{rEnd, L}, {rBore, L}, {rBore, L - 0.6f}};
    mb.lathe({0, 0, 0}, {1, 0, 0}, face, 14, false);
    mb.setMaterial(p.dark);
    mb.disc({L - 0.55f, 0, 0}, {1, 0, 0}, rBore, 12);
}

namespace {

struct Shape {
    float len, wid, h;        // gunhouse plan length, width and height
    float frontWid, chamfer;  // plan: width of the front face and the chamfer depth
    float frontSlope;         // the roof edge is this far behind the base of the front face
    float sideTaper;          // sides lean in by this fraction of the half-width
    float rearRadius;
    float trunnionBack;       // trunnions behind the front face
    float spacing;            // between barrels
    bool enclosed = true;
    bool shield = false;      // open-backed shield
};

Shape shapeFor(const Ctx& cx, const sim::MountDef& m) {
    double cal = m.gun ? m.gun->caliberMm : 127.0;
    float k = float(cal / 203.0);
    const std::string& st = m.style;
    Shape s{};
    // Battleship turrets do not grow in proportion to the calibre.
    float big = k > 1.0f ? std::pow(k, 0.55f) : 1.0f, tall = k > 1.0f ? std::pow(k, 0.5f) : 1.0f, wide = k > 1.0f ? std::pow(k, 0.8f) : 1.0f;
    if (st == "turret3") {
        float g = k > 1.0f ? big : std::max(0.78f, k);
        float h = k > 1.0f ? tall : std::max(0.85f, k), sp = k > 1.0f ? wide : std::max(0.85f, k);
        s = {9.8f * g, 8.8f * g, 3.1f * h, 6.2f * g, 1.4f * g, 1.2f * g, 0.08f, 1.4f * g, 2.0f * g, 1.6f * sp};
        if (cx.usn && cal < 170) s.wid *= 1.05f;  // Brooklyn: wide, flat-faced 6" turrets
    } else if (st == "turret2") {
        float g = k > 1.0f ? big : std::max(0.68f, k);
        float h = k > 1.0f ? tall : std::max(0.8f, k);
        if (cx.ijn) s = {8.6f * g, 7.0f * g, 3.0f * h, 3.6f * g, 2.0f * g, 1.4f * g, 0.1f, 1.6f * g, 2.2f * g, 1.9f * g};
        else s = {8.4f * g, 7.0f * g, 3.0f * h, 5.2f * g, 1.0f * g, 1.1f * g, 0.06f, 2.0f * g, 2.0f * g, 1.9f * g};
    } else if (st == "dd_turret") {
        s = {6.6f, 4.9f, 2.6f, 2.8f, 1.4f, 1.0f, 0.06f, 1.2f, 1.5f, 1.25f};
    } else if (st == "dd_turret1") {
        s = {5.6f, 3.8f, 2.5f, 2.0f, 1.0f, 0.9f, 0.06f, 1.0f, 1.3f, 0.0f};
    } else if (st == "dp_twin") {
        s = {6.6f, 5.0f, 2.6f, 3.6f, 1.2f, 0.9f, 0.08f, 1.4f, 1.6f, 1.15f};
    } else if (st == "dp_single") {
        s = {5.0f, 3.6f, 2.45f, 2.2f, 0.9f, 0.75f, 0.1f, 1.2f, 1.3f, 0.0f};
    } else if (st == "shield") {
        s = {3.2f, 2.8f, 2.25f, 2.0f, 0.6f, 0.45f, 0.05f, 0.6f, 0.5f, 0.0f};
        if (m.barrels > 1) s.wid = 3.6f, s.spacing = 0.9f;  // twin HA mounts with a shield
        s.enclosed = false;
        s.shield = true;
    } else if (st == "casemate") {
        s = {2.0f, 2.0f, 1.8f, 2.0f, 0.0f, 0.0f, 0.0f, 0.3f, 0.6f, 0.0f};
        s.enclosed = false;
    } else {
        s = {2.4f, 1.8f, 1.5f, 1.6f, 0.3f, 0.0f, 0.0f, 0.3f, 0.3f, 0.0f};
        s.enclosed = false;
    }
    return s;
}

/// Base and roof rings of a gunhouse whose plan spans x0 (rear) .. x0 + len.
void gunhouseRings(const Shape& s, float x0, float y, float z0, std::vector<Vec3f>& bot, std::vector<Vec3f>& top) {
    auto o = gunhouseOutline(x0 + s.len * 0.5f, y, s.len, s.wid, s.frontWid, s.chamfer, s.rearRadius);
    bot.clear();
    top.clear();
    for (auto& p : o) {
        bot.push_back({p.x, p.y, z0});
        float t = (p.x - x0) / s.len;  // 0 rear .. 1 front
        float xr = x0 + (p.x - x0) * (s.len - s.frontSlope) / s.len;
        float yr = y + (p.y - y) * (1.0f - s.sideTaper * (0.4f + 0.6f * t));
        top.push_back({xr, yr, z0 + s.h});
    }
}

void sightHood(MeshBuilder& mb, const PaintScheme& p, Vec3f c, float w, float l, float h) {
    mb.setMaterial(p.superstructure);
    mb.taperedBox(c - Vec3f{l * 0.5f, w * 0.5f, 0}, c + Vec3f{l * 0.5f, w * 0.5f, h}, 0.0f, 0.1f, h * 0.6f);
    mb.setMaterial(p.dark);
    mb.flatQuad(c + Vec3f{l * 0.5f - h * 0.3f + 0.01f, -w * 0.35f, h * 0.45f}, c + Vec3f{l * 0.5f - h * 0.3f + 0.01f, w * 0.35f, h * 0.45f},
                c + Vec3f{l * 0.5f - h * 0.5f + 0.01f, w * 0.35f, h * 0.75f}, c + Vec3f{l * 0.5f - h * 0.5f + 0.01f, -w * 0.35f, h * 0.75f});
}

/// Mount carriage for open and shielded guns: training platform, side frames, seats and handwheels.
void carriage(MeshBuilder& mb, MeshBuilder& kb, const PaintScheme& p, Vec3f base, float trunZ, float r, bool hw) {
    mb.setMaterial(p.superstructure);
    mb.ellipticCylinder(base, r, r, 0.2f, 16, 0.0f, 1.0f, true, false);
    mb.lathe(base + Vec3f{0, 0, 0.2f}, {0, 0, 1}, {{0.55f, 0}, {0.45f, 0.3f}, {0.4f, trunZ - base.z - 0.6f}}, 12, false);
    for (int side : {1, -1}) {
        float sy = float(side) * 0.42f;
        mb.flatQuad(base + Vec3f{-0.7f, sy, 0.2f}, base + Vec3f{0.6f, sy, 0.2f}, base + Vec3f{0.25f, sy, trunZ - base.z + 0.25f},
                    base + Vec3f{-0.35f, sy, trunZ - base.z + 0.25f});
    }
    if (!hw) return;
    kb.setMaterial(p.black);
    for (int side : {1, -1}) {
        Vec3f w = base + Vec3f{-0.3f, float(side) * 0.75f, trunZ - base.z - 0.35f};
        kb.torus(w, {0, 1, 0}, 0.22f, 0.025f, 12, 4);
        kb.setMaterial(p.superstructure);
        kb.box(base + Vec3f{-1.0f, float(side) * 0.9f - 0.2f, 0.2f}, base + Vec3f{-0.6f, float(side) * 0.9f + 0.2f, 0.75f});  // seat
        kb.setMaterial(p.black);
    }
}

}  // namespace

void buildMount(Ctx& cx, size_t mi) {
    const sim::MountDef& m = cx.c.mounts[mi];
    Shape s = shapeFor(cx, m);
    double cal = m.gun ? m.gun->caliberMm : 127.0;
    float x = float(m.x), y = float(m.y), z = float(m.z);
    double deckZ = cx.deckZ(m.x, m.y);
    float gz0 = s.enclosed ? z - s.h * 0.45f : float(deckZ);
    bool casemate = m.style == "casemate";
    if (!s.enclosed) gz0 = casemate ? z - 0.5f : std::max(float(deckZ), z - 1.6f);
    float barrelLen = m.gun ? float(m.gun->caliberMm / 1000.0 * m.gun->calibres) : 5.0f;
    float rM = float(cal / 1000.0) * 0.82f, rB = float(cal / 1000.0) * (cal > 150 ? 1.55f : 1.35f);
    bool swell = cx.ijn && cal > 130;
    float r = std::max(s.len, s.wid) * 0.5f;
    if (m.style != "casemate") cx.takeCircle(x, y, s.enclosed ? r : 1.6f);

    // Barbette or gun platform (static).
    {
        MeshBuilder mb = cx.at(0);
        mb.setMaterial(cx.paint.superstructure);
        if (s.enclosed && gz0 > deckZ + 0.15) {
            float br = std::min(s.wid, s.len) * 0.46f;
            mb.ellipticCylinder(v3(x, y, deckZ - 0.1), br, br, float(gz0 - deckZ + 0.05), 24, 0.0f, 1.0f, true, false);
            MeshBuilder fb = cx.at(1);
            fb.setMaterial(cx.paint.superstructure);
            fb.torus(v3(x, y, gz0 - 0.08f), {0, 0, 1}, br + 0.04f, 0.08f, 24, 4);
        } else if (!s.enclosed && gz0 > deckZ + 0.4) {
            // Raised gun platform on a trunk.
            mb.ellipticCylinder(v3(x, y, deckZ - 0.1), 0.9f, 0.9f, float(gz0 - deckZ), 12, 0.0f, 1.0f, false, false);
            auto pf = ellipseOutline(x, y, 2.1f, 2.1f, 16);
            std::vector<Vec3f> lo, hi;
            for (auto& q : pf) {
                lo.push_back({q.x, q.y, gz0 - 0.25f});
                hi.push_back({q.x, q.y, gz0});
            }
            mb.loft(lo, hi, true, true);
        }
    }

    // Training part: gunhouse, or carriage and shield.
    int ti = cx.part("mount_" + m.id, PartKind::Turret, 0);
    cx.P(ti).mount = int(mi);
    cx.P(ti).pivot = {x, y, gz0};
    int tdi = cx.part("mount_" + m.id + "_detail", PartKind::Turret, 2);
    cx.P(tdi).mount = int(mi);
    cx.P(tdi).pivot = {x, y, gz0};
    MeshBuilder mb(cx.P(ti));
    MeshBuilder kb(cx.P(tdi));
    float x0 = x - s.len * 0.45f;  // rear of the gunhouse
    float frontX = x0 + s.len;
    float trunX = s.enclosed ? frontX - s.trunnionBack : x + 0.15f;
    if (s.shield) trunX = x + 0.1f;
    std::vector<float> offsets;
    for (int b = 0; b < m.barrels; ++b) offsets.push_back((float(b) - (m.barrels - 1) * 0.5f) * s.spacing);

    if (s.enclosed) {
        std::vector<Vec3f> bot, top;
        gunhouseRings(s, x0, y, gz0, bot, top);
        mb.setMaterial(cx.paint.superstructure);
        mb.loft(bot, top, true, false);
        // Gun ports: dark slots in the front face.
        mb.setMaterial(cx.paint.dark);
        for (float off : offsets) {
            float fx = frontX - s.frontSlope * 0.45f + 0.03f;
            mb.flatQuad(v3(fx, y + off - rB * 1.3f, z - rB * 1.6f), v3(fx, y + off + rB * 1.3f, z - rB * 1.6f),
                        v3(fx - s.frontSlope * 0.25f, y + off + rB * 1.3f, z + rB * 2.4f), v3(fx - s.frontSlope * 0.25f, y + off - rB * 1.3f, z + rB * 2.4f));
        }
        float roofZ = gz0 + s.h;
        bool cruiserMain = m.style == "turret3" || m.style == "turret2";
        // Sighting hoods for the pointer and trainer at the front of the roof.
        float hoodY = s.wid * 0.36f;
        for (int side : {1, -1}) sightHood(mb, cx.paint, v3(frontX - s.frontSlope - s.len * 0.18f, y + side * hoodY * (1.0f - s.sideTaper), roofZ - 0.02f), 0.55f, 1.0f, 0.42f);
        if (m.style == "turret3") sightHood(mb, cx.paint, v3(frontX - s.frontSlope - s.len * 0.2f, y, roofZ - 0.02f), 0.55f, 1.0f, 0.42f);
        // Rangefinder ears at the rear (cruiser main battery).
        if (cruiserMain && cal >= 150) {
            float rf = s.wid * (cx.ijn ? 1.0f : 1.12f);
            mb.setMaterial(cx.paint.superstructure);
            Vec3f c = v3(x0 + s.len * 0.2f, y, roofZ - 0.45f);
            if (cx.ijn) c.z = roofZ + 0.35f;
            mb.tube(c - Vec3f{0, rf * 0.5f, 0}, c + Vec3f{0, rf * 0.5f, 0}, 0.32f, 0.32f, 10, true);
            for (int side : {1, -1}) {
                Vec3f e = c + Vec3f{0, side * rf * 0.5f, 0};
                mb.box(e + Vec3f{-0.55f, -0.45f, -0.5f}, e + Vec3f{0.5f, 0.45f, 0.55f});
                mb.setMaterial(cx.paint.dark);
                mb.box(e + Vec3f{0.5f, -0.3f, -0.12f}, e + Vec3f{0.53f, 0.3f, 0.2f});
                mb.setMaterial(cx.paint.superstructure);
            }
        }
        // Roof hatches, ventilator, rear door and ladder rungs (close-ups).
        kb.setMaterial(cx.paint.superstructure);
        kb.box(v3(x0 + s.len * 0.3f - 0.4f, y - s.wid * 0.12f - 0.4f, roofZ), v3(x0 + s.len * 0.3f + 0.4f, y - s.wid * 0.12f + 0.4f, roofZ + 0.12f));
        kb.box(v3(x0 + s.len * 0.45f - 0.35f, y + s.wid * 0.15f - 0.35f, roofZ), v3(x0 + s.len * 0.45f + 0.35f, y + s.wid * 0.15f + 0.35f, roofZ + 0.1f));
        kb.lathe(v3(x0 + s.len * 0.12f, y + s.wid * 0.25f, roofZ), {0, 0, 1}, {{0.2f, 0}, {0.2f, 0.4f}, {0.38f, 0.45f}, {0.0f, 0.6f}}, 10, false);
        kb.box(v3(x0 - 0.05f, y - 0.4f, gz0 + 0.2f), v3(x0 + 0.03f, y + 0.4f, gz0 + 1.9f));
        kb.setMaterial(cx.paint.metal);
        for (float zr = gz0 + 0.4f; zr < roofZ; zr += 0.35f) kb.bar(v3(x0 - 0.12f, y + 0.8f, zr), v3(x0 - 0.12f, y + 1.3f, zr), 0.04f, 0.04f);
        // Splinter plating lip around the roof edge on IJN turrets.
        if (cx.ijn) {
            MeshBuilder fb(cx.P(ti));
            fb.setMaterial(cx.paint.superstructure);
            fb.polyline(top, 0.05f, 4, true);
        }
    } else if (casemate) {
        // Casemate gun in the hull side: an embrasure in the plating, the gun's armoured mantlet flush
        // with the side (it turns with the gun), the barrel poking out.
        float side = y >= 0.0f ? 1.0f : -1.0f;
        MeshBuilder hb = cx.at(0);
        hb.setMaterial(cx.paint.dark);
        float hy = side * (float(cx.sideY(m.x, z)) + 0.03f);
        hb.flatQuad(v3(x - 1.3f, hy, z - 1.0f), v3(x + 1.3f, hy, z - 1.0f), v3(x + 1.3f, hy, z + 1.0f), v3(x - 1.3f, hy, z + 1.0f));
        mb.setMaterial(cx.paint.hull);
        mb.box(v3(x + 0.85f, y - 1.0f, z - 0.85f), v3(x + 1.2f, y + 1.0f, z + 0.85f));
        mb.setMaterial(cx.paint.dark);
        mb.box(v3(x + 1.2f, y - 0.25f, z - 0.35f), v3(x + 1.22f, y + 0.25f, z + 0.45f));
    } else {
        // Open mount: carriage on a pedestal; shielded mounts add the bent-plate shield and a roof.
        carriage(mb, kb, cx.paint, v3(x, y, gz0), z, s.shield ? 1.4f : 1.2f, true);
        if (s.shield) {
            mb.setMaterial(cx.paint.superstructure);
            float hw = s.wid * 0.5f;
            std::vector<Vec3f> sh;
            const int n = 10;
            for (int k = 0; k <= n; ++k) {
                float a = -float(kPi) * 0.5f + float(kPi) * float(k) / n;
                float px = x + 0.6f + std::cos(a) * 0.7f - (std::fabs(std::sin(a)) > 0.95f ? 0.9f : 0.0f);
                sh.push_back(v3(px, y + std::sin(a) * hw, gz0 + 0.2f));
            }
            // Extend the sides aft.
            sh.front() = v3(x - s.len * 0.45f, y - hw, gz0 + 0.2f);
            sh.back() = v3(x - s.len * 0.45f, y + hw, gz0 + 0.2f);
            for (size_t i = 0; i + 1 < sh.size(); ++i) {
                Vec3f a = sh[i], b = sh[i + 1];
                mb.flatQuad(a, b, b + Vec3f{-0.15f, 0, s.h}, a + Vec3f{-0.15f, 0, s.h});
            }
            // Roof plate over the gun (USN Mk 21 and IJN shields).
            std::vector<Vec3f> roofPts;
            for (auto& p : sh) roofPts.push_back(p + Vec3f{-0.15f, 0, s.h});
            mb.polygon(roofPts);
            mb.setMaterial(cx.paint.dark);
            for (float off : offsets) mb.box(v3(x + 1.25f, y + off - 0.28f, z - 0.3f), v3(x + 1.32f, y + off + 0.28f, z + 0.55f));
            sightHood(mb, cx.paint, v3(x + 0.3f, y - hw * 0.6f, gz0 + 0.2f + s.h - 0.02f), 0.45f, 0.8f, 0.35f);
        } else {
            // Fuze setter and ready-use rack beside an open mount.
            kb.setMaterial(cx.paint.superstructure);
            kb.box(v3(x - 1.1f, y - 1.0f, gz0 + 0.2f), v3(x - 0.6f, y - 0.6f, gz0 + 1.3f));
            kb.setMaterial(cx.paint.brass);
            for (int k = 0; k < 4; ++k) kb.tube(v3(x - 1.0f + 0.12f * k, y + 0.9f, gz0 + 0.25f), v3(x - 1.0f + 0.12f * k, y + 0.9f, gz0 + 1.0f), 0.05f, 0.05f, 6, true);
        }
    }

    // Cradle: elevates about the trunnions; blast bags at the gun ports.
    int ci = cx.part("cradle_" + m.id, PartKind::Barrel, 0, ti);
    MeshPart& cp = cx.P(ci);
    cp.mount = int(mi);
    cp.pivot = {trunX, y, z};
    MeshBuilder cb(cp);
    for (float off : offsets) {
        Vec3f t = v3(trunX, y + off, z);
        if (s.enclosed) {
            // Canvas bloomers, painted over in the ship's colours.
            Material bloomer = cx.paint.canvas;
            bloomer.albedo = cx.paint.superstructure.albedo * 0.95f;
            cb.setMaterial(bloomer);
            float fx = frontX - s.frontSlope * 0.45f - trunX;
            cb.lathe(t + Vec3f{fx - 0.1f, 0, 0}, {1, 0, 0}, {{rB * 1.9f, 0}, {rB * 1.75f, 0.25f}, {rB * 1.35f, 0.55f}, {rB * 1.12f, 0.8f}, {rB * 1.08f, 0.95f}}, 12, false);
            cb.setMaterial(cx.paint.metal);
            cb.box(t + Vec3f{-1.2f, -rB * 1.3f, -rB * 1.4f}, t + Vec3f{0.6f, rB * 1.3f, rB * 1.4f});
        } else {
            // Slide with recoil cylinders above and below, breech block behind the trunnions.
            cb.setMaterial(cx.paint.metal);
            cb.box(t + Vec3f{-0.9f, -rB * 1.5f, -rB * 1.3f}, t + Vec3f{1.2f, rB * 1.5f, rB * 1.3f});
            cb.tube(t + Vec3f{-0.6f, 0, rB * 1.8f}, t + Vec3f{1.5f, 0, rB * 1.8f}, rB * 0.55f, rB * 0.55f, 8, true);
            cb.tube(t + Vec3f{-0.6f, 0, -rB * 1.8f}, t + Vec3f{1.3f, 0, -rB * 1.8f}, rB * 0.5f, rB * 0.5f, 8, true);
            cb.box(t + Vec3f{-1.5f, -rB * 1.2f, -rB * 1.2f}, t + Vec3f{-0.9f, rB * 1.2f, rB * 1.2f});
            if (!s.shield) {
                // Loading tray behind the breech.
                cb.setMaterial(cx.paint.superstructure);
                cb.box(t + Vec3f{-2.3f, -0.2f, -rB * 1.0f}, t + Vec3f{-1.5f, 0.2f, -rB * 0.85f});
            }
        }
        cb.setMaterial(cx.paint.metal);
        cb.tube(t + Vec3f{0, -rB * 1.6f, 0}, t + Vec3f{0, rB * 1.6f, 0}, rB * 0.45f, rB * 0.45f, 8, true);  // trunnions
    }
    // Barrels: one part each so every gun can recoil on its own.
    for (int b = 0; b < m.barrels; ++b) {
        int gi = cx.part("gun_" + m.id + "_" + std::to_string(b), PartKind::Gun, 0, ci);
        MeshPart& gp = cx.P(gi);
        gp.mount = int(mi);
        gp.sub = b;
        gp.axis = {1, 0, 0};
        gp.pivot = {trunX, y + offsets[size_t(b)], z};
        MeshBuilder gb(gp);
        float breechBack = barrelLen * (s.enclosed ? 0.26f : 0.2f);
        gb.setTransform(Mat4f::translation(v3(trunX - breechBack, y + offsets[size_t(b)], z)));
        gunBarrel(gb, cx.paint, barrelLen, rB, rM, swell);
        gb.clearTransform();
    }
}

// --------------------------------------------------------------------------------------- torpedoes
void buildTorpedoMount(Ctx& cx, size_t ti) {
    const sim::TorpedoMountDef& t = cx.c.torpedoMounts[ti];
    double deckZ = cx.surfaceZ(t.x, t.y, cx.deckX(t.x) + 3.0);  // on the deck or a low deckhouse
    float dia = t.torpedo ? float(t.torpedo->diameterMm / 1000.0) : 0.533f;
    float r = dia * 0.5f + 0.06f;
    float len = dia > 0.6f ? 9.2f : 7.4f;
    float tl = dia > 0.6f ? 9.0f : 7.2f;  // torpedo length
    float x = float(t.x), y = float(t.y), z0 = float(deckZ);
    float tz = z0 + 1.0f;
    float pitch = r * 2.12f;
    bool ijnShield = cx.ijn && cx.destroyer && t.tubes >= 3;
    // Pedestal (static).
    MeshBuilder sb = cx.at(0);
    sb.setMaterial(cx.paint.superstructure);
    sb.ellipticCylinder(v3(x, y, deckZ - 0.1), 1.0f, 1.0f, 0.5f, 14, 0.0f, 1.0f, true, false);
    cx.takeCircle(x, y, len * 0.5);
    int pi = cx.part("torpedo_" + t.id, PartKind::TorpedoMount, 0);
    MeshPart& p = cx.P(pi);
    p.mount = int(ti);
    p.pivot = {x, y, z0 + 0.6f};
    MeshBuilder mb(p);
    mb.setMaterial(cx.paint.superstructure);
    float w = t.tubes * pitch + 0.4f;
    mb.ellipticCylinder(v3(x, y, z0 + 0.4f), std::max(1.3f, w * 0.55f), std::max(1.3f, w * 0.55f), 0.22f, 18, 0.0f, 1.0f, true, false);
    for (int k = 0; k < t.tubes; ++k) {
        float off = (float(k) - (t.tubes - 1) * 0.5f) * pitch;
        Vec3f a = v3(x - len * 0.45f, y + off, tz), b = v3(x + len * 0.55f, y + off, tz);
        mb.setMaterial(cx.paint.superstructure);
        mb.tube(a, b, r, r * 0.97f, 12, false);
        // Breech door, stiffening rings and the open muzzle with the dark bore.
        mb.setMaterial(cx.paint.metal);
        mb.lathe(a, {1, 0, 0}, {{0.0f, -0.25f}, {r * 0.8f, -0.25f}, {r * 1.1f, -0.1f}, {r * 1.1f, 0.15f}, {r, 0.2f}}, 12, false);
        for (float f : {0.3f, 0.62f}) mb.torus(lerp(a, b, f), {1, 0, 0}, r + 0.02f, 0.04f, 12, 4);
        mb.torus(b, {1, 0, 0}, r, 0.045f, 12, 4);
        mb.setMaterial(cx.paint.dark);
        mb.disc(b - Vec3f{0.3f, 0, 0}, {1, 0, 0}, r * 0.95f, 12);
        // Torpedo in the tube (slides out when fired).
        int ki = cx.part("torpedo_" + t.id + "_" + std::to_string(k), PartKind::Torpedo, 1, pi);
        MeshPart& kp = cx.P(ki);
        kp.mount = int(ti);
        kp.sub = k;
        kp.axis = {1, 0, 0};
        kp.pivot = b;
        MeshBuilder tb(kp);
        float tr = dia * 0.5f;
        Vec3f tail = b - Vec3f{0.32f + tl, 0, 0};
        tb.setMaterial(mat(srgb(40, 42, 44), 0.4f, 0.6f, kMetal));
        tb.lathe(tail, {1, 0, 0}, {{0.0f, -0.2f}, {tr * 0.3f, 0.0f}, {tr * 0.85f, 0.7f}, {tr, 1.4f}, {tr, tl - 0.7f}}, 12, false);
        tb.setMaterial(mat(srgb(150, 130, 70), 0.4f, 0.8f, kPlain));  // warhead
        tb.lathe(tail, {1, 0, 0}, {{tr, tl - 0.7f}, {tr * 0.8f, tl - 0.25f}, {0.0f, tl}}, 12, false);
        for (int f = 0; f < 4; ++f) {
            float a = float(f) * float(kPi) * 0.5f;
            Vec3f d{0, std::cos(a), std::sin(a)};
            tb.flatQuad(tail + d * (tr * 0.2f), tail + Vec3f{0.55f, 0, 0} + d * (tr * 0.45f), tail + Vec3f{0.55f, 0, 0} + d * (tr * 1.05f),
                        tail + d * (tr * 1.05f));
        }
        tb.disc(tail - Vec3f{0.18f, 0, 0}, {1, 0, 0}, tr * 0.6f, 8);
    }
    if (ijnShield) {
        // Enclosed splinter shield of the IJN destroyer mounts: rounded front, flat roof, open aft end.
        mb.setMaterial(cx.paint.superstructure);
        float hw = w * 0.5f + 0.15f, hz = tz + r + 0.55f;
        float fx = x + len * 0.12f, rx0 = x - len * 0.35f;
        std::vector<Vec3f> lo, hi;
        for (int k = 0; k <= 12; ++k) {
            float a = -float(kPi) * 0.5f + float(kPi) * float(k) / 12.0f;
            lo.push_back(v3(fx + std::cos(a) * 1.2f, y + std::sin(a) * hw, z0 + 0.62f));
            hi.push_back(v3(fx + std::cos(a) * 1.0f - 0.3f, y + std::sin(a) * hw * 0.95f, hz));
        }
        lo.insert(lo.begin(), v3(rx0, y - hw, z0 + 0.62f));
        hi.insert(hi.begin(), v3(rx0, y - hw * 0.95f, hz));
        lo.push_back(v3(rx0, y + hw, z0 + 0.62f));
        hi.push_back(v3(rx0, y + hw * 0.95f, hz));
        for (size_t i = 0; i + 1 < lo.size(); ++i) mb.flatQuad(lo[i], lo[i + 1], hi[i + 1], hi[i]);
        mb.polygon(hi);
        mb.setMaterial(cx.paint.dark);
        for (int k = 0; k < t.tubes; ++k) {
            float off = (float(k) - (t.tubes - 1) * 0.5f) * pitch;
            mb.disc(v3(fx + 1.21f, y + off, tz), {1, 0, 0}, r * 1.05f, 10);
        }
    } else {
        // Trainer's position: a small hooded cab at the after end (USN) or a seat with a sight.
        mb.setMaterial(cx.paint.superstructure);
        float cabX = x - len * 0.45f - 0.2f;
        if (cx.usn) {
            mb.taperedBox(v3(cabX - 1.2f, y - 0.65f, z0 + 0.6f), v3(cabX, y + 0.65f, z0 + 2.3f), 0.15f, 0.1f, 0.0f);
            mb.setMaterial(cx.paint.glass);
            mb.box(v3(cabX - 0.05f, y - 0.45f, z0 + 1.7f), v3(cabX + 0.02f, y + 0.45f, z0 + 2.0f));
        } else {
            mb.box(v3(cabX - 0.6f, y - 0.3f, z0 + 0.6f), v3(cabX, y + 0.3f, z0 + 1.2f));
            mb.tube(v3(cabX - 0.3f, y, z0 + 1.2f), v3(cabX - 0.3f, y, z0 + 1.9f), 0.06f, 0.06f, 6, false);
            mb.box(v3(cabX - 0.45f, y - 0.1f, z0 + 1.9f), v3(cabX - 0.1f, y + 0.1f, z0 + 2.1f));
        }
    }
}

// ---------------------------------------------------------------------------------------- light AA
namespace {

struct AAType {
    float cal;        // mm
    int barrels;
    int rows;         // barrel rows (2 for the pom-pom)
    float barrelLen;  // m
    float spacing;
    bool shield, tub, flashHider, magazineTop;
    float tubR;
};

AAType aaType(const std::string& st) {
    if (st.rfind("usn_28mm", 0) == 0) return {28, 4, 1, 2.1f, 0.32f, false, true, false, true, 2.3f};
    if (st.rfind("usn_40mm", 0) == 0) return {40, 2, 1, 2.6f, 0.55f, true, true, true, true, 2.4f};
    if (st.rfind("usn_50cal", 0) == 0) return {12.7f, 1, 1, 1.15f, 0.0f, false, false, false, false, 0.0f};
    if (st.rfind("ijn_25mm_3", 0) == 0) return {25, 3, 1, 1.9f, 0.32f, false, false, true, true, 0.0f};
    if (st.rfind("ijn_25mm", 0) == 0) return {25, 2, 1, 1.9f, 0.36f, false, false, true, true, 0.0f};
    if (st.rfind("ijn_13mm", 0) == 0) return {13.2f, 2, 1, 1.25f, 0.3f, false, false, false, true, 0.0f};
    if (st.rfind("rn_pompom", 0) == 0) return {40, 4, 2, 1.6f, 0.3f, true, true, false, false, 2.8f};
    if (st.rfind("rn_05in", 0) == 0) return {12.7f, 4, 1, 1.1f, 0.14f, false, false, false, false, 0.0f};
    return {20, 1, 1, 2.2f, 0.0f, true, st.find("tub") != std::string::npos, true, true, 1.3f};  // 20 mm Oerlikon
}

}  // namespace

void lightAA(Ctx& cx, const sim::ModelPart& pt) {
    AAType a = aaType(pt.style);
    double xx = cx.x(pt.f);
    float x = float(xx), y = float(pt.y);
    float z0 = float(cx.deckZ(xx, pt.y) + pt.z);
    float base = 0.0f;
    if (pt.y > 0.5) base = 90.0f;
    else if (pt.y < -0.5) base = -90.0f;
    else base = pt.f < 0.5 ? 0.0f : 180.0f;
    cx.model.aa.push_back({v3(x, y, z0), base, 150.0f, a.barrels * a.rows, a.cal});
    int index = int(cx.model.aa.size()) - 1;
    cx.takeCircle(x, y, std::max(1.0f, a.tubR));
    // Tub (circular splinter shield) or a raised platform.
    MeshBuilder sb = cx.at(1);
    sb.setMaterial(cx.paint.superstructure);
    if (pt.z > 0.6) {
        sb.ellipticCylinder(v3(x, y, cx.deckZ(xx, pt.y) - 0.1), 0.35f, 0.35f, float(pt.z) - 0.1f, 8, 0.0f, 1.0f, false, false);
        auto pf = ellipseOutline(x, y, std::max(1.4f, a.tubR + 0.1f), std::max(1.4f, a.tubR + 0.1f), 16);
        std::vector<Vec3f> lo, hi;
        for (auto& q : pf) {
            lo.push_back({q.x, q.y, z0 - 0.2f});
            hi.push_back({q.x, q.y, z0});
        }
        sb.loft(lo, hi, true, true);
    }
    if (a.tub) {
        float tr = a.tubR;
        std::vector<Vec3f> lo, hi;
        for (int k = 0; k <= 24; ++k) {
            float ang = float(k) / 24.0f * float(kTwoPi);
            lo.push_back(v3(x + std::cos(ang) * tr, y + std::sin(ang) * tr, z0));
            hi.push_back(v3(x + std::cos(ang) * tr, y + std::sin(ang) * tr, z0 + 1.15f));
        }
        for (size_t i = 0; i + 1 < lo.size(); ++i) sb.flatQuad(lo[i], lo[i + 1], hi[i + 1], hi[i]);
        MeshBuilder kb = cx.at(2);
        kb.setMaterial(cx.paint.superstructure);
        kb.polyline(hi, 0.04f, 4);
        // Ready-use ammunition lockers inside the tub.
        kb.box(v3(x - tr * 0.75f, y - 0.35f, z0), v3(x - tr * 0.45f, y + 0.35f, z0 + 0.8f));
    }
    // Mount (trains) and guns (elevate).
    int mi = cx.part("aa_mount", PartKind::AAMount, 1);
    MeshPart& mp = cx.P(mi);
    mp.mount = index;
    mp.pivot = v3(x, y, z0);
    float trunZ = z0 + (a.cal >= 28 ? 1.25f : 1.05f);
    MeshBuilder mb(mp);
    mb.setMaterial(cx.paint.superstructure);
    // The guns are built pointing forward; the renderer turns the mount to its stowed bearing.
    float width = a.barrels > 1 ? a.spacing * (a.barrels - 1) + 0.5f : 0.4f;
    if (a.cal >= 28) {
        mb.ellipticCylinder(v3(x, y, z0), 1.1f, 1.1f, 0.25f, 14, 0.0f, 1.0f, true, false);
        for (int side : {1, -1}) mb.flatQuad(v3(x - 0.5f, y + side * width * 0.55f, z0 + 0.25f), v3(x + 0.5f, y + side * width * 0.55f, z0 + 0.25f),
                                              v3(x + 0.2f, y + side * width * 0.55f, trunZ + 0.2f), v3(x - 0.3f, y + side * width * 0.55f, trunZ + 0.2f));
        // Pointer's and trainer's seats with their sights.
        for (int side : {1, -1}) {
            mb.box(v3(x - 0.7f, y + side * (width * 0.55f + 0.5f) - 0.2f, z0 + 0.25f), v3(x - 0.3f, y + side * (width * 0.55f + 0.5f) + 0.2f, z0 + 0.8f));
            mb.box(v3(x - 0.2f, y + side * (width * 0.55f + 0.45f) - 0.08f, trunZ - 0.1f), v3(x + 0.2f, y + side * (width * 0.55f + 0.45f) + 0.08f, trunZ + 0.15f));
        }
    } else {
        mb.lathe(v3(x, y, z0), {0, 0, 1}, {{0.35f, 0}, {0.3f, 0.1f}, {0.12f, 0.2f}, {0.1f, trunZ - z0 - 0.15f}, {0.16f, trunZ - z0}}, 10, false);
    }
    if (a.shield) {
        float sw = std::max(0.9f, width * 0.6f + 0.4f);
        mb.flatQuad(v3(x + 0.45f, y - sw, trunZ - 0.55f), v3(x + 0.45f, y + sw, trunZ - 0.55f), v3(x + 0.4f, y + sw, trunZ + 0.55f), v3(x + 0.4f, y - sw, trunZ + 0.55f));
    }
    int gi = cx.part("aa_guns", PartKind::AAGuns, 1, mi);
    MeshPart& gp = cx.P(gi);
    gp.mount = index;
    gp.pivot = v3(x, y, trunZ);
    MeshBuilder gb(gp);
    float rr = a.cal / 1000.0f * 0.75f + 0.015f;
    for (int row = 0; row < a.rows; ++row)
        for (int b = 0; b < a.barrels; ++b) {
            float off = (float(b) - (a.barrels - 1) * 0.5f) * a.spacing;
            float rz = trunZ + (a.rows > 1 ? (float(row) - 0.5f) * 0.35f : 0.0f);
            Vec3f br = v3(x - 0.3f, y + off, rz), mz = v3(x - 0.3f + a.barrelLen, y + off, rz);
            gb.setMaterial(cx.paint.black);
            gb.tube(br, mz, rr * 1.3f, rr, 8, true);
            gb.box(br - Vec3f{0.45f, rr * 2.2f, rr * 2.2f}, br + Vec3f{0.35f, rr * 2.2f, rr * 2.2f});  // receiver
            if (a.flashHider) gb.lathe(mz, {1, 0, 0}, {{rr * 1.1f, 0}, {rr * 2.2f, 0.28f}, {rr * 2.0f, 0.3f}}, 8, false);
            if (a.magazineTop) {
                gb.setMaterial(cx.paint.metal);
                if (a.cal <= 20.5f) gb.ellipticCylinder(br + Vec3f{-0.05f, 0, rr * 2.2f}, 0.21f, 0.21f, 0.12f, 12, 0.0f, 1.0f, true, true);  // drum
                else gb.box(br + Vec3f{-0.3f, -0.08f, rr * 2.2f}, br + Vec3f{0.15f, 0.08f, rr * 2.2f + 0.35f});                            // clip
            }
        }
    if (a.cal <= 20.5f) {
        // Shoulder rests and the cartwheel sight of the Oerlikon.
        gb.setMaterial(cx.paint.black);
        for (int side : {1, -1}) gb.bar(v3(x - 0.75f, y + side * 0.22f, trunZ - 0.15f), v3(x - 0.45f, y + side * 0.05f, trunZ), 0.05f, 0.05f);
        gb.torus(v3(x + 0.35f, y, trunZ + 0.25f), {1, 0, 0}, 0.12f, 0.01f, 10, 3);
    }
}

}  // namespace it::procgen::detail
