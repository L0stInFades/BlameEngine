#include "next/ops/python_worker.h"

#include "next/ops/ops_workspace.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <sstream>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Next {
namespace {

using Clock = std::chrono::steady_clock;

std::vector<std::string> BuildPythonArgs(const PythonWorkerConfig& config) {
    std::vector<std::string> args;
    args.push_back(config.pythonExecutable.empty() ? DefaultPythonExecutable() : config.pythonExecutable);
    args.push_back(config.scriptPath.empty() ? "policy.py" : config.scriptPath);
    args.insert(args.end(), config.arguments.begin(), config.arguments.end());
    return args;
}

#ifdef _WIN32

std::string LastWin32Error() {
    const DWORD code = GetLastError();
    if (code == 0) {
        return "unknown error";
    }

    char* message = nullptr;
    const DWORD length = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message),
        0,
        nullptr);

    std::string out = length > 0 && message ? std::string(message, length) : std::to_string(code);
    if (message) {
        LocalFree(message);
    }
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return out;
}

void CloseHandleIfValid(HANDLE& handle) {
    if (handle && handle != INVALID_HANDLE_VALUE) {
        CloseHandle(handle);
    }
    handle = nullptr;
}

std::string QuoteWindowsArg(const std::string& arg) {
    if (arg.empty()) {
        return "\"\"";
    }

    const bool needsQuote = arg.find_first_of(" \t\"") != std::string::npos;
    if (!needsQuote) {
        return arg;
    }

    std::string out = "\"";
    size_t backslashes = 0;
    for (const char ch : arg) {
        if (ch == '\\') {
            ++backslashes;
        } else if (ch == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back(ch);
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            backslashes = 0;
            out.push_back(ch);
        }
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

std::string BuildWindowsCommandLine(const std::vector<std::string>& args) {
    std::string out;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            out.push_back(' ');
        }
        out += QuoteWindowsArg(args[i]);
    }
    return out;
}

void DrainPipe(HANDLE pipe, std::string& out) {
    while (pipe) {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0) {
            return;
        }

        std::vector<char> buffer(std::min<DWORD>(available, 65536));
        DWORD bytesRead = 0;
        if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) || bytesRead == 0) {
            return;
        }
        out.append(buffer.data(), bytesRead);
    }
}

