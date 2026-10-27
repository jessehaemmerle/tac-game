#include "audio/Audio.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xaudio2.h>

#include <algorithm>
#include <cmath>
#include <cstring>

#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Log.h"

namespace it::audio {

namespace {

using CreateFn = HRESULT(WINAPI*)(IXAudio2**, UINT32, UINT32);

bool loadWav(const std::filesystem::path& path, Clip& out) {
    auto data = fs::readBinaryFile(path);
    if (!data || data->size() < 44) return false;
    const uint8_t* p = data->data();
    size_t n = data->size();
    if (std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) return false;
    size_t off = 12;
    bool fmt = false;
    while (off + 8 <= n) {
        uint32_t size;
        std::memcpy(&size, p + off + 4, 4);
        const uint8_t* body = p + off + 8;
        if (off + 8 + size > n) size = uint32_t(n - off - 8);
        if (std::memcmp(p + off, "fmt ", 4) == 0 && size >= 16) {
            uint16_t format, channels, bits;
            uint32_t rate;
            std::memcpy(&format, body, 2);
            std::memcpy(&channels, body + 2, 2);
            std::memcpy(&rate, body + 4, 4);
            std::memcpy(&bits, body + 14, 2);
            if ((format != 1 && format != 0xFFFE) || bits != 16) return false;
            out.channels = channels;
            out.rate = rate;
            fmt = true;
        } else if (std::memcmp(p + off, "data", 4) == 0 && fmt) {
            out.pcm.assign(body, body + size);
            return !out.pcm.empty();
        }
        off += 8 + size + (size & 1);
    }
    return false;
}

std::string voiceKeyFor(const sim::Event& e, const sim::World& w, sim::Side me) {
    auto own = [&](sim::EntityId id) {
        const sim::Ship* s = w.ship(id);
        return s && (me == sim::Side::Neutral || s->side == me);
    };
    using T = sim::EventType;
    switch (e.type) {
        case T::ContactNew: return e.side == me ? ((e.value2 > 0.5) ? "contact_radar" : "contact") : "";
        case T::ContactIdentified: return e.side == me ? "identified" : "";
        case T::ContactLost: return e.side == me ? "contact_lost" : "";
        case T::TorpedoSighted: return own(e.a) ? "torpedo_sighted" : "";
        case T::ShellHit: return own(e.b) ? "hit_taken" : own(e.a) ? "hit_scored" : "";
        case T::Straddle: return own(e.a) ? "straddle" : "";
        case T::TorpedoHit: return own(e.b) ? "torpedo_hit_taken" : own(e.a) ? "torpedo_hit_scored" : "";
        case T::FireStarted: return own(e.a) ? "fire" : "";
        case T::ShipSunk: return own(e.a) ? "ship_lost" : "enemy_sinking";
        case T::MagazineExplosion: return "magazine";
        case T::StarshellBurst: return own(e.a) ? "" : "starshell_enemy";
        case T::SearchlightOn: return own(e.a) ? "" : "searchlight_enemy";
        case T::FlareDropped: return own(e.a) ? "" : "flare_enemy";
        case T::TorpedoLaunched: return own(e.a) ? "torpedoes_away" : "";
        case T::UnderFire: return own(e.a) ? "under_fire" : "";
        case T::Collision: return own(e.a) ? "collision" : "";
        case T::ShipGrounded: return own(e.a) ? "grounded" : "";
        case T::OrderAcknowledged: return e.side == me ? e.text : "";
        case T::AirAttack: return own(e.b) && e.side != me ? "under_fire" : "";
        case T::Report:
            if (e.side != me) return "";
            if (e.text == "report.rudder_jammed") return "rudder_jammed";
            if (e.text == "report.magazine_flooded") return "magazine_flooded";
            if (e.text == "report.torpedo_reloaded") return "torpedo_reloaded";
            if (e.text == "report.capsized") return "capsized";
            return "";
        default: return "";
    }
}

int voicePriority(const std::string& k) {
    if (k == "torpedo_sighted" || k == "torpedo_hit_taken" || k == "magazine" || k == "ship_lost" || k == "general_quarters") return 3;
    if (k == "hit_taken" || k == "fire" || k == "contact" || k == "contact_radar" || k == "searchlight_enemy" || k == "starshell_enemy") return 2;
    return 1;
}

}  // namespace

AudioEngine::AudioEngine() = default;
AudioEngine::~AudioEngine() { shutdown(); }

bool AudioEngine::init() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    (void)hr;
    const wchar_t* dlls[] = {L"xaudio2_9.dll", L"xaudio2_9redist.dll", L"xaudio2_8.dll"};
    for (auto* d : dlls)
        if ((dll_ = LoadLibraryW(d)) != nullptr) break;
    if (!dll_) return false;
    auto create = reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(HMODULE(dll_), "XAudio2Create")));
    if (!create || FAILED(create(&xa_, 0, 0x00000001 /* XAUDIO2_DEFAULT_PROCESSOR */))) {
        xa_ = nullptr;
        return false;
    }
    if (FAILED(xa_->CreateMasteringVoice(&master_))) {
        xa_->Release();
        xa_ = nullptr;
        return false;
    }
    xa_->CreateSubmixVoice(&sfxBus_, 2, 44100);
    xa_->CreateSubmixVoice(&voiceBus_, 2, 44100);
    xa_->CreateSubmixVoice(&ambBus_, 2, 44100);
    IT_LOG_INFO("XAudio2 ready");
    return true;
}

