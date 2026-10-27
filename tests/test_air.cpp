#include <doctest/doctest.h>

#include "core/FileSystem.h"
#include "sim/Damage.h"
#include "sim/World.h"

using namespace it;
using namespace it::sim;

namespace {
const Database& db() {
    static Database d;
    static bool ok = d.loadFromDirectory(fs::dataPath("db").string());
    REQUIRE(ok);
    return d;
}

struct AirFixture {
    World w;
    explicit AirFixture(double hour = 11.0, uint64_t seed = 5) : w(db(), seed) {
        DateTime dt;
        dt.year = 1941;
        dt.month = 12;
        dt.day = 10;
        dt.hour = hour;
        dt.utcOffset = 7.5;
        WeatherConfig wx;
        wx.cloudCover = 0.2;
        wx.cloudBaseM = 3500;
        wx.visibilityKm = 30;
        w.env().configure(3.6, 104.5, dt, wx, {0, 0}, 80000, w.rng());
    }
    Ship& ship(const char* cls, Side side, Vec2 pos, double hdg, double kn) {
        const ShipClassDef* c = db().shipClass(cls);
        REQUIRE(c);
        Ship& s = w.spawnShip(*c, cls, side, pos, hdg, kn, CrewQuality::Regular);
        s.alertedAt = 0;
        return s;
    }
    int airfield(Side side, Vec2 pos) {
        AirBase b;
        b.name = "field";
        b.side = side;
        b.pos = pos;
        return w.addAirBase(b);
    }
    Flight& flight(const char* type, const char* loadout, int n, Side side, int base, CrewQuality crew = CrewQuality::Veteran) {
        const AircraftDef* d = db().aircraft(type);
        REQUIRE(d);
        return w.addFlight(type, side, *d, loadout, n, base, crew);
    }
    void run(double seconds) {
        for (int i = 0; i < int(seconds / kStep); ++i) w.step();
    }
    template <typename F> void runUntil(double seconds, F done) {
        for (int i = 0; i < int(seconds / kStep) && !done(); ++i) w.step();
    }
    int count(EventType t, Side side = Side::Neutral) {
        int n = 0;
        for (const auto& e : w.history())
            if (e.type == t && (side == Side::Neutral || e.side == side)) ++n;
        return n;
    }
};

/// Armed search: the flight looks for ships around a point and attacks what it finds.
AirOrder search(Vec2 p, bool strike = true) {
    AirOrder o;
    o.type = AirOrderType::Patrol;
    o.route = {p};
    o.point = p;
    o.strike = strike;
    return o;
}
}  // namespace

TEST_CASE("air database: loadouts link aerial torpedoes and bombs; catapult floatplanes by navy") {
    const AircraftDef* betty = db().aircraft("ijn_g4m1");
    REQUIRE(betty);
    const AircraftLoadout* t = betty->loadout("torpedo");
    REQUIRE(t);
    REQUIRE(t->torpedo);
    CHECK(t->torpedo->aerial);
    CHECK(betty->loadout("bomb800")->bomb != nullptr);
    CHECK(db().catapultAircraft("jake", Nation::KM)->id == "km_ar196");  // Scharnhorst's model shows a Jake
    CHECK(db().catapultAircraft("jake", Nation::IJN)->id == "ijn_e13a1");
    CHECK(db().catapultAircraft("walrus", Nation::RAN)->id == "rn_walrus");
    for (const auto& c : db().shipClasses())
        if (c->floatplanes > 0) CHECK_MESSAGE(db().catapultAircraft(c->floatplaneStyle, c->nation) != nullptr, c->id);
    // Light AA of every class is simulated, and dual-purpose guns join the AA batteries.
    Ship s;
    s.initFromClass(*db().shipClass("usn_dd_fletcher"));
    int heavy = 0, light = 0;
    for (const auto& b : s.aa) (b.mount >= 0 ? heavy : light)++;
    CHECK(heavy == 5);
    CHECK(light >= 5);
}

TEST_CASE("air: a flight takes off, flies its patrol and lands again") {
    AirFixture f;
    int field = f.airfield(Side::Allies, {0, 0});
    Flight& fl = f.flight("usn_sbd3", "bomb1000", 3, Side::Allies, field);
    int id = fl.id;
    AirOrder o = search({30000, 0}, false);
    REQUIRE(f.w.orderFlight(id, o));
    f.runUntil(1800, [&] { return f.w.flight(id)->state == FlightState::Station; });
    CHECK(f.w.flight(id)->state == FlightState::Station);
    for (EntityId pid : f.w.flight(id)->planes) {
        const Aircraft* a = f.w.plane(pid);
        CHECK(a->flying());
        CHECK(distance(a->pos.xy(), Vec2{30000, 0}) < 12000.0);
        CHECK(a->pos.z > 1500.0);
    }
    AirOrder back;
    back.type = AirOrderType::Return;
    REQUIRE(f.w.orderFlight(id, back));
    f.runUntil(1800, [&] { return f.w.flight(id)->state == FlightState::Rearming; });
    CHECK(f.w.flight(id)->state == FlightState::Rearming);
    CHECK(f.w.flight(id)->alive(f.w) == 3);
    CHECK(f.count(EventType::FlightLaunched) == 1);
    CHECK(f.count(EventType::FlightLanded) == 1);
}

