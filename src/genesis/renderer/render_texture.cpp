/*
 * Genesis Render - render textures.
 */
#include "renderer/render_texture.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <algorithm>

namespace Genesis
{
    namespace Render
    {
        namespace
        {
            RHI::RenderPassDesc makePass(const std::string &name, RHI::PixelFormat color, RHI::PixelFormat depth, bool clear)
            {
                RHI::RenderPassDesc rpd;
                rpd.name = name + (clear ? ".clear" : ".load");
                rpd.attachment_count = depth != RHI::PixelFormat::Undefined ? 2u : 1u;
                RHI::AttachmentDesc &c = rpd.attachments[0];
                c.format = color;
                c.load_op = clear ? RHI::LoadOp::Clear : RHI::LoadOp::Load;
                c.store_op = RHI::StoreOp::Store;
                c.initial_layout = clear ? RHI::TextureLayout::Undefined : RHI::TextureLayout::ShaderReadOnlyOptimal;
                c.final_layout = RHI::TextureLayout::ShaderReadOnlyOptimal;
                c.is_depth_stencil = false;
                if (depth != RHI::PixelFormat::Undefined)
                {
                    RHI::AttachmentDesc &d = rpd.attachments[1];
                    d.format = depth;
                    d.load_op = clear ? RHI::LoadOp::Clear : RHI::LoadOp::Load;
                    d.store_op = RHI::StoreOp::Store; // passes may be resumed
                    d.stencil_load_op = d.load_op;
                    d.stencil_store_op = RHI::StoreOp::Store;
                    d.initial_layout = clear ? RHI::TextureLayout::Undefined : RHI::TextureLayout::DepthStencilAttachmentOptimal;
                    d.final_layout = RHI::TextureLayout::DepthStencilAttachmentOptimal;
                    d.is_depth_stencil = true;
                }
                return rpd;
            }
        } // namespace

        RenderTexture::RenderTexture(RHI::RHIDevice *device, const std::string &name, uint32_t width, uint32_t height,
                                     RHI::PixelFormat color_format, bool with_depth, DeferFn defer)
            : device_(device), name_(name), with_depth_(with_depth), defer_(std::move(defer))
        {
            ref_.color_format = color_format;
            ref_.identity = this;
            RHI::SamplerDesc sd;
            sd.min_filter = sd.mag_filter = RHI::FilterMode::Linear;
            sd.mipmap_mode = RHI::MipmapMode::Nearest;
            sd.address_u = sd.address_v = sd.address_w = RHI::AddressMode::ClampToEdge;
            sd.max_lod = 0.0f;
            sd.name = name + ".sampler";
            sampler_ = device_->createSampler(sd);
            create(width, height);
        }

        void RenderTexture::releaseGpu()
        {
            if (!device_) return;
            destroy(false);
            if (sampler_) device_->destroySampler(sampler_);
            sampler_ = nullptr;
            defer_ = nullptr;
            device_ = nullptr;
        }

        RenderTexture::~RenderTexture()
        {
            if (!device_) return;
            destroy(true);
            if (sampler_)
            {
                RHI::RHIDevice *device = device_;
                RHI::RHISampler *sampler = sampler_;
                if (defer_) defer_([device, sampler]() { device->destroySampler(sampler); });
                else device_->destroySampler(sampler_);
            }
        }

        bool RenderTexture::hdr() const
        {
            return RHI::pixelFormatIsFloat(ref_.color_format);
        }

