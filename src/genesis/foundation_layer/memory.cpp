/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-24 21:36:41
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 14:20:52
 * @version:
 */

#include "foundation_layer/memory.hpp"
#include "foundation_layer/platform.hpp"
#include "external/tlsf.h"
#include "assert.hpp"

#include <memory>

#define HEAP_ALLOCATOR_STATS

namespace Genesis
{
    void memory_copy(void *dst, void *src, size_t size)
    {
        memcpy(dst, src, size);
    }

    size_t memory_align(size_t size, size_t alignment)
    {
        return (size + alignment - 1) & ~(alignment - 1);
    }

    static size_t s_size = MegaB(32) + tlsf_size() + 8;

    MemoryService *MemoryService::instance()
    {
        static MemoryService s_memory_service;
        return &s_memory_service;
    }

    void MemoryService::init(void *config)
    {
        GenesisPrint("init memoryService\n");
        MemoryServiceConfig *mem_cfg = static_cast<MemoryServiceConfig *>(config);
        system_allocator.init(mem_cfg ? mem_cfg->max_dynamic_size : s_size);
    }

    void MemoryService::shutdown()
    {
        system_allocator.shutdown();
        GenesisPrint("shutdown memoryService\n");
    }

    // heap alloc
    void HeapAllocator::init(size_t size)
    {
        memory_ = malloc(size);
        max_size_ = size;
        allocated_size_ = 0;

        tlsf_handle_ = tlsf_create_with_pool(memory_, size);
        if (!tlsf_handle_)
        {
            GENESISASSERT_MSG(false, "tlsf_create_with_pool failed");
        }
    }

    void exit_walker(void *ptr, size_t size, int used, void *userData)
    {
        MemoryData *data = static_cast<MemoryData *>(userData);
        data->add(used ? size : 0);

        if (used)
        {
            GenesisPrint("Found active allocation %p, %llu\n", ptr, size);
        }
    }

    void HeapAllocator::shutdown()
    {
        MemoryData data{0, max_size_};
        pool_t pool = tlsf_get_pool(tlsf_handle_);
        tlsf_walk_pool(pool, exit_walker, (void *)&data);

        if (data.alloc_bytes)
        {
            GenesisPrint("HeapAllocator Shutdown.\n===============\nFAILURE! Allocated memory detected. allocated %llu, total %llu\n===============\n\n", data.alloc_bytes, data.total_bytes);
        }
        else
        {
            GenesisPrint("HeapAllocator Shutdown - all memory free!\n");
        }
    }

    void *HeapAllocator::allocate(size_t size, size_t alignment)
    {
#ifdef HEAP_ALLOCATOR_STATS
        void *allocated_memory = alignment == 1 ? tlsf_malloc(tlsf_handle_, size) : tlsf_memalign(tlsf_handle_, alignment, size);
        size_t actual_size = tlsf_block_size(allocated_memory);
        allocated_size_ += actual_size;

        return allocated_memory;
#else
        return tlsf_malloc(tlsf_handle_, size);
#endif
    }

    void *HeapAllocator::allocate(size_t size, size_t alignment, const char *file, int32_t line)
    {
        return allocate(size, alignment);
    }

    void HeapAllocator::deallocate(void *ptr)
    {
#ifdef HEAP_ALLOCATOR_STATS
        size_t actual_size = tlsf_block_size(ptr);
        allocated_size_ -= actual_size;

        tlsf_free(tlsf_handle_, ptr);

#else
        tlsf_free(tlsf_handle_, ptr);
#endif
    }
}