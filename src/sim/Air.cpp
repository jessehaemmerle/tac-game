// Air units: flights and their aircraft. Formation flying, orders, attack runs (torpedo, dive, level,
// strafing), air combat, scouting reports and the World's air interface.
#include "sim/Air.h"

#include <algorithm>

#include "core/Units.h"
#include "sim/Ballistics.h"
#include "sim/Damage.h"
#include "sim/World.h"

namespace it::sim {

const char* airOrderKey(AirOrderType t) {
    switch (t) {
        case AirOrderType::Patrol: return "air.order.patrol";
        case AirOrderType::Attack: return "air.order.attack";
        case AirOrderType::Cap: return "air.order.cap";
        case AirOrderType::Escort: return "air.order.escort";
        case AirOrderType::Flares: return "air.order.flares";
        case AirOrderType::Spot: return "air.order.spot";
        case AirOrderType::Return: return "air.order.return";
        default: return "air.order.none";
    }
}

AirOrderType airOrderFromString(const std::string& s) {
    if (s == "patrol" || s == "search") return AirOrderType::Patrol;
    if (s == "attack" || s == "strike") return AirOrderType::Attack;
    if (s == "cap") return AirOrderType::Cap;
    if (s == "escort") return AirOrderType::Escort;
    if (s == "flares") return AirOrderType::Flares;
    if (s == "spot" || s == "shadow") return AirOrderType::Spot;
    if (s == "return") return AirOrderType::Return;
    return AirOrderType::None;
}

const char* flightStateKey(FlightState s) {
    switch (s) {
        case FlightState::Ready: return "air.state.ready";
        case FlightState::Launching: return "air.state.launching";
        case FlightState::Transit: return "air.state.transit";
        case FlightState::Patrol: return "air.state.patrol";
        case FlightState::Station: return "air.state.station";
        case FlightState::Intercept: return "air.state.intercept";
        case FlightState::Attack: return "air.state.attack";
        case FlightState::Rally: return "air.state.rally";
        case FlightState::Returning: return "air.state.returning";
        case FlightState::Rearming: return "air.state.rearming";
        default: return "air.state.lost";
    }
}

int Flight::alive(const World& w) const {
    int n = 0;
    for (EntityId id : planes)
        if (const Aircraft* a = w.plane(id); a && (a->fate == Aircraft::Fate::Flying || a->fate == Aircraft::Fate::Parked)) ++n;
    return n;
}

namespace {

constexpr double kOrbitRadius = 3500.0;

double crewSkill(CrewQuality q) { return crewFactor(q); }

/// Formation slot (x forward, y starboard, z up) of aircraft k: vics of three in a large V, each block of
/// three vics stacked behind the one ahead (the Japanese chutai of nine, the American division).
Vec3 slotOffset(int k, double s) {
    int v = k / 3, m = k % 3;
    int block = v / 3, inBlock = v % 3;
    double x = -block * 7.5 * s, y = 0, z = -block * 15.0;
    if (inBlock == 1) x -= 2.5 * s, y = -2.5 * s, z -= 8.0;
    if (inBlock == 2) x -= 2.5 * s, y = 2.5 * s, z -= 8.0;
    if (m == 1) x -= s, y -= s;
    if (m == 2) x -= s, y += s;
    return {x, y, z};
}

double spacingFor(const AircraftDef& d) { return d.role == AirRole::Fighter ? 30.0 : std::clamp(d.model.span * 1.8, 25.0, 70.0); }

double turnRateFor(const AircraftDef& d) { return 3.0 + 9.0 * d.agility; }

Vec2 basePosition(const World& w, const AirBase& b) {
    if (b.ship != kNoEntity)
        if (const Ship* s = w.ship(b.ship)) return s->pos;
    return b.pos;
}

void arm(Aircraft& a, const AircraftLoadout* l) {
    a.gunAmmo = a.def ? a.def->gunSeconds : 0.0;
    a.torpedoes = l && l->torpedo ? std::max(1, l->count) : 0;
    a.bombs = l && l->bomb ? std::max(1, l->count) : 0;
    a.bombs2 = l && l->bomb2 ? l->count2 : 0;
    a.aborted = false;
}

/// Steer an aircraft towards a point in space at a speed; returns the horizontal distance left.
double flyTowards(Aircraft& a, const Vec3& p, double speed, double dt, double maxTurn) {
    Vec2 d = p.xy() - a.pos.xy();
    double dist = length(d);
    double desired = dist > 1.0 ? bearingOf(d) : a.heading;
    double err = angleDiff(a.heading, desired);
    double rate = clamp(err * 0.9, -maxTurn, maxTurn);
    a.heading = wrap360(a.heading + rate * dt);
    double bankTarget = clamp(std::atan(rate * kDegToRad * std::max(a.speed, 30.0) / 9.81) * kRadToDeg, -70.0, 70.0);
    a.bank += (bankTarget - a.bank) * std::min(1.0, dt * 2.0);
    a.speed += clamp(speed - a.speed, -8.0 * dt, 5.0 * dt);
    double climbMax = a.def ? a.def->climbMps : 6.0;
    double vz = clamp((p.z - a.pos.z) * 0.25, -std::max(12.0, climbMax * 2.0), climbMax);
    a.pos.z += vz * dt;
    a.pitch += (std::atan2(vz, std::max(20.0, a.speed)) * kRadToDeg - a.pitch) * std::min(1.0, dt * 2.0);
    a.pos += Vec3{bearingVector(a.heading) * (a.speed * dt), 0.0};
    return dist;
}

/// Leader reference of the flight towards a point at a speed and altitude.
double steerFlight(Flight& f, const Vec2& p, double speed, double alt, double dt) {
    Vec2 d = p - f.pos;
    double dist = length(d);
    double rate = turnRateFor(*f.def) * 0.5;
    if (dist > 1.0) f.heading = wrap360(f.heading + clamp(angleDiff(f.heading, bearingOf(d)) * 0.5, -rate, rate) * dt);
    f.speed += clamp(speed - f.speed, -4.0 * dt, 3.0 * dt);
    f.altitude += clamp(alt - f.altitude, -f.def->climbMps * 2.0 * dt, f.def->climbMps * dt);
    f.pos += bearingVector(f.heading) * (f.speed * dt);
    return dist;
}

/// Orbit the reference around a centre.
void orbitFlight(Flight& f, const Vec2& c, double radius, double speed, double alt, double dt) {
    Vec2 r = f.pos - c;
    double dist = length(r);
    if (dist > radius * 1.6) {
        steerFlight(f, c, speed, alt, dt);
        return;
    }
    // Tangent course plus a correction toward the circle.
    double brg = bearingOf(r);
    double tangent = wrap360(brg + 90.0);
    double corr = clamp((dist - radius) / radius * 45.0, -45.0, 45.0);
    double want = wrap360(tangent + corr);
    double rate = turnRateFor(*f.def) * 0.4;
    f.heading = wrap360(f.heading + clamp(angleDiff(f.heading, want), -rate, rate) * dt);
    f.speed += clamp(speed - f.speed, -4.0 * dt, 3.0 * dt);
    f.altitude += clamp(alt - f.altitude, -f.def->climbMps * 2.0 * dt, f.def->climbMps * dt);
    f.pos += bearingVector(f.heading) * (f.speed * dt);
}

void report(World& w, const Flight& f, const char* key, std::vector<std::string> args = {}) {
    Event e;
    e.type = EventType::FlightReport;
    e.time = w.time();
    e.side = f.side;
    e.value = f.id;
    e.pos = {f.pos, f.altitude};
    e.text = key;
    e.args = std::move(args);
    w.emit(e);
}

/// A ship's value as a target for the strike leader (capital ships first, as the doctrine of every navy).
double targetValue(const Ship& s) {
    switch (s.cls->type) {
        case ShipType::CV: return 10;
        case ShipType::BB: return 9;
        case ShipType::BC: return 8.5;
        case ShipType::CA: return 6;
        case ShipType::CL: case ShipType::CLAA: return 5;
        case ShipType::AP: case ShipType::AK: case ShipType::AO: return 4.5;
        case ShipType::DD: return 2.5;
        default: return 2;
    }
}

}  // namespace

// ============================================================================================ World API
int World::addAirBase(const AirBase& b) {
    AirBase nb = b;
    nb.id = int(airBases_.size());
    airBases_.push_back(nb);
    return nb.id;
}

AirBase* World::airBase(int id) { return id >= 0 && id < int(airBases_.size()) ? &airBases_[size_t(id)] : nullptr; }
const AirBase* World::airBase(int id) const { return id >= 0 && id < int(airBases_.size()) ? &airBases_[size_t(id)] : nullptr; }

Flight& World::addFlight(const std::string& name, Side side, const AircraftDef& def, const std::string& loadout, int count, int base,
                         CrewQuality crew) {
    Flight f;
    f.id = int(flights_.size());
    f.entity = nextId();
    f.name = name;
    f.side = side;
    f.nation = def.nation;
    f.def = &def;
    f.loadout = def.loadout(loadout);
    f.crew = crew;
    f.base = base;
    f.fuel = def.enduranceH * 3600.0;
    f.flaresLeft = def.flares;
    const AirBase* b = airBase(base);
    Vec2 bp = b ? basePosition(*this, *b) : Vec2{};
    f.pos = bp;
    f.heading = 0;
    f.speed = def.cruise();
    for (int k = 0; k < count; ++k) {
        Aircraft a;
        a.id = nextId();
        a.flight = f.id;
        a.def = &def;
        a.side = side;
        a.slot = k;
        a.fate = Aircraft::Fate::Parked;
        a.pos = {bp, 0.0};
        arm(a, f.loadout);
        planeIndex_[a.id] = planes_.size();
        planes_.push_back(a);
        f.planes.push_back(a.id);
    }
    flights_.push_back(f);
    return flights_.back();
}

void World::putAirborne(Flight& f, const Vec2& pos, double altitude, double heading) {
    f.pos = pos;
    f.altitude = altitude;
    f.heading = heading;
    f.speed = f.def->cruise();
    f.launched = int(f.planes.size());
    f.state = f.order.type == AirOrderType::None ? FlightState::Station : FlightState::Transit;
    if (f.order.type == AirOrderType::None) {
        f.order.type = AirOrderType::Patrol;
        f.order.point = pos;
        f.order.route = {pos};
    }
    double s = spacingFor(*f.def);
    for (EntityId id : f.planes) {
        Aircraft* a = plane(id);
        if (!a) continue;
        Vec3 o = slotOffset(a->slot, s);
        Vec2 off = bearingVector(heading) * o.x + bearingVector(heading + 90.0) * o.y;
        a->pos = {pos + off, altitude + o.z};
        a->heading = heading;
        a->speed = f.speed;
        a->fate = Aircraft::Fate::Flying;
    }
    f.sorties++;
}

Flight* World::flight(int id) { return id >= 0 && id < int(flights_.size()) ? &flights_[size_t(id)] : nullptr; }
const Flight* World::flight(int id) const { return id >= 0 && id < int(flights_.size()) ? &flights_[size_t(id)] : nullptr; }

Flight* World::flightOfEntity(EntityId e) {
    for (auto& f : flights_)
        if (f.entity == e) return &f;
    return nullptr;
}

Aircraft* World::plane(EntityId id) {
    auto it = planeIndex_.find(id);
    return it == planeIndex_.end() ? nullptr : &planes_[it->second];
}

const Aircraft* World::plane(EntityId id) const {
    auto it = planeIndex_.find(id);
    return it == planeIndex_.end() ? nullptr : &planes_[it->second];
}

bool World::orderFlight(int id, const AirOrder& o, std::string* why) {
    Flight* f = flight(id);
    if (!f || f->state == FlightState::Lost || f->alive(*this) == 0) {
        if (why) *why = "air.why.lost";
        return false;
    }
    if (o.type == AirOrderType::Attack && !(f->loadout && f->loadout->armed()) && f->def->role != AirRole::Fighter) {
        if (why) *why = "air.why.unarmed";
        return false;
    }
    if (o.type == AirOrderType::Flares && f->flaresLeft <= 0) {
        if (why) *why = "air.why.no_flares";
        return false;
    }
    if ((o.type == AirOrderType::Attack || o.type == AirOrderType::Flares || o.type == AirOrderType::Spot) && o.track &&
        !picture(f->side).find(o.track)) {
        if (why) *why = "air.why.no_contact";
        return false;
    }
    f->order = o;
    if (!f->airborne() && f->order.startTime < 0) f->order.startTime = time_;
    f->leg = 0;
    f->targetTruth = kNoEntity;
    f->interceptTruth = kNoEntity;
    if (const Track* t = o.track ? picture(f->side).find(o.track) : nullptr) f->targetPos = t->predict(time_);
    if (f->airborne()) {
        f->state = o.type == AirOrderType::Return ? FlightState::Returning : FlightState::Transit;
        f->stateTime = 0;
        for (EntityId pid : f->planes)
            if (Aircraft* a = plane(pid); a && a->flying() && a->run != Aircraft::Run::Dogfight) a->run = Aircraft::Run::None;
    }
    report(*this, *f, "air.report.order", {airOrderKey(o.type)});
    return true;
}

bool World::launchAircraft(Ship& s, bool flares, const Vec2& target) {
    if (!s.operational() || s.dmg.aviation < 0.4) return false;
    const AircraftDef* def = db_.catapultAircraft(s.cls->floatplaneStyle, s.nation);
    if (!def) return false;
    // A floatplane already recovered aboard and ready flies again; otherwise one from the hangar.
    Flight* f = nullptr;
    for (auto& fl : flights_) {
        const AirBase* b = airBase(fl.base);
        if (b && b->ship == s.id && fl.state == FlightState::Ready && fl.alive(*this) > 0) f = &fl;
    }
    if (!f) {
        if (s.floatplanes <= 0) return false;
        int base = -1;
        for (auto& b : airBases_)
            if (b.ship == s.id) base = b.id;
        if (base < 0) {
            AirBase b;
            b.name = s.name;
            b.side = s.side;
            b.ship = s.id;
            b.launchInterval = 60;
            b.turnaround = 1500;
            base = addAirBase(b);
        }
        f = &addFlight(s.name, s.side, *def, "recon", 1, base, s.crew);
        f->playerControlled = false;
    }
    s.floatplanes = std::max(0, s.floatplanes - 1);
    AirOrder o;
    o.type = flares ? AirOrderType::Flares : AirOrderType::Patrol;
    o.point = target;
    o.route = {target};
    o.radius = 3000;
    f->order = o;
    f->leg = 0;
    f->state = FlightState::Launching;
    f->stateTime = 0;
    f->launched = 0;
    f->nextLaunch = time_ + 60.0;  // the catapult trains out and the engine warms up before the shot
    f->flaresLeft = def->flares;
    f->fuel = def->enduranceH * 3600.0;
    Event e;
    e.type = EventType::Report;
    e.time = time_;
    e.a = s.id;
    e.side = s.side;
    e.text = flares ? "report.aircraft_flares" : "report.aircraft_recon";
    e.args = {s.name};
    emit(e);
    return true;
}

void World::dropTorpedo(Aircraft& a, const TorpedoDef& td, double heading) {
    Torpedo t;
    t.id = nextId();
    t.firer = a.id;
    t.side = a.side;
    t.def = &td;
    t.pos = a.pos.xy() + bearingVector(a.heading) * 60.0;  // the torpedo travels on through the air
    t.baseHeading = t.heading = wrap360(heading + rng_.normal() * td.wanderFraction * 20.0);
    const auto& setting = td.settings.front();
    t.speed = setting.speedKn * 0.514444;
    t.maxRun = setting.range * rng_.uniform(0.97, 1.03);
    // Dropped too fast or too high, the torpedo dives deep, breaks up or runs erratically.
    double over = std::max(0.0, a.speed / 0.514444 - td.dropKn) / std::max(1.0, td.dropKn) + std::max(0.0, a.pos.z - td.dropAltM) / std::max(10.0, td.dropAltM);
    t.deep = rng_.chance(td.deepRunChance + 0.6 * over);
    t.erratic = rng_.chance(td.erraticChance + 0.4 * over);
    if (over > 1.0 && rng_.chance(0.5)) t.maxRun = 0;  // broke up on hitting the water
    t.wanderPhase = rng_.uniform(0.0, kTwoPi);
    t.wanderAmp = td.wanderFraction * 30.0;
    torpedoes_.push_back(t);
    a.torpedoes = std::max(0, a.torpedoes - 1);
    if (Flight* f = flight(a.flight)) f->torpedoesDropped++;
    Event e;
    e.type = EventType::TorpedoDropped;
    e.time = time_;
    e.a = a.id;
    e.side = a.side;
    e.pos = {t.pos, 0.0};
    e.value2 = t.heading;
    emit(e);
}

void World::releaseBomb(Aircraft& a, const BombDef& b, const Vec3& vel, EntityId intended) {
    Shell sh;
    sh.id = nextId();
    sh.firer = a.id;
    sh.side = a.side;
    sh.gun = &b.ballistic;
    sh.shell = &b.ballistic.shells.front();
    sh.pos = a.pos - Vec3{0, 0, 2.0};
    sh.prevPos = sh.pos;
    sh.vel = vel;
    sh.origin = a.pos.xy();
    sh.intendedTarget = intended;
    addShell(sh);
    if (Flight* f = flight(a.flight)) f->bombsDropped++;
    Event e;
    e.type = EventType::BombReleased;
    e.time = time_;
    e.a = a.id;
    e.side = a.side;
    e.pos = a.pos;
    e.value = b.massKg;
    emit(e);
}

void World::aircraftHit(Aircraft& a, double damage, EntityId by) {
    if (!a.flying()) return;
    a.health -= damage / std::max(0.2, a.def->durability);
    a.smoke = std::max(a.smoke, clamp(1.0 - a.health, 0.0, 1.0));
    Event e;
    e.time = time_;
    e.a = a.id;
    e.b = by;
    e.side = a.side;
    e.pos = a.pos;
    e.value = a.flight;
    if (a.health <= 0.0) {
        a.fate = Aircraft::Fate::Falling;
        a.fateTime = 0;
        a.smoke = 1.0;
        a.run = Aircraft::Run::None;
        e.type = EventType::AircraftShotDown;
        emit(e);
        if (Flight* f = flight(a.flight)) f->losses++;
        if (Ship* s = ship(by)) s->aircraftShotDown++;
        if (Aircraft* k = plane(by))
            if (Flight* kf = flight(k->flight)) kf->kills++;
        return;
    }
    e.type = EventType::AircraftHit;
    emit(e);
    // Badly hit crews jettison their load and turn for home; good crews press on.
    if (a.health < 0.5 && a.armed() && a.run != Aircraft::Run::Dive) {
        const Flight* f = flight(a.flight);
        double press = f ? crewSkill(f->crew) : 1.0;
        if (rng_.chance(clamp(0.9 - 0.45 * press, 0.2, 0.7))) {
            a.aborted = true;
            a.torpedoes = a.bombs = a.bombs2 = 0;
            a.run = Aircraft::Run::Egress;
            a.runTime = 0;
        }
    }
}

bool World::aircraftSpotting(Side side, EntityId shipTruth) const {
    const Ship* t = ship(shipTruth);
    if (!t) return false;
    // Only aircraft sent to spot (or shadow) watch the fall of shot; flare droppers and strikes do not.
    for (const auto& f : flights_) {
        if (f.side != side || !f.airborne() || f.state == FlightState::Launching || f.order.type != AirOrderType::Spot) continue;
        if (distance(f.pos, t->pos) > 25000.0) continue;
        for (EntityId id : f.planes) {
            const Aircraft* a = plane(id);
            if (a && a->flying() && distance(a->pos.xy(), t->pos) < air::visualRangeToShip(*this, *a, *t)) return true;
        }
    }
    return false;
}

namespace air {

double cruiseAltitude(const Flight& f) {
    if (f.order.altitude > 0) return f.order.altitude;
    const AircraftDef& d = *f.def;
    if (f.loadout && f.loadout->torpedo) return 2500;
    switch (d.role) {
        case AirRole::Fighter: return 4000;
        case AirRole::Dive: return 3800;
        case AirRole::Level: return d.bombAltM > 0 ? d.bombAltM : 3200;
        case AirRole::Torpedo: return d.bombAltM > 0 ? d.bombAltM : 3200;
        case AirRole::Patrol: return 1200;
        default: return f.order.type == AirOrderType::Flares ? 1200 : 900;
    }
}

double timeToBase(const World& w, const Flight& f) {
    const AirBase* b = w.airBase(f.base);
    if (!b) return 0;
    return distance(f.pos, basePosition(w, *b)) / std::max(30.0, f.def->cruise()) * 1.1 + 900.0;
}

double visualRangeToShip(const World& w, const Aircraft& a, const Ship& s) {
    const auto& wx = w.env().weather();
    double h = std::max(30.0, a.pos.z);
    double horizon = 3570.0 * 1.08 * (std::sqrt(h) + std::sqrt(std::max(1.0, s.silhouetteHeight())));
    // From the air a ship is a plan view and, above all, a wake.
    double planArea = s.cls->length * s.cls->beam;
    double size = std::sqrt(planArea / 3400.0);
    double wake = 0.5 + 0.5 * smoothstep(4.0, 26.0, s.speedKn());
    double light = std::pow(clamp(w.illuminationOn(s), 0.004, 1.0), 0.45);
    double r = 45000.0 * size * wake * light;
    // Above the cloud base the sea is seen only through the gaps.
    if (a.pos.z > wx.cloudBaseM) r *= 1.0 - 0.85 * wx.cloudCover;
    r = std::min(r, wx.visibilityKm * 1000.0 * 1.3) * w.env().squallTransmission(a.pos.xy(), s.pos);
    // Burning ships, searchlights and gun flashes at night.
    if (s.dmg.totalFireIntensity() > 0.05) r = std::max(r, 8000.0 + 20000.0 * std::min(1.0, s.dmg.totalFireIntensity()));
    if (w.env().isNight() && w.time() - s.lastFired < 10.0) r = std::max(r, 25000.0);
    if (s.orders.searchlight && s.searchlightOnSince >= 0) r = std::max(r, 25000.0);
    // ASV radar (Swordfish, Black Cat Catalinas) sees through darkness and cloud.
    if (a.def->radar) r = std::max(r, lerp(12000.0, 30000.0, clamp(planArea / 4000.0, 0.0, 1.0)));
    return std::min(r, horizon);
}

namespace {

/// Visual range at which aircrews see another formation.
double airToAirRange(const World& w, const Aircraft& a, const Aircraft& b, int count) {
    double light = clamp(w.env().ambientLight(), 0.0, 1.0);
    double r = (2500.0 + 14000.0 * std::pow(light, 0.7)) * std::pow(std::max(1, count) / 3.0, 0.2);
    const auto& wx = w.env().weather();
    bool cloudBetween = (a.pos.z - wx.cloudBaseM) * (b.pos.z - wx.cloudBaseM) < 0;
    if (cloudBetween) r *= 1.0 - 0.8 * wx.cloudCover;
    return std::min(r, wx.visibilityKm * 1000.0);
}

Vec2 centroid(const World& w, const Flight& f, Vec2 fallback, double* alt = nullptr, int* n = nullptr) {
    Vec2 c{};
    double z = 0;
    int k = 0;
    for (EntityId id : f.planes)
        if (const Aircraft* a = w.plane(id); a && a->flying()) {
            c += a->pos.xy();
            z += a->pos.z;
            ++k;
        }
    if (n) *n = k;
    if (k == 0) return fallback;
    if (alt) *alt = z / k;
    return c / double(k);
}

// ------------------------------------------------------------------------------------------- scouting
/// The flight's crews report the ships they see to their side, and keep their own target in sight.
void scout(World& w, Flight& f) {
    double now = w.time();
    if (now < f.nextScan) return;
    f.nextScan = now + 10.0;
    ContactPicture& pic = w.picture(f.side);
    const Aircraft* eye = nullptr;
    for (EntityId id : f.planes)
        if (const Aircraft* a = w.plane(id); a && a->flying() && (!eye || a->slot < eye->slot)) eye = a;
    if (!eye) return;
    double skill = crewSkill(f.crew);
    // The ship the attack order is about, if the crew can find it.
    EntityId intended = kNoEntity;
    if (f.order.track)
        if (const Track* ot = pic.find(f.order.track)) intended = pic.truthOf(*ot);
    EntityId best = kNoEntity;
    double bestScore = -1;
    for (auto& sp : w.ships()) {
        Ship& s = *sp;
        if (!s.operational() || s.side == f.side || s.side == Side::Neutral) continue;
        double r = distance(eye->pos.xy(), s.pos);
        if (r > 80000.0) continue;
        double vr = visualRangeToShip(w, *eye, s);
        if (r > vr || !w.rng().chance(0.75 * clamp(skill, 0.5, 1.1))) continue;
        double q = r / std::max(1.0, vr);
        Measurement m;
        m.observer = kNoEntity;
        m.truth = s.id;
        m.sense = kSenseAircraft;
        m.pos = s.pos + Vec2{w.rng().normal(), w.rng().normal()} * (300.0 + 0.04 * r);
        m.sigma = 400.0 + 0.05 * r;
        m.level = q < 0.3 ? ClassLevel::TypeKnown : (q < 0.6 ? ClassLevel::SizeKnown : ClassLevel::Detected);
        m.size = s.cls->size();
        // Aircrews misidentified ships notoriously (destroyers became cruisers, cruisers battleships).
        ShipType t = s.cls->type;
        if (w.rng().chance(0.3 * (1.3 - skill))) t = t == ShipType::DD ? ShipType::CL : (t == ShipType::CL || t == ShipType::CA ? ShipType::BB : t);
        m.typeGuess = m.level >= ClassLevel::TypeKnown ? t : ShipType::Unknown;
        m.hasVelocity = q < 0.7;
        m.velocity = s.velocity() + Vec2{w.rng().normal(), w.rng().normal()} * 2.0;
        m.velSigma = 3.0;
        bool created = false;
        Track* tr = pic.report(m, now, &created);
        if (created && tr) report(w, f, "air.report.sighting", {shipTypeId(tr->typeGuess), std::to_string(int(bearingTo(f.pos, s.pos)))});
        // Attack target choice: the intended ship if seen, else the most valuable ship in sight.
        if (f.order.type == AirOrderType::Attack || f.order.type == AirOrderType::Flares || f.order.type == AirOrderType::Spot ||
            (f.order.type == AirOrderType::Patrol && f.order.strike)) {
            double score = targetValue(s) - r / 20000.0 + (s.id == intended ? 50.0 : 0.0);
            if (f.order.track && f.order.type != AirOrderType::Patrol) {
                const Track* ot = pic.find(f.order.track);
                if (ot && distance(ot->predict(now), s.pos) > 20000.0 && s.id != intended) score -= 30.0;
            }
            if (score > bestScore) {
                bestScore = score;
                best = s.id;
            }
        }
    }
    if (best != kNoEntity) {
        if (f.targetTruth != best && f.targetTruth != kNoEntity) {
            // Keep the chosen target unless it is lost from sight.
            if (const Ship* cur = w.ship(f.targetTruth); cur && cur->operational() && now - f.targetSeen < 30.0) best = f.targetTruth;
        }
        f.targetTruth = best;
        f.targetPos = w.ship(best)->pos;
        f.targetSeen = now;
    }
}

/// Enemy formations this flight sees (fighters looking for bombers to intercept).
EntityId spotEnemyFormation(World& w, const Flight& f, double maxRange, Vec2* where) {
    const Aircraft* eye = nullptr;
    for (EntityId id : f.planes)
        if (const Aircraft* a = w.plane(id); a && a->flying() && (!eye || a->slot < eye->slot)) eye = a;
    if (!eye) return kNoEntity;
    EntityId best = kNoEntity;
    double bestD = 1e18;
    for (auto& o : w.flights()) {
        if (o.side == f.side || o.side == Side::Neutral || !o.airborne()) continue;
        int n = 0;
        double alt = 0;
        Vec2 c = centroid(w, o, o.pos, &alt, &n);
        if (n == 0) continue;
        double d = distance(c, eye->pos.xy());
        const Aircraft* sample = nullptr;
        for (EntityId id : o.planes)
            if (const Aircraft* a = w.plane(id); a && a->flying()) sample = a;
        if (!sample || d > std::min(maxRange, airToAirRange(w, *eye, *sample, n))) continue;
        // Bombers before fighters; the nearest first.
        double score = d * (o.def->role == AirRole::Fighter ? 1.6 : 1.0);
        if (score < bestD) {
            bestD = score;
            best = o.entity;
            if (where) *where = c;
        }
    }
    return best;
}

// ------------------------------------------------------------------------------------ attack execution
void startDogfights(World& w, Flight& f, Flight& enemy) {
    std::vector<Aircraft*> targets;
    for (EntityId id : enemy.planes)
        if (Aircraft* a = w.plane(id); a && a->flying()) targets.push_back(a);
    if (targets.empty()) return;
    size_t k = 0;
    for (EntityId id : f.planes) {
        Aircraft* a = w.plane(id);
        if (!a || !a->flying() || a->aborted || a->gunAmmo <= 0.0) continue;
        a->run = Aircraft::Run::Dogfight;
        a->target = targets[k++ % targets.size()]->id;
        a->runTime = 0;
    }
    // Escorting and patrolling fighters of the other side turn to meet them.
    if (enemy.def->role == AirRole::Fighter)
        for (EntityId id : enemy.planes)
            if (Aircraft* e = w.plane(id); e && e->flying() && e->run != Aircraft::Run::Dogfight && e->gunAmmo > 0.0 && !f.planes.empty()) {
                e->run = Aircraft::Run::Dogfight;
                e->target = f.planes[size_t(w.rng().range(0, int(f.planes.size()) - 1))];
                e->runTime = 0;
            }
}

void dogfight(World& w, Aircraft& a, double dt) {
    Aircraft* t = w.plane(a.target);
    a.runTime += dt;
    double now = w.time();
    if (!t || !t->flying() || a.runTime > 600.0 || a.gunAmmo <= 0.0) {
        a.run = Aircraft::Run::None;
        a.target = kNoEntity;
        a.burstUntil = a.breakUntil = -1;
        return;
    }
    const Flight* f = w.flight(a.flight);
    double skill = f ? crewSkill(f->crew) : 1.0;
    double maxTurn = turnRateFor(*a.def) * (0.9 + 0.2 * skill) * 1.6;
    if (now < a.breakUntil) {
        // Breaking away after a firing pass, to set up the next one.
        Vec3 away{a.pos.xy() + bearingVector(a.breakHeading) * 2000.0, t->pos.z + 300.0};
        flyTowards(a, away, a.def->maxSpeed(), dt, maxTurn);
        return;
    }
    // Pursuit with a little lead.
    Vec3 lead = t->pos + Vec3{bearingVector(t->heading) * (t->speed * 1.2), 0.0};
    flyTowards(a, lead, a.def->maxSpeed(), dt, maxTurn);
    double d = length(t->pos - a.pos);
    double angleOff = std::fabs(angleDiff(a.heading, bearingTo(a.pos.xy(), t->pos.xy())));
    bool window = d < 650.0 && angleOff < 18.0 && std::fabs(t->pos.z - a.pos.z) < 120.0;
    if (a.def->firepower > 0 && window && a.burstUntil < 0) a.burstUntil = now + 2.5;  // open fire: a short burst
    if (a.burstUntil >= 0) {
        if (now <= a.burstUntil && window) {
            a.lastGuns = now;
            a.gunsAt = t->id;
            a.gunAmmo -= dt;
            double evade = t->def->role == AirRole::Fighter ? 1.0 - 0.5 * t->def->agility : 1.0;
            double rate = 0.15 * a.def->firepower * skill * evade;
            if (w.rng().chance(rate * dt)) w.aircraftHit(*t, w.rng().uniform(0.25, 0.55), a.id);
        }
        if (now > a.burstUntil || d < 120.0) {
            // Pass over: break away, come round again.
            a.burstUntil = -1;
            a.breakUntil = now + w.rng().uniform(10.0, 22.0);
            a.breakHeading = wrap360(a.heading + (w.rng().chance(0.5) ? 1.0 : -1.0) * w.rng().uniform(70.0, 120.0));
        }
    }
    // The target's gunners fire back - the more bombers close together, the denser the fire; a fighter on a
    // fighter's tail makes it break hard.
    if (t->flying() && d < 600.0) {
        double angleBehind = std::fabs(angleDiff(t->heading, bearingTo(t->pos.xy(), a.pos.xy())));
        if (t->def->defense > 0 && angleBehind > 100.0) {
            t->lastGuns = now;
            t->gunsAt = a.id;
            const Flight* tf = w.flight(t->flight);
            int mates = 0;
            if (tf)
                for (EntityId id : tf->planes)
                    if (const Aircraft* m = w.plane(id); m && m != t && m->flying() && length(m->pos - t->pos) < 500.0) ++mates;
            double rate = 0.03 * t->def->defense * (tf ? crewSkill(tf->crew) : 1.0) * std::min(3.0, 1.0 + 0.25 * mates);
            if (w.rng().chance(rate * dt)) w.aircraftHit(a, w.rng().uniform(0.25, 0.5), t->id);
        }
        if (t->def->role == AirRole::Fighter && angleBehind > 140.0 && t->run != Aircraft::Run::Dogfight && t->gunAmmo > 0.0) {
            t->run = Aircraft::Run::Dogfight;
            t->target = a.id;
            t->runTime = 0;
        }
    }
}

/// Lead point of the target for a weapon arriving after `tof` seconds (what the crew estimates).
Vec2 leadPoint(const Ship& s, double tof, double courseErrDeg, double speedErr) {
    Vec2 v = bearingVector(s.heading + courseErrDeg) * std::max(0.0, s.speed + speedErr);
    return s.pos + v * tof;
}

void torpedoRun(World& w, Flight& f, Aircraft& a, const Ship& tgt, double dt) {
    const TorpedoDef* td = f.loadout ? f.loadout->torpedo : nullptr;
    if (!td) return;
    double skill = crewSkill(f.crew);
    double dropSpeed = std::min(a.def->attackSpeed(), units::knotsToMs(td->dropKn) * 0.97);
    double dropAlt = std::min(60.0, td->dropAltM * 0.7);
    double tSpeed = td->settings.front().speedKn * 0.514444;
    a.runTime += dt;
    if (a.run == Aircraft::Run::Approach) {
        // Out to the set-up point on the target's bow, low over the water.
        Vec2 ahead = tgt.pos + tgt.velocity() * 60.0;
        double angle = 55.0 + 15.0 * (a.slot % 3) / 2.0;
        double sgn = a.side_ >= 0 ? 1.0 : -1.0;
        a.setup = ahead + bearingVector(tgt.heading + sgn * angle) * (a.releaseRange + 2000.0);
        double distSetup = flyTowards(a, {a.setup, dropAlt + 40.0}, a.def->maxSpeed() * 0.92, dt, turnRateFor(*a.def));
        double rangeNow = distance(a.pos.xy(), tgt.pos);
        double relAngle = std::fabs(angleDiff(tgt.heading, bearingTo(tgt.pos, a.pos.xy())));
        if (distSetup < 1500.0 || (rangeNow < a.releaseRange + 2500.0 && relAngle < 110.0) || a.runTime > 420.0) {
            a.run = Aircraft::Run::Run;
            a.runTime = 0;
            Event e;
            e.type = EventType::AirAttack;
            e.time = w.time();
            e.a = a.id;
            e.b = tgt.id;
            e.side = a.side;
            e.pos = a.pos;
            e.text = "torpedo";
            w.emit(e);
        }
        return;
    }
    if (a.run != Aircraft::Run::Run) return;
    // In the run: aim the aircraft along the torpedo's intercept course and drop at the release range.
    double courseErr = w.rng().normal() * 0.0;  // the estimate error is applied once, at the drop
    double t = 0;
    Vec2 aim;
    double intercept = bearingTo(a.pos.xy(), tgt.pos);
    if (solveIntercept(a.pos.xy(), tSpeed, tgt.pos, tgt.velocity(), t, aim)) intercept = bearingTo(a.pos.xy(), aim);
    (void)courseErr;
    flyTowards(a, {a.pos.xy() + bearingVector(intercept) * 1000.0, dropAlt}, dropSpeed, dt, turnRateFor(*a.def) * 1.2);
    double range = distance(a.pos.xy(), tgt.pos);
    double headingErr = std::fabs(angleDiff(a.heading, intercept));
    bool slowLow = a.speed <= units::knotsToMs(td->dropKn) * 1.05 && a.pos.z <= td->dropAltM * 1.2;
    // A torpedo needs the target on the beam: a ship that has turned her bow or stern to the aircraft
    // (combing) spoils the attack, and the pilot goes round for another approach.
    double aspect = std::fabs(angleDiff(tgt.heading, bearingTo(tgt.pos, a.pos.xy())));
    bool goodAspect = aspect > 25.0 && aspect < 140.0;
    if (range < a.releaseRange * 0.65 && !goodAspect && a.runTime < 150.0 && a.side_ != 0) {
        a.run = Aircraft::Run::Approach;
        a.runTime = 0;
        a.side_ = -a.side_ * 2;  // the other bow; |side_| > 1 marks a second approach
        if (std::abs(a.side_) > 2) a.side_ = a.side_ > 0 ? 1 : -1;
        return;
    }
    if ((range <= a.releaseRange && headingErr < 12.0 && slowLow && goodAspect) || a.runTime > 150.0) {
        // The crew's estimate of the target's course and speed.
        double cErr = w.rng().normal() * (10.0 / skill);
        double sErr = w.rng().normal() * (3.0 / skill) * 0.514444;
        double aimHeading = intercept;
        Vec2 v = bearingVector(tgt.heading + cErr) * std::max(0.0, tgt.speed + sErr);
        if (solveIntercept(a.pos.xy(), tSpeed, tgt.pos, v, t, aim)) aimHeading = bearingTo(a.pos.xy(), aim);
        w.dropTorpedo(a, *td, aimHeading);
        a.run = Aircraft::Run::Egress;
        a.runTime = 0;
        // Break away across the target's stern.
        a.setup = tgt.pos - tgt.velocity() * 40.0 + bearingVector(a.heading + (a.side_ > 0 ? 70.0 : -70.0)) * 3000.0;
    }
}

void diveRun(World& w, Flight& f, Aircraft& a, const Ship& tgt, double dt) {
    const BombDef* bd = a.bombs > 0 && f.loadout ? f.loadout->bomb : nullptr;
    if (!bd) return;
    double skill = crewSkill(f.crew);
    a.runTime += dt;
    if (a.run == Aircraft::Run::Approach) {
        // To the push-over point upwind of the target, along her course (the best chance of a hit).
        double alt = std::max(2500.0, f.altitude);
        double diveHeading = tgt.speedKn() > 5.0 ? tgt.heading : bearingTo(a.pos.xy(), tgt.pos);
        Vec2 entry = tgt.pos + tgt.velocity() * 25.0 - bearingVector(diveHeading) * (alt * 0.38);
        double d = flyTowards(a, {entry, alt}, a.def->cruise() * 1.1, dt, turnRateFor(*a.def));
        if ((d < 400.0 && a.runTime > 10.0) || a.runTime > 300.0) {
            a.run = Aircraft::Run::Dive;
            a.runTime = 0;
            Event e;
            e.type = EventType::AirAttack;
            e.time = w.time();
            e.a = a.id;
            e.b = tgt.id;
            e.side = a.side;
            e.pos = a.pos;
            e.text = "dive";
            w.emit(e);
        }
        return;
    }
    if (a.run != Aircraft::Run::Dive) return;
    // In the dive: 65-75 degrees, dive brakes out, the pilot holding the target in his sight.
    double releaseAlt = 650.0 - 150.0 * (skill - 1.0) * 2.0;
    double vDive = a.def->attackSpeed();
    double fall = std::sqrt(2.0 * releaseAlt / kGravity) * 0.6;
    Vec2 aim = leadPoint(tgt, fall + (a.pos.z - releaseAlt) / std::max(30.0, vDive * 0.94), 0.0, 0.0);
    Vec2 d = aim - a.pos.xy();
    double horiz = length(d);
    double pitch = -std::clamp(std::atan2(a.pos.z, std::max(1.0, horiz)) * kRadToDeg, 55.0, 78.0);
    a.heading = wrap360(a.heading + clamp(angleDiff(a.heading, bearingOf(d)), -20.0 * dt, 20.0 * dt));
    a.pitch += (pitch - a.pitch) * std::min(1.0, dt * 1.5);
    a.bank *= std::max(0.0, 1.0 - dt);
    a.speed += clamp(vDive - a.speed, -10.0 * dt, 12.0 * dt);
    Vec3 dir{bearingVector(a.heading) * std::cos(a.pitch * kDegToRad), std::sin(a.pitch * kDegToRad)};
    a.pos += dir * (a.speed * dt);
    if (a.pos.z <= releaseAlt || a.runTime > 40.0) {
        // Aim error grows with the release height and with a shaken (hit) crew.
        double sigma = (12.0 + 0.035 * a.pos.z) / skill * (a.health < 0.8 ? 1.8 : 1.0);
        Vec3 vel = dir * a.speed;
        Vec2 err = Vec2{w.rng().normal(), w.rng().normal()} * (sigma / std::max(2.0, fall * 1.6));
        vel += Vec3{err, 0.0};
        w.releaseBomb(a, *bd, vel, tgt.id);
        a.bombs = 0;
        if (a.bombs2 > 0 && f.loadout->bomb2) {
            for (int k = 0; k < a.bombs2; ++k) {
                Vec2 e2 = Vec2{w.rng().normal(), w.rng().normal()} * (sigma * 1.5 / std::max(2.0, fall * 1.6));
                w.releaseBomb(a, *f.loadout->bomb2, dir * a.speed + Vec3{e2, 0.0}, tgt.id);
            }
            a.bombs2 = 0;
        }
        a.run = Aircraft::Run::Egress;
        a.runTime = 0;
        a.setup = a.pos.xy() + bearingVector(a.heading + w.rng().uniform(-60.0, 60.0)) * 5000.0;
    }
}

void strafeRun(World& w, Flight& f, Aircraft& a, Ship& tgt, double dt) {
    a.runTime += dt;
    if (a.run == Aircraft::Run::Approach) {
        Vec2 entry = tgt.pos + bearingVector(tgt.heading + (a.slot % 2 ? 140.0 : -140.0)) * 4000.0;
        double d = flyTowards(a, {entry, 400.0}, a.def->maxSpeed(), dt, turnRateFor(*a.def));
        if (d < 800.0 || a.runTime > 200.0) {
            a.run = Aircraft::Run::Run;
            a.runTime = 0;
        }
        return;
    }
    if (a.run != Aircraft::Run::Run) return;
    flyTowards(a, {tgt.pos, 60.0}, a.def->maxSpeed(), dt, turnRateFor(*a.def) * 1.3);
    double d = distance(a.pos.xy(), tgt.pos);
    if (d < 1200.0 && d > 250.0) {
        a.lastGuns = w.time();
        a.gunsAt = tgt.id;
        if (w.rng().chance(0.5 * dt)) {
            // Machine guns sweep the open AA positions and bridges of a ship.
            tgt.aaSuppressedUntil = std::max(tgt.aaSuppressedUntil, w.time() + 25.0);
            tgt.dmg.crewLoss = std::min(1.0, tgt.dmg.crewLoss + 0.004 * a.def->firepower * crewSkill(f.crew) / std::max(0.3, tgt.displacement() / 3000.0));
        }
    }
    if (d < 250.0 || a.runTime > 90.0) {
        a.run = Aircraft::Run::Egress;
        a.runTime = 0;
        a.setup = a.pos.xy() + bearingVector(a.heading) * 5000.0;
        a.bombs = a.torpedoes = 0;  // one pass
        a.aborted = true;
    }
}

/// Level bombers: the whole formation runs in along (or across) the target and releases on the leader.
void levelRun(World& w, Flight& f, const Ship& tgt, double dt) {
    if (!f.loadout || !f.loadout->bomb) return;
    double skill = crewSkill(f.crew);
    double alt = air::cruiseAltitude(f);
    // Plan the run: approach along the target's course from astern.
    double runHeading = tgt.speedKn() > 4.0 ? tgt.heading : f.heading;
    double v = f.def->cruise() * 1.05;
    if (f.bombFall <= 0.0) {
        // The bombsight's tables: time of fall and forward throw of this bomb from this height and speed.
        Vec3 p{0, 0, alt}, vel{v, 0, 0};
        const BombDef& b = *f.loadout->bomb;
        ballistics::ShellParams bp{b.massKg, b.caliberMm / 1000.0, b.ballistic.shells.front().formFactor};
        double t = 0;
        while (p.z > 0.0 && t < 120.0) {
            ballistics::integrate(p, vel, 0.05, bp);
            t += 0.05;
        }
        f.bombFall = t;
        f.bombThrow = p.x;
    }
    double fall = f.bombFall;
    double throwDist = f.bombThrow;
    Vec2 impactAim = tgt.pos + tgt.velocity() * fall;
    Vec2 release = impactAim - bearingVector(runHeading) * throwDist;
    Vec2 start = release - bearingVector(runHeading) * 9000.0;
    double distStart = distance(f.pos, start);
    double onCourse = std::fabs(angleDiff(f.heading, runHeading));
    if (f.stateTime < 2.0) {
        f.leg = 0;
        f.bombFall = 0.0;
    }
    if (f.leg == 0) {
        // Out to the start of the run.
        steerFlight(f, start, v, alt, dt);
        if (distStart < 2500.0) f.leg = 1;
        if (f.stateTime > 900.0) f.leg = 1;
        return;
    }
    // Straight and level on the bombsight: the leader corrects onto the release point.
    steerFlight(f, release + bearingVector(runHeading) * 200.0, v, alt, dt);
    Vec2 toRelease = release - f.pos;
    double along = dot(toRelease, bearingVector(f.heading));
    if (f.leg == 1 && f.stateTime > 30.0) {
        Event e;
        e.type = EventType::AirAttack;
        e.time = w.time();
        e.b = tgt.id;
        e.side = f.side;
        e.pos = {f.pos, f.altitude};
        e.text = "level";
        e.value = f.id;
        if (!f.planes.empty()) e.a = f.planes.front();
        w.emit(e);
        f.leg = 2;
    }
    if (along > 0.0 && f.stateTime < 1200.0) return;
    // Bombs away: every aircraft releases its stick at once; the pattern follows the formation.
    // The bomb-aimer's error moves the whole pattern; each bomb has its own small dispersion.
    double sigma = alt * (0.016 - 0.005 * (skill - 1.0) * 3.0) * (onCourse > 10.0 ? 1.8 : 1.0);
    Vec2 patternErr = Vec2{w.rng().normal(), w.rng().normal()} * (sigma / fall);
    for (EntityId id : f.planes) {
        Aircraft* a = w.plane(id);
        if (!a || !a->flying() || a->aborted || a->bombs <= 0) continue;
        int n = a->bombs >= 4 ? a->bombs / 2 : a->bombs;  // big loads: two runs
        for (int k = 0; k < n; ++k) {
            Vec3 vel{bearingVector(a->heading) * a->speed + patternErr, 0.0};
            vel += Vec3{Vec2{w.rng().normal(), w.rng().normal()} * (25.0 / fall), 0.0};
            // Bombs of a stick leave a fraction of a second apart.
            Aircraft tmp = *a;
            tmp.pos += Vec3{bearingVector(a->heading) * (a->speed * 0.25 * k), 0.0};
            w.releaseBomb(tmp, *f.loadout->bomb, vel, tgt.id);
        }
        a->bombs -= n;
    }
    f.leg = 3;
}

/// Begin attack runs of the flight's aircraft on the chosen target.
void assignRuns(World& w, Flight& f, const Ship& tgt) {
    double skill = crewSkill(f.crew);
    int k = 0;
    // Anvil attack: torpedo aircraft split to both bows when there are enough of them.
    int armed = 0;
    for (EntityId id : f.planes)
        if (const Aircraft* a = w.plane(id); a && a->flying() && a->armed() && !a->aborted) ++armed;
    double approachSide = angleDiff(tgt.heading, bearingTo(tgt.pos, f.pos)) >= 0 ? 1.0 : -1.0;
    for (EntityId id : f.planes) {
        Aircraft* a = w.plane(id);
        if (!a || !a->flying() || a->aborted || a->run != Aircraft::Run::None) continue;
        if (!a->armed() && f.def->role != AirRole::Fighter) continue;
        a->target = tgt.id;
        a->run = Aircraft::Run::Approach;
        a->runTime = -2.5 * k;  // aircraft peel off one after another
        a->side_ = armed >= 4 ? (k % 2 ? -1 : 1) : int(approachSide);
        a->releaseRange = (1700.0 - 450.0 * (skill - 0.85) / 0.3) * w.rng().uniform(0.85, 1.15);
        ++k;
    }
}

// ------------------------------------------------------------------------------------------ per flight
void stepFlight(World& w, Flight& f, double dt) {
    double now = w.time();
    f.stateTime += dt;
    const AirBase* base = w.airBase(f.base);
    Vec2 home = base ? basePosition(w, *base) : f.pos;
    int flying = 0;
    for (EntityId id : f.planes)
        if (const Aircraft* a = w.plane(id); a && a->flying()) ++flying;
    if (f.airborne() && f.state != FlightState::Launching && flying == 0) {
        f.state = FlightState::Lost;
        report(w, f, "air.report.lost");
        return;
    }
    if (f.state == FlightState::Lost) return;
    double cruise = f.def->cruise();
    double alt = air::cruiseAltitude(f);
    // Fuel.
    if (f.airborne()) {
        f.fuel -= dt * std::pow(std::max(0.5, f.speed / std::max(1.0, cruise)), 1.5);
        if (f.state != FlightState::Returning && f.state != FlightState::Launching && f.fuel < air::timeToBase(w, f)) {
            f.state = FlightState::Returning;
            f.stateTime = 0;
            report(w, f, "air.report.fuel");
        }
        if (f.fuel <= 0) {
            for (EntityId id : f.planes)
                if (Aircraft* a = w.plane(id); a && a->flying()) {
                    a->fate = Aircraft::Fate::Ditched;
                    a->fateTime = 0;
                    f.losses++;
                }
            f.state = FlightState::Lost;
            report(w, f, "air.report.ditched");
            return;
        }
    }
    switch (f.state) {
        case FlightState::Ready:
            f.pos = home;
            if (f.order.type != AirOrderType::None && f.order.type != AirOrderType::Return && now >= f.order.startTime &&
                f.order.startTime >= 0) {
                f.state = FlightState::Launching;
                f.stateTime = 0;
                f.launched = 0;
                f.nextLaunch = now;
            }
            return;
        case FlightState::Rearming:
            f.pos = home;
            if (f.stateTime > (base ? base->turnaround : 2700.0)) {
                f.state = FlightState::Ready;
                f.stateTime = 0;
                f.fuel = f.def->enduranceH * 3600.0;
                f.flaresLeft = f.def->flares;
                for (EntityId id : f.planes)
                    if (Aircraft* a = w.plane(id); a && a->fate == Aircraft::Fate::Parked) arm(*a, f.loadout);
                report(w, f, "air.report.ready");
            }
            return;
        case FlightState::Launching: {
            // Aircraft take off one after another and circle the base while the formation assembles.
            Ship* ship = base && base->ship != kNoEntity ? w.ship(base->ship) : nullptr;
            if (f.launched < int(f.planes.size()) && now >= f.nextLaunch) {
                Aircraft* a = w.plane(f.planes[size_t(f.launched)]);
                if (a && a->fate == Aircraft::Fate::Parked) {
                    a->fate = Aircraft::Fate::Flying;
                    a->pos = {home, ship ? ship->cls->model.freeboardMid + 6.0 : 5.0};
                    a->heading = ship ? ship->heading : f.heading;
                    a->speed = cruise * 0.7;
                    a->pitch = 8;
                    a->bank = 0;
                    a->run = Aircraft::Run::None;
                }
                f.launched++;
                f.nextLaunch = now + (base ? base->launchInterval : 25.0);
                if (f.launched == 1) {
                    f.pos = home;
                    f.heading = a ? a->heading : 0.0;
                    f.altitude = 300.0;
                    Event e;
                    e.type = EventType::FlightLaunched;
                    e.time = now;
                    e.side = f.side;
                    e.value = f.id;
                    e.pos = {home, 0.0};
                    w.emit(e);
                    f.sorties++;
                }
            }
            orbitFlight(f, home, 2500.0, cruise * 0.85, std::min(alt, 900.0), dt);
            if (f.launched >= int(f.planes.size()) && now >= f.nextLaunch + 30.0) {
                f.state = f.order.type == AirOrderType::Return ? FlightState::Returning : FlightState::Transit;
                f.stateTime = 0;
            }
            break;
        }
        case FlightState::Transit:
        case FlightState::Patrol: {
            Vec2 obj = f.order.point;
            const Track* tr = f.order.track ? w.picture(f.side).find(f.order.track) : nullptr;
            if (tr) obj = tr->predict(now);
            if (f.order.type == AirOrderType::Patrol && !f.order.route.empty()) obj = f.order.route[std::min(f.leg, f.order.route.size() - 1)];
            if ((f.order.type == AirOrderType::Cap || f.order.type == AirOrderType::Spot) && f.order.group >= 0)
                if (Group* g = w.group(f.order.group))
                    if (const Ship* gs = w.guideOf(*g)) obj = gs->pos;
            if (f.order.type == AirOrderType::Escort)
                if (const Flight* e = w.flight(f.order.escort); e && e->airborne()) obj = e->pos;
            double d = steerFlight(f, obj, cruise, alt, dt);
            if (f.order.type == AirOrderType::Attack || f.order.type == AirOrderType::Flares || f.order.type == AirOrderType::Spot ||
                f.order.type == AirOrderType::Patrol)
                scout(w, f);
            if (f.order.type == AirOrderType::Patrol) {
                if (d < 3000.0) {
                    if (f.leg + 1 < f.order.route.size()) f.leg++;
                    else {
                        f.state = FlightState::Station;  // search the last area until fuel or new orders
                        f.stateTime = 0;
                    }
                }
                if (f.order.strike && f.targetTruth != kNoEntity && now - f.targetSeen < 30.0 && f.loadout && f.loadout->armed()) {
                    f.order.type = AirOrderType::Attack;
                    f.state = FlightState::Attack;
                    f.stateTime = 0;
                    if (const Track* t = w.picture(f.side).byTruth(f.targetTruth)) f.order.track = t->id;
                    report(w, f, "air.report.attacking");
                }
            } else if (f.order.type == AirOrderType::Attack) {
                bool inSight = f.targetTruth != kNoEntity && now - f.targetSeen < 30.0;
                if (inSight && distance(f.pos, f.targetPos) < 30000.0) {
                    f.state = FlightState::Attack;
                    f.stateTime = 0;
                    report(w, f, "air.report.attacking");
                } else if (d < 4000.0 && !inSight) {
                    f.state = FlightState::Patrol;  // not there: search around the reported position
                    f.order.type = AirOrderType::Patrol;
                    f.order.strike = true;
                    f.order.route = {obj + Vec2{8000, 0}, obj + Vec2{0, 8000}, obj + Vec2{-8000, 0}, obj + Vec2{0, -8000}};
                    f.leg = 0;
                    report(w, f, "air.report.not_found");
                }
            } else if (f.order.type == AirOrderType::Return) {
                f.state = FlightState::Returning;
            } else if (d < 5000.0) {
                f.state = FlightState::Station;
                f.stateTime = 0;
            }
            break;
        }
        case FlightState::Station: {
            Vec2 c = f.order.point;
            const Track* tr = f.order.track ? w.picture(f.side).find(f.order.track) : nullptr;
            if (tr) c = tr->predict(now);
            if (f.targetTruth != kNoEntity && now - f.targetSeen < 60.0) c = f.targetPos;
            if (f.order.group >= 0)
                if (Group* g = w.group(f.order.group))
                    if (const Ship* gs = w.guideOf(*g)) c = gs->pos;
            if (f.order.type == AirOrderType::Escort) {
                const Flight* e = w.flight(f.order.escort);
                if (!e || !e->airborne()) {
                    f.state = FlightState::Returning;
                    break;
                }
                // Above and behind the escorted formation.
                steerFlight(f, e->pos - bearingVector(e->heading) * 800.0, std::max(e->speed, cruise * 0.9), e->altitude + 500.0, dt);
            } else if (f.order.type == AirOrderType::Spot) {
                orbitFlight(f, c, 15000.0, cruise * 0.9, alt, dt);  // shadow outside the reach of the AA guns
            } else if (f.order.type == AirOrderType::Patrol) {
                orbitFlight(f, c, 8000.0, cruise * 0.9, alt, dt);
            } else {
                orbitFlight(f, c, f.order.type == AirOrderType::Flares ? 3000.0 : kOrbitRadius, cruise * 0.85, alt, dt);
            }
            scout(w, f);
            if (f.order.type == AirOrderType::Flares && f.flaresLeft > 0 && now >= f.nextFlare) {
                Flare fl;
                fl.pos = {c + bearingVector(w.rng().uniform(0, 360)) * w.rng().uniform(0, 2000.0), std::max(600.0, f.altitude)};
                fl.intensity = 1.5;
                fl.maxLife = 150.0;
                fl.fallRate = 4.0;
                fl.side = f.side;
                fl.aircraft = true;
                w.addFlare(fl);
                f.flaresLeft--;
                f.nextFlare = now + 75.0;
                Event e;
                e.type = EventType::FlareDropped;
                e.time = now;
                e.a = base ? base->ship : kNoEntity;
                e.side = f.side;
                e.pos = fl.pos;
                e.value = f.id;
                w.emit(e);
            }
            if (f.order.type == AirOrderType::Flares && f.flaresLeft <= 0) {
                f.state = FlightState::Returning;
                f.stateTime = 0;
            }
            // Fighters on patrol or escort go for enemy formations they see or that radar reports.
            if (f.def->role == AirRole::Fighter && (f.order.type == AirOrderType::Cap || f.order.type == AirOrderType::Escort ||
                                                    f.order.type == AirOrderType::Patrol)) {
                Vec2 where;
                EntityId e = spotEnemyFormation(w, f, 25000.0, &where);
                if (e == kNoEntity && f.order.type == AirOrderType::Cap) {
                    // Radar direction from the ships: the nearest raid inside 40 km of the station.
                    double best = 40000.0;
                    for (const auto& t : w.picture(f.side).airTracks()) {
                        if (t.lost || t.sharedAt > now) continue;
                        double dd = distance(t.predict(now), c);
                        if (dd < best) {
                            best = dd;
                            where = t.predict(now);
                            e = w.picture(f.side).truthOf(t);
                        }
                    }
                }
                if (e != kNoEntity) {
                    f.interceptTruth = e;
                    f.state = FlightState::Intercept;
                    f.stateTime = 0;
                    report(w, f, "air.report.tallyho");
                }
            }
            if (f.order.type == AirOrderType::Patrol && f.order.strike && f.targetTruth != kNoEntity && now - f.targetSeen < 30.0 &&
                f.loadout && f.loadout->armed()) {
                f.order.type = AirOrderType::Attack;
                f.state = FlightState::Attack;
                f.stateTime = 0;
                if (const Track* t = w.picture(f.side).byTruth(f.targetTruth)) f.order.track = t->id;
                report(w, f, "air.report.attacking");
            }
            break;
        }
        case FlightState::Intercept: {
            Flight* enemy = w.flightOfEntity(f.interceptTruth);
            int n = 0;
            Vec2 c = enemy ? centroid(w, *enemy, enemy->pos, nullptr, &n) : f.pos;
            if (!enemy || n == 0 || f.stateTime > 900.0) {
                f.state = f.order.type == AirOrderType::Escort ? FlightState::Station : FlightState::Station;
                f.stateTime = 0;
                break;
            }
            steerFlight(f, c + bearingVector(enemy->heading) * (enemy->speed * 20.0), f.def->maxSpeed() * 0.95,
                        std::max(enemy->altitude + 300.0, 300.0), dt);
            bool fighting = false, ammo = false;
            for (EntityId id : f.planes)
                if (const Aircraft* a = w.plane(id); a && a->flying()) {
                    if (a->run == Aircraft::Run::Dogfight) fighting = true;
                    if (a->gunAmmo > 0.5) ammo = true;
                }
            if (!ammo) {
                f.state = FlightState::Returning;
                f.stateTime = 0;
                report(w, f, "air.report.no_ammo");
                break;
            }
            if (!fighting && distance(f.pos, c) < 3500.0) startDogfights(w, f, *enemy);
            if (fighting) f.stateTime = std::min(f.stateTime, 600.0);
            break;
        }
        case FlightState::Attack: {
            Ship* tgt = w.ship(f.targetTruth);
            scout(w, f);
            if (!tgt || !tgt->operational()) {
                // Target gone (sunk): pick another in sight, else rally.
                f.targetTruth = kNoEntity;
                scout(w, f);
                tgt = w.ship(f.targetTruth);
                if (!tgt) {
                    f.state = FlightState::Rally;
                    f.stateTime = 0;
                    break;
                }
            }
            // The load decides the attack: torpedoes are run in low; bombs are dropped from level flight
            // by bombers and in a dive by dive bombers, fighters and floatplanes.
            bool level = f.loadout && !f.loadout->torpedo && f.loadout->bomb &&
                         (f.def->role == AirRole::Level || f.def->role == AirRole::Torpedo || f.def->role == AirRole::Patrol);
            if (level) {
                levelRun(w, f, *tgt, dt);
                if (f.leg == 3) {
                    f.state = FlightState::Rally;
                    f.stateTime = 0;
                }
                break;
            }
            // Torpedo, dive and strafing attacks: the flight closes, then the aircraft go in individually.
            double d = distance(f.pos, tgt->pos);
            bool torpedo = f.loadout && f.loadout->torpedo;
            double approachAlt = torpedo ? (d < 15000.0 ? 150.0 : alt) : alt;
            double approachSpeed = torpedo && d < 15000.0 ? f.def->attackSpeed() : cruise * 1.1;
            if (d > 9000.0) steerFlight(f, tgt->pos, approachSpeed, approachAlt, dt);
            else orbitFlight(f, tgt->pos, 9000.0, approachSpeed, approachAlt, dt);
            bool anyRun = false, anyArmed = false;
            for (EntityId id : f.planes)
                if (const Aircraft* a = w.plane(id); a && a->flying()) {
                    if (a->run != Aircraft::Run::None && a->run != Aircraft::Run::Dogfight) anyRun = true;
                    if (a->armed() && !a->aborted) anyArmed = true;
                }
            if (d < (torpedo ? 12000.0 : 9000.0) && f.stateTime > 5.0) {
                bool started = false;
                for (EntityId id : f.planes)
                    if (const Aircraft* a = w.plane(id); a && a->run != Aircraft::Run::None) started = true;
                if (!started) assignRuns(w, f, *tgt);
            }
            if (f.def->role == AirRole::Fighter) anyArmed = f.stateTime < 300.0;
            if (!anyRun && !anyArmed && f.stateTime > 10.0) {
                f.state = FlightState::Rally;
                f.stateTime = 0;
            }
            if (f.stateTime > 1500.0) {
                f.state = FlightState::Rally;
                f.stateTime = 0;
            }
            break;
        }
        case FlightState::Rally: {
            // Re-form clear of the target, then go round again (bombs left) or home.
            Vec2 away = f.targetPos + normalize(f.pos - f.targetPos + Vec2{1, 0}) * 15000.0;
            steerFlight(f, away, cruise, alt, dt);
            bool busy = false, anyArmed = false;
            for (EntityId id : f.planes)
                if (const Aircraft* a = w.plane(id); a && a->flying()) {
                    if (a->run != Aircraft::Run::None && a->run != Aircraft::Run::Dogfight) busy = true;
                    if (a->armed() && !a->aborted) anyArmed = true;
                }
            if (!busy && f.stateTime > 90.0) {
                if (anyArmed && f.order.type == AirOrderType::Attack && f.fuel > air::timeToBase(w, f) + 1200.0) {
                    f.state = FlightState::Attack;
                    f.leg = 0;
                } else {
                    f.state = FlightState::Returning;
                    report(w, f, "air.report.returning");
                }
                f.stateTime = 0;
            }
            break;
        }
        case FlightState::Returning: {
            double d = steerFlight(f, home, cruise, std::min(alt, 1500.0), dt);
            if (d < 2500.0) {
                // Land: aircraft taken aboard / onto the field are parked for rearming.
                Ship* ship = base && base->ship != kNoEntity ? w.ship(base->ship) : nullptr;
                if (base && base->ship != kNoEntity && (!ship || !ship->operational())) {
                    for (EntityId id : f.planes)
                        if (Aircraft* a = w.plane(id); a && a->flying()) {
                            a->fate = Aircraft::Fate::Ditched;
                            a->fateTime = 0;
                        }
                    f.state = FlightState::Lost;
                    report(w, f, "air.report.ditched");
                    break;
                }
                int landed = 0;
                for (EntityId id : f.planes)
                    if (Aircraft* a = w.plane(id); a && a->flying()) {
                        a->fate = Aircraft::Fate::Parked;
                        a->run = Aircraft::Run::None;
                        a->pos = {home, 0.0};
                        a->health = std::max(a->health, 0.3);
                        a->smoke = 0;
                        ++landed;
                    }
                if (ship) ship->floatplanes += landed;
                f.state = FlightState::Rearming;
                f.stateTime = 0;
                f.order = {};
                f.order.startTime = -1;
                Event e;
                e.type = EventType::FlightLanded;
                e.time = now;
                e.side = f.side;
                e.value = f.id;
                e.pos = {home, 0.0};
                w.emit(e);
            }
            break;
        }
        default: break;
    }
}

void stepAircraft(World& w, Flight& f, Aircraft& a, double dt) {
    double now = w.time();
    if (a.fate == Aircraft::Fate::Falling) {
        // Spinning in, trailing fire and smoke.
        a.fateTime += dt;
        a.pitch = std::max(-70.0, a.pitch - 12.0 * dt);
        a.bank += 40.0 * dt * (a.id % 2 ? 1.0 : -1.0);
        a.speed = std::min(a.speed + 4.0 * dt, 160.0);
        Vec3 dir{bearingVector(a.heading) * std::cos(a.pitch * kDegToRad), std::sin(a.pitch * kDegToRad)};
        a.pos += dir * (a.speed * dt);
        if (a.pos.z <= 0.0) {
            a.pos.z = 0.0;
            a.fate = Aircraft::Fate::Destroyed;
            a.fateTime = 0;
            Event e;
            e.type = EventType::AircraftCrashed;
            e.time = now;
            e.a = a.id;
            e.side = a.side;
            e.pos = a.pos;
            w.emit(e);
        }
        return;
    }
    if (a.fate == Aircraft::Fate::Ditched || a.fate == Aircraft::Fate::Destroyed) {
        a.fateTime += dt;
        a.pos.z = 0.0;
        a.speed = 0.0;
        return;
    }
    if (a.fate != Aircraft::Fate::Flying) return;
    // Damaged aircraft lose speed and trail smoke; some of them do not make it home.
    if (a.health < 0.35 && w.rng().chance(0.0006 * dt * (0.35 - a.health) / 0.35)) {
        a.health = -0.01;
        w.aircraftHit(a, 0.01, kNoEntity);
        return;
    }
    // Individual runs.
    if (a.run == Aircraft::Run::Dogfight) {
        dogfight(w, a, dt);
        return;
    }
    Ship* tgt = w.ship(a.target);
    if (a.run == Aircraft::Run::Approach || a.run == Aircraft::Run::Run || a.run == Aircraft::Run::Dive) {
        if (a.runTime < 0.0) {
            a.runTime += dt;  // waiting for its turn: stay in formation
        } else if (!tgt || !tgt->operational()) {
            a.run = Aircraft::Run::Egress;
            a.runTime = 0;
            a.setup = a.pos.xy() + bearingVector(a.heading) * 4000.0;
        } else {
            if (f.loadout && f.loadout->torpedo && a.torpedoes > 0) torpedoRun(w, f, a, *tgt, dt);
            else if (f.loadout && f.loadout->bomb && a.bombs > 0) diveRun(w, f, a, *tgt, dt);
            else strafeRun(w, f, a, *tgt, dt);
            return;
        }
    }
    if (a.run == Aircraft::Run::Egress) {
        a.runTime += dt;
        flyTowards(a, {a.setup, std::max(200.0, std::min(a.pos.z + 150.0, 800.0))}, a.def->maxSpeed() * 0.9, dt, turnRateFor(*a.def));
        if (a.runTime > 45.0 || distance(a.pos.xy(), a.setup) < 800.0) {
            a.run = Aircraft::Run::None;
            a.runTime = 0;
        }
        return;
    }
    // Formation flying on the flight's reference.
    double s = spacingFor(*a.def);
    Vec3 o = slotOffset(a.slot, s);
    // Aborted aircraft straggle behind the formation.
    if (a.aborted) o.x -= 300.0;
    Vec2 slot = f.pos + bearingVector(f.heading) * o.x + bearingVector(f.heading + 90.0) * o.y;
    Vec2 ahead = slot + bearingVector(f.heading) * (std::max(f.speed, 30.0) * 4.0);
    double along = dot(slot - a.pos.xy(), bearingVector(f.heading));
    double speed = clamp(f.speed + along * 0.08, f.speed * 0.75, a.def->maxSpeed() * (a.health < 0.6 ? 0.8 : 1.0));
    if (f.state == FlightState::Launching && f.altitude < 200.0) speed = std::max(speed, a.def->cruise() * 0.7);
    double z = f.altitude + o.z;
    if (f.state == FlightState::Attack && a.armed()) z = std::max(z, 40.0);
    flyTowards(a, {ahead, z}, speed, dt, turnRateFor(*a.def));
}

}  // namespace

void step(World& w, double dt) {
    for (auto& f : w.flights()) {
        stepFlight(w, f, dt);
        for (EntityId id : f.planes)
            if (Aircraft* a = w.plane(id)) stepAircraft(w, f, *a, dt);
    }
}

}  // namespace air
}  // namespace it::sim
