// FFT ocean: directional Pierson-Moskowitz spectrum (CPU), time evolution and inverse FFT (GPU), radial grid mesh.
#include <cstring>

#include "render/RendererImpl.h"

namespace it::render {

namespace {

constexpr uint32_t kN = 256;
constexpr uint32_t kCascades = 3;

struct OceanPass {
    Vec4f cascadeL, time;
    uint32_t mip[4];
};

/// Significant wave height (m) for a Douglas sea state (mid-range of each band).
double significantWaveHeight(double seaState) {
    static const double hs[] = {0.0, 0.1, 0.3, 0.875, 1.875, 3.25, 5.0, 7.5, 11.5, 14.0};
    double s = clamp(seaState, 0.0, 9.0);
    int i = std::min(8, int(s));
    return lerp(hs[i], hs[i + 1], s - i);
}

}  // namespace

bool initOcean(Renderer::Impl& P, GpuDevice& dev) {
    auto arrayTex = [&](GpuResource& r, DXGI_FORMAT fmt, uint32_t mips, bool uav, const char* name) {
        D3D12_RESOURCE_FLAGS flags = uav ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
        if (!dev.createTexture(r, fmt, kN, kN, mips, flags, uav ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS : D3D12_RESOURCE_STATE_COPY_DEST, nullptr, 1,
                               kCascades, name))
            return false;
        dev.makeSrv(r);
        if (uav) dev.makeUav(r);
        return true;
    };
    const uint32_t normalMips = 9;  // 256 .. 1
    if (!arrayTex(P.h0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, false, "oceanH0") ||
        !arrayTex(P.specA, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, true, "oceanSpecA") ||
        !arrayTex(P.specB, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, true, "oceanSpecB") ||
        !arrayTex(P.tmpA, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, true, "oceanTmpA") ||
        !arrayTex(P.tmpB, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, true, "oceanTmpB") ||
        !arrayTex(P.disp, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, true, "oceanDisp"))
        return false;
    for (int i = 0; i < 2; ++i) {
        if (!arrayTex(P.normal[i], DXGI_FORMAT_R16G16B16A16_FLOAT, normalMips, true, "oceanNormal")) return false;
        P.normalMipSrv[i].clear();
        for (uint32_t m = 0; m < normalMips; ++m) P.normalMipSrv[i].push_back(dev.makeMipSrv(P.normal[i], m));
    }

    // Radial grid around the camera: dense near the eye, exponentially coarser towards the horizon.
    const int segments = 256, rings = 300;
    const double r0 = 0.6, rMax = 60000.0;
    const double growth = std::pow(rMax / r0, 1.0 / (rings - 1));
    std::vector<float> verts;
    verts.reserve(size_t(segments) * rings * 2 + 2);
    verts.push_back(0.0f);
    verts.push_back(0.0f);
    for (int r = 0; r < rings; ++r) {
        double radius = r0 * std::pow(growth, r);
        for (int s = 0; s < segments; ++s) {
            double a = 2.0 * kPi * s / segments;
            verts.push_back(float(std::cos(a) * radius));
            verts.push_back(float(std::sin(a) * radius));
        }
    }
    std::vector<uint32_t> idx;
    idx.reserve(size_t(segments) * rings * 6);
    for (int s = 0; s < segments; ++s) idx.insert(idx.end(), {0u, uint32_t(1 + (s + 1) % segments), uint32_t(1 + s)});
    for (int r = 0; r + 1 < rings; ++r)
        for (int s = 0; s < segments; ++s) {
            uint32_t a = 1 + r * segments + s, b = 1 + r * segments + (s + 1) % segments;
            uint32_t c = a + segments, d = b + segments;
            idx.insert(idx.end(), {a, b, d, a, d, c});
        }
    if (!dev.createBuffer(P.oceanVB, verts.size() * 4, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "oceanVB") ||
        !dev.uploadBuffer(P.oceanVB, verts.data(), verts.size() * 4, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) ||
        !dev.createBuffer(P.oceanIB, idx.size() * 4, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "oceanIB") ||
        !dev.uploadBuffer(P.oceanIB, idx.data(), idx.size() * 4, D3D12_RESOURCE_STATE_INDEX_BUFFER))
        return false;
    P.oceanVB.width = uint32_t(verts.size() * 4);
    P.oceanIndices = uint32_t(idx.size());
    return true;
}

void buildOceanSpectrum(Renderer::Impl& P, GpuDevice& dev, double seaState, Vec2 windDir, double windSpeed) {
    const double g = 9.81;
    double hs = std::max(0.05, significantWaveHeight(seaState));
    // Fully developed sea (Pierson-Moskowitz): Hs = 0.21 U^2 / g, peak frequency wp = 0.877 g / U.
    double U = std::max(std::sqrt(hs * g / 0.21), 1.0);
    double wp = 0.877 * g / U;
    const double alpha = 0.0081;
    double wdx = windDir.x, wdy = windDir.y;
    double wl = std::sqrt(wdx * wdx + wdy * wdy);
    if (wl < 1e-6) wdx = 0, wdy = 1, wl = 1;
    wdx /= wl;
    wdy /= wl;
    (void)windSpeed;
    Rng rng(0x5eed0ce4u + uint64_t(seaState * 100.0));
    std::vector<float> data(size_t(kN) * kN * 4 * kCascades);
    std::vector<float> amp(size_t(kN) * kN * 2);
    for (uint32_t c = 0; c < kCascades; ++c) {
        double L = P.cascadeL[c];
        double dk = 2.0 * kPi / L;
        // Band limits so overlapping cascades do not count the same wavelengths twice.
        double kLow = c == 0 ? 0.0 : 2.0 * kPi / P.cascadeL[c] * 4.0;
        double kHigh = c + 1 < kCascades ? 2.0 * kPi / P.cascadeL[c + 1] * 4.0 : 1e9;
        // Complex Gaussian amplitudes h0(k).
        for (uint32_t y = 0; y < kN; ++y)
            for (uint32_t x = 0; x < kN; ++x) {
                double kx = (int(x) - int(kN / 2)) * dk, ky = (int(y) - int(kN / 2)) * dk;
                double k = std::sqrt(kx * kx + ky * ky);
                double a = 0.0;
                if (k > 1e-6 && k >= kLow && k < kHigh) {
                    double w = std::sqrt(g * k);
                    double Sw = alpha * g * g / std::pow(w, 5.0) * std::exp(-1.25 * std::pow(wp / w, 4.0));
                    double Sk = Sw * g / (2.0 * w);  // dw/dk = g / 2w
                    double cosT = (kx * wdx + ky * wdy) / k;
                    // cos^2 spreading (normalised over the half plane) plus a little swell from behind.
                    double D = (cosT > 0.0 ? cosT * cosT : 0.08 * cosT * cosT) * (2.0 / kPi);
                    double S2 = Sk * D / k;               // 2-D wavenumber spectrum
                    S2 *= std::exp(-k * k * 0.0004);     // capillary cut-off
                    a = 0.5 * std::sqrt(S2) * dk;        // E|h0|^2 = S dk^2 / 2
                }
                size_t i = (size_t(y) * kN + x) * 2;
                amp[i] = float(a * rng.normal());
                amp[i + 1] = float(a * rng.normal());
            }
        // Pack h0(k) and conj(h0(-k)).
        for (uint32_t y = 0; y < kN; ++y)
            for (uint32_t x = 0; x < kN; ++x) {
                uint32_t mx = (kN - x) % kN, my = (kN - y) % kN;
                size_t i = (size_t(y) * kN + x) * 2, m = (size_t(my) * kN + mx) * 2;
                float* o = &data[((size_t(c) * kN + y) * kN + x) * 4];
                o[0] = amp[i];
                o[1] = amp[i + 1];
                o[2] = amp[m];
                o[3] = -amp[m + 1];
            }
    }
    dev.flush();
    dev.uploadTexture(P.h0, data.data(), 16, kSrvState, kCascades);
    // Steep seas break: more foam and choppier crests.
    P.choppiness = float(clamp(0.9 + seaState * 0.12, 0.9, 1.6));
    IT_LOG_INFO("Ocean spectrum: sea state %.1f, Hs %.2f m, peak period %.1f s", seaState, hs, 2.0 * kPi / wp);
}

void dispatchOcean(Renderer::Impl& P, GpuDevice& dev, double time) {
    auto* cmd = dev.cmd();
    OceanPass op{};
    op.cascadeL = {P.cascadeL[0], P.cascadeL[1], P.cascadeL[2], 0.0f};
    // Foam: decay per frame of the previous value, bias = Jacobian threshold below which crests break.
    op.time = {float(std::fmod(time, 100000.0)), P.choppiness, 0.94f, 0.55f + float(P.oceanSeaState) * 0.04f};
    const int cur = 1 - P.normalCur, prev = P.normalCur;
    auto bindTables = [&](std::initializer_list<uint32_t> srvs, std::initializer_list<uint32_t> uavs) {
        TableBuilder s(dev, 8), u(dev, 4);
        uint32_t i = 0;
        for (uint32_t v : srvs) s.srv(i++, v);
        for (; i < 8; ++i) s.srv(i, P.nullTexArray);
        i = 0;
        uint32_t last = UINT32_MAX;
        for (uint32_t v : uavs) u.srv(i++, last = v);
        for (; i < 4; ++i) u.srv(i, last);
        cmd->SetComputeRootDescriptorTable(4, s.handle());
        cmd->SetComputeRootDescriptorTable(5, u.handle());
    };
    cmd->SetComputeRootConstantBufferView(2, dev.constants(op));
    // 1. Spectrum at time t.
    dev.transition(P.h0, kSrvState);
    dev.transition(P.specA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dev.transition(P.specB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(P.pso.oceanSpectrum.get());
    bindTables({P.h0.srv}, {P.specA.uav[0], P.specB.uav[0]});
    cmd->Dispatch(kN / 8, kN / 8, kCascades);
    // 2. Inverse FFT along rows, then columns.
    dev.transition(P.specA, kSrvState);
    dev.transition(P.specB, kSrvState);
    dev.transition(P.tmpA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dev.transition(P.tmpB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(P.pso.oceanRows.get());
    bindTables({P.nullTexArray, P.specA.srv, P.specB.srv}, {P.tmpA.uav[0], P.tmpB.uav[0]});
    cmd->Dispatch(kN, kCascades, 1);
    dev.transition(P.tmpA, kSrvState);
    dev.transition(P.tmpB, kSrvState);
    dev.transition(P.specA, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dev.transition(P.specB, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    cmd->SetPipelineState(P.pso.oceanCols.get());
    bindTables({P.nullTexArray, P.tmpA.srv, P.tmpB.srv}, {P.specA.uav[0], P.specB.uav[0]});
    cmd->Dispatch(kN, kCascades, 1);
    // 3. Displacement, slopes, foam.
    dev.transition(P.specA, kSrvState);
    dev.transition(P.specB, kSrvState);
    dev.transition(P.disp, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dev.transition(P.normal[cur], D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    dev.transition(P.normal[prev], kSrvState);
    cmd->SetPipelineState(P.pso.oceanFinalize.get());
    bindTables({P.nullTexArray, P.specA.srv, P.specB.srv, P.normal[prev].srv}, {P.disp.uav[0], P.normal[cur].uav[0]});
    cmd->Dispatch(kN / 8, kN / 8, kCascades);
    // 4. Mip chain of slopes/foam (per-subresource transitions).
    GpuResource& n = P.normal[cur];
    cmd->SetPipelineState(P.pso.oceanDownsample.get());
    auto mipBarrier = [&](uint32_t mip, D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to) {
        D3D12_RESOURCE_BARRIER b[kCascades];
        for (uint32_t s = 0; s < kCascades; ++s) b[s] = transition(n.get(), from, to, mip + s * n.mips);
        cmd->ResourceBarrier(kCascades, b);
    };
    for (uint32_t m = 1; m < n.mips; ++m) {
        mipBarrier(m - 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kSrvState);
        bindTables({P.nullTexArray, P.normalMipSrv[cur][m - 1]}, {n.uav[m]});
        uint32_t size = std::max(1u, kN >> m);
        cmd->Dispatch((size + 7) / 8, (size + 7) / 8, kCascades);
    }
    mipBarrier(n.mips - 1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, kSrvState);
    n.state = kSrvState;
    dev.transition(P.disp, kSrvState);
    P.normalCur = cur;
}

void drawOcean(Renderer::Impl& P, GpuDevice& dev) {
    auto* cmd = dev.cmd();
    D3D12_VERTEX_BUFFER_VIEW vb{P.oceanVB.get()->GetGPUVirtualAddress(), P.oceanVB.width, 8};
    D3D12_INDEX_BUFFER_VIEW ib{P.oceanIB.get()->GetGPUVirtualAddress(), P.oceanIndices * 4, DXGI_FORMAT_R32_UINT};
    cmd->IASetVertexBuffers(0, 1, &vb);
    cmd->IASetIndexBuffer(&ib);
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->DrawIndexedInstanced(P.oceanIndices, 1, 0, 0, 0);
}

}  // namespace it::render
