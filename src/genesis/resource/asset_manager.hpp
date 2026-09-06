/*
 * Genesis Render - Engine
 * asset_manager.hpp : owns GPU model / texture / material resources and caches
 *                     them by path on top of the RHI.
 */
#pragma once

#include "model.hpp"
#include "image.hpp"
#include "rhi/rhi.hpp"
#include <unordered_map>
#include <memory>

namespace Genesis
{
    class AssetManager
    {
    public:
        void init(IRHIDevice *device);
        void shutdown();

        IRHIDevice *device() const { return device_; }
        bool flipY() const { return !device_->caps().ndc_y_down; }

        // Textures
        TextureHandle loadTexture2D(const std::string &path, bool srgb,
                                    bool generate_mips = true);
        TextureHandle textureFromImage(const CPUImage &img, bool srgb,
                                       bool generate_mips = true);
        TextureHandle equirectFromHDR(const std::string &path);
        TextureHandle proceduralEnvironment(uint32_t w = 1024, uint32_t h = 512);

        TextureHandle defaultAlbedo() const { return default_albedo_; }
        TextureHandle defaultNormal() const { return default_normal_; }
        TextureHandle defaultORM() const { return default_orm_; }
        TextureHandle defaultEmissive() const { return default_emissive_; }

        // Models
        std::shared_ptr<ModelAsset> loadModel(const std::string &path);
        std::shared_ptr<ModelAsset> uploadModel(const CPUModel &cpu, const std::string &name);

        // Re-upload material UBO data after UI/script changes.
        void syncMaterial(PBRMaterial &m);

    private:
        TextureHandle createTextureRGBA8(int w, int h, const uint8_t *rgba, bool srgb,
                                         uint16_t mips, bool flip);
        TextureHandle createTextureRGBA32F(int w, int h, const float *rgba, uint16_t mips);
        TextureHandle embeddedOrPathTexture(const CPUMaterial::Embedded &emb,
                                            const std::string &path, bool srgb);

        IRHIDevice *device_ = nullptr;
        std::unordered_map<std::string, TextureHandle> texture_cache_;
        std::unordered_map<std::string, std::shared_ptr<ModelAsset>> model_cache_;
        TextureHandle default_albedo_, default_normal_, default_orm_, default_emissive_;
        std::vector<std::shared_ptr<PBRMaterial>> material_gc_;
    };
}
