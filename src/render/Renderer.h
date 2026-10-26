#pragma once
// Scene renderer: FFT ocean, sky, islands, procedural ships, particles/lights, post-processing.
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "procgen/ShipModel.h"
#include "render/Camera.h"
#include "render/GpuDevice.h"
#include "render/ShaderLibrary.h"
#include "sim/World.h"

namespace it::render {

struct RenderSettings {
    uint32_t msaa = 4;
    uint32_t shadowSize = 2048;
    bool vsync = true;
    float exposureBias = 0.0f;   // EV
    float bloom = 0.06f;
    bool filmGrain = true;
    int quality = 2;             // 0 low, 1 medium, 2 high
};

struct SceneOptions {
    sim::EntityId selected = sim::kNoEntity;
    bool paused = false;
    double timeScale = 1.0;
};

struct GpuMesh {
    GpuResource vb, ib;
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    D3D12_INDEX_BUFFER_VIEW ibv{};
    struct Part {
        uint32_t firstIndex = 0, indexCount = 0;
        procgen::PartKind kind = procgen::PartKind::Static;
        int mount = -1, parent = -1;
        Vec3f pivot;
    };
    std::vector<Part> parts;
    Aabb bounds;
};

/// Camera and options of a ship-class preview (unit reference).
struct PreviewParams {
    float yaw = 135.0f, pitch = 10.0f;  // deg; yaw 0 looks at the bow
    float zoom = 1.0f;                  // 1: the whole ship fills the view
    float focus = 0.0f;                 // look-at point along the hull: +0.5 bow .. -0.5 stern
    float lift = 0.0f;                  // look-at height offset (m)
    bool night = false;
    bool animate = false;               // armament demonstration
};

struct Particle;  // effects (Renderer.cpp)

class Renderer {
public:
    Renderer();
    ~Renderer();
    bool init(GpuDevice& dev, ShaderLibrary& shaders, const sim::Database& db, const RenderSettings& s);
    void shutdown();
    void resize(uint32_t w, uint32_t h);
    bool reloadShaders();
    RenderSettings& settings() { return settings_; }

    /// Bind a world (scenario start). Builds terrain mesh and textures; nullptr detaches.
    void setWorld(const sim::World* world);
    /// Simulation events for visual effects.
    void onEvents(const std::vector<sim::Event>& events);
    /// Advance effects by real and simulated time.
    void update(double realDt, double simDt);
    /// Render the 3D scene into the back buffer (after which the UI is drawn on top).
    void render(const Camera& cam, const SceneOptions& opt);
    /// Render a preview of a ship class (unit reference) and return an ImGui texture id.
    uint64_t shipPreview(const std::string& classId, const PreviewParams& view, uint32_t w, uint32_t h);
    /// Hypsometric chart texture of the terrain for the tactical map (ImGui texture id, 0 if none).
    uint64_t chartTexture() const { return chart_.uiSrv != UINT32_MAX ? dev_->gpuHeap().gpu(chart_.uiSrv).ptr : 0; }
    /// Ship under a screen position (3D view picking), or kNoEntity.
    sim::EntityId pick(const Camera& cam, float sx, float sy) const;
    /// Approximate water surface height near a point (for camera clamping).
    double waterHeight() const { return 0.0; }
    const GpuMesh* shipMesh(const std::string& classId);
    double simTimeVisual() const { return visualTime_; }
    struct Impl;  // internal state (RendererImpl.h)

private:
    std::unique_ptr<Impl> p_;
    GpuDevice* dev_ = nullptr;
    ShaderLibrary* shaders_ = nullptr;
    const sim::Database* db_ = nullptr;
    const sim::World* world_ = nullptr;
    RenderSettings settings_;
    GpuResource chart_;
    double visualTime_ = 0;
    friend struct RendererAccess;
};

}  // namespace it::render
