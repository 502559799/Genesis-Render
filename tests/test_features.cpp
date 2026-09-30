/*
 * Genesis Render - tests of the extended feature set:
 *
 *   reflection (RTTI registry, values, properties, methods)
 *   Lua scripting through the RTTI (scene building, command buffers)
 *   engine command buffers (render textures, custom pipeline, blits, 2D)
 *   SDF text in 2D and 3D
 *   material system edits through reflection
 *   UI orientation in captured frames
 *
 * GPU tests run once per available backend (OpenGL, Vulkan, Metal).
 */
#include "test_framework.hpp"
#include "test_gpu_utils.hpp"

#include "reflection/engine_bindings.hpp"
#include "renderer/render_system.hpp"
#include "script/script_system.hpp"
#include "ui/ui_system.hpp"

#include "imgui.h"

using namespace Genesis;
using namespace gpu_test;

namespace
{
    bool isRed(const Image &img, uint32_t x, uint32_t y)
    {
        uint8_t r, g, b, a;
        img.at(x, y, r, g, b, a);
        return r > 180 && g < 80 && b < 80;
    }
    bool isDark(const Image &img, uint32_t x, uint32_t y, float lum = 0.08f) { return img.luminance(x, y) < lum; }

    // Pixels brighter than `threshold` inside a rectangle.
    size_t brightIn(const Image &img, uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1, float threshold = 0.5f)
    {
        size_t n = 0;
        for (uint32_t y = y0; y < std::min(y1, img.height); ++y)
            for (uint32_t x = x0; x < std::min(x1, img.width); ++x)
                if (img.luminance(x, y) > threshold) ++n;
        return n;
    }

    // Unlit scene settings: output == base colour (no tone mapping / gamma).
    void plainOutput(TestEngine &te)
    {
        Render::RenderSettings &s = te.engine.render().settings();
        s.show_skybox = false;
        s.use_ibl = false;
        s.tonemap_mode = Render::TonemapMode::None;
        s.exposure = 1.0f;
        s.gamma = 1.0f;
        s.ambient_intensity = 0.0f;
    }

    Render::MaterialPtr unlitMaterial(TestEngine &te, const std::string &name, const Math::Color &c)
    {
        Render::MaterialPtr m = te.engine.render().scene().createMaterial(name);
        m->shading = Render::MaterialShading::Unlit;
        m->base_color = c;
        return m;
    }
} // namespace

//======================================================================
// Reflection (CPU)
//======================================================================
TEST(reflection, registry_describes_every_engine_type)
{
    Reflect::registerEngineTypes();
    const char *expected[] = {"Engine", "World", "Entity", "Transform", "Camera", "Light", "Mesh", "Environment", "Text",
                              "Script", "CameraController", "Tag", "Material", "RenderTexture", "MeshAsset",
                              "CommandBuffer", "RenderSettings", "Renderer", "Input"};
    for (const char *name : expected)
        CHECK_MSG(Reflect::Registry::instance().find(name) != nullptr, std::string("type registered: ") + name);
    const Reflect::TypeInfo *mat = Reflect::Registry::instance().find("Material");
    CHECK(mat && mat->findProperty("roughness") && mat->findMethod("setTexture") && mat->constructor);
    const Reflect::TypeInfo *cb = Reflect::Registry::instance().find("CommandBuffer");
    CHECK(cb && cb->findMethod("drawMesh") && cb->findMethod("blit") && cb->findMethod("drawText3D"));
    CHECK(!Reflect::Registry::instance().functions().empty());
}

TEST(reflection, values_convert_tolerantly)
{
    Reflect::Value v3(Math::Vec3(0.25f, 0.5f, 0.75f));
    Math::Color c = v3.asColor();
    CHECK_NEAR(c.g, 0.5f, 1e-6f);
    CHECK_NEAR(c.a, 1.0f, 1e-6f);
    CHECK(Reflect::Value(1).asBool());
    CHECK(!Reflect::Value(0.0f).asBool());
    CHECK_NEAR((float)Reflect::Value(3).asFloat(), 3.0f, 1e-6f);
    CHECK_EQ(Reflect::Value(2.9).asInt(), (int64_t)2);
    CHECK(Reflect::Value("abc").asString() == "abc");
    Reflect::Value list = Reflect::Value::list({Reflect::Value(1), Reflect::Value("x")});
    CHECK_EQ(list.asList().size(), (size_t)2);
    CHECK(Reflect::Value().isNil());
}

