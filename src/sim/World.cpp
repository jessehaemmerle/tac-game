#include "sim/World.h"

#include <algorithm>

#include "sim/Ballistics.h"
#include "sim/Damage.h"
#include "sim/FireControl.h"
#include "sim/Sensors.h"

namespace it::sim {

World::World(const Database& db, uint64_t seed) : db_(db), rng_(seed) {
    terrain_.createFlat(200000.0);
    for (auto& p : pictures_) p.setReporting(20.0, 60.0, 0.9, &rng_);
}

Ship& World::spawnShip(const ShipClassDef& cls, const std::string& name, Side side, const Vec2& pos, double heading,
                       double speedKn, CrewQuality crew) {
    auto s = std::make_unique<Ship>();
    s->id = nextId();
    s->name = name;
    s->side = side;
    s->crew = crew;
    s->initFromClass(cls);
    s->pos = pos;
    s->heading = wrap360(heading);
    s->speed = speedKn * 0.514444;
    s->orders.course = s->heading;
    s->orders.speedKn = speedKn;
    for (auto& sen : s->sensors) sen.nextSweep = rng_.uniform(0.0, 4.0);
    ships_.push_back(std::move(s));
    return *ships_.back();
}

Group& World::addGroup(const std::string& name, Side side, Formation f, double spacing) {
    Group g;
    g.id = int(groups_.size());
    g.name = name;
    g.side = side;
    g.formation = f;
    g.spacing = spacing;
    groups_.push_back(g);
    return groups_.back();
}

Group* World::group(int id) { return id >= 0 && id < int(groups_.size()) ? &groups_[size_t(id)] : nullptr; }
const Group* World::group(int id) const { return id >= 0 && id < int(groups_.size()) ? &groups_[size_t(id)] : nullptr; }

Ship* World::guideOf(const Group& g) {
    for (EntityId id : g.members)
        if (Ship* s = ship(id); s && s->operational()) return s;
    return nullptr;
}

Ship* World::ship(EntityId id) {
    for (auto& s : ships_)
        if (s->id == id) return s.get();
    return nullptr;
}
const Ship* World::ship(EntityId id) const {
    for (auto& s : ships_)
        if (s->id == id) return s.get();
    return nullptr;
}

void World::emit(Event e) {
    if (e.time == 0) e.time = time_;
    // Anything violent near a ship sounds general quarters.
    switch (e.type) {
        case EventType::ShellHit:
        case EventType::TorpedoHit:
        case EventType::TorpedoDud:
            if (Ship* s = ship(e.b)) s->alert(time_);
            break;
        case EventType::UnderFire:
        case EventType::TorpedoSighted:
            if (Ship* s = ship(e.a)) s->alert(time_);
            break;
        case EventType::StarshellBurst:
        case EventType::FlareDropped:
        case EventType::GunFired:
            for (auto& s : ships_)
                if (distanceSq(s->pos, e.pos.xy()) < sqr(12000.0)) s->alert(time_ + (s->side == e.side ? 0.0 : 20.0));
            break;
        case EventType::ContactNew:
            break;
        default: break;
    }
    events_.push_back(e);
    if (history_.size() < 200000 && e.type != EventType::ShellSplash && e.type != EventType::GunFired) history_.push_back(e);
}

std::vector<Event> World::takeEvents() {
    std::vector<Event> out;
    out.swap(events_);
    return out;
}

Shell& World::addShell(const Shell& s) {
    shells_.push_back(s);
    shells_.back().id = nextId();
    return shells_.back();
}

Salvo* World::salvo(uint32_t id) {
    for (auto it = salvos_.rbegin(); it != salvos_.rend(); ++it)
        if (it->id == id) return &*it;
    return nullptr;
}

Torpedo* World::torpedo(uint32_t id) {
    for (auto& t : torpedoes_)
        if (t.id == id) return &t;
    return nullptr;
}

void World::addFlare(const Flare& f) {
    flares_.push_back(f);
    flares_.back().id = nextId();
}

Ship* World::truthOf(Side side, uint32_t track) {
    const Track* t = picture(side).find(track);
    return t ? ship(picture(side).truthOf(*t)) : nullptr;
}

int World::aliveShips(Side s) const {
    int n = 0;
    for (auto& sh : ships_)
        if (sh->side == s && sh->operational()) ++n;
    return n;
}

double World::sideTonnageAfloat(Side s) const {
    double t = 0;
    for (auto& sh : ships_)
        if (sh->side == s && sh->operational()) t += sh->displacement();
    return t;
}

// ------------------------------------------------------------------------------------- light model
double World::illuminationOn(const Ship& target) const {
    double L = env_.ambientLight();
    Vec3 p{target.pos, 10.0};
    for (auto& f : flares_) {
        if (!f.alive) continue;
        double d = distance(f.pos, p);
        double fade = smoothstep(0.0, 4.0, f.life) * (1.0 - smoothstep(f.maxLife - 6.0, f.maxLife, f.life));
        L += f.intensity * 0.9 * fade / (1.0 + sqr(d / (f.aircraft ? 1400.0 : 950.0)));
    }
    for (auto& s : ships_) {
        if (!s->operational() || !s->orders.searchlight || s->searchlightTarget != target.id || s->dmg.searchlights < 0.3 ||
            s->dmg.power < 0.3)
            continue;
        double d = distance(s->pos, target.pos);
        L += 0.9 * (1.0 - smoothstep(4000.0, 9000.0, d));
    }
    L += 0.6 * std::min(1.0, target.dmg.totalFireIntensity());
    // Nearby burning ships cast light too.
    for (auto& s : ships_) {
        if (s.get() == &target || s->dmg.sunk) continue;
        double fi = s->dmg.totalFireIntensity();
        if (fi < 0.05) continue;
        L += 0.25 * std::min(1.0, fi) / (1.0 + sqr(distance(s->pos, target.pos) / 700.0));
    }
    return L;
}

double World::illuminationAt(const Vec3& p) const {
    double L = env_.ambientLight();
    for (auto& f : flares_)
        if (f.alive) L += f.intensity * 0.9 / (1.0 + sqr(distance(f.pos, p) / (f.aircraft ? 1400.0 : 950.0)));
    for (auto& s : ships_) {
        double fi = s->dmg.totalFireIntensity();
        if (fi > 0.05 && !s->dmg.sunk) L += 0.25 * std::min(1.0, fi) / (1.0 + sqr(distance(s->pos, p.xy()) / 700.0));
    }
    return L;
}

double World::backlight(const Vec2& observer, const Ship& target) const {
    Vec2 los = target.pos - observer;
    double dT = length(los);
    if (dT < 1.0) return 0.0;
    Vec2 dir = los / dT;
    double b = 0;
    auto consider = [&](const Vec2& src, double strength) {
        Vec2 v = src - observer;
        double dS = length(v);
        if (dS <= dT + 200.0) return;  // must be behind the target
        double ang = std::acos(clamp(dot(v / dS, dir), -1.0, 1.0)) * kRadToDeg;
        if (ang < 8.0) b += strength * (1.0 - ang / 8.0);
    };
    for (auto& f : flares_)
        if (f.alive) consider(f.pos.xy(), f.intensity);
    for (auto& s : ships_)
        if (s.get() != &target && !s->dmg.sunk) {
            double fi = s->dmg.totalFireIntensity();
            if (fi > 0.05) consider(s->pos, std::min(1.0, fi));
        }
    // A low moon behind the target silhouettes it.
    if (env_.moonAltitudeDeg() > 0 && env_.moonAltitudeDeg() < 20) {
        double rel = std::fabs(angleDiff(bearingOf(dir), env_.moonAzimuthDeg()));
        if (rel < 25.0) b += 0.6 * env_.moonIllumination() * (1.0 - rel / 25.0);
    }
    return clamp(b, 0.0, 1.0);
}

// ------------------------------------------------------------------------------------- commands
int World::fireTorpedoesOnCourse(Ship& s, double course, int count, double spreadDeg, int speedSetting, std::string* why) {
    if (!s.operational()) return 0;
    std::vector<std::pair<TorpedoMountState*, int>> plan;
    int available = 0;
    double rel = wrap180(course - s.heading);
    for (auto& tm : s.torpedoMounts) {
        if (!tm.operational() || tm.loaded <= 0 || !tm.def->torpedo) continue;
        // Gyro angling gives roughly +-20 deg of freedom beyond the mount's training arc.
        bool ok = inArc(rel, tm.def->arcMin - 20.0, tm.def->arcMax + 20.0, tm.def->mirrored);
        if (!ok) continue;
        int n = std::min(tm.loaded, count - available);
        if (n <= 0) break;
        plan.push_back({&tm, n});
        available += n;
    }
    if (available == 0) {
        if (why) *why = "order.torpedo_no_mount";
        return 0;
    }
    int idx = 0;
    for (auto& [tm, n] : plan) {
        const TorpedoDef* td = tm->def->torpedo;
        const auto& setting = td->settings[size_t(clamp(speedSetting, 0, int(td->settings.size()) - 1))];
        for (int k = 0; k < n; ++k, ++idx) {
            Torpedo t;
            t.id = nextId();
            t.firer = s.id;
            t.side = s.side;
            t.def = td;
            t.pos = s.localToWorld(tm->def->x, tm->def->y) + bearingVector(course) * 12.0;
            double offset = available > 1 ? spreadDeg * (double(idx) / (available - 1) - 0.5) : 0.0;
            t.baseHeading = t.heading = wrap360(course + offset + rng_.normal() * td->wanderFraction * 20.0);
            t.speed = setting.speedKn * 0.514444;
            t.maxRun = setting.range * rng_.uniform(0.97, 1.03);
            t.deep = rng_.chance(td->deepRunChance);
            t.erratic = rng_.chance(td->erraticChance);
            t.wanderPhase = rng_.uniform(0.0, kTwoPi);
            t.wanderAmp = td->wanderFraction * 30.0;
            torpedoes_.push_back(t);
        }
        tm->loaded -= n;
        tm->train = rel;
    }
    s.stats.torpedoesFired += available;
    Event e;
    e.type = EventType::TorpedoLaunched;
    e.time = time_;
    e.a = s.id;
    e.side = s.side;
    e.pos = {s.pos, 0.0};
    e.value = available;
    e.value2 = course;
    emit(e);
    return available;
}

int World::fireTorpedoes(Ship& s, uint32_t trackId, int count, double spreadDeg, int speedSetting, std::string* why) {
    const Track* tr = picture(s.side).find(trackId);
    if (!tr) {
        if (why) *why = "order.no_target";
        return 0;
    }
    const TorpedoDef* td = nullptr;
    for (auto& tm : s.torpedoMounts)
        if (tm.loaded > 0 && tm.operational() && tm.def->torpedo) td = tm.def->torpedo;
    if (!td) {
        if (why) *why = "order.torpedo_none";
        return 0;
    }
    const auto& setting = td->settings[size_t(clamp(speedSetting, 0, int(td->settings.size()) - 1))];
    double tSpeed = setting.speedKn * 0.514444;
    double tt;
    Vec2 aim;
    Vec2 targetNow = tr->predict(time_);
    if (!solveIntercept(s.pos, tSpeed, targetNow, tr->vel, tt, aim)) {
        if (why) *why = "order.torpedo_no_solution";
        return 0;
    }
    if (distance(s.pos, aim) > setting.range * 0.95) {
        if (why) *why = "order.torpedo_out_of_range";
        return 0;
    }
    return fireTorpedoesOnCourse(s, bearingTo(s.pos, aim), count, spreadDeg, speedSetting, why);
}

void World::setDirectorTarget(Ship& s, int director, uint32_t track) {
    if (director < 0 || director >= int(s.directors.size())) return;
    s.directors[size_t(director)].targetTrack = track;
}

void World::setSearchlight(Ship& s, bool on, uint32_t track) {
    bool wasOn = s.orders.searchlight;
    s.orders.searchlight = on && s.dmg.searchlights > 0.3 && s.cls->searchlights > 0;
    s.orders.searchlightTrack = track;
    s.searchlightTarget = kNoEntity;
    if (s.orders.searchlight) {
        if (Ship* t = truthOf(s.side, track)) {
            const Track* tr = picture(s.side).find(track);
            // The beam finds the target if our estimate is good enough and it is within reach.
            if (tr && distance(tr->predict(time_), t->pos) < 600.0 && distance(s.pos, t->pos) < 9000.0) s.searchlightTarget = t->id;
        }
        if (!wasOn) s.searchlightOnSince = time_;
        Event e;
        e.type = EventType::SearchlightOn;
        e.time = time_;
        e.a = s.id;
        e.b = s.searchlightTarget;
        e.side = s.side;
        emit(e);
    } else if (wasOn) {
        s.searchlightOnSince = -1;
        Event e;
        e.type = EventType::SearchlightOff;
        e.time = time_;
        e.a = s.id;
        e.side = s.side;
        emit(e);
    }
}

bool World::launchAircraft(Ship& s, AircraftMission::Kind kind, const Vec2& target) {
    if (!s.operational() || s.floatplanes <= 0 || s.dmg.aviation < 0.4) return false;
    s.floatplanes--;
    AircraftMission a;
    a.id = nextId();
    a.kind = kind;
    a.side = s.side;
    a.parent = s.id;
    a.pos = s.pos;
    a.target = target;
    a.heading = bearingTo(s.pos, target);
    a.stateTime = 0;
    a.flaresLeft = kind == AircraftMission::Kind::Flares ? 8 : 2;
    aircraft_.push_back(a);
    Event e;
    e.type = EventType::Report;
    e.time = time_;
    e.a = s.id;
    e.side = s.side;
    e.text = kind == AircraftMission::Kind::Flares ? "report.aircraft_flares" : "report.aircraft_recon";
    e.args = {s.name};
    emit(e);
    return true;
}

// ------------------------------------------------------------------------------------- stepping
void World::step() {
    const double dt = kStep;
    time_ += dt;
    ++tick_;
    env_.update(time_, dt);
    stepShips(dt);
    if (tick_ % 4 == 0) updateTrails();
    for (auto& s : ships_) {
        if (!s->operational()) continue;
        for (auto& sen : s->sensors) {
            if (time_ < sen.nextSweep) continue;
            const SensorDef* def = sen.def ? sen.def->def : nullptr;
            double period = def ? def->sweepPeriod : 5.0;
            sen.nextSweep = time_ + period * rng_.uniform(0.9, 1.1);
            bool powered = !(sen.def && sen.def->def && sen.def->def->kind == SensorKind::Radar) || s->dmg.power > 0.4;
            if (sen.working(time_) && powered) sensors::sweep(*this, *s, sen);
        }
        if (tick_ % 40 == uint64_t(s->id % 40)) sensors::torpedoWatch(*this, *s);
        if (s->orders.searchlight && tick_ % 20 == 0) setSearchlight(*s, true, s->orders.searchlightTrack);
        firecontrol::update(*this, *s, dt);
    }
    stepShells(dt);
    stepTorpedoes(dt);
    stepFlares(dt);
    stepAircraft(dt);
    for (auto& s : ships_) damage::update(*this, *s, dt);
    if (tick_ % 10 == 0) stepCollisions();
    if (tick_ % 20 == 0) {
        for (int si = 0; si < kSideCount; ++si) {
            for (uint32_t id : pictures_[size_t(si)].update(time_)) {
                Event e;
                e.type = EventType::ContactLost;
                e.time = time_;
                e.side = Side(si);
                e.track = id;
                emit(e);
            }
        }
        salvos_.erase(std::remove_if(salvos_.begin(), salvos_.end(), [&](const Salvo& s) { return s.spotted && s.landed >= s.inFlight; }),
                      salvos_.end());
    }
}

void World::stepShips(double dt) {
    for (auto& sp : ships_) {
        Ship& s = *sp;
        if (s.dmg.sunk) continue;
        const auto& c = *s.cls;
        if (s.alertedAt >= 0 && s.readiness < 1.0) {
            // Manning battle stations takes roughly 3 (elite) to 8 (green) minutes.
            double t = 480.0 / (crewFactor(s.crew) * crewFactor(s.crew));
            s.readiness = std::min(1.0, s.readiness + dt / t);
        }
        double vmax = s.maxSpeedNow();
        if (s.grounded) {
            s.speed = 0;
            s.turnRate = 0;
            continue;
        }
        // --- speed
        double target = clamp(s.orders.speedKn * 0.514444, -0.25 * vmax, vmax);
        double accel = c.accelKnPerMin * 0.514444 / 60.0 * std::max(0.1, s.dmg.propulsion);
        if (s.speed < target) {
            double a = accel * std::max(0.08, 1.0 - 0.85 * sqr(s.speed / std::max(1.0, c.maxSpeed())));
            s.speed = std::min(target, s.speed + a * dt);
        } else {
            s.speed = std::max(target, s.speed - accel * 1.6 * dt);
        }
        s.speed -= std::fabs(s.rudder) * s.speed * 0.006 * dt;  // speed lost in turns
        // --- rudder autopilot
        if (s.dmg.rudderJammed) {
            s.rudder = s.dmg.jammedRudder;
        } else if (!s.dmg.sinking) {
            double err = angleDiff(s.heading, s.orders.course);
            double desired = clamp((err - s.turnRate * 7.0) / 18.0, -1.0, 1.0);
            double rate = 0.12 * std::max(0.2, s.dmg.steering);
            s.rudder += clamp(desired - s.rudder, -rate * dt, rate * dt);
        }
        // --- turning
        double radius = c.tacticalDiameter * 0.5 * (1.0 + 0.6 * (1.0 - s.dmg.steering));
        double targetRate = (s.speed / radius) * kRadToDeg * s.rudder;
        double tau = 4.0 + c.length / 40.0;
        s.turnRate += (targetRate - s.turnRate) * std::min(1.0, dt / tau);
        s.heading = wrap360(s.heading + s.turnRate * dt);
        double targetHeel = -s.turnRate * s.speed * 0.25 + s.dmg.list;
        s.heel += (targetHeel - s.heel) * std::min(1.0, dt / 3.0);
        s.pos += s.forward() * (s.speed * dt);
        // --- grounding
        Vec2 bow = s.pos + s.forward() * (c.length * 0.45);
        if (s.speed > 0.3 && terrain_.shoal(bow, c.draft)) {
            s.grounded = true;
            damage::grounding(*this, s);
            s.speed = 0;
        }
    }
}

void World::updateTrails() {
    for (auto& g : groups_) {
        Ship* guide = guideOf(g);
        if (!guide) continue;
        if (g.trail.empty() || distance(g.trail.back(), guide->pos) > 40.0) g.trail.push_back(guide->pos);
        if (g.trail.size() > 600) g.trail.erase(g.trail.begin(), g.trail.begin() + 100);
    }
}

void World::stepShells(double dt) {
    for (auto& sh : shells_) {
        if (!sh.alive) continue;
        sh.prevPos = sh.pos;
        ballistics::ShellParams p{sh.shell->massKg, sh.gun->caliberMm / 1000.0, sh.shell->formFactor};
        ballistics::integrate(sh.pos, sh.vel, dt, p);
        sh.age += dt;
        Salvo* sv = salvo(sh.salvo);
        if (sh.burstRange > 0) {
            if (distance(sh.pos.xy(), sh.origin) >= sh.burstRange || (sh.vel.z < 0 && sh.pos.z < 400.0) || sh.pos.z < 50) {
                Flare f;
                f.pos = sh.pos;
                f.pos.z = std::max(150.0, sh.pos.z);
                f.intensity = 1.0;
                f.maxLife = 40.0;
                f.fallRate = 5.0;
                f.side = sh.side;
                addFlare(f);
                Event e;
                e.type = EventType::StarshellBurst;
                e.time = time_;
                e.a = sh.firer;
                e.side = sh.side;
                e.pos = f.pos;
                emit(e);
                sh.alive = false;
                if (sv) sv->landed++;
            }
            continue;
        }
        // Ship hits.
        bool hitSomething = false;
        for (auto& tp : ships_) {
            Ship& t = *tp;
            if (t.dmg.sunk || (t.id == sh.firer && sh.age < 2.0)) continue;
            if (distanceSq(t.pos, sh.pos.xy()) > sqr(t.cls->length * 0.6 + length(sh.vel) * dt)) continue;
            damage::HitInfo hit;
            if (damage::intersect(t, sh.prevPos, sh.pos, hit)) {
                damage::shellHit(*this, t, sh, hit);
                if (Ship* f = ship(sh.firer)) f->stats.hitsScored++;
                if (sv) {
                    sv->hits++;
                    sv->landed++;
                }
                sh.alive = false;
                hitSomething = true;
                break;
            }
        }
        if (hitSomething) continue;
        double ground = terrain_.heightAt(sh.pos.xy());
        if (sh.pos.z <= std::max(0.0, ground)) {
            double f = sh.prevPos.z / std::max(1e-6, sh.prevPos.z - sh.pos.z);
            Vec3 impact = lerp(sh.prevPos, sh.pos, clamp(f, 0.0, 1.0));
            impact.z = std::max(0.0, ground);
            sh.alive = false;
            Event e;
            e.type = EventType::ShellSplash;
            e.time = time_;
            e.a = sh.firer;
            e.side = sh.side;
            e.pos = impact;
            e.value = sh.gun->caliberMm;
            e.value2 = ground > 0 ? 1.0 : 0.0;
            emit(e);
            // Fall-of-shot bookkeeping for spotting, relative to the intended target.
            if (sv) {
                sv->landed++;
                if (const Ship* t = ship(sh.intendedTarget)) {
                    Vec2 los = normalize(t->pos - sh.origin);
                    Vec2 rel = impact.xy() - t->pos;
                    double along = dot(rel, los);
                    sv->sumRangeErr += along;
                    sv->sumDeflErr += cross(los, rel) * -1.0;
                    if (along > t->cls->beam) sv->overs++;
                    else if (along < -t->cls->beam) sv->shorts++;
                }
            }
            // Near misses, diving shells and the "under fire" alarm.
            for (auto& tp : ships_) {
                Ship& t = *tp;
                if (t.dmg.sunk || t.id == sh.firer) continue;
                double d = distance(t.pos, impact.xy());
                if (d > t.cls->length * 0.5 + 400.0) continue;
                Vec2 local = t.worldToLocal(impact.xy());
                double edge = std::max(std::fabs(local.x) - t.cls->length * 0.5, 0.0) + std::max(std::fabs(local.y) - t.cls->beam * 0.5, 0.0);
                if (edge < 40.0) damage::nearMiss(*this, t, sh, edge);
                if (sh.shell->diving && edge < 30.0 && rng_.chance(0.25)) {
                    damage::HitInfo hit;
                    hit.local = {clamp(local.x, -t.cls->length * 0.45, t.cls->length * 0.45), local.y >= 0 ? t.cls->beam * 0.5 : -t.cls->beam * 0.5, -3.5};
                    hit.face = damage::HitFace::Side;
                    hit.belowWater = true;
                    hit.obliquityDeg = 20;
                    damage::shellHit(*this, t, sh, hit);
                    if (sv) sv->hits++;
                }
                if (d < t.cls->length * 0.5 + 300.0 && sh.intendedTarget == t.id && rng_.chance(0.3)) {
                    Event u;
                    u.type = EventType::UnderFire;
                    u.time = time_;
                    u.a = t.id;
                    u.b = sh.firer;
                    u.side = t.side;
                    emit(u);
                }
            }
        }
        if (sv && sv->landed >= sv->inFlight) firecontrol::salvoLanded(*this, *sv);
    }
    shells_.erase(std::remove_if(shells_.begin(), shells_.end(), [](const Shell& s) { return !s.alive; }), shells_.end());
}

void World::stepTorpedoes(double dt) {
    for (auto& t : torpedoes_) {
        if (!t.alive) continue;
        double prevRun = t.run;
        if (t.erratic && t.run > 300.0)
            t.heading = wrap360(t.heading + 1.2 * dt);  // circular runner
        else
            t.heading = wrap360(t.baseHeading + std::sin(t.wanderPhase + t.run / 900.0) * t.wanderAmp);
        t.pos += bearingVector(t.heading) * (t.speed * dt);
        t.run += t.speed * dt;
        bool armed = t.run > 250.0;
        if (t.run >= t.maxRun) {
            t.alive = false;
        } else if (terrain_.heightAt(t.pos) > -2.0) {
            t.alive = false;
            Event e;
            e.type = EventType::Explosion;
            e.time = time_;
            e.side = t.side;
            e.pos = {t.pos, 0.0};
            e.value = t.def->warheadKg;
            emit(e);
        } else if (armed || prevRun > 0) {
            for (auto& sp : ships_) {
                Ship& s = *sp;
                if (s.dmg.sunk || (s.id == t.firer && t.run < 400.0)) continue;
                Vec2 l = s.worldToLocal(t.pos);
                double halfL = s.cls->length * 0.47;
                double hw = s.cls->beam * 0.5 * (std::fabs(l.x) > halfL * 0.6 ? 0.6 : 1.0);
                if (std::fabs(l.x) > halfL || std::fabs(l.y) > hw) continue;
                if (!armed) continue;
                if (t.deep && s.cls->draft < 6.0) continue;  // runs under shallow-draft ships
                t.alive = false;
                Event e;
                e.time = time_;
                e.a = t.firer;
                e.b = s.id;
                e.side = t.side;
                e.pos = {t.pos, 0.0};
                e.value = t.def->warheadKg;
                if (rng_.chance(t.def->dudChance)) {
                    e.type = EventType::TorpedoDud;
                    emit(e);
                } else {
                    e.type = EventType::TorpedoHit;
                    emit(e);
                    damage::torpedoHit(*this, s, t, l);
                    if (Ship* f = ship(t.firer)) f->stats.torpedoHits++;
                }
                break;
            }
        }
        if (!t.alive) {
            Event e;
            e.type = EventType::TorpedoEnded;
            e.time = time_;
            e.b = t.id;
            e.side = t.side;
            emit(e);
        }
    }
    torpedoes_.erase(std::remove_if(torpedoes_.begin(), torpedoes_.end(), [](const Torpedo& t) { return !t.alive; }), torpedoes_.end());
}

void World::stepFlares(double dt) {
    for (auto& f : flares_) {
        f.life += dt;
        f.pos.z -= f.fallRate * dt;
        if (f.life > f.maxLife || f.pos.z < 5.0) f.alive = false;
    }
    flares_.erase(std::remove_if(flares_.begin(), flares_.end(), [](const Flare& f) { return !f.alive; }), flares_.end());
}

void World::stepAircraft(double dt) {
    for (auto& a : aircraft_) {
        a.stateTime += dt;
        Ship* parent = ship(a.parent);
        auto flyTo = [&](const Vec2& p) {
            a.heading = bearingTo(a.pos, p);
            a.pos += bearingVector(a.heading) * (a.speed * dt);
            return distance(a.pos, p) < 1500.0;
        };
        switch (a.state) {
            case AircraftMission::State::Launching:
                if (parent) a.pos = parent->pos;
                if (a.stateTime > 90.0) { a.state = AircraftMission::State::Outbound; a.stateTime = 0; }
                break;
            case AircraftMission::State::Outbound:
                if (flyTo(a.target)) { a.state = AircraftMission::State::OnStation; a.stateTime = 0; a.nextFlare = 0; }
                break;
            case AircraftMission::State::OnStation: {
                // Orbit the target area.
                a.heading = wrap360(a.heading + 3.0 * dt);
                a.pos = a.target + bearingVector(a.heading - 90.0) * 3000.0;
                if (a.kind == AircraftMission::Kind::Flares && a.stateTime >= a.nextFlare && a.flaresLeft > 0) {
                    Flare f;
                    f.pos = {a.target + bearingVector(rng_.uniform(0, 360)) * rng_.uniform(0, 2500.0), a.altitude};
                    f.intensity = 1.5;
                    f.maxLife = 150.0;
                    f.fallRate = 4.0;
                    f.side = a.side;
                    f.aircraft = true;
                    addFlare(f);
                    a.flaresLeft--;
                    a.nextFlare = a.stateTime + 75.0;
                    Event e;
                    e.type = EventType::FlareDropped;
                    e.time = time_;
                    e.a = a.parent;
                    e.side = a.side;
                    e.pos = f.pos;
                    emit(e);
                }
                if (a.stateTime >= a.nextScan) {
                    a.nextScan = a.stateTime + 20.0;
                    // Aircraft observers spot wakes and ships under the flares.
                    for (auto& sp : ships_) {
                        Ship& s = *sp;
                        if (!s.operational() || s.side == a.side) continue;
                        double d = distance(s.pos, a.pos);
                        double light = illuminationOn(s);
                        double range = 4000.0 + 20000.0 * std::pow(clamp(light, 0.0, 1.0), 0.6);
                        if (d > range || !rng_.chance(0.6)) continue;
                        Measurement m;
                        m.observer = kNoEntity;
                        m.truth = s.id;
                        m.sense = kSenseAircraft;
                        m.pos = s.pos + Vec2{rng_.normal() * 900.0, rng_.normal() * 900.0};
                        m.sigma = 1200.0;
                        m.level = d < range * 0.4 ? ClassLevel::TypeKnown : ClassLevel::SizeKnown;
                        m.size = s.cls->size();
                        m.typeGuess = rng_.chance(0.3) ? (s.cls->type == ShipType::DD ? ShipType::CL : ShipType::BB) : s.cls->type;
                        m.hasVelocity = true;
                        m.velocity = s.velocity() + Vec2{rng_.normal() * 2.0, rng_.normal() * 2.0};
                        m.velSigma = 4.0;
                        picture(a.side).report(m, time_);
                    }
                }
                if ((a.kind == AircraftMission::Kind::Flares && a.flaresLeft <= 0) || a.stateTime > a.endurance)
                    a.state = AircraftMission::State::Returning;
                break;
            }
            case AircraftMission::State::Returning:
                if (!parent || flyTo(parent->pos)) a.state = AircraftMission::State::Done;
                break;
            case AircraftMission::State::Done: break;
        }
    }
    aircraft_.erase(std::remove_if(aircraft_.begin(), aircraft_.end(), [](const AircraftMission& a) { return a.state == AircraftMission::State::Done; }),
                    aircraft_.end());
}

void World::stepCollisions() {
    for (size_t i = 0; i < ships_.size(); ++i) {
        Ship& a = *ships_[i];
        if (a.dmg.sunk) continue;
        for (size_t j = i + 1; j < ships_.size(); ++j) {
            Ship& b = *ships_[j];
            if (b.dmg.sunk) continue;
            double maxD = (a.cls->length + b.cls->length) * 0.5;
            if (distanceSq(a.pos, b.pos) > maxD * maxD) continue;
            // Three circles along each hull.
            bool hit = false;
            for (int ka = -1; ka <= 1 && !hit; ++ka)
                for (int kb = -1; kb <= 1 && !hit; ++kb) {
                    Vec2 pa = a.pos + a.forward() * (ka * a.cls->length * 0.33);
                    Vec2 pb = b.pos + b.forward() * (kb * b.cls->length * 0.33);
                    hit = distance(pa, pb) < (a.cls->beam + b.cls->beam) * 0.5;
                }
            if (!hit) continue;
            double rel = length(a.velocity() - b.velocity());
            damage::collision(*this, a, b, rel);
            // Separate the hulls and kill most of the momentum.
            Vec2 n = normalize(b.pos - a.pos);
            a.pos -= n * 5.0;
            b.pos += n * 5.0;
            a.speed *= 0.4;
            b.speed *= 0.4;
        }
    }
}

}  // namespace it::sim
