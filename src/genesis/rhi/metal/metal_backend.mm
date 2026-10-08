/*
 * Genesis Render - Metal backend implementation.
 *
 * Implements the RHI on Apple Metal.  This file is compiled as Objective-C++
 * (and only on macOS); it is the translation unit that actually calls the
 * Metal / QuartzCore frameworks.  The public interface in metal_backend.hpp
 * is Objective-C free (Metal objects carried as void*), so the renderer and
 * demo never include Metal headers.
 */
#include "rhi/metal/metal_backend.hpp"
#include "rhi/metal/metal_shaders.hpp"
#include "foundation_layer/genesis_log.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#import <AppKit/AppKit.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>

#include <algorithm>
#include <cstring>
#include <set>
#include <vector>

namespace Genesis
{
    namespace RHI
    {
        //==================================================================
        // Format mapping
        //==================================================================
        MTLPixelFormatMap mtlPixelFormatMap(PixelFormat format)
        {
            MTLPixelFormat pf = MTLPixelFormatBGRA8Unorm;
            bool is_depth = false;
            switch (format)
            {
            case PixelFormat::R8_UNORM: pf = MTLPixelFormatR8Unorm; break;
            case PixelFormat::RG8_UNORM: pf = MTLPixelFormatRG8Unorm; break;
            case PixelFormat::RGB8_UNORM: pf = MTLPixelFormatRGBA8Unorm; break;
            case PixelFormat::RGBA8_UNORM: pf = MTLPixelFormatRGBA8Unorm; break;
            case PixelFormat::RGBA8_SRGB: pf = MTLPixelFormatBGRA8Unorm_sRGB; break;
            case PixelFormat::BGRA8_UNORM: pf = MTLPixelFormatBGRA8Unorm; break;
            case PixelFormat::R16_SFLOAT: pf = MTLPixelFormatR16Float; break;
            case PixelFormat::RG16_SFLOAT: pf = MTLPixelFormatRG16Float; break;
            case PixelFormat::RGBA16_SFLOAT: pf = MTLPixelFormatRGBA16Float; break;
            case PixelFormat::R32_SFLOAT: pf = MTLPixelFormatR32Float; break;
            case PixelFormat::D32_SFLOAT: pf = MTLPixelFormatDepth32Float; is_depth = true; break;
            case PixelFormat::D24_UNORM_S8_UINT: pf = MTLPixelFormatDepth24Unorm_Stencil8; is_depth = true; break;
            case PixelFormat::D32_SFLOAT_S8_UINT: pf = MTLPixelFormatDepth32Float_Stencil8; is_depth = true; break;
            default: break;
            }
            MTLPixelFormatMap m{(unsigned int)pf, is_depth};
            return m;
        }

        static MTLPrimitiveType mtlTopology(PrimitiveTopology t)
        {
            switch (t)
            {
            case PrimitiveTopology::TriangleList: return MTLPrimitiveTypeTriangle;
            case PrimitiveTopology::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
            case PrimitiveTopology::LineList: return MTLPrimitiveTypeLine;
            default: return MTLPrimitiveTypeTriangle;
            }
        }

        //==================================================================
        // Buffer
        //==================================================================
        void *MetalBufferRHI::map(uint64_t offset, uint64_t size)
        {
            (void)size;
            id<MTLBuffer> b = (__bridge id<MTLBuffer>)mtl_buffer;
            return b.contents ? (reinterpret_cast<char *>(b.contents) + offset) : nullptr;
        }
        void MetalBufferRHI::unmap() {}

        //==================================================================
        // Texture
        //==================================================================
        // (no custom methods beyond the in-class ones)

        //==================================================================
        // Resource layout
        //==================================================================
        const ResourceBinding *MetalResourceLayoutRHI::findBinding(uint32_t set, uint32_t binding) const
        {
            for (const auto &b : desc_.bindings)
                if (b.set == set && b.binding == binding) return &b;
            return nullptr;
        }

        //==================================================================
        // Descriptor set
        //==================================================================
        void MetalDescriptorSetRHI::write(const WriteDescriptorSet *writes, uint32_t count)
        {
            for (uint32_t i = 0; i < count; ++i)
            {
                const WriteDescriptorSet &w = writes[i];
                if (w.type == ResourceType::UniformBuffer || w.type == ResourceType::StorageBuffer ||
                    w.type == ResourceType::UniformBufferDynamic || w.type == ResourceType::StorageBufferDynamic)
                {
                    if (w.buffer_info)
                    {
                        BufferEntry e;
                        e.buffer = static_cast<MetalBufferRHI *>(w.buffer_info->buffer);
                        e.offset = w.buffer_info->offset;
                        e.range = w.buffer_info->range ? w.buffer_info->range :
                                  (e.buffer ? e.buffer->size() : 0);
                        e.is_dynamic = (w.type == ResourceType::UniformBufferDynamic ||
                                         w.type == ResourceType::StorageBufferDynamic);
                        if (w.binding >= buffers.size()) buffers.resize(w.binding + 1);
                        buffers[w.binding] = e;
                    }
                }
                else
                {
                    if (w.image_info)
                    {
                        ImageEntry e;
                        e.texture = static_cast<MetalTextureRHI *>(w.image_info->texture);
                        e.sampler = static_cast<MetalSamplerRHI *>(w.image_info->sampler);
                        if (w.binding >= images.size()) images.resize(w.binding + 1);
                        images[w.binding] = e;
                    }
                }
            }
        }

        //==================================================================
        // Command buffer
        //==================================================================
        void MetalCommandBufferRHI::begin(CommandBufferUsage usage)
        {
            recording_ = true;
            one_time_ = usage == CommandBufferUsage::OneTimeSubmit;
            // A new command buffer is created by the device when the frame
            // starts; nothing to do here except mark recording.
        }

        void MetalCommandBufferRHI::end()
        {
            if (mtl_encoder)
            {
                [(__bridge id<MTLRenderCommandEncoder>)mtl_encoder endEncoding];
                mtl_encoder = nullptr;
            }
            recording_ = false;
        }

