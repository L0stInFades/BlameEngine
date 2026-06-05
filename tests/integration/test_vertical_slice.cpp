// The moat, end to end, headless (no renderer). A sandboxed guest program — real "player code" —
// each tick: senses the nearest objective, reads its position, and issues a MoveTo intent, all
// through the Game API and nothing else. The sim applies the intent at the tick boundary, steps
// kinematics, and the sim↔UE5 boundary publishes a snapshot a UE5 mirror would consume. This is
// the ADR-0005 architecture proven without UE5: sandbox → Game API → authoritative world → snapshot.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "next/boundary/snapshot.h"
#include "next/boundary/snapshot_publisher.h"
#include "next/boundary/transport.h"
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

namespace {

constexpr uint32_t kTargetTag = 3;

// memory layout the guest uses
constexpr uint32_t kArgs = 0;       // 16 B: sense args / move args
constexpr uint32_t kSenseRet = 64;  // 16 B: SenseNearestResult{entity, distance}
constexpr uint32_t kPosRet = 80;    // 12 B: PositionResult{Vec3}

int64_t FloatBits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return static_cast<int64_t>(u);
}

void WriteFloat(sandbox::BytecodeBuilder& b, uint32_t addr, float value) {
    b.PushI(addr).PushI(FloatBits(value)).Emit(sandbox::Op::St32);
}
void WriteU32(sandbox::BytecodeBuilder& b, uint32_t addr, uint32_t value) {
    b.PushI(addr).PushI(value).Emit(sandbox::Op::St32);
}
void CopyWord(sandbox::BytecodeBuilder& b, uint32_t dst, uint32_t src) {
    b.PushI(dst).PushI(src).Emit(sandbox::Op::Ld32).Emit(sandbox::Op::St32);
}
void HostCall(sandbox::BytecodeBuilder& b, gameapi::CallId id, uint32_t argsOff, uint32_t argsLen, uint32_t retOff,
              uint32_t retLen) {
    b.PushI(argsOff).PushI(argsLen).PushI(retOff).PushI(retLen).HostCall(id);
}

// A seek agent: find nearest objective -> read its position -> move toward it. Pure Game API.
std::vector<uint8_t> BuildSeekGuest(float speed) {
    sandbox::BytecodeBuilder b;
    const auto done = b.NewLabel();

    // SenseNearest(radius=1000, tag=kTargetTag) -> kSenseRet
    WriteFloat(b, kArgs + 0, 1000.0f);
    WriteU32(b, kArgs + 4, kTargetTag);
    HostCall(b, gameapi::CallId::SenseNearest, kArgs, 8, kSenseRet, 16);
    b.Jnz(done);  // status != Ok -> bail (leaves status on the way out; Halt returns it)

    // GetPosition(entity @ kSenseRet) -> kPosRet (entity is the first 8 bytes of the sense result)
    HostCall(b, gameapi::CallId::GetPosition, kSenseRet, 8, kPosRet, 12);
    b.Jnz(done);

    // Build MoveToArgs{target = sensed position, maxSpeed = speed} at kArgs, then MoveTo.
    CopyWord(b, kArgs + 0, kPosRet + 0);
    CopyWord(b, kArgs + 4, kPosRet + 4);
    CopyWord(b, kArgs + 8, kPosRet + 8);
    WriteFloat(b, kArgs + 12, speed);
    HostCall(b, gameapi::CallId::MoveTo, kArgs, 16, 0, 0);
    b.Emit(sandbox::Op::Pop);  // discard MoveTo status

    b.Bind(done);
    b.Emit(sandbox::Op::Halt);
    return b.Build();
}

struct Slice {
    World world;
    gameapi::SimClock clock;
    gameapi::ObjectiveStore objectives;
    Entity agent;
    Entity target;

    Slice() {
        clock.fixedDt = 0.1;

        agent = world.CreateEntity();
        world.AddComponent<TransformComponent>(agent);  // origin
        world.AddComponent<boundary::RenderableComponent>(agent, boundary::RenderableComponent{1, 0});

        target = world.CreateEntity();
        auto& tt = world.AddComponent<TransformComponent>(target);
        tt.position[0] = 10.0f;
        gameapi::GameTag tag;
        tag.Set(kTargetTag);
        world.AddComponent<gameapi::GameTag>(target, tag);
        world.AddComponent<boundary::RenderableComponent>(target, boundary::RenderableComponent{2, 0});
    }
};

