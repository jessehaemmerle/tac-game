#include "sim/Damage.h"

#include "sim/Ballistics.h"
#include "sim/World.h"

#include <algorithm>

namespace it::sim::damage {
namespace {

double crewRate(const Ship& s) { return crewFactor(s.crew) * (1.0 - 0.7 * s.dmg.crewLoss) * (0.6 + 0.4 * s.readiness); }

double fOf(const Ship& s, double localX) { return clamp(0.5 - localX / s.cls->length, 0.0, 1.0); }

void report(World& w, const Ship& s, const char* key, std::vector<std::string> args = {}) {
    Event e;
    e.type = EventType::Report;
    e.time = w.time();
    e.a = s.id;
    e.side = s.side;
    e.text = key;
    if (args.empty()) args.push_back(s.name);
    e.args = std::move(args);
    w.emit(e);
}

void systemHit(World& w, Ship& s, double& health, double severity, const char* key) {
    if (health <= 0.0) return;
    double before = health;
    health = std::max(0.0, health - severity * w.rng().uniform(0.5, 1.2));
    if (before > 0.3 && health <= 0.3) {
        Event e;
        e.type = EventType::SystemDamaged;
        e.time = w.time();
        e.a = s.id;
        e.side = s.side;
        e.text = key;
        e.args = {s.name};
        w.emit(e);
    }
}

// Share of the machinery one hit can knock out: big ships have several engine and boiler rooms
// on separate shafts (unit system), a destroyer can be stopped by a single hit.
double machineryShare(const ShipClassDef& c) { return c.shafts >= 4 ? 0.45 : (c.shafts == 3 ? 0.65 : 1.0); }

void startFire(World& w, Ship& s, double f, double intensity) {
    for (auto& fire : s.dmg.fires)
        if (std::fabs(fire.f - f) < 0.06) {
            fire.intensity = std::min(1.0, fire.intensity + intensity * 0.6);
            return;
        }
    s.dmg.fires.push_back({f, clamp(intensity, 0.05, 1.0)});
    Event e;
    e.type = EventType::FireStarted;
    e.time = w.time();
    e.a = s.id;
    e.side = s.side;
    e.pos = {s.localToWorld((0.5 - f) * s.cls->length, 0), s.cls->model.deckHeightAt(f)};
    w.emit(e);
}

void magazineExplosion(World& w, Ship& s) {
    if (s.dmg.magazineExploded) return;
    s.dmg.magazineExploded = true;
    s.dmg.hullIntegrity = -1.0;
    Event e;
    e.type = EventType::MagazineExplosion;
    e.time = w.time();
    e.a = s.id;
    e.side = s.side;
    e.pos = {s.pos, 10.0};
    e.value = s.displacement() * 0.05;
    w.emit(e);
}

/// Damage systems located near ship-local x within `radius` (m). `inside` = the burst happened inside the hull.
void damageNear(World& w, Ship& s, const Vec3& p, double radius, double severity, bool inside, bool high, bool vitals) {
    const auto& c = *s.cls;
    for (size_t i = 0; i < s.mounts.size(); ++i) {
        auto& m = s.mounts[i];
        double d = std::hypot(m.def->x - p.x, m.def->y - p.y);
        if (d < radius) systemHit(w, s, m.health, severity * (1.0 - d / radius) * (m.def->armorFace > 50 ? 0.4 : 1.0), "system.mount");
    }
    for (auto& t : s.torpedoMounts) {
        double d = std::hypot(t.def->x - p.x, t.def->y - p.y);
        if (d < radius) {
            systemHit(w, s, t.health, severity * (1.0 - d / radius), "system.torpedo_mount");
            // Exposed torpedo warheads on deck: a deadly hazard for IJN cruisers.
            if (t.loaded > 0 && t.def->torpedo && w.rng().chance(0.10 * severity)) {
                double kg = t.def->torpedo->warheadKg * t.loaded * 0.5;
                t.loaded = 0;
                t.health = 0;
                s.dmg.hullIntegrity -= kg / (std::pow(s.displacement(), 0.85) * 1.6);
                s.dmg.crewLoss = std::min(1.0, s.dmg.crewLoss + 0.08);
                startFire(w, s, fOf(s, t.def->x), 0.8);
                Event e;
                e.type = EventType::Explosion;
                e.time = w.time();
                e.a = s.id;
                e.side = s.side;
                e.pos = {s.localToWorld(t.def->x, t.def->y), t.def->z};
                e.value = kg;
                w.emit(e);
                report(w, s, "report.torpedo_explosion");
            }
        }
    }
    for (auto& d : s.directors) {
        double dd = std::fabs(d.def->x - p.x);
        if (high && dd < radius * 1.5) systemHit(w, s, d.health, severity * 0.8, "system.director");
    }
    for (auto& sen : s.sensors) {
        double dd = std::fabs(sen.def->x - p.x);
        if (high && dd < radius * 1.6) systemHit(w, s, sen.health, severity * 0.7, "system.sensor");
    }
    double f = fOf(s, p.x);
    if (vitals && f > 0.35 && f < 0.65 && p.z < c.model.freeboardMid) {
        systemHit(w, s, s.dmg.propulsion, severity * 0.35 * machineryShare(c), "system.propulsion");
        systemHit(w, s, s.dmg.power, severity * 0.3 * machineryShare(c), "system.power");  // generators, switchboards
    }
    if (inside && p.z < c.model.freeboardMid + 3.0)
        s.dmg.power = std::max(0.0, s.dmg.power - severity * 0.04 * machineryShare(c));  // cut cables
    if (vitals && f > 0.88 && p.z < 1.0) {
        systemHit(w, s, s.dmg.steering, severity * 0.6, "system.steering");
        if (s.dmg.steering < 0.3 && !s.dmg.rudderJammed && w.rng().chance(0.5)) {
            s.dmg.rudderJammed = true;
            s.dmg.jammedRudder = s.rudder;
            report(w, s, "report.rudder_jammed");
        }
    }
    if (high && f > 0.2 && f < 0.42) systemHit(w, s, s.dmg.bridge, severity * 0.4, "system.bridge");
    if (high) systemHit(w, s, s.dmg.searchlights, severity * 0.2, "system.searchlights");
    if (c.aviationFuel && f > 0.5 && f < 0.7 && s.dmg.aviation > 0) {
        systemHit(w, s, s.dmg.aviation, severity, "system.aviation");
        // Burning floatplanes and aviation gasoline lit up USN cruisers at Savo Island.
        if (w.rng().chance(0.45)) startFire(w, s, f, 0.7);
    }
}

bool slab(double p0, double d, double lo, double hi, double& tmin, double& tmax, int axis, int& enterAxis, int& enterSign) {
    if (std::fabs(d) < 1e-12) return p0 >= lo && p0 <= hi;
    double t0 = (lo - p0) / d, t1 = (hi - p0) / d;
    int sign = -1;
    if (t0 > t1) {
        std::swap(t0, t1);
        sign = 1;
    }
    if (t0 > tmin) {
        tmin = t0;
        enterAxis = axis;
        enterSign = sign;
    }
    tmax = std::min(tmax, t1);
    return tmin <= tmax;
}

}  // namespace

bool intersect(const Ship& s, const Vec3& p0, const Vec3& p1, HitInfo& hit) {
    const auto& c = *s.cls;
    double L = c.length, B = c.beam, T = c.draft;
    double sinkDrop = s.dmg.sinkProgress * (c.model.freeboardMid + 4.0);
    auto toLocal = [&](const Vec3& p) {
        Vec2 l = s.worldToLocal(p.xy());
        return Vec3{l.x, l.y, p.z + sinkDrop};
    };
    Vec3 a = toLocal(p0), b = toLocal(p1);
    Vec3 d = b - a;
    struct Box {
        Vec3 lo, hi;
        HitFace topFace;
    };
    const Box boxes[] = {
        {{-0.28 * L, -0.5 * B, -T}, {0.30 * L, 0.5 * B, c.model.freeboardMid}, HitFace::Deck},
        {{0.30 * L, -0.3 * B, -T}, {0.5 * L, 0.3 * B, c.model.freeboardBow}, HitFace::Deck},
        {{-0.5 * L, -0.38 * B, -T}, {-0.28 * L, 0.38 * B, c.model.freeboardStern}, HitFace::Deck},
        {{-0.15 * L, -0.3 * B, c.model.freeboardMid}, {0.25 * L, 0.3 * B, c.model.freeboardMid + c.superstructureHeight * 0.6},
         HitFace::Superstructure},
    };
    double best = 2.0;
    for (int bi = 0; bi < 4; ++bi) {
        const Box& bx = boxes[bi];
        double tmin = 0.0, tmax = 1.0;
        int axis = -1, sign = 0;
        if (!slab(a.x, d.x, bx.lo.x, bx.hi.x, tmin, tmax, 0, axis, sign)) continue;
        if (!slab(a.y, d.y, bx.lo.y, bx.hi.y, tmin, tmax, 1, axis, sign)) continue;
        if (!slab(a.z, d.z, bx.lo.z, bx.hi.z, tmin, tmax, 2, axis, sign)) continue;
        if (tmin >= best || tmin > 1.0) continue;
        best = tmin;
        hit.t = tmin;
        hit.local = a + d * tmin;
        Vec3 dir = normalize(d);
        if (bi == 3) {
            hit.face = HitFace::Superstructure;
            hit.obliquityDeg = 30.0;
        } else if (axis == 2) {
            hit.face = HitFace::Deck;
            hit.obliquityDeg = std::acos(clamp(std::fabs(dir.z), 0.0, 1.0)) * kRadToDeg;
        } else if (axis == 1) {
            hit.face = HitFace::Side;
            hit.obliquityDeg = std::acos(clamp(std::fabs(dir.y), 0.0, 1.0)) * kRadToDeg;
        } else {
            hit.face = HitFace::End;
            hit.obliquityDeg = std::acos(clamp(std::fabs(dir.x), 0.0, 1.0)) * kRadToDeg;
        }
        hit.belowWater = hit.local.z < 0.0;
    }
    if (best > 1.0) return false;
    // Turret / mount hits.
    hit.mount = -1;
    for (size_t i = 0; i < s.mounts.size(); ++i) {
        const auto& m = *s.mounts[i].def;
        double rad = m.style.rfind("turret", 0) == 0 ? std::max(3.0, 0.028 * L * (0.8 + 0.2 * m.barrels)) : 2.0;
        if (std::hypot(hit.local.x - m.x, hit.local.y - m.y) < rad && hit.local.z > m.z - 3.0) {
            hit.mount = int(i);
            break;
        }
    }
    return true;
}

void shellHit(World& w, Ship& s, const Shell& sh, const HitInfo& hit) {
    const auto& c = *s.cls;
    const GunDef& g = *sh.gun;
    const ShellDef& sd = *sh.shell;
    double v = length(sh.vel);
    double fallDeg = std::atan2(-sh.vel.z, length(sh.vel.xy())) * kRadToDeg;
    double f = fOf(s, hit.local.x);
    bool citadel = f > 0.2 && f < 0.8;
    bool nearMagazine = false;
    for (auto& m : s.mounts)
        if (m.def->gun && m.def->gun->caliberMm >= 120 && std::fabs(m.def->x - hit.local.x) < 0.05 * c.length) nearMagazine = true;

    // ---- armour encountered
    double plate = 0;
    bool high = false, beltZone = false;
    if (hit.mount >= 0) {
        const auto& m = *s.mounts[size_t(hit.mount)].def;
        plate = hit.face == HitFace::Deck ? std::max(c.armor.turretRoof, m.armorFace * 0.4) : m.armorFace;
        high = true;
    } else if (hit.face == HitFace::Superstructure) {
        plate = (f > 0.25 && f < 0.35 && sh.vel.z > -50) ? c.armor.conningTower * 0.3 : 10.0;
        high = true;
    } else if (hit.face == HitFace::Deck) {
        plate = citadel ? std::max(12.0, c.armor.deck) : 12.0;
    } else {
        beltZone = citadel && hit.local.z > -2.0 && hit.local.z < 2.6;
        plate = beltZone ? std::max(16.0, nearMagazine ? c.armor.beltMagazine : c.armor.belt) : 16.0;
    }

    double pen = hit.face == HitFace::Deck
                     ? ballistics::deckPenetrationMm(sd.massKg, g.caliberMm, v, fallDeg)
                     : ballistics::beltPenetrationMm(sd.massKg, g.caliberMm, v, hit.obliquityDeg);
    if (sd.kind == ShellKind::Common) pen *= 0.45;
    if (sd.kind == ShellKind::HE) pen *= 0.25;
    bool penetrated = pen > plate;

    // ---- explosive effect
    double effect = sd.bursterKg + 0.5 * sd.massKg * v * v * 1e-6 * 0.2;
    bool overpen = false;
    if (sd.kind == ShellKind::AP && penetrated && plate < g.caliberMm * 0.08 &&
        (hit.face == HitFace::Superstructure || !citadel || s.cls->size() == SizeClass::Small)) {
        // Thin plating does not arm the base fuse: the shell passes straight through.
        overpen = w.rng().chance(0.55);
        if (overpen) effect *= 0.2;
    }
    // A shell defeated by real armour bursts outside it: scars and splinters, little more.
    if (!penetrated) effect *= plate >= 75.0 ? 0.05 : (sd.kind == ShellKind::AP ? 0.15 : 0.45);
    bool inside = penetrated && !overpen;

    s.dmg.hitsTaken++;
    // Structure: size, the class's construction (hull_strength: armour decks, subdivision) and where the
    // shell burst - wrecking the upperworks of a battleship does little to keep her afloat.
    // A shell through the thin side above the belt meets the armoured deck before it gets near the
    // machinery, a magazine or the buoyancy below it.
    bool belowArmour = inside && (hit.face != HitFace::Side || beltZone || c.armor.deck < 50.0);
    double upper = hit.face == HitFace::Superstructure ? (s.displacement() > 20000.0 ? 0.2 : 0.5) : 1.0;
    if (inside && !belowArmour) upper = std::min(upper, 0.2);
    s.dmg.hullIntegrity -= effect / (std::pow(s.displacement(), 0.9) * 0.06 * std::sqrt(std::max(0.3, c.hullStrength))) * upper;
    s.dmg.crewLoss = std::min(1.0, s.dmg.crewLoss + effect / (c.crew * 0.6) * 0.35 * (high ? 1.6 : 1.0));

    double radius = 3.0 + std::sqrt(std::max(0.0, effect)) * 2.2;
    double severity = clamp(effect / 6.0, 0.15, 1.2);
    if (hit.mount >= 0) {
        auto& m = s.mounts[size_t(hit.mount)];
        systemHit(w, s, m.health, penetrated ? 1.0 : (w.rng().chance(0.3) ? 0.6 : 0.15), "system.mount");
    }
    damageNear(w, s, hit.local, radius, severity * (inside ? 1.0 : 0.4), inside, high, belowArmour);

    double fireChance = (sd.kind == ShellKind::AP ? 0.12 : 0.32) * (high ? 1.4 : 1.0) * (inside ? 1.0 : 0.4);
    if (w.rng().chance(fireChance)) startFire(w, s, f, 0.15 + 0.1 * severity);

    if (hit.belowWater || (hit.face == HitFace::Side && hit.local.z < 0.8 && penetrated)) {
        double area = sqr(g.caliberMm / 1000.0) * (penetrated ? 3.0 : 1.0) * (sd.diving ? 2.0 : 1.0);
        s.dmg.floodRate += 0.6 * area * std::sqrt(2.0 * kGravity * 2.5) * 1.025;
        s.dmg.list += (hit.local.y >= 0 ? 1.0 : -1.0) * area * 2.0;
    }
    if (belowArmour && nearMagazine && hit.local.z < c.model.freeboardMid && g.caliberMm >= 120) {
        double p = 0.004 + 0.002 * sd.bursterKg;
        if (s.cls->size() == SizeClass::Small) p *= 1.5;
        if (w.rng().chance(p)) magazineExplosion(w, s);
    }
    if (hit.mount >= 0 && penetrated && g.caliberMm >= 150 && w.rng().chance(0.012)) magazineExplosion(w, s);
    if (s.dmg.sunkBy == kNoEntity || w.rng().chance(0.3)) s.dmg.sunkBy = sh.firer;

    Event e;
    e.type = EventType::ShellHit;
    e.time = w.time();
    e.a = sh.firer;
    e.b = s.id;
    e.side = sh.side;
    e.pos = {s.localToWorld(hit.local.x, hit.local.y), hit.local.z};
    e.value = g.caliberMm;
    e.value2 = penetrated ? 1.0 : 0.0;
    e.text = overpen ? "overpen" : (penetrated ? "pen" : "nopen");
    w.emit(e);
}

void nearMiss(World& w, Ship& s, const Shell& sh, double dist) {
    // Splinters from near misses riddle unarmoured ships (destroyers, superstructures).
    double effect = sh.shell->bursterKg * (1.0 - dist / 40.0);
    if (effect <= 0) return;
    if (s.cls->armor.belt > 50) effect *= 0.3;
    s.dmg.crewLoss = std::min(1.0, s.dmg.crewLoss + effect / (s.cls->crew * 0.6) * 0.1);
    if (w.rng().chance(0.1 * effect)) systemHit(w, s, s.sensors.empty() ? s.dmg.searchlights : s.sensors[0].health, 0.3, "system.sensor");
    if (s.cls->size() == SizeClass::Small && w.rng().chance(0.05 * effect)) s.dmg.floodRate += 0.02;
}

void torpedoHit(World& w, Ship& s, const Torpedo& t, const Vec2& local) {
    const auto& c = *s.cls;
    double kg = t.def->warheadKg;
    double prot = c.armor.torpedoProtection;
    double flood = kg * 4.2 * (1.0 - prot);
    s.dmg.flooding += flood;
    s.dmg.floodRate += kg * 0.004 * (1.0 - prot);
    s.dmg.hullIntegrity -= kg / (std::pow(s.displacement(), 0.85) * 0.9 * std::sqrt(std::max(0.3, c.hullStrength))) * (1.0 - 0.5 * prot);
    s.dmg.list += (local.y >= 0 ? 1.0 : -1.0) * flood / s.displacement() * 25.0;
    s.dmg.crewLoss = std::min(1.0, s.dmg.crewLoss + kg / (c.crew * 0.6) * 0.03);
    s.dmg.torpedoHitsTaken++;
    if (s.displacement() < 3500 && kg > 200 && w.rng().chance(0.6)) s.dmg.brokenBack = true;
    Vec3 p{local.x, local.y, -2.0};
    damageNear(w, s, p, 10.0 + kg / 40.0, 1.0, true, false, true);
    double f = fOf(s, local.x);
    if (f > 0.35 && f < 0.65) {
        double share = machineryShare(c) * (1.0 - 0.5 * prot);  // the side protection system absorbs much of it
        systemHit(w, s, s.dmg.propulsion, 0.5 * share, "system.propulsion");
        systemHit(w, s, s.dmg.power, 0.45 * share, "system.power");
    }
    if (f > 0.85) systemHit(w, s, s.dmg.steering, 0.8, "system.steering");
    if (w.rng().chance(0.3)) startFire(w, s, f, 0.4);
    // Magazine detonations from torpedo hits under turrets.
    for (auto& m : s.mounts)
        if (m.def->gun && m.def->gun->caliberMm >= 150 && std::fabs(m.def->x - local.x) < 0.04 * c.length && w.rng().chance(0.06))
            magazineExplosion(w, s);
    s.dmg.sunkBy = t.firer;
}

void collision(World& w, Ship& a, Ship& b, double relSpeed) {
    auto hurt = [&](Ship& s, const Ship& o) {
        double e = relSpeed * relSpeed * o.displacement() / (s.displacement() + o.displacement()) * 0.02;
        s.dmg.hullIntegrity -= e / std::pow(s.displacement(), 0.9) * 0.5;
        s.dmg.floodRate += e * 0.002;
    };
    hurt(a, b);
    hurt(b, a);
    Event ev;
    ev.type = EventType::Collision;
    ev.time = w.time();
    ev.a = a.id;
    ev.b = b.id;
    ev.side = a.side;
    ev.pos = {(a.pos + b.pos) * 0.5, 0.0};
    w.emit(ev);
}

void grounding(World& w, Ship& s) {
    s.dmg.hullIntegrity -= 0.1 * std::min(1.0, s.speed / 10.0);
    s.dmg.floodRate += 0.05 * s.speed;
    s.dmg.propulsion = std::max(0.0, s.dmg.propulsion - 0.2);
    Event e;
    e.type = EventType::ShipGrounded;
    e.time = w.time();
    e.a = s.id;
    e.side = s.side;
    e.pos = {s.pos, 0.0};
    w.emit(e);
}

void update(World& w, Ship& s, double dt) {
    if (s.dmg.sunk) return;
    auto& d = s.dmg;
    const auto& c = *s.cls;
    double dc = crewRate(s) * (d.sinking ? 0.0 : 1.0);

    // Flooding: inflow, damage-control shoring and pumps.
    d.flooding += d.floodRate * dt;
    d.floodRate *= std::exp(-dt * 0.004 * dc);
    d.flooding = std::max(0.0, d.flooding - s.displacement() * 0.000015 * dc * dt);
    d.list *= std::exp(-dt * 0.0015 * dc);  // counter-flooding

    // Fires grow when not fought, spread, and damage nearby systems. Index-based: spreading fires and
    // secondary damage may append to d.fires while we iterate.
    std::vector<Fire> spread;
    const size_t fireCount = d.fires.size();
    // Capital ships: more fire parties, a structure that suffers relatively less from each fire, and
    // magazines deep behind the armour (cruisers and smaller ships are unaffected).
    double big = s.displacement() / 15000.0;
    double partyFactor = std::max(1.0, std::pow(big, 0.7));
    double structureFactor = std::min(1.0, std::pow(1.0 / std::max(big, 1e-3), 0.8));
    double magazineFactor = c.armor.belt >= 200.0 ? 0.3 : 1.0;
    for (size_t i = 0; i < fireCount && i < d.fires.size(); ++i) {
        double growth = 0.006 * (c.aviationFuel ? 1.3 : 1.0);
        double fight = 0.008 * dc * partyFactor * (1.0 - 0.5 * d.fires[i].intensity);
        d.fires[i].intensity = clamp(d.fires[i].intensity + (growth - fight) * dt * w.rng().uniform(0.5, 1.5), 0.0, 1.0);
        const Fire fire = d.fires[i];
        if (fire.intensity > 0.6 && w.rng().chance(0.002 * dt))
            spread.push_back({clamp(fire.f + w.rng().uniform(-0.08, 0.08), 0.02, 0.98), 0.2});
        d.crewLoss = std::min(1.0, d.crewLoss + fire.intensity * 0.00003 * dt);
        if (fire.intensity > 0.5) d.power = std::max(0.0, d.power - (fire.intensity - 0.5) * 0.002 * dt * machineryShare(c));  // burning cables
        d.hullIntegrity -= fire.intensity * 0.00002 * dt * structureFactor;
        if (fire.intensity > 0.5 && w.rng().chance(0.004 * dt)) {
            double lx = (0.5 - fire.f) * c.length;
            damageNear(w, s, {lx, 0, c.model.freeboardMid}, 8.0, 0.25, true, true, true);
        }
        // Fire reaching a magazine (flooding the magazines is the usual counter-measure).
        if (fire.intensity > 0.85 && w.rng().chance(0.00012 * dt * magazineFactor)) {
            for (auto& m : s.mounts)
                if (m.def->gun && m.def->gun->caliberMm >= 150 && std::fabs((0.5 - fire.f) * c.length - m.def->x) < 0.05 * c.length) {
                    if (dc > 0.5) {
                        m.ammo.clear();  // magazine flooded
                        report(w, s, "report.magazine_flooded");
                    } else {
                        magazineExplosion(w, s);
                    }
                    break;
                }
        }
    }
    // A fire spreading into a burning section feeds that fire instead of starting another one.
    for (auto& sp : spread) {
        auto near = std::find_if(d.fires.begin(), d.fires.end(), [&](const Fire& f) { return std::fabs(f.f - sp.f) < 0.06; });
        if (near != d.fires.end()) near->intensity = std::min(1.0, near->intensity + sp.intensity * 0.6);
        else d.fires.push_back(sp);
    }
    size_t before = d.fires.size();
    d.fires.erase(std::remove_if(d.fires.begin(), d.fires.end(), [](const Fire& f) { return f.intensity < 0.02; }), d.fires.end());
    if (before > 0 && d.fires.empty()) {
        Event e;
        e.type = EventType::FireOut;
        e.time = w.time();
        e.a = s.id;
        e.side = s.side;
        w.emit(e);
    }

    // Slow temporary repairs of systems by the damage-control parties.
    if (!d.sinking && w.rng().chance(0.01 * dt * dc)) {
        auto repair = [&](double& h) {
            if (h > 0.05 && h < 0.6) h = std::min(0.6, h + 0.15);
        };
        repair(d.propulsion);
        repair(d.steering);
        repair(d.searchlights);
        repair(d.power);
        for (auto& m : s.mounts) repair(m.health);
        for (auto& sen : s.sensors) repair(sen.health);
        for (auto& dir : s.directors) repair(dir.health);
        if (d.rudderJammed && d.steering > 0.4) d.rudderJammed = false;
    }

    // Sinking.
    double reserve = s.displacement() * c.reserveBuoyancy;
    if (!d.sinking) {
        const char* cause = nullptr;
        double rate = 0;
        if (d.magazineExploded) { cause = "report.sunk_magazine"; rate = 1.0 / 25.0; }
        else if (d.brokenBack) { cause = "report.sunk_broken"; rate = 1.0 / 80.0; }
        else if (d.hullIntegrity <= 0.0) { cause = "report.sunk_structural"; rate = 1.0 / 150.0; }
        else if (d.flooding >= reserve) { cause = "report.sunk_flooding"; rate = 1.0 / 240.0; }
        else if (std::fabs(d.list) > 30.0) { cause = "report.capsized"; rate = 1.0 / 60.0; d.capsized = true; }
        if (cause) {
            d.sinking = true;
            d.sinkRate = rate;
            d.fires.clear();
            Event e;
            e.type = EventType::ShipCrippled;
            e.time = w.time();
            e.a = s.id;
            e.side = s.side;
            e.text = cause;
            e.args = {s.name};
            w.emit(e);
        }
    }
    if (d.sinking) {
        d.sinkProgress = std::min(1.0, d.sinkProgress + d.sinkRate * dt);
        d.list += (d.list >= 0 ? 1.0 : -1.0) * dt * (d.capsized ? 2.0 : 0.15);
        s.speed *= std::exp(-dt * 0.05);
        if (d.sinkProgress >= 1.0) {
            d.sunk = true;
            if (Ship* k = w.ship(d.sunkBy)) k->stats.tonnageSunk += s.displacement();
            Event e;
            e.type = EventType::ShipSunk;
            e.time = w.time();
            e.a = s.id;
            e.b = d.sunkBy;
            e.side = s.side;
            e.pos = {s.pos, 0.0};
            e.args = {s.name};
            w.emit(e);
        }
    }
}

}  // namespace it::sim::damage
