/*
 * Genesis Render - engine level command buffers (recording side).
 */
#include "renderer/command_buffer.hpp"

#include <cstring>

namespace Genesis
{
    namespace Render
    {
        const char *cameraEventName(CameraEvent e)
        {
            switch (e)
            {
            case CameraEvent::BeforeScene: return "before_scene";
            case CameraEvent::AfterOpaque: return "after_opaque";
            case CameraEvent::AfterTransparent: return "after_transparent";
            case CameraEvent::BeforePost: return "before_post";
            case CameraEvent::AfterPost: return "after_post";
            case CameraEvent::Overlay: return "overlay";
            default: return "unknown";
            }
        }

        bool cameraEventFromName(const std::string &name, CameraEvent &out)
        {
            for (uint32_t i = 0; i < (uint32_t)CameraEvent::Count; ++i)
            {
                if (name == cameraEventName((CameraEvent)i))
                {
                    out = (CameraEvent)i;
                    return true;
                }
            }
            return false;
        }

        const char *postEffectName(PostEffect effect)
        {
            switch (effect)
            {
            case PostEffect::None: return "none";
            case PostEffect::Invert: return "invert";
            case PostEffect::Grayscale: return "grayscale";
            case PostEffect::Sepia: return "sepia";
            case PostEffect::Blur: return "blur";
            case PostEffect::Edge: return "edge";
            case PostEffect::Pixelate: return "pixelate";
            case PostEffect::Vignette: return "vignette";
            case PostEffect::Scanline: return "scanline";
            case PostEffect::Chromatic: return "chromatic";
            case PostEffect::Tint: return "tint";
            case PostEffect::Posterize: return "posterize";
            default: return "none";
            }
        }

        bool postEffectFromName(const std::string &name, PostEffect &out)
        {
            for (uint32_t i = 0; i < (uint32_t)PostEffect::Count; ++i)
            {
                if (name == postEffectName((PostEffect)i))
                {
                    out = (PostEffect)i;
                    return true;
                }
            }
            return false;
        }

        void CommandBuffer::setRenderTarget(const RenderTargetId &target)
        {
            push(DrawCommand::Type::SetRenderTarget).target = target;
        }

        void CommandBuffer::clearRenderTarget(bool clear_color, const Math::Color &color, bool clear_depth, float depth)
        {
            DrawCommand &c = push(DrawCommand::Type::ClearRenderTarget);
            c.b0 = clear_color;
            c.b1 = clear_depth;
            c.c0 = color;
            c.f0 = depth;
        }

        void CommandBuffer::setViewport(float x, float y, float width, float height)
        {
            push(DrawCommand::Type::SetViewport).v0 = Math::Vec4(x, y, width, height);
        }

        void CommandBuffer::resetViewport() { push(DrawCommand::Type::ResetViewport); }

        void CommandBuffer::setViewProjection(const Math::Mat4 &view, const Math::Mat4 &projection)
        {
            DrawCommand &c = push(DrawCommand::Type::SetViewProjection);
            c.m0 = view;
            c.m1 = projection;
        }

        void CommandBuffer::setCamera(const Math::Vec3 &eye, const Math::Vec3 &target, float fov_degrees, float near_plane,
                                      float far_plane)
        {
            DrawCommand &c = push(DrawCommand::Type::SetCamera);
            c.v0 = Math::Vec4(eye.x, eye.y, eye.z, 0.0f);
            c.v1 = Math::Vec4(target.x, target.y, target.z, 0.0f);
            c.f0 = fov_degrees;
            c.f1 = near_plane;
            c.f2 = far_plane;
        }

        void CommandBuffer::setCameraEntity(ECS::Entity camera) { push(DrawCommand::Type::SetCameraEntity).entity = camera; }
        void CommandBuffer::resetCamera() { push(DrawCommand::Type::ResetCamera); }
        void CommandBuffer::setDepthTest3D(bool enabled) { push(DrawCommand::Type::SetDepthTest3D).b0 = enabled; }