TEST(reflection, properties_enums_and_read_only)
{
    Reflect::registerEngineTypes();
    Render::RenderSettings s;
    Reflect::ObjectRef ref = Reflect::refRaw(&s);
    CHECK(ref.valid());
    CHECK(Reflect::setProperty(ref, "exposure", Reflect::Value(2.5f)));
    CHECK_NEAR(s.exposure, 2.5f, 1e-6f);
    CHECK(Reflect::setProperty(ref, "tonemap", Reflect::Value("reinhard")));
    CHECK(s.tonemap_mode == Render::TonemapMode::Reinhard);
    Reflect::Value out;
    CHECK(Reflect::getProperty(ref, "tonemap", out) && out.asString() == "reinhard");
    CHECK(Reflect::setProperty(ref, "post_effect", Reflect::Value("edge")));
    CHECK(s.post_effect == Render::PostEffect::Edge);
    std::string err;
    CHECK(!Reflect::setProperty(ref, "no_such_property", Reflect::Value(1), &err));
    CHECK(!err.empty());

    auto mat = std::make_shared<Render::Material>();
    Reflect::ObjectRef mref = Reflect::refShared(mat);
    const uint64_t rev = mat->revision;
    CHECK(Reflect::setProperty(mref, "roughness", Reflect::Value(0.3f)));
    CHECK_NEAR(mat->roughness, 0.3f, 1e-6f);
    CHECK_MSG(mat->revision > rev, "edits through the RTTI mark the material dirty");
    CHECK(Reflect::setProperty(mref, "blend", Reflect::Value("transparent")));
    CHECK(mat->isTransparent());
    CHECK_MSG(!Reflect::setProperty(mref, "index", Reflect::Value(5)), "read-only properties reject writes");
}

TEST(reflection, methods_record_command_buffer_commands)
{
    Reflect::registerEngineTypes();
    Render::CommandBuffer cb("reflected");
    Reflect::ObjectRef ref = Reflect::refRaw(&cb);
    Reflect::Value out;
    CHECK(Reflect::callMethod(ref, "drawRect", {1.0f, 2.0f, 3.0f, 4.0f, Math::Color(1, 0, 0, 1)}, out));
    CHECK(Reflect::callMethod(ref, "drawText", {Reflect::Value("hi"), 10.0f, 20.0f}, out)); // optional args default
    CHECK(Reflect::callMethod(ref, "setRenderTarget", {Reflect::Value("backbuffer")}, out));
    CHECK_EQ(cb.size(), (size_t)3);
    CHECK(cb.commands()[0].type == Render::DrawCommand::Type::DrawRect);
    CHECK_NEAR(cb.commands()[0].v0.z, 3.0f, 1e-6f);
    CHECK(cb.commands()[1].text == "hi");
    CHECK(cb.commands()[2].target.kind == Render::RenderTargetId::Kind::Backbuffer);
    CHECK(Reflect::getProperty(ref, "count", out) && out.asInt() == 3);
    CHECK(!Reflect::callMethod(ref, "noSuchMethod", {}, out));
    CHECK(Reflect::callMethod(ref, "clear", {}, out));
    CHECK(cb.empty());
}

