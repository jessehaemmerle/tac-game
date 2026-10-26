#pragma once
// Static unit database: guns, shells, torpedoes, sensors and ship classes, loaded from data/db/*.json.
// Everything here is immutable after loading and shared by simulation, AI, procedural models and UI.
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/Json.h"
#include "core/Math.h"
#include "sim/Types.h"

namespace it::sim {

struct ShellDef {
    ShellKind kind = ShellKind::AP;
    std::string name;
    double massKg = 0;
    double muzzleVelocity = 0;  // m/s
    double bursterKg = 0;       // explosive filler
    bool diving = false;        // IJN Type 91 style underwater trajectory
    double formFactor = 1.0;    // drag form factor, calibrated at load time from maxRange
};

struct BallisticSample {
    double elevationDeg = 0, range = 0, timeOfFlight = 0, angleOfFallDeg = 0, strikingVelocity = 0, apex = 0;
};

struct GunDef {
    std::string id, name;
    Nation nation = Nation::Unknown;
    double caliberMm = 0;
    double calibres = 50;          // barrel length in calibres
    double rofRpm = 2;             // per barrel, sustained
    double maxElevationDeg = 40, minElevationDeg = -5;
    double loadingElevationDeg = -1;  // fixed loading angle (<0: any)
    double traverseRateDps = 5, elevationRateDps = 8;
    double maxRange = 20000;        // m at maxRangeElevation (historical, used for calibration)
    double maxRangeElevationDeg = 40;
    double dispersion = 0.006;      // salvo range dispersion (1 sigma) as fraction of range
    bool dualPurpose = false;
    std::vector<ShellDef> shells;
    std::vector<BallisticSample> table;  // computed range table (index = elevation step)

