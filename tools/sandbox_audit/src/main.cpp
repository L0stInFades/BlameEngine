// Sandbox security audit harness (ADR-0008).
//
// Boots a REAL headless authoritative world (ECS World + GameApi + ObjectiveStore + reference
// physics), wires the player-code sandbox to it through the production GameApiGateway, and then
// drives it with a battery of hand-written guest programs. Nothing here is mocked: every guest
// runs on the same RefVm and through the same Game API a shipped player would hit. Its stdout is
// the evidence behind docs/security/sandbox-audit-2026-05-30.md.
//
// Three investigations, matching the audit brief:
//   A. the exposed API boundary  — call every CallId, observe Status + side effects
//   B. the ISA / "language"      — exercise the opcode surface, document what is expressible
//   C. the security boundary     — adversarial guests against every edge condition
//
// Verdict tags:  [SAFE] a security boundary held as required   [INFO] documented behavior
//                [FIND] an audit finding / hardening item worth recording
//
// Exit code: 0 if every [SAFE] expectation held, 1 otherwise (so CI / a human can gate on it).

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "next/foundation/logger.h"
#include "next/gameapi/components.h"
#include "next/gameapi/game_api.h"
#include "next/gameapi/intent_resolver.h"
#include "next/gameapi/objective_store.h"
#include "next/gameapi/sim_clock.h"
#include "next/gameplay/physics_world_query.h"
#include "next/physics/components.h"
#include "next/physics/reference_physics_world.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"
#include "next/sandbox/bytecode.h"
#include "next/sandbox/gameapi_gateway.h"
#include "next/sandbox/ref_vm.h"
#include "next/sandbox/sandbox.h"

using namespace Next;
using namespace Next::sandbox;
using gameapi::CapabilitySet;
using gameapi::CallId;
using gameapi::GameApi;
using gameapi::GameApiConfig;
using gameapi::Status;

namespace {

// ---------------------------------------------------------------------------
// Reporting
// ---------------------------------------------------------------------------
int g_safe = 0, g_info = 0, g_find = 0, g_failed = 0;

// A [SAFE] line asserts a boundary held; `held==false` is an audit FAILURE (escape / crash path).
void Safe(bool held, const std::string& name, const std::string& observed) {
    std::printf("  [%s] %-46s | %s\n", held ? "SAFE" : "FAIL", name.c_str(), observed.c_str());
    if (held)
        ++g_safe;
    else
        ++g_failed;
}
void Info(const std::string& name, const std::string& observed) {
    std::printf("  [INFO] %-46s | %s\n", name.c_str(), observed.c_str());
    ++g_info;
}
void Find(const std::string& name, const std::string& observed) {
    std::printf("  [FIND] %-46s | %s\n", name.c_str(), observed.c_str());
    ++g_find;
}
void Section(const char* title) {
    std::printf("\n== %s ==\n", title);
}

const char* S(Status s) {
    switch (s) {
        case Status::Ok:
            return "Ok";
        case Status::InvalidArgument:
            return "InvalidArgument";
        case Status::NotFound:
            return "NotFound";
        case Status::PermissionDenied:
            return "PermissionDenied";
        case Status::Unsupported:
            return "Unsupported";
        case Status::OutOfRange:
            return "OutOfRange";
        case Status::RateLimited:
            return "RateLimited";
        case Status::BufferTooSmall:
            return "BufferTooSmall";
        case Status::Internal:
            return "Internal";
    }
    return "?";
}
Status St(int64_t ret) {
    return static_cast<Status>(static_cast<int32_t>(ret));
}

// ---------------------------------------------------------------------------
// Guest-encoding helpers (all operate on the BytecodeBuilder)
// ---------------------------------------------------------------------------
int64_t F2B(float f) {  // float -> zero-extended 32-bit pattern, for St32
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof(u));
    return static_cast<int64_t>(u);
}
float B2F(int64_t bits) {
    uint32_t u = static_cast<uint32_t>(bits & 0xFFFFFFFFu);
    float f = 0.0f;
    std::memcpy(&f, &u, sizeof(f));
    return f;
}

void StoreF32(BytecodeBuilder& b, uint32_t off, float v) {
    b.PushI(off).PushI(F2B(v)).Emit(Op::St32);
}
void StoreU32(BytecodeBuilder& b, uint32_t off, uint32_t v) {
    b.PushI(off).PushI(v).Emit(Op::St32);
}
void StoreI64(BytecodeBuilder& b, uint32_t off, int64_t v) {
    b.PushI(off).PushI(v).Emit(Op::St64);
}

// Push the four host-call window operands in the order the VM pops them (it pops retLen, retOff,
// argsLen, argsOff — i.e. push argsOff,argsLen,retOff,retLen), then HostCall.
void HC(BytecodeBuilder& b, int64_t aOff, int64_t aLen, int64_t rOff, int64_t rLen, CallId id) {
    b.PushI(aOff).PushI(aLen).PushI(rOff).PushI(rLen).HostCall(id);
}

std::vector<uint8_t> Img(const BytecodeBuilder& b) {
    return b.Build();
}

