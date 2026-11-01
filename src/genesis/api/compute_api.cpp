/*
 * Genesis Render - Compute API implementation
 */
#include "api/compute_api.hpp"

#include "foundation_layer/genesis_log.hpp"
#include "genesis_shaders_generated.h"

#include <cstring>

namespace Genesis
{
    namespace API
    {
        ComputeAPI::Kernel::~Kernel()
        {
            if (!owner_ || !owner_->device_) return;
            if (set_) owner_->device_->destroyDescriptorSet(set_);
            if (pipeline_) owner_->device_->destroyPipeline(pipeline_);
            if (layout_) owner_->device_->destroyResourceLayout(layout_);
            set_ = nullptr;
            pipeline_ = nullptr;
            layout_ = nullptr;
        }

        ComputeAPI::~ComputeAPI()
        {
            shutdown();
        }

        bool ComputeAPI::initialize(RHI::RHIDevice *device)
        {
            device_ = device;
            return device_ != nullptr;
        }

        void ComputeAPI::shutdown()
        {
            device_ = nullptr;
        }

        bool ComputeAPI::supportsHardwareCompute() const
        {
            return device_ && device_->info().supports_compute;
        }

        ComputeAPI::Buffer ComputeAPI::createBuffer(uint64_t size, RHI::BufferUsageFlags usage,
                                                     RHI::MemoryUsage memory, const void *initial_data,
                                                     uint64_t initial_size)
        {
            Buffer out;
            if (!device_ || size == 0) return out;
            RHI::BufferDesc desc;
            desc.size = size;
            desc.usage = usage | (uint32_t)RHI::BufferUsageFlagBits::Storage;
            desc.memory_usage = memory;
            desc.initial_data = initial_data;
            desc.initial_data_size = initial_data ? initial_size : 0;
            desc.name = "compute.buffer";
            out.buffer = device_->createBuffer(desc);
            out.size = out.buffer ? size : 0;
            out.memory_usage = memory;
            return out;
        }

        void ComputeAPI::destroyBuffer(Buffer &buffer)
        {
            if (device_ && buffer.buffer) device_->destroyBuffer(buffer.buffer);
            buffer = Buffer{};
        }

        bool ComputeAPI::write(const Buffer &buffer, const void *data, uint64_t size, uint64_t offset)
        {
            if (!device_ || !buffer.buffer || !data || size == 0) return false;
            if (offset + size > buffer.size) return false;
            device_->writeBuffer(buffer.buffer, data, size, offset);
            return true;
        }

        bool ComputeAPI::read(const Buffer &buffer, void *out, uint64_t size, uint64_t offset)
        {
            if (!device_ || !buffer.buffer || !out || size == 0) return false;
            if (offset + size > buffer.size) return false;
            void *mapped = buffer.buffer->map(offset, size);
            if (!mapped) return false;
            std::memcpy(out, mapped, (size_t)size);
            buffer.buffer->unmap();
            return true;
        }

        std::unique_ptr<ComputeAPI::Kernel> ComputeAPI::createKernel(const KernelDesc &desc, std::string *error)
        {
            if (!device_)
            {
                if (error) *error = "compute device is not initialised";
                return nullptr;
            }
            if (!device_->info().supports_compute)
            {
                if (error) *error = "the current backend has no compute support";
                return nullptr;
            }
            const Shaders::ShaderBinary *binary = Shaders::findShader(desc.shader.c_str());
            if (!binary)
            {
                if (error) *error = "shader '" + desc.shader + "' is not baked";
                return nullptr;
            }

            RHI::ShaderStageDesc stage;
            stage.stage = RHI::ShaderStageFlagBits::Compute;
            stage.name = desc.shader;
            stage.entry = "main";
            const RHI::BackendType backend = device_->backend();
            if (backend == RHI::BackendType::Vulkan)
            {
                if (!binary->spirv || !binary->spirv_words)
                {
                    if (error) *error = "shader '" + desc.shader + "' has no SPIR-V variant";
                    return nullptr;
                }
                stage.code_type = RHI::ShaderCodeType::Spirv;
                stage.code = binary->spirv;
                stage.code_size = binary->spirv_words * sizeof(uint32_t);
            }
            else if (backend == RHI::BackendType::Metal)
            {
                if (!binary->msl || !binary->msl_size)
                {
                    if (error) *error = "shader '" + desc.shader + "' has no MSL variant";
                    return nullptr;
                }
                stage.code_type = RHI::ShaderCodeType::MslSource;
                stage.code = binary->msl;
                stage.code_size = binary->msl_size;
                stage.entry = "main0";
            }
            else
            {
                if (!binary->glsl || !binary->glsl_size)
                {
                    if (error) *error = "shader '" + desc.shader + "' has no GLSL variant";
                    return nullptr;
                }
                stage.code_type = RHI::ShaderCodeType::GlslSource;
                stage.code = binary->glsl;
                stage.code_size = binary->glsl_size;
            }

            RHI::ResourceLayoutDesc layout_desc;
            layout_desc.name = desc.shader + ".layout";
            layout_desc.push_constant_stages = (uint32_t)RHI::ShaderStageFlagBits::Compute;
            for (const Binding &b : desc.bindings)
            {
                RHI::ResourceBinding rb;
                rb.set = 0;
                rb.binding = b.binding;
                rb.type = RHI::ResourceType::StorageBuffer;
                rb.stages = (uint32_t)RHI::ShaderStageFlagBits::Compute;
                rb.name = b.name;
                layout_desc.bindings.push_back(rb);
            }

            auto kernel = std::make_unique<Kernel>();
            kernel->owner_ = this;
            kernel->desc_ = desc;
            kernel->layout_ = device_->createResourceLayout(layout_desc);
            if (!kernel->layout_)
            {
                if (error) *error = "cannot create the compute resource layout";
                return nullptr;
            }

            RHI::PipelineDesc pipeline_desc;
            pipeline_desc.name = desc.shader + ".pipeline";
            pipeline_desc.shaders.push_back(stage);
            pipeline_desc.is_compute = true;
            pipeline_desc.compute_workgroup_x = desc.workgroup_x;
            pipeline_desc.compute_workgroup_y = desc.workgroup_y;
            pipeline_desc.compute_workgroup_z = desc.workgroup_z;
            pipeline_desc.resource_layout = layout_desc;
            kernel->pipeline_ = device_->createPipeline(pipeline_desc);
            if (!kernel->pipeline_)
            {
                if (error) *error = "cannot create the compute pipeline";
                return nullptr;
            }
            kernel->set_ = device_->createDescriptorSet(kernel->layout_, 0);
            if (!kernel->set_)
            {
                if (error) *error = "cannot create the compute descriptor set";
                return nullptr;
            }
            for (const Binding &b : desc.bindings) kernel->binding_slots_.push_back(b.binding);
            return kernel;
        }