TEST_CASE("air: torpedo bombers find and torpedo a battleship steaming a steady course") {
    int hits = 0, dropped = 0;
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        AirFixture f(11.0, seed);
        Ship& bb = f.ship("rn_bb_kgv", Side::Allies, {0, 0}, 90, 20);
        bb.orders.weapons = WeaponsStatus::Hold;  // attack mechanics only
        int field = f.airfield(Side::Axis, {0, -60000});
        Flight& fl = f.flight("ijn_g4m1", "torpedo", 9, Side::Axis, field);
        int id = fl.id;
        f.w.putAirborne(fl, {0, -40000}, 2500, 0);
        REQUIRE(f.w.orderFlight(id, search({8000, 0})));
        f.run(1500);
        dropped += f.w.flight(id)->torpedoesDropped;
        hits += bb.dmg.torpedoHitsTaken;
    }
    CHECK(dropped >= 20);
    CHECK(hits >= 3);
}

TEST_CASE("air: dive bombers hit a cruiser") {
    int hits = 0;
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        AirFixture f(11.0, seed);
        Ship& ca = f.ship("usn_ca_new_orleans", Side::Allies, {0, 0}, 0, 15);
        ca.orders.weapons = WeaponsStatus::Hold;
        int field = f.airfield(Side::Axis, {0, -60000});
        Flight& fl = f.flight("ijn_d3a1", "bomb250", 6, Side::Axis, field);
        int id = fl.id;
        f.w.putAirborne(fl, {-30000, 0}, 3800, 90);
        REQUIRE(f.w.orderFlight(id, search({0, 3000})));
        f.run(900);
        CHECK(f.w.flight(id)->bombsDropped >= 6);
        hits += ca.dmg.hitsTaken;
    }
    CHECK(hits >= 3);
}

TEST_CASE("air: level bombers' pattern falls around a stationary target") {
    AirFixture f(11.0, 3);
    Ship& bb = f.ship("rn_bb_kgv", Side::Allies, {0, 0}, 90, 0);
    bb.orders.weapons = WeaponsStatus::Hold;
    int field = f.airfield(Side::Axis, {0, -80000});
    Flight& fl = f.flight("ijn_g3m2", "bomb500", 9, Side::Axis, field);
    int id = fl.id;
    f.w.putAirborne(fl, {0, -40000}, 3000, 0);
    REQUIRE(f.w.orderFlight(id, search({0, 0})));
    // Splashes of the 500 kg bombs (and hits) around the battleship.
    double sum = 0;
    int n = 0;
    for (int i = 0; i < int(1500 / kStep); ++i) {
        f.w.step();
        for (const auto& e : f.w.takeEvents())
            if ((e.type == EventType::ShellSplash || e.type == EventType::ShellHit) && f.w.plane(e.a)) {
                sum += distance(e.pos.xy(), bb.pos);
                ++n;
            }
    }
    REQUIRE(n >= 9);
    CHECK(sum / n < 450.0);
}

TEST_CASE("AA: a cruiser and a destroyer damage attacking aircraft; holding fire silences them") {
    auto attack = [](WeaponsStatus ws, uint64_t seed) {
        AirFixture f(11.0, seed);
        Ship& ca = f.ship("usn_ca_new_orleans", Side::Allies, {0, 0}, 90, 25);
        Ship& dd = f.ship("usn_dd_fletcher", Side::Allies, {0, 1500}, 90, 25);
        ca.orders.weapons = dd.orders.weapons = ws;
        int field = f.airfield(Side::Axis, {0, -60000});
        Flight& fl = f.flight("ijn_g4m1", "torpedo", 9, Side::Axis, field);
        int id = fl.id;
        f.w.putAirborne(fl, {0, -35000}, 2500, 0);
        REQUIRE(f.w.orderFlight(id, search({6000, 0})));
        f.run(900);
        return f.count(EventType::AircraftHit) + f.count(EventType::AircraftShotDown);
    };
    int free = 0, hold = 0;
    for (uint64_t seed = 1; seed <= 3; ++seed) {
        free += attack(WeaponsStatus::Free, seed);
        hold += attack(WeaponsStatus::Hold, seed);
    }
    CHECK(free > 0);
    CHECK(hold == 0);
}