// ---------------------------------------------------------------------------
// The headless world under test
// ---------------------------------------------------------------------------
struct Sim {
    World world;
    gameapi::SimClock clock;
    gameapi::ObjectiveStore objectives;
    std::unique_ptr<physics::IPhysicsWorld> physics = physics::MakeReferencePhysicsWorld();
    std::unique_ptr<gameplay::PhysicsWorldQuery> query;

    Entity agent;
    gameapi::EntityId agentId = 0;
    gameapi::EntityId t1 = 0, t2 = 0, t3 = 0;

    Sim() {
        clock.fixedDt = 0.1;
        clock.tick = 7;
        clock.seconds = 0.7;

        // Agent at origin, tagged bit 1.
        agent = world.CreateEntity();
        world.AddComponent<TransformComponent>(agent);
        gameapi::GameTag at;
        at.Set(1);
        world.AddComponent<gameapi::GameTag>(agent, at);
        agentId = gameapi::ToEntityId(agent);

        t1 = MakeTarget(3.0f, 0.0f, 0.0f, 5);
        t2 = MakeTarget(10.0f, 0.0f, 0.0f, 5);
        t3 = MakeTarget(0.0f, 4.0f, 0.0f, 7);

        objectives.Set(42, 100);

        // A static physics box at (5,0,0), linked to an ECS entity so a raycast maps back to it.
        physics::BodyDesc desc;
        desc.motion = physics::MotionType::Static;
        desc.shape = physics::ShapeType::Box;
        desc.halfExtents[0] = desc.halfExtents[1] = desc.halfExtents[2] = 1.0f;
        desc.position[0] = 5.0f;
        const physics::BodyId wall = physics->CreateBody(desc);
        Entity wallE = world.CreateEntity();
        physics::RigidBodyComponent rb;
        rb.desc = desc;
        rb.body = wall;
        world.AddComponent<physics::RigidBodyComponent>(wallE, rb);

        query = std::make_unique<gameplay::PhysicsWorldQuery>(physics.get(), &world);
    }

    gameapi::EntityId MakeTarget(float x, float y, float z, uint32_t tag) {
        Entity e = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(e);
        t.position[0] = x;
        t.position[1] = y;
        t.position[2] = z;
        gameapi::GameTag g;
        g.Set(tag);
        world.AddComponent<gameapi::GameTag>(e, g);
        return gameapi::ToEntityId(e);
    }

    GameApi MakeApi(CapabilitySet caps, bool withQuery = true, uint32_t maxHostPerTick = 4096, uint32_t maxComms = 64,
                    uint32_t maxLogs = 64) {
        GameApiConfig cfg;
        cfg.world = &world;
        cfg.clock = &clock;
        cfg.objectives = &objectives;
        cfg.worldQuery = withQuery ? query.get() : nullptr;
        cfg.self = agentId;
        cfg.capabilities = caps;
        cfg.maxHostCallsPerTick = maxHostPerTick;
        cfg.maxCommsPerTick = maxComms;
        cfg.maxLogsPerTick = maxLogs;
        return GameApi(cfg);
    }

    float AgentX() {
        const TransformComponent* t = world.GetComponent<TransformComponent>(agent);
        return t ? t->position[0] : -999.0f;
    }
};

SandboxPolicy Policy(CapabilitySet caps, uint32_t mem = 4096) {
    SandboxPolicy p;
    p.fuel = 1'000'000;
    p.memoryBytes = mem;
    p.stackSlots = 256;
    p.callDepth = 16;
    p.maxHostCalls = 100'000;
    p.capabilities = caps;
    return p;
}

// Run an image once against `api` (fresh tick), return the VM result.
RunResult RunGuest(GameApi& api, const std::vector<uint8_t>& image, const SandboxPolicy& policy, int64_t arg = 0) {
    api.BeginTick();
    GameApiGateway gw(&api);
    RefVm vm;
    std::string err;
    if (!vm.LoadModule(image.data(), image.size(), &err)) {
        std::printf("  [FAIL] module load: %s\n", err.c_str());
        ++g_failed;
        return {};
    }
    return vm.Run(policy, gw, 0, arg);
}