TEST(command_buffer, recording_copies_arguments_and_names_roundtrip)
{
    Render::CommandBuffer cb;
    std::string text = "first";
    Math::Color c(0.1f, 0.2f, 0.3f, 1.0f);
    cb.drawText(text, 0, 0, 12.0f, c);
    text = "changed";
    c.r = 1.0f;
    CHECK_MSG(cb.commands()[0].text == "first", "recorded text is a copy (organise / execute separation)");
    CHECK_NEAR(cb.commands()[0].c0.r, 0.1f, 1e-6f);

    for (uint32_t i = 0; i < (uint32_t)Render::CameraEvent::Count; ++i)
    {
        Render::CameraEvent e;
        CHECK(Render::cameraEventFromName(Render::cameraEventName((Render::CameraEvent)i), e) && (uint32_t)e == i);
    }
    for (uint32_t i = 0; i < (uint32_t)Render::PostEffect::Count; ++i)
    {
        Render::PostEffect fx;
        CHECK(Render::postEffectFromName(Render::postEffectName((Render::PostEffect)i), fx) && fx == (Render::PostEffect)i);
    }
}

//======================================================================
// Command buffers (GPU)
//======================================================================
TEST_GPU(commandbuffer, render_texture_draw_mesh_and_image_orientation)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0), 40.0f);
    plainOutput(te);
    Render::RenderSystem &rs = te.engine.render();

    // Sphere rendered by a command buffer into a render texture, ABOVE the
    // centre of that texture; the texture is then drawn into the top-left
    // quadrant of the frame.
    Render::RenderTexturePtr rt = rs.createRenderTexture("test_rt", 64, 64, true, true);
    Reflect::MeshAssetPtr sphere = Reflect::createMeshAsset("sphere", 0.7f, 32);
    CHECK(rt && sphere && !sphere->gpu_meshes.empty());
    if (!rt || !sphere) return;
    Render::MaterialPtr red = unlitMaterial(te, "cb_red", Math::Color(1, 0, 0, 1));

    auto off = std::make_shared<Render::CommandBuffer>("offscreen");
    off->setRenderTarget(rt);
    off->clearRenderTarget(true, Math::Color(0, 0, 0, 1), true);
    off->setCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0), 40.0f);
    off->drawMesh(sphere->gpu_meshes[0], Math::Mat4::translate(Math::Vec3(0, 0.55f, 0)), red);
    rs.addCommandBuffer(Render::CameraEvent::BeforeScene, off);
    auto show = std::make_shared<Render::CommandBuffer>("show");
    show->drawImage(rt, 0, 0, 64, 64);
    rs.addCommandBuffer(Render::CameraEvent::Overlay, show);

    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;
    uint8_t r, g, b, a;
    img.at(32, 16, r, g, b, a);
    GEN_LOG_INFO("[test] rt sphere pixel (32,16)=(%u,%u,%u)", r, g, b);
    CHECK_MSG(isRed(img, 32, 16), "mesh drawn by a command buffer into a render texture");
    CHECK_MSG(isDark(img, 32, 58), "render texture keeps its orientation when displayed (sphere is at the top)");
    CHECK_MSG(isDark(img, 100, 100), "drawImage only covers its rectangle");
    CHECK(rs.stats().command_buffers >= 2);
}

TEST_GPU(commandbuffer, rect_2d_uses_top_left_pixel_coordinates)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 96)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0));
    plainOutput(te);
    Render::CommandBuffer &imm = te.engine.render().immediate(Render::CameraEvent::Overlay);
    imm.drawRect(0, 0, 32, 24, Math::Color(1, 0, 0, 1));
    imm.drawLine2D(Math::Vec2(0, 90), Math::Vec2(127, 90), Math::Color(0, 1, 0, 1), 4.0f);
    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;
    CHECK_MSG(isRed(img, 4, 4) && isRed(img, 30, 22), "rect covers the top-left corner");
    CHECK_MSG(!isRed(img, 40, 4) && !isRed(img, 4, 30), "rect ends at 32 x 24 pixels");
    uint8_t r, g, b, a;
    img.at(64, 90, r, g, b, a);
    CHECK_MSG(g > 180 && r < 80, "2D line near the bottom edge");
}

