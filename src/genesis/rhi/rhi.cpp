/*
 * Genesis Render - Engine
 * rhi.cpp : RHI backend factory.
 */
#include "rhi.hpp"
#include "foundation_layer/log.hpp"

#include "opengl/gl_rhi.hpp"
#if defined(GENESIS_ENABLE_VULKAN)
#include "vulkan/vk_rhi.hpp"
#endif

namespace Genesis
{
    IRHIDevice *IRHIDevice::create(RenderBackend backend)
    {
        switch (backend)
        {
        case RenderBackend::OpenGL:
            return createGLRhi();
        case RenderBackend::Vulkan:
#if defined(GENESIS_ENABLE_VULKAN)
            return createVKRhi();
#else
            GenesisPrint("[RHI] Vulkan backend compiled out, falling back to OpenGL\n");
            return createGLRhi();
#endif
        }
        return createGLRhi();
    }
}
