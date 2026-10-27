#pragma once
// Display formatting of naval quantities (ranges, bearings, clock times, tonnage) in either nautical
// (nm / yards) or metric units.
#include <string>

#include "sim/Types.h"

namespace it::ui {

std::string fmtRange(double metres, bool metric);
std::string fmtRangeShort(double metres, bool metric);
std::string fmtBearing(double deg);
std::string fmtClock(double localHour);
std::string fmtDuration(double seconds);
std::string fmtTonnes(double t);
std::string fmtKnots(double kn);
std::string fmtPercent(double fraction);
std::string fmtInt(long long v);
/// A name that may be a localisation key (scenario groups, flights, bases) or a literal (ship names).
std::string locName(const std::string& keyOrLiteral);
/// Localised names for enums.
const char* sideName(sim::Side s);
const char* nationName(sim::Nation n);
const char* shipTypeName(sim::ShipType t);
const char* shipTypeAbbrev(sim::ShipType t);
const char* crewName(sim::CrewQuality c);

}  // namespace it::ui
