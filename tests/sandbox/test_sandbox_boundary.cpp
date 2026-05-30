// Sandbox boundary-condition regression suite (ADR-0008).
//
// These assertions were first established by the headless audit harness (tools/sandbox_audit) and
// are pinned here so the boundaries it confirmed stay confirmed — they run in ctest under
// ASan/UBSan in CI. They deliberately cover surfaces the original test_sandbox_security.cpp did
// NOT: the argsLen/retLen==0 wild-offset paths, undersized/overlapping ret windows, unknown
// CallIds, per-tick rate limits, NaN rejection, format-string-as-data, and cross-run isolation.
// See docs/security/sandbox-audit-2026-05-30.md.

#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <vector>

#include "next/gameapi/components.h"
#include "next/gameapi/game_api.h"
#include "next/gameapi/sim_clock.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/sandbox/bytecode.h"
#include "next/sandbox/gameapi_gateway.h"
#include "next/sandbox/ref_vm.h"
#include "next/sandbox/sandbox.h"

using namespace Next;
using namespace Next::sandbox;
using gameapi::CallId;
using gameapi::CapabilitySet;
using gameapi::GameApi;
using gameapi::GameApiConfig;
using gameapi::Status;

namespace {

int64_t F2B(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return static_cast<int64_t>(u);
}

void StoreF32(BytecodeBuilder& b, uint32_t off, float v) {
    b.PushI(off).PushI(F2B(v)).Emit(Op::St32);
}
void StoreU32(BytecodeBuilder& b, uint32_t off, uint32_t v) {
    b.PushI(off).PushI(static_cast<int64_t>(v)).Emit(Op::St32);
}
void StoreI64(BytecodeBuilder& b, uint32_t off, int64_t v) {
    b.PushI(off).PushI(v).Emit(Op::St64);
}
// Push host-call window operands in the VM's pop order, then HostCall.
void HC(BytecodeBuilder& b, int64_t aOff, int64_t aLen, int64_t rOff, int64_t rLen, CallId id) {
    b.PushI(aOff).PushI(aLen).PushI(rOff).PushI(rLen).HostCall(id);
}

// A headless world: agent at origin, one tag-5 prop at (3,0,0), objective 42 == 100.
struct Sim {
    World world;
    gameapi::SimClock clock;
    gameapi::ObjectiveStore objectives;
    Entity agent;
    gameapi::EntityId agentId = 0;

    Sim() {
        clock.fixedDt = 0.1;
        agent = world.CreateEntity();
        world.AddComponent<TransformComponent>(agent);
        agentId = gameapi::ToEntityId(agent);

        Entity prop = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(prop);
        t.position[0] = 3.0f;
        gameapi::GameTag g;
        g.Set(5);
        world.AddComponent<gameapi::GameTag>(prop, g);

        objectives.Set(42, 100);
    }

    GameApi MakeApi(CapabilitySet caps, uint32_t maxHostPerTick = 4096, uint32_t maxComms = 64) {
        GameApiConfig cfg;
        cfg.world = &world;
        cfg.clock = &clock;
        cfg.objectives = &objectives;
        cfg.self = agentId;
        cfg.capabilities = caps;
        cfg.maxHostCallsPerTick = maxHostPerTick;
        cfg.maxCommsPerTick = maxComms;
        return GameApi(cfg);
    }
};

SandboxPolicy Policy(uint32_t mem = 4096) {
    SandboxPolicy p;
    p.memoryBytes = mem;
    p.capabilities = CapabilitySet::PlayerDefault();
    return p;
}

RunResult RunGuest(GameApi& api, const std::vector<uint8_t>& image, const SandboxPolicy& policy) {
    api.BeginTick();
    GameApiGateway gw(&api);
    RefVm vm;
    std::string err;
    EXPECT_TRUE(vm.LoadModule(image.data(), image.size(), &err)) << err;
    return vm.Run(policy, gw, 0, 0);
}

Status St(int64_t ret) {
    return static_cast<Status>(static_cast<int32_t>(ret));
}

// C1: argsLen==0 makes the VM skip the offset bounds-check; a wild offset must still be safe
// (gateway hands nullptr, dispatch refuses to read) -> InvalidArgument, never a trap/OOB.
TEST(SandboxBoundary, ArgsLenZeroWithWildOffsetIsSafe) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    BytecodeBuilder b;
    HC(b, 0xFFFFFFF0LL, 0, 0, 0, CallId::MoveTo);
    b.Emit(Op::Halt);
    const RunResult r = RunGuest(api, b.Build(), Policy());
    EXPECT_EQ(r.trap, TrapReason::None);
    EXPECT_EQ(St(r.ret), Status::InvalidArgument);
}

