#include "next/terminal/nvim_surface.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#define popen _popen
#define pclose _pclose
#endif

namespace {

struct DemoOptions {
    std::string policyPath = "tools/nvim_surface_probe/sample_policy.py";
    std::string snapshotPath;
    bool useNvim = true;
    bool cleanNvim = true;
    uint32_t width = 88;
    uint32_t height = 20;
};

struct CommandResult {
    int exitCode = 0;
    std::string output;
};

std::string QuoteShellArg(const std::string& value) {
#ifdef _WIN32
    std::string quoted = "\"";
    for (char ch : value) {
        if (ch == '"' || ch == '\\') {
            quoted += '\\';
        }
        quoted += ch;
    }
    quoted += "\"";
    return quoted;
#else
    std::string quoted = "'";
    for (char ch : value) {
        if (ch == '\'') {
            quoted += "'\\''";
        } else {
            quoted += ch;
        }
    }
    quoted += "'";
    return quoted;
#endif
}

bool ReadValue(int& index, int argc, char** argv, std::string& out) {
    if (index + 1 >= argc) {
        return false;
    }
    out = argv[++index];
    return true;
}

bool ReadValue(int& index, int argc, char** argv, uint32_t& out) {
    std::string value;
    if (!ReadValue(index, argc, argv, value)) {
        return false;
    }
    out = static_cast<uint32_t>(std::strtoul(value.c_str(), nullptr, 10));
    return out > 0;
}

void PrintUsage() {
    std::cerr
        << "Usage: hackops_demo [--policy path] [--snapshot path] [--no-nvim]\n"
        << "                    [--user-nvim] [--width columns] [--height rows]\n";
}

bool ParseArgs(int argc, char** argv, DemoOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--policy") {
            if (!ReadValue(i, argc, argv, options.policyPath)) return false;
        } else if (arg == "--snapshot") {
            if (!ReadValue(i, argc, argv, options.snapshotPath)) return false;
        } else if (arg == "--no-nvim") {
            options.useNvim = false;
        } else if (arg == "--user-nvim") {
            options.cleanNvim = false;
        } else if (arg == "--width") {
            if (!ReadValue(i, argc, argv, options.width)) return false;
        } else if (arg == "--height") {
            if (!ReadValue(i, argc, argv, options.height)) return false;
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else {
            return false;
        }
    }
    return true;
}

CommandResult RunCommand(const std::string& command) {
    CommandResult result;
    std::array<char, 256> buffer{};

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.exitCode = -1;
        result.output = "failed to launch command";
        return result;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result.output += buffer.data();
    }
    result.exitCode = pclose(pipe);
    return result;
}

CommandResult RunPolicy(const std::string& policyPath) {
#ifdef _WIN32
    const std::string command = "python " + QuoteShellArg(policyPath) + " 2>&1";
#else
    const std::string command = "python3 " + QuoteShellArg(policyPath) + " 2>&1";
#endif
    return RunCommand(command);
}

bool ParseScore(const std::string& text, double& score) {
    std::istringstream in(text);
    in >> score;
    return !in.fail();
}

std::string RouteStateForScore(double score) {
    if (score <= 22.0) {
        return "released: courier route is green, camera windows stay open";
    }
    if (score <= 28.0) {
        return "review: dispatcher asks for one more camera-safe route";
    }
    return "held: order is blocked until policy risk drops";
}

bool CapturePolicySurface(const DemoOptions& options) {
    if (!options.useNvim) {
        return true;
    }

    Next::NvimSurfaceConfig config;
    config.filePath = options.policyPath;
    config.width = options.width;
    config.height = options.height;
    config.loadUserConfig = !options.cleanNvim;

    Next::NvimSurface surface;
    if (!surface.Start(config)) {
        std::cerr << "nvim surface failed: " << surface.LastError() << "\n";
        return false;
    }

    surface.Pump(std::chrono::milliseconds(250));
    const std::string text = surface.Snapshot().ToPlainText();
    surface.Shutdown();

    if (!options.snapshotPath.empty()) {
        std::ofstream out(options.snapshotPath);
        if (!out) {
            std::cerr << "failed to open snapshot: " << options.snapshotPath << "\n";
            return false;
        }
        out << text;
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    DemoOptions options;
    if (!ParseArgs(argc, argv, options)) {
        PrintUsage();
        return 2;
    }

    std::cout << "HackOps demo: maintenance-window policy loop\n";
    std::cout << "policy=" << options.policyPath << "\n";

    if (!CapturePolicySurface(options)) {
        return 1;
    }

    const CommandResult policy = RunPolicy(options.policyPath);
    if (policy.exitCode != 0) {
        std::cerr << "policy execution failed:\n" << policy.output;
        return 1;
    }

    double score = 0.0;
    if (!ParseScore(policy.output, score)) {
        std::cerr << "policy did not print a numeric score:\n" << policy.output;
        return 1;
    }

    std::cout << "score=" << score << "\n";
    std::cout << "world.order_state=" << RouteStateForScore(score) << "\n";
    if (!options.snapshotPath.empty()) {
        std::cout << "terminal.snapshot=" << options.snapshotPath << "\n";
    }
    std::cout << "HACKOPS_DEMO_OK\n";
    return 0;
}