void AudioEngine::shutdown() {
    if (!xa_) return;
    for (auto& a : active_)
        if (a.voice) a.voice->DestroyVoice();
    active_.clear();
    for (Active* a : {&ambSea_, &ambWind_, &fireLoop_})
        if (a->voice) a->voice->DestroyVoice();
    ambSea_ = ambWind_ = fireLoop_ = {};
    currentVoice_ = nullptr;
    for (auto* b : {sfxBus_, voiceBus_, ambBus_})
        if (b) b->DestroyVoice();
    sfxBus_ = voiceBus_ = ambBus_ = nullptr;
    if (master_) master_->DestroyVoice();
    master_ = nullptr;
    xa_->Release();
    xa_ = nullptr;
    if (dll_) FreeLibrary(HMODULE(dll_));
    dll_ = nullptr;
}

void AudioEngine::loadBank(const std::string& dir) {
    auto j = loadJsonFile(std::filesystem::path(dir) / "bank.json");
    if (!j) {
        IT_LOG_WARN("Audio bank not found in %s", dir.c_str());
        return;
    }
    size_t count = 0;
    auto loadList = [&](const Json& arr, std::vector<std::shared_ptr<Clip>>& out) {
        for (const auto& f : arr) {
            auto c = std::make_shared<Clip>();
            if (loadWav(std::filesystem::path(dir) / f.get<std::string>(), *c)) {
                out.push_back(c);
                ++count;
            }
        }
    };
    if (j->contains("sounds"))
        for (auto& [id, arr] : (*j)["sounds"].items()) loadList(arr, sounds_[id]);
    if (j->contains("voices"))
        for (auto& [lang, table] : (*j)["voices"].items())
            for (auto& [key, arr] : table.items()) loadList(arr, voices_[lang][key]);
    IT_LOG_INFO("Audio bank: %zu clips", count);
}

void AudioEngine::setVolumes(float master, float sfx, float voice, float amb) {
    if (!xa_) return;
    master_->SetVolume(master);
    sfxBus_->SetVolume(sfx);
    voiceBus_->SetVolume(voice);
    ambBus_->SetVolume(amb);
}

const Clip* AudioEngine::pick(const std::string& id) {
    auto it = sounds_.find(id);
    if (it == sounds_.end() || it->second.empty()) return nullptr;
    rng_ = rng_ * 1664525u + 1013904223u;
    return it->second[(rng_ >> 8) % it->second.size()].get();
}