        void MetalCommandBufferRHI::beginRenderPass(RHIRenderPass *pass, RHIFramebuffer *fb,
                                                    const RenderPassBeginInfo &info)
        {
            current_framebuffer = static_cast<MetalFramebufferRHI *>(fb);
            id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)mtl_command_buffer;
            MTLRenderPassDescriptor *d = current_framebuffer
                ? (__bridge MTLRenderPassDescriptor *)current_framebuffer->render_pass_descriptor
                : nullptr;
            if (!d) return;

            // Apply clear values from the RHI descriptor into the MTL pass.
            MTLRenderPassColorAttachmentDescriptor *ca = d.colorAttachments[0];
            if (ca && ca.texture)
            {
                if (info.clear_count > 0)
                {
                    ca.loadAction = MTLLoadActionClear;
                    ca.clearColor = MTLClearColorMake(info.clears[0].color[0], info.clears[0].color[1],
                                                      info.clears[0].color[2], info.clears[0].color[3]);
                }
                else
                    ca.loadAction = MTLLoadActionDontCare;
                ca.storeAction = MTLStoreActionStore;
            }
            if (d.depthAttachment && d.depthAttachment.texture)
            {
                if (info.clear_count > 1)
                {
                    d.depthAttachment.loadAction = MTLLoadActionClear;
                    d.depthAttachment.clearDepth = info.clears[1].depth;
                }
                else
                    d.depthAttachment.loadAction = MTLLoadActionDontCare;
                d.depthAttachment.storeAction = MTLStoreActionDontCare;
            }
            id<MTLRenderCommandEncoder> encoder = [cb renderCommandEncoderWithDescriptor:d];
            mtl_encoder = (__bridge void *)encoder;
        }

        void MetalCommandBufferRHI::endRenderPass()
        {
            if (mtl_encoder)
            {
                [(__bridge id<MTLRenderCommandEncoder>)mtl_encoder endEncoding];
                mtl_encoder = nullptr;
            }
            current_framebuffer = nullptr;
        }

        void MetalCommandBufferRHI::bindPipeline(RHIPipeline *pipeline)
        {
            current_pipeline = static_cast<MetalPipelineRHI *>(pipeline);
            if (mtl_encoder && current_pipeline && current_pipeline->pipeline_state)
            {
                id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtl_encoder;
                [enc setRenderPipelineState:(__bridge id<MTLRenderPipelineState>)current_pipeline->pipeline_state];
            }
        }

        void MetalCommandBufferRHI::bindVertexBuffer(RHIBuffer *buffer, uint64_t offset)
        {
            current_vertex_buffer = static_cast<MetalBufferRHI *>(buffer);
            if (mtl_encoder && current_pipeline)
            {
                id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtl_encoder;
                id<MTLBuffer> b = (__bridge id<MTLBuffer>)current_vertex_buffer->mtl_buffer;
                [enc setVertexBuffer:b offset:offset atIndex:0];
            }
        }

        void MetalCommandBufferRHI::bindIndexBuffer(RHIBuffer *buffer, IndexType type, uint64_t offset)
        {
            current_index_buffer = static_cast<MetalBufferRHI *>(buffer);
            current_index_type = type == IndexType::UInt32 ? 1 : 0;
        }

        void MetalCommandBufferRHI::bindDescriptorSets(RHIPipeline *pipeline, RHIDescriptorSet *const *sets,
                                                       uint32_t set_count, const uint32_t *dynamic_offsets, uint32_t offset_count)
        {
            (void)pipeline;
            if (!mtl_encoder) return;
            id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtl_encoder;

            // Push constants are a tiny uniform buffer the renderer uploads
            // via the setPushConstants path; here we just bind the descriptor
            // buffers/textures.
            uint32_t dyn_index = 0; // consumed in order, one per dynamic binding
            for (uint32_t s = 0; s < set_count; ++s)
            {
                MetalDescriptorSetRHI *set = static_cast<MetalDescriptorSetRHI *>(sets[s]);
                if (!set) continue;
                // RHI set/binding map to Metal resource indices with an 8-slot
                // stride per set (same scheme as the GL texture-unit / uniform
                // block formula): index = set * 8 + binding.  The embedded MSL
                // shaders declare their [[buffer(n)]] / [[texture(n)]] with the
                // same indices, so bindings land on the right slots.
                for (size_t b = 0; b < set->buffers.size(); ++b)
                {
                    const MetalDescriptorSetRHI::BufferEntry &e = set->buffers[b];
                    if (!e.buffer || !e.buffer->mtl_buffer) continue;
                    id<MTLBuffer> buf = (__bridge id<MTLBuffer>)e.buffer->mtl_buffer;
                    uint64_t off = e.offset;
                    // Dynamic UBO (per-draw PerDraw matrices): apply the next
                    // dynamic offset exactly like the Vulkan/GL backends do.
                    if (e.is_dynamic && dynamic_offsets && dyn_index < offset_count)
                        off += dynamic_offsets[dyn_index++];
                    uint32_t index = s * 8 + (uint32_t)b;
                    // Vertex attributes are bound at buffer index 0 (see
                    // bindVertexBuffer / the vertex descriptor), so every UBO
                    // occupies index +1.  The MSL shaders declare their UBOs at
                    // the same +1 offset (e.g. [[buffer(1)]], [[buffer(9)]],
                    // [[buffer(17)]]).
                    [enc setVertexBuffer:buf offset:off atIndex:index + 1];
                    [enc setFragmentBuffer:buf offset:off atIndex:index + 1];
                }
                for (size_t b = 0; b < set->images.size(); ++b)
                {
                    const MetalDescriptorSetRHI::ImageEntry &e = set->images[b];
                    if (!e.texture || !e.texture->mtl_texture) continue;
                    id<MTLTexture> tex = (__bridge id<MTLTexture>)e.texture->mtl_texture;
                    id<MTLSamplerState> samp = e.sampler ? (__bridge id<MTLSamplerState>)e.sampler->mtl_sampler : nil;
                    uint32_t index = s * 8 + (uint32_t)b;
                    [enc setVertexTexture:tex atIndex:index];
                    [enc setFragmentTexture:tex atIndex:index];
                    if (samp) [enc setFragmentSamplerState:samp atIndex:index];
                }
            }
        }

