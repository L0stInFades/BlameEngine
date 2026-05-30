// End-to-end: author a level, load it into a fresh World, run a SANDBOXED guest (RefVm) that drives
// the agent through the Game API, and watch the level's win condition transition to Won — twice,
// deterministically. This is the level system composing with the full vertical slice
// (sandbox -> Game API -> intents -> kinematics) on a LOADED (not hand-built) world.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "next/gameapi/components.h"
#include "next/gameapi/game_api.h"
#include "next/gameapi/intent_resolver.h"
#include "next/gameapi/objective_store.h"
#include "next/gameapi/sim_clock.h"
#include "next/level/level_builder.h"
#include "next/level/level_loader.h"
#include "next/level/win_conditions.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/sandbox/bytecode.h"
#include "next/sandbox/gameapi_gateway.h"
#include "next/sandbox/ref_vm.h"
#include "next/sandbox/sandbox.h"

using namespace Next;
using namespace Next::level;

namespace {

int64_t FloatBits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return static_cast<int64_t>(u);
}

// Guest: write MoveToArgs{target=(tx,0,0), maxSpeed} at offset 0, then HostCall MoveTo each run.
std::vector<uint8_t> MoveToProgram(float tx, float maxSpeed) {
    sandbox::BytecodeBuilder b;
    b.PushI(0).PushI(FloatBits(tx)).Emit(sandbox::Op::St32);
    b.PushI(4).PushI(FloatBits(0.0f)).Emit(sandbox::Op::St32);
    b.PushI(8).PushI(FloatBits(0.0f)).Emit(sandbox::Op::St32);
    b.PushI(12).PushI(FloatBits(maxSpeed)).Emit(sandbox::Op::St32);
    b.PushI(0).PushI(16).PushI(0).PushI(0).HostCall(gameapi::CallId::MoveTo);
    b.Emit(sandbox::Op::Halt);
    return b.Build();
}

// A level: agent at origin, a goal marker at (5,0,0), objective 1, win = agent reaches (5,0,0).
LevelDef MakeSeekLevel() {
    LevelBuilder b("seek-01", "Seek");
    const LevelEntityRef agent = b.AddEntity("agent");
    b.WithPosition(0.0f, 0.0f, 0.0f).WithTagIndex(1);
    b.SetAgent(agent);
    b.AddEntity("goal");
    b.WithPosition(5.0f, 0.0f, 0.0f).WithTagIndex(2);
    b.AddObjective(1, 0);
    WinConditionDef win;
    win.kind = WinKind::EntityReached;
    win.entity = agent;
    win.point[0] = 5.0f;
    win.radius = 0.6f;
    b.AddWinCondition(win);
    return b.Build();
}

// Load the level into a fresh world and run the guest until Won (or the tick budget runs out).
// Returns the tick at which the level was won, or -1.
int RunUntilWon(const LevelDef& def, float* outAgentX) {
    World world;
    gameapi::SimClock clock;
    clock.fixedDt = 0.1;
    gameapi::ObjectiveStore objectives;

    const LoadResult lr = LevelLoader::Load(def, world, &objectives);
    EXPECT_TRUE(lr.loaded);
    if (!lr.loaded) {
        return -1;
    }

    gameapi::GameApiConfig cfg;
    cfg.world = &world;
    cfg.clock = &clock;
    cfg.objectives = &objectives;
    cfg.self = gameapi::ToEntityId(lr.level.agent);
    cfg.capabilities = gameapi::CapabilitySet::PlayerDefault();
    gameapi::GameApi api(cfg);
    sandbox::GameApiGateway gateway(&api);

    sandbox::RefVm vm;
    std::string err;
    const auto image = MoveToProgram(5.0f, 5.0f);
    EXPECT_TRUE(vm.LoadModule(image.data(), image.size(), &err)) << err;
    sandbox::SandboxPolicy policy;
    policy.memoryBytes = 1024;
    policy.capabilities = gameapi::CapabilitySet::PlayerDefault();

    gameapi::DefaultIntentResolver resolver(&objectives);

    int wonTick = -1;
    for (int t = 0; t < 60; ++t) {
        api.BeginTick();
        const sandbox::RunResult r = vm.Run(policy, gateway, 0, 0);
        EXPECT_EQ(r.trap, sandbox::TrapReason::None) << sandbox::ToString(r.trap);
        resolver.Apply(world, api.Intents().Items());
        api.ClearIntents();
        gameapi::DefaultIntentResolver::StepKinematics(world, static_cast<float>(clock.fixedDt));
        clock.Advance();
        if (WinEvaluator::Evaluate(def, world, &objectives, lr.level) == LevelOutcome::Won) {
            wonTick = t;
            break;
        }
    }
    if (outAgentX != nullptr) {
        const TransformComponent* t = world.GetComponent<TransformComponent>(lr.level.agent);
        *outAgentX = (t != nullptr) ? t->position[0] : -999.0f;
    }
    return wonTick;
}

TEST(LevelSlice, LoadRunGuestWinFires) {
    const LevelDef def = MakeSeekLevel();
    float agentX = 0.0f;
    const int wonTick = RunUntilWon(def, &agentX);
    EXPECT_GE(wonTick, 0) << "level was never won within the tick budget";
    EXPECT_GE(agentX, 4.4f);  // agent reached the goal zone
}

TEST(LevelSlice, DeterministicAcrossRuns) {
    const LevelDef def = MakeSeekLevel();
    float xa = 0.0f;
    float xb = 0.0f;
    const int a = RunUntilWon(def, &xa);
    const int b = RunUntilWon(def, &xb);
    EXPECT_GE(a, 0);
    EXPECT_EQ(a, b);  // identical winning tick
    EXPECT_FLOAT_EQ(xa, xb);
}

}  // namespace
