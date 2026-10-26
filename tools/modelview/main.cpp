// Software preview of procedural ship models (development tool): renders a model with a z-buffer
// rasteriser and writes a PPM. Usage: irontide_modelview <class_id> <out.ppm> [yaw] [pitch] [width]
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core/FileSystem.h"
#include "core/Log.h"
#include "procgen/ShipModel.h"
#include "sim/Database.h"

using namespace it;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <class_id|all> <out.ppm> [yaw] [pitch] [width]\n", argv[0]);
        return 1;
    }
    fs::locateDataRoot(std::filesystem::absolute(argv[0]).parent_path());
    setLogLevel(LogLevel::Warn);
    sim::Database db;
    if (!db.loadFromDirectory(fs::dataPath("db").string())) return 1;
    std::vector<const sim::ShipClassDef*> classes;
    if (std::string(argv[1]) == "all")
        for (auto& c : db.shipClasses()) classes.push_back(c.get());
    else if (auto* c = db.shipClass(argv[1]))
        classes.push_back(c);
    if (classes.empty()) return 1;
    float yaw = argc > 3 ? float(std::atof(argv[3])) : 35.0f, pitch = argc > 4 ? float(std::atof(argv[4])) : 12.0f;
    int W = argc > 5 ? std::atoi(argv[5]) : 1200;
    int rowH = int(W * 0.28);
    int H = rowH * int(classes.size());
    std::vector<float> img(size_t(W) * H * 3), zb(size_t(W) * H);
    for (size_t i = 0; i < img.size() / 3; ++i) {
        float t = float(i / W % rowH) / rowH;
        img[i * 3] = 0.55f - 0.2f * t;
        img[i * 3 + 1] = 0.65f - 0.2f * t;
        img[i * 3 + 2] = 0.78f - 0.2f * t;
    }
    std::fill(zb.begin(), zb.end(), 1e30f);
    float cy = std::cos(yaw * float(kDegToRad)), sy = std::sin(yaw * float(kDegToRad));
    float cp = std::cos(pitch * float(kDegToRad)), sp = std::sin(pitch * float(kDegToRad));
    Vec3f light = normalize(Vec3f{0.4f, -0.5f, 0.75f});
    for (size_t ci = 0; ci < classes.size(); ++ci) {
        procgen::Model m = procgen::buildShipModel(*classes[ci]);
        float scale = W * 0.92f / float(classes[ci]->length * 1.05);
        float ox = W * 0.5f, oy = float(ci * rowH) + rowH * 0.62f;
        auto project = [&](const Vec3f& p, float& sx, float& syy, float& depth) {
            // view: rotate around z by yaw, then tilt by pitch; camera looks along +y
            float x1 = p.x * cy - p.y * sy, y1 = p.x * sy + p.y * cy, z1 = p.z;
            float y2 = y1 * cp - z1 * sp, z2 = y1 * sp + z1 * cp;
            sx = ox + x1 * scale;
            syy = oy - z2 * scale;
            depth = y2;
        };
        for (auto& part : m.parts) {
            // Aft turrets at their stowed training (facing aft) for a natural look.
            float rot = 0;
            if ((part.kind == procgen::PartKind::Turret || part.kind == procgen::PartKind::Barrel) && part.mount >= 0) {
                const auto& md = classes[ci]->mounts[size_t(part.mount)];
                if (md.arcMin > 0) rot = float(kPi);
                else if (md.arcMax < 0) rot = -float(kPi) * 0.5f;
            }
            Vec3f pv = part.kind == procgen::PartKind::Barrel && part.parent >= 0 ? m.parts[size_t(part.parent)].pivot : part.pivot;
            auto tx = [&](Vec3f p) {
                if (rot == 0) return p;
                Vec3f d = p - pv;
                return pv + Vec3f{d.x * std::cos(rot) - d.y * std::sin(rot), d.x * std::sin(rot) + d.y * std::cos(rot), d.z};
            };
            for (size_t k = 0; k + 2 < part.indices.size(); k += 3) {
                const auto& a = part.vertices[part.indices[k]];
                const auto& b = part.vertices[part.indices[k + 1]];
                const auto& c = part.vertices[part.indices[k + 2]];
                Vec3f pa = tx(a.pos), pb = tx(b.pos), pc = tx(c.pos);
                float x0, y0, d0, x1, y1, d1, x2, y2, d2;
                project(pa, x0, y0, d0);
                project(pb, x1, y1, d1);
                project(pc, x2, y2, d2);
                float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
                if (std::fabs(area) < 1e-6f) continue;
                Vec3f n = normalize(cross(pb - pa, pc - pa));
                int minx = std::max(0, int(std::min({x0, x1, x2}))), maxx = std::min(W - 1, int(std::max({x0, x1, x2})) + 1);
                int miny = std::max(int(ci * rowH), int(std::min({y0, y1, y2}))), maxy = std::min(int((ci + 1) * rowH) - 1, int(std::max({y0, y1, y2})) + 1);
                auto col = [](uint32_t c, int sh) { return float((c >> sh) & 255) / 255.0f; };
                float r = col(a.color, 0), g = col(a.color, 8), bl = col(a.color, 16);
                float shade = 0.35f + 0.65f * std::max(0.0f, dot(n, light) * (area > 0 ? -1.0f : 1.0f) * -1.0f);
                shade = 0.35f + 0.65f * std::fabs(dot(n, light));
                for (int py = miny; py <= maxy; ++py)
                    for (int px = minx; px <= maxx; ++px) {
                        float w0 = ((x1 - px) * (y2 - py) - (x2 - px) * (y1 - py)) / area;
                        float w1 = ((x2 - px) * (y0 - py) - (x0 - px) * (y2 - py)) / area;
                        float w2 = 1 - w0 - w1;
                        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                        float d = w0 * d0 + w1 * d1 + w2 * d2;
                        size_t idx = size_t(py) * W + px;
                        if (d >= zb[idx]) continue;
                        zb[idx] = d;
                        float under = (pa.z * w0 + pb.z * w1 + pc.z * w2) < 0 ? 0.55f : 1.0f;
                        img[idx * 3] = std::pow(r * shade, 1 / 2.2f) * under;
                        img[idx * 3 + 1] = std::pow(g * shade, 1 / 2.2f) * under;
                        img[idx * 3 + 2] = std::pow(bl * shade, 1 / 2.2f) * under;
                    }
            }
        }
        std::printf("%-22s %7zu triangles, bounds %.1f x %.1f x %.1f m\n", classes[ci]->id.c_str(), m.triangleCount(),
                    m.bounds.hi.x - m.bounds.lo.x, m.bounds.hi.y - m.bounds.lo.y, m.bounds.hi.z - m.bounds.lo.z);
    }
    FILE* f = std::fopen(argv[2], "wb");
    std::fprintf(f, "P6 %d %d 255\n", W, H);
    for (float v : img) std::fputc(int(clamp(v, 0.0f, 1.0f) * 255), f);
    std::fclose(f);
    return 0;
}
