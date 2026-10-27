#pragma once
// Detection models. Each sweep of a sensor produces noisy measurements that feed the observer
// side's ContactPicture. No sensor ever reveals more than a real operator could have known.
#include "sim/Contacts.h"

namespace it::sim {
class World;
class Ship;
struct SensorDef;
struct SensorState;

namespace sensors {

struct VisualFactors {
    double range = 0;     // resulting detection range (m)
    double light = 0;     // light on target
    double size = 0;      // silhouette factor
    double background = 1;
    double weather = 1;
    double horizon = 0;
};

/// Visual detection range of `target` by `observer` (lookouts with the given optics).
VisualFactors visualRange(const World& w, const Ship& observer, const Ship& target, const SensorDef* optics, double eyeHeight);
/// Radar detection range of target (0 if blocked by terrain / beyond radar horizon).
double radarRange(const World& w, const Ship& observer, const SensorDef& radar, double antennaHeight, const Ship& target);
/// Probability of detection on one sweep given the 30-second cumulative detection curve.
double sweepProbability(double range, double detRange, double sweepPeriod, double steepness = 6.0);

/// Run one sweep of a sensor; pushes measurements into the observer side's picture.
void sweep(World& w, Ship& observer, SensorState& sensor);
/// A ship fired its guns: enemy observers may see the flashes.
void gunFlash(World& w, const Ship& firer, double calibreMm);
/// Lookouts search for torpedo tracks.
void torpedoWatch(World& w, Ship& observer);
/// Air search radars and lookouts look for aircraft formations (call every few seconds).
void airWatch(World& w, Ship& observer, double interval);

}  // namespace sensors
}  // namespace it::sim