TEST_CASE("air: radar reports a raid long before the lookouts identify it") {
    AirFixture f(11.0, 2);
    Ship& ca = f.ship("usn_ca_new_orleans", Side::Allies, {0, 0}, 0, 15);  // SC air search
    int field = f.airfield(Side::Axis, {0, -150000});
    Flight& fl = f.flight("ijn_g4m1", "torpedo", 18, Side::Axis, field);
    int id = fl.id;
    f.w.putAirborne(fl, {0, -100000}, 3000, 0);
    REQUIRE(f.w.orderFlight(id, search({0, 0})));
    double firstRange = -1, roleRange = -1;
    for (int i = 0; i < int(1500 / kStep); ++i) {
        f.w.step();
        for (const auto& t : f.w.picture(Side::Allies).airTracks()) {
            double r = distance(t.pos, ca.pos);
            if (firstRange < 0) firstRange = r;
            if (t.roleKnown && roleRange < 0) roleRange = r;
        }
        if (roleRange >= 0) break;
    }
    CHECK(firstRange > 60000.0);
    CHECK(roleRange > 0.0);
    CHECK(roleRange < 20000.0);
    CHECK(f.count(EventType::AirContactNew, Side::Allies) >= 1);
}

TEST_CASE("air: fighters on patrol over a ship intercept a raid and shoot bombers down") {
    AirFixture f(11.0, 6);
    Ship& ca = f.ship("usn_ca_new_orleans", Side::Allies, {0, 0}, 0, 15);
    ca.orders.weapons = WeaponsStatus::Hold;
    int us = f.airfield(Side::Allies, {0, 20000});
    Flight& cap = f.flight("usn_f4f4", "guns", 4, Side::Allies, us);
    int capId = cap.id;
    f.w.putAirborne(cap, {0, 0}, 4000, 180);
    AirOrder o;
    o.type = AirOrderType::Cap;
    o.point = {0, 0};
    REQUIRE(f.w.orderFlight(capId, o));
    int jp = f.airfield(Side::Axis, {0, -150000});
    Flight& raid = f.flight("ijn_g4m1", "torpedo", 9, Side::Axis, jp, CrewQuality::Regular);
    int raidId = raid.id;
    f.w.putAirborne(raid, {0, -70000}, 3000, 0);
    REQUIRE(f.w.orderFlight(raidId, search({0, 0})));
    f.run(1500);
    CHECK(f.w.flight(capId)->kills >= 1);
    CHECK(f.w.flight(raidId)->losses >= 1);
}

TEST_CASE("air: a cruiser's floatplane drops flares over the enemy and comes back aboard") {
    AirFixture f(1.0, 3);  // night
    Ship& ca = f.ship("ijn_ca_takao", Side::Axis, {0, 0}, 0, 20);
    f.ship("usn_ca_new_orleans", Side::Allies, {0, 15000}, 180, 15);
    int before = ca.floatplanes;
    REQUIRE(f.w.launchAircraft(ca, true, {0, 15000}));
    CHECK(ca.floatplanes == before - 1);
    f.run(900);
    CHECK(f.count(EventType::FlareDropped) >= 3);
    f.run(5400);
    CHECK(ca.floatplanes == before);  // recovered
}

TEST_CASE("damage: aerial torpedoes sink Repulse and Prince of Wales as on 10 December 1941, two do not") {
    // Hits along the port side, a few minutes apart; then an hour for the water to win or lose.
    auto sunkOf = [](const char* cls, int hits) {
        int sunk = 0;
        for (uint64_t seed = 1; seed <= 6; ++seed) {
            AirFixture f(12.0, seed);
            Ship& s = f.ship(cls, Side::Allies, {0, 0}, 0, 20);
            Torpedo t;
            t.def = db().torpedo("ijn_type91_air");
            t.firer = kNoEntity;
            const double xs[] = {0.1, -0.25, 0.05, -0.1, 0.2, -0.35, 0.0};
            for (int i = 0; i < hits; ++i) {
                damage::torpedoHit(f.w, s, t, {xs[i] * s.cls->length, (i % 4 == 3 ? 0.5 : -0.5) * s.cls->beam});
                f.run(150);
            }
            f.runUntil(3600, [&] { return s.dmg.sinking; });
            sunk += s.dmg.sinking;
        }
        return sunk;
    };
    CHECK(sunkOf("rn_bc_repulse", 2) <= 1);
    CHECK(sunkOf("rn_bc_repulse", 5) >= 5);
    CHECK(sunkOf("rn_bb_kgv", 3) <= 1);
    CHECK(sunkOf("rn_bb_kgv", 7) >= 5);
}