        void CommandBuffer::drawMesh(uint32_t mesh_index, const Math::Mat4 &model, const MaterialPtr &material)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawMesh);
            c.u0 = mesh_index;
            c.m0 = model;
            c.material = material;
        }

        void CommandBuffer::drawEntity(ECS::Entity entity) { push(DrawCommand::Type::DrawEntity).entity = entity; }
        void CommandBuffer::drawScene() { push(DrawCommand::Type::DrawScene); }
        void CommandBuffer::drawSkybox() { push(DrawCommand::Type::DrawSkybox); }

        void CommandBuffer::blit(const RenderTargetId &source, const RenderTargetId &destination, PostEffect effect,
                                 const Math::Vec4 &params, const Math::Color &tint, bool tonemap)
        {
            DrawCommand &c = push(DrawCommand::Type::Blit);
            c.source = source;
            c.target = destination;
            c.u0 = (uint32_t)effect;
            c.v1 = params;
            c.c1 = tint;
            c.b0 = tonemap;
        }

        void CommandBuffer::drawRect(float x, float y, float width, float height, const Math::Color &color)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawRect);
            c.v0 = Math::Vec4(x, y, width, height);
            c.c0 = color;
        }

        void CommandBuffer::drawRectOutline(float x, float y, float width, float height, const Math::Color &color, float thickness)
        {
            drawLine2D(Math::Vec2(x, y), Math::Vec2(x + width, y), color, thickness);
            drawLine2D(Math::Vec2(x + width, y), Math::Vec2(x + width, y + height), color, thickness);
            drawLine2D(Math::Vec2(x + width, y + height), Math::Vec2(x, y + height), color, thickness);
            drawLine2D(Math::Vec2(x, y + height), Math::Vec2(x, y), color, thickness);
        }

        void CommandBuffer::drawLine2D(const Math::Vec2 &a, const Math::Vec2 &b, const Math::Color &color, float thickness)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawLine2D);
            c.v0 = Math::Vec4(a.x, a.y, b.x, b.y);
            c.c0 = color;
            c.f0 = thickness;
        }

        void CommandBuffer::drawImage(const RenderTexturePtr &image, float x, float y, float width, float height,
                                      const Math::Color &tint)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawImage);
            c.source = RenderTargetId::of(image);
            c.v0 = Math::Vec4(x, y, width, height);
            c.c0 = tint;
        }

        void CommandBuffer::drawText(const std::string &text, float x, float y, float size, const Math::Color &color,
                                     Text::TextAlign align, float outline, const Math::Color &outline_color)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawText);
            c.text = text;
            c.v0 = Math::Vec4(x, y, 0.0f, 0.0f);
            c.f0 = size;
            c.f1 = outline;
            c.c0 = color;
            c.c1 = outline_color;
            c.u0 = (uint32_t)align;
        }

        void CommandBuffer::drawLine3D(const Math::Vec3 &a, const Math::Vec3 &b, const Math::Color &color, float thickness)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawLine3D);
            c.v0 = Math::Vec4(a.x, a.y, a.z, 0.0f);
            c.v1 = Math::Vec4(b.x, b.y, b.z, 0.0f);
            c.c0 = color;
            c.f0 = thickness;
        }

        void CommandBuffer::drawBox3D(const Math::AABB &box, const Math::Color &color, float thickness)
        {
            if (!box.valid) return;
            const Math::Vec3 mn = box.minPoint, mx = box.maxPoint;
            const Math::Vec3 p[8] = {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}, {mn.x, mx.y, mn.z},
                                     {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}};
            static const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (const auto &edge : e) drawLine3D(p[edge[0]], p[edge[1]], color, thickness);
        }

        void CommandBuffer::drawQuad3D(const Math::Vec3 &center, const Math::Vec2 &size, const Math::Color &color, bool billboard)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawQuad3D);
            c.m0 = Math::Mat4::translate(center);
            c.v0 = Math::Vec4(size.x, size.y, 0.0f, 0.0f);
            c.c0 = color;
            c.b1 = billboard;
        }

        void CommandBuffer::drawQuad3D(const Math::Mat4 &transform, const Math::Vec2 &size, const Math::Color &color)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawQuad3D);
            c.m0 = transform;
            c.v0 = Math::Vec4(size.x, size.y, 0.0f, 0.0f);
            c.c0 = color;
            c.b1 = false;
        }

        void CommandBuffer::drawText3D(const std::string &text, const Math::Vec3 &position, float size, const Math::Color &color,
                                       bool billboard, Text::TextAlign align, float outline, const Math::Color &outline_color)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawText3D);
            c.text = text;
            c.m0 = Math::Mat4::translate(position);
            c.f0 = size;
            c.f1 = outline;
            c.c0 = color;
            c.c1 = outline_color;
            c.u0 = (uint32_t)align;
            c.b1 = billboard;
        }

        void CommandBuffer::drawText3D(const std::string &text, const Math::Mat4 &transform, float size, const Math::Color &color,
                                       Text::TextAlign align, float outline, const Math::Color &outline_color)
        {
            DrawCommand &c = push(DrawCommand::Type::DrawText3D);
            c.text = text;
            c.m0 = transform;
            c.f0 = size;
            c.f1 = outline;
            c.c0 = color;
            c.c1 = outline_color;
            c.u0 = (uint32_t)align;
            c.b1 = false;
        }

    } // namespace Render
} // namespace Genesis
