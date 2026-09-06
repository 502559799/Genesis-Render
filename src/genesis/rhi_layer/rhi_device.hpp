/*
 * @Descripttion: 渲染设备抽象接口 (RHIDevice)
 *               Genesis 多后端渲染的统一入口, 对齐 Piccolo RHI 层职责,
 *               但接口层面完全不泄漏后端原生类型 (对比 Piccolo rhi.h 中的
 *               VmaAllocator/GLFW 泄漏)
 * @version: v1.0
 */
#ifndef __GENESIS_RHI_DEVICE_H__
#define __GENESIS_RHI_DEVICE_H__

#include "rhi_layer/rhi_types.hpp"

namespace Genesis
{
    namespace rhi
    {
        // 帧内 / 一次性命令录制语义说明:
        //   beginFrame()  ~ endFrame()      : 交换链帧 (acquire -> record -> submit -> present)
        //   beginCommands() ~ endCommands() : 帧外一次性 GPU 工作 (资源上传 / IBL 烘焙), endCommands 阻塞等待完成
        //   渲染通道只能在上述两种录制区间内开启
        class RHIDevice
        {
        public:
            virtual ~RHIDevice() = default;

            virtual bool init(const DeviceCreateInfo &info) = 0;
            virtual void shutdown() = 0;

            virtual Backend getBackend() const = 0;
            virtual uint32_t getWidth() const = 0;
            virtual uint32_t getHeight() const = 0;
            // 交换链 pass 的布局 (颜色格式 + 深度格式)
            virtual RenderTargetLayout getSwapchainLayout() const = 0;

            //--------------------[Resources]--------------------//
            virtual BufferHandle createBuffer(const BufferCreateInfo &info) = 0;
            virtual void destroyBuffer(BufferHandle handle) = 0;
            virtual void updateBuffer(BufferHandle handle, const void *data, uint64_t size, uint64_t offset = 0) = 0;

            virtual TextureHandle createTexture(const TextureCreateInfo &info) = 0;
            virtual void destroyTexture(TextureHandle handle) = 0;
            virtual void updateTexture(TextureHandle handle, const void *data, uint64_t size, uint32_t mipLevel = 0, uint32_t layer = 0) = 0;
            virtual void generateMipmaps(TextureHandle handle) = 0;
            virtual TextureInfo getTextureInfo(TextureHandle handle) const = 0;

            virtual SamplerHandle createSampler(const SamplerCreateInfo &info) = 0;
            virtual void destroySampler(SamplerHandle handle) = 0;

            virtual ShaderHandle createShader(const ShaderCreateInfo &info) = 0;
            virtual void destroyShader(ShaderHandle handle) = 0;

            virtual PipelineHandle createPipeline(const PipelineCreateInfo &info) = 0;
            virtual void destroyPipeline(PipelineHandle handle) = 0;

            virtual DescriptorSetLayoutHandle createDescriptorSetLayout(const DescriptorSetLayoutCreateInfo &info) = 0;
            virtual void destroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) = 0;

            virtual DescriptorSetHandle createDescriptorSet(const DescriptorSetCreateInfo &info) = 0;
            virtual void destroyDescriptorSet(DescriptorSetHandle handle) = 0;

            virtual RenderTargetHandle createRenderTarget(const RenderTargetCreateInfo &info) = 0;
            virtual void destroyRenderTarget(RenderTargetHandle handle) = 0;

            //--------------------[Frame & Commands]--------------------//
            virtual void beginFrame() = 0;
            virtual void endFrame() = 0;

            virtual void beginCommands() = 0;
            virtual void endCommands() = 0; // 阻塞直至 GPU 完成

            //--------------------[Render Pass]--------------------//
            // target 为 invalid handle 时渲染到交换链
            virtual void beginRenderPass(RenderTargetHandle target, const float clearColor[4], float clearDepth, bool clear) = 0;
            virtual void endRenderPass() = 0;

            virtual void bindPipeline(PipelineHandle pipeline) = 0;
            virtual void bindDescriptorSet(uint32_t slot, DescriptorSetHandle set) = 0;
            virtual void bindVertexBuffer(BufferHandle buffer, uint64_t offset = 0) = 0;
            virtual void bindIndexBuffer(BufferHandle buffer, uint64_t offset = 0) = 0;
            virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
            virtual void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t vertexOffset = 0) = 0;

            //--------------------[Capture]--------------------//
            // 在 endFrame 之后调用: 把最近一次呈现的交换链内容读出为 RGBA8 并写 png
            // (OpenGL 后端实现为在交换缓冲前读取, 见各自实现)
            virtual bool captureScreen(const char *pngPath) = 0;

            // 读取纹理 (mip 0, layer 0) 内容, RGBA8 输出, 用于自动化功能验证
            virtual bool readbackTexture(TextureHandle handle, uint8_t *outRGBA, uint32_t rowPitch) = 0;

            virtual void waitIdle() = 0;

            // 标记: 当前帧是否需要截图 (OpenGL 必须在 swap 前读)
            virtual void requestScreenCapture(const char *pngPath) = 0;
        };

        // 工厂: 按后端创建渲染设备
        RHIDevice *createRHIDevice(Backend backend);
        void destroyRHIDevice(RHIDevice *device);

    } // namespace rhi
} // namespace Genesis

#endif // __GENESIS_RHI_DEVICE_H__