        void MetalCommandBufferRHI::setPushConstants(const void *data, uint32_t size)
        {
            // Push constants are a small UBO; vertex attributes occupy Metal
            // buffer index 0 (bound through the vertex descriptor /
            // bindVertexBuffer), so the push-constant UBO uses index 1,
            // matching the line shader's [[buffer(1)]] declaration.
            if (!mtl_encoder || !data) return;
            id<MTLDevice> dev = [(__bridge id<MTLCommandBuffer>)mtl_command_buffer device];
            id<MTLBuffer> tmp = [dev newBufferWithLength:size options:MTLResourceStorageModeShared];
            memcpy(tmp.contents, data, size);
            id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtl_encoder;
            [enc setVertexBuffer:tmp offset:0 atIndex:1];
            [enc setFragmentBuffer:tmp offset:0 atIndex:1];
        }

        void MetalCommandBufferRHI::setViewport(const Viewport &vp)
        {
            if (!mtl_encoder) return;
            id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtl_encoder;
            // Metal viewport: origin bottom-left, height (depth 0..1).
            MTLViewport mtl_vp{(double)vp.x, (double)vp.y, (double)vp.width, (double)vp.height,
                                (double)vp.min_depth, (double)vp.max_depth};
            [enc setViewport:mtl_vp];
        }

        void MetalCommandBufferRHI::setScissor(const Rect2D &rect)
        {
            if (!mtl_encoder) return;
            id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtl_encoder;
            MTLScissorRect sr{(unsigned long)rect.x, (unsigned long)rect.y,
                              (unsigned long)rect.width, (unsigned long)rect.height};
            [enc setScissorRect:sr];
        }

        void MetalCommandBufferRHI::draw(uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count, uint32_t first_instance)
        {
            (void)first_instance;
            if (!mtl_encoder || !current_pipeline) return;
            id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtl_encoder;
            MTLPrimitiveType pt = mtlTopology(current_pipeline->desc_.topology);
            if (instance_count > 1)
                [enc drawPrimitives:pt vertexStart:first_vertex vertexCount:vertex_count instanceCount:instance_count];
            else
                [enc drawPrimitives:pt vertexStart:first_vertex vertexCount:vertex_count];
        }

