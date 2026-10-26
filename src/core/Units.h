#pragma once
// Unit conversions. Simulation uses SI internally (m, s, kg); UI shows naval units.
namespace it::units {

constexpr double kKnot = 0.514444;        // m/s per knot
constexpr double kNauticalMile = 1852.0;  // m
constexpr double kYard = 0.9144;          // m
constexpr double kFoot = 0.3048;          // m
constexpr double kInch = 0.0254;          // m
constexpr double kPound = 0.45359237;     // kg

constexpr double knotsToMs(double kn) { return kn * kKnot; }
constexpr double msToKnots(double ms) { return ms / kKnot; }
constexpr double nmToM(double nm) { return nm * kNauticalMile; }
constexpr double mToNm(double m) { return m / kNauticalMile; }
constexpr double mToYards(double m) { return m / kYard; }
constexpr double yardsToM(double y) { return y * kYard; }

}  // namespace it::units
