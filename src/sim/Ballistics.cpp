#include "sim/Ballistics.h"

#include "sim/Database.h"

namespace it::sim::ballistics {

double airDensity(double h) {
    h = std::max(0.0, h);
    if (h < 11000.0) {
        double t = 288.15 - 0.0065 * h;
        return 1.225 * std::pow(t / 288.15, 4.2559);
    }
    return 0.3639 * std::exp(-(h - 11000.0) / 6341.6);
}

double speedOfSound(double h) {
    double t = std::max(216.65, 288.15 - 0.0065 * std::max(0.0, h));
    return std::sqrt(1.4 * 287.05 * t);
}

double referenceDrag(double m) {
    // Piecewise-linear approximation of the G7 reference drag curve.
    static const double M[] = {0.0, 0.7, 0.85, 0.95, 1.0, 1.05, 1.2, 1.5, 2.0, 2.5, 3.0, 4.0};
    static const double C[] = {0.120, 0.120, 0.135, 0.200, 0.380, 0.400, 0.370, 0.320, 0.280, 0.245, 0.215, 0.180};
    constexpr int n = sizeof(M) / sizeof(M[0]);
    if (m <= M[0]) return C[0];
    for (int i = 1; i < n; ++i)
        if (m <= M[i]) return lerp(C[i - 1], C[i], (m - M[i - 1]) / (M[i] - M[i - 1]));
    return C[n - 1];
}

Vec3 acceleration(const Vec3& v, double alt, const ShellParams& p) {
    double speed = length(v);
    Vec3 a{0, 0, -kGravity};
    if (speed > 1e-6) {
        double area = kPi * 0.25 * p.caliberM * p.caliberM;
        double cd = referenceDrag(speed / speedOfSound(alt)) * p.formFactor;
        double drag = 0.5 * airDensity(alt) * speed * speed * cd * area / p.massKg;
        a -= v * (drag / speed);
    }
    return a;
}

void integrate(Vec3& pos, Vec3& vel, double dt, const ShellParams& p) {
    Vec3 k1v = acceleration(vel, pos.z, p);
    Vec3 k1x = vel;
    Vec3 k2v = acceleration(vel + k1v * (dt * 0.5), pos.z + k1x.z * dt * 0.5, p);
    Vec3 k2x = vel + k1v * (dt * 0.5);
    Vec3 k3v = acceleration(vel + k2v * (dt * 0.5), pos.z + k2x.z * dt * 0.5, p);
    Vec3 k3x = vel + k2v * (dt * 0.5);
    Vec3 k4v = acceleration(vel + k3v * dt, pos.z + k3x.z * dt, p);
    Vec3 k4x = vel + k3v * dt;
    pos += (k1x + k2x * 2.0 + k3x * 2.0 + k4x) * (dt / 6.0);
    vel += (k1v + k2v * 2.0 + k3v * 2.0 + k4v) * (dt / 6.0);
}

TrajectoryResult simulate(double v0, double elevDeg, const ShellParams& p, double dt, double muzzleHeight) {
    double e = elevDeg * kDegToRad;
    Vec3 pos{0, 0, muzzleHeight};
    Vec3 vel{0, v0 * std::cos(e), v0 * std::sin(e)};
    TrajectoryResult r;
    double t = 0;
    for (int i = 0; i < 20000; ++i) {
        Vec3 prevPos = pos, prevVel = vel;
        integrate(pos, vel, dt, p);
        t += dt;
        r.apex = std::max(r.apex, pos.z);
        if (pos.z <= 0.0 && vel.z < 0.0) {
            double f = prevPos.z / std::max(1e-9, prevPos.z - pos.z);
            r.range = lerp(prevPos.y, pos.y, f);
            r.timeOfFlight = t - dt + dt * f;
            Vec3 v = lerp(prevVel, vel, f);
            r.strikingVelocity = length(v);
            r.angleOfFallDeg = std::atan2(-v.z, v.y) * kRadToDeg;
            return r;
        }
    }
    r.range = pos.y;
    r.timeOfFlight = t;
    return r;
}

double calibrateFormFactor(double v0, double elevDeg, double targetRange, double massKg, double caliberM) {
    double lo = 0.15, hi = 4.0;
    for (int i = 0; i < 40; ++i) {
        double mid = 0.5 * (lo + hi);
        double r = simulate(v0, elevDeg, {massKg, caliberM, mid}).range;
        if (r > targetRange)
            lo = mid;  // too far: more drag
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

void buildTables(GunDef& gun) {
    if (gun.shells.empty()) return;
    double cal = gun.caliberMm / 1000.0;
    // The primary shell (first) is calibrated against the historical maximum range. Other shells share
    // its form factor scaled by sectional density so lighter shells (starshell) lose speed faster.
    ShellDef& primary = gun.shells.front();
    primary.formFactor = calibrateFormFactor(primary.muzzleVelocity, gun.maxRangeElevationDeg, gun.maxRange,
                                             primary.massKg, cal);
    for (size_t i = 1; i < gun.shells.size(); ++i) {
        ShellDef& s = gun.shells[i];
        s.formFactor = primary.formFactor * (s.kind == ShellKind::Starshell ? 1.35 : 1.0);
    }
    gun.table.clear();
    ShellParams p{primary.massKg, cal, primary.formFactor};
    double best = -1;
    for (double e = 0.0; e <= gun.maxElevationDeg + 1e-6; e += 0.5) {
        auto r = simulate(primary.muzzleVelocity, e, p);
        if (r.range <= best) break;  // beyond max-range elevation
        best = r.range;
        gun.table.push_back({e, r.range, r.timeOfFlight, r.angleOfFallDeg, r.strikingVelocity, r.apex});
    }
}

double beltPenetrationMm(double massKg, double caliberMm, double velocity, double obliquityDeg) {
    // De Marre formula, b[dm] = (v*sqrt(m) / (K*d^0.75))^(1/0.7), K tuned for WWII face-hardened plate.
    constexpr double K = 2250.0;
    double d = caliberMm / 100.0;
    double vn = velocity * std::cos(clamp(obliquityDeg, 0.0, 80.0) * kDegToRad);
    double b = std::pow(vn * std::sqrt(massKg) / (K * std::pow(d, 0.75)), 1.0 / 0.7);
    return b * 100.0;
}

double deckPenetrationMm(double massKg, double caliberMm, double velocity, double angleOfFallDeg) {
    // Homogeneous deck plate is softer (lower K) but impacts are highly oblique.
    constexpr double K = 2000.0;
    double d = caliberMm / 100.0;
    double obliquity = 90.0 - clamp(angleOfFallDeg, 1.0, 89.0);
    double vn = velocity * std::cos(std::min(obliquity, 80.0) * kDegToRad);
    double b = std::pow(vn * std::sqrt(massKg) / (K * std::pow(d, 0.75)), 1.0 / 0.7);
    // Shells at very flat angles ricochet off decks.
    if (angleOfFallDeg < 8.0) b *= angleOfFallDeg / 8.0;
    return b * 100.0;
}

}  // namespace it::sim::ballistics
