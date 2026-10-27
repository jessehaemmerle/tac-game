#pragma once
#include <map>
#include <string>
#include <vector>

#include "core/Math.h"
#include "sim/Database.h"
#include "sim/Types.h"

namespace it::sim {

struct MountState {
    const MountDef* def = nullptr;
    double train = 0;      // current relative bearing (deg, 0 = ahead)
    double elevation = 0;  // deg
    double reload = 0;     // s until loaded
    double health = 1.0;   // 0 = out of action
    std::map<ShellKind, int> ammo;  // rounds remaining for the whole mount
    ShellKind loaded = ShellKind::AP;
    bool onTarget = false;
    bool bears = false;  // target within arc
    int shotsFired = 0;
    double aaUntil = -1;       // dual-purpose mount busy firing at aircraft until
    double lastSurface = -1e9; // last round at a surface target
    bool operational() const { return health > 0.25; }
    int totalAmmo() const;
};

struct TorpedoMountState {
    const TorpedoMountDef* def = nullptr;
    double train = 0;
    int loaded = 0;      // tubes loaded
    int reloads = 0;     // reload torpedoes left
    double reloadTimer = 0;
    double health = 1.0;
    bool operational() const { return health > 0.3; }
};

/// Fire-control solution held by a director (its own tracking of the target, independent of the
/// side's contact picture: a ship can only shoot at what its own director can range on).
struct DirectorState {
    const DirectorDef* def = nullptr;
    double health = 1.0;
    uint32_t targetTrack = 0;   // contact picture track id (0 = none)
    EntityId targetTruth = kNoEntity;  // internal: who the director is physically ranging on
    bool tracking = false;      // currently has a measurement lock
    double trackTime = 0;       // s of continuous tracking
    double lastMeasure = -1e9;
    Vec2 estPos, estVel;        // estimated target position / velocity
    double rangeSigma = 1000;   // current 1-sigma range error estimate (m)
    double rangeCorrection = 0, deflectionCorrection = 0;  // spotting corrections (m)
    double quality = 0;         // 0..1 for UI/AI
    double lastSalvo = -1e9;
    uint32_t salvoSeq = 0;      // id of the last salvo fired (for spotting)
    int salvosAtTarget = 0;
    bool straddled = false;     // range found: switch from ladder (wait for fall of shot) to rapid fire
    double lastSalvoTof = 0;
    bool radarRanging = false;
    bool opticalLayer = false;  // the director can see the target (lay on it, watch the fall of shot)
    bool radarFitted = true;    // per-ship refit state (period variants without FC radar)
    bool illuminate = false;    // secondary directors: fire starshells over the target instead of shells
    bool operational() const { return health > 0.3; }
};

struct SensorState {
    const SensorMountDef* def = nullptr;
    double nextSweep = 0;
    double health = 1.0;
    bool on = true;            // emission control (radar)
    double outageUntil = -1;   // equipment/operator problems
    bool working(double now) const { return health > 0.3 && on && now >= outageUntil; }
};

/// An anti-aircraft battery: a light AA mount from the model or a dual-purpose gun mount.
struct AaBatteryState {
    const AaWeaponDef* def = nullptr;  // light AA weapon (dual-purpose mounts: nullptr)
    int mount = -1;                    // dual-purpose mount index
    Vec3 local;                        // ship-local position
    double health = 1.0;
    EntityId target = kNoEntity;       // aircraft engaged (sim-internal)
    double firingUntil = -1;           // the guns are firing (tracers, flak) until
    double nextRound = 0;
    double train = 0, elevation = 0;   // where the guns point (deg, relative bearing / elevation)
    int roundsFired = 0;
    bool operational() const { return health > 0.3; }
};

struct Fire {
    double f = 0.5;          // position along hull (0 bow .. 1 stern)
    double intensity = 0.3;  // 0..1
};

struct DamageState {
    double hullIntegrity = 1.0;  // structural; < 0 = breaks up
    double flooding = 0;         // tonnes of water aboard
    double floodRate = 0;        // tonnes / s (uncontrolled inflow)
    double holed = 0;            // tonnes of the flooding open to the sea or counter-flooded: pumps cannot clear it
    double list = 0;             // deg, + = starboard
    double trim = 0;             // deg, + = bow down
    double crewLoss = 0;         // fraction of crew killed/wounded
    double propulsion = 1.0, steering = 1.0, bridge = 1.0, radio = 1.0, searchlights = 1.0, aviation = 1.0;
    double power = 1.0;  // electrical/hydraulic supply: turrets, directors, radar and searchlights depend on it
    bool rudderJammed = false;
    double jammedRudder = 0;
    std::vector<Fire> fires;
    bool sinking = false, sunk = false, magazineExploded = false, brokenBack = false, capsized = false;
    double sinkProgress = 0;  // 0..1 visual sinking
    double sinkRate = 0;      // progress per s
    EntityId sunkBy = kNoEntity;
    int hitsTaken = 0, torpedoHitsTaken = 0;
    double totalFireIntensity() const;
};

struct ShipOrders {
    double course = 0;        // ordered heading (deg)
    double speedKn = 0;       // ordered speed (kn)
    WeaponsStatus weapons = WeaponsStatus::Free;
    bool holdFireUntilIllumination = false;
    bool searchlight = false;
    uint32_t searchlightTrack = 0;
    bool makeSmoke = false;
    // Player control
    bool playerSteering = false;   // ship detached from formation, steered by the player
    uint32_t playerTarget = 0;     // main battery target chosen by the player (0 = captain's choice)
    bool playerHoldTorpedoes = true;
    std::vector<Vec2> route;       // player waypoints while detached from the formation, first = next
};

struct ShipStats {
    int salvos = 0, shellsFired = 0, hitsScored = 0, torpedoesFired = 0, torpedoHits = 0;
    double tonnageSunk = 0;
};

class Ship {
public:
    EntityId id = kNoEntity;
    std::string name;
    const ShipClassDef* cls = nullptr;
    Side side = Side::Neutral;
    Nation nation = Nation::Unknown;
    CrewQuality crew = CrewQuality::Regular;
    int groupId = -1;