TEST_GPU(commandbuffer, custom_pipeline_replaces_the_forward_pipeline)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(96, 96)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0), 40.0f);
    plainOutput(te);
    Render::RenderSystem &rs = te.engine.render();
    ECS::Entity e = te.engine.world().createEntity("red_sphere");
    Reflect::attachMesh(e, Reflect::createMeshAsset("sphere", 1.0f, 32), unlitMaterial(te, "pipe_red", Math::Color(1, 0, 0, 1)));

    Image forward;
    CHECK(te.capture(forward));
    if (!forward.valid()) return;
    CHECK_MSG(isRed(forward, 48, 48) && isDark(forward, 4, 4), "forward pipeline draws the entity on black");

    auto custom = std::make_shared<Render::CommandBuffer>("custom");
    custom->setRenderTarget(Render::RenderTargetId::backbuffer());
    custom->clearRenderTarget(true, Math::Color(0, 0, 1, 1), true);
    rs.addCustomCommandBuffer(custom);
    rs.settings().pipeline_mode = Render::PipelineMode::Custom;
    Image only_clear;
    CHECK(te.capture(only_clear));
    if (!only_clear.valid()) return;
    uint8_t r, g, b, a;
    only_clear.at(48, 48, r, g, b, a);
    CHECK_MSG(b > 200 && r < 40, "custom pipeline: the scene is not drawn unless the command buffer asks for it");

    custom->drawScene();
    Image with_scene;
    CHECK(te.capture(with_scene));
    if (!with_scene.valid()) return;
    with_scene.at(4, 4, r, g, b, a);
    CHECK_MSG(isRed(with_scene, 48, 48) && b > 200, "custom pipeline: drawScene + clear colour");

    rs.settings().pipeline_mode = Render::PipelineMode::Forward;
    Image back;
    CHECK(te.capture(back));
    if (back.valid()) CHECK_MSG(forward.meanAbsDifference(back) < 0.01f, "switching back restores the forward pipeline");
}

TEST_GPU(commandbuffer, blit_effects_transform_the_image)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(96, 96)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0));
    plainOutput(te);
    Render::RenderSystem &rs = te.engine.render();
    Render::RenderTexturePtr src = rs.createRenderTexture("blit_src", 96, 96, false, false);
    rs.settings().pipeline_mode = Render::PipelineMode::Custom;
    auto cb = std::make_shared<Render::CommandBuffer>("blit");
    rs.addCustomCommandBuffer(cb);

    auto record = [&](Render::PostEffect fx)
    {
        cb->clear();
        cb->setRenderTarget(src);
        cb->clearRenderTarget(true, Math::Color(0.8f, 0.3f, 0.1f, 1.0f), false);
        cb->drawRect(10, 10, 30, 70, Math::Color(0.1f, 0.2f, 0.9f, 1.0f));
        cb->drawRect(55, 20, 30, 30, Math::Color(1, 1, 1, 1));
        cb->blit(Render::RenderTargetId::of(src), Render::RenderTargetId::backbuffer(), fx, Math::Vec4(1.0f, 3.0f, 0, 0),
                 Math::Color(0.2f, 1.0f, 0.4f, 1.0f));
    };
    record(Render::PostEffect::None);
    Image base;
    CHECK(te.capture(base));
    if (!base.valid()) return;
    uint8_t r, g, b, a;
    base.at(80, 85, r, g, b, a);
    CHECK_MSG(r > 180 && g > 50 && g < 110, "plain blit copies the texture");
    CHECK_MSG(base.luminance(25, 40) < base.luminance(70, 35), "blit keeps 2D content in place");

    record(Render::PostEffect::Invert);
    Image inv;
    CHECK(te.capture(inv));
    if (inv.valid())
    {
        inv.at(80, 85, r, g, b, a);
        CHECK_MSG(r < 80 && b > 180, "invert effect");
    }
    record(Render::PostEffect::Grayscale);
    Image grey;
    CHECK(te.capture(grey));
    if (grey.valid())
    {
        grey.at(80, 85, r, g, b, a);
        CHECK_MSG(std::abs((int)r - (int)g) < 6 && std::abs((int)g - (int)b) < 6, "grayscale effect");
    }
    for (uint32_t fx = 1; fx < (uint32_t)Render::PostEffect::Count; ++fx)
    {
        record((Render::PostEffect)fx);
        Image out;
        CHECK(te.capture(out));
        if (!out.valid()) continue;
        const float diff = base.meanAbsDifference(out);
        GEN_LOG_INFO("[test] effect %-10s mean diff %.4f", Render::postEffectName((Render::PostEffect)fx), diff);
        CHECK_MSG(diff > 0.002f, std::string("effect changes the image: ") + Render::postEffectName((Render::PostEffect)fx));
    }
}

