// Sandbox security contract (ADR-0008), verified end to end against the real Game API gateway:
// capability gating (and defense-in-depth intersection), zero ambient authority (a guest cannot
// touch the world except through a granted host-call), and determinism.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "next/gameapi/components.h"
#include "next/gameapi/game_api.h"
#include "next/gameapi/intent_resolver.h"
#include "next/gameapi/sim_clock.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/sandbox/bytecode.h"
#include "next/sandbox/gameapi_gateway.h"
#include "next/sandbox/ref_vm.h"
#include "next/sandbox/sandbox.h"

using namespace Next;
using namespace Next::sandbox;
using Next::gameapi::CapabilitySet;
using Next::gameapi::GameApi;
using Next::gameapi::GameApiConfig;
using Next::gameapi::Status;

namespace {

int64_t FloatBits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return static_cast<int64_t>(u);
}

// A world with one agent at the origin that a guest can control.
struct Sim {
    World world;
    gameapi::SimClock clock;
    gameapi::ObjectiveStore objectives;
    Entity agent;

    Sim() {
        clock.fixedDt = 0.1;
        agent = world.CreateEntity();
        world.AddComponent<TransformComponent>(agent);
    }

    GameApi MakeApi(CapabilitySet caps) {
        GameApiConfig cfg;
        cfg.world = &world;
        cfg.clock = &clock;
        cfg.objectives = &objectives;
        cfg.self = gameapi::ToEntityId(agent);
        cfg.capabilities = caps;
        return GameApi(cfg);
    }

    float AgentX() {
        const TransformComponent* t = world.GetComponent<TransformComponent>(agent);
        return t != nullptr ? t->position[0] : -999.0f;
    }
};

SandboxPolicy MakePolicy(CapabilitySet caps) {
    SandboxPolicy p;
    p.memoryBytes = 1024;
    p.capabilities = caps;
    return p;
}

// A gateway that should never be invoked (for guests that make no host-calls).
struct NullGateway : HostGateway {
    gameapi::Status Invoke(gameapi::CallId, uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                           const gameapi::CapabilitySet&) override {
        return gameapi::Status::Internal;
    }
};

// Guest that writes MoveToArgs{target=(tx,0,0), maxSpeed} at offset 0 then calls MoveTo.
std::vector<uint8_t> MoveToProgram(float tx, float maxSpeed) {
    BytecodeBuilder b;
    b.PushI(0).PushI(FloatBits(tx)).Emit(Op::St32);         // target.x
    b.PushI(4).PushI(FloatBits(0.0f)).Emit(Op::St32);       // target.y
    b.PushI(8).PushI(FloatBits(0.0f)).Emit(Op::St32);       // target.z
    b.PushI(12).PushI(FloatBits(maxSpeed)).Emit(Op::St32);  // maxSpeed
    // argsOff=0, argsLen=16, retOff=0, retLen=0
    b.PushI(0).PushI(16).PushI(0).PushI(0).HostCall(gameapi::CallId::MoveTo);
    b.Emit(Op::Halt);  // leaves the MoveTo status on the stack as the return value
    return b.Build();
}

TEST(SandboxSecurity, GrantedGuestDrivesWorldThroughGameApi) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    GameApiGateway gateway(&api);

    RefVm vm;
    std::string err;
    const auto image = MoveToProgram(10.0f, 5.0f);
    ASSERT_TRUE(vm.LoadModule(image.data(), image.size(), &err)) << err;
    const RunResult r = vm.Run(MakePolicy(CapabilitySet::PlayerDefault()), gateway, 0, 0);

    EXPECT_EQ(r.trap, TrapReason::None);
    EXPECT_EQ(r.ret, static_cast<int64_t>(Status::Ok));
    ASSERT_EQ(api.Intents().Size(), 1u);

    gameapi::DefaultIntentResolver resolver(&sim.objectives);
    resolver.Apply(sim.world, api.Intents().Items());
    gameapi::DefaultIntentResolver::StepKinematics(sim.world, sim.clock.fixedDt);
    EXPECT_FLOAT_EQ(sim.AgentX(), 0.5f);  // 5 u/s * 0.1 s
}

