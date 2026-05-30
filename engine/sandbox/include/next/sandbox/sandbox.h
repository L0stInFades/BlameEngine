#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "next/gameapi/abi.h"
#include "next/gameapi/capability.h"

// Player-code sandbox: a security boundary, not a particular VM (ADR-0008). The contract lives
// here on the interface; concrete backends (the reference bytecode VM today, WASM later) sit
// behind ISandbox. Five red lines hold for every backend:
//   1. zero ambient authority  — a guest can only reach the host via host-calls
//   2. capability-gated host-calls — the host surface IS the Game API, filtered by CapabilitySet
//   3. fuel metering           — CPU is bounded by a deterministic instruction count, not wall time
//   4. memory cap              — guest memory is a fixed arena; overflow traps
//   5. fault isolation         — any guest trap aborts only the guest, never the host

namespace Next::sandbox {

// Why a guest run stopped. None == it returned/halted cleanly; everything else is a trap that
// aborts the guest. Traps never propagate into the host as exceptions or corruption.
enum class TrapReason : int32_t {
    None = 0,            // clean Halt / Ret from the top frame
    FuelExhausted,       // instruction budget spent
    OutOfMemory,         // memory arena could not satisfy a request (reserved; v1 arenas are fixed)
    BadMemoryAccess,     // load/store or host-call pointer outside the arena
    IllegalInstruction,  // unknown opcode, truncated operand, or out-of-range branch/local
    StackOverflow,       // operand or call stack exceeded its cap
    StackUnderflow,      // pop on an empty operand stack
    DivideByZero,        // integer div/mod by zero
    HostCallDenied,      // per-run host-call budget exhausted
    HostCallError,       // gateway reported an unrecoverable host-side error
};

const char* ToString(TrapReason reason);

struct RunResult {
    TrapReason trap = TrapReason::None;
    uint64_t fuelUsed = 0;
    uint64_t hostCalls = 0;
    int64_t ret = 0;  // top-of-stack at clean halt (0 if the stack was empty)

    bool Ok() const { return trap == TrapReason::None; }
};

// The safety budget granted to one guest run. Every field is a hard ceiling.
struct SandboxPolicy {
    uint64_t fuel = 1'000'000;            // instructions before FuelExhausted
    uint32_t memoryBytes = 64 * 1024;     // linear memory arena size
    uint32_t stackSlots = 1024;           // operand stack depth cap
    uint32_t callDepth = 64;              // call stack depth cap
    uint64_t maxHostCalls = 4096;         // host-calls before HostCallDenied
    gameapi::CapabilitySet capabilities;  // which Game API domains the guest may reach
};

// The single, mediated exit from the sandbox. A backend hands over the guest's (already
// bounds-checked) memory and the requested arg/ret windows; the gateway re-checks the
// capability (defense in depth) and performs the Game API call. Returning a non-Ok Status is
// normal — it is delivered to the guest as a value, NOT a trap (a guest may handle a denied or
// invalid call). Only structural violations (bad pointers, exhausted budgets) trap, and those
// are detected by the backend, not here.
struct HostGateway {
    virtual ~HostGateway() = default;

    virtual gameapi::Status Invoke(gameapi::CallId id, uint8_t* memoryBase, uint32_t memorySize, uint32_t argsOffset,
                                   uint32_t argsLen, uint32_t retOffset, uint32_t retLen,
                                   const gameapi::CapabilitySet& granted) = 0;
};

// A loadable, runnable guest. Backends implement this; callers never see backend internals.
struct ISandbox {
    virtual ~ISandbox() = default;

    // Validate and load a module image. Returns false and fills `error` on malformed input.
    virtual bool LoadModule(const uint8_t* image, size_t size, std::string* error) = 0;

    // Run from `entryPc` (a byte offset into the loaded code) with `arg` placed on the operand
    // stack. Never throws (red line #5): any fault — including allocation failure — is reported as
    // a TrapReason. `noexcept` makes the contract compiler-enforced for every backend.
    virtual RunResult Run(const SandboxPolicy& policy, HostGateway& gateway, uint32_t entryPc,
                          int64_t arg) noexcept = 0;
};

}  // namespace Next::sandbox
