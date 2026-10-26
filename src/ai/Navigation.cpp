#include "ai/Navigation.h"

namespace it::ai::nav {

double freeDistance(const sim::Terrain& chart, const Vec2& pos, double course, double draft, double maxDist) {
    if (chart.coastlines().empty()) return maxDist;
    Vec2 dir = bearingVector(course);
    for (double d = 100.0; d <= maxDist; d += 120.0) {
        Vec2 p = pos + dir * d;
        if (chart.shoal(p, draft + 4.0) || chart.distanceToLand(p, 1000.0) < 220.0) return d;
    }
    return maxDist;
}

double avoidLand(const sim::Terrain& chart, const Vec2& pos, double desired, double draft, double need) {
    if (chart.coastlines().empty()) return desired;
    if (freeDistance(chart, pos, desired, draft, need) >= need) return desired;
    double bestCourse = desired, bestFree = -1;
    for (int k = 1; k <= 12; ++k) {
        for (int sgn : {1, -1}) {
            double c = wrap360(desired + sgn * k * 15.0);
            double f = freeDistance(chart, pos, c, draft, need);
            if (f >= need) return c;
            if (f > bestFree) {
                bestFree = f;
                bestCourse = c;
            }
        }
    }
    return bestCourse;
}

double avoidFriends(const SideView& v, const sim::Ship& self, double course, double speedKn) {
    double c = course;
    for (int iter = 0; iter < 4; ++iter) {
        Vec2 myVel = bearingVector(c) * (speedKn * 0.514444);
        bool conflict = false;
        for (auto* o : v.ownShips()) {
            if (o == &self || !o->operational()) continue;
            double d = distance(o->pos, self.pos);
            if (d > 3000.0) continue;
            double t = closestApproachTime(self.pos, myVel, o->pos, o->velocity());
            if (t > 90.0) continue;
            double cpa = distance(self.pos + myVel * t, o->pos + o->velocity() * t);
            double safe = (self.cls->length + o->cls->length) * 0.5 + 150.0;
            if (cpa < safe) {
                // Give way to ships on our starboard side and to larger ships (rule of the road, roughly).
                double rel = self.relativeBearing(o->pos);
                c = wrap360(c + (rel > -10.0 && rel < 112.5 ? 25.0 : -20.0));
                conflict = true;
                break;
            }
        }
        if (!conflict) break;
    }
    return c;
}

Vec2 trailPoint(const sim::Group& g, const sim::Ship& guide, double back) {
    Vec2 prev = guide.pos;
    double acc = 0;
    for (int i = int(g.trail.size()) - 1; i >= 0; --i) {
        double seg = distance(prev, g.trail[size_t(i)]);
        if (acc + seg >= back) return lerp(prev, g.trail[size_t(i)], (back - acc) / std::max(1e-6, seg));
        acc += seg;
        prev = g.trail[size_t(i)];
    }
    return prev - guide.forward() * (back - acc);
}

Steer followTrail(const sim::Group& g, const sim::Ship& self, int slot, const sim::Ship& guide) {
    double back = slot * g.spacing;
    Vec2 station = trailPoint(g, guide, back);
    double lookahead = std::max(350.0, self.speed * 35.0);
    Vec2 aim = trailPoint(g, guide, std::max(0.0, back - lookahead));
    Steer st;
    double dist = distance(self.pos, station);
    if (dist > 2500.0) {
        st.course = bearingTo(self.pos, aim);
        st.speedKn = self.cls->maxSpeedKn;
        return st;
    }
    st.course = bearingTo(self.pos, aim);
    // Along-track error: positive when we are behind our station.
    double along = dot(station - self.pos, guide.forward());
    if (distance(self.pos, aim) < 1.0) along = 0;
    st.speedKn = clamp(g.speedKn + along / 120.0, std::max(0.0, g.speedKn - 8.0), self.cls->maxSpeedKn);
    return st;
}

Vec2 stationOffset(const sim::Group& g, int slot, int n) {
    double s = g.spacing;
    switch (g.formation) {
        case sim::Formation::LineAbreast: {
            int side = (slot % 2 == 1) ? 1 : -1;
            int rank = (slot + 1) / 2;
            return {0.0, side * rank * s};
        }
        case sim::Formation::EchelonRight: return {-slot * s * 0.7, slot * s * 0.7};
        case sim::Formation::EchelonLeft: return {-slot * s * 0.7, -slot * s * 0.7};
        case sim::Formation::Screen: {
            int screens = std::max(1, n - 1);
            double a = screens == 1 ? 0.0 : -70.0 + 140.0 * double(slot - 1) / (screens - 1);
            double r = std::max(2500.0, s * 2.5);
            return {std::cos(a * kDegToRad) * r, std::sin(a * kDegToRad) * r};
        }
        default: return {-slot * s, 0.0};
    }
}

Steer keepStation(const sim::Group& g, const sim::Ship& self, int slot, const sim::Ship& guide, int n) {
    Vec2 off = stationOffset(g, slot, n);
    Vec2 station = guide.pos + guide.forward() * off.x + guide.right() * off.y;
    Steer st;
    double dist = distance(self.pos, station);
    double gc = g.course;
    if (dist > 1800.0) {
        st.course = bearingTo(self.pos, station + guide.velocity() * 60.0);
        st.speedKn = self.cls->maxSpeedKn;
        return st;
    }
    Vec2 err = station - self.pos;
    double alongErr = dot(err, bearingVector(gc));
    double crossErr = dot(err, bearingVector(gc + 90.0));
    st.course = wrap360(gc + clamp(crossErr / 25.0, -35.0, 35.0));
    st.speedKn = clamp(g.speedKn + alongErr / 100.0, std::max(0.0, g.speedKn - 8.0), self.cls->maxSpeedKn);
    return st;
}

}  // namespace it::ai::nav
