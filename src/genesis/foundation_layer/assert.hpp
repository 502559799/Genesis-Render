/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-24 18:55:34
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-25 11:16:21
 * @version:
 */
#include "foundation_layer/log.hpp"

namespace Genesis
{
#define GENESISASSERT(condition)                   \
    if (!(condition))                                \
    {                                              \
        GenesisPrint(GENESIS_FILENAME("FALSE\n")); \
        GENESIS_DEBUG_BREAK                        \
    }

#define GENESISASSERT_MSG(condition, message, ...)                                    \
    if (!(condition))                                                                   \
    {                                                                                 \
        GenesisPrint(GENESIS_FILENAME(GENESIS_CONCAT(message, "\n")), ##__VA_ARGS__); \
        GENESIS_DEBUG_BREAK                                                           \
    }
}