TEST_GPU(commandbuffer, sampling_the_current_target_is_skipped)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(64, 64)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0));
    plainOutput(te);
    Render::RenderSystem &rs = te.engine.render();
    Render::RenderTexturePtr rt = rs.createRenderTexture("feedback", 64, 64, false, true);
    Render::MaterialPtr screen = unlitMaterial(te, "feedback_screen", Math::Color(1, 1, 1, 1));
    screen->setRenderTexture(Render::TextureSlot::BaseColor, rt);
    ECS::Entity e = te.engine.world().createEntity("screen");
    Reflect::attachMesh(e, Reflect::createMeshAsset("box", 1.0f), screen);
    auto cb = std::make_shared<Render::CommandBuffer>("into_rt");
    cb->setRenderTarget(rt);
    cb->clearRenderTarget(true, Math::Color(0, 1, 0, 1), true);
    cb->drawScene(); // the box samples `rt` -> must be skipped, not read while written
    rs.addCommandBuffer(Render::CameraEvent::BeforeScene, cb);
    Image img;
    CHECK(te.capture(img));
    CHECK_MSG(rs.stats().feedback_skips >= 1, "render feedback detected");
    uint8_t r, g, b, a;
    if (img.valid())
    {
        img.at(32, 32, r, g, b, a);
        CHECK_MSG(g > 180 && r < 60, "main view shows the box textured with the render texture");
    }
}

TEST_GPU(render, debug_lines_last_one_frame)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(64, 64)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0), 40.0f);
    plainOutput(te);
    Render::RenderSystem &rs = te.engine.render();
    rs.drawBox(Math::AABB{Math::Vec3(-0.6f), Math::Vec3(0.6f), true}, Math::Color(1, 1, 1, 1));
    Image with_lines, next_frame;
    CHECK(te.capture(with_lines));
    CHECK(te.capture(next_frame));
    if (!with_lines.valid() || !next_frame.valid()) return;
    CHECK_MSG(with_lines.countAbove(0.5f) > 20, "debug lines drawn in the frame they were added");
    CHECK_MSG(next_frame.countAbove(0.5f) == 0, "debug lines do not accumulate across frames");
}

//======================================================================
// SDF text (GPU)
//======================================================================
TEST_GPU(text, sdf_2d_text_is_placed_and_outlined)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(256, 128)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0));
    plainOutput(te);
    Render::RenderSystem &rs = te.engine.render();
    CHECK_MSG(rs.font() != nullptr, "SDF font atlas baked");
    if (!rs.font()) return;
    Math::Vec2 size = rs.font()->measure("HELLO", 40.0f);
    CHECK(size.x > 60.0f && size.x < 200.0f);

    rs.immediate(Render::CameraEvent::Overlay).drawText("HELLO", 16, 8, 40.0f, Math::Color(1, 1, 1, 1));
    Image plain;
    CHECK(te.capture(plain));
    if (!plain.valid()) return;
    const size_t top = brightIn(plain, 0, 0, 256, 64), bottom = brightIn(plain, 0, 64, 256, 128);
    const size_t inside = brightIn(plain, 16, 8, 16 + (uint32_t)size.x + 2, 8 + 44);
    GEN_LOG_INFO("[test] 2D text bright px: top %zu bottom %zu inside-box %zu (box %.0fx40)", top, bottom, inside, size.x);
    CHECK_MSG(top > 250 && bottom == 0, "text drawn at y = 8 lands in the top rows");
    CHECK_MSG(inside * 10 >= top * 9, "glyph coverage is inside the measured box");

    rs.immediate(Render::CameraEvent::Overlay).drawText("HELLO", 16, 8, 40.0f, Math::Color(1, 1, 1, 1), Text::TextAlign::Left,
                                                        0.3f, Math::Color(1, 0, 0, 1));
    Image outlined;
    CHECK(te.capture(outlined));
    if (!outlined.valid()) return;
    size_t red = 0;
    for (uint32_t y = 0; y < 64; ++y)
        for (uint32_t x = 0; x < 256; ++x)
            if (isRed(outlined, x, y)) ++red;
    CHECK_MSG(red > 100, "outline adds a coloured border around the glyphs");
}

