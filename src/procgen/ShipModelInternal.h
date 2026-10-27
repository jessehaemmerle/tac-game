#pragma once
// Internals shared by the procedural ship generator: ShipModel.cpp (entry point, paint), ShipHull.cpp
// (hull, deck edge, underwater body, anchors), ShipStructures.cpp (superstructure, funnels, masts,
// sensors), ShipWeapons.cpp (turrets, guns, torpedo tubes, light AA) and ShipFittings.cpp (deck
// fittings, railings, boats, aircraft, flags).
#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "procgen/ShipModel.h"

namespace it::procgen::detail {

Vec3f srgb(int r, int g, int b);
Material mat(Vec3f albedo, float rough, float metal, uint8_t id, float emissive = 0.0f);

// Material ids select the texture layer and special shading in data/shaders/ship.hlsl.
enum MatId : uint8_t {
    kHull = 0, kBottom, kBoot, kDeck, kSuper, kMetal, kGlass, kCanvas, kLens, kAircraft, kStrip,
    kDeckLino, kDeckSteel, kGrating, kWood, kFlag, kPlain
};

inline Vec3f v3(double x, double y, double z) { return {float(x), float(y), float(z)}; }
inline Vec3f rotZv(const Vec3f& v, float rad) {
    float c = std::cos(rad), s = std::sin(rad);
    return {v.x * c - v.y * s, v.x * s + v.y * c, v.z};
}
/// Local frame: origin, rotation about z (rad), then tilt about the local y and x axes (rad).
inline Mat4f frame(const Vec3f& origin, float yaw, float pitch = 0.0f, float roll = 0.0f) {
    return Mat4f::translation(origin) * Mat4f::rotation(Quatf::axisAngle({0, 0, 1}, yaw)) *
           Mat4f::rotation(Quatf::axisAngle({0, 1, 0}, pitch)) * Mat4f::rotation(Quatf::axisAngle({1, 0, 0}, roll));
}

/// Area of the weather deck (or a roof) taken by a structure; fittings are only placed on free deck.
struct Footprint {
    float x0, x1, y0, y1;
};

/// A deck surface fittings can stand on (weather deck segments and deckhouse roofs).
struct Roof {
    float x0, x1, y0, y1, z;
};

struct Ctx {
    const sim::ShipClassDef& c;
    const ShipModelOptions& opt;
    PaintScheme paint;
    double L, B, T;
    bool usn, ijn, rn, rm, km, destroyer, cruiser, capital;
    Model model;
    std::mt19937 rng;
    int hull = 0, fittings = 1, fine = 2;  // static parts by level of detail
    std::vector<Footprint> taken;
    std::vector<Roof> roofs;
    double bulwarkEndX = 1e9;  // the bow bulwark ends here (no railing forward of it)
    std::vector<Vec3f> mastTops;  // topmast heads (for antenna wires), fore to aft
    std::vector<Vec3f> yardEnds;  // yardarm ends (signal halyards)
    Vec3f gaffEnd{};              // peak of the mainmast gaff (battle ensign); z = 0: none
    struct Catapult {
        Vec3f pos;
        int part;
    };
    std::vector<Catapult> catapults;  // aircraft on a catapult train with it

    Ctx(const sim::ShipClassDef& cls, const ShipModelOptions& o);

    double x(double u) const { return (0.5 - u) * L; }
    double uOf(double xx) const { return clamp(0.5 - xx / L, 0.0, 1.0); }
    double deck(double u) const { return c.model.deckHeightAt(u); }
    double deckX(double xx) const { return deck(uOf(xx)); }
    /// Height of the weather deck surface (with camber) at model x/y.
    double deckZ(double xx, double yy) const;
    /// Height of whatever a fitting at x/y stands on: the deck or the highest roof covering the point
    /// (roofs above `below` are ignored).
    double surfaceZ(double xx, double yy, double below = 1e9) const;
    void addRoof(const std::vector<Vec2f>& outline, float z);
    /// Half-width of the weather deck edge at model x (accounts for the raked stem).
    double edgeY(double xx) const;
    /// Hull half-width at model x and height z (on the side plating, includes the stem rake near the bow).
    double sideY(double xx, double z) const;
    float rand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); }

    /// New part (the parts vector is reserved up front so builders may keep references).
    int part(const std::string& name, PartKind kind, uint8_t lod = 0, int parent = -1);
    MeshPart& P(int i) { return model.parts[size_t(i)]; }
    MeshBuilder at(int lod) { return MeshBuilder(model.parts[size_t(lod == 0 ? hull : (lod == 1 ? fittings : fine))]); }

    void take(double x0, double x1, double y0, double y1) { taken.push_back({float(std::min(x0, x1)), float(std::max(x0, x1)), float(std::min(y0, y1)), float(std::max(y0, y1))}); }
    void takeCircle(double xx, double yy, double r) { take(xx - r, xx + r, yy - r, yy + r); }
    bool isFree(double xx, double yy, double r) const;
};

