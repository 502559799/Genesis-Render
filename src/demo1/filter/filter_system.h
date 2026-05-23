/*
 * Filter System Interface
 * Author: Genesis Render Team
 */

#ifndef __GENESIS_FILTER_SYSTEM_H__
#define __GENESIS_FILTER_SYSTEM_H__

#include <cstdint>
#include <vector>
#include <string>

namespace Genesis
{

enum class FilterType
{
    None = 0,
    Grayscale,
    GaussianBlur,
    SobelEdge,
    Invert,
    Sepia,
    Sharpen,
    Emboss
};

struct FilterParams
{
    float sigma = 1.0f;          // For Gaussian blur
    float intensity = 1.0f;      // For edge detection
    float brightness = 0.0f;     // -1.0 to 1.0
    float contrast = 1.0f;       // 0.0 to 2.0
    float saturation = 1.0f;     // 0.0 to 2.0
};

class FilterSystem
{
public:
    FilterSystem();
    ~FilterSystem();

    bool init(uint32_t width, uint32_t height);
    void shutdown();

    void setActiveFilter(FilterType type);
    void setFilterParams(const FilterParams& params);
    FilterType getActiveFilter() const { return active_filter_; }
    FilterParams getFilterParams() const { return params_; }

    // Apply filter to CPU buffer (for simple filters)
    void applyFilterCPU(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride);

    static std::vector<std::string> getFilterNames();
    static std::string getFilterName(FilterType type);

private:
    FilterType active_filter_ = FilterType::None;
    FilterParams params_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    // CPU filter implementations
    void applyGrayscale(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride);
    void applyInvert(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride);
    void applySepia(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride);
    void applySharpen(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride);
    void applyEmboss(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride);
    void applyBrightnessContrast(uint8_t* src, uint8_t* dst, uint32_t width, uint32_t height, uint32_t stride);
};

} // namespace Genesis

#endif // __GENESIS_FILTER_SYSTEM_H__
