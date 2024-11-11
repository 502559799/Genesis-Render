/*
 * @Author: Yan Dongbao
 * @Date: 2024-11-11 14:47:40
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-11-12 14:57:28
 * @version:
 */
#ifndef __VULKAN_RENDERER_GPU_ENUM_H__
#define __VULKAN_RENDERER_GPU_ENUM_H__

#include "foundation_layer/platform.hpp"

namespace Genesis
{

    namespace CullMode
    {
        static const char *s_name[] = {
            "None",
            "Front",
            "Back",
            "Count"};

        enum Enum
        {
            None,
            Front,
            Back,
            Count
        };

        enum Mask
        {
            None_mask = 1 << 0,
            Front_mask = 1 << 1,
            Back_mask = 1 << 2,
            Count_mask = 1 << 3
        };

        static const char *toString(Enum value)
        {
            return (uint32_t)value < Count ? s_name[(int)value] : "Unknown";
        }
    }

    namespace BlendMode
    {
        // TODO
    }

    namespace TextureType
    {
        static const char *s_name[] = {
            "Texture1D",
            "Texture2D",
            "Texture3D",
            "TextureCube",
            "Count"};

        enum Enum
        {
            Texture1D,
            Texture2D,
            Texture3D,
            TextureCube,
            Count
        };

        enum Mask
        {
            Texture1D_mask = 1 << 0,
            Texture2D_mask = 1 << 1,
            Texture3D_mask = 1 << 2,
            TextureCube_mask = 1 << 3,
            Count_mask = 1 << 4
        };

        static const char *toString(Enum value)
        {
            return (uint32_t)value < Count ? s_name[(int)value] : "Unknown";
        }
    }

    namespace ResourceUsageType
    {
        static const char *s_name[] = {
            "Immutable",
            "Dynamic",
            "Stream",
            "Count"};

        enum Enum
        {
            Immutable,
            Dynamic,
            Stream,
            Count
        };

        enum Mask
        {
            Immutable_mask = 1 << 0,
            Dynamic_mask = 1 << 1,
            Stream_mask = 1 << 2,
            Count_mask = 1 << 3
        };

        static const char *toString(Enum value)
        {
            return (uint32_t)value < Count ? s_name[(int)value] : "Unknown";
        }
    }

}

#endif /* __VULKAN_RENDERER_GPU_ENUM_H__ */