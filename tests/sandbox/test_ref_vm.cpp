// Reference VM mechanics and trap behavior (ADR-0008). Programs are built with BytecodeBuilder,
// loaded, and run under explicit budgets. Every fault must surface as a TrapReason — never as a
// host crash, exception, or memory corruption (the suite runs clean under ASan/UBSan in CI).

#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "next/gameapi/abi.h"
#include "next/sandbox/bytecode.h"
#include "next/sandbox/ref_vm.h"
#include "next/sandbox/sandbox.h"

using namespace Next;
using namespace Next::sandbox;

namespace {

SandboxPolicy MakePolicy() {
    SandboxPolicy p;
    p.fuel = 1'000'000;
    p.memoryBytes = 4096;
    p.stackSlots = 256;
    p.callDepth = 16;
    p.maxHostCalls = 64;
    p.capabilities = gameapi::CapabilitySet::PlayerDefault();
    return p;
}

// A host gateway that records calls and always succeeds — isolates VM mechanics from the Game API.
struct RecordingGateway : HostGateway {
    int calls = 0;
    gameapi::CallId lastId = gameapi::CallId::GetTick;
    uint32_t lastArgsLen = 0;
    uint32_t lastRetLen = 0;

    gameapi::Status Invoke(gameapi::CallId id, uint8_t* /*mem*/, uint32_t /*memSize*/, uint32_t /*aOff*/,
                           uint32_t argsLen, uint32_t /*rOff*/, uint32_t retLen,
                           const gameapi::CapabilitySet& /*granted*/) override {
        ++calls;
        lastId = id;
        lastArgsLen = argsLen;
        lastRetLen = retLen;
        return gameapi::Status::Ok;
    }
};

// A gateway whose host-call throws — to exercise the VM's exception->trap fault isolation.
struct ThrowingGateway : HostGateway {
    gameapi::Status Invoke(gameapi::CallId, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                           const gameapi::CapabilitySet&) override {
        throw std::runtime_error("gateway boom");
    }
};

std::vector<uint8_t> MakeRawImage(const std::vector<uint8_t>& code) {
    std::vector<uint8_t> img = {'N', 'B', 'V', 'M'};
    const uint32_t ver = kModuleVersion;
    const uint32_t len = static_cast<uint32_t>(code.size());
    for (int i = 0; i < 4; ++i)
        img.push_back(static_cast<uint8_t>((ver >> (8 * i)) & 0xFF));
    for (int i = 0; i < 4; ++i)
        img.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
    img.insert(img.end(), code.begin(), code.end());
    return img;
}

RunResult RunProgram(const std::vector<uint8_t>& image, const SandboxPolicy& policy, HostGateway& gw, int64_t arg = 0) {
    RefVm vm;
    std::string err;
    EXPECT_TRUE(vm.LoadModule(image.data(), image.size(), &err)) << err;
    return vm.Run(policy, gw, 0, arg);
}

// ---- loading ----

TEST(RefVmLoad, RejectsBadMagic) {
    RefVm vm;
    std::vector<uint8_t> img = {'X', 'X', 'X', 'X', 1, 0, 0, 0, 0, 0, 0, 0};
    std::string err;
    EXPECT_FALSE(vm.LoadModule(img.data(), img.size(), &err));
    EXPECT_FALSE(err.empty());
}

TEST(RefVmLoad, RejectsLengthMismatch) {
    RefVm vm;
    auto img = MakeRawImage({static_cast<uint8_t>(Op::Halt)});
    img.push_back(0);  // extra trailing byte not counted in codeLen
    std::string err;
    EXPECT_FALSE(vm.LoadModule(img.data(), img.size(), &err));
}

// ---- arithmetic and stack ----

TEST(RefVmArith, AddsConstants) {
    BytecodeBuilder b;
    b.PushI(2).PushI(3).Emit(Op::Add).Emit(Op::Halt);
    RecordingGateway gw;
    const RunResult r = RunProgram(b.Build(), MakePolicy(), gw);
    EXPECT_EQ(r.trap, TrapReason::None);
    EXPECT_EQ(r.ret, 5);
}

TEST(RefVmArith, SubDivMod) {
    BytecodeBuilder b;
    b.PushI(20).PushI(6).Emit(Op::Sub);  // 14
    b.PushI(3).Emit(Op::Div);            // 14/3 = 4
    b.PushI(3).Emit(Op::Mod);            // 4%3 = 1
    b.Emit(Op::Halt);
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).ret, 1);
}

TEST(RefVmArith, FloatRoundTrip) {
    BytecodeBuilder b;
    b.PushF(1.5).PushF(2.25).Emit(Op::FAdd).Emit(Op::F2I).Emit(Op::Halt);  // 3.75 -> 3
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).ret, 3);
}

