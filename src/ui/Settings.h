#pragma once
// User settings (persisted as JSON in the per-user data directory).
#include <string>

namespace it::ui {

struct Settings {
    // General
    std::string language = "de";
    bool metric = false;          // false: nautical miles / yards; true: kilometres / metres
    float uiScale = 1.0f;
    bool showTutorialHints = true;
    // Display & graphics
    bool fullscreen = false;
    bool vsync = true;
    int msaa = 4;
    int shadowSize = 2048;
    float exposureBias = 0.0f;
    float bloom = 0.06f;
    bool filmGrain = true;
    // Audio
    float masterVolume = 0.8f, sfxVolume = 1.0f, voiceVolume = 1.0f, ambienceVolume = 0.6f;
    bool voiceReports = true;
    // Gameplay
    float aiSkill = 0.7f;         // enemy reaction & tactical skill (never extra information)
    int enemyCrewShift = 0;       // -1 .. +1
    bool autoEvade = true;        // own ships comb sighted torpedo tracks on their own
    bool revealEnemyAi = false;   // show enemy AI decisions during the battle (spoiler)
    bool pauseOnContact = false;  // pause when a new enemy contact is reported

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

}  // namespace it::ui
