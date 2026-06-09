#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "next/sandbox/sandbox.h"

// WebAssembly sandbox backend (ADR-0011): the player-LANGUAGE path. Where RefVm executes the
// bespoke NBVM bytecode, this backend executes real WASM compiled from modern C++ / Rust (or any
// language with a wasm32 target). It implements the SAME ISandbox contract and routes a guest's
// host-calls through the SAME HostGateway — so a guest written in C++ speaks the identical Game
// API ABI as a hand-assembled NBVM guest. Backed by wasm3 (a small, embeddable interpreter).
//
// Guest contract (the player-side ABI):
//   * export  `i32 run(i32 arg)`               — entry point; Run()'s `arg` is passed in, `ret`
//                                                 receives the return value. (entryPc is unused.)
//   * import  `env.host_call(i32 callId, i32 argsOff, i32 argsLen, i32 retOff, i32 retLen) -> i32`
//                                                 — the one mediated exit; offsets index the
//                                                   guest's own linear memory, exactly like the
//                                                   NBVM HostCall windows. Returns a gameapi::Status.
//
// This header is wasm3-free (like jolt_physics_world.h is Jolt-free); only wasm_sandbox.cpp
// includes wasm3. The library is OPTIONAL (BUILD_WITH_WASM); the headless core never needs it.

namespace Next::sandbox {

// Reference WASM backend. Per-run state (runtime, linear memory) is local to Run(); the instance
// only retains the loaded module bytes, so one loaded module can be Run() repeatedly in isolation.
//
// Security posture vs RefVm: memory safety (WASM bounds-checks every access), capability gating,
// and the per-run host-call budget all hold here. CPU fuel is also enforced — wasm3 has no native
// fuel hook, so LoadModule rewrites the module with load-time gas instrumentation (wasm_meter.h):
// it charges policy.fuel per metered block and traps FuelExhausted when spent, so an infinite loop
// is bounded, not a hang. RunResult::fuelUsed reports the exact amount consumed (ADR-0012).
//
// policy.memoryBytes is enforced (B2 fix): every linear-memory allocation wasm3 makes for the
// guest — declared initial memory and every memory.grow — is byte-capped at the policy, so a
// memory-bomb module cannot OOM the host; a module whose DECLARED initial memory exceeds the
// policy is rejected up front with a deterministic OutOfMemory (parity with RefVm's arena check).
// policy.callDepth is enforced through the interpreter stack budget (wasm3 keeps operands and
// call frames on one stack): the stack is sized from stackSlots and capped at callDepth frames'
// worth of space, so runaway recursion traps StackOverflow instead of growing host memory.
class Wasm3Sandbox final : public ISandbox {
public:
    bool LoadModule(const uint8_t* image, size_t size, std::string* error) override;
    RunResult Run(const SandboxPolicy& policy, HostGateway& gateway, uint32_t entryPc, int64_t arg) noexcept override;

private:
    std::vector<uint8_t> code_;  // the .wasm module bytes
};

// Construct the WASM backend. Available only when the library is built (BUILD_WITH_WASM).
std::unique_ptr<ISandbox> MakeWasm3Sandbox();

}  // namespace Next::sandbox
