#pragma once

#include <cstdarg>
#include <string>

#if defined(__clang__) || defined(__GNUC__)
#define NEXT_PRINTF_FORMAT(formatIndex, firstArg) __attribute__((format(printf, formatIndex, firstArg)))
#else
#define NEXT_PRINTF_FORMAT(formatIndex, firstArg)
#endif

namespace Next {

/**
 * @brief Logging verbosity levels for categorizing log messages
 *
 * Trace: Most verbose, for detailed execution tracing
 * Debug: Development and debugging information
 * Info: General informational messages
 * Warning: Something unexpected but not critical
 * Error: Error occurred but execution can continue
 * Fatal: Critical error requiring immediate attention
 */
enum class LogLevel {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
    Fatal
};

/**
 * @brief Centralized logging system for the NEXT engine
 *
 * Provides thread-safe logging with multiple severity levels.
 * Logs are output to the console and optionally to file.
 *
 * Usage:
 *   NEXT_LOG_INFO("Player spawned at position (%d, %d, %d)", x, y, z);
 *   NEXT_LOG_ERROR("Failed to load asset: %s", assetPath);
 */
class Logger {
public:
    /**
     * @brief Initialize the logging system
     *
     * Sets up console and file logging. Must be called before any logging operations.
     * Call this once during engine initialization.
     */
    static void Initialize();

    // Log filtering (minimum level to emit). Defaults:
    // - Debug builds: Debug
    // - Release builds: Info
    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();

    /**
     * @brief Shutdown the logging system
     *
     * Flushes any pending log messages and closes file handles.
     * Call during engine shutdown.
     */
    static void Shutdown();

    /**
     * @brief Log a message with the specified severity level
     *
     * Thread-safe logging function that outputs formatted messages.
     *
     * @param level Severity level of the log message
     * @param format Printf-style format string
     * @param ... Variable arguments matching the format string
     */
    static void Log(LogLevel level, const char* format, ...) NEXT_PRINTF_FORMAT(2, 3);

private:
    static void LogV(LogLevel level, const char* format, va_list args) NEXT_PRINTF_FORMAT(2, 0);
};

} // namespace Next

#undef NEXT_PRINTF_FORMAT

#define NEXT_LOG_TRACE(fmt, ...) ::Next::Logger::Log(::Next::LogLevel::Trace, fmt, ##__VA_ARGS__)
#define NEXT_LOG_DEBUG(fmt, ...) ::Next::Logger::Log(::Next::LogLevel::Debug, fmt, ##__VA_ARGS__)
#define NEXT_LOG_INFO(fmt, ...)  ::Next::Logger::Log(::Next::LogLevel::Info, fmt, ##__VA_ARGS__)
#define NEXT_LOG_WARNING(fmt, ...) ::Next::Logger::Log(::Next::LogLevel::Warning, fmt, ##__VA_ARGS__)
#define NEXT_LOG_ERROR(fmt, ...) ::Next::Logger::Log(::Next::LogLevel::Error, fmt, ##__VA_ARGS__)
#define NEXT_LOG_FATAL(fmt, ...) ::Next::Logger::Log(::Next::LogLevel::Fatal, fmt, ##__VA_ARGS__)