// =====================================================================================
// PART A — exposed API boundary: drive every CallId from real guest code
// =====================================================================================
void AuditApiBoundary(Sim& sim) {
    Section("PART A — exposed API boundary (16 host-calls, PlayerDefault capabilities)");
    const auto caps = CapabilitySet::PlayerDefault();
    const auto pol = Policy(caps);

    // GetTick -> TickResult{tick} at ret[0..8); guest returns the tick value.
    {
        BytecodeBuilder b;
        HC(b, 0, 0, 0, 8, CallId::GetTick);
        b.Emit(Op::Pop);                           // drop status
        b.PushI(0).Emit(Op::Ld64).Emit(Op::Halt);  // return mem[0..8) = tick
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Info("GetTick [Time]", "ret tick=" + std::to_string(r.ret) + " (world tick=7), trap=" + ToString(r.trap));
        Safe(r.trap == TrapReason::None && r.ret == 7, "GetTick returns authoritative clock",
             "tick=" + std::to_string(r.ret));
    }
    // GetTimeSeconds -> double bits; decode.
    {
        BytecodeBuilder b;
        HC(b, 0, 0, 0, 8, CallId::GetTimeSeconds);
        b.Emit(Op::Pop);
        b.PushI(0).Emit(Op::Ld64).Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        double secs = 0.0;
        std::memcpy(&secs, &r.ret, sizeof(secs));
        Info("GetTimeSeconds [Time]", "ret seconds=" + std::to_string(secs));
    }
    // Self -> EntityId; compare to known agentId.
    {
        BytecodeBuilder b;
        HC(b, 0, 0, 0, 8, CallId::Self);
        b.Emit(Op::Pop);
        b.PushI(0).Emit(Op::Ld64).Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(static_cast<gameapi::EntityId>(r.ret) == sim.agentId, "Self returns controlled entity id",
             "id=" + std::to_string(static_cast<uint64_t>(r.ret)));
    }
    // IsValid(self) -> BoolResult; and IsValid(garbage) -> 0.
    {
        BytecodeBuilder b;
        StoreI64(b, 0, static_cast<int64_t>(sim.agentId));
        HC(b, 0, 8, 16, 4, CallId::IsValid);
        b.Emit(Op::Pop);
        b.PushI(16).Emit(Op::Ld32).Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Info("IsValid(self) [Observe]", "valid=" + std::to_string(r.ret));

        BytecodeBuilder b2;
        StoreI64(b2, 0, 0x00000000DEADBEEFLL);
        HC(b2, 0, 8, 16, 4, CallId::IsValid);
        b2.Emit(Op::Pop);
        b2.PushI(16).Emit(Op::Ld32).Emit(Op::Halt);
        GameApi api2 = sim.MakeApi(caps);
        RunResult r2 = RunGuest(api2, Img(b2), pol);
        Safe(r2.ret == 0, "IsValid(bogus id) reports invalid", "valid=" + std::to_string(r2.ret));
    }
    // GetPosition(self) -> PositionResult{x,y,z}; agent is at origin -> x==0.
    {
        BytecodeBuilder b;
        StoreI64(b, 0, static_cast<int64_t>(sim.agentId));
        HC(b, 0, 8, 16, 12, CallId::GetPosition);
        b.Emit(Op::Pop);
        b.PushI(16).Emit(Op::Ld32).Emit(Op::Halt);  // x bits
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Info("GetPosition(self) [Observe]", "x=" + std::to_string(B2F(r.ret)));
    }
    // QueryByTag(5) -> EntityListHeader{count,capacity} + ids. Two targets carry tag 5.
    {
        BytecodeBuilder b;
        StoreU32(b, 0, 5);                               // QueryByTagArgs.tag
        HC(b, 0, 4, 16, 8 + 8 * 4, CallId::QueryByTag);  // header + room for 4 ids
        b.Emit(Op::Pop);
        b.PushI(16).Emit(Op::Ld32).Emit(Op::Halt);  // header.count
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.ret == 2, "QueryByTag(5) finds the two tagged props", "count=" + std::to_string(r.ret));
    }
    // SenseRadius(4) from origin -> entities within 4 units (t1 at 3, t3 at 4; t2 at 10 excluded).
    {
        BytecodeBuilder b;
        StoreF32(b, 0, 4.0f);
        HC(b, 0, 4, 16, 8 + 8 * 8, CallId::SenseRadius);
        b.Emit(Op::Pop);
        b.PushI(16).Emit(Op::Ld32).Emit(Op::Halt);  // count
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.ret == 2, "SenseRadius(4) inclusive-radius count",
             "count=" + std::to_string(r.ret) + " (expect t1@3,t3@4)");
    }
    // SenseNearest(radius=100, tag=5) -> nearest tag-5 entity is t1 at distance 3.
    {
        BytecodeBuilder b;
        StoreF32(b, 0, 100.0f);
        StoreU32(b, 4, 5);
        HC(b, 0, 8, 16, 16, CallId::SenseNearest);
        b.Emit(Op::Pop);
        b.PushI(16 + 8).Emit(Op::Ld32).Emit(Op::Halt);  // distance float at result+8
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(std::abs(B2F(r.ret) - 3.0f) < 1e-4f, "SenseNearest(tag5) picks closest",
             "distance=" + std::to_string(B2F(r.ret)));
    }
    // MoveTo((5,0,0), 2.0) -> records an Actuate intent; verify via the intent queue.
    {
        BytecodeBuilder b;
        StoreF32(b, 0, 5.0f);
        StoreF32(b, 4, 0.0f);
        StoreF32(b, 8, 0.0f);
        StoreF32(b, 12, 2.0f);
        HC(b, 0, 16, 0, 0, CallId::MoveTo);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(St(r.ret) == Status::Ok && api.Intents().Size() == 1, "MoveTo records one Actuate intent",
             "status=" + std::string(S(St(r.ret))) + " intents=" + std::to_string(api.Intents().Size()));
    }
    // Stop -> intent.
    {
        BytecodeBuilder b;
        HC(b, 0, 0, 0, 0, CallId::Stop);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Info("Stop [Actuate]",
             "status=" + std::string(S(St(r.ret))) + " intents=" + std::to_string(api.Intents().Size()));
    }
    // SetActionFlag(3, on) -> intent; and the out-of-range action -> OutOfRange.
    {
        BytecodeBuilder b;
        StoreU32(b, 0, 3);
        StoreU32(b, 4, 1);
        HC(b, 0, 8, 0, 0, CallId::SetActionFlag);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Info("SetActionFlag(3,on) [Actuate]", "status=" + std::string(S(St(r.ret))));

        BytecodeBuilder b2;
        StoreU32(b2, 0, 99);  // action >= 32
        StoreU32(b2, 4, 1);
        HC(b2, 0, 8, 0, 0, CallId::SetActionFlag);
        b2.Emit(Op::Halt);
        GameApi api2 = sim.MakeApi(caps);
        RunResult r2 = RunGuest(api2, Img(b2), pol);
        Safe(St(r2.ret) == Status::OutOfRange, "SetActionFlag(action>=32) rejected", S(St(r2.ret)));
    }
    // SendSignal(channel=2, code=7, payload=1.5) -> Comms intent.
    {
        BytecodeBuilder b;
        StoreU32(b, 0, 2);
        StoreU32(b, 4, 7);  // int32 code
        StoreF32(b, 8, 1.5f);
        HC(b, 0, 12, 0, 0, CallId::SendSignal);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Info("SendSignal [Comms]",
             "status=" + std::string(S(St(r.ret))) + " intents=" + std::to_string(api.Intents().Size()));
    }
    // GetObjective(42) -> state 100.
    {
        BytecodeBuilder b;
        StoreU32(b, 0, 42);
        HC(b, 0, 4, 16, 4, CallId::GetObjective);
        b.Emit(Op::Pop);
        b.PushI(16).Emit(Op::Ld32).Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.ret == 100, "GetObjective(42) reads world state", "state=" + std::to_string(r.ret));

        BytecodeBuilder b2;
        StoreU32(b2, 0, 999);  // unknown objective
        HC(b2, 0, 4, 16, 4, CallId::GetObjective);
        b2.Emit(Op::Halt);
        GameApi api2 = sim.MakeApi(caps);
        RunResult r2 = RunGuest(api2, Img(b2), pol);
        Safe(St(r2.ret) == Status::NotFound, "GetObjective(unknown) -> NotFound", S(St(r2.ret)));
    }
    // ReportProgress(42, +5) -> Tasks intent.
    {
        BytecodeBuilder b;
        StoreU32(b, 0, 42);
        StoreU32(b, 4, static_cast<uint32_t>(5));
        HC(b, 0, 8, 0, 0, CallId::ReportProgress);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Info("ReportProgress [Tasks]",
             "status=" + std::string(S(St(r.ret))) + " intents=" + std::to_string(api.Intents().Size()));
    }
    // Log: write "hi" after the LogArgs header and emit it.
    {
        BytecodeBuilder b;
        StoreU32(b, 0, 2);  // level
        StoreU32(b, 4, 2);  // len
        b.PushI(8).PushI('h').Emit(Op::St8);
        b.PushI(9).PushI('i').Emit(Op::St8);
        HC(b, 0, 10, 0, 0, CallId::Log);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        const bool logged = !api.LogRing().empty() && api.LogRing().back() == "hi";
        Safe(St(r.ret) == Status::Ok && logged, "Log captures guest bytes",
             "ring.back='" + (api.LogRing().empty() ? "" : api.LogRing().back()) + "'");
    }
    // Raycast from (0,0,0) along +x -> hits the static wall at x=5 (face at x=4).
    {
        BytecodeBuilder b;
        StoreF32(b, 0, 0.0f);
        StoreF32(b, 4, 0.0f);
        StoreF32(b, 8, 0.0f);  // origin
        StoreF32(b, 12, 1.0f);
        StoreF32(b, 16, 0.0f);
        StoreF32(b, 20, 0.0f);    // dir +x
        StoreF32(b, 24, 100.0f);  // maxDistance
        HC(b, 0, 28, 32, 48, CallId::Raycast);
        b.Emit(Op::Pop);
        b.PushI(32).Emit(Op::Ld32).Emit(Op::Halt);  // RaycastResult.hit
        GameApi api = sim.MakeApi(caps);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.ret == 1, "Raycast(+x) strikes the physical wall", "hit=" + std::to_string(r.ret));

        // Same call with NO worldQuery wired -> Unsupported (not a crash).
        BytecodeBuilder b2;
        StoreF32(b2, 12, 1.0f);
        StoreF32(b2, 24, 100.0f);
        HC(b2, 0, 28, 32, 48, CallId::Raycast);
        b2.Emit(Op::Halt);
        GameApi api2 = sim.MakeApi(caps, /*withQuery=*/false);
        RunResult r2 = RunGuest(api2, Img(b2), pol);
        Safe(St(r2.ret) == Status::Unsupported, "Raycast w/o physics -> Unsupported", S(St(r2.ret)));
    }
}

