#include "next/log/log.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <sstream>
#include <cstdio>

#ifdef _WIN32
#include <Windows.h>
#include <debugapi.h>
#endif

namespace Next {

// =============================================================================
// ConsoleLogAppender 实现
// =============================================================================

ConsoleLogAppender::ConsoleLogAppender(bool useColor) : useColor_(useColor) {}

std::string ConsoleLogAppender::GetLevelString(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
        case LogLevel::Fatal:   return "FATAL";
        default:                return "UNKNOWN";
    }
}

std::string ConsoleLogAppender::GetColorCode(LogLevel level) {
    if (!useColor_) return "";

    switch (level) {
        case LogLevel::Debug:   return "\033[0;36m";   // Cyan
        case LogLevel::Info:    return "\033[0;32m";   // Green
        case LogLevel::Warning: return "\033[0;33m";   // Yellow
        case LogLevel::Error:   return "\033[0;31m";   // Red
        case LogLevel::Fatal:   return "\033[1;31m";   // Bold Red
        default:                return "\033[0m";      // Reset
    }
}

void ConsoleLogAppender::Write(const LogRecord& record) {
    std::ostringstream oss;

    // 日志级别
    std::string colorCode = GetColorCode(record.level);
    std::string resetCode = useColor_ ? "\033[0m" : "";

    oss << colorCode << "[" << GetLevelString(record.level) << "]" << resetCode;

    // 时间戳
    if (record.timestamp > 0.0) {
        time_t time = static_cast<time_t>(record.timestamp);
        tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        oss << " [" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "]";
    }

    // 文件位置
    if (!record.file.empty()) {
        oss << " [" << record.file;
        if (record.line > 0) {
            oss << ":" << record.line;
        }
        oss << "]";
    }

    // 消息
    oss << " " << record.message;

    // 输出
    std::cout << oss.str() << std::endl;
}

void ConsoleLogAppender::Flush() {
    std::cout.flush();
}

// =============================================================================
// FileLogAppender 实现
// =============================================================================

FileLogAppender::FileLogAppender(const std::string& filePath)
    : filePath_(filePath) {
    OpenFile();
}

FileLogAppender::~FileLogAppender() {
    CloseFile();
}

void FileLogAppender::OpenFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open()) {
        file_.open(filePath_, std::ios::out | std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "Failed to open log file: " << filePath_ << std::endl;
        }
    }
}

void FileLogAppender::CloseFile() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.close();
    }
}

void FileLogAppender::Write(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!file_.is_open()) {
        return;
    }

    // 日志级别
    std::ostringstream oss;
    switch (record.level) {
        case LogLevel::Debug:   oss << "[DEBUG]"; break;
        case LogLevel::Info:    oss << "[INFO]"; break;
        case LogLevel::Warning: oss << "[WARN]"; break;
        case LogLevel::Error:   oss << "[ERROR]"; break;
        case LogLevel::Fatal:   oss << "[FATAL]"; break;
        default:                oss << "[UNKNOWN]"; break;
    }

    // 时间戳
    if (record.timestamp > 0.0) {
        time_t time = static_cast<time_t>(record.timestamp);
        tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        oss << " [" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "]";
    }

    // 线程ID
    if (record.threadId > 0) {
        oss << " [TID:" << record.threadId << "]";
    }

    // 文件位置
    if (!record.file.empty()) {
        oss << " [" << record.file;
        if (record.line > 0) {
            oss << ":" << record.line;
        }
        if (!record.function.empty()) {
            oss << " " << record.function;
        }
        oss << "]";
    }

    // 消息
    oss << " " << record.message;

    // 输出
    file_ << oss.str() << std::endl;
}

void FileLogAppender::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
    }
}

// =============================================================================
// DebuggerLogAppender 实现
// =============================================================================

void DebuggerLogAppender::Write(const LogRecord& record) {
#ifdef _WIN32
    std::ostringstream oss;

    // 日志级别
    switch (record.level) {
        case LogLevel::Debug:   oss << "[DEBUG]"; break;
        case LogLevel::Info:    oss << "[INFO]"; break;
        case LogLevel::Warning: oss << "[WARN]"; break;
        case LogLevel::Error:   oss << "[ERROR]"; break;
        case LogLevel::Fatal:   oss << "[FATAL]"; break;
        default:                oss << "[UNKNOWN]"; break;
    }

    // 时间戳
    if (record.timestamp > 0.0) {
        time_t time = static_cast<time_t>(record.timestamp);
        tm tm{};
        localtime_s(&tm, &time);
        oss << " [" << std::put_time(&tm, "%H:%M:%S") << "]";
    }

    // 文件位置
    if (!record.file.empty()) {
        oss << " [" << record.file;
        if (record.line > 0) {
            oss << ":" << record.line;
        }
        oss << "]";
    }

    // 消息
    oss << " " << record.message;

    // 输出到调试器
    OutputDebugStringA((oss.str() + "\n").c_str());
#else
    // 非 Windows 平台，输出到 stderr
    std::cerr << "[DEBUGGER] " << record.message << std::endl;
#endif
}

void DebuggerLogAppender::Flush() {
    // 调试器输出无需刷新
}

