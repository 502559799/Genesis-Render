/*
 * Genesis Render - engine level command buffers.
 *
 * A CommandBuffer is a recorded list of high level rendering commands.  It
 * splits rendering into two steps, which is what makes the programmable
 * pipeline possible:
 *
 *   organise (logic)  : gameplay code / Lua records commands at any time
 *                       (typically during world.update).  Every argument is
 *                       copied, so later changes to the logic state do not
 *                       affect what was recorded.
 *   submit   (render) : the RenderSystem executes the list on the GPU, either
 *                       - injected at a CameraEvent of the built-in forward
 *                         pipeline (persistent, every frame),
 *                       - once, on the next frame (executeCommandBuffer), or
 *                       - as the whole frame in PipelineMode::Custom, where
 *                         the fixed forward pipeline is skipped and the user
 *                         command buffers build the frame themselves.
 *
 * Commands are not tied to the ECS Renderer + Camera structure: they can draw
 * any mesh with any material and matrix, switch render targets, set any view,
 * blit with effects between render textures and draw 2D / 3D primitives and
 * SDF text.
 *
 * Coordinates: 2D commands use pixels of the current target with the origin
 * at its top-left corner (y down); 3D commands use world units.
 */
#ifndef __GENESIS_COMMAND_BUFFER_HPP__
#define __GENESIS_COMMAND_BUFFER_HPP__

