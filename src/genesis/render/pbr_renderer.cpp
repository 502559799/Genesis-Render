/*
 * Genesis Render - PBR forward renderer implementation.
 */
#include "render/pbr_renderer.hpp"
#include "render/primitives.hpp"
#include "foundation_layer/log.hpp"

#include <cstring>
#include <cstdio>
#include <cstdint>
#include <cstdio>

namespace Genesis {

// defined in ibl_baker.cpp
void bakeIBL(PBRRenderer& r);

static VertexLayout kPosLayout = {
    sizeof(Vertex),
    {{0, 0,  RHIAttrib::Float3}},
};
static VertexLayout kMeshLayout = {
    sizeof(Vertex),
    {{0, 0,  RHIAttrib::Float3},
     {1, 12, RHIAttrib::Float3},
     {2, 24, RHIAttrib::Float4},
     {3, 40, RHIAttrib::Float2}},
};

PBRRenderer::~PBRRenderer() = default;

void PBRRenderer::init(IDevice* device, ShaderLibrary* shaders, uint32_t width, uint32_t height) {
    dev_ = device;
    shaders_ = shaders;
    width_ = width; height_ = height;
    uboAlign_ = (uint32_t)(uintptr_t)dev_->nativeHandle("ubo_alignment");
    if (uboAlign_ < 16) uboAlign_ = 16;

    BufferDesc fd;
    fd.size = sizeof(FrameUBO); fd.usage = BU_Uniform; fd.host_visible = true;
    fd.name = "frame";
    frameUbo_ = dev_->createBuffer(fd, nullptr);

    // ---- binding layouts ----
    auto faceDecls = [](uint32_t base){
        std::vector<BindingDecl> d;
        for (uint32_t i = 0; i < 6; ++i) d.push_back({base + i, BindingType::Texture2D});
        return d;
    };
    {
        std::vector<BindingDecl> d = {
            {BindingTable::Global, BindingType::UBO},
            {BindingTable::Draw, BindingType::UBO},
            {BindingTable::Material, BindingType::UBO},
            {BindingTable::IrrAtlas, BindingType::Texture2D},
            {BindingTable::PrefAtlas, BindingType::Texture2D},
            {BindingTable::BrdfLUT, BindingType::Texture2D},
            {BindingTable::AlbedoTex, BindingType::Texture2D},
            {BindingTable::NormalTex, BindingType::Texture2D},
            {BindingTable::MetallicRoughTex, BindingType::Texture2D},
            {BindingTable::AOTex, BindingType::Texture2D},
            {BindingTable::EmissiveTex, BindingType::Texture2D},
        };
        pbrLayout_ = dev_->createBindingSetLayout(d);
    }
    skyLayout_ = dev_->createBindingSetLayout({
        {BindingTable::Global, BindingType::UBO},
        {BindingTable::EnvAtlas, BindingType::Texture2D},
    });
    cubeSampleLayout_ = dev_->createBindingSetLayout({
        {BindingTable::Global, BindingType::UBO},
        {BindingTable::EnvAtlas, BindingType::Texture2D},
    });
        equirectLayout_ = dev_->createBindingSetLayout({
        {BindingTable::Global, BindingType::UBO},
        {BindingTable::EquirectTex, BindingType::Texture2D},
    });
    emptyLayout_ = dev_->createBindingSetLayout({});
    postLayout_ = dev_->createBindingSetLayout({
        {BindingTable::Global, BindingType::UBO},
        {BindingTable::SceneColorTex, BindingType::Texture2D},
    });

    createTargets();
    createPipelines();
}

void PBRRenderer::createTargets() {
    // HDR scene
    TextureDesc cd;
    cd.width = width_; cd.height = height_;
    cd.format = RHIFormat::RGBA16F;
    cd.usage = TU_RenderTarget | TU_Sampled;
    cd.wrap = RHIWrap::ClampEdge;
    sceneColor_ = dev_->createTexture(cd);

    TextureDesc dd;
    dd.width = width_; dd.height = height_;
    dd.format = RHIFormat::D32F;
    dd.usage = TU_Depth | TU_Sampled;
    sceneDepth_ = dev_->createTexture(dd);

    FrameBufferDesc fd;
    fd.width = width_; fd.height = height_;
    fd.colors.push_back({sceneColor_, 0, 0});
    //fd.depth = sceneDepth_;
    sceneFBO_ = dev_->createFrameBuffer(fd);
}

void PBRRenderer::createPipelines() {
    // capture cube geometry
    MeshData cube; buildCube(cube);
    BufferDesc vd; vd.size = cube.vertices.size() * sizeof(Vertex);
    vd.usage = BU_Vertex; vd.host_visible = true;
    cubeVbo_ = dev_->createBuffer(vd, cube.vertices.data());
    BufferDesc id; id.size = cube.indices.size() * sizeof(uint32_t);
    id.usage = BU_Index; id.host_visible = true;
    cubeIbo_ = dev_->createBuffer(id, cube.indices.data());

    // PBR forward
    PipelineDesc pd;
    pd.program = shaders_->load("pbr");
    pd.vertex_layout = kMeshLayout;
    pd.cull = RHICull::None;
    pd.depth_test = true; pd.depth_write = true; pd.depth_func = RHICompare::LEqual;
    pd.color_formats = {RHIFormat::RGBA16F};
    pd.depth_format = RHIFormat::D32F;
    pd.binding_layout = pbrLayout_;
    pbrPipe_ = dev_->createPipeline(pd); 

    // skybox
    PipelineDesc sd;
    sd.program = shaders_->load("skybox");
    sd.vertex_layout = VertexLayout{};
    sd.cull = RHICull::None;
    sd.depth_test = false; sd.depth_write = false; sd.depth_func = RHICompare::LEqual;
    sd.color_formats = {RHIFormat::RGBA16F};
    sd.depth_format = RHIFormat::D32F;
    sd.binding_layout = skyLayout_;
    skyPipe_ = dev_->createPipeline(sd); 

    // equirect -> cube
    PipelineDesc ed;
    ed.program = shaders_->load("equirect2cube");
    ed.vertex_layout = kMeshLayout;
    ed.depth_test = false; ed.depth_write = false; ed.depth_func = RHICompare::LEqual;
    ed.color_formats = {RHIFormat::RGBA16F};
    ed.depth_format = RHIFormat::D32F;
    ed.binding_layout = equirectLayout_;
    equirectPipe_ = dev_->createPipeline(ed); 

    // irradiance
    PipelineDesc ir;
    ir.program = shaders_->load("irradiance");
    ir.vertex_layout = kMeshLayout;
    ir.depth_test = false; ir.depth_write = false; ir.depth_func = RHICompare::LEqual;
    ir.color_formats = {RHIFormat::RGBA16F};
    ir.depth_format = RHIFormat::D32F;
    ir.binding_layout = cubeSampleLayout_;
    irrPipe_ = dev_->createPipeline(ir); 

    // prefilter
    PipelineDesc pf;
    pf.program = shaders_->load("prefilter");
    pf.vertex_layout = kMeshLayout;
    pf.depth_test = false; pf.depth_write = false; pf.depth_func = RHICompare::LEqual;
    pf.color_formats = {RHIFormat::RGBA16F};
    pf.depth_format = RHIFormat::D32F;
    pf.binding_layout = cubeSampleLayout_;
    prefilterPipe_ = dev_->createPipeline(pf); 

    // brdf lut (no vertex input)
    PipelineDesc bd;
    bd.program = shaders_->load("brdf");
    bd.color_formats = {RHIFormat::RG16F};
    bd.binding_layout = emptyLayout_;
    brdfPipe_ = dev_->createPipeline(bd); 

    // post
    PipelineDesc po;
    po.program = shaders_->load("post");
    po.color_formats = {dev_->screenFormat()};
    po.binding_layout = postLayout_;
    postPipe_ = dev_->createPipeline(po); 

    // static binding sets
    skySet_ = dev_->createBindingSet(skyLayout_);
    postSet_ = dev_->createBindingSet(postLayout_);
    equirectSet_ = dev_->createBindingSet(equirectLayout_);
    cubeSampleSet_ = dev_->createBindingSet(cubeSampleLayout_);
    brdfSet_ = dev_->createBindingSet(emptyLayout_);
    // force software drivers (lavapipe) to finish async pipeline compilation
    dev_->waitIdle();
}

void PBRRenderer::ensureSize(uint32_t w, uint32_t h) {
    if (w == width_ && h == height_) return;
    width_ = w; height_ = h;
    dev_->waitIdle();
    // resources are process-lifetime in this build; just recreate targets.
    TextureDesc cd;
    cd.width = w; cd.height = h; cd.format = RHIFormat::RGBA16F;
    cd.usage = TU_RenderTarget | TU_Sampled; cd.wrap = RHIWrap::ClampEdge;
    sceneColor_ = dev_->createTexture(cd);
    TextureDesc dd;
    dd.width = w; dd.height = h; dd.format = RHIFormat::D32F; dd.usage = TU_Depth | TU_Sampled;
    sceneDepth_ = dev_->createTexture(dd);
    FrameBufferDesc fd; fd.width = w; fd.height = h;
    fd.colors.push_back({sceneColor_, 0, 0}); fd.depth = sceneDepth_;
    sceneFBO_ = dev_->createFrameBuffer(fd);
    drawSets_.clear();
}

void PBRRenderer::onResize(uint32_t w, uint32_t h) { ensureSize(w, h); }

void PBRRenderer::generateProceduralEnvironment() {
    auto px = makeEnvironmentEquirect(1024, 512);
    bakeEnvironment(px, 1024, 512);
}

void PBRRenderer::bakeEnvironment(const std::vector<float>& data, uint32_t w, uint32_t h) {
    equirectCpu_ = data; eqW_ = w; eqH_ = h;
    baked_ = false;
}

std::vector<uint8_t> PBRRenderer::screenshotRGBA8() const {
    ITexture* t = dev_->isHeadless() ? dev_->screenColorTexture() : sceneColor_;
    return const_cast<IDevice*>(dev_)->readTextureRGBA8(t);
}

void PBRRenderer::renderFrame(Scene& scene, const std::function<void()>& uiCallback) {
    if (!dev_->beginFrame()) { dev_->endFrame(); return; }
    drawCalls = triangles = 0;
    if (!baked_) bake();
    IRenderContext* rc = dev_->context();

    // ---- frame UBO (independent buffer) ----
    float aspect = (float)width_ / (float)height_;
    Mat4 proj = scene.projectionMatrix(aspect, dev_->vulkanClip());
    Mat4 view = scene.viewMatrix();
    FrameUBO frame{};
    frame.viewProj = proj * view;
    Vec3 eye = scene.cameraPosition();
    frame.cameraPos = Vec4{eye.x, eye.y, eye.z, 1};
    frame.dirLightDir = Vec4{scene.settings.sun.direction.x, scene.settings.sun.direction.y,
                             scene.settings.sun.direction.z, scene.settings.sun.enabled ? 1.f : 0.f};
    frame.dirLightColor = Vec4{scene.settings.sun.color.x, scene.settings.sun.color.y,
                               scene.settings.sun.color.z, scene.settings.sun.intensity};
    frame.params = Vec4{scene.settings.exposure, scene.settings.iblIntensity,
                        (float)frameIndex_ * 0.016f, (float)scene.settings.lightingModel};
    frame.misc = Vec4{0, scene.settings.directLightEnabled ? 1.f : 0.f,
                      scene.settings.iblEnabled ? 1.f : 0.f, scene.settings.envRotation};
    int pointCount = 0;
    scene.registry.view<TransformComponent, LightComponent>([&](Entity e) {
        auto* t = scene.registry.get<TransformComponent>(e);
        auto* l = scene.registry.get<LightComponent>(e);
        if (!l->enabled || l->type != LightType::Point || pointCount >= 4 || !t) return;
        frame.pointPos[pointCount] = Vec4{t->world.m[12], t->world.m[13], t->world.m[14], 0};
        frame.pointData[pointCount] = Vec4{l->color.x*l->intensity, l->color.y*l->intensity,
                                           l->color.z*l->intensity, l->range};
        pointCount++;
    });
    frame.misc.x = (float)pointCount;
    frameUbo_->upload(0, sizeof(frame), &frame);

    ClearValue clear;
    clear.color[0]=0.02f; clear.color[1]=0.02f; clear.color[2]=0.03f;

    // ================= HDR scene pass =================
    rc->beginPass(sceneFBO_, clear, true, true);

    if (scene.settings.skyboxEnabled && scene.settings.iblEnabled) {
        rc->bindPipeline(skyPipe_);
        skySet_->setTexture(BindingTable::EquirectTex, equirect_);
        skySet_->setBuffer(BindingTable::Global, frameUbo_, 0, sizeof(FrameUBO));
        rc->bindSet(skySet_, nullptr, 0);
        rc->draw(3);
        drawCalls++;
    }

    rc->bindPipeline(pbrPipe_);
    scene.registry.view<TransformComponent, MeshComponent>([&](Entity e) {
        auto* tr = scene.registry.get<TransformComponent>(e);
        auto* mc = scene.registry.get<MeshComponent>(e);
        if (!mc->visible || !mc->model || !tr) return;
        for (const GpuNodeDraw& d : mc->model->draws) {
            if (mc->node >= 0 && (int32_t)d.node != mc->node) continue;

            DrawUBO draw{};
            draw.model = tr->world;
            draw.normalMatrix = makeNormalMatrix(tr->world);
            draw.tint = Vec4{mc->tint.x, mc->tint.y, mc->tint.z, mc->tintAlpha};

            // per (entity, submesh) independent model UBO
            uint64_t uboKey = ((uint64_t)e.id << 32) | (uint32_t)d.indexOffset;
            IBuffer*& modelUbo = drawUbos_[uboKey];
            if (!modelUbo) {
                BufferDesc md; md.size=sizeof(DrawUBO); md.usage=BU_Uniform; md.host_visible=true;
                modelUbo = dev_->createBuffer(md, nullptr);
            }
            modelUbo->upload(0, sizeof(draw), &draw);

            uint64_t key = (uint64_t)(uintptr_t)d.material;
            IBindingSet* set = nullptr;
            auto it = drawSets_.find(key);
            if (it != drawSets_.end()) set = it->second;
            else {
                set = dev_->createBindingSet(pbrLayout_);
                GpuMaterial* m = d.material;
                set->setBuffer(BindingTable::Global, frameUbo_, 0, sizeof(FrameUBO));
                // Draw buffer is rebound per entity below (same binding slot),
                // so just point at the current model UBO initially; rebound every draw.
                set->setBuffer(BindingTable::Draw, modelUbo, 0, sizeof(DrawUBO));
                set->setBuffer(BindingTable::Material, m->ubo, 0, sizeof(MaterialUBO));
                set->setTexture(BindingTable::IrrAtlas, irrAtlas_);
                set->setTexture(BindingTable::PrefAtlas, equirect_);
                set->setTexture(BindingTable::BrdfLUT, brdfLUT_);
                set->setTexture(BindingTable::AlbedoTex, m->albedo);
                set->setTexture(BindingTable::NormalTex, m->normal);
                set->setTexture(BindingTable::MetallicRoughTex, m->metallicRoughness);
                set->setTexture(BindingTable::AOTex, m->occlusion);
                set->setTexture(BindingTable::EmissiveTex, m->emissive);
                drawSets_[key] = set;
            }
            // model transform differs per entity even for shared materials
            set->setBuffer(BindingTable::Draw, modelUbo, 0, sizeof(DrawUBO));
            rc->bindSet(set, nullptr, 0);
            rc->bindVertexBuffer(mc->model->vertexBuffer);
            rc->bindIndexBuffer(mc->model->indexBuffer, RHIIndexType::UInt32);
            rc->drawIndexed(d.indexCount, d.indexOffset);
            drawCalls++; triangles += d.indexCount/3;
        }
    });
    rc->endPass();

    // ================= tonemap to LDR =================
    PostUBO post{Vec4{scene.settings.exposure, 0.0f, 2.2f, 0.0f}};
    if (!postUbo_) {
        BufferDesc pd; pd.size=sizeof(PostUBO); pd.usage=BU_Uniform; pd.host_visible=true;
        postUbo_ = dev_->createBuffer(pd, nullptr);
    }
    postUbo_->upload(0, sizeof(post), &post);
    postSet_->setBuffer(BindingTable::Global, postUbo_, 0, sizeof(PostUBO));
    postSet_->setTexture(BindingTable::SceneColorTex, sceneColor_);

    rc->beginPass(dev_->currentScreenFB(), ClearValue{}, true, true);
    rc->bindPipeline(postPipe_);
    rc->bindSet(postSet_, nullptr, 0);
    rc->draw(3);
    drawCalls++;
    if (uiCallback) {
        rc->endPass();
        rc->beginPass(dev_->currentScreenFB(), ClearValue{}, false, false);
        uiCallback();
    }
    rc->endPass();

    dev_->endFrame();
    frameIndex_++;
}


} // namespace Genesis
