/*
 * Genesis Render - demo application: ImGui panels.
 */
#include "demo_app.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "renderer/render_system.hpp"
#include "script/script_system.hpp"
#include "ui/inspector.hpp"
#include "ui/ui_system.hpp"

#include "imgui.h"

#include <algorithm>
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
        if (show_console_) drawConsolePanel();
        if (show_log_) drawLogPanel();
        if (show_help_) drawHelpPanel();
    }

    void DemoApp::drawMenuBar()
    {
        if (!ImGui::BeginMainMenuBar()) return;
        if (ImGui::BeginMenu("Scene"))
        {
            const char *labels[] = {"Showcase (C++)", "Built by Lua script (RTTI)", "CommandBuffer demo (Lua)", "Empty"};
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
            ImGui::MenuItem("Lua console", nullptr, &show_console_);
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
        ImGui::Separator();
        const char *scenes[] = {"Showcase (C++)", "Built by Lua (RTTI)", "CommandBuffer demo (Lua)", "Empty"};
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
    void DemoApp::runConsole(const std::string &code)
    {
        if (!engine.hasScript() || code.empty()) return;
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
        if (!ImGui::Begin("Lua console", &show_console_)) { ImGui::End(); return; }
        if (!engine.hasScript())
        {
            ImGui::TextDisabled("scripting disabled (--no-script)");
            ImGui::End();
            return;
        }
        ImGui::SetNextItemWidth(260);
        if (ImGui::BeginCombo("##examples", "examples..."))
        {
            for (const auto &ex : kConsoleExamples)
                if (ImGui::Selectable(ex[0])) std::snprintf(console_, sizeof(console_), "%s", ex[1]);
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
        for (size_t i = 0; i < history.size(); ++i)
            if (history[i].text.find("[Lua]") != std::string::npos) lua_lines.push_back(i);
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
            show_help_ = true;
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
            for (const auto &ex : kConsoleExamples)
            {
                runConsole(ex[1]);
                frames(1);
            }
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
        const int errors = (int)(Logger::instance().errorCount() - errors_before) + (int)engine.script().errorCount();
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
