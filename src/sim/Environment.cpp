#include "sim/Environment.h"

namespace it::sim {
namespace {

double rev(double x) { return x - std::floor(x / 360.0) * 360.0; }
double sind(double x) { return std::sin(x * kDegToRad); }
double cosd(double x) { return std::cos(x * kDegToRad); }
double atan2d(double y, double x) { return std::atan2(y, x) * kRadToDeg; }

// Days since 2000 Jan 0.0 UT (Schlyter, "How to compute planetary positions").
double dayNumber(int y, int m, int d, double ut) {
    long dn = 367L * y - (7L * (y + (m + 9) / 12)) / 4 + (275L * m) / 9 + d - 730530L;
    return double(dn) + ut / 24.0;
}

void equatorialToHorizontal(double ra, double dec, double d, double ut, double lat, double lon, double& alt, double& az) {
    // Sun mean longitude for sidereal time.
    double ws = 282.9404 + 4.70935e-5 * d;
    double ms = rev(356.0470 + 0.9856002585 * d);
    double gmst0 = rev(ws + ms + 180.0) / 15.0;  // hours
    double lst = gmst0 + ut + lon / 15.0;
    double ha = rev(lst * 15.0 - ra);
    double x = cosd(ha) * cosd(dec), y = sind(ha) * cosd(dec), z = sind(dec);
    double xhor = x * sind(lat) - z * cosd(lat);
    double yhor = y;
    double zhor = x * cosd(lat) + z * sind(lat);
    az = rev(atan2d(yhor, xhor) + 180.0);
    alt = atan2d(zhor, std::sqrt(xhor * xhor + yhor * yhor));
}

void sunEcliptic(double d, double& lonEcl, double& r, double& ms) {
    double w = 282.9404 + 4.70935e-5 * d;
    double e = 0.016709 - 1.151e-9 * d;
    ms = rev(356.0470 + 0.9856002585 * d);
    double E = ms + kRadToDeg * e * sind(ms) * (1.0 + e * cosd(ms));
    double x = cosd(E) - e, y = sind(E) * std::sqrt(1.0 - e * e);
    r = std::sqrt(x * x + y * y);
    lonEcl = rev(atan2d(y, x) + w);
}

}  // namespace

void Environment::sunPosition(int y, int m, int dd, double ut, double lat, double lon, double& alt, double& az) {
    double d = dayNumber(y, m, dd, ut);
    double slon, r, ms;
    sunEcliptic(d, slon, r, ms);
    double ecl = 23.4393 - 3.563e-7 * d;
    double xe = r * cosd(slon), ye = r * sind(slon) * cosd(ecl), ze = r * sind(slon) * sind(ecl);
    double ra = rev(atan2d(ye, xe)), dec = atan2d(ze, std::sqrt(xe * xe + ye * ye));
    equatorialToHorizontal(ra, dec, d, ut, lat, lon, alt, az);
}

void Environment::moonPosition(int y, int m, int dd, double ut, double lat, double lon, double& alt, double& az,
                               double& phase) {
    double d = dayNumber(y, m, dd, ut);
    double N = rev(125.1228 - 0.0529538083 * d), i = 5.1454, w = rev(318.0634 + 0.1643573223 * d);
    double a = 60.2666, e = 0.054900, M = rev(115.3654 + 13.0649929509 * d);
    double E = M + kRadToDeg * e * sind(M) * (1.0 + e * cosd(M));
    for (int k = 0; k < 5; ++k) E = E - (E - kRadToDeg * e * sind(E) - M) / (1.0 - e * cosd(E));
    double xv = a * (cosd(E) - e), yv = a * std::sqrt(1.0 - e * e) * sind(E);
    double v = atan2d(yv, xv), r = std::sqrt(xv * xv + yv * yv);
    double xh = r * (cosd(N) * cosd(v + w) - sind(N) * sind(v + w) * cosd(i));
    double yh = r * (sind(N) * cosd(v + w) + cosd(N) * sind(v + w) * cosd(i));
    double zh = r * (sind(v + w) * sind(i));
    double mlon = atan2d(yh, xh), mlat = atan2d(zh, std::sqrt(xh * xh + yh * yh));
    // Main perturbations.
    double slon, sr, ms;
    sunEcliptic(d, slon, sr, ms);
    double ws = 282.9404 + 4.70935e-5 * d;
    double Ls = ms + ws, Lm = M + w + N, D = Lm - Ls, F = Lm - N;
    mlon += -1.274 * sind(M - 2 * D) + 0.658 * sind(2 * D) - 0.186 * sind(ms) - 0.059 * sind(2 * M - 2 * D) -
            0.057 * sind(M - 2 * D + ms) + 0.053 * sind(M + 2 * D) + 0.046 * sind(2 * D - ms) + 0.041 * sind(M - ms) -
            0.035 * sind(D) - 0.031 * sind(M + ms) - 0.015 * sind(2 * F - 2 * D) + 0.011 * sind(M - 4 * D);
    mlat += -0.173 * sind(F - 2 * D) - 0.055 * sind(M - F - 2 * D) - 0.046 * sind(M + F - 2 * D) + 0.033 * sind(F + 2 * D) +
            0.017 * sind(2 * M + F);
    r += -0.58 * cosd(M - 2 * D) - 0.46 * cosd(2 * D);
    double ecl = 23.4393 - 3.563e-7 * d;
    double xg = r * cosd(mlon) * cosd(mlat), yg = r * sind(mlon) * cosd(mlat), zg = r * sind(mlat);
    double xe = xg, ye = yg * cosd(ecl) - zg * sind(ecl), ze = yg * sind(ecl) + zg * cosd(ecl);
    double ra = rev(atan2d(ye, xe)), dec = atan2d(ze, std::sqrt(xe * xe + ye * ye));
    equatorialToHorizontal(ra, dec, d, ut, lat, lon, alt, az);
    alt -= std::asin(1.0 / r) * kRadToDeg * cosd(alt);  // topocentric parallax
    double elong = std::acos(clamp(cosd(slon - mlon) * cosd(mlat), -1.0, 1.0));
    phase = (1.0 - std::cos(elong)) * 0.5;
}

double Environment::lightLevel(double sunAlt, double moonAlt, double moonPhase, double cloud) {
    static const double H[] = {-18, -12, -6, 0, 6};
    static const double Lv[] = {0.0, 0.04, 0.18, 0.5, 1.0};
    double sun = 0;
    if (sunAlt >= H[4])
        sun = 1.0;
    else if (sunAlt > H[0]) {
        for (int k = 1; k < 5; ++k)
            if (sunAlt <= H[k]) {
                sun = lerp(Lv[k - 1], Lv[k], (sunAlt - H[k - 1]) / (H[k] - H[k - 1]));
                break;
            }
    }
    sun *= 1.0 - 0.35 * cloud;
    double moon = moonAlt > 0 ? 0.2 * std::pow(moonPhase, 1.5) * std::sqrt(sind(moonAlt)) * (1.0 - 0.85 * cloud) : 0.0;
    double stars = 0.025 * (1.0 - 0.7 * cloud);
    return clamp(sun + moon + stars, 0.0, 1.0);
}

void Environment::parse(const Json& j, DateTime& dt, WeatherConfig& w) {
    const Json& d = jobj(j, "date");
    dt.year = jint(d, "year", dt.year);
    dt.month = jint(d, "month", dt.month);
    dt.day = jint(d, "day", dt.day);
    dt.hour = jnum(d, "hour", dt.hour);
    dt.utcOffset = jnum(d, "utc_offset", dt.utcOffset);
    const Json& wj = jobj(j, "weather");
    w.cloudCover = jnum(wj, "cloud_cover", w.cloudCover);
    w.visibilityKm = jnum(wj, "visibility_km", w.visibilityKm);
    w.seaState = jnum(wj, "sea_state", w.seaState);
    w.windDirDeg = jnum(wj, "wind_dir", w.windDirDeg);
    w.windSpeedKn = jnum(wj, "wind_kn", w.windSpeedKn);
    w.squalls = jint(wj, "squalls", w.squalls);
    w.squallIntensity = jnum(wj, "squall_intensity", w.squallIntensity);
}

void Environment::configure(double latDeg, double lonDeg, const DateTime& start, const WeatherConfig& weather,
                            const Vec2& areaCenter, double areaRadius, Rng& rng) {
    lat_ = latDeg;
    lon_ = lonDeg;
    start_ = start;
    weather_ = weather;
    areaCenter_ = areaCenter;
    areaRadius_ = areaRadius;
    squalls_.clear();
    Vec2 drift = bearingVector(wrap360(weather.windDirDeg + 180.0)) * (weather.windSpeedKn * 0.514444);
    for (int k = 0; k < weather.squalls; ++k) {
        Squall s;
        double ang = rng.uniform(0, 360), dist = std::sqrt(rng.uniform()) * areaRadius;
        s.pos = areaCenter + bearingVector(ang) * dist;
        s.radius = rng.uniform(1500, 4500);
        s.intensity = clamp(weather.squallIntensity * rng.uniform(0.7, 1.15), 0.1, 0.95);
        s.velocity = drift;
        squalls_.push_back(s);
    }
    simTime_ = 0;
    lastSkyUpdate_ = -1e9;
    computeSky();
}

DateTime Environment::now() const {
    DateTime d = start_;
    d.hour = start_.hour + simTime_ / 3600.0;
    while (d.hour >= 24.0) {  // simple day roll-over (scenarios are short; month ends handled loosely)
        d.hour -= 24.0;
        d.day += 1;
    }
    return d;
}

double Environment::localHour() const { return now().hour; }

void Environment::computeSky() {
    DateTime d = now();
    double ut = d.hour - d.utcOffset;
    int day = d.day;
    while (ut < 0) {
        ut += 24;
        day -= 1;
    }
    while (ut >= 24) {
        ut -= 24;
        day += 1;
    }
    sunPosition(d.year, d.month, day, ut, lat_, lon_, sunAlt_, sunAz_);
    moonPosition(d.year, d.month, day, ut, lat_, lon_, moonAlt_, moonAz_, moonPhase_);
    ambient_ = lightLevel(sunAlt_, moonAlt_, moonPhase_, weather_.cloudCover);
}

void Environment::update(double simTime, double dt) {
    simTime_ = simTime;
    for (auto& s : squalls_) {
        s.pos += s.velocity * dt;
        // Wrap squalls that drift out of the area back to the upwind side.
        Vec2 rel = s.pos - areaCenter_;
        if (length(rel) > areaRadius_ * 1.3) s.pos = areaCenter_ - rel * 0.95;
    }
    if (simTime_ - lastSkyUpdate_ > 10.0) {
        lastSkyUpdate_ = simTime_;
        computeSky();
    }
}

Vec3 Environment::sunDirection() const {
    return {sind(sunAz_) * cosd(sunAlt_), cosd(sunAz_) * cosd(sunAlt_), sind(sunAlt_)};
}
Vec3 Environment::moonDirection() const {
    return {sind(moonAz_) * cosd(moonAlt_), cosd(moonAz_) * cosd(moonAlt_), sind(moonAlt_)};
}

double Environment::secondsUntilSunAbove(double altDeg) const {
    DateTime d = now();
    for (int minutes = 0; minutes <= 24 * 60; minutes += 5) {
        double hour = d.hour + minutes / 60.0;
        double ut = hour - d.utcOffset;
        int day = d.day;
        while (ut < 0) { ut += 24; day -= 1; }
        while (ut >= 24) { ut -= 24; day += 1; }
        double alt, az;
        sunPosition(d.year, d.month, day, ut, lat_, lon_, alt, az);
        if (alt > altDeg) return minutes * 60.0;
    }
    return 1e9;
}

double Environment::squallTransmission(const Vec2& a, const Vec2& b) const {
    double t = 1.0;
    Vec2 ab = b - a;
    double len = length(ab);
    if (len < 1.0) return inSquall(a) ? 1.0 - 0.3 : 1.0;
    Vec2 dir = ab / len;
    for (auto& s : squalls_) {
        // Length of the segment inside the squall disc.
        Vec2 ac = s.pos - a;
        double proj = clamp(dot(ac, dir), 0.0, len);
        double dist = length(a + dir * proj - s.pos);
        if (dist >= s.radius) continue;
        double half = std::sqrt(s.radius * s.radius - dist * dist);
        double t0 = std::max(0.0, dot(ac, dir) - half), t1 = std::min(len, dot(ac, dir) + half);
        double inside = std::max(0.0, t1 - t0);
        // Exponential attenuation: a full squall diameter attenuates by `intensity`.
        t *= std::exp(std::log(std::max(1e-3, 1.0 - s.intensity)) * inside / (2.0 * s.radius));
    }
    return t;
}

bool Environment::inSquall(const Vec2& p) const {
    for (auto& s : squalls_)
        if (lengthSq(p - s.pos) < s.radius * s.radius) return true;
    return false;
}

}  // namespace it::sim
