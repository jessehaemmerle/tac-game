#include <doctest/doctest.h>

#include "core/FileSystem.h"
#include "procgen/ShipAnimation.h"
#include "procgen/ShipModel.h"

using namespace it;
using procgen::PartKind;

TEST_CASE("procedural ship models are valid and match class dimensions") {
    sim::Database db;
    REQUIRE(db.loadFromDirectory(fs::dataPath("db").string()));
    for (auto& c : db.shipClasses()) {
        INFO(c->id);
        procgen::Model m = procgen::buildShipModel(*c);
        size_t lodTris[3] = {0, 0, 0};
        for (auto& p : m.parts) lodTris[std::min<size_t>(p.lod, 2)] += p.indices.size() / 3;
        CHECK(m.triangleCount() > 20000);
        CHECK(m.triangleCount() < 200000);
        CHECK(lodTris[0] < 50000);  // what distant ships cost
        double len = m.bounds.hi.x - m.bounds.lo.x;
        CHECK(len > c->length * 0.97);
        CHECK(len < c->length * 1.25);  // barrels and aircraft may overhang
        CHECK(m.bounds.hi.y - m.bounds.lo.y < c->beam * 1.35 + 2.0);  // nothing floats off the side
        CHECK(m.bounds.hi.z < c->mastHeight + 12.0);
        CHECK(m.bounds.lo.z < -c->draft * 0.8);
        int turrets = 0, badIndex = 0, badVertex = 0, badParent = 0, guns = 0, torpedoes = 0, aaMounts = 0, flags = 0;
        for (size_t i = 0; i < m.parts.size(); ++i) {
            const auto& p = m.parts[i];
            for (uint32_t k : p.indices) badIndex += k >= p.vertices.size();
            for (auto& v : p.vertices) badVertex += !std::isfinite(v.pos.x) || std::fabs(length(v.normal) - 1.0f) > 0.02f;
            badParent += p.parent >= int(i);  // parents come first (the renderer composes in order)
            auto parentKind = [&]() { return p.parent >= 0 ? m.parts[size_t(p.parent)].kind : PartKind::Static; };
            switch (p.kind) {
                case PartKind::Turret: turrets += p.lod == 0; break;
                case PartKind::Barrel: badParent += parentKind() != PartKind::Turret; break;
                case PartKind::Gun:
                    ++guns;
                    badParent += parentKind() != PartKind::Barrel || m.parts[size_t(p.parent)].mount != p.mount || p.sub < 0;
                    break;
                case PartKind::Torpedo:
                    ++torpedoes;
                    badParent += parentKind() != PartKind::TorpedoMount;
                    break;
                case PartKind::AAMount: ++aaMounts; break;
                case PartKind::AAGuns: badParent += parentKind() != PartKind::AAMount; break;
                case PartKind::SearchlightLamp: badParent += parentKind() != PartKind::Searchlight; break;
                case PartKind::Flag: ++flags; break;
                default: break;
            }
        }
        CHECK(badIndex == 0);
        CHECK(badVertex == 0);
        CHECK(badParent == 0);
        CHECK(turrets == int(c->mounts.size()));
        int barrels = 0;
        for (auto& mt : c->mounts) barrels += mt.barrels;
        CHECK(guns == barrels);  // every barrel recoils on its own
        CHECK(torpedoes == c->torpedoTubes());
        CHECK(aaMounts == int(m.aa.size()));
        CHECK(flags == 1);
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

TEST_CASE("model details follow the class data: light AA, propellers, aircraft on their catapults") {
    sim::Database db;
    REQUIRE(db.loadFromDirectory(fs::dataPath("db").string()));
    const sim::ShipClassDef* fletcher = db.shipClass("usn_dd_fletcher");
    const sim::ShipClassDef* takao = db.shipClass("ijn_ca_takao");
    REQUIRE(fletcher);
    REQUIRE(takao);
    procgen::Model f = procgen::buildShipModel(*fletcher);
    int props = 0;
    for (auto& p : f.parts) props += p.kind == PartKind::Propeller;
    CHECK(props == fletcher->shafts);
    // The 1.1" quad and the four 20 mm guns of a 1942 Fletcher.
    CHECK(f.aa.size() == 5);
    int quad = 0;
    for (auto& a : f.aa) quad += a.barrels == 4;
    CHECK(quad == 1);
    // Takao's floatplanes ride on (and train with) their catapults.
    procgen::Model t = procgen::buildShipModel(*takao);
    int onCatapult = 0;
    for (auto& p : t.parts)
        if (p.kind == PartKind::Aircraft && p.parent >= 0 && t.parts[size_t(p.parent)].kind == PartKind::Catapult) ++onCatapult;
    CHECK(onCatapult == 2);
    // Without details only the hull, structures and armament remain.
    procgen::ShipModelOptions plain;
    plain.details = false;
    procgen::Model p = procgen::buildShipModel(*takao, plain);
    CHECK(p.triangleCount() < t.triangleCount() / 2);
    CHECK(p.aa.empty());
}

TEST_CASE("weapon animations: recoil, torpedo launch, catapult launch and the drill timeline") {
    namespace an = procgen::anim;
    // Recoil: nothing before the shot, full run-back right after, run out within two seconds.
    CHECK(an::recoil(-0.1, 203.0) == 0.0f);
    CHECK(an::recoil(0.07, 203.0) == doctest::Approx(0.65).epsilon(0.05));
    CHECK(an::recoil(0.07, 127.0) < an::recoil(0.07, 203.0));
    CHECK(an::recoil(2.5, 203.0) < 0.01f);
    CHECK(an::barrelDelay(2) > an::barrelDelay(1));
    // Torpedoes leave the tube, accelerate, drop once clear and are gone after a few seconds.
    CHECK(an::torpedoTravel(0.0) == 0.0f);
    CHECK(an::torpedoTravel(0.5) > 0.0f);
    CHECK(an::torpedoTravel(1.0) > an::torpedoTravel(0.5));
    CHECK(an::torpedoDrop(5.0f, 9.0f) == 0.0f);
    CHECK(an::torpedoDrop(15.0f, 9.0f) < 0.0f);
    CHECK(an::torpedoGone(an::torpedoTravel(3.0)));
    // Floatplane launch: trains out, waits on the carriage, is shot off, catapult trains back.
    CHECK(an::catapultLaunch(0.0).catapultTrain == 0.0f);
    CHECK(an::catapultLaunch(40.0).catapultTrain == doctest::Approx(45.0f));
    CHECK(an::catapultLaunch(59.0).travel < 0.0f);
    CHECK(an::catapultLaunch(61.0).travel == doctest::Approx(18.0f));
    CHECK_FALSE(an::catapultLaunch(62.0).gone);
    CHECK(an::catapultLaunch(70.0).gone);
    CHECK(an::catapultLaunch(89.0).catapultTrain == doctest::Approx(0.0f).epsilon(0.01));
    // Drill: trained and elevated while firing, salvos at 9/13/17 s, torpedoes at 23 s, home at the end.
    an::Drill d = an::drill(9.1);
    CHECK(d.trainBlend == doctest::Approx(1.0f));
    CHECK(d.elevation > 15.0f);
    CHECK(d.sinceSalvo == doctest::Approx(0.1));
    CHECK(an::drill(8.0).sinceSalvo > 100.0);
    CHECK(an::drill(23.5).sinceTorpedoes == doctest::Approx(0.5));
    CHECK(an::drill(20.0).sinceTorpedoes < 0.0);
    CHECK(an::drill(26.5).launch.travel > 0.0f);
    CHECK(an::drill(35.0).launch.gone);
    CHECK_FALSE(an::drill(41.0).launch.gone);
    CHECK(an::drill(43.9).trainBlend < 0.01f);
    CHECK(an::drill(an::kDrillCycle + 9.1).sinceSalvo == doctest::Approx(0.1));
    // The drill bearing is always one the mount can reach.
    struct Arc {
        double lo, hi;
        bool mirrored;
    } arcs[] = {{-150, 150, false}, {30, 150, true}, {30, 330, false}, {-150, -30, false}, {0, 180, false}, {-180, 0, false}};
    for (auto& a : arcs) CHECK(sim::inArc(an::drillBearing(a.lo, a.hi, a.mirrored), a.lo, a.hi, a.mirrored));
}