// C2: retLen==0 + wild offset on a writing call must not write and must not OOB.
TEST(SandboxBoundary, RetLenZeroWithWildOffsetIsSafe) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    BytecodeBuilder b;
    StoreI64(b, 0, static_cast<int64_t>(sim.agentId));
    HC(b, 0, 8, 0xFFFFFFF0LL, 0, CallId::GetPosition);
    b.Emit(Op::Halt);
    const RunResult r = RunGuest(api, b.Build(), Policy());
    EXPECT_EQ(r.trap, TrapReason::None);
    EXPECT_EQ(St(r.ret), Status::BufferTooSmall);
}

// C3: a huge (negative) argsLen traps in the VM window check before the gateway is reached.
TEST(SandboxBoundary, NegativeArgsLenTraps) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    BytecodeBuilder b;
    HC(b, 0, -1, 0, 0, CallId::MoveTo);
    b.Emit(Op::Halt);
    EXPECT_EQ(RunGuest(api, b.Build(), Policy()).trap, TrapReason::BadMemoryAccess);
}

// C4: a ret window that straddles the arena end traps (no one-byte-past write).
TEST(SandboxBoundary, RetWindowStraddlingArenaEndTraps) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    BytecodeBuilder b;
    StoreI64(b, 0, static_cast<int64_t>(sim.agentId));
    HC(b, 0, 8, 4096 - 4, 12, CallId::GetPosition);  // 12-byte result cannot fit in 4 at the edge
    b.Emit(Op::Halt);
    EXPECT_EQ(RunGuest(api, b.Build(), Policy()).trap, TrapReason::BadMemoryAccess);
}

// C5: an in-bounds-but-undersized ret window yields BufferTooSmall and never overspills it.
TEST(SandboxBoundary, UndersizedRetBufferDoesNotOverspill) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    BytecodeBuilder b;
    StoreI64(b, 0, static_cast<int64_t>(sim.agentId));
    b.PushI(100).PushI(0xAB).Emit(Op::St8);   // canary just past the window
    HC(b, 0, 8, 96, 4, CallId::GetPosition);  // window [96,100), result is 12 bytes
    b.Emit(Op::Pop);
    b.PushI(100).Emit(Op::Ld8).Emit(Op::Halt);
    EXPECT_EQ(RunGuest(api, b.Build(), Policy()).ret, 0xAB);
}

// C6: an unknown CallId is denied as a value (CapabilityFor -> Count_), never a trap.
TEST(SandboxBoundary, UnknownCallIdDeniedAsValue) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    BytecodeBuilder b;
    HC(b, 0, 0, 0, 0, static_cast<CallId>(9999));
    b.Emit(Op::Halt);
    const RunResult r = RunGuest(api, b.Build(), Policy());
    EXPECT_EQ(r.trap, TrapReason::None);
    EXPECT_EQ(St(r.ret), Status::PermissionDenied);
}

// C10: per-Run state does not bleed across runs on the same RefVm (fresh arena each Run).
TEST(SandboxBoundary, FreshArenaEachRun) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    GameApiGateway gw(&api);
    RefVm vm;
    std::string err;

    BytecodeBuilder a;
    a.PushI(0).PushI(0xAB).Emit(Op::St8).Emit(Op::Halt);
    const auto ia = a.Build();
    ASSERT_TRUE(vm.LoadModule(ia.data(), ia.size(), &err));
    vm.Run(Policy(), gw, 0, 0);

    BytecodeBuilder b;
    b.PushI(0).Emit(Op::Ld8).Emit(Op::Halt);
    const auto ib = b.Build();
    ASSERT_TRUE(vm.LoadModule(ib.data(), ib.size(), &err));
    EXPECT_EQ(vm.Run(Policy(), gw, 0, 0).ret, 0);  // mem[0] is zero, not the 0xAB from run A
}

