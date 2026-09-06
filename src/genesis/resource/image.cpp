/*
 * Genesis Render - Engine
 * image.cpp
 */
#include "image.hpp"
#include "foundation_layer/log.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "external/stb_image.h"

#include <cstring>

namespace Genesis
{
static void flipRowsRGBA8(uint8_t *p, int w, int h, int ch)
{
    std::vector<uint8_t> row((size_t)w * ch);
    for (int y = 0; y < h / 2; ++y)
    {
        uint8_t *a = p + (size_t)y * w * ch;
        uint8_t *b = p + (size_t)(h - 1 - y) * w * ch;
        memcpy(row.data(), a, row.size());
        memcpy(a, b, row.size());
        memcpy(b, row.data(), row.size());
    }
}
static void flipRowsRGBA32F(float *p, int w, int h, int ch)
{
    std::vector<float> row((size_t)w * ch);
    for (int y = 0; y < h / 2; ++y)
    {
        float *a = p + (size_t)y * w * ch;
        float *b = p + (size_t)(h - 1 - y) * w * ch;
        memcpy(row.data(), a, row.size() * sizeof(float));
        memcpy(a, b, row.size() * sizeof(float));
        memcpy(b, row.data(), row.size() * sizeof(float));
    }
}

bool loadImageFile(const std::string &path, bool force_hdr, bool flip_y, CPUImage &out)
{
    int w, h, c;
    if (force_hdr)
    {
        float *px = stbi_loadf(path.c_str(), &w, &h, &c, 4);
        if (!px)
        {
            GenesisPrint("[Image] failed to load HDR %s: %s\n", path.c_str(), stbi_failure_reason());
            return false;
        }
        if (flip_y) flipRowsRGBA32F(px, w, h, 4);
        out.w = w; out.h = h; out.hdr = true;
        out.floats.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
    }
    else
    {
        uint8_t *px = stbi_load(path.c_str(), &w, &h, &c, 4);
        if (!px)
        {
            GenesisPrint("[Image] failed to load %s: %s\n", path.c_str(), stbi_failure_reason());
            return false;
        }
        if (flip_y) flipRowsRGBA8(px, w, h, 4);
        out.w = w; out.h = h; out.hdr = false;
        out.bytes.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
    }
    return true;
}

bool loadImageMemory(const uint8_t *data, size_t size, bool force_hdr, bool flip_y, CPUImage &out)
{
    int w, h, c;
    if (force_hdr)
    {
        float *px = stbi_loadf_from_memory(data, (int)size, &w, &h, &c, 4);
        if (!px) return false;
        if (flip_y) flipRowsRGBA32F(px, w, h, 4);
        out.w = w; out.h = h; out.hdr = true;
        out.floats.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
    }
    else
    {
        uint8_t *px = stbi_load_from_memory(data, (int)size, &w, &h, &c, 4);
        if (!px) return false;
        if (flip_y) flipRowsRGBA8(px, w, h, 4);
        out.w = w; out.h = h; out.hdr = false;
        out.bytes.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
    }
    return true;
}

bool imageFromRawRGBA8(int w, int h, const uint8_t *rgba, bool flip_y, CPUImage &out)
{
    out.w = w; out.h = h; out.hdr = false;
    out.bytes.assign(rgba, rgba + (size_t)w * h * 4);
    if (flip_y) flipRowsRGBA8(out.bytes.data(), w, h, 4);
    return true;
}
bool imageFromRawRGBA32F(int w, int h, const float *rgba, bool flip_y, CPUImage &out)
{
    out.w = w; out.h = h; out.hdr = true;
    out.floats.assign(rgba, rgba + (size_t)w * h * 4);
    if (flip_y) flipRowsRGBA32F(out.floats.data(), w, h, 4);
    return true;
}
}
