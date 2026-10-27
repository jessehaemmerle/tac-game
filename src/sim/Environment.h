#pragma once
// Time of day, astronomy (sun & moon), weather and the resulting ambient light level. Light is the
// single most important factor in WWII night surface actions: it drives visual detection, spotting
// of fall of shot and the value of starshells, searchlights and burning ships.
#include <vector>

#include "core/Json.h"
#include "core/Math.h"
#include "core/Random.h"

namespace it::sim {

struct DateTime {
    int year = 1942, month = 8, day = 9;
    double hour = 1.5;        // local clock time (hours)
    double utcOffset = 11.0;  // local = UTC + offset
};

struct Squall {
    Vec2 pos;
    Vec2 velocity;
    double radius = 2000;
    double intensity = 0.6;  // 0..1 visual attenuation inside the squall
};

struct WeatherConfig {
    double cloudCover = 0.3;     // 0..1
    double cloudBaseM = 1500;    // height of the cloud base (aircraft above it see and are seen only through gaps)
    double visibilityKm = 20.0;  // meteorological visibility
    double seaState = 2;         // Douglas sea state 0..9
    double windDirDeg = 120;     // direction the wind blows FROM
    double windSpeedKn = 10;
    int squalls = 0;             // number of rain squalls to generate
    double squallIntensity = 0.7;
};

class Environment {
public:
    void configure(double latDeg, double lonDeg, const DateTime& start, const WeatherConfig& weather, const Vec2& areaCenter,
                   double areaRadius, Rng& rng);
    void update(double simTime, double dt);
    static void parse(const Json& j, DateTime& dt, WeatherConfig& w);

    // Clock
    double simTime() const { return simTime_; }
    DateTime now() const;
    double localHour() const;

    // Astronomy (computed on update, refreshed every few sim seconds)
    double sunAltitudeDeg() const { return sunAlt_; }
    double sunAzimuthDeg() const { return sunAz_; }
    double moonAltitudeDeg() const { return moonAlt_; }
    double moonAzimuthDeg() const { return moonAz_; }
    double moonIllumination() const { return moonPhase_; }  // illuminated fraction 0..1
    Vec3 sunDirection() const;   // unit vector, x east, y north, z up
    Vec3 moonDirection() const;
    /// Seconds from now until the sun climbs above `altDeg` (e.g. -6 = civil dawn); large if not within 24 h.
    double secondsUntilSunAbove(double altDeg) const;

    // Light & weather
    double ambientLight() const { return ambient_; }  // 0 (pitch dark) .. 1 (full daylight)
    bool isNight() const { return ambient_ < 0.3; }
    const WeatherConfig& weather() const { return weather_; }
    WeatherConfig& weather() { return weather_; }
    const std::vector<Squall>& squalls() const { return squalls_; }
    /// Visual transmission factor along a line (1 = clear, small inside rain squalls).
    double squallTransmission(const Vec2& a, const Vec2& b) const;
    bool inSquall(const Vec2& p) const;
    double latitude() const { return lat_; }
    double longitude() const { return lon_; }

    /// Pure astronomy helpers (UT in hours), returning altitude/azimuth in degrees. Exposed for tests.
    static void sunPosition(int y, int m, int d, double ut, double lat, double lon, double& alt, double& az);
    static void moonPosition(int y, int m, int d, double ut, double lat, double lon, double& alt, double& az, double& phase);
    static double lightLevel(double sunAlt, double moonAlt, double moonPhase, double cloud);

private:
    void computeSky();

    double lat_ = -9.1, lon_ = 159.8;
    DateTime start_;
    WeatherConfig weather_;
    std::vector<Squall> squalls_;
    Vec2 areaCenter_;
    double areaRadius_ = 50000;
    double simTime_ = 0;
    double lastSkyUpdate_ = -1e9;
    double sunAlt_ = -30, sunAz_ = 0, moonAlt_ = -30, moonAz_ = 0, moonPhase_ = 0, ambient_ = 0.03;
};

}  // namespace it::sim