    const ShellDef* shell(ShellKind k) const;
    /// Elevation (deg) needed to reach range r with the primary shell; returns false if out of range.
    bool elevationForRange(double r, double& elevDeg, double& tof) const;
    BallisticSample sampleAtRange(double r) const;
    double effectiveMaxRange() const { return table.empty() ? maxRange : table.back().range; }
};

struct TorpedoSetting {
    double speedKn = 0, range = 0;
};

struct TorpedoDef {
    std::string id, name;
    Nation nation = Nation::Unknown;
    double diameterMm = 533;
    double massKg = 1500;
    double warheadKg = 300;
    double wakeVisibility = 1.0;  // 1 = normal steam/air wake, 0.1 = oxygen (Type 93)
    double dudChance = 0.05;      // failure to explode on contact
    double deepRunChance = 0.0;   // runs under target
    double erraticChance = 0.02;  // circular/erratic run
    double wanderFraction = 0.025;  // lateral wander as fraction of run distance
    std::vector<TorpedoSetting> settings;  // ordered fast→slow
};

struct SensorDef {
    std::string id, name;
    SensorKind kind = SensorKind::Radar;
    Nation nation = Nation::Unknown;
    // Radar
    double rangeLarge = 0;          // m vs. large ship (cruiser/battleship)
    double rangeSmall = 0;          // m vs. destroyer-size target
    double rangeAccuracy = 50;      // 1 sigma, m
    double bearingAccuracyDeg = 1;  // 1 sigma
    double sweepPeriod = 5;         // s
    double landClutter = 0.5;       // 0 = immune, 1 = blind near land
    double reliability = 0.98;      // probability per minute to be working (operator/equipment issues)
    bool fireControl = false;
    bool surfaceSearch = true;
    bool airSearch = false;
    int introducedYear = 1940;
    // Optical
    double magnification = 7;
    double nightFactor = 1.0;  // optics light gathering vs. 7x50 baseline
};

struct MountDef {
    std::string id;      // "A", "B", "X", "Y", "1"...
    std::string weapon;  // gun id
    const GunDef* gun = nullptr;
    int barrels = 1;
    double x = 0, y = 0, z = 5;  // ship-local: x forward of midships, y to starboard, z above waterline (m)
    double arcMin = -150, arcMax = 150;  // relative bearing limits, deg (arcMin<arcMax, may exceed ±180)
    bool mirrored = false;  // arc also valid mirrored to the other beam ([-arcMax,-arcMin])
    double armorFace = 0;  // mm
    std::string director;  // director id controlling this mount
    bool superfiring = false;
    std::string style;     // procedural model style: "turret3", "turret2", "turret1", "open", "shield"
    std::map<ShellKind, int> ammo;  // rounds per barrel
};

struct TorpedoMountDef {
    std::string id;
    std::string weapon;
    const TorpedoDef* torpedo = nullptr;
    int tubes = 4;
    double x = 0, y = 0, z = 3;
    double arcMin = -150, arcMax = -30;  // relative bearings, e.g. port side [-150,-30]
    bool mirrored = false;  // centre-line mount trainable to both beams
    int reloads = 0;        // reload torpedoes for this mount
    double reloadTime = 0;  // s to reload all tubes
};

struct DirectorDef {
    std::string id;
    std::string role = "main";  // main | secondary | torpedo
    double rangefinderBase = 3;  // m (optical stereo/coincidence base length)
    double height = 20;
    std::string radar;  // fire-control radar sensor id
    const SensorDef* radarDef = nullptr;
    double x = 0;
};

struct SensorMountDef {
    std::string sensor;
    const SensorDef* def = nullptr;
    double height = 30;
    double x = 0;
};

struct ArmorDef {
    double belt = 0, beltMagazine = 0, deck = 0, turretFace = 0, turretRoof = 0, barbette = 0, conningTower = 0;
    double torpedoProtection = 0;  // 0..1 underwater protection effectiveness
};

/// Procedural model description (consumed by procgen); positions as fractions from bow (0) to stern (1).
struct ModelPart {
    std::string type;   // superstructure, bridge, pagoda, funnel, mast, tripod, catapult, crane, boat, searchlight, director, radar, deckhouse
    double f = 0.5;     // position from bow (0) to stern (1)
    double y = 0;       // lateral offset, m (starboard +)
    double len = 5, wid = 5, h = 3;  // size in metres
    double z = 0;       // base height above main deck (m)
    double rake = 0;    // funnel/mast rake, deg (aft positive)
    double taper = 0;   // 0..1 top taper
    int levels = 1;
    std::string style;
    std::string ref;    // e.g. sensor id for radar parts
    double f2 = -1;     // second position (trunked funnels)
};

struct ModelDesc {
    double bowRake = 0.06;          // horizontal overhang of stem as fraction of length
    double sternShape = 0;          // 0 = cruiser stern (rounded), 1 = transom
    double freeboardBow = 8, freeboardMid = 6, freeboardStern = 5;  // m above waterline
    double forecastleEnd = 0.0;     // fraction where forecastle deck ends (0 = flush deck)
    double forecastleDrop = 0.0;    // m drop to main deck aft of forecastle
    double fineness = 0.55;         // block-coefficient-like fullness of waterplane
    double flare = 0.15;            // bow flare
    double tumblehome = 0.0;
    std::string hullPaint = "grey";  // paint scheme key
    std::string deckPaint = "teak";
    std::vector<ModelPart> parts;
    std::string gltf;  // optional replacement model path (relative to data/models)
    double gltfScale = 1.0;

