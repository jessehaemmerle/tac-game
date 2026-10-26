#pragma once
// Situation assessment for a group, computed only from the side's contact picture. Enemy strength is
// an estimate with an uncertainty that depends on how well contacts are classified — a cautious
// admiral plans against the pessimistic end, a bold one against the optimistic end.
#include <vector>

#include "ai/Doctrine.h"
#include "ai/SideView.h"

namespace it::ai {

struct ThreatEstimate {
    uint32_t track = 0;
    Vec2 pos, vel;
    double sigma = 0;
    double power = 0, powerSigma = 0;
    double torpedoThreat = 0;
    sim::ShipType type = sim::ShipType::Unknown;
    sim::SizeClass size = sim::SizeClass::Unknown;
    double length = 120;  // estimated hull length (m)
    double distance = 0;  // from group centre
    double bearing = 0;
    bool hostile = false;
    bool fireable = false;  // ROE permits engagement
    double hostileProb = 0.5;
    double age = 0;
    double priority = 0;
};

struct Assessment {
    std::vector<sim::Ship*> ships;
    Vec2 center;
    double power = 0, torpedoes = 0, torpedoFraction = 0, health = 1, ammo = 1, maxSpeedKn = 30;
    std::vector<ThreatEstimate> threats;
    double enemyPower = 0, enemyPowerSigma = 0;
    Vec2 enemyCenter;
    double nearest = 1e9;
    int nearestIdx = -1;
    double knowledge = 0;         // 0..1 how well we know the enemy
    bool detectedByEnemy = false;  // they fired at / illuminated us
    bool underFire = false;
    int recentHits = 0;
    bool night = true;
    double secondsToDawn = 1e9;
    /// Ratio own power / enemy power with a caution-weighted pessimistic margin.
    double ratio(double caution) const;
    const ThreatEstimate* primary() const;
};

/// Combat power of a ship class (gun weight of fire scaled by penetration, torpedoes weighted by night).
double classPower(const sim::ShipClassDef& c, bool night);
/// Current combat power of an own ship (damage, ammunition, torpedoes remaining).
double shipPower(const sim::Ship& s, bool night);
/// Rules of engagement: may this contact be fired upon under the doctrine?
bool fireable(const sim::Track& t, const Doctrine& d, double now);
/// Belief that a contact is an enemy ship.
double hostileProbability(const sim::Track& t, const Doctrine& d);

Assessment assess(const SideView& view, const std::vector<sim::Ship*>& ships, const Doctrine& d);

}  // namespace it::ai
