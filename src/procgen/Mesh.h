#pragma once
// Mesh data for procedurally generated (and imported) models. Model space follows the simulation's
// ship-local frame: x forward, y starboard, z up (metres, origin at midships on the waterline).
#include <cstdint>
#include <string>
#include <vector>

#include "core/Math.h"

namespace it::procgen {

/// Packed vertex: 32 bytes. Colour is linear-space albedo in RGBA8; params = roughness, metalness,
/// material id, emissive (each 0..255).
struct Vertex {
    Vec3f pos;
    Vec3f normal;
    uint32_t color = 0xffffffff;
    uint32_t params = 0;
};

struct Material {
    Vec3f albedo{0.5f, 0.5f, 0.5f};  // linear
    float roughness = 0.7f;
    float metalness = 0.0f;
    uint8_t id = 0;
    float emissive = 0.0f;
    uint32_t packColor() const;
    uint32_t packParams() const;
};

enum class PartKind : uint8_t { Static, Turret, Barrel, TorpedoMount, Searchlight, Radar, Director, Catapult, Aircraft };

struct MeshPart {
    std::string name;
    PartKind kind = PartKind::Static;
    int mount = -1;         // index into the class mounts / torpedo mounts this part animates with
    int parent = -1;        // parent part (barrels belong to a turret)
    Vec3f pivot;            // rotation pivot in model space
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    Aabb bounds() const;
};

struct Model {
    std::string name;
    std::vector<MeshPart> parts;
    Aabb bounds;
    size_t triangleCount() const;
    void computeBounds();
};

/// Incremental builder of primitive shapes into a MeshPart (all in model space).
class MeshBuilder {
public:
    explicit MeshBuilder(MeshPart& part) : p_(part) {}
    void setMaterial(const Material& m) { mat_ = m; }
    const Material& material() const { return mat_; }

    uint32_t vertex(const Vec3f& pos, const Vec3f& n);
    void tri(uint32_t a, uint32_t b, uint32_t c) { p_.indices.insert(p_.indices.end(), {a, b, c}); }
    void quad(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
        tri(a, b, c);
        tri(a, c, d);
    }
    /// Flat-shaded quad with automatic normal (counter-clockwise when seen from outside).
    void flatQuad(const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d);
    void flatTri(const Vec3f& a, const Vec3f& b, const Vec3f& c);

    /// Axis-aligned box (min/max in model space).
    void box(const Vec3f& lo, const Vec3f& hi);
    /// Box whose top face is shrunk by `taper` (0..1) on x/y — sloped superstructure sides.
    void taperedBox(const Vec3f& lo, const Vec3f& hi, float taperX, float taperY, float frontSlope = 0.0f);
    /// Elliptic cylinder along z from z0 to z1, optionally raked (top offset along -x by rake metres).
    void ellipticCylinder(const Vec3f& base, float rx, float ry, float height, int segments, float rakeX = 0.0f, float topScale = 1.0f,
                          bool capTop = true, bool capBottom = false);
    /// Cylinder between two points with radius r0 at a and r1 at b.
    void tube(const Vec3f& a, const Vec3f& b, float r0, float r1, int segments, bool caps = true);
    void sphere(const Vec3f& c, float rx, float ry, float rz, int segs = 10, int rings = 6);

private:
    MeshPart& p_;
    Material mat_;
};

/// Recompute smooth vertex normals from triangles (for lofted surfaces).
void computeSmoothNormals(MeshPart& part, uint32_t firstVertex = 0, uint32_t firstIndex = 0);

}  // namespace it::procgen
