#include "next/sandbox/wasm_sandbox.h"

#include <cstring>

#include "next/sandbox/ref_vm.h"  // kMaxArenaBytes: the same arena red line as the reference VM
#include "next/sandbox/wasm_meter.h"
#include "m3_env.h"  // wasm3 internals: runtime->memoryLimit (B2 byte cap) + memory.numPages
#include "wasm3.h"

namespace Next::sandbox {
namespace {

// Per-run context handed to the host-call trampoline via wasm3's userdata. Carries the gateway
// (the one mediated exit), the granted capabilities, the host-call budget, and a slot to record a
// structural trap (bad pointer / budget) so Run() can surface the precise TrapReason — wasm3 only
// gives us an error string back from m3_Call.
struct HostContext {
    HostGateway* gateway = nullptr;
    gameapi::CapabilitySet caps;
    uint64_t hostCalls = 0;
    uint64_t maxHostCalls = 0;
    TrapReason structuralTrap = TrapReason::None;
};

// host_call(i32 callId, i32 argsOff, i32 argsLen, i32 retOff, i32 retLen) -> i32 status.
// The guest's offsets index its own linear memory; we re-derive base+size from the runtime and
// apply the SAME window bounds checks as RefVm before handing the (now host) pointers to the
// gateway. Any structural violation traps the guest (never corrupts the host).
m3ApiRawFunction(HostCallTrampoline) {
    m3ApiReturnType(int32_t);
    m3ApiGetArg(int32_t, callId);
    m3ApiGetArg(int32_t, argsOff);
    m3ApiGetArg(int32_t, argsLen);
    m3ApiGetArg(int32_t, retOff);
    m3ApiGetArg(int32_t, retLen);

    HostContext* ctx = static_cast<HostContext*>(_ctx->userdata);

    uint32_t memSize = 0;
    uint8_t* base = m3_GetMemory(runtime, &memSize, 0);
    if (base == nullptr) {
        ctx->structuralTrap = TrapReason::BadMemoryAccess;
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    }

    // Negative i32 -> huge u64, caught by the `> memSize` guard (same shape as ref_vm.cpp).
    const uint64_t aOff = static_cast<uint32_t>(argsOff);
    const uint64_t aLen = static_cast<uint32_t>(argsLen);
    const uint64_t rOff = static_cast<uint32_t>(retOff);
    const uint64_t rLen = static_cast<uint32_t>(retLen);
    if (aLen > 0 && (aLen > memSize || aOff > memSize - aLen)) {
        ctx->structuralTrap = TrapReason::BadMemoryAccess;
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    }
    if (rLen > 0 && (rLen > memSize || rOff > memSize - rLen)) {
        ctx->structuralTrap = TrapReason::BadMemoryAccess;
        m3ApiTrap(m3Err_trapOutOfBoundsMemoryAccess);
    }
    if (ctx->hostCalls >= ctx->maxHostCalls) {
        ctx->structuralTrap = TrapReason::HostCallDenied;
        m3ApiTrap(m3Err_trapAbort);
    }
    ++ctx->hostCalls;

    const gameapi::Status st = ctx->gateway->Invoke(
        static_cast<gameapi::CallId>(callId), base, memSize, static_cast<uint32_t>(aOff), static_cast<uint32_t>(aLen),
        static_cast<uint32_t>(rOff), static_cast<uint32_t>(rLen), ctx->caps);
    m3ApiReturn(static_cast<int32_t>(st));
}

// Map a wasm3 error string to a TrapReason for guest-originated traps (a structural trap recorded
// in HostContext takes precedence and is handled by the caller).
TrapReason MapM3Error(M3Result r) {
    if (r == m3Err_none) {
        return TrapReason::None;
    }
    if (r == m3Err_trapOutOfBoundsMemoryAccess) {
        return TrapReason::BadMemoryAccess;
    }
    if (r == m3Err_trapDivisionByZero) {
        return TrapReason::DivideByZero;
    }
    if (r == m3Err_trapStackOverflow) {
        return TrapReason::StackOverflow;
    }
    // unreachable / indirect-call type mismatch / bad conversion / etc.
    return TrapReason::IllegalInstruction;
}

constexpr uint8_t kWasmMagic[4] = {0x00, 0x61, 0x73, 0x6D};  // "\0asm"

}  // namespace

bool Wasm3Sandbox::LoadModule(const uint8_t* image, size_t size, std::string* error) {
    auto fail = [&](const char* msg) {
        if (error != nullptr) {
            *error = msg;
        }
        return false;
    };
    if (image == nullptr || size < 8) {
        return fail("module too small for a WASM header");
    }
    if (std::memcmp(image, kWasmMagic, 4) != 0) {
        return fail("bad WASM magic (expected \\0asm)");
    }
    const uint32_t version = static_cast<uint32_t>(image[4]) | (static_cast<uint32_t>(image[5]) << 8) |
                             (static_cast<uint32_t>(image[6]) << 16) | (static_cast<uint32_t>(image[7]) << 24);
    if (version != 1) {
        return fail("unsupported WASM version");
    }
    // Make the module meter its own CPU: rewrite it to charge the exported __fuel global per block
    // and trap when it runs out (wasm3 has no native fuel hook). Fail-closed — a module the meter
    // cannot instrument (unknown/SIMD opcode, missing section) is rejected, never run un-metered.
    const std::vector<uint8_t> raw(image, image + size);
    std::string meterErr;
    if (!InstrumentWasmForFuel(raw, code_, &meterErr)) {
        code_.clear();
        return fail(meterErr.empty() ? "fuel instrumentation failed" : meterErr.c_str());
    }
    return true;
}

RunResult Wasm3Sandbox::Run(const SandboxPolicy& policy, HostGateway& gateway, uint32_t /*entryPc*/,
                            int64_t arg) noexcept {
    RunResult result;
    if (code_.empty()) {
        result.trap = TrapReason::IllegalInstruction;
        return result;
    }
    // B2 fix: reject an absurd arena up front (parity with RefVm's check) — deterministic, and
    // independent of whether the allocation would actually fail under overcommit.
    if (policy.memoryBytes > kMaxArenaBytes) {
        result.trap = TrapReason::OutOfMemory;
        return result;
    }

    // Fault isolation (ADR-0008 red line #5): nothing below may throw into the host.
    try {
        // wasm3 runs operands AND call frames on ONE interpreter stack, so both policy knobs bound
        // it (B2): stackSlots sizes it (16 bytes per slot, 16 KiB floor so real guests have room)
        // and callDepth caps it (2 KiB per allowed frame — a generous per-frame allowance, so the
        // cap binds on runaway recursion, not honest code). Recursion past the budget traps
        // StackOverflow deterministically instead of growing host memory.
        uint64_t stackBudget = policy.stackSlots > 1024 ? static_cast<uint64_t>(policy.stackSlots) * 16u : 16u * 1024u;
        const uint64_t depthBudget = static_cast<uint64_t>(policy.callDepth == 0 ? 1 : policy.callDepth) * 2048u;
        if (stackBudget > depthBudget) {
            stackBudget = depthBudget;
        }
        if (stackBudget < 4096u) {
            stackBudget = 4096u;  // floor: one frame + operands must fit even under callDepth=1
        }
        if (stackBudget > 8u * 1024u * 1024u) {
            stackBudget = 8u * 1024u * 1024u;  // ceiling: a huge stackSlots cannot OOM the host
        }
        const uint32_t stackBytes = static_cast<uint32_t>(stackBudget);

        IM3Environment env = m3_NewEnvironment();
        if (env == nullptr) {
            result.trap = TrapReason::OutOfMemory;
            return result;
        }
        IM3Runtime rt = m3_NewRuntime(env, stackBytes, nullptr);
        if (rt == nullptr) {
            m3_FreeEnvironment(env);
            result.trap = TrapReason::OutOfMemory;
            return result;
        }
        // B2 fix: hard-cap EVERY linear-memory allocation wasm3 makes for this guest — the
        // declared initial memory and every memory.grow — at the policy red line, so a
        // memory-bomb module cannot OOM the host. wasm3 treats memoryLimit==0 as "no limit",
        // so a zero-byte policy becomes a 1-byte cap (any module that declares memory is then
        // rejected by the declared-pages check below).
        rt->memoryLimit = policy.memoryBytes != 0 ? policy.memoryBytes : 1u;

        TrapReason trap = TrapReason::None;
        int64_t ret = 0;
        HostContext ctx;
        ctx.gateway = &gateway;
        ctx.caps = policy.capabilities;
        ctx.maxHostCalls = policy.maxHostCalls;

        IM3Module mod = nullptr;
        M3Result r = m3_ParseModule(env, &mod, code_.data(), static_cast<uint32_t>(code_.size()));
        if (r != m3Err_none) {
            trap = TrapReason::IllegalInstruction;  // malformed module
        } else if ((r = m3_LoadModule(rt, mod)) != m3Err_none) {
            // m3_LoadModule takes ownership of `mod` on success; on failure wasm3 frees it with the runtime.
            trap = TrapReason::IllegalInstruction;
        } else if (static_cast<uint64_t>(rt->memory.numPages) * d_m3MemPageSize > policy.memoryBytes) {
            // B2 fix: the module's DECLARED initial memory already exceeds the policy. Reject up
            // front with the same deterministic OutOfMemory a RefVm guest gets for an oversized
            // arena — not a confusing mid-run BadMemoryAccess at the first access past the cap.
            // (memoryLimit above already kept the actual host allocation under the cap.)
            trap = TrapReason::OutOfMemory;
        } else {
            // Link the single host import. A pure-compute guest may not import it; that lookup
            // failure is benign (the import simply isn't there to satisfy).
            r = m3_LinkRawFunctionEx(mod, "env", "host_call", "i(iiiii)", &HostCallTrampoline, &ctx);
            if (r != m3Err_none && r != m3Err_functionLookupFailed) {
                trap = TrapReason::HostCallError;
            } else {
                IM3Function fn = nullptr;
                r = m3_FindFunction(&fn, rt, "run");
                if (r != m3Err_none || fn == nullptr) {
                    trap = TrapReason::IllegalInstruction;  // no `run` export
                } else {
                    // Load the per-run fuel budget into the metered module's exported __fuel global.
                    // The gas instrumentation decrements it per block and traps `unreachable` at < 0.
                    IM3Global fuelG = m3_FindGlobal(mod, kFuelGlobalName);
                    if (fuelG != nullptr) {
                        M3TaggedValue v;
                        v.type = c_m3Type_i64;
                        v.value.i64 = policy.fuel;
                        m3_SetGlobal(fuelG, &v);
                    }
                    const int32_t a = static_cast<int32_t>(arg);
                    r = m3_CallV(fn, a);

                    // Recover the remainder (the global persists across a trap) -> exact fuelUsed.
                    int64_t remaining = static_cast<int64_t>(policy.fuel);
                    if (fuelG != nullptr) {
                        M3TaggedValue rv;
                        if (m3_GetGlobal(fuelG, &rv) == m3Err_none && rv.type == c_m3Type_i64) {
                            remaining = static_cast<int64_t>(rv.value.i64);
                        }
                    }
                    const int64_t budget = static_cast<int64_t>(policy.fuel);
                    const int64_t clamped = remaining < 0 ? 0 : (remaining > budget ? budget : remaining);
                    result.fuelUsed = static_cast<uint64_t>(budget - clamped);

                    if (r != m3Err_none) {
                        // Fuel exhaustion surfaces as an `unreachable` trap from __gas with the global
                        // driven below zero; report it precisely, else a structural trap, else map.
                        if (fuelG != nullptr && remaining <= 0) {
                            trap = TrapReason::FuelExhausted;
                        } else {
                            trap = (ctx.structuralTrap != TrapReason::None) ? ctx.structuralTrap : MapM3Error(r);
                        }
                    } else {
                        int32_t out = 0;
                        m3_GetResultsV(fn, &out);
                        ret = out;
                    }
                }
            }
        }

        result.trap = trap;
        result.ret = ret;
        result.hostCalls = ctx.hostCalls;

        m3_FreeRuntime(rt);  // also frees the loaded module
        m3_FreeEnvironment(env);
    } catch (...) {
        result.trap = TrapReason::HostCallError;
    }
    return result;
}

std::unique_ptr<ISandbox> MakeWasm3Sandbox() {
    return std::make_unique<Wasm3Sandbox>();
}

}  // namespace Next::sandbox
