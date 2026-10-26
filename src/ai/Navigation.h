#pragma once
// Steering primitives: follow-the-leader in column, station keeping in line/echelon/screen,
// land avoidance from the chart and collision avoidance with friendly ships.
#include "ai/SideView.h"

namespace it::ai::nav {

struct Steer {
    double course = 0;
    double speedKn = 0;
};

/// Free water (m) ahead on the given course before shoal water or land, up to maxDist.
double freeDistance(const sim::Terrain& chart, const Vec2& pos, double course, double draft, double maxDist);
/// Course closest to `desired` with at least `need` metres of free water.
double avoidLand(const sim::Terrain& chart, const Vec2& pos, double desired, double draft, double need);
/// Adjust course to avoid collisions with own ships (CPA check over the next ~90 s).
double avoidFriends(const SideView& v, const sim::Ship& self, double course, double speedKn);

/// Position on the guide's trail `back` metres behind the guide (falls back to straight astern).
Vec2 trailPoint(const sim::Group& g, const sim::Ship& guide, double back);
/// Follow-the-leader in column: slot 1 = second ship.
Steer followTrail(const sim::Group& g, const sim::Ship& self, int slot, const sim::Ship& guide);
/// Station keeping in line abreast, echelon or screen formations.
Steer keepStation(const sim::Group& g, const sim::Ship& self, int slot, const sim::Ship& guide, int memberCount);
/// Station offset (guide frame: x forward, y starboard) for a slot.
Vec2 stationOffset(const sim::Group& g, int slot, int memberCount);

}  // namespace it::ai::nav
