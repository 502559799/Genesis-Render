/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-22 20:59:23
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 21:10:54
 * @version:
 */
#ifndef __VULKAN_RENDERER_MEMORY_H__
#define __VULKAN_RENDERER_MEMORY_H__

#include "foundation_layer/platform.hpp"
#include "foundation_layer/service.hpp"

namespace Genesis
{

#define KiloB(size) (1024 * size)
#define MegaB(size) (1024 * 1024 * size)
#define GigaB(size) (1024 * 1024 * 1024 * size)
#define GenesisAlloc(size, allocator) (allocator)->allocate(size, 1, __FILE__, __LINE__)
#define GenesisAllocAlign(size, alignment, allocator) (allocator)->allocate(size, alignment, __FILE__, __LINE__)
#define GenesisDealloc(ptr, allocator) (allocator)->deallocate(ptr)


    void memory_copy(void *dst, void *src, size_t size);
    size_t memory_align(size_t size, size_t alignment);

    struct MemoryData
    {
        size_t alloc_bytes;
        size_t total_bytes;
        uint32_t alloc_count;

        void add(size_t bytes)
        {
            if (bytes)
            {
                alloc_bytes += bytes;
                ++alloc_count;
            }
        }
    };

    class Allocator
    {
    public:
        virtual ~Allocator() {}
        virtual void *allocate(size_t size, size_t alignment) = 0;
        virtual void *allocate(size_t size, size_t alignment, const char *file, int32_t line) = 0;
        virtual void deallocate(void *ptr) = 0;
    };

    class HeapAllocator : public Allocator
    {
    public:
        void *allocate(size_t size, size_t alignment) override;
        void *allocate(size_t size, size_t alignment, const char *file, int32_t line) override;
        void deallocate(void *ptr) override;
        ~HeapAllocator() override {};

        void init(size_t size);
        void shutdown();

    private:
        void *tlsf_handle_;
        void *memory_;
        size_t allocated_size_{0};
        size_t max_size_{0};
    };

    struct MemoryServiceConfig
    {
        size_t max_dynamic_size = MegaB(32);
    };
    class MemoryService : public Service
    {
    public:
        GENESIS_DECLARE_SERVICE(MemoryService);
        void init(void *config);
        void shutdown();

        HeapAllocator system_allocator;

        static constexpr const char *name_{"Genesis_Memory_Services"};
        GENESIS_SINGLETON_CLASS(MemoryService);
    };
}

#endif /* __VULKAN_RENDERER_MEMORY_H__ */