        bool RenderTexture::create(uint32_t width, uint32_t height)
        {
            width = std::max(1u, width);
            height = std::max(1u, height);
            ++generation_;

            RHI::TextureDesc cd;
            cd.type = RHI::TextureType::Tex2D;
            cd.format = ref_.color_format;
            cd.width = width;
            cd.height = height;
            cd.mip_levels = 1;
            cd.usage = (uint32_t)RHI::TextureUsageFlagBits::RenderTarget | (uint32_t)RHI::TextureUsageFlagBits::Sampled |
                       (uint32_t)RHI::TextureUsageFlagBits::TransferSrc | (uint32_t)RHI::TextureUsageFlagBits::TransferDst;
            cd.name = name_ + ".color";
            ref_.color = device_->createTexture(cd);
            if (!ref_.color)
            {
                GEN_LOG_ERROR("[RenderTexture] '%s': colour texture creation failed", name_.c_str());
                return false;
            }

            ref_.depth_format = RHI::PixelFormat::Undefined;
            if (with_depth_)
            {
                RHI::TextureDesc dd;
                dd.type = RHI::TextureType::Tex2D;
                dd.format = device_->info().depth_format;
                dd.width = width;
                dd.height = height;
                dd.mip_levels = 1;
                dd.usage = (uint32_t)RHI::TextureUsageFlagBits::DepthStencil;
                dd.name = name_ + ".depth";
                ref_.depth = device_->createTexture(dd);
                if (!ref_.depth)
                {
                    GEN_LOG_ERROR("[RenderTexture] '%s': depth texture creation failed", name_.c_str());
                    destroy(false);
                    return false;
                }
                ref_.depth_format = dd.format;
            }

            RHI::RenderPassDesc clear_desc = makePass(name_, ref_.color_format, ref_.depth_format, true);
            RHI::RenderPassDesc load_desc = makePass(name_, ref_.color_format, ref_.depth_format, false);
            ref_.pass_clear = device_->createRenderPass(clear_desc);
            ref_.pass_load = device_->createRenderPass(load_desc);

            RHI::FramebufferDesc fbd;
            fbd.render_pass = clear_desc;
            fbd.width = width;
            fbd.height = height;
            fbd.name = name_ + ".fb";
            fbd.attachments.push_back(RHI::FramebufferDesc::Attachment{ref_.color, 0, 0});
            if (ref_.depth) fbd.attachments.push_back(RHI::FramebufferDesc::Attachment{ref_.depth, 0, 0});
            ref_.framebuffer = device_->createFramebuffer(fbd);
            if (!ref_.framebuffer || !ref_.pass_clear || !ref_.pass_load)
            {
                GEN_LOG_ERROR("[RenderTexture] '%s': framebuffer creation failed (format %u)", name_.c_str(),
                              (unsigned)ref_.color_format);
                destroy(false);
                return false;
            }
            ref_.width = width;
            ref_.height = height;
            ref_.hdr = hdr();

            // Give the attachments defined contents (and the layout the load
            // pass expects) so the texture can be sampled or resumed safely
            // before anything has been rendered into it.
            struct ClearCtx
            {
                const TargetRef *ref;
            } ctx{&ref_};
            device_->submitImmediate(
                [](RHI::RHICommandBuffer *cmd, void *user)
                {
                    const TargetRef &r = *static_cast<ClearCtx *>(user)->ref;
                    RHI::RenderPassBeginInfo begin;
                    begin.clear_count = 2;
                    begin.clears[0].color[0] = begin.clears[0].color[1] = begin.clears[0].color[2] = 0.0f;
                    begin.clears[0].color[3] = 1.0f;
                    begin.clears[1].depth = 1.0f;
                    cmd->beginRenderPass(r.pass_clear, r.framebuffer, begin);
                    cmd->endRenderPass();
                },
                &ctx);
            return true;
        }

        void RenderTexture::destroy(bool deferred)
        {
            if (hook_) hook_(this, hook_user_);
            // Render passes are cached (shared) by the backends and are not
            // destroyed here.
            RHI::RHIDevice *device = device_;
            RHI::RHIFramebuffer *fb = ref_.framebuffer;
            RHI::RHITexture *depth = ref_.depth;
            RHI::RHITexture *color = ref_.color;
            auto work = [device, fb, depth, color]()
            {
                if (fb) device->destroyFramebuffer(fb);
                if (depth) device->destroyTexture(depth);
                if (color) device->destroyTexture(color);
            };
            if (deferred && defer_) defer_(work);
            else work();
            ref_.framebuffer = nullptr;
            ref_.depth = nullptr;
            ref_.color = nullptr;
            ref_.pass_clear = ref_.pass_load = nullptr;
            ref_.width = ref_.height = 0;
        }

        bool RenderTexture::resize(uint32_t width, uint32_t height)
        {
            if (!device_) return false;
            if (width == ref_.width && height == ref_.height && valid()) return true;
            device_->waitIdle();
            destroy(false);
            return create(width, height);
        }

    } // namespace Render
} // namespace Genesis
