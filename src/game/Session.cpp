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
    airBaseMap_.clear();
    for (const auto& b : sc.airBases) {
        sim::AirBase wb;
        wb.name = b.name;
        wb.side = b.side;
        wb.pos = b.pos;
        wb.turnaround = b.turnaround;
        wb.launchInterval = b.launchInterval;
        airBaseMap_.push_back(w.addAirBase(wb));
    }
    flightMap_.assign(sc.airGroups.size(), -1);
    for (size_t i = 0; i < sc.airGroups.size(); ++i)
        if (sc.airGroups[i].appearTime <= 0) spawnAirGroup(i);

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
    objectiveTime_.assign(sc.objectives.size(), 0.0);
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
        if (!ss.torpedoReloads)
            for (auto& tm : s.torpedoMounts) tm.reloads = 0;
        s.dmg.propulsion = ss.propulsion;
        s.dmg.power = ss.power;
        s.dmg.flooding = ss.flooding;
        if (ss.propulsion < 0.08) s.speed = 0.0;  // dead in the water
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
        if (sg.knownToEnemy)
            w.picture(sg.side == sim::Side::Allies ? sim::Side::Axis : sim::Side::Allies).addPriorIntel(s.id, cls->type, cls->size(), cls->id);
        w.group(gid)->members.push_back(s.id);
        ++k;
    }
}

void Session::spawnAirGroup(size_t index) {
    const ScenarioAirGroup& ag = scenario_.airGroups[index];
    sim::World& w = *world_;
    const sim::AircraftDef* def = db_.aircraft(ag.aircraft);
    if (!def) {
        IT_LOG_WARN("Scenario %s: unknown aircraft %s", scenario_.id.c_str(), ag.aircraft.c_str());
        flightMap_[index] = -2;
        return;
    }
    int base = -1;
    for (size_t k = 0; k < scenario_.airBases.size(); ++k)
        if (scenario_.airBases[k].id == ag.base) base = airBaseMap_[k];
    if (base < 0) {
        // No base given: an improvised one under the flight's position (it lands there).
        sim::AirBase b;
        b.name = ag.base.empty() ? ag.name : ag.base;
        b.side = ag.side;
        b.pos = ag.airborne ? ag.pos : Vec2{};
        base = w.addAirBase(b);
    }
    sim::CrewQuality crew = ag.crew;
    if (ag.side != options_.player && options_.player != sim::Side::Neutral)
        crew = sim::CrewQuality(clamp(int(crew) + options_.enemyCrewShift, 0, 3));
    sim::Flight& f = w.addFlight(ag.name, ag.side, *def, ag.loadout, ag.count, base, crew);
    int fid = f.id;
    f.playerControlled = ag.side == options_.player && ag.playable;
    f.aiControlled = !f.playerControlled;
    flightMap_[index] = fid;
    sim::AirOrder o = ag.order;
    if (!ag.coverGroup.empty()) o.group = worldGroup(ag.coverGroup);
    if (!ag.escortFlight.empty()) o.escort = worldFlight(ag.escortFlight);
    if (ag.airborne) {
        f.order = ag.hasOrder ? o : sim::AirOrder{};
        w.putAirborne(f, ag.pos, ag.altitude > 0 ? ag.altitude : sim::air::cruiseAltitude(f), ag.heading);
        if (ag.hasOrder) w.orderFlight(fid, o);
    } else if (ag.hasOrder && o.startTime >= 0) {
        w.flight(fid)->order = o;
    }
}

int Session::worldFlight(const std::string& id) const {
    for (size_t i = 0; i < scenario_.airGroups.size() && i < flightMap_.size(); ++i)
        if (scenario_.airGroups[i].id == id) return flightMap_[i];
    return -1;
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
    for (size_t i = 0; i < scenario_.airGroups.size() && i < flightMap_.size(); ++i)
        if (flightMap_[i] == -1 && w.time() >= scenario_.airGroups[i].appearTime) spawnAirGroup(i);
    if (w.tick() % 10 == 0) steerRoutes();
    w.step();
    auto events = w.takeEvents();
    for (auto& e : events) {
        for (auto& a : ai_)
            if (a) a->observe(e);
    }
    for (auto& a : ai_)
        if (a) a->update();
    if (pending_.size() < 20000) pending_.insert(pending_.end(), events.begin(), events.end());
    if (w.tick() % 20 == 0) {
        trackObjectives(20 * sim::kStep);
        checkEnd();
    }
}

