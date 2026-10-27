#pragma once
// The deterministic naval simulation. Fixed time step (kStep), owns all entities, produces events.
#include <array>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Random.h"
#include "sim/Air.h"
#include "sim/Contacts.h"
#include "sim/Database.h"
#include "sim/Environment.h"
#include "sim/Events.h"
#include "sim/Group.h"
#include "sim/Ship.h"
#include "sim/Terrain.h"

namespace it::sim {

constexpr double kStep = 0.05;  // s

struct Shell {
    uint32_t id = 0;
    EntityId firer = kNoEntity;
    Side side = Side::Neutral;
    const GunDef* gun = nullptr;
    const ShellDef* shell = nullptr;
    Vec3 pos, vel, prevPos;
    double age = 0;
    Vec2 origin;               // firing position
    double burstRange = -1;    // starshells: horizontal distance at which the fuse functions
    uint32_t salvo = 0;
    EntityId intendedTarget = kNoEntity;
    bool alive = true;
};

/// Bookkeeping for spotting: a salvo's fall of shot is observed relative to the target.
struct Salvo {
    uint32_t id = 0;
    EntityId firer = kNoEntity;
    int director = 0;
    EntityId target = kNoEntity;
    int inFlight = 0;
    int landed = 0;
    int hits = 0;
    double sumRangeErr = 0;   // along line of fire (+ = over)
    double sumDeflErr = 0;    // across line of fire (+ = right)
    int overs = 0, shorts = 0;
    bool spotted = false;
};

struct Torpedo {
    uint32_t id = 0;
    EntityId firer = kNoEntity;
    Side side = Side::Neutral;
    const TorpedoDef* def = nullptr;
    Vec2 pos;
    double heading = 0, baseHeading = 0;
    double speed = 0;        // m/s
    double maxRun = 0, run = 0;
    bool deep = false, erratic = false;
    double wanderPhase = 0, wanderAmp = 0;
    bool alive = true;
    mutable std::vector<EntityId> sightedBy;  // observers that reported this track
};

struct Flare {
    uint32_t id = 0;
    Vec3 pos;
    double intensity = 1.0;  // relative light output
    double life = 0, maxLife = 45;
    double fallRate = 4.0;   // m/s
    Side side = Side::Neutral;
    bool aircraft = false;
    bool alive = true;
};

class World {
public:
    World(const Database& db, uint64_t seed);

    // ------------------------------------------------------------------ setup
    Ship& spawnShip(const ShipClassDef& cls, const std::string& name, Side side, const Vec2& pos, double heading,
                    double speedKn, CrewQuality crew);
    Environment& env() { return env_; }
    const Environment& env() const { return env_; }
    Terrain& terrain() { return terrain_; }
    const Terrain& terrain() const { return terrain_; }

    Group& addGroup(const std::string& name, Side side, Formation f, double spacing);
    Group* group(int id);
    const Group* group(int id) const;
    std::vector<Group>& groups() { return groups_; }
    const std::vector<Group>& groups() const { return groups_; }
    /// First operational member of the group (guide), or nullptr.
    Ship* guideOf(const Group& g);

    // ------------------------------------------------------------------ simulation
    void step();  // advances by kStep
    double time() const { return time_; }
    uint64_t tick() const { return tick_; }

    // ------------------------------------------------------------------ access
    const Database& db() const { return db_; }
    Rng& rng() { return rng_; }
    std::vector<std::unique_ptr<Ship>>& ships() { return ships_; }
    const std::vector<std::unique_ptr<Ship>>& ships() const { return ships_; }
    Ship* ship(EntityId id);
    const Ship* ship(EntityId id) const;
    const std::vector<Shell>& shells() const { return shells_; }
    const std::vector<Torpedo>& torpedoes() const { return torpedoes_; }
    const std::vector<Flare>& flares() const { return flares_; }
    std::vector<Salvo>& salvos() { return salvos_; }
    ContactPicture& picture(Side s) { return pictures_[sideIndex(s)]; }
    const ContactPicture& picture(Side s) const { return pictures_[sideIndex(s)]; }

    void emit(Event e);
    std::vector<Event> takeEvents();
    const std::vector<Event>& pendingEvents() const { return events_; }
    /// Complete event history (for debrief), capped.
    const std::vector<Event>& history() const { return history_; }

