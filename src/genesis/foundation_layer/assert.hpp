/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-24 18:55:34
 * @Descripttion: 断言宏 (v2.0 修复: 增加 include guard, 变参打印安全化)
 */
#ifndef __GENESIS_ASSERT_H__
#define __GENESIS_ASSERT_H__

#include "foundation_layer/log.hpp"

#define GENESISASSERT(condition)                        \
    do                                                  \
    {                                                   \
        if (!(condition))                               \
        {                                               \
            GenesisPrint(GENESIS_FILENAME("FALSE\n"));  \
            GENESIS_DEBUG_BREAK;                        \
        }                                               \
    } while (0)

#define GENESISASSERT_MSG(condition, message, ...)           \
    do                                                       \
    {                                                        \
        if (!(condition))                                    \
        {                                                    \
            GenesisPrint(GENESIS_FILENAME("ASSERT FAILED ")); \
            GenesisPrint(message, ##__VA_ARGS__);            \
            GenesisPrint("\n");                              \
            GENESIS_DEBUG_BREAK;                             \
        }                                                    \
    } while (0)

#endif // __GENESIS_ASSERT_H__