    /// Height of the weather deck above the waterline at fraction f from the bow (sheer + forecastle break).
    double deckHeightAt(double f) const;
};

struct ShipClassDef {
    std::string id;
    Nation nation = Nation::Unknown;
    ShipType type = ShipType::Unknown;
    std::string name;          // class name ("New Orleans")
    std::vector<std::string> ships;  // hull names in class
    int year = 1940;           // first commissioned
    // Dimensions
    double length = 100, lengthWL = 95, beam = 10, draft = 4;
    double displacementStd = 1000, displacementFull = 1500;  // tonnes
    // Propulsion & handling
    double maxSpeedKn = 30, cruiseSpeedKn = 15;
    double shp = 50000;
    double accelKnPerMin = 5;     // at low speed
    double tacticalDiameter = 700;  // m at full rudder & speed
    int shafts = 2;
    int crew = 200;
    ArmorDef armor;
    std::vector<MountDef> mounts;
    std::vector<TorpedoMountDef> torpedoMounts;
    std::vector<DirectorDef> directors;
    std::vector<SensorMountDef> sensors;
    int searchlights = 2;
    double searchlightCm = 90;
    int floatplanes = 0;
    int depthCharges = 0;
    double mastHeight = 30;     // highest point above waterline (lookouts & silhouette)
    double lookoutHeight = 20;
    double superstructureHeight = 12;
    double radarCrossSection = 1.0;  // relative (cruiser = 1)
    double reserveBuoyancy = 0.6;    // fraction of displacement that can flood before sinking
    double hullStrength = 1.0;       // structural toughness multiplier
    bool aviationFuel = false;       // carries floatplanes/av-gas (fire hazard)
    ModelDesc model;
    std::string sourceNote;  // data sources
    std::string historyKey;  // localisation key for history text

    SizeClass size() const { return sizeOf(type); }
    double maxSpeed() const;  // m/s
    /// Silhouette area estimate (m^2) from broadside view, used for visual detection.
    double silhouetteArea() const { return length * superstructureHeight * 0.55 + length * model.freeboardMid; }
    const MountDef* mount(const std::string& id) const;
    const DirectorDef* director(const std::string& id) const;
    /// Total broadside weight (kg) of main battery, for force evaluation.
    double broadsideWeight() const;
    int torpedoTubes() const;
};

struct NationDef {
    Nation nation = Nation::Unknown;
    std::string id, nameKey, flag;
    Side side = Side::Neutral;
    std::string shipPrefix;  // "USS", "HMAS", ""
    std::string language;    // radio voice language tag
};

/// True if relative bearing (deg) lies within [arcMin, arcMax] (optionally mirrored to the other beam).
bool inArc(double relBearingDeg, double arcMin, double arcMax, bool mirrored = false);

class Database {
public:
    bool loadFromDirectory(const std::string& dbDir, std::string* error = nullptr);
    /// Validate cross references and plausibility; returns list of problems (empty = ok).
    std::vector<std::string> validate() const;

    const GunDef* gun(const std::string& id) const;
    const TorpedoDef* torpedo(const std::string& id) const;
    const SensorDef* sensor(const std::string& id) const;
    const ShipClassDef* shipClass(const std::string& id) const;
    const NationDef* nation(Nation n) const;

    const std::vector<std::unique_ptr<ShipClassDef>>& shipClasses() const { return classes_; }
    const std::vector<std::unique_ptr<GunDef>>& guns() const { return guns_; }
    const std::vector<std::unique_ptr<TorpedoDef>>& torpedoes() const { return torpedoes_; }
    const std::vector<std::unique_ptr<SensorDef>>& sensors() const { return sensors_; }

    // Programmatic insertion (tests).
    GunDef& addGun(std::unique_ptr<GunDef> g);
    TorpedoDef& addTorpedo(std::unique_ptr<TorpedoDef> t);
    SensorDef& addSensor(std::unique_ptr<SensorDef> s);
    ShipClassDef& addShipClass(std::unique_ptr<ShipClassDef> c);
    /// Resolve pointers (mount->gun etc.) after insertion; also computes ballistic tables.
    void link();

    static void parseShipClass(const Json& j, ShipClassDef& out);

private:
    std::vector<std::unique_ptr<GunDef>> guns_;
    std::vector<std::unique_ptr<TorpedoDef>> torpedoes_;
    std::vector<std::unique_ptr<SensorDef>> sensors_;
    std::vector<std::unique_ptr<ShipClassDef>> classes_;
    std::vector<NationDef> nations_;
};

}  // namespace it::sim
