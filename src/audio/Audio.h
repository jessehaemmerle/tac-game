#pragma once
// XAudio2 sound engine (loaded dynamically, so the game runs without audio if XAudio2 is missing):
// positional effects with sound-propagation delay (a distant salvo is seen before it is heard),
// bridge voice reports in the selected language, ambience loops and interface sounds.
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/Math.h"
#include "sim/Events.h"
#include "sim/World.h"

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SubmixVoice;
struct IXAudio2SourceVoice;

namespace it::audio {

struct Clip {
    std::vector<uint8_t> pcm;  // 16-bit PCM
    uint32_t rate = 44100;
    uint16_t channels = 1;
    double duration() const { return channels && rate ? double(pcm.size()) / (2.0 * channels * rate) : 0.0; }
};

class AudioEngine {
public:
    AudioEngine();
    ~AudioEngine();
    bool init();
    void shutdown();
    /// Load data/audio/bank.json (sound ids -> WAV files, voice lines per language).
    void loadBank(const std::string& dir);
    void setVolumes(float master, float sfx, float voice, float ambience);
    void setLanguage(const std::string& lang) { language_ = lang; }
    void playUi(const char* id);
    /// Translate simulation events into positional sounds and voice reports.
    void onEvents(const std::vector<sim::Event>& events, const sim::World* w, const Vec3& listener, double listenerYaw, sim::Side player,
                  double timeScale);
    void update(double realDt, const sim::World* w, const Vec3& listener, bool paused);
    bool available() const { return xa_ != nullptr; }

private:
    struct Pending {
        double due = 0;
        std::string id;
        float volume = 1, pan = 0, pitch = 1;
    };
    struct Active {
        IXAudio2SourceVoice* voice = nullptr;
        bool loop = false;
        std::string id;
    };
    const Clip* pick(const std::string& id);
    void play(const std::string& id, float volume, float pan, float pitch, IXAudio2SubmixVoice* bus, bool loop = false, Active* out = nullptr);
    void voice(const std::string& key, int priority);
    void positional(const std::string& id, const Vec3& pos, double loudness, double reachM);

    void* dll_ = nullptr;
    IXAudio2* xa_ = nullptr;
    IXAudio2MasteringVoice* master_ = nullptr;
    IXAudio2SubmixVoice *sfxBus_ = nullptr, *voiceBus_ = nullptr, *ambBus_ = nullptr;
    std::map<std::string, std::vector<std::shared_ptr<Clip>>> sounds_;
    std::map<std::string, std::map<std::string, std::vector<std::shared_ptr<Clip>>>> voices_;  // lang -> key -> clips
    std::vector<Active> active_;
    std::deque<Pending> pending_;
    struct VoiceReq {
        std::string key;
        int priority;
        double queued;
    };
    std::deque<VoiceReq> voiceQueue_;
    IXAudio2SourceVoice* currentVoice_ = nullptr;
    std::map<std::string, double> voiceCooldown_;
    Active ambSea_, ambWind_;
    std::string language_ = "de";
    double clock_ = 0;
    Vec3 listener_;
    double listenerYaw_ = 0;
    uint32_t rng_ = 12345;
};

}  // namespace it::audio
