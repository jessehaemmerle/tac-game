// Scene geometry: islands (height field mesh, depth texture, chart), ship meshes and their articulation.
#include <cstring>

#include "core/FileSystem.h"
#include "core/Image.h"
#include "core/Log.h"
#include "procgen/GltfImport.h"
#include "render/RendererImpl.h"

namespace it::render {

namespace {

Mat4f rotZ(float deg) { return Mat4f::rotation(Quatf::axisAngle({0, 0, 1}, float(deg * kDegToRad))); }
Mat4f rotY(float deg) { return Mat4f::rotation(Quatf::axisAngle({0, 1, 0}, float(deg * kDegToRad))); }
Mat4f rotX(float deg) { return Mat4f::rotation(Quatf::axisAngle({1, 0, 0}, float(deg * kDegToRad))); }
Mat4f aboutPivot(const Vec3f& pivot, const Mat4f& r) { return Mat4f::translation(pivot) * r * Mat4f::translation(-pivot); }

uint8_t toByte(double v) { return uint8_t(clamp(v, 0.0, 1.0) * 255.0 + 0.5); }

/// Sphere vs. view frustum (camera-relative centre) using the rows of the view-projection matrix.
bool sphereVisible(const Mat4f& vp, const Vec3f& c, float r) {
    auto row = [&](int i) { return Vec4f{vp.m[0][i], vp.m[1][i], vp.m[2][i], vp.m[3][i]}; };
    Vec4f r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);
    Vec4f planes[5] = {{r3.x + r0.x, r3.y + r0.y, r3.z + r0.z, r3.w + r0.w}, {r3.x - r0.x, r3.y - r0.y, r3.z - r0.z, r3.w - r0.w},
                       {r3.x + r1.x, r3.y + r1.y, r3.z + r1.z, r3.w + r1.w}, {r3.x - r1.x, r3.y - r1.y, r3.z - r1.z, r3.w - r1.w},
                       {r3.x - r2.x, r3.y - r2.y, r3.z - r2.z, r3.w - r2.w}};
    for (auto& p : planes) {
        float len = std::sqrt(p.x * p.x + p.y * p.y + p.z * p.z);
        if (len < 1e-8f) continue;
        if ((p.x * c.x + p.y * c.y + p.z * c.z + p.w) / len < -r) return false;
    }
    return true;
}

}  // namespace

Mat4f modelToRenderBasis(double headingDeg) {
    float s = float(std::sin(headingDeg * kDegToRad)), c = float(std::cos(headingDeg * kDegToRad));
    Mat4f m;
    m.m[0][0] = s, m.m[0][1] = 0, m.m[0][2] = c, m.m[0][3] = 0;   // model x (forward)
    m.m[1][0] = c, m.m[1][1] = 0, m.m[1][2] = -s, m.m[1][3] = 0;  // model y (starboard)
    m.m[2][0] = 0, m.m[2][1] = 1, m.m[2][2] = 0, m.m[2][3] = 0;   // model z (up)
    return m;
}

