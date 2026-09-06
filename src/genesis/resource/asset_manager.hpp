/*
 * Genesis Render - AssetManager turns CPU ModelAssets into GPU resources,
 * owns texture/material caches and provides background model loading.
 */
#pragma once

#include "rhi/rhi.hpp"
#include "resource/model_loader.hpp"
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis {

// std140 UBO for binding 2 (keep in sync with pbr.frag)
struct MaterialUBO {
    float baseColor[4] = {1,1,1,1};
    float pbr[4] = {0.0f, 1.0f, 1.0f, 1.0f}; // metallic, roughness, normalScale, ao
    float emissive[4] = {0,0,0,0};
    float texMask[4] = {0,0,0,0};             // albedo, normal, mr, ao present
    float texMask2[4] = {0,0,0,0};            // emissive present, doubleSided, -
};
static_assert(sizeof(MaterialUBO) == 80, "");

struct GpuMaterial {
    MaterialUBO uboData;
    IBuffer* ubo = nullptr;
    ITexture* albedo = nullptr;
    ITexture* normal = nullptr;
    ITexture* metallicRoughness = nullptr;
    ITexture* occlusion = nullptr;
    ITexture* emissive = nullptr;
    bool doubleSided = false;
    std::string name;
};

struct GpuNodeDraw {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t node = 0;
    GpuMaterial* material = nullptr;
};

struct GpuModel {
    std::shared_ptr<ModelAsset> source;
    IBuffer* vertexBuffer = nullptr;
    IBuffer* indexBuffer = nullptr;
    uint32_t indexCount = 0;
    std::vector<GpuNodeDraw> draws;
    std::vector<GpuMaterial*> materials;
    Vec3 boundsMin{0}, boundsMax{0};
};

class AssetManager {
public:
    explicit AssetManager(IDevice* device);
    ~AssetManager();

    GpuModel* loadModel(const std::string& path);
    // Wrap procedural MeshData (already built / merged) as a GPU model.
    GpuModel* createMeshModel(const std::string& name, const MeshData& mesh,
                              const MaterialDesc& material = {});
    ITexture* loadTexture(const std::string& path, bool srgb);

    // Async: parse/CPU-decode off-thread; GPU upload happens in pollLoaded()
    // (must be called on the render thread between frames).
    void loadModelAsync(const std::string& path);
    std::vector<GpuModel*> pollLoaded();
    bool busy() const;

    GpuMaterial* defaultMaterial();
    ITexture* defaultAlbedo() { return defAlbedo_; }

private:
    ITexture* resolveImage(const std::string& uri, bool srgb, ModelAsset* owner);
    GpuMaterial* buildMaterial(const MaterialDesc& d, ModelAsset* owner);
    GpuModel* uploadModel(std::shared_ptr<ModelAsset> asset, const std::string& key);

    IDevice* dev_;
    std::unordered_map<std::string, ITexture*> textures_;
    std::unordered_map<std::string, std::unique_ptr<GpuMaterial>> materials_;
    std::unordered_map<std::string, std::unique_ptr<GpuModel>> models_;

    ITexture *defAlbedo_, *defNormal_, *defMR_, *defWhite_, *defEmissive_;
    GpuMaterial* defMaterial_ = nullptr;

    struct Pending {
        std::string path;
        std::future<std::shared_ptr<ModelAsset>> future;
        std::string err;
    };
    std::vector<Pending> pending_;
};

} // namespace Genesis
