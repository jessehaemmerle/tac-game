#include "sim/Types.h"
#include "sim/Group.h"

#include <algorithm>
#include <cctype>

namespace it::sim {
namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
}  // namespace

const char* sideKey(Side s) {
    switch (s) {
        case Side::Allies: return "side.allies";
        case Side::Axis: return "side.axis";
        default: return "side.neutral";
    }
}

Side sideFromString(const std::string& str) {
    std::string s = lower(str);
    if (s == "allies" || s == "allied" || s == "blue") return Side::Allies;
    if (s == "axis" || s == "red") return Side::Axis;
    return Side::Neutral;
}

const char* nationId(Nation n) {
    switch (n) {
        case Nation::USN: return "usn";
        case Nation::RAN: return "ran";
        case Nation::RN: return "rn";
        case Nation::IJN: return "ijn";
        case Nation::KM: return "km";
        case Nation::RM: return "rm";
        case Nation::MN: return "mn";
        default: return "unknown";
    }
}

Nation nationFromString(const std::string& str) {
    std::string s = lower(str);
    if (s == "usn") return Nation::USN;
    if (s == "ran") return Nation::RAN;
    if (s == "rn") return Nation::RN;
    if (s == "ijn") return Nation::IJN;
    if (s == "km") return Nation::KM;
    if (s == "rm") return Nation::RM;
    if (s == "mn") return Nation::MN;
    return Nation::Unknown;
}

Side defaultSide(Nation n) {
    switch (n) {
        case Nation::USN:
        case Nation::RAN:
        case Nation::RN:
        case Nation::MN: return Side::Allies;
        case Nation::IJN:
        case Nation::KM:
        case Nation::RM: return Side::Axis;
        default: return Side::Neutral;
    }
}

static const char* kTypeIds[] = {"BB", "BC", "CV", "CA", "CL", "CLAA", "DD", "DE", "TB", "PT", "SS", "AP", "AK", "AO", "?"};

const char* shipTypeId(ShipType t) { return kTypeIds[int(t)]; }

ShipType shipTypeFromString(const std::string& str) {
    for (int i = 0; i < int(ShipType::Unknown); ++i)
        if (str == kTypeIds[i]) return ShipType(i);
    return ShipType::Unknown;
}

SizeClass sizeOf(ShipType t) {
    switch (t) {
        case ShipType::BB:
        case ShipType::BC:
        case ShipType::CV:
        case ShipType::CA:
        case ShipType::AP:
        case ShipType::AK:
        case ShipType::AO: return SizeClass::Large;
        case ShipType::CL:
        case ShipType::CLAA: return SizeClass::Medium;
        case ShipType::DD:
        case ShipType::DE:
        case ShipType::TB:
        case ShipType::PT:
        case ShipType::SS: return SizeClass::Small;
        default: return SizeClass::Unknown;
    }
}

bool isCombatant(ShipType t) { return t != ShipType::AP && t != ShipType::AK && t != ShipType::AO && t != ShipType::Unknown; }

CrewQuality crewFromString(const std::string& str) {
    std::string s = lower(str);
    if (s == "green") return CrewQuality::Green;
    if (s == "veteran") return CrewQuality::Veteran;
    if (s == "elite") return CrewQuality::Elite;
    return CrewQuality::Regular;
}

const char* crewKey(CrewQuality q) {
    static const char* keys[] = {"crew.green", "crew.regular", "crew.veteran", "crew.elite"};
    return keys[int(q)];
}

double crewFactor(CrewQuality q) {
    static const double f[] = {0.72, 0.9, 1.0, 1.12};
    return f[int(q)];
}

ShellKind shellKindFromString(const std::string& str) {
    std::string s = lower(str);
    if (s == "ap") return ShellKind::AP;
    if (s == "he") return ShellKind::HE;
    if (s == "star" || s == "starshell") return ShellKind::Starshell;
    return ShellKind::Common;
}

const char* shellKindKey(ShellKind k) {
    static const char* keys[] = {"shell.ap", "shell.common", "shell.he", "shell.star"};
    return keys[int(k)];
}

SensorKind sensorKindFromString(const std::string& str) {
    std::string s = lower(str);
    if (s == "optical") return SensorKind::Optical;
    if (s == "hydrophone") return SensorKind::Hydrophone;
    if (s == "sonar") return SensorKind::Sonar;
    if (s == "radar_detector") return SensorKind::RadarDetector;
    return SensorKind::Radar;
}

const char* formationKey(Formation f) {
    static const char* k[] = {"formation.column", "formation.line_abreast", "formation.echelon_right", "formation.echelon_left",
                              "formation.screen", "formation.free"};
    return k[int(f)];
}

Formation formationFromString(const std::string& str) {
    std::string s = lower(str);
    if (s == "line_abreast" || s == "line") return Formation::LineAbreast;
    if (s == "echelon_right") return Formation::EchelonRight;
    if (s == "echelon_left") return Formation::EchelonLeft;
    if (s == "screen") return Formation::Screen;
    if (s == "free") return Formation::Free;
    return Formation::Column;
}

AirRole airRoleFromString(const std::string& str) {
    std::string s = lower(str);
    if (s == "fighter") return AirRole::Fighter;
    if (s == "torpedo") return AirRole::Torpedo;
    if (s == "dive") return AirRole::Dive;
    if (s == "level") return AirRole::Level;
    if (s == "patrol") return AirRole::Patrol;
    return AirRole::Recon;
}

const char* airRoleId(AirRole r) {
    switch (r) {
        case AirRole::Fighter: return "fighter";
        case AirRole::Torpedo: return "torpedo";
        case AirRole::Dive: return "dive";
        case AirRole::Level: return "level";
        case AirRole::Patrol: return "patrol";
        default: return "recon";
    }
}

const char* airRoleKey(AirRole r) {
    switch (r) {
        case AirRole::Fighter: return "air.role.fighter";
        case AirRole::Torpedo: return "air.role.torpedo";
        case AirRole::Dive: return "air.role.dive";
        case AirRole::Level: return "air.role.level";
        case AirRole::Patrol: return "air.role.patrol";
        default: return "air.role.recon";
    }
}

}  // namespace it::sim
