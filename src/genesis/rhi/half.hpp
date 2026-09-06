/* Minimal IEEE754 float32 -> float16 packing (round-to-nearest). */
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace Genesis {

inline uint16_t f32ToF16(float f) {
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
    uint32_t mant = x & 0x7FFFFF;
    if (((x >> 23) & 0xFF) == 0xFF) {
        // inf / nan
        return (uint16_t)(sign | 0x7C00 | (mant ? 1 : 0));
    }
    if (exp >= 31) return (uint16_t)(sign | 0x7C00);
    if (exp <= 0) {
        if (exp < -10) return (uint16_t)sign;
        mant |= 0x800000;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t r = mant >> shift;
        uint32_t round = (mant >> (shift - 1)) & 1;
        return (uint16_t)(sign | ((r + round) & 0x3FF));
    }
    uint32_t r = mant >> 13;
    uint32_t round = (mant >> 12) & 1;
    return (uint16_t)(sign | (exp << 10) | r) + (uint16_t)round;
}

inline std::vector<uint16_t> f32ToF16(const float* src, size_t floats) {
    std::vector<uint16_t> out(floats);
    for (size_t i = 0; i < floats; ++i) out[i] = f32ToF16(src[i]);
    return out;
}

} // namespace Genesis