void AudioEngine::play(const std::string& id, float volume, float pan, float pitch, IXAudio2SubmixVoice* bus, bool loop, Active* out) {
    if (!xa_ || !bus) return;
    const Clip* c = pick(id);
    if (!c) return;
    if (active_.size() > 48 && !loop) return;
    WAVEFORMATEX wf{};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = c->channels;
    wf.nSamplesPerSec = c->rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = WORD(c->channels * 2);
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    XAUDIO2_SEND_DESCRIPTOR send{0, bus};
    XAUDIO2_VOICE_SENDS sends{1, &send};
    IXAudio2SourceVoice* v = nullptr;
    if (FAILED(xa_->CreateSourceVoice(&v, &wf, XAUDIO2_VOICE_USEFILTER, 4.0f, nullptr, &sends))) return;
    XAUDIO2_BUFFER buf{};
    buf.AudioBytes = UINT32(c->pcm.size());
    buf.pAudioData = c->pcm.data();
    buf.Flags = XAUDIO2_END_OF_STREAM;
    if (loop) buf.LoopCount = XAUDIO2_LOOP_INFINITE;
    v->SubmitSourceBuffer(&buf);
    v->SetVolume(volume);
    v->SetFrequencyRatio(std::clamp(pitch, 0.25f, 4.0f));
    pan = std::clamp(pan, -1.0f, 1.0f);
    float l = std::sqrt(0.5f * (1.0f - pan)), r = std::sqrt(0.5f * (1.0f + pan));
    if (c->channels == 1) {
        float m[2] = {l, r};
        v->SetOutputMatrix(bus, 1, 2, m);
    } else {
        float m[4] = {l * 1.4142f, 0.0f, 0.0f, r * 1.4142f};
        v->SetOutputMatrix(bus, 2, 2, m);
    }
    v->Start();
    Active a{v, loop, id};
    if (out) *out = a;
    else active_.push_back(a);
}

void AudioEngine::playUi(const char* id) {
    if (xa_) play(std::string("ui_") + id, 0.6f, 0.0f, 1.0f, sfxBus_);
}

void AudioEngine::positional(const std::string& id, const Vec3& pos, double loudness, double reach, double refDistance) {
    double d = length(pos - listener_);
    if (d > reach) return;
    double v = loudness * std::pow(refDistance / (refDistance + d), 1.1);
    if (v < 0.01) return;
    double brg = std::atan2(pos.x - listener_.x, pos.z - listener_.z) * kRadToDeg;
    double rel = wrap180(brg - listenerYaw_) * kDegToRad;
    Pending p;
    p.due = clock_ + d / 343.0;  // sound travels at 343 m/s
    p.id = id;
    p.volume = float(std::min(1.0, v));
    p.pan = float(std::sin(rel) * 0.8);
    rng_ = rng_ * 1664525u + 1013904223u;
    p.pitch = float((0.94 + 0.12 * ((rng_ >> 9) % 1000) / 1000.0) * (d > 3000 ? 0.85 : 1.0));
    pending_.push_back(p);
}

void AudioEngine::voice(const std::string& key, int priority) {
    if (key.empty()) return;
    auto it = voiceCooldown_.find(key);
    if (it != voiceCooldown_.end() && clock_ - it->second < (priority >= 3 ? 4.0 : 9.0)) return;
    voiceCooldown_[key] = clock_;
    voiceQueue_.push_back({key, priority, clock_});
}