        void MetalCommandBufferRHI::drawIndexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset,
                                                uint32_t instance_count, uint32_t first_instance)
        {
            (void)vertex_offset; (void)first_instance;
            if (!mtl_encoder || !current_pipeline || !current_index_buffer) return;
            id<MTLRenderCommandEncoder> enc = (__bridge id<MTLRenderCommandEncoder>)mtl_encoder;
            MTLPrimitiveType pt = mtlTopology(current_pipeline->desc_.topology);
            id<MTLBuffer> ib = (__bridge id<MTLBuffer>)current_index_buffer->mtl_buffer;
            MTLIndexType itype = current_index_type == 1 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
            [enc drawIndexedPrimitives:pt indexCount:index_count indexType:itype
                           indexBuffer:ib indexBufferOffset:first_index * (current_index_type == 1 ? 4 : 2)
                          instanceCount:instance_count];
        }

        void MetalCommandBufferRHI::dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z)
        {
            (void)group_x; (void)group_y; (void)group_z;
            // Compute shaders are not used by the engine on any backend.
        }

        void MetalCommandBufferRHI::copyBufferToBuffer(RHIBuffer *, uint64_t, RHIBuffer *, uint64_t, uint64_t) {}
        void MetalCommandBufferRHI::copyBufferToImage(RHIBuffer *, RHITexture *, const BufferImageCopy &) {}
        void MetalCommandBufferRHI::copyImageToBuffer(RHITexture *, RHIBuffer *, const BufferImageCopy &) {}
        void MetalCommandBufferRHI::pipelineBarrier(const TextureBarrier *, uint32_t) {}

        void MetalCommandBufferRHI::generateMipmaps(RHITexture *texture)
        {
            if (!mtl_command_buffer) return;
            MetalTextureRHI *t = static_cast<MetalTextureRHI *>(texture);
            if (!t || !t->mtl_texture) return;
            // End any active render encoder first; Metal supports one encoder
            // type at a time on a command buffer.
            if (mtl_encoder)
            {
                [(__bridge id<MTLRenderCommandEncoder>)mtl_encoder endEncoding];
                mtl_encoder = nullptr;
            }
            id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)mtl_command_buffer;
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit generateMipmapsForTexture:(__bridge id<MTLTexture>)t->mtl_texture];
            [blit endEncoding];
        }

        void MetalCommandBufferRHI::blitImage(RHITexture *src, uint32_t src_mip, RHITexture *dst, uint32_t dst_mip,
                                               uint32_t src_layer, uint32_t dst_layer, uint32_t layer_count, bool linear_filter)
        {
            // The IBL bake passes (the main users of blitImage) are supported
            // by the same fullscreen-quad pipeline used for other backends;
            // implement a 2D copy here as a fallback.
            (void)src; (void)src_mip; (void)dst; (void)dst_mip;
            (void)src_layer; (void)dst_layer; (void)layer_count; (void)linear_filter;
        }

        bool MetalCommandBufferRHI::readImagePixels(RHITexture *src, uint32_t, uint32_t layer,
                                                     void *dst, uint32_t dst_size_bytes)
        {
            id<MTLTexture> tex = (__bridge id<MTLTexture>)static_cast<MetalTextureRHI *>(src)->mtl_texture;
            if (!tex) return false;
            MTLRegion region = MTLRegionMake2D(0, 0, (NSUInteger)tex.width, (NSUInteger)tex.height);
            NSUInteger bytes_per_row = (NSUInteger)tex.width * 4; // captured targets are RGBA8
            [tex getBytes:dst bytesPerRow:bytes_per_row bytesPerImage:bytes_per_row
                   fromRegion:region mipmapLevel:0 slice:layer];
            (void)dst_size_bytes;
            return true;
        }

        //==================================================================
        // Device
        //==================================================================
        MetalDeviceRHI::MetalDeviceRHI() {}
        MetalDeviceRHI::~MetalDeviceRHI() { shutdown(); }

        bool MetalDeviceRHI::initialize(const DeviceDesc &desc)
        {
            desc_ = desc;
            id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
            if (!dev)
            {
                GEN_LOG_ERROR("[Metal] no default MTL device available");
                return false;
            }
            device_ = dev;
            command_queue_ = [dev newCommandQueue];

            info_.backend = BackendType::Metal;
            info_.device_name = dev.name ? dev.name.UTF8String : "Apple MTL";
            info_.api_version = "Metal";
            info_.max_uniform_buffer_bindings = 32;
            info_.max_texture_image_units = 32;
            info_.supports_compute = false;
            info_.supports_geometry_shader = false;
            info_.supports_tessellation = false;
            info_.supports_multisample = true;
            info_.supports_srgb_framebuffer = true;
            info_.min_uniform_buffer_offset_alignment = 256;
            info_.max_uniform_buffer_range = 65536;

            // Build the embedded MSL library from the prelude (structs +
            // helpers) plus every stage function concatenated together, so a
            // single newLibraryWithSource compiles all entries.  Dedup by the
            // source pointer: the same MSL source may be registered under more
            // than one logical name (e.g. post/tonemap.frag and
            // post/effects.frag share the fragment), and redefining the same
            // function in one library is a compile error.
            std::string combined = kMetalPrelude;
            std::set<const char *> included_sources;
            for (uint32_t i = 0; i < kMetalShaderCount; ++i)
            {
                if (included_sources.count(kMetalShaders[i].msl)) continue;
                included_sources.insert(kMetalShaders[i].msl);
                combined += "\n\n";
                combined += kMetalShaders[i].msl;
            }
            NSString *source = [NSString stringWithUTF8String:combined.c_str()];
            NSError *err = nil;
            id<MTLLibrary> lib = [dev newLibraryWithSource:source options:nil error:&err];
            if (!lib)
            {
                GEN_LOG_ERROR("[Metal] failed to create library: %s",
                              err ? err.localizedDescription.UTF8String : "unknown");
                return false;
            }
            library_ = (__bridge void *)lib;

            if (desc_.native_window)
            {
                if (!createSwapchain(desc_.native_window, desc_.width, desc_.height))
                {
                    GEN_LOG_ERROR("[Metal] swapchain creation failed");
                    return false;
                }
            }

            initialized_ = true;
            GEN_LOG_INFO("[Metal] device: %s", info_.device_name.c_str());
            return true;
        }

        void *MetalDeviceRHI::compileShader(const char *msl_source, const char *entry, ShaderStageFlagBits)
        {
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
            id<MTLLibrary> lib = (__bridge id<MTLLibrary>)library_;
            // The prelude defines all shared structs; the stage source is
            // compiled from the name table (see createShader).
            (void)msl_source;
            NSString *fn = [NSString stringWithUTF8String:entry];
            id<MTLFunction> f = [lib newFunctionWithName:fn];
            return (__bridge void *)f;
        }

        bool MetalDeviceRHI::createSwapchain(void *native_window, uint32_t width, uint32_t height)
        {
            window_ = native_window;
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;

            SDL_Window *sdl_window = (SDL_Window *)native_window;
            SDL_SysWMinfo wmInfo;
            SDL_VERSION(&wmInfo.version);
            if (!SDL_GetWindowWMInfo(sdl_window, &wmInfo) || wmInfo.subsystem != SDL_SYSWM_COCOA)
            {
                GEN_LOG_ERROR("[Metal] window does not provide a Cocoa handle");
                return false;
            }
            NSWindow *ns_window = wmInfo.info.cocoa.window;
            NSView *ns_view = [ns_window contentView];

            CAMetalLayer *layer = [CAMetalLayer layer];
            layer.device = dev;
            layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            layer.framebufferOnly = YES;
            layer.drawableSize = CGSizeMake(width, height);
            layer.contentsScale = ns_window.backingScaleFactor ?: 1.0;
            [ns_view setLayer:layer];
            [ns_view setWantsLayer:YES];
            sw_layer_ = (__bridge void *)layer;

            swapchain_width_ = width;
            swapchain_height_ = height;
            swapchain_format_ = PixelFormat::BGRA8_UNORM;

            return createSwapchainObjects(width, height);
        }

        bool MetalDeviceRHI::createSwapchainObjects(uint32_t width, uint32_t height)
        {
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;

            // Swapchain render pass (BGRA8 color, no depth - present is 2D text/overlay).
            RenderPassDesc rpd;
            rpd.attachment_count = 1;
            rpd.name = "swapchain";
            rpd.attachments[0].format = PixelFormat::BGRA8_UNORM;
            rpd.attachments[0].load_op = LoadOp::Clear;
            rpd.attachments[0].store_op = StoreOp::Store;
            rpd.attachments[0].is_depth_stencil = false;
            swapchain_render_pass_ = static_cast<MetalRenderPassRHI *>(createRenderPass(rpd));
            if (!swapchain_render_pass_) return false;

            // Framebuffer holds the MTLRenderPassDescriptor; its color texture
            // is replaced with the current drawable's texture each frame.
            MTLRenderPassDescriptor *rpass = [[MTLRenderPassDescriptor alloc] init];
            MTLRenderPassColorAttachmentDescriptor *ca = rpass.colorAttachments[0];
            ca.loadAction = MTLLoadActionDontCare;
            ca.storeAction = MTLStoreActionStore;

            MetalFramebufferRHI *fb = new MetalFramebufferRHI();
            fb->render_pass_descriptor = rpass;
            fb->width_ = width;
            fb->height_ = height;
            fb->is_swapchain_ = true;
            swapchain_framebuffer_ = fb;
            framebuffers_.push_back(fb);

            // The color attachment texture is replaced with the current
            // drawable's texture in beginFrame(); depth for the HDR scene
            // target is created lazily by the renderer's own framebuffer.
            (void)dev;
            return true;
        }

        void MetalDeviceRHI::destroySwapchain()
        {
            sw_layer_ = nullptr;
            swapchain_render_pass_ = nullptr;
            swapchain_framebuffer_ = nullptr;
        }

        void MetalDeviceRHI::resizeSwapchain(uint32_t width, uint32_t height)
        {
            swapchain_width_ = width;
            swapchain_height_ = height;
            CAMetalLayer *layer = (__bridge CAMetalLayer *)sw_layer_;
            if (layer) layer.drawableSize = CGSizeMake(width, height);
        }

        bool MetalDeviceRHI::beginFrame()
        {
            if (!initialized_) return false;
            id<MTLCommandQueue> queue = (__bridge id<MTLCommandQueue>)command_queue_;
            id<MTLCommandBuffer> cb = [queue commandBuffer];

            if (desc_.native_window)
            {
                CAMetalLayer *layer = (__bridge CAMetalLayer *)sw_layer_;
                id<CAMetalDrawable> drawable = [layer nextDrawable];
                if (drawable)
                {
                    drawable_ = (__bridge void *)drawable;
                    // Bind the drawable's texture as the swapchain color
                    // attachment for this frame.
                    if (swapchain_framebuffer_ && swapchain_framebuffer_->render_pass_descriptor)
                    {
                        MTLRenderPassDescriptor *rpass = (MTLRenderPassDescriptor *)
                            swapchain_framebuffer_->render_pass_descriptor;
                        rpass.colorAttachments[0].texture = drawable.texture;
                        rpass.colorAttachments[0].loadAction = MTLLoadActionClear;
                        rpass.colorAttachments[0].clearColor = MTLClearColorMake(0.0, 0.0, 0.0, 1.0);
                    }
                }
            }

            frame_command_buffer_ = new MetalCommandBufferRHI();
            frame_command_buffer_->device = this;
            frame_command_buffer_->mtl_command_buffer = (__bridge void *)cb;
            frame_command_buffer_->begin(CommandBufferUsage::RenderFrame);
            return true;
        }

        void MetalDeviceRHI::endFrame()
        {
            if (!frame_command_buffer_) return;
            id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)frame_command_buffer_->mtl_command_buffer;
            if (frame_command_buffer_->mtl_encoder)
                [(__bridge id<MTLRenderCommandEncoder>)frame_command_buffer_->mtl_encoder endEncoding];
            frame_command_buffer_->mtl_encoder = nullptr;
            if (drawable_)
            {
                id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)drawable_;
                [cb presentDrawable:drawable];
                [cb commit];
                drawable_ = nullptr;
            }
            else
            {
                [cb commit];
            }
            last_command_buffer_ = (void *)cb;
            delete frame_command_buffer_;
            frame_command_buffer_ = nullptr;
            ++frame_number_;
        }

        void MetalDeviceRHI::waitIdle()
        {
            // waitUntilCompleted is a command-buffer method; wait on the most
            // recently submitted command buffer (tracked in endFrame/submit).
            if (last_command_buffer_)
            {
                id<MTLCommandBuffer> cb = (id<MTLCommandBuffer>)last_command_buffer_;
                [cb waitUntilCompleted];
            }
        }

        // ---- object creation ----------------------------------------------
        RHIBuffer *MetalDeviceRHI::createBuffer(const BufferDesc &desc)
        {
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
            MTLResourceOptions opts = MTLResourceStorageModeShared;
            id<MTLBuffer> b = [dev newBufferWithLength:desc.size options:opts];
            if (!b) return nullptr;
            if (desc.initial_data && desc.initial_data_size > 0)
                memcpy(b.contents, desc.initial_data, desc.initial_data_size);
            MetalBufferRHI *buf = new MetalBufferRHI();
            buf->mtl_buffer = (__bridge void *)b;
            buf->size_ = desc.size;
            buf->usage_ = desc.usage;
            buf->memory_usage_ = desc.memory_usage;
            buf->name_ = desc.name;
            buffers_.push_back(buf);
            return buf;
        }

        RHITexture *MetalDeviceRHI::createTexture(const TextureDesc &desc)
        {
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
            MTLTextureDescriptor *td = [[MTLTextureDescriptor alloc] init];
            td.width = desc.width;
            td.height = desc.height;
            td.mipmapLevelCount = std::max<uint32_t>(1, desc.mip_levels);
            MTLPixelFormatMap fm = mtlPixelFormatMap(desc.format);
            td.pixelFormat = (MTLPixelFormat)fm.mtl_pixel_format;
            bool is_cube = (desc.type == TextureType::TexCube);
            td.textureType = is_cube ? MTLTextureTypeCube : MTLTextureType2D;
            if (is_cube) td.arrayLength = 6;
            else td.arrayLength = std::max<uint32_t>(1, desc.array_layers);
            // Render targets (HDR scene / IBL bake / cubemap / capture) are
            // GPU-only Private; textures uploaded from the CPU (env map written
            // via writeTexture, glyph atlas, white quad) are Shared.
            bool is_render_target = (desc.usage & (uint32_t)TextureUsageFlagBits::RenderTarget) != 0;
            bool needs_cpu_upload = (desc.usage & (uint32_t)TextureUsageFlagBits::TransferDst) != 0 &&
                                    !is_render_target;
            td.storageMode = needs_cpu_upload ? MTLStorageModeShared : MTLStorageModePrivate;
            td.usage = MTLTextureUsageShaderRead | MTLTextureUsageRenderTarget;
            if (desc.mip_levels > 1) td.usage |= MTLTextureUsageShaderWrite;
            id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
            MetalTextureRHI *t = new MetalTextureRHI();
            t->mtl_texture = (__bridge void *)tex;
            t->mtl_view = (__bridge void *)tex;
            t->desc_ = desc;
            t->layout_ = TextureLayout::ShaderReadOnlyOptimal;
            textures_.push_back(t);
            return t;
        }

        RHISampler *MetalDeviceRHI::createSampler(const SamplerDesc &desc)
        {
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
            MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
            sd.minFilter = desc.min_filter == FilterMode::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
            sd.magFilter = desc.mag_filter == FilterMode::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
            sd.sAddressMode = (desc.address_u == AddressMode::Repeat) ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
            sd.tAddressMode = (desc.address_v == AddressMode::Repeat) ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
            sd.rAddressMode = MTLSamplerAddressModeClampToEdge;
            // Linear min filter enables mipmap selection (used by the IBL
            // prefilter LOD sampling); mag stays point or linear per request.
            if (desc.min_filter == FilterMode::Linear)
                sd.mipFilter = MTLSamplerMipFilterLinear;
            else
                sd.mipFilter = MTLSamplerMipFilterNearest;
            sd.maxAnisotropy = desc.max_anisotropy > 1.0f ? 8 : 1;
            id<MTLSamplerState> s = [dev newSamplerStateWithDescriptor:sd];
            MetalSamplerRHI *sam = new MetalSamplerRHI();
            sam->mtl_sampler = (__bridge void *)s;
            sam->desc_ = desc;
            samplers_.push_back(sam);
            return sam;
        }

        RHIShader *MetalDeviceRHI::createShader(const ShaderStageDesc &desc)
        {
            MetalShaderRHI *sh = new MetalShaderRHI();
            sh->stage_ = desc.stage;
            sh->name_ = desc.name;
            // Look up the function by logical name.
            for (uint32_t i = 0; i < kMetalShaderCount; ++i)
            {
                if (std::string(kMetalShaders[i].name) == desc.name)
                {
                    sh->mtl_function = compileShader(nullptr, kMetalShaders[i].entry, desc.stage);
                    break;
                }
            }
            shaders_.push_back(sh);
            return sh;
        }

        RHIResourceLayout *MetalDeviceRHI::createResourceLayout(const ResourceLayoutDesc &desc)
        {
            MetalResourceLayoutRHI *l = new MetalResourceLayoutRHI(desc);
            layouts_.push_back(l);
            return l;
        }

        RHIPipeline *MetalDeviceRHI::createPipeline(const PipelineDesc &desc)
        {
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
            MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];

            // shaders
            for (const auto &sd : desc.shaders)
            {
                id<MTLFunction> fn = nullptr;
                for (uint32_t i = 0; i < kMetalShaderCount; ++i)
                {
                    if (std::string(kMetalShaders[i].name) == sd.name)
                        fn = (__bridge id<MTLFunction>)compileShader(nullptr, kMetalShaders[i].entry, sd.stage);
                }
                if (sd.stage == ShaderStageFlagBits::Vertex) pd.vertexFunction = fn;
                else if (sd.stage == ShaderStageFlagBits::Fragment) pd.fragmentFunction = fn;
            }

            // color target format
            if (desc.color_format_count > 0)
            {
                MTLPixelFormatMap fm = mtlPixelFormatMap((PixelFormat)desc.color_formats[0]);
                pd.colorAttachments[0].pixelFormat = (MTLPixelFormat)fm.mtl_pixel_format;
                if (desc.blend.attachments[0].blend_enable)
                {
                    pd.colorAttachments[0].blendingEnabled = YES;
                    pd.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
                    pd.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                    pd.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
                    pd.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
                    pd.colorAttachments[0].rgbBlendOperation = MTLBlendOperationAdd;
                }
            }
            // depth.  Metal's pipeline validation requires a valid (not
            // Invalid) depthAttachmentPixelFormat for ANY pipeline that can
            // be drawn into a depth render pass, and a valid
            // stencilAttachmentPixelFormat for depth+stencil formats.
            // Leaving depthAttachmentPixelFormat as Invalid (the default)
            // triggers the assert for a pipeline drawn into the HDR scene
            // pass (which has a depth texture) even when depth testing is
            // disabled.  Default to Depth24Unorm_Stencil8 whenever the
            // caller did not specify a depth format; Metal ignores a depth
            // attachment on passes that don't have one (the present pass).
            MTLPixelFormat depth_pf = MTLPixelFormatInvalid;
            if (desc.depth_format != PixelFormat::Undefined)
            {
                switch (desc.depth_format)
                {
                case PixelFormat::D32_SFLOAT:
                    depth_pf = MTLPixelFormatDepth32Float; break;
                case PixelFormat::D32_SFLOAT_S8_UINT:
                    depth_pf = MTLPixelFormatDepth32Float_Stencil8; break;
                case PixelFormat::D24_UNORM_S8_UINT:
                default:
                    depth_pf = MTLPixelFormatDepth24Unorm_Stencil8; break;
                }
            }
            else
            {
                // No explicit depth format: default so the pipeline always has
                // a valid depth attachment for the HDR scene pass.
                depth_pf = MTLPixelFormatDepth24Unorm_Stencil8;
            }
            pd.depthAttachmentPixelFormat = depth_pf;
            if (depth_pf == MTLPixelFormatDepth24Unorm_Stencil8 ||
                depth_pf == MTLPixelFormatDepth32Float_Stencil8)
            {
                pd.stencilAttachmentPixelFormat = depth_pf;
            }

            // vertex descriptor
            MTLVertexDescriptor *vd = [[MTLVertexDescriptor alloc] init];
            for (const auto &attr : desc.vertex_layout.attributes)
            {
                MTLVertexFormat f;
                switch (attr.format)
                {
                case VertexFormat::Float1: f = MTLVertexFormatFloat; break;
                case VertexFormat::Float2: f = MTLVertexFormatFloat2; break;
                case VertexFormat::Float3: f = MTLVertexFormatFloat3; break;
                case VertexFormat::Float4: f = MTLVertexFormatFloat4; break;
                default: f = MTLVertexFormatFloat4; break;
                }
                vd.attributes[attr.location].format = f;
                vd.attributes[attr.location].offset = (NSUInteger)attr.offset;
                vd.attributes[attr.location].bufferIndex = 0;
            }
            vd.layouts[0].stride = (NSUInteger)desc.vertex_layout.stride;
            vd.layouts[0].stepRate = 1;
            vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;
            pd.vertexDescriptor = vd;

            NSError *err = nil;
            id<MTLRenderPipelineState> state = [dev newRenderPipelineStateWithDescriptor:pd error:&err];
            if (!state)
            {
                GEN_LOG_ERROR("[Metal] pipeline '%s' failed: %s", desc.name.c_str(),
                              err ? err.localizedDescription.UTF8String : "unknown");
                return nullptr;
            }

            MetalPipelineRHI *p = new MetalPipelineRHI();
            p->pipeline_state = (__bridge void *)state;
            p->resource_layout_ = desc.resource_layout;
            p->desc_ = desc;
            pipelines_.push_back(p);
            return p;
        }

        RHIRenderPass *MetalDeviceRHI::createRenderPass(const RenderPassDesc &desc)
        {
            uint64_t hash = 0;
            for (uint32_t i = 0; i < desc.attachment_count; ++i)
            {
                const AttachmentDesc &a = desc.attachments[i];
                hash = hash * 131 + (uint64_t)a.format;
                hash = hash * 131 + (uint64_t)(a.is_depth_stencil ? 1 : 0);
            }
            auto it = render_passes_.find(hash);
            if (it != render_passes_.end()) return it->second;

            MetalRenderPassRHI *pass = new MetalRenderPassRHI();
            pass->desc_ = desc;
            pass->hash_ = hash;
            render_passes_[hash] = pass;
            return pass;
        }

        RHIFramebuffer *MetalDeviceRHI::createFramebuffer(const FramebufferDesc &desc)
        {
            id<MTLDevice> dev = (__bridge id<MTLDevice>)device_;
            MTLRenderPassDescriptor *rpd = [[MTLRenderPassDescriptor alloc] init];
            uint32_t color_idx = 0;
            for (const auto &att : desc.attachments)
            {
                MetalTextureRHI *t = static_cast<MetalTextureRHI *>(att.texture);
                if (!t) continue;
                // The attachment carries the texture; its format lives on the
                // texture descriptor (Attachment has no separate format field).
                MTLPixelFormatMap fm = mtlPixelFormatMap(t->format());
                if (fm.is_depth)
                {
                    // Depth attachment: a 2D (or cubemap-face) view into the
                    // depth texture slice.
                    MTLRenderPassDepthAttachmentDescriptor *da = rpd.depthAttachment;
                    id<MTLTexture> depth = (__bridge id<MTLTexture>)t->mtl_texture;
                    da.texture = depth;
                    da.loadAction = MTLLoadActionDontCare;
                    da.storeAction = MTLStoreActionDontCare;
                    if (att.array_layer > 0 || att.mip_level > 0)
                    {
                        id<MTLTexture> view = [depth newTextureViewWithPixelFormat:(MTLPixelFormat)fm.mtl_pixel_format
                                               textureType:MTLTextureType2D
                                               levels:NSMakeRange(att.mip_level, 1)
                                               slices:NSMakeRange(att.array_layer, 1)];
                        if (view) da.texture = view;
                    }
                    continue;
                }
                if (color_idx >= 8) break;
                MTLRenderPassColorAttachmentDescriptor *ca = rpd.colorAttachments[color_idx];
                id<MTLTexture> tex = (__bridge id<MTLTexture>)t->mtl_texture;
                // For cubemap faces (array_layer >= 1) build a 2D view into
                // that slice/mip; otherwise use the 2D texture directly.
                if (att.array_layer > 0 || att.mip_level > 0 ||
                    t->desc_.type == TextureType::TexCube)
                {
                    MTLTextureType type = t->desc_.type == TextureType::TexCube
                                              ? MTLTextureType2D // a single face -> 2D view
                                              : MTLTextureType2D;
                    id<MTLTexture> view = [tex newTextureViewWithPixelFormat:(MTLPixelFormat)fm.mtl_pixel_format
                                           textureType:type
                                           levels:NSMakeRange(att.mip_level, 1)
                                           slices:NSMakeRange(att.array_layer, 1)];
                    ca.texture = view ?: tex;
                }
                else
                {
                    ca.texture = tex;
                }
                ca.loadAction = MTLLoadActionDontCare;
                ca.storeAction = MTLStoreActionStore;
                ++color_idx;
            }
            MetalFramebufferRHI *fb = new MetalFramebufferRHI();
            fb->render_pass_descriptor = rpd;
            fb->width_ = desc.width;
            fb->height_ = desc.height;
            fb->name_ = desc.name;
            framebuffers_.push_back(fb);
            return fb;
        }

        RHIDescriptorSet *MetalDeviceRHI::createDescriptorSet(RHIResourceLayout *layout, uint32_t set_index)
        {
            MetalDescriptorSetRHI *s = new MetalDescriptorSetRHI();
            s->layout_ = static_cast<MetalResourceLayoutRHI *>(layout);
            s->set_index_ = set_index;
            descriptor_sets_.push_back(s);
            return s;
        }

        RHICommandBuffer *MetalDeviceRHI::createCommandBuffer(CommandBufferUsage usage)
        {
            id<MTLCommandQueue> q = (__bridge id<MTLCommandQueue>)command_queue_;
            id<MTLCommandBuffer> cb = [q commandBuffer];
            MetalCommandBufferRHI *cmd = new MetalCommandBufferRHI();
            cmd->device = this;
            cmd->mtl_command_buffer = (__bridge void *)cb;
            cmd->one_time_ = usage == CommandBufferUsage::OneTimeSubmit;
            cmd->begin(usage);
            command_buffers_.push_back(cmd);
            return cmd;
        }

        // ---- destroy -------------------------------------------------------
        void MetalDeviceRHI::destroyBuffer(RHIBuffer *b)
        {
            delete static_cast<MetalBufferRHI *>(b);
        }
        void MetalDeviceRHI::destroyTexture(RHITexture *t) { delete static_cast<MetalTextureRHI *>(t); }
        void MetalDeviceRHI::destroySampler(RHISampler *s) { delete static_cast<MetalSamplerRHI *>(s); }
        void MetalDeviceRHI::destroyShader(RHIShader *s) { delete static_cast<MetalShaderRHI *>(s); }
        void MetalDeviceRHI::destroyResourceLayout(RHIResourceLayout *l) { delete static_cast<MetalResourceLayoutRHI *>(l); }
        void MetalDeviceRHI::destroyPipeline(RHIPipeline *p) { delete static_cast<MetalPipelineRHI *>(p); }
        void MetalDeviceRHI::destroyRenderPass(RHIRenderPass *p) { delete static_cast<MetalRenderPassRHI *>(p); }
        void MetalDeviceRHI::destroyFramebuffer(RHIFramebuffer *f) { delete static_cast<MetalFramebufferRHI *>(f); }
        void MetalDeviceRHI::destroyDescriptorSet(RHIDescriptorSet *s) { delete static_cast<MetalDescriptorSetRHI *>(s); }
        void MetalDeviceRHI::destroyCommandBuffer(RHICommandBuffer *c)
        {
            MetalCommandBufferRHI *cmd = static_cast<MetalCommandBufferRHI *>(c);
            if (cmd->mtl_encoder)
                [(__bridge id<MTLRenderCommandEncoder>)cmd->mtl_encoder endEncoding];
            delete cmd;
        }

        // ---- data upload ---------------------------------------------------
        void MetalDeviceRHI::writeBuffer(RHIBuffer *buffer, const void *data, uint64_t size, uint64_t offset)
        {
            MetalBufferRHI *b = static_cast<MetalBufferRHI *>(buffer);
            if (!b || !b->mtl_buffer) return;
            id<MTLBuffer> mb = (__bridge id<MTLBuffer>)b->mtl_buffer;
            memcpy((char *)mb.contents + offset, data, size);
        }

        void MetalDeviceRHI::writeTexture(RHITexture *texture, const void *data, uint64_t size,
                                            uint32_t width, uint32_t height, uint32_t mip, uint32_t base_layer, uint32_t layer_count)
        {
            MetalTextureRHI *t = static_cast<MetalTextureRHI *>(texture);
            if (!t || !t->mtl_texture) return;
            id<MTLTexture> tex = (__bridge id<MTLTexture>)t->mtl_texture;
            // Bytes per row for the actual target format: R8 = 1, 16F linear
            // HDR = 8 per texel, RGBA8/UNORM = 4.
            NSUInteger bpp = 4;
            switch (tex.pixelFormat)
            {
            case MTLPixelFormatR8Unorm: bpp = 1; break;
            case MTLPixelFormatRG16Float: bpp = 8; break;
            case MTLPixelFormatRGBA16Float: bpp = 8; break;
            case MTLPixelFormatR16Float: bpp = 2; break;
            default: break;
            }
            NSUInteger row = width * bpp;
            MTLRegion region = MTLRegionMake2D(0, 0, width, height);
            // writeTexture only uploads 2D source images (equirect/HDR env,
            // SDF glyph atlas, white quad); cubemaps are render targets
            // written via the framebuffer, not here.
            [tex replaceRegion:region mipmapLevel:mip withBytes:data bytesPerRow:row];
            (void)layer_count; (void)base_layer;
        }

        void MetalDeviceRHI::submitImmediate(void (*record_fn)(RHICommandBuffer *, void *), void *user_data)
        {
            MetalCommandBufferRHI *cmd =
                static_cast<MetalCommandBufferRHI *>(createCommandBuffer(CommandBufferUsage::OneTimeSubmit));
            record_fn(cmd, user_data);
            cmd->end();
            id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)cmd->mtl_command_buffer;
            [cb commit];
            [cb waitUntilCompleted];
            destroyCommandBuffer(cmd);
        }

        void MetalDeviceRHI::submitAndWait(RHICommandBuffer *cmd)
        {
            MetalCommandBufferRHI *m = static_cast<MetalCommandBufferRHI *>(cmd);
            if (m->mtl_encoder)
                [(__bridge id<MTLRenderCommandEncoder>)m->mtl_encoder endEncoding];
            id<MTLCommandBuffer> cb = (__bridge id<MTLCommandBuffer>)m->mtl_command_buffer;
            [cb commit];
            [cb waitUntilCompleted];
        }

        void MetalDeviceRHI::shutdown()
        {
            if (!device_) return;
            waitIdle();
            for (auto *c : command_buffers_) { if (c) c->end(); }
            for (auto *p : pipelines_)
            {
                if (p->pipeline_state) [(__bridge id<MTLRenderPipelineState>)p->pipeline_state release];
                delete p;
            }
            for (auto *l : layouts_) delete l;
            for (auto *s : samplers_)
            {
                if (s->mtl_sampler) [(__bridge id<MTLSamplerState>)s->mtl_sampler release];
                delete s;
            }
            for (auto *t : textures_)
            {
                if (t->mtl_texture) [(__bridge id<MTLTexture>)t->mtl_texture release];
                delete t;
            }
            for (auto *b : buffers_)
            {
                if (b->mtl_buffer) [(__bridge id<MTLBuffer>)b->mtl_buffer release];
                delete b;
            }
            for (auto *fb : framebuffers_)
            {
                if (fb->render_pass_descriptor) [(__bridge MTLRenderPassDescriptor *)fb->render_pass_descriptor release];
                delete fb;
            }
            for (auto &kv : render_passes_)
            {
                if (kv.second) delete kv.second;
            }
            render_passes_.clear();
            for (auto *ds : descriptor_sets_) delete ds;
            delete frame_command_buffer_;
            frame_command_buffer_ = nullptr;
            if (library_) [(__bridge id<MTLLibrary>)library_ release];
            if (command_queue_) [(__bridge id<MTLCommandQueue>)command_queue_ release];
            if (device_) [(__bridge id<MTLDevice>)device_ release];
            if (drawable_) [(__bridge id<CAMetalDrawable>)drawable_ release];
            sw_layer_ = nullptr;
            library_ = nullptr;
            command_queue_ = nullptr;
            device_ = nullptr;
            drawable_ = nullptr;
            initialized_ = false;
        }

    } // namespace RHI
} // namespace Genesis