// ------------------------------------------------------------------------------------------- terrain
void buildTerrain(Renderer::Impl& P, GpuDevice& dev, const sim::Terrain& t, GpuResource& chart) {
    const auto& info = t.info();
    const int W = info.width, H = info.height;
    const double cell = info.cellSize;
    const auto& hs = t.samples();
    Vec2 mn = t.minCorner(), mx = t.maxCorner();
    auto hAt = [&](int x, int y) {
        x = clamp(x, 0, W - 1);
        y = clamp(y, 0, H - 1);
        return double(hs[size_t(y) * W + x]);
    };
    // Height-field mesh of everything that can show above the (opaque) sea surface.
    std::vector<int32_t> remap(size_t(W) * H, -1);
    std::vector<float> verts;
    std::vector<uint32_t> idx;
    auto vertex = [&](int x, int y) -> uint32_t {
        int32_t& r = remap[size_t(y) * W + x];
        if (r >= 0) return uint32_t(r);
        double h = hAt(x, y);
        double nx = (hAt(x - 1, y) - hAt(x + 1, y)) / (2.0 * cell);
        double nz = (hAt(x, y + 1) - hAt(x, y - 1)) / (2.0 * cell);  // rows run north -> south
        Vec3 n = normalize(Vec3{nx, 1.0, nz});
        r = int32_t(verts.size() / 6);
        verts.insert(verts.end(), {float(mn.x + x * cell), float(h), float(mx.y - y * cell), float(n.x), float(n.y), float(n.z)});
        return uint32_t(r);
    };
    for (int y = 0; y + 1 < H; ++y)
        for (int x = 0; x + 1 < W; ++x) {
            double top = std::max({hAt(x, y), hAt(x + 1, y), hAt(x, y + 1), hAt(x + 1, y + 1)});
            if (top < -12.0) continue;
            uint32_t a = vertex(x, y), b = vertex(x + 1, y), c = vertex(x, y + 1), d = vertex(x + 1, y + 1);
            // Clockwise seen from above (render space: x east, z north; row y+1 is further south).
            idx.insert(idx.end(), {a, b, c, b, d, c});
        }
    P.hasTerrain = false;
    if (!idx.empty()) {
        if (dev.createBuffer(P.terrainVB, verts.size() * 4, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "terrainVB") &&
            dev.uploadBuffer(P.terrainVB, verts.data(), verts.size() * 4, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) &&
            dev.createBuffer(P.terrainIB, idx.size() * 4, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, "terrainIB") &&
            dev.uploadBuffer(P.terrainIB, idx.data(), idx.size() * 4, D3D12_RESOURCE_STATE_INDEX_BUFFER)) {
            P.terrainVB.width = uint32_t(verts.size() * 4);
            P.terrainIndices = uint32_t(idx.size());
        }
    }
    // Height texture for water depth (shallows, surf, wave damping). Row 0 = north edge.
    if (dev.createTexture(P.terrainHeight, DXGI_FORMAT_R32_FLOAT, W, H, 1, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, 1, 1,
                          "terrainHeight")) {
        dev.uploadTexture(P.terrainHeight, hs.data(), 4, kSrvState);
        dev.makeSrv(P.terrainHeight);
        P.hasTerrain = true;
    }
    P.terrainRect = {float(mn.x - cell * 0.5), float(mn.y - cell * 0.5), float(W * cell), float(H * cell)};

    // Chart texture: bathymetric tints, hypsometric land colours and hill shading (sRGB values for the UI).
    std::vector<uint8_t> rgba(size_t(W) * H * 4);
    const Vec3 light = normalize(Vec3{-1.0, 1.4, 1.0});
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double h = hAt(x, y);
            double r, g, b;
            if (h <= 0.0) {
                double d = std::log10(1.0 + (-h)) / 3.3;  // 0 .. ~1 at 2000 m
                d = clamp(d, 0.0, 1.0);
                r = lerp(0.26, 0.055, d);
                g = lerp(0.40, 0.11, d);
                b = lerp(0.47, 0.19, d);
                if (h > -20.0) {  // reef / shoal band
                    double s = (h + 20.0) / 20.0;
                    r = lerp(r, 0.36, s * 0.6);
                    g = lerp(g, 0.50, s * 0.6);
                    b = lerp(b, 0.50, s * 0.6);
                }
            } else {
                double e = clamp(h / 1600.0, 0.0, 1.0);
                r = lerp(0.36, 0.55, e);
                g = lerp(0.42, 0.47, e);
                b = lerp(0.27, 0.38, e);
                double nx = (hAt(x - 1, y) - hAt(x + 1, y)) / (2.0 * cell) * 2.5;
                double nz = (hAt(x, y + 1) - hAt(x, y - 1)) / (2.0 * cell) * 2.5;
                double shade = clamp(dot(normalize(Vec3{nx, 1.0, nz}), light) * 1.15, 0.35, 1.25);
                r *= shade;
                g *= shade;
                b *= shade;
            }
            uint8_t* o = &rgba[(size_t(y) * W + x) * 4];
            o[0] = toByte(r), o[1] = toByte(g), o[2] = toByte(b), o[3] = 255;
        }
    if (dev.createTexture(chart, DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 1, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, 1, 1,
                          "chart")) {
        dev.uploadTexture(chart, rgba.data(), 4, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        dev.makeSrv(chart);
        dev.makeUiSrv(chart);
    }
    IT_LOG_INFO("Terrain: %d x %d samples, %u land triangles", W, H, unsigned(idx.size() / 3));
}

