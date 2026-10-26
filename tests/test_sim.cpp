#include <doctest/doctest.h>

#include "core/FileSystem.h"
#include "sim/Damage.h"
#include "sim/Sensors.h"
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

struct Fixture {
    World w;
    explicit Fixture(double hour = 1.5, uint64_t seed = 7) : w(db(), seed) {
        DateTime dt;
        dt.year = 1942;
        dt.month = 8;
        dt.day = 9;
        dt.hour = hour;
        WeatherConfig wx;
        wx.cloudCover = 0.2;
        wx.visibilityKm = 25;
        w.env().configure(-9.1, 159.8, dt, wx, {0, 0}, 50000, w.rng());
    }
    Ship& add(const char* cls, Side side, Vec2 pos, double hdg, double kn, CrewQuality q = CrewQuality::Regular) {
        const ShipClassDef* c = db().shipClass(cls);
        REQUIRE(c);
        return w.spawnShip(*c, cls, side, pos, hdg, kn, q);
    }
    void run(double seconds) {
        int n = int(seconds / kStep);
        for (int i = 0; i < n; ++i) w.step();
    }
};
}  // namespace

TEST_CASE("astronomy: Savo Island night of 8/9 August 1942 was dark") {
    double alt, az, phase;
    Environment::sunPosition(1942, 8, 8, 14.6, -9.1, 159.8, alt, az);  // 01:36 local = 14:36 UT (8 Aug)
    CHECK(alt < -40.0);
    Environment::sunPosition(1942, 8, 9, 1.0, -9.1, 159.8, alt, az);  // local noon
    CHECK(alt > 55.0);
    Environment::moonPosition(1942, 8, 8, 14.6, -9.1, 159.8, alt, az, phase);
    CHECK(phase < 0.35);  // waning crescent shortly before new moon
    double light = Environment::lightLevel(-45.0, alt, phase, 0.7);
    CHECK(light < 0.06);
    CHECK(Environment::lightLevel(30.0, -10.0, 0.0, 0.0) == doctest::Approx(1.0));
}

TEST_CASE("ship handling: turns onto ordered course and keeps speed") {
    Fixture f(12.0);
    Ship& s = f.add("usn_dd_fletcher", Side::Allies, {0, 0}, 0, 30);
    s.orders.course = 90;
    s.orders.speedKn = 30;
    f.run(90);
    CHECK(std::fabs(angleDiff(s.heading, 90.0)) < 3.0);
    CHECK(s.speedKn() > 25.0);
    // Turning circle: a destroyer at full rudder needs roughly 600-900 m to reverse course.
    Fixture g(12.0);
    Ship& t = g.add("usn_dd_fletcher", Side::Allies, {0, 0}, 0, 30);
    t.orders.course = 180;
    double maxX = 0;
    for (int i = 0; i < int(120 / kStep); ++i) {
        g.w.step();
        maxX = std::max(maxX, std::fabs(t.pos.x));
    }
    CHECK(maxX > 350.0);
    CHECK(maxX < 1200.0);
}

TEST_CASE("visual detection: night optics and light dominate") {
    Fixture f(1.5);
    Ship& usn = f.add("usn_ca_new_orleans", Side::Allies, {0, 0}, 0, 15, CrewQuality::Regular);
    Ship& ijn = f.add("ijn_ca_takao", Side::Axis, {0, -2000}, 0, 15, CrewQuality::Elite);
    Ship& target = f.add("usn_ca_northampton", Side::Allies, {8000, 0}, 0, 15);  // seen by both observers
    const SensorDef* usnEyes = db().sensor("usn_lookouts");
    const SensorDef* ijnEyes = db().sensor("ijn_lookouts");
    double usnSees = sensors::visualRange(f.w, usn, target, usnEyes, 27).range;
    double ijnSees = sensors::visualRange(f.w, ijn, target, ijnEyes, 27).range;
    CHECK(ijnSees > usnSees * 1.4);
    CHECK(usnSees > 3000.0);
    CHECK(usnSees < 9000.0);
    // A starshell over the target changes everything.
    Flare fl;
    fl.pos = {8200, 0, 450};
    fl.life = 10;
    fl.maxLife = 40;
    f.w.addFlare(fl);
    double lit = sensors::visualRange(f.w, usn, target, usnEyes, 27).range;
    CHECK(lit > usnSees * 2.0);
    // Daylight
    Fixture d(11.0);
    Ship& a = d.add("usn_ca_new_orleans", Side::Allies, {0, 0}, 0, 15);
    Ship& b = d.add("ijn_dd_kagero", Side::Axis, {8000, 0}, 0, 26);
    CHECK(sensors::visualRange(d.w, a, b, usnEyes, 27).range > 14000.0);
}

TEST_CASE("radar: SG outranges SC, islands cast radar shadows") {
    Fixture f(1.5);
    Ship& a = f.add("usn_cl_st_louis", Side::Allies, {0, 0}, 0, 15);
    Ship& b = f.add("ijn_ca_aoba", Side::Axis, {20000, 0}, 0, 26);
    double sg = sensors::radarRange(f.w, a, *db().sensor("usn_sg"), 30, b);
    double sc = sensors::radarRange(f.w, a, *db().sensor("usn_sc"), 33, b);
    CHECK(sg > sc);
    CHECK(sg > 20000.0);
    // Put a 400 m island between them.
    std::vector<float> h(64 * 64, -500.0f);
    for (int y = 28; y < 36; ++y)
        for (int x = 28; x < 36; ++x) h[size_t(y) * 64 + x] = 400.0f;
    f.w.terrain().createFromSamples(64, 64, 1000.0, h);
    a.pos = {-8000, 0};
    b.pos = {8000, 0};
    CHECK(sensors::radarRange(f.w, a, *db().sensor("usn_sg"), 30, b) == 0.0);
}

