/*
 * Genesis Render - Metal backend implementation (Objective-C++, ARC).
 *
 * Maps the explicit RHI (render passes, pipelines, descriptor sets, command
 * buffers) onto Metal:
 *
 *   RHIRenderPass + RHIFramebuffer -> MTLRenderPassDescriptor built at
 *                                     beginRenderPass (load/store/clear from
 *                                     the pass, textures/level/slice from the
 *                                     framebuffer)
 *   RHIPipeline                    -> MTLRenderPipelineState + depth-stencil
 *                                     state + raster state (cull / winding /
 *                                     fill) applied at bindPipeline
 *   RHIDescriptorSet               -> a binding table replayed onto the
 *                                     encoder at bindDescriptorSets
 *                                     (slot = set * 8 + binding, dynamic
 *                                     offsets consumed in binding order)
 *   push constants                 -> setVertexBytes / setFragmentBytes at 24
 *   frames in flight               -> dispatch semaphore (2 frames)
 *
 * All GPU textures use private storage; CPU uploads and readbacks go through
 * shared staging buffers and blit encoders, so cubemaps, mip chains and
 * render targets behave the same as on Vulkan.
 */
#include "rhi/metal/metal_backend.hpp"
#include "foundation_layer/genesis_log.hpp"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_metal.h>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#if !__has_feature(objc_arc)
#error "metal_backend.mm must be compiled with -fobjc-arc"
#endif

namespace Genesis
{
    namespace RHI
    {
        namespace
        {
            //==============================================================
            // Format / state conversion
            //==============================================================
            MTLPixelFormat mtlFormat(PixelFormat f, bool depth24_supported)
            {
                switch (f)
                {
                case PixelFormat::R8_UNORM: return MTLPixelFormatR8Unorm;
                case PixelFormat::RG8_UNORM: return MTLPixelFormatRG8Unorm;
                case PixelFormat::RGB8_UNORM: return MTLPixelFormatRGBA8Unorm;
                case PixelFormat::RGBA8_UNORM: return MTLPixelFormatRGBA8Unorm;
                case PixelFormat::RGBA8_SRGB: return MTLPixelFormatRGBA8Unorm_sRGB;
                case PixelFormat::BGRA8_UNORM: return MTLPixelFormatBGRA8Unorm;
                case PixelFormat::R16_SFLOAT: return MTLPixelFormatR16Float;
                case PixelFormat::RG16_SFLOAT: return MTLPixelFormatRG16Float;
                case PixelFormat::RGBA16_SFLOAT: return MTLPixelFormatRGBA16Float;
                case PixelFormat::R32_SFLOAT: return MTLPixelFormatR32Float;
                case PixelFormat::RG32_SFLOAT: return MTLPixelFormatRG32Float;
                case PixelFormat::RGB32_SFLOAT: return MTLPixelFormatRGBA32Float;
                case PixelFormat::RGBA32_SFLOAT: return MTLPixelFormatRGBA32Float;
                case PixelFormat::RGBA32_UINT: return MTLPixelFormatRGBA32Uint;
                case PixelFormat::R32_UINT: return MTLPixelFormatR32Uint;
                case PixelFormat::D16_UNORM: return MTLPixelFormatDepth16Unorm;
                case PixelFormat::D24_UNORM_S8_UINT:
#if TARGET_OS_OSX
                    return depth24_supported ? MTLPixelFormatDepth24Unorm_Stencil8 : MTLPixelFormatDepth32Float_Stencil8;
#else
                    (void)depth24_supported;
                    return MTLPixelFormatDepth32Float_Stencil8;
#endif
                case PixelFormat::D32_SFLOAT: return MTLPixelFormatDepth32Float;
                case PixelFormat::D32_SFLOAT_S8_UINT: return MTLPixelFormatDepth32Float_Stencil8;
                default: return MTLPixelFormatInvalid;
                }
            }

            bool formatHasStencil(MTLPixelFormat f)
            {
                return f == MTLPixelFormatDepth32Float_Stencil8
#if TARGET_OS_OSX
                       || f == MTLPixelFormatDepth24Unorm_Stencil8
#endif
                       || f == MTLPixelFormatStencil8;
            }

            bool formatIsDepth(MTLPixelFormat f)
            {
                return f == MTLPixelFormatDepth16Unorm || f == MTLPixelFormatDepth32Float ||
                       f == MTLPixelFormatDepth32Float_Stencil8
#if TARGET_OS_OSX
                       || f == MTLPixelFormatDepth24Unorm_Stencil8
#endif
                    ;
            }

            // Bytes per texel as stored in the Metal texture (RGB8 is widened).
            uint32_t mtlBytesPerPixel(PixelFormat f)
            {
                switch (f)
                {
                case PixelFormat::R8_UNORM: return 1;
                case PixelFormat::RG8_UNORM: return 2;
                case PixelFormat::RGB8_UNORM:
                case PixelFormat::RGBA8_UNORM:
                case PixelFormat::RGBA8_SRGB:
                case PixelFormat::BGRA8_UNORM: return 4;
                case PixelFormat::R16_SFLOAT: return 2;
                case PixelFormat::RG16_SFLOAT: return 4;
                case PixelFormat::RGBA16_SFLOAT: return 8;
                case PixelFormat::R32_SFLOAT:
                case PixelFormat::R32_UINT: return 4;
                case PixelFormat::RG32_SFLOAT: return 8;
                case PixelFormat::RGB32_SFLOAT:
                case PixelFormat::RGBA32_SFLOAT:
                case PixelFormat::RGBA32_UINT: return 16;
                default: return 4;
                }
            }

            MTLVertexFormat mtlVertexFormat(VertexFormat f)
            {
                switch (f)
                {
                case VertexFormat::Float1: return MTLVertexFormatFloat;
                case VertexFormat::Float2: return MTLVertexFormatFloat2;
                case VertexFormat::Float3: return MTLVertexFormatFloat3;
                case VertexFormat::Float4: return MTLVertexFormatFloat4;
                case VertexFormat::UByte4: return MTLVertexFormatUChar4;
                case VertexFormat::UByte4Norm: return MTLVertexFormatUChar4Normalized;
                case VertexFormat::Short2: return MTLVertexFormatShort2;
                case VertexFormat::Short4: return MTLVertexFormatShort4;
                case VertexFormat::Short2Norm: return MTLVertexFormatShort2Normalized;
                case VertexFormat::Short4Norm: return MTLVertexFormatShort4Normalized;
                case VertexFormat::UInt1: return MTLVertexFormatUInt;
                case VertexFormat::UInt2: return MTLVertexFormatUInt2;
                case VertexFormat::UInt4: return MTLVertexFormatUInt4;
                default: return MTLVertexFormatFloat3;
                }
            }

            MTLBlendFactor mtlBlendFactor(BlendFactor f)
            {
                switch (f)
                {
                case BlendFactor::Zero: return MTLBlendFactorZero;
                case BlendFactor::One: return MTLBlendFactorOne;
                case BlendFactor::SrcColor: return MTLBlendFactorSourceColor;
                case BlendFactor::OneMinusSrcColor: return MTLBlendFactorOneMinusSourceColor;
                case BlendFactor::DstColor: return MTLBlendFactorDestinationColor;
                case BlendFactor::OneMinusDstColor: return MTLBlendFactorOneMinusDestinationColor;
                case BlendFactor::SrcAlpha: return MTLBlendFactorSourceAlpha;
                case BlendFactor::OneMinusSrcAlpha: return MTLBlendFactorOneMinusSourceAlpha;
                case BlendFactor::DstAlpha: return MTLBlendFactorDestinationAlpha;
                case BlendFactor::OneMinusDstAlpha: return MTLBlendFactorOneMinusDestinationAlpha;
                case BlendFactor::ConstantColor: return MTLBlendFactorBlendColor;
                case BlendFactor::OneMinusConstantColor: return MTLBlendFactorOneMinusBlendColor;
                }
                return MTLBlendFactorOne;
            }

            MTLBlendOperation mtlBlendOp(BlendOp op)
            {
                switch (op)
                {
                case BlendOp::Add: return MTLBlendOperationAdd;
                case BlendOp::Subtract: return MTLBlendOperationSubtract;
                case BlendOp::ReverseSubtract: return MTLBlendOperationReverseSubtract;
                case BlendOp::Min: return MTLBlendOperationMin;
                case BlendOp::Max: return MTLBlendOperationMax;
                }
                return MTLBlendOperationAdd;
            }

            MTLCompareFunction mtlCompare(CompareOp op)
            {
                switch (op)
                {
                case CompareOp::Never: return MTLCompareFunctionNever;
                case CompareOp::Less: return MTLCompareFunctionLess;
                case CompareOp::Equal: return MTLCompareFunctionEqual;
                case CompareOp::LessOrEqual: return MTLCompareFunctionLessEqual;
                case CompareOp::Greater: return MTLCompareFunctionGreater;
                case CompareOp::NotEqual: return MTLCompareFunctionNotEqual;
                case CompareOp::GreaterOrEqual: return MTLCompareFunctionGreaterEqual;
                case CompareOp::Always: return MTLCompareFunctionAlways;
                }
                return MTLCompareFunctionLessEqual;
            }

