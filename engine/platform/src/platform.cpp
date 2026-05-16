#include "next/platform/platform.h"
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#include <dbghelp.h>
#include <eh.h>
#endif

#if defined(__clang__) || defined(__GNUC__)
#define NEXT_PLATFORM_PRINTF_FORMAT(formatIndex, firstArg) __attribute__((format(printf, formatIndex, firstArg)))
#else
#define NEXT_PLATFORM_PRINTF_FORMAT(formatIndex, firstArg)
#endif

namespace Next {

namespace {

void PlatformLog(const char* level, const char* format, ...) NEXT_PLATFORM_PRINTF_FORMAT(2, 3);

#if defined(_WIN32)
void LogWithPrefix(const char* level, const char* format, va_list args) NEXT_PLATFORM_PRINTF_FORMAT(2, 0);
void LogWithPrefix(const char* level, const char* format, va_list args) {
    char buffer[1024] = {};
    int prefixLen = snprintf(buffer, sizeof(buffer), "%s ", level);
    if (prefixLen > 0 && prefixLen < static_cast<int>(sizeof(buffer))) {
        vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, format, args);
    }

    size_t len = strlen(buffer);
    if (len < sizeof(buffer) - 1) {
        buffer[len] = '\n';
        buffer[len + 1] = '\0';
    }

    OutputDebugStringA(buffer);
    fprintf(stderr, "%s", buffer);
    fflush(stderr);
}

// Windows exception filter for crash handling
LONG WINAPI CrashHandler(EXCEPTION_POINTERS* exceptionInfo) {
    PlatformLog("[FATAL]", "=== CRASH DETECTED ===");

    DWORD exceptionCode = exceptionInfo->ExceptionRecord->ExceptionCode;
    const char* exceptionName = "Unknown";

    switch (exceptionCode) {
        case EXCEPTION_ACCESS_VIOLATION:
            exceptionName = "Access Violation";
            break;
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
            exceptionName = "Array Bounds Exceeded";
            break;
        case EXCEPTION_BREAKPOINT:
            exceptionName = "Breakpoint";
            break;
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            exceptionName = "Data Type Misalignment";
            break;
        case EXCEPTION_FLT_DENORMAL_OPERAND:
            exceptionName = "Float Denormal Operand";
            break;
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:
            exceptionName = "Float Divide by Zero";
            break;
        case EXCEPTION_FLT_INEXACT_RESULT:
            exceptionName = "Float Inexact Result";
            break;
        case EXCEPTION_FLT_INVALID_OPERATION:
            exceptionName = "Float Invalid Operation";
            break;
        case EXCEPTION_FLT_OVERFLOW:
            exceptionName = "Float Overflow";
            break;
        case EXCEPTION_FLT_STACK_CHECK:
            exceptionName = "Float Stack Check";
            break;
        case EXCEPTION_FLT_UNDERFLOW:
            exceptionName = "Float Underflow";
            break;
        case EXCEPTION_ILLEGAL_INSTRUCTION:
            exceptionName = "Illegal Instruction";
            break;
        case EXCEPTION_IN_PAGE_ERROR:
            exceptionName = "In Page Error";
            break;
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
            exceptionName = "Integer Divide by Zero";
            break;
        case EXCEPTION_INT_OVERFLOW:
            exceptionName = "Integer Overflow";
            break;
        case EXCEPTION_INVALID_DISPOSITION:
            exceptionName = "Invalid Disposition";
            break;
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
            exceptionName = "Noncontinuable Exception";
            break;
        case EXCEPTION_PRIV_INSTRUCTION:
            exceptionName = "Privileged Instruction";
            break;
        case EXCEPTION_SINGLE_STEP:
            exceptionName = "Single Step";
            break;
        case EXCEPTION_STACK_OVERFLOW:
            exceptionName = "Stack Overflow";
            break;
    }

    PlatformLog("[FATAL]", "Exception Code: 0x%08X (%s)", static_cast<unsigned int>(exceptionCode), exceptionName);
    PlatformLog("[FATAL]", "Exception Address: 0x%p", exceptionInfo->ExceptionRecord->ExceptionAddress);

    // Write mini dump if possible
    HANDLE hFile = CreateFileA("crash_dump.dmp",
                               GENERIC_WRITE,
                               0,
                               nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL,
                               nullptr);

    if (hFile != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei;
        mei.ThreadId = GetCurrentThreadId();
        mei.ExceptionPointers = exceptionInfo;
        mei.ClientPointers = FALSE;

        if (MiniDumpWriteDump(GetCurrentProcess(),
                             GetCurrentProcessId(),
                             hFile,
                             MiniDumpNormal,
                             &mei,
                             nullptr,
                             nullptr)) {
            PlatformLog("[FATAL]", "Mini dump written to: crash_dump.dmp");
        } else {
            PlatformLog("[ERROR]", "Failed to write mini dump");
        }
        CloseHandle(hFile);
    }

    char message[512];
    snprintf(message, sizeof(message),
             "NEXT Engine has crashed!\n\nException: %s (0x%08X)\nA crash dump has been saved.",
             exceptionName, static_cast<unsigned int>(exceptionCode));
    MessageBoxA(nullptr, message, "NEXT Engine - Fatal Error", MB_ICONERROR | MB_OK);

    return EXCEPTION_EXECUTE_HANDLER;
}

#else

void LogWithPrefix(const char* level, const char* format, va_list args) NEXT_PLATFORM_PRINTF_FORMAT(2, 0);
void LogWithPrefix(const char* level, const char* format, va_list args) {
    char buffer[1024] = {};
    int prefixLen = snprintf(buffer, sizeof(buffer), "%s ", level);
    if (prefixLen > 0 && prefixLen < static_cast<int>(sizeof(buffer))) {
        vsnprintf(buffer + prefixLen, sizeof(buffer) - prefixLen, format, args);
    }

    size_t len = strlen(buffer);
    if (len < sizeof(buffer) - 1) {
        buffer[len] = '\n';
        buffer[len + 1] = '\0';
    }
    fprintf(stderr, "%s", buffer);
    fflush(stderr);
}

#endif

void PlatformLog(const char* level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    LogWithPrefix(level, format, args);
    va_end(args);
}

} // namespace

#undef NEXT_PLATFORM_PRINTF_FORMAT

bool PlatformInitialize() {
#if defined(_WIN32)
    // Set up crash handler
    SetUnhandledExceptionFilter(CrashHandler);
    _set_purecall_handler([]() {
        PlatformLog("[FATAL]", "Pure virtual function call!");
        std::abort();
    });
    _set_invalid_parameter_handler([](const wchar_t*,
                                     const wchar_t*,
                                     const wchar_t*,
                                     unsigned int,
                                     uintptr_t) {
        PlatformLog("[FATAL]", "Invalid parameter detected");
        std::abort();
    });
    PlatformLog("[INFO]", "Platform crash handlers initialized");
#else
    PlatformLog("[INFO]", "Platform initialization completed (non-Windows)");
#endif
    return true;
}

void PlatformShutdown() {
    PlatformLog("[INFO]", "Platform shutting down");
}

const char* GetPlatformName() {
#if defined(_WIN32)
    return "Windows";
#elif defined(__APPLE__)
    return "macOS";
#else
    return "Unknown";
#endif
}

double GetTimeInSeconds() {
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration<double>(now - startTime).count();
}

void SleepMs(uint32_t milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}

} // namespace Next
