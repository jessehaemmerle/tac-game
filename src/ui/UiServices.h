#pragma once
// Services the platform layer (renderer, audio, window) provides to the portable UI.
#include <cstdint>
#include <string>

#include "sim/Types.h"

namespace it::ui {

struct Settings;

/// Camera and options of the unit reference's 3D model view.
struct PreviewView {
    float yaw = 135.0f, pitch = 10.0f;  // deg; yaw 0 looks at the bow
    float zoom = 1.0f;                  // 1: the whole ship fills the view
    float focus = 0.0f;                 // look-at point along the hull: +0.5 bow .. -0.5 stern
    float lift = 0.0f;                  // look-at height offset (m)
    bool night = false;
    bool animate = false;               // armament demonstration: training, elevating, firing
    float drillTime = -1.0f;            // >= 0: freeze the demonstration at this time (s, screenshots)
};

class UiServices {
public:
    virtual ~UiServices() = default;
    /// Rendered 3D preview of a ship class; returns an ImGui texture id (0 if unavailable).
    virtual uint64_t shipPreview(const std::string& classId, const PreviewView& view, uint32_t w, uint32_t h) = 0;
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
    uint64_t shipPreview(const std::string&, const PreviewView&, uint32_t, uint32_t) override { return 0; }
    uint64_t chartTexture() override { return 0; }
    sim::EntityId pick3D(float, float) override { return sim::kNoEntity; }
    void settingsChanged(const Settings&) override {}
    void playUiSound(const char*) override {}
    std::string gpuDescription() override { return "headless"; }
    void quit() override { quitRequested = true; }
    bool quitRequested = false;
};

}  // namespace it::ui