TEST_GPU(text, sdf_3d_text_billboard_depth_and_components)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128)) return;
    te.addCamera(Math::Vec3(0, 0, 4), Math::Vec3(0, 0, 0), 40.0f);
    plainOutput(te);
    Render::RenderSystem &rs = te.engine.render();

    rs.immediate(Render::CameraEvent::AfterTransparent).drawText3D("HHH", Math::Vec3(0, 0, 0), 0.6f, Math::Color(1, 1, 1, 1));
    Image text;
    CHECK(te.capture(text));
    if (!text.valid()) return;
    const size_t centre = brightIn(text, 32, 44, 96, 84);
    GEN_LOG_INFO("[test] 3D text bright px in centre: %zu", centre);
    CHECK_MSG(centre > 150, "world-space text rendered at the origin");

    // An opaque red box in front hides depth tested text, but not text drawn
    // with the depth test disabled.
    ECS::Entity box = te.engine.world().createEntity("occluder");
    Reflect::attachMesh(box, Reflect::createMeshAsset("box", 1.6f), unlitMaterial(te, "occluder", Math::Color(1, 0, 0, 1)));
    if (auto *t = te.engine.world().getComponent<ECS::TransformComponent>(box)) t->setPosition(Math::Vec3(0, 0, 1.2f));
    rs.immediate(Render::CameraEvent::AfterTransparent).drawText3D("HHH", Math::Vec3(0, 0, 0), 0.6f, Math::Color(1, 1, 1, 1));
    Image hidden;
    CHECK(te.capture(hidden));
    if (hidden.valid()) CHECK_MSG(brightIn(hidden, 32, 44, 96, 84) < centre / 10, "depth tested 3D text is occluded");
    Render::CommandBuffer &imm = rs.immediate(Render::CameraEvent::AfterTransparent);
    imm.setDepthTest3D(false);
    imm.drawText3D("HHH", Math::Vec3(0, 0, 0), 0.6f, Math::Color(1, 1, 1, 1));
    Image overlay;
    CHECK(te.capture(overlay));
    if (overlay.valid()) CHECK_MSG(brightIn(overlay, 32, 44, 96, 84) > centre / 2, "text without depth test draws on top");
    te.engine.world().destroyEntity(box);

    // TextComponent: a label attached to an entity, offset above it.
    ECS::Entity labelled = te.engine.world().createEntity("labelled");
    te.engine.world().addComponent<ECS::TransformComponent>(labelled);
    ECS::TextComponent label;
    label.text = "LABEL";
    label.size = 0.35f;
    label.offset = Math::Vec3(0, 0.9f, 0);
    te.engine.world().addComponent<ECS::TextComponent>(labelled, label);
    Image comp;
    CHECK(te.capture(comp));
    if (comp.valid())
    {
        const size_t above = brightIn(comp, 0, 0, 128, 60), below = brightIn(comp, 0, 68, 128, 128);
        CHECK_MSG(above > 60 && below == 0, "TextComponent label drawn above its entity");
    }
}