// =============================================================================
// CustomLogAppender 实现
// =============================================================================

CustomLogAppender::CustomLogAppender(LogCallback callback)
    : callback_(std::move(callback)) {
}

void CustomLogAppender::Write(const LogRecord& record) {
    if (callback_) {
        callback_(record);
    }
}

void CustomLogAppender::Flush() {
    // 自定义输出器无需刷新
}

// =============================================================================
// Logger 实现
// =============================================================================

Logger& Logger::GetInstance() {
    static Logger instance;
    return instance;
}

void Logger::Initialize(const LogConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    config_ = config;

    // 如果没有添加任何输出器，添加默认的控制台输出器
    if (appenders_.empty()) {
        appenders_.push_back(std::make_unique<ConsoleLogAppender>(true));
    }

    initialized_ = true;

    // 输出初始化信息
    Log(LogLevel::Info, "Logger initialized", __FILE__, __LINE__, __FUNCTION__);
}

void Logger::Shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    Log(LogLevel::Info, "Logger shutting down", __FILE__, __LINE__, __FUNCTION__);
    Flush();

    appenders_.clear();
    initialized_ = false;
}

void Logger::AddAppender(std::unique_ptr<LogAppender> appender) {
    std::lock_guard<std::mutex> lock(mutex_);
    appenders_.push_back(std::move(appender));
}

void Logger::ClearAppenders() {
    std::lock_guard<std::mutex> lock(mutex_);
    appenders_.clear();
}

void Logger::Log(LogLevel level, const std::string& message,
                 const char* file, int line, const char* function) {
    if (!ShouldLog(level)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 创建日志记录
    LogRecord record;
    record.level = level;
    record.message = message;

    if (config_.useFileLocation) {
        if (file) record.file = file;
        record.line = line;
        if (function) record.function = function;
    }

    if (config_.useTimestamp) {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        record.timestamp = std::chrono::duration<double>(duration).count();
    }

    if (config_.useThreadId) {
#ifdef _WIN32
        record.threadId = GetCurrentThreadId();
#else
        // 简化版本，实际应该使用平台相关的线程ID获取函数
        record.threadId = 0;
#endif
    }

    // 写入所有输出器
    for (auto& appender : appenders_) {
        if (appender) {
            appender->Write(record);
        }
    }

    // 如果配置为每次写入后刷新
    if (config_.flushOnWrite) {
        Flush();
    }

    // Fatal 级别日志后刷新
    if (level == LogLevel::Fatal) {
        Flush();
    }
}

void Logger::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& appender : appenders_) {
        if (appender) {
            appender->Flush();
        }
    }
}

std::string Logger::FormatRecord(const LogRecord& record) {
    std::ostringstream oss;

    // 日志级别
    switch (record.level) {
        case LogLevel::Debug:   oss << "[DEBUG]"; break;
        case LogLevel::Info:    oss << "[INFO]"; break;
        case LogLevel::Warning: oss << "[WARN]"; break;
        case LogLevel::Error:   oss << "[ERROR]"; break;
        case LogLevel::Fatal:   oss << "[FATAL]"; break;
        default:                oss << "[UNKNOWN]"; break;
    }

    // 时间戳
    if (record.timestamp > 0.0) {
        time_t time = static_cast<time_t>(record.timestamp);
        tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &time);
#else
        localtime_r(&time, &tm);
#endif
        oss << " [" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "]";
    }

    // 线程ID
    if (record.threadId > 0) {
        oss << " [TID:" << record.threadId << "]";
    }

    // 文件位置
    if (!record.file.empty()) {
        oss << " [" << record.file;
        if (record.line > 0) {
            oss << ":" << record.line;
        }
        if (!record.function.empty()) {
            oss << " " << record.function;
        }
        oss << "]";
    }

    // 消息
    oss << " " << record.message;

    return oss.str();
}

bool Logger::ShouldLog(LogLevel level) const {
    if (!initialized_) {
        return false;
    }
    return level >= config_.minLevel;
}

// =============================================================================
// LogStream 实现
// =============================================================================

LogStream::LogStream(LogLevel level, const char* file, int line, const char* function)
    : level_(level)
    , file_(file)
    , line_(line)
    , function_(function) {
}

LogStream::~LogStream() {
    std::string message = stream_.str();
    if (!message.empty()) {
        Logger::GetInstance().Log(level_, message, file_, line_, function_);
    }
}

// =============================================================================
// 字符串格式化辅助函数（简化版）
// =============================================================================

template<typename... Args>
std::string FormatString(const char* format, Args... args) {
    if (!format) return "";

    // 简化版本：使用 snprintf
    // 注意：这只是一个基础实现，完整的实现需要处理更多情况
    size_t size = snprintf(nullptr, 0, format, args...) + 1;
    std::unique_ptr<char[]> buf(new char[size]);
    snprintf(buf.get(), size, format, args...);
    return std::string(buf.get(), buf.get() + size - 1);
}

// 显式实例化常用的模板
template std::string FormatString<int>(const char*, int);
template std::string FormatString<const char*>(const char*, const char*);
template std::string FormatString<int, const char*>(const char*, int, const char*);
template std::string FormatString<double>(const char*, double);

} // namespace Next
