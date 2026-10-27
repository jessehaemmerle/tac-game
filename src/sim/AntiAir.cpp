#include "sim/AntiAir.h"

#include <algorithm>

#include "sim/World.h"

namespace it::sim::aa {
namespace {

/// Quality of a navy's high-angle fire control (US Mk 37 and Mk 33 directors were the best of the war's
/// first half; British HACS and the Japanese Type 94 predicted poorly against fast, low targets).
double fireControl(Nation n) {
    switch (n) {
        case Nation::USN: return 1.0;
        case Nation::RN:
        case Nation::RAN: return 0.72;
        case Nation::IJN: return 0.65;
        case Nation::KM: return 0.8;
        case Nation::RM: return 0.6;
        default: return 0.7;
    }
}

int poisson(Rng& rng, double mean) {
    if (mean <= 0.0) return 0;
    if (mean < 0.25) return rng.chance(mean) ? 1 : 0;
    double L = std::exp(-mean), p = 1.0;
    int k = 0;
    do {
        ++k;
        p *= rng.uniform();
    } while (p > L && k < 20);
    return k - 1;
}

struct Candidate {
    Aircraft* a = nullptr;
    double slant = 0;
    double seen = 1;
    bool attacking = false;
};

}  // namespace

void step(World& w, Ship& s, double dt) {
    if (!s.operational() || s.aa.empty()) return;
    double now = w.time();
    if (s.orders.weapons == WeaponsStatus::Hold) {
        for (auto& b : s.aa) b.target = kNoEntity;
        return;
    }
    const auto& wx = w.env().weather();
    bool night = w.env().ambientLight() < 0.3;
    bool fcRadar = false;
    for (const auto& d : s.directors)
        if (d.operational() && d.radarFitted && d.def && d.def->radarDef && d.def->radarDef->rangeAir > 0) fcRadar = true;
    ContactPicture& pic = w.picture(s.side);
    std::vector<Candidate> cands;
    for (auto& a : w.planes()) {
        if (!a.flying() || a.side == s.side || a.side == Side::Neutral) continue;
        Vec3 rel = a.pos - Vec3{s.pos, 0.0};
        double slant = length(rel);
        if (slant > 11000.0) continue;
        const Flight* f = w.flight(a.flight);
        const AirTrack* t = f ? pic.airByTruth(f->entity) : nullptr;
        // Gunners engage what the ship knows of: the raid on the plot, or attackers close aboard.
        if (!t && slant > 3000.0) continue;
        bool attacking = a.run == Aircraft::Run::Run || a.run == Aircraft::Run::Dive || (a.run == Aircraft::Run::Approach && slant < 6000.0);
        if (s.orders.weapons == WeaponsStatus::Tight && !attacking && !(t && (t->attacking || t->roleKnown))) continue;
        double seen = 1.0;
        if (night) {
            seen = slant < 2000.0 ? 0.35 : 0.12;  // barrage on the engine noise and exhaust flames
            for (const auto& fl : w.flares())
                if (fl.alive && length(fl.pos - a.pos) < 2500.0) seen = std::max(seen, 0.55);
        }
        if (a.pos.z > wx.cloudBaseM + 100.0) seen *= 1.0 - 0.8 * wx.cloudCover;
        cands.push_back({&a, slant, seen, attacking});
    }
    if (cands.empty()) {
        for (auto& b : s.aa) b.target = kNoEntity;
        return;
    }
    // With enemy ships about, the dual-purpose guns are kept for them: only an attack on the ship
    // itself turns them on aircraft (the floatplanes over Guadalcanal drew the light guns, not the 5-inch).
    bool surfaceThreat = false;
    for (const auto& t : pic.tracks())
        if (t.hostile && !t.lost && now - t.lastSeen < 120.0 && distance(t.predict(now), s.pos) < 20000.0) {
            surfaceThreat = true;
            break;
        }
    double crew = crewFactor(s.crew) * (0.3 + 0.7 * s.readiness) * (1.0 - 0.7 * s.dmg.crewLoss);
    double motion = 1.0 - 0.3 * clamp(std::fabs(s.turnRate) / 3.0, 0.0, 1.0);
    double suppressed = now < s.aaSuppressedUntil ? 0.5 : 1.0;
    for (size_t bi = 0; bi < s.aa.size(); ++bi) {
        AaBatteryState& b = s.aa[bi];
        if (!b.operational()) continue;
        bool heavy = b.mount >= 0;
        MountState* m = heavy ? &s.mounts[size_t(b.mount)] : nullptr;
        double range, ceiling, killRounds, rof;
        int barrels;
        bool surfaceBusy = false;
        if (heavy) {
            if (!m->operational() || s.dmg.power < 0.3 || m->totalAmmo() <= 0) continue;
            const GunDef* g = m->def->gun;
            range = g->aaRangeM;
            ceiling = range * 0.9;
            killRounds = g->aaKillRounds;
            rof = g->rofRpm;
            barrels = m->def->barrels;
            // A dual-purpose battery with a surface target keeps shooting at it, and turns on aircraft
            // only when they attack the ship (a floatplane dropping flares does not draw the 5-inch guns).
            surfaceBusy = now - m->lastSurface < 8.0;
            for (const auto& d : s.directors)
                if (d.def && d.def->id == m->def->director && d.targetTrack != 0 && d.tracking) surfaceBusy = true;
        } else {
            range = b.def->rangeM;
            ceiling = b.def->ceilingM;
            killRounds = b.def->killRounds;
            rof = b.def->rofRpm;
            barrels = b.def->barrels;
        }
        // Target: attackers first, then the nearest; keep the present target while it stays in reach.
        Candidate* best = nullptr;
        double bestScore = 1e18;
        for (auto& c : cands) {
            if (c.slant > range || c.a->pos.z > ceiling) continue;
            double rel = s.relativeBearing(c.a->pos.xy());
            if (heavy && !inArc(rel, m->def->arcMin, m->def->arcMax, m->def->mirrored)) continue;
            // Light AA on one beam is masked by the superstructure for targets on the other.
            if (!heavy && std::fabs(b.local.y) > 0.5 && (b.local.y > 0) != (rel > 0) && std::fabs(rel) > 25.0 && std::fabs(rel) < 155.0)
                continue;
            if (heavy && (surfaceBusy || surfaceThreat) && !(c.attacking && c.slant < 6000.0)) continue;
            // At night the heavy guns hold their fire against snoopers and flare droppers: the flashes
            // would give the ship away, and the rounds are wanted for the surface action.
            if (heavy && night && !c.attacking) continue;
            double score = c.slant * (c.attacking ? 0.4 : 1.0) - (c.a->id == b.target ? 800.0 : 0.0);
            if (score < bestScore) {
                bestScore = score;
                best = &c;
            }
        }
        if (!best) {
            b.target = kNoEntity;
            continue;
        }
        Aircraft& a = *best->a;
        b.target = a.id;
        Vec3 gun{s.localToWorld(b.local.x, b.local.y), b.local.z};
        Vec3 los = a.pos - gun;
        b.train = s.relativeBearing(a.pos.xy());
        b.elevation = std::atan2(los.z, std::max(1.0, length(los.xy()))) * kRadToDeg;
        if (heavy) {
            m->train = b.train;
            m->elevation = std::min(b.elevation, m->def->gun->maxElevationDeg);
            m->aaUntil = now + 3.0;
        }
        b.firingUntil = now + (heavy ? 1.5 : 0.6);
        // Hits: rounds fired this tick times the probability that one of them destroys (or damages) the aircraft.
        double rounds = barrels * rof / 60.0 * dt * b.health;
        double fr = 1.0 - smoothstep(0.35 * range, range, best->slant);
        double fh = heavy ? 0.4 + 0.6 * smoothstep(30.0, 300.0, a.pos.z) : 1.0;
        Vec2 vrel = bearingVector(a.heading) * a.speed - s.velocity();
        Vec2 dir = normalize(a.pos.xy() - s.pos);
        double crossing = std::fabs(cross(dir, vrel));
        double fc = 1.0 / (1.0 + 0.5 * sqr(crossing / 120.0));
        double director = heavy ? fireControl(s.nation) * (fcRadar ? 1.15 : 1.0) : 1.0;
        double seen = best->seen;
        if (heavy && night && fcRadar) seen = std::max(seen, 0.45);
        double p = fr * fh * fc * director * crew * motion * suppressed * seen;
        double dmgPerHit = heavy ? 0.6 : 0.5;
        int hits = poisson(w.rng(), rounds * p / (killRounds * dmgPerHit));
        for (int k = 0; k < hits && a.flying(); ++k) w.aircraftHit(a, dmgPerHit * w.rng().uniform(0.6, 1.4), s.id);
        if (heavy && now >= b.nextRound) {
            b.nextRound = now + 60.0 / std::max(1.0, rof);
            for (int k = 0; k < barrels; ++k) {
                for (ShellKind kind : {ShellKind::HE, ShellKind::Common, ShellKind::AP}) {
                    auto it = m->ammo.find(kind);
                    if (it != m->ammo.end() && it->second > 0) {
                        it->second--;
                        break;
                    }
                }
            }
            m->shotsFired += barrels;
            b.roundsFired += barrels;
            Event e;
            e.type = EventType::AaFire;
            e.time = now;
            e.a = s.id;
            e.b = a.id;
            e.side = s.side;
            e.pos = gun;
            e.value = m->def->gun->caliberMm;
            e.value2 = double(bi);
            w.emit(e);
        } else if (!heavy) {
            b.roundsFired += int(rounds + w.rng().uniform());
        }
    }
}

}  // namespace it::sim::aa