// =====================================================================================
// PART B — the ISA / "language" surface
// =====================================================================================
void AuditIsa(Sim& sim) {
    Section("PART B — ISA / language surface (no host-calls; NullGateway implicit)");
    const auto caps = CapabilitySet::PlayerDefault();
    const auto pol = Policy(caps);

    auto run = [&](const BytecodeBuilder& b, int64_t arg = 0) {
        GameApi api = sim.MakeApi(caps);
        return RunGuest(api, Img(b), pol, arg);
    };

    // Integer arithmetic incl. wrapping (defined as 2's-complement, no UB).
    {
        BytecodeBuilder b;
        b.PushI(0x7FFFFFFFFFFFFFFFLL).PushI(1).Emit(Op::Add).Emit(Op::Halt);
        Info("Add overflow wraps (INT64_MAX+1)", "ret=" + std::to_string(run(b).ret) + " (==INT64_MIN)");
    }
    {
        BytecodeBuilder b;
        b.PushI(-9223372036854775807LL - 1).PushI(-1).Emit(Op::Div).Emit(Op::Halt);
        RunResult r = run(b);
        Safe(r.trap == TrapReason::None && r.ret == (-9223372036854775807LL - 1), "Div INT64_MIN/-1 defined (no UB)",
             "ret=" + std::to_string(r.ret));
    }
    {
        BytecodeBuilder b;
        b.PushI(-9223372036854775807LL - 1).PushI(-1).Emit(Op::Mod).Emit(Op::Halt);
        Safe(run(b).ret == 0, "Mod INT64_MIN/-1 == 0 (no UB)", "ret=0");
    }
    // Bitwise + shifts (shift amount masked to 0..63).
    {
        BytecodeBuilder b;
        b.PushI(1).PushI(64).Emit(Op::Shl).Emit(Op::Halt);
        Info("Shl by 64 masks to 0 (1<<0)", "ret=" + std::to_string(run(b).ret));
    }
    {
        BytecodeBuilder b;
        b.PushI(-8).PushI(1).Emit(Op::Shr).Emit(Op::Halt);
        Safe(run(b).ret == -4, "Shr arithmetic + portable (-8>>1==-4)", "ret=-4");
    }
    // Float: IEEE-754 doubles via bit reinterpret; /0 -> inf (not a trap).
    {
        BytecodeBuilder b;
        b.PushF(1.0).PushF(0.0).Emit(Op::FDiv).Emit(Op::Halt);
        RunResult r = run(b);
        double d;
        std::memcpy(&d, &r.ret, 8);
        Safe(r.trap == TrapReason::None && std::isinf(d), "FDiv by zero -> +inf (no trap)", "ret=inf");
    }
    {
        BytecodeBuilder b;  // F2I(NaN)==0, F2I(+1e300) saturates to INT64_MAX
        b.PushF(std::nan("")).Emit(Op::F2I).Emit(Op::Halt);
        Safe(run(b).ret == 0, "F2I(NaN) == 0 (defined)", "ret=0");
    }
    {
        BytecodeBuilder b;
        b.PushF(1e300).Emit(Op::F2I).Emit(Op::Halt);
        Safe(run(b).ret == 0x7FFFFFFFFFFFFFFFLL, "F2I(1e300) saturates to INT64_MAX", "ret=INT64_MAX");
    }
    // Control flow: Call/Ret and a counted loop (already covered by tests; documented here).
    {
        BytecodeBuilder b;
        const auto fn = b.NewLabel();
        b.PushI(41).Call(fn).Emit(Op::Halt);
        b.Bind(fn);
        b.PushI(1).Emit(Op::Add).Emit(Op::Ret);
        Safe(run(b).ret == 42, "Call/Ret frame works", "ret=42");
    }
    // Locals: 16 per frame; index 16 is out of range -> IllegalInstruction.
    {
        BytecodeBuilder b;
        b.PushI(5).StLoc(16).Emit(Op::Halt);
        Safe(run(b).trap == TrapReason::IllegalInstruction, "Local index 16 (>=16) traps", "IllegalInstruction");
    }

    Info("ISA shape",
         "stack VM, 46 opcodes (0..45), int64 cells + IEEE-754 f64 bits, 16 locals/frame, flat byte arena");
    Info("NO high-level language frontend",
         "guests are bytecode (BytecodeBuilder); no C/Rust/WASM standard is accepted yet");
    Info("Deliberately absent primitives",
         "no heap/alloc, no indirect/computed call, no syscalls, no FMA, no pointer to host");
}