TEST(RefVmMemory, StoreThenLoadRoundTrips) {
    BytecodeBuilder b;
    b.PushI(16).PushI(0x1234'5678).Emit(Op::St32);  // mem[16..20) = 0x12345678
    b.PushI(16).Emit(Op::Ld32).Emit(Op::Halt);
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).ret, 0x1234'5678);
}

TEST(RefVmLocals, StoreAndLoad) {
    BytecodeBuilder b;
    b.PushI(7).StLoc(3);
    b.LdLoc(3).LdLoc(3).Emit(Op::Add).Emit(Op::Halt);  // 14
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).ret, 14);
}

TEST(RefVmControl, LoopSumsToTriangular) {
    BytecodeBuilder b;
    const auto loop = b.NewLabel();
    const auto end = b.NewLabel();
    b.StLoc(2);           // N = arg
    b.PushI(0).StLoc(1);  // acc = 0
    b.PushI(1).StLoc(0);  // i = 1
    b.Bind(loop);
    b.LdLoc(0).LdLoc(2).Emit(Op::Gts).Jnz(end);  // if i > N goto end
    b.LdLoc(1).LdLoc(0).Emit(Op::Add).StLoc(1);  // acc += i
    b.LdLoc(0).PushI(1).Emit(Op::Add).StLoc(0);  // i += 1
    b.Jmp(loop);
    b.Bind(end);
    b.LdLoc(1).Emit(Op::Halt);
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw, 10).ret, 55);
}

TEST(RefVmControl, CallAndReturn) {
    BytecodeBuilder b;
    const auto addOne = b.NewLabel();
    b.PushI(41).Call(addOne).Emit(Op::Halt);
    b.Bind(addOne);
    b.PushI(1).Emit(Op::Add).Emit(Op::Ret);
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).ret, 42);
}

// ---- host-call mechanics ----

TEST(RefVmHostCall, InvokesGatewayAndPushesStatus) {
    BytecodeBuilder b;
    // push argsOff, argsLen, retOff, retLen (all 0 here), then HOSTCALL Stop
    b.PushI(0).PushI(0).PushI(0).PushI(0).HostCall(gameapi::CallId::Stop).Emit(Op::Halt);
    RecordingGateway gw;
    const RunResult r = RunProgram(b.Build(), MakePolicy(), gw);
    EXPECT_EQ(r.trap, TrapReason::None);
    EXPECT_EQ(r.hostCalls, 1u);
    EXPECT_EQ(gw.calls, 1);
    EXPECT_EQ(gw.lastId, gameapi::CallId::Stop);
    EXPECT_EQ(r.ret, static_cast<int64_t>(gameapi::Status::Ok));  // status pushed onto stack
}

// ---- traps ----

TEST(RefVmTrap, DivideByZero) {
    BytecodeBuilder b;
    b.PushI(1).PushI(0).Emit(Op::Div).Emit(Op::Halt);
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).trap, TrapReason::DivideByZero);
}

TEST(RefVmTrap, StackUnderflow) {
    BytecodeBuilder b;
    b.Emit(Op::Pop).Emit(Op::Pop).Emit(Op::Halt);  // entry pushes 1 arg; 2nd Pop underflows
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).trap, TrapReason::StackUnderflow);
}

TEST(RefVmTrap, StackOverflow) {
    BytecodeBuilder b;
    const auto loop = b.NewLabel();
    b.Bind(loop);
    b.PushI(1).Jmp(loop);  // push forever
    SandboxPolicy p = MakePolicy();
    p.stackSlots = 8;
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), p, gw).trap, TrapReason::StackOverflow);
}

TEST(RefVmTrap, FuelExhausted) {
    BytecodeBuilder b;
    const auto loop = b.NewLabel();
    b.Bind(loop);
    b.Emit(Op::Nop).Jmp(loop);  // spin
    SandboxPolicy p = MakePolicy();
    p.fuel = 100;
    RecordingGateway gw;
    const RunResult r = RunProgram(b.Build(), p, gw);
    EXPECT_EQ(r.trap, TrapReason::FuelExhausted);
    EXPECT_EQ(r.fuelUsed, 100u);
}

TEST(RefVmTrap, OutOfBoundsLoadTraps) {
    BytecodeBuilder b;
    b.PushI(1'000'000).Emit(Op::Ld32).Emit(Op::Halt);  // far past a 4 KiB arena
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).trap, TrapReason::BadMemoryAccess);
}

