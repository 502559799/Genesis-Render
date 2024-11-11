/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-30 13:36:16
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 21:15:46
 * @version:
 */
#ifndef __VULKAN_RENDERER_FILE_H__
#define __VULKAN_RENDERER_FILE_H__

#include <stdio.h>
#include "foundation_layer/platform.hpp"
#include "foundation_layer/service.hpp"

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

    class FileService : public Service
    {
    public:
        GENESIS_DECLARE_SERVICE(FileService);
        static constexpr const char *name_{"Genesis_Log_Services"};

        // read file & allocate memory
        //  free memory manually
        FileReadResult readBin(const char *path, Allocator *allocator);
        char *readBin(const char *path, Allocator *allocator, uint32_t size);

        FileReadResult readText(const char *path, Allocator *allocator);
        char *readText(const char *path, Allocator *allocator, uint32_t size);

        void writeBin(const char *path, void *data, size_t size);

        void fileOpen(const char *path, FileHandle *handle, const char *mode);
        void fileClose(FileHandle handle);
        size_t fileWrite(FileHandle *handle, uint8_t *data, uint32_t size, uint32_t count);
        bool fileDelete(const char *path);
        bool fileExists(const char *path);

        GENESIS_SINGLETON_CLASS(FileService);
        long fileGetSize(FileHandle *handle);
    };

}

#endif /* __VULKAN_RENDERER_FILE_H__ */