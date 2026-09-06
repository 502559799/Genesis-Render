/*
 * Genesis Render - AssetManager implementation.
 */
#include "resource/asset_manager.hpp"
#include "resource/texture_loader.hpp"
#include "foundation_layer/log.hpp"

#include <cstring>
#include <limits>
#include <algorithm>

namespace Genesis {

AssetManager::AssetManager(IDevice* device) : dev_(device) {
    defAlbedo_ = createDefaultAlbedo(dev_);
    defNormal_ = createDefaultNormal(dev_);
    defMR_ = createDefaultMR(dev_);
    defWhite_ = createDefaultWhite(dev_);
    defEmissive_ = createDefaultEmissive(dev_);
}

AssetManager::~AssetManager() = default;

ITexture* AssetManager::resolveImage(const std::string& uri, bool srgb, ModelAsset* owner) {
    if (uri.empty()) return nullptr;
    auto it = textures_.find(uri);
    if (it != textures_.end()) return it->second;
    LoadedImage img;
    bool ok = false;
    if (uri.rfind("embedded://", 0) == 0 && owner) {
        auto key = uri.substr(11);
        auto eit = owner->embeddedImages.find(key);
        if (eit != owner->embeddedImages.end())
            ok = loadImageMemory(eit->second.data(), eit->second.size(), img);
    } else {
        ok = loadImageFile(uri, img);
    }
    if (!ok) return nullptr;
    ITexture* t = createRhiTexture(dev_, img, srgb);
    textures_[uri] = t;
    return t;
}

static void fillMaterialUBO(const MaterialDesc& d, GpuMaterial& m) {
    MaterialUBO& u = m.uboData;
    u.baseColor[0] = d.baseColorFactor.x; u.baseColor[1] = d.baseColorFactor.y;
    u.baseColor[2] = d.baseColorFactor.z; u.baseColor[3] = d.baseColorFactor.w;
    u.pbr[0] = d.metallicFactor; u.pbr[1] = d.roughnessFactor;
    u.pbr[2] = d.normalScale; u.pbr[3] = 1.0f;
    u.emissive[0] = d.emissiveFactor.x; u.emissive[1] = d.emissiveFactor.y;
    u.emissive[2] = d.emissiveFactor.z;
    // Mask is 1 only for *real* textures, never for default fallbacks
    // (otherwise the default grey MR texture overrides material factors).
    u.texMask[0] = d.baseColorTex.empty() ? 0.f : (m.albedo ? 1.f : 0.f);
    u.texMask[1] = d.normalTex.empty() ? 0.f : (m.normal ? 1.f : 0.f);
    u.texMask[2] = d.metallicRoughnessTex.empty() ? 0.f : (m.metallicRoughness ? 1.f : 0.f);
    u.texMask[3] = d.occlusionTex.empty() ? 0.f : (m.occlusion ? 1.f : 0.f);
    u.texMask2[0] = d.emissiveTex.empty() ? 0.f : (m.emissive ? 1.f : 0.f);
    u.texMask2[1] = d.doubleSided ? 1.f : 0.f;
}

GpuMaterial* AssetManager::buildMaterial(const MaterialDesc& d, ModelAsset* owner) {
    if (!defAlbedo_) {
        defAlbedo_ = createDefaultAlbedo(dev_);
        defNormal_ = createDefaultNormal(dev_);
        defMR_ = createDefaultMR(dev_);
        defWhite_ = createDefaultWhite(dev_);
        defEmissive_ = createDefaultEmissive(dev_);
    }
    auto up = std::make_unique<GpuMaterial>();
    GpuMaterial* m = up.get();
    m->name = d.name;
    m->doubleSided = d.doubleSided;
    m->albedo = resolveImage(d.baseColorTex, true, owner);
    m->normal = resolveImage(d.normalTex, false, owner);
    m->metallicRoughness = resolveImage(d.metallicRoughnessTex, false, owner);
    m->occlusion = resolveImage(d.occlusionTex, false, owner);
    m->emissive = resolveImage(d.emissiveTex, true, owner);
    if (!m->albedo) m->albedo = defAlbedo_;
    if (!m->normal) m->normal = defNormal_;
    if (!m->metallicRoughness) m->metallicRoughness = defMR_;
    if (!m->occlusion) m->occlusion = defWhite_;
    if (!m->emissive) m->emissive = defEmissive_;
    fillMaterialUBO(d, *m);
    BufferDesc bd;
    bd.size = sizeof(MaterialUBO);
    bd.usage = BU_Uniform;
    bd.host_visible = true;
    m->ubo = dev_->createBuffer(bd, &m->uboData);
    std::string key;
    if (!d.baseColorTex.empty() || !d.normalTex.empty() || !d.metallicRoughnessTex.empty())
        key = d.baseColorTex + "|" + d.normalTex + "|" + d.metallicRoughnessTex;
    else {
        static uint64_t synthId = 1;
        char buf[48];
        snprintf(buf, sizeof(buf), "synth://%llu", (unsigned long long)synthId++);
        key = buf;
    }
    materials_[key] = std::move(up);
    return m;
}

ITexture* AssetManager::loadTexture(const std::string& path, bool srgb) {
    return resolveImage(path, srgb, nullptr);
}

GpuModel* AssetManager::uploadModel(std::shared_ptr<ModelAsset> asset, const std::string& key) {
    if (!asset) return nullptr;
    auto up = std::make_unique<GpuModel>();
    GpuModel* m = up.get();
    m->source = asset;

    BufferDesc vbd;
    vbd.size = asset->combined.vertices.size() * sizeof(Vertex);
    vbd.usage = BU_Vertex;
    vbd.host_visible = true;
    m->vertexBuffer = dev_->createBuffer(vbd, asset->combined.vertices.data());

    BufferDesc ibd;
    ibd.size = asset->combined.indices.size() * sizeof(uint32_t);
    ibd.usage = BU_Index;
    ibd.host_visible = true;
    m->indexBuffer = dev_->createBuffer(ibd, asset->combined.indices.data());
    m->indexCount = (uint32_t)asset->combined.indices.size();

    m->materials.reserve(asset->materials.size());
    for (const auto& md : asset->materials) {
        GpuMaterial* gm = buildMaterial(md, asset.get());
        m->materials.push_back(gm);
    }

    Vec3 lo{std::numeric_limits<float>::max(), std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max()};
    Vec3 hi = lo * -1.0f;
    for (const Vertex& v : asset->combined.vertices) {
        lo = Vec3{std::min(lo.x, v.position.x), std::min(lo.y, v.position.y), std::min(lo.z, v.position.z)};
        hi = Vec3{std::max(hi.x, v.position.x), std::max(hi.y, v.position.y), std::max(hi.z, v.position.z)};
    }
    m->boundsMin = lo; m->boundsMax = hi;

    for (const auto& cs : asset->combinedSubs) {
        GpuNodeDraw d;
        d.indexOffset = cs.indexOffset;
        d.indexCount = cs.indexCount;
        d.node = cs.node;
        d.material = cs.materialIndex >= 0 && cs.materialIndex < (int32_t)m->materials.size()
                         ? m->materials[cs.materialIndex]
                         : defaultMaterial();
        m->draws.push_back(d);
    }
    models_[key] = std::move(up);
    GenesisPrint("[asset] GPU model '%s': %u indices, %zu materials\n",
                 asset->name.c_str(), m->indexCount, m->materials.size());
    return m;
}

GpuMaterial* AssetManager::defaultMaterial() {
    if (defMaterial_) return defMaterial_;
    MaterialDesc d;
    auto up = std::make_unique<GpuMaterial>();
    defMaterial_ = up.get();
    defMaterial_->albedo = defAlbedo_;
    defMaterial_->normal = defNormal_;
    defMaterial_->metallicRoughness = defMR_;
    defMaterial_->occlusion = defWhite_;
    defMaterial_->emissive = defEmissive_;
    fillMaterialUBO(d, *defMaterial_);
    BufferDesc bd; bd.size = sizeof(MaterialUBO); bd.usage = BU_Uniform; bd.host_visible = true;
    defMaterial_->ubo = dev_->createBuffer(bd, &defMaterial_->uboData);
    // ownership: leak once into a holder to match cache lifetime
    materials_["__default__"] = std::move(up);
    return defMaterial_;
}

GpuModel* AssetManager::createMeshModel(const std::string& name, const MeshData& mesh,
                                        const MaterialDesc& material) {
    auto asset = std::make_shared<ModelAsset>();
    asset->name = name;
    asset->combined = mesh;
    asset->merged = true;
    asset->materials.push_back(material);
    MeshNode node; node.name = name;
    for (size_t i = 0; i < mesh.submeshes.size(); ++i) {
        node.meshIndices.push_back((uint32_t)i);
        ModelAsset::CombinedSub cs;
        cs.indexOffset = mesh.submeshes[i].indexOffset;
        cs.indexCount = mesh.submeshes[i].indexCount;
        cs.materialIndex = 0; cs.node = 0;
        asset->combinedSubs.push_back(cs);
    }
    asset->nodes.push_back(node);
    return uploadModel(asset, "primitive://" + name);
}

GpuModel* AssetManager::loadModel(const std::string& path) {
    auto it = models_.find(path);
    if (it != models_.end()) return it->second.get();
    std::string err;
    auto asset = loadModelFile(path, err);
    if (!asset) { GenesisPrint("[asset] load failed %s: %s\n", path.c_str(), err.c_str()); return nullptr; }
    return uploadModel(asset, path);
}

void AssetManager::loadModelAsync(const std::string& path) {
    if (models_.count(path)) return;
    for (auto& p : pending_) if (p.path == path) return;
    Pending p;
    p.path = path;
    p.future = std::async(std::launch::async, [path]() {
        std::string err;
        return loadModelFile(path, err);
    });
    pending_.push_back(std::move(p));
}

std::vector<GpuModel*> AssetManager::pollLoaded() {
    std::vector<GpuModel*> ready;
    for (auto& p : pending_) {
        using namespace std::chrono_literals;
        if (p.future.wait_for(0ms) != std::future_status::ready) continue;
        auto asset = p.future.get();
        if (asset) ready.push_back(uploadModel(asset, p.path));
    }
    pending_.erase(std::remove_if(pending_.begin(), pending_.end(),
                                  [](const Pending& p) {
                                      using namespace std::chrono_literals;
                                      return p.future.valid() == false;
                                  }),
                   pending_.end());
    return ready;
}

bool AssetManager::busy() const {
    for (auto& p : pending_) {
        using namespace std::chrono_literals;
        if (p.future.valid() && p.future.wait_for(0ms) != std::future_status::ready) return true;
    }
    return false;
}

} // namespace Genesis
