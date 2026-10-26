#define CGLTF_IMPLEMENTATION
#include "procgen/GltfImport.h"

#include <cgltf/cgltf.h>

#include <map>

namespace it::procgen {
namespace {

Mat4f nodeMatrix(const cgltf_node* n) {
    float m[16];
    cgltf_node_transform_world(n, m);
    Mat4f r;
    for (int c = 0; c < 4; ++c)
        for (int rr = 0; rr < 4; ++rr) r.m[c][rr] = m[c * 4 + rr];
    return r;
}

Vec3f toShip(const Vec3f& g, double scale) { return Vec3f{g.z, -g.x, g.y} * float(scale); }

}  // namespace

bool importGltf(const std::string& path, const sim::ShipClassDef& cls, double scale, Model& out, std::string* error) {
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) {
        if (error) *error = "cannot parse " + path;
        return false;
    }
    if (cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) {
        cgltf_free(data);
        if (error) *error = "cannot load buffers of " + path;
        return false;
    }
    out = {};
    out.name = cls.id;
    out.parts.push_back({});
    out.parts[0].name = "hull";
    std::map<std::string, int> partByName;
    for (size_t ni = 0; ni < data->nodes_count; ++ni) {
        const cgltf_node* node = &data->nodes[ni];
        if (!node->mesh) continue;
        // Find the animated ancestor (mount_X / guns_X) if any.
        int partIndex = 0;
        for (const cgltf_node* p = node; p; p = p->parent) {
            if (!p->name) continue;
            std::string nm = p->name;
            bool turret = nm.rfind("mount_", 0) == 0, guns = nm.rfind("guns_", 0) == 0;
            if (!turret && !guns) continue;
            auto it = partByName.find(nm);
            if (it == partByName.end()) {
                MeshPart part;
                part.name = nm;
                part.kind = turret ? PartKind::Turret : PartKind::Barrel;
                std::string mid = nm.substr(turret ? 6 : 5);
                for (size_t k = 0; k < cls.mounts.size(); ++k)
                    if (cls.mounts[k].id == mid) part.mount = int(k);
                Mat4f pm = nodeMatrix(p);
                part.pivot = toShip(pm.transformPoint({0, 0, 0}), scale);
                out.parts.push_back(part);
                it = partByName.emplace(nm, int(out.parts.size()) - 1).first;
            }
            partIndex = it->second;
            break;
        }
        Mat4f world = nodeMatrix(node);
        MeshPart& part = out.parts[size_t(partIndex)];
        for (size_t pi = 0; pi < node->mesh->primitives_count; ++pi) {
            const cgltf_primitive& prim = node->mesh->primitives[pi];
            if (prim.type != cgltf_primitive_type_triangles) continue;
            const cgltf_accessor *pos = nullptr, *nrm = nullptr;
            for (size_t a = 0; a < prim.attributes_count; ++a) {
                if (prim.attributes[a].type == cgltf_attribute_type_position) pos = prim.attributes[a].data;
                if (prim.attributes[a].type == cgltf_attribute_type_normal) nrm = prim.attributes[a].data;
            }
            if (!pos) continue;
            Material m;
            m.id = 4;
            if (prim.material && prim.material->has_pbr_metallic_roughness) {
                const auto& pbr = prim.material->pbr_metallic_roughness;
                m.albedo = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
                m.roughness = pbr.roughness_factor;
                m.metalness = pbr.metallic_factor;
            }
            uint32_t base = uint32_t(part.vertices.size());
            for (size_t v = 0; v < pos->count; ++v) {
                float p3[3] = {0, 0, 0}, n3[3] = {0, 1, 0};
                cgltf_accessor_read_float(pos, v, p3, 3);
                if (nrm) cgltf_accessor_read_float(nrm, v, n3, 3);
                Vertex vx;
                vx.pos = toShip(world.transformPoint({p3[0], p3[1], p3[2]}), scale);
                vx.normal = normalize(toShip(world.transformDir({n3[0], n3[1], n3[2]}), 1.0));
                vx.color = m.packColor();
                vx.params = m.packParams();
                part.vertices.push_back(vx);
            }
            if (prim.indices) {
                for (size_t k = 0; k < prim.indices->count; k += 3) {
                    // Axis conversion flips handedness: swap two indices to keep outward winding.
                    uint32_t a = uint32_t(cgltf_accessor_read_index(prim.indices, k));
                    uint32_t b = uint32_t(cgltf_accessor_read_index(prim.indices, k + 1));
                    uint32_t c = uint32_t(cgltf_accessor_read_index(prim.indices, k + 2));
                    part.indices.insert(part.indices.end(), {base + a, base + c, base + b});
                }
            } else {
                for (uint32_t k = 0; k + 2 < uint32_t(pos->count); k += 3) part.indices.insert(part.indices.end(), {base + k, base + k + 2, base + k + 1});
            }
            if (!nrm) computeSmoothNormals(part, base, uint32_t(part.indices.size()) - uint32_t(prim.indices ? prim.indices->count : pos->count));
        }
    }
    // Barrels parent to their turret.
    for (auto& p : out.parts)
        if (p.kind == PartKind::Barrel)
            for (size_t k = 0; k < out.parts.size(); ++k)
                if (out.parts[k].kind == PartKind::Turret && out.parts[k].mount == p.mount) p.parent = int(k);
    cgltf_free(data);
    out.computeBounds();
    return out.triangleCount() > 0;
}

}  // namespace it::procgen
