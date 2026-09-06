#include <cstdlib>
/*
 * Genesis Render - Engine
 * renderer.cpp
 */
#include "renderer.hpp"
#include "shader_embeds.hpp"
#include "rhi/glsl_preprocess.hpp"
#include "foundation_layer/log.hpp"

#include <cstring>
#include <cmath>
#include <array>

namespace Genesis
{
namespace
{
    Mat4 kCaptureViews[6];
    Mat4 kCaptureViewProj[6];

    // 36-vertex unit cube (positions only), matches cubemap face winding.
    static const float kCubeVerts[] = {
        -1.0f,-1.0f,-1.0f,  1.0f,-1.0f,-1.0f,  1.0f, 1.0f,-1.0f,  1.0f, 1.0f,-1.0f, -1.0f, 1.0f,-1.0f, -1.0f,-1.0f,-1.0f,
        -1.0f,-1.0f, 1.0f,  1.0f,-1.0f, 1.0f,  1.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f,-1.0f, 1.0f,
        -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,-1.0f, -1.0f,-1.0f,-1.0f, -1.0f,-1.0f,-1.0f, -1.0f,-1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
         1.0f, 1.0f, 1.0f,  1.0f, 1.0f,-1.0f,  1.0f,-1.0f,-1.0f,  1.0f,-1.0f,-1.0f,  1.0f,-1.0f, 1.0f,  1.0f, 1.0f, 1.0f,
        -1.0f,-1.0f,-1.0f,  1.0f,-1.0f,-1.0f,  1.0f,-1.0f, 1.0f,  1.0f,-1.0f, 1.0f, -1.0f,-1.0f, 1.0f, -1.0f,-1.0f,-1.0f,
        -1.0f, 1.0f,-1.0f,  1.0f, 1.0f,-1.0f,  1.0f, 1.0f, 1.0f,  1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f, -1.0f, 1.0f,-1.0f};