TEST(VerticalSlice, SandboxedGuestSeeksObjectiveAndStreamsToView) {
    Slice sim;

    gameapi::GameApiConfig cfg;
    cfg.world = &sim.world;
    cfg.clock = &sim.clock;
    cfg.objectives = &sim.objectives;
    cfg.self = gameapi::ToEntityId(sim.agent);
    cfg.capabilities = gameapi::CapabilitySet::PlayerDefault();
    gameapi::GameApi api(cfg);

    sandbox::GameApiGateway gateway(&api);
    sandbox::SandboxPolicy policy;
    policy.capabilities = gameapi::CapabilitySet::PlayerDefault();
    policy.memoryBytes = 1024;

    sandbox::RefVm vm;
    std::string err;
    const auto image = BuildSeekGuest(5.0f);
    ASSERT_TRUE(vm.LoadModule(image.data(), image.size(), &err)) << err;

    gameapi::DefaultIntentResolver resolver(&sim.objectives);
    boundary::SnapshotPublisher publisher;
    boundary::InProcessTransport transport;

    const float startX = sim.world.GetComponent<TransformComponent>(sim.agent)->position[0];
    EXPECT_FLOAT_EQ(startX, 0.0f);

    int totalSpawns = 0;
    bool sawAgentUpdate = false;
    const boundary::EntityId agentId = static_cast<boundary::EntityId>(sim.agent);

    for (int t = 0; t < 40; ++t) {
        // --- run the player's code in the sandbox ---
        api.BeginTick();
        const sandbox::RunResult r = vm.Run(policy, gateway, 0, 0);
        ASSERT_EQ(r.trap, sandbox::TrapReason::None) << sandbox::ToString(r.trap);

        // --- apply intents at the tick boundary, then advance the authoritative world ---
        resolver.Apply(sim.world, api.Intents().Items());
        api.ClearIntents();
        gameapi::DefaultIntentResolver::StepKinematics(sim.world, sim.clock.fixedDt);
        sim.clock.Advance();

        // --- publish the render-view delta the UE5 mirror would consume ---
        publisher.PublishTo(transport, sim.world, sim.clock.tick, sim.clock.seconds);
        const boundary::SnapshotBlock* snap = transport.AcquireSnapshot();
        ASSERT_NE(snap, nullptr);
        totalSpawns += static_cast<int>(snap->spawns.size());
        for (const auto& u : snap->updates) {
            if (u.id == agentId)
                sawAgentUpdate = true;
        }
        publisher.Acknowledge(snap->sequence);  // lossless consumer acks -> deltas compress to updates
    }

    // The agent reached the objective (snap-to-target guarantees exact arrival).
    const TransformComponent* finalT = sim.world.GetComponent<TransformComponent>(sim.agent);
    ASSERT_NE(finalT, nullptr);
    EXPECT_FLOAT_EQ(finalT->position[0], 10.0f);
    EXPECT_FLOAT_EQ(finalT->position[1], 0.0f);
    EXPECT_FLOAT_EQ(finalT->position[2], 0.0f);

    // The view stream saw both entities spawn once and the agent move.
    EXPECT_EQ(totalSpawns, 2);
    EXPECT_TRUE(sawAgentUpdate);
}

TEST(VerticalSlice, DeterministicReplayProducesIdenticalTrajectory) {
    auto runOnce = [](std::vector<float>& trajectory) {
        Slice sim;
        gameapi::GameApiConfig cfg;
        cfg.world = &sim.world;
        cfg.clock = &sim.clock;
        cfg.objectives = &sim.objectives;
        cfg.self = gameapi::ToEntityId(sim.agent);
        cfg.capabilities = gameapi::CapabilitySet::PlayerDefault();
        gameapi::GameApi api(cfg);

        sandbox::GameApiGateway gateway(&api);
        sandbox::SandboxPolicy policy;
        policy.capabilities = gameapi::CapabilitySet::PlayerDefault();
        policy.memoryBytes = 1024;

        sandbox::RefVm vm;
        std::string err;
        const auto image = BuildSeekGuest(5.0f);
        vm.LoadModule(image.data(), image.size(), &err);
        gameapi::DefaultIntentResolver resolver(&sim.objectives);

        for (int t = 0; t < 25; ++t) {
            api.BeginTick();
            vm.Run(policy, gateway, 0, 0);
            resolver.Apply(sim.world, api.Intents().Items());
            api.ClearIntents();
            gameapi::DefaultIntentResolver::StepKinematics(sim.world, sim.clock.fixedDt);
            sim.clock.Advance();
            trajectory.push_back(sim.world.GetComponent<TransformComponent>(sim.agent)->position[0]);
        }
    };

    std::vector<float> a;
    std::vector<float> b;
    runOnce(a);
    runOnce(b);
    ASSERT_EQ(a.size(), b.size());
    for (size_t i = 0; i < a.size(); ++i) {
        EXPECT_FLOAT_EQ(a[i], b[i]) << "divergence at tick " << i;
    }
}

}  // namespace
