// Renderer core: root signature, pipelines, render targets, frame constants and pass orchestration.
#include <algorithm>
#include <cstring>

#include "procgen/ShipAnimation.h"
#include "render/RendererImpl.h"

namespace it::render {

namespace {

struct PostPass {
    Vec4f post, expo, grade;
};
struct WakePass {
    Vec4f center;
};

D3D12_INPUT_ELEMENT_DESC element(const char* semantic, UINT index, DXGI_FORMAT fmt, UINT offset) {
    return {semantic, index, fmt, 0, offset, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};
}

std::vector<D3D12_INPUT_ELEMENT_DESC> shipLayout() {
    return {element("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0), element("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12),
            element("COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 24), element("TEXCOORD", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 28)};
}

/// Lighting state derived from the environment (or a studio setup for previews and menus).
struct Lighting {
    Vec3f keyDir{0, 1, 0}, keyColor{1, 1, 1}, sunDir{0, 1, 0}, moonDir{0, -1, 0};
    float keyIntensity = 0, sunAlt = 30, moonPhase = 0.5f, moonVisible = 0;
    bool keyIsMoon = false;
    Vec3f ambientSky, ambientGround;
    float ambientLevel = 1, day = 1, cloud = 0.3f, visibility = 20000, rain = 0, seaState = 2;
    Vec2 windDir{0, 1};  // direction waves travel towards (render xz)
    float windSpeed = 5;
};

Lighting computeLighting(double sunAltDeg, double sunAzDeg, double moonAltDeg, double moonAzDeg, double moonPhase, double cloud,
                         double visibilityKm, double seaState, double windFromDeg, double windKn, double rain) {
    Lighting L;
    auto dirOf = [](double alt, double az) {
        double ca = std::cos(alt * kDegToRad);
        return Vec3f{float(std::sin(az * kDegToRad) * ca), float(std::sin(alt * kDegToRad)), float(std::cos(az * kDegToRad) * ca)};
    };
    L.sunDir = dirOf(sunAltDeg, sunAzDeg);
    L.moonDir = dirOf(moonAltDeg, moonAzDeg);
    L.sunAlt = float(sunAltDeg);
    L.moonPhase = float(moonPhase);
    L.cloud = float(cloud);
    L.day = float(smoothstep(-6.0, 6.0, sunAltDeg));
    float overcast = float(1.0 - 0.75 * cloud);
    float sunI = float(3.2 * smoothstep(-3.0, 10.0, sunAltDeg)) * overcast;
    float moonUp = float(smoothstep(-1.0, 8.0, moonAltDeg));
    float moonI = float(0.045 * moonPhase) * moonUp * float(1.0 - 0.6 * cloud);
    L.moonVisible = moonUp * float(1.0 - 0.5 * cloud);
    if (sunAltDeg > -2.0 || moonI <= 0.0f) {
        L.keyDir = L.sunDir.y < 0.02f ? normalize(Vec3f{L.sunDir.x, 0.02f, L.sunDir.z}) : L.sunDir;
        float warm = float(smoothstep(0.0, 25.0, sunAltDeg));
        L.keyColor = lerp(Vec3f{1.0f, 0.5f, 0.25f}, Vec3f{1.0f, 0.95f, 0.88f}, warm);
        L.keyIntensity = sunI;
    } else {
        L.keyDir = L.moonDir;
        L.keyColor = {0.72f, 0.80f, 1.0f};
        L.keyIntensity = moonI;
        L.keyIsMoon = true;
    }
    Vec3f dayAmb{0.30f, 0.38f, 0.50f}, nightAmb{0.0035f, 0.0045f, 0.008f};
    Vec3f moonAmb = Vec3f{0.018f, 0.022f, 0.032f} * float(moonPhase) * moonUp * float(1.0 - 0.5 * cloud);
    L.ambientSky = lerp(nightAmb + moonAmb, dayAmb * overcast, L.day);
    L.ambientGround = L.ambientSky * 0.35f;
    L.visibility = float(std::max(200.0, visibilityKm * 1000.0));
    L.rain = float(rain);
    L.seaState = float(seaState);
    double to = windFromDeg + 180.0;
    L.windDir = {std::sin(to * kDegToRad), std::cos(to * kDegToRad)};
    L.windSpeed = float(windKn * 0.514444);
    return L;
}

Lighting lightingFor(const sim::World* w, const Vec3& camPos) {
    if (!w) return computeLighting(28, 300, -20, 0, 0.5, 0.35, 25, 3, 120, 12, 0);
    const auto& env = w->env();
    const auto& wx = env.weather();
    Vec2 camSim{camPos.x, camPos.z};
    double rain = env.inSquall(camSim) ? 1.0 : 0.0;
    double vis = wx.visibilityKm * (rain > 0 ? 0.25 : 1.0);
    return computeLighting(env.sunAltitudeDeg(), env.sunAzimuthDeg(), env.moonAltitudeDeg(), env.moonAzimuthDeg(), env.moonIllumination(),
                           wx.cloudCover, vis, wx.seaState, wx.windDirDeg, wx.windSpeedKn, rain);
}

/// Shadow map projection (camera-relative) covering `size` metres around `centerWorld`, snapped to texels.
Mat4f shadowMatrix(const Vec3f& lightDir, const Vec3& centerWorld, const Vec3& camPos, float size, uint32_t res) {
    Vec3f f = -lightDir;
    Vec3f up = std::abs(f.y) > 0.99f ? Vec3f{0, 0, 1} : Vec3f{0, 1, 0};
    Vec3f s = normalize(cross(up, f));
    Vec3f u = cross(f, s);
    double texel = size / double(res);
    Vec3 sD{s.x, s.y, s.z}, uD{u.x, u.y, u.z};
    double cs = dot(centerWorld, sD), cu = dot(centerWorld, uD);
    Vec3 snapped = centerWorld + sD * (std::round(cs / texel) * texel - cs) + uD * (std::round(cu / texel) * texel - cu);
    Vec3 rel = snapped - camPos;
    Vec3f c{float(rel.x), float(rel.y), float(rel.z)};
    const float depthRange = 8000.0f;
    Mat4f view = Mat4f::lookAtLH(c - f * (depthRange * 0.5f), c, up);
    return Mat4f::orthoLH(size, size, 0.0f, depthRange) * view;
}

}  // namespace

// ------------------------------------------------------------------------------------------------ PSO
static ComPtr<ID3D12PipelineState> buildGraphics(ID3D12RootSignature* root, GpuDevice& dev, ShaderLibrary& sh, const PsoDesc& pd) {
    Bytecode vs, ps;
    if (!sh.compile(pd.file, pd.vs, "vs_5_1", pd.defines, vs)) return {};
    if (pd.ps && !sh.compile(pd.file, pd.ps, "ps_5_1", pd.defines, ps)) return {};
    D3D12_GRAPHICS_PIPELINE_STATE_DESC d{};
    d.pRootSignature = root;
    d.VS = vs.bc();
    if (pd.ps) d.PS = ps.bc();
    auto& rt = d.BlendState.RenderTarget[0];
    rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    rt.LogicOp = D3D12_LOGIC_OP_NOOP;
    switch (pd.blend) {
        case BlendMode::Opaque: break;
        case BlendMode::Premultiplied:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = D3D12_BLEND_ONE;
            rt.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOp = D3D12_BLEND_OP_ADD;
            rt.SrcBlendAlpha = D3D12_BLEND_ONE;
            rt.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            break;
        case BlendMode::Additive:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = rt.DestBlend = D3D12_BLEND_ONE;
            rt.SrcBlendAlpha = rt.DestBlendAlpha = D3D12_BLEND_ONE;
            rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_ADD;
            break;
        case BlendMode::MaxRed:
            rt.BlendEnable = TRUE;
            rt.SrcBlend = rt.DestBlend = D3D12_BLEND_ONE;
            rt.SrcBlendAlpha = rt.DestBlendAlpha = D3D12_BLEND_ONE;
            rt.BlendOp = rt.BlendOpAlpha = D3D12_BLEND_OP_MAX;
            rt.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_RED;
            break;
    }
    d.SampleMask = UINT_MAX;
    auto& rs = d.RasterizerState;
    rs.FillMode = D3D12_FILL_MODE_SOLID;
    rs.CullMode = pd.cull;
    rs.FrontCounterClockwise = FALSE;
    rs.DepthClipEnable = TRUE;
    rs.MultisampleEnable = pd.samples > 1;
    if (pd.depth == DepthMode::ShadowWrite) {
        rs.DepthBias = 600;
        rs.SlopeScaledDepthBias = 2.0f;
        rs.DepthBiasClamp = 0.01f;
    }
    auto& ds = d.DepthStencilState;
    ds.DepthEnable = pd.depth != DepthMode::None;
    ds.DepthWriteMask = (pd.depth == DepthMode::Write || pd.depth == DepthMode::ShadowWrite) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    ds.DepthFunc = pd.depth == DepthMode::ShadowWrite ? D3D12_COMPARISON_FUNC_LESS_EQUAL : D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    d.InputLayout = {pd.layout.empty() ? nullptr : pd.layout.data(), UINT(pd.layout.size())};
    d.PrimitiveTopologyType = pd.topology;
    if (pd.rt != DXGI_FORMAT_UNKNOWN) {
        d.NumRenderTargets = 1;
        d.RTVFormats[0] = pd.rt;
    }
    if (pd.depth != DepthMode::None) d.DSVFormat = DXGI_FORMAT_D32_FLOAT;
    d.SampleDesc.Count = pd.samples;
    ComPtr<ID3D12PipelineState> pso;
    if (!IT_CHECK(dev.device()->CreateGraphicsPipelineState(&d, IID_PPV_ARGS(pso.put())))) {
        IT_LOG_ERROR("Pipeline %s %s/%s failed", pd.file, pd.vs, pd.ps ? pd.ps : "-");
        return {};
    }
    return pso;
}

static ComPtr<ID3D12PipelineState> buildCompute(ID3D12RootSignature* root, GpuDevice& dev, ShaderLibrary& sh, const char* file, const char* cs) {
    Bytecode bc;
    if (!sh.compile(file, cs, "cs_5_1", {}, bc)) return {};
    D3D12_COMPUTE_PIPELINE_STATE_DESC d{};
    d.pRootSignature = root;
    d.CS = bc.bc();
    ComPtr<ID3D12PipelineState> pso;
    if (!IT_CHECK(dev.device()->CreateComputePipelineState(&d, IID_PPV_ARGS(pso.put())))) return {};
    return pso;
}

static bool createRootSignature(GpuDevice& dev, ComPtr<ID3D12RootSignature>& out) {
    D3D12_DESCRIPTOR_RANGE ranges[3]{};
    ranges[0] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0, 0, 0};
    ranges[1] = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 16, 0, 0};
    ranges[2] = {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0, 0, 0};
    D3D12_ROOT_PARAMETER params[6]{};
    for (int i = 0; i < 3; ++i) {
        params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[i].Descriptor = {UINT(i), 0};
        params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    for (int i = 0; i < 3; ++i) {
        params[3 + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[3 + i].DescriptorTable = {1, &ranges[i]};
        params[3 + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    }
    D3D12_STATIC_SAMPLER_DESC samplers[5]{};
    auto sampler = [&](int i, D3D12_FILTER f, D3D12_TEXTURE_ADDRESS_MODE a) {
        auto& s = samplers[i];
        s.Filter = f;
        s.AddressU = s.AddressV = s.AddressW = a;
        s.MaxAnisotropy = 8;
        s.ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        s.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        s.MaxLOD = D3D12_FLOAT32_MAX;
        s.ShaderRegister = UINT(i);
        s.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    };
    sampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    sampler(1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    sampler(2, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP);
    sampler(3, D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_BORDER);
    sampler(4, D3D12_FILTER_ANISOTROPIC, D3D12_TEXTURE_ADDRESS_MODE_WRAP);
    D3D12_ROOT_SIGNATURE_DESC rd{};
    rd.NumParameters = 6;
    rd.pParameters = params;
    rd.NumStaticSamplers = 5;
    rd.pStaticSamplers = samplers;
    rd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ComPtr<ID3DBlob> blob, err;
    if (FAILED(D3D12SerializeRootSignature(&rd, D3D_ROOT_SIGNATURE_VERSION_1, blob.put(), err.put()))) {
        IT_LOG_ERROR("Root signature: %s", err ? static_cast<const char*>(err->GetBufferPointer()) : "?");
        return false;
    }
    return IT_CHECK(dev.device()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(out.put())));
}

static bool createPipelines(Renderer::Impl& P, GpuDevice& dev, ShaderLibrary& sh, uint32_t msaa, Pipelines& out) {
    ID3D12RootSignature* root = P.root.get();
    const DXGI_FORMAT hdr = DXGI_FORMAT_R16G16B16A16_FLOAT;
    auto G = [&](PsoDesc d) { return buildGraphics(root, dev, sh, d); };
    auto C = [&](const char* f, const char* cs) { return buildCompute(root, dev, sh, f, cs); };
    PsoDesc d;
    d = {"sky.hlsl", "VSMain", "PSMain", {}, BlendMode::Opaque, DepthMode::Read, D3D12_CULL_MODE_NONE, hdr, msaa};
    out.sky = G(d);
    d = {"ship.hlsl", "VSMain", "PSMain", shipLayout(), BlendMode::Opaque, DepthMode::Write, D3D12_CULL_MODE_NONE, hdr, msaa};
    out.ship = G(d);
    d = {"ship.hlsl", "VSShadow", nullptr, shipLayout(), BlendMode::Opaque, DepthMode::ShadowWrite, D3D12_CULL_MODE_NONE, DXGI_FORMAT_UNKNOWN, 1};
    out.shipShadow = G(d);
    std::vector<D3D12_INPUT_ELEMENT_DESC> terrainLayout = {element("POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0),
                                                           element("NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 12)};
    d = {"terrain.hlsl", "VSMain", "PSMain", terrainLayout, BlendMode::Opaque, DepthMode::Write, D3D12_CULL_MODE_BACK, hdr, msaa};
    out.terrain = G(d);
    d = {"terrain.hlsl", "VSShadow", nullptr, terrainLayout, BlendMode::Opaque, DepthMode::ShadowWrite, D3D12_CULL_MODE_NONE, DXGI_FORMAT_UNKNOWN, 1};
    out.terrainShadow = G(d);
    d = {"ocean.hlsl", "VSMain", "PSMain", {element("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0)}, BlendMode::Opaque, DepthMode::Write,
         D3D12_CULL_MODE_NONE, hdr, msaa};
    out.ocean = G(d);
    d = {"wake.hlsl", "VSMain", "PSMain",
         {element("POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0), element("TEXCOORD", 0, DXGI_FORMAT_R32_FLOAT, 8)}, BlendMode::MaxRed,
         DepthMode::None, D3D12_CULL_MODE_NONE, DXGI_FORMAT_R32G32B32A32_FLOAT, 1};
    out.wake = G(d);
    d = {"wake.hlsl", "VSFull", "PSClear", {}, BlendMode::Opaque, DepthMode::None, D3D12_CULL_MODE_NONE, DXGI_FORMAT_R32G32B32A32_FLOAT, 1};
    out.wakeClear = G(d);
    d = {"particles.hlsl", "VSMain", "PSMain", {}, BlendMode::Premultiplied, DepthMode::Read, D3D12_CULL_MODE_NONE, hdr, msaa};
    out.particles = G(d);
    d = {"post.hlsl", "VSFull", "PSDownsampleFirst", {}, BlendMode::Opaque, DepthMode::None, D3D12_CULL_MODE_NONE, hdr, 1};
    out.bloomFirst = G(d);
    d.ps = "PSDownsample";
    out.bloomDown = G(d);
    d.ps = "PSUpsample";
    d.blend = BlendMode::Additive;
    out.bloomUp = G(d);
    d = {"post.hlsl", "VSFull", "PSTonemap", {}, BlendMode::Opaque, DepthMode::None, D3D12_CULL_MODE_NONE, dev.backBufferFormat(), 1};
    out.tonemap = G(d);
    d.ps = "PSTonemapPreview";
    d.rt = DXGI_FORMAT_R8G8B8A8_UNORM;
    out.tonemapPreview = G(d);
    d = {"post.hlsl", "VSFull", "PSResolveDepthMS", {}, BlendMode::Opaque, DepthMode::None, D3D12_CULL_MODE_NONE, DXGI_FORMAT_R32_FLOAT, 1,
         {msaa > 1 ? "DEPTH_MS=1" : "DEPTH_MS=0"}};
    out.resolveDepth = G(d);
    out.oceanSpectrum = C("ocean_fft.hlsl", "CSSpectrum");
    out.oceanRows = C("ocean_fft.hlsl", "CSFFTRows");
    out.oceanCols = C("ocean_fft.hlsl", "CSFFTCols");
    out.oceanFinalize = C("ocean_fft.hlsl", "CSFinalize");
    out.oceanDownsample = C("ocean_fft.hlsl", "CSDownsample");
    out.luminance = C("post.hlsl", "CSLuminance");
    ComPtr<ID3D12PipelineState>* all[] = {&out.sky, &out.ship, &out.shipShadow, &out.terrain, &out.terrainShadow, &out.ocean, &out.wake,
                                          &out.wakeClear, &out.particles, &out.bloomFirst, &out.bloomDown, &out.bloomUp, &out.tonemap,
                                          &out.tonemapPreview, &out.resolveDepth, &out.oceanSpectrum, &out.oceanRows, &out.oceanCols,
                                          &out.oceanFinalize, &out.oceanDownsample, &out.luminance};
    for (auto* p : all)
        if (!*p) return false;
    return true;
}

// ---------------------------------------------------------------------------------------- targets
static uint32_t supportedMsaa(GpuDevice& dev, uint32_t want) {
    for (uint32_t s = want; s > 1; s /= 2) {
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS q{};
        q.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        q.SampleCount = s;
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS qd{};
        qd.Format = DXGI_FORMAT_D32_FLOAT;
        qd.SampleCount = s;
        if (SUCCEEDED(dev.device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &q, sizeof(q))) && q.NumQualityLevels > 0 &&
            SUCCEEDED(dev.device()->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &qd, sizeof(qd))) && qd.NumQualityLevels > 0)
            return s;
    }
    return 1;
}

static bool createColorTarget(GpuDevice& dev, GpuResource& r, DXGI_FORMAT fmt, uint32_t w, uint32_t h, uint32_t samples, const char* name,
                              bool uav = false) {
    D3D12_CLEAR_VALUE cv{};
    cv.Format = fmt;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (uav) flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (!dev.createTexture(r, fmt, w, h, 1, flags, D3D12_RESOURCE_STATE_RENDER_TARGET, &cv, samples, 1, name)) return false;
    dev.makeRtv(r);
    dev.makeSrv(r);
    return true;
}

static bool createDepthTarget(GpuDevice& dev, GpuResource& r, uint32_t w, uint32_t h, uint32_t samples, const char* name) {
    D3D12_CLEAR_VALUE cv{};
    cv.Format = DXGI_FORMAT_D32_FLOAT;
    cv.DepthStencil.Depth = 0.0f;
    if (!dev.createTexture(r, DXGI_FORMAT_D32_FLOAT, w, h, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv,
                           samples, 1, name))
        return false;
    dev.makeDsv(r, DXGI_FORMAT_D32_FLOAT);
    dev.makeSrv(r, DXGI_FORMAT_R32_FLOAT);
    return true;
}

static bool createScreenTargets(Renderer::Impl& P, GpuDevice& dev, uint32_t w, uint32_t h) {
    w = std::max(1u, w);
    h = std::max(1u, h);
    bool ok = createColorTarget(dev, P.hdrMS, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, P.msaa, "hdrMS") &&
              createDepthTarget(dev, P.depthMS, w, h, P.msaa, "depthMS") &&
              createColorTarget(dev, P.hdr, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, 1, "hdr") &&
              createColorTarget(dev, P.depth, DXGI_FORMAT_R32_FLOAT, w, h, 1, "depthResolved");
    if (!ok) return false;
    if (P.depthReadOnlyDsv == UINT32_MAX) P.depthReadOnlyDsv = dev.dsvHeap().alloc();
    {
        D3D12_DEPTH_STENCIL_VIEW_DESC d{};
        d.Format = DXGI_FORMAT_D32_FLOAT;
        d.ViewDimension = P.msaa > 1 ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
        d.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH;
        dev.device()->CreateDepthStencilView(P.depthMS.get(), &d, dev.dsvHeap().cpu(P.depthReadOnlyDsv));
    }
    uint32_t bw = w, bh = h;
    for (auto& b : P.bloom) {
        bw = std::max(1u, bw / 2);
        bh = std::max(1u, bh / 2);
        if (!createColorTarget(dev, b, DXGI_FORMAT_R16G16B16A16_FLOAT, bw, bh, 1, "bloom")) return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------- Renderer
Renderer::Renderer() : p_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

bool Renderer::init(GpuDevice& dev, ShaderLibrary& shaders, const sim::Database& db, const RenderSettings& s) {
    dev_ = &dev;
    shaders_ = &shaders;
    db_ = &db;
    settings_ = s;
    auto& P = *p_;
    if (!createRootSignature(dev, P.root)) return false;
    P.msaa = supportedMsaa(dev, std::max(1u, s.msaa));
    if (!createPipelines(P, dev, shaders, P.msaa, P.pso)) return false;
    if (!createScreenTargets(P, dev, dev.width(), dev.height())) return false;
    // Shadow map, wake map, adapted luminance.
    {
        D3D12_CLEAR_VALUE cv{};
        cv.Format = DXGI_FORMAT_D32_FLOAT;
        cv.DepthStencil.Depth = 1.0f;
        if (!dev.createTexture(P.shadow, DXGI_FORMAT_D32_FLOAT, s.shadowSize, s.shadowSize, 1, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL,
                               D3D12_RESOURCE_STATE_DEPTH_WRITE, &cv, 1, 1, "shadow"))
            return false;
        dev.makeDsv(P.shadow, DXGI_FORMAT_D32_FLOAT);
        dev.makeSrv(P.shadow, DXGI_FORMAT_R32_FLOAT);
    }
    if (!createColorTarget(dev, P.wake, DXGI_FORMAT_R32G32B32A32_FLOAT, kWakeMapSize, kWakeMapSize, 1, "wake")) return false;
    for (auto& l : P.lum) {
        if (!dev.createTexture(l, DXGI_FORMAT_R32_FLOAT, 1, 1, 1, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                               nullptr, 1, 1, "luminance"))
            return false;
        dev.makeSrv(l);
        dev.makeUav(l);
    }
    // Null descriptors for unused table slots.
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC d{};
        d.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        d.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        d.Texture2D.MipLevels = 1;
        P.nullTex2D = dev.stagingHeap().alloc();
        dev.device()->CreateShaderResourceView(nullptr, &d, dev.stagingHeap().cpu(P.nullTex2D));
        d.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        d.Texture2DArray.MipLevels = 1;
        d.Texture2DArray.ArraySize = 1;
        P.nullTexArray = dev.stagingHeap().alloc();
        dev.device()->CreateShaderResourceView(nullptr, &d, dev.stagingHeap().cpu(P.nullTexArray));
    }
    if (!initOcean(P, dev)) return false;
    loadShipTextures(P, dev);
    IT_LOG_INFO("Renderer ready: %ux%u, MSAA %ux, shadow %u", dev.width(), dev.height(), P.msaa, s.shadowSize);
    return true;
}

void Renderer::shutdown() {
    if (!dev_) return;
    dev_->flush();
    p_ = std::make_unique<Impl>();
    world_ = nullptr;
}

void Renderer::resize(uint32_t w, uint32_t h) {
    if (!dev_ || w == 0 || h == 0) return;
    dev_->flush();
    createScreenTargets(*p_, *dev_, w, h);
}

bool Renderer::reloadShaders() {
    Pipelines fresh;
    if (!createPipelines(*p_, *dev_, *shaders_, p_->msaa, fresh)) {
        IT_LOG_WARN("Shader reload failed; keeping previous pipelines");
        return false;
    }
    dev_->flush();
    p_->pso = std::move(fresh);
    IT_LOG_INFO("Shaders reloaded");
    return true;
}

void Renderer::setWorld(const sim::World* world) {
    world_ = world;
    auto& P = *p_;
    P.particles.clear();
    P.flashes.clear();
    P.trails.clear();
    P.torpedoTrails.clear();
    P.foam.clear();
    P.anims.clear();
    P.oceanSeaState = -1;
    P.hasTerrain = false;
    if (world && !world->terrain().empty() && dev_) {
        dev_->flush();
        buildTerrain(P, *dev_, world->terrain(), chart_);
    }
    if (world) {
        visualTime_ = world->time();
        // Seed wakes by extrapolating each ship's track backwards so a freshly loaded battle does not
        // start with ships sitting on undisturbed water.
        for (const auto& sp : world->ships()) {
            const sim::Ship& s = *sp;
            if (!s.cls || s.dmg.sunk || s.speed < 0.3) continue;
            auto& tr = P.trails[s.id];
            Vec2 stern = s.pos - s.forward() * (s.cls->length * 0.45);
            for (double t = 0.0; t <= 150.0; t += 1.0) tr.points.push_back({stern - s.velocity() * t, visualTime_ - t});
            tr.lastSeen = visualTime_;
        }
    }
}

void Renderer::onEvents(const std::vector<sim::Event>& events) {
    if (!world_) return;
    recordShipEvents(*p_, *world_, events);
    effectsEvents(*p_, *world_, events);
}

void Renderer::update(double realDt, double simDt) {
    visualTime_ += simDt;
    // Waves follow simulated time, but never run absurdly fast under high time compression.
    p_->oceanTime += std::min(simDt, realDt * 3.0) + (world_ ? 0.0 : realDt);
    if (world_) {
        updateShipAnims(*p_, *world_);
        effectsUpdate(*p_, *world_, realDt, simDt, visualTime_);
    }
}

const GpuMesh* Renderer::shipMesh(const std::string& classId) {
    const sim::ShipClassDef* c = db_ ? db_->shipClass(classId) : nullptr;
    return c ? shipMeshFor(*p_, *dev_, *c) : nullptr;
}

// ------------------------------------------------------------------------------------- frame setup
namespace {

struct FrameSetup {
    Lighting light;
    Vec3 camPos;
    Vec3 shadowCenter;
    float shadowSize = 1000;
    bool shadows = true;
};

void fillFrameConstants(Renderer::Impl& P, const Camera& cam, float w, float h, const FrameSetup& fs, double time, double dt, uint64_t frame,
                        float exposureBias, float flashGlow, uint32_t shadowRes) {
    FrameConstants& fc = P.fc;
    const Lighting& L = fs.light;
    Mat4f view = cam.view(), proj = cam.proj(w / h);
    fc.view = view;
    fc.viewProj = proj * view;
    fc.invViewProj = fc.viewProj.inverse();
    fc.shadowViewProj = shadowMatrix(L.keyDir, fs.shadowCenter, fs.camPos, fs.shadowSize, shadowRes);
    if (!fs.shadows) {
        // Map everything outside the shadow map (shadowFactor() returns 1).
        Mat4f m;
        for (auto& c : m.m)
            for (float& v : c) v = 0.0f;
        m.m[3][0] = 5.0f;
        m.m[3][3] = 1.0f;
        fc.shadowViewProj = m;
    }
    fc.cameraPos = {float(fs.camPos.x), float(fs.camPos.y), float(fs.camPos.z), float(time)};
    fc.sunDir = {L.keyDir, L.keyIntensity};
    fc.sunColor = {L.keyColor, L.keyIsMoon ? 1.0f : 0.0f};
    fc.moonDir = {L.moonDir, L.moonPhase};
    fc.trueSunDir = {L.sunDir, L.sunAlt};
    fc.ambientSky = {L.ambientSky, L.day};
    fc.ambientGround = {L.ambientGround, 0.0f};
    float density = 3.912f / L.visibility;
    fc.fogColor = {L.ambientSky * 0.15f, density};
    fc.fogParams = {L.visibility, 1.0f / 600.0f, L.rain, L.cloud};
    fc.screen = {w, h, 1.0f / w, 1.0f / h};
    fc.time = {float(time), float(dt), float(frame % 100000), std::exp2(exposureBias)};
    fc.wind = {float(L.windDir.x), float(L.windDir.y), L.windSpeed, L.seaState};
    fc.ocean = {P.cascadeL[0], P.cascadeL[1], P.cascadeL[2], P.choppiness};
    fc.terrain = P.hasTerrain ? P.terrainRect : Vec4f{1e8f, 1e8f, 1.0f, 1.0f};
    fc.lights = {float(P.lights.size()), 0, 0, 0};
    fc.sky = {1.0f - L.day, 1.0f, flashGlow, L.moonVisible};
}

D3D12_GPU_DESCRIPTOR_HANDLE globalTable(Renderer::Impl& P, GpuDevice& dev, bool sceneDepth, bool usePreviewLights = false) {
    TableBuilder t(dev, 8);
    for (uint32_t i = 0; i < 8; ++i) t.srv(i, P.nullTex2D);
    size_t n = usePreviewLights ? 1 : std::max<size_t>(1, P.lights.size());
    UploadAlloc a = dev.upload(n * sizeof(GpuLight), sizeof(GpuLight));
    std::memset(a.cpu, 0, n * sizeof(GpuLight));
    if (!usePreviewLights && !P.lights.empty()) std::memcpy(a.cpu, P.lights.data(), P.lights.size() * sizeof(GpuLight));
    t.buffer(0, a, uint32_t(n), sizeof(GpuLight));
    t.srv(1, P.shadow.srv);
    t.srv(2, P.disp.srv);
    t.srv(3, P.normal[P.normalCur].srv);
    if (P.hasTerrain) t.srv(4, P.terrainHeight.srv);
    t.srv(5, P.wake.srv);
    if (sceneDepth) t.srv(6, P.depth.srv);
    return t.handle();
}

void viewport(ID3D12GraphicsCommandList* cmd, uint32_t w, uint32_t h) {
    D3D12_VIEWPORT vp{0, 0, float(w), float(h), 0.0f, 1.0f};
    D3D12_RECT sc{0, 0, LONG(w), LONG(h)};
    cmd->RSSetViewports(1, &vp);
    cmd->RSSetScissorRects(1, &sc);
}

FrameSetup frameSetupFor(const sim::World* world, const Camera& cam) {
    FrameSetup fs;
    fs.camPos = cam.pos;
    fs.light = lightingFor(world, cam.pos);
    Vec3f f = cam.forward();
    double reach = 600.0;
    if (f.y < -0.01f) reach = clamp(cam.pos.y / -f.y, 60.0, 1500.0);
    fs.shadowCenter = cam.pos + Vec3{f.x, f.y, f.z} * reach;
    fs.shadowCenter.y = 0.0;
    fs.shadowSize = float(clamp(reach * 1.6, 350.0, 2400.0));
    fs.shadows = fs.light.keyIntensity > 0.004f;
    return fs;
}

}  // namespace

void renderWakeMap(Renderer::Impl& P, GpuDevice& dev, const Vec3& camPos) {
    auto* cmd = dev.cmd();
    // Snap the map centre to whole texels so the foam does not crawl as the camera moves.
    double texel = 2.0 * kWakeHalfSize / kWakeMapSize;
    Vec2 c{std::round(camPos.x / texel) * texel, std::round(camPos.z / texel) * texel};
    WakePass wp{{float(c.x), float(c.y), float(kWakeHalfSize), 0.0f}};
    dev.transition(P.wake, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto rtv = dev.rtvHeap().cpu(P.wake.rtv);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    viewport(cmd, kWakeMapSize, kWakeMapSize);
    cmd->SetGraphicsRootConstantBufferView(2, dev.constants(wp));
    cmd->SetPipelineState(P.pso.wakeClear.get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);
    if (!P.wakeVerts.empty()) {
        size_t bytes = P.wakeVerts.size() * sizeof(float);
        UploadAlloc a = dev.upload(bytes, 16);
        std::memcpy(a.cpu, P.wakeVerts.data(), bytes);
        D3D12_VERTEX_BUFFER_VIEW vb{a.gpu, UINT(bytes), 12};
        cmd->IASetVertexBuffers(0, 1, &vb);
        cmd->SetPipelineState(P.pso.wake.get());
        cmd->DrawInstanced(UINT(P.wakeVerts.size() / 3), 1, 0, 0);
    }
    dev.transition(P.wake, kSrvState);
}

/// Muzzle flashes and gun smoke of the weapons drill (the preview has no effects simulation).
static void drawPreviewEffects(Renderer::Impl& P, GpuDevice& dev, const sim::ShipClassDef& cls, const GpuMesh& mesh, const ShipPose& pose,
                               const Mat4f& world, double drillTime) {
    double since = procgen::anim::drill(drillTime).sinceSalvo;
    std::vector<GpuParticle> fx;
    auto add = [&](Vec3f pos, float size, Vec4f color, int type, float age, float emissive, float seed) {
        GpuParticle g{};
        g.pos = world.transformPoint(pos);
        g.axis = g.pos;
        g.size = size;
        g.color = color;
        g.type = float(type);
        g.age = age;
        g.emissive = emissive;
        g.seed = seed;
        fx.push_back(g);
    };
    for (const Muzzle& mz : muzzles(mesh, pose)) {
        const auto& m = cls.mounts[size_t(mz.mount)];
        double cal = m.gun ? m.gun->caliberMm : 127.0;
        float k = float(cal / 203.0), sk = std::sqrt(k);
        double t = since - 0.12 * mz.mount - procgen::anim::barrelDelay(mz.barrel);
        if (t < 0.0) continue;
        Vec3f up{0, 0, 1};
        float seed = float(mz.mount * 7 + mz.barrel);
        if (t < 0.25) {
            float a1 = float(t / 0.14), a2 = float(t / 0.25);
            if (a1 < 1.0f) add(mz.pos + mz.dir * float(cal * 0.03), 3.0f + 6.0f * k, {1.0f, 0.72f, 0.38f, 1.0f}, kGlow, a1, 40.0f * sk * (1 - a1) * (1 - a1), seed);
            add(mz.pos, 8.0f + 12.0f * k, {1.0f, 0.55f, 0.25f, 1.0f}, kGlow, a2, 3.0f * sk * (1 - a2) * (1 - a2), seed);
        }
        if (t < 6.0) {
            // Brown cordite smoke rolling out ahead of the muzzle and rising.
            float age = float(t / 8.0);
            for (int q = 0; q < 2; ++q) {
                float push = float(cal * 0.05 * (q + 1)) + float(6.0 * std::sqrt(t)) * k * (1.0f + q);
                Vec3f p = mz.pos + mz.dir * push + up * float(0.6 * t);
                float alpha = 0.55f * std::min(1.0f, age * 8.0f) * (1.0f - age * age);
                add(p, 3.0f + 3.0f * k + float(t) * (2.0f + 2.0f * k), {0.42f, 0.40f, 0.37f, alpha}, kSmoke, age, 0.6f * std::max(0.0f, 1.0f - float(t) * 4.0f), seed + q);
            }
        }
    }
    if (fx.empty()) return;
    std::sort(fx.begin(), fx.end(), [](const GpuParticle& a, const GpuParticle& b) { return lengthSq(a.pos) > lengthSq(b.pos); });
    auto* cmd = dev.cmd();
    TableBuilder t(dev, 8);
    for (uint32_t i = 0; i < 8; ++i) t.srv(i, P.nullTex2D);
    UploadAlloc a = dev.upload(fx.size() * sizeof(GpuParticle), sizeof(GpuParticle));
    std::memcpy(a.cpu, fx.data(), fx.size() * sizeof(GpuParticle));
    t.buffer(0, a, uint32_t(fx.size()), sizeof(GpuParticle));
    cmd->SetGraphicsRootDescriptorTable(4, t.handle());
    cmd->SetPipelineState(P.pso.particles.get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    cmd->DrawInstanced(4, UINT(fx.size()), 0, 0);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

static void renderShadowMap(Renderer::Impl& P, GpuDevice& dev, const sim::World* world, const Camera& cam, double visualTime, bool enabled,
                            const GpuMesh* previewMesh = nullptr, const Mat4f* previewWorld = nullptr, const ShipPose* previewPose = nullptr) {
    auto* cmd = dev.cmd();
    dev.transition(P.shadow, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto dsv = dev.dsvHeap().cpu(P.shadow.dsv);
    cmd->OMSetRenderTargets(0, nullptr, FALSE, &dsv);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
    if (enabled) {
        viewport(cmd, P.shadow.width, P.shadow.height);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        if (previewMesh) {
            cmd->SetPipelineState(P.pso.shipShadow.get());
            drawShipModel(P, dev, *previewMesh, *previewWorld, {}, {}, *previewPose);
        } else if (world) {
            cmd->SetPipelineState(P.pso.shipShadow.get());
            drawShips(P, dev, *world, cam, true, sim::kNoEntity, visualTime);
            if (P.hasTerrain) {
                cmd->SetPipelineState(P.pso.terrainShadow.get());
                drawTerrain(P, dev);
            }
        }
    }
    dev.transition(P.shadow, kSrvState);
}

/// Bloom chain, eye adaptation and tone mapping of `hdr` into `target`.
static void postProcess(Renderer::Impl& P, GpuDevice& dev, const RenderSettings& s, float night, D3D12_CPU_DESCRIPTOR_HANDLE target, uint32_t w,
                        uint32_t h) {
    auto* cmd = dev.cmd();
    PostPass pp{};
    pp.expo = {1.6f, 0.06f, 10.0f, 0.14f};
    pp.grade = {0.32f, s.filmGrain ? 0.018f : 0.0f, night, 1.0f - 0.25f * night};
    // Eye adaptation.
    int next = 1 - P.lumCur;
    dev.transition(P.lum[P.lumCur], kSrvState);
    dev.transition(P.lum[next], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    {
        TableBuilder srv(dev, 8), uav(dev, 4);
        for (uint32_t i = 0; i < 8; ++i) srv.srv(i, P.nullTex2D);
        srv.srv(0, P.hdr.srv);
        srv.srv(2, P.lum[P.lumCur].srv);
        for (uint32_t i = 0; i < 4; ++i) uav.srv(i, P.lum[next].uav[0]);
        cmd->SetPipelineState(P.pso.luminance.get());
        cmd->SetComputeRootConstantBufferView(2, dev.constants(pp));
        cmd->SetComputeRootDescriptorTable(4, srv.handle());
        cmd->SetComputeRootDescriptorTable(5, uav.handle());
        cmd->Dispatch(1, 1, 1);
    }
    dev.transition(P.lum[next], kSrvState);
    P.lumCur = next;
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    auto passTable = [&](uint32_t src, uint32_t bloom) {
        TableBuilder t(dev, 8);
        for (uint32_t i = 0; i < 8; ++i) t.srv(i, P.nullTex2D);
        t.srv(0, src);
        t.srv(1, bloom);
        t.srv(2, P.lum[P.lumCur].srv);
        return t.handle();
    };
    // Bloom: threshold + downsample chain, then additive upsampling back to level 0.
    const int levels = int(std::size(P.bloom));
    for (int i = 0; i < levels; ++i) {
        GpuResource& dst = P.bloom[i];
        GpuResource& src = i == 0 ? P.hdr : P.bloom[i - 1];
        dev.transition(src, kSrvState);
        dev.transition(dst, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = dev.rtvHeap().cpu(dst.rtv);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        viewport(cmd, dst.width, dst.height);
        pp.post = {s.bloom, 1.0f, 1.0f / float(src.width), 1.0f / float(src.height)};
        cmd->SetGraphicsRootConstantBufferView(2, dev.constants(pp));
        cmd->SetGraphicsRootDescriptorTable(4, passTable(src.srv, P.nullTex2D));
        cmd->SetPipelineState(i == 0 ? P.pso.bloomFirst.get() : P.pso.bloomDown.get());
        cmd->DrawInstanced(3, 1, 0, 0);
    }
    cmd->SetPipelineState(P.pso.bloomUp.get());
    for (int i = levels - 1; i > 0; --i) {
        GpuResource& src = P.bloom[i];
        GpuResource& dst = P.bloom[i - 1];
        dev.transition(src, kSrvState);
        dev.transition(dst, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto rtv = dev.rtvHeap().cpu(dst.rtv);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        viewport(cmd, dst.width, dst.height);
        pp.post = {s.bloom, 1.0f, 1.0f / float(src.width), 1.0f / float(src.height)};
        cmd->SetGraphicsRootConstantBufferView(2, dev.constants(pp));
        cmd->SetGraphicsRootDescriptorTable(4, passTable(src.srv, P.nullTex2D));
        cmd->DrawInstanced(3, 1, 0, 0);
    }
    dev.transition(P.bloom[0], kSrvState);
    // Tone map into the target.
    cmd->OMSetRenderTargets(1, &target, FALSE, nullptr);
    viewport(cmd, w, h);
    pp.post = {s.bloom, 1.0f, 1.0f / float(w), 1.0f / float(h)};
    cmd->SetGraphicsRootConstantBufferView(2, dev.constants(pp));
    cmd->SetGraphicsRootDescriptorTable(4, passTable(P.hdr.srv, P.bloom[0].srv));
    cmd->SetPipelineState(P.pso.tonemap.get());
    cmd->DrawInstanced(3, 1, 0, 0);
}

static void clearUavsOnce(Renderer::Impl& P, GpuDevice& dev) {
    if (P.clearedUavs) return;
    P.clearedUavs = true;
    auto* cmd = dev.cmd();
    const float zero[4] = {0, 0, 0, 0};
    auto clear = [&](GpuResource& r) {
        dev.transition(r, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        uint32_t slot = dev.allocTable(1);
        dev.device()->CopyDescriptorsSimple(1, dev.gpuHeap().cpu(slot), dev.stagingHeap().cpu(r.uav[0]), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cmd->ClearUnorderedAccessViewFloat(dev.gpuHeap().gpu(slot), dev.stagingHeap().cpu(r.uav[0]), r.get(), zero, 0, nullptr);
    };
    for (auto& l : P.lum) clear(l);
    for (auto& n : P.normal) clear(n);
}

static void ensureOcean(Renderer::Impl& P, GpuDevice& dev, const Lighting& L) {
    if (std::abs(P.oceanSeaState - L.seaState) > 0.01) {
        buildOceanSpectrum(P, dev, L.seaState, L.windDir, L.windSpeed);
        P.oceanSeaState = L.seaState;
    }
    if (P.oceanFrame != dev.frameCounter()) {
        dispatchOcean(P, dev, P.oceanTime);
        P.oceanFrame = dev.frameCounter();
    }
}

void Renderer::render(const Camera& cam, const SceneOptions& opt) {
    if (!p_->root || !dev_) return;
    auto& P = *p_;
    auto& dev = *dev_;
    auto* cmd = dev.cmd();
    const uint32_t w = dev.width(), h = dev.height();
    if (P.hdrMS.width != w || P.hdrMS.height != h) createScreenTargets(P, dev, w, h);
    P.cam = cam;
    cmd->SetGraphicsRootSignature(P.root.get());
    cmd->SetComputeRootSignature(P.root.get());
    clearUavsOnce(P, dev);

    FrameSetup fs = frameSetupFor(world_, cam);
    ensureOcean(P, dev, fs.light);
    if (world_) effectsGather(P, *world_, cam, visualTime_);
    else {
        P.lights.clear();
        P.gpuParticles.clear();
        P.wakeVerts.clear();
    }
    fillFrameConstants(P, cam, float(w), float(h), fs, visualTime_, 1.0 / 60.0, dev.frameCounter(), settings_.exposureBias, float(P.flashGlow),
                       P.shadow.width);
    D3D12_GPU_VIRTUAL_ADDRESS fcAddr = dev.constants(P.fc);
    cmd->SetGraphicsRootConstantBufferView(0, fcAddr);
    cmd->SetComputeRootConstantBufferView(0, fcAddr);

    renderWakeMap(P, dev, cam.pos);
    renderShadowMap(P, dev, world_, cam, visualTime_, fs.shadows);

    // ---- Main opaque pass (MSAA)
    dev.transition(P.hdrMS, D3D12_RESOURCE_STATE_RENDER_TARGET);
    dev.transition(P.depthMS, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto rtv = dev.rtvHeap().cpu(P.hdrMS.rtv);
    auto dsv = dev.dsvHeap().cpu(P.depthMS.dsv);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const float black[4] = {0, 0, 0, 1};
    cmd->ClearRenderTargetView(rtv, black, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
    viewport(cmd, w, h);
    cmd->SetGraphicsRootDescriptorTable(3, globalTable(P, dev, false));
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    if (world_) {
        cmd->SetPipelineState(P.pso.ship.get());
        drawShips(P, dev, *world_, cam, false, opt.selected, visualTime_);
        if (P.hasTerrain) {
            cmd->SetPipelineState(P.pso.terrain.get());
            drawTerrain(P, dev);
        }
    }
    cmd->SetPipelineState(P.pso.ocean.get());
    drawOcean(P, dev);
    cmd->SetPipelineState(P.pso.sky.get());
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawInstanced(3, 1, 0, 0);

    // ---- Depth resolve (soft particles, picking helpers)
    dev.transition(P.depthMS, D3D12_RESOURCE_STATE_DEPTH_READ | kSrvState);
    dev.transition(P.depth, D3D12_RESOURCE_STATE_RENDER_TARGET);
    {
        auto drtv = dev.rtvHeap().cpu(P.depth.rtv);
        cmd->OMSetRenderTargets(1, &drtv, FALSE, nullptr);
        TableBuilder t(dev, 8);
        for (uint32_t i = 0; i < 8; ++i) t.srv(i, P.nullTex2D);
        t.srv(3, P.depthMS.srv);
        cmd->SetGraphicsRootDescriptorTable(4, t.handle());
        cmd->SetPipelineState(P.pso.resolveDepth.get());
        cmd->DrawInstanced(3, 1, 0, 0);
    }
    dev.transition(P.depth, kSrvState);

    // ---- Particles (MSAA target, depth-tested against the read-only depth buffer)
    if (!P.gpuParticles.empty()) {
        auto ro = dev.dsvHeap().cpu(P.depthReadOnlyDsv);
        cmd->OMSetRenderTargets(1, &rtv, FALSE, &ro);
        cmd->SetGraphicsRootDescriptorTable(3, globalTable(P, dev, true));
        TableBuilder t(dev, 8);
        for (uint32_t i = 0; i < 8; ++i) t.srv(i, P.nullTex2D);
        UploadAlloc a = dev.upload(P.gpuParticles.size() * sizeof(GpuParticle), sizeof(GpuParticle));
        std::memcpy(a.cpu, P.gpuParticles.data(), P.gpuParticles.size() * sizeof(GpuParticle));
        t.buffer(0, a, uint32_t(P.gpuParticles.size()), sizeof(GpuParticle));
        cmd->SetGraphicsRootDescriptorTable(4, t.handle());
        cmd->SetPipelineState(P.pso.particles.get());
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        cmd->DrawInstanced(4, UINT(P.gpuParticles.size()), 0, 0);
        cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    // ---- Resolve HDR
    if (P.msaa > 1) {
        dev.transition(P.hdrMS, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        dev.transition(P.hdr, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cmd->ResolveSubresource(P.hdr.get(), 0, P.hdrMS.get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
    } else {
        dev.transition(P.hdrMS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dev.transition(P.hdr, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(P.hdr.get(), P.hdrMS.get());
    }
    dev.transition(P.hdr, kSrvState);
    dev.transition(P.depthMS, D3D12_RESOURCE_STATE_DEPTH_WRITE);

    postProcess(P, dev, settings_, 1.0f - fs.light.day, dev.backBufferRtv(), w, h);
}

// ------------------------------------------------------------------------------------------ preview
uint64_t Renderer::shipPreview(const std::string& classId, const PreviewParams& view, uint32_t w, uint32_t h) {
    const float yaw = view.yaw, pitch = view.pitch;
    const bool night = view.night;
    if (!dev_ || !p_->root || w == 0 || h == 0) return 0;
    auto& P = *p_;
    auto& dev = *dev_;
    auto* cmd = dev.cmd();
    if (!db_->shipClass(classId))
        if (const sim::AircraftDef* a = db_->aircraft(classId)) return aircraftPreview(*a, view, w, h);
    const GpuMesh* mesh = shipMesh(classId);
    const sim::ShipClassDef* cls = db_->shipClass(classId);
    if (!mesh || !cls) return 0;
    auto& pv = P.preview;
    if (pv.w != w || pv.h != h) {
        dev.flush();
        createColorTarget(dev, pv.hdrMS, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, P.msaa, "previewMS");
        createDepthTarget(dev, pv.depthMS, w, h, P.msaa, "previewDepth");
        createColorTarget(dev, pv.hdr, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, 1, "previewHdr");
        createColorTarget(dev, pv.ldr, DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, "previewLdr");
        dev.makeUiSrv(pv.ldr);
        pv.w = w;
        pv.h = h;
    }
    cmd->SetGraphicsRootSignature(P.root.get());
    cmd->SetComputeRootSignature(P.root.get());
    clearUavsOnce(P, dev);

    // Studio-like scene far from any map so terrain and wakes stay out of the picture.
    const Vec3 origin{200000.0, 0.0, 200000.0};
    Aabb b = mesh->bounds;
    Vec3f ext = b.extent();
    double radius = std::max({double(ext.x), double(ext.y), double(ext.z)}) * 1.15 + 2.0;
    // Model x (bow) maps to world +z at heading 0.
    Vec3 target = origin + Vec3{0.0, double(b.center().z) * 0.6 + view.lift, double(view.focus) * double(cls->length)};
    Camera cam;
    cam.fovY = 30;
    // Fit the hull length horizontally and the masts vertically.
    double aspect = double(w) / double(h);
    double tanY = std::tan(cam.fovY * 0.5 * kDegToRad), tanX = tanY * aspect;
    double dist = std::max({double(ext.x) * 1.12 / tanX, double(ext.z + 4.0) * 1.6 / tanY, double(ext.y) * 3.0 / tanY}) + double(ext.y);
    dist = std::max(4.0, dist / std::clamp(double(view.zoom), 0.5, 40.0));
    double cp = std::cos(pitch * kDegToRad), sp = std::sin(pitch * kDegToRad);
    Vec3 dir{std::sin(yaw * kDegToRad) * cp, sp, std::cos(yaw * kDegToRad) * cp};
    cam.pos = target - dir * dist;
    cam.pos.y = std::max(cam.pos.y, 1.5);
    Vec3 look = target - cam.pos;
    cam.yaw = bearingOf({look.x, look.z});
    cam.pitch = std::atan2(look.y, std::sqrt(look.x * look.x + look.z * look.z)) * kRadToDeg;
    cam.nearZ = std::min(0.5, dist * 0.02);

    FrameSetup fs;
    fs.camPos = cam.pos;
    fs.light = night ? computeLighting(-30, 0, 40, yaw + 140, 0.9, 0.2, 30, 2, 120, 8, 0)
                     : computeLighting(32, yaw + 135, -20, 0, 0.5, 0.25, 40, 2, 120, 8, 0);
    fs.shadowCenter = origin;
    fs.shadowSize = float(radius * 2.4);
    fs.shadows = true;
    ensureOcean(P, dev, fs.light);
    auto savedLights = std::move(P.lights);
    P.lights.clear();
    bool savedTerrain = P.hasTerrain;
    P.hasTerrain = false;

    // Model matrix (camera relative); stowed armament, or the weapons drill.
    Vec3 rel = origin - cam.pos;
    Mat4f world = Mat4f::translation({float(rel.x), float(rel.y), float(rel.z)}) * modelToRenderBasis(0.0);
    double drillT = view.drillTime >= 0.0f ? double(view.drillTime) : P.oceanTime;
    ShipPose pose = view.animate ? drillPose(*cls, *mesh, drillT) : stowedPose(*cls);
    if (!view.animate) pose.radarAngle = float(std::fmod(P.oceanTime * 25.0, 360.0));
    pose.searchlightOn = night;
    if (view.animate) {
        // Muzzle flashes light up the ship and the sea for an instant after each salvo.
        double since = procgen::anim::drill(drillT).sinceSalvo;
        for (const Muzzle& mz : muzzles(*mesh, pose)) {
            double cal = cls->mounts[size_t(mz.mount)].gun ? cls->mounts[size_t(mz.mount)].gun->caliberMm : 127.0;
            double t = since - 0.12 * mz.mount;
            if (t < 0.0 || t > 0.18 || mz.barrel != 0) continue;
            float k = float(std::exp(-t * 18.0) * cal / 203.0);
            GpuLight l{};
            l.pos = world.transformPoint(mz.pos + mz.dir * 6.0f);
            l.radius = 200.0f;
            l.color = Vec3f{1.0f, 0.62f, 0.3f} * (30.0f * k);
            if (P.lights.size() < 16) P.lights.push_back(l);
        }
    }
    fillFrameConstants(P, cam, float(w), float(h), fs, P.oceanTime, 1.0 / 60.0, dev.frameCounter(), 0.0f, 0.0f, P.shadow.width);
    D3D12_GPU_VIRTUAL_ADDRESS fcAddr = dev.constants(P.fc);
    cmd->SetGraphicsRootConstantBufferView(0, fcAddr);
    cmd->SetComputeRootConstantBufferView(0, fcAddr);

    renderShadowMap(P, dev, nullptr, cam, 0.0, true, mesh, &world, &pose);

    dev.transition(pv.hdrMS, D3D12_RESOURCE_STATE_RENDER_TARGET);
    dev.transition(pv.depthMS, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto rtv = dev.rtvHeap().cpu(pv.hdrMS.rtv);
    auto dsv = dev.dsvHeap().cpu(pv.depthMS.dsv);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const float black[4] = {0, 0, 0, 1};
    cmd->ClearRenderTargetView(rtv, black, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
    viewport(cmd, w, h);
    cmd->SetGraphicsRootDescriptorTable(3, globalTable(P, dev, false, P.lights.empty()));
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetPipelineState(P.pso.ship.get());
    bindShipTextures(P, dev);
    Vec4f params{float(std::hash<std::string>{}(classId) % 97), 0.0f, float(cls->length), float(cls->model.freeboardMid)};
    drawShipModel(P, dev, *mesh, world, {0, 0, 0, night ? 1.0f : 0.0f}, params, pose);
    cmd->SetPipelineState(P.pso.ocean.get());
    drawOcean(P, dev);
    cmd->SetPipelineState(P.pso.sky.get());
    cmd->DrawInstanced(3, 1, 0, 0);
    if (view.animate) drawPreviewEffects(P, dev, *cls, *mesh, pose, world, drillT);

    if (P.msaa > 1) {
        dev.transition(pv.hdrMS, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        dev.transition(pv.hdr, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cmd->ResolveSubresource(pv.hdr.get(), 0, pv.hdrMS.get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
    } else {
        dev.transition(pv.hdrMS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dev.transition(pv.hdr, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(pv.hdr.get(), pv.hdrMS.get());
    }
    dev.transition(pv.hdr, kSrvState);
    dev.transition(pv.ldr, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto lrtv = dev.rtvHeap().cpu(pv.ldr.rtv);
    cmd->OMSetRenderTargets(1, &lrtv, FALSE, nullptr);
    PostPass pp{};
    pp.expo = {0, 0, 0, night ? 9.0f : 0.85f};
    cmd->SetGraphicsRootConstantBufferView(2, dev.constants(pp));
    TableBuilder t(dev, 8);
    for (uint32_t i = 0; i < 8; ++i) t.srv(i, P.nullTex2D);
    t.srv(0, pv.hdr.srv);
    cmd->SetGraphicsRootDescriptorTable(4, t.handle());
    cmd->SetPipelineState(P.pso.tonemapPreview.get());
    cmd->DrawInstanced(3, 1, 0, 0);
    dev.transition(pv.ldr, kSrvState);

    P.lights = std::move(savedLights);
    P.hasTerrain = savedTerrain;
    return dev.gpuHeap().gpu(pv.ldr.uiSrv).ptr;
}

uint64_t Renderer::aircraftPreview(const sim::AircraftDef& a, const PreviewParams& view, uint32_t w, uint32_t h) {
    auto& P = *p_;
    auto& dev = *dev_;
    auto* cmd = dev.cmd();
    const GpuMesh* mesh = aircraftMeshFor(P, dev, a);
    if (!mesh) return 0;
    auto& pv = P.preview;
    if (pv.w != w || pv.h != h) {
        dev.flush();
        createColorTarget(dev, pv.hdrMS, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, P.msaa, "previewMS");
        createDepthTarget(dev, pv.depthMS, w, h, P.msaa, "previewDepth");
        createColorTarget(dev, pv.hdr, DXGI_FORMAT_R16G16B16A16_FLOAT, w, h, 1, "previewHdr");
        createColorTarget(dev, pv.ldr, DXGI_FORMAT_R8G8B8A8_UNORM, w, h, 1, "previewLdr");
        dev.makeUiSrv(pv.ldr);
        pv.w = w;
        pv.h = h;
    }
    cmd->SetGraphicsRootSignature(P.root.get());
    cmd->SetComputeRootSignature(P.root.get());
    clearUavsOnce(P, dev);

    // In flight a little above the sea, far from any map; the camera circles it.
    const bool night = view.night;
    const double altitude = a.model.layout == "float" || a.model.layout == "boat" ? 25.0 : 40.0;
    const Vec3 origin{200000.0, altitude, 200000.0};
    Aabb b = mesh->bounds;
    Vec3f ext = b.extent();
    Camera cam;
    cam.fovY = 30;
    double aspect = double(w) / double(h);
    double tanY = std::tan(cam.fovY * 0.5 * kDegToRad), tanX = tanY * aspect;
    double size = std::max({double(ext.x), double(ext.y), double(ext.z)});
    double dist = std::max(size * 0.75 / tanX, size * 0.6 / tanY) + size * 0.5;
    dist = std::max(2.0, dist / std::clamp(double(view.zoom), 0.5, 40.0));
    Vec3 target = origin + Vec3{0.0, double(view.lift) * 0.2, double(view.focus) * a.model.length};
    double cp = std::cos(view.pitch * kDegToRad), sp = std::sin(view.pitch * kDegToRad);
    Vec3 dir{std::sin(view.yaw * kDegToRad) * cp, sp, std::cos(view.yaw * kDegToRad) * cp};
    cam.pos = target - dir * dist;
    cam.pos.y = std::max(cam.pos.y, 1.5);
    Vec3 look = target - cam.pos;
    cam.yaw = bearingOf({look.x, look.z});
    cam.pitch = std::atan2(look.y, std::sqrt(look.x * look.x + look.z * look.z)) * kRadToDeg;
    cam.nearZ = std::min(0.3, dist * 0.02);

    FrameSetup fs;
    fs.camPos = cam.pos;
    fs.light = night ? computeLighting(-30, 0, 40, view.yaw + 140, 0.9, 0.2, 30, 2, 120, 8, 0)
                     : computeLighting(38, view.yaw + 135, -20, 0, 0.35, 0.25, 40, 2, 120, 8, 0);
    fs.shadowCenter = origin;
    fs.shadowSize = float(size * 3.0);
    fs.shadows = true;
    ensureOcean(P, dev, fs.light);
    auto savedLights = std::move(P.lights);
    P.lights.clear();
    bool savedTerrain = P.hasTerrain;
    P.hasTerrain = false;

    Vec3 rel = origin - cam.pos;
    // A gentle bank in the demonstration, level otherwise.
    float bank = view.animate ? float(18.0 * std::sin(P.oceanTime * 0.6)) : 0.0f;
    Mat4f world = Mat4f::translation({float(rel.x), float(rel.y), float(rel.z)}) * modelToRenderBasis(0.0) * rotX(-bank);
    fillFrameConstants(P, cam, float(w), float(h), fs, P.oceanTime, 1.0 / 60.0, dev.frameCounter(), 0.0f, 0.0f, P.shadow.width);
    D3D12_GPU_VIRTUAL_ADDRESS fcAddr = dev.constants(P.fc);
    cmd->SetGraphicsRootConstantBufferView(0, fcAddr);
    cmd->SetComputeRootConstantBufferView(0, fcAddr);
    ShipPose still;
    renderShadowMap(P, dev, nullptr, cam, 0.0, true, mesh, &world, &still);

    dev.transition(pv.hdrMS, D3D12_RESOURCE_STATE_RENDER_TARGET);
    dev.transition(pv.depthMS, D3D12_RESOURCE_STATE_DEPTH_WRITE);
    auto rtv = dev.rtvHeap().cpu(pv.hdrMS.rtv);
    auto dsv = dev.dsvHeap().cpu(pv.depthMS.dsv);
    cmd->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
    const float black[4] = {0, 0, 0, 1};
    cmd->ClearRenderTargetView(rtv, black, 0, nullptr);
    cmd->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
    viewport(cmd, w, h);
    cmd->SetGraphicsRootDescriptorTable(3, globalTable(P, dev, false, true));
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetPipelineState(P.pso.ship.get());
    bindShipTextures(P, dev);
    // The load of the type's first loadout hangs under it; the propellers turn.
    const sim::AircraftLoadout* load = a.loadouts.empty() ? nullptr : &a.loadouts.front();
    bool torpedo = load && load->torpedo, bombs = load && load->bomb && !load->torpedo;
    float spin = float(std::fmod(P.oceanTime * 7.3 * 360.0, 360.0));
    Vec4f params{float(std::hash<std::string>{}(a.id) % 97), -1e4f, float(a.model.length), 2.0f};
    cmd->IASetVertexBuffers(0, 1, &mesh->vbv);
    cmd->IASetIndexBuffer(&mesh->ibv);
    for (const auto& part : mesh->parts) {
        if (!part.indexCount) continue;
        Mat4f local;
        if (part.kind == procgen::PartKind::Propeller) local = aboutPivot(part.pivot, rotX(part.sub == 1 ? -spin : spin));
        if (part.kind == procgen::PartKind::Ordnance && ((part.sub == 0 && !torpedo) || (part.sub == 1 && !bombs))) continue;
        ObjectConstants oc{world * local, {0, 0, 0, 0}, params};
        cmd->SetGraphicsRootConstantBufferView(1, dev.constants(oc));
        cmd->DrawIndexedInstanced(part.indexCount, 1, part.firstIndex, 0, 0);
    }
    cmd->SetPipelineState(P.pso.ocean.get());
    drawOcean(P, dev);
    cmd->SetPipelineState(P.pso.sky.get());
    cmd->DrawInstanced(3, 1, 0, 0);

    if (P.msaa > 1) {
        dev.transition(pv.hdrMS, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
        dev.transition(pv.hdr, D3D12_RESOURCE_STATE_RESOLVE_DEST);
        cmd->ResolveSubresource(pv.hdr.get(), 0, pv.hdrMS.get(), 0, DXGI_FORMAT_R16G16B16A16_FLOAT);
    } else {
        dev.transition(pv.hdrMS, D3D12_RESOURCE_STATE_COPY_SOURCE);
        dev.transition(pv.hdr, D3D12_RESOURCE_STATE_COPY_DEST);
        cmd->CopyResource(pv.hdr.get(), pv.hdrMS.get());
    }
    dev.transition(pv.hdr, kSrvState);
    dev.transition(pv.ldr, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto lrtv = dev.rtvHeap().cpu(pv.ldr.rtv);
    cmd->OMSetRenderTargets(1, &lrtv, FALSE, nullptr);
    PostPass pp{};
    pp.expo = {0, 0, 0, night ? 9.0f : 0.85f};
    cmd->SetGraphicsRootConstantBufferView(2, dev.constants(pp));
    TableBuilder t(dev, 8);
    for (uint32_t i = 0; i < 8; ++i) t.srv(i, P.nullTex2D);
    t.srv(0, pv.hdr.srv);
    cmd->SetGraphicsRootDescriptorTable(4, t.handle());
    cmd->SetPipelineState(P.pso.tonemapPreview.get());
    cmd->DrawInstanced(3, 1, 0, 0);
    dev.transition(pv.ldr, kSrvState);

    P.lights = std::move(savedLights);
    P.hasTerrain = savedTerrain;
    return dev.gpuHeap().gpu(pv.ldr.uiSrv).ptr;
}

// --------------------------------------------------------------------------------------------- picking
sim::EntityId Renderer::pick(const Camera& cam, float sx, float sy) const {
    if (!world_ || !dev_) return sim::kNoEntity;
    Vec3 origin;
    Vec3f dirf;
    cam.ray(sx, sy, float(dev_->width()), float(dev_->height()), origin, dirf);
    Vec3 dir{dirf.x, dirf.y, dirf.z};
    double best = 1e18;
    sim::EntityId hit = sim::kNoEntity;
    for (auto& sp : world_->ships()) {
        const sim::Ship& s = *sp;
        if (!s.alive() || !s.cls) continue;
        // Ray into the ship frame: x forward, y starboard, z up (render y).
        Vec3 rel = origin - Vec3{s.pos.x, 0.0, s.pos.y};
        Vec2 fwd = s.forward(), right = s.right();
        Vec3 o{rel.x * fwd.x + rel.z * fwd.y, rel.x * right.x + rel.z * right.y, rel.y};
        Vec3 d{dir.x * fwd.x + dir.z * fwd.y, dir.x * right.x + dir.z * right.y, dir.y};
        double half[3] = {s.cls->length * 0.5, std::max(8.0, s.cls->beam * 0.5 + 4.0), s.cls->mastHeight};
        double lo[3] = {-half[0], -half[1], -s.cls->draft}, hi[3] = {half[0], half[1], half[2]};
        double t0 = 0, t1 = 1e18;
        bool miss = false;
        for (int a = 0; a < 3 && !miss; ++a) {
            if (std::abs(d[a]) < 1e-12) {
                if (o[a] < lo[a] || o[a] > hi[a]) miss = true;
                continue;
            }
            double ta = (lo[a] - o[a]) / d[a], tb = (hi[a] - o[a]) / d[a];
            if (ta > tb) std::swap(ta, tb);
            t0 = std::max(t0, ta);
            t1 = std::min(t1, tb);
            if (t0 > t1) miss = true;
        }
        if (!miss && t0 < best) {
            best = t0;
            hit = s.id;
        }
    }
    // Aircraft: a sphere around each airframe, never smaller than a few pixels' worth at its distance.
    for (const auto& a : world_->planes()) {
        if (!a.flying() || !a.def) continue;
        Vec3 c{a.pos.x, a.pos.z, a.pos.y};
        Vec3 oc = c - origin;
        double t = dot(oc, dir);
        if (t <= 0.0 || t >= best) continue;
        double r = std::max(a.def->model.span * 0.7, t * 0.012);
        if (lengthSq(oc - dir * t) < r * r) {
            best = t;
            hit = a.id;
        }
    }
    return hit;
}

}  // namespace it::render
