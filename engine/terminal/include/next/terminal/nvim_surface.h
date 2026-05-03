#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Next {

struct NvimCursor {
    uint32_t row = 0;
    uint32_t column = 0;
};

struct NvimCell {
    std::string text = " ";
    int64_t highlightId = 0;
};

struct NvimSurfaceConfig {
    std::string executable = "nvim";
    std::string filePath;
    uint32_t width = 100;
    uint32_t height = 32;
    bool loadUserConfig = true;
    std::chrono::milliseconds requestTimeout{5000};
};

struct NvimSurfaceSnapshot {
    uint32_t width = 0;
    uint32_t height = 0;
    NvimCursor cursor;
    std::vector<std::string> rows;

    std::string ToPlainText() const;
};

class NvimSurface {
public:
    NvimSurface();
    ~NvimSurface();

    NvimSurface(const NvimSurface&) = delete;
    NvimSurface& operator=(const NvimSurface&) = delete;

    bool Start(const NvimSurfaceConfig& config);
    void Shutdown();

    bool IsRunning() const;
    bool Pump(std::chrono::milliseconds duration);
    bool SendInput(const std::string& input);
    bool Command(const std::string& command);
    bool Resize(uint32_t width, uint32_t height);

    NvimSurfaceSnapshot Snapshot() const;
    const std::string& LastError() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

std::string ExpandNvimInputTokens(const std::string& input);

} // namespace Next
