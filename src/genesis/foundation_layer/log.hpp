/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-23 21:17:37
 * @Descripttion: 
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 16:28:54
 * @version: 
 */
#ifndef __VULKAN_RENDERER_LOG_H__
#define __VULKAN_RENDERER_LOG_H__

#include "foundation_layer/service.hpp"
#include "foundation_layer/platform.hpp"

namespace Genesis
{
    typedef void (*LogCallback)(const char *message);

    class LogService : public Service
    {
    public:
        GENESIS_DECLARE_SERVICE(LogService);

        void setCallback(LogCallback callback);
        void format_print(const char *format, ...);

        static constexpr const char *name_{"Genesis_Log_Services"};

        GENESIS_SINGLETON_CLASS(LogService);

    private:
        LogCallback callback_ = nullptr;
    };

    #define GenesisPrint(format, ...) LogService::instance()->format_print(format, ## __VA_ARGS__);
    #define GenesisPrintRet(format, ...) LogService::instance()->format_print(format, ## __VA_ARGS__); Genesis::LogService::instance()->format_print("\n");
}

#endif /* __VULKAN_RENDERER_LOG_H__ */