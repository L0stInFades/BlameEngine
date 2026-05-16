#include "next/foundation/logger.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <atomic>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

namespace Next {
namespace {

static int ToInt(LogLevel level) {
    return static_cast<int>(level);
}

static LogLevel ParseLevel(const char* s) {
    if (!s || !*s) {
        return LogLevel::Debug;
    }

    auto eq = [](char a, char b) {
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        return a == b;
    };

    auto ieq = [&](const char* lit) {
        size_t i = 0;
        for (; s[i] && lit[i]; ++i) {
            if (!eq(s[i], lit[i])) return false;
        }
        return s[i] == '\0' && lit[i] == '\0';
    };

    if (ieq("trace")) return LogLevel::Trace;
    if (ieq("debug")) return LogLevel::Debug;
    if (ieq("info")) return LogLevel::Info;
    if (ieq("warn") || ieq("warning")) return LogLevel::Warning;
    if (ieq("error")) return LogLevel::Error;
    if (ieq("fatal")) return LogLevel::Fatal;
    return LogLevel::Debug;
}

#if defined(NDEBUG)
static std::atomic<int> g_minLevel{ToInt(LogLevel::Info)};
#else
static std::atomic<int> g_minLevel{ToInt(LogLevel::Debug)};
#endif

} // namespace

// File logging is not currently implemented. Future enhancements:
// - Add file logging with rotation
// - Implement crash handler integration
// - Add log level filtering at runtime
// - Support for multiple log sinks (console, file, network)

// Internal helper to output to both debugger and console
static void OutputLogMessage(const char* message) {
#ifdef _WIN32
    // Output to Visual Studio debugger
    OutputDebugStringA(message);
#endif
    // Output to stderr
    fprintf(stderr, "%s", message);
}

void Logger::Initialize() {
    // Currently only console logging to stderr is supported
    // Initialize file handles and log rotation here in future implementation

    // Allow runtime override via env var (e.g. "info", "debug", "trace").
    const char* env = std::getenv("NEXT_LOG_LEVEL");
    if (env && *env) {
        SetLevel(ParseLevel(env));
    }
}

void Logger::Shutdown() {
    // Flush and close file handles here in future implementation
    fflush(stderr);
}

void Logger::SetLevel(LogLevel level) {
    g_minLevel.store(ToInt(level), std::memory_order_relaxed);
}

LogLevel Logger::GetLevel() {
    return static_cast<LogLevel>(g_minLevel.load(std::memory_order_relaxed));
}

void Logger::Log(LogLevel level, const char* format, ...) {
    if (ToInt(level) < g_minLevel.load(std::memory_order_relaxed)) {
        return;
    }
    va_list args;
    va_start(args, format);
    LogV(level, format, args);
    va_end(args);
}

void Logger::LogV(LogLevel level, const char* format, va_list args) {
    if (ToInt(level) < g_minLevel.load(std::memory_order_relaxed)) {
        return;
    }
    const char* levelStr = "";
    switch (level) {
        case LogLevel::Trace: levelStr = "[TRACE]"; break;
        case LogLevel::Debug: levelStr = "[DEBUG]"; break;
        case LogLevel::Info:  levelStr = "[INFO] "; break;
        case LogLevel::Warning: levelStr = "[WARN] "; break;
        case LogLevel::Error: levelStr = "[ERROR]"; break;
        case LogLevel::Fatal: levelStr = "[FATAL]"; break;
    }

    // Get current time
    char timeStr[64];
    time_t now = time(nullptr);
    struct tm timeInfo;
#ifdef _WIN32
    localtime_s(&timeInfo, &now);
#else
    localtime_r(&now, &timeInfo);
#endif
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeInfo);

    // Format the complete log message
    char buffer[4096] = {};
    int prefixLen = snprintf(buffer, sizeof(buffer), "%s %s ", timeStr, levelStr);
    if (prefixLen > 0 && prefixLen < static_cast<int>(sizeof(buffer)) && format) {
        const int messageLen = vsnprintf(buffer + prefixLen,
                                         sizeof(buffer) - static_cast<size_t>(prefixLen),
                                         format,
                                         args);
        if (messageLen < 0) {
            buffer[prefixLen] = '\0';
        }
    } else if (prefixLen < 0) {
        buffer[0] = '\0';
    } else {
        buffer[sizeof(buffer) - 1] = '\0';
    }
    
    // Add newline
    size_t len = strlen(buffer);
    if (len < sizeof(buffer) - 1) {
        buffer[len] = '\n';
        buffer[len + 1] = '\0';
    } else {
        buffer[sizeof(buffer) - 2] = '\n';
        buffer[sizeof(buffer) - 1] = '\0';
    }

    // Output to both debugger and console
    OutputLogMessage(buffer);
    fflush(stderr);
}

} // namespace Next
