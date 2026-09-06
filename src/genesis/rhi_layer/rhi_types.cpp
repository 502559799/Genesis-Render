/*
 * @Descripttion: RHI 类型辅助实现 (半精度浮点转换)
 */
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "external/stb_image_write.h"

#include "rhi_layer/rhi_types.hpp"

namespace Genesis
{
    namespace rhi
    {
        uint16_t floatToHalf(float value)
        {
            union
            {
                float f;
                uint32_t u;
            } in{value};

            uint32_t sign = (in.u >> 16) & 0x8000;
            int32_t exponent = ((in.u >> 23) & 0xff) - 112;
            uint32_t mantissa = in.u & 0x007fffff;

            if (exponent <= 0)
            {
                // 次正规数或下溢
                if (exponent < -10)
                    return (uint16_t)sign;
                mantissa = (mantissa | 0x00800000) >> (1 - exponent);
                return (uint16_t)(sign | ((mantissa + 0x1000) >> 13));
            }
            if (exponent >= 31)
            {
                // 上溢 -> inf
                return (uint16_t)(sign | 0x7c00);
            }
            return (uint16_t)(sign | (exponent << 10) | ((mantissa + 0x1000) >> 13));
        }

        float halfToFloat(uint16_t value)
        {
            uint32_t sign = (value & 0x8000) << 16;
            uint32_t exponent = (value >> 10) & 0x1f;
            uint32_t mantissa = value & 0x03ff;

            union
            {
                float f;
                uint32_t u;
            } out;

            if (exponent == 0)
            {
                if (mantissa == 0)
                {
                    out.u = sign;
                    return out.f;
                }
                // 次正规数
                exponent = 1;
                while (!(mantissa & 0x0400))
                {
                    mantissa <<= 1;
                    --exponent;
                }
                mantissa &= 0x03ff;
                out.u = sign | ((exponent + 112) << 23) | (mantissa << 13);
                return out.f;
            }
            if (exponent == 31)
            {
                out.u = sign | 0x7f800000 | (mantissa << 13);
                return out.f;
            }
            out.u = sign | ((exponent + 112) << 23) | (mantissa << 13);
            return out.f;
        }

    } // namespace rhi
} // namespace Genesis
