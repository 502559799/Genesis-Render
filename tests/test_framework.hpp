/*
 * Genesis Render - minimal test framework
 *
 * Deliberately tiny: a registration macro, a runner with substring filtering,
 * and CHECK macros that record failures without aborting the whole suite.
 */
#ifndef __GENESIS_TEST_FRAMEWORK_HPP__
#define __GENESIS_TEST_FRAMEWORK_HPP__

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace GenesisTest
{
    struct TestCase
    {
        std::string group;
        std::string name;
        std::function<void()> fn;
        bool requires_gpu = false;
    };

    class Registry
    {
    public:
        static Registry &instance()
        {
            static Registry r;
            return r;
        }
        void add(const TestCase &t) { tests_.push_back(t); }
        const std::vector<TestCase> &tests() const { return tests_; }

        int &failures() { return failures_; }
        int &checks() { return checks_; }
        bool verbose() const { return verbose_; }
        void setVerbose(bool v) { verbose_ = v; }
        const std::string &currentTest() const { return current_; }
        void setCurrentTest(const std::string &n) { current_ = n; }

    private:
        std::vector<TestCase> tests_;
        int failures_ = 0;
        int checks_ = 0;
        bool verbose_ = false;
        std::string current_;
    };

    struct AutoRegister
    {
        AutoRegister(const char *group, const char *name, std::function<void()> fn, bool requires_gpu)
        {
            Registry::instance().add(TestCase{group, name, std::move(fn), requires_gpu});
        }
    };

    inline void reportFailure(const char *file, int line, const std::string &message)
    {
        ++Registry::instance().failures();
        std::fprintf(stderr, "  FAIL %s:%d [%s] %s\n", file, line,
                     Registry::instance().currentTest().c_str(), message.c_str());
    }

    inline void reportPass(const std::string &message)
    {
        if (Registry::instance().verbose())
            std::printf("  ok   %s\n", message.c_str());
    }

#define TEST(group, name)                                                                                  \
    static void genesis_test_##group##_##name();                                                           \
    static ::GenesisTest::AutoRegister genesis_reg_##group##_##name(#group, #name,                         \
                                                                    genesis_test_##group##_##name, false); \
    static void genesis_test_##group##_##name()

#define TEST_GPU(group, name)                                                                              \
    static void genesis_test_##group##_##name();                                                           \
    static ::GenesisTest::AutoRegister genesis_reg_##group##_##name(#group, #name,                         \
                                                                    genesis_test_##group##_##name, true);  \
    static void genesis_test_##group##_##name()

#define CHECK(cond)                                                                                     \
    do                                                                                                  \
    {                                                                                                   \
        ++::GenesisTest::Registry::instance().checks();                                                 \
        if (!(cond)) ::GenesisTest::reportFailure(__FILE__, __LINE__, std::string("CHECK(" #cond ")"));  \
        else ::GenesisTest::reportPass(#cond);                                                          \
    } while (0)

#define CHECK_MSG(cond, msg)                                                                            \
    do                                                                                                  \
    {                                                                                                   \
        ++::GenesisTest::Registry::instance().checks();                                                 \
        if (!(cond)) ::GenesisTest::reportFailure(__FILE__, __LINE__, (msg));                            \
        else ::GenesisTest::reportPass(msg);                                                             \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                           \
    do                                                                                                  \
    {                                                                                                   \
        ++::GenesisTest::Registry::instance().checks();                                                 \
        double _va = (double)(a), _vb = (double)(b);                                                    \
        if (!(std::fabs(_va - _vb) <= (double)(eps)))                                                 \
        {                                                                                               \
            char _buf[256];                                                                             \
            std::snprintf(_buf, sizeof(_buf), "CHECK_NEAR(%s=%g, %s=%g, eps=%g)", #a, _va, #b, _vb,      \
                          (double)(eps));                                                               \
            ::GenesisTest::reportFailure(__FILE__, __LINE__, _buf);                                     \
        }                                                                                               \
        else ::GenesisTest::reportPass(#a " ~= " #b);                                                   \
    } while (0)

#define CHECK_EQ(a, b)                                                                                  \
    do                                                                                                  \
    {                                                                                                   \
        ++::GenesisTest::Registry::instance().checks();                                                 \
        auto _va = (a);                                                                                 \
        auto _vb = (b);                                                                                 \
        if (!(_va == _vb))                                                                              \
        {                                                                                               \
            char _buf[256];                                                                             \
            std::snprintf(_buf, sizeof(_buf), "CHECK_EQ(%s, %s)", #a, #b);                              \
            ::GenesisTest::reportFailure(__FILE__, __LINE__, _buf);                                     \
        }                                                                                               \
        else ::GenesisTest::reportPass(#a " == " #b);                                                   \
    } while (0)

} // namespace GenesisTest

#endif /* __GENESIS_TEST_FRAMEWORK_HPP__ */
