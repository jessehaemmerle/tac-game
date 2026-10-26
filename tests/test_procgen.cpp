#include <doctest/doctest.h>

#include "core/FileSystem.h"
#include "procgen/ShipModel.h"

using namespace it;

TEST_CASE("procedural ship models are valid and match class dimensions") {
    sim::Database db;
    REQUIRE(db.loadFromDirectory(fs::dataPath("db").string()));
    for (auto& c : db.shipClasses()) {
        INFO(c->id);
        procgen::Model m = procgen::buildShipModel(*c);
        CHECK(m.triangleCount() > 2000);
        CHECK(m.triangleCount() < 60000);
        double len = m.bounds.hi.x - m.bounds.lo.x;
        CHECK(len > c->length * 0.97);
        CHECK(len < c->length * 1.25);  // barrels may overhang
        CHECK(m.bounds.lo.z < -c->draft * 0.8);
        int turrets = 0, badIndex = 0, badVertex = 0, orphanBarrels = 0;
        for (auto& p : m.parts) {
            for (uint32_t i : p.indices) badIndex += i >= p.vertices.size();
            for (auto& v : p.vertices) badVertex += !std::isfinite(v.pos.x) || std::fabs(length(v.normal) - 1.0f) > 0.02f;
            if (p.kind == procgen::PartKind::Turret) ++turrets;
            if (p.kind == procgen::PartKind::Barrel && p.parent < 0) ++orphanBarrels;
        }
        CHECK(badIndex == 0);
        CHECK(badVertex == 0);
        CHECK(orphanBarrels == 0);
        CHECK(turrets == int(c->mounts.size()));
    }
}

TEST_CASE("hull form: waterline narrows to the stem and the keel lies at draft") {
    sim::Database db;
    REQUIRE(db.loadFromDirectory(fs::dataPath("db").string()));
    const sim::ShipClassDef* c = db.shipClass("usn_dd_fletcher");
    REQUIRE(c);
    CHECK(procgen::hullHalfWidth(*c, 0.0, 0.0) < 0.2);
    CHECK(procgen::hullHalfWidth(*c, 0.5, 0.0) == doctest::Approx(c->beam * 0.5).epsilon(0.02));
    CHECK(procgen::keelDepth(*c, 0.5) == doctest::Approx(c->draft).epsilon(0.02));
    CHECK(procgen::hullHalfWidth(*c, 0.5, -c->draft) < 0.05);
}