TEST(SandboxSecurity, GatewayDeniesUngrantedCapability) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::ObserverOnly());  // no Actuate
    api.BeginTick();
    GameApiGateway gateway(&api);

    RefVm vm;
    std::string err;
    const auto image = MoveToProgram(10.0f, 5.0f);
    ASSERT_TRUE(vm.LoadModule(image.data(), image.size(), &err)) << err;
    // Policy also lacks Actuate -> the gateway's outer ring denies before AbiDispatch.
    const RunResult r = vm.Run(MakePolicy(CapabilitySet::ObserverOnly()), gateway, 0, 0);

    EXPECT_EQ(r.trap, TrapReason::None);  // a denied call is a value, not a crash
    EXPECT_EQ(r.ret, static_cast<int64_t>(Status::PermissionDenied));
    EXPECT_TRUE(api.Intents().Empty());
    EXPECT_FLOAT_EQ(sim.AgentX(), 0.0f);  // world untouched
}

TEST(SandboxSecurity, DefenseInDepthIntersectionFacadeStillDenies) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::ObserverOnly());  // facade lacks Actuate
    api.BeginTick();
    GameApiGateway gateway(&api);

    RefVm vm;
    std::string err;
    const auto image = MoveToProgram(10.0f, 5.0f);
    ASSERT_TRUE(vm.LoadModule(image.data(), image.size(), &err)) << err;
    // Policy GRANTS Actuate, so the gateway's outer ring passes — but the facade re-checks and
    // denies. Effective permission is the intersection; the more restrictive layer wins.
    const RunResult r = vm.Run(MakePolicy(CapabilitySet::PlayerDefault()), gateway, 0, 0);

    EXPECT_EQ(r.ret, static_cast<int64_t>(Status::PermissionDenied));
    EXPECT_TRUE(api.Intents().Empty());
}

TEST(SandboxSecurity, NoAmbientAuthorityWithoutHostCall) {
    Sim sim;
    GameApi api = sim.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    GameApiGateway gateway(&api);

    // A guest that scribbles all over its own memory but never makes a host-call cannot affect
    // the world: memory is a private arena, and the world is only reachable via host-calls.
    BytecodeBuilder b;
    const auto loop = b.NewLabel();
    const auto end = b.NewLabel();
    b.PushI(0).StLoc(0);  // i = 0
    b.Bind(loop);
    b.LdLoc(0).PushI(1024).Emit(Op::Ges).Jnz(end);  // while i < 1024
    b.LdLoc(0).LdLoc(0).Emit(Op::St8);              // mem[i] = i (low byte)
    b.LdLoc(0).PushI(1).Emit(Op::Add).StLoc(0);
    b.Jmp(loop);
    b.Bind(end);
    b.Emit(Op::Halt);

    RefVm vm;
    std::string err;
    const auto image = b.Build();
    ASSERT_TRUE(vm.LoadModule(image.data(), image.size(), &err)) << err;
    const RunResult r = vm.Run(MakePolicy(CapabilitySet::PlayerDefault()), gateway, 0, 0);

    EXPECT_EQ(r.trap, TrapReason::None);
    EXPECT_EQ(r.hostCalls, 0u);
    EXPECT_TRUE(api.Intents().Empty());
    EXPECT_FLOAT_EQ(sim.AgentX(), 0.0f);  // world untouched
}

TEST(SandboxSecurity, DeterministicAcrossRuns) {
    // Same program + same inputs -> identical observable result every time (replay/anti-cheat).
    BytecodeBuilder b;
    const auto loop = b.NewLabel();
    const auto end = b.NewLabel();
    b.PushI(0).StLoc(1);  // acc
    b.PushI(0).StLoc(0);  // i
    b.Bind(loop);
    b.LdLoc(0).PushI(1000).Emit(Op::Ges).Jnz(end);
    b.LdLoc(1).LdLoc(0).Emit(Op::Add).StLoc(1);
    b.LdLoc(0).PushI(1).Emit(Op::Add).StLoc(0);
    b.Jmp(loop);
    b.Bind(end);
    b.LdLoc(1).Emit(Op::Halt);
    const auto image = b.Build();

    RefVm vm;
    std::string err;
    ASSERT_TRUE(vm.LoadModule(image.data(), image.size(), &err)) << err;

    gameapi::CapabilitySet caps = CapabilitySet::PlayerDefault();
    NullGateway gw;
    RunResult first = vm.Run(MakePolicy(caps), gw, 0, 0);
    for (int i = 0; i < 5; ++i) {
        RunResult again = vm.Run(MakePolicy(caps), gw, 0, 0);
        EXPECT_EQ(again.trap, first.trap);
        EXPECT_EQ(again.ret, first.ret);
        EXPECT_EQ(again.fuelUsed, first.fuelUsed);
        EXPECT_EQ(again.hostCalls, first.hostCalls);
    }
    EXPECT_EQ(first.ret, 499500);  // sum 0..999
}

}  // namespace