void Session::reportRouteDone(sim::EntityId ship, const std::string& name) {
    sim::Event e;
    e.type = sim::EventType::Report;
    e.time = world_->time();
    e.a = ship;
    e.side = options_.player;
    e.text = "report.route_done";
    e.args = {name};
    pending_.push_back(std::move(e));
}

void Session::steerRoutes() {
    sim::World& w = *world_;
    // A waypoint counts as reached inside half a minute's steaming (and never closer than 400 m).
    auto next = [](std::vector<Vec2>& route, const Vec2& pos, double speedMs) -> const Vec2* {
        while (!route.empty() && distance(pos, route.front()) < std::max(400.0, speedMs * 30.0)) route.erase(route.begin());
        return route.empty() ? nullptr : &route.front();
    };
    for (auto& g : w.groups()) {
        if (g.route.empty() || !g.playerControlled) continue;
        sim::Ship* guide = w.guideOf(g);
        if (!guide) {
            g.route.clear();
            continue;
        }
        if (const Vec2* wp = next(g.route, guide->pos, guide->speed)) g.course = bearingTo(guide->pos, *wp);
        else reportRouteDone(guide->id, g.name);
    }
    for (auto& sp : w.ships()) {
        sim::Ship& s = *sp;
        if (s.orders.route.empty() || !s.orders.playerSteering || !s.operational()) continue;
        if (const Vec2* wp = next(s.orders.route, s.pos, s.speed)) s.orders.course = bearingTo(s.pos, *wp);
        else reportRouteDone(s.id, s.name);
    }
}