//======================================================================
// Materials through the RTTI (GPU)
//======================================================================
TEST_GPU(material, reflected_edits_textures_and_render_textures)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(96, 96)) return;
    Reflect::registerEngineTypes();
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0), 40.0f);
    plainOutput(te);
    Render::RenderSystem &rs = te.engine.render();
    Render::MaterialPtr mat = rs.scene().createMaterial("reflected");
    ECS::Entity e = te.engine.world().createEntity("box");
    Reflect::attachMesh(e, Reflect::createMeshAsset("box", 1.2f), mat);
    Reflect::ObjectRef ref = Reflect::refShared(mat);

    CHECK(Reflect::setProperty(ref, "shading", Reflect::Value("unlit")));
    CHECK(Reflect::setProperty(ref, "base_color", Reflect::Value(Math::Color(0, 1, 0, 1))));
    Image green;
    CHECK(te.capture(green));
    if (!green.valid()) return;
    uint8_t r, g, b, a;
    green.at(48, 48, r, g, b, a);
    GEN_LOG_INFO("[test] reflected unlit green = (%u,%u,%u)", r, g, b);
    CHECK_MSG(g > 245 && r < 10 && b < 10, "unlit shading outputs the exact base colour");

    Reflect::Value ok;
    CHECK(Reflect::callMethod(ref, "setTexture", {Reflect::Value("base_color"), Reflect::Value(assetPath("textures/checker_albedo.png"))}, ok));
    CHECK(ok.asBool());
    Image checker;
    CHECK(te.capture(checker));
    if (checker.valid())
    {
        float lo = 1.0f, hi = 0.0f;
        for (uint32_t y = 36; y < 60; ++y)
            for (uint32_t x = 36; x < 60; ++x)
            {
                lo = std::min(lo, checker.luminance(x, y));
                hi = std::max(hi, checker.luminance(x, y));
            }
        CHECK_MSG(hi - lo > 0.3f, "texture assigned through the RTTI shows its pattern");
    }

    // A render texture filled with red by a command buffer feeds the material.
    Render::RenderTexturePtr rt = rs.createRenderTexture("mat_rt", 32, 32, false, false);
    auto cb = std::make_shared<Render::CommandBuffer>("fill");
    cb->setRenderTarget(rt);
    cb->clearRenderTarget(true, Math::Color(1, 0, 0, 1), false);
    rs.addCommandBuffer(Render::CameraEvent::BeforeScene, cb);
    CHECK(Reflect::callMethod(ref, "setTexture", {Reflect::Value("base_color"), Reflect::Value(Reflect::refShared(rt))}, ok));
    CHECK(Reflect::setProperty(ref, "base_color", Reflect::Value(Math::Color(1, 1, 1, 1))));
    Image from_rt;
    CHECK(te.capture(from_rt));
    if (from_rt.valid()) CHECK_MSG(isRed(from_rt, 48, 48), "material samples a render texture");

    // Transparent blending over a blue background.
    CHECK(Reflect::callMethod(ref, "clearTexture", {Reflect::Value("base_color")}, ok));
    CHECK(Reflect::setProperty(ref, "base_color", Reflect::Value(Math::Color(1, 0, 0, 0.5f))));
    CHECK(Reflect::setProperty(ref, "blend", Reflect::Value("transparent")));
    auto bg = std::make_shared<Render::CommandBuffer>("blue");
    bg->clearRenderTarget(true, Math::Color(0, 0, 1, 1), false);
    rs.addCommandBuffer(Render::CameraEvent::AfterOpaque, bg);
    Image blended;
    CHECK(te.capture(blended));
    if (blended.valid())
    {
        blended.at(48, 48, r, g, b, a);
        GEN_LOG_INFO("[test] transparent red over blue = (%u,%u,%u)", r, g, b);
        CHECK_MSG(r > 90 && b > 90 && r < 170 && b < 170, "alpha blending mixes the material with the background");
    }
}

