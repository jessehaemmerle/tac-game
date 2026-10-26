#pragma once
// Exterior ballistics (point-mass with Mach-dependent drag and standard atmosphere) and armour
// penetration. Range tables are computed once per gun at database load and calibrated so the
// computed maximum range matches the historical figure.
#include "core/Math.h"

namespace it::sim {
struct GunDef;
struct ShellDef;
}

namespace it::sim::ballistics {

double airDensity(double altitudeM);
double speedOfSound(double altitudeM);
/// Reference drag coefficient (G7-like, long boat-tailed projectile) as function of Mach.
double referenceDrag(double mach);

struct ShellParams {
    double massKg = 100, caliberM = 0.2, formFactor = 1.0;
};
/// Acceleration (gravity + drag) acting on a shell; z is up.
Vec3 acceleration(const Vec3& velocity, double altitude, const ShellParams& p);
/// One RK4 step of position/velocity.
void integrate(Vec3& pos, Vec3& vel, double dt, const ShellParams& p);

struct TrajectoryResult {
    double range = 0, timeOfFlight = 0, angleOfFallDeg = 0, strikingVelocity = 0, apex = 0;
};
TrajectoryResult simulate(double muzzleVelocity, double elevationDeg, const ShellParams& p, double dt = 0.1,
                          double muzzleHeight = 0.0);

/// Find the drag form factor that reproduces `targetRange` at `elevationDeg`.
double calibrateFormFactor(double muzzleVelocity, double elevationDeg, double targetRange, double massKg,
                           double caliberM);
/// Calibrate all shells of the gun and build its range table.
void buildTables(GunDef& gun);

/// Belt (vertical) armour penetration in mm of face-hardened plate. obliquity = angle from plate normal.
double beltPenetrationMm(double massKg, double caliberMm, double velocity, double obliquityDeg);
/// Deck (horizontal) penetration in mm of homogeneous plate given angle of fall.
double deckPenetrationMm(double massKg, double caliberMm, double velocity, double angleOfFallDeg);

}  // namespace it::sim::ballistics
