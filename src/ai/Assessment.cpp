#include "ai/Assessment.h"

#include <map>

namespace it::ai {
namespace {

double gunPower(const sim::ShipClassDef& c) {
    double p = 0;
    for (auto& m : c.mounts) {
        if (!m.gun || m.gun->shells.empty()) continue;
        double pen = clamp((m.gun->caliberMm - 70.0) / 133.0, 0.15, 1.2);  // 8in = 1.0, 5in = 0.43
        p += m.barrels * m.gun->rofRpm * m.gun->shells.front().massKg * pen;
    }
    return p;
}

double torpedoPower(const sim::ShipClassDef& c, bool night) {
    double p = 0;
    for (auto& t : c.torpedoMounts) {
        if (!t.torpedo) continue;
        double rel = 1.0 - t.torpedo->dudChance - t.torpedo->deepRunChance;
        p += (t.tubes + 0.5 * t.reloads) * (t.torpedo->warheadKg / 490.0) * 300.0 * rel;
    }
    return p * (night ? 1.3 : 0.7);
}

struct TypicalPower {
    double mean = 0, sigma = 0, length = 0;
};

/// Average power/length of enemy classes of a type (or size) — what an officer would assume.
TypicalPower typical(const SideView& v, sim::ShipType type, sim::SizeClass size, bool night) {
    double sum = 0, sum2 = 0, len = 0;
    int n = 0;
    sim::Side enemy = sim::enemyOf(v.side());
    for (auto& c : v.db().shipClasses()) {
        if (sim::defaultSide(c->nation) != enemy) continue;
        bool match = type != sim::ShipType::Unknown ? c->type == type
                     : size != sim::SizeClass::Unknown ? (c->size() == size && sim::isCombatant(c->type))
                                                       : sim::isCombatant(c->type);
        if (!match) continue;
        double p = classPower(*c, night);
        sum += p;
        sum2 += p * p;
        len += c->length;
        ++n;
    }
    if (n == 0) return {1500.0, 1200.0, 140.0};
    double mean = sum / n;
    return {mean, std::sqrt(std::max(0.0, sum2 / n - mean * mean)) + 0.15 * mean, len / n};
}

}  // namespace

double classPower(const sim::ShipClassDef& c, bool night) { return gunPower(c) + torpedoPower(c, night); }

double shipPower(const sim::Ship& s, bool night) {
    if (!s.operational()) return 0.0;
    double g = 0;
    for (auto& m : s.mounts) {
        if (!m.def->gun || m.def->gun->shells.empty() || !m.operational() || m.totalAmmo() <= 0) continue;
        double pen = clamp((m.def->gun->caliberMm - 70.0) / 133.0, 0.15, 1.2);
        g += m.def->barrels * m.def->gun->rofRpm * m.def->gun->shells.front().massKg * pen * m.health;
    }
    double t = 0;
    for (auto& tm : s.torpedoMounts) {
        if (!tm.def->torpedo || !tm.operational()) continue;
        double rel = 1.0 - tm.def->torpedo->dudChance - tm.def->torpedo->deepRunChance;
        t += (tm.loaded + 0.5 * tm.reloads) * (tm.def->torpedo->warheadKg / 490.0) * 300.0 * rel;
    }
    t *= night ? 1.3 : 0.7;
    double fc = 0;
    for (auto& d : s.directors) fc = std::max(fc, d.health);
    return (g * (0.4 + 0.6 * fc) + t) * (1.0 - 0.7 * s.dmg.crewLoss);
}

bool fireable(const sim::Track& t, const Doctrine& d, double now) {
    if (t.lost) return false;
    if (t.hostile || t.firedOnUs) return true;
    if (now - t.lastFlash < 60.0) return true;  // gun flashes from the darkness: that is the enemy
    // A force deep in enemy waters treats every warship it meets as hostile.
    if (d.assumeHostile >= 0.8 && t.level >= sim::ClassLevel::SizeKnown) return true;
    return !d.requireIdentification && t.level >= sim::ClassLevel::SizeKnown;
}

double hostileProbability(const sim::Track& t, const Doctrine& d) {
    if (t.hostile || t.firedOnUs) return 1.0;
    static const double byLevel[] = {0.35, 0.45, 0.6, 0.8, 1.0};
    return std::max(byLevel[int(t.level)], d.assumeHostile);
}

double Assessment::ratio(double caution) const {
    double e = enemyPower + caution * enemyPowerSigma;
    return power / std::max(100.0, e);
}

const ThreatEstimate* Assessment::primary() const {
    const ThreatEstimate* best = nullptr;
    for (auto& t : threats)
        if (!best || t.priority > best->priority) best = &t;
    return best;
}

Assessment assess(const SideView& v, const std::vector<sim::Ship*>& ships, const Doctrine& d) {
    Assessment a;
    a.ships = ships;
    a.night = v.night();
    a.secondsToDawn = v.secondsUntilDawn();
    double now = v.time();
    int alive = 0;
    double ammoSum = 0, ammoMax = 0;
    int tubes = 0, loaded = 0;
    a.maxSpeedKn = 99;
    for (auto* s : ships) {
        if (!s->operational()) continue;
        ++alive;
        a.center += s->pos;
        a.power += shipPower(*s, a.night);
        a.health = std::min(a.health, s->flotation());
        a.maxSpeedKn = std::min(a.maxSpeedKn, s->maxSpeedNow() / 0.514444);
        for (auto& m : s->mounts) {
            if (!m.def->gun || m.def->director != "main") continue;
            int total = 0;
            for (auto& [k, n] : m.def->ammo) total += n * m.def->barrels;
            ammoSum += m.totalAmmo();
            ammoMax += total;
        }
        tubes += s->torpedoesTotal();
        loaded += s->torpedoesReady();
        for (auto& tm : s->torpedoMounts) a.torpedoes += tm.loaded + tm.reloads;
    }
    if (alive == 0) return a;
    a.center = a.center / double(alive);
    a.ammo = ammoMax > 0 ? ammoSum / ammoMax : 0.0;
    a.torpedoFraction = tubes > 0 ? double(loaded) / tubes : 0.0;
    for (auto& h : v.recentHits())
        if (now - h.time < 120.0) {
            for (auto* s : ships)
                if (s->id == h.ship) ++a.recentHits;
        }

    int known = 0;
    double wsum = 0;
    for (auto& t : v.contacts()) {
        if (t.lost && now - t.lastSeen > 600.0) continue;
        if (!v.known(t, &ships)) continue;  // report has not reached us yet
        ThreatEstimate te;
        te.track = t.id;
        te.pos = t.predict(now);
        te.vel = t.vel;
        te.sigma = std::sqrt(sqr(t.posSigma) + sqr(t.velSigma * (now - t.lastUpdate)));
        te.type = t.typeGuess;
        te.size = t.size;
        te.hostile = t.hostile;
        te.fireable = fireable(t, d, now);
        te.age = now - t.lastSeen;
        te.distance = distance(a.center, te.pos);
        te.bearing = bearingTo(a.center, te.pos);
        TypicalPower tp;
        if (t.level >= sim::ClassLevel::ClassKnown && !t.classGuess.empty() && v.db().shipClass(t.classGuess)) {
            const auto* c = v.db().shipClass(t.classGuess);
            tp = {classPower(*c, a.night), 0.12 * classPower(*c, a.night), c->length};
        } else if (t.level >= sim::ClassLevel::TypeKnown) {
            tp = typical(v, t.typeGuess, t.size, a.night);
        } else {
            tp = typical(v, sim::ShipType::Unknown, t.size, a.night);
            tp.sigma *= 1.3;
        }
        te.power = tp.mean;
        te.powerSigma = tp.sigma;
        te.length = tp.length;
        for (auto& tm : v.db().shipClasses())
            if (tm->id == t.classGuess) te.torpedoThreat = torpedoPower(*tm, a.night);
        if (t.firedOnUs) a.detectedByEnemy = true;
        if (t.firedOnUs && now - t.lastFlash < 60.0) a.underFire = true;
        double recency = t.lost ? 0.3 : 1.0;
        te.hostileProb = hostileProbability(t, d);
        recency *= te.hostileProb;
        te.priority = d.priority(te.type) * std::sqrt(std::max(1.0, te.power)) * recency * (te.fireable ? 1.0 : 0.6) /
                      (1.0 + te.distance / 8000.0);
        a.enemyPower += te.power * recency;
        a.enemyPowerSigma += sqr(te.powerSigma);
        a.enemyCenter += te.pos * te.power;
        wsum += te.power;
        if (te.distance < a.nearest && !t.lost) {
            a.nearest = te.distance;
            a.nearestIdx = int(a.threats.size());
        }
        if (t.level >= sim::ClassLevel::TypeKnown && te.age < 90.0) ++known;
        a.threats.push_back(te);
    }
    a.enemyPowerSigma = std::sqrt(a.enemyPowerSigma);
    if (wsum > 0) a.enemyCenter = a.enemyCenter / wsum;
    a.knowledge = a.threats.empty() ? 0.0 : double(known) / a.threats.size();
    if (a.recentHits > 0) {
        a.detectedByEnemy = true;
        a.underFire = true;
    }
    for (auto* s : ships)
        if (s->operational() && s->dmg.totalFireIntensity() > 0.3) a.detectedByEnemy = true;  // burning: they see us
    return a;
}

}  // namespace it::ai
