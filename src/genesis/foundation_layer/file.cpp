/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-30 16:58:40
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 21:57:32
 * @version:
 */
#include "foundation_layer/file.hpp"
#include "foundation_layer/memory.hpp"

#define MAX_PATH 65536
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Genesis
{
    ////////////basic file operation
    void FileService::fileOpen(const char *path, FileHandle *handle, const char *mode)
    {
        *handle = fopen(path, mode);
    }

    void FileService::fileClose(FileHandle handle)
    {
        if (handle)
            fclose(handle);
    }

    size_t FileService::fileWrite(FileHandle *handle, uint8_t *data, uint32_t size, uint32_t count)
    {
        return fwrite(data, size, count, *handle);
    }

    bool FileService::fileDelete(const char *path)
    {
        return remove(path) == 0;
    }

    bool FileService::fileExists(const char *path)
    {
        return access(path, F_OK) == 0;
    }

    long FileService::fileGetSize(FileHandle *handle)
    {
        long pos = ftell(*handle);
        fseek(*handle, 0, SEEK_END);
        long size = ftell(*handle);
        fseek(*handle, pos, SEEK_SET);
        return size;
    }

    ////////////file operation
    FileReadResult FileService::readBin(const char *path, Allocator *allocator)
    {
        FileReadResult result{0, nullptr};
        FileHandle handle;

        fileOpen(path, &handle, "rb");
        if (handle)
        {
            size_t file_size = fileGetSize(&handle);
            result.size = file_size;
            result.data = (char *)GenesisAlloc(file_size, allocator);
            fread(result.data, file_size, 1, handle);

            fileClose(handle);
        }
        return result;
    }

    char *FileService::readBin(const char *path, Allocator *allocator, uint32_t size)
    {
        char *result = nullptr;

        FileHandle handle;
        fileOpen(path, &handle, "rb");
        if (handle)
        {
            auto file_size = fileGetSize(&handle);
            if (file_size < size)
            {
                fileClose(handle);
                return nullptr;
            }
            result = (char *)GenesisAlloc(file_size + 1, allocator);
            fread(result, size, 1, handle);
            result[file_size] = '\0';
            fileClose(handle);
        }
        return result;
    }

    FileReadResult FileService::readText(const char *path, Allocator *allocator)
    {
        FileReadResult result{0, nullptr};
        FileHandle handle;
        fileOpen(path, &handle, "r");
        if (handle)
        {
            size_t file_size = fileGetSize(&handle);
            result.data = (char *)GenesisAlloc(file_size + 1, allocator);
            auto bytes = fread(result.data, file_size, 1, handle);
            result.data[file_size] = '\0';
            result.size = bytes;
            fileClose(handle);
        }
        return result;
    }

    char *FileService::readText(const char *path, Allocator *allocator, uint32_t size)
    {
        char *result = nullptr;
        FileHandle handle;
        fileOpen(path, &handle, "r");
        if (handle)
        {
            auto file_size = fileGetSize(&handle);
            if (file_size < size)
            {
                fileClose(handle);
                return nullptr;
            }
            result = (char *)GenesisAlloc(file_size + 1, allocator);
            auto bytes = fread(result, file_size, 1, handle);
            result[file_size] = '\0';
            fileClose(handle);
        }
        return result;
    }
}