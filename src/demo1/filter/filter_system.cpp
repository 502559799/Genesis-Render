/*
 * Filter System Implementation
 * Author: Genesis Render Team
 */

#include "filter_system.h"
#include <algorithm>
#include <cmath>

namespace Genesis
{

FilterSystem::FilterSystem()
{
}

FilterSystem::~FilterSystem()
{
    shutdown();
}

bool FilterSystem::init(uint32_t width, uint32_t height)
{
    width_ = width;
    height_ = height;
    return true;
}

void FilterSystem::shutdown()
{
}

void FilterSystem::setActiveFilter(FilterType type)
{
    active_filter_ = type;
}

void FilterSystem::setFilterParams(const FilterParams& params)
{
    params_ = params;
}

void FilterSystem::applyFilterCPU(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride)
{
    switch (active_filter_)
    {
        case FilterType::None:
            // Copy as-is
            for (uint32_t y = 0; y < height; y++)
            {
                std::copy(src + y * stride, src + y * stride + width * 4, dst + y * stride);
            }
            break;

        case FilterType::Grayscale:
            applyGrayscale(src, dst, width, height, stride);
            break;

        case FilterType::Invert:
            applyInvert(src, dst, width, height, stride);
            break;

        case FilterType::Sepia:
            applySepia(src, dst, width, height, stride);
            break;

        case FilterType::Sharpen:
            applySharpen(src, dst, width, height, stride);
            break;

        case FilterType::Emboss:
            applyEmboss(src, dst, width, height, stride);
            break;

        default:
            applyBrightnessContrast(src, dst, width, height, stride);
            break;
    }
}

void FilterSystem::applyGrayscale(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride)
{
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            uint32_t offset = y * stride + x * 4;
            uint8_t b = src[offset];
            uint8_t g = src[offset + 1];
            uint8_t r = src[offset + 2];
            uint8_t a = src[offset + 3];

            // BT.709 luminance formula
            uint8_t gray = (uint8_t)(0.0722f * b + 0.7152f * g + 0.2126f * r);

            dst[offset]     = gray;
            dst[offset + 1] = gray;
            dst[offset + 2] = gray;
            dst[offset + 3] = a;
        }
    }
}

void FilterSystem::applyInvert(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride)
{
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            uint32_t offset = y * stride + x * 4;
            dst[offset]     = 255 - src[offset];
            dst[offset + 1] = 255 - src[offset + 1];
            dst[offset + 2] = 255 - src[offset + 2];
            dst[offset + 3] = src[offset + 3];
        }
    }
}

void FilterSystem::applySepia(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride)
{
    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            uint32_t offset = y * stride + x * 4;
            uint8_t b = src[offset];
            uint8_t g = src[offset + 1];
            uint8_t r = src[offset + 2];
            uint8_t a = src[offset + 3];

            // Sepia tone matrix
            uint8_t outR = (uint8_t)std::min(255, (int)(0.272f * r + 0.534f * g + 0.131f * b));
            uint8_t outG = (uint8_t)std::min(255, (int)(0.349f * r + 0.686f * g + 0.168f * b));
            uint8_t outB = (uint8_t)std::min(255, (int)(0.393f * r + 0.769f * g + 0.189f * b));

            dst[offset]     = outB;
            dst[offset + 1] = outG;
            dst[offset + 2] = outR;
            dst[offset + 3] = a;
        }
    }
}

void FilterSystem::applySharpen(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride)
{
    // Simple 3x3 sharpen kernel
    for (uint32_t y = 1; y < height - 1; y++)
    {
        for (uint32_t x = 1; x < width - 1; x++)
        {
            uint32_t sumR = 0, sumG = 0, sumB = 0;

            // Kernel: [ 0, -1,  0]
            //         [-1,  5, -1]
            //         [ 0, -1,  0]
            for (int ky = -1; ky <= 1; ky++)
            {
                for (int kx = -1; kx <= 1; kx++)
                {
                    float kernel = 0.0f;
                    if (ky == 0 && kx == 0) kernel = 5.0f;
                    else if (ky == 0 || kx == 0) kernel = -1.0f;

                    uint32_t offset = (y + ky) * stride + (x + kx) * 4;
                    sumB += (uint8_t)(src[offset] * kernel);
                    sumG += (uint8_t)(src[offset + 1] * kernel);
                    sumR += (uint8_t)(src[offset + 2] * kernel);
                }
            }

            uint32_t offset = y * stride + x * 4;
            auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
            dst[offset]     = (uint8_t)clamp8((int)sumB);
            dst[offset + 1] = (uint8_t)clamp8((int)sumG);
            dst[offset + 2] = (uint8_t)clamp8((int)sumR);
            dst[offset + 3] = src[offset + 3];
        }
    }
}

void FilterSystem::applyEmboss(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride)
{
    for (uint32_t y = 1; y < height - 1; y++)
    {
        for (uint32_t x = 1; x < width - 1; x++)
        {
            uint32_t offset = y * stride + x * 4;
            uint32_t offsetTL = (y - 1) * stride + (x - 1) * 4;
            uint32_t offsetBR = (y + 1) * stride + (x + 1) * 4;

            int b = (src[offsetBR] - src[offsetTL]) + 128;
            int g = (src[offsetBR + 1] - src[offsetTL + 1]) + 128;
            int r = (src[offsetBR + 2] - src[offsetTL + 2]) + 128;

            dst[offset]     = (uint8_t)std::clamp(b, 0, 255);
            dst[offset + 1] = (uint8_t)std::clamp(g, 0, 255);
            dst[offset + 2] = (uint8_t)std::clamp(r, 0, 255);
            dst[offset + 3] = src[offset + 3];
        }
    }
}

void FilterSystem::applyBrightnessContrast(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride)
{
    float brightness = params_.brightness * 255.0f;
    float contrast = params_.contrast;
    float factor = (259.0f * (contrast * 255.0f + 255.0f)) / (255.0f * (259.0f - contrast * 255.0f));

    for (uint32_t y = 0; y < height; y++)
    {
        for (uint32_t x = 0; x < width; x++)
        {
            uint32_t offset = y * stride + x * 4;

            auto clamp8 = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
            for (int c = 0; c < 3; c++)
            {
                float value = src[offset + c];
                value = factor * (value - 128.0f) + 128.0f + brightness;
                dst[offset + c] = (uint8_t)clamp8((int)value);
            }
            dst[offset + 3] = src[offset + 3];
        }
    }
}

std::vector<std::string> FilterSystem::getFilterNames()
{
    return {
        "None",
        "Grayscale",
        "Gaussian Blur",
        "Sobel Edge",
        "Invert",
        "Sepia",
        "Sharpen",
        "Emboss"
    };
}

std::string FilterSystem::getFilterName(FilterType type)
{
    return getFilterNames()[static_cast<int>(type)];
}

} // namespace Genesis