    // ------------------------------------------------------------------ light model
    /// Light falling on a ship (0..~1.5): ambient + starshells/flares + searchlights + own fires.
    double illuminationOn(const Ship& target) const;
    /// Extra contrast when bright light sources lie behind the target as seen from the observer.
    double backlight(const Vec2& observer, const Ship& target) const;
    /// Light at an arbitrary point (splashes, torpedo wakes).
    double illuminationAt(const Vec3& p) const;

    // ------------------------------------------------------------------ commands
    /// Fire a torpedo spread at a contact using the side's track estimate. Returns torpedoes fired.
    int fireTorpedoes(Ship& s, uint32_t track, int count, double spreadDeg, int speedSetting, std::string* why = nullptr);
    /// Fire a spread along a given true course (deg) from the ship.
    int fireTorpedoesOnCourse(Ship& s, double course, int count, double spreadDeg, int speedSetting, std::string* why = nullptr);
    void setDirectorTarget(Ship& s, int director, uint32_t track);
    void setSearchlight(Ship& s, bool on, uint32_t track = 0);
    /// Catapult a floatplane for reconnaissance or flare dropping over a point.
    bool launchAircraft(Ship& s, bool flares, const Vec2& target);

    // ------------------------------------------------------------------ air units (Air.cpp)
    int addAirBase(const AirBase& b);
    AirBase* airBase(int id);
    const AirBase* airBase(int id) const;
    const std::vector<AirBase>& airBases() const { return airBases_; }
    /// A flight of `count` aircraft parked at a base, ready to take off.
    Flight& addFlight(const std::string& name, Side side, const AircraftDef& def, const std::string& loadout, int count, int base,
                      CrewQuality crew);
    /// Put a flight in the air at a position (scenario start).
    void putAirborne(Flight& f, const Vec2& pos, double altitude, double heading);
    std::vector<Flight>& flights() { return flights_; }
    const std::vector<Flight>& flights() const { return flights_; }
    Flight* flight(int id);
    const Flight* flight(int id) const;
    Flight* flightOfEntity(EntityId e);
    std::vector<Aircraft>& planes() { return planes_; }
    const std::vector<Aircraft>& planes() const { return planes_; }
    Aircraft* plane(EntityId id);
    const Aircraft* plane(EntityId id) const;
    /// Give a flight an order (launches it if at its base). Returns false (with a reason key) if impossible.
    bool orderFlight(int flight, const AirOrder& o, std::string* why = nullptr);
    void dropTorpedo(Aircraft& a, const TorpedoDef& t, double heading);
    void releaseBomb(Aircraft& a, const BombDef& b, const Vec3& vel, EntityId intended);
    /// Damage an aircraft (fraction of a durability-1 airframe); shoots it down when spent.
    void aircraftHit(Aircraft& a, double damage, EntityId by);
    /// Does an own aircraft have this ship in sight (spotting the fall of shot)? Sim-internal.
    bool aircraftSpotting(Side side, EntityId shipTruth) const;

    // internal helpers used by subsystems
    uint32_t nextId() { return ++idCounter_; }
    Shell& addShell(const Shell& s);
    Salvo* salvo(uint32_t id);
    Torpedo* torpedo(uint32_t id);
    void addFlare(const Flare& f);
    /// Truth lookup for a track (sim-internal only).
    Ship* truthOf(Side side, uint32_t track);
    int aliveShips(Side s) const;
    double sideTonnageAfloat(Side s) const;

private:
    void stepShips(double dt);
    void stepShells(double dt);
    void stepTorpedoes(double dt);
    void stepFlares(double dt);
    void stepCollisions();
    void updateTrails();

    const Database& db_;
    Rng rng_;
    Environment env_;
    Terrain terrain_;
    double time_ = 0;
    uint64_t tick_ = 0;
    uint32_t idCounter_ = 0;
    std::vector<std::unique_ptr<Ship>> ships_;
    std::vector<Shell> shells_;
    std::vector<Salvo> salvos_;
    std::vector<Torpedo> torpedoes_;
    std::vector<Flare> flares_;
    std::vector<AirBase> airBases_;
    std::vector<Flight> flights_;
    std::vector<Aircraft> planes_;
    std::unordered_map<EntityId, size_t> planeIndex_;
    std::vector<Group> groups_;
    std::array<ContactPicture, kSideCount> pictures_{ContactPicture(Side::Allies), ContactPicture(Side::Axis),
                                                     ContactPicture(Side::Neutral)};
    std::vector<Event> events_;
    std::vector<Event> history_;
    std::map<std::pair<EntityId, EntityId>, double> lastCollision_;  // hulls grinding together report once
};

}  // namespace it::sim