    // Kinematics
    Vec2 pos;
    double heading = 0;    // deg
    double speed = 0;      // m/s through water
    double rudder = 0;     // -1 (port) .. 1 (starboard)
    double turnRate = 0;   // deg/s
    double heel = 0;       // deg (visual)
    bool grounded = false;

    std::vector<MountState> mounts;
    std::vector<TorpedoMountState> torpedoMounts;
    std::vector<DirectorState> directors;
    std::vector<SensorState> sensors;
    std::vector<AaBatteryState> aa;
    double aaSuppressedUntil = -1;  // strafing fighters drive the light AA crews to cover
    int aircraftShotDown = 0;
    DamageState dmg;
    ShipOrders orders;
    ShipStats stats;

    // Readiness: 1 = all hands at battle stations (Condition I). Ships cruising at Condition II/III need
    // minutes after the alarm to man every gun, director and lookout station.
    double readiness = 1.0;
    double alertedAt = -1;        // time the alarm was sounded (-1: not alerted)
    void alert(double now) {
        if (alertedAt < 0) alertedAt = now;
    }

    double lastFired = -1e9;      // time of last gun discharge (flash visibility)
    double lastFiredCalibre = 0;
    double searchlightOnSince = -1;
    EntityId searchlightTarget = kNoEntity;  // internal: ship lit by our searchlight
    int floatplanes = 0;

    void initFromClass(const ShipClassDef& c);
    bool alive() const { return !dmg.sunk; }
    bool operational() const { return alive() && !dmg.sinking; }
    Vec2 forward() const { return bearingVector(heading); }
    Vec2 right() const { return bearingVector(heading + 90.0); }
    Vec2 velocity() const { return forward() * speed; }
    Vec2 localToWorld(double x, double y) const { return pos + forward() * x + right() * y; }
    Vec2 worldToLocal(const Vec2& p) const {
        Vec2 d = p - pos;
        return {dot(d, forward()), dot(d, right())};
    }
    double relativeBearing(const Vec2& p) const { return wrap180(bearingTo(pos, p) - heading); }
    double speedKn() const { return speed / 0.514444; }
    /// Maximum speed achievable now (m/s) given machinery damage and flooding.
    double maxSpeedNow() const;
    /// 0..1 overall combat effectiveness (crew, bridge, fire control) used by AI and UI.
    double effectiveness() const;
    /// Remaining flotation as fraction (1 = undamaged, 0 = sinking).
    double flotation() const;
    double displacement() const { return cls ? cls->displacementFull : 1000.0; }
    int torpedoesReady() const;
    int torpedoesTotal() const;
    /// Height (m) above waterline of the top of the silhouette.
    double silhouetteHeight() const { return cls ? cls->mastHeight : 20.0; }
};

}  // namespace it::sim
