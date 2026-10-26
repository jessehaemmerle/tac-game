#include "sim/Sensors.h"

#include "sim/World.h"

namespace it::sim::sensors {
namespace {

constexpr double kDayRangeCruiser = 28000.0;  // m, cruiser broadside in clear daylight
constexpr double kCruiserSilhouette = 2900.0;  // m^2

double sizeT(double displacement) {
    return clamp((std::log(displacement) - std::log(1500.0)) / (std::log(10000.0) - std::log(1500.0)), 0.0, 1.3);
}

/// Pick a (possibly wrong) type / class guess for a target, depending on crew skill.
void classify(World& w, const Ship& observer, const Ship& target, ClassLevel level, Measurement& m) {
    m.level = level;
    m.size = target.cls->size();
    if (level < ClassLevel::TypeKnown) return;
    double skill = crewFactor(observer.crew);
    double errChance = 0.28 * (1.25 - skill);
    ShipType t = target.cls->type;
    if (w.rng().chance(errChance)) {
        // Classic misidentification: over-estimating the enemy is far more common than under-estimating.
        static const ShipType up[] = {ShipType::BB, ShipType::BB, ShipType::CV, ShipType::BB, ShipType::CA, ShipType::CL,
                                      ShipType::CL, ShipType::DD, ShipType::DD, ShipType::DD, ShipType::SS, ShipType::CA,
                                      ShipType::CL, ShipType::CL, ShipType::Unknown};
        t = up[int(t)];
    }
    m.typeGuess = t;
    if (level < ClassLevel::ClassKnown) return;
    if (t == target.cls->type && !w.rng().chance(errChance * 0.8)) {
        m.classGuess = target.cls->id;
    } else {
        // Guess another class of the guessed type from the same navy.
        std::vector<const ShipClassDef*> cands;
        for (auto& c : w.db().shipClasses())
            if (c->type == t && c->nation == target.nation) cands.push_back(c.get());
        m.classGuess = cands.empty() ? target.cls->id : cands[size_t(w.rng().range(0, int(cands.size()) - 1))]->id;
    }
}

Measurement measure(World& w, const Ship& observer, const Ship& target, double rangeSigma, double bearingSigmaDeg, uint32_t sense) {
    Measurement m;
    m.observer = observer.id;
    m.truth = target.id;
    m.sense = sense;
    Vec2 d = target.pos - observer.pos;
    double r = length(d);
    double brg = bearingOf(d) + w.rng().normal() * bearingSigmaDeg;
    double rr = std::max(50.0, r + w.rng().normal() * rangeSigma);
    m.pos = observer.pos + bearingVector(brg) * rr;
    m.sigma = std::max(rangeSigma, r * bearingSigmaDeg * kDegToRad);
    return m;
}

}  // namespace

VisualFactors visualRange(const World& w, const Ship& observer, const Ship& target, const SensorDef* optics, double eyeHeight) {
    VisualFactors f;
    // Projected silhouette: bow-on targets are much harder to see.
    double aspect = std::fabs(std::sin(target.relativeBearing(observer.pos) * kDegToRad));
    double area = target.cls->silhouetteArea() * (0.25 + 0.75 * aspect);
    f.size = std::sqrt(area / kCruiserSilhouette);

    f.light = w.illuminationOn(target);
    double lightF = std::pow(clamp(f.light, 0.005, 1.0), 0.38);
    double back = w.backlight(observer.pos, target);
    lightF = std::max(lightF, lerp(lightF, 0.7, back));
    // Ships against an island backdrop merge into the dark mass of the land at night.
    if (f.light < 0.5 && w.terrain().landBehind(observer.pos, target.pos, 12000.0)) f.background = 0.55;

    bool night = w.env().ambientLight() < 0.3;
    double optic = 1.0;
    if (optics) optic = night ? optics->nightFactor : 1.0 + (optics->magnification - 7.0) * 0.01;
    double crew = crewFactor(observer.crew) * (1.0 - 0.5 * observer.dmg.crewLoss);
    double r = kDayRangeCruiser * f.size * lightF * f.background * optic * crew;
    // Bow waves and wakes of fast ships catch the eye at night.
    if (night) r *= 1.0 + 0.25 * smoothstep(15.0, 30.0, target.speedKn());
    // Burning ships and searchlights are beacons.
    if (target.dmg.totalFireIntensity() > 0.05) r = std::max(r, 6000.0 + 14000.0 * std::min(1.0, target.dmg.totalFireIntensity()));
    if (target.orders.searchlight && target.searchlightOnSince >= 0) r = std::max(r, 22000.0);

    const auto& wx = w.env().weather();
    f.weather = w.env().squallTransmission(observer.pos, target.pos);
    if (w.env().inSquall(observer.pos)) f.weather *= 0.6;
    r = std::min(r, wx.visibilityKm * 1000.0 * 1.1) * f.weather;

    f.horizon = 3570.0 * 1.08 * (std::sqrt(std::max(1.0, eyeHeight)) + std::sqrt(std::max(1.0, target.silhouetteHeight())));
    f.range = std::min(r, f.horizon);
    return f;
}

double radarRange(const World& w, const Ship& observer, const SensorDef& radar, double antennaHeight, const Ship& target) {
    double r = lerp(radar.rangeSmall, radar.rangeLarge, sizeT(target.displacement()));
    double horizon = 4120.0 * (std::sqrt(std::max(1.0, antennaHeight)) + std::sqrt(std::max(1.0, target.silhouetteHeight() * 0.6)));
    r = std::min(r, horizon);
    // Land clutter: metric radars in particular could not separate ships close to islands.
    double dLand = w.terrain().distanceToLand(target.pos, 10000.0);
    double clutter = radar.landClutter * (1.0 - smoothstep(1200.0, 6000.0, dLand));
    if (w.terrain().landBehind(observer.pos, target.pos, 4000.0)) clutter = std::max(clutter, radar.landClutter * 0.6);
    r *= 1.0 - 0.85 * clutter;
    // Radar shadow behind islands.
    Vec3 a{observer.pos, antennaHeight}, b{target.pos, target.silhouetteHeight() * 0.5};
    if (distance(observer.pos, target.pos) < r && !w.terrain().lineOfSight(a, b)) return 0.0;
    return r;
}

double sweepProbability(double range, double detRange, double sweepPeriod, double steepness) {
    if (detRange <= 1.0) return 0.0;
    double p30 = 1.0 / (1.0 + std::exp(steepness * (range / detRange - 1.0)));
    return 1.0 - std::pow(1.0 - p30, sweepPeriod / 30.0);
}

void sweep(World& w, Ship& observer, SensorState& sensor) {
    const SensorDef* def = sensor.def ? sensor.def->def : nullptr;
    if (!def) return;
    double now = w.time();
    double crew = crewFactor(observer.crew) * (1.0 - 0.6 * observer.dmg.crewLoss);
    ContactPicture& pic = w.picture(observer.side);
    // Random equipment / operator outages (radars of 1942 were temperamental).
    if (def->kind == SensorKind::Radar && w.rng().chance((1.0 - def->reliability) * def->sweepPeriod / 60.0)) {
        sensor.outageUntil = now + w.rng().uniform(60.0, 420.0);
        return;
    }
    for (auto& tp : w.ships()) {
        Ship& target = *tp;
        if (!target.alive() || target.side == observer.side) continue;
        double r = distance(observer.pos, target.pos);
        if (r > 45000.0) continue;
        if (def->kind == SensorKind::Optical) {
            VisualFactors vf = visualRange(w, observer, target, def, sensor.def->height);
            if (vf.range < 50.0 || r > vf.range * 1.6) continue;
            if (!w.rng().chance(sweepProbability(r, vf.range, def->sweepPeriod) * clamp(crew * 1.1, 0.3, 1.0))) continue;
            double q = r / vf.range;
            Measurement m = measure(w, observer, target, 0.12 * r, def->bearingAccuracyDeg, kSenseVisual);
            ClassLevel lvl = q < 0.30 ? ClassLevel::Identified
                           : q < 0.42 ? ClassLevel::ClassKnown
                           : q < 0.58 ? ClassLevel::TypeKnown
                           : q < 0.78 ? ClassLevel::SizeKnown
                                      : ClassLevel::Detected;
            classify(w, observer, target, lvl, m);
            if (q < 0.65) {
                // Course and speed estimated from aspect and bow wave.
                double cErr = w.rng().normal() * (8.0 + 25.0 * q);
                double sErr = w.rng().normal() * (1.5 + 3.0 * q) * 0.514444;
                m.hasVelocity = true;
                m.velocity = bearingVector(target.heading + cErr) * std::max(0.0, target.speed + sErr);
                m.velSigma = 2.5 + 6.0 * q;
            }
            pic.report(m, now);
        } else if (def->kind == SensorKind::Radar) {
            if (!def->surfaceSearch) continue;
            double rr = radarRange(w, observer, *def, sensor.def->height, target);
            if (rr < 50.0 || r > rr * 1.5) continue;
            if (!w.rng().chance(sweepProbability(r, rr, def->sweepPeriod, 7.0) * clamp(crew, 0.3, 1.0))) continue;
            Measurement m = measure(w, observer, target, def->rangeAccuracy, def->bearingAccuracyDeg, kSenseRadar);
            // Blip size gives a rough size class on microwave sets.
            m.level = (def->bearingAccuracyDeg <= 1.5 && r < 0.8 * rr) ? ClassLevel::SizeKnown : ClassLevel::Detected;
            m.size = m.level >= ClassLevel::SizeKnown ? target.cls->size() : SizeClass::Unknown;
            bool created = false;
            pic.report(m, now, &created);
        }
    }
}

void gunFlash(World& w, const Ship& firer, double calibreMm) {
    double amb = w.env().ambientLight();
    double now = w.time();
    for (auto& op : w.ships()) {
        Ship& obs = *op;
        if (!obs.operational() || obs.side == firer.side) continue;
        double r = distance(obs.pos, firer.pos);
        // Flashes light up the night sky; by day the smoke and flash are visible within normal visual range.
        double flashRange = (amb < 0.35 ? 18000.0 : 9000.0) * std::sqrt(clamp(calibreMm / 150.0, 0.4, 1.6));
        flashRange *= w.env().squallTransmission(obs.pos, firer.pos);
        if (r > flashRange) continue;
        double p = 0.75 * crewFactor(obs.crew) * (1.0 - r / flashRange * 0.5);
        if (!w.rng().chance(p)) continue;
        Measurement m = measure(w, obs, firer, 0.18 * r, 0.6, kSenseFlash);
        m.hostileAct = true;
        m.level = ClassLevel::Detected;
        m.size = calibreMm >= 180 ? SizeClass::Large : SizeClass::Unknown;
        w.picture(obs.side).report(m, now);
    }
}

void torpedoWatch(World& w, Ship& observer) {
    double crew = crewFactor(observer.crew) * (1.0 - 0.5 * observer.dmg.crewLoss);
    for (auto& t : w.torpedoes()) {
        if (!t.alive || t.side == observer.side) continue;
        double r = distance(t.pos, observer.pos);
        if (r > 4000.0) continue;
        bool already = false;
        for (EntityId id : t.sightedBy)
            if (id == observer.id) already = true;
        if (already) continue;
        double light = w.illuminationAt({t.pos, 0.0});
        double sightRange = 2800.0 * std::sqrt(t.def->wakeVisibility) * std::pow(clamp(light, 0.02, 1.0), 0.3) * crew;
        if (r > sightRange || !w.rng().chance(0.35)) continue;
        t.sightedBy.push_back(observer.id);
        Event e;
        e.type = EventType::TorpedoSighted;
        e.time = w.time();
        e.a = observer.id;
        e.b = t.id;
        e.side = observer.side;
        e.pos = {t.pos, 0.0};
        e.value = t.heading;
        e.value2 = t.speed;
        w.emit(e);
    }
}

}  // namespace it::sim::sensors
