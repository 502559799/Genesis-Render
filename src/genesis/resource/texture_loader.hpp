/*
 * Genesis Render - image decoding (stb_image) and RHI texture construction.
 */
#pragma once

#include "rhi/rhi.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace Genesis {

struct LoadedImage {
    int width = 0, height = 0, comp = 0;
    std::vector<uint8_t> rgba8;    // LDR, always 4 components
    std::vector<float> rgba32f;    // HDR, always 4 components
    bool hdr = false;
};

bool loadImageFile(const std::string& path, LoadedImage& out);
bool loadImageMemory(const uint8_t* data, size_t size, LoadedImage& out);

// Creates a sampled 2D texture with mipmaps; srgb selects sRGB decode on sample.
ITexture* createRhiTexture(IDevice* device, const LoadedImage& img, bool srgb);

// Procedural defaults (1x1 / small, deterministic).
ITexture* createDefaultAlbedo(IDevice* device);
ITexture* createDefaultNormal(IDevice* device);
ITexture* createDefaultMR(IDevice* device);       // G=1 roughness, B=0 metal
ITexture* createDefaultWhite(IDevice* device);
ITexture* createDefaultEmissive(IDevice* device);  // black

} // namespace Genesis
