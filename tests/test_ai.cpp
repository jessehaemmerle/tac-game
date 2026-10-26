#include <doctest/doctest.h>

#include "ai/AiController.h"
#include "core/FileSystem.h"
#include "game/Session.h"

#include <tuple>

using namespace it;
using namespace it::sim;

namespace {
const Database& db() {
    static Database d;
    static bool ok = d.loadFromDirectory(fs::dataPath("db").string());
    REQUIRE(ok);
    return d;
}
const ai::DoctrineLibrary& doctrines() {
    static ai::DoctrineLibrary lib;
    static bool ok = lib.load(fs::dataPath("doctrine").string());
    REQUIRE(ok);
    return lib;
}

struct Battle {
    World w;
    explicit Battle(double hour, uint64_t seed = 3) : w(db(), seed) {
        DateTime dt;
        dt.hour = hour;
        WeatherConfig wx;
        wx.cloudCover = 0.3;
        w.env().configure(-9.1, 159.8, dt, wx, {0, 0}, 50000, w.rng());
    }
    int group(const char* name, Side side, std::initializer_list<const char*> classes, Vec2 pos, double course, double kn,
                 CrewQuality crew = CrewQuality::Regular) {
        Group& g = w.addGroup(name, side, Formation::Column, 900);
        int gid = g.id;
        int k = 0;
        for (auto* c : classes) {
            Ship& s = w.spawnShip(*db().shipClass(c), std::string(name) + std::to_string(k), side,
                                  pos - bearingVector(course) * (900.0 * k), course, kn, crew);
            s.groupId = gid;
            w.group(gid)->members.push_back(s.id);
            ++k;
        }
        w.group(gid)->course = course;
        w.group(gid)->speedKn = kn;
        return gid;
    }
    void run(std::vector<ai::AiController*> ais, double seconds) {
        for (int i = 0; i < int(seconds / kStep); ++i) {
            w.step();
            for (auto& e : w.takeEvents())
                for (auto* a : ais) a->observe(e);
            for (auto* a : ais) a->update();
        }
    }
};

bool decided(const ai::DecisionLog& log, const std::string& key) {
    for (auto& r : log.records())
        if (r.decision == key) return true;
    return false;
}
}  // namespace

TEST_CASE("fairness: the AI does not react to enemies it has not detected") {
    Battle b(1.5);
    int ijn = b.group("ijn", Side::Axis, {"ijn_ca_takao", "ijn_ca_aoba"}, {0, 0}, 90, 20, CrewQuality::Elite);
    b.group("usn", Side::Allies, {"usn_ca_new_orleans"}, {0, 40000}, 90, 10);  // far beyond any sensor
    ai::AiController axis(b.w, Side::Axis, doctrines());
    ai::Mission m;
    m.type = ai::MissionType::Patrol;
    m.speedKn = 20;
    axis.setMission(ijn, m, "ijn_1942");
    b.run({&axis}, 240);
    CHECK(b.w.picture(Side::Axis).tracks().empty());
    CHECK(axis.brain(ijn)->posture == ai::Posture::Mission);
    for (auto& s : b.w.ships())
        if (s->side == Side::Axis)
            for (auto& d : s->directors) CHECK(d.targetTrack == 0u);
}

TEST_CASE("IJN doctrine: torpedo attack first, guns held until the torpedoes run") {
    Battle b(1.5, 11);
    int ijn = b.group("ijn", Side::Axis, {"ijn_dd_kagero", "ijn_dd_kagero", "ijn_dd_kagero"}, {0, 0}, 90, 30, CrewQuality::Elite);
    int usn = b.group("usn", Side::Allies, {"usn_ca_northampton", "usn_ca_new_orleans"}, {11000, -5000}, 0, 15);
    ai::AiController axis(b.w, Side::Axis, doctrines());
    ai::AiController allies(b.w, Side::Allies, doctrines());
    ai::Mission m;
    m.type = ai::MissionType::Attack;
    m.waypoints = {{30000, 0}};
    m.speedKn = 30;
    m.attackSpeedKn = 33;
    axis.setMission(ijn, m, "ijn_1942");
    ai::Mission pm;
    pm.type = ai::MissionType::Patrol;
    pm.speedKn = 15;
    allies.setMission(usn, pm, "usn_1942");
    int firedGunsBeforeTorpedoes = 0;
    bool torpedoesAway = false;
    for (int i = 0; i < int(1500 / kStep); ++i) {
        b.w.step();
        for (auto& e : b.w.takeEvents()) {
            if (e.type == EventType::TorpedoLaunched && e.side == Side::Axis) torpedoesAway = true;
            if (e.type == EventType::GunFired && e.side == Side::Axis && !torpedoesAway) ++firedGunsBeforeTorpedoes;
            axis.observe(e);
            allies.observe(e);
        }
        axis.update();
        allies.update();
    }
    CHECK(decided(axis.log(), "ai.o.torpedo_attack"));
    CHECK(torpedoesAway);
    CHECK(firedGunsBeforeTorpedoes == 0);
}

