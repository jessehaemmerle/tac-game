#pragma once
// Services the platform layer (renderer, audio, window) provides to the portable UI.
#include <cstdint>
#include <string>

#include "sim/Types.h"

namespace it::ui {

struct Settings;

class UiServices {
public:
    virtual ~UiServices() = default;
    /// Rendered 3D preview of a ship class; returns an ImGui texture id (0 if unavailable).
    virtual uint64_t shipPreview(const std::string& classId, float yaw, float pitch, uint32_t w, uint32_t h, bool night) = 0;
    /// Chart texture of the current world's terrain (0 if none).
    virtual uint64_t chartTexture() = 0;
    /// Ship under a screen position in the 3D view.
    virtual sim::EntityId pick3D(float x, float y) = 0;
    virtual void settingsChanged(const Settings& s) = 0;
    virtual void playUiSound(const char* id) = 0;
    virtual std::string gpuDescription() = 0;
    virtual void quit() = 0;
};

/// No-op services (headless UI tests).
class NullServices : public UiServices {
public:
    uint64_t shipPreview(const std::string&, float, float, uint32_t, uint32_t, bool) override { return 0; }
    uint64_t chartTexture() override { return 0; }
    sim::EntityId pick3D(float, float) override { return sim::kNoEntity; }
    void settingsChanged(const Settings&) override {}
    void playUiSound(const char*) override {}
    std::string gpuDescription() override { return "headless"; }
    void quit() override { quitRequested = true; }
    bool quitRequested = false;
};

}  // namespace it::ui
