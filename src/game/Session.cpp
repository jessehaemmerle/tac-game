#include "game/Session.h"

#include "core/FileSystem.h"
#include "core/Log.h"

namespace it::game {

Session::Session(const sim::Database& db, const ai::DoctrineLibrary& lib) : db_(db), lib_(lib) {}

bool Session::start(const Scenario& sc, const SessionOptions& opt, std::string* error) {
    scenario_ = sc;
    options_ = opt;
    world_ = std::make_unique<sim::World>(db_, opt.seed);
    sim::World& w = *world_;
    if (!sc.terrain.empty()) {
        if (!w.terrain().load(fs::dataPath(sc.terrain).string())) {
            if (error) *error = "terrain " + sc.terrain;
            return false;
        }
    }
    double lat = w.terrain().info().centerLat != 0 ? w.terrain().info().centerLat : sc.lat;
    double lon = w.terrain().info().centerLon != 0 ? w.terrain().info().centerLon : sc.lon;
    w.env().configure(lat, lon, sc.date, sc.weather, {0, 0}, 40000.0, w.rng());

    groupMap_.assign(sc.groups.size(), -1);
    for (size_t i = 0; i < sc.groups.size(); ++i)
        if (sc.groups[i].appearTime <= 0) spawnGroup(i);

    for (int si = 0; si < 2; ++si) {
        sim::Side side = sim::Side(si);
        // Command & control quality of the side (contact reports) from its first group's doctrine.
        for (auto& g : sc.groups)
            if (g.side == side) {
                sim::Nation nat = sim::Nation::Unknown;
                if (!g.ships.empty())
                    if (auto* c = db_.shipClass(g.ships.front().cls)) nat = c->nation;
                const ai::Doctrine& d = lib_.get(g.doctrine, nat);
                w.picture(side).setReporting(d.reportDelayMin, d.reportDelayMax, d.reportReliability, &w.rng());
                break;
            }
        ai::AiSettings as;
        bool isPlayer = side == opt.player;
        as.assistMode = isPlayer;
        as.autoEvade = opt.autoEvade;
        as.skill = isPlayer ? 0.7 : opt.aiSkill;
        ai_[size_t(si)] = std::make_unique<ai::AiController>(w, side, lib_, as);
        for (size_t i = 0; i < sc.groups.size(); ++i)
            if (sc.groups[i].side == side && groupMap_[i] >= 0)
                ai_[size_t(si)]->setMission(groupMap_[i], sc.groups[i].mission, sc.groups[i].doctrine);
    }
    finished_ = false;
    endReason_.clear();
    accumulator_ = 0;
    quietSince_ = 0;
    return true;
}

void Session::spawnGroup(size_t index) {
    const ScenarioGroup& sg = scenario_.groups[index];
    sim::World& w = *world_;
    sim::Group& g = w.addGroup(sg.name, sg.side, sg.formation, sg.spacing);
    g.course = sg.course;
    g.speedKn = sg.speedKn;
    g.doctrine = sg.doctrine;
    g.playerControlled = sg.side == options_.player && sg.playable;
    int gid = g.id;
    groupMap_[index] = gid;
    Vec2 back = bearingVector(sg.course + 180.0);
    Vec2 right = bearingVector(sg.course + 90.0);
    int k = 0;
    for (auto& ss : sg.ships) {
        const sim::ShipClassDef* cls = db_.shipClass(ss.cls);
        if (!cls) {
            IT_LOG_WARN("Scenario %s: unknown class %s", scenario_.id.c_str(), ss.cls.c_str());
            continue;
        }
        sim::CrewQuality crew = ss.crew.value_or(sg.crew);
        if (sg.side != options_.player && options_.player != sim::Side::Neutral)
            crew = sim::CrewQuality(clamp(int(crew) + options_.enemyCrewShift, 0, 3));
        // Initial stations according to formation.
        Vec2 pos = sg.pos;
        switch (sg.formation) {
            case sim::Formation::LineAbreast: pos += right * ((k % 2 ? 1.0 : -1.0) * ((k + 1) / 2) * sg.spacing); break;
            case sim::Formation::Screen:
                if (k > 0) pos += bearingVector(sg.course - 70.0 + 140.0 * (k - 1) / std::max<size_t>(1, sg.ships.size() - 2)) * 2500.0;
                break;
            default: pos += back * (k * sg.spacing); break;
        }
        sim::Ship& s = w.spawnShip(*cls, ss.name, sg.side, pos, sg.course, sg.speedKn, crew);
        s.groupId = gid;
        s.readiness = sg.readiness;
        if (sg.readiness >= 1.0) s.alertedAt = 0;
        if (!ss.fcRadar)
            for (auto& d : s.directors) d.radarFitted = false;
        if (!ss.sensors.empty()) {
            s.sensors.clear();
            // Period refits: the session owns the replacement sensor mount definitions.
            for (auto& sid : ss.sensors) {
                const sim::SensorDef* def = db_.sensor(sid);
                if (!def) continue;
                auto m = std::make_unique<sim::SensorMountDef>();
                m->sensor = sid;
                m->def = def;
                m->height = def->kind == sim::SensorKind::Optical ? cls->lookoutHeight : cls->mastHeight * 0.9;
                m->x = cls->length * 0.15;
                sim::SensorState st;
                st.def = m.get();
                st.nextSweep = w.rng().uniform(0.0, 4.0);
                s.sensors.push_back(st);
                sensorPool_.push_back(std::move(m));
            }
        }
        w.group(gid)->members.push_back(s.id);
        ++k;
    }
}

int Session::worldGroup(const std::string& id) const {
    for (size_t i = 0; i < scenario_.groups.size(); ++i)
        if (scenario_.groups[i].id == id) return groupMap_[i];
    return -1;
}

std::vector<sim::Event> Session::drainEvents() {
    std::vector<sim::Event> out;
    out.swap(pending_);
    return out;
}

int Session::update(double realDt) {
    if (paused || finished_) return 0;
    accumulator_ += std::min(realDt, 0.25) * timeScale;
    int steps = 0;
    while (accumulator_ >= sim::kStep && steps < 2000) {
        accumulator_ -= sim::kStep;
        step();
        ++steps;
        if (finished_) break;
    }
    return steps;
}

void Session::step() {
    sim::World& w = *world_;
    // Reinforcements.
    for (size_t i = 0; i < scenario_.groups.size(); ++i)
        if (groupMap_[i] < 0 && w.time() >= scenario_.groups[i].appearTime) {
            spawnGroup(i);
            if (auto* a = ai(scenario_.groups[i].side)) a->setMission(groupMap_[i], scenario_.groups[i].mission, scenario_.groups[i].doctrine);
        }
    w.step();
    auto events = w.takeEvents();
    for (auto& e : events) {
        for (auto& a : ai_)
            if (a) a->observe(e);
    }
    for (auto& a : ai_)
        if (a) a->update();
    if (pending_.size() < 20000) pending_.insert(pending_.end(), events.begin(), events.end());
    if (w.tick() % 20 == 0) checkEnd();
}

void Session::checkEnd() {
    sim::World& w = *world_;
    if (w.time() >= scenario_.timeLimit) {
        finished_ = true;
        endReason_ = "end.time_limit";
        return;
    }
    for (int si = 0; si < 2; ++si) {
        sim::Side s = sim::Side(si);
        bool anyCombatant = false, anyShip = false;
        for (auto& sh : w.ships())
            if (sh->side == s && sh->operational()) {
                anyShip = true;
                if (sim::isCombatant(sh->cls->type)) anyCombatant = true;
            }
        if (!anyShip || (!anyCombatant && s == sim::Side::Axis)) {
            finished_ = true;
            endReason_ = s == sim::Side::Allies ? "end.allies_destroyed" : "end.axis_destroyed";
            return;
        }
    }
    // Both sides disengaged: no live contacts for twenty minutes after the first half hour.
    bool anyContact = false;
    for (int si = 0; si < 2; ++si)
        for (auto& t : w.picture(sim::Side(si)).tracks())
            if (!t.lost) anyContact = true;
    if (anyContact || w.time() < 1800.0) {
        quietSince_ = w.time();
    } else if (w.time() - quietSince_ > 1200.0) {
        finished_ = true;
        endReason_ = "end.disengaged";
    }
}

SideResult Session::result(sim::Side s) const {
    SideResult r;
    const sim::World& w = *world_;
    double total = 0;
    for (auto& sh : w.ships()) {
        double t = sh->displacement();
        if (sh->side == s) {
            total += t;
            if (sh->dmg.sinking || sh->dmg.sunk) {
                r.shipsLost++;
                r.tonnageLost += t;
            } else {
                r.damageTaken += t * (1.0 - sh->flotation());
            }
        } else if (sh->side == sim::enemyOf(s) && (sh->dmg.sinking || sh->dmg.sunk)) {
            r.shipsSunkOfEnemy++;
            r.tonnageSunk += t;
        }
    }
    if (total > 0) r.damageTaken /= total;
    r.points = r.tonnageSunk / 100.0 - r.tonnageLost / 100.0;
    for (auto& o : scenario_.objectives) {
        if (o.side != s) continue;
        bool met = false;
        switch (o.type) {
            case ObjectiveType::DestroyTonnage: met = r.tonnageSunk >= o.value; break;
            case ObjectiveType::Preserve: met = total > 0 && r.tonnageLost / total <= 1.0 - o.value; break;
            case ObjectiveType::DestroyGroup:
            case ObjectiveType::ProtectGroup: {
                int gid = worldGroup(o.group);
                const sim::Group* g = gid >= 0 ? w.group(gid) : nullptr;
                int alive = 0, all = 0;
                if (g)
                    for (auto id : g->members)
                        if (const sim::Ship* sh = w.ship(id)) {
                            ++all;
                            if (sh->operational()) ++alive;
                        }
                double frac = all ? double(alive) / all : 0.0;
                met = o.type == ObjectiveType::DestroyGroup ? frac <= 1.0 - o.value : frac >= o.value;
                break;
            }
            case ObjectiveType::ExitArea:
            case ObjectiveType::Survive: {
                for (auto& sh : w.ships())
                    if (sh->side == s && sh->operational() && (o.type == ObjectiveType::Survive || distance(sh->pos, o.area) < o.radius))
                        met = true;
                break;
            }
        }
        (met ? r.objectivesMet : r.objectivesFailed).push_back(o.text);
        if (met) r.points += o.points;
    }
    return r;
}

// ------------------------------------------------------------------------------------- commands
sim::Ship* Session::playerShip(sim::EntityId id) {
    sim::Ship* s = world_->ship(id);
    return (s && s->side == options_.player && s->operational()) ? s : nullptr;
}

void Session::orderGroupCourse(int group, double course, double speedKn) {
    if (sim::Group* g = world_->group(group); g && g->side == options_.player) {
        g->course = wrap360(course);
        g->speedKn = std::max(0.0, speedKn);
    }
}

void Session::orderGroupFormation(int group, sim::Formation f, double spacing) {
    if (sim::Group* g = world_->group(group); g && g->side == options_.player) {
        g->formation = f;
        if (spacing > 0) g->spacing = spacing;
    }
}

void Session::orderShipCourse(sim::EntityId id, double course, double speedKn) {
    if (sim::Ship* s = playerShip(id)) {
        s->orders.playerSteering = true;
        s->orders.course = wrap360(course);
        s->orders.speedKn = clamp(speedKn, -8.0, s->cls->maxSpeedKn);
    }
}

void Session::rejoinGroup(sim::EntityId id) {
    if (sim::Ship* s = playerShip(id)) s->orders.playerSteering = false;
}

void Session::orderTarget(sim::EntityId id, uint32_t track) {
    if (sim::Ship* s = playerShip(id)) {
        s->orders.playerTarget = track;
        world_->setDirectorTarget(*s, 0, track);
    }
}

void Session::orderWeapons(sim::EntityId id, sim::WeaponsStatus ws) {
    if (sim::Ship* s = playerShip(id)) s->orders.weapons = ws;
}

int Session::orderTorpedoes(sim::EntityId id, uint32_t track, int count, double spreadDeg, std::string* why) {
    sim::Ship* s = playerShip(id);
    if (!s) return 0;
    // Pick the fastest setting that reaches the target with margin.
    int setting = 0;
    if (const sim::Track* t = world_->picture(s->side).find(track))
        for (auto& tm : s->torpedoMounts)
            if (tm.def->torpedo) {
                double run = distance(s->pos, t->predict(world_->time())) * 1.3;
                const auto& st = tm.def->torpedo->settings;
                setting = int(st.size()) - 1;
                for (size_t i = 0; i < st.size(); ++i)
                    if (st[i].range >= run) {
                        setting = int(i);
                        break;
                    }
                break;
            }
    return world_->fireTorpedoes(*s, track, count, spreadDeg, setting, why);
}

void Session::orderSearchlight(sim::EntityId id, bool on, uint32_t track) {
    if (sim::Ship* s = playerShip(id)) world_->setSearchlight(*s, on, track);
}

void Session::orderStarshells(sim::EntityId id, bool on) {
    if (sim::Ship* s = playerShip(id))
        for (auto& d : s->directors)
            if (d.def->role == "secondary") {
                d.illuminate = on;
                if (on && s->directors[0].targetTrack) d.targetTrack = s->directors[0].targetTrack;
            }
}

bool Session::orderFloatplane(sim::EntityId id, bool flares, const Vec2& target) {
    sim::Ship* s = playerShip(id);
    return s && world_->launchAircraft(*s, flares ? sim::AircraftMission::Kind::Flares : sim::AircraftMission::Kind::Recon, target);
}

}  // namespace it::game