TEST(RefVmTrap, NegativeAddressTraps) {
    BytecodeBuilder b;
    b.PushI(-1).Emit(Op::Ld8).Emit(Op::Halt);  // -1 as uint64 is huge -> OOB
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).trap, TrapReason::BadMemoryAccess);
}

TEST(RefVmTrap, StraddleEndOfArenaTraps) {
    BytecodeBuilder b;
    b.PushI(4094).Emit(Op::Ld32).Emit(Op::Halt);  // reads [4094,4098) past 4096
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(b.Build(), MakePolicy(), gw).trap, TrapReason::BadMemoryAccess);
}

TEST(RefVmTrap, IllegalOpcode) {
    auto img = MakeRawImage({0xFE, static_cast<uint8_t>(Op::Halt)});
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(img, MakePolicy(), gw).trap, TrapReason::IllegalInstruction);
}

TEST(RefVmTrap, TruncatedOperand) {
    auto img = MakeRawImage({static_cast<uint8_t>(Op::Push), 0x01, 0x02});  // Push needs 8 bytes
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(img, MakePolicy(), gw).trap, TrapReason::IllegalInstruction);
}

TEST(RefVmTrap, RunningOffTheEndTraps) {
    auto img = MakeRawImage({static_cast<uint8_t>(Op::Nop)});  // no Halt/Ret
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(img, MakePolicy(), gw).trap, TrapReason::IllegalInstruction);
}

TEST(RefVmTrap, OutOfRangeJumpTraps) {
    // Jmp with a huge positive relative offset -> target past code end.
    std::vector<uint8_t> code = {static_cast<uint8_t>(Op::Jmp), 0x00, 0x00, 0x00, 0x40};  // +0x40000000
    RecordingGateway gw;
    EXPECT_EQ(RunProgram(MakeRawImage(code), MakePolicy(), gw).trap, TrapReason::IllegalInstruction);
}

TEST(RefVmTrap, HostCallBudgetExhausted) {
    BytecodeBuilder b;
    const auto loop = b.NewLabel();
    b.Bind(loop);
    b.PushI(0).PushI(0).PushI(0).PushI(0).HostCall(gameapi::CallId::Stop).Emit(Op::Pop).Jmp(loop);
    SandboxPolicy p = MakePolicy();
    p.maxHostCalls = 3;
    RecordingGateway gw;
    const RunResult r = RunProgram(b.Build(), p, gw);
    EXPECT_EQ(r.trap, TrapReason::HostCallDenied);
    EXPECT_EQ(r.hostCalls, 3u);
}

TEST(RefVmTrap, GatewayThrowBecomesTrapWithAccounting) {
    // A throwing host-call must be contained as a trap (never escape into the host), AND the run
    // must still report the fuel/host-calls consumed before the throw (deterministic accounting).
    BytecodeBuilder b;
    b.Emit(Op::Nop).Emit(Op::Nop);
    b.PushI(0).PushI(0).PushI(0).PushI(0).HostCall(gameapi::CallId::Stop).Emit(Op::Halt);
    ThrowingGateway gw;
    const RunResult r = RunProgram(b.Build(), MakePolicy(), gw);
    EXPECT_EQ(r.trap, TrapReason::HostCallError);
    EXPECT_GE(r.fuelUsed, kHostCallFuel);  // the 2 Nops + the host-call's fuel were charged
    EXPECT_EQ(r.hostCalls, 1u);            // ++hostCalls ran before Invoke threw
}

TEST(RefVmTrap, PathologicalMemorySizeTrapsNotCrash) {
    // A huge arena request must become a trap, never a host-side throw (ADR-0008 red line #5).
    BytecodeBuilder b;
    b.Emit(Op::Halt);
    SandboxPolicy p = MakePolicy();
    p.memoryBytes = 0xFFFF'FFFFu;  // ~4 GiB; allocation is expected to fail
    RecordingGateway gw;
    const RunResult r = RunProgram(b.Build(), p, gw);
    EXPECT_EQ(r.trap, TrapReason::OutOfMemory);
}

TEST(RefVmTrap, HostCallOutOfBoundsArgsTraps) {
    BytecodeBuilder b;
    // argsOff=0, argsLen=100000 (way past arena) -> BadMemoryAccess before gateway is called
    b.PushI(0).PushI(100000).PushI(0).PushI(0).HostCall(gameapi::CallId::Stop).Emit(Op::Halt);
    RecordingGateway gw;
    const RunResult r = RunProgram(b.Build(), MakePolicy(), gw);
    EXPECT_EQ(r.trap, TrapReason::BadMemoryAccess);
    EXPECT_EQ(gw.calls, 0);  // gateway never reached with a bad pointer
}

}  // namespace
