#include "ui/Settings.h"

#include "core/FileSystem.h"
#include "core/Json.h"

namespace it::ui {

bool Settings::load(const std::string& path) {
    auto j = loadJsonFile(path);
    if (!j) return false;
    language = jstr(*j, "language", language);
    metric = jbool(*j, "metric", metric);
    uiScale = float(jnum(*j, "ui_scale", uiScale));
    showTutorialHints = jbool(*j, "hints", showTutorialHints);
    fullscreen = jbool(*j, "fullscreen", fullscreen);
    vsync = jbool(*j, "vsync", vsync);
    msaa = jint(*j, "msaa", msaa);
    shadowSize = jint(*j, "shadow_size", shadowSize);
    exposureBias = float(jnum(*j, "exposure_bias", exposureBias));
    bloom = float(jnum(*j, "bloom", bloom));
    filmGrain = jbool(*j, "film_grain", filmGrain);
    masterVolume = float(jnum(*j, "master_volume", masterVolume));
    sfxVolume = float(jnum(*j, "sfx_volume", sfxVolume));
    voiceVolume = float(jnum(*j, "voice_volume", voiceVolume));
    ambienceVolume = float(jnum(*j, "ambience_volume", ambienceVolume));
    voiceReports = jbool(*j, "voice_reports", voiceReports);
    aiSkill = float(jnum(*j, "ai_skill", aiSkill));
    enemyCrewShift = jint(*j, "enemy_crew_shift", enemyCrewShift);
    autoEvade = jbool(*j, "auto_evade", autoEvade);
    revealEnemyAi = jbool(*j, "reveal_enemy_ai", revealEnemyAi);
    pauseOnContact = jbool(*j, "pause_on_contact", pauseOnContact);
    slowOnAlert = jbool(*j, "slow_on_alert", slowOnAlert);
    return true;
}

bool Settings::save(const std::string& path) const {
    Json j = Json::object();
    j["language"] = language;
    j["metric"] = metric;
    j["ui_scale"] = uiScale;
    j["hints"] = showTutorialHints;
    j["fullscreen"] = fullscreen;
    j["vsync"] = vsync;
    j["msaa"] = msaa;
    j["shadow_size"] = shadowSize;
    j["exposure_bias"] = exposureBias;
    j["bloom"] = bloom;
    j["film_grain"] = filmGrain;
    j["master_volume"] = masterVolume;
    j["sfx_volume"] = sfxVolume;
    j["voice_volume"] = voiceVolume;
    j["ambience_volume"] = ambienceVolume;
    j["voice_reports"] = voiceReports;
    j["ai_skill"] = aiSkill;
    j["enemy_crew_shift"] = enemyCrewShift;
    j["auto_evade"] = autoEvade;
    j["reveal_enemy_ai"] = revealEnemyAi;
    j["pause_on_contact"] = pauseOnContact;
    j["slow_on_alert"] = slowOnAlert;
    return fs::writeTextFile(path, j.dump(2));
}

}  // namespace it::ui
