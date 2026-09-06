/*
 * Genesis Render - model loader dispatch + geometry utilities.
 */
#include "resource/model_loader.hpp"
#include "foundation_layer/log.hpp"
#include <algorithm>
#include <cstring>

namespace Genesis {

static std::string extensionOf(const std::string& path) {
    auto p = path.find_last_of('.');
    if (p == std::string::npos) return "";
    std::string e = path.substr(p + 1);
    for (auto& c : e) c = (char)tolower(c);
    return e;
}

void generateTangents(MeshData& mesh) {
    std::vector<Vec3> tan1(mesh.vertices.size(), Vec3{0, 0, 0});
    for (size_t i = 0; i + 3 <= mesh.indices.size(); i += 3) {
        uint32_t i0 = mesh.indices[i], i1 = mesh.indices[i + 1], i2 = mesh.indices[i + 2];
        Vertex& v0 = mesh.vertices[i0];
        Vertex& v1 = mesh.vertices[i1];
        Vertex& v2 = mesh.vertices[i2];
        Vec3 e1 = v1.position - v0.position;
        Vec3 e2 = v2.position - v0.position;
        Vec2 du{v1.uv.x - v0.uv.x, v1.uv.y - v0.uv.y};
        Vec2 dv{v2.uv.x - v0.uv.x, v2.uv.y - v0.uv.y};
        float det = du.x * dv.y - dv.x * du.y;
        if (std::fabs(det) < 1e-10f) continue;
        float f = 1.0f / det;
        Vec3 t{e1.x * dv.y - e2.x * du.y, e1.y * dv.y - e2.y * du.y, e1.z * dv.y - e2.z * du.y};
        t = t * f;
        tan1[i0] += t; tan1[i1] += t; tan1[i2] += t;
    }
    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        Vertex& v = mesh.vertices[i];
        Vec3 n = normalize(v.normal);
        Vec3 t = tan1[i];
        Vec3 orth = t - n * dot(t, n);
        if (length(orth) < 1e-8f) orth = normalize(cross(n, std::fabs(n.y) < 0.99f ? Vec3{0,1,0} : Vec3{1,0,0}));
        orth = normalize(orth);
        v.tangent = Vec4{orth.x, orth.y, orth.z, 1.0f};
    }
}

void mergeMeshes(ModelAsset& a) {
    if (a.merged) return;
    a.combined.vertices.clear();
    a.combined.indices.clear();
    a.combinedSubs.clear();
    a.nodeDraws.assign(a.nodes.size(), {});

    // flatten node->submeshes in node order
    for (uint32_t ni = 0; ni < a.nodes.size(); ++ni) {
        const MeshNode& node = a.nodes[ni];
        if (node.meshIndices.empty()) {
            a.nodeDraws[ni].combinedOffset = (uint32_t)a.combined.indices.size();
            continue;
        }
        uint32_t start = (uint32_t)a.combined.indices.size();
        for (uint32_t mi : node.meshIndices) {
            if (mi >= a.meshes.size()) continue;
            const MeshData& m = a.meshes[mi];
            uint32_t baseVertex = (uint32_t)a.combined.vertices.size();
            a.combined.vertices.insert(a.combined.vertices.end(), m.vertices.begin(), m.vertices.end());
            for (const SubMesh& sm : m.submeshes) {
                ModelAsset::CombinedSub cs;
                cs.indexOffset = (uint32_t)a.combined.indices.size();
                cs.indexCount = sm.indexCount;
                cs.materialIndex = sm.materialIndex;
                cs.node = ni;
                for (uint32_t k = 0; k < sm.indexCount; ++k)
                    a.combined.indices.push_back(m.indices[sm.indexOffset + k] + baseVertex);
                a.combinedSubs.push_back(cs);
            }
        }
        a.nodeDraws[ni].combinedOffset = start;
        a.nodeDraws[ni].combinedCount = (uint32_t)a.combined.indices.size() - start;
    }
    generateTangents(a.combined);
    a.merged = true;
}

std::shared_ptr<ModelAsset> loadModelFile(const std::string& path, std::string& err) {
    std::string ext = extensionOf(path);
    std::shared_ptr<ModelAsset> asset;
    if (ext == "gltf" || ext == "glb") {
        asset = loadGLTF(path, err);
    } else {
#ifdef GENESIS_WITH_ASSIMP
        asset = loadAssimpModel(path, err);
#else
        err = "format '" + ext + "' requires Assimp build (glTF/glb supported natively)";
#endif
    }
    if (asset) mergeMeshes(*asset);
    return asset;
}

} // namespace Genesis