    ShaderHandle makeShader(IRHIDevice *d, const char *name, const char *vert, const char *frag)
    {
        auto resolve = [](const std::string &n) -> std::string {
            const char *s = embeddedShaderGLSL(n);
            return s ? s : "";
        };
        ShaderProgramDesc pd;
        pd.name = name;
        pd.stage_count = 2;
        ShaderStageBlob &vs = pd.stages[0];
        vs.stage = ShaderStage::Vertex;
        std::string vsrc = resolveShaderIncludes(embeddedShaderGLSL(vert), resolve);
        vs.glsl = vsrc.c_str();
        size_t bytes = 0;
        vs.spv = embeddedShaderSPV(vert, bytes);
        vs.spv_size_bytes = bytes;
        ShaderStageBlob &fs = pd.stages[1];
        fs.stage = ShaderStage::Fragment;
        std::string fsrc = resolveShaderIncludes(embeddedShaderGLSL(frag), resolve);
        fs.glsl = fsrc.c_str();
        fs.spv = embeddedShaderSPV(frag, bytes);
        fs.spv_size_bytes = bytes;
        return d->createShaderProgram(pd);
    }
}

void Renderer::init(IRHIDevice *device, AssetManager *assets)
{
    device_ = device;
    assets_ = assets;
    bool z01 = device_->caps().clip_depth_zero_to_one;
    bool ydown = device_->caps().ndc_y_down;
    Mat4 cap_proj = perspective(kHalfPi, 1.0f, 0.1f, 10.0f, z01, true);
    Vec3 o{0, 0, 0};
    kCaptureViews[0] = lookAt(o, Vec3(1, 0, 0), Vec3(0, -1, 0));
    kCaptureViews[1] = lookAt(o, Vec3(-1, 0, 0), Vec3(0, -1, 0));
    kCaptureViews[2] = lookAt(o, Vec3(0, 1, 0), Vec3(0, 0, 1));
    kCaptureViews[3] = lookAt(o, Vec3(0, -1, 0), Vec3(0, 0, -1));
    kCaptureViews[4] = lookAt(o, Vec3(0, 0, 1), Vec3(0, -1, 0));
    kCaptureViews[5] = lookAt(o, Vec3(0, 0, -1), Vec3(0, -1, 0));
    for (int i = 0; i < 6; ++i) kCaptureViewProj[i] = cap_proj * kCaptureViews[i];

    for (uint32_t i = 0; i < kFrames; ++i)
    {
        BufferDesc cb; cb.size = 256; cb.usage = BufferUsage_Uniform; cb.host_visible = true;
        camera_ubo_[i] = device_->createBuffer(cb);
        BufferDesc lb; lb.size = sizeof(LightingUBO) + 64; lb.usage = BufferUsage_Uniform; lb.host_visible = true;
        light_ubo_[i] = device_->createBuffer(lb);
    }
    BufferDesc bb; bb.size = 256; bb.usage = BufferUsage_Uniform; bb.host_visible = true;
    bake_ubo_ = device_->createBuffer(bb);

    buildCubeGeometry();
    createPipelines();
}

void Renderer::buildCubeGeometry()
{
    BufferDesc vb;
    vb.size = sizeof(kCubeVerts);
    vb.usage = BufferUsage_Vertex | BufferUsage_TransferDst;
    cube_vbo_ = device_->createBuffer(vb, kCubeVerts);
    cube_index_count_ = 36;
}

void Renderer::createPipelines()
{
    // PBR mesh pipeline
    {
        ShaderHandle sh = makeShader(device_, "pbr", "pbr.vert", "pbr.frag");
        PipelineDesc d;
        d.shader = sh;
        d.vertex_layout.stride = sizeof(ModelVertex);
        d.vertex_layout.add(0, Format::R32G32B32Float, 0)
            .add(1, Format::R32G32B32Float, 12)
            .add(3, Format::R32G32Float, 24)
            .add(2, Format::R32G32B32A32Float, 32);
        d.cull = CullMode::Back;
        d.depth_test = true; d.depth_write = true;
        d.color_formats[0] = Format::R8G8B8A8Unorm;
        d.depth_format = Format::D32Float;
        d.push_constant_size = kPushConstantSize;
        d.name = "pbr";
        pbr_pipeline_ = device_->createPipeline(d);
    }
    // Skybox + cube capture pipelines (vertex = vec3 position)
    auto cubePipeline = [&](const char *name, const char *frag, Format color, bool depth_write) {
        ShaderHandle sh = makeShader(device_, name, "cube.vert", frag);
        PipelineDesc d;
        d.shader = sh;
        d.vertex_layout.stride = 12;
        d.vertex_layout.add(0, Format::R32G32B32Float, 0);
        d.cull = CullMode::None; // cube rendered from inside; winding agnostic
        d.depth_test = true; d.depth_write = depth_write;
        d.depth_compare = CompareOp::LessEqual;
        d.color_formats[0] = color;
        d.depth_format = Format::D32Float;
        d.push_constant_size = kPushConstantSize;
        d.name = name;
        return device_->createPipeline(d);
    };
    skybox_pipeline_ = cubePipeline("skybox", "skybox.frag", Format::R8G8B8A8Unorm, false);
    equirect_pipeline_ = cubePipeline("equirect", "equirect2cube.frag", Format::R16G16B16A16Float, true);
    irradiance_pipeline_ = cubePipeline("irradiance", "irradiance.frag", Format::R16G16B16A16Float, true);
    prefilter_pipeline_ = cubePipeline("prefilter", "prefilter.frag", Format::R16G16B16A16Float, true);

    // Fullscreen pipelines
    auto fsPipeline = [&](const char *name, const char *frag, Format color, bool depth) {
        ShaderHandle sh = makeShader(device_, name, "fullscreen.vert", frag);
        PipelineDesc d;
        d.shader = sh;
        d.cull = CullMode::None;
        d.depth_test = depth; d.depth_write = depth;
        d.color_formats[0] = color;
        d.depth_format = depth ? Format::D32Float : Format::Unknown;
        d.push_constant_size = 0;
        d.name = name;
        return device_->createPipeline(d);
    };
    brdf_pipeline_ = fsPipeline("brdf", "brdf_lut.frag", Format::R16G16B16A16Float, false);
    blit_pipeline_ = fsPipeline("blit", "blit.frag", device_->swapchainFormat(), false);
}

void Renderer::renderToCubemapFace(TextureHandle target, uint32_t face, uint32_t mip,
                                  uint32_t size, PipelineHandle pipeline,
                                  DescriptorSetHandle set0, DescriptorSetHandle set1,
                                  Vec4 /*bake_params*/, bool)
{
    ColorAttachment color{};
    color.texture = target;
    color.mip = (uint16_t)mip;
    color.layer = (uint16_t)face;
    color.clear_color[0] = color.clear_color[1] = color.clear_color[2] = 0.0f;
    color.clear_color[3] = 1.0f;
    DepthAttachment depth{};
    depth.texture = size > 128 ? cube_depth_512_ : (size > 64 ? cube_depth_128_ : cube_depth_64_);
    depth.clear_depth = 1.0f;
    device_->beginPass(1, &color, &depth);
    device_->bindPipeline(pipeline);
    if (set0.valid()) device_->bindDescriptorSet(0, set0);
    if (set1.valid()) device_->bindDescriptorSet(1, set1);
    struct Push { Mat4 view_proj; Mat4 pad; } push;
    push.view_proj = kCaptureViewProj[face];
    push.pad = Mat4::identity();
    device_->pushConstants(&push, sizeof(push));
    device_->bindVertexBuffer(cube_vbo_, 0, 0);
    device_->draw(36, 1, 0);
    device_->endPass();
}

void Renderer::bakeEnvironment(TextureHandle equirect)
{
    // depth scratch targets
    auto makeDepth = [&](uint32_t s) {
        TextureDesc d;
        d.format = Format::D32Float; d.width = s; d.height = s;
        d.usage = TextureUsage_DepthStencil; d.name = "bake_depth";
        return device_->createTexture(d, nullptr);
    };
    cube_depth_512_ = makeDepth(512);
    cube_depth_128_ = makeDepth(128);
    cube_depth_64_ = makeDepth(64);

    auto makeCube = [&](uint32_t size, uint16_t mips, const char *n) {
        TextureDesc d;
        d.type = TextureType::TextureCube;
        d.format = Format::R16G16B16A16Float;
        d.width = size; d.height = size; d.mip_levels = mips; d.layers = 6;
        d.usage = TextureUsage_Sampled | TextureUsage_RenderTarget;
        d.name = n;
        return device_->createTexture(d, nullptr);
    };
    env_cube_ = makeCube(256, 1, "env_cube");
    irradiance_cube_ = makeCube(64, 1, "irradiance_cube");
    prefilter_cube_ = makeCube(128, 8, "prefilter_cube");
    {
        TextureDesc d;
        d.format = Format::R16G16B16A16Float;
        d.width = 256; d.height = 256; d.mip_levels = 1;
        d.usage = TextureUsage_Sampled | TextureUsage_RenderTarget;
        d.name = "brdf_lut";
        brdf_lut_ = device_->createTexture(d, nullptr);
    }

    // sets
    DescriptorSetDesc eq; eq.set_index = 0;
    eq.addTexture(kBindingScene2D, equirect, device_->linearSampler());
    equirect_set_ = device_->createDescriptorSet(eq);

    DescriptorSetDesc env; env.set_index = 0;
    env.addTexture(kBindingEnvCube, env_cube_, device_->cubeMipSampler());
    env_set_ = device_->createDescriptorSet(env);

    DescriptorSetDesc bake1; bake1.set_index = 1;
    bake1.addUniform(kBindingMaterial, bake_ubo_);
    bake_set1_ = device_->createDescriptorSet(bake1);

    // 1. equirect -> environment cubemap
    for (uint32_t f = 0; f < 6; ++f)
        renderToCubemapFace(env_cube_, f, 0, 256, equirect_pipeline_, equirect_set_,
                            kInvalidSet, Vec4(-1, -1, 0, 0), true);

    // 2. diffuse irradiance
    for (uint32_t f = 0; f < 6; ++f)
        renderToCubemapFace(irradiance_cube_, f, 0, 64, irradiance_pipeline_, env_set_,
                            kInvalidSet, Vec4(-1, -1, 0, 0), true);

    // 3. GGX pre-filter, one roughness per mip
    const uint32_t mips = 8;
    for (uint32_t m = 0; m < mips; ++m)
    {
        float rough = (float)m / (mips - 1);
        uint32_t size = 128 >> m;
        Vec4 params(rough, (float)(128 >> m), 0, 0);
        device_->writeBuffer(bake_ubo_, 0, &params, sizeof(params));
        for (uint32_t f = 0; f < 6; ++f)
            renderToCubemapFace(prefilter_cube_, f, m, size, prefilter_pipeline_, env_set_,
                                bake_set1_, params, true);
    }

    // 4. BRDF integration LUT
    {
        ColorAttachment color{};
        color.texture = brdf_lut_;
        color.clear_color[0] = color.clear_color[1] = color.clear_color[2] = 0;
        color.clear_color[3] = 1;
        device_->beginPass(1, &color, nullptr);
        device_->bindPipeline(brdf_pipeline_);
        device_->drawFullscreen();
        device_->endPass();
    }

    // per-frame global sets
    for (uint32_t i = 0; i < kFrames; ++i)
    {
        DescriptorSetDesc g; g.set_index = 0;
        g.addUniform(kBindingCamera, camera_ubo_[i])
         .addUniform(kBindingLighting, light_ubo_[i])
         .addTexture(kBindingIrradiance, irradiance_cube_, device_->cubeMipSampler())
         .addTexture(kBindingPrefilter, prefilter_cube_, device_->cubeMipSampler())
         .addTexture(kBindingBrdfLUT, brdf_lut_, device_->linearSampler())
         .addTexture(kBindingEnvCube, env_cube_, device_->cubeMipSampler());
        global_set_[i] = device_->createDescriptorSet(g);
    }
    // blit set (scene color -> swapchain)
    DescriptorSetDesc bl; bl.set_index = 0;
    SamplerHandle clamp_linear = device_->linearSampler();
    bl.addTexture(kBindingScene2D, device_->sceneColor(), clamp_linear);
    blit_set_ = device_->createDescriptorSet(bl);

    GenesisPrint("[Renderer] IBL baked: env 256^2, irradiance 64^2, prefilter 128^2/8 mips, BRDF 256^2\n");
}

void Renderer::renderScene(Scene &scene, const CameraComponent &camera,
                           const RenderSettings &settings, uint32_t width, uint32_t height,
                           float time)
{
    (void)time;
    const DeviceCaps &caps = device_->caps();
    uint32_t fi = device_->currentFrameIndex();

    // ---------------- camera
       float aspect = (float)width / std::max(1u, height);
    Vec3 forward{std::cos(camera.pitch) * std::sin(camera.yaw),
                 std::sin(camera.pitch),
                 std::cos(camera.pitch) * std::cos(camera.yaw)};
    Vec3 eye = camera.target + forward * camera.distance;
    Mat4 view = lookAt(eye, camera.target, Vec3(0, 1, 0));
    Mat4 proj = camera.orthographic
                    ? ortho(-camera.ortho_size * aspect, camera.ortho_size * aspect,
                            -camera.ortho_size, camera.ortho_size, camera.near_z, camera.far_z,
                            caps.clip_depth_zero_to_one)
                    : perspective(radians(camera.fov_y), aspect, camera.near_z, camera.far_z,
                                  caps.clip_depth_zero_to_one, true);
    Mat4 view_proj = proj * view;
    CameraUBO cubo;
    cubo.view = view; cubo.proj = proj; cubo.view_proj = view_proj;
    cubo.camera_pos = Vec4(eye, 1);
    device_->writeBuffer(camera_ubo_[fi], 0, &cubo, sizeof(cubo));

    // ---------------- lights
    LightingUBO lu{};
    lu.dir_direction = Vec4(0, -1, 0, 0);
    lu.dir_color = Vec4(1, 1, 1, 3);
    lu.flags = Vec4(settings.direct_lighting ? 1.f : 0.f,
                    settings.image_based_lighting ? 1.f : 0.f,
                    (float)settings.direct_model, settings.env_intensity);
    lu.ambient = Vec4(settings.ambient.x, settings.ambient.y, settings.ambient.z, 0);
    bool has_dir = false;
    scene.view<TransformComponent, DirectionalLightComponent>(
        [&](Entity, TransformComponent &t, DirectionalLightComponent &l) {
            if (has_dir) return;
            Vec3 d = quatRotate(t.rotation, Vec3(0, -1, 0));
            lu.dir_direction = Vec4(d.x, d.y, d.z, 1);
            lu.dir_color = Vec4(l.color.x, l.color.y, l.color.z, l.intensity);
            has_dir = true;
        });
    uint32_t pc = 0;
    scene.view<TransformComponent, PointLightComponent>(
        [&](Entity, TransformComponent &t, PointLightComponent &l) {
            if (pc >= kMaxPointLights) return;
            Vec3 wp = Vec3(t.world.m[12], t.world.m[13], t.world.m[14]);
            lu.points[pc].position_range = Vec4(wp, l.range);
            lu.points[pc].color_intensity = Vec4(l.color, l.intensity);
            ++pc;
        });
    lu.point_count = Vec4((float)pc, 0, 0, 0);
    device_->writeBuffer(light_ubo_[fi], 0, &lu, sizeof(lu));

    // ---------------- scene pass
    ColorAttachment color{};
    color.texture = device_->sceneColor();
    color.clear_color[0] = 0.02f; color.clear_color[1] = 0.02f; color.clear_color[2] = 0.03f;
    color.clear_color[3] = 1.0f;
    DepthAttachment depth{};
    depth.texture = device_->sceneDepth();
    depth.clear_depth = 1.0f;
    device_->beginPass(1, &color, &depth);
    // skybox
    if (settings.show_skybox && env_cube_.valid())
    {
        Mat4 rot_view = view;
        rot_view.m[12] = rot_view.m[13] = rot_view.m[14] = 0;
        device_->bindPipeline(skybox_pipeline_);
        device_->bindDescriptorSet(0, global_set_[fi]);
        struct Push { Mat4 view_proj; Mat4 pad; } push{proj * rot_view, Mat4::identity()};
        device_->pushConstants(&push, sizeof(push));
        device_->bindVertexBuffer(cube_vbo_);
        device_->draw(36);
    }

    // opaque meshes
    device_->bindPipeline(pbr_pipeline_);
    device_->bindDescriptorSet(0, global_set_[fi]);
    scene.view<TransformComponent, MeshComponent>(
        [&](Entity, TransformComponent &t, MeshComponent &mc) {
            if (!mc.model || !mc.visible) return;
            struct Push { Mat4 model; Mat4 normal_matrix; } push;
            push.model = t.world;
            push.normal_matrix = t.normal_matrix;
            device_->pushConstants(&push, sizeof(push));
            for (const GPUMesh &gm : mc.model->meshes)
            {
                for (const SubMesh &sm : gm.submeshes)
                {
                    uint32_t mi = std::min(sm.material_index,
                                           (uint32_t)mc.model->materials.size() - 1);
                    auto &mat = mc.model->materials[mi];
                    if (mat->dirty) assets_->syncMaterial(*mat);
                    if (mat->set.valid()) device_->bindDescriptorSet(1, mat->set);
                    device_->bindVertexBuffer(gm.vbo);
                    device_->bindIndexBuffer(gm.ibo, gm.index_type);
                    device_->drawIndexed(sm.index_count, 1, sm.index_offset / 4, 0);
                }
            }
        });
    device_->endPass();

    // refresh the blit set (texture may be recreated on resize)
    DescriptorSetDesc bl; bl.set_index = 0;
    bl.addTexture(kBindingScene2D, device_->sceneColor(), device_->linearSampler());
    if (blit_set_.valid()) device_->updateDescriptorSet(blit_set_, bl);
}

void Renderer::drawBlit()
{
    if (!blit_set_.valid()) return;
    device_->bindPipeline(blit_pipeline_);
    device_->bindDescriptorSet(0, blit_set_);
    device_->drawFullscreen();
}

void Renderer::shutdown()
{
    // RHI owns all GPU resource lifetimes for this demo; nothing manual here.
}
}
