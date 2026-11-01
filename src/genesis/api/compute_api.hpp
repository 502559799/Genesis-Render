/*
 * Genesis Render - Compute API
 *
 * A small cross-backend facade over the RHI compute primitives:
 *
 *   - storage buffers (read/write)
 *   - compute kernels loaded from the baked shader registry
 *     (GLSL / SPIR-V / MSL selected automatically)
 *   - descriptor binding, dispatch and buffer barriers
 *   - one-shot execution with optional readback
 *
 * The same API is used by the vertex animation system and by the portable ray
 * tracer, so both exercise the compute path of every backend.
 */
#ifndef __GENESIS_COMPUTE_API_HPP__
#define __GENESIS_COMPUTE_API_HPP__

#include "rhi/rhi.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Genesis
{
    namespace API
    {
        class ComputeAPI
        {
        public:
            struct Buffer
            {
                RHI::RHIBuffer *buffer = nullptr;
                uint64_t size = 0;
                RHI::MemoryUsage memory_usage = RHI::MemoryUsage::GpuOnly;
                bool valid() const { return buffer != nullptr; }
            };

            struct Binding
            {
                uint32_t binding = 0;
                std::string name; // GL storage-block name (ignored by VK/Metal)
                bool read_only = true;
            };

            struct KernelDesc
            {
                std::string shader; // e.g. "anim/vertex_anim.comp"
                uint32_t workgroup_x = 64;
                uint32_t workgroup_y = 1;
                uint32_t workgroup_z = 1;
                std::vector<Binding> bindings;
            };

            class Kernel
            {
            public:
                Kernel() = default;
                ~Kernel();

                bool valid() const { return pipeline_ != nullptr && set_ != nullptr; }
                const std::string &name() const { return desc_.shader; }

            private:
                friend class ComputeAPI;
                ComputeAPI *owner_ = nullptr;
                KernelDesc desc_;
                RHI::RHIPipeline *pipeline_ = nullptr;
                RHI::RHIResourceLayout *layout_ = nullptr;
                RHI::RHIDescriptorSet *set_ = nullptr;
                std::vector<uint32_t> binding_slots_;
                std::vector<Buffer *> bound_buffers_;
                bool bound_ = false;
            };

            ComputeAPI() = default;
            ~ComputeAPI();

            bool initialize(RHI::RHIDevice *device);
            void shutdown();
            bool available() const { return device_ != nullptr; }
            bool supportsHardwareCompute() const;

            Buffer createBuffer(uint64_t size, RHI::BufferUsageFlags usage, RHI::MemoryUsage memory,
                                const void *initial_data = nullptr, uint64_t initial_size = 0);
            void destroyBuffer(Buffer &buffer);
            bool write(const Buffer &buffer, const void *data, uint64_t size, uint64_t offset = 0);
            bool read(const Buffer &buffer, void *out, uint64_t size, uint64_t offset = 0);

            std::unique_ptr<Kernel> createKernel(const KernelDesc &desc, std::string *error = nullptr);
            bool bind(Kernel &kernel, const std::vector<Buffer *> &buffers, std::string *error = nullptr);

            // Records a dispatch into an existing command buffer (frame loop).
            void dispatch(RHI::RHICommandBuffer *cmd, Kernel &kernel, uint32_t groups_x, uint32_t groups_y = 1,
                          uint32_t groups_z = 1, Buffer *written = nullptr,
                          RHI::BufferAccess written_then = RHI::BufferAccess::VertexAttributeRead);
            // Submits a one-shot dispatch and waits for completion.
            bool execute(Kernel &kernel, uint32_t groups_x, uint32_t groups_y = 1, uint32_t groups_z = 1,
                         Buffer *written = nullptr,
                         RHI::BufferAccess written_then = RHI::BufferAccess::HostRead);

            RHI::RHIDevice *device() const { return device_; }

        private:
            RHI::RHIDevice *device_ = nullptr;
        };
    } // namespace API
} // namespace Genesis

#endif /* __GENESIS_COMPUTE_API_HPP__ */
