#include "procgen/Mesh.h"

#include <algorithm>

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

void MeshBuilder::lathe(const Vec3f& base, const Vec3f& axis, const std::vector<Vec2f>& profile, int segments, bool capEnds) {
    if (profile.size() < 2) return;
    Vec3f w = normalize(axis);
    Vec3f helper = std::fabs(w.z) < 0.9f ? Vec3f{0, 0, 1} : Vec3f{1, 0, 0};
    Vec3f u = normalize(cross(helper, w)), v = cross(w, u);
    // Each profile segment gets its own ring pair so creases stay sharp.
    for (size_t k = 0; k + 1 < profile.size(); ++k) {
        Vec2f p0 = profile[k], p1 = profile[k + 1];
        Vec2f d = p1 - p0;
        if (std::fabs(d.x) < 1e-6f && std::fabs(d.y) < 1e-6f) continue;
        // Outward normal in profile space: perpendicular to the segment, pointing away from the axis.
        Vec2f pn = normalize(Vec2f{d.y, -d.x});
        if (pn.x < 0 && std::fabs(d.y) > 1e-6f) pn = -pn;
        uint32_t start = uint32_t(p_.vertices.size());
        for (int i = 0; i <= segments; ++i) {
            float a = float(i) / segments * float(kTwoPi);
            Vec3f dir = u * std::cos(a) + v * std::sin(a);
            Vec3f n = normalize(dir * pn.x + w * pn.y);
            vertex(base + dir * p0.x + w * p0.y, n);
            vertex(base + dir * p1.x + w * p1.y, n);
        }
        for (int i = 0; i < segments; ++i) {
            uint32_t a = start + uint32_t(i) * 2, b = a + 1, c = a + 2, dd = a + 3;
            tri(a, c, dd);
            tri(a, dd, b);
        }
    }
    if (capEnds) {
        for (int end = 0; end < 2; ++end) {
            Vec2f p = end ? profile.back() : profile.front();
            if (p.x < 1e-4f) continue;
            disc(base + w * p.y, end ? w : -w, p.x, segments);
        }
    }
}

void MeshBuilder::loft(const std::vector<Vec3f>& bottom, const std::vector<Vec3f>& top, bool capTop, bool capBottom) {
    size_t n = bottom.size();
    if (n < 3 || top.size() != n) return;
    // Face normals of each side.
    std::vector<Vec3f> fn(n);
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n;
        Vec3f e0 = bottom[j] - bottom[i], e1 = top[i] - bottom[i];
        if (lengthSq(e1) < 1e-10f) e1 = top[j] - bottom[i];
        Vec3f c = cross(e0, e1);
        fn[i] = lengthSq(c) > 1e-14f ? normalize(c) : Vec3f{0, 0, 1};
    }
    // Outward orientation: compare with the direction from the ring centre.
    Vec3f ctr{};
    for (auto& p : bottom) ctr += p;
    ctr = ctr / float(n);
    float sgn = dot(fn[0], (bottom[0] + bottom[1 % n]) * 0.5f - ctr) < 0 ? -1.0f : 1.0f;
    for (auto& f : fn) f = f * sgn;
    for (size_t i = 0; i < n; ++i) {
        size_t j = (i + 1) % n, ip = (i + n - 1) % n, jn = (j + 1) % n;
        // Smooth across gentle corners.
        Vec3f na = dot(fn[i], fn[ip]) > 0.82f ? normalize(fn[i] + fn[ip]) : fn[i];
        Vec3f nb = dot(fn[i], fn[j]) > 0.82f ? normalize(fn[i] + fn[j]) : fn[i];
        (void)jn;
        uint32_t a = vertex(bottom[i], na), b = vertex(bottom[j], nb), c = vertex(top[j], nb), d = vertex(top[i], na);
        if (sgn > 0) quad(a, b, c, d);
        else quad(a, d, c, b);
    }
    if (capTop) polygon(top);
    if (capBottom) {
        std::vector<Vec3f> rev(bottom.rbegin(), bottom.rend());
        polygon(rev);
    }
}

void MeshBuilder::prism(const std::vector<Vec2f>& outline, float z0, float z1, float inset, bool capBottom) {
    if (outline.size() < 3) return;
    Vec2f c{};
    for (auto& p : outline) c += p;
    c = c / float(outline.size());
    std::vector<Vec3f> lo, hi;
    for (auto& p : outline) {
        lo.push_back({p.x, p.y, z0});
        Vec2f d = p - c;
        float l = length(d);
        Vec2f q = l > 1e-4f ? p - d / l * std::min(inset, l * 0.9f) : p;
        hi.push_back({q.x, q.y, z1});
    }
    loft(lo, hi, true, capBottom);
}

void MeshBuilder::polyline(const std::vector<Vec3f>& pts, float radius, int segments, bool closed) {
    size_t n = pts.size();
    if (n < 2) return;
    size_t count = closed ? n + 1 : n;
    auto at = [&](size_t i) { return pts[i % n]; };
    uint32_t start = uint32_t(p_.vertices.size());
    Vec3f prevU{};
    for (size_t i = 0; i < count; ++i) {
        Vec3f t;
        if (closed) t = normalize(at(i + 1) - at(i + n - 1));
        else if (i == 0) t = normalize(pts[1] - pts[0]);
        else if (i + 1 == n) t = normalize(pts[n - 1] - pts[n - 2]);
        else t = normalize(normalize(pts[i + 1] - pts[i]) + normalize(pts[i] - pts[i - 1]));
        Vec3f u;
        if (i == 0) {
            Vec3f helper = std::fabs(t.z) < 0.9f ? Vec3f{0, 0, 1} : Vec3f{1, 0, 0};
            u = normalize(cross(helper, t));
        } else {
            u = normalize(prevU - t * dot(prevU, t));  // parallel transport
        }
        prevU = u;
        Vec3f v = cross(t, u);
        for (int k = 0; k <= segments; ++k) {
            float a = float(k) / segments * float(kTwoPi);
            Vec3f d = u * std::cos(a) + v * std::sin(a);
            vertex(at(i) + d * radius, d);
        }
    }
    uint32_t ring = uint32_t(segments + 1);
    for (size_t i = 0; i + 1 < count; ++i)
        for (int k = 0; k < segments; ++k) {
            uint32_t a = start + uint32_t(i) * ring + uint32_t(k), b = a + 1, c = a + ring + 1, d = a + ring;
            quad(a, d, c, b);
        }
}

