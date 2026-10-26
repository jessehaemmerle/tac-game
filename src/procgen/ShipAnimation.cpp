#include "procgen/ShipAnimation.h"

#include <algorithm>

#include "core/Math.h"
#include "sim/Database.h"

namespace it::procgen::anim {

float recoil(double t, double calibreMm) {
    if (t < 0.0) return 0.0f;
    // Recoil length grows with the calibre: about 0.4 m for a 5-inch gun, 0.65 m for an 8-inch.
    double len = std::min(0.9, calibreMm / 1000.0 * 3.2);
    const double kick = 0.07;  // s to full recoil
    double r = t < kick ? len * (t / kick) : len * std::exp(-(t - kick) * 2.8);
    return r < 0.002 ? 0.0f : float(r);
}

float torpedoTravel(double t) {
    if (t <= 0.0) return 0.0f;
    // Impulse launch: leaves the tube at about 12 m/s and keeps going as it drops.
    return float(12.0 * t + 6.0 * t * t);
}

float torpedoDrop(float travel, float tubeLength) {
    float clear = std::max(0.0f, travel - tubeLength);
    return -0.05f * clear * clear;
}

Launch catapultLaunch(double t) {
    Launch l;
    // Train out over 20 s, hold while the engine warms up, shoot at kLaunchShot, train back.
    double out = smoothstep(5.0, 25.0, t), back = smoothstep(70.0, 85.0, t);
    l.catapultTrain = float(45.0 * out * (1.0 - back));
    if (t >= kLaunchShot) {
        double s = t - kLaunchShot;
        // 18 m of catapult in about one second, then flying away at 30 m/s.
        l.travel = float(s < 1.0 ? 18.0 * s * s : 18.0 + 30.0 * (s - 1.0));
        l.gone = s > 6.0;  // hidden before the catapult trains back in
    }
    return l;
}

Drill drill(double t) {
    Drill d;
    double c = std::fmod(std::max(0.0, t), kDrillCycle);
    // Train out (0-6 s) and elevate (2-7 s), three salvos (9, 13, 17 s), back to stowed (36-42 s).
    double home = smoothstep(36.0, 42.0, c);
    d.trainBlend = float(smoothstep(0.5, 6.0, c) * (1.0 - home));
    d.elevation = float(18.0 * smoothstep(2.0, 7.0, c) * (1.0 - smoothstep(34.0, 38.0, c)));
    for (double s : {9.0, 13.0, 17.0})
        if (c >= s) d.sinceSalvo = c - s;
    // Torpedoes: train out 18-22 s, fire at 23 s, reloaded at 40 s.
    d.torpTrainBlend = float(smoothstep(18.0, 22.0, c) * (1.0 - smoothstep(30.0, 36.0, c)));
    d.sinceTorpedoes = c >= 23.0 && c < 40.0 ? c - 23.0 : -1.0;
    // Floatplane: the launch sequence compressed so that it is shot off at 26 s; a fresh aircraft is
    // on the catapult again from 40 s.
    if (c >= 6.0 && c < 40.0) {
        double lt = c < 26.0 ? 5.0 + (c - 6.0) * (kLaunchShot - 5.0) / 20.0 : kLaunchShot + (c - 26.0) * 2.0;
        d.launch = catapultLaunch(lt);
        if (c >= 26.0) d.launch.travel = catapultLaunch(kLaunchShot + (c - 26.0)).travel;  // real speed in the air
        d.launch.gone = c > 32.0;
    }
    d.searchlightTrain = float(50.0 * std::sin(t * 0.21));
    d.searchlightElev = float(4.0 + 6.0 * std::sin(t * 0.33));
    d.aaSweep = float(35.0 * std::sin(t * 0.27));
    d.aaElevation = float(30.0 + 25.0 * std::sin(t * 0.19 + 1.0));
    d.rudder = float(-20.0 * std::sin(t * 0.12));
    d.propellerTurns = float(t * 2.5);
    return d;
}

double drillBearing(double arcMin, double arcMax, bool mirrored) {
    for (double b : {70.0, -70.0, 110.0, -110.0, 30.0, -30.0, 150.0, -150.0, 0.0, 180.0})
        if (sim::inArc(b, arcMin, arcMax, mirrored)) return b;
    return 0.5 * (arcMin + arcMax);
}

}  // namespace it::procgen::anim