//======================================================================
// Lua through the RTTI (GPU)
//======================================================================
TEST_GPU(lua, script_builds_and_renders_a_scene_through_rtti)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128, false, true)) return;
    CHECK(te.engine.hasScript());
    if (!te.engine.hasScript()) return;
    const char *code = R"LUA(
        settings.skybox = false
        settings.ibl = false
        settings.tonemap = "none"
        settings.gamma = 1.0
        settings.ambient_intensity = 0
        local cam = world:create("LuaCamera")
        cam:add("Camera").fov = 40
        cam.Transform.position = vec3(0, 0, 3)
        local m = renderer:createMaterial("lua_red", "unlit")
        m.base_color = color(1, 0, 0)
        assert(m.shading == "unlit")
        local e = world:create("LuaSphere")
        e:setMesh(renderer:createMesh("sphere", 1.0, 32), m)
        assert(e:has("Mesh") and e:has("Transform"))
        assert(world:find("LuaSphere") == e)
        local v = vec3(1, 2, 3) + vec3(1, 1, 1)
        assert(v.x == 2 and v.z == 4)
        hud = CommandBuffer.new("lua_hud")
        hud:drawRect(0, 0, 16, 16, color(0, 1, 0))
        renderer:addCommandBuffer("overlay", hud)
        assert(hud.count == 1)
        local d = reflect.describe("Material")
        assert(d and #d.properties > 10)
    )LUA";
    CHECK_MSG(te.engine.script().executeString(code, "=rtti_test"), "Lua scene script runs");
    CHECK_EQ(te.engine.script().errorCount(), 0u);
    Image img;
    CHECK(te.capture(img));
    if (!img.valid()) return;
    uint8_t r, g, b, a;
    img.at(6, 6, r, g, b, a);
    CHECK_MSG(isRed(img, 64, 64), "sphere with a Lua created material");
    CHECK_MSG(g > 200 && r < 40, "Lua command buffer draws the HUD rect");

    // Errors are reported, not fatal.
    const uint32_t before = te.engine.script().errorCount();
    CHECK(!te.engine.script().executeString("local m = renderer:findMaterial('lua_red'); m.no_such_field = 1", "=bad"));
    CHECK(te.engine.script().errorCount() == before + 1);
}

TEST_GPU(lua, bundled_scene_scripts_run_without_errors)
{
    if (!g_available) return;
    const char *scripts[] = {"scripts/scene_builder.lua", "scripts/commandbuffer_demo.lua"};
    for (const char *script : scripts)
    {
        TestEngine te;
        if (!te.init(160, 96, false, true)) return;
        std::string path = assetPath(script);
        CHECK(ResourceIO::fileExists(path));
        ECS::Entity host = te.engine.world().createEntity("host");
        te.engine.world().addComponent<ECS::TransformComponent>(host);
        ECS::ScriptComponent sc;
        sc.script_path = path;
        te.engine.world().addComponent<ECS::ScriptComponent>(host, sc);
        Image img;
        for (int i = 0; i < 3; ++i) CHECK(te.capture(img));
        GEN_LOG_INFO("[test] %s: %zu entities, %zu materials, luminance %.3f", script, te.engine.world().entityCount(),
                     te.engine.render().scene().materialCount(), img.averageLuminance());
        CHECK_MSG(te.engine.script().errorCount() == 0, std::string("no Lua errors in ") + script);
        CHECK_MSG(te.engine.world().entityCount() > 8, std::string("scene populated by ") + script);
        CHECK_MSG(img.valid() && img.averageLuminance() > 0.15f, std::string("scene rendered by ") + script);
    }
}

//======================================================================
// UI (GPU)
//======================================================================
TEST_GPU(ui, imgui_is_upright_in_captured_frames)
{
    if (!g_available) return;
    TestEngine te;
    if (!te.init(128, 128, true, false)) return;
    te.addCamera(Math::Vec3(0, 0, 3), Math::Vec3(0, 0, 0));
    plainOutput(te);
    te.engine.setUiDrawCallback([](Engine &)
    {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(80, 40));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1, 1, 1, 1));
        ImGui::Begin("top", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration);
        ImGui::End();
        ImGui::PopStyleColor();
    });
    Image img;
    CHECK(te.capture(img));
    CHECK(te.capture(img)); // ImGui needs one frame to lay the window out
    if (!img.valid()) return;
    CHECK_MSG(img.luminance(40, 20) > 0.9f, "UI window at the top-left of the frame");
    CHECK_MSG(isDark(img, 40, 100) && isDark(img, 110, 20), "nothing else is covered");
}