bool RunProcessWindows(const PythonWorkerConfig& config, PythonWorkerResult& result, std::string& error) {
    SECURITY_ATTRIBUTES securityAttributes;
    securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAttributes.lpSecurityDescriptor = nullptr;
    securityAttributes.bInheritHandle = TRUE;

    HANDLE stdoutRead = nullptr;
    HANDLE stdoutWrite = nullptr;
    HANDLE stderrRead = nullptr;
    HANDLE stderrWrite = nullptr;
    HANDLE stdinRead = nullptr;

    if (!CreatePipe(&stdoutRead, &stdoutWrite, &securityAttributes, 0) ||
        !CreatePipe(&stderrRead, &stderrWrite, &securityAttributes, 0)) {
        error = "CreatePipe failed: " + LastWin32Error();
        CloseHandleIfValid(stdoutRead);
        CloseHandleIfValid(stdoutWrite);
        CloseHandleIfValid(stderrRead);
        CloseHandleIfValid(stderrWrite);
        return false;
    }

    if (!SetHandleInformation(stdoutRead, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(stderrRead, HANDLE_FLAG_INHERIT, 0)) {
        error = "SetHandleInformation failed: " + LastWin32Error();
        CloseHandleIfValid(stdoutRead);
        CloseHandleIfValid(stdoutWrite);
        CloseHandleIfValid(stderrRead);
        CloseHandleIfValid(stderrWrite);
        return false;
    }

    stdinRead = CreateFileA(
        "NUL",
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &securityAttributes,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (stdinRead == INVALID_HANDLE_VALUE) {
        stdinRead = nullptr;
    }

    STARTUPINFOA startupInfo;
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = stdinRead ? stdinRead : GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = stdoutWrite;
    startupInfo.hStdError = stderrWrite;

    PROCESS_INFORMATION processInfo;
    ZeroMemory(&processInfo, sizeof(processInfo));

    std::string commandLine = BuildWindowsCommandLine(BuildPythonArgs(config));
    std::vector<char> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back('\0');

    const BOOL started = CreateProcessA(
        nullptr,
        commandLineBuffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        config.workingDirectory.empty() ? nullptr : config.workingDirectory.c_str(),
        &startupInfo,
        &processInfo);

    CloseHandleIfValid(stdoutWrite);
    CloseHandleIfValid(stderrWrite);
    CloseHandleIfValid(stdinRead);

    if (!started) {
        error = "CreateProcess failed: " + LastWin32Error();
        CloseHandleIfValid(stdoutRead);
        CloseHandleIfValid(stderrRead);
        return false;
    }

    const auto start = Clock::now();
    const auto deadline = start + config.timeout;
    DWORD waitResult = WAIT_TIMEOUT;
    while (Clock::now() < deadline) {
        DrainPipe(stdoutRead, result.stdoutText);
        DrainPipe(stderrRead, result.stderrText);
        waitResult = WaitForSingleObject(processInfo.hProcess, 10);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
    }

    if (waitResult != WAIT_OBJECT_0) {
        result.timedOut = true;
        TerminateProcess(processInfo.hProcess, 124);
        WaitForSingleObject(processInfo.hProcess, 1000);
    }

    DrainPipe(stdoutRead, result.stdoutText);
    DrainPipe(stderrRead, result.stderrText);

    DWORD exitCode = 1;
    if (GetExitCodeProcess(processInfo.hProcess, &exitCode)) {
        result.exitCode = static_cast<int>(exitCode);
    }

    result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();

    CloseHandleIfValid(processInfo.hThread);
    CloseHandleIfValid(processInfo.hProcess);
    CloseHandleIfValid(stdoutRead);
    CloseHandleIfValid(stderrRead);
    return true;
}

#else

void CloseFd(int& fd) {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

bool SetNonBlocking(int fd, std::string& error) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        error = "fcntl failed: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

void DrainFd(int& fd, std::string& out) {
    if (fd < 0) {
        return;
    }

    char buffer[65536];
    while (true) {
        const ssize_t bytesRead = read(fd, buffer, sizeof(buffer));
        if (bytesRead > 0) {
            out.append(buffer, static_cast<size_t>(bytesRead));
            continue;
        }
        if (bytesRead == 0) {
            CloseFd(fd);
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        CloseFd(fd);
        return;
    }
}

bool RunProcessPosix(const PythonWorkerConfig& config, PythonWorkerResult& result, std::string& error) {
    int stdoutPipe[2] = {-1, -1};
    int stderrPipe[2] = {-1, -1};
    if (pipe(stdoutPipe) != 0 || pipe(stderrPipe) != 0) {
        error = "pipe failed: " + std::string(std::strerror(errno));
        CloseFd(stdoutPipe[0]);
        CloseFd(stdoutPipe[1]);
        CloseFd(stderrPipe[0]);
        CloseFd(stderrPipe[1]);
        return false;
    }

    const auto args = BuildPythonArgs(config);
    const pid_t child = fork();
    if (child < 0) {
        error = "fork failed: " + std::string(std::strerror(errno));
        CloseFd(stdoutPipe[0]);
        CloseFd(stdoutPipe[1]);
        CloseFd(stderrPipe[0]);
        CloseFd(stderrPipe[1]);
        return false;
    }

    if (child == 0) {
        dup2(stdoutPipe[1], STDOUT_FILENO);
        dup2(stderrPipe[1], STDERR_FILENO);
        CloseFd(stdoutPipe[0]);
        CloseFd(stdoutPipe[1]);
        CloseFd(stderrPipe[0]);
        CloseFd(stderrPipe[1]);

        if (!config.workingDirectory.empty() && chdir(config.workingDirectory.c_str()) != 0) {
            const std::string message = "chdir failed: " + std::string(std::strerror(errno)) + "\n";
            write(STDERR_FILENO, message.data(), message.size());
            _exit(126);
        }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(argv[0], argv.data());
        const std::string message = "execvp failed: " + std::string(std::strerror(errno)) + "\n";
        write(STDERR_FILENO, message.data(), message.size());
        _exit(127);
    }

    CloseFd(stdoutPipe[1]);
    CloseFd(stderrPipe[1]);
    if (!SetNonBlocking(stdoutPipe[0], error) || !SetNonBlocking(stderrPipe[0], error)) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        CloseFd(stdoutPipe[0]);
        CloseFd(stderrPipe[0]);
        return false;
    }

    const auto start = Clock::now();
    const auto deadline = start + config.timeout;
    bool childExited = false;
    int status = 0;

    while (stdoutPipe[0] >= 0 || stderrPipe[0] >= 0 || !childExited) {
        if (!childExited) {
            const pid_t waited = waitpid(child, &status, WNOHANG);
            if (waited == child) {
                childExited = true;
                if (WIFEXITED(status)) {
                    result.exitCode = WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    result.exitCode = 128 + WTERMSIG(status);
                }
            }
        }

        if (!childExited && Clock::now() >= deadline) {
            result.timedOut = true;
            kill(child, SIGKILL);
            waitpid(child, &status, 0);
            childExited = true;
            result.exitCode = 124;
        }

        fd_set readSet;
        FD_ZERO(&readSet);
        int maxFd = -1;
        if (stdoutPipe[0] >= 0) {
            FD_SET(stdoutPipe[0], &readSet);
            maxFd = std::max(maxFd, stdoutPipe[0]);
        }
        if (stderrPipe[0] >= 0) {
            FD_SET(stderrPipe[0], &readSet);
            maxFd = std::max(maxFd, stderrPipe[0]);
        }

        if (maxFd >= 0) {
            timeval tv;
            tv.tv_sec = 0;
            tv.tv_usec = 10000;
            const int ready = select(maxFd + 1, &readSet, nullptr, nullptr, &tv);
            if (ready < 0 && errno != EINTR) {
                error = "select failed: " + std::string(std::strerror(errno));
                kill(child, SIGKILL);
                waitpid(child, nullptr, 0);
                CloseFd(stdoutPipe[0]);
                CloseFd(stderrPipe[0]);
                return false;
            }
        }

        DrainFd(stdoutPipe[0], result.stdoutText);
        DrainFd(stderrPipe[0], result.stderrText);

        if (childExited && stdoutPipe[0] < 0 && stderrPipe[0] < 0) {
            break;
        }
    }

    result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count();
    return true;
}

#endif

} // namespace

bool PythonWorker::RunPolicy(const OpsWorkspace& workspace, PythonWorkerResult* outResult) {
    PythonWorkerConfig config;
    config.workingDirectory = workspace.RootPath();
    config.scriptPath = "policy.py";
    return Run(config, outResult);
}

bool PythonWorker::Run(const PythonWorkerConfig& config, PythonWorkerResult* outResult) {
    lastError_.clear();
    if (config.workingDirectory.empty()) {
        SetError("working directory is empty");
        return false;
    }
    if (!outResult) {
        SetError("outResult is null");
        return false;
    }

    PythonWorkerResult result;
    bool ok = false;
#ifdef _WIN32
    ok = RunProcessWindows(config, result, lastError_);
#else
    ok = RunProcessPosix(config, result, lastError_);
#endif
    *outResult = std::move(result);
    if (!ok) {
        return false;
    }
    if (outResult->timedOut) {
        SetError("python worker timed out");
    } else if (outResult->exitCode != 0) {
        SetError("python worker exited with code " + std::to_string(outResult->exitCode));
    }
    return true;
}

void PythonWorker::SetError(const std::string& message) const {
    lastError_ = message;
}

std::string DefaultPythonExecutable() {
#ifdef _WIN32
    return "python";
#else
    return "python3";
#endif
}

} // namespace Next

