#include "sim/FireControl.h"

#include "sim/Sensors.h"
#include "sim/World.h"

namespace it::sim::firecontrol {
namespace {

const SensorDef* lookoutOptics(const Ship& s) {
    for (auto& ss : s.sensors)
        if (ss.def && ss.def->def && ss.def->def->kind == SensorKind::Optical) return ss.def->def;
    return nullptr;
}

double crewRate(const Ship& s) { return crewFactor(s.crew) * (1.0 - 0.6 * s.dmg.crewLoss); }

/// Move `cur` toward `target` (deg) by at most rate*dt, staying inside the mount's arc.
double driveAngle(double cur, double target, double rate, double dt) {
    double diff = angleDiff(cur, target);
    double step = clamp(diff, -rate * dt, rate * dt);
    return wrap180(cur + step);
}

struct Aim {
    double bearing = 0, elevation = 0, range = 0, tof = 0;
    Vec2 point;
};

/// Is the line of fire fouled by a friendly ship? Gunnery officers do not fire through their own line.
bool lineFouled(const World& w, const Ship& s, const Vec2& from, const Vec2& to) {
    Vec2 d = to - from;
    double len = length(d);
    if (len < 1.0) return false;
    Vec2 dir = d / len;
    for (auto& op : w.ships()) {
        const Ship& o = *op;
        if (&o == &s || o.side != s.side || !o.alive()) continue;
        Vec2 rel = o.pos - from;
        double along = dot(rel, dir);
        if (along < 0.0 || along > len * 0.92) continue;
        double across = std::fabs(cross(dir, rel));
        // Clearance grows with distance: shells pass high over far ships, low over near ones.
        double clear = o.cls->length * 0.5 * std::fabs(cross(dir, o.forward())) + o.cls->beam + 120.0;
        if (across < clear && along < 9000.0) return true;
    }
    return false;
}

bool computeAim(const World& w, const Ship& s, const DirectorState& d, const MountState& m, Aim& out, bool starshell) {
    const GunDef* gun = m.def->gun;
    if (!gun) return false;
    Vec2 mountPos = s.localToWorld(m.def->x, m.def->y);
    double t = w.time();
    double tof = 0;
    Vec2 aim;
    Vec2 own = s.velocity();
    for (int it = 0; it < 3; ++it) {
        Vec2 p = predictedTarget(d, t + tof);
        Vec2 los = normalize(p - mountPos);
        Vec2 right{los.y, -los.x};
        p += los * d.rangeCorrection + right * d.deflectionCorrection;
        if (starshell) p += los * 700.0;  // burst beyond the target to silhouette it
        aim = p - own * tof;               // shell inherits own ship's velocity
        double range = distance(mountPos, aim);
        double elev, ttof;
        double lookup = starshell ? range + 1500.0 : range;
        if (!gun->elevationForRange(lookup, elev, ttof)) return false;
        out.range = range;
        out.elevation = elev;
        tof = ttof;
    }
    out.tof = tof;
    out.point = aim;
    out.bearing = bearingTo(mountPos, aim);
    return true;
}

/// Error of the salvo's mean point of impact: fire-control solution residuals, own roll and laying
/// errors that do not average out within a salvo. Shared by every shell of the salvo.
struct SalvoError {
    double range = 0, deflection = 0;
};

SalvoError salvoError(World& w, const Ship& s, const DirectorState& d, double range) {
    double crew = crewRate(s);
    double base = 0.006 + 0.010 * (1.0 - d.quality);              // fraction of range
    bool nightOptical = !d.radarRanging && w.env().ambientLight() < 0.3;
    if (nightOptical) base *= 1.8;  // night, optical ranging on a dark shape or a glare
    // Blind radar fire (1942 sets): the range is good, but the layers follow a pip on a scope and
    // the director cannot correct the line; the errors grow with the range.
    bool radarBlind = d.radarRanging && !d.opticalLayer;
    if (radarBlind) base *= 1.0 + 2.0 * smoothstep(2000.0, 14000.0, range);
    if (s.cls->size() == SizeClass::Small) base *= 1.35;            // lively destroyer platforms
    // A seaway: own roll and pitch the stabilisation cannot fully remove, spray over the director windows.
    base *= 1.0 + 0.25 * std::max(0.0, w.env().weather().seaState - 2.0);
    base /= std::max(0.45, crew);
    // Deflection errors grow at night: the director layer has only a silhouette or a searchlight beam to lay on.
    return {w.rng().normal() * base * range, w.rng().normal() * base * (nightOptical ? 0.65 : radarBlind ? 0.5 : 0.35) * range};
}

void fireMount(World& w, Ship& s, DirectorState& d, MountState& m, const Aim& aim, uint32_t salvoId, EntityId target, bool starshell,
               ShellKind kind, const SalvoError& se) {
    const GunDef* gun = m.def->gun;
    double crew = crewRate(s);
    double disp = gun->dispersion * (1.0 / std::max(0.4, crew)) * (1.0 + 0.6 * (1.0 - d.health)) * (s.dmg.power < 0.3 ? 2.0 : 1.0) *
                  (1.0 + 0.6 * std::min(1.0, std::fabs(s.turnRate) / 1.5)) * (1.0 + 0.4 * (1.0 - m.health));
    double sigmaR = disp * aim.range;
    Vec3 muzzle{s.localToWorld(m.def->x, m.def->y), m.def->z};
    for (int b = 0; b < m.def->barrels; ++b) {
        auto it = m.ammo.find(kind);
        if (it == m.ammo.end() || it->second <= 0) break;
        const ShellDef* sd = gun->shell(kind);
        if (!sd) break;
        it->second--;
        double r = std::max(200.0, aim.range + se.range + (starshell ? 1500.0 : 0.0) + w.rng().normal() * sigmaR);
        double elev = aim.elevation, tof;
        if (!gun->elevationForRange(r, elev, tof)) elev = aim.elevation;
        double az = aim.bearing + (se.deflection + w.rng().normal() * 0.3 * sigmaR) / std::max(500.0, aim.range) * kRadToDeg;
        double mv = sd->muzzleVelocity * (1.0 + w.rng().normal() * 0.003);
        Shell sh;
        sh.firer = s.id;
        sh.side = s.side;
        sh.gun = gun;
        sh.shell = sd;
        sh.pos = sh.prevPos = muzzle;
        sh.origin = muzzle.xy();
        Vec2 h = bearingVector(az) * (mv * std::cos(elev * kDegToRad));
        sh.vel = Vec3{h + s.velocity(), mv * std::sin(elev * kDegToRad)};
        sh.salvo = salvoId;
        sh.intendedTarget = target;
        if (starshell) sh.burstRange = aim.range;
        w.addShell(sh);
        if (Salvo* sv = w.salvo(salvoId)) sv->inFlight++;
        s.stats.shellsFired++;
    }
    // Without power the mounts are trained, elevated and loaded by hand in local control.
    double rof = gun->rofRpm * crew * (0.5 + 0.5 * m.health) * (0.25 + 0.75 * s.readiness) * (0.3 + 0.7 * s.dmg.power);
    // A rolling ship in heavy seas: the layers wait for the roll, loaders lose their footing.
    rof /= 1.0 + 0.1 * std::max(0.0, w.env().weather().seaState - 3.0);
    double reload = 60.0 / std::max(0.2, rof);
    if (gun->loadingElevationDeg >= 0)
        reload += 2.0 * std::fabs(aim.elevation - gun->loadingElevationDeg) / std::max(1.0, gun->elevationRateDps);
    m.reload = reload * w.rng().uniform(0.95, 1.08);
    m.shotsFired++;
    m.loaded = kind;
}

void updateDirector(World& w, Ship& s, size_t di, double dt) {
    DirectorState& d = s.directors[di];
    double now = w.time();
    auto clearTarget = [&] {
        d.targetTrack = 0;
        d.targetTruth = kNoEntity;
        d.tracking = false;
        d.trackTime = 0;
        d.quality = 0;
    };
    if (!d.operational() || d.targetTrack == 0) {
        d.tracking = false;
        d.quality *= std::exp(-dt / 20.0);
        return;
    }
    Track* tr = w.picture(s.side).find(d.targetTrack);
    Ship* tgt = tr ? w.truthOf(s.side, d.targetTrack) : nullptr;
    if (!tr || !tgt || !tgt->alive() || (tgt->dmg.sinking && tgt->dmg.sinkProgress > 0.5)) {
        clearTarget();
        return;
    }
    if (d.targetTruth != tgt->id) {
        d.targetTruth = tgt->id;
        d.tracking = false;
        d.trackTime = 0;
        d.rangeCorrection = d.deflectionCorrection = 0;
        d.salvosAtTarget = 0;
        d.straddled = false;
    }
    double r = distance(s.pos, tgt->pos);
    if (now - d.lastMeasure >= 1.0) {
        bool radarOk = false;
        if (d.def->radarDef && d.radarFitted && d.health > 0.5 && s.dmg.power > 0.4) {
            double rr = sensors::radarRange(w, s, *d.def->radarDef, d.def->height, *tgt);
            radarOk = r < rr * 0.9;
        }
        auto vf = sensors::visualRange(w, s, *tgt, lookoutOptics(s), d.def->height);
        bool opticalOk = r < vf.range * 0.85;
        if (radarOk || opticalOk) {
            double crew = crewRate(s) * (0.5 + 0.5 * d.health) * (0.5 + 0.5 * s.readiness);
            double rangeSig, brgSig;
            if (radarOk) {
                // Echoes of ships close inshore merge with the land return: range and bearing wander.
                double merge = d.def->radarDef->landClutter * (1.0 - smoothstep(800.0, 5000.0, w.terrain().distanceToLand(tgt->pos, 6000.0)));
                rangeSig = d.def->radarDef->rangeAccuracy * (1.0 + 6.0 * merge);
                brgSig = d.def->radarDef->bearingAccuracyDeg * (1.0 + 3.0 * merge);
            } else {
                double lightF = std::pow(clamp(vf.light, 0.01, 1.0), 0.38);
                rangeSig = r * r * 2.5e-6 / std::max(1.0, d.def->rangefinderBase) * (1.0 + 3.0 * (1.0 - lightF)) + 15.0;
                brgSig = 0.08;
            }
            if (opticalOk) brgSig = std::min(brgSig, 0.08);
            rangeSig /= std::max(0.4, crew);
            brgSig /= std::max(0.4, crew);
            double mr = r + w.rng().normal() * rangeSig;
            double mb = bearingTo(s.pos, tgt->pos) + w.rng().normal() * brgSig;
            Vec2 meas = s.pos + bearingVector(mb) * mr;
            double dtm = now - d.lastMeasure;
            if (!d.tracking || dtm > 20.0) {
                d.estPos = meas;
                d.estVel = tr->vel;
                d.trackTime = 0;
            } else {
                Vec2 pred = d.estPos + d.estVel * dtm;
                Vec2 res = meas - pred;
                double alpha = clamp(0.35 * crew, 0.12, 0.45);
                double beta = clamp(0.035 * crew, 0.008, 0.06);
                d.estPos = pred + res * alpha;
                d.estVel += res * (beta / dtm);
            }
            d.tracking = true;
            d.radarRanging = radarOk;
            d.opticalLayer = opticalOk;
            d.lastMeasure = now;
            d.rangeSigma = rangeSig;
        } else {
            d.tracking = false;
        }
    }
    if (d.tracking) d.trackTime += dt;
    double convTime = (!d.radarRanging && w.env().ambientLight() < 0.3) ? 90.0 : 45.0;
    double conv = 1.0 - std::exp(-d.trackTime / convTime);
    double target = d.tracking ? conv / (1.0 + d.rangeSigma / 250.0) : 0.0;
    d.quality += (target - d.quality) * std::min(1.0, dt / 3.0);

    // ---- weapons: drive mounts and fire
    bool allowed = s.orders.weapons == WeaponsStatus::Free || (s.orders.weapons == WeaponsStatus::Tight && tr->hostile);
    bool freshSolution = now - d.lastMeasure < 15.0 && d.trackTime > 4.0;
    const std::string& did = d.def->id;
    int bearing = 0, ready = 0;
    std::vector<std::pair<size_t, Aim>> aims;
    ShellKind wanted = ShellKind::AP;
    for (size_t mi = 0; mi < s.mounts.size(); ++mi) {
        MountState& m = s.mounts[mi];
        if (m.def->director != did || !m.operational() || !m.def->gun) continue;
        if (now < m.aaUntil) {
            m.bears = m.onTarget = false;  // engaging aircraft
            continue;
        }
        bool star = d.illuminate && m.ammo.count(ShellKind::Starshell) && m.ammo[ShellKind::Starshell] > 0;
        Aim aim;
        if (!computeAim(w, s, d, m, aim, star) || aim.range > m.def->gun->effectiveMaxRange() * 0.97) {
            m.bears = m.onTarget = false;
            continue;
        }
        double rel = wrap180(aim.bearing - s.heading);
        m.bears = inArc(rel, m.def->arcMin, m.def->arcMax, m.def->mirrored) &&
                  !lineFouled(w, s, s.localToWorld(m.def->x, m.def->y), aim.point);
        if (!m.bears) {
            m.onTarget = false;
            continue;
        }
        double rate = m.def->gun->traverseRateDps * (0.5 + 0.5 * m.health) * (0.25 + 0.75 * s.dmg.power);
        m.train = driveAngle(m.train, rel, rate + std::fabs(s.turnRate), dt);
        m.elevation += clamp(aim.elevation - m.elevation, -m.def->gun->elevationRateDps * dt, m.def->gun->elevationRateDps * dt);
        m.onTarget = std::fabs(angleDiff(m.train, rel)) < 0.5 && std::fabs(aim.elevation - m.elevation) < 0.3;
        if (m.reload > 0) m.reload -= dt;
        wanted = star ? ShellKind::Starshell : chooseShell(m, tr->size == SizeClass::Unknown ? tgt->cls->size() : tr->size);
        if (m.totalAmmo() <= 0) continue;
        ++bearing;
        if (m.reload <= 0 && m.onTarget) {
            ++ready;
            aims.push_back({mi, aim});
        }
    }
    if (!allowed || !freshSolution || bearing == 0 || aims.empty() || s.readiness < 0.6) return;
    double minInterval = d.illuminate ? 25.0 : 2.0;
    // Ladder / bracket: until a straddle is seen, wait for the fall of shot before the next salvo.
    if (!d.illuminate && !d.straddled) minInterval = std::max(minInterval, d.lastSalvoTof + 3.0);
    if (now - d.lastSalvo < minInterval) return;
    bool full = ready == bearing;
    bool partial = ready * 2 >= bearing && now - d.lastSalvo > 60.0 / 1.5;
    if (!full && !partial) return;
    Salvo sv;
    sv.id = w.nextId();
    sv.firer = s.id;
    sv.director = int(di);
    sv.target = tgt->id;
    w.salvos().push_back(sv);
    double maxCal = 0;
    SalvoError se = salvoError(w, s, d, aims.front().second.range);
    for (auto& [mi, aim] : aims) {
        MountState& m = s.mounts[mi];
        bool star = d.illuminate && m.ammo.count(ShellKind::Starshell) && m.ammo[ShellKind::Starshell] > 0;
        ShellKind kind = star ? ShellKind::Starshell : chooseShell(m, tr->size == SizeClass::Unknown ? tgt->cls->size() : tr->size);
        if (!m.ammo.count(kind) || m.ammo[kind] <= 0) {
            for (auto& [k, n] : m.ammo)
                if (n > 0 && k != ShellKind::Starshell) kind = k;
        }
        fireMount(w, s, d, m, aim, sv.id, tgt->id, star, kind, se);
        m.lastSurface = now;
        maxCal = std::max(maxCal, m.def->gun->caliberMm);
        Event e;
        e.type = EventType::GunFired;
        e.time = now;
        e.a = s.id;
        e.b = tgt->id;
        e.side = s.side;
        e.pos = {s.localToWorld(m.def->x, m.def->y), m.def->z};
        e.value = m.def->gun->caliberMm;
        e.value2 = double(mi);
        w.emit(e);
        if (star) break;  // one starshell at a time is enough
    }
    (void)wanted;
    d.lastSalvo = now;
    d.lastSalvoTof = aims.front().second.tof;
    d.salvoSeq = sv.id;
    d.salvosAtTarget++;
    s.stats.salvos++;
    s.lastFired = now;
    s.lastFiredCalibre = maxCal;
    sensors::gunFlash(w, s, maxCal);
}

}  // namespace

Vec2 predictedTarget(const DirectorState& d, double t) { return d.estPos + d.estVel * (t - d.lastMeasure); }

const DirectorState* solutionFor(const Ship& s, uint32_t track, double minTrackTime) {
    for (const auto& d : s.directors)
        if (track && d.targetTrack == track && d.tracking && d.trackTime >= minTrackTime) return &d;
    return nullptr;
}

ShellKind chooseShell(const MountState& m, SizeClass targetSize) {
    const GunDef* g = m.def->gun;
    bool big = targetSize == SizeClass::Large || targetSize == SizeClass::Medium;
    auto has = [&](ShellKind k) {
        auto it = m.ammo.find(k);
        return it != m.ammo.end() && it->second > 0;
    };
    if (big && g->caliberMm >= 150 && has(ShellKind::AP)) return ShellKind::AP;
    if (has(ShellKind::Common)) return ShellKind::Common;
    if (has(ShellKind::HE)) return ShellKind::HE;
    if (has(ShellKind::AP)) return ShellKind::AP;
    return g->shells.empty() ? ShellKind::AP : g->shells.front().kind;
}

void update(World& w, Ship& s, double dt) {
    if (!s.operational()) return;
    for (size_t di = 0; di < s.directors.size(); ++di) updateDirector(w, s, di, dt);
    // Mounts without a solution reload anyway and slowly return to a neutral training.
    for (auto& m : s.mounts)
        if (!m.bears && m.reload > 0) m.reload -= dt;
    // Torpedo reloads (IJN destroyers and cruisers carried reload torpedoes).
    double crew = crewRate(s);
    for (auto& t : s.torpedoMounts) {
        if (!t.operational() || t.loaded > 0 || t.reloads <= 0) continue;
        if (t.reloadTimer <= 0) t.reloadTimer = t.def->reloadTime;
        t.reloadTimer -= dt * crew;
        if (t.reloadTimer <= 0) {
            int n = std::min(t.def->tubes, t.reloads);
            t.loaded = n;
            t.reloads -= n;
            t.reloadTimer = 0;
            Event e;
            e.type = EventType::Report;
            e.time = w.time();
            e.a = s.id;
            e.side = s.side;
            e.text = "report.torpedo_reloaded";
            e.args = {s.name};
            w.emit(e);
        }
    }
}

void salvoLanded(World& w, Salvo& sv) {
    if (sv.spotted) return;
    sv.spotted = true;
    Ship* firer = w.ship(sv.firer);
    Ship* tgt = w.ship(sv.target);
    if (!firer || !firer->operational() || !tgt || sv.director >= int(firer->directors.size())) return;
    DirectorState& d = firer->directors[size_t(sv.director)];
    if (d.targetTruth != sv.target || sv.landed == 0) return;
    if (sv.overs > 0 && sv.shorts > 0) {
        Event e;
        e.type = EventType::Straddle;
        e.time = w.time();
        e.a = firer->id;
        e.b = tgt->id;
        e.side = firer->side;
        w.emit(e);
    }
    double crew = crewRate(*firer);
    double light = w.illuminationOn(*tgt);
    double range = distance(firer->pos, tgt->pos);
    // Splashes on the radar scope merge with the target pip and with each other at long range; without
    // a sight of the target there is no way to tell them apart.
    double blind = d.radarRanging && !d.opticalLayer ? smoothstep(4000.0, 16000.0, range) : 0.0;
    double pObserve = d.radarRanging ? 0.9 * (1.0 - 0.65 * blind) : clamp(std::pow(clamp(light, 0.0, 1.0), 0.4) * 1.6, 0.05, 1.0);
    if (sv.hits > 0) pObserve = std::max(pObserve, 0.8);
    // An own aircraft over the target spots the fall of shot (Chokai's floatplanes, Warspite's Swordfish).
    bool airSpot = w.aircraftSpotting(firer->side, tgt->id);
    if (airSpot) {
        pObserve = std::max(pObserve, 0.85);
        blind *= 0.4;
    }
    if (!w.rng().chance(pObserve * clamp(crew, 0.4, 1.0))) return;
    double meanR = sv.sumRangeErr / sv.landed, meanD = sv.sumDeflErr / sv.landed;
    double noise = (d.radarRanging ? 30.0 : 60.0) + (0.004 + 0.012 * blind) * range;
    double obsR = meanR + w.rng().normal() * noise, obsD = meanD + w.rng().normal() * noise * 0.5;
    if (sv.hits > 0) obsR *= 0.5;  // hits confirm the range
    if (sv.overs > 0 && sv.shorts > 0) d.straddled = true;
    if (std::fabs(obsR) > 600.0) d.straddled = false;  // lost the range again
    double gain = clamp(0.6 * crew, 0.3, 0.7);
    d.rangeCorrection -= gain * obsR;
    d.deflectionCorrection -= gain * obsD;
    d.rangeCorrection = clamp(d.rangeCorrection, -3000.0, 3000.0);
    d.deflectionCorrection = clamp(d.deflectionCorrection, -800.0, 800.0);
}

}  // namespace it::sim::firecontrol
