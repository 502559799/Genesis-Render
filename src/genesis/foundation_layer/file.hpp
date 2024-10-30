/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-30 13:36:16
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 13:49:23
 * @version:
 */
#ifndef __VULKAN_RENDERER_FILE_H__
#define __VULKAN_RENDERER_FILE_H__

#include <stdio.h>
#include "foundation_layer/platform.hpp"

namespace Genesis
{
    class Allocator;

    using FileHandle = FILE *;

    static const uint32_t max_file_path_ = 512;

    struct FileDirectory
    {
        char path[max_file_path_];
    };

    struct FileReadResult
    {
        uint32_t size;
        char *data;
    };

    // read file & allocate memory
    //  free memory manually
    FileReadResult read_file(const char *path, Allocator *allocator);
    FileReadResult read_file_binary(const char *path, Allocator *allocator);

    void write_file_binary(const char *path, void *data, size_t size);

}

#endif /* __VULKAN_RENDERER_FILE_H__ */