void drawTerrain(Renderer::Impl& P, GpuDevice& dev) {
    if (!P.terrainIndices) return;
    auto* cmd = dev.cmd();
    D3D12_VERTEX_BUFFER_VIEW vb{P.terrainVB.get()->GetGPUVirtualAddress(), P.terrainVB.width, 24};
    D3D12_INDEX_BUFFER_VIEW ib{P.terrainIB.get()->GetGPUVirtualAddress(), P.terrainIndices * 4, DXGI_FORMAT_R32_UINT};
    cmd->IASetVertexBuffers(0, 1, &vb);
    cmd->IASetIndexBuffer(&ib);
    cmd->DrawIndexedInstanced(P.terrainIndices, 1, 0, 0, 0);
}

// ------------------------------------------------------------------------------------ ship textures
namespace {
bool loadArray(GpuDevice& dev, const std::string& file, bool srgb, GpuResource& out) {
    ImageRGBA8 img;
    if (!loadImage(fs::dataPath("textures/" + file), img) || img.width == 0 || img.height % img.width != 0) {
        IT_LOG_WARN("Ship texture %s missing or not a stack of square layers", file.c_str());
        return false;
    }
    const uint32_t n = img.width, layers = img.height / n;
    uint32_t mips = 1;
    while ((n >> mips) >= 4) ++mips;
    std::vector<ImageRGBA8> subs;  // slice-major: slice * mips + mip
    for (uint32_t l = 0; l < layers; ++l) {
        subs.push_back(crop(img, l * n, n));
        for (uint32_t m = 1; m < mips; ++m) subs.push_back(downsample(subs.back(), srgb));
    }
    DXGI_FORMAT fmt = srgb ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
    if (!dev.createTexture(out, fmt, n, n, mips, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, 1, layers, file.c_str()))
        return false;
    std::vector<const uint8_t*> data;
    for (auto& s : subs) data.push_back(s.pixels.data());
    if (!dev.uploadSubresources(out, data, 4, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)) return false;
    dev.makeSrv(out);
    return true;
}
}  // namespace

bool loadShipTextures(Renderer::Impl& P, GpuDevice& dev) {
    bool ok = loadArray(dev, "ship_albedo.png", true, P.shipAlbedo) && loadArray(dev, "ship_surface.png", false, P.shipSurface);
    if (ok) IT_LOG_INFO("Ship textures: %u layers of %u px", P.shipAlbedo.depth, P.shipAlbedo.width);
    return ok;
}

void bindShipTextures(Renderer::Impl& P, GpuDevice& dev) {
    TableBuilder t(dev, 8);
    for (uint32_t i = 0; i < 8; ++i) t.srv(i, P.nullTexArray);
    if (P.shipAlbedo.srv != UINT32_MAX && P.shipSurface.srv != UINT32_MAX) {
        t.srv(0, P.shipAlbedo.srv);
        t.srv(1, P.shipSurface.srv);
    }
    dev.cmd()->SetGraphicsRootDescriptorTable(4, t.handle());
}