TEST_CASE("captains comb torpedo tracks they have been warned about") {
    Battle b(10.0, 5);
    int g = b.group("usn", Side::Allies, {"usn_dd_fletcher"}, {0, 0}, 0, 20, CrewQuality::Veteran);
    ai::AiController allies(b.w, Side::Allies, doctrines());
    ai::Mission m;
    m.type = ai::MissionType::Patrol;
    m.waypoints = {{0, 20000}};
    m.speedKn = 20;
    allies.setMission(g, m, "usn_1942_late");
    b.run({&allies}, 10);
    Ship* s = b.w.ship(b.w.group(g)->members[0]);
    // A torpedo track sighted on the starboard beam, heading straight for us.
    Event e;
    e.type = EventType::TorpedoSighted;
    e.time = b.w.time();
    e.a = s->id;
    e.b = 999;
    e.side = Side::Allies;
    e.pos = {s->pos + Vec2{1500, 400}, 0};
    e.value = 270.0;  // heading west
    e.value2 = 24.0;
    allies.observe(e);
    b.run({&allies}, 40);
    bool evaded = decided(allies.log(), "ai.d.comb_torpedo") || decided(allies.log(), "ai.d.evade_torpedo");
    CHECK(evaded);
    // Combing means steering roughly parallel (090 or 270) to the track.
    double off = std::min(std::fabs(angleDiff(s->orders.course, 90.0)), std::fabs(angleDiff(s->orders.course, 270.0)));
    CHECK(off < 50.0);
}

TEST_CASE("a lone destroyer withdraws from an identified cruiser force in daylight") {
    Battle b(10.0, 9);
    int dd = b.group("usn", Side::Allies, {"usn_dd_bagley"}, {0, 0}, 0, 20);
    b.group("ijn", Side::Axis, {"ijn_ca_takao", "ijn_ca_aoba", "ijn_ca_furutaka"}, {0, 14000}, 180, 25, CrewQuality::Elite);
    ai::AiController allies(b.w, Side::Allies, doctrines());
    ai::Mission m;
    m.type = ai::MissionType::Patrol;
    m.waypoints = {{0, 30000}};
    m.speedKn = 20;
    allies.setMission(dd, m, "usn_1942");
    b.run({&allies}, 300);
    CHECK(decided(allies.log(), "ai.o.withdraw"));
}

TEST_CASE("integration: Savo Island AI vs AI produces a battle and stays deterministic") {
    game::Scenario sc;
    REQUIRE(game::loadScenario(fs::dataPath("scenarios/savo_island.json").string(), sc));
    auto run = [&](uint64_t seed) {
        game::Session s(db(), doctrines());
        game::SessionOptions o;
        o.seed = seed;
        REQUIRE(s.start(sc, o));
        while (!s.finished() && s.world().time() < 5400.0) s.step();
        auto a = s.result(Side::Allies), x = s.result(Side::Axis);
        int decisions = int(s.ai(Side::Axis)->log().records().size() + s.ai(Side::Allies)->log().records().size());
        return std::tuple<double, double, int>(a.tonnageLost + a.damageTaken, x.tonnageLost + x.damageTaken, decisions);
    };
    auto r1 = run(42), r2 = run(42);
    CHECK(r1 == r2);
    CHECK(std::get<2>(r1) > 5);
    double hurt = std::get<0>(r1) + std::get<1>(r1);
    CHECK(hurt > 0.0);  // somebody got hurt within the first 90 minutes
}

TEST_CASE("player orders are acknowledged by the bridge") {
    game::Scenario sc;
    REQUIRE(game::loadScenario(fs::dataPath("scenarios/savo_island.json").string(), sc));
    game::Session s(db(), doctrines());
    game::SessionOptions o;
    o.player = Side::Allies;
    REQUIRE(s.start(sc, o));
    s.step();
    s.drainEvents();
    const Ship* own = nullptr;
    for (const auto& sh : s.world().ships())
        if (sh->side == Side::Allies && sh->operational()) own = sh.get();
    REQUIRE(own);
    auto replies = [&] {
        std::vector<std::string> out;
        for (const auto& e : s.drainEvents())
            if (e.type == EventType::OrderAcknowledged) out.push_back(e.text);
        return out;
    };
    s.orderWeapons(own->id, WeaponsStatus::Hold);
    CHECK(replies() == std::vector<std::string>{"cease_fire"});
    s.orderWeapons(own->id, WeaponsStatus::Hold);  // no change, no reply
    CHECK(replies().empty());
    s.orderShipCourse(own->id, own->heading, own->speedKn());
    CHECK(replies() == std::vector<std::string>{"aye"});
    s.orderShipCourse(own->id, own->heading + 40.0, own->speedKn());
    CHECK(replies() == std::vector<std::string>{"course_steady"});
    s.orderShipCourse(own->id, own->heading + 40.0, own->speedKn() + 6.0);
    CHECK(replies() == std::vector<std::string>{"engines"});
    // The enemy's ships take no orders from the player.
    for (const auto& sh : s.world().ships())
        if (sh->side == Side::Axis) {
            s.orderWeapons(sh->id, WeaponsStatus::Hold);
            break;
        }
    CHECK(replies().empty());
}
