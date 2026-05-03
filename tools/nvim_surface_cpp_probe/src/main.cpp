#include "next/terminal/nvim_surface.h"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

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
        << "Usage: next_nvim_surface_probe [--file path] [--snapshot path] [--input keys]\n"
        << "                               [--width columns] [--height rows] [--clean]\n";
}

} // namespace

int main(int argc, char** argv) {
    Next::NvimSurfaceConfig config;
    std::string snapshotPath;
    std::string input;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--file") {
            if (!ReadValue(i, argc, argv, config.filePath)) {
                PrintUsage();
                return 2;
            }
        } else if (arg == "--snapshot") {
            if (!ReadValue(i, argc, argv, snapshotPath)) {
                PrintUsage();
                return 2;
            }
        } else if (arg == "--input") {
            if (!ReadValue(i, argc, argv, input)) {
                PrintUsage();
                return 2;
            }
        } else if (arg == "--width") {
            if (!ReadValue(i, argc, argv, config.width)) {
                PrintUsage();
                return 2;
            }
        } else if (arg == "--height") {
            if (!ReadValue(i, argc, argv, config.height)) {
                PrintUsage();
                return 2;
            }
        } else if (arg == "--clean") {
            config.loadUserConfig = false;
        } else {
            PrintUsage();
            return 2;
        }
    }

    Next::NvimSurface surface;
    if (!surface.Start(config)) {
        std::cerr << surface.LastError() << "\n";
        return 1;
    }

    if (!input.empty() && !surface.SendInput(input)) {
        std::cerr << surface.LastError() << "\n";
        return 1;
    }

    const auto text = surface.Snapshot().ToPlainText();
    if (!snapshotPath.empty()) {
        std::ofstream out(snapshotPath);
        if (!out) {
            std::cerr << "failed to open snapshot: " << snapshotPath << "\n";
            return 1;
        }
        out << text;
    } else {
        std::cout << text;
    }

    surface.Shutdown();
    return 0;
}
