/*
 * Genesis Render - Engine
 * image.hpp : CPU image loading over stb_image (LDR + Radiance HDR).
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Genesis
{
    struct CPUImage
    {
        int w = 0, h = 0;
        bool hdr = false;
        std::vector<uint8_t> bytes;          // RGBA8 when !hdr
        std::vector<float> floats;           // RGBA32F when hdr
        void *data() const { return hdr ? (void *)floats.data() : (void *)bytes.data(); }
        size_t byteSize() const
        {
            return hdr ? floats.size() * sizeof(float) : bytes.size();
        }
    };

    // force_hdr: pick stbi_loadf for .hdr
    bool loadImageFile(const std::string &path, bool force_hdr, bool flip_y, CPUImage &out);
    bool loadImageMemory(const uint8_t *data, size_t size, bool force_hdr, bool flip_y, CPUImage &out);

    // Raw 8-bit texel payload (e.g. assimp embedded uncompressed textures).
    bool imageFromRawRGBA8(int w, int h, const uint8_t *rgba, bool flip_y, CPUImage &out);
    bool imageFromRawRGBA32F(int w, int h, const float *rgba, bool flip_y, CPUImage &out);
}
