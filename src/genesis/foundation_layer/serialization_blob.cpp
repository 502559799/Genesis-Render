/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-31 20:25:43
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-11-01 14:39:55
 * @version:
 */
#include "foundation_layer/serialization_blob.hpp"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

namespace Genesis
{
    void BlobSerializer::doWrite(Allocator *allocator, uint32_t version, size_t size)
    {
        allocator_ = allocator;
        // do allocate
        blob_memory_ = (char *)GenesisAlloc(size + sizeof(BlobHeader), allocator_); // allocate blob memory total size
        GENESISASSERT(blob_memory_);

        is_allocated_ = true;
        total_size_ = (u_int32_t)size + sizeof(BlobHeader);

        serialized_offset_ = allocated_offset_ = 0;
        serialized_offset_ = cur_version_ = version;
        is_mappable_ = is_reading_ = 0;
        // write header
        BlobHeader *header = (BlobHeader *)allocateS(sizeof(BlobHeader));
        header->version = version;
        header->mappable = is_mappable_;

        serialized_offset_ = allocated_offset_;
    }

    char *BlobSerializer::allocateS(size_t size)
    {
        if (allocated_offset_ + size > total_size_)
        {
            GenesisPrint("Blob allocation error: allocated,requested, total - %u + %u > %u\n", allocated_offset_, size, total_size_);
        }

        uint32_t offset = allocated_offset_;
        allocated_offset_ += size; // update allocated offset
        return is_reading_ ? data_memory_ + offset : blob_memory_ + offset;
    }

    void BlobSerializer::shutdown()
    {
        if (is_reading_)
        {
            if (blob_memory_ && is_allocated_)
            {
                GenesisDealloc(blob_memory_, allocator_);
            }
        }
        else
        {
            if (blob_memory_)
            {
                GenesisDealloc(blob_memory_, allocator_);
            }
        }

        serialized_offset_ = allocated_offset_ = is_allocated_ = false;
    }

}