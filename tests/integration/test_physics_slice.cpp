// Full-stack physics slice (ADR-0005/0009/0010), headless. The SAME sandboxed seek-guest used by
// test_vertical_slice — sense nearest objective, read its position, issue MoveTo — but here the
// agent is a PHYSICS body. The intent flows: sandbox guest → Game API → DefaultIntentResolver
// (MoveTarget) → gameplay ActuationSystem (sets body velocity) → PhysicsSystem (integrates, writes
// Transform) → boundary SnapshotPublisher. It proves the entire spine composes and that the Game
// API is a stable contract independent of how the entity is actually moved (kinematic resolver vs
// physics). Deterministic on replay.

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
#include "next/gameplay/actuation_system.h"
#include "next/physics/components.h"
#include "next/physics/physics_system.h"
#include "next/physics/reference_physics_world.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/sandbox/bytecode.h"
#include "next/sandbox/gameapi_gateway.h"
#include "next/sandbox/ref_vm.h"
#include "next/sandbox/sandbox.h"

using namespace Next;

namespace {

constexpr uint32_t kTargetTag = 4;
constexpr uint32_t kArgs = 0;       // 16 B: sense args / move args
constexpr uint32_t kSenseRet = 64;  // 16 B: SenseNearestResult
constexpr uint32_t kPosRet = 80;    // 12 B: PositionResult

int64_t FloatBits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return static_cast<int64_t>(u);
}

// Identical "seek the objective" guest as the non-physics slice: it only speaks the Game API.
std::vector<uint8_t> BuildSeekGuest(float speed) {
    sandbox::BytecodeBuilder b;
    const auto done = b.NewLabel();
    auto hostCall = [&](gameapi::CallId id, uint32_t aOff, uint32_t aLen, uint32_t rOff, uint32_t rLen) {
        b.PushI(aOff).PushI(aLen).PushI(rOff).PushI(rLen).HostCall(id);
    };
    auto writeF = [&](uint32_t addr, float v) { b.PushI(addr).PushI(FloatBits(v)).Emit(sandbox::Op::St32); };
    auto copyWord = [&](uint32_t dst, uint32_t src) {
        b.PushI(dst).PushI(src).Emit(sandbox::Op::Ld32).Emit(sandbox::Op::St32);
    };

    writeF(kArgs + 0, 1000.0f);                                    // SenseNearestArgs.radius
    b.PushI(kArgs + 4).PushI(kTargetTag).Emit(sandbox::Op::St32);  // .tag
    hostCall(gameapi::CallId::SenseNearest, kArgs, 8, kSenseRet, 16);
    b.Jnz(done);
    hostCall(gameapi::CallId::GetPosition, kSenseRet, 8, kPosRet, 12);
    b.Jnz(done);
    copyWord(kArgs + 0, kPosRet + 0);
    copyWord(kArgs + 4, kPosRet + 4);
    copyWord(kArgs + 8, kPosRet + 8);
    writeF(kArgs + 12, speed);
    hostCall(gameapi::CallId::MoveTo, kArgs, 16, 0, 0);
    b.Emit(sandbox::Op::Pop);
    b.Bind(done);
    b.Emit(sandbox::Op::Halt);
    return b.Build();
}

TEST(PhysicsSlice, GuestDrivesPhysicsBodyToObjectiveAndStreams) {
    World world;
    gameapi::SimClock clock;
    clock.fixedDt = 0.1;
    gameapi::ObjectiveStore objectives;
    auto physics = physics::MakeReferencePhysicsWorld();

    // Agent: a kinematic physics body the guest controls. Renderable so it appears in snapshots.
    Entity agent = world.CreateEntity();
    world.AddComponent<TransformComponent>(agent);
    physics::RigidBodyComponent agentBody;
    agentBody.desc.motion = physics::MotionType::Kinematic;
    agentBody.desc.shape = physics::ShapeType::Sphere;
    agentBody.desc.halfExtents[0] = agentBody.desc.halfExtents[1] = agentBody.desc.halfExtents[2] = 0.5f;
    world.AddComponent<physics::RigidBodyComponent>(agent, agentBody);
    world.AddComponent<boundary::RenderableComponent>(agent, boundary::RenderableComponent{1, 0});

    // Objective: a tagged prop at (10,0,0), sensed via the Game API (ECS tag, not physics).
    Entity target = world.CreateEntity();
    auto& tt = world.AddComponent<TransformComponent>(target);
    tt.position[0] = 10.0f;
    gameapi::GameTag tag;
    tag.Set(kTargetTag);
    world.AddComponent<gameapi::GameTag>(target, tag);

    // Game API context for the agent.
    gameapi::GameApiConfig cfg;
    cfg.world = &world;
    cfg.clock = &clock;
    cfg.objectives = &objectives;
    cfg.self = gameapi::ToEntityId(agent);
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

    gameapi::DefaultIntentResolver resolver(&objectives);
    gameplay::ActuationSystem actuation(physics.get());
    physics::PhysicsSystem physicsSystem(physics.get());
    world.RegisterSystem(&actuation);      // intent -> body velocity
    world.RegisterSystem(&physicsSystem);  // integrate -> write Transform

    boundary::SnapshotPublisher publisher;
    boundary::InProcessTransport transport;
    bool sawAgentUpdate = false;
    const boundary::EntityId agentId = static_cast<boundary::EntityId>(agent);

    for (int t = 0; t < 80; ++t) {
        api.BeginTick();
        const sandbox::RunResult r = vm.Run(policy, gateway, 0, 0);
        ASSERT_EQ(r.trap, sandbox::TrapReason::None) << sandbox::ToString(r.trap);

        resolver.Apply(world, api.Intents().Items());
        api.ClearIntents();
        world.Update(static_cast<float>(clock.fixedDt));  // ActuationSystem + PhysicsSystem
        clock.Advance();

        publisher.PublishTo(transport, world, clock.tick, clock.seconds);
        const boundary::SnapshotBlock* snap = transport.AcquireSnapshot();
        ASSERT_NE(snap, nullptr);
        for (const auto& u : snap->updates) {
            if (u.id == agentId)
                sawAgentUpdate = true;
        }
    }

    // The guest, speaking only the Game API, drove its physics body to the objective.
    const TransformComponent* t = world.GetComponent<TransformComponent>(agent);
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->position[0], 10.0f);
    EXPECT_TRUE(sawAgentUpdate);  // physics-driven motion reached the UE5 view stream
}

}  // namespace
