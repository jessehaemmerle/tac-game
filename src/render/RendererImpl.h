#pragma once
// Internal state of the Renderer, shared by its translation units.
#include <deque>

#include "core/Random.h"
#include "render/Renderer.h"

namespace it::render {

struct FrameConstants {
    Mat4f viewProj, invViewProj, view, shadowViewProj;
    Vec4f cameraPos, sunDir, sunColor, moonDir, trueSunDir, ambientSky, ambientGround, fogColor, fogParams, screen, time, wind, ocean,
        terrain, lights, sky;
};

struct ObjectConstants {
    Mat4f world;
    Vec4f tint, params;
};

struct GpuLight {
    Vec3f pos;
    float radius;
    Vec3f color;
    float type;
    Vec3f dir;
    float cosAngle;
    Vec4f extra;
};
static_assert(sizeof(GpuLight) == 64, "light layout");

struct GpuParticle {
    Vec3f pos;
    float size;
    Vec4f color;
    Vec3f axis;
    float type, age, seed, emissive, stretch;
};
static_assert(sizeof(GpuParticle) == 64, "particle layout");

enum ParticleType { kSmoke = 0, kFire = 1, kColumn = 2, kGlow = 3, kSpark = 4, kBeam = 5, kTracer = 6, kRain = 7, kSnow = 8, kCurtain = 9 };

struct Particle {
    Vec3 pos, vel;
    Vec3 end;  // beams
    float size = 1, growth = 0;
    Vec4f color{1, 1, 1, 1};
    float life = 0, maxLife = 1;
    int type = kSmoke;
    float seed = 0, emissive = 0, stretch = 1;
    float drag = 0.5f, buoyancy = 0;
};

/// A heavy AA shell on its way: it bursts at `pos` when the visual clock reaches `at`.
struct FlakBurst {
    Vec3 pos;
    double at = 0;
    float size = 6;
    bool night = false;
};

struct FlashLight {
    Vec3 pos;
    Vec3f color;
    float radius = 500, life = 0, maxLife = 0.2f;
};

struct WakeTrail {
    std::deque<std::pair<Vec2, double>> points;  // position, visual time
    double lastSeen = 0;
};

struct FoamStamp {
    Vec2 pos;          // render xz
    double born = 0;   // visual time
    float radius = 10, strength = 1, life = 20;
};

/// Articulation of a ship model: mount trains/elevations (deg), recoil and launch travel (m) and
/// auxiliary part angles. Empty vectors (and NaN entries) leave the parts in their stowed position.
struct ShipPose {
    static constexpr float kGone = 1e5f;
    std::vector<float> train, elev, torpTrain;
    std::vector<std::vector<float>> recoil;      // per mount and barrel: run-back (m)
    std::vector<std::vector<float>> torpedoOut;  // per torpedo mount and tube: travel out of the tube (m), < 0: tube empty
    std::vector<float> aaTrain, aaElev;          // light AA: train relative to the bow and elevation (deg)
    std::vector<float> directorTrain;            // per director (class index), NaN: stowed
    std::vector<float> catapultTrain;            // per catapult: deg outboard
    std::vector<float> aircraftTravel;           // per floatplane: < 0 on its carriage, m flown once shot off, >= kGone hidden
    float searchlightTrain = 0, searchlightElev = 0, radarAngle = 0;
    float propAngle = 0, rudder = 0;             // deg
    bool searchlightOn = false;
};

/// What the renderer remembers of each ship's weapons between frames (recoil and launch timing).
struct ShipAnim {
    std::vector<double> fired;         // per mount: sim time of the last salvo
    std::vector<int> torpLoaded;       // per torpedo mount: tubes loaded at the last check
    std::vector<double> torpLaunch;    // per torpedo mount: sim time of the last launch
    std::vector<int> torpFrom, torpTo; // tubes [from, to) fired in that launch
};

/// Muzzle of one barrel in model space (for flashes).
struct Muzzle {
    int mount = -1, barrel = -1;
    Vec3f pos, dir;
};

struct Pipelines {
    ComPtr<ID3D12PipelineState> sky, ship, shipShadow, terrain, terrainShadow, ocean, wake, wakeClear, particles;
    ComPtr<ID3D12PipelineState> bloomFirst, bloomDown, bloomUp, tonemap, tonemapPreview, resolveDepth;
    ComPtr<ID3D12PipelineState> oceanSpectrum, oceanRows, oceanCols, oceanFinalize, oceanDownsample, luminance;
};

constexpr D3D12_RESOURCE_STATES kSrvState =
    D3D12_RESOURCE_STATES(int(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) | int(D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
constexpr uint32_t kWakeMapSize = 1024;
constexpr double kWakeHalfSize = 2000.0;  // m (must match ocean.hlsl: 4 km map)

struct Renderer::Impl {
    ComPtr<ID3D12RootSignature> root;
    Pipelines pso;
    // Targets
    GpuResource hdrMS, depthMS, hdr, depth, shadow, wake;
    GpuResource bloom[6];
    GpuResource lum[2];
    int lumCur = 0;
    uint32_t depthReadOnlyDsv = UINT32_MAX;
    uint32_t nullTex2D = UINT32_MAX, nullTexArray = UINT32_MAX;
    uint32_t msaa = 1;
    bool clearedUavs = false;
    // Ocean
    GpuResource h0, specA, specB, tmpA, tmpB, disp, normal[2];
    std::vector<uint32_t> normalMipSrv[2];
    int normalCur = 0;
    uint64_t oceanFrame = UINT64_MAX;  // device frame of the last ocean update
    double oceanTime = 0;
    GpuResource oceanVB, oceanIB;
    uint32_t oceanIndices = 0;
    float cascadeL[3] = {600.0f, 120.0f, 24.0f};
    float choppiness = 1.2f;
    double oceanSeaState = -1;
    // Terrain
    GpuResource terrainVB, terrainIB, terrainHeight;
    uint32_t terrainIndices = 0;
    bool hasTerrain = false;
    Vec4f terrainRect;
    // Ships
    std::unordered_map<std::string, std::unique_ptr<GpuMesh>> meshes;
    GpuResource shipAlbedo, shipSurface;  // tiling material layers (data/textures, tools/textures)
    // Effects
    std::vector<Particle> particles;
    std::vector<FlashLight> flashes;
    std::unordered_map<sim::EntityId, WakeTrail> trails;
    std::unordered_map<uint32_t, WakeTrail> torpedoTrails;
    std::vector<FoamStamp> foam;
    std::vector<FlakBurst> flak;
    double smokeAccum = 0;
    std::vector<GpuLight> lights;
    std::unordered_map<sim::EntityId, ShipAnim> anims;
    std::vector<GpuParticle> gpuParticles;
    std::vector<float> wakeVerts;  // x, z, foam
    Rng rng{12345};
    double flashGlow = 0;
    // Preview
    struct Preview {
        GpuResource hdrMS, depthMS, hdr, ldr;
        uint32_t w = 0, h = 0;
    } preview;
    FrameConstants fc{};
    Camera cam;

};

enum class BlendMode { Opaque, Premultiplied, Additive, MaxRed };
enum class DepthMode { None, Write, Read, ShadowWrite };

struct PsoDesc {
    const char* file = nullptr;
    const char* vs = nullptr;
    const char* ps = nullptr;  // nullptr: depth only
    std::vector<D3D12_INPUT_ELEMENT_DESC> layout;
    BlendMode blend = BlendMode::Opaque;
    DepthMode depth = DepthMode::None;
    D3D12_CULL_MODE cull = D3D12_CULL_MODE_NONE;
    DXGI_FORMAT rt = DXGI_FORMAT_UNKNOWN;
    uint32_t samples = 1;
    std::vector<std::string> defines;
    D3D12_PRIMITIVE_TOPOLOGY_TYPE topology = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
};

/// Shader-visible table helper: copy staging SRVs and/or create transient buffer SRVs.
struct TableBuilder {
    GpuDevice& dev;
    uint32_t start, count;
    TableBuilder(GpuDevice& d, uint32_t n) : dev(d), start(d.allocTable(n)), count(n) {}
    void srv(uint32_t slot, uint32_t staging) {
        if (staging != UINT32_MAX)
            dev.device()->CopyDescriptorsSimple(1, dev.gpuHeap().cpu(start + slot), dev.stagingHeap().cpu(staging), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
    void buffer(uint32_t slot, const UploadAlloc& a, uint32_t elements, uint32_t stride) {
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        d.Buffer.FirstElement = a.offset / stride;
        d.Buffer.NumElements = std::max(1u, elements);
        d.Buffer.StructureByteStride = stride;
        dev.device()->CreateShaderResourceView(a.resource, &d, dev.gpuHeap().cpu(start + slot));
    }
    D3D12_GPU_DESCRIPTOR_HANDLE handle() const { return dev.gpuHeap().gpu(start); }
};

// Implemented in the split translation units.
bool initOcean(Renderer::Impl& p, GpuDevice& dev);
void buildOceanSpectrum(Renderer::Impl& p, GpuDevice& dev, double seaState, Vec2 windDir, double windSpeed);
void dispatchOcean(Renderer::Impl& p, GpuDevice& dev, double time);
void drawOcean(Renderer::Impl& p, GpuDevice& dev);
void buildTerrain(Renderer::Impl& p, GpuDevice& dev, const sim::Terrain& t, GpuResource& chart);
// Model-space rotations (deg) and a rotation about a part's pivot.
inline Mat4f rotZ(float deg) { return Mat4f::rotation(Quatf::axisAngle({0, 0, 1}, float(deg * kDegToRad))); }
inline Mat4f rotY(float deg) { return Mat4f::rotation(Quatf::axisAngle({0, 1, 0}, float(deg * kDegToRad))); }
inline Mat4f rotX(float deg) { return Mat4f::rotation(Quatf::axisAngle({1, 0, 0}, float(deg * kDegToRad))); }
inline Mat4f aboutPivot(const Vec3f& pivot, const Mat4f& r) { return Mat4f::translation(pivot) * r * Mat4f::translation(-pivot); }

const GpuMesh* shipMeshFor(Renderer::Impl& p, GpuDevice& dev, const sim::ShipClassDef& c);
const GpuMesh* aircraftMeshFor(Renderer::Impl& p, GpuDevice& dev, const sim::AircraftDef& a);
/// Sphere vs. view frustum (camera-relative centre).
bool sphereVisible(const Mat4f& viewProj, const Vec3f& c, float r);
/// Load the ship material texture arrays (missing files leave the shader on flat paint).
bool loadShipTextures(Renderer::Impl& p, GpuDevice& dev);
/// Bind the ship material textures to the per-pass table (t16, t17).
void bindShipTextures(Renderer::Impl& p, GpuDevice& dev);
void drawTerrain(Renderer::Impl& p, GpuDevice& dev);
void drawShips(Renderer::Impl& p, GpuDevice& dev, const sim::World& w, const Camera& cam, bool shadowPass, sim::EntityId selected,
               double visualTime);
/// Draw a ship model with its articulation; parts above `maxLod` are skipped (0 hull and armament,
/// 1 fittings and light AA, 2 close-up details such as railings and rigging).
void drawShipModel(Renderer::Impl& p, GpuDevice& dev, const GpuMesh& mesh, const Mat4f& world, const Vec4f& tint, const Vec4f& params,
                   const ShipPose& pose, int maxLod = 2);
// RendererAnimation.cpp
ShipPose shipPose(Renderer::Impl& p, const sim::Ship& s, const sim::World& w, const GpuMesh* mesh, double visualTime);
ShipPose stowedPose(const sim::ShipClassDef& c);
/// Weapons drill of the unit reference at time t (procgen::anim::drill applied to this class).
ShipPose drillPose(const sim::ShipClassDef& c, const GpuMesh& mesh, double t);
/// Model-space transform of every part for a pose, and which parts are hidden.
void partTransforms(const GpuMesh& mesh, const ShipPose& pose, std::vector<Mat4f>& local, std::vector<uint8_t>& hidden);
std::vector<Muzzle> muzzles(const GpuMesh& mesh, const ShipPose& pose, int mount = -1);
/// Record weapon events (salvos) and torpedo launches for the animations.
void recordShipEvents(Renderer::Impl& p, const sim::World& w, const std::vector<sim::Event>& events);
void updateShipAnims(Renderer::Impl& p, const sim::World& w);
/// Aircraft in the air (and fresh wrecks on the water), each with its own procedural model.
void drawFlyingAircraft(Renderer::Impl& p, GpuDevice& dev, const sim::World& w, const Camera& cam, double visualTime);
Mat4f shipWorldMatrix(const sim::Ship& s, const Vec3& camPos, double visualTime, double seaState);
/// Ship model space (x fwd, y stbd, z up) to render space at heading 0 (x east, y up, z north), as a matrix.
Mat4f modelToRenderBasis(double headingDeg);
void effectsEvents(Renderer::Impl& p, const sim::World& w, const std::vector<sim::Event>& events);
void effectsUpdate(Renderer::Impl& p, const sim::World& w, double realDt, double simDt, double visualTime);
void effectsGather(Renderer::Impl& p, const sim::World& w, const Camera& cam, double visualTime);

}  // namespace it::render