            MTLSamplerAddressMode mtlAddress(AddressMode m)
            {
                switch (m)
                {
                case AddressMode::Repeat: return MTLSamplerAddressModeRepeat;
                case AddressMode::MirroredRepeat: return MTLSamplerAddressModeMirrorRepeat;
                case AddressMode::ClampToEdge: return MTLSamplerAddressModeClampToEdge;
                case AddressMode::ClampToBorder: return MTLSamplerAddressModeClampToBorderColor;
                }
                return MTLSamplerAddressModeRepeat;
            }

            MTLPrimitiveType mtlPrimitive(PrimitiveTopology t)
            {
                switch (t)
                {
                case PrimitiveTopology::TriangleList: return MTLPrimitiveTypeTriangle;
                case PrimitiveTopology::TriangleStrip: return MTLPrimitiveTypeTriangleStrip;
                case PrimitiveTopology::LineList: return MTLPrimitiveTypeLine;
                case PrimitiveTopology::LineStrip: return MTLPrimitiveTypeLineStrip;
                case PrimitiveTopology::PointList: return MTLPrimitiveTypePoint;
                }
                return MTLPrimitiveTypeTriangle;
            }

            MTLColorWriteMask mtlWriteMask(uint32_t mask)
            {
                MTLColorWriteMask m = MTLColorWriteMaskNone;
                if (mask & (uint32_t)ColorComponentFlagBits::R) m |= MTLColorWriteMaskRed;
                if (mask & (uint32_t)ColorComponentFlagBits::G) m |= MTLColorWriteMaskGreen;
                if (mask & (uint32_t)ColorComponentFlagBits::B) m |= MTLColorWriteMaskBlue;
                if (mask & (uint32_t)ColorComponentFlagBits::A) m |= MTLColorWriteMaskAlpha;
                return m;
            }

            NSString *nsString(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()]; }
        } // namespace

        class MTLDeviceRHI;

        //==================================================================
        // Resources
        //==================================================================
        class MTLBufferRHI : public RHIBuffer
        {
        public:
            id<MTLBuffer> buffer = nil;
            BufferDesc desc;

            uint64_t size() const override { return desc.size; }
            BufferUsageFlags usage() const override { return desc.usage; }
            MemoryUsage memoryUsage() const override { return desc.memory_usage; }
            const std::string &name() const override { return desc.name; }
            void *map(uint64_t offset, uint64_t) override { return buffer ? (uint8_t *)buffer.contents + offset : nullptr; }
            void unmap() override {}
            void *nativeHandle() const override { return (__bridge void *)buffer; }
        };

        class MTLTextureRHI : public RHITexture
        {
        public:
            id<MTLTexture> texture = nil;
            TextureDesc desc_;
            TextureLayout layout_ = TextureLayout::Undefined;

            const TextureDesc &desc() const override { return desc_; }
            uint32_t width() const override { return desc_.width; }
            uint32_t height() const override { return desc_.height; }
            uint32_t mipLevels() const override { return desc_.mip_levels; }
            uint32_t layerCount() const override { return desc_.layerCount(); }
            PixelFormat format() const override { return desc_.format; }
            const std::string &name() const override { return desc_.name; }
            TextureLayout currentLayout() const override { return layout_; }
            void setCurrentLayout(TextureLayout l) override { layout_ = l; }
            void *nativeHandle() const override { return (__bridge void *)texture; }
            void *nativeViewHandle() const override { return (__bridge void *)texture; }
        };

        class MTLSamplerRHI : public RHISampler
        {
        public:
            id<MTLSamplerState> sampler = nil;
            SamplerDesc desc_;
            const SamplerDesc &desc() const override { return desc_; }
            const std::string &name() const override { return desc_.name; }
            void *nativeHandle() const override { return (__bridge void *)sampler; }
        };

        class MTLShaderRHI : public RHIShader
        {
        public:
            id<MTLFunction> function = nil;
            ShaderStageFlagBits stage_ = ShaderStageFlagBits::Vertex;
            std::string name_;
            ShaderStageFlagBits stage() const override { return stage_; }
            const std::string &name() const override { return name_; }
            void *nativeHandle() const override { return (__bridge void *)function; }
        };

        class MTLResourceLayoutRHI : public RHIResourceLayout
        {
        public:
            ResourceLayoutDesc desc_;
            const ResourceLayoutDesc &desc() const override { return desc_; }
            const ResourceBinding *findBinding(uint32_t set, uint32_t binding) const override
            {
                for (const auto &b : desc_.bindings)
                    if (b.set == set && b.binding == binding) return &b;
                return nullptr;
            }
            void *nativeHandle() const override { return nullptr; }
        };

        class MTLPipelineRHI : public RHIPipeline
        {
        public:
            id<MTLRenderPipelineState> state = nil;
            id<MTLComputePipelineState> compute_state = nil;
            bool compute_ = false;
            id<MTLDepthStencilState> depth_state = nil;
            PipelineDesc desc_;
            MTLPrimitiveType primitive = MTLPrimitiveTypeTriangle;
            MTLCullMode cull = MTLCullModeBack;
            MTLWinding winding = MTLWindingClockwise;
            MTLTriangleFillMode fill = MTLTriangleFillModeFill;
            MTLDepthClipMode clip = MTLDepthClipModeClip;
            uint32_t stencil_reference = 0;

            const std::string &name() const override { return desc_.name; }
            bool isCompute() const override { return compute_; }
            const ResourceLayoutDesc &resourceLayout() const override { return desc_.resource_layout; }
            void *nativeHandle() const override { return (__bridge void *)state; }
        };

        class MTLRenderPassRHI : public RHIRenderPass
        {
        public:
            RenderPassDesc desc_;
            uint64_t hash = 0;
            const RenderPassDesc &desc() const override { return desc_; }
            const std::string &name() const override { return desc_.name; }
            void *nativeHandle() const override { return nullptr; }
        };

        class MTLFramebufferRHI : public RHIFramebuffer
        {
        public:
            struct Attachment
            {
                MTLTextureRHI *texture = nullptr;
                uint32_t mip = 0;
                uint32_t layer = 0;
            };
            std::vector<Attachment> attachments; // one per render pass attachment
            MTLRenderPassRHI *pass = nullptr;
            uint32_t w = 1, h = 1;
            std::string name_;
            bool is_swapchain = false;

            uint32_t width() const override { return w; }
            uint32_t height() const override { return h; }
            RHIRenderPass *renderPass() const override { return pass; }
            const std::string &name() const override { return name_; }
            void *nativeHandle() const override { return nullptr; }
        };

        class MTLDescriptorSetRHI : public RHIDescriptorSet
        {
        public:
            struct Entry
            {
                ResourceType type = ResourceType::UniformBuffer;
                MTLBufferRHI *buffer = nullptr;
                uint64_t offset = 0;
                MTLTextureRHI *texture = nullptr;
                MTLSamplerRHI *sampler = nullptr;
            };
            MTLResourceLayoutRHI *layout_ = nullptr;
            uint32_t set_index = 0;
            std::map<uint32_t, Entry> entries; // ordered by binding (dynamic offset order)

            RHIResourceLayout *layout() const override { return layout_; }
            void write(const WriteDescriptorSet *writes, uint32_t count) override
            {
                for (uint32_t i = 0; i < count; ++i)
                {
                    const WriteDescriptorSet &w = writes[i];
                    Entry e;
                    e.type = w.type;
                    if (resourceTypeIsBuffer(w.type))
                    {
                        if (!w.buffer_info) continue;
                        e.buffer = static_cast<MTLBufferRHI *>(w.buffer_info->buffer);
                        e.offset = w.buffer_info->offset;
                    }
                    else
                    {
                        if (!w.image_info) continue;
                        e.texture = static_cast<MTLTextureRHI *>(w.image_info->texture);
                        e.sampler = static_cast<MTLSamplerRHI *>(w.image_info->sampler);
                    }
                    entries[w.binding] = e;
                }
            }
            void *nativeHandle() const override { return nullptr; }
        };

        //==================================================================
        // Command buffer
        //==================================================================
        class MTLCommandBufferRHI : public RHICommandBuffer
        {
        public:
            MTLDeviceRHI *device = nullptr;
            id<MTLCommandBuffer> cb = nil;
            id<MTLRenderCommandEncoder> render = nil;
            id<MTLBlitCommandEncoder> blit = nil;
            id<MTLComputeCommandEncoder> compute = nil;
            bool compute_bound = false;
            bool frame_buffer = false; // owned by the device frame loop

            MTLPipelineRHI *pipeline = nullptr;
            MTLBufferRHI *index_buffer = nullptr;
            uint64_t index_offset = 0;
            IndexType index_type = IndexType::UInt16;
            MTLFramebufferRHI *framebuffer = nullptr;
            MTLRenderPassRHI *pass = nullptr;

