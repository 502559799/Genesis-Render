/*
 * @Author: Yan Dongbao
 * @Date: 2024-10-23 21:17:45
 * @Descripttion:
 * @LastEditors: Yan Dongbao
 * @LastEditTime: 2024-10-30 16:29:22
 * @version:
 */
#include "log.hpp"

#include <stdio.h>
#include <stdarg.h>

namespace Genesis
{
    static constexpr uint32_t buffer_size = 1024 * 1024;
    static char buffer[buffer_size];

    static void console_log(char *buffer)
    {
        printf("%s", buffer);
    }

    LogService *LogService::instance()
    {
        static LogService log_service;
        return &log_service;
    }

    void LogService::format_print(const char *format, ...)
    {
        va_list args;
        va_start(args, format);
        vsnprintf(buffer, ARRAY_SIZE(buffer), format, args);

        buffer[ARRAY_SIZE(buffer) - 1] = '\0';
        va_end(args);

        console_log(buffer);

        if (callback_)
        {
            callback_(buffer);
        }
    }

    void LogService::setCallback(LogCallback callback)
    {
        callback_ = callback;
    }
}