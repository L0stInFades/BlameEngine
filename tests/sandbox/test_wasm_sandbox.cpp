// WASM backend security-contract tests (ADR-0011 + B2 fix). These prove the SandboxPolicy red
// lines hold for the wasm3 backend the same way test_ref_vm proves them for the reference VM:
//   * memoryBytes is a hard cap — a module declaring more initial memory than the policy is
//     rejected up front (deterministic OutOfMemory) and the host never allocates past the cap;
//   * memory.grow bombs stay bounded (the run ends in a trap, not host OOM);
//   * unbounded recursion traps StackOverflow via the callDepth/stackSlots stack budget;
//   * a well-behaved guest still runs and returns its value.
// The guest modules are hand-encoded wasm binaries, so the suite needs NO wasm toolchain and can
// run in CI (including under ASan/UBSan, closing the "WASM backend outside the sanitizer gate"
// completeness hole).

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "next/sandbox/ref_vm.h"  // kMaxArenaBytes
#include "next/sandbox/sandbox.h"
#include "next/sandbox/wasm_sandbox.h"

using namespace Next;
using namespace Next::sandbox;

namespace {

// A gateway that denies everything: none of these guests import host_call, so it must never fire.
struct DenyAllGateway final : HostGateway {
    gameapi::Status Invoke(gameapi::CallId, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                           const gameapi::CapabilitySet&) override {
        return gameapi::Status::PermissionDenied;
    }
};

// Hand-encoded wasm32 modules. Shared layout: one func type (i32)->(i32), one function, a memory,
// one (dummy) mutable i32 global so the gas meter has a global section to extend, and an exported
// "run". Only the memory limits and the code body differ per guest.
std::vector<uint8_t> BuildModule(const std::vector<uint8_t>& memorySection, const std::vector<uint8_t>& codeBody) {
    std::vector<uint8_t> m = {
        0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00,  // \0asm, version 1
        0x01, 0x06, 0x01, 0x60, 0x01, 0x7F, 0x01, 0x7F,  // type: (i32) -> (i32)
        0x03, 0x02, 0x01, 0x00,                          // function: 1 func of type 0
    };
    m.insert(m.end(), memorySection.begin(), memorySection.end());
    const std::vector<uint8_t> globalAndExport = {
        0x06, 0x06, 0x01, 0x7F, 0x01, 0x41, 0x00, 0x0B,        // global: 1 mutable i32 = 0
        0x07, 0x07, 0x01, 0x03, 0x72, 0x75, 0x6E, 0x00, 0x00,  // export: "run" = func 0
    };
    m.insert(m.end(), globalAndExport.begin(), globalAndExport.end());
    m.push_back(0x0A);  // code section id
    m.push_back(static_cast<uint8_t>(codeBody.size() + 2));
    m.push_back(0x01);  // one body
    m.push_back(static_cast<uint8_t>(codeBody.size()));
    m.insert(m.end(), codeBody.begin(), codeBody.end());
    return m;
}

const std::vector<uint8_t> kOnePageMemory = {0x05, 0x03, 0x01, 0x00, 0x01};         // (memory 1)
const std::vector<uint8_t> k4096PageMemory = {0x05, 0x04, 0x01, 0x00, 0x80, 0x20};  // (memory 4096) = 256 MiB
const std::vector<uint8_t> kEchoBody = {0x00, 0x20, 0x00, 0x0B};                    // return arg
const std::vector<uint8_t> kRecursionBody = {0x00, 0x20, 0x00, 0x10, 0x00, 0x0B};   // run(arg) = run(arg)
const std::vector<uint8_t> kGrowBombBody = {
    0x00,        // no locals
    0x03, 0x40,  // loop (empty result)
    0x41, 0x01,  //   i32.const 1
    0x40, 0x00,  //   memory.grow
    0x1A,        //   drop
    0x0C, 0x00,  //   br 0 (back to the loop head)
    0x0B,        // end loop
    0x41, 0x00,  // i32.const 0 (unreachable epilogue keeps the validator happy)
    0x0B,        // end function
};

SandboxPolicy BasePolicy() {
    SandboxPolicy p;
    p.fuel = 1'000'000;
    p.memoryBytes = 64 * 1024;  // exactly the one declared page
    p.stackSlots = 1024;
    p.callDepth = 64;
    p.maxHostCalls = 16;
    return p;
}

RunResult LoadAndRun(const std::vector<uint8_t>& module, const SandboxPolicy& policy, int64_t arg = 7) {
    Wasm3Sandbox vm;
    std::string err;
    EXPECT_TRUE(vm.LoadModule(module.data(), module.size(), &err)) << err;
    DenyAllGateway gateway;
    return vm.Run(policy, gateway, 0, arg);
}

}  // namespace

