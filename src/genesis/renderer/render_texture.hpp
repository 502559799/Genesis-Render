/*
 * Genesis Render - render textures and render target references.
 *
 * A RenderTexture is an offscreen colour (+ optional depth) target that can
 * be rendered into by the built-in pipeline or a CommandBuffer and then be
 * sampled by materials, blits or the UI.  Every render texture owns two
 * compatible render passes (clear / load) so passes can be split and resumed
 * freely (e.g. a CommandBuffer switching targets in the middle of the scene).
 *
 * Orientation: rendered content always has row 0 = NDC y -1 = the BOTTOM of
 * the view (the natural GL convention, also used by Vulkan and by Metal with
 * a flipped clip-space y).  Code that displays a render texture as an image
 * flips v (see RenderTexture::displayUV).
 */
#ifndef __GENESIS_RENDER_TEXTURE_HPP__
#define __GENESIS_RENDER_TEXTURE_HPP__

#include "rhi/rhi.hpp"

#include <functional>
#include <memory>
#include <string>

namespace Genesis
{
    namespace Render
    {
        // Everything the renderer needs to draw into a target.
        struct TargetRef
        {
            RHI::RHIRenderPass *pass_clear = nullptr;
            RHI::RHIRenderPass *pass_load = nullptr;
            RHI::RHIFramebuffer *framebuffer = nullptr;
            RHI::RHITexture *color = nullptr; // nullptr for the swapchain
            RHI::RHITexture *depth = nullptr;
            RHI::PixelFormat color_format = RHI::PixelFormat::RGBA8_UNORM;
            RHI::PixelFormat depth_format = RHI::PixelFormat::Undefined;
            uint32_t width = 0;
            uint32_t height = 0;
            // True when row 0 is displayed at the top (swapchains of Vulkan /
            // Metal, capture targets).  Screen space drawing (2D, UI) uses it
            // to map "pixel y down from the top" onto rows.
            bool row0_top = false;
            // True for HDR scene colour (tone mapped when presented).
            bool hdr = false;
            const void *identity = nullptr; // stable id (RenderTexture*, swapchain, ...)

            bool valid() const { return framebuffer && pass_clear && pass_load && width && height; }
            bool operator==(const TargetRef &o) const { return identity == o.identity && framebuffer == o.framebuffer; }
            bool operator!=(const TargetRef &o) const { return !(*this == o); }
        };

        class RenderTexture
        {
        public:
            // `defer` (optional) receives destruction work so GPU objects that
            // an in-flight frame may still reference are released safely.
            using DeferFn = std::function<void(std::function<void()>)>;
            RenderTexture(RHI::RHIDevice *device, const std::string &name, uint32_t width, uint32_t height,
                          RHI::PixelFormat color_format, bool with_depth, DeferFn defer = nullptr);
            ~RenderTexture();
            RenderTexture(const RenderTexture &) = delete;
            RenderTexture &operator=(const RenderTexture &) = delete;

            bool valid() const { return ref_.valid(); }
            const std::string &name() const { return name_; }
            uint32_t width() const { return ref_.width; }
            uint32_t height() const { return ref_.height; }
            bool hdr() const;
            bool hasDepth() const { return ref_.depth != nullptr; }
            RHI::PixelFormat colorFormat() const { return ref_.color_format; }
            RHI::RHITexture *color() const { return ref_.color; }
            RHI::RHITexture *depth() const { return ref_.depth; }
            RHI::RHISampler *sampler() const { return sampler_; }
            const TargetRef &target() const { return ref_; }

            // Marks this texture as a "display" target (row 0 at the top),
            // used for the capture target whose readback is top-down.
            void setRow0Top(bool v) { ref_.row0_top = v; }

            // Incremented whenever the GPU objects are re-created (resize), so
            // caches keyed on the texture can invalidate themselves.
            uint32_t generation() const { return generation_; }
            bool resize(uint32_t width, uint32_t height);

            // uv rectangle that shows the rendered image upright:
            // (u0, v0) top-left, (u1, v1) bottom-right of a screen-space quad.
            static void displayUV(float &u0, float &v0, float &u1, float &v1)
            {
                u0 = 0.0f; v0 = 1.0f; u1 = 1.0f; v1 = 0.0f;
            }

            // Destroys the GPU objects now and detaches from the device (used
            // when the renderer shuts down while scripts still hold handles).
            void releaseGpu();

            // Called before the GPU objects are destroyed / re-created.
            using ReleaseHook = void (*)(RenderTexture *self, void *user);
            void setReleaseHook(ReleaseHook hook, void *user) { hook_ = hook; hook_user_ = user; }

        private:
            bool create(uint32_t width, uint32_t height);
            void destroy(bool deferred);

            RHI::RHIDevice *device_ = nullptr;
            std::string name_;
            TargetRef ref_;
            RHI::RHISampler *sampler_ = nullptr;
            bool with_depth_ = false;
            uint32_t generation_ = 0;
            ReleaseHook hook_ = nullptr;
            void *hook_user_ = nullptr;
            DeferFn defer_;
        };

        using RenderTexturePtr = std::shared_ptr<RenderTexture>;

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_RENDER_TEXTURE_HPP__ */
