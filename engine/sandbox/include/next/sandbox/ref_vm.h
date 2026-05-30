#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "next/sandbox/sandbox.h"

namespace Next::sandbox {

// Reference backend (ADR-0008): a deterministic, fuel-metered stack VM that proves the sandbox
// security contract and is fully headless-testable. It holds no ambient authority — the only
// way it can affect anything outside its own operand stack / locals / memory arena is a
// HostCall through the supplied HostGateway. A WASM backend (wasm3 / wasmtime) can later
// implement the same ISandbox without touching callers.
//
// Per-run state (stacks, memory) is local to Run(); the instance only retains the loaded code,
// so one loaded module can be Run() repeatedly with independent budgets.
class RefVm final : public ISandbox {
public:
    bool LoadModule(const uint8_t* image, size_t size, std::string* error) override;
    RunResult Run(const SandboxPolicy& policy, HostGateway& gateway, uint32_t entryPc, int64_t arg) noexcept override;

    uint32_t CodeSize() const { return static_cast<uint32_t>(code_.size()); }

private:
    std::vector<uint8_t> code_;  // module code (header stripped)
};

// Fuel charged per host-call (host-side work costs more than a plain instruction).
constexpr uint64_t kHostCallFuel = 50;

// Upper bound on a guest's linear-memory arena. A policy requesting more is rejected with an
// OutOfMemory trap at Run() entry — deterministic, and avoids relying on allocation failure (which
// virtual-memory overcommit can hide). 256 MiB is far beyond any sane player-code arena.
constexpr uint32_t kMaxArenaBytes = 256u * 1024u * 1024u;

}  // namespace Next::sandbox
