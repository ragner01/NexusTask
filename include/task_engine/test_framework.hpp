#pragma once

#include <string>
#include <vector>
#include <functional>
#include <iostream>
#include <sstream>
#include <exception>

namespace task_engine {
namespace testing {

class TestCase {
public:
    TestCase(const std::string& name, std::function<void()> test_func)
        : name_(name)
        , test_func_(std::move(test_func))
    {}

    bool run() {
        try {
            test_func_();
            return true;
        } catch (const std::exception& e) {
            std::cerr << "  FAILED: " << e.what() << "\n";
            return false;
        } catch (...) {
            std::cerr << "  FAILED: Unknown exception\n";
            return false;
        }
    }

    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::function<void()> test_func_;
};

class TestSuite {
public:
    void add_test(const std::string& name, std::function<void()> test_func) {
        tests_.emplace_back(name, std::move(test_func));
    }

    int run_all() {
        std::cout << "Running " << tests_.size() << " tests...\n\n";
        int passed = 0;
        int failed = 0;

        for (auto& test : tests_) {
            std::cout << "Test: " << test.name() << "... ";
            if (test.run()) {
                std::cout << "PASSED\n";
                ++passed;
            } else {
                std::cout << "FAILED\n";
                ++failed;
            }
        }

        std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
        return failed == 0 ? 0 : 1;
    }

private:
    std::vector<TestCase> tests_;
};

// Macros for easier test writing
#define TEST(name) \
    suite.add_test(#name, []()

#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        throw std::runtime_error("Assertion failed: " #condition); \
    }

#define ASSERT_FALSE(condition) \
    if (condition) { \
        throw std::runtime_error("Assertion failed: " #condition " should be false"); \
    }

#define ASSERT_EQ(a, b) \
    if ((a) != (b)) { \
        std::ostringstream oss; \
        oss << "Assertion failed: " #a " != " #b " (" << (a) << " != " << (b) << ")"; \
        throw std::runtime_error(oss.str()); \
    }

#define ASSERT_NE(a, b) \
    if ((a) == (b)) { \
        std::ostringstream oss; \
        oss << "Assertion failed: " #a " == " #b; \
        throw std::runtime_error(oss.str()); \
    }

} // namespace testing
} // namespace task_engine

