/*
 * Genesis Render - runtime Image-Based Lighting baker.
 *
 * Equirect HDR  ->  env cube (mip 0)
 *               ->  prefiltered roughness mip chain (GGX importance)
 *               ->  diffuse irradiance cube
 *               ->  BRDF integration LUT (RG16F)
 *
 * Everything renders into the same command buffer as the first scene frame.
 */
#include "render/pbr_renderer.hpp"
#include "foundation_layer/log.hpp"

#include <cmath>
#include <cstring>
#include "render/cpu_ibl.hpp"
#include <cstdlib>

namespace Genesis {

namespace {
struct FaceBasis { Vec3 target, up; };
const FaceBasis kFaces[6] = {
    {{ 1, 0, 0}, {0, 1, 0}}, // +X
    {{-1, 0, 0}, {0, 1, 0}}, // -X
    {{ 0, 1, 0}, {0, 0,-1}}, // +Y
    {{ 0,-1, 0}, {0, 0, 1}}, // -Y
    {{ 0, 0, 1}, {0, 1, 0}}, // +Z
    {{ 0, 0,-1}, {0, 1, 0}}, // -Z
};

// 90 deg fov cube projection. y stays positive so cube content is identical
// across GL (z [-1,1]) and Vulkan (z [0,1]); only depth mapping differs.
Mat4 cubeProjection(bool vulkanClip, float nearZ = 0.1f, float farZ = 10.0f) {
    Mat4 r(0.0f);
    r.at(0, 0) = 1.0f;
    r.at(1, 1) = 1.0f;
    if (vulkanClip) {
        r.at(2, 2) = farZ / (nearZ - farZ);
        r.at(2, 3) = -1.0f;
        r.at(3, 2) = (nearZ * farZ) / (nearZ - farZ);
    } else {
        r.at(2, 2) = (farZ + nearZ) / (nearZ - farZ);
        r.at(2, 3) = -1.0f;
        r.at(3, 2) = (2.0f * nearZ * farZ) / (nearZ - farZ);
    }
    return r;
}
} // namespace

std::vector<float> makeEnvironmentEquirect(uint32_t w, uint32_t h) {
    std::vector<float> data((size_t)w * h * 4, 0.0f);
    const Vec3 sunDir = normalize(Vec3{-0.45f, 0.55f, -0.35f});
    for (uint32_t y = 0; y < h; ++y) {
        float v = (float)y / (h - 1);
        float elev = (0.5f - v) * kPi; // zenith at top
        for (uint32_t x = 0; x < w; ++x) {
            float u = (float)x / (w - 1);
            float azim = u * 2.0f * kPi;
            Vec3 dir{std::cos(elev) * std::sin(azim), std::sin(elev),
                     std::cos(elev) * std::cos(azim)};
            Vec3 col;
            if (elev >= 0.0f) {
                float t = std::pow(std::clamp(elev / (kPi * 0.5f), 0.0f, 1.0f), 0.65f);
                col = lerp(Vec3{0.62f, 0.72f, 0.85f}, Vec3{0.12f, 0.28f, 0.62f}, t);
            } else {
                float t = std::clamp(-elev / (kPi * 0.5f), 0.0f, 1.0f);
                col = lerp(Vec3{0.55f, 0.55f, 0.50f}, Vec3{0.12f, 0.10f, 0.08f}, t);
            }
            float sun = std::pow(std::max(dot(dir, sunDir), 0.0f), 900.0f) * 12.0f;
            float halo = std::pow(std::max(dot(dir, sunDir), 0.0f), 8.0f) * 0.35f;
            col += Vec3{1.0f, 0.92f, 0.75f} * (sun + halo);
            size_t o = ((size_t)y * w + x) * 4;
            data[o] = col.x; data[o+1] = col.y; data[o+2] = col.z; data[o+3] = 1.0f;
        }
    }
    return data;
}

void PBRRenderer::bake() {
    using namespace genesis::cpuibl;
    if (!equirect_) {
        if (equirectCpu_.empty()) {
            equirectCpu_ = makeEnvironmentEquirect(1024, 512);
            eqW_ = 1024; eqH_ = 512;
        }
        TextureDesc d;
        d.width=eqW_; d.height=eqH_; d.mips=1; d.format=RHIFormat::RGBA16F;
        d.usage=TU_Sampled; d.wrap=RHIWrap::ClampEdge;
        d.filter_min=d.filter_mag=RHIFilter::Linear;
        equirect_=dev_->createTexture(d);
        equirect_->uploadRGBA32F(equirectCpu_.data());
        auto mkAtlas = [&](uint32_t S, uint32_t mips, RHIFilter f){
            TextureDesc t; t.width=6*S; t.height=S; t.mips=1;
            t.format=RHIFormat::RGBA16F; t.usage=TU_RenderTarget|TU_Sampled;
            t.filter_min=f; t.filter_mag=RHIFilter::Linear; t.wrap=RHIWrap::ClampEdge;
            return dev_->createTexture(t);
        };
        envAtlas_ = mkAtlas(ENV_SIZE, 1, RHIFilter::Linear);
        prefAtlas_ = mkAtlas(ENV_SIZE, PREF_MIPS, RHIFilter::Linear);
        irrAtlas_ = mkAtlas(IRR_SIZE, 1, RHIFilter::Linear);
        TextureDesc l; l.width=LUT_SIZE; l.height=LUT_SIZE; l.mips=1;
        l.format=RHIFormat::RG16F; l.usage=TU_RenderTarget|TU_Sampled;
        l.filter_min=l.filter_mag=RHIFilter::Nearest; l.wrap=RHIWrap::ClampEdge;
        brdfLUT_=dev_->createTexture(l);
    }
    const float* eq=equirectCpu_.data();
    auto env=buildEnvFaces(eq,eqW_,eqH_,ENV_SIZE);
    auto irr=buildIrrFaces(eq,eqW_,eqH_,IRR_SIZE);
    {
        const float* c=irr.data()+((2*IRR_SIZE*IRR_SIZE)+(IRR_SIZE/2*IRR_SIZE+IRR_SIZE/2))*4;
        fprintf(stderr,"[cpu-irr] face2 center rgb=%.2f,%.2f,%.2f size=%zu\n",c[0],c[1],c[2],irr.size()/4);
    }
    auto pref=buildPrefilter(eq,eqW_,eqH_,ENV_SIZE,PREF_MIPS,32);
    auto brdf=buildBRDFLUT(LUT_SIZE,64);
    envAtlas_->uploadRGBA16F(env.data(),0);
    irrAtlas_->uploadRGBA16F(irr.data(),0);
    // single-mip atlas: upload the roughness-0 (mip 0) face set
    prefAtlas_->uploadRGBA16F(pref.data(), 0);
    brdfLUT_->uploadRGBA16F(brdf.data(),0);
    skySet_->setTexture(BindingTable::EnvAtlas, envAtlas_);
    baked_=true;
    GenesisPrint("[ibl] CPU bake complete (env %u, irr %u, lut %u)\n", ENV_SIZE, IRR_SIZE, LUT_SIZE);
}

} // namespace Genesis
