/*
 * Genesis Render - per-frame transient GPU memory and deferred destruction.
 */
#include "renderer/frame_resources.hpp"
#include "foundation_layer/genesis_log.hpp"

#include <algorithm>

namespace Genesis
{
    namespace Render
    {
        bool FrameAllocator::initialize(RHI::RHIDevice *device, uint64_t uniform_bytes_per_frame, uint64_t vertex_bytes_per_frame)
        {
            device_ = device;
            slot_count_ = std::max(1u, device->info().frames_in_flight);
            alignment_ = std::max<uint64_t>(16, device->info().min_uniform_buffer_offset_alignment);
            uniform_region_ = RHI::alignUp(uniform_bytes_per_frame, alignment_);
            vertex_region_ = RHI::alignUp(vertex_bytes_per_frame, 256);

            RHI::BufferDesc ud;
            ud.name = "frame.uniform_ring";
            ud.size = uniform_region_ * slot_count_;
            ud.usage = (uint32_t)RHI::BufferUsageFlagBits::Uniform | (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
            ud.memory_usage = RHI::MemoryUsage::CpuToGpu;
            uniform_ring_ = device->createBuffer(ud);

            RHI::BufferDesc vd;
            vd.name = "frame.vertex_ring";
            vd.size = vertex_region_ * slot_count_;
            vd.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::Index |
                       (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
            vd.memory_usage = RHI::MemoryUsage::CpuToGpu;
            vertex_ring_ = device->createBuffer(vd);

            pending_.assign(slot_count_, Pending());
            overflow_.assign(slot_count_, {});
            slot_ = 0;
            uniform_head_ = 0;
            vertex_head_ = 0;
            if (!uniform_ring_ || !vertex_ring_)
            {
                GEN_LOG_ERROR("[Frame] failed to allocate the transient rings");
                return false;
            }
            GEN_LOG_DEBUG("[Frame] rings: %u slot(s), uniform %llu KB/frame (align %llu), vertex %llu KB/frame",
                          slot_count_, (unsigned long long)(uniform_region_ / 1024), (unsigned long long)alignment_,
                          (unsigned long long)(vertex_region_ / 1024));
            return true;
        }

        void FrameAllocator::shutdown()
        {
            if (!device_) return;
            flushReleases();
            if (uniform_ring_) device_->destroyBuffer(uniform_ring_);
            if (vertex_ring_) device_->destroyBuffer(vertex_ring_);
            uniform_ring_ = vertex_ring_ = nullptr;
            device_ = nullptr;
        }

        void FrameAllocator::beginFrame(uint32_t slot)
        {
            slot_ = slot % slot_count_;
            uniform_head_ = uniform_base();
            vertex_head_ = vertex_base();
            Pending &p = pending_[slot_];
            std::vector<std::function<void()>> actions;
            actions.swap(p.actions);
            for (auto &fn : actions) fn();
            for (RHI::RHIBuffer *b : overflow_[slot_]) device_->destroyBuffer(b);
            overflow_[slot_].clear();
        }

        TransientAllocation FrameAllocator::uploadUniform(const void *data, uint64_t size)
        {
            TransientAllocation out;
            const uint64_t aligned = RHI::alignUp(size, alignment_);
            if (uniform_head_ + aligned > uniform_base() + uniform_region_)
            {
                if (!warned_uniform_)
                    GEN_LOG_ERROR("[Frame] uniform ring exhausted (%llu KB per frame) - draws will reuse stale data",
                                  (unsigned long long)(uniform_region_ / 1024));
                warned_uniform_ = true;
                // Keep rendering with the last slot rather than overrunning
                // the region of a frame that may still be in flight.
                uniform_head_ = uniform_base() + uniform_region_ - aligned;
            }
            out.buffer = uniform_ring_;
            out.offset = uniform_head_;
            out.size = size;
            if (data) device_->writeBuffer(uniform_ring_, data, size, uniform_head_);
            uniform_head_ += aligned;
            return out;
        }

        TransientAllocation FrameAllocator::uploadVertices(const void *data, uint64_t size)
        {
            TransientAllocation out;
            const uint64_t aligned = RHI::alignUp(size, 16);
            if (vertex_head_ + aligned > vertex_base() + vertex_region_)
            {
                // Large one-off frames get a dedicated buffer that lives until
                // the slot is recycled.
                if (!warned_vertex_)
                    GEN_LOG_WARN("[Frame] vertex ring exhausted - using overflow buffers");
                warned_vertex_ = true;
                RHI::BufferDesc d;
                d.name = "frame.vertex_overflow";
                d.size = std::max<uint64_t>(aligned, 64 * 1024);
                d.usage = (uint32_t)RHI::BufferUsageFlagBits::Vertex | (uint32_t)RHI::BufferUsageFlagBits::Index |
                          (uint32_t)RHI::BufferUsageFlagBits::TransferDst;
                d.memory_usage = RHI::MemoryUsage::CpuToGpu;
                RHI::RHIBuffer *b = device_->createBuffer(d);
                if (!b) return out;
                overflow_[slot_].push_back(b);
                if (data) device_->writeBuffer(b, data, size, 0);
                out.buffer = b;
                out.offset = 0;
                out.size = size;
                return out;
            }
            out.buffer = vertex_ring_;
            out.offset = vertex_head_;
            out.size = size;
            if (data) device_->writeBuffer(vertex_ring_, data, size, vertex_head_);
            vertex_head_ += aligned;
            return out;
        }

        void FrameAllocator::release(RHI::RHIDescriptorSet *set)
        {
            if (!set) return;
            RHI::RHIDevice *d = device_;
            release([d, set]() { d->destroyDescriptorSet(set); });
        }

        void FrameAllocator::release(RHI::RHIBuffer *buffer)
        {
            if (!buffer) return;
            RHI::RHIDevice *d = device_;
            release([d, buffer]() { d->destroyBuffer(buffer); });
        }

        void FrameAllocator::release(RHI::RHITexture *texture)
        {
            if (!texture) return;
            RHI::RHIDevice *d = device_;
            release([d, texture]() { d->destroyTexture(texture); });
        }

        void FrameAllocator::release(RHI::RHIFramebuffer *framebuffer)
        {
            if (!framebuffer) return;
            RHI::RHIDevice *d = device_;
            release([d, framebuffer]() { d->destroyFramebuffer(framebuffer); });
        }

        void FrameAllocator::release(std::function<void()> fn)
        {
            if (!device_ || pending_.empty())
            {
                if (fn) fn();
                return;
            }
            // Destroyed when this slot is reused, i.e. after the device has
            // waited for every frame that could reference the object.
            pending_[slot_].actions.push_back(std::move(fn));
        }

        void FrameAllocator::flushReleases()
        {
            for (uint32_t i = 0; i < pending_.size(); ++i)
            {
                std::vector<std::function<void()>> actions;
                actions.swap(pending_[i].actions);
                for (auto &fn : actions) fn();
                for (RHI::RHIBuffer *b : overflow_[i]) device_->destroyBuffer(b);
                overflow_[i].clear();
            }
        }

    } // namespace Render
} // namespace Genesis
