/*
 * Genesis Render - glTF 2.0 (.gltf/.glb) loader based on cgltf.
 * Produces merged interleaved Vertex geometry, PBR materials and a node tree.
 */
#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "resource/model_loader.hpp"
#include "foundation_layer/log.hpp"

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace Genesis {
namespace fs = std::filesystem;

static std::string parentDir(const std::string& path) {
    fs::path p(path);
    return p.parent_path().string();
}
static std::string baseName(const std::string& path) {
    return fs::path(path).filename().string();
}

static Vec3 readVec3(const cgltf_float* p) { return Vec3{p[0], p[1], p[2]}; }

// Materialize an embedded (glb) image once and return an embedded:// uri.
static std::string embeddedUri(ModelAsset& a, const cgltf_image* img) {
    if (img->buffer_view && img->buffer_view->buffer->data) {
        std::string key = std::string("img") + std::to_string(a.embeddedImages.size());
        const uint8_t* d = (const uint8_t*)img->buffer_view->buffer->data;
        std::vector<uint8_t> bytes(d + img->buffer_view->offset,
                                   d + img->buffer_view->offset + img->buffer_view->size);
        a.embeddedImages[key] = std::move(bytes);
        return "embedded://" + key;
    }
    return "";
}

static std::string imagePath(ModelAsset& a, const cgltf_image* img) {
    if (!img) return "";
    if (img->buffer_view) return embeddedUri(a, img);
    if (img->uri && strncmp(img->uri, "data:", 5) != 0) {
        fs::path p = fs::path(a.baseDir) / img->uri;
        return p.lexically_normal().string();
    }
    return "";
}

static void readMaterial(ModelAsset& a, const cgltf_material* m) {
    MaterialDesc md;
    md.name = m->name ? m->name : "material";
    const cgltf_pbr_metallic_roughness& pbr = m->has_pbr_metallic_roughness
                                                  ? m->pbr_metallic_roughness
                                                  : cgltf_pbr_metallic_roughness{};
    md.baseColorFactor = Vec4{pbr.base_color_factor[0], pbr.base_color_factor[1],
                              pbr.base_color_factor[2], pbr.base_color_factor[3]};
    md.metallicFactor = pbr.metallic_factor;
    md.roughnessFactor = pbr.roughness_factor;
    md.emissiveFactor = readVec3(m->emissive_factor);
    md.doubleSided = m->double_sided != 0;
    md.normalScale = m->normal_texture.scale;
    if (pbr.base_color_texture.texture) {
        md.baseColorTex = imagePath(a, pbr.base_color_texture.texture->image);
        md.hasTextures = true;
    }
    if (pbr.metallic_roughness_texture.texture) {
        md.metallicRoughnessTex = imagePath(a, pbr.metallic_roughness_texture.texture->image);
        md.hasTextures = true;
    }
    if (m->normal_texture.texture) md.normalTex = imagePath(a, m->normal_texture.texture->image);
    if (m->occlusion_texture.texture) md.occlusionTex = imagePath(a, m->occlusion_texture.texture->image);
    if (m->emissive_texture.texture) md.emissiveTex = imagePath(a, m->emissive_texture.texture->image);
    a.materials.push_back(md);
}

static void readPrimitive(MeshData& out, const cgltf_primitive& prim, int materialIndex) {
    const cgltf_accessor* posA = nullptr;
    const cgltf_accessor* nrmA = nullptr;
    const cgltf_accessor* tanA = nullptr;
    const cgltf_accessor* uvA = nullptr;
    for (cgltf_size i = 0; i < prim.attributes_count; ++i) {
        switch (prim.attributes[i].type) {
        case cgltf_attribute_type_position: posA = prim.attributes[i].data; break;
        case cgltf_attribute_type_normal: nrmA = prim.attributes[i].data; break;
        case cgltf_attribute_type_tangent: tanA = prim.attributes[i].data; break;
        case cgltf_attribute_type_texcoord:
            if (prim.attributes[i].index == 0) uvA = prim.attributes[i].data;
            break;
        default: break;
        }
    }
    if (!posA) return;
    uint32_t baseVertex = (uint32_t)out.vertices.size();
    out.vertices.resize(baseVertex + posA->count);
    for (cgltf_size i = 0; i < posA->count; ++i) {
        Vertex& v = out.vertices[baseVertex + i];
        cgltf_float p[3] = {0,0,0}, n[3] = {0,1,0}, t[4] = {1,0,0,1}, uv[2] = {0,0};
        cgltf_accessor_read_float(posA, i, p, 3);
        if (nrmA) cgltf_accessor_read_float(nrmA, i, n, 3);
        if (tanA) cgltf_accessor_read_float(tanA, i, t, 4);
        if (uvA) cgltf_accessor_read_float(uvA, i, uv, 2);
        v.position = Vec3{p[0], p[1], p[2]};
        v.normal = Vec3{n[0], n[1], n[2]};
        v.tangent = Vec4{t[0], t[1], t[2], t[3]};
        v.uv = Vec2{uv[0], 1.0f - uv[1]}; // flip to engine convention
        if (!nrmA) v.normal = Vec3{0, 1, 0};
    }
    uint32_t start = (uint32_t)out.indices.size();
    if (prim.indices) {
        for (cgltf_size i = 0; i < prim.indices->count; ++i)
            out.indices.push_back((uint32_t)cgltf_accessor_read_index(prim.indices, i) + baseVertex);
    } else {
        for (cgltf_size i = 0; i < posA->count; ++i)
            out.indices.push_back(baseVertex + (uint32_t)i);
    }
    SubMesh sm;
    sm.indexOffset = start;
    sm.indexCount = (uint32_t)(out.indices.size() - start);
    sm.materialIndex = materialIndex;
    out.submeshes.push_back(sm);
}

static uint32_t makeNode(ModelAsset& a, const cgltf_node* n) {
    MeshNode node;
    node.name = n->name ? n->name : ("node" + std::to_string(a.nodes.size()));
    if (n->has_matrix) {
        const float* m = n->matrix;
        // cgltf matrix is column-major like our Mat4
        for (int c = 0; c < 4; ++c)
            for (int r = 0; r < 4; ++r)
                node.local.at(c, r) = m[c * 4 + r];
    } else {
        Vec3 pos{0,0,0}, euler{0,0,0}, scl{1,1,1};
        if (n->has_translation) pos = readVec3(n->translation);
        if (n->has_scale) scl = readVec3(n->scale);
        if (n->has_rotation) {
            Quat q{n->rotation[0], n->rotation[1], n->rotation[2], n->rotation[3]};
            euler = quatToEuler(q);
        }
        node.local = makeTransform(pos, euler, scl);
    }
    a.nodes.push_back(node);
    return (uint32_t)a.nodes.size() - 1;
}

static uint32_t nodeRecurse(ModelAsset& a, const cgltf_node* n,
                            const cgltf_mesh* allMeshes, cgltf_size meshCount) {
    uint32_t ni = makeNode(a, n);
    if (n->mesh) {
        uint32_t mi = (uint32_t)(n->mesh - allMeshes);
        if (mi < meshCount) a.nodes[ni].meshIndices.push_back(mi);
    }
    for (cgltf_size k = 0; k < n->children_count; ++k)
        a.nodes[ni].children.push_back(nodeRecurse(a, n->children[k], allMeshes, meshCount));
    return ni;
}

std::shared_ptr<ModelAsset> loadGLTF(const std::string& path, std::string& err) {
    cgltf_options opt{};
    cgltf_data* data = nullptr;
    cgltf_result r = cgltf_parse_file(&opt, path.c_str(), &data);
    if (r != cgltf_result_success) {
        err = "cgltf_parse_file failed: " + std::to_string((int)r);
        return nullptr;
    }
    r = cgltf_load_buffers(&opt, data, path.c_str());
    if (r != cgltf_result_success) {
        err = "cgltf_load_buffers failed: " + std::to_string((int)r);
        cgltf_free(data);
        return nullptr;
    }
    auto asset = std::make_shared<ModelAsset>();
    asset->name = baseName(path);
    asset->baseDir = parentDir(path);

    for (cgltf_size i = 0; i < data->materials_count; ++i)
        readMaterial(*asset, &data->materials[i]);

    asset->meshes.resize(data->meshes_count);
    for (cgltf_size mi = 0; mi < data->meshes_count; ++mi) {
        const cgltf_mesh& m = data->meshes[mi];
        for (cgltf_size pi = 0; pi < m.primitives_count; ++pi) {
            const cgltf_primitive& prim = m.primitives[pi];
            int matIdx = -1;
            if (prim.material) matIdx = (int32_t)(prim.material - data->materials);
            readPrimitive(asset->meshes[mi], prim, matIdx);
        }
    }
    for (cgltf_size i = 0; i < data->nodes_count; ++i) {
        if (data->nodes[i].parent == nullptr)
            asset->roots.push_back(nodeRecurse(*asset, &data->nodes[i], data->meshes, data->meshes_count));
    }
    GenesisPrint("[gltf] loaded %s: %zu meshes, %zu nodes, %zu materials\n",
                 asset->name.c_str(), asset->meshes.size(), asset->nodes.size(), asset->materials.size());
    cgltf_free(data);
    return asset;
}

} // namespace Genesis