void MeshBuilder::bar(const Vec3f& a, const Vec3f& b, float w, float h, const Vec3f& up) {
    Vec3f axis = b - a;
    float len = length(axis);
    if (len < 1e-5f) return;
    Vec3f t = axis / len;
    Vec3f s = cross(t, up);
    if (lengthSq(s) < 1e-8f) s = cross(t, Vec3f{1, 0, 0});
    s = normalize(s);
    Vec3f u = cross(s, t);
    Vec3f hs = s * (w * 0.5f), hu = u * (h * 0.5f);
    Vec3f c0 = a - hs - hu, c1 = a + hs - hu, c2 = a + hs + hu, c3 = a - hs + hu;
    Vec3f d0 = c0 + axis, d1 = c1 + axis, d2 = c2 + axis, d3 = c3 + axis;
    flatQuad(c0, c1, d1, d0);
    flatQuad(c1, c2, d2, d1);
    flatQuad(c2, c3, d3, d2);
    flatQuad(c3, c0, d0, d3);
}

void MeshBuilder::disc(const Vec3f& c, const Vec3f& n, float r, int segments) {
    Vec3f w = normalize(n);
    Vec3f helper = std::fabs(w.z) < 0.9f ? Vec3f{0, 0, 1} : Vec3f{1, 0, 0};
    Vec3f u = normalize(cross(helper, w)), v = cross(w, u);
    uint32_t center = vertex(c, w), ring = uint32_t(p_.vertices.size());
    for (int i = 0; i <= segments; ++i) {
        float a = float(i) / segments * float(kTwoPi);
        vertex(c + (u * std::cos(a) + v * std::sin(a)) * r, w);
    }
    for (int i = 0; i < segments; ++i) tri(center, ring + uint32_t(i), ring + uint32_t(i) + 1);
}

void MeshBuilder::polygon(const std::vector<Vec3f>& pts) {
    if (pts.size() < 3) return;
    Vec3f n{};
    for (size_t i = 1; i + 1 < pts.size(); ++i) n += cross(pts[i] - pts[0], pts[i + 1] - pts[0]);
    n = lengthSq(n) > 1e-14f ? normalize(n) : Vec3f{0, 0, 1};
    uint32_t first = uint32_t(p_.vertices.size());
    for (auto& p : pts) vertex(p, n);
    for (uint32_t i = 1; i + 1 < pts.size(); ++i) tri(first, first + i, first + i + 1);
}

std::vector<Vec2f> roundedRect(float cx, float cy, float len, float wid, float radius, int cornerSegments) {
    std::vector<Vec2f> out;
    radius = std::min({radius, len * 0.5f, wid * 0.5f});
    float hx = len * 0.5f - radius, hy = wid * 0.5f - radius;
    const Vec2f centres[4] = {{cx + hx, cy - hy}, {cx + hx, cy + hy}, {cx - hx, cy + hy}, {cx - hx, cy - hy}};
    for (int c = 0; c < 4; ++c) {
        float a0 = -float(kPi) * 0.5f + float(c) * float(kPi) * 0.5f;
        for (int k = 0; k <= cornerSegments; ++k) {
            float a = a0 + float(kPi) * 0.5f * float(k) / float(std::max(1, cornerSegments));
            out.push_back(centres[c] + Vec2f{std::cos(a), std::sin(a)} * radius);
        }
    }
    return out;
}

std::vector<Vec2f> gunhouseOutline(float cx, float cy, float len, float wid, float frontWid, float chamfer, float rearRadius) {
    std::vector<Vec2f> out;
    float x0 = cx - len * 0.5f, x1 = cx + len * 0.5f, hw = wid * 0.5f, fw = frontWid * 0.5f;
    out.push_back({x1, cy - fw});
    out.push_back({x1, cy + fw});
    out.push_back({x1 - chamfer, cy + hw});
    // Rounded rear corners.
    rearRadius = std::min(rearRadius, std::min(hw, (x1 - chamfer - x0) * 0.9f));
    for (int k = 0; k <= 4; ++k) {
        float a = float(kPi) * 0.5f + float(kPi) * 0.5f * float(k) / 4.0f;
        out.push_back(Vec2f{x0 + rearRadius, cy + hw - rearRadius} + Vec2f{std::cos(a), std::sin(a)} * rearRadius);
    }
    for (int k = 0; k <= 4; ++k) {
        float a = float(kPi) + float(kPi) * 0.5f * float(k) / 4.0f;
        out.push_back(Vec2f{x0 + rearRadius, cy - hw + rearRadius} + Vec2f{std::cos(a), std::sin(a)} * rearRadius);
    }
    out.push_back({x1 - chamfer, cy - hw});
    return out;
}

std::vector<Vec2f> ellipseOutline(float cx, float cy, float rx, float ry, int segments) {
    std::vector<Vec2f> out;
    for (int i = 0; i < segments; ++i) {
        float a = float(i) / segments * float(kTwoPi);
        out.push_back({cx + std::cos(a) * rx, cy + std::sin(a) * ry});
    }
    return out;
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
