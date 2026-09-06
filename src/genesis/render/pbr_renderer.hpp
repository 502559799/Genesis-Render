/*
 * Genesis Render - forward PBR renderer + runtime IBL baker.
 *
 * Per frame:
 *   scene HDR pass (RGBA16F + depth): skybox cube, PBR meshes
 *   (UI overlay pass, load)
 *   tonemap/post pass to the swapchain/offscreen LDR target
 *
 * Indirect lighting (cubemap, diffuseIrradiance, prefiltered env, BRDF LUT)
 * is generated on the GPU at load time from an equirectangular HDR source.
 */
#pragma once

#include "rhi/rhi.hpp"
#include "render/shader_library.hpp"
#include "ecs/scene.hpp"
#include "resource/asset_manager.hpp"
#include <functional>
#include <map>

namespace Genesis {

// Match pbr.frag FrameBlock (std140)
struct FrameUBO {
    Mat4 viewProj;            // 64
    Vec4 cameraPos;           // 16
    Vec4 dirLightDir;         // 16
    Vec4 dirLightColor;       // 16
    Vec4 params;              // 16 exposure, iblStrength, time, lightingModel
    Vec4 pointPos[4];         // 64
    Vec4 pointData[4];        // 64
    Vec4 misc;                // 16 pointCount, directOn, iblOn, envRot
};
static_assert(sizeof(FrameUBO) == 272, "");

struct DrawUBO {
    Mat4 model;
    Mat4 normalMatrix;
    Vec4 tint;
};
static_assert(sizeof(DrawUBO) == 144, "");

struct PostUBO {
    Vec4 params; // exposure, tonemap mode, gamma, unused
};

class PBRRenderer {
public:
    PBRRenderer() = default;
    ~PBRRenderer();

    void init(IDevice* device, ShaderLibrary* shaders, uint32_t width, uint32_t height);
    void onResize(uint32_t w, uint32_t h);

    // CPU RGBA32F equirect source -> env/irradiance/prefilter/BRDF GPU textures.
    void bakeEnvironment(const std::vector<float>& equirect, uint32_t eqW, uint32_t eqH);
    void generateProceduralEnvironment();
    bool isBaked() const { return baked_; }

    // Full frame. uiCallback (optional) issues ImGui draw data inside an
    // overlay pass on the final target.
    void renderFrame(Scene& scene, const std::function<void()>& uiCallback = {});

    std::vector<uint8_t> screenshotRGBA8() const;
    std::vector<uint8_t> dumpSceneColor() { return dev_->readTextureRGBA8(sceneColor_); }
    std::vector<uint8_t> dumpEnvAtlas() { return dev_->readTextureRGBA8(envAtlas_,0); }
    std::vector<uint8_t> dumpIrrAtlas() { return dev_->readTextureRGBA8(irrAtlas_,0); }
    std::vector<uint8_t> dumpPrefAtlas() { return dev_->readTextureRGBA8(prefAtlas_,0); }
    std::vector<uint8_t> dumpEnvFace(int face) {
        return dev_->readTextureRGBA8(envAtlas_, 0);
    }
    std::vector<uint8_t> dumpEquirect() { return dev_->readTextureRGBA8(equirect_, 0); }

    // accessors for the editor UI
    ITexture* brdfLUT() const { return brdfLUT_; }
    ITexture* irradiance() const { return irrAtlas_; }
    ITexture* prefilter() const { return prefAtlas_; }
    ITexture* envAtlas() const { return envAtlas_; }

    uint32_t drawCalls = 0;
    uint32_t triangles = 0;

private:
    void createTargets();
    void createPipelines();
    void ensureSize(uint32_t w, uint32_t h);
    size_t ringAlloc(size_t bytes);
    void bake();
    void drawBakedCubes();

    IDevice* dev_ = nullptr;
    ShaderLibrary* shaders_ = nullptr;
    uint32_t width_ = 1, height_ = 1;

    // HDR scene targets
    ITexture* sceneColor_ = nullptr;
    ITexture* sceneDepth_ = nullptr;
    IFrameBuffer* sceneFBO_ = nullptr;

    // IBL
    static constexpr uint32_t ENV_SIZE = 256;
    static constexpr uint32_t PREF_MIPS = 9;
    static constexpr uint32_t IRR_SIZE = 32;
    static constexpr uint32_t LUT_SIZE = 256;
    ITexture* envAtlas_ = nullptr;
    ITexture* prefAtlas_ = nullptr;
    ITexture* irrAtlas_ = nullptr;
    ITexture* brdfLUT_ = nullptr;
    ITexture* equirect_ = nullptr;
    std::vector<float> equirectCpu_;
    uint32_t eqW_ = 0, eqH_ = 0;
    bool baked_ = false;

    // skybox/capture cube
    IBuffer* cubeVbo_ = nullptr;
    IBuffer* cubeIbo_ = nullptr;

    // layouts
    IBindingSetLayout *pbrLayout_ = nullptr, *skyLayout_ = nullptr;
    IBindingSetLayout *cubeSampleLayout_ = nullptr, *equirectLayout_ = nullptr;
    IBindingSetLayout *emptyLayout_ = nullptr, *postLayout_ = nullptr;
    // pipelines
    IPipeline *pbrPipe_ = nullptr, *skyPipe_ = nullptr;
    IPipeline *equirectPipe_ = nullptr, *irrPipe_ = nullptr, *prefilterPipe_ = nullptr;
    IPipeline *brdfPipe_ = nullptr, *postPipe_ = nullptr;
    // sets
    IBindingSet *skySet_ = nullptr, *postSet_ = nullptr;
    IBindingSet *equirectSet_ = nullptr, *cubeSampleSet_ = nullptr, *brdfSet_ = nullptr;
    std::unordered_map<uint64_t, IBindingSet*> drawSets_;

    // independent uniform buffers (deterministic on software drivers)
    IBuffer* frameUbo_ = nullptr;
    IBuffer* postUbo_ = nullptr;
    std::map<uint64_t, IBuffer*> drawUbos_;
    size_t uboAlign_ = 16;
    uint64_t frameIndex_ = 0;
};

// Procedural equirect sky (returns RGBA32F).
std::vector<float> makeEnvironmentEquirect(uint32_t w, uint32_t h);

} // namespace Genesis
