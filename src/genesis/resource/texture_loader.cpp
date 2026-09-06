/*
 * Genesis Render - image decoding and RHI texture construction.
 */
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "resource/texture_loader.hpp"
#include "foundation_layer/log.hpp"

#include <cstring>

namespace Genesis {

bool loadImageMemory(const uint8_t* data, size_t size, LoadedImage& out) {
    int w, h, c;
    uint8_t* px = stbi_load_from_memory(data, (int)size, &w, &h, &c, 4);
    if (px) {
        out.width = w; out.height = h; out.comp = 4; out.hdr = false;
        out.rgba8.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
        return true;
    }
    float* hdr = stbi_loadf_from_memory(data, (int)size, &w, &h, &c, 4);
    if (hdr) {
        out.width = w; out.height = h; out.comp = 4; out.hdr = true;
        out.rgba32f.assign(hdr, hdr + (size_t)w * h * 4);
        stbi_image_free(hdr);
        return true;
    }
    return false;
}

bool loadImageFile(const std::string& path, LoadedImage& out) {
    int w, h, c;
    if (stbi_is_hdr(path.c_str())) {
        float* px = stbi_loadf(path.c_str(), &w, &h, &c, 4);
        if (!px) { GenesisPrint("[tex] failed to load HDR %s: %s\n", path.c_str(), stbi_failure_reason()); return false; }
        out.width = w; out.height = h; out.comp = 4; out.hdr = true;
        out.rgba32f.assign(px, px + (size_t)w * h * 4);
        stbi_image_free(px);
        return true;
    }
    uint8_t* px = stbi_load(path.c_str(), &w, &h, &c, 4);
    if (!px) { GenesisPrint("[tex] failed to load %s: %s\n", path.c_str(), stbi_failure_reason()); return false; }
    out.width = w; out.height = h; out.comp = 4; out.hdr = false;
    out.rgba8.assign(px, px + (size_t)w * h * 4);
    stbi_image_free(px);
    return true;
}

static uint32_t mipCount(uint32_t w, uint32_t h) {
    uint32_t m = 1;
    while (w > 1 || h > 1) { w = std::max(1u, w / 2); h = std::max(1u, h / 2); ++m; }
    return m;
}

ITexture* createRhiTexture(IDevice* device, const LoadedImage& img, bool srgb) {
    TextureDesc d;
    d.width = img.width; d.height = img.height;
    d.mips = mipCount(img.width, img.height);
    d.format = img.hdr ? RHIFormat::RGBA16F : (srgb ? RHIFormat::RGBA8_SRGB : RHIFormat::RGBA8);
    d.usage = TU_Sampled;
    d.wrap = RHIWrap::Repeat;
    ITexture* t = device->createTexture(d);
    if (img.hdr) t->uploadRGBA32F(img.rgba32f.data());
    else t->uploadRGBA8(img.rgba8.data());
    return t;
}

static ITexture* solidTexture(IDevice* device, const uint8_t rgba[4], RHIFormat fmt) {
    TextureDesc d;
    d.width = 1; d.height = 1; d.mips = 1; d.format = fmt;
    d.usage = TU_Sampled; d.filter_min = d.filter_mag = RHIFilter::Nearest;
    d.wrap = RHIWrap::Repeat;
    ITexture* t = device->createTexture(d);
    t->uploadRGBA8(rgba);
    return t;
}

ITexture* createDefaultAlbedo(IDevice* d)  { static const uint8_t p[4] = {255,255,255,255}; return solidTexture(d, p, RHIFormat::RGBA8_SRGB); }
ITexture* createDefaultNormal(IDevice* d)  { static const uint8_t p[4] = {128,128,255,255}; return solidTexture(d, p, RHIFormat::RGBA8); }
ITexture* createDefaultMR(IDevice* d)      { static const uint8_t p[4] = {255,255,255,255}; return solidTexture(d, p, RHIFormat::RGBA8); }
ITexture* createDefaultWhite(IDevice* d)   { static const uint8_t p[4] = {255,255,255,255}; return solidTexture(d, p, RHIFormat::RGBA8); }
ITexture* createDefaultEmissive(IDevice* d){ static const uint8_t p[4] = {0,0,0,255}; return solidTexture(d, p, RHIFormat::RGBA8); }

} // namespace Genesis