// C11: overlapping args/ret windows stay correct (dispatch copies args out before writing ret).
TEST(SandboxBoundary, OverlappingArgsRetWindowsStayCorrect) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    BytecodeBuilder b;
    StoreF32(b, 0, 100.0f);
    StoreU32(b, 4, 5);
    HC(b, 0, 8, 0, 16, CallId::SenseNearest);  // args [0,8) and ret [0,16) overlap
    b.Emit(Op::Pop);
    b.PushI(8).Emit(Op::Ld32).Emit(Op::Halt);  // distance at result+8
    const RunResult r = RunGuest(api, b.Build(), Policy());
    float dist = 0.0f;
    const uint32_t bits = static_cast<uint32_t>(r.ret & 0xFFFFFFFFu);
    std::memcpy(&dist, &bits, sizeof(dist));
    EXPECT_NEAR(dist, 3.0f, 1e-4f);
}

// C12: Log treats printf metacharacters as literal data (no format-string injection).
TEST(SandboxBoundary, LogTreatsMetacharsAsData) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    const char* evil = "%n%n%s%x%p";
    const uint32_t n = static_cast<uint32_t>(std::strlen(evil));
    BytecodeBuilder b;
    StoreU32(b, 0, 2);
    StoreU32(b, 4, n);
    for (uint32_t i = 0; i < n; ++i)
        b.PushI(8 + i).PushI(static_cast<int64_t>(static_cast<uint8_t>(evil[i]))).Emit(Op::St8);
    HC(b, 0, 8 + n, 0, 0, CallId::Log);
    b.Emit(Op::Halt);
    const RunResult r = RunGuest(api, b.Build(), Policy());
    EXPECT_EQ(St(r.ret), Status::Ok);
    ASSERT_FALSE(api.LogRing().empty());
    EXPECT_EQ(api.LogRing().back(), evil);
}

// C13: the per-tick Comms quota is enforced (the 4th SendSignal with quota 3 is RateLimited).
TEST(SandboxBoundary, CommsPerTickQuotaEnforced) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault(), 4096, /*maxComms*/ 3);
    BytecodeBuilder b;
    StoreU32(b, 0, 1);
    StoreU32(b, 4, 0);
    StoreF32(b, 8, 0.0f);
    for (int i = 0; i < 4; ++i) {
        HC(b, 0, 12, 0, 0, CallId::SendSignal);
        if (i < 3)
            b.Emit(Op::Pop);
    }
    b.Emit(Op::Halt);
    EXPECT_EQ(St(RunGuest(api, b.Build(), Policy()).ret), Status::RateLimited);
}

// C14: the per-tick host-call budget is enforced (the 3rd call with budget 2 is RateLimited).
TEST(SandboxBoundary, HostCallPerTickBudgetEnforced) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault(), /*maxHostPerTick*/ 2);
    BytecodeBuilder b;
    for (int i = 0; i < 3; ++i) {
        HC(b, 0, 0, 0, 8, CallId::GetTick);
        if (i < 2)
            b.Emit(Op::Pop);
    }
    b.Emit(Op::Halt);
    EXPECT_EQ(St(RunGuest(api, b.Build(), Policy()).ret), Status::RateLimited);
}

// C15: NaN arguments are rejected at the facade and record no intent.
TEST(SandboxBoundary, NaNArgumentRejected) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    BytecodeBuilder b;
    StoreF32(b, 0, std::nanf(""));
    StoreF32(b, 12, 1.0f);
    HC(b, 0, 16, 0, 0, CallId::MoveTo);
    b.Emit(Op::Halt);
    const RunResult r = RunGuest(api, b.Build(), Policy());
    EXPECT_EQ(St(r.ret), Status::InvalidArgument);
    EXPECT_TRUE(api.Intents().Empty());
}

}  // namespace
