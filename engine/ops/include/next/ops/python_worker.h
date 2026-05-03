#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace Next {

class OpsWorkspace;

struct PythonWorkerConfig {
    std::string pythonExecutable;
    std::string workingDirectory;
    std::string scriptPath = "policy.py";
    std::vector<std::string> arguments;
    std::chrono::milliseconds timeout{5000};
};

struct PythonWorkerResult {
    int exitCode = -1;
    bool timedOut = false;
    int64_t durationMs = 0;
    std::string stdoutText;
    std::string stderrText;

    bool Succeeded() const { return !timedOut && exitCode == 0; }
};

class PythonWorker {
public:
    bool RunPolicy(const OpsWorkspace& workspace, PythonWorkerResult* outResult);
    bool Run(const PythonWorkerConfig& config, PythonWorkerResult* outResult);

    const std::string& LastError() const { return lastError_; }

private:
    void SetError(const std::string& message) const;

    mutable std::string lastError_;
};

std::string DefaultPythonExecutable();

} // namespace Next