void AudioEngine::onEvents(const std::vector<sim::Event>& events, const sim::World* w, const Vec3& listener, double yaw, sim::Side me,
                           double timeScale) {
    listener_ = listener;
    listenerYaw_ = yaw;
    if (w != world_) {
        world_ = w;
        generalQuarters_ = false;
    }
    if (!xa_ || !w) return;
    // At high time compression sound delays shrink accordingly and only the loudest events are kept.
    double ts = std::max(1.0, timeScale);
    using T = sim::EventType;
    for (const auto& e : events) {
        Vec3 pos{e.pos.x, e.pos.z, e.pos.y};
        switch (e.type) {
            case T::GunFired: {
                double cal = e.value;
                double loud = 0.9 * std::min(1.4, cal / 180.0 + 0.3);
                if (ts > 8.0 && cal < 180) break;
                // Beyond a few miles only the low rumble of the discharge carries.
                if (length(pos - listener_) > 7000.0) positional("gun_distant", pos, 0.55 * loud, 40000.0, 3000.0);
                else positional(cal >= 180 ? "gun_large" : cal >= 120 ? "gun_medium" : "gun_small", pos, loud, 30000.0);
                break;
            }
            case T::ShellSplash: {
                positional(e.value >= 150 ? "splash_large" : "splash_small", pos, 0.6, 3500.0);
                // A shell falling close to the listener is heard coming in before it lands.
                double d = length(pos - listener_);
                if (d < 400.0 && clock_ - lastWhistle_ > 1.5 && ts <= 2.0) {
                    lastWhistle_ = clock_;
                    play("shell_whistle", float(0.5 * (1.0 - d / 500.0)), float(std::sin((wrap180(std::atan2(pos.x - listener_.x, pos.z - listener_.z) * kRadToDeg - listenerYaw_)) * kDegToRad) * 0.6), 1.0f, sfxBus_);
                }
                break;
            }
            case T::ShellHit: positional(e.value2 > 0.5 ? "explosion_medium" : "hit_metal", pos, 1.0, 12000.0); break;
            case T::TorpedoHit: positional("torpedo_explosion", pos, 1.4, 25000.0); break;
            case T::Explosion: positional("explosion_medium", pos, 1.0, 15000.0); break;
            case T::MagazineExplosion:
                if (const sim::Ship* s = w->ship(e.a)) positional("magazine_explosion", {s->pos.x, 10.0, s->pos.y}, 2.0, 40000.0);
                break;
            case T::StarshellBurst: positional("starshell_pop", pos, 0.5, 6000.0); break;
            case T::TorpedoLaunched:
                if (const sim::Ship* s = w->ship(e.a)) positional("torpedo_launch", {s->pos.x, 5.0, s->pos.y}, 0.7, 2500.0);
                break;
            case T::SearchlightOn:
                if (const sim::Ship* s = w->ship(e.a)) positional("searchlight_on", {s->pos.x, 15.0, s->pos.y}, 0.5, 1500.0);
                break;
            case T::ShipSunk:
                if (const sim::Ship* s = w->ship(e.a)) positional("sinking", {s->pos.x, 5.0, s->pos.y}, 0.9, 12000.0, 800.0);
                break;
            case T::OrderAcknowledged:
                if (e.side == me && e.text == "engines") play("ui_telegraph", 0.55f, 0.0f, 1.0f, sfxBus_);
                break;
            // Air war: heavy AA rounds, torpedoes hitting the water, aircraft going in.
            case T::AaFire: positional(e.value >= 120.0 ? "gun_medium" : "gun_small", pos, 0.55, 15000.0); break;
            case T::TorpedoDropped: positional("splash_small", pos, 0.5, 3000.0); break;
            case T::AircraftCrashed:
                positional("splash_large", pos, 0.8, 6000.0);
                positional("explosion_medium", pos, 0.45, 8000.0);
                break;
            case T::BombReleased:
                if (length(pos - listener_) < 1500.0 && clock_ - lastWhistle_ > 2.0 && ts <= 2.0) {
                    lastWhistle_ = clock_;
                    positional("shell_whistle", pos, 0.6, 1500.0);
                }
                break;
            case T::ContactNew:
            case T::AirContactNew:
                if (e.side == me && me != sim::Side::Neutral && !generalQuarters_) {
                    generalQuarters_ = true;
                    play("alarm_bell", 0.45f, 0.0f, 1.0f, sfxBus_);
                    voice("general_quarters", 3);
                }
                break;
            default: break;
        }
        if (me != sim::Side::Neutral) {
            std::string k = voiceKeyFor(e, *w, me);
            if (!k.empty()) voice(k, voicePriority(k));
        }
    }
    for (auto& p : pending_)
        if (ts > 1.0) p.due = clock_ + (p.due - clock_) / ts;
}