void Session::trackObjectives(double dt) {
    const sim::World& w = *world_;
    for (size_t i = 0; i < scenario_.objectives.size() && i < objectiveTime_.size(); ++i) {
        const Objective& o = scenario_.objectives[i];
        if (o.type != ObjectiveType::Bombard && o.type != ObjectiveType::PreventBombard) continue;
        int gid = worldGroup(o.group);
        const sim::Group* g = gid >= 0 ? w.group(gid) : nullptr;
        if (!g) continue;
        // A bombardment needs a ship with guns in action inside the area.
        for (auto id : g->members)
            if (const sim::Ship* sh = w.ship(id); sh && sh->operational() && !sh->mounts.empty() && distance(sh->pos, o.area) < o.radius) {
                objectiveTime_[i] += dt;
                break;
            }
    }
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
        bool anyCombatant = false, anyShip = false, anyAir = false, hadShips = false;
        for (auto& sh : w.ships())
            if (sh->side == s) {
                hadShips = true;
                if (!sh->operational()) continue;
                anyShip = true;
                if (sim::isCombatant(sh->cls->type)) anyCombatant = true;
            }
        // A side fighting with aircraft alone (the bombers that found Force Z) is beaten when they are gone.
        for (auto& f : w.flights())
            if (f.side == s && f.state != sim::FlightState::Lost && f.alive(w) > 0) anyAir = true;
        bool pending = false;
        for (size_t i = 0; i < flightMap_.size(); ++i)
            if (flightMap_[i] == -1 && scenario_.airGroups[i].side == s) pending = true;
        for (size_t i = 0; i < groupMap_.size(); ++i)
            if (groupMap_[i] < 0 && scenario_.groups[i].side == s) pending = true;
        if (pending) continue;
        bool beaten = hadShips ? (!anyShip || (!anyCombatant && s == sim::Side::Axis)) : !anyAir;
        if (beaten) {
            finished_ = true;
            endReason_ = s == sim::Side::Allies ? "end.allies_destroyed" : "end.axis_destroyed";
            return;
        }
    }
    // Both sides disengaged: no live contacts for twenty minutes after the first half hour.
    bool anyContact = false;
    for (int si = 0; si < 2; ++si) {
        for (auto& t : w.picture(sim::Side(si)).tracks())
            if (!t.lost) anyContact = true;
        for (auto& t : w.picture(sim::Side(si)).airTracks())
            if (!t.lost) anyContact = true;
    }
    // Strikes on their way or waiting to launch keep the battle open.
    for (auto& f : w.flights())
        if ((f.airborne() && f.state != sim::FlightState::Returning) || (f.state == sim::FlightState::Ready && f.order.startTime >= 0 &&
                                                                         f.order.type != sim::AirOrderType::None))
            anyContact = true;
    for (size_t i = 0; i < flightMap_.size(); ++i)
        if (flightMap_[i] == -1) anyContact = true;
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
    for (const auto& a : w.planes()) {
        bool down = a.fate == sim::Aircraft::Fate::Falling || a.fate == sim::Aircraft::Fate::Destroyed || a.fate == sim::Aircraft::Fate::Ditched;
        if (!down) continue;
        if (a.side == s) r.aircraftLost++;
        else if (a.side == sim::enemyOf(s)) r.aircraftDowned++;
    }
    r.points = r.tonnageSunk / 100.0 - r.tonnageLost / 100.0 + 3.0 * (r.aircraftDowned - r.aircraftLost);
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
            case ObjectiveType::Bombard:
            case ObjectiveType::PreventBombard: {
                size_t i = size_t(&o - scenario_.objectives.data());
                double t = i < objectiveTime_.size() ? objectiveTime_[i] : 0.0;
                met = o.type == ObjectiveType::Bombard ? t >= o.value : t < o.value;
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

void Session::acknowledge(sim::EntityId ship, const char* reply) {
    sim::Event e;
    e.type = sim::EventType::OrderAcknowledged;
    e.time = world_->time();
    e.a = ship;
    e.side = options_.player;
    e.text = reply;
    pending_.push_back(std::move(e));
}

namespace {
// Speed orders ring the engine telegraph, course orders are repeated by the helmsman.
const char* helmReply(double oldCourse, double oldSpeed, double course, double speed) {
    if (std::abs(speed - oldSpeed) >= 0.5) return "engines";
    if (std::abs(wrap180(course - oldCourse)) >= 1.0) return "course_steady";
    return nullptr;
}
}  // namespace

sim::Group* Session::playerGroup(int group) {
    sim::Group* g = world_->group(group);
    return g && g->side == options_.player ? g : nullptr;
}

void Session::orderGroupCourse(int group, double course, double speedKn) {
    if (sim::Group* g = playerGroup(group)) {
        const char* reply = helmReply(g->course, g->speedKn, wrap360(course), std::max(0.0, speedKn));
        if (std::abs(wrap180(course - g->course)) >= 1.0) g->route.clear();  // a new course replaces the route
        g->course = wrap360(course);
        g->speedKn = std::max(0.0, speedKn);
        if (reply && !g->members.empty()) acknowledge(g->members.front(), reply);
    }
}

void Session::orderGroupFormation(int group, sim::Formation f, double spacing) {
    if (sim::Group* g = playerGroup(group)) {
        if (g->formation != f && !g->members.empty()) acknowledge(g->members.front(), "aye");
        g->formation = f;
        if (spacing > 0) g->spacing = spacing;
    }
}

void Session::orderShipCourse(sim::EntityId id, double course, double speedKn) {
    if (sim::Ship* s = playerShip(id)) {
        double speed = clamp(speedKn, -8.0, s->cls->maxSpeedKn);
        const char* reply = s->orders.playerSteering ? helmReply(s->orders.course, s->orders.speedKn, wrap360(course), speed) : "aye";
        if (!s->orders.playerSteering || std::abs(wrap180(course - s->orders.course)) >= 1.0) s->orders.route.clear();
        s->orders.playerSteering = true;
        s->orders.course = wrap360(course);
        s->orders.speedKn = speed;
        if (reply) acknowledge(id, reply);
    }
}

void Session::rejoinGroup(sim::EntityId id) {
    if (sim::Ship* s = playerShip(id)) {
        if (s->orders.playerSteering) acknowledge(id, "aye");
        s->orders.playerSteering = false;
        s->orders.route.clear();
    }
}

void Session::orderGroupRoute(int group, const std::vector<Vec2>& points, bool append) {
    sim::Group* g = playerGroup(group);
    if (!g || !g->playerControlled) return;
    if (!append) g->route.clear();
    g->route.insert(g->route.end(), points.begin(), points.end());
    if (sim::Ship* guide = world_->guideOf(*g); guide && !g->route.empty()) {
        g->course = bearingTo(guide->pos, g->route.front());
        acknowledge(guide->id, "course_steady");
    }
}

void Session::orderShipRoute(sim::EntityId id, const std::vector<Vec2>& points, bool append) {
    sim::Ship* s = playerShip(id);
    if (!s) return;
    if (points.empty()) {
        if (!append) s->orders.route.clear();
        return;
    }
    if (!s->orders.playerSteering) {
        // Leaving the formation keeps the present speed.
        s->orders.playerSteering = true;
        s->orders.speedKn = s->speedKn();
        s->orders.route.clear();
    }
    if (!append) s->orders.route.clear();
    s->orders.route.insert(s->orders.route.end(), points.begin(), points.end());
    if (!s->orders.route.empty()) {
        s->orders.course = bearingTo(s->pos, s->orders.route.front());
        acknowledge(id, "course_steady");
    }
}

void Session::delegateGroup(int group, bool toAi) {
    sim::Group* g = playerGroup(group);
    if (!g) return;
    bool playable = false;
    for (size_t i = 0; i < scenario_.groups.size() && i < groupMap_.size(); ++i)
        if (groupMap_[i] == group) playable = scenario_.groups[i].playable;
    if (!playable) return;
    if (g->playerControlled == !toAi) return;
    g->playerControlled = !toAi;
    g->aiDelegated = toAi;
    g->route.clear();
    if (toAi)
        for (auto id : g->members)
            if (sim::Ship* s = world_->ship(id)) {
                s->orders.playerSteering = false;
                s->orders.route.clear();
            }
    if (!g->members.empty()) acknowledge(g->members.front(), "aye");
}

void Session::orderTarget(sim::EntityId id, uint32_t track) {
    if (sim::Ship* s = playerShip(id)) {
        if (s->orders.playerTarget != track) acknowledge(id, "aye");
        s->orders.playerTarget = track;
        world_->setDirectorTarget(*s, 0, track);
    }
}

void Session::orderWeapons(sim::EntityId id, sim::WeaponsStatus ws) {
    if (sim::Ship* s = playerShip(id)) {
        if (s->orders.weapons != ws)
            acknowledge(id, ws == sim::WeaponsStatus::Free ? "open_fire" : ws == sim::WeaponsStatus::Hold ? "cease_fire" : "aye");
        s->orders.weapons = ws;
    }
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
    if (sim::Ship* s = playerShip(id)) {
        if (s->orders.searchlight != on) acknowledge(id, "aye");
        world_->setSearchlight(*s, on, track);
    }
}

void Session::orderStarshells(sim::EntityId id, bool on) {
    if (sim::Ship* s = playerShip(id)) {
        acknowledge(id, "aye");
        for (auto& d : s->directors)
            if (d.def->role == "secondary") {
                d.illuminate = on;
                if (on && s->directors[0].targetTrack) d.targetTrack = s->directors[0].targetTrack;
            }
    }
}

bool Session::orderFloatplane(sim::EntityId id, bool flares, const Vec2& target) {
    sim::Ship* s = playerShip(id);
    if (!s || !world_->launchAircraft(*s, flares, target)) return false;
    // The floatplane the player sent off stays under the player's orders.
    for (auto& f : world_->flights())
        if (const sim::AirBase* b = world_->airBase(f.base); b && b->ship == id && f.state == sim::FlightState::Launching) {
            f.playerControlled = true;
            f.aiControlled = false;
        }
    acknowledge(id, "aye");
    return true;
}

sim::Flight* Session::playerFlight(int id) {
    sim::Flight* f = world_->flight(id);
    return (f && f->side == options_.player && f->playerControlled) ? f : nullptr;
}

bool Session::orderFlight(int id, const sim::AirOrder& o, std::string* why) {
    sim::Flight* f = playerFlight(id);
    if (!f) {
        if (why) *why = "air.why.not_yours";
        return false;
    }
    return world_->orderFlight(id, o, why);
}

void Session::commandFlight(int id, bool player) {
    sim::Flight* f = world_->flight(id);
    if (!f || f->side != options_.player || options_.player == sim::Side::Neutral) return;
    f->playerControlled = player;
    f->aiControlled = !player;
}

}  // namespace it::game
