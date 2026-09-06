/*
 * Genesis Render - Engine
 * vk_rhi.hpp : Vulkan 1.x implementation of IRHIDevice (runs on MoltenVK too).
 */
#pragma once

#include "../rhi.hpp"

namespace Genesis
{
    IRHIDevice *createVKRhi();
}
