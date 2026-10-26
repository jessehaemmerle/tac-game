#pragma once
// Timing of weapon and equipment animations: gun recoil, torpedo launch, the floatplane launch from
// its catapult and the scripted weapons drill of the unit reference. Shared by the renderer (which
// applies them to the model parts) and the tests.
#include <cmath>

namespace it::procgen::anim {

/// Run-back of a barrel (m) `t` seconds after it fired: a sharp recoil, then the run-out.
float recoil(double t, double calibreMm);
/// Delay of barrel `b` in a salvo (s): the guns of a mount do not fire in the same instant.
inline double barrelDelay(int barrel) { return 0.05 * barrel; }

/// Travel of a torpedo out of its tube (m) `t` seconds after its launch (0 before).
float torpedoTravel(double t);
/// Drop below the tube axis once the torpedo is clear of the muzzle (m, negative).
float torpedoDrop(float travel, float tubeLength);
/// Delay between the tubes of one mount (s).
inline double tubeDelay(int tube) { return 0.35 * tube; }
/// A launched torpedo has left the picture (fallen into the sea).
inline bool torpedoGone(float travel) { return travel > 30.0f; }

/// Floatplane launch, `t` seconds into the 90 s launch phase of an aircraft mission: the catapult
/// trains outboard, the aircraft runs up its engine, is shot off at 60 s and climbs away.
struct Launch {
    float catapultTrain = 0;    // deg outboard
    float travel = -1;          // m along the catapult; < 0 still on the carriage
    bool gone = false;          // flown out of the picture
};
Launch catapultLaunch(double t);
constexpr double kLaunchShot = 60.0;

/// Weapons drill of the unit reference (repeats every kDrillCycle seconds).
constexpr double kDrillCycle = 44.0;
struct Drill {
    float trainBlend = 0;          // 0 stowed .. 1 on the drill bearing
    float elevation = 0;           // gun elevation (deg)
    double sinceSalvo = 1e9;       // s since the last salvo
    float torpTrainBlend = 0;      // torpedo mounts: 0 stowed .. 1 trained out
    double sinceTorpedoes = -1;    // s since the tubes were fired (< 0: loaded)
    Launch launch;                 // catapult and floatplane (gone: hidden until the next one is ready)
    float searchlightTrain = 0, searchlightElev = 0;  // deg
    float aaSweep = 0, aaElevation = 0;               // light AA: offset from the stowed bearing, elevation (deg)
    float rudder = 0;              // deg (positive: trailing edge to port, ship turns to starboard)
    float propellerTurns = 0;      // revolutions so far
};
Drill drill(double t);

/// Point in a mount's arc for the drill: a bearing on the beam the mount can reach.
double drillBearing(double arcMin, double arcMax, bool mirrored);

}  // namespace it::procgen::anim