        bool ComputeAPI::bind(Kernel &kernel, const std::vector<Buffer *> &buffers, std::string *error)
        {
            if (!device_ || !kernel.set_) return false;
            if (buffers.size() != kernel.binding_slots_.size())
            {
                if (error) *error = "compute kernel expects " + std::to_string(kernel.binding_slots_.size()) +
                                    " buffers, got " + std::to_string(buffers.size());
                return false;
            }
            std::vector<RHI::DescriptorBufferInfo> infos(buffers.size());
            std::vector<RHI::WriteDescriptorSet> writes(buffers.size());
            for (size_t i = 0; i < buffers.size(); ++i)
            {
                if (!buffers[i] || !buffers[i]->buffer)
                {
                    if (error) *error = "compute buffer " + std::to_string(i) + " is null";
                    return false;
                }
                infos[i].buffer = buffers[i]->buffer;
                infos[i].offset = 0;
                infos[i].range = buffers[i]->size;
                writes[i].binding = kernel.binding_slots_[i];
                writes[i].type = RHI::ResourceType::StorageBuffer;
                writes[i].buffer_info = &infos[i];
                writes[i].count = 1;
            }
            kernel.set_->write(writes.data(), (uint32_t)writes.size());
            kernel.bound_buffers_.clear();
            kernel.bound_ = true;
            return true;
        }

        void ComputeAPI::dispatch(RHI::RHICommandBuffer *cmd, Kernel &kernel, uint32_t groups_x, uint32_t groups_y,
                                  uint32_t groups_z, Buffer *written, RHI::BufferAccess written_then)
        {
            if (!cmd || !kernel.valid() || !kernel.bound_) return;
            cmd->bindPipeline(kernel.pipeline_);
            RHI::RHIDescriptorSet *set = kernel.set_;
            cmd->bindDescriptorSets(kernel.pipeline_, &set, 1, nullptr, 0);
            cmd->dispatch(groups_x, groups_y, groups_z);
            if (written && written->buffer) cmd->bufferBarrier(written->buffer, RHI::BufferAccess::ShaderWrite, written_then);
        }

        bool ComputeAPI::execute(Kernel &kernel, uint32_t groups_x, uint32_t groups_y, uint32_t groups_z,
                                 Buffer *written, RHI::BufferAccess written_then)
        {
            if (!device_ || !kernel.valid() || !kernel.bound_) return false;
            struct Context
            {
                ComputeAPI *self;
                Kernel *kernel;
                uint32_t gx, gy, gz;
                Buffer *written;
                RHI::BufferAccess after;
            } context{this, &kernel, groups_x, groups_y, groups_z, written, written_then};
            device_->submitImmediate(
                [](RHI::RHICommandBuffer *cmd, void *user)
                {
                    Context *c = static_cast<Context *>(user);
                    c->self->dispatch(cmd, *c->kernel, c->gx, c->gy, c->gz, c->written, c->after);
                },
                &context);
            return true;
        }

    } // namespace API
} // namespace Genesis