void AudioEngine::update(double dt, const sim::World* w, const Vec3& listener, bool paused) {
    clock_ += dt;
    listener_ = listener;
    if (!xa_) return;
    // Delayed positional sounds.
    for (auto it = pending_.begin(); it != pending_.end();) {
        if (it->due <= clock_) {
            play(it->id, it->volume, it->pan, it->pitch, sfxBus_);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    if (pending_.size() > 200) pending_.erase(pending_.begin(), pending_.begin() + long(pending_.size() - 200));
    // Finished voices.
    for (auto it = active_.begin(); it != active_.end();) {
        XAUDIO2_VOICE_STATE st{};
        it->voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued == 0) {
            if (it->voice == currentVoice_) currentVoice_ = nullptr;
            it->voice->DestroyVoice();
            it = active_.erase(it);
        } else {
            ++it;
        }
    }
    // Bridge reports: one at a time, most important first, stale low-priority reports dropped.
    if (!currentVoice_ && !voiceQueue_.empty()) {
        std::stable_sort(voiceQueue_.begin(), voiceQueue_.end(), [](auto& a, auto& b) { return a.priority > b.priority; });
        while (!voiceQueue_.empty() && voiceQueue_.front().priority < 3 && clock_ - voiceQueue_.front().queued > 6.0) voiceQueue_.pop_front();
        if (!voiceQueue_.empty()) {
            auto req = voiceQueue_.front();
            voiceQueue_.pop_front();
            auto lang = voices_.find(language_);
            if (lang == voices_.end()) lang = voices_.find("en");
            if (lang != voices_.end()) {
                auto k = lang->second.find(req.key);
                if (k != lang->second.end() && !k->second.empty()) {
                    sounds_["__voice"] = {k->second[(rng_ >> 7) % k->second.size()]};
                    rng_ = rng_ * 1664525u + 1013904223u;
                    play("__voice", 1.0f, 0.0f, 1.0f, voiceBus_);
                    if (!active_.empty()) currentVoice_ = active_.back().voice;
                }
            }
        }
    }
    // Ambience loops follow the weather and the camera height.
    if (w) {
        if (!ambSea_.voice) play("amb_sea", 0.0f, 0.0f, 1.0f, ambBus_, true, &ambSea_);
        if (!ambWind_.voice) play("amb_wind", 0.0f, 0.0f, 1.0f, ambBus_, true, &ambWind_);
        double h = std::max(1.0, listener.y);
        double sea = clamp(0.25 + w->env().weather().seaState * 0.12, 0.0, 1.0) * clamp(60.0 / (h + 20.0), 0.08, 1.0);
        double wind = clamp(w->env().weather().windSpeedKn / 30.0, 0.05, 1.0) * clamp(0.4 + h / 400.0, 0.3, 1.0);
        if (ambSea_.voice) ambSea_.voice->SetVolume(float(sea * (paused ? 0.6 : 1.0)));
        if (ambWind_.voice) ambWind_.voice->SetVolume(float(wind * 0.6));
        updateFireLoop(*w, paused);
        if (!paused) updateAaChatter(*w);
    } else {
        for (Active* a : {&ambSea_, &ambWind_, &fireLoop_})
            if (a->voice) a->voice->SetVolume(0.0f);
    }
}

void AudioEngine::updateAaChatter(const sim::World& w) {
    // Light AA has no event per round: the nearest ship whose guns are firing chatters in bursts.
    if (clock_ < nextAaChatter_) return;
    double now = w.time(), best = 0;
    Vec3 at;
    for (const auto& s : w.ships()) {
        if (!s->operational()) continue;
        int firing = 0;
        for (const auto& b : s->aa)
            if (b.mount < 0 && now < b.firingUntil) ++firing;
        if (!firing) continue;
        Vec3 p{s->pos.x, 10.0, s->pos.y};
        double v = std::min(1.0, 0.35 + 0.1 * firing) * std::pow(300.0 / (300.0 + length(p - listener_)), 1.1);
        if (v > best) {
            best = v;
            at = p;
        }
    }
    rng_ = rng_ * 1664525u + 1013904223u;
    nextAaChatter_ = clock_ + 0.09 + 0.12 * ((rng_ >> 9) % 1000) / 1000.0;
    if (best > 0.02) positional("gun_small", at, best * 0.7, 6000.0, 150.0);
}

void AudioEngine::updateFireLoop(const sim::World& w, bool paused) {
    // The roar of the nearest burning ship, panned towards it.
    double best = 0, pan = 0;
    for (const auto& s : w.ships()) {
        if (s->dmg.sunk || s->dmg.fires.empty()) continue;
        Vec3 p{s->pos.x, 8.0, s->pos.y};
        double d = length(p - listener_);
        double v = std::min(1.0, s->dmg.totalFireIntensity()) * std::pow(150.0 / (150.0 + d), 1.2);
        if (v > best) {
            best = v;
            pan = std::sin(wrap180(std::atan2(p.x - listener_.x, p.z - listener_.z) * kRadToDeg - listenerYaw_) * kDegToRad) * 0.7;
        }
    }
    if (best < 0.01) {
        if (fireLoop_.voice) fireLoop_.voice->SetVolume(0.0f);
        return;
    }
    if (!fireLoop_.voice) play("fire_loop", 0.0f, 0.0f, 1.0f, sfxBus_, true, &fireLoop_);
    if (!fireLoop_.voice) return;
    fireLoop_.voice->SetVolume(float(std::min(0.8, best * 1.5) * (paused ? 0.5 : 1.0)));
    float l = float(std::sqrt(0.5 * (1.0 - pan))), r = float(std::sqrt(0.5 * (1.0 + pan)));
    float m[2] = {l, r};
    fireLoop_.voice->SetOutputMatrix(sfxBus_, 1, 2, m);
}

}  // namespace it::audio
