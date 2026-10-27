#include "ai/Doctrine.h"

#include "core/FileSystem.h"
#include "sim/Group.h"

namespace it::ai {

double Doctrine::priority(sim::ShipType t) const {
    auto it = targetPriority.find(t);
    return it != targetPriority.end() ? it->second : 0.7;
}

Doctrine parseDoctrine(const Json& j) {
    Doctrine d;
    d.id = jstr(j, "id", "default");
    d.nation = sim::nationFromString(jstr(j, "nation"));
    d.aggression = jnum(j, "aggression", d.aggression);
    d.caution = jnum(j, "caution", d.caution);
    d.nightPreference = jnum(j, "night_preference", d.nightPreference);
    d.initiative = jnum(j, "initiative", d.initiative);
    d.gunRangeNight = jnum(j, "gun_range_night_m", d.gunRangeNight);
    d.gunRangeDay = jnum(j, "gun_range_day_m", d.gunRangeDay);
    d.requireIdentification = jbool(j, "require_identification", d.requireIdentification);
    std::string il = jstr(j, "illumination", "starshell");
    d.illumination = il == "searchlight" ? Illumination::Searchlight : (il == "none" ? Illumination::None : Illumination::Starshell);
    d.useAircraftFlares = jbool(j, "use_aircraft_flares", d.useAircraftFlares);
    d.chaseSalvos = jbool(j, "chase_salvos", d.chaseSalvos);
    d.torpedoFirst = jbool(j, "torpedo_first", d.torpedoFirst);
    d.holdFireUntilTorpedoImpact = jbool(j, "hold_fire_until_torpedo_impact", d.holdFireUntilTorpedoImpact);
    d.torpedoRangeNight = jnum(j, "torpedo_range_night_m", d.torpedoRangeNight);
    d.torpedoRangeDay = jnum(j, "torpedo_range_day_m", d.torpedoRangeDay);
    d.torpedoSpacingLengths = jnum(j, "torpedo_spacing_lengths", d.torpedoSpacingLengths);
    d.reloadAndReattack = jbool(j, "reload_and_reattack", d.reloadAndReattack);
    d.torpedoesPerSpread = jint(j, "torpedoes_per_spread", d.torpedoesPerSpread);
    d.reactionTime = jnum(j, "reaction_time_s", d.reactionTime);
    d.evasionSkill = jnum(j, "evasion_skill", d.evasionSkill);
    d.reportDelayMin = jnum(j, "report_delay_min_s", d.reportDelayMin);
    d.reportDelayMax = jnum(j, "report_delay_max_s", d.reportDelayMax);
    d.reportReliability = jnum(j, "report_reliability", d.reportReliability);
    d.assumeHostile = jnum(j, "assume_hostile", d.assumeHostile);
    d.formation = sim::formationFromString(jstr(j, "formation", "column"));
    d.columnSpacing = jnum(j, "column_spacing_m", d.columnSpacing);
    d.withdrawBeforeDawnMinutes = jnum(j, "withdraw_before_dawn_min", d.withdrawBeforeDawnMinutes);
    d.airCoverDelay = jnum(j, "air_cover_delay_s", d.airCoverDelay);
    d.forceRatioEngage = jnum(j, "force_ratio_engage", d.forceRatioEngage);
    d.forceRatioWithdraw = jnum(j, "force_ratio_withdraw", d.forceRatioWithdraw);
    const Json& tp = jobj(j, "target_priority");
    for (auto it = tp.begin(); it != tp.end(); ++it) d.targetPriority[sim::shipTypeFromString(it.key())] = it->get<double>();
    return d;
}

bool DoctrineLibrary::load(const std::string& dir) {
    doctrines_.clear();
    for (auto& p : fs::listFiles(dir, ".json"))
        if (auto j = loadJsonFile(p)) doctrines_.push_back(parseDoctrine(*j));
    return !doctrines_.empty();
}

const Doctrine& DoctrineLibrary::get(const std::string& id, sim::Nation fallbackNation) const {
    for (auto& d : doctrines_)
        if (d.id == id) return d;
    std::string fb = std::string(sim::nationId(fallbackNation)) + "_1942";
    for (auto& d : doctrines_)
        if (d.id == fb) return d;
    for (auto& d : doctrines_)
        if (d.nation == fallbackNation) return d;
    return default_;
}

}  // namespace it::ai