#include "ecs/ecs.hpp"
#include "foundation_layer/math/math.hpp"
#include "renderer/material.hpp"
#include "renderer/render_texture.hpp"
#include "renderer/render_ubo.hpp"
#include "text/font.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    namespace Render
    {
        // Injection points of the built-in forward pipeline, in execution order.
        enum class CameraEvent : uint32_t
        {
            BeforeScene = 0,      // before the scene pass (target: camera HDR, not yet cleared)
            AfterOpaque = 1,      // inside the scene pass, after opaque geometry
            AfterTransparent = 2, // inside the scene pass, after transparent geometry
            BeforePost = 3,       // after the scene pass, before tone mapping (target: camera HDR)
            AfterPost = 4,        // after tone mapping (target: backbuffer)
            Overlay = 5,          // after the built-in 2D overlay, before the UI (target: backbuffer)
            Count
        };
        const char *cameraEventName(CameraEvent e);
        bool cameraEventFromName(const std::string &name, CameraEvent &out);

        enum class PipelineMode : uint32_t
        {
            Forward = 0, // built-in forward pipeline + injected command buffers
            Custom = 1,  // the frame is built only by the custom command buffers
        };

        // Destination / source of a pass.
        struct RenderTargetId
        {
            enum class Kind : uint32_t
            {
                Camera = 0,     // the camera HDR scene colour (+ depth)
                Backbuffer = 1, // the presented image (swapchain / capture target)
                Texture = 2,    // a user RenderTexture
            };
            Kind kind = Kind::Camera;
            RenderTexturePtr texture;

            static RenderTargetId camera() { return RenderTargetId(); }
            static RenderTargetId backbuffer() { RenderTargetId t; t.kind = Kind::Backbuffer; return t; }
            static RenderTargetId of(const RenderTexturePtr &rt)
            {
                RenderTargetId t;
                t.kind = rt ? Kind::Texture : Kind::Camera;
                t.texture = rt;
                return t;
            }
        };

        struct DrawCommand
        {
            enum class Type : uint32_t
            {
                SetRenderTarget,
                ClearRenderTarget,
                SetViewport,
                ResetViewport,
                SetViewProjection,
                SetCamera,
                SetCameraEntity,
                ResetCamera,
                DrawMesh,
                DrawEntity,
                DrawScene,
                DrawSkybox,
                Blit,
                DrawRect,
                DrawLine2D,
                DrawImage,
                DrawSprite,
                DrawText,
                DrawLine3D,
                DrawQuad3D,
                DrawText3D,
                SetDepthTest3D,
            };
            Type type = Type::DrawRect;

            RenderTargetId target;          // SetRenderTarget / Blit destination
            RenderTargetId source;          // Blit / DrawImage source
            RHI::RHITexture *raw_texture = nullptr;   // DrawImage from a plain texture
            RHI::RHISampler *raw_sampler = nullptr;
            bool flip_v = false;
            MaterialPtr material;           // DrawMesh
            ECS::Entity entity;             // DrawEntity / SetCameraEntity
            Math::Mat4 m0 = Math::Mat4::identity(); // model / view / text transform
            Math::Mat4 m1 = Math::Mat4::identity(); // projection
            Math::Vec4 v0 = Math::Vec4(0.0f);       // rect / points / position + size
            Math::Vec4 v1 = Math::Vec4(0.0f);       // second point / effect params / uv
            Math::Color c0 = Math::Color::white();  // main colour
            Math::Color c1 = Math::Color::black();  // secondary colour (outline, tint)
            float f0 = 0.0f, f1 = 0.0f, f2 = 0.0f;  // thickness / size / fov etc.
            uint32_t u0 = 0, u1 = 0;                // mesh index / effect / flags / alignment
            bool b0 = false, b1 = false;            // clear colour, clear depth / billboard
            std::string text;
        };

        class CommandBuffer
        {
        public:
            explicit CommandBuffer(const std::string &name = "command_buffer") : name_(name) {}

            const std::string &name() const { return name_; }
            void setName(const std::string &n) { name_ = n; }
            const std::vector<DrawCommand> &commands() const { return commands_; }
            size_t size() const { return commands_.size(); }
            bool empty() const { return commands_.empty(); }
            void clear() { commands_.clear(); }

            // ---- targets & state ---------------------------------------
            void setRenderTarget(const RenderTargetId &target);
            void setRenderTarget(const RenderTexturePtr &rt) { setRenderTarget(RenderTargetId::of(rt)); }
            void clearRenderTarget(bool clear_color, const Math::Color &color, bool clear_depth, float depth = 1.0f);
            // Pixels of the current target, origin top-left.
            void setViewport(float x, float y, float width, float height);
            void resetViewport();
            void setViewProjection(const Math::Mat4 &view, const Math::Mat4 &projection);
            void setCamera(const Math::Vec3 &eye, const Math::Vec3 &target, float fov_degrees = 60.0f,
                           float near_plane = 0.05f, float far_plane = 500.0f);
            void setCameraEntity(ECS::Entity camera);
            void resetCamera(); // back to the main camera
            void setDepthTest3D(bool enabled);

            // ---- geometry ---------------------------------------------
            // `mesh_index` is a RenderScene mesh; a null material uses the
            // mesh's own material.
            void drawMesh(uint32_t mesh_index, const Math::Mat4 &model, const MaterialPtr &material = nullptr);
            void drawEntity(ECS::Entity entity);
            void drawScene();   // every visible mesh entity, culled for the current view
            void drawSkybox();

            // Fullscreen pass `source` -> `destination` with a post effect.
            // params: x strength, y radius / size (px), zw effect specific.
            void blit(const RenderTargetId &source, const RenderTargetId &destination, PostEffect effect = PostEffect::None,
                      const Math::Vec4 &params = Math::Vec4(1.0f, 1.0f, 0.0f, 0.0f),
                      const Math::Color &tint = Math::Color::white(), bool tonemap = false);

            // ---- 2D (current target pixels) -----------------------------
            void drawRect(float x, float y, float width, float height, const Math::Color &color);
            void drawRectOutline(float x, float y, float width, float height, const Math::Color &color, float thickness = 1.0f);
            void drawLine2D(const Math::Vec2 &a, const Math::Vec2 &b, const Math::Color &color, float thickness = 1.0f);
            void drawImage(const RenderTexturePtr &image, float x, float y, float width, float height,
                           const Math::Color &tint = Math::Color::white());
            // Draws an arbitrary sampled texture (the caller keeps it alive),
            // e.g. the procedural gaussian falloff used by the splat renderer.
            void drawImageTexture(RHI::RHITexture *texture, RHI::RHISampler *sampler, float x, float y, float width,
                                  float height, const Math::Color &tint = Math::Color::white(), bool flip_v = false);
            // Textured quad with an explicit UV rect (sprite sheets).
            void drawSprite(RHI::RHITexture *texture, RHI::RHISampler *sampler, float x, float y, float width,
                            float height, const Math::Vec2 &uv0, const Math::Vec2 &uv1,
                            const Math::Color &tint = Math::Color::white());
            void drawText(const std::string &text, float x, float y, float size, const Math::Color &color,
                          Text::TextAlign align = Text::TextAlign::Left, float outline = 0.0f,
                          const Math::Color &outline_color = Math::Color::black());

            // ---- 3D (world units) ---------------------------------------
            void drawLine3D(const Math::Vec3 &a, const Math::Vec3 &b, const Math::Color &color, float thickness = 0.02f);
            void drawBox3D(const Math::AABB &box, const Math::Color &color, float thickness = 0.02f);
            // Camera facing (billboard) or XY-plane quad centred on `center`.
            void drawQuad3D(const Math::Vec3 &center, const Math::Vec2 &size, const Math::Color &color, bool billboard = true);
            void drawQuad3D(const Math::Mat4 &transform, const Math::Vec2 &size, const Math::Color &color);
            void drawText3D(const std::string &text, const Math::Vec3 &position, float size, const Math::Color &color,
                            bool billboard = true, Text::TextAlign align = Text::TextAlign::Center, float outline = 0.0f,
                            const Math::Color &outline_color = Math::Color::black());
            void drawText3D(const std::string &text, const Math::Mat4 &transform, float size, const Math::Color &color,
                            Text::TextAlign align = Text::TextAlign::Center, float outline = 0.0f,
                            const Math::Color &outline_color = Math::Color::black());

        private:
            DrawCommand &push(DrawCommand::Type type)
            {
                commands_.emplace_back();
                commands_.back().type = type;
                return commands_.back();
            }
            std::string name_;
            std::vector<DrawCommand> commands_;
        };

        using CommandBufferPtr = std::shared_ptr<CommandBuffer>;

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_COMMAND_BUFFER_HPP__ */
