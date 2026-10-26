#include "ai/AiController.h"

#include <algorithm>

#include "core/Log.h"
#include "sim/FireControl.h"

namespace it::ai {

MissionType missionFromString(const std::string& s) {
    if (s == "attack") return MissionType::Attack;
    if (s == "transit") return MissionType::Transit;
    if (s == "escort") return MissionType::Escort;
    if (s == "picket") return MissionType::Picket;
    if (s == "hold") return MissionType::Hold;
    if (s == "intercept") return MissionType::Intercept;
    return MissionType::Patrol;
}

const char* missionKey(MissionType m) {
    static const char* k[] = {"mission.patrol", "mission.attack", "mission.transit", "mission.escort",
                              "mission.picket", "mission.hold",   "mission.intercept"};
    return k[int(m)];
}

const char* postureKey(Posture p) {
    static const char* k[] = {"ai.o.mission", "ai.o.investigate", "ai.o.engage",  "ai.o.torpedo_attack",
                              "ai.o.shadow",  "ai.o.withdraw",    "ai.o.regroup"};
    return k[int(p)];
}

namespace {
Posture postureFromOption(const std::string& o) {
    for (int i = 0; i <= int(Posture::Regroup); ++i)
        if (o == postureKey(Posture(i))) return Posture(i);
    return Posture::Mission;
}
std::string fmt1(double v) { return formatString("%.0f", v); }
}  // namespace

AiController::AiController(sim::World& w, sim::Side side, const DoctrineLibrary& lib, AiSettings settings)
    : w_(w), view_(w, side), lib_(lib), settings_(settings) {
    for (auto* g : view_.ownGroups()) {
        GroupBrain b;
        b.groupId = g->id;
        sim::Nation nat = sim::Nation::Unknown;
        if (!g->members.empty())
            if (auto* s = w.ship(g->members.front())) nat = s->nation;
        b.doctrine = &lib_.get(g->doctrine, nat);
        b.postureSince = w.time();
        groups_[g->id] = b;
    }
    for (auto* s : view_.ownShips()) {
        CaptainBrain c;
        c.ship = s->id;
        c.nextThink = w.time() + double(s->id % 10) * 0.1;
        captains_[s->id] = c;
    }
}

void AiController::setMission(int groupId, const Mission& m, const std::string& doctrine) {
    auto it = groups_.find(groupId);
    if (it == groups_.end()) return;
    it->second.mission = m;
    it->second.waypoint = 0;
    if (!doctrine.empty()) {
        sim::Nation nat = sim::Nation::Unknown;
        if (auto* g = w_.group(groupId); g && !g->members.empty())
            if (auto* s = w_.ship(g->members.front())) nat = s->nation;
        it->second.doctrine = &lib_.get(doctrine, nat);
    }
    if (auto* g = w_.group(groupId)) {
        g->speedKn = m.speedKn;
        if (!m.waypoints.empty())
            if (auto* guide = w_.guideOf(*g)) g->course = bearingTo(guide->pos, m.waypoints.front());
    }
}

const GroupBrain* AiController::brain(int groupId) const {
    auto it = groups_.find(groupId);
    return it == groups_.end() ? nullptr : &it->second;
}
GroupBrain* AiController::brain(int groupId) {
    auto it = groups_.find(groupId);
    return it == groups_.end() ? nullptr : &it->second;
}
const CaptainBrain* AiController::captain(sim::EntityId ship) const {
    auto it = captains_.find(ship);
    return it == captains_.end() ? nullptr : &it->second;
}

GroupBrain* AiController::brainOf(const sim::Ship& s) { return s.groupId >= 0 ? brain(s.groupId) : nullptr; }

const Doctrine& AiController::doctrineFor(const sim::Ship& s) const {
    if (s.groupId >= 0)
        if (const GroupBrain* b = brain(s.groupId); b && b->doctrine) return *b->doctrine;
    return lib_.get("", s.nation);
}

std::vector<sim::Ship*> AiController::members(const sim::Group& grp) const {
    std::vector<sim::Ship*> out;
    for (sim::EntityId id : grp.members)
        if (sim::Ship* s = view_.own(id); s && s->operational() && !s->orders.playerSteering) out.push_back(s);
    return out;
}

int AiController::slotOf(const sim::Group& grp, const sim::Ship& s) const {
    int slot = 0;
    for (sim::EntityId id : grp.members) {
        sim::Ship* m = view_.own(id);
        if (!m || !m->operational() || m->orders.playerSteering) continue;
        if (m == &s) return slot;
        ++slot;
    }
    return slot;
}

double AiController::reactionTime(const sim::Ship& s, const Doctrine& d) const {
    return d.reactionTime * (1.6 - 0.9 * settings_.skill) / sim::crewFactor(s.crew) * (1.0 + 2.0 * (1.0 - s.dmg.bridge)) *
           (1.0 + s.dmg.crewLoss);
}

void AiController::record(DecisionLevel level, const std::string& actor, const std::string& decision, const std::string& detail,
                          std::vector<std::string> args, std::vector<OptionScore> alts, double score) {
    DecisionRecord r;
    r.time = w_.time();
    r.side = view_.side();
    r.level = level;
    r.actor = actor;
    r.decision = decision;
    r.detail = detail;
    r.args = std::move(args);
    r.alternatives = std::move(alts);
    r.score = score;
    log_.add(std::move(r));
}

void AiController::observe(const sim::Event& e) { view_.observe(e); }

// ============================================================================================ update
void AiController::update() {
    double now = w_.time();
    // Late-joining groups/ships (scenario reinforcements).
    for (auto* g : view_.ownGroups())
        if (!groups_.count(g->id)) {
            GroupBrain b;
            b.groupId = g->id;
            sim::Nation nat = g->members.empty() ? sim::Nation::Unknown : (w_.ship(g->members.front()) ? w_.ship(g->members.front())->nation : sim::Nation::Unknown);
            b.doctrine = &lib_.get(g->doctrine, nat);
            b.postureSince = now;
            groups_[g->id] = b;
        }
    for (auto* s : view_.ownShips())
        if (!captains_.count(s->id)) captains_[s->id] = CaptainBrain{s->id, now};
    if (w_.tick() % 20 == 0) view_.prune();

    for (auto& [id, g] : groups_) {
        sim::Group* grp = w_.group(id);
        if (!grp || g.exited) continue;
        bool commanded = !(settings_.assistMode || grp->playerControlled);
        if (commanded && now >= g.nextThink) {
            g.nextThink = now + 12.0 + double(id % 5);
            thinkCommander(g);
        }
        if (g.pendingAt >= 0 && now >= g.pendingAt) {
            g.posture = g.pendingPosture;
            g.postureSince = now;
            g.pendingAt = -1;
            if (g.posture == Posture::TorpedoAttack) {
                g.phase = TorpPhase::Approach;
                g.phaseSince = now;
            }
        }
        if (w_.tick() % 40 == uint64_t(id % 40)) {
            if (commanded) runGroup(g);
        }
        if (now >= g.nextAssign) {
            g.nextAssign = now + 8.0;
            if (!commanded) g.last = assess(view_, members(*grp), *g.doctrine);
            assignTargets(g);
        }
    }
    for (auto& [id, c] : captains_) {
        if (now < c.nextThink) continue;
        c.nextThink = now + 1.0;
        runCaptain(c);
    }
}

// ========================================================================================= commander
void AiController::thinkCommander(GroupBrain& g) {
    sim::Group& grp = *w_.group(g.groupId);
    auto mem = members(grp);
    if (mem.empty()) return;
    const Doctrine& d = *g.doctrine;
    double now = w_.time();
    Assessment a = assess(view_, mem, d);
    g.last = a;
    double ratio = a.ratio(d.caution);
    bool contacts = false, anyFireable = false;
    for (auto& t : a.threats) {
        if (t.age < 300.0) contacts = true;
        if (t.fireable && t.age < 120.0) anyFireable = true;
    }
    const ThreatEstimate* pri = a.primary();
    double gunR = d.gunRange(a.night), torR = d.torpedoRange(a.night);
    double proximity = contacts ? 1.0 - clamp(a.nearest / (gunR * 3.0), 0.0, 1.0) : 0.0;

    // Dawn pressure: the IJN at Savo broke off to be out of range of carrier aircraft at first light.
    double dawn = 0.0;
    if (d.withdrawBeforeDawnMinutes > 0 && a.night && a.secondsToDawn < 1e8) {
        double margin = d.withdrawBeforeDawnMinutes * 60.0;
        dawn = 1.0 - clamp((a.secondsToDawn - margin) / 3600.0, 0.0, 1.0);
    }
    if (g.mission.withdrawAtHour >= 0) {
        double h = w_.env().localHour();
        double dh = std::fmod(h - g.mission.withdrawAtHour + 36.0, 24.0) - 12.0;  // >0: past the hour
        if (dh > 0) dawn = std::max(dawn, clamp(0.6 + dh, 0.0, 1.0));
    }
    double spent = 1.0 - std::max(a.ammo * 0.8, a.torpedoFraction);

    std::vector<OptionScore> opts;
    {
        OptionScore o(postureKey(Posture::Mission), 1.0);
        o.add("no_threat", proximity, respond(Curve::Quadratic, proximity, true));
        o.add("mission_pending", g.missionDone ? 0.0 : 1.0, g.missionDone ? 0.35 : 0.9);
        o.add("dawn", dawn, 1.0 - 0.5 * dawn);
        opts.push_back(o);
    }
    if (contacts) {
        OptionScore o(postureKey(Posture::Engage), 1.0);
        o.add("force_ratio", ratio, respond(Curve::Logistic, ratio / (2.0 * d.forceRatioEngage), false, 8.0, 0.5));
        o.add("proximity", proximity, respond(Curve::Sqrt, proximity));
        o.add("aggression", d.aggression, 0.4 + 0.6 * d.aggression);
        o.add("target_identified", anyFireable ? 1.0 : a.knowledge, anyFireable ? 1.0 : 0.3 + 0.4 * a.knowledge);
        o.add("ammunition", a.ammo, respond(Curve::Sqrt, a.ammo));
        o.add("dawn", dawn, 1.0 - 0.9 * dawn);
        o.add("health", a.health, respond(Curve::Sqrt, a.health));
        if (d.torpedoFirst && a.torpedoFraction > 0.3 && !a.detectedByEnemy) o.add("torpedoes_first", 1.0, 0.45);
        if (pri) o.add("target_value", double(pri->size), pri->size == sim::SizeClass::Small ? 0.55 : (pri->size == sim::SizeClass::Medium ? 0.8 : 1.0));
        if (g.mission.type == MissionType::Attack && !g.missionDone && !a.detectedByEnemy) o.add("preserve_surprise", 1.0, 0.65);
        opts.push_back(o);
    }
    if (contacts && a.torpedoes > 0 && pri) {
        OptionScore o(postureKey(Posture::TorpedoAttack), 1.05);
        double rel = pri->distance / std::max(1000.0, torR);
        o.add("torpedoes_ready", a.torpedoFraction, respond(Curve::Sqrt, a.torpedoFraction));
        o.add("night", a.night ? 1.0 : 0.0, a.night ? 0.55 + 0.45 * d.nightPreference : 0.45);
        o.add("doctrine", d.torpedoFirst ? 1.0 : 0.0, d.torpedoFirst ? 1.0 : 0.5);
        o.add("surprise", a.detectedByEnemy ? 0.0 : 1.0, a.detectedByEnemy ? 0.65 : 1.0);
        o.add("range_band", rel, respond(Curve::Bell, clamp(rel / 5.0, 0.0, 1.0), false, 5.0, 0.3));
        o.add("target_size", double(pri->size), pri->size == sim::SizeClass::Small ? 0.45 : 1.0);
        if (g.mission.type == MissionType::Attack && !g.missionDone && !a.detectedByEnemy && pri->size == sim::SizeClass::Small)
            o.add("preserve_surprise", 1.0, 0.6);
        o.add("dawn", dawn, 1.0 - 0.8 * dawn);
        o.add("health", a.health, respond(Curve::Sqrt, a.health));
        opts.push_back(o);
    }
    if (contacts && !anyFireable) {
        OptionScore o(postureKey(Posture::Investigate), 0.8);
        o.add("unidentified", 1.0 - a.knowledge, 0.5 + 0.5 * (1.0 - a.knowledge));
        o.add("aggression", d.aggression, 0.3 + 0.7 * d.aggression);
        o.add("proximity", proximity, respond(Curve::Bell, proximity, false, 4.0, 0.4));
        o.add("dawn", dawn, 1.0 - dawn);
        opts.push_back(o);
    }
    if (contacts) {
        OptionScore o(postureKey(Posture::Shadow), 0.55);
        o.add("weaker", ratio, respond(Curve::Logistic, ratio, true, 6.0, 0.8));
        o.add("mission_picket", g.mission.type == MissionType::Picket ? 1.0 : 0.0, g.mission.type == MissionType::Picket ? 1.0 : 0.5);
        o.add("unidentified", a.knowledge, g.mission.type == MissionType::Picket ? 1.0 : 0.7);
        o.add("health", a.health, a.health);
        opts.push_back(o);
    }
    {
        OptionScore o(postureKey(Posture::Withdraw), 1.0);
        double danger = contacts ? respond(Curve::Logistic, 1.0 - ratio / (2.0 * std::max(0.05, d.forceRatioWithdraw)), false, 8.0, 0.5) *
                                       respond(Curve::Sqrt, proximity)
                                 : 0.0;
        double damage = respond(Curve::Quadratic, 1.0 - a.health);
        double reason = std::max({danger, damage, dawn, contacts ? spent * 0.9 : spent * 0.5, g.missionDone && !contacts ? 0.6 : 0.0});
        o.add("reason", reason, reason);
        // Pickets stay on station to report unless actually under fire.
        if (g.mission.type == MissionType::Picket && !a.underFire) o.add("picket_duty", 1.0, 0.6);
        o.add("caution", d.caution, 0.6 + 0.4 * d.caution);
        opts.push_back(o);
    }
    {
        double disp = 0;
        int n = 0;
        if (sim::Ship* guide = view_.guide(grp))
            for (auto* s : mem) {
                if (s == guide) continue;
                int slot = slotOf(grp, *s);
                Vec2 off = nav::stationOffset(grp, slot, int(mem.size()));
                Vec2 st = guide->pos + guide->forward() * off.x + guide->right() * off.y;
                disp += distance(s->pos, st) / std::max(500.0, grp.spacing);
                ++n;
            }
        disp = n ? disp / n : 0.0;
        OptionScore o(postureKey(Posture::Regroup), 0.7);
        o.add("dispersion", disp, respond(Curve::Logistic, clamp(disp / 4.0, 0.0, 1.0), false, 10.0, 0.5));
        o.add("no_threat", proximity, 1.0 - proximity);
        opts.push_back(o);
    }

    int idx = choose(opts, postureKey(g.posture), 0.2);
    if (idx < 0) return;
    Posture p = postureFromOption(opts[size_t(idx)].option);
    bool emergency = p == Posture::Withdraw && a.health < 0.5;
    if (p == g.posture || g.pendingAt >= 0) return;
    if (now - g.postureSince < 45.0 && !emergency) return;
    applyPosture(g, p, opts, idx);
}

void AiController::applyPosture(GroupBrain& g, Posture p, const std::vector<OptionScore>& options, int chosen) {
    sim::Group& grp = *w_.group(g.groupId);
    double delay = 8.0;
    if (sim::Ship* guide = view_.guide(grp)) delay = 0.6 * reactionTime(*guide, *g.doctrine) + 6.0;
    g.pendingPosture = p;
    g.pendingAt = w_.time() + delay;
    std::string detail;
    if (const ThreatEstimate* pri = g.last.primary())
        detail = formatString("%s|%.0f|%.0f", sim::shipTypeId(pri->type), pri->bearing, pri->distance);
    record(DecisionLevel::Commander, grp.name, postureKey(p), detail, {grp.name}, options, options[size_t(chosen)].score);
}

// ============================================================================================= group
void AiController::runGroup(GroupBrain& g) {
    sim::Group& grp = *w_.group(g.groupId);
    sim::Ship* guide = view_.guide(grp);
    if (!guide) return;
    const Doctrine& d = *g.doctrine;
    double now = w_.time();
    switch (g.posture) {
        case Posture::Mission: runMission(g, grp, *guide); break;
        case Posture::Engage: runEngage(g, grp, *guide); break;
        case Posture::TorpedoAttack: runTorpedoAttack(g, grp, *guide); break;
        case Posture::Withdraw: runWithdraw(g, grp, *guide); break;
        case Posture::Investigate: {
            const ThreatEstimate* best = nullptr;
            for (auto& t : g.last.threats)
                if (!t.fireable && (!best || t.distance < best->distance)) best = &t;
            if (best) {
                grp.course = bearingTo(guide->pos, best->pos + best->vel * 120.0);
                grp.speedKn = std::min(g.last.maxSpeedKn, g.mission.speedKn + 6.0);
            } else {
                runMission(g, grp, *guide);
            }
            break;
        }
        case Posture::Shadow: {
            if (g.last.nearestIdx >= 0) {
                const auto& t = g.last.threats[size_t(g.last.nearestIdx)];
                double keep = d.gunRange(g.last.night) * 1.5;
                double base = length(t.vel) > 1.0 ? bearingOf(t.vel) : grp.course;
                double brg = bearingTo(guide->pos, t.pos);
                double err = (t.distance - keep) / keep;
                double side = angleDiff(base, brg) > 0 ? 1.0 : -1.0;
                grp.course = wrap360(base + side * clamp(err * 60.0, -50.0, 50.0));
                grp.speedKn = std::min(g.last.maxSpeedKn, std::max(12.0, length(t.vel) / 0.514444 + 2.0));
            }
            break;
        }
        case Posture::Regroup:
            grp.speedKn = std::max(8.0, g.mission.speedKn * 0.6);
            break;
    }
    // Fire discipline: stay dark while the torpedoes run (IJN), and do not betray the force to minor
    // contacts while closing the real objective unless already discovered. A destroyer force keeps its
    // guns silent even under fire - muzzle flashes would give away every ship, not only the one being shot
    // at (Tanaka at Tassafaronga) - while cruisers answer with their main battery.
    bool stealthy = d.torpedoFirst || d.nightPreference > 0.7;
    bool approach = g.posture == Posture::Mission || g.posture == Posture::Investigate || g.posture == Posture::Shadow ||
                    g.posture == Posture::Regroup;
    bool torpedoRun = g.posture == Posture::TorpedoAttack && d.holdFireUntilTorpedoImpact &&
                      (g.phase != TorpPhase::Wait || now < g.impactTime + 20.0);
    bool destroyers = true;
    for (auto* s : members(grp))
        if (s->operational() && s->cls->size() != sim::SizeClass::Small) destroyers = false;
    g.holdFire = (torpedoRun && destroyers && g.last.night) ||
                 (!g.last.underFire && !g.last.detectedByEnemy && (torpedoRun || (approach && stealthy && g.last.night)));
    // Aircraft flares over the enemy (Chokai's floatplanes at Savo Island).
    if (d.useAircraftFlares && g.last.night && !g.flaresLaunched &&
        (g.posture == Posture::Engage || g.posture == Posture::TorpedoAttack)) {
        if (const ThreatEstimate* pri = g.last.primary(); pri && pri->distance < 30000.0) {
            for (auto* s : members(grp))
                if (s->floatplanes > 0 && view_.launchAircraft(*s, true, g.last.enemyCenter)) {
                    g.flaresLaunched = true;
                    record(DecisionLevel::Group, grp.name, "ai.d.launch_flares", "", {s->name});
                    break;
                }
        }
    }
    grp.course = nav::avoidLand(view_.chart(), guide->pos, grp.course, guide->cls->draft,
                                std::max(2000.0, grp.speedKn * 0.514444 * 180.0));
}

void AiController::runMission(GroupBrain& g, sim::Group& grp, sim::Ship& guide) {
    const Mission& m = g.mission;
    grp.speedKn = m.speedKn;
    if (m.type == MissionType::Escort && m.escortGroup >= 0) {
        if (sim::Group* eg = w_.group(m.escortGroup))
            if (sim::Ship* eguide = view_.guide(*eg)) {
                Vec2 station = eguide->pos + eguide->forward() * m.escortOffset;
                grp.course = bearingTo(guide.pos, station);
                grp.speedKn = clamp(eg->speedKn + distance(guide.pos, station) / 300.0, 0.0, guide.cls->maxSpeedKn);
                return;
            }
    }
    if (m.waypoints.empty()) {
        if (m.type == MissionType::Hold) {
            grp.course = wrap360(grp.course + 1.0);
            grp.speedKn = std::min(grp.speedKn, 10.0);
        }
        return;
    }
    if (g.waypoint >= m.waypoints.size()) {
        g.missionDone = true;
        if (m.type == MissionType::Attack) {
            // Sweep the objective area.
            Vec2 rel = guide.pos - m.objective;
            double ang = bearingOf(rel) + 25.0;
            grp.course = bearingTo(guide.pos, m.objective + bearingVector(ang) * m.radius * 0.6);
        } else if (m.type == MissionType::Transit) {
            g.exited = true;
        }
        return;
    }
    Vec2 wp = m.waypoints[g.waypoint];
    if (distance(guide.pos, wp) < 1200.0) {
        ++g.waypoint;
        if (g.waypoint >= m.waypoints.size() && m.loop) g.waypoint = 0;
        if (g.waypoint < m.waypoints.size()) wp = m.waypoints[g.waypoint];
    }
    grp.course = bearingTo(guide.pos, wp);
}

void AiController::runEngage(GroupBrain& g, sim::Group& grp, sim::Ship& guide) {
    const Doctrine& d = *g.doctrine;
    const ThreatEstimate* pri = g.last.primary();
    if (!pri) {
        runMission(g, grp, guide);
        return;
    }
    g.primary = pri->track;
    double R = d.gunRange(g.last.night) * (g.last.ratio(d.caution) > 1.5 ? 0.85 : 1.0);
    double B = bearingTo(guide.pos, pri->pos);
    Vec2 enemyDir = length(pri->vel) > 1.0 ? normalize(pri->vel) : bearingVector(B + 90.0);
    // Broadside course that crosses ahead of the enemy (capping the T) with as little turning as possible.
    double best = B + 90.0, bestScore = -1e9;
    for (double c : {B + 90.0, B - 90.0}) {
        double sc = dot(bearingVector(c), enemyDir) * 0.8 + std::cos(angleDiff(guide.heading, c) * kDegToRad) * 0.6;
        if (sc > bestScore) {
            bestScore = sc;
            best = c;
        }
    }
    double dist = distance(guide.pos, pri->pos);
    double corr = clamp((dist - R) / R * 60.0, -45.0, 45.0);
    double towards = angleDiff(best, B) > 0 ? 1.0 : -1.0;
    grp.course = wrap360(best + towards * corr);
    grp.speedKn = std::min(g.last.maxSpeedKn, g.mission.attackSpeedKn);
}

void AiController::runTorpedoAttack(GroupBrain& g, sim::Group& grp, sim::Ship& guide) {
    const Doctrine& d = *g.doctrine;
    double now = w_.time();
    // Target: the best large contact with a usable track.
    const ThreatEstimate* tgt = nullptr;
    for (auto& t : g.last.threats) {
        if (t.age > 120.0) continue;
        double v = t.priority * (t.size == sim::SizeClass::Small ? 0.5 : 1.0) / (1.0 + t.sigma / 800.0);
        if (!tgt || v > tgt->priority * (tgt->size == sim::SizeClass::Small ? 0.5 : 1.0) / (1.0 + tgt->sigma / 800.0)) tgt = &t;
    }
    if (!tgt) {
        runMission(g, grp, guide);
        return;
    }
    g.torpTarget = tgt->track;
    double Rl = d.torpedoRange(g.last.night);
    double dist = distance(guide.pos, tgt->pos);
    double B = bearingTo(guide.pos, tgt->pos);
    double attackSpeed = std::min(g.last.maxSpeedKn, g.mission.attackSpeedKn);
    auto setPhase = [&](TorpPhase p, const char* key) {
        g.phase = p;
        g.phaseSince = now;
        record(DecisionLevel::Group, grp.name, key, formatString("%.0f|%.0f", B, dist), {grp.name, fmt1(dist / 1000.0)});
    };
    switch (g.phase) {
        case TorpPhase::Approach: {
            double tCourse = length(tgt->vel) > 1.0 ? bearingOf(tgt->vel) : wrap360(B + 180.0);
            Vec2 tdir = bearingVector(tCourse);
            double side = cross(tdir, guide.pos - tgt->pos) >= 0 ? -1.0 : 1.0;  // our side of their track
            double lead = dist / std::max(5.0, attackSpeed * 0.514444);
            Vec2 fp = tgt->pos + tgt->vel * (lead * 0.6) + bearingVector(tCourse + side * 55.0) * Rl;
            grp.course = bearingTo(guide.pos, fp);
            grp.speedKn = attackSpeed;
            // Under fire there is no time to reach the ideal position: launch from longer range.
            if (dist <= Rl * (g.last.underFire ? 1.6 : 1.15)) {
                g.launchRange = dist * 1.1;
                double c1 = wrap360(B + 90.0), c2 = wrap360(B - 90.0);
                g.turnAwayCourse = std::fabs(angleDiff(guide.heading, c1)) < std::fabs(angleDiff(guide.heading, c2)) ? c1 : c2;
                setPhase(TorpPhase::Launch, "ai.d.torpedo_launch_position");
            } else if (now - g.phaseSince > 1500.0) {
                g.pendingPosture = Posture::Engage;
                g.pendingAt = now;
            }
            break;
        }
        case TorpPhase::Launch: {
            // Present the beam to the target so the centre-line mounts bear.
            grp.course = g.turnAwayCourse;
            grp.speedKn = attackSpeed;
            int ready = 0;
            for (auto* s : members(grp)) ready += s->torpedoesReady();
            if (ready == 0 || now - g.phaseSince > 150.0) {
                double tSpeed = 48.0 * 0.514444;
                g.impactTime = now + dist / tSpeed;
                g.turnAwayCourse = wrap360(B + 180.0 + (angleDiff(B, guide.heading) > 0 ? -30.0 : 30.0));
                setPhase(TorpPhase::TurnAway, "ai.d.torpedo_turn_away");
            }
            break;
        }
        case TorpPhase::TurnAway:
            grp.course = g.turnAwayCourse;
            grp.speedKn = attackSpeed;
            if (now > g.impactTime + 30.0) setPhase(TorpPhase::Wait, "ai.d.torpedo_impact_window");
            break;
        case TorpPhase::Wait: {
            int ready = 0, total = 0;
            for (auto* s : members(grp)) {
                ready += s->torpedoesReady();
                total += s->torpedoesTotal();
            }
            // Open the range while reloading, keep the enemy in sight.
            double keep = Rl * 1.8;
            double away = wrap360(B + 180.0);
            grp.course = dist < keep ? away : wrap360(B + 90.0);
            grp.speedKn = std::min(attackSpeed, 26.0);
            if (d.reloadAndReattack && total > 0 && ready * 2 >= total) setPhase(TorpPhase::Approach, "ai.d.torpedo_reattack");
            break;
        }
    }
}

void AiController::runWithdraw(GroupBrain& g, sim::Group& grp, sim::Ship& guide) {
    const Mission& m = g.mission;
    if (!m.exitRoute.empty()) {
        size_t i = 0;
        double best = 1e18;
        for (size_t k = 0; k < m.exitRoute.size(); ++k) {
            double dd = distance(guide.pos, m.exitRoute[k]);
            if (dd < best) {
                best = dd;
                i = k;
            }
        }
        if (best < 2000.0 && i + 1 < m.exitRoute.size()) ++i;
        if (best < 2000.0 && i + 1 == m.exitRoute.size()) g.exited = g.last.nearest > 25000.0;
        grp.course = bearingTo(guide.pos, m.exitRoute[i]);
    } else if (!g.last.threats.empty()) {
        grp.course = wrap360(bearingTo(guide.pos, g.last.enemyCenter) + 180.0);
    }
    grp.speedKn = g.last.maxSpeedKn;
}

void AiController::assignTargets(GroupBrain& g) {
    sim::Group* grp = w_.group(g.groupId);
    if (!grp) return;
    const Doctrine& d = *g.doctrine;
    double now = w_.time();
    std::map<uint32_t, int> load;
    for (auto* s : members(*grp)) {
        auto& slots = g.assignment[s->id];
        slots = {0, 0, 0};
        for (size_t di = 0; di < s->directors.size() && di < 3; ++di) {
            const auto& dir = s->directors[di];
            if (dir.def->role == "torpedo") continue;
            double gunMax = 0, cal = 0;
            for (auto& m : s->mounts)
                if (m.def->director == dir.def->id && m.def->gun) {
                    gunMax = std::max(gunMax, m.def->gun->effectiveMaxRange());
                    cal = std::max(cal, m.def->gun->caliberMm);
                }
            if (gunMax <= 0) continue;
            bool secondary = dir.def->role == "secondary";
            uint32_t best = 0;
            double bestV = 0;
            for (auto& t : g.last.threats) {
                if (!t.fireable || t.age > 60.0) continue;
                const sim::Track* tr = view_.contact(t.track);
                if (!tr) continue;
                double dist = distance(s->pos, tr->predict(now));
                if (dist > gunMax * 0.95) continue;
                double held = tr->heldBy(s->id, now, 30.0) ? 1.0 : 0.45;
                double v = t.priority * held * (1.0 - 0.5 * dist / gunMax);
                if (secondary) v *= (t.size == sim::SizeClass::Small ? 1.4 : 0.8) * (dist < 9000.0 ? 1.2 : 0.6);
                else if (cal >= 150 && t.size == sim::SizeClass::Small) v *= 0.7;
                if (dir.targetTrack == t.track) v *= 1.25;  // keep the solution we have
                v /= 1.0 + 0.7 * load[t.track];
                (void)d;
                if (v > bestV) {
                    bestV = v;
                    best = t.track;
                }
            }
            slots[di] = best;
            if (best) load[best]++;
        }
    }
}

// =========================================================================================== captain
int AiController::torpedoSetting(const sim::Ship& s, double run) const {
    for (auto& tm : s.torpedoMounts) {
        if (!tm.def->torpedo) continue;
        const auto& st = tm.def->torpedo->settings;
        for (size_t i = 0; i < st.size(); ++i)
            if (st[i].range >= run * 1.15) return int(i);
        return int(st.size()) - 1;
    }
    return 0;
}

bool torpedoFanEndangers(const Vec2& launcher, double course, double spreadDeg, double run, double speed, const sim::Ship& o) {
    double half = spreadDeg * 0.5 + 4.0;  // gyro and launch-angle errors widen the fan
    double margin = o.cls->length * 0.5 + 400.0;
    if (distance(launcher, o.pos) > run + margin + length(o.velocity()) * run / speed) return false;
    for (double a = -half; a <= half + 0.01; a += std::max(1.0, half / 4.0)) {
        Vec2 dir = bearingVector(course + a);
        for (double dd = 300.0; dd <= run; dd += 300.0)
            if (distance(launcher + dir * dd, o.pos + o.velocity() * (dd / speed)) < margin) return true;
    }
    return false;
}

bool AiController::friendlyInTorpedoPath(const sim::Ship& s, double course, double spreadDeg, int setting) const {
    // Torpedoes that miss run on to the end of their range: check every track of the fan over the full
    // run against where each friendly ship will be when the torpedo gets there.
    double speed = 45.0 * 0.514444, run = 10000.0;
    for (auto& tm : s.torpedoMounts)
        if (tm.def->torpedo && !tm.def->torpedo->settings.empty()) {
            const auto& st = tm.def->torpedo->settings[size_t(clamp(setting, 0, int(tm.def->torpedo->settings.size()) - 1))];
            speed = st.speedKn * 0.514444;
            run = st.range;
            break;
        }
    for (auto* o : view_.ownShips())
        if (o != &s && o->operational() && torpedoFanEndangers(s.pos, course, spreadDeg, run, speed, *o)) return true;
    return false;
}

void AiController::runCaptain(CaptainBrain& c) {
    sim::Ship* s = view_.own(c.ship);
    if (!s || !s->operational()) return;
    GroupBrain* g = brainOf(*s);
    const Doctrine& d = doctrineFor(*s);
    captainMovement(c, *s, g, d);
    captainWeapons(c, *s, g, d);
    captainTorpedoes(c, *s, g, d);
}

bool AiController::captainEvasion(CaptainBrain& c, sim::Ship& s, const Doctrine& d, double& course) {
    double now = w_.time();
    if (settings_.assistMode && !settings_.autoEvade) return false;
    for (auto& tw : view_.torpedoWarnings()) {
        if (c.handledWarnings.count(tw.key)) continue;
        double delay = reactionTime(s, d) + (tw.observer == s.id ? 0.0 : 20.0);  // relayed warnings take longer
        if (now < tw.time + delay) continue;
        Vec2 tp = tw.predict(now);
        Vec2 tv = bearingVector(tw.heading) * tw.speed;
        double t = closestApproachTime(tp, tv, s.pos, s.velocity());
        if (t <= 0.0 || t > 400.0) continue;
        double cpa = distance(tp + tv * t, s.pos + s.velocity() * t);
        if (cpa > s.cls->length * 0.5 + 180.0) {
            c.handledWarnings.insert(tw.key);
            continue;
        }
        c.handledWarnings.insert(tw.key);
        // Comb the track: turn parallel to it (towards or away), whichever needs the smaller turn.
        double c1 = tw.heading, c2 = wrap360(tw.heading + 180.0);
        double choice = std::fabs(angleDiff(s.heading, c1)) < std::fabs(angleDiff(s.heading, c2)) ? c1 : c2;
        bool good = w_.rng().chance(clamp(d.evasionSkill * (0.5 + 0.5 * settings_.skill) * sim::crewFactor(s.crew) + 0.2, 0.1, 0.98));
        if (!good) choice = wrap360(s.heading + (w_.rng().chance(0.5) ? 45.0 : -45.0));
        c.override = CaptainBrain::Override::Evading;
        c.evadeCourse = choice;
        c.overrideUntil = now + t + 40.0;
        record(DecisionLevel::Captain, s.name, good ? "ai.d.comb_torpedo" : "ai.d.evade_torpedo", formatString("%.0f", choice),
               {s.name, fmt1(choice)});
    }
    if (c.override == CaptainBrain::Override::Evading) {
        if (now < c.overrideUntil) {
            course = c.evadeCourse;
            return true;
        }
        c.override = CaptainBrain::Override::None;
    }
    return false;
}

void AiController::captainMovement(CaptainBrain& c, sim::Ship& s, GroupBrain* g, const Doctrine& d) {
    double now = w_.time();
    sim::Group* grp = s.groupId >= 0 ? w_.group(s.groupId) : nullptr;
    double course = s.orders.course, speed = s.orders.speedKn;
    double groupSpeed = grp ? grp->speedKn : speed;
    bool playerShip = s.orders.playerSteering;
    if (!playerShip && grp) {
        sim::Ship* guide = view_.guide(*grp);
        // The guide steers the group course, limited by the slowest ship so the formation stays together.
        if (guide == &s || !guide) {
            course = grp->course;
            double slowest = 99;
            for (auto* m : members(*grp)) slowest = std::min(slowest, m->maxSpeedNow() / 0.514444);
            speed = std::min(grp->speedKn, std::max(6.0, slowest - 1.0));
        } else if (g && g->posture == Posture::TorpedoAttack && g->phase == TorpPhase::TurnAway) {
            course = g->turnAwayCourse;  // turn together after launching
            speed = s.cls->maxSpeedKn;
        } else {
            int slot = slotOf(*grp, s);
            nav::Steer st = grp->formation == sim::Formation::Column ? nav::followTrail(*grp, s, slot, *guide)
                                                                      : nav::keepStation(*grp, s, slot, *guide, int(members(*grp).size()));
            course = st.course;
            speed = st.speedKn;
        }
    }
    // Badly damaged ships leave the line.
    if (!playerShip && c.override != CaptainBrain::Override::Retiring && grp && view_.guide(*grp) &&
        (s.flotation() < 0.35 || s.dmg.bridge < 0.15 || (groupSpeed > 12.0 && s.maxSpeedNow() / 0.514444 < 0.5 * groupSpeed))) {
        c.override = CaptainBrain::Override::Retiring;
        record(DecisionLevel::Captain, s.name, "ai.d.retire_damaged", "", {s.name});
    }
    if (c.override == CaptainBrain::Override::Retiring) {
        Vec2 threat;
        double w = 0;
        for (auto& t : view_.contacts())
            if (!t.lost) {
                threat += t.predict(now);
                w += 1;
            }
        if (w > 0) course = wrap360(bearingTo(s.pos, threat / w) + 180.0);
        if (g && !g->mission.exitRoute.empty()) course = bearingTo(s.pos, g->mission.exitRoute.back());
        speed = s.maxSpeedNow() / 0.514444;
    }
    double evade;
    if (captainEvasion(c, s, d, evade)) {
        course = evade;
        speed = std::max(speed, s.maxSpeedNow() / 0.514444 * 0.9);
    } else if (d.chaseSalvos && !playerShip) {
        bool underFire = false;
        for (auto& h : view_.recentHits())
            if (h.ship == s.id && now - h.time < 90.0) underFire = true;
        bool independent = s.cls->size() == sim::SizeClass::Small || c.override == CaptainBrain::Override::Retiring ||
                           (grp && grp->formation != sim::Formation::Column);
        if (underFire && independent) {
            if (now > c.nextZig) {
                c.zigOffset = w_.rng().uniform(10.0, 25.0) * (w_.rng().chance(0.5) ? 1.0 : -1.0);
                c.nextZig = now + w_.rng().uniform(25.0, 45.0);
            }
            course = wrap360(course + c.zigOffset);
        } else {
            c.zigOffset = 0;
        }
    }
    double need = playerShip ? 900.0 : std::max(1500.0, speed * 0.514444 * 150.0);
    course = nav::avoidLand(view_.chart(), s.pos, course, s.cls->draft, need);
    if (!playerShip || c.override == CaptainBrain::Override::Evading) course = nav::avoidFriends(view_, s, course, speed);
    s.orders.course = course;
    s.orders.speedKn = speed;
}

void AiController::captainWeapons(CaptainBrain& c, sim::Ship& s, GroupBrain* g, const Doctrine& d) {
    double now = w_.time();
    bool playerShip = settings_.assistMode;
    if (s.dmg.hitsTaken > c.lastHitCount) {
        c.lastHitCount = s.dmg.hitsTaken;
        c.lastHitTime = now;
    }
    if (!playerShip) {
        if (g && g->holdFire && now - c.lastHitTime > 90.0)
            s.orders.weapons = sim::WeaponsStatus::Hold;
        else
            s.orders.weapons = d.requireIdentification ? sim::WeaponsStatus::Tight : sim::WeaponsStatus::Free;
    }
    std::array<uint32_t, 3> want{0, 0, 0};
    if (g) {
        auto it = g->assignment.find(s.id);
        if (it != g->assignment.end()) want = it->second;
    }
    if (s.orders.playerTarget) want[0] = s.orders.playerTarget;
    for (size_t di = 0; di < s.directors.size() && di < 3; ++di) {
        auto& dir = s.directors[di];
        if (dir.def->role == "torpedo") continue;
        uint32_t t = want[di];
        // If the director cannot hold the assigned target, take the best one we can see ourselves.
        if (t && dir.targetTrack == t && !dir.tracking && now - dir.lastMeasure > 25.0 && !(di == 0 && s.orders.playerTarget)) t = 0;
        if (!t) {
            double best = 0;
            for (auto& tr : view_.contacts()) {
                if (!fireable(tr, d, now) || !tr.heldBy(s.id, now, 15.0)) continue;
                double v = d.priority(tr.typeGuess) / (1.0 + distance(s.pos, tr.predict(now)) / 6000.0);
                if (v > best) {
                    best = v;
                    t = tr.id;
                }
            }
        }
        dir.illuminate = false;
        // Starshells from the secondary battery over the main battery's target (USN / RN practice).
        if (dir.def->role == "secondary" && d.illumination == Illumination::Starshell && view_.night() && s.directors[0].targetTrack &&
            !s.directors[0].radarRanging && s.orders.weapons != sim::WeaponsStatus::Hold) {
            for (auto& m : s.mounts)
                if (m.def->director == dir.def->id && m.ammo.count(sim::ShellKind::Starshell) && m.ammo[sim::ShellKind::Starshell] > 0) {
                    dir.illuminate = true;
                    t = s.directors[0].targetTrack;
                    break;
                }
        }
        if (dir.targetTrack != t) view_.setDirectorTarget(s, int(di), t);
    }
    // Searchlights (IJN practice): one designated ship per group illuminates the target briefly once fire
    // is opened; the others shoot. A long cooldown keeps the force from becoming a row of beacons.
    if (d.illumination == Illumination::Searchlight && s.cls->searchlights > 0) {
        uint32_t tgt = s.directors.empty() ? 0 : s.directors[0].targetTrack;
        bool designated = true;
        if (sim::Group* grp = s.groupId >= 0 ? w_.group(s.groupId) : nullptr) {
            for (sim::EntityId id : grp->members) {
                sim::Ship* o = view_.own(id);
                if (!o || !o->operational() || o->cls->size() == sim::SizeClass::Small || o->cls->searchlights == 0) continue;
                if (o == &s) break;           // first capable ship in the column is the illuminating ship
                if (o->orders.searchlight || o->dmg.searchlights > 0.3) {
                    designated = false;
                    break;
                }
            }
        }
        bool wantLight = designated && view_.night() && tgt && s.orders.weapons != sim::WeaponsStatus::Hold && !playerShip &&
                         s.cls->size() != sim::SizeClass::Small;
        const sim::Track* tr = wantLight ? view_.contact(tgt) : nullptr;
        if (wantLight) wantLight = tr && distance(s.pos, tr->predict(now)) < 7500.0;
        int hitsNow = 0;
        for (auto& h : view_.recentHits())
            if (h.ship == s.id && now - h.time < 60.0) ++hitsNow;
        bool cooling = c.searchlightSince >= 0 && now - c.searchlightSince > 120.0;
        if (cooling || hitsNow >= 2) wantLight = false;
        if (wantLight && c.searchlightSince < 0) {
            view_.setSearchlight(s, true, tgt);
            c.searchlightSince = now;
            record(DecisionLevel::Captain, s.name, "ai.d.searchlight_on", "", {s.name});
        } else if (!wantLight && s.orders.searchlight) {
            view_.setSearchlight(s, false, 0);
            record(DecisionLevel::Captain, s.name, "ai.d.searchlight_off", "", {s.name});
        }
        if (!s.orders.searchlight && c.searchlightSince >= 0 && now - c.searchlightSince > 900.0) c.searchlightSince = -1;
    }
}

void AiController::captainTorpedoes(CaptainBrain& c, sim::Ship& s, GroupBrain* g, const Doctrine& d) {
    if (settings_.assistMode && !settings_.autoTorpedoes) return;
    int ready = s.torpedoesReady();
    if (ready == 0) return;
    double now = w_.time();
    if (now - c.lastLaunch < 30.0) return;
    bool attackRun = g && g->posture == Posture::TorpedoAttack && g->phase == TorpPhase::Launch;
    bool engaged = g && (g->posture == Posture::Engage || g->posture == Posture::Withdraw);
    if (!attackRun && !engaged) return;
    double rangeLimit = d.torpedoRange(view_.night()) * (attackRun ? 1.35 : 1.0);
    // Ships further down the column are farther from the target than the guide that set the launch range.
    if (attackRun) rangeLimit = std::max(rangeLimit, g->launchRange * 1.3);
    uint32_t best = 0;
    double bestV = 0;
    std::vector<sim::Ship*> self{&s};
    for (auto& t : view_.contacts()) {
        if (t.lost || !fireable(t, d, now) || now - t.lastSeen > 30.0) continue;
        if (!view_.known(t, &self) && !(g && view_.known(t, nullptr))) continue;
        if (auto it = c.torpedoedTracks.find(t.id); it != c.torpedoedTracks.end() && now - it->second < 300.0) continue;
        Vec2 p = t.predict(now);
        double dist = distance(s.pos, p);
        if (dist > rangeLimit) continue;
        // A deliberate attack run accepts a poorer solution: the spread covers the error. A director that has
        // been following the target knows its course and speed far better than the plot.
        double velSigma = t.velSigma, posSigma = t.posSigma;
        if (const sim::DirectorState* sol = sim::firecontrol::solutionFor(s, t.id)) {
            velSigma = std::min(velSigma, 1.0 + 4.0 * (1.0 - sol->quality));
            posSigma = std::min(posSigma, sol->rangeSigma + 150.0);
        }
        if (velSigma > (attackRun ? 4.5 : 3.0) || posSigma > (attackRun ? 1500.0 : 600.0)) continue;
        double crossing = std::fabs(std::sin((t.courseEstimate() - bearingTo(s.pos, p)) * kDegToRad));
        if (length(t.vel) > 2.0 && crossing < 0.45 && !attackRun) continue;
        double v = d.priority(t.typeGuess) * (t.size == sim::SizeClass::Small ? 0.45 : 1.0) * (0.5 + 0.5 * crossing) / (1.0 + dist / 5000.0);
        if (attackRun && g && t.id == g->torpTarget) v *= 2.0;
        if (v > bestV) {
            bestV = v;
            best = t.id;
        }
    }
    if (!best) {
        if (attackRun && now - c.lastLaunch > 60.0 && now - c.searchlightSince > 0 && w_.tick() % 600 == 0)
            record(DecisionLevel::Captain, s.name, "ai.d.torpedo_no_solution", "", {s.name});
        return;
    }
    const sim::Track* tr = view_.contact(best);
    Vec2 p = tr->predict(now);
    double dist = distance(s.pos, p);
    // Only tubes that bear on the launch course take part in the spread.
    double rel = wrap180(bearingTo(s.pos, p) - s.heading);
    int bearing = 0;
    for (auto& tm : s.torpedoMounts)
        if (tm.operational() && tm.loaded > 0 && sim::inArc(rel, tm.def->arcMin - 20.0, tm.def->arcMax + 20.0, tm.def->mirrored))
            bearing += tm.loaded;
    if (bearing == 0) return;
    int n = std::min(bearing, attackRun ? ready : d.torpedoesPerSpread);
    double tlen = 140.0;
    if (const auto* cl = view_.db().shipClass(tr->classGuess)) tlen = cl->length;
    else if (tr->size == sim::SizeClass::Small) tlen = 105.0;
    else if (tr->size == sim::SizeClass::Large) tlen = 180.0;
    double crossing = length(tr->vel) > 1.0 ? std::fabs(std::sin((tr->courseEstimate() - bearingTo(s.pos, p)) * kDegToRad)) : 1.0;
    double spacing = d.torpedoSpacingLengths * tlen * std::max(0.4, crossing);
    double spread = clamp((n - 1) * spacing / std::max(1000.0, dist) * kRadToDeg, 1.0, 14.0);
    int setting = torpedoSetting(s, dist * 1.3);
    double course = bearingTo(s.pos, p);
    if (friendlyInTorpedoPath(s, course, spread, setting)) {
        if (!c.torpedoedTracks.count(best)) record(DecisionLevel::Captain, s.name, "ai.d.torpedo_hold_friendly", "", {s.name});
        c.torpedoedTracks[best] = now - 240.0;  // re-check in a minute
        return;
    }
    std::string why;
    int fired = view_.fireTorpedoes(s, best, n, spread, setting, &why);
    if (fired == 0 && attackRun) {
        c.torpedoedTracks[best] = now - 270.0;  // retry in half a minute
        record(DecisionLevel::Captain, s.name, "ai.d.torpedo_blocked", why, {s.name});
    }
    if (fired > 0) {
        c.torpedoedTracks[best] = now;
        c.lastLaunch = now;
        if (g) g->launches++;
        record(DecisionLevel::Captain, s.name, "ai.d.torpedoes_launched", formatString("%d|%.1f|%.0f", fired, spread, dist),
               {s.name, std::to_string(fired), fmt1(dist / 1000.0)});
    }
}

}  // namespace it::ai
