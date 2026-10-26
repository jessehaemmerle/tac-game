#pragma once
#include <cstdint>
#include <string>

namespace it::sim {

using EntityId = uint32_t;
constexpr EntityId kNoEntity = 0;

enum class Side : uint8_t { Allies = 0, Axis = 1, Neutral = 2 };
constexpr int kSideCount = 3;
inline int sideIndex(Side s) { return int(s); }
inline Side enemyOf(Side s) { return s == Side::Allies ? Side::Axis : (s == Side::Axis ? Side::Allies : Side::Neutral); }
const char* sideKey(Side s);  // localisation key
Side sideFromString(const std::string& s);

enum class Nation : uint8_t { USN, RAN, RN, IJN, KM, RM, MN, Unknown };
const char* nationId(Nation n);  // "usn"
Nation nationFromString(const std::string& s);
Side defaultSide(Nation n);

enum class ShipType : uint8_t { BB, BC, CV, CA, CL, CLAA, DD, DE, TB, PT, SS, AP, AK, AO, Unknown };
const char* shipTypeId(ShipType t);  // "CA"
ShipType shipTypeFromString(const std::string& s);
/// Rough size category used by sensors/AI when the exact type is unknown.
enum class SizeClass : uint8_t { Unknown, Small, Medium, Large };
SizeClass sizeOf(ShipType t);
bool isCombatant(ShipType t);

enum class CrewQuality : uint8_t { Green = 0, Regular = 1, Veteran = 2, Elite = 3 };
CrewQuality crewFromString(const std::string& s);
const char* crewKey(CrewQuality q);
/// Generic efficiency multiplier for crew quality (0.7 .. 1.15).
double crewFactor(CrewQuality q);

enum class ShellKind : uint8_t { AP, Common, HE, Starshell };
ShellKind shellKindFromString(const std::string& s);
const char* shellKindKey(ShellKind k);

enum class SensorKind : uint8_t { Radar, Optical, Hydrophone, Sonar, RadarDetector };
SensorKind sensorKindFromString(const std::string& s);

/// Weapon control status, as in modern naval games.
enum class WeaponsStatus : uint8_t { Hold = 0, Tight = 1, Free = 2 };

}  // namespace it::sim
