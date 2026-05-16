#include "next/foundation/logger.h"
#include <gtest/gtest.h>
#include <sstream>
#include <regex>

namespace Next {
namespace testing {

using ::testing::Test;

class LoggerTest : public Test {
protected:
    void SetUp() override {
        // Initialize logger before each test
        Logger::Initialize();
    }

    void TearDown() override {
        // Cleanup after each test
        Logger::Shutdown();
    }
};

// Test that logger can be initialized and shutdown
TEST_F(LoggerTest, InitializeAndShutdown) {
    // If we got here without crashing, initialization succeeded
    // Shutdown will happen in TearDown
    SUCCEED();
}

// Test that logger can be initialized multiple times (idempotent)
TEST_F(LoggerTest, MultipleInitialize) {
    Logger::Initialize();
    Logger::Initialize();
    Logger::Shutdown();
    SUCCEED();
}

// Test logging at different levels
TEST_F(LoggerTest, LogDifferentLevels) {
    // These should not crash
    NEXT_LOG_TRACE("This is a trace message: %d", 42);
    NEXT_LOG_DEBUG("This is a debug message: %s", "test");
    NEXT_LOG_INFO("This is an info message: %.2f", 3.14f);
    NEXT_LOG_WARNING("This is a warning message: %x", 0xFF);
    NEXT_LOG_ERROR("This is an error message: %p", static_cast<void*>(nullptr));

    SUCCEED();
}

// Test logging with empty format string
TEST_F(LoggerTest, LogEmptyFormat) {
    NEXT_LOG_INFO("");
    NEXT_LOG_DEBUG("%s", "");
    SUCCEED();
}

// Test logging with a null format pointer
TEST_F(LoggerTest, LogNullFormat) {
    const char* nullFormat = nullptr;
    ::testing::internal::CaptureStderr();
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-security"
#endif
    Logger::Log(LogLevel::Info, nullFormat);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
    const std::string output = ::testing::internal::GetCapturedStderr();
    ASSERT_FALSE(output.empty());
    EXPECT_EQ(output.back(), '\n');
}

// Test logging with multiple arguments
TEST_F(LoggerTest, LogMultipleArguments) {
    int a = 1;
    float b = 2.5f;
    const char* c = "test";
    NEXT_LOG_INFO("Int: %d, Float: %.2f, String: %s", a, b, c);
    SUCCEED();
}

// Test logging with special characters
TEST_F(LoggerTest, LogSpecialCharacters) {
    NEXT_LOG_INFO("Special chars: \\n \\t \\r %%");
    NEXT_LOG_DEBUG("Unicode test: \u4E2D\u6587");  // Chinese characters
    SUCCEED();
}

// Test logging with very long messages
TEST_F(LoggerTest, LongMessage) {
    std::string longMessage(8192, 'A');
    ::testing::internal::CaptureStderr();
    NEXT_LOG_INFO("Long message: %s", longMessage.c_str());
    const std::string output = ::testing::internal::GetCapturedStderr();
    ASSERT_FALSE(output.empty());
    EXPECT_NE(output.find("Long message:"), std::string::npos);
    EXPECT_LT(output.size(), longMessage.size());
    EXPECT_EQ(output.back(), '\n');
}

// Test that all log levels work (using macros)
TEST_F(LoggerTest, AllLogLevelEnums) {
    NEXT_LOG_TRACE("Trace level");
    NEXT_LOG_DEBUG("Debug level");
    NEXT_LOG_INFO("Info level");
    NEXT_LOG_WARNING("Warning level");
    NEXT_LOG_ERROR("Error level");
    // Note: Fatal might terminate, so we skip it in normal tests
    SUCCEED();
}

// Test format string validation
TEST_F(LoggerTest, FormatStringValidation) {
    // Correct format strings
    NEXT_LOG_INFO("%d", 42);
    NEXT_LOG_INFO("%s", "test");
    NEXT_LOG_INFO("%f", 3.14f);
    NEXT_LOG_INFO("%x %o %u", 255, 8, 42);

    // Multiple placeholders
    NEXT_LOG_INFO("%d %s %f", 42, "test", 3.14f);

    SUCCEED();
}

} // namespace testing
} // namespace Next
