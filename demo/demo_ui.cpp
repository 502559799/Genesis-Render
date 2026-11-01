/*
 * Genesis Render - demo application: ImGui panels.
 */
#include "demo_app.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "animation/animation_system.hpp"
#include "api/compute_api.hpp"
#include "api/rendering_api.hpp"
#include "renderer/render_system.hpp"
#include "resource/asset_package.hpp"
#include "script/script_system.hpp"
#include "ui/inspector.hpp"
#include "ui/ui_system.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Demo
{
    namespace
    {
        const char *kConsoleExamples[][2] = {
            {"List reflected types",
             "for _, t in ipairs(reflect.types()) do print(t) end"},
            {"Describe Material",
             "local d = reflect.describe(\"Material\")\n"
             "print(d.name .. \" - \" .. d.description)\n"
             "for _, p in ipairs(d.properties) do print(\"  \" .. p.name .. \" : \" .. p.type) end\n"
             "for _, m in ipairs(d.methods) do print(\"  \" .. m.name .. m.signature) end"},
            {"Spawn a sphere (RTTI)",
             "local m = renderer:createMaterial(\"console_material\")\n"
             "m.base_color = color(math.random(), math.random(), math.random())\n"
             "m.metallic = 1.0\n"
             "m.roughness = 0.25\n"
             "local e = world:create(\"ConsoleSphere\")\n"
             "e:setMesh(renderer:createMesh(\"sphere\", 0.8), m)\n"
             "e.Transform.position = vec3(math.random() * 6 - 3, 2.6, math.random() * 2)\n"
             "local t = e:add(\"Text\")\n"
             "t.text = \"from the console\"\n"
             "t.offset = vec3(0, 0.6, 0)\n"
             "print(\"spawned\", e, \"entities:\", world.entity_count)"},
            {"Edit a material",
             "local m = renderer:findMaterial(\"Gold\")\n"
             "if m then m.roughness = 0.65; m.base_color = color(0.95, 0.55, 0.25) end\n"
             "print(m)"},
            {"Post effect",
             "settings.post_effect = \"sepia\"\n"
             "settings.post_params = vec4(1, 2, 0, 0)"},
            {"Persistent overlay command buffer",
             "if console_cb then renderer:removeCommandBuffer(\"overlay\", console_cb) end\n"
             "console_cb = CommandBuffer.new(\"console_overlay\")\n"
             "console_cb:drawRect(40, 260, 520, 70, color(0, 0, 0, 0.6))\n"
             "console_cb:drawText(\"Hello from a Lua CommandBuffer\", 56, 276, 34, color(1, 0.8, 0.2), \"left\", 0.2)\n"
             "renderer:addCommandBuffer(\"overlay\", console_cb)"},
            {"Remove overlay",
             "if console_cb then renderer:removeCommandBuffer(\"overlay\", console_cb); console_cb = nil end"},
            {"Entity components",
             "local e = world:find(\"DamagedHelmet\")\n"
             "if e then\n"
             "  for _, c in ipairs(e:components()) do print(c) end\n"
             "  e.Transform.scale = vec3(1.3)\n"
             "end"},
        };

        const char *kJsConsoleExamples[][2] = {
            {"JS: list reflected types",
             "print(reflect.types().join(', '))"},
            {"JS: describe Material",
             "var d = reflect.describe('Material');\nprint(d.name + ' - ' + d.description);\nd.properties.forEach(function(p){ print('  ' + p.name + ' : ' + p.type); });"},
            {"JS: spawn a sphere (RTTI)",
             "var m = renderer.createMaterial('js_console_material');\nm.base_color = color(Math.random(), Math.random(), 0.8, 1);\nm.metallic = 1.0; m.roughness = 0.2;\nvar e = world.create('JS console sphere');\ne.setMesh(renderer.createMesh('sphere', 0.8), m);\ne.Transform.position = vec3(Math.random()*4-2, 2.6, Math.random()*2);\nvar t = e.add('Text'); t.text = 'from JavaScript'; t.offset = vec3(0, 0.6, 0);\nprint('spawned entity ' + e.id);"},
            {"JS: edit a material",
             "var m = renderer.findMaterial('Gold');\nif (m) { m.roughness = 0.7; m.base_color = color(0.95, 0.55, 0.25, 1); }\nprint(m);"},
            {"JS: physics impulse",
             "var e = world.find('Ball_0');\nif (e) physics.applyImpulse(e, vec3(0, 8, 0), e.Transform.position);\nprint('bodies ' + physics.body_count + ', contacts ' + physics.contacts);"},
            {"JS: package config",
             "print(asset_text('config/game.json'));"},
        };

        bool colorEdit(const char *label, Genesis::Math::Color &c, bool alpha = false)
        {
            return alpha ? ImGui::ColorEdit4(label, &c.r) : ImGui::ColorEdit3(label, &c.r);
        }

        // Feature windows share the column under the Engine panel so the
        // centre of the viewport stays visible.
        void leftColumn()
        {
            ImGuiIO &io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(8, 338), ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowSize(ImVec2(350, std::max(200.0f, io.DisplaySize.y - 346)), ImGuiCond_FirstUseEver);
        }

        void sectionTitle(const char *text)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.8f, 1.0f, 1.0f), "%s", text);
            ImGui::Separator();
        }
    } // namespace

    void DemoApp::drawUI(Engine &e)
    {
        (void)e;
        drawMenuBar();
        if (show_engine_) drawEnginePanel();
        if (show_render_) drawRenderPanel();
        if (show_inspector_) drawInspectorPanel();
        if (show_material_) drawMaterialPanel();
        if (show_cb_) drawCommandBufferPanel();
        if (show_text_) drawTextPanel();
        if (show_physics_) drawPhysicsPanel();
        if (show_gaussian_) drawGaussianPanel();
        if (show_package_) drawPackagePanel();
        if (show_game_) drawGamePanel();
        if (show_animation_) drawAnimationPanel();
        if (show_raytracing_) drawRayTracingPanel();
        if (show_codec_) drawCodecPanel();
        if (show_apis_) drawApisPanel();
        if (show_media_) drawMediaPanel();
        if (show_console_) drawConsolePanel();
        if (show_log_) drawLogPanel();
        if (show_help_) drawHelpPanel();
    }

    void DemoApp::drawMenuBar()
    {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("Scene"))
        {
            const char *labels[] = {"Showcase (C++)", "Built by Lua script (RTTI)", "CommandBuffer demo (Lua)",
                                    "Physics playground", "Cascade mini-game (package)", "Gaussian splat viewer",
                                    "Package scene (.gpkg)", "Animation lab (2D sprite + 3D compute)",
                                    "Ray tracing (compute)", "Media player (libavcodec)", "Empty"};
            for (uint32_t i = 0; i < (uint32_t)SceneKind::Count; ++i)
                if (ImGui::MenuItem(labels[i], nullptr, scene_ == (SceneKind)i)) pending_scene_ = (SceneKind)i;
            ImGui::Separator();
            if (ImGui::MenuItem("Reload current scene")) pending_scene_ = scene_;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Windows"))
        {
            ImGui::MenuItem("Engine", nullptr, &show_engine_);
            ImGui::MenuItem("Render settings", nullptr, &show_render_);
            ImGui::MenuItem("Hierarchy / Inspector", nullptr, &show_inspector_);
            ImGui::MenuItem("Material editor", nullptr, &show_material_);
            ImGui::MenuItem("CommandBuffer lab", nullptr, &show_cb_);
            ImGui::MenuItem("SDF text lab", nullptr, &show_text_);
            ImGui::MenuItem("Physics lab", nullptr, &show_physics_);
            ImGui::MenuItem("Gaussian splat lab", nullptr, &show_gaussian_);
            ImGui::MenuItem("Asset package browser", nullptr, &show_package_);
            ImGui::MenuItem("Cascade game", nullptr, &show_game_);
            ImGui::MenuItem("Animation lab", nullptr, &show_animation_);
            ImGui::MenuItem("Ray tracing lab", nullptr, &show_raytracing_);
            ImGui::MenuItem("Encoder / Decoder lab", nullptr, &show_codec_);
            ImGui::MenuItem("Engine APIs (4)", nullptr, &show_apis_);
            ImGui::MenuItem("Media player", nullptr, &show_media_);
            ImGui::MenuItem("Lua / JS console", nullptr, &show_console_);
            ImGui::MenuItem("Log", nullptr, &show_log_);
            ImGui::MenuItem("Controls", nullptr, &show_help_);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        const Render::RenderStats &st = engine.render().stats();
        ImGui::Text("%s | %.0f fps | %.2f ms | %u draws | %s pipeline", RHI::backendTypeName(engine.device()->backend()),
                    engine.fps(), engine.frameTimeMs(), st.draw_calls,
                    engine.render().settings().pipeline_mode == Render::PipelineMode::Custom ? "custom" : "forward");
        ImGui::EndMainMenuBar();
    }

    //======================================================================
    void DemoApp::drawEnginePanel()
    {
        ImGui::SetNextWindowPos(ImVec2(8, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(350, 300), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Engine", &show_engine_)) { ImGui::End(); return; }
        Render::RenderSystem &rs = engine.render();
        const Render::RenderStats &st = rs.stats();
        const RHI::DeviceInfo &info = engine.device()->info();
        ImGui::Text("backend : %s", RHI::backendTypeName(engine.device()->backend()));
        ImGui::TextWrapped("device  : %s", info.device_name.c_str());
        ImGui::Text("api     : %s", info.api_version.c_str());
        ImGui::Text("depth   : %s, clip z %s", RHI::pixelFormatName(info.depth_format),
                    info.clip_depth_zero_to_one ? "[0,1]" : "[-1,1]");
        ImGui::Separator();
        ImGui::Text("frame   : %.2f ms (%.0f fps)", engine.frameTimeMs(), engine.fps());
        ImGui::Text("draws   : %u  tris %u  passes %u", st.draw_calls, st.triangles, st.passes);
        ImGui::Text("items   : %u opaque %u blended %u culled", st.opaque_items, st.transparent_items, st.culled_items);
        ImGui::Text("cmdbufs : %u (%u commands, %u batched quads)", st.command_buffers, st.commands, st.batched_quads);
        ImGui::Text("uniforms: %.1f KB / frame", st.uniform_bytes / 1024.0);
        ImGui::Text("library : %zu meshes, %zu materials", rs.scene().meshCount(), rs.scene().materialCount());
        ImGui::Text("world   : %zu entities, IBL %s", engine.world().entityCount(), rs.iblReady() ? "baked" : "fallback");
        if (engine.hasScript())
            ImGui::Text("lua     : %zu scripts, %.2f ms, %u errors", engine.script().loadedScripts(), engine.script().lastTickMs(),
                        engine.script().errorCount());
        if (engine.hasJS())
            ImGui::Text("js      : %zu scripts, %.2f ms, %u errors", engine.js().loadedScripts(), engine.js().lastTickMs(),
                        engine.js().errorCount());
        ImGui::Separator();
        const char *scenes[] = {"Showcase (C++)", "Built by Lua (RTTI)", "CommandBuffer demo (Lua)", "Physics playground",
                                "Cascade mini-game (package)", "Gaussian splat viewer", "Package scene (.gpkg)",
                                "Animation lab (2D sprite + 3D compute)", "Ray tracing (compute)",
                                "Media player (libavcodec)", "Empty"};
        int sc = (int)scene_;
        if (ImGui::Combo("scene", &sc, scenes, (int)SceneKind::Count)) pending_scene_ = (SceneKind)sc;
        if (ImGui::Button("Screenshot"))
        {
            std::string path = std::string("screenshot_") + RHI::backendTypeName(engine.device()->backend()) + ".png";
            engine.ui().pushLog(engine.saveScreenshot(path) ? "saved " + path : "screenshot FAILED");
        }
        ImGui::SameLine();
        if (ImGui::Button("Spawn Duck"))
            spawnFile(asset("models/Duck.glb"), "Duck", Math::Vec3(0.0f, 0.0f, 4.5f), 0.01f);
        ImGui::SameLine();
        if (ImGui::Button("Quit")) engine.requestQuit();
        ImGui::End();
    }

    void DemoApp::drawRenderPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Render settings", &show_render_)) { ImGui::End(); return; }
        ImGui::TextDisabled("generated from the RenderSettings RTTI");
        UI::inspectObject(Reflect::refRaw(&engine.render().settings()), {"pipeline"});
        sectionTitle("lights");
        ImGui::Checkbox("animate point lights", &animate_lights_);
        ImGui::SliderFloat("sun elevation", &sun_elevation_, -10.0f, 90.0f, "%.1f deg");
        ImGui::SliderFloat("sun azimuth", &sun_azimuth_, 0.0f, 360.0f, "%.1f deg");
        if (ImGui::Button("Re-bake IBL"))
        {
            std::string hdr = asset("textures/sky_equirect.hdr");
            bool ok = ResourceIO::fileExists(hdr) ? engine.render().bakeIBL(hdr, {}) : engine.render().bakeProceduralIBL({});
            engine.ui().pushLog(ok ? "IBL re-baked" : "IBL bake FAILED");
        }
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawInspectorPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Hierarchy / Inspector", &show_inspector_)) { ImGui::End(); return; }
        ECS::World &world = engine.world();
        if (ImGui::BeginChild("hierarchy", ImVec2(0, 170), true))
        {
            for (ECS::Entity e : world.entities())
            {
                if (world.parentOf(e).valid()) continue;
                std::vector<ECS::Entity> stack{e};
                std::vector<int> depth{0};
                while (!stack.empty())
                {
                    ECS::Entity cur = stack.back();
                    int d = depth.back();
                    stack.pop_back();
                    depth.pop_back();
                    char label[192];
                    std::snprintf(label, sizeof(label), "%*s%s##%u", d * 2, "", world.entityName(cur).c_str(), cur.id);
                    if (ImGui::Selectable(label, selected_ == cur)) selected_ = cur;
                    std::vector<ECS::Entity> kids = world.childrenOf(cur);
                    for (auto it = kids.rbegin(); it != kids.rend(); ++it)
                    {
                        stack.push_back(*it);
                        depth.push_back(d + 1);
                    }
                }
            }
        }
        ImGui::EndChild();

        if (!world.isAlive(selected_))
        {
            ImGui::TextDisabled("select an entity");
            ImGui::End();
            return;
        }
        Reflect::Value ev = Reflect::entityValue(&world, selected_);
        if (const Reflect::ObjectRef *ref = ev.asObject())
        {
            ImGui::PushID("entity");
            UI::inspectObject(*ref, {"id", "children", "parent"});
            ImGui::PopID();
        }
        for (const Reflect::ComponentBinding &cb : Reflect::componentBindings())
        {
            if (!cb.has(world, selected_)) continue;
            ImGui::PushID(cb.name.c_str());
            bool open = ImGui::CollapsingHeader(cb.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
            if (ImGui::BeginPopupContextItem())
            {
                if (ImGui::MenuItem("remove component")) cb.remove(world, selected_);
                ImGui::EndPopup();
            }
            if (open && cb.has(world, selected_))
            {
                Reflect::Value v = cb.get(world, selected_);
                if (const Reflect::ObjectRef *r = v.asObject()) UI::inspectObject(*r, {"code"});
            }
            ImGui::PopID();
        }
        if (Render::MaterialPtr m = entityMaterial(selected_))
        {
            ImGui::Separator();
            ImGui::Text("material: %s", m->name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("edit"))
            {
                material_index_ = m->index;
                show_material_ = true;
                scroll_to_material_ = true;
            }
        }
        ImGui::Separator();
        static int add_index = 0;
        std::vector<const char *> names;
        for (const Reflect::ComponentBinding &cb : Reflect::componentBindings()) names.push_back(cb.name.c_str());
        ImGui::SetNextItemWidth(140);
        ImGui::Combo("##add", &add_index, names.data(), (int)names.size());
        ImGui::SameLine();
        if (ImGui::Button("Add component") && add_index < (int)names.size())
        {
            const Reflect::ComponentBinding &cb = Reflect::componentBindings()[add_index];
            if (!cb.has(world, selected_)) cb.add(world, selected_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Destroy"))
        {
            engine.device()->waitIdle();
            world.destroyEntity(selected_);
            selected_ = ECS::nullEntity();
        }
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawTextureSlot(const Render::MaterialPtr &mat, Render::TextureSlot slot)
    {
        Render::Material::Slot &s = mat->slots[(size_t)slot];
        ImGui::PushID((int)slot);
        const float thumb = 44.0f;
        if (s.render_texture && s.render_texture->valid())
            UI::image(s.render_texture, thumb, thumb);
        else if (s.texture.valid())
            ImGui::Image((ImTextureID)s.texture.texture, ImVec2(thumb, thumb));
        else
            ImGui::Dummy(ImVec2(thumb, thumb));
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::Text("%s", Render::textureSlotName(slot));
        std::string current = s.source.empty() ? "(none)" : ResourceIO::pathFilename(s.source);
        ImGui::SetNextItemWidth(170);
        if (ImGui::BeginCombo("##src", current.c_str()))
        {
            if (ImGui::Selectable("(none)", s.source.empty())) mat->clearTexture(slot);
            for (const std::string &f : texture_files_)
                if (ImGui::Selectable(ResourceIO::pathFilename(f).c_str(), s.source == f)) mat->setTexture(slot, f);
            ImGui::Separator();
            if (ImGui::Selectable("render texture: security camera", s.render_texture == monitor_rt_))
                mat->setRenderTexture(slot, monitor_rt_);
            if (ImGui::Selectable("render texture: custom pipeline LDR", s.render_texture == custom_ldr_))
                mat->setRenderTexture(slot, custom_ldr_);
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        bool enabled = s.enabled;
        if (ImGui::Checkbox("on", &enabled)) mat->setTextureEnabled(slot, enabled);
        ImGui::EndGroup();
        ImGui::PopID();
    }

    void DemoApp::drawMaterialPanel()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 390, 30), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(380, io.DisplaySize.y - 40), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Material editor", &show_material_)) { ImGui::End(); return; }
        Render::RenderScene &scene = engine.render().scene();

        // ---- library -------------------------------------------------------
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "filter materials", material_filter_, sizeof(material_filter_));
        if (ImGui::BeginListBox("##materials", ImVec2(-1, 120)))
        {
            for (size_t i = 0; i < scene.materialCount(); ++i)
            {
                Render::Material *m = scene.material((int32_t)i);
                if (!m) continue;
                if (material_filter_[0] && m->name.find(material_filter_) == std::string::npos) continue;
                char label[160];
                std::snprintf(label, sizeof(label), "%s##%zu", m->name.c_str(), i);
                if (ImGui::Selectable(label, material_index_ == (int)i)) material_index_ = (int)i;
                if (scroll_to_material_ && material_index_ == (int)i)
                {
                    ImGui::SetScrollHereY(0.5f);
                    scroll_to_material_ = false;
                }
            }
            ImGui::EndListBox();
        }
        Render::MaterialPtr mat = selectedMaterial();
        if (ImGui::Button("New"))
        {
            Render::MaterialPtr m = scene.createMaterial("material_" + std::to_string(scene.materialCount()));
            material_index_ = m->index;
            scroll_to_material_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate") && mat)
        {
            Render::MaterialPtr m = scene.cloneMaterial(*mat, mat->name + " copy");
            if (m) material_index_ = m->index;
            scroll_to_material_ = true;
        }
        ImGui::SameLine();
        bool can_assign = mat && engine.world().hasComponent<ECS::MeshComponent>(selected_);
        if (!can_assign) ImGui::BeginDisabled();
        if (ImGui::Button("Assign to selected") && can_assign)
        {
            if (ECS::MeshComponent *mc = engine.world().getComponent<ECS::MeshComponent>(selected_))
                mc->material_overrides.assign(mc->model ? mc->model->meshes.size() : 1, mat->index);
        }
        if (!can_assign) ImGui::EndDisabled();
        if (!mat)
        {
            ImGui::End();
            return;
        }

        // ---- live preview ----------------------------------------------------
        sectionTitle("live preview (render texture + command buffer)");
        const float pw = std::min(ImGui::GetContentRegionAvail().x, 240.0f);
        UI::image(preview_ldr_, pw, pw);
        ImGui::SameLine();
        ImGui::BeginGroup();
        const char *meshes[] = {"sphere", "box", "torus", "helmet"};
        ImGui::SetNextItemWidth(90);
        ImGui::Combo("##mesh", &preview_mesh_, meshes, 4);
        ImGui::Checkbox("rotate", &preview_rotate_);
        ImGui::Checkbox("sky", &preview_sky_);
        ImGui::EndGroup();

        // ---- parameters through the RTTI ---------------------------------------
        sectionTitle("parameters (reflected, applied live)");
        UI::inspectObject(Reflect::refShared(mat), {"index"});

        sectionTitle("textures");
        for (uint32_t i = 0; i < (uint32_t)Render::TextureSlot::Count; ++i) drawTextureSlot(mat, (Render::TextureSlot)i);
        ImGui::TextDisabled("every entity using \"%s\" updates immediately", mat->name.c_str());
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawCommandBufferPanel()
    {
        leftColumn();
        if (!ImGui::Begin("CommandBuffer lab", &show_cb_)) { ImGui::End(); return; }
        Render::RenderSystem &rs = engine.render();
        Render::RenderSettings &s = rs.settings();

        sectionTitle("1. render to texture (injected at before_scene)");
        ImGui::Checkbox("security camera", &monitor_enabled_);
        if (monitor_enabled_) UI::image(monitor_rt_, 256, 144);
        ImGui::TextWrapped("A second view renders the scene into a RenderTexture; the monitor mesh's unlit material "
                           "samples it (feedback into itself is skipped by the renderer).");

        sectionTitle("2. built-in post effect (forward pipeline)");
        std::vector<const char *> fx;
        for (uint32_t i = 0; i < (uint32_t)Render::PostEffect::Count; ++i) fx.push_back(Render::postEffectName((Render::PostEffect)i));
        int effect = (int)s.post_effect;
        if (ImGui::Combo("effect", &effect, fx.data(), (int)fx.size())) s.post_effect = (Render::PostEffect)effect;
        ImGui::SliderFloat("strength", &s.post_params.x, 0.0f, 1.0f);
        ImGui::SliderFloat("radius / size", &s.post_params.y, 0.5f, 16.0f);
        colorEdit("tint", s.post_tint);

        sectionTitle("3. programmable pipeline (PipelineMode::Custom)");
        bool custom = custom_pipeline_;
        if (ImGui::Checkbox("replace the forward pipeline by a command buffer", &custom)) custom_pipeline_ = custom;
        ImGui::Combo("effect A", &custom_effect_a_, fx.data(), (int)fx.size());
        ImGui::Combo("effect B", &custom_effect_b_, fx.data(), (int)fx.size());
        ImGui::Checkbox("picture in picture", &custom_pip_);
        ImGui::SameLine();
        ImGui::Checkbox("split view", &custom_split_);
        ImGui::TextWrapped("scene -> HDR RT -> tonemap -> effect A -> effect B -> backbuffer, then PiP / split and text "
                           "overlays, all recorded by the demo every frame and executed by the renderer.");

        sectionTitle("registered command buffers");
        for (uint32_t ev = 0; ev < (uint32_t)Render::CameraEvent::Count; ++ev)
            for (const Render::CommandBufferPtr &cb : rs.commandBuffers((Render::CameraEvent)ev))
                ImGui::BulletText("%-16s %-18s %zu cmds", Render::cameraEventName((Render::CameraEvent)ev), cb->name().c_str(), cb->size());
        for (const Render::CommandBufferPtr &cb : rs.customCommandBuffers())
            ImGui::BulletText("%-16s %-18s %zu cmds", "custom", cb->name().c_str(), cb->size());
        const Render::RenderStats &st = rs.stats();
        ImGui::Text("executed: %u buffers, %u commands, %u passes", st.command_buffers, st.commands, st.passes);
        ImGui::Text("batched quads %u, feedback skips %u", st.batched_quads, st.feedback_skips);
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawTextPanel()
    {
        leftColumn();
        if (!ImGui::Begin("SDF text lab", &show_text_)) { ImGui::End(); return; }
        Render::RenderSystem &rs = engine.render();

        sectionTitle("2D screen text (overlay command buffer)");
        ImGui::Checkbox("enabled##2d", &text2d_enabled_);
        ImGui::InputText("text##2d", text2d_, sizeof(text2d_));
        ImGui::SliderFloat("size px", &text2d_size_, 8.0f, 160.0f);
        ImGui::DragFloat2("position", text2d_pos_, 1.0f, 0.0f, 2000.0f);
        colorEdit("color##2d", text2d_color_, true);
        ImGui::SliderFloat("outline##2d", &text2d_outline_, 0.0f, 0.4f);
        colorEdit("outline color##2d", text2d_outline_color_, true);
        const char *aligns[] = {"left", "center", "right"};
        ImGui::Combo("align", &text2d_align_, aligns, 3);

        sectionTitle("3D world text");
        ImGui::Checkbox("enabled##3d", &text3d_enabled_);
        ImGui::InputText("text##3d", text3d_, sizeof(text3d_));
        ImGui::SliderFloat("size (world)", &text3d_size_, 0.05f, 2.0f);
        ImGui::DragFloat3("position##3d", &text3d_pos_.x, 0.05f);
        colorEdit("color##3d", text3d_color_, true);
        ImGui::SliderFloat("outline##3d", &text3d_outline_, 0.0f, 0.4f);
        ImGui::Checkbox("billboard", &text3d_billboard_);
        ImGui::SameLine();
        ImGui::Checkbox("spin", &text3d_spin_);
        ImGui::SameLine();
        ImGui::Checkbox("depth test", &text3d_depth_);
        ImGui::Checkbox("Text components (entity labels)", &rs.settings().draw_text_components);

        if (Text::Font *font = rs.font())
        {
            sectionTitle("signed distance atlas (single channel, shown red)");
            ImGui::Image((ImTextureID)font->atlas(), ImVec2(256, 256));
        }
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawPhysicsPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Physics lab", &show_physics_)) { ImGui::End(); return; }
        Physics::PhysicsSystem &physics = engine.physics();
        Physics::PhysicsSettings &ps = physics.settings();
        ImGui::TextDisabled("sequential impulses - sphere / box / plane / capsule + hinges");
        ImGui::Checkbox("enabled", &ps.enabled);
        ImGui::SameLine();
        bool paused = physics.paused();
        if (ImGui::Checkbox("pause", &paused)) physics.setPaused(paused);
        ImGui::SameLine();
        if (ImGui::Button("step") && physics.paused()) physics.stepOnce(engine.world());
        ImGui::Checkbox("debug shapes", &physics_debug_);
        ImGui::SetNextItemWidth(150);
        ImGui::DragFloat("gravity Y", &ps.gravity.y, 0.05f, -30.0f, 0.0f, "%.2f");
        int iterations = (int)ps.solver_iterations;
        if (ImGui::SliderInt("iterations", &iterations, 1, 20)) ps.solver_iterations = (uint32_t)iterations;
        ImGui::Text("bodies  : %zu (%zu dynamic)", physics.bodyCount(), physics.dynamicBodyCount());
        ImGui::Text("contacts: %zu  triggers: %zu", physics.contactCount(), physics.triggerCount());
        ImGui::Text("solver  : %.3f ms, %llu steps", physics.stepTimeMs(),
                    (unsigned long long)physics.stepCount());

        sectionTitle("spawn");
        const char *kinds[] = {"sphere", "box", "capsule"};
        ImGui::SetNextItemWidth(110);
        ImGui::Combo("shape", &physics_spawn_kind_, kinds, 3);
        ImGui::DragFloat("height", &physics_spawn_height_, 0.1f, 1.0f, 20.0f);
        ImGui::DragFloat("spread", &physics_spawn_spread_, 0.05f, 0.0f, 5.0f);
        if (ImGui::Button("Spawn body"))
        {
            Render::RenderScene &scene = engine.render().scene();
            static const char *names[] = {"Physics balls", "Physics crates", "Physics capsule"};
            Render::MaterialPtr mat = scene.findMaterial(names[physics_spawn_kind_]);
            if (!mat)
            {
                mat = scene.createMaterial(names[physics_spawn_kind_]);
                mat->base_color = Math::Color(0.9f, 0.5f, 0.2f, 1.0f);
                mat->roughness = 0.45f;
            }
            const float offset = (float)((physics.stepCount() * 37) % 21 - 10) * 0.08f;
            const Math::Vec3 position(offset * physics_spawn_spread_, physics_spawn_height_, 2.0f + offset * 0.3f);
            const char *kinds2[] = {"sphere", "box", "capsule"};
            std::string name = "Spawned_" + std::to_string(engine.world().entityCount());
            ECS::Entity e = spawnMesh(name, Reflect::createMeshAsset(kinds2[physics_spawn_kind_], 1.0f, 24),
                                      mat, position);
            ECS::RigidBodyComponent rb;
            rb.type = ECS::BodyType::Dynamic;
            rb.mass = 0.8f;
            rb.restitution = 0.45f;
            engine.world().addComponent<ECS::RigidBodyComponent>(e, rb);
            ECS::ColliderComponent col;
            if (physics_spawn_kind_ == 0) { col.shape = ECS::ColliderShape::Sphere; col.radius = 0.5f; }
            else if (physics_spawn_kind_ == 1) { col.shape = ECS::ColliderShape::Box; col.half_extents = Math::Vec3(0.5f); }
            else { col.shape = ECS::ColliderShape::Capsule; col.radius = 0.4f; col.height = 2.0f; }
            engine.world().addComponent<ECS::ColliderComponent>(e, col);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset playground")) pending_scene_ = SceneKind::Physics;
        if (engine.hasPhysics())
        {
            ImGui::TextDisabled("Hinge demo: the brass bar is constrained to a world anchor.");
        }
        if (ImGui::CollapsingHeader("recent contacts"))
        {
            const auto &contacts = physics.contacts();
            for (size_t i = 0; i < contacts.size() && i < 12; ++i)
            {
                const Physics::Contact &c = contacts[i];
                ImGui::BulletText("%s / %s  pen %.3f%s", engine.world().entityName(c.a).c_str(),
                                  engine.world().entityName(c.b).c_str(), c.penetration,
                                  c.trigger ? " (trigger)" : "");
            }
        }
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawGaussianPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Gaussian splat lab", &show_gaussian_)) { ImGui::End(); return; }
        ImGui::TextDisabled("screen-space sorted gaussian splats (CPU sort, RHI batch)");
        ImGui::SetNextItemWidth(-90);
        ImGui::InputText("##path", gaussian_path_input_, sizeof(gaussian_path_input_));
        ImGui::SameLine();
        if (ImGui::Button("Load"))
        {
            gaussian_path_ = gaussian_path_input_;
            std::string error;
            if (!gaussian_cloud_.loadPly(gaussian_path_, &error))
                engine.ui().pushLog("splat load failed: " + error);
            else
                pending_scene_ = SceneKind::Gaussian;
        }
        ImGui::Checkbox("auto orbit", &gaussian_auto_orbit_);
        ImGui::SliderFloat("size", &gaussian_settings_.size_scale, 0.1f, 4.0f);
        ImGui::SliderFloat("opacity", &gaussian_settings_.opacity_scale, 0.0f, 2.0f);
        ImGui::SliderFloat("max radius", &gaussian_settings_.max_screen_radius, 2.0f, 400.0f);
        int max_splats = (int)gaussian_settings_.max_splats;
        if (ImGui::DragInt("max splats", &max_splats, 100.0f, 0, (int)std::max<size_t>(1, gaussian_cloud_.count())))
            gaussian_settings_.max_splats = (uint32_t)std::max(0, max_splats);
        ImGui::Checkbox("depth sort", &gaussian_settings_.sort);
        ImGui::SameLine();
        ImGui::Checkbox("colour", &gaussian_settings_.colored);
        colorEdit("tint", gaussian_settings_.tint, true);

        const auto &load = gaussian_cloud_.stats();
        const auto &stats = gaussian_renderer_.stats();
        ImGui::Separator();
        ImGui::Text("file    : %s", gaussian_cloud_.sourcePath().c_str());
        ImGui::Text("load    : %llu vertices (%s, %s), %llu skipped",
                    (unsigned long long)load.vertices, load.binary ? "binary" : "ascii",
                    load.has_sh ? "3DGS" : "point cloud", (unsigned long long)load.skipped);
        ImGui::Text("render  : %zu / %zu visible, %zu drawn", stats.visible, stats.total, stats.drawn);
        ImGui::Text("timing  : project %.2f ms, sort %.2f ms", stats.project_ms, stats.sort_ms);
        ImGui::TextWrapped("The page is intentionally self contained: an anisotropic covariance / SH "
                           "extension can replace the projection and shader without touching the rest of "
                           "the engine. See docs/GAUSSIAN_SPLATTING.md.");
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawPackagePanel()
    {
        leftColumn();
        if (!ImGui::Begin("Asset package browser", &show_package_)) { ImGui::End(); return; }
        ImGui::TextDisabled("runtime .gpkg loading: scene / materials / 3-backend shaders / textures / scripts / config");
        if (package_files_.empty())
            ImGui::TextWrapped("No packages found. Build them with: python3 tools/genesis_pack.py build "
                               "assets/packages/cascade_src");
        if (!package_files_.empty())
        {
            if (package_index_ < 0) package_index_ = 0;
            if (package_index_ >= (int)package_files_.size()) package_index_ = (int)package_files_.size() - 1;
            std::vector<std::string> labels;
            for (const std::string &p : package_files_) labels.push_back(ResourceIO::pathFilename(p));
            std::vector<const char *> names;
            for (const std::string &label : labels) names.push_back(label.c_str());
            ImGui::SetNextItemWidth(-1);
            ImGui::Combo("##packages", &package_index_, names.data(), (int)names.size());
            if (ImGui::Button("Load package"))
            {
                if (loadPackage(package_files_[package_index_], nullptr))
                    pending_scene_ = SceneKind::Package;
            }
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) refreshPackageList();
            if (ImGui::Button("Open in scene"))
            {
                if (engine.activePackage()) pending_scene_ = SceneKind::Package;
                else if (loadPackage(package_files_[package_index_], nullptr)) pending_scene_ = SceneKind::Package;
            }
        }
        ImGui::Separator();
        ImGui::TextWrapped("%s", package_status_.c_str());
        if (engine.activePackage())
        {
            const AssetPackagePtr &package = engine.activePackage();
            ImGui::Text("name: %s  files: %zu  raw: %.1f KB", package->name().c_str(), package->entries().size(),
                        package->totalRawBytes() / 1024.0);
            if (ImGui::CollapsingHeader("manifest", ImGuiTreeNodeFlags_DefaultOpen))
                ImGui::TextWrapped("%s", package->manifestJson().c_str());
            if (ImGui::CollapsingHeader("contents"))
            {
                for (const PackageEntry &entry : package->entries())
                    ImGui::BulletText("%s  (%u B)", entry.path.c_str(), entry.raw_size);
            }
        }
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawGamePanel()
    {
        leftColumn();
        if (!ImGui::Begin("Cascade game", &show_game_)) { ImGui::End(); return; }
        ImGui::TextDisabled("physics mini-game - assets loaded from cascade.gpkg");
        ImGui::TextWrapped("%s", game_status_.c_str());
        ImGui::Separator();
        ImGui::BulletText("Aim with the mouse, click / SPACE to launch a ball");
        ImGui::BulletText("Knock the golden crates off the platform");
        ImGui::BulletText("R restarts the round, P toggles the physics debug view");
        ImGui::Separator();
        if (ImGui::Button("Restart round")) pending_scene_ = SceneKind::Game;
        ImGui::SameLine();
        if (ImGui::Button("Physics lab")) pending_scene_ = SceneKind::Physics;
        ImGui::SameLine();
        if (ImGui::Button("Package browser")) show_package_ = true;
        Physics::PhysicsSystem &physics = engine.physics();
        ImGui::Text("bodies %zu (dynamic %zu), contacts %zu", physics.bodyCount(), physics.dynamicBodyCount(),
                    physics.contactCount());
        ImGui::Checkbox("debug shapes", &physics_debug_);
        if (!engine.hasJS())
            ImGui::TextWrapped("JavaScript is unavailable in this build - the package ships a Lua fallback script.");
        ImGui::End();
    }

    namespace
    {
        std::vector<API::FrameImage> makeCodecFrames(int count)
        {
            std::vector<API::FrameImage> frames;
            for (int f = 0; f < count; ++f)
            {
                API::FrameImage frame;
                frame.width = 128;
                frame.height = 72;
                frame.rgba.resize((size_t)frame.width * frame.height * 4);
                const float cx = 32.0f + (float)f * 10.0f;
                const float cy = 36.0f + std::sin((float)f * 0.9f) * 12.0f;
                for (uint32_t y = 0; y < frame.height; ++y)
                    for (uint32_t x = 0; x < frame.width; ++x)
                    {
                        const size_t i = ((size_t)y * frame.width + x) * 4;
                        frame.rgba[i + 0] = (uint8_t)(x * 2);
                        frame.rgba[i + 1] = (uint8_t)(y * 3);
                        frame.rgba[i + 2] = (uint8_t)(40 + f * 18);
                        const float d = std::hypot((float)x - cx, (float)y - cy);
                        if (d < 14.0f)
                        {
                            frame.rgba[i + 0] = 255;
                            frame.rgba[i + 1] = (uint8_t)(140 + f * 10);
                            frame.rgba[i + 2] = 60;
                        }
                        frame.rgba[i + 3] = 255;
                    }
                frames.push_back(std::move(frame));
            }
            return frames;
        }
    } // namespace

    //======================================================================
    void DemoApp::drawAnimationPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Animation lab", &show_animation_)) { ImGui::End(); return; }
        Animation::AnimationSystem &anim = engine.animation();
        ImGui::TextDisabled("2D sprite-sheet sequence + 3D compute vertex animation");
        ECS::World &world = engine.world();
        ECS::Entity hud = world.findEntity("SpriteAnimation2D");
        ECS::SpriteAnimationComponent *hud_sprite = world.getComponent<ECS::SpriteAnimationComponent>(hud);
        ECS::SpriteAnimationComponent *world_sprite =
            world.getComponent<ECS::SpriteAnimationComponent>(anim_sprite_);
        ECS::VertexAnimationComponent *vertex =
            world.getComponent<ECS::VertexAnimationComponent>(anim_vertex_);

        sectionTitle("2D sequence frames");
        bool playing = hud_sprite ? hud_sprite->playing : sprite_playing_;
        if (ImGui::Checkbox("playing", &playing))
        {
            sprite_playing_ = playing;
            if (hud_sprite) hud_sprite->playing = playing;
            if (world_sprite) world_sprite->playing = playing;
        }
        if (ImGui::SliderFloat("fps", &sprite_fps_, 1.0f, 60.0f))
        {
            if (hud_sprite) hud_sprite->fps = sprite_fps_;
            if (world_sprite) world_sprite->fps = sprite_fps_;
        }
        if (hud_sprite)
        {
            ImGui::Text("frame %u / %u   columns %u x rows %u", hud_sprite->frame,
                        std::max(1u, hud_sprite->frame_count), hud_sprite->columns, hud_sprite->rows);
            if (ImGui::Button("prev")) hud_sprite->frame = (hud_sprite->frame + hud_sprite->frame_count - 1) % hud_sprite->frame_count;
            ImGui::SameLine();
            if (ImGui::Button("next")) hud_sprite->frame = (hud_sprite->frame + 1) % hud_sprite->frame_count;
            ImGui::SameLine();
            ImGui::TextDisabled("texture: textures/sprite_sheet.png");
        }

        sectionTitle("3D compute vertex animation");
        ImGui::Text("backend compute : %s", anim.computeAvailable() ? "available" : "unavailable");
        ImGui::Text("vertex animated : %zu   dispatches: %zu", anim.vertex_animations(), anim.dispatches());
        bool enabled = vertex ? vertex->enabled : vertex_animation_enabled_;
        if (ImGui::Checkbox("enabled##vertex", &enabled))
        {
            vertex_animation_enabled_ = enabled;
            if (vertex) vertex->enabled = enabled;
        }
        if (ImGui::SliderFloat("amplitude", &vertex_amplitude_, 0.0f, 1.2f) && vertex)
            vertex->amplitude = vertex_amplitude_;
        if (ImGui::SliderFloat("frequency", &vertex_frequency_, 0.0f, 8.0f) && vertex)
            vertex->frequency = vertex_frequency_;
        if (ImGui::SliderFloat("speed", &vertex_speed_, 0.0f, 8.0f) && vertex)
            vertex->speed = vertex_speed_;
        ImGui::TextWrapped("The torus vertex buffer is rewritten every frame by "
                           "anim/vertex_anim.comp (GLSL / SPIR-V / MSL). The mesh pass binds the "
                           "compute-written buffer after a ShaderWrite -> VertexAttributeRead barrier.");
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawRayTracingPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Ray tracing lab", &show_raytracing_)) { ImGui::End(); return; }
        API::RayTracingAPI &rt = engine.rayTracing();
        ImGui::TextDisabled("portable GPU ray tracing via the Compute API (compute shader path)");
        ImGui::Text("backend : %s", rt.backendName().c_str());
        ImGui::Text("native hardware RT : %s", rt.hardwareRayTracing() ? "active" : "not available (compute path)");
        ImGui::Checkbox("auto trace", &rt_auto_trace_);
        ImGui::SameLine();
        if (ImGui::Button("Trace now")) rt_trace_requested_ = true;
        int width = (int)rt_settings_.width;
        int height = (int)rt_settings_.height;
        int samples = (int)rt_settings_.samples;
        bool changed = false;
        ImGui::SetNextItemWidth(120);
        if (ImGui::DragInt("width", &width, 8.0f, 64, 1280)) changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        if (ImGui::DragInt("height", &height, 8.0f, 36, 720)) changed = true;
        if (ImGui::SliderInt("samples", &samples, 1, 8)) changed = true;
        if (changed)
        {
            rt_settings_.width = (uint32_t)std::max(16, width);
            rt_settings_.height = (uint32_t)std::max(16, height);
            rt_settings_.samples = (uint32_t)std::max(1, samples);
            rt_settings_.width &= ~7u;
            rt_settings_.height &= ~7u;
            rt_trace_requested_ = true;
        }
        ImGui::Text("last    : %.2f ms  (%s)", rt_last_ms_, rt_status_.c_str());
        ImGui::Text("image   : %ux%u, %u spp, %u spheres", rt.stats().width, rt.stats().height,
                    rt.stats().samples, rt.stats().spheres);
        ImGui::Text("GPU     : %s", rt.stats().gpu ? "yes (compute)" : "no (CPU fallback)");
        if (ImGui::Button("Add sphere"))
        {
            API::RTSphere sphere;
            const float a = (float)rt_scene_.size() * 0.9f;
            sphere.center = Math::Vec3(std::cos(a) * 2.8f, 1.2f + 0.2f * (float)(rt_scene_.size() % 3),
                                       std::sin(a) * 2.8f - 1.0f);
            sphere.radius = 0.35f + 0.05f * (float)(rt_scene_.size() % 4);
            sphere.color = Math::Color(0.9f, 0.5f, 0.2f + 0.05f * (float)rt_scene_.size(), 1.0f);
            rt_scene_.push_back(sphere);
            rt.setScene(rt_scene_);
            rt_trace_requested_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset spheres")) pending_scene_ = SceneKind::RayTracing;
        ImGui::TextWrapped("The compute kernel intersects spheres and a ground plane, traces hard "
                           "shadows and one reflection bounce. VK_KHR_ray_tracing / Metal ray "
                           "tracing can replace the backend behind this same API.");
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawCodecPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Encoder / Decoder lab", &show_codec_)) { ImGui::End(); return; }
        API::EncoderDecoderAPI &codec = engine.encoderDecoder();
        ImGui::TextDisabled("hardware-capable encode/decode API (NVENC / VA-API / QSV / VideoToolbox + software)");
        if (ImGui::BeginTable("codecs", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("codec");
            ImGui::TableSetupColumn("hardware");
            ImGui::TableSetupColumn("available");
            ImGui::TableSetupColumn("detail");
            ImGui::TableHeadersRow();
            for (const API::CodecInfo &info : codec.codecs())
            {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%s", info.name.c_str());
                ImGui::TableSetColumnIndex(1); ImGui::Text("%s", info.hardware ? "yes" : "-");
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(info.available ? ImVec4(0.5f, 1, 0.6f, 1) : ImVec4(1, 0.5f, 0.5f, 1),
                                   "%s", info.available ? "yes" : "no");
                ImGui::TableSetColumnIndex(3); ImGui::Text("%s", info.detail.c_str());
            }
            ImGui::EndTable();
        }
        if (ImGui::Button("Raw sequence roundtrip"))
        {
            auto frames = makeCodecFrames(8);
            API::CodecOptions options;
            options.codec = API::Codec::Raw;
            std::string error;
            const bool ok = codec.encodeSequence(frames, "/tmp/genesis_codec_ui.gseq", options, &error);
            std::vector<API::FrameImage> decoded;
            if (ok && codec.decodeSequence("/tmp/genesis_codec_ui.gseq", decoded, &error) && decoded.size() == frames.size())
                codec_status_ = "raw: encoded+decoded " + std::to_string(decoded.size()) + " frames";
            else
                codec_status_ = "raw failed: " + error;
        }
        ImGui::SameLine();
        if (ImGui::Button("H.264 sequence roundtrip"))
        {
            auto frames = makeCodecFrames(10);
            API::CodecOptions options;
            options.codec = API::Codec::H264;
            options.fps = 15;
            options.prefer_hardware = true;
            std::string error;
            const bool ok = codec.encodeSequence(frames, "/tmp/genesis_codec_ui.mp4", options, &error);
            std::vector<API::FrameImage> decoded;
            if (ok && codec.decodeSequence("/tmp/genesis_codec_ui.mp4", decoded, &error))
                codec_status_ = "H.264: " + codec.lastBackend() + ", decoded " + std::to_string(decoded.size()) + " frames";
            else
                codec_status_ = "H.264 failed: " + error;
        }
        if (ImGui::Button("Encode current frame (PNG)"))
        {
            codec_capture_requested_ = true;
            codec_status_ = "capturing the next completed frame...";
        }
        ImGui::TextWrapped("%s", codec_status_.c_str());
        const API::CodecStats &stats = codec.stats();
        ImGui::Text("last backend : %s", stats.backend.c_str());
        ImGui::Text("encode %.2f ms   decode %.2f ms", stats.encode_ms, stats.decode_ms);
        ImGui::Text("frames %u  %ux%u", stats.frames, stats.width, stats.height);
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawApisPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Engine APIs (4)", &show_apis_)) { ImGui::End(); return; }
        ImGui::TextDisabled("Rendering / Compute / Ray Tracing / Encoder & Decoder");
        sectionTitle("1. Rendering API");
        ImGui::BulletText("backend: %s  %ux%u", engine.rendering().backendName(), engine.rendering().width(),
                          engine.rendering().height());
        ImGui::BulletText("draws: %u  triangles: %u", engine.rendering().stats().draw_calls,
                          engine.rendering().stats().triangles);
        sectionTitle("2. Compute API");
        ImGui::BulletText("hardware compute: %s", engine.compute().supportsHardwareCompute() ? "yes" : "no");
        ImGui::BulletText("animation dispatches: %zu", engine.animation().dispatches());
        ImGui::BulletText("vertex animated meshes: %zu", engine.animation().vertex_animations());
        sectionTitle("3. Ray Tracing API");
        ImGui::BulletText("backend: %s", engine.rayTracing().backendName().c_str());
        ImGui::BulletText("hardware RT: %s", engine.rayTracing().hardwareRayTracing() ? "yes" : "no (compute)");
        ImGui::BulletText("last trace: %.2f ms", engine.rayTracing().stats().trace_ms);
        sectionTitle("4. Encoder & Decoder API");
        ImGui::BulletText("ffmpeg: %s", engine.encoderDecoder().available() ? "available" : "not found");
        ImGui::BulletText("codecs: %zu", engine.encoderDecoder().codecs().size());
        ImGui::BulletText("last codec: %s", engine.encoderDecoder().lastBackend().c_str());
        ImGui::Separator();
        ImGui::TextWrapped("The four facade APIs delegate to the existing RHI/RenderSystem/systems; "
                           "the original demo scenes and tests keep using the same implementation.");
        ImGui::End();
    }

    //======================================================================
    void DemoApp::drawMediaPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Media player", &show_media_)) { ImGui::End(); return; }
        ImGui::TextDisabled("libavformat + libavcodec + libswscale + libswresample, in-process; SDL audio");
        ImGui::SetNextItemWidth(-90);
        ImGui::InputText("##media_path", media_path_, sizeof(media_path_));
        ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
            pending_scene_ = SceneKind::Media;
        }
        Media::MediaPlayer *player = media_player_.get();
        if (!player || !player->isOpen())
        {
            ImGui::TextWrapped("%s", media_status_.c_str());
            ImGui::TextWrapped("The H.264 decoder and the audio decoder are linked directly into the "
                               "engine; no ffmpeg subprocess is started at run time.");
            ImGui::End();
            return;
        }
        const Media::MediaInfo &info = player->info();
        ImGui::Text("video : %s  %dx%d @ %.1f fps", info.video_codec.c_str(), info.width, info.height, info.fps);
        ImGui::Text("audio : %s  %d Hz  %d ch  |  SDL device: %s", info.audio_codec.c_str(), info.sample_rate,
                    info.channels, player->audioDeviceOpen() ? "open" : "off");
        ImGui::Text("duration: %.2f s   queued audio samples: %zu", info.duration, player->queuedAudioSamples());
        if (ImGui::Button(player->playing() ? "Pause" : "Play")) player->toggle();
        ImGui::SameLine();
        if (ImGui::Button("Restart")) player->seek(0.0);
        float pos = (float)player->position();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::SliderFloat("##seek", &pos, 0.0f, (float)std::max(0.01, info.duration)))
            player->seek(pos);
        if (ImGui::SliderFloat("volume", &media_volume_, 0.0f, 1.0f)) player->setVolume(media_volume_);
        ImGui::Checkbox("debug / no audio device", &no_audio_device_);
        if (ImGui::Button("Reopen with audio setting"))
        {
            const std::string path = media_path_;
            player->close();
            std::string error;
            player->open(path, !no_audio_device_, &error);
            player->setVolume(media_volume_);
            player->play();
        }
        ImGui::TextWrapped("Decoded video is uploaded to an RHI texture and drawn with the batch path; "
                           "decoded audio is resampled to 48 kHz S16 stereo and played by SDL.");
        ImGui::End();
    }

    //======================================================================
    void DemoApp::runConsole(const std::string &code)
    {
        if (code.empty()) return;
        if (console_language_ == 1)
        {
            if (!engine.hasJS()) return;
            GEN_LOG_INFO("[JS] > %s", code.c_str());
            engine.js().executeString(code, "=console");
            console_history_.push_back(code);
            return;
        }
        if (!engine.hasScript()) return;
        GEN_LOG_INFO("[Lua] > %s", code.c_str());
        lua_State *L = engine.script().state();
        // Expressions print their value; statements just run.
        std::string expr = "return " + code;
        bool is_expression = luaL_loadbuffer(L, expr.c_str(), expr.size(), "=console") == LUA_OK;
        lua_pop(L, 1);
        engine.script().executeString(is_expression ? "print(" + code + ")" : code, "=console");
        console_history_.push_back(code);
    }

    void DemoApp::drawConsolePanel()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(340, io.DisplaySize.y - 330), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(560, 320), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Lua / JS console", &show_console_)) { ImGui::End(); return; }
        const bool lua_ok = engine.hasScript();
        const bool js_ok = engine.hasJS();
        if (!lua_ok && !js_ok)
        {
            ImGui::TextDisabled("scripting disabled (--no-script / no V8)");
            ImGui::End();
            return;
        }
        const char *languages[] = {"Lua 5.4", "JavaScript (V8)"};
        ImGui::SetNextItemWidth(180);
        ImGui::Combo("language", &console_language_, languages, 2);
        if (console_language_ == 0 && !lua_ok) console_language_ = 1;
        if (console_language_ == 1 && !js_ok) console_language_ = 0;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("##examples", "examples..."))
        {
            const int count = console_language_ == 1 ? (int)(sizeof(kJsConsoleExamples) / sizeof(kJsConsoleExamples[0]))
                                                     : (int)(sizeof(kConsoleExamples) / sizeof(kConsoleExamples[0]));
            for (int i = 0; i < count; ++i)
            {
                const char *label = console_language_ == 1 ? kJsConsoleExamples[i][0] : kConsoleExamples[i][0];
                const char *code = console_language_ == 1 ? kJsConsoleExamples[i][1] : kConsoleExamples[i][1];
                if (ImGui::Selectable(label)) std::snprintf(console_, sizeof(console_), "%s", code);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        bool run = ImGui::Button("Run (Ctrl+Enter)");
        ImGui::SameLine();
        if (ImGui::Button("Clear")) console_[0] = '\0';
        ImGui::InputTextMultiline("##code", console_, sizeof(console_), ImVec2(-1, 110));
        if (ImGui::IsItemFocused() && io.KeyCtrl && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter))) run = true;
        if (run) runConsole(console_);

        // Output: the Lua related part of the engine log (print() is routed there).
        ImGui::BeginChild("output", ImVec2(0, 0), true);
        auto history = Logger::instance().history();
        std::vector<size_t> lua_lines;
        const char *tag = console_language_ == 1 ? "[JS]" : "[Lua]";
        for (size_t i = 0; i < history.size(); ++i)
            if (history[i].text.find(tag) != std::string::npos) lua_lines.push_back(i);
        const size_t first = lua_lines.size() > 120 ? lua_lines.size() - 120 : 0;
        for (size_t k = first; k < lua_lines.size(); ++k)
        {
            const auto &entry = history[lua_lines[k]];
            ImVec4 c = entry.level >= LogLevel::Error ? ImVec4(1, 0.45f, 0.45f, 1) : ImVec4(0.85f, 0.9f, 0.85f, 1);
            ImGui::TextColored(c, "%s", entry.text.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::End();
    }

    int DemoApp::selfTest()
    {
        const uint32_t errors_before = Logger::instance().errorCount();
        auto frames = [&](int n)
        {
            for (int i = 0; i < n; ++i)
            {
                engine.tick();
                tick(1.0f / 60.0f);
            }
        };
        show_engine_ = show_render_ = show_inspector_ = show_material_ = show_cb_ = show_text_ = show_console_ = show_log_ =
            show_help_ = show_physics_ = show_gaussian_ = show_package_ = show_game_ = true;
        show_animation_ = show_raytracing_ = show_codec_ = show_apis_ = show_media_ = true;
        for (uint32_t k = 0; k < (uint32_t)SceneKind::Count; ++k)
        {
            GEN_LOG_INFO("[selftest] scene %s", sceneName((SceneKind)k));
            pending_scene_ = (SceneKind)k;
            frames(8);
            custom_pipeline_ = true;
            frames(4);
            custom_split_ = true;
            custom_pip_ = true;
            custom_effect_b_ = (int)Render::PostEffect::Vignette;
            frames(3);
            custom_pipeline_ = custom_split_ = false;
            frames(2);
            for (uint32_t fx = 0; fx < (uint32_t)Render::PostEffect::Count; ++fx)
            {
                engine.render().settings().post_effect = (Render::PostEffect)fx;
                frames(1);
            }
            engine.render().settings().post_effect = Render::PostEffect::None;
            console_language_ = 0;
            for (const auto &ex : kConsoleExamples)
            {
                runConsole(ex[1]);
                frames(1);
            }
            if (engine.hasJS())
            {
                console_language_ = 1;
                for (const auto &ex : kJsConsoleExamples)
                {
                    runConsole(ex[1]);
                    frames(1);
                }
                console_language_ = 0;
            }
            engine.physics().setPaused(true);
            engine.physics().stepOnce(engine.world());
            frames(1);
            engine.physics().setPaused(false);
            Render::RenderScene &scene = engine.render().scene();
            Render::MaterialPtr m = scene.createMaterial("selftest_material");
            material_index_ = m->index;
            m->setRenderTexture(Render::TextureSlot::BaseColor, monitor_rt_);
            Render::MaterialPtr copy = scene.cloneMaterial(*m, "selftest_copy");
            if (ECS::MeshComponent *mc = engine.world().getComponent<ECS::MeshComponent>(selected_))
                mc->material_overrides.assign(mc->model ? mc->model->meshes.size() : 1, copy->index);
            for (preview_mesh_ = 0; preview_mesh_ < 4; ++preview_mesh_) frames(1);
            preview_mesh_ = 0;
            if (engine.world().isAlive(selected_))
            {
                engine.device()->waitIdle();
                engine.world().destroyEntity(selected_);
                selected_ = ECS::nullEntity();
            }
            frames(3);
        }
        int errors = (int)(Logger::instance().errorCount() - errors_before);
        if (engine.hasScript()) errors += (int)engine.script().errorCount();
        if (engine.hasJS()) errors += (int)engine.js().errorCount();
        GEN_LOG_INFO("[selftest] done: %d error(s)", errors);
        return errors;
    }

    void DemoApp::drawLogPanel()
    {
        ImGuiIO &io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 760, io.DisplaySize.y - 260), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(360, 250), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("Log", &show_log_)) { ImGui::End(); return; }
        auto history = Logger::instance().history();
        size_t start = history.size() > 200 ? history.size() - 200 : 0;
        for (size_t i = start; i < history.size(); ++i)
        {
            ImVec4 color(0.85f, 0.85f, 0.85f, 1.0f);
            if (history[i].level == LogLevel::Warn) color = ImVec4(1.0f, 0.8f, 0.3f, 1.0f);
            else if (history[i].level >= LogLevel::Error) color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
            ImGui::TextColored(color, "%s", history[i].text.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
        ImGui::End();
    }

    void DemoApp::drawHelpPanel()
    {
        leftColumn();
        if (!ImGui::Begin("Controls", &show_help_)) { ImGui::End(); return; }
        ImGui::BulletText("left mouse drag : orbit camera, wheel: zoom");
        ImGui::BulletText("F1 : toggle UI");
        ImGui::BulletText("F2 : Cook-Torrance / Blinn-Phong");
        ImGui::BulletText("F3 : wireframe      F4 : IBL");
        ImGui::BulletText("F5 : custom pipeline (CommandBuffer)");
        ImGui::BulletText("F6 : next built-in post effect");
        ImGui::BulletText("CommandBuffer demo scene: C / E / M");
        ImGui::BulletText("ESC : quit");
        ImGui::End();
    }

} // namespace Demo
