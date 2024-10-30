/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-22 18:31:16
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-25 11:30:53
 * @version:
 */

#ifndef __VULKAN_RENDERER_PLATFORM_H__
#define __VULKAN_RENDERER_PLATFORM_H__

#include <stdint.h>
#include <signal.h>

// Macros
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

#define GENESIS_DEBUG_BREAK \
    raise(SIGTRAP);

#define GENESIS_CONCAT_OPERATOR(x, y) \
    x y
#define GENESIS_CONCAT(x, y) \
    GENESIS_CONCAT_OPERATOR(x, y)

#define GENESIS_UNIQUE_SUFFIX(param) \
    GENESIS_CONCAT(param, __LINE__)

#define GENESIS_STRINGIFY(x) \
    #x
#define GENESIS_STRINGIFY_VALUE(x) \
    GENESIS_STRINGIFY(x)

#define GENESIS_LINE_STRING \
    GENESIS_STRINGIFY_VALUE(__LINE__)
#define GENESIS_FILENAME(message) \
    __FILE__ "(" GENESIS_LINE_STRING ") : " message

//

#endif /* __VULKAN_RENDERER_PLATFORM_H__ */