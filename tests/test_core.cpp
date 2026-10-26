#include <doctest/doctest.h>

#include "core/Localization.h"
#include "core/Math.h"
#include "core/Random.h"

using namespace it;

TEST_CASE("angles wrap and bearings are nautical") {
    CHECK(wrap360(-10.0) == doctest::Approx(350.0));
    CHECK(wrap180(270.0) == doctest::Approx(-90.0));
    CHECK(angleDiff(350.0, 10.0) == doctest::Approx(20.0));
    CHECK(bearingOf({0, 1}) == doctest::Approx(0.0));
    CHECK(bearingOf({1, 0}) == doctest::Approx(90.0));
    CHECK(bearingOf({0, -1}) == doctest::Approx(180.0));
    Vec2 v = bearingVector(270.0);
    CHECK(v.x == doctest::Approx(-1.0));
    CHECK(v.y == doctest::Approx(0.0).epsilon(1e-9));
}

TEST_CASE("intercept solution hits a moving target") {
    double t;
    Vec2 aim;
    REQUIRE(solveIntercept({0, 0}, 20.0, {1000, 0}, {0, 10}, t, aim));
    CHECK(length(aim) == doctest::Approx(20.0 * t).epsilon(1e-6));
    CHECK(aim.y == doctest::Approx(10.0 * t).epsilon(1e-6));
    CHECK_FALSE(solveIntercept({0, 0}, 5.0, {1000, 0}, {20, 0}, t, aim));
}

TEST_CASE("matrix inverse round-trips") {
    Mat4f m = Mat4f::trs({3, -2, 5}, Quatf::axisAngle({0, 1, 0}, 0.7f), {2, 2, 2});
    Mat4f i = m.inverse();
    Vec3f p{1, 2, 3};
    Vec3f q = i.transformPoint(m.transformPoint(p));
    CHECK(q.x == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(q.y == doctest::Approx(2.0f).epsilon(1e-4));
    CHECK(q.z == doctest::Approx(3.0f).epsilon(1e-4));
}

TEST_CASE("rng is deterministic per seed") {
    Rng a(42), b(42), c(43);
    for (int i = 0; i < 100; ++i) CHECK(a.next() == b.next());
    bool differs = false;
    Rng a2(42);
    for (int i = 0; i < 10; ++i) differs |= a2.next() != c.next();
    CHECK(differs);
    Rng n(7);
    double sum = 0, sq = 0;
    for (int i = 0; i < 20000; ++i) {
        double x = n.normal();
        sum += x;
        sq += x * x;
    }
    CHECK(sum / 20000 == doctest::Approx(0.0).epsilon(0.03));
    CHECK(sq / 20000 == doctest::Approx(1.0).epsilon(0.05));
}

TEST_CASE("localisation loads both languages and falls back") {
    REQUIRE(loc::load("de"));
    CHECK(std::string(tr("menu.quit")) != "menu.quit");
    CHECK(std::string(tr("does.not.exist")) == "does.not.exist");
    loc::load("en");
    CHECK(std::string(tr("menu.quit")) == "Quit");
}
