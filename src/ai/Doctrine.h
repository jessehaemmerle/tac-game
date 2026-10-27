#pragma once
// Navy doctrine as data. Encodes how a navy fought in a given period (IJN night battle doctrine,
// early-war USN radar gunnery and identification caution, ...). Loaded from data/doctrine/*.json.
#include <map>
#include <string>
#include <vector>

#include "core/Json.h"
#include "sim/Group.h"
#include "sim/Types.h"

namespace it::ai {

enum class Illumination : uint8_t { None, Starshell, Searchlight };

struct Doctrine {
    std::string id = "default";
    sim::Nation nation = sim::Nation::Unknown;
    // Personality
    double aggression = 0.6;       // 0..1 willingness to close and accept risk
    double caution = 0.5;          // 0..1 weight of pessimistic force estimates
    double nightPreference = 0.5;  // 0..1 (IJN high)
    double initiative = 0.6;       // subordinates act without orders
    // Gunnery
    double gunRangeNight = 8000, gunRangeDay = 16000;  // preferred engagement range (m)
    bool requireIdentification = true;   // do not open fire on unidentified contacts unless fired upon
    Illumination illumination = Illumination::Starshell;
    bool useAircraftFlares = false;
    bool chaseSalvos = true;
    // Torpedoes
    bool torpedoFirst = false;            // launch torpedoes before revealing the force with gunfire
    bool holdFireUntilTorpedoImpact = false;
    double torpedoRangeNight = 5000, torpedoRangeDay = 10000;  // preferred launch range (m)
    double torpedoSpacingLengths = 0.9;   // spread spacing in target lengths
    bool reloadAndReattack = false;
    int torpedoesPerSpread = 4;
    // Command & control
    double reactionTime = 10.0;           // s (scaled by crew quality and bridge damage)
    double reportDelayMin = 30, reportDelayMax = 120;  // contact reports to the flag / other groups (s)
    double reportReliability = 0.8;       // probability a report gets through promptly
    double assumeHostile = 0.5;           // prior that an unidentified contact is an enemy
    double evasionSkill = 0.6;            // 0..1 how well torpedo tracks are combed
    sim::Formation formation = sim::Formation::Column;
    double columnSpacing = 900;
    // Strategic
    double withdrawBeforeDawnMinutes = 0; // > 0: retire so as to be out of air range at dawn
    double airCoverDelay = 0;             // s after the first air attack before the force breaks radio silence to call fighters
    double forceRatioEngage = 0.8;        // estimated own/enemy power needed to engage
    double forceRatioWithdraw = 0.4;      // below this, break off
    std::map<sim::ShipType, double> targetPriority;

    double priority(sim::ShipType t) const;
    double gunRange(bool night) const { return night ? gunRangeNight : gunRangeDay; }
    double torpedoRange(bool night) const { return night ? torpedoRangeNight : torpedoRangeDay; }
};

Doctrine parseDoctrine(const Json& j);
/// Loads all doctrines from a directory; lookup by id, falling back to "<nation>_1942" then "default".
class DoctrineLibrary {
public:
    bool load(const std::string& dir);
    const Doctrine& get(const std::string& id, sim::Nation fallbackNation = sim::Nation::Unknown) const;
    void add(const Doctrine& d) { doctrines_.push_back(d); }
    const std::vector<Doctrine>& all() const { return doctrines_; }

private:
    std::vector<Doctrine> doctrines_;
    Doctrine default_;
};

}  // namespace it::ai