// ShipHull.cpp
double bowRakeOffset(const Ctx& cx, double u, double z);
void buildHull(Ctx& cx);
void buildHullFittings(Ctx& cx);   // deck edge, bulwarks, breakwater, anchors, degaussing cable
void buildUnderwater(Ctx& cx);     // bilge keels, shafts, struts, propellers, rudders

// ShipStructures.cpp
void deckhouse(Ctx& cx, const sim::ModelPart& pt, bool hangar);
void bridge(Ctx& cx, const sim::ModelPart& pt);
void pagoda(Ctx& cx, const sim::ModelPart& pt);
void funnel(Ctx& cx, const sim::ModelPart& pt);
void mast(Ctx& cx, const sim::ModelPart& pt, bool tripod);
void tower(Ctx& cx, const sim::ModelPart& pt);
void director(Ctx& cx, const sim::ModelPart& pt);
void radar(Ctx& cx, const sim::ModelPart& pt);
void searchlight(Ctx& cx, const sim::ModelPart& pt);
void crane(Ctx& cx, const sim::ModelPart& pt);
/// Antenna wires and signal halyards between the masts (after all masts are built).
void rigging(Ctx& cx);

// ShipWeapons.cpp
void buildMount(Ctx& cx, size_t mountIndex);
void buildTorpedoMount(Ctx& cx, size_t torpedoMountIndex);
void lightAA(Ctx& cx, const sim::ModelPart& pt);
/// Gun barrel along +x from the breech face at the origin: length, breech and muzzle radius.
void gunBarrel(MeshBuilder& mb, const PaintScheme& p, float length, float rBreech, float rMuzzle, bool swell);

// ShipFittings.cpp
void catapult(Ctx& cx, const sim::ModelPart& pt);
void aircraft(Ctx& cx, const sim::ModelPart& pt);
void boat(Ctx& cx, const sim::ModelPart& pt);
void rafts(Ctx& cx, const sim::ModelPart& pt);
void deckFittings(Ctx& cx);   // bollards, fairleads, vents, hatches, lockers (free deck only)
void railings(Ctx& cx);
void depthCharges(Ctx& cx);
void ensign(Ctx& cx);
/// Wing panel from root to tip (y), airfoil chord along x, with sweep and dihedral (tip rise).
void wing(MeshBuilder& mb, Vec3f root, float rootChord, float tipChord, float span, float sweep, float dihedral, float thick);
/// National aircraft marking (roundel, hinomaru, star, Balkenkreuz, fasces) centred on c, facing n.
void marking(MeshBuilder& mb, sim::Nation nation, Vec3f c, Vec3f n, float r);
/// Ship floatplane of a style, in a local frame: x forward, origin under the main float, z up.
void floatplane(MeshBuilder& mb, const PaintScheme& p, const std::string& style, sim::Nation nation, bool blades = true);
/// Red and white air-recognition bands across the forecastle (Regia Marina, from late 1940).
void recognitionStripes(Ctx& cx);
/// Inclined ladder with handrails from (x0, y, z0) up to (x1, y, z1).
void ladder(MeshBuilder& mb, const PaintScheme& p, Vec3f lo, Vec3f hi, float width);
/// Railing of stanchions and `rails` wires along a polyline (open), height h.
void railing(MeshBuilder& mb, const PaintScheme& p, const std::vector<Vec3f>& pts, float h, int rails, bool closed = false);
/// Carley float, flat in its local x/y plane (transform the builder to place it).
void carleyFloat(MeshBuilder& mb, const PaintScheme& p, float len, float wid);

}  // namespace it::procgen::detail