// --------------------------------------------------------------------------------------------- ships
const GpuMesh* shipMeshFor(Renderer::Impl& P, GpuDevice& dev, const sim::ShipClassDef& c) {
    auto it = P.meshes.find(c.id);
    if (it != P.meshes.end()) return it->second.get();
    procgen::Model model;
    bool imported = false;
    if (!c.model.gltf.empty()) {
        std::string err;
        imported = procgen::importGltf(fs::dataPath("models/" + c.model.gltf).string(), c, c.model.gltfScale, model, &err);
        if (!imported) IT_LOG_WARN("glTF model for %s not used: %s", c.id.c_str(), err.c_str());
    }
    if (!imported) model = procgen::buildShipModel(c);
    auto mesh = std::make_unique<GpuMesh>();
    std::vector<procgen::Vertex> verts;
    std::vector<uint32_t> idx;
    for (const auto& part : model.parts) {
        GpuMesh::Part gp;
        gp.firstIndex = uint32_t(idx.size());
        gp.indexCount = uint32_t(part.indices.size());
        gp.kind = part.kind;
        gp.mount = part.mount;
        gp.parent = part.parent;
        gp.sub = part.sub;
        gp.lod = part.lod;
        gp.pivot = part.pivot;
        gp.axis = part.axis;
        uint32_t base = uint32_t(verts.size());
        verts.insert(verts.end(), part.vertices.begin(), part.vertices.end());
        for (uint32_t i : part.indices) idx.push_back(base + i);
        mesh->parts.push_back(gp);
    }
    model.computeBounds();
    mesh->bounds = model.bounds;
    mesh->aa = model.aa;
    if (verts.empty() || idx.empty()) return nullptr;
    size_t vb = verts.size() * sizeof(procgen::Vertex), ib = idx.size() * 4;
    if (!dev.createBuffer(mesh->vb, vb, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, c.id.c_str()) ||
        !dev.uploadBuffer(mesh->vb, verts.data(), vb, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER) ||
        !dev.createBuffer(mesh->ib, ib, D3D12_RESOURCE_FLAG_NONE, D3D12_RESOURCE_STATE_COPY_DEST, c.id.c_str()) ||
        !dev.uploadBuffer(mesh->ib, idx.data(), ib, D3D12_RESOURCE_STATE_INDEX_BUFFER))
        return nullptr;
    mesh->vbv = {mesh->vb.get()->GetGPUVirtualAddress(), UINT(vb), UINT(sizeof(procgen::Vertex))};
    mesh->ibv = {mesh->ib.get()->GetGPUVirtualAddress(), UINT(ib), DXGI_FORMAT_R32_UINT};
    IT_LOG_INFO("Ship mesh %s: %zu triangles, %zu parts%s", c.id.c_str(), idx.size() / 3, mesh->parts.size(), imported ? " (glTF)" : "");
    const GpuMesh* out = mesh.get();
    P.meshes[c.id] = std::move(mesh);
    return out;
}

ShipPose stowedPose(const sim::ShipClassDef& c) {
    ShipPose p;
    for (const auto& m : c.mounts) {
        float t = m.arcMin > 0 ? 180.0f : 0.0f;
        if (m.arcMin > 0 && m.arcMax < 180) t = 90.0f;
        if (m.arcMax < 0) t = -90.0f;
        p.train.push_back(t);
        p.elev.push_back(m.gun && m.gun->caliberMm < 140 ? 5.0f : 2.0f);
    }
    for (const auto& t : c.torpedoMounts) p.torpTrain.push_back(t.mirrored ? 0.0f : float(0.5 * (t.arcMin + t.arcMax)));
    return p;
}

ShipPose shipPose(const sim::Ship& s, const sim::World& w, double visualTime) {
    ShipPose p;
    for (const auto& m : s.mounts) {
        p.train.push_back(float(m.train));
        p.elev.push_back(float(m.elevation));
    }
    for (const auto& t : s.torpedoMounts) p.torpTrain.push_back(float(t.train));
    p.searchlightOn = s.operational() && s.orders.searchlight && s.dmg.searchlights >= 0.3;
    if (p.searchlightOn) {
        if (const sim::Ship* t = w.ship(s.searchlightTarget)) p.searchlightTrain = float(s.relativeBearing(t->pos));
        else p.searchlightTrain = float(std::sin(visualTime * 0.25 + s.id) * 35.0);
    }
    if (!s.directors.empty() && s.directors[0].tracking) p.directorTrain = float(s.relativeBearing(s.directors[0].estPos));
    bool radarOn = false;
    for (const auto& sn : s.sensors)
        if (sn.def && sn.def->def && sn.def->def->kind == sim::SensorKind::Radar && sn.working(w.time())) radarOn = true;
    p.radarAngle = radarOn ? float(std::fmod(visualTime * 36.0 + s.id * 37.0, 360.0)) : 0.0f;
    return p;
}

