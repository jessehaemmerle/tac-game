#pragma once
#include "core/Math.h"
#include "sim/Types.h"

namespace it::sim {
class World;
class Ship;
struct Shell;
struct Torpedo;

namespace damage {

enum class HitFace : uint8_t { Side, Deck, End, Superstructure };

struct HitInfo {
    Vec3 local;           // ship-local hit point: x forward, y starboard, z up (m)
    HitFace face = HitFace::Side;
    double obliquityDeg = 0;  // angle between shell path and plate normal
    bool belowWater = false;
    int mount = -1;       // index of mount struck (turret hit)
    double t = 0;         // segment parameter of the hit (0..1)
};

/// Segment-vs-hull test in ship-local space. Returns true and fills `hit` on intersection.
bool intersect(const Ship& ship, const Vec3& p0, const Vec3& p1, HitInfo& hit);
void shellHit(World& w, Ship& target, const Shell& shell, const HitInfo& hit);
void nearMiss(World& w, Ship& target, const Shell& shell, double distance);
void torpedoHit(World& w, Ship& target, const Torpedo& torp, const Vec2& localPoint);
void collision(World& w, Ship& a, Ship& b, double relSpeed);
void grounding(World& w, Ship& s);
/// Fires, flooding, damage control, sinking. Called every tick.
void update(World& w, Ship& s, double dt);

}  // namespace damage
}  // namespace it::sim
