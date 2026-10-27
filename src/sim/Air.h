#pragma once
// Air units. A flight (Kette, section, division, Chutai) is the unit a commander orders; it is made of
// individual aircraft, which fly in formation, make their own attack runs, are hit by anti-aircraft fire
// one by one and are drawn one by one. Flights operate from air bases: airfields (often far off the map,
// like the Saigon fields of the bombers that sank Force Z) and ships' catapults.
#include <string>
#include <vector>

#include "core/Math.h"
#include "sim/Database.h"
#include "sim/Types.h"

namespace it::sim {

class World;
class Ship;

enum class AirOrderType : uint8_t {
    None,
    Patrol,   // fly a route or search an area, report what is seen (armed: strike what is found)
    Attack,   // strike a ship contact
    Cap,      // combat air patrol over a point or an own group: intercept enemy aircraft
    Escort,   // stay with another flight and fight off its attackers
    Flares,   // drop parachute flares over a contact or point
    Spot,     // shadow a contact: keep it in sight, report it, spot the fall of shot for own ships
    Return,   // back to base
};
const char* airOrderKey(AirOrderType t);
AirOrderType airOrderFromString(const std::string& s);

struct AirOrder {
    AirOrderType type = AirOrderType::None;
    std::vector<Vec2> route;     // patrol route (search legs); empty: the point
    Vec2 point;                  // area centre for patrol / CAP / flares / spot without a contact
    double radius = 12000;       // CAP / flare area
    uint32_t track = 0;          // ship contact (own side's picture) to attack / illuminate / shadow
    int group = -1;              // own group to cover (CAP) or spot for
    int escort = -1;             // flight to escort
    bool strike = false;         // patrol: attack the first suitable target found (armed search)
    double altitude = -1;        // cruise altitude (m); < 0: by role
    double startTime = -1;       // launch time (world seconds) for orders given to a flight at its base
};

enum class FlightState : uint8_t {
    Ready,      // at its base, armed and fuelled
    Launching,  // taking off and forming up
    Transit,    // flying to the objective
    Patrol,     // flying search legs
    Station,    // orbiting: CAP, flares, shadowing, escort
    Intercept,  // fighters closing an enemy formation
    Attack,     // attack runs in progress
    Rally,      // re-forming after the attack
    Returning,  // heading home
    Rearming,   // landed, being refuelled and rearmed
    Lost,       // every aircraft gone
};
const char* flightStateKey(FlightState s);

struct AirBase {
    int id = -1;
    std::string name;             // localisation key or literal
    Side side = Side::Neutral;
    Vec2 pos;                     // local metres (airfields may lie far off the map)
    EntityId ship = kNoEntity;    // catapult ship (floatplanes)
    double turnaround = 2700;     // s to refuel and rearm after landing
    double launchInterval = 25;   // s between take-offs
};

/// One airframe.
struct Aircraft {
    enum class Fate : uint8_t { Flying, Parked, Falling, Ditched, Destroyed };
    enum class Run : uint8_t { None, Approach, Run, Dive, Egress, Dogfight };
    EntityId id = kNoEntity;
    int flight = -1;
    const AircraftDef* def = nullptr;
    Side side = Side::Neutral;
    Vec3 pos;                     // local metres, z = altitude
    double heading = 0, pitch = 0, bank = 0;  // deg (pitch: nose up +)
    double speed = 0;             // m/s
    double health = 1.0;          // < 0: shot down
    Fate fate = Fate::Parked;
    double fateTime = 0;          // time since shot down / ditched
    int slot = 0;                 // position in the formation
    // weapons
    int torpedoes = 0, bombs = 0, bombs2 = 0;
    bool aborted = false;         // damaged: jettisoned, heading home
    // attack run
    Run run = Run::None;
    double runTime = 0;
    EntityId target = kNoEntity;  // ship (attack) or aircraft (dogfight) - sim-internal truth
    Vec2 setup;                   // torpedo set-up point / dive entry point
    int side_ = 1;                // anvil: which bow (+1 starboard of the target's course, -1 port)
    double releaseRange = 1000;
    double lastGuns = -1e9;       // time guns were last fired (tracers)
    EntityId gunsAt = kNoEntity;
    double gunAmmo = 0;           // seconds of fire left in the forward guns
    double burstUntil = -1, breakUntil = -1, breakHeading = 0;  // air combat: firing pass and breakaway
    double smoke = 0;             // 0..1 visible damage (smoke trail)
    bool flying() const { return fate == Fate::Flying; }
    bool armed() const { return torpedoes > 0 || bombs > 0 || bombs2 > 0; }
};

struct Flight {
    int id = -1;
    EntityId entity = kNoEntity;  // identity of the formation for the enemy's air picture
    std::string name;             // localisation key or literal
    Side side = Side::Neutral;
    Nation nation = Nation::Unknown;
    const AircraftDef* def = nullptr;
    const AircraftLoadout* loadout = nullptr;
    CrewQuality crew = CrewQuality::Regular;
    int base = -1;
    std::vector<EntityId> planes;
    FlightState state = FlightState::Ready;
    double stateTime = 0;
    AirOrder order;
    // Formation reference (the leader's intended path)
    Vec2 pos;
    double altitude = 0, heading = 0, speed = 0;
    double fuel = 0;              // s of flight left at cruise
    size_t leg = 0;               // patrol route index
    int launched = 0;
    double nextLaunch = 0;
    int flaresLeft = 0;
    double nextFlare = 0, nextScan = 0;
    // Attack: what the flight itself has in sight (sim-internal truth) and where it thinks the target is.
    EntityId targetTruth = kNoEntity;
    Vec2 targetPos;
    double targetSeen = -1e9;
    EntityId interceptTruth = kNoEntity;  // enemy flight being intercepted
    double bombFall = 0, bombThrow = 0;   // level bombing: time of fall and forward throw for the run
    bool playerControlled = false;
    bool aiControlled = true;     // the side's AI may give it orders
    // Statistics
    int sorties = 0, kills = 0, losses = 0, hits = 0, torpedoesDropped = 0, bombsDropped = 0;

    int alive(const World& w) const;
    bool airborne() const {
        return state != FlightState::Ready && state != FlightState::Rearming && state != FlightState::Lost;
    }
};

namespace air {

/// Default cruise altitude (m) for a role and order.
double cruiseAltitude(const Flight& f);
/// Seconds of flight needed to get home from pos (with a reserve).
double timeToBase(const World& w, const Flight& f);
/// Advance flights, aircraft, attacks and air combat.
void step(World& w, double dt);
/// Does an aircraft type's crew see this ship from its position now (range, light, weather)? Sim-internal.
double visualRangeToShip(const World& w, const Aircraft& a, const Ship& s);

}  // namespace air
}  // namespace it::sim
