/*
 * Genesis Render - lightweight logger used by the new engine layers
 *
 * Kept deliberately simple (no service initialisation order dependency, no
 * allocations) while still supporting:
 *   - severity levels with coloured console output
 *   - a ring buffer of recent messages that the UI log panel can display
 *   - an optional user callback (used by tests to capture output)
 *
 * The legacy `LogService` in foundation_layer/log.hpp is still used by the
 * original demo1 code and remains untouched.
 */
#ifndef __GENESIS_LOG2_HPP__
#define __GENESIS_LOG2_HPP__

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace Genesis
{
    enum class LogLevel : uint32_t
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error,
        Fatal,
    };

    class Logger
    {
    public:
        static Logger &instance()
        {
            static Logger s_instance;
            return s_instance;
        }

        struct Entry
        {
            LogLevel level;
            std::string text;
        };

        void setMinLevel(LogLevel level) { min_level_ = level; }
        LogLevel minLevel() const { return min_level_; }
        void setColorOutput(bool enabled) { color_ = enabled; }

        using Callback = void (*)(LogLevel level, const char *message);
        void setCallback(Callback cb) { callback_ = cb; }

        void print(LogLevel level, const char *format, ...)
        {
            if ((uint32_t)level < (uint32_t)min_level_) return;
            char buf[2048];
            va_list args;
            va_start(args, format);
            vsnprintf(buf, sizeof(buf), format, args);
            va_end(args);
            buf[sizeof(buf) - 1] = '\0';
            emit(level, buf);
        }

        void printV(LogLevel level, const char *format, va_list args)
        {
            if ((uint32_t)level < (uint32_t)min_level_) return;
            char buf[2048];
            vsnprintf(buf, sizeof(buf), format, args);
            buf[sizeof(buf) - 1] = '\0';
            emit(level, buf);
        }

        // Recent messages, for the UI log panel.
        std::vector<Entry> history() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return history_;
        }

        void clearHistory()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            history_.clear();
        }

        uint32_t errorCount() const { return error_count_; }
        uint32_t warningCount() const { return warning_count_; }

        static const char *levelName(LogLevel level)
        {
            switch (level)
            {
            case LogLevel::Trace: return "TRACE";
            case LogLevel::Debug: return "DEBUG";
            case LogLevel::Info: return "INFO ";
            case LogLevel::Warn: return "WARN ";
            case LogLevel::Error: return "ERROR";
            case LogLevel::Fatal: return "FATAL";
            }
            return "?????";
        }

    private:
        Logger() {}

        void emit(LogLevel level, const char *msg)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                history_.push_back(Entry{level, msg});
                if (history_.size() > max_history_)
                    history_.erase(history_.begin(), history_.begin() + (history_.size() - max_history_));
                if (level >= LogLevel::Error) ++error_count_;
                else if (level == LogLevel::Warn) ++warning_count_;
            }

            const char *color = "";
            const char *reset = "";
            if (color_)
            {
                switch (level)
                {
                case LogLevel::Trace: color = "\033[90m"; break;
                case LogLevel::Debug: color = "\033[36m"; break;
                case LogLevel::Info: color = "\033[37m"; break;
                case LogLevel::Warn: color = "\033[33m"; break;
                case LogLevel::Error: color = "\033[31m"; break;
                case LogLevel::Fatal: color = "\033[35m"; break;
                }
                reset = "\033[0m";
            }
            std::fprintf(level >= LogLevel::Warn ? stderr : stdout, "%s[%s] %s%s\n",
                         color, levelName(level), msg, reset);
            std::fflush(level >= LogLevel::Warn ? stderr : stdout);

            if (callback_) callback_(level, msg);
        }

        mutable std::mutex mutex_;
        std::vector<Entry> history_;
        size_t max_history_ = 512;
        LogLevel min_level_ = LogLevel::Info;
        bool color_ = true;
        Callback callback_ = nullptr;
        uint32_t error_count_ = 0;
        uint32_t warning_count_ = 0;
    };

#define GEN_LOG_INFO(fmt, ...) ::Genesis::Logger::instance().print(::Genesis::LogLevel::Info, fmt, ##__VA_ARGS__)
#define GEN_LOG_WARN(fmt, ...) ::Genesis::Logger::instance().print(::Genesis::LogLevel::Warn, fmt, ##__VA_ARGS__)
#define GEN_LOG_ERROR(fmt, ...) ::Genesis::Logger::instance().print(::Genesis::LogLevel::Error, fmt, ##__VA_ARGS__)
#define GEN_LOG_DEBUG(fmt, ...) ::Genesis::Logger::instance().print(::Genesis::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define GEN_LOG_TRACE(fmt, ...) ::Genesis::Logger::instance().print(::Genesis::LogLevel::Trace, fmt, ##__VA_ARGS__)
#define GEN_LOG_FATAL(fmt, ...) ::Genesis::Logger::instance().print(::Genesis::LogLevel::Fatal, fmt, ##__VA_ARGS__)

} // namespace Genesis

#endif /* __GENESIS_LOG2_HPP__ */
