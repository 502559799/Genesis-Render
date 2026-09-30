/*
 * Genesis Render - per-frame transient GPU memory and deferred destruction.
 *
 * Vulkan and Metal record frame N+1 while the GPU may still execute frame N,
 * so no buffer or descriptor set that a submitted frame references may be
 * overwritten or destroyed on the CPU.  The FrameAllocator provides the one
 * mechanism the renderer uses for everything that changes per draw or per
 * frame:
 *
 *   - one uniform ring and one vertex/index ring, each split into
 *     `frames_in_flight` regions; allocations are linear inside the region of
 *     the current frame and the region is recycled only after the device has
 *     waited for that frame slot (RHIDevice::beginFrame),
 *   - uniform data is always bound through *dynamic* offsets into the ring,
 *     so descriptor sets stay immutable after creation (never rewritten while
 *     a command buffer that uses them is pending),
 *   - objects released during a frame are destroyed when their slot comes
 *     around again (or at shutdown).
 */
#ifndef __GENESIS_FRAME_RESOURCES_HPP__
#define __GENESIS_FRAME_RESOURCES_HPP__

#include "rhi/rhi.hpp"

#include <cstdint>
#include <functional>
#include <vector>

namespace Genesis
{
    namespace Render
    {
        struct TransientAllocation
        {
            RHI::RHIBuffer *buffer = nullptr;
            uint64_t offset = 0;
            uint64_t size = 0;
            bool valid() const { return buffer != nullptr; }
        };

        class FrameAllocator
        {
        public:
            bool initialize(RHI::RHIDevice *device, uint64_t uniform_bytes_per_frame, uint64_t vertex_bytes_per_frame);
            void shutdown();

            // Starts a new frame slot: recycles its ring regions and destroys
            // everything that was released the last time the slot was used.
            void beginFrame(uint32_t slot);
            uint32_t slot() const { return slot_; }
            uint32_t slotCount() const { return slot_count_; }

            // Uniform data (aligned to the device's dynamic offset alignment).
            // Returns the offset to pass as dynamic offset; the data is
            // uploaded immediately.
            TransientAllocation uploadUniform(const void *data, uint64_t size);
            template <typename T>
            uint32_t pushUniform(const T &value)
            {
                return (uint32_t)uploadUniform(&value, sizeof(T)).offset;
            }
            RHI::RHIBuffer *uniformBuffer() const { return uniform_ring_; }

            // Vertex / index data (16 byte aligned).
            TransientAllocation uploadVertices(const void *data, uint64_t size);

            // Deferred destruction (safe while frames are in flight).
            void release(RHI::RHIDescriptorSet *set);
            void release(RHI::RHIBuffer *buffer);
            void release(RHI::RHITexture *texture);
            void release(RHI::RHIFramebuffer *framebuffer);
            void release(std::function<void()> fn);
            // Destroys everything pending immediately (call after waitIdle()).
            void flushReleases();

            uint64_t uniformBytesUsed() const { return uniform_head_ - uniform_base(); }
            uint64_t vertexBytesUsed() const { return vertex_head_ - vertex_base(); }

        private:
            uint64_t uniform_base() const { return (uint64_t)slot_ * uniform_region_; }
            uint64_t vertex_base() const { return (uint64_t)slot_ * vertex_region_; }

            RHI::RHIDevice *device_ = nullptr;
            RHI::RHIBuffer *uniform_ring_ = nullptr;
            RHI::RHIBuffer *vertex_ring_ = nullptr;
            uint64_t uniform_region_ = 0;
            uint64_t vertex_region_ = 0;
            uint64_t uniform_head_ = 0;
            uint64_t vertex_head_ = 0;
            uint64_t alignment_ = 256;
            uint32_t slot_ = 0;
            uint32_t slot_count_ = 1;
            bool warned_uniform_ = false;
            bool warned_vertex_ = false;

            struct Pending
            {
                std::vector<std::function<void()>> actions;
            };
            std::vector<Pending> pending_;
            // Overflow vertex buffers created when a frame exceeds its region.
            std::vector<std::vector<RHI::RHIBuffer *>> overflow_;
        };

    } // namespace Render
} // namespace Genesis

#endif /* __GENESIS_FRAME_RESOURCES_HPP__ */
