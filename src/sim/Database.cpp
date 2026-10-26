#include "sim/Database.h"

#include <algorithm>

#include "core/FileSystem.h"
#include "core/Log.h"
#include "core/Units.h"
#include "sim/Ballistics.h"

namespace it::sim {

// ------------------------------------------------------------------------------------------- GunDef
const ShellDef* GunDef::shell(ShellKind k) const {
    for (auto& s : shells)
        if (s.kind == k) return &s;
    return nullptr;
}

bool GunDef::elevationForRange(double r, double& elevDeg, double& tof) const {
    if (table.empty() || r > table.back().range) return false;
    if (r <= table.front().range) {
        elevDeg = table.front().elevationDeg;
        tof = table.front().timeOfFlight * (r / std::max(1.0, table.front().range));
        return true;
    }
    for (size_t i = 1; i < table.size(); ++i) {
        if (r <= table[i].range) {
            double f = (r - table[i - 1].range) / (table[i].range - table[i - 1].range);
            elevDeg = lerp(table[i - 1].elevationDeg, table[i].elevationDeg, f);
            tof = lerp(table[i - 1].timeOfFlight, table[i].timeOfFlight, f);
            return true;
        }
    }
    return false;
}

BallisticSample GunDef::sampleAtRange(double r) const {
    if (table.empty()) return {};
    if (r <= table.front().range) return table.front();
    for (size_t i = 1; i < table.size(); ++i) {
        if (r <= table[i].range) {
            double f = (r - table[i - 1].range) / (table[i].range - table[i - 1].range);
            const auto &a = table[i - 1], &b = table[i];
            return {lerp(a.elevationDeg, b.elevationDeg, f), r, lerp(a.timeOfFlight, b.timeOfFlight, f),
                    lerp(a.angleOfFallDeg, b.angleOfFallDeg, f), lerp(a.strikingVelocity, b.strikingVelocity, f),
                    lerp(a.apex, b.apex, f)};
        }
    }
    return table.back();
}

bool inArc(double rel, double arcMin, double arcMax, bool mirrored) {
    auto test = [](double r, double lo, double hi) {
        double x = lo + wrap360(r - lo);
        return x <= hi + 1e-9;
    };
    if (test(rel, arcMin, arcMax)) return true;
    return mirrored && test(rel, -arcMax, -arcMin);
}

// --------------------------------------------------------------------------------------- ModelDesc
double ModelDesc::deckHeightAt(double f) const {
    f = clamp(f, 0.0, 1.0);
    double h = f < 0.5 ? freeboardMid + (freeboardBow - freeboardMid) * sqr(1.0 - f / 0.5)
                       : freeboardMid + (freeboardStern - freeboardMid) * sqr((f - 0.5) / 0.5);
    if (forecastleEnd > 0.0) h -= forecastleDrop * smoothstep(forecastleEnd - 0.004, forecastleEnd + 0.004, f);
    return h;
}

// ------------------------------------------------------------------------------------ ShipClassDef
double ShipClassDef::maxSpeed() const { return units::knotsToMs(maxSpeedKn); }

const MountDef* ShipClassDef::mount(const std::string& mid) const {
    for (auto& m : mounts)
        if (m.id == mid) return &m;
    return nullptr;
}

const DirectorDef* ShipClassDef::director(const std::string& did) const {
    for (auto& d : directors)
        if (d.id == did) return &d;
    return nullptr;
}

double ShipClassDef::broadsideWeight() const {
    double w = 0;
    for (auto& m : mounts) {
        if (!m.gun || m.gun->shells.empty()) continue;
        w += m.barrels * m.gun->shells.front().massKg;
    }
    return w;
}

int ShipClassDef::torpedoTubes() const {
    int n = 0;
    for (auto& t : torpedoMounts) n += t.tubes;
    return n;
}

// ------------------------------------------------------------------------------------------ parsing
namespace {

void parseGun(const Json& j, GunDef& g) {
    g.id = jstr(j, "id");
    g.name = jstr(j, "name", g.id);
    g.nation = nationFromString(jstr(j, "nation"));
    g.caliberMm = jnum(j, "caliber_mm", 100);
    g.calibres = jnum(j, "calibres", 50);
    g.rofRpm = jnum(j, "rof_rpm", 2);
    g.maxElevationDeg = jnum(j, "elev_max", 40);
    g.minElevationDeg = jnum(j, "elev_min", -5);
    g.loadingElevationDeg = jnum(j, "loading_elev", -1);
    g.traverseRateDps = jnum(j, "traverse_dps", 5);
    g.elevationRateDps = jnum(j, "elev_dps", 8);
    g.maxRange = jnum(j, "max_range_m", 20000);
    g.maxRangeElevationDeg = jnum(j, "max_range_elev", g.maxElevationDeg);
    g.dispersion = jnum(j, "dispersion", 0.006);
    g.dualPurpose = jbool(j, "dual_purpose");
    for (auto& s : jarr(j, "shells")) {
        ShellDef sd;
        sd.kind = shellKindFromString(jstr(s, "kind", "AP"));
        sd.name = jstr(s, "name");
        sd.massKg = jnum(s, "mass_kg", 50);
        sd.muzzleVelocity = jnum(s, "mv", 800);
        sd.bursterKg = jnum(s, "burster_kg", 1);
        sd.diving = jbool(s, "diving");
        g.shells.push_back(sd);
    }
}

void parseTorpedo(const Json& j, TorpedoDef& t) {
    t.id = jstr(j, "id");
    t.name = jstr(j, "name", t.id);
    t.nation = nationFromString(jstr(j, "nation"));
    t.diameterMm = jnum(j, "diameter_mm", 533);
    t.massKg = jnum(j, "mass_kg", 1500);
    t.warheadKg = jnum(j, "warhead_kg", 300);
    t.wakeVisibility = jnum(j, "wake", 1.0);
    t.dudChance = jnum(j, "dud", 0.05);
    t.deepRunChance = jnum(j, "deep_run", 0.0);
    t.erraticChance = jnum(j, "erratic", 0.02);
    t.wanderFraction = jnum(j, "wander", 0.025);
    for (auto& s : jarr(j, "settings")) t.settings.push_back({jnum(s, "speed_kn"), jnum(s, "range_m")});
    std::sort(t.settings.begin(), t.settings.end(), [](auto& a, auto& b) { return a.speedKn > b.speedKn; });
}

void parseSensor(const Json& j, SensorDef& s) {
    s.id = jstr(j, "id");
    s.name = jstr(j, "name", s.id);
    s.kind = sensorKindFromString(jstr(j, "kind", "radar"));
    s.nation = nationFromString(jstr(j, "nation"));
    s.rangeLarge = jnum(j, "range_large_m", 0);
    s.rangeSmall = jnum(j, "range_small_m", s.rangeLarge * 0.6);
    s.rangeAccuracy = jnum(j, "range_acc_m", 50);
    s.bearingAccuracyDeg = jnum(j, "bearing_acc_deg", 1);
    s.sweepPeriod = jnum(j, "sweep_s", 5);
    s.landClutter = jnum(j, "land_clutter", 0.5);
    s.reliability = jnum(j, "reliability", 0.98);
    s.fireControl = jbool(j, "fire_control");
    s.surfaceSearch = jbool(j, "surface", true);
    s.airSearch = jbool(j, "air", false);
    s.introducedYear = jint(j, "year", 1940);
    s.magnification = jnum(j, "magnification", 7);
    s.nightFactor = jnum(j, "night_factor", 1.0);
}

}  // namespace

void Database::parseShipClass(const Json& j, ShipClassDef& c) {
    c.id = jstr(j, "id");
    c.nation = nationFromString(jstr(j, "nation"));
    c.type = shipTypeFromString(jstr(j, "type"));
    c.name = jstr(j, "name", c.id);
    for (auto& s : jarr(j, "ships")) c.ships.push_back(s.get<std::string>());
    c.year = jint(j, "year", 1940);
    const Json& dim = jobj(j, "dimensions");
    c.length = jnum(dim, "length", 100);
    c.lengthWL = jnum(dim, "length_wl", c.length * 0.96);
    c.beam = jnum(dim, "beam", 10);
    c.draft = jnum(dim, "draft", 4);
    c.mastHeight = jnum(dim, "mast_height", 30);
    c.lookoutHeight = jnum(dim, "lookout_height", c.mastHeight * 0.6);
    c.superstructureHeight = jnum(dim, "superstructure_height", 12);
    const Json& disp = jobj(j, "displacement");
    c.displacementStd = jnum(disp, "standard", 1000);
    c.displacementFull = jnum(disp, "full", c.displacementStd * 1.3);
    const Json& prop = jobj(j, "propulsion");
    c.maxSpeedKn = jnum(prop, "max_speed_kn", 30);
    c.cruiseSpeedKn = jnum(prop, "cruise_speed_kn", 15);
    c.shp = jnum(prop, "shp", 50000);
    c.accelKnPerMin = jnum(prop, "accel_kn_per_min", 6);
    c.tacticalDiameter = jnum(prop, "tactical_diameter_m", c.length * 5.0);
    c.shafts = jint(prop, "shafts", 2);
    c.crew = jint(j, "crew", 200);
    const Json& ar = jobj(j, "armor");
    c.armor.belt = jnum(ar, "belt", 0);
    c.armor.beltMagazine = jnum(ar, "belt_magazine", c.armor.belt);
    c.armor.deck = jnum(ar, "deck", 0);
    c.armor.turretFace = jnum(ar, "turret_face", 0);
    c.armor.turretRoof = jnum(ar, "turret_roof", 0);
    c.armor.barbette = jnum(ar, "barbette", 0);
    c.armor.conningTower = jnum(ar, "conning_tower", 0);
    c.armor.torpedoProtection = jnum(ar, "torpedo_protection", 0);
    const Json& mj = jobj(j, "model");
    ModelDesc& md = c.model;
    md.bowRake = jnum(mj, "bow_rake", 0.05);
    md.sternShape = jnum(mj, "stern", 0.0);
    md.freeboardBow = jnum(mj, "freeboard_bow", c.draft * 1.8);
    md.freeboardMid = jnum(mj, "freeboard_mid", c.draft * 1.2);
    md.freeboardStern = jnum(mj, "freeboard_stern", c.draft * 1.0);
    md.forecastleEnd = jnum(mj, "forecastle_end", 0.0);
    md.forecastleDrop = jnum(mj, "forecastle_drop", 0.0);
    md.fineness = jnum(mj, "fineness", 0.55);
    md.flare = jnum(mj, "flare", 0.15);
    md.tumblehome = jnum(mj, "tumblehome", 0.0);
    md.hullPaint = jstr(mj, "hull_paint", "grey");
    md.deckPaint = jstr(mj, "deck_paint", "teak");
    md.gltf = jstr(mj, "gltf");
    md.gltfScale = jnum(mj, "gltf_scale", 1.0);
    for (auto& p : jarr(mj, "parts")) {
        ModelPart mp;
        mp.type = jstr(p, "type");
        mp.f = jnum(p, "f", 0.5);
        mp.f2 = jnum(p, "f2", -1);
        mp.y = jnum(p, "y", 0);
        mp.len = jnum(p, "len", 5);
        mp.wid = jnum(p, "wid", 5);
        mp.h = jnum(p, "h", 3);
        mp.z = jnum(p, "z", 0);
        mp.rake = jnum(p, "rake", 0);
        mp.taper = jnum(p, "taper", 0);
        mp.levels = jint(p, "levels", 1);
        mp.style = jstr(p, "style");
        mp.ref = jstr(p, "ref");
        md.parts.push_back(mp);
    }
    for (auto& m : jarr(j, "mounts")) {
        MountDef mt;
        mt.id = jstr(m, "id");
        mt.weapon = jstr(m, "weapon");
        mt.barrels = jint(m, "barrels", 1);
        double f = jnum(m, "f", 0.5);
        mt.x = (0.5 - f) * c.length;
        mt.y = jnum(m, "y", 0);
        mt.superfiring = jbool(m, "superfiring");
        mt.z = jnum(m, "z", c.model.deckHeightAt(f) + (mt.superfiring ? 3.6 : 1.6) + jnum(m, "raise", 0));
        mt.arcMin = jnum(m, "arc_min", -150);
        mt.arcMax = jnum(m, "arc_max", 150);
        mt.mirrored = jbool(m, "mirrored");
        mt.armorFace = jnum(m, "armor", c.armor.turretFace);
        mt.director = jstr(m, "director", "main");
        mt.style = jstr(m, "style", mt.barrels >= 3 ? "turret3" : (mt.barrels == 2 ? "turret2" : "shield"));
        const Json& ammo = jobj(m, "ammo");
        for (auto it = ammo.begin(); it != ammo.end(); ++it) mt.ammo[shellKindFromString(it.key())] = it->get<int>();
        c.mounts.push_back(mt);
    }
    for (auto& m : jarr(j, "torpedo_mounts")) {
        TorpedoMountDef td;
        td.id = jstr(m, "id");
        td.weapon = jstr(m, "weapon");
        td.tubes = jint(m, "tubes", 4);
        double f = jnum(m, "f", 0.5);
        td.x = (0.5 - f) * c.length;
        td.y = jnum(m, "y", 0);
        td.z = jnum(m, "z", c.model.deckHeightAt(f) + 1.0);
        td.arcMin = jnum(m, "arc_min", -150);
        td.arcMax = jnum(m, "arc_max", 150);
        td.mirrored = jbool(m, "mirrored");
        td.reloads = jint(m, "reloads", 0);
        td.reloadTime = jnum(m, "reload_s", 900);
        c.torpedoMounts.push_back(td);
    }
    for (auto& d : jarr(j, "directors")) {
        DirectorDef dd;
        dd.id = jstr(d, "id", "main");
        dd.role = jstr(d, "role", "main");
        dd.rangefinderBase = jnum(d, "rangefinder_m", 3);
        dd.height = jnum(d, "height", 20);
        dd.radar = jstr(d, "radar");
        dd.x = (0.5 - jnum(d, "f", 0.35)) * c.length;
        c.directors.push_back(dd);
    }
    if (c.directors.empty()) c.directors.push_back(DirectorDef{});
    for (auto& s : jarr(j, "sensors")) {
        SensorMountDef sm;
        sm.sensor = jstr(s, "id");
        sm.height = jnum(s, "height", c.mastHeight * 0.8);
        sm.x = (0.5 - jnum(s, "f", 0.35)) * c.length;
        c.sensors.push_back(sm);
    }
    c.searchlights = jint(j, "searchlights", 2);
    c.searchlightCm = jnum(j, "searchlight_cm", 90);
    c.floatplanes = jint(j, "floatplanes", 0);
    c.depthCharges = jint(j, "depth_charges", 0);
    c.radarCrossSection = jnum(j, "rcs", c.displacementStd / 10000.0);
    c.reserveBuoyancy = jnum(j, "reserve_buoyancy", 0.6);
    c.hullStrength = jnum(j, "hull_strength", 1.0);
    c.aviationFuel = jbool(j, "aviation_fuel", c.floatplanes > 0);
    c.sourceNote = jstr(j, "sources");
    c.historyKey = jstr(j, "history", "history." + c.id);

}

bool Database::loadFromDirectory(const std::string& dbDir, std::string* error) {
    std::filesystem::path dir(dbDir);
    auto loadArray = [&](const char* file, auto&& fn) -> bool {
        std::string err;
        auto j = loadJsonFile(dir / file, &err);
        if (!j) {
            if (error) *error = err;
            return false;
        }
        for (auto& e : *j) fn(e);
        return true;
    };
    if (!loadArray("guns.json", [&](const Json& e) {
            auto g = std::make_unique<GunDef>();
            parseGun(e, *g);
            guns_.push_back(std::move(g));
        }))
        return false;
    if (!loadArray("torpedoes.json", [&](const Json& e) {
            auto t = std::make_unique<TorpedoDef>();
            parseTorpedo(e, *t);
            torpedoes_.push_back(std::move(t));
        }))
        return false;
    if (!loadArray("sensors.json", [&](const Json& e) {
            auto s = std::make_unique<SensorDef>();
            parseSensor(e, *s);
            sensors_.push_back(std::move(s));
        }))
        return false;
    if (!loadArray("nations.json", [&](const Json& e) {
            NationDef n;
            n.id = jstr(e, "id");
            n.nation = nationFromString(n.id);
            n.nameKey = jstr(e, "name", "nation." + n.id);
            n.flag = jstr(e, "flag");
            n.side = sideFromString(jstr(e, "side"));
            n.shipPrefix = jstr(e, "prefix");
            n.language = jstr(e, "language", "en");
            nations_.push_back(n);
        }))
        return false;
    for (auto& p : fs::listFiles(dir / "ships", ".json")) {
        std::string err;
        auto j = loadJsonFile(p, &err);
        if (!j) {
            if (error) *error = err;
            return false;
        }
        auto c = std::make_unique<ShipClassDef>();
        parseShipClass(*j, *c);
        classes_.push_back(std::move(c));
    }
    link();
    IT_LOG_INFO("Database: %zu guns, %zu torpedoes, %zu sensors, %zu ship classes", guns_.size(), torpedoes_.size(),
                sensors_.size(), classes_.size());
    return true;
}

void Database::link() {
    for (auto& g : guns_)
        if (g->table.empty()) ballistics::buildTables(*g);
    for (auto& c : classes_) {
        for (auto& m : c->mounts) m.gun = gun(m.weapon);
        for (auto& t : c->torpedoMounts) t.torpedo = torpedo(t.weapon);
        for (auto& s : c->sensors) s.def = sensor(s.sensor);
        for (auto& d : c->directors) d.radarDef = d.radar.empty() ? nullptr : sensor(d.radar);
    }
}

std::vector<std::string> Database::validate() const {
    std::vector<std::string> problems;
    for (auto& g : guns_) {
        if (g->shells.empty()) problems.push_back("gun " + g->id + " has no shells");
        if (g->table.size() < 4) problems.push_back("gun " + g->id + " has degenerate range table");
        else if (std::fabs(g->table.back().range - g->maxRange) / g->maxRange > 0.05)
            problems.push_back("gun " + g->id + " range calibration off: " + std::to_string(g->table.back().range));
    }
    for (auto& t : torpedoes_)
        if (t->settings.empty()) problems.push_back("torpedo " + t->id + " has no speed settings");
    for (auto& c : classes_) {
        if (c->nation == Nation::Unknown) problems.push_back("class " + c->id + " has unknown nation");
        if (c->type == ShipType::Unknown) problems.push_back("class " + c->id + " has unknown type");
        if (c->ships.empty()) problems.push_back("class " + c->id + " lists no ships");
        for (auto& m : c->mounts) {
            if (!m.gun) problems.push_back("class " + c->id + " mount " + m.id + " unknown gun " + m.weapon);
            if (!c->director(m.director)) problems.push_back("class " + c->id + " mount " + m.id + " unknown director " + m.director);
            if (m.arcMax <= m.arcMin) problems.push_back("class " + c->id + " mount " + m.id + " invalid arc");
        }
        for (auto& t : c->torpedoMounts)
            if (!t.torpedo) problems.push_back("class " + c->id + " torpedo mount " + t.id + " unknown torpedo " + t.weapon);
        for (auto& s : c->sensors)
            if (!s.def) problems.push_back("class " + c->id + " unknown sensor " + s.sensor);
        for (auto& d : c->directors)
            if (!d.radar.empty() && !d.radarDef) problems.push_back("class " + c->id + " director radar " + d.radar + " unknown");
        if (c->length <= c->beam) problems.push_back("class " + c->id + " implausible dimensions");
    }
    return problems;
}

const GunDef* Database::gun(const std::string& id) const {
    for (auto& g : guns_)
        if (g->id == id) return g.get();
    return nullptr;
}
const TorpedoDef* Database::torpedo(const std::string& id) const {
    for (auto& t : torpedoes_)
        if (t->id == id) return t.get();
    return nullptr;
}
const SensorDef* Database::sensor(const std::string& id) const {
    for (auto& s : sensors_)
        if (s->id == id) return s.get();
    return nullptr;
}
const ShipClassDef* Database::shipClass(const std::string& id) const {
    for (auto& c : classes_)
        if (c->id == id) return c.get();
    return nullptr;
}
const NationDef* Database::nation(Nation n) const {
    for (auto& x : nations_)
        if (x.nation == n) return &x;
    return nullptr;
}

GunDef& Database::addGun(std::unique_ptr<GunDef> g) { return *guns_.emplace_back(std::move(g)); }
TorpedoDef& Database::addTorpedo(std::unique_ptr<TorpedoDef> t) { return *torpedoes_.emplace_back(std::move(t)); }
SensorDef& Database::addSensor(std::unique_ptr<SensorDef> s) { return *sensors_.emplace_back(std::move(s)); }
ShipClassDef& Database::addShipClass(std::unique_ptr<ShipClassDef> c) { return *classes_.emplace_back(std::move(c)); }

}  // namespace it::sim
