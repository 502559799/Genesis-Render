/*
 * @Descripttion: OpenGL (core profile) 后端渲染设备
 *               与 VulkanDevice 实现同一 RHIDevice 接口
 * @version: v1.0
 */
#ifndef __GENESIS_OPENGL_DEVICE_H__
#define __GENESIS_OPENGL_DEVICE_H__

#include "rhi_layer/rhi_device.hpp"
#include "rhi_layer/resource_pool.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace Genesis
{
    namespace rhi
    {
        struct GLBufferRes
        {
            uint32_t m_id = 0;
            uint64_t m_size = 0;
            uint32_t m_usage = 0;
        };

        struct GLTextureRes
        {
            uint32_t m_id = 0;
            TextureInfo m_info{};
            bool m_render_target = false;
        };

        struct GLSamplerRes
        {
            SamplerCreateInfo m_info{};
        };

        struct GLShaderRes
        {
            uint32_t m_id = 0;
            ShaderStage m_stage = ShaderStage::Vertex;
        };

        struct GLSetLayoutRes
        {
            DescriptorSetLayoutCreateInfo m_info{};
        };

        // 解析后的 uniform 绑定 (按名字查找结果)
        struct GLUniformBinding
        {
            uint32_t m_slot = 0;
            uint32_t m_binding = 0;
            DescriptorType m_type;
            int32_t m_block_index = -1;  // uniform buffer
            int32_t m_sampler_location = -1; // sampler
            uint32_t m_texture_target = 0;
        };

        struct GLPipelineRes
        {
            uint32_t m_program = 0;
            PipelineCreateInfo m_info{};
            std::vector<GLUniformBinding> m_uniform_bindings;
        };

        struct GLSetWriteRes
        {
            uint32_t m_binding = 0;
            DescriptorType m_type;
            BufferHandle m_buffer;
            uint64_t m_buffer_offset = 0;
            uint64_t m_buffer_size = 0;
            TextureHandle m_texture;
            SamplerHandle m_sampler;
        };

        struct GLSetRes
        {
            DescriptorSetLayoutHandle m_layout;
            std::vector<GLSetWriteRes> m_writes;
        };

        struct GLRenderTargetRes
        {
            uint32_t m_fbo = 0;
            uint32_t m_width = 0;
            uint32_t m_height = 0;
            RenderTargetLayout m_layout{};
        };

        class OpenGLDevice : public RHIDevice
        {
        public:
            OpenGLDevice() = default;
            ~OpenGLDevice() override = default;

            bool init(const DeviceCreateInfo &info) override;
            void shutdown() override;

            Backend getBackend() const override { return Backend::OpenGL; }
            uint32_t getWidth() const override { return m_width; }
            uint32_t getHeight() const override { return m_height; }
            RenderTargetLayout getSwapchainLayout() const override
            {
                RenderTargetLayout layout{};
                layout.m_color_count = 1;
                layout.m_color_formats[0] = Format::BGRA8_UNorm;
                layout.m_depth_format = Format::D32_Float;
                return layout;
            }

            BufferHandle createBuffer(const BufferCreateInfo &info) override;
            void destroyBuffer(BufferHandle handle) override;
            void updateBuffer(BufferHandle handle, const void *data, uint64_t size, uint64_t offset) override;

            TextureHandle createTexture(const TextureCreateInfo &info) override;
            void destroyTexture(TextureHandle handle) override;
            void updateTexture(TextureHandle handle, const void *data, uint64_t size, uint32_t mipLevel, uint32_t layer) override;
            void generateMipmaps(TextureHandle handle) override;
            TextureInfo getTextureInfo(TextureHandle handle) const override;

            SamplerHandle createSampler(const SamplerCreateInfo &info) override;
            void destroySampler(SamplerHandle handle) override;

            ShaderHandle createShader(const ShaderCreateInfo &info) override;
            void destroyShader(ShaderHandle handle) override;

            PipelineHandle createPipeline(const PipelineCreateInfo &info) override;
            void destroyPipeline(PipelineHandle handle) override;

            DescriptorSetLayoutHandle createDescriptorSetLayout(const DescriptorSetLayoutCreateInfo &info) override;
            void destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) override;

            DescriptorSetHandle createDescriptorSet(const DescriptorSetCreateInfo &info) override;
            void destroyDescriptorSet(DescriptorSetHandle handle) override;

            RenderTargetHandle createRenderTarget(const RenderTargetCreateInfo &info) override;
            void destroyRenderTarget(RenderTargetHandle handle) override;

            void beginFrame() override;
            void endFrame() override;
            void beginCommands() override;
            void endCommands() override;

            void beginRenderPass(RenderTargetHandle target, const float clearColor[4], float clearDepth, bool clear) override;
            void endRenderPass() override;

            void bindPipeline(PipelineHandle pipeline) override;
            void bindDescriptorSet(uint32_t slot, DescriptorSetHandle set) override;
            void bindVertexBuffer(BufferHandle buffer, uint64_t offset) override;
            void bindIndexBuffer(BufferHandle buffer, uint64_t offset) override;
            void draw(uint32_t vertexCount, uint32_t firstVertex) override;
            void drawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t vertexOffset) override;

            bool captureScreen(const char *pngPath) override;
            bool readbackTexture(TextureHandle handle, uint8_t *outRGBA, uint32_t rowPitch) override;
            void waitIdle() override;
            void requestScreenCapture(const char *pngPath) override;

        private:
            void applySamplerToTexture(uint32_t texture_target, const SamplerCreateInfo &info);
            uint32_t getOrCreateVAO(uint32_t pipeline_idx);
            void doCapture();

        private:
            void *m_window = nullptr;
            void *m_gl_context = nullptr;
            uint32_t m_width = 0;
            uint32_t m_height = 0;

            bool m_in_pass = false;

            std::string m_capture_path;
            bool m_capture_pending = false;

            ResourcePool<GLBufferRes> m_buffers;
            ResourcePool<GLTextureRes> m_textures;
            ResourcePool<GLSamplerRes> m_samplers;
            ResourcePool<GLShaderRes> m_shaders;
            ResourcePool<GLPipelineRes> m_pipelines;
            ResourcePool<GLSetLayoutRes> m_set_layouts;
            ResourcePool<GLSetRes> m_sets;
            ResourcePool<GLRenderTargetRes> m_render_targets;

            uint32_t m_current_pipeline = 0;
            BufferHandle m_current_vbo;
            BufferHandle m_current_ibo;
            std::unordered_map<uint64_t, uint32_t> m_vao_cache;
        };

    } // namespace rhi
} // namespace Genesis

#endif // __GENESIS_OPENGL_DEVICE_H__
