#include <doctest/doctest.h>

#include "core/FileSystem.h"
#include "sim/Ballistics.h"
#include "sim/Database.h"

using namespace it;
using namespace it::sim;

namespace {
const Database& db() {
    static Database d;
    static bool loaded = [] {
        std::string err;
        bool ok = d.loadFromDirectory(fs::dataPath("db").string(), &err);
        if (!ok) MESSAGE(err);
        return ok;
    }();
    REQUIRE(loaded);
    return d;
}
}  // namespace

TEST_CASE("database loads and validates cleanly") {
    auto problems = db().validate();
    for (auto& p : problems) MESSAGE(p);
    CHECK(problems.empty());
    CHECK(db().shipClasses().size() >= 18);
}

TEST_CASE("ballistic calibration reproduces historical maximum ranges") {
    for (auto& g : db().guns()) {
        INFO(g->id);
        CHECK(g->effectiveMaxRange() == doctest::Approx(g->maxRange).epsilon(0.03));
        CHECK(g->shells.front().formFactor > 0.3);
        CHECK(g->shells.front().formFactor < 2.5);
    }
}

TEST_CASE("range table is monotonic and gives plausible flight times") {
    const GunDef* g = db().gun("usn_8in55");
    REQUIRE(g);
    double elev, tof;
    REQUIRE(g->elevationForRange(15000, elev, tof));
    // 8"/55 at ~16,400 yd: roughly 13-14 deg elevation and ~30 s time of flight.
    CHECK(elev > 9.0);
    CHECK(elev < 18.0);
    CHECK(tof > 20.0);
    CHECK(tof < 40.0);
    for (size_t i = 1; i < g->table.size(); ++i) CHECK(g->table[i].range > g->table[i - 1].range);
    CHECK_FALSE(g->elevationForRange(40000, elev, tof));
}

TEST_CASE("penetration falls with range and angle") {
    const GunDef* g = db().gun("ijn_20cm50");
    REQUIRE(g);
    auto near = g->sampleAtRange(5000), far = g->sampleAtRange(20000);
    const ShellDef& ap = g->shells.front();
    double pNear = ballistics::beltPenetrationMm(ap.massKg, g->caliberMm, near.strikingVelocity, near.angleOfFallDeg);
    double pFar = ballistics::beltPenetrationMm(ap.massKg, g->caliberMm, far.strikingVelocity, far.angleOfFallDeg);
    CHECK(pNear > pFar);
    // An 8-inch AP shell at 5 km must defeat a destroyer, a 127 mm cruiser belt, but not a 400 mm battleship belt.
    CHECK(pNear > 127.0);
    CHECK(pNear < 400.0);
    double dFar = ballistics::deckPenetrationMm(ap.massKg, g->caliberMm, far.strikingVelocity, far.angleOfFallDeg);
    double dNear = ballistics::deckPenetrationMm(ap.massKg, g->caliberMm, near.strikingVelocity, near.angleOfFallDeg);
    CHECK(dFar > dNear);  // plunging fire threatens decks
}

TEST_CASE("firing arcs including mirrored and wrap-around") {
    CHECK(inArc(0, -150, 150));
    CHECK_FALSE(inArc(180, -150, 150));
    CHECK(inArc(180, 30, 330));
    CHECK_FALSE(inArc(0, 30, 330));
    CHECK(inArc(90, 40, 140, true));
    CHECK(inArc(-90, 40, 140, true));
    CHECK(inArc(270, 40, 140, true));
    CHECK_FALSE(inArc(0, 40, 140, true));
}

TEST_CASE("class data sanity: IJN Type 93 outranges US Mark 15") {
    auto* t93 = db().torpedo("ijn_type93");
    auto* mk15 = db().torpedo("usn_mk15");
    REQUIRE(t93);
    REQUIRE(mk15);
    CHECK(t93->settings.back().range > 2.5 * mk15->settings.back().range);
    CHECK(t93->wakeVisibility < mk15->wakeVisibility);
    auto* chokai = db().shipClass("ijn_ca_takao");
    REQUIRE(chokai);
    CHECK(chokai->torpedoTubes() == 8);
    CHECK(chokai->mounts.size() == 9);
    for (auto& c : db().shipClasses()) {
        INFO(c->id);
        CHECK(c->model.deckHeightAt(0.0) > c->model.deckHeightAt(0.5) - 1e-6);
        for (auto& m : c->mounts) CHECK(m.z > 1.0);
    }
}