TEST_CASE("gunnery: a daylight duel produces hits within minutes") {
    Fixture f(10.0, 11);
    Ship& shooter = f.add("usn_ca_new_orleans", Side::Allies, {0, 0}, 90, 20, CrewQuality::Veteran);
    Ship& target = f.add("ijn_ca_aoba", Side::Axis, {0, 12000}, 90, 20, CrewQuality::Veteran);
    target.orders.weapons = WeaponsStatus::Hold;
    f.run(20);  // lookouts acquire
    const Track* tr = nullptr;
    for (auto& t : f.w.picture(Side::Allies).tracks()) tr = &t;
    REQUIRE(tr);
    f.w.setDirectorTarget(shooter, 0, tr->id);
    f.run(300);
    CHECK(shooter.stats.salvos >= 8);
    CHECK(shooter.stats.hitsScored >= 1);
    CHECK(target.dmg.hitsTaken >= 1);
}

TEST_CASE("torpedoes: Type 93 spreads hit a destroyer steaming on a steady course") {
    int hits = 0, sunk = 0;
    for (uint64_t seed = 1; seed <= 6; ++seed) {
        Fixture f(10.0, seed);
        Ship& ijn = f.add("ijn_dd_kagero", Side::Axis, {0, 0}, 0, 12, CrewQuality::Veteran);
        Ship& usn = f.add("usn_dd_bagley", Side::Allies, {6500, -1500}, 0, 15);
        usn.orders.weapons = WeaponsStatus::Hold;
        f.run(90);  // track the target long enough to estimate course and speed
        const Track* tr = nullptr;
        for (auto& t : f.w.picture(Side::Axis).tracks()) tr = &t;
        REQUIRE(tr);
        std::string why;
        int n = f.w.fireTorpedoes(ijn, tr->id, 8, 4.0, 0, &why);  // ~1 target length between torpedoes
        CHECK_MESSAGE(n > 0, why);
        f.run(420);
        hits += usn.dmg.torpedoHitsTaken > 0;
        sunk += usn.dmg.sinking || usn.dmg.sunk;
    }
    CHECK(hits >= 2);
    CHECK(sunk >= 1);
}

TEST_CASE("damage: one Type 93 sinks a destroyer, a heavy cruiser survives one") {
    Fixture f(10.0, 5);
    Ship& dd = f.add("usn_dd_benson", Side::Allies, {0, 0}, 0, 0);
    Ship& ca = f.add("usn_ca_new_orleans", Side::Allies, {5000, 0}, 0, 0);
    Torpedo t;
    t.def = db().torpedo("ijn_type93");
    t.firer = kNoEntity;
    damage::torpedoHit(f.w, dd, t, {0.0, 5.0});
    damage::torpedoHit(f.w, ca, t, {-20.0, 9.0});
    f.run(600);
    CHECK((dd.dmg.sinking || dd.dmg.sunk));
    CHECK_FALSE(ca.dmg.sinking);
    CHECK(ca.dmg.flooding > 500.0);
}

TEST_CASE("determinism: identical seeds give identical battles") {
    auto simulate = [](uint64_t seed) {
        Fixture f(1.0, seed);
        Ship& a = f.add("ijn_ca_takao", Side::Axis, {0, 0}, 90, 26, CrewQuality::Elite);
        f.add("usn_ca_northampton", Side::Allies, {0, 9000}, 270, 15);
        f.run(60);
        for (auto& t : f.w.picture(Side::Axis).tracks()) f.w.setDirectorTarget(a, 0, t.id);
        f.run(240);
        double h = 0;
        for (auto& s : f.w.ships()) h += s->pos.x * 1.3 + s->pos.y + s->dmg.hullIntegrity * 1000 + s->stats.shellsFired;
        return h;
    };
    CHECK(simulate(99) == simulate(99));
}

TEST_CASE("contacts: new tracks and identifications are announced as events") {
    Fixture f(13.0);  // daylight
    f.add("usn_dd_fletcher", Side::Allies, {0, 0}, 0, 15);
    f.add("ijn_dd_kagero", Side::Axis, {0, 9000}, 180, 15);
    int fresh = 0, identified = 0;
    for (int i = 0; i < int(240.0 / kStep); ++i) {
        f.w.step();
        for (const auto& e : f.w.takeEvents()) {
            if (e.type == EventType::ContactNew && e.side == Side::Allies) {
                ++fresh;
                CHECK(f.w.picture(Side::Allies).find(e.track) != nullptr);
            }
            if (e.type == EventType::ContactIdentified && e.side == Side::Allies) {
                ++identified;
                CHECK(e.text == "ijn_dd_kagero");
            }
        }
    }
    CHECK(fresh == 1);       // one ship, one track
    CHECK(identified <= 1);  // announced once at most
}