Mat4f shipWorldMatrix(const sim::Ship& s, const Vec3& camPos, double t, double seaState) {
    const sim::ShipClassDef& c = *s.cls;
    double hsig = std::max(0.05, 0.12 * seaState * seaState + 0.1 * seaState);
    double phase = double(s.id) * 1.7;
    double rollAmp = hsig * 2.2 * clamp(12.0 / c.beam, 0.5, 2.0);
    double pitchAmp = hsig * 0.9 * clamp(120.0 / c.length, 0.4, 2.5);
    double heaveAmp = hsig * 0.22 * clamp(100.0 / c.length, 0.3, 1.5);
    double tRoll = 6.0 + c.beam * 0.3, tPitch = 4.5 + c.length / 45.0;
    double roll = s.heel + s.dmg.list + rollAmp * std::sin(kTwoPi * t / tRoll + phase);
    double pitch = s.dmg.trim + pitchAmp * std::sin(kTwoPi * t / tPitch + phase * 0.7);
    double heave = heaveAmp * std::sin(kTwoPi * t / (tPitch * 1.1) + phase * 1.3);
    if (s.dmg.sinking || s.dmg.sunk) {
        double p = clamp(s.dmg.sinkProgress, 0.0, 1.0);
        double sign = s.dmg.trim >= 0 ? 1.0 : -1.0;
        if (s.dmg.capsized) roll += (s.dmg.list >= 0 ? 1.0 : -1.0) * std::min(150.0, p * 260.0);
        else roll += (s.dmg.list >= 0 ? 1.0 : -1.0) * p * 18.0;
        pitch += sign * p * (s.dmg.brokenBack ? 8.0 : 22.0);
        heave -= std::pow(p, 1.5) * (c.mastHeight + c.draft + 6.0);
    }
    Vec3 rel = Vec3{s.pos.x, heave, s.pos.y} - camPos;
    return Mat4f::translation({float(rel.x), float(rel.y), float(rel.z)}) * modelToRenderBasis(s.heading) * rotY(float(pitch)) *
           rotX(float(-roll));
}

void drawShipModel(Renderer::Impl& P, GpuDevice& dev, const GpuMesh& mesh, const Mat4f& world, const Vec4f& tint, const Vec4f& params,
                   const ShipPose& pose, int maxLod) {
    auto* cmd = dev.cmd();
    cmd->IASetVertexBuffers(0, 1, &mesh.vbv);
    cmd->IASetIndexBuffer(&mesh.ibv);
    std::vector<Mat4f> local(mesh.parts.size());
    std::vector<uint8_t> hidden(mesh.parts.size(), 0);
    auto angle = [](const std::vector<float>& v, int i) { return (i >= 0 && size_t(i) < v.size()) ? v[size_t(i)] : 0.0f; };
    auto nested = [](const std::vector<std::vector<float>>& v, int i, int j, float fallback) {
        if (i < 0 || size_t(i) >= v.size() || j < 0 || size_t(j) >= v[size_t(i)].size()) return fallback;
        return v[size_t(i)][size_t(j)];
    };
    for (size_t i = 0; i < mesh.parts.size(); ++i) {
        const auto& part = mesh.parts[i];
        Mat4f m;
        switch (part.kind) {
            case procgen::PartKind::Turret: m = aboutPivot(part.pivot, rotZ(angle(pose.train, part.mount))); break;
            case procgen::PartKind::Barrel: m = aboutPivot(part.pivot, rotY(-angle(pose.elev, part.mount))); break;
            case procgen::PartKind::Gun: m = Mat4f::translation(part.axis * -nested(pose.recoil, part.mount, part.sub, 0.0f)); break;
            case procgen::PartKind::TorpedoMount: m = aboutPivot(part.pivot, rotZ(angle(pose.torpTrain, part.mount))); break;
            case procgen::PartKind::Torpedo: {
                float out = nested(pose.torpedoOut, part.mount, part.sub, 0.0f);
                hidden[i] = out < 0.0f || out > 40.0f;
                m = Mat4f::translation(part.axis * std::max(0.0f, out));
                break;
            }
            case procgen::PartKind::Searchlight: m = aboutPivot(part.pivot, rotZ(pose.searchlightTrain)); break;
            case procgen::PartKind::SearchlightLamp: m = aboutPivot(part.pivot, rotY(-pose.searchlightElev)); break;
            case procgen::PartKind::Director: m = aboutPivot(part.pivot, rotZ(pose.directorTrain)); break;
            case procgen::PartKind::Radar: m = aboutPivot(part.pivot, rotZ(pose.radarAngle)); break;
            case procgen::PartKind::AAMount: {
                float base = part.mount >= 0 && size_t(part.mount) < mesh.aa.size() ? mesh.aa[size_t(part.mount)].baseTrain : 0.0f;
                float t = size_t(part.mount) < pose.aaTrain.size() ? pose.aaTrain[size_t(part.mount)] : base;
                m = aboutPivot(part.pivot, rotZ(t));
                break;
            }
            case procgen::PartKind::AAGuns: m = aboutPivot(part.pivot, rotY(-angle(pose.aaElev, part.mount))); break;
            case procgen::PartKind::Catapult: m = aboutPivot(part.pivot, rotZ(part.pivot.y >= 0 ? pose.catapultTrain : -pose.catapultTrain)); break;
            case procgen::PartKind::Aircraft:
                if (pose.aircraftLaunch >= 0.0f) {
                    hidden[i] = pose.aircraftLaunch > 60.0f;
                    m = Mat4f::translation(Vec3f{std::min(pose.aircraftLaunch, 12.0f), 0, 0} + pose.aircraftFlight);
                }
                break;
            case procgen::PartKind::Propeller: m = aboutPivot(part.pivot, rotX(part.sub == 1 ? -pose.propAngle : pose.propAngle)); break;
            case procgen::PartKind::Rudder: m = aboutPivot(part.pivot, rotZ(pose.rudder)); break;
            default: break;
        }
        if (part.parent >= 0 && size_t(part.parent) < i) {
            m = local[size_t(part.parent)] * m;
            hidden[i] |= hidden[size_t(part.parent)];
        }
        local[i] = m;
        if (!part.indexCount || hidden[i] || part.lod > maxLod) continue;
        ObjectConstants oc{world * m, tint, params};
        bool lamp = part.kind == procgen::PartKind::Searchlight || part.kind == procgen::PartKind::SearchlightLamp;
        oc.tint.w = lamp && pose.searchlightOn ? 1.0f : 0.0f;
        cmd->SetGraphicsRootConstantBufferView(1, dev.constants(oc));
        cmd->DrawIndexedInstanced(part.indexCount, 1, part.firstIndex, 0, 0);
    }
}

