#pragma once
// The only window through which AI code sees the world. It exposes full information about the
// side's own ships (the admiral knows his own fleet), the side's contact picture (estimates only),
// torpedo sightings reported by own lookouts, and public knowledge (charts, clock, weather).
// It deliberately offers no access to enemy ships' true state: the AI is fair by construction.
#include <vector>

#include "sim/World.h"

namespace it::ai {

struct TorpedoWarning {
    Vec2 pos;          // where the track was seen
    double heading = 0;
    double speed = 0;  // estimated (m/s)
    double time = 0;   // when it was sighted
    sim::EntityId observer = sim::kNoEntity;
    uint32_t key = 0;  // de-duplication key (per sighting)
    Vec2 predict(double now) const { return pos + bearingVector(heading) * (speed * (now - time)); }
};

/// Own lookouts see aircraft begin an attack run on an own ship.
struct AirAttackWarning {
    sim::EntityId ship = sim::kNoEntity;  // the ship attacked
    double time = 0;
    std::string kind;                     // "torpedo", "dive", "level", "strafe"
    Vec2 from;                            // where the attacker was
    uint32_t key = 0;
};

struct HitReport {
    sim::EntityId ship = sim::kNoEntity;  // own ship that was hit
    double time = 0;
    bool torpedo = false;
    Vec2 pos;
};

class SideView {
public:
    SideView(sim::World& w, sim::Side side) : w_(w), side_(side) {}

    sim::Side side() const { return side_; }
    double time() const { return w_.time(); }

    // Own forces (full knowledge, full control).
    std::vector<sim::Ship*> ownShips() const;
    sim::Ship* own(sim::EntityId id) const;
    std::vector<sim::Group*> ownGroups() const;
    sim::Group* group(int id) const { return w_.group(id); }
    sim::Ship* guide(const sim::Group& g) const { return w_.guideOf(g); }

    // Knowledge of the enemy: estimates only.
    const std::vector<sim::Track>& contacts() const { return w_.picture(side_).tracks(); }
    const sim::Track* contact(uint32_t id) const { return w_.picture(side_).find(id); }
    /// Does the flag (group == nullptr) or the given group know of this contact right now?
    bool known(const sim::Track& t, const std::vector<sim::Ship*>* group = nullptr) const;
    const std::vector<TorpedoWarning>& torpedoWarnings() const { return torpedoWarnings_; }
    const std::vector<AirAttackWarning>& airAttacks() const { return airAttacks_; }
    /// Air picture: enemy formations seen by radar, lookouts and own aircraft.
    const std::vector<sim::AirTrack>& airContacts() const { return w_.picture(side_).airTracks(); }
    bool known(const sim::AirTrack& t) const { return time() >= t.sharedAt; }
    /// Own flights (full knowledge, as of own ships).
    std::vector<sim::Flight*> ownFlights() const;
    const std::vector<HitReport>& recentHits() const { return hits_; }

    // Public knowledge.
    double ambientLight() const { return w_.env().ambientLight(); }
    bool night() const { return w_.env().isNight(); }
    double secondsUntilDawn() const { return w_.env().secondsUntilSunAbove(-6.0); }
    const sim::Terrain& chart() const { return w_.terrain(); }
    const sim::Database& db() const { return w_.db(); }

    // Commands (only for own ships).
    int fireTorpedoes(sim::Ship& s, uint32_t track, int count, double spreadDeg, int speedSetting, std::string* why = nullptr);
    void setDirectorTarget(sim::Ship& s, int director, uint32_t track);
    void setSearchlight(sim::Ship& s, bool on, uint32_t track);
    bool launchAircraft(sim::Ship& s, bool flares, const Vec2& target);
    bool orderFlight(sim::Flight& f, const sim::AirOrder& o);

    /// Feed simulation events; keeps only what this side legitimately learns.
    void observe(const sim::Event& e);
    void prune();

private:
    sim::World& w_;
    sim::Side side_;
    std::vector<TorpedoWarning> torpedoWarnings_;
    std::vector<AirAttackWarning> airAttacks_;
    uint32_t airKey_ = 0;
    std::vector<HitReport> hits_;
};

}  // namespace it::ai
