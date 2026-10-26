#include "procgen/Mesh.h"

namespace it::procgen {

uint32_t Material::packColor() const {
    auto c = [](float v) { return uint32_t(clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return c(albedo.x) | (c(albedo.y) << 8) | (c(albedo.z) << 16) | (255u << 24);
}

uint32_t Material::packParams() const {
    auto c = [](float v) { return uint32_t(clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return c(roughness) | (c(metalness) << 8) | (uint32_t(id) << 16) | (c(emissive) << 24);
}

Aabb MeshPart::bounds() const {
    Aabb b;
    for (auto& v : vertices) b.add(v.pos);
    return b;
}

size_t Model::triangleCount() const {
    size_t n = 0;
    for (auto& p : parts) n += p.indices.size() / 3;
    return n;
}

void Model::computeBounds() {
    bounds = {};
    for (auto& p : parts)
        for (auto& v : p.vertices) bounds.add(v.pos);
}

uint32_t MeshBuilder::vertex(const Vec3f& pos, const Vec3f& n) {
    Vertex v;
    v.pos = pos;
    v.normal = n;
    v.color = mat_.packColor();
    v.params = mat_.packParams();
    p_.vertices.push_back(v);
    return uint32_t(p_.vertices.size() - 1);
}

void MeshBuilder::flatQuad(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d) {
    Vec3f n = normalize(cross(b - a, c - a));
    if (lengthSq(n) < 1e-12f) n = normalize(cross(c - a, d - a));
    uint32_t i0 = vertex(a, n), i1 = vertex(b, n), i2 = vertex(c, n), i3 = vertex(d, n);
    quad(i0, i1, i2, i3);
}

void MeshBuilder::flatTri(const Vec3f& a, const Vec3f& b, const Vec3f& c) {
    Vec3f n = normalize(cross(b - a, c - a));
    uint32_t i0 = vertex(a, n), i1 = vertex(b, n), i2 = vertex(c, n);
    tri(i0, i1, i2);
}

void MeshBuilder::box(const Vec3f& lo, const Vec3f& hi) { taperedBox(lo, hi, 0.0f, 0.0f); }

void MeshBuilder::taperedBox(const Vec3f& lo, const Vec3f& hi, float taperX, float taperY, float frontSlope) {
    float cx = (lo.x + hi.x) * 0.5f, cy = (lo.y + hi.y) * 0.5f;
    float hx = (hi.x - lo.x) * 0.5f, hy = (hi.y - lo.y) * 0.5f;
    float tx = hx * (1.0f - taperX), ty = hy * (1.0f - taperY);
    // Bottom corners
    Vec3f b0{lo.x, lo.y, lo.z}, b1{hi.x, lo.y, lo.z}, b2{hi.x, hi.y, lo.z}, b3{lo.x, hi.y, lo.z};
    // Top corners (front face may slope back by frontSlope metres)
    Vec3f t0{cx - tx, cy - ty, hi.z}, t1{cx + tx - frontSlope, cy - ty, hi.z}, t2{cx + tx - frontSlope, cy + ty, hi.z}, t3{cx - tx, cy + ty, hi.z};
    flatQuad(t0, t1, t2, t3);  // top (normal +z): order gives +z with x fwd, y stbd? verified by cross product
    flatQuad(b0, b3, b2, b1);  // bottom
    flatQuad(b0, b1, t1, t0);  // port side (y = lo)
    flatQuad(b2, b3, t3, t2);  // starboard side
    flatQuad(b1, b2, t2, t1);  // front (x = hi)
    flatQuad(b3, b0, t0, t3);  // back
}

void MeshBuilder::ellipticCylinder(const Vec3f& base, float rx, float ry, float height, int segments, float rakeX, float topScale,
                                   bool capTop, bool capBottom) {
    uint32_t start = uint32_t(p_.vertices.size());
    Vec3f top = base + Vec3f{-rakeX, 0, height};
    for (int i = 0; i <= segments; ++i) {
        float a = float(i) / segments * float(kTwoPi);
        float c = std::cos(a), s = std::sin(a);
        Vec3f n = normalize(Vec3f{c / std::max(rx, 1e-3f), s / std::max(ry, 1e-3f), 0.0f});
        vertex(base + Vec3f{c * rx, s * ry, 0}, n);
        vertex(top + Vec3f{c * rx * topScale, s * ry * topScale, 0}, n);
    }
    for (int i = 0; i < segments; ++i) {
        uint32_t a = start + uint32_t(i) * 2, b = a + 1, c = a + 2, d = a + 3;
        tri(a, c, d);
        tri(a, d, b);
    }
    if (capTop) {
        uint32_t center = vertex(top, {0, 0, 1});
        uint32_t ring = uint32_t(p_.vertices.size());
        for (int i = 0; i <= segments; ++i) {
            float a = float(i) / segments * float(kTwoPi);
            vertex(top + Vec3f{std::cos(a) * rx * topScale, std::sin(a) * ry * topScale, 0}, {0, 0, 1});
        }
        for (int i = 0; i < segments; ++i) tri(center, ring + uint32_t(i), ring + uint32_t(i) + 1);
    }
    if (capBottom) {
        uint32_t center = vertex(base, {0, 0, -1});
        uint32_t ring = uint32_t(p_.vertices.size());
        for (int i = 0; i <= segments; ++i) {
            float a = float(i) / segments * float(kTwoPi);
            vertex(base + Vec3f{std::cos(a) * rx, std::sin(a) * ry, 0}, {0, 0, -1});
        }
        for (int i = 0; i < segments; ++i) tri(center, ring + uint32_t(i) + 1, ring + uint32_t(i));
    }
}

void MeshBuilder::tube(const Vec3f& a, const Vec3f& b, float r0, float r1, int segments, bool caps) {
    Vec3f axis = b - a;
    float len = length(axis);
    if (len < 1e-5f) return;
    Vec3f w = axis / len;
    Vec3f helper = std::fabs(w.z) < 0.9f ? Vec3f{0, 0, 1} : Vec3f{1, 0, 0};
    Vec3f u = normalize(cross(helper, w));
    Vec3f v = cross(w, u);
    uint32_t start = uint32_t(p_.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        float ang = float(i) / segments * float(kTwoPi);
        Vec3f dir = u * std::cos(ang) + v * std::sin(ang);
        vertex(a + dir * r0, dir);
        vertex(b + dir * r1, dir);
    }
    for (int i = 0; i < segments; ++i) {
        uint32_t i0 = start + uint32_t(i) * 2, i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
        tri(i0, i2, i3);
        tri(i0, i3, i1);
    }
    if (caps) {
        uint32_t cb = vertex(b, w), ring = uint32_t(p_.vertices.size());
        for (int i = 0; i <= segments; ++i) {
            float ang = float(i) / segments * float(kTwoPi);
            vertex(b + (u * std::cos(ang) + v * std::sin(ang)) * r1, w);
        }
        for (int i = 0; i < segments; ++i) tri(cb, ring + uint32_t(i), ring + uint32_t(i) + 1);
        uint32_t ca = vertex(a, -w), ring2 = uint32_t(p_.vertices.size());
        for (int i = 0; i <= segments; ++i) {
            float ang = float(i) / segments * float(kTwoPi);
            vertex(a + (u * std::cos(ang) + v * std::sin(ang)) * r0, -w);
        }
        for (int i = 0; i < segments; ++i) tri(ca, ring2 + uint32_t(i) + 1, ring2 + uint32_t(i));
    }
}

void MeshBuilder::sphere(const Vec3f& c, float rx, float ry, float rz, int segs, int rings) {
    uint32_t start = uint32_t(p_.vertices.size());
    for (int r = 0; r <= rings; ++r) {
        float phi = float(r) / rings * float(kPi);
        for (int s = 0; s <= segs; ++s) {
            float th = float(s) / segs * float(kTwoPi);
            Vec3f d{std::sin(phi) * std::cos(th), std::sin(phi) * std::sin(th), std::cos(phi)};
            vertex(c + Vec3f{d.x * rx, d.y * ry, d.z * rz}, normalize(Vec3f{d.x / rx, d.y / ry, d.z / rz}));
        }
    }
    for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segs; ++s) {
            uint32_t a = start + uint32_t(r * (segs + 1) + s), b = a + 1, c2 = a + uint32_t(segs + 1), d = c2 + 1;
            tri(a, c2, d);
            tri(a, d, b);
        }
}

void computeSmoothNormals(MeshPart& part, uint32_t firstVertex, uint32_t firstIndex) {
    for (size_t i = firstVertex; i < part.vertices.size(); ++i) part.vertices[i].normal = {};
    for (size_t i = firstIndex; i + 2 < part.indices.size(); i += 3) {
        auto& a = part.vertices[part.indices[i]];
        auto& b = part.vertices[part.indices[i + 1]];
        auto& c = part.vertices[part.indices[i + 2]];
        Vec3f n = cross(b.pos - a.pos, c.pos - a.pos);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (size_t i = firstVertex; i < part.vertices.size(); ++i) {
        Vec3f n = part.vertices[i].normal;
        part.vertices[i].normal = lengthSq(n) > 1e-20f ? normalize(n) : Vec3f{0, 0, 1};
    }
}

}  // namespace it::procgen