TEST(WasmSandbox, WellBehavedGuestRunsWithinPolicy) {
    const RunResult r = LoadAndRun(BuildModule(kOnePageMemory, kEchoBody), BasePolicy(), 41);
    EXPECT_EQ(r.trap, TrapReason::None);
    EXPECT_EQ(r.ret, 41);
    EXPECT_GT(r.fuelUsed, 0u);  // the gas meter charged the function entry
}

TEST(WasmSandbox, DeclaredMemoryBeyondPolicyIsRejectedOutOfMemory) {
    // B2: a 256 MiB memory bomb against a 64 KiB policy. The wasm3 memoryLimit keeps the actual
    // host allocation under the cap, and the declared-pages check rejects the run outright.
    const RunResult r = LoadAndRun(BuildModule(k4096PageMemory, kEchoBody), BasePolicy());
    EXPECT_EQ(r.trap, TrapReason::OutOfMemory);
    EXPECT_EQ(r.hostCalls, 0u);
}

TEST(WasmSandbox, ZeroByteMemoryPolicyRejectsAnyDeclaredMemory) {
    SandboxPolicy p = BasePolicy();
    p.memoryBytes = 0;
    const RunResult r = LoadAndRun(BuildModule(kOnePageMemory, kEchoBody), p);
    EXPECT_EQ(r.trap, TrapReason::OutOfMemory);
}

TEST(WasmSandbox, ArenaBeyondRedLineIsRejectedLikeRefVm) {
    SandboxPolicy p = BasePolicy();
    p.memoryBytes = kMaxArenaBytes + 1;
    const RunResult r = LoadAndRun(BuildModule(kOnePageMemory, kEchoBody), p);
    EXPECT_EQ(r.trap, TrapReason::OutOfMemory);
}

TEST(WasmSandbox, MemoryGrowBombStaysBoundedAndTraps) {
    // B2: an infinite memory.grow loop. memoryLimit caps what wasm3 ever allocates, so the loop
    // just burns fuel until the meter traps — bounded CPU, bounded memory, live host.
    SandboxPolicy p = BasePolicy();
    p.fuel = 200'000;
    const RunResult r = LoadAndRun(BuildModule(kOnePageMemory, kGrowBombBody), p);
    EXPECT_EQ(r.trap, TrapReason::FuelExhausted);
    EXPECT_EQ(r.fuelUsed, p.fuel);
}

TEST(WasmSandbox, UnboundedRecursionTrapsStackOverflow) {
    // B2: callDepth bounds the interpreter stack budget — runaway recursion traps StackOverflow
    // deterministically instead of growing host memory (and before the ample fuel runs out).
    SandboxPolicy p = BasePolicy();
    p.fuel = 100'000'000;
    p.stackSlots = 65536;  // generous operand stack...
    p.callDepth = 4;       // ...but a tight frame budget binds first
    const RunResult r = LoadAndRun(BuildModule(kOnePageMemory, kRecursionBody), p);
    EXPECT_EQ(r.trap, TrapReason::StackOverflow);
    EXPECT_LT(r.fuelUsed, p.fuel);
}
