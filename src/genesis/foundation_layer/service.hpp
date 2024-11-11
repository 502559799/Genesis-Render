/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-22 19:27:22
 * @Descripttion: 
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-11-08 20:56:45
 * @version: 
 */

#ifndef __VULKAN_RENDERER_SERVICE_H__
#define __VULKAN_RENDERER_SERVICE_H__

#include "platform.hpp"

namespace Genesis
{
    class Service
    {
        virtual void init(void * config) {}

        virtual void shutdown() {}
    };

    #define GENESIS_DECLARE_SERVICE(Type) \
        static Type* instance();

    #define GENESIS_SINGLETON_CLASS(Type) \
        Type(const Type&) = delete; \
        Type(Type&&) = delete; \
        Type& operator=(const Type&) = delete; \
        private: Type() {}
}

#endif // __VULKAN_RENDERER_SERVICE_H__