// =====================================================================================
// PART C — security boundary, adversarial guests
// =====================================================================================
void AuditSecurity(Sim& sim) {
    Section("PART C — security boundary conditions (adversarial player code)");
    const auto player = CapabilitySet::PlayerDefault();
    const auto observer = CapabilitySet::ObserverOnly();
    const auto pol = Policy(player);
    const uint32_t MEM = 4096;

    // C1. argsLen==0 with a wild argsOffset: the VM skips the offset bounds-check when len==0; the
    // gateway must hand args=nullptr and dispatch must refuse to read -> no OOB, no crash.
    {
        BytecodeBuilder b;
        HC(b, /*aOff*/ 0xFFFFFFF0LL, /*aLen*/ 0, 0, 0, CallId::MoveTo);  // MoveTo needs 16 args bytes
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.trap == TrapReason::None && St(r.ret) == Status::InvalidArgument,
             "argsLen=0 + wild offset -> InvalidArgument",
             "trap=" + std::string(ToString(r.trap)) + " status=" + S(St(r.ret)));
    }
    // C2. retLen==0 with wild retOffset on a writing call: must not write, no OOB.
    {
        BytecodeBuilder b;
        StoreI64(b, 0, static_cast<int64_t>(sim.agentId));
        HC(b, 0, 8, /*rOff*/ 0xFFFFFFF0LL, /*rLen*/ 0, CallId::GetPosition);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.trap == TrapReason::None && St(r.ret) == Status::BufferTooSmall,
             "retLen=0 + wild offset -> BufferTooSmall",
             "trap=" + std::string(ToString(r.trap)) + " status=" + S(St(r.ret)));
    }
    // C3. Negative (== huge unsigned) argsLen -> window check traps BEFORE the gateway.
    {
        BytecodeBuilder b;
        HC(b, 0, /*aLen*/ -1, 0, 0, CallId::MoveTo);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.trap == TrapReason::BadMemoryAccess, "argsLen=-1 (huge) -> BadMemoryAccess", ToString(r.trap));
    }
    // C4. ret window straddling the arena end -> trap (cannot write one byte past the arena).
    {
        BytecodeBuilder b;
        StoreI64(b, 0, static_cast<int64_t>(sim.agentId));
        HC(b, 0, 8, /*rOff*/ MEM - 4, /*rLen*/ 12, CallId::GetPosition);  // 12-byte result won't fit in 4
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.trap == TrapReason::BadMemoryAccess, "ret window straddling arena end -> trap", ToString(r.trap));
    }
    // C5. ret window in-bounds but SMALLER than the result struct -> BufferTooSmall, and the byte
    // just past the declared window must be untouched (no partial overspill). We probe with a guest
    // that writes a canary, calls with retLen=4 (< 12-byte PositionResult), then reads the canary.
    {
        BytecodeBuilder b;
        StoreI64(b, 0, static_cast<int64_t>(sim.agentId));
        b.PushI(100).PushI(0xAB).Emit(Op::St8);                     // canary at mem[100]
        HC(b, 0, 8, /*rOff*/ 96, /*rLen*/ 4, CallId::GetPosition);  // window [96,100), canary at 100
        b.Emit(Op::Pop);
        b.PushI(100).Emit(Op::Ld8).Emit(Op::Halt);  // read canary back
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.ret == 0xAB, "undersized ret buffer does not overspill window",
             "canary=" + std::to_string(r.ret) + " (0xAB=171 expected, intact)");
    }
    // C6. Unknown / non-callable CallId -> PermissionDenied (CapabilityFor -> Count_), delivered as
    // a value (never a trap), and never reaches the typed facade.
    {
        BytecodeBuilder b;
        HC(b, 0, 0, 0, 0, static_cast<CallId>(9999));  // id outside the table
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.trap == TrapReason::None && St(r.ret) == Status::PermissionDenied,
             "unknown CallId 9999 -> PermissionDenied", "status=" + std::string(S(St(r.ret))));
    }
    // C7. Capability denial: an Observer guest tries MoveTo. Denied as a value; world untouched.
    {
        BytecodeBuilder b;
        StoreF32(b, 0, 5.0f);
        StoreF32(b, 12, 9.0f);
        HC(b, 0, 16, 0, 0, CallId::MoveTo);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(observer);
        RunResult r = RunGuest(api, Img(b), Policy(observer));
        Safe(r.trap == TrapReason::None && St(r.ret) == Status::PermissionDenied && api.Intents().Empty(),
             "ungranted Actuate denied (no intent)",
             "status=" + std::string(S(St(r.ret))) + " intents=" + std::to_string(api.Intents().Size()));
    }
    // C8. Defense in depth: policy GRANTS Actuate but the facade is Observer -> intersection denies.
    {
        BytecodeBuilder b;
        StoreF32(b, 0, 5.0f);
        StoreF32(b, 12, 9.0f);
        HC(b, 0, 16, 0, 0, CallId::MoveTo);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(observer);                  // facade: no Actuate
        RunResult r = RunGuest(api, Img(b), Policy(player));  // policy: grants Actuate
        Safe(St(r.ret) == Status::PermissionDenied && api.Intents().Empty(), "defense-in-depth: facade still denies",
             S(St(r.ret)));
    }
    // C9. No ambient authority: a guest scribbles its whole arena but makes no host-call.
    {
        BytecodeBuilder b;
        const auto loop = b.NewLabel(), end = b.NewLabel();
        b.PushI(0).StLoc(0);
        b.Bind(loop);
        b.LdLoc(0).PushI(static_cast<int64_t>(MEM)).Emit(Op::Ges).Jnz(end);
        b.LdLoc(0).LdLoc(0).Emit(Op::St8);
        b.LdLoc(0).PushI(1).Emit(Op::Add).StLoc(0);
        b.Jmp(loop);
        b.Bind(end);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player);
        const float before = sim.AgentX();
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(r.trap == TrapReason::None && r.hostCalls == 0 && api.Intents().Empty() && sim.AgentX() == before,
             "scribble arena, no host-call -> world untouched", "hostCalls=" + std::to_string(r.hostCalls));
    }
    // C10. State isolation between runs on the SAME RefVm: run A leaves a value in memory; run B
    // sees a zeroed arena (per-Run locals, no bleed).
    {
        RefVm vm;
        BytecodeBuilder a;
        a.PushI(0).PushI(0xAB).Emit(Op::St8).Emit(Op::Halt);  // mem[0]=0xAB
        BytecodeBuilder b2;
        b2.PushI(0).Emit(Op::Ld8).Emit(Op::Halt);  // read mem[0]
        std::string err;
        GameApi api = sim.MakeApi(player);
        GameApiGateway gw(&api);
        const auto ia = a.Build();
        const auto ib = b2.Build();
        vm.LoadModule(ia.data(), ia.size(), &err);
        vm.Run(pol, gw, 0, 0);
        vm.LoadModule(ib.data(), ib.size(), &err);
        RunResult r = vm.Run(pol, gw, 0, 0);
        Safe(r.ret == 0, "fresh arena each Run (no cross-run bleed)", "mem[0] on run B = " + std::to_string(r.ret));
    }
    // C11. Aliased args/ret windows (SenseNearest: args 8B and ret 16B both at offset 0). Dispatch
    // copies args out before writing ret, so the overlap cannot corrupt the inputs.
    {
        BytecodeBuilder b;
        StoreF32(b, 0, 100.0f);
        StoreU32(b, 4, 5);
        HC(b, 0, 8, 0, 16, CallId::SenseNearest);  // args & ret overlap at [0,16)
        b.Emit(Op::Pop);
        b.PushI(8).Emit(Op::Ld32).Emit(Op::Halt);  // distance at result+8
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(std::abs(B2F(r.ret) - 3.0f) < 1e-4f, "overlapping args/ret windows stay correct",
             "distance=" + std::to_string(B2F(r.ret)));
    }
    // C12. Log is data, never a format string: feed printf metacharacters as the message.
    {
        const char* evil = "%n%n%s%x%p";
        const uint32_t n = static_cast<uint32_t>(std::strlen(evil));
        BytecodeBuilder b;
        StoreU32(b, 0, 2);
        StoreU32(b, 4, n);
        for (uint32_t i = 0; i < n; ++i)
            b.PushI(8 + i).PushI(static_cast<int64_t>(static_cast<uint8_t>(evil[i]))).Emit(Op::St8);
        HC(b, 0, 8 + n, 0, 0, CallId::Log);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), pol);
        const bool ok = St(r.ret) == Status::Ok && !api.LogRing().empty() && api.LogRing().back() == evil;
        Safe(ok, "Log treats %-metachars as literal data",
             "ring.back='" + (api.LogRing().empty() ? "" : api.LogRing().back()) + "'");
    }
    // C13. Per-tick rate limit (facade): with maxCommsPerTick=3, the 4th SendSignal -> RateLimited.
    {
        BytecodeBuilder b;
        StoreU32(b, 0, 1);
        StoreU32(b, 4, 0);
        StoreF32(b, 8, 0.0f);
        for (int i = 0; i < 4; ++i) {
            HC(b, 0, 12, 0, 0, CallId::SendSignal);
            if (i < 3)
                b.Emit(Op::Pop);  // keep the 4th status as the result
        }
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player, true, 4096, /*maxComms*/ 3);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(St(r.ret) == Status::RateLimited, "Comms per-tick quota enforced (4th denied)", S(St(r.ret)));
    }
    // C14. Per-tick host-call budget (facade): maxHostCallsPerTick=2, the 3rd ANY call -> RateLimited.
    {
        BytecodeBuilder b;
        for (int i = 0; i < 3; ++i) {
            HC(b, 0, 0, 0, 8, CallId::GetTick);
            if (i < 2)
                b.Emit(Op::Pop);
        }
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player, true, /*maxHostPerTick*/ 2);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(St(r.ret) == Status::RateLimited, "host-call per-tick budget enforced (3rd denied)", S(St(r.ret)));
    }
    // C15. NaN / non-finite arguments are rejected at the facade (anti-grief / anti-NaN-poisoning).
    {
        BytecodeBuilder b;
        StoreF32(b, 0, std::nanf(""));  // target.x = NaN
        StoreF32(b, 12, 1.0f);
        HC(b, 0, 16, 0, 0, CallId::MoveTo);
        b.Emit(Op::Halt);
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), pol);
        Safe(St(r.ret) == Status::InvalidArgument && api.Intents().Empty(), "MoveTo(NaN) rejected, no intent",
             S(St(r.ret)));
    }
    // C16. Tiny arena vs a wide load: width must not underflow the bounds check.
    {
        BytecodeBuilder b;
        b.PushI(0).Emit(Op::Ld32).Emit(Op::Halt);  // load 4 bytes from a 2-byte arena
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), Policy(player, /*mem*/ 2));
        Safe(r.trap == TrapReason::BadMemoryAccess, "Ld32 on 2-byte arena traps (no width underflow)",
             ToString(r.trap));
    }
    // C17. Call-stack depth cap: unbounded recursion traps StackOverflow (no native stack blowout).
    {
        BytecodeBuilder b;
        const auto rec = b.NewLabel();
        b.Bind(rec);
        b.Call(rec);  // tail-less self-recursion
        b.Emit(Op::Halt);
        SandboxPolicy p = Policy(player);
        p.callDepth = 32;
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(b), p);
        Safe(r.trap == TrapReason::StackOverflow, "unbounded recursion -> StackOverflow", ToString(r.trap));
    }
    // C18. Control-flow integrity: every branch target is bounds-checked against code length, so a
    // guest cannot transfer control outside its own code. An in-range Jmp resolves; out-of-range
    // jumps trap IllegalInstruction (proven in tests/sandbox/test_ref_vm.cpp::OutOfRangeJumpTraps).
    {
        BytecodeBuilder j;
        const auto far = j.NewLabel();
        j.Jmp(far).Emit(Op::Nop);
        j.Bind(far);
        j.PushI(99).Emit(Op::Halt);
        GameApi api = sim.MakeApi(player);
        RunResult r = RunGuest(api, Img(j), pol);
        Safe(r.trap == TrapReason::None && r.ret == 99, "in-range Jmp resolves; targets are bounds-checked",
             "trap=" + std::string(ToString(r.trap)));
    }
}

// =====================================================================================
// PART D — asymmetric host-call cost (the headline finding), measured on two world sizes
// =====================================================================================
void AuditAsymmetricCost() {
    Section("PART D — host-call cost asymmetry (algorithmic-DoS surface)");
    const auto caps = CapabilitySet::PlayerDefault();
    const auto pol = Policy(caps);

    // A guest that issues K SenseRadius(1e6) calls — each forces a FULL-WORLD O(N) scan in the
    // facade but costs the guest a FLAT 50 fuel regardless of N.
    const int K = 50;
    auto senseGuest = [&]() {
        BytecodeBuilder b;
        StoreF32(b, 0, 1.0e6f);  // radius covering everything
        for (int i = 0; i < K; ++i) {
            HC(b, 0, 4, 16, 8, CallId::SenseRadius);  // header-only ret (capacity 0)
            b.Emit(Op::Pop);
        }
        b.Emit(Op::Halt);
        return Img(b);
    };

    auto measure = [&](int targets) {
        World world;
        gameapi::SimClock clock;
        clock.fixedDt = 0.1;
        Entity agent = world.CreateEntity();
        world.AddComponent<TransformComponent>(agent);
        for (int i = 0; i < targets; ++i) {
            Entity e = world.CreateEntity();
            auto& t = world.AddComponent<TransformComponent>(e);
            t.position[0] = static_cast<float>(i);
        }
        GameApiConfig cfg;
        cfg.world = &world;
        cfg.clock = &clock;
        cfg.self = gameapi::ToEntityId(agent);
        cfg.capabilities = caps;
        GameApi api(cfg);
        api.BeginTick();
        GameApiGateway gw(&api);
        RefVm vm;
        std::string err;
        const auto img = senseGuest();
        vm.LoadModule(img.data(), img.size(), &err);
        return vm.Run(pol, gw, 0, 0);
    };

    RunResult small = measure(4);
    RunResult big = measure(4000);
    Info("K SenseRadius calls, world N=4",
         "fuelUsed=" + std::to_string(small.fuelUsed) + " hostCalls=" + std::to_string(small.hostCalls));
    Info("K SenseRadius calls, world N=4000",
         "fuelUsed=" + std::to_string(big.fuelUsed) + " hostCalls=" + std::to_string(big.hostCalls));
    Find("flat fuel independent of host work", "N=4 and N=4000 cost the guest identical fuel (" +
                                                   std::to_string(small.fuelUsed) +
                                                   "); host work scales O(K*N). Fuel does not price the scan.");
}

}  // namespace

int main() {
    Logger::Initialize();
    std::printf("================ Blame Engine — Player-Code Sandbox Security Audit ================\n");
    std::printf("ABI: Game API v%u.%u | ISA: NBVM v%u | reference physics backend\n", gameapi::kGameApiVersionMajor,
                gameapi::kGameApiVersionMinor, kModuleVersion);
    std::printf("Headless world booted: agent + 3 tagged props + 1 static physics wall + 1 objective.\n");

    Sim sim;
    AuditApiBoundary(sim);
    AuditIsa(sim);
    AuditSecurity(sim);
    AuditAsymmetricCost();

    std::printf("\n================ SUMMARY ================\n");
    std::printf("  SAFE (boundary held): %d\n", g_safe);
    std::printf("  INFO (documented):    %d\n", g_info);
    std::printf("  FIND (audit notes):   %d\n", g_find);
    std::printf("  FAILED expectations:  %d\n", g_failed);
    Logger::Shutdown();
    return g_failed == 0 ? 0 : 1;
}