void drawShips(Renderer::Impl& P, GpuDevice& dev, const sim::World& w, const Camera& cam, bool shadowPass, sim::EntityId selected,
               double visualTime) {
    double seaState = w.env().weather().seaState;
    if (!shadowPass) bindShipTextures(P, dev);
    for (const auto& sp : w.ships()) {
        const sim::Ship& s = *sp;
        if (!s.cls || (s.dmg.sunk && s.dmg.sinkProgress >= 1.0)) continue;
        Vec3 rel = Vec3{s.pos.x, 0.0, s.pos.y} - cam.pos;
        float radius = float(s.cls->length * 0.6 + s.cls->mastHeight);
        Vec3f c{float(rel.x), float(rel.y + s.cls->mastHeight * 0.3), float(rel.z)};
        double dist = length(rel);
        if (shadowPass) {
            if (dist > 3500.0) continue;
        } else if (!sphereVisible(P.fc.viewProj, c, radius)) {
            continue;
        }
        const GpuMesh* mesh = shipMeshFor(P, dev, *s.cls);
        if (!mesh) continue;
        Mat4f world = shipWorldMatrix(s, cam.pos, visualTime, seaState);
        double burn = clamp(s.dmg.hitsTaken * 0.035 + s.dmg.totalFireIntensity() * 0.5 + (1.0 - s.dmg.hullIntegrity) * 0.6, 0.0, 1.0);
        Vec4f tint{float(s.dmg.sinkProgress), s.id == selected ? 1.0f : 0.0f, float(burn), 0.0f};
        Vec4f params{float(s.id % 97), float(-cam.pos.y), float(s.cls->length), float(s.cls->model.freeboardMid)};
        // Level of detail from the projected size of a metre (pixels).
        double pixPerMetre = double(P.fc.screen.y) / (2.0 * std::tan(cam.fovY * 0.5 * kDegToRad) * std::max(1.0, dist));
        int lod = pixPerMetre > 4.0 ? 2 : (pixPerMetre > 0.8 ? 1 : 0);
        if (shadowPass) lod = std::min(lod, dist < 400.0 ? 1 : 0);
        drawShipModel(P, dev, *mesh, world, tint, params, shipPose(s, w, visualTime), lod);
    }
}

}  // namespace it::render
