/*
 * Genesis Render - Engine
 * rhi.hpp : Render Hardware Interface. A single abstract GPU device implemented
 *           by rhi/opengl (gl_rhi) and rhi/vulkan (vk_rhi).
 *
 * Frame structure:
 *   beginFrame()                       // acquire swapchain image / make current
 *     beginPass(scene targets...)     // offscreen passes (IBL baking, scene RT)
 *     draw...
 *     endPass()
 *     beginSwapchainPass()             // final pass: blit scene + ImGui
 *     draw fullscreen scene / imgui
 *     endPass()
 *   endFrame()                         // submit + present
 */
#pragma once

#include "rhi_types.hpp"

struct SDL_Window;
struct ImDrawData; // defined by Dear ImGui, only used through an opaque pointer

namespace Genesis
{
    class IRHIDevice
    {
    public:
        virtual ~IRHIDevice() = default;

        static IRHIDevice *create(RenderBackend backend);

        virtual bool init(SDL_Window *window, uint32_t width, uint32_t height, bool enable_debug = false) = 0;
        virtual void shutdown() = 0;
        virtual void onResize(uint32_t width, uint32_t height) = 0;
        virtual const DeviceCaps &caps() const = 0;
        virtual Format swapchainFormat() const = 0;

        // --------------------------------------------------------- frame
        virtual bool beginFrame() = 0; // false => skip this frame (resize/minimized)
        virtual uint32_t currentFrameIndex() const = 0; // 0..frames-in-flight-1
        // scene offscreen targets (created by RHI so it can recreate on resize)
        virtual TextureHandle sceneColor() const = 0;
        virtual TextureHandle sceneDepth() const = 0;
        virtual SamplerHandle linearSampler() const = 0;
        virtual SamplerHandle nearestSampler() const = 0;
        virtual SamplerHandle cubeMipSampler() const = 0;

        // render passes (offscreen / final swapchain)
        virtual void beginPass(uint32_t colorCount, ColorAttachment *colors, const DepthAttachment *depth) = 0;
        virtual void endPass() = 0;
        virtual void beginSwapchainPass(const float clearColor[4], float clearDepth) = 0;
        virtual void endSwapchainPass() = 0;
        virtual void endFrame() = 0;

        // --------------------------------------------------------- resources
        virtual BufferHandle createBuffer(const BufferDesc &desc, const void *initial_data = nullptr) = 0;
        virtual void destroyBuffer(BufferHandle) = 0;
        virtual void writeBuffer(BufferHandle, size_t offset, const void *data, size_t size) = 0;
        virtual void *mapBuffer(BufferHandle) = 0;
        virtual void unmapBuffer(BufferHandle) = 0;

        // initial_data: tightly packed pixels; for cubes pointer to 6 faces;
        // mip 0 only (generateMipmaps builds the rest when needed).
        virtual TextureHandle createTexture(const TextureDesc &desc, const void *initial_data = nullptr) = 0;
        virtual void destroyTexture(TextureHandle) = 0;
        virtual void uploadTexture2D(TextureHandle, uint32_t level, uint32_t x, uint32_t y,
                                     uint32_t w, uint32_t h, const void *data) = 0;
        virtual void uploadCubeFace(TextureHandle, uint32_t face, uint32_t level,
                                    uint32_t w, uint32_t h, const void *data) = 0;
        virtual void generateMipmaps(TextureHandle) = 0;

        virtual SamplerHandle createSampler(const SamplerDesc &desc) = 0;
        virtual void destroySampler(SamplerHandle) = 0;

        virtual ShaderHandle createShaderProgram(const ShaderProgramDesc &desc) = 0;
        virtual void destroyShaderProgram(ShaderHandle) = 0;

        virtual PipelineHandle createPipeline(const PipelineDesc &desc) = 0;
        virtual void destroyPipeline(PipelineHandle) = 0;

        virtual DescriptorSetHandle createDescriptorSet(const DescriptorSetDesc &desc) = 0;
        virtual void updateDescriptorSet(DescriptorSetHandle, const DescriptorSetDesc &desc) = 0;
        virtual void destroyDescriptorSet(DescriptorSetHandle) = 0;

        // --------------------------------------------------------- commands
        virtual void bindPipeline(PipelineHandle) = 0;
        virtual void bindDescriptorSet(uint32_t set_index, DescriptorSetHandle) = 0;
        virtual void pushConstants(const void *data, uint32_t size) = 0;
        virtual void bindVertexBuffer(BufferHandle, size_t offset = 0, uint32_t binding = 0) = 0;
        virtual void bindIndexBuffer(BufferHandle, IndexType type, size_t offset = 0) = 0;
        virtual void setViewport(uint32_t w, uint32_t h) = 0;
        virtual void draw(uint32_t vertex_count, uint32_t instance_count = 1,
                          uint32_t first_vertex = 0) = 0;
        virtual void drawIndexed(uint32_t index_count, uint32_t instance_count = 1,
                                 uint32_t first_index = 0, int32_t vertex_offset = 0) = 0;
        // fullscreen triangle helper (no vertex buffer)
        virtual void drawFullscreen() = 0;

        // read back a texture as RGBA8 (offscreen scene RT, IBL assets...)
        virtual void readTexturePixels(TextureHandle, uint32_t w, uint32_t h, void *out_rgba8) = 0;

        // --------------------------------------------------------- imgui
        virtual void imguiBackendInit() = 0;
        virtual void imguiBackendBeginFrame() = 0;
        virtual void imguiBackendRenderDrawData(::ImDrawData *draw_data) = 0;
        virtual void imguiBackendShutdown() = 0;
        // returns the ImTextureID usable with ImGui::Image for a given texture
        virtual void *imguiTextureID(TextureHandle, SamplerHandle) = 0;
    };
}
