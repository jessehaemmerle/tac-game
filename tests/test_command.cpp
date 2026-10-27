// Player command: waypoint routes, handing groups to the AI commander and back, commanding flights.
#include <doctest/doctest.h>

#include "ai/AiController.h"
#include "core/FileSystem.h"
#include "game/Session.h"

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

struct PlayerBattle {
    game::Scenario sc;
    game::Session s{db(), doctrines()};
    PlayerBattle(const char* scenario, Side player) {
        REQUIRE(game::loadScenario(fs::dataPath(std::string("scenarios/") + scenario + ".json").string(), sc));
        game::SessionOptions o;
        o.player = player;
        o.seed = 11;
        REQUIRE(s.start(sc, o));
    }
    void run(double seconds) {
        for (int i = 0; i < int(seconds / kStep) && !s.finished(); ++i) s.step();
    }
    Group* firstPlayable() {
        for (auto& g : s.world().groups())
            if (g.playerControlled) return &g;
        return nullptr;
    }
};
}  // namespace

TEST_CASE("command: a group steers through its waypoints and reports the last one") {
    PlayerBattle b("savo_island", Side::Allies);
    Group* g = b.firstPlayable();
    REQUIRE(g);
    int gid = g->id;
    Ship* guide = b.s.world().guideOf(*g);
    REQUIRE(guide);
    Vec2 start = guide->pos;
    Vec2 a = start + bearingVector(g->course + 60.0) * 4000.0;
    Vec2 c = a + bearingVector(g->course - 30.0) * 4000.0;
    b.s.orderGroupRoute(gid, {a}, false);
    b.s.orderGroupRoute(gid, {c}, true);
    CHECK(b.s.world().group(gid)->route.size() == 2);
    double nearA = 1e9, nearC = 1e9;
    bool done = false;
    for (int i = 0; i < 90 && !done; ++i) {
        b.run(20);
        if (Ship* gs = b.s.world().guideOf(*b.s.world().group(gid))) {
            nearA = std::min(nearA, distance(gs->pos, a));
            nearC = std::min(nearC, distance(gs->pos, c));
        }
        for (const auto& e : b.s.drainEvents())
            if (e.type == EventType::Report && e.text == "report.route_done") done = true;
    }
    CHECK(nearA < 1200.0);
    CHECK(nearC < 1200.0);
    CHECK(done);
    CHECK(b.s.world().group(gid)->route.empty());
    // A new course replaces a route.
    b.s.orderGroupRoute(gid, {start}, false);
    b.s.orderGroupCourse(gid, b.s.world().group(gid)->course + 90.0, 15.0);
    CHECK(b.s.world().group(gid)->route.empty());
}

TEST_CASE("command: a ship leaves the formation to follow her own waypoints, and rejoins") {
    PlayerBattle b("savo_island", Side::Allies);
    Group* g = b.firstPlayable();
    REQUIRE(g);
    REQUIRE(g->members.size() >= 2);
    EntityId id = g->members.back();
    Ship* s = b.s.world().ship(id);
    REQUIRE(s);
    Vec2 wp = s->pos + bearingVector(s->heading + 90.0) * 3000.0;
    b.s.orderShipRoute(id, {wp}, false);
    CHECK(s->orders.playerSteering);
    double nearest = 1e9;
    for (int i = 0; i < 40; ++i) {
        b.run(15);
        nearest = std::min(nearest, distance(s->pos, wp));
    }
    CHECK(nearest < 1000.0);
    CHECK(s->orders.route.empty());
    b.s.rejoinGroup(id);
    CHECK_FALSE(s->orders.playerSteering);
}

TEST_CASE("command: a group handed to the AI commander is fought by it until the player takes it back") {
    PlayerBattle b("savo_island", Side::Allies);
    Group* g = b.firstPlayable();
    REQUIRE(g);
    int gid = g->id;
    ai::AiController* ai = b.s.ai(Side::Allies);
    REQUIRE(ai);
    // The commander thinks (and schedules its next appreciation) only for groups it commands.
    auto lastThought = [&] { return ai->brain(gid) ? ai->brain(gid)->nextThink : -1.0; };
    b.run(120);
    CHECK(lastThought() <= 0.0);  // the player's group: no AI commander
    b.s.delegateGroup(gid, true);
    CHECK_FALSE(b.s.world().group(gid)->playerControlled);
    CHECK(b.s.world().group(gid)->aiDelegated);
    b.run(120);
    double t = b.s.world().time();
    CHECK(lastThought() > t - 30.0);
    b.s.delegateGroup(gid, false);
    CHECK(b.s.world().group(gid)->playerControlled);
    double frozen = lastThought();
    b.run(240);
    CHECK(lastThought() == frozen);
}

TEST_CASE("command: Force Z can call its fighters - the Buffaloes take off and cover the ships") {
    PlayerBattle b("force_z", Side::Allies);
    World& w = b.s.world();
    int buffalo = b.s.worldFlight("buffalo");
    REQUIRE(buffalo >= 0);
    CHECK(w.flight(buffalo)->playerControlled);
    Group* fz = nullptr;
    for (auto& g : w.groups())
        if (g.name == "group.force_z.capital") fz = &g;
    REQUIRE(fz);
    int fzId = fz->id;
    AirOrder o;
    o.type = AirOrderType::Cap;
    o.group = fzId;
    std::string why;
    REQUIRE(b.s.orderFlight(buffalo, o, &why));
    b.run(120);
    CHECK(w.flight(buffalo)->airborne());
    // The Japanese bombers are not the player's to command.
    int jp = b.s.worldFlight("mihoro1");
    REQUIRE(jp >= 0);
    CHECK_FALSE(b.s.orderFlight(jp, o, &why));
    CHECK(why == "air.why.not_yours");
    double closest = 1e9;
    for (int i = 0; i < 80; ++i) {
        b.run(60);
        const Flight* f = w.flight(buffalo);
        const Ship* guide = w.guideOf(*w.group(fzId));
        if (f && guide && f->airborne()) closest = std::min(closest, distance(f->pos, guide->pos));
    }
    CHECK(closest < 20000.0);
}
