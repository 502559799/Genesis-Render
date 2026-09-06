/*
 * @Descripttion: OpenGL 函数加载实现 (SDL_GL_GetProcAddress)
 */
#include "rhi_layer/opengl/gl_loader.hpp"

#include "foundation_layer/log.hpp"

#include <SDL.h>

#define GENESIS_GL_DEFINE(UPPER, LOWER, RET, ARGS) PFNGL##UPPER##PROC LOWER = nullptr;
GENESIS_GL_LIST(GENESIS_GL_DEFINE)
#undef GENESIS_GL_DEFINE

namespace Genesis
{
    namespace rhi
    {
        bool glLoadAll()
        {
            int missing = 0;
#define GENESIS_GL_LOAD(UPPER, LOWER, RET, ARGS)                                                     \
    LOWER = (PFNGL##UPPER##PROC)SDL_GL_GetProcAddress(#LOWER);                                       \
    if (!LOWER)                                                                                      \
    {                                                                                                \
        GenesisPrint("[RHI:OpenGL] missing gl function: %s\n", #LOWER);                              \
        ++missing;                                                                                   \
    }
            GENESIS_GL_LIST(GENESIS_GL_LOAD)
#undef GENESIS_GL_LOAD

            if (missing > 0)
            {
                GenesisPrint("[RHI:OpenGL] %d gl functions unresolved\n", missing);
                return false;
            }
            return true;
        }
    } // namespace rhi
} // namespace Genesis