            void begin(CommandBufferUsage usage) override;
            void end() override;
            void beginRenderPass(RHIRenderPass *pass, RHIFramebuffer *fb, const RenderPassBeginInfo &info) override;
            void endRenderPass() override;
            void bindPipeline(RHIPipeline *pipeline) override;
            void bindVertexBuffer(RHIBuffer *buffer, uint64_t offset) override;
            void bindIndexBuffer(RHIBuffer *buffer, IndexType type, uint64_t offset) override;
            void bindDescriptorSets(RHIPipeline *pipeline, RHIDescriptorSet *const *sets, uint32_t set_count,
                                    const uint32_t *dynamic_offsets, uint32_t offset_count) override;
            void setPushConstants(const void *data, uint32_t size) override;
            void setViewport(const Viewport &vp) override;
            void setScissor(const Rect2D &rect) override;
            void draw(uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count, uint32_t first_instance) override;
            void drawIndexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset,
                             uint32_t instance_count, uint32_t first_instance) override;
            void dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z) override;
            void copyBufferToBuffer(RHIBuffer *src, uint64_t src_offset, RHIBuffer *dst, uint64_t dst_offset, uint64_t size) override;
            void copyBufferToImage(RHIBuffer *src, RHITexture *dst, const BufferImageCopy &region) override;
            void copyImageToBuffer(RHITexture *src, RHIBuffer *dst, const BufferImageCopy &region) override;
            void pipelineBarrier(const TextureBarrier *barriers, uint32_t count) override
            {
                // Metal tracks hazards for device-created resources; only the
                // layout bookkeeping shared with the other backends is kept.
                for (uint32_t i = 0; i < count; ++i)
                    if (barriers[i].texture) static_cast<MTLTextureRHI *>(barriers[i].texture)->setCurrentLayout(barriers[i].new_layout);
            }
            void bufferBarrier(RHIBuffer *, BufferAccess, BufferAccess) override
            {
                // MTLResource hazardTrackingMode is tracked for device-created
                // buffers, so the encoder transition is sufficient.
            }
            void generateMipmaps(RHITexture *texture) override;
            void blitImage(RHITexture *src, uint32_t src_mip, RHITexture *dst, uint32_t dst_mip,
                           uint32_t src_layer, uint32_t dst_layer, uint32_t layer_count, bool linear_filter) override;
            bool readImagePixels(RHITexture *src, uint32_t mip, uint32_t layer, void *dst, uint32_t dst_size_bytes) override;
            void *nativeHandle() const override { return (__bridge void *)cb; }

            void endEncoders()
            {
                if (render) { [render endEncoding]; render = nil; }
                if (compute) { [compute endEncoding]; compute = nil; compute_bound = false; }
                if (blit) { [blit endEncoding]; blit = nil; }
            }
            id<MTLComputeCommandEncoder> computeEncoder()
            {
                if (render) { [render endEncoding]; render = nil; }
                if (blit) { [blit endEncoding]; blit = nil; }
                if (!compute) compute = [cb computeCommandEncoder];
                return compute;
            }
            id<MTLBlitCommandEncoder> blitEncoder()
            {
                if (render) { [render endEncoding]; render = nil; }
                if (!blit) blit = [cb blitCommandEncoder];
                return blit;
            }
        };

        //==================================================================
        // Device
        //==================================================================
        class MTLDeviceRHI : public RHIDevice
        {
        public:
            id<MTLDevice> device = nil;
            id<MTLCommandQueue> queue = nil;
            CAMetalLayer *layer = nil;
            SDL_MetalView metal_view = nullptr;
            id<CAMetalDrawable> drawable = nil;
            dispatch_semaphore_t frame_semaphore = nullptr;
            bool depth24_supported = false;

            DeviceDesc desc_;
            DeviceInfo info_;
            SDL_Window *window = nullptr;
            uint32_t swapchain_w = 1, swapchain_h = 1;
            MTLRenderPassRHI *swapchain_pass = nullptr;
            MTLFramebufferRHI *swapchain_fb = nullptr;
            MTLCommandBufferRHI *frame_cmd = nullptr;
            uint32_t frame_slot = 0;
            uint64_t frame_number = 0;
            bool in_frame = false;

            std::vector<MTLBufferRHI *> buffers;
            std::vector<MTLTextureRHI *> textures;
            std::vector<MTLSamplerRHI *> samplers;
            std::vector<MTLShaderRHI *> shaders;
            std::vector<MTLResourceLayoutRHI *> layouts;
            std::vector<MTLPipelineRHI *> pipelines;
            std::unordered_map<uint64_t, MTLRenderPassRHI *> passes;
            std::vector<MTLFramebufferRHI *> framebuffers;
            std::vector<MTLDescriptorSetRHI *> sets;
            std::vector<MTLCommandBufferRHI *> command_buffers;
            // Compiled MSL functions, keyed by source pointer (sources are
            // static strings from the generated shader registry).
            std::unordered_map<const void *, id<MTLFunction>> function_cache;

            ~MTLDeviceRHI() override { shutdown(); }

            MTLPixelFormat format(PixelFormat f) const { return mtlFormat(f, depth24_supported); }

            //--------------------------------------------------------------
            bool initialize(const DeviceDesc &d) override
            {
                desc_ = d;
                device = MTLCreateSystemDefaultDevice();
                if (!device)
                {
                    GEN_LOG_ERROR("[Metal] no Metal device available");
                    return false;
                }
                queue = [device newCommandQueue];
                frame_semaphore = dispatch_semaphore_create(kMetalFramesInFlight);
#if TARGET_OS_OSX
                depth24_supported = device.depth24Stencil8PixelFormatSupported;
#endif
                info_.backend = BackendType::Metal;
                info_.device_name = device.name ? device.name.UTF8String : "Metal device";
                info_.api_version = "Metal (MSL 2.1)";
                info_.max_texture_size = 16384;
                info_.max_uniform_buffer_bindings = 31;
                info_.max_texture_image_units = 31;
                info_.max_uniform_buffer_range = 64 * 1024;
                info_.min_uniform_buffer_offset_alignment = 256;
                info_.max_push_constant_size = 4096;
                info_.supports_compute = true;
                info_.supports_geometry_shader = false;
                info_.supports_tessellation = false;
                info_.supports_multisample = true;
                info_.supports_srgb_framebuffer = true;
                info_.clip_depth_zero_to_one = true;
                info_.present_row0_top = true;
                info_.depth_format = depth24_supported ? PixelFormat::D24_UNORM_S8_UINT : PixelFormat::D32_SFLOAT_S8_UINT;
                info_.frames_in_flight = kMetalFramesInFlight;

                if (desc_.native_window && !desc_.headless)
                {
                    if (!createSwapchain(desc_.native_window, desc_.width, desc_.height)) return false;
                }
                GEN_LOG_INFO("[Metal] device: %s (depth %s)", info_.device_name.c_str(),
                             depth24_supported ? "D24S8" : "D32S8");
                return true;
            }

            void shutdown() override
            {
                if (!device) return;
                waitIdle();
                for (auto *p : command_buffers) delete p;
                command_buffers.clear();
                delete frame_cmd;
                frame_cmd = nullptr;
                for (auto *p : sets) delete p;
                sets.clear();
                for (auto *p : framebuffers) delete p;
                framebuffers.clear();
                delete swapchain_fb;
                swapchain_fb = nullptr;
                for (auto &kv : passes) delete kv.second;
                passes.clear();
                swapchain_pass = nullptr;
                for (auto *p : pipelines) delete p;
                pipelines.clear();
                for (auto *p : layouts) delete p;
                layouts.clear();
                for (auto *p : shaders) delete p;
                shaders.clear();
                for (auto *p : samplers) delete p;
                samplers.clear();
                for (auto *p : textures) delete p;
                textures.clear();
                for (auto *p : buffers) delete p;
                buffers.clear();
                function_cache.clear();
                drawable = nil;
                if (metal_view)
                {
                    SDL_Metal_DestroyView(metal_view);
                    metal_view = nullptr;
                }
                layer = nil;
                queue = nil;
                device = nil;
                frame_semaphore = nullptr;
            }

            BackendType backend() const override { return BackendType::Metal; }
            const DeviceInfo &info() const override { return info_; }
            const DeviceDesc &desc() const override { return desc_; }

            //--------------------------------------------------------------
            // Swapchain (CAMetalLayer owned by an SDL Metal view)
            //--------------------------------------------------------------
            bool createSwapchain(void *native_window, uint32_t width, uint32_t height) override
            {
                window = static_cast<SDL_Window *>(native_window);
                if (!metal_view)
                {
                    metal_view = SDL_Metal_CreateView(window);
                    if (!metal_view)
                    {
                        GEN_LOG_ERROR("[Metal] SDL_Metal_CreateView failed: %s", SDL_GetError());
                        return false;
                    }
                    layer = (__bridge CAMetalLayer *)SDL_Metal_GetLayer(metal_view);
                }
                if (!layer) return false;
                layer.device = device;
                layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
                layer.framebufferOnly = YES;
                layer.maximumDrawableCount = 3;
                layer.displaySyncEnabled = desc_.vsync ? YES : NO;
                resizeSwapchain(width, height);

                RenderPassDesc rpd;
                rpd.name = "swapchain";
                rpd.attachment_count = 1;
                rpd.attachments[0].format = PixelFormat::BGRA8_UNORM;
                rpd.attachments[0].load_op = LoadOp::Clear;
                rpd.attachments[0].store_op = StoreOp::Store;
                rpd.attachments[0].final_layout = TextureLayout::PresentSrc;
                swapchain_pass = static_cast<MTLRenderPassRHI *>(createRenderPass(rpd));
                if (!swapchain_fb) swapchain_fb = new MTLFramebufferRHI();
                swapchain_fb->pass = swapchain_pass;
                swapchain_fb->is_swapchain = true;
                swapchain_fb->name_ = "swapchain";
                swapchain_fb->attachments.assign(1, MTLFramebufferRHI::Attachment{});
                swapchain_fb->w = swapchain_w;
                swapchain_fb->h = swapchain_h;
                return true;
            }

            void destroySwapchain() override
            {
                waitIdle();
                drawable = nil;
                delete swapchain_fb;
                swapchain_fb = nullptr;
                if (metal_view)
                {
                    SDL_Metal_DestroyView(metal_view);
                    metal_view = nullptr;
                }
                layer = nil;
            }

            void resizeSwapchain(uint32_t width, uint32_t height) override
            {
                swapchain_w = std::max(1u, width);
                swapchain_h = std::max(1u, height);
                if (layer) layer.drawableSize = CGSizeMake(swapchain_w, swapchain_h);
                if (swapchain_fb)
                {
                    swapchain_fb->w = swapchain_w;
                    swapchain_fb->h = swapchain_h;
                }
            }

            uint32_t swapchainWidth() const override { return swapchain_w; }
            uint32_t swapchainHeight() const override { return swapchain_h; }
            PixelFormat swapchainFormat() const override { return PixelFormat::BGRA8_UNORM; }

            //--------------------------------------------------------------
            // Frame loop
            //--------------------------------------------------------------
            bool beginFrame() override
            {
                if (!device) return false;
                dispatch_semaphore_wait(frame_semaphore, DISPATCH_TIME_FOREVER);
                ++frame_number;
                frame_slot = (uint32_t)(frame_number % kMetalFramesInFlight);
                @autoreleasepool
                {
                    drawable = layer ? [layer nextDrawable] : nil;
                }
                if (!frame_cmd)
                {
                    frame_cmd = new MTLCommandBufferRHI();
                    frame_cmd->device = this;
                    frame_cmd->frame_buffer = true;
                }
                frame_cmd->begin(CommandBufferUsage::RenderFrame);
                in_frame = true;
                return true;
            }

            RHICommandBuffer *currentCommandBuffer() override { return in_frame ? frame_cmd : nullptr; }
            RHIRenderPass *swapchainRenderPass() override { return drawable ? swapchain_pass : nullptr; }
            RHIFramebuffer *currentSwapchainFramebuffer() override { return drawable ? swapchain_fb : nullptr; }

            void endFrame() override
            {
                if (!in_frame) return;
                in_frame = false;
                frame_cmd->endEncoders();
                dispatch_semaphore_t sema = frame_semaphore;
                [frame_cmd->cb addCompletedHandler:^(id<MTLCommandBuffer>) { dispatch_semaphore_signal(sema); }];
                if (drawable) [frame_cmd->cb presentDrawable:drawable];
                [frame_cmd->cb commit];
                frame_cmd->cb = nil;
                drawable = nil;
            }

            uint32_t currentFrameIndex() const override { return frame_slot; }
            uint64_t frameNumber() const override { return frame_number; }

            void waitIdle() override
            {
                if (!queue) return;
                // The queue executes in submission order: an empty buffer that
                // completes means everything before it has completed.
                id<MTLCommandBuffer> cb = [queue commandBuffer];
                [cb commit];
                [cb waitUntilCompleted];
            }

            //--------------------------------------------------------------
            // Objects
            //--------------------------------------------------------------
            RHIBuffer *createBuffer(const BufferDesc &d) override
            {
                // Apple GPUs have unified memory: shared storage is both CPU
                // writable and fast to read on the GPU.
                id<MTLBuffer> b = [device newBufferWithLength:std::max<uint64_t>(d.size, 16)
                                                      options:MTLResourceStorageModeShared];
                if (!b)
                {
                    GEN_LOG_ERROR("[Metal] createBuffer('%s', %llu bytes) failed", d.name.c_str(), (unsigned long long)d.size);
                    return nullptr;
                }
                if (!d.name.empty()) b.label = nsString(d.name);
                if (d.initial_data && d.initial_data_size)
                    std::memcpy(b.contents, d.initial_data, (size_t)std::min(d.initial_data_size, d.size));
                MTLBufferRHI *out = new MTLBufferRHI();
                out->buffer = b;
                out->desc = d;
                out->desc.initial_data = nullptr;
                buffers.push_back(out);
                return out;
            }

            RHITexture *createTexture(const TextureDesc &d) override
            {
                MTLTextureDescriptor *td = [[MTLTextureDescriptor alloc] init];
                const bool cube = d.type == TextureType::TexCube;
                td.textureType = cube ? MTLTextureTypeCube
                                      : (d.type == TextureType::Tex3D ? MTLTextureType3D
                                                                      : (d.array_layers > 1 ? MTLTextureType2DArray : MTLTextureType2D));
                td.pixelFormat = format(d.format);
                if (td.pixelFormat == MTLPixelFormatInvalid)
                {
                    GEN_LOG_ERROR("[Metal] texture '%s': unsupported pixel format %u", d.name.c_str(), (unsigned)d.format);
                    return nullptr;
                }
                td.width = std::max(1u, d.width);
                td.height = std::max(1u, d.height);
                td.depth = d.type == TextureType::Tex3D ? std::max(1u, d.depth) : 1;
                td.mipmapLevelCount = std::max(1u, d.mip_levels);
                td.arrayLength = cube ? std::max(1u, d.array_layers) : (d.type == TextureType::Tex3D ? 1 : std::max(1u, d.array_layers));
                td.storageMode = MTLStorageModePrivate;
                MTLTextureUsage usage = MTLTextureUsageShaderRead;
                if (d.usage & ((uint32_t)TextureUsageFlagBits::RenderTarget | (uint32_t)TextureUsageFlagBits::DepthStencil))
                    usage |= MTLTextureUsageRenderTarget;
                if (d.usage & (uint32_t)TextureUsageFlagBits::Storage) usage |= MTLTextureUsageShaderWrite;
                td.usage = usage;
                id<MTLTexture> tex = [device newTextureWithDescriptor:td];
                if (!tex)
                {
                    GEN_LOG_ERROR("[Metal] createTexture('%s') failed", d.name.c_str());
                    return nullptr;
                }
                if (!d.name.empty()) tex.label = nsString(d.name);
                MTLTextureRHI *out = new MTLTextureRHI();
                out->texture = tex;
                out->desc_ = d;
                out->desc_.mip_levels = std::max(1u, d.mip_levels);
                textures.push_back(out);
                return out;
            }

            RHISampler *createSampler(const SamplerDesc &d) override
            {
                MTLSamplerDescriptor *sd = [[MTLSamplerDescriptor alloc] init];
                sd.minFilter = d.min_filter == FilterMode::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
                sd.magFilter = d.mag_filter == FilterMode::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
                sd.mipFilter = d.mipmap_mode == MipmapMode::Linear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
                sd.sAddressMode = mtlAddress(d.address_u);
                sd.tAddressMode = mtlAddress(d.address_v);
                sd.rAddressMode = mtlAddress(d.address_w);
                sd.lodMinClamp = d.min_lod;
                sd.lodMaxClamp = std::max(d.min_lod, d.max_lod);
                sd.maxAnisotropy = (NSUInteger)std::max(1.0f, std::min(16.0f, d.max_anisotropy));
                if (d.compare_enable) sd.compareFunction = mtlCompare(d.compare_op);
                id<MTLSamplerState> s = [device newSamplerStateWithDescriptor:sd];
                if (!s) return nullptr;
                MTLSamplerRHI *out = new MTLSamplerRHI();
                out->sampler = s;
                out->desc_ = d;
                samplers.push_back(out);
                return out;
            }

            id<MTLFunction> compileFunction(const ShaderStageDesc &sd)
            {
                if (sd.code_type != ShaderCodeType::MslSource || !sd.code)
                {
                    GEN_LOG_ERROR("[Metal] shader '%s' is not MSL source", sd.name.c_str());
                    return nil;
                }
                auto it = function_cache.find(sd.code);
                if (it != function_cache.end()) return it->second;
                std::string src((const char *)sd.code, (size_t)sd.code_size);
                NSError *error = nil;
                MTLCompileOptions *opts = [[MTLCompileOptions alloc] init];
                id<MTLLibrary> lib = [device newLibraryWithSource:nsString(src) options:opts error:&error];
                if (!lib)
                {
                    GEN_LOG_ERROR("[Metal] MSL compile failed for '%s': %s", sd.name.c_str(),
                                  error ? error.localizedDescription.UTF8String : "unknown error");
                    return nil;
                }
                id<MTLFunction> fn = [lib newFunctionWithName:nsString(sd.entry.empty() ? "main0" : sd.entry)];
                if (!fn)
                {
                    GEN_LOG_ERROR("[Metal] entry point '%s' missing in '%s'", sd.entry.c_str(), sd.name.c_str());
                    return nil;
                }
                function_cache[sd.code] = fn;
                return fn;
            }

            RHIShader *createShader(const ShaderStageDesc &sd) override
            {
                id<MTLFunction> fn = compileFunction(sd);
                if (!fn) return nullptr;
                MTLShaderRHI *out = new MTLShaderRHI();
                out->function = fn;
                out->stage_ = sd.stage;
                out->name_ = sd.name;
                shaders.push_back(out);
                return out;
            }

            RHIResourceLayout *createResourceLayout(const ResourceLayoutDesc &d) override
            {
                MTLResourceLayoutRHI *out = new MTLResourceLayoutRHI();
                out->desc_ = d;
                layouts.push_back(out);
                return out;
            }

            RHIPipeline *createPipeline(const PipelineDesc &d) override
            {
                if (d.is_compute)
                {
                    id<MTLFunction> fn = nil;
                    for (const ShaderStageDesc &sd : d.shaders)
                        if (sd.stage == ShaderStageFlagBits::Compute) { fn = compileFunction(sd); break; }
                    if (!fn)
                    {
                        GEN_LOG_ERROR("[Metal] compute pipeline '%s' has no compute stage", d.name.c_str());
                        return nullptr;
                    }
                    NSError *error = nil;
                    id<MTLComputePipelineState> state = [device newComputePipelineStateWithFunction:fn error:&error];
                    if (!state)
                    {
                        GEN_LOG_ERROR("[Metal] compute pipeline '%s' creation failed: %s", d.name.c_str(),
                                      error ? error.localizedDescription.UTF8String : "unknown error");
                        return nullptr;
                    }
                    MTLPipelineRHI *out = new MTLPipelineRHI();
                    out->compute_state = state;
                    out->compute_ = true;
                    out->desc_ = d;
                    out->desc_.shaders.clear();
                    pipelines.push_back(out);
                    return out;
                }
                MTLRenderPipelineDescriptor *pd = [[MTLRenderPipelineDescriptor alloc] init];
                pd.label = nsString(d.name);
                for (const ShaderStageDesc &sd : d.shaders)
                {
                    id<MTLFunction> fn = compileFunction(sd);
                    if (!fn) return nullptr;
                    if (sd.stage == ShaderStageFlagBits::Vertex) pd.vertexFunction = fn;
                    else if (sd.stage == ShaderStageFlagBits::Fragment) pd.fragmentFunction = fn;
                }
                if (!pd.vertexFunction)
                {
                    GEN_LOG_ERROR("[Metal] pipeline '%s' has no vertex stage", d.name.c_str());
                    return nullptr;
                }

                if (d.vertex_layout.valid())
                {
                    MTLVertexDescriptor *vd = [[MTLVertexDescriptor alloc] init];
                    for (const VertexAttribute &a : d.vertex_layout.attributes)
                    {
                        vd.attributes[a.location].format = mtlVertexFormat(a.format);
                        vd.attributes[a.location].offset = a.offset;
                        vd.attributes[a.location].bufferIndex = kMetalVertexBufferSlot;
                    }
                    vd.layouts[kMetalVertexBufferSlot].stride = d.vertex_layout.stride;
                    vd.layouts[kMetalVertexBufferSlot].stepFunction =
                        d.vertex_layout.per_instance ? MTLVertexStepFunctionPerInstance : MTLVertexStepFunctionPerVertex;
                    vd.layouts[kMetalVertexBufferSlot].stepRate = 1;
                    pd.vertexDescriptor = vd;
                }

                uint32_t color_index = 0;
                for (uint32_t i = 0; i < d.color_format_count && i < 8; ++i)
                {
                    if (d.color_formats[i] == PixelFormat::Undefined) continue;
                    const BlendAttachmentState &b = d.blend.attachments[std::min(i, std::max(1u, d.blend.attachment_count) - 1)];
                    MTLRenderPipelineColorAttachmentDescriptor *ca = pd.colorAttachments[color_index++];
                    ca.pixelFormat = format(d.color_formats[i]);
                    ca.writeMask = mtlWriteMask(b.write_mask);
                    ca.blendingEnabled = b.blend_enable ? YES : NO;
                    ca.sourceRGBBlendFactor = mtlBlendFactor(b.src_color);
                    ca.destinationRGBBlendFactor = mtlBlendFactor(b.dst_color);
                    ca.rgbBlendOperation = mtlBlendOp(b.color_op);
                    ca.sourceAlphaBlendFactor = mtlBlendFactor(b.src_alpha);
                    ca.destinationAlphaBlendFactor = mtlBlendFactor(b.dst_alpha);
                    ca.alphaBlendOperation = mtlBlendOp(b.alpha_op);
                }
                if (d.depth_format != PixelFormat::Undefined)
                {
                    MTLPixelFormat df = format(d.depth_format);
                    pd.depthAttachmentPixelFormat = df;
                    if (formatHasStencil(df)) pd.stencilAttachmentPixelFormat = df;
                }
                pd.inputPrimitiveTopology = (d.topology == PrimitiveTopology::LineList || d.topology == PrimitiveTopology::LineStrip)
                                                ? MTLPrimitiveTopologyClassLine
                                                : (d.topology == PrimitiveTopology::PointList ? MTLPrimitiveTopologyClassPoint
                                                                                               : MTLPrimitiveTopologyClassTriangle);

                NSError *error = nil;
                id<MTLRenderPipelineState> state = [device newRenderPipelineStateWithDescriptor:pd error:&error];
                if (!state)
                {
                    GEN_LOG_ERROR("[Metal] pipeline '%s' creation failed: %s", d.name.c_str(),
                                  error ? error.localizedDescription.UTF8String : "unknown error");
                    return nullptr;
                }

                MTLDepthStencilDescriptor *dsd = [[MTLDepthStencilDescriptor alloc] init];
                // GL/Vulkan never write depth when the depth test is disabled.
                dsd.depthCompareFunction = d.depth_stencil.depth_test ? mtlCompare(d.depth_stencil.depth_compare) : MTLCompareFunctionAlways;
                dsd.depthWriteEnabled = (d.depth_stencil.depth_test && d.depth_stencil.depth_write) ? YES : NO;
                if (d.depth_stencil.stencil_test)
                {
                    MTLStencilDescriptor *st = [[MTLStencilDescriptor alloc] init];
                    st.stencilCompareFunction = mtlCompare(d.depth_stencil.stencil_compare);
                    st.readMask = d.depth_stencil.stencil_read_mask;
                    st.writeMask = d.depth_stencil.stencil_write_mask;
                    st.depthStencilPassOperation = MTLStencilOperationReplace;
                    dsd.frontFaceStencil = st;
                    dsd.backFaceStencil = st;
                }

                MTLPipelineRHI *out = new MTLPipelineRHI();
                out->state = state;
                out->depth_state = [device newDepthStencilStateWithDescriptor:dsd];
                out->desc_ = d;
                out->desc_.shaders.clear();
                out->primitive = mtlPrimitive(d.topology);
                out->cull = d.rasterizer.cull_mode == CullMode::None    ? MTLCullModeNone
                            : d.rasterizer.cull_mode == CullMode::Front ? MTLCullModeFront
                                                                        : MTLCullModeBack;
                // The MSL vertex stages flip clip-space y (SPIRV-Cross
                // --flip-vert-y) so rows match GL/Vulkan; that mirrors the
                // winding, hence engine CCW == Metal clockwise.
                out->winding = d.rasterizer.front_face == FrontFace::CounterClockwise ? MTLWindingClockwise : MTLWindingCounterClockwise;
                out->fill = d.rasterizer.polygon_mode == PolygonMode::Line ? MTLTriangleFillModeLines : MTLTriangleFillModeFill;
                out->clip = d.rasterizer.depth_clamp ? MTLDepthClipModeClamp : MTLDepthClipModeClip;
                out->stencil_reference = d.depth_stencil.stencil_reference;
                pipelines.push_back(out);
                return out;
            }

            RHIRenderPass *createRenderPass(const RenderPassDesc &d) override
            {
                uint64_t h = 1469598103934665603ull;
                auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ull; };
                mix(d.attachment_count);
                for (uint32_t i = 0; i < d.attachment_count; ++i)
                {
                    const AttachmentDesc &a = d.attachments[i];
                    mix((uint64_t)a.format);
                    mix((uint64_t)a.load_op);
                    mix((uint64_t)a.store_op);
                    mix((uint64_t)a.stencil_load_op);
                    mix((uint64_t)a.initial_layout);
                    mix((uint64_t)a.final_layout);
                    mix(a.is_depth_stencil ? 1 : 0);
                }
                auto it = passes.find(h);
                if (it != passes.end()) return it->second;
                MTLRenderPassRHI *out = new MTLRenderPassRHI();
                out->desc_ = d;
                out->hash = h;
                passes[h] = out;
                return out;
            }

            RHIFramebuffer *createFramebuffer(const FramebufferDesc &d) override
            {
                MTLFramebufferRHI *out = new MTLFramebufferRHI();
                out->pass = static_cast<MTLRenderPassRHI *>(createRenderPass(d.render_pass));
                out->w = std::max(1u, d.width);
                out->h = std::max(1u, d.height);
                out->name_ = d.name;
                for (const auto &a : d.attachments)
                {
                    MTLFramebufferRHI::Attachment att;
                    att.texture = static_cast<MTLTextureRHI *>(a.texture);
                    att.mip = a.mip_level;
                    att.layer = a.array_layer;
                    out->attachments.push_back(att);
                }
                framebuffers.push_back(out);
                return out;
            }

            RHIDescriptorSet *createDescriptorSet(RHIResourceLayout *layout, uint32_t set_index) override
            {
                MTLDescriptorSetRHI *out = new MTLDescriptorSetRHI();
                out->layout_ = static_cast<MTLResourceLayoutRHI *>(layout);
                out->set_index = set_index;
                sets.push_back(out);
                return out;
            }

            RHICommandBuffer *createCommandBuffer(CommandBufferUsage) override
            {
                MTLCommandBufferRHI *out = new MTLCommandBufferRHI();
                out->device = this;
                command_buffers.push_back(out);
                return out;
            }

            template <typename T>
            static bool eraseAndDelete(std::vector<T *> &list, T *p)
            {
                auto it = std::find(list.begin(), list.end(), p);
                if (it == list.end()) return false;
                list.erase(it);
                delete p;
                return true;
            }

            void destroyBuffer(RHIBuffer *p) override { if (p) eraseAndDelete(buffers, static_cast<MTLBufferRHI *>(p)); }
            void destroyTexture(RHITexture *p) override { if (p) eraseAndDelete(textures, static_cast<MTLTextureRHI *>(p)); }
            void destroySampler(RHISampler *p) override { if (p) eraseAndDelete(samplers, static_cast<MTLSamplerRHI *>(p)); }
            void destroyShader(RHIShader *p) override { if (p) eraseAndDelete(shaders, static_cast<MTLShaderRHI *>(p)); }
            void destroyResourceLayout(RHIResourceLayout *p) override { if (p) eraseAndDelete(layouts, static_cast<MTLResourceLayoutRHI *>(p)); }
            void destroyPipeline(RHIPipeline *p) override { if (p) eraseAndDelete(pipelines, static_cast<MTLPipelineRHI *>(p)); }
            void destroyFramebuffer(RHIFramebuffer *p) override { if (p) eraseAndDelete(framebuffers, static_cast<MTLFramebufferRHI *>(p)); }
            void destroyDescriptorSet(RHIDescriptorSet *p) override { if (p) eraseAndDelete(sets, static_cast<MTLDescriptorSetRHI *>(p)); }
            void destroyCommandBuffer(RHICommandBuffer *p) override
            {
                MTLCommandBufferRHI *c = static_cast<MTLCommandBufferRHI *>(p);
                if (!c) return;
                c->endEncoders();
                eraseAndDelete(command_buffers, c);
            }
            void destroyRenderPass(RHIRenderPass *p) override
            {
                MTLRenderPassRHI *rp = static_cast<MTLRenderPassRHI *>(p);
                if (!rp) return;
                auto it = passes.find(rp->hash);
                if (it != passes.end() && it->second == rp) passes.erase(it);
                if (rp == swapchain_pass) swapchain_pass = nullptr;
                delete rp;
            }

            //--------------------------------------------------------------
            // Uploads (synchronous, like the other backends)
            //--------------------------------------------------------------
            void writeBuffer(RHIBuffer *buffer, const void *data, uint64_t size, uint64_t offset) override
            {
                MTLBufferRHI *b = static_cast<MTLBufferRHI *>(buffer);
                if (!b || !data || !size) return;
                if (offset + size > b->desc.size)
                {
                    GEN_LOG_ERROR("[Metal] writeBuffer('%s') out of range", b->desc.name.c_str());
                    return;
                }
                std::memcpy((uint8_t *)b->buffer.contents + offset, data, (size_t)size);
            }

            void writeTexture(RHITexture *texture, const void *data, uint64_t size, uint32_t width, uint32_t height,
                              uint32_t mip, uint32_t base_layer, uint32_t layer_count) override
            {
                MTLTextureRHI *t = static_cast<MTLTextureRHI *>(texture);
                if (!t || !data || !size) return;
                const PixelFormat pf = t->format();
                const uint32_t src_bpp = pixelFormatBytesPerPixel(pf);
                const uint32_t dst_bpp = mtlBytesPerPixel(pf);
                const size_t layer_src = (size_t)width * height * src_bpp;
                const size_t layer_dst = (size_t)width * height * dst_bpp;
                id<MTLBuffer> staging = [device newBufferWithLength:std::max<size_t>(layer_dst * layer_count, 16)
                                                            options:MTLResourceStorageModeShared];
                const uint8_t *src = static_cast<const uint8_t *>(data);
                uint8_t *dst = (uint8_t *)staging.contents;
                for (uint32_t l = 0; l < layer_count; ++l)
                {
                    if (src_bpp == dst_bpp)
                    {
                        std::memcpy(dst + l * layer_dst, src + l * layer_src, std::min(layer_src, (size_t)size - l * layer_src));
                    }
                    else
                    {
                        // RGB8 / RGB32F are stored with an alpha channel.
                        const uint32_t comp = src_bpp == 3 ? 1 : 4;
                        const uint32_t channels = 3;
                        for (size_t px = 0; px < (size_t)width * height; ++px)
                        {
                            const uint8_t *s = src + l * layer_src + px * src_bpp;
                            uint8_t *dd = dst + l * layer_dst + px * dst_bpp;
                            std::memcpy(dd, s, channels * comp);
                            if (comp == 1) dd[3] = 255;
                            else { float one = 1.0f; std::memcpy(dd + 12, &one, 4); }
                        }
                    }
                }
                id<MTLCommandBuffer> cb = [queue commandBuffer];
                id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
                for (uint32_t l = 0; l < layer_count; ++l)
                {
                    [blit copyFromBuffer:staging
                             sourceOffset:l * layer_dst
                        sourceBytesPerRow:(NSUInteger)width * dst_bpp
                      sourceBytesPerImage:layer_dst
                               sourceSize:MTLSizeMake(width, height, 1)
                                toTexture:t->texture
                         destinationSlice:base_layer + l
                         destinationLevel:mip
                        destinationOrigin:MTLOriginMake(0, 0, 0)];
                }
                [blit endEncoding];
                [cb commit];
                [cb waitUntilCompleted];
                t->setCurrentLayout(TextureLayout::ShaderReadOnlyOptimal);
            }

            void submitImmediate(void (*record_fn)(RHICommandBuffer *, void *), void *user_data) override
            {
                MTLCommandBufferRHI cmd;
                cmd.device = this;
                cmd.begin(CommandBufferUsage::OneTimeSubmit);
                record_fn(&cmd, user_data);
                cmd.end();
                [cmd.cb commit];
                [cmd.cb waitUntilCompleted];
                cmd.cb = nil;
            }

            void submitAndWait(RHICommandBuffer *c) override
            {
                MTLCommandBufferRHI *cmd = static_cast<MTLCommandBufferRHI *>(c);
                if (!cmd || !cmd->cb) return;
                cmd->endEncoders();
                [cmd->cb commit];
                [cmd->cb waitUntilCompleted];
                if (cmd->cb.status == MTLCommandBufferStatusError)
                    GEN_LOG_ERROR("[Metal] command buffer failed: %s",
                                  cmd->cb.error ? cmd->cb.error.localizedDescription.UTF8String : "unknown");
                cmd->cb = nil;
            }
        };

        //==================================================================
        // Command buffer implementation
        //==================================================================
        void MTLCommandBufferRHI::begin(CommandBufferUsage)
        {
            endEncoders();
            cb = [device->queue commandBuffer];
            pipeline = nullptr;
            index_buffer = nullptr;
            framebuffer = nullptr;
            pass = nullptr;
            compute_bound = false;
        }

        void MTLCommandBufferRHI::end()
        {
            endEncoders();
        }

        void MTLCommandBufferRHI::beginRenderPass(RHIRenderPass *rp, RHIFramebuffer *rfb, const RenderPassBeginInfo &info)
        {
            MTLRenderPassRHI *p = static_cast<MTLRenderPassRHI *>(rp);
            MTLFramebufferRHI *fb = static_cast<MTLFramebufferRHI *>(rfb);
            if (!p || !fb || !cb) return;
            endEncoders();

            MTLRenderPassDescriptor *rpd = [MTLRenderPassDescriptor renderPassDescriptor];
            const RenderPassDesc &d = p->desc_;
            uint32_t color_index = 0;
            for (uint32_t i = 0; i < d.attachment_count; ++i)
            {
                const AttachmentDesc &ad = d.attachments[i];
                const RenderPassBeginInfo::ClearValue cv = i < info.clear_count ? info.clears[i] : RenderPassBeginInfo::ClearValue();
                id<MTLTexture> tex = nil;
                uint32_t mip = 0, layer = 0;
                if (fb->is_swapchain)
                {
                    if (!ad.is_depth_stencil) tex = device->drawable.texture;
                }
                else if (i < fb->attachments.size() && fb->attachments[i].texture)
                {
                    tex = fb->attachments[i].texture->texture;
                    mip = fb->attachments[i].mip;
                    layer = fb->attachments[i].layer;
                }
                if (!tex) continue;
                const MTLLoadAction load = ad.load_op == LoadOp::Clear ? MTLLoadActionClear
                                           : ad.load_op == LoadOp::Load ? MTLLoadActionLoad
                                                                        : MTLLoadActionDontCare;
                const MTLStoreAction store = ad.store_op == StoreOp::Store ? MTLStoreActionStore : MTLStoreActionDontCare;
                if (ad.is_depth_stencil)
                {
                    rpd.depthAttachment.texture = tex;
                    rpd.depthAttachment.level = mip;
                    rpd.depthAttachment.slice = layer;
                    rpd.depthAttachment.loadAction = load;
                    rpd.depthAttachment.storeAction = store;
                    rpd.depthAttachment.clearDepth = cv.depth;
                    if (formatHasStencil(tex.pixelFormat))
                    {
                        rpd.stencilAttachment.texture = tex;
                        rpd.stencilAttachment.level = mip;
                        rpd.stencilAttachment.slice = layer;
                        rpd.stencilAttachment.loadAction = ad.stencil_load_op == LoadOp::Load ? MTLLoadActionLoad
                                                           : ad.stencil_load_op == LoadOp::Clear ? MTLLoadActionClear
                                                                                                 : MTLLoadActionDontCare;
                        rpd.stencilAttachment.storeAction = ad.stencil_store_op == StoreOp::Store ? MTLStoreActionStore : MTLStoreActionDontCare;
                        rpd.stencilAttachment.clearStencil = cv.stencil;
                    }
                }
                else
                {
                    MTLRenderPassColorAttachmentDescriptor *ca = rpd.colorAttachments[color_index++];
                    ca.texture = tex;
                    ca.level = mip;
                    ca.slice = layer;
                    ca.loadAction = load;
                    ca.storeAction = store;
                    ca.clearColor = MTLClearColorMake(cv.color[0], cv.color[1], cv.color[2], cv.color[3]);
                }
            }
            render = [cb renderCommandEncoderWithDescriptor:rpd];
            if (!render)
            {
                GEN_LOG_ERROR("[Metal] failed to begin render pass '%s'", d.name.c_str());
                return;
            }
            framebuffer = fb;
            pass = p;
            MTLViewport vp{0.0, 0.0, (double)fb->w, (double)fb->h, 0.0, 1.0};
            [render setViewport:vp];
        }

        void MTLCommandBufferRHI::endRenderPass()
        {
            if (render)
            {
                [render endEncoding];
                render = nil;
            }
            if (framebuffer && pass)
            {
                for (uint32_t i = 0; i < pass->desc_.attachment_count && i < framebuffer->attachments.size(); ++i)
                    if (framebuffer->attachments[i].texture)
                        framebuffer->attachments[i].texture->setCurrentLayout(pass->desc_.attachments[i].final_layout);
            }
            framebuffer = nullptr;
            pass = nullptr;
            pipeline = nullptr;
        }

        void MTLCommandBufferRHI::bindPipeline(RHIPipeline *p)
        {
            pipeline = static_cast<MTLPipelineRHI *>(p);
            if (!pipeline) return;
            if (pipeline->compute_)
            {
                id<MTLComputeCommandEncoder> enc = computeEncoder();
                if (!enc) return;
                [enc setComputePipelineState:pipeline->compute_state];
                compute_bound = true;
                return;
            }
            if (!render) return;
            [render setRenderPipelineState:pipeline->state];
            [render setDepthStencilState:pipeline->depth_state];
            [render setCullMode:pipeline->cull];
            [render setFrontFacingWinding:pipeline->winding];
            [render setTriangleFillMode:pipeline->fill];
            [render setDepthClipMode:pipeline->clip];
            [render setStencilReferenceValue:pipeline->stencil_reference];
        }

        void MTLCommandBufferRHI::bindVertexBuffer(RHIBuffer *buffer, uint64_t offset)
        {
            MTLBufferRHI *b = static_cast<MTLBufferRHI *>(buffer);
            if (!render || !b) return;
            [render setVertexBuffer:b->buffer offset:offset atIndex:kMetalVertexBufferSlot];
        }

        void MTLCommandBufferRHI::bindIndexBuffer(RHIBuffer *buffer, IndexType type, uint64_t offset)
        {
            index_buffer = static_cast<MTLBufferRHI *>(buffer);
            index_type = type;
            index_offset = offset;
        }

        void MTLCommandBufferRHI::bindDescriptorSets(RHIPipeline *, RHIDescriptorSet *const *bound, uint32_t set_count,
                                                     const uint32_t *dynamic_offsets, uint32_t offset_count)
        {
            id<MTLComputeCommandEncoder> compute_enc = (compute && compute_bound) ? compute : nil;
            if (!render && !compute_enc) return;
            uint32_t dyn = 0;
            for (uint32_t s = 0; s < set_count; ++s)
            {
                MTLDescriptorSetRHI *set = static_cast<MTLDescriptorSetRHI *>(bound[s]);
                if (!set) continue;
                for (const auto &kv : set->entries)
                {
                    const uint32_t slot = set->set_index * 8 + kv.first;
                    const MTLDescriptorSetRHI::Entry &e = kv.second;
                    if (resourceTypeIsBuffer(e.type))
                    {
                        uint64_t offset = e.offset;
                        if ((e.type == ResourceType::UniformBufferDynamic || e.type == ResourceType::StorageBufferDynamic) &&
                            dynamic_offsets && dyn < offset_count)
                            offset += dynamic_offsets[dyn++];
                        if (!e.buffer) continue;
                        if (compute_enc) [compute_enc setBuffer:e.buffer->buffer offset:offset atIndex:slot];
                        if (render) [render setVertexBuffer:e.buffer->buffer offset:offset atIndex:slot];
                        if (render) [render setFragmentBuffer:e.buffer->buffer offset:offset atIndex:slot];
                    }
                    else if (e.texture)
                    {
                        if (compute_enc) [compute_enc setTexture:e.texture->texture atIndex:slot];
                        if (render) [render setVertexTexture:e.texture->texture atIndex:slot];
                        if (render) [render setFragmentTexture:e.texture->texture atIndex:slot];
                        if (e.sampler && slot < 16)
                        {
                            if (compute_enc) [compute_enc setSamplerState:e.sampler->sampler atIndex:slot];
                            if (render) [render setVertexSamplerState:e.sampler->sampler atIndex:slot];
                            if (render) [render setFragmentSamplerState:e.sampler->sampler atIndex:slot];
                        }
                    }
                }
            }
        }

        void MTLCommandBufferRHI::setPushConstants(const void *data, uint32_t size)
        {
            if (!data || !size) return;
            if (compute && compute_bound) [compute setBytes:data length:size atIndex:kMetalPushConstantSlot];
            if (render)
            {
                [render setVertexBytes:data length:size atIndex:kMetalPushConstantSlot];
                [render setFragmentBytes:data length:size atIndex:kMetalPushConstantSlot];
            }
        }

        void MTLCommandBufferRHI::setViewport(const Viewport &vp)
        {
            if (!render) return;
            MTLViewport v{vp.x, vp.y, vp.width, vp.height, vp.min_depth, vp.max_depth};
            [render setViewport:v];
        }

        void MTLCommandBufferRHI::setScissor(const Rect2D &rect)
        {
            if (!render || !framebuffer) return;
            // Metal rejects scissor rectangles outside the attachment.
            int32_t x0 = std::max(0, rect.x), y0 = std::max(0, rect.y);
            int32_t x1 = std::min((int32_t)framebuffer->w, rect.x + (int32_t)rect.width);
            int32_t y1 = std::min((int32_t)framebuffer->h, rect.y + (int32_t)rect.height);
            MTLScissorRect r{(NSUInteger)x0, (NSUInteger)y0, (NSUInteger)std::max(0, x1 - x0), (NSUInteger)std::max(0, y1 - y0)};
            [render setScissorRect:r];
        }

        void MTLCommandBufferRHI::draw(uint32_t vertex_count, uint32_t first_vertex, uint32_t instance_count, uint32_t first_instance)
        {
            if (!render || !pipeline || !vertex_count) return;
            [render drawPrimitives:pipeline->primitive
                       vertexStart:first_vertex
                       vertexCount:vertex_count
                     instanceCount:std::max(1u, instance_count)
                      baseInstance:first_instance];
        }

        void MTLCommandBufferRHI::drawIndexed(uint32_t index_count, uint32_t first_index, int32_t vertex_offset,
                                              uint32_t instance_count, uint32_t first_instance)
        {
            if (!render || !pipeline || !index_buffer || !index_count) return;
            const uint32_t stride = index_type == IndexType::UInt32 ? 4 : 2;
            [render drawIndexedPrimitives:pipeline->primitive
                               indexCount:index_count
                                indexType:index_type == IndexType::UInt32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16
                              indexBuffer:index_buffer->buffer
                        indexBufferOffset:index_offset + (uint64_t)first_index * stride
                            instanceCount:std::max(1u, instance_count)
                               baseVertex:vertex_offset
                             baseInstance:first_instance];
        }

        void MTLCommandBufferRHI::dispatch(uint32_t group_x, uint32_t group_y, uint32_t group_z)
        {
            if (!compute || !compute_bound || !pipeline || !pipeline->compute_state) return;
            const uint32_t wx = std::max(1u, pipeline->desc_.compute_workgroup_x);
            const uint32_t wy = std::max(1u, pipeline->desc_.compute_workgroup_y);
            const uint32_t wz = std::max(1u, pipeline->desc_.compute_workgroup_z);
            [compute dispatchThreadgroups:MTLSizeMake(group_x, group_y, group_z)
                    threadsPerThreadgroup:MTLSizeMake(wx, wy, wz)];
        }

        void MTLCommandBufferRHI::copyBufferToBuffer(RHIBuffer *src, uint64_t src_offset, RHIBuffer *dst, uint64_t dst_offset, uint64_t size)
        {
            MTLBufferRHI *s = static_cast<MTLBufferRHI *>(src);
            MTLBufferRHI *d = static_cast<MTLBufferRHI *>(dst);
            if (!s || !d || !cb) return;
            [blitEncoder() copyFromBuffer:s->buffer sourceOffset:src_offset toBuffer:d->buffer destinationOffset:dst_offset size:size];
        }

        void MTLCommandBufferRHI::copyBufferToImage(RHIBuffer *src, RHITexture *dst, const BufferImageCopy &r)
        {
            MTLBufferRHI *s = static_cast<MTLBufferRHI *>(src);
            MTLTextureRHI *t = static_cast<MTLTextureRHI *>(dst);
            if (!s || !t || !cb) return;
            const uint32_t bpp = mtlBytesPerPixel(t->format());
            const uint32_t row_px = r.buffer_row_length ? r.buffer_row_length : r.width;
            const uint32_t rows = r.buffer_image_height ? r.buffer_image_height : r.height;
            id<MTLBlitCommandEncoder> b = blitEncoder();
            for (uint32_t l = 0; l < std::max(1u, r.layer_count); ++l)
            {
                [b copyFromBuffer:s->buffer
                         sourceOffset:r.buffer_offset + (uint64_t)l * row_px * rows * bpp
                    sourceBytesPerRow:(NSUInteger)row_px * bpp
                  sourceBytesPerImage:(NSUInteger)row_px * rows * bpp
                           sourceSize:MTLSizeMake(r.width, r.height, std::max(1u, r.depth))
                            toTexture:t->texture
                     destinationSlice:r.base_layer + l
                     destinationLevel:r.mip_level
                    destinationOrigin:MTLOriginMake(r.offset_x, r.offset_y, r.offset_z)];
            }
        }

        void MTLCommandBufferRHI::copyImageToBuffer(RHITexture *src, RHIBuffer *dst, const BufferImageCopy &r)
        {
            MTLTextureRHI *t = static_cast<MTLTextureRHI *>(src);
            MTLBufferRHI *d = static_cast<MTLBufferRHI *>(dst);
            if (!t || !d || !cb) return;
            const uint32_t bpp = mtlBytesPerPixel(t->format());
            const uint32_t row_px = r.buffer_row_length ? r.buffer_row_length : r.width;
            const uint32_t rows = r.buffer_image_height ? r.buffer_image_height : r.height;
            id<MTLBlitCommandEncoder> b = blitEncoder();
            for (uint32_t l = 0; l < std::max(1u, r.layer_count); ++l)
            {
                [b copyFromTexture:t->texture
                                 sourceSlice:r.base_layer + l
                                 sourceLevel:r.mip_level
                                sourceOrigin:MTLOriginMake(r.offset_x, r.offset_y, r.offset_z)
                                  sourceSize:MTLSizeMake(r.width, r.height, std::max(1u, r.depth))
                                    toBuffer:d->buffer
                           destinationOffset:r.buffer_offset + (uint64_t)l * row_px * rows * bpp
                      destinationBytesPerRow:(NSUInteger)row_px * bpp
                    destinationBytesPerImage:(NSUInteger)row_px * rows * bpp];
            }
        }

        void MTLCommandBufferRHI::generateMipmaps(RHITexture *texture)
        {
            MTLTextureRHI *t = static_cast<MTLTextureRHI *>(texture);
            if (!t || !cb || t->mipLevels() < 2) return;
            [blitEncoder() generateMipmapsForTexture:t->texture];
            t->setCurrentLayout(TextureLayout::ShaderReadOnlyOptimal);
        }

        void MTLCommandBufferRHI::blitImage(RHITexture *src, uint32_t src_mip, RHITexture *dst, uint32_t dst_mip,
                                            uint32_t src_layer, uint32_t dst_layer, uint32_t layer_count, bool)
        {
            MTLTextureRHI *s = static_cast<MTLTextureRHI *>(src);
            MTLTextureRHI *d = static_cast<MTLTextureRHI *>(dst);
            if (!s || !d || !cb) return;
            const uint32_t sw = std::max(1u, s->width() >> src_mip), sh = std::max(1u, s->height() >> src_mip);
            const uint32_t dw = std::max(1u, d->width() >> dst_mip), dh = std::max(1u, d->height() >> dst_mip);
            if (sw != dw || sh != dh)
            {
                GEN_LOG_WARN("[Metal] blitImage only supports equal sizes (%ux%u -> %ux%u); use a render pass to scale",
                             sw, sh, dw, dh);
                return;
            }
            id<MTLBlitCommandEncoder> b = blitEncoder();
            for (uint32_t l = 0; l < layer_count; ++l)
            {
                [b copyFromTexture:s->texture
                          sourceSlice:src_layer + l
                          sourceLevel:src_mip
                         sourceOrigin:MTLOriginMake(0, 0, 0)
                           sourceSize:MTLSizeMake(sw, sh, 1)
                            toTexture:d->texture
                     destinationSlice:dst_layer + l
                     destinationLevel:dst_mip
                    destinationOrigin:MTLOriginMake(0, 0, 0)];
            }
        }

        bool MTLCommandBufferRHI::readImagePixels(RHITexture *src, uint32_t mip, uint32_t layer, void *dst, uint32_t dst_size_bytes)
        {
            MTLTextureRHI *t = static_cast<MTLTextureRHI *>(src);
            if (!t || !dst) return false;
            if (render)
            {
                GEN_LOG_ERROR("[Metal] readImagePixels called inside a render pass");
                return false;
            }
            const uint32_t w = std::max(1u, t->width() >> mip), h = std::max(1u, t->height() >> mip);
            const uint32_t bpp = pixelFormatBytesPerPixel(t->format());
            const uint32_t mtl_bpp = mtlBytesPerPixel(t->format());
            if (dst_size_bytes < w * h * bpp)
            {
                GEN_LOG_ERROR("[Metal] readImagePixels: destination too small");
                return false;
            }
            id<MTLBuffer> staging = [device->device newBufferWithLength:(NSUInteger)w * h * mtl_bpp options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> once = [device->queue commandBuffer];
            id<MTLBlitCommandEncoder> b = [once blitCommandEncoder];
            [b copyFromTexture:t->texture
                             sourceSlice:layer
                             sourceLevel:mip
                            sourceOrigin:MTLOriginMake(0, 0, 0)
                              sourceSize:MTLSizeMake(w, h, 1)
                                toBuffer:staging
                       destinationOffset:0
                  destinationBytesPerRow:(NSUInteger)w * mtl_bpp
                destinationBytesPerImage:(NSUInteger)w * h * mtl_bpp];
            [b endEncoding];
            [once commit];
            [once waitUntilCompleted];
            if (bpp == mtl_bpp)
            {
                std::memcpy(dst, staging.contents, (size_t)w * h * bpp);
            }
            else
            {
                const uint8_t *s = (const uint8_t *)staging.contents;
                uint8_t *d = static_cast<uint8_t *>(dst);
                for (size_t px = 0; px < (size_t)w * h; ++px) std::memcpy(d + px * bpp, s + px * mtl_bpp, bpp);
            }
            return true;
        }

        RHIDevice *createMetalDevice() { return new MTLDeviceRHI(); }

    } // namespace RHI
} // namespace Genesis
