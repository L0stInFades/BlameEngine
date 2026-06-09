// Game API contract tests (ADR-0007): capability gating, deterministic reads, write-as-intent,
// the tick-boundary resolver, and the flat ABI dispatch round-trip.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

#include "next/gameapi/abi_dispatch.h"
#include "next/gameapi/capability.h"
#include "next/gameapi/components.h"
#include "next/gameapi/game_api.h"
#include "next/gameapi/intent_resolver.h"
#include "next/gameapi/objective_store.h"
#include "next/gameapi/sim_clock.h"
#include "next/gameapi/world_query.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

using namespace Next;
using namespace Next::gameapi;

namespace {

// Build a world with one controllable agent at the origin and a few tagged props around it.
struct Fixture {
    World world;
    SimClock clock;
    ObjectiveStore objectives;
    Entity agent;

    Fixture() {
        clock.fixedDt = 0.1;  // 10 Hz keeps movement arithmetic exact for assertions
        agent = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(agent);
        t.position[0] = t.position[1] = t.position[2] = 0.0f;
        world.AddComponent<GameTag>(agent);  // agent itself untagged by default
    }

    Entity SpawnTagged(float x, float y, float z, uint32_t tag) {
        Entity e = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(e);
        t.position[0] = x;
        t.position[1] = y;
        t.position[2] = z;
        GameTag g;
        g.Set(tag);
        world.AddComponent<GameTag>(e, g);
        return e;
    }

    GameApi MakeApi(CapabilitySet caps) {
        GameApiConfig cfg;
        cfg.world = &world;
        cfg.clock = &clock;
        cfg.objectives = &objectives;
        cfg.self = ToEntityId(agent);
        cfg.capabilities = caps;
        return GameApi(cfg);
    }
};

TEST(GameApiAbi, VersionReflectsAdditiveWaterStateCall) {
    EXPECT_EQ(kGameApiVersionMajor, 1u);
    EXPECT_EQ(kGameApiVersionMinor, 1u);
    EXPECT_EQ(kGameApiAbiVersion, 0x00010001u);
    EXPECT_EQ(static_cast<uint32_t>(CallId::GetWaterState), 17u);
}

TEST(GameApiCapabilities, MissingCapabilityIsDenied) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::ObserverOnly());  // no Actuate
    api.BeginTick();
    EXPECT_EQ(api.MoveTo(Vec3Abi{1, 0, 0}, 1.0f), Status::PermissionDenied);
    // A denied call records no intent and does not consume the host-call budget.
    EXPECT_TRUE(api.Intents().Empty());
    EXPECT_EQ(api.HostCallsThisTick(), 0u);
}

TEST(GameApiRateLimit, WorldScanQuotaBoundsONScans) {
    // F-1 fix: the O(N) world scans (QueryByTag / SenseRadius / SenseNearest) share a dedicated,
    // tighter per-tick quota — a guest cannot turn flat-priced host-calls into unbounded host work.
    Fixture f;
    f.SpawnTagged(1.0f, 0.0f, 0.0f, 3);

    GameApiConfig cfg;
    cfg.world = &f.world;
    cfg.clock = &f.clock;
    cfg.objectives = &f.objectives;
    cfg.self = ToEntityId(f.agent);
    cfg.capabilities = CapabilitySet::PlayerDefault();
    cfg.maxWorldScansPerTick = 2;
    GameApi api(cfg);
    api.BeginTick();

    EntityId ids[8];
    uint32_t count = 0;
    EXPECT_EQ(api.SenseRadius(10.0f, ids, 8, count), Status::Ok);  // scan 1
    EXPECT_EQ(api.QueryByTag(3, ids, 8, count), Status::Ok);       // scan 2
    EXPECT_EQ(api.WorldScansThisTick(), 2u);

    // Quota spent: every further scan is RateLimited and does no host work...
    EntityId nearest = kInvalidEntity;
    float distance = 0.0f;
    EXPECT_EQ(api.SenseNearest(10.0f, 3, nearest, distance), Status::RateLimited);
    EXPECT_EQ(api.SenseRadius(10.0f, ids, 8, count), Status::RateLimited);
    EXPECT_EQ(api.WorldScansThisTick(), 2u);

    // ...while non-scan calls still proceed (only the scan quota is exhausted).
    EntityId self = kInvalidEntity;
    EXPECT_EQ(api.GetSelf(self), Status::Ok);

    // BeginTick resets the quota with the other per-tick counters.
    api.BeginTick();
    EXPECT_EQ(api.SenseRadius(10.0f, ids, 8, count), Status::Ok);
    EXPECT_EQ(api.WorldScansThisTick(), 1u);

    // An invalid call is rejected BEFORE the quota is charged.
    EXPECT_EQ(api.SenseRadius(-1.0f, ids, 8, count), Status::InvalidArgument);
    EXPECT_EQ(api.WorldScansThisTick(), 1u);
}

TEST(GameApiCapabilities, GrantedCapabilityAllows) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    EXPECT_EQ(api.MoveTo(Vec3Abi{1, 0, 0}, 1.0f), Status::Ok);
    EXPECT_EQ(api.Intents().Size(), 1u);
}

TEST(GameApiTime, ReadsDeterministicClock) {
    Fixture f;
    f.clock.tick = 42;
    f.clock.seconds = 4.2;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    uint64_t tick = 0;
    double secs = 0.0;
    EXPECT_EQ(api.GetTick(tick), Status::Ok);
    EXPECT_EQ(api.GetTimeSeconds(secs), Status::Ok);
    EXPECT_EQ(tick, 42u);
    EXPECT_DOUBLE_EQ(secs, 4.2);
}

TEST(GameApiObserve, QueryByTagIsSortedAndComplete) {
    Fixture f;
    Entity c = f.SpawnTagged(3, 0, 0, 5);
    Entity a = f.SpawnTagged(1, 0, 0, 5);
    Entity b = f.SpawnTagged(2, 0, 0, 5);
    f.SpawnTagged(9, 0, 0, 6);  // different tag, must not match

    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    std::vector<EntityId> buf(8);
    uint32_t count = 0;
    EXPECT_EQ(api.QueryByTag(5, buf.data(), 8, count), Status::Ok);
    ASSERT_EQ(count, 3u);
    std::vector<EntityId> expected{ToEntityId(a), ToEntityId(b), ToEntityId(c)};
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(buf[0], expected[0]);
    EXPECT_EQ(buf[1], expected[1]);
    EXPECT_EQ(buf[2], expected[2]);
}

TEST(GameApiObserve, QueryByTagReportsTotalWhenBufferTooSmall) {
    Fixture f;
    f.SpawnTagged(1, 0, 0, 5);
    f.SpawnTagged(2, 0, 0, 5);
    f.SpawnTagged(3, 0, 0, 5);
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    std::vector<EntityId> buf(2);
    uint32_t count = 0;
    EXPECT_EQ(api.QueryByTag(5, buf.data(), 2, count), Status::BufferTooSmall);
    EXPECT_EQ(count, 3u);  // total reported even though only 2 written
}

TEST(GameApiObserve, InvalidTagRejected) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    std::vector<EntityId> buf(4);
    uint32_t count = 0;
    EXPECT_EQ(api.QueryByTag(64, buf.data(), 4, count), Status::InvalidArgument);
}

TEST(GameApiSense, NearestPicksClosestTagged) {
    Fixture f;
    f.SpawnTagged(10, 0, 0, 3);
    Entity near = f.SpawnTagged(2, 0, 0, 3);
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    EntityId found = kInvalidEntity;
    float dist = 0.0f;
    EXPECT_EQ(api.SenseNearest(100.0f, 3, found, dist), Status::Ok);
    EXPECT_EQ(found, ToEntityId(near));
    EXPECT_FLOAT_EQ(dist, 2.0f);
}

TEST(GameApiSense, NearestRespectsRadius) {
    Fixture f;
    f.SpawnTagged(50, 0, 0, 3);
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    EntityId found = kInvalidEntity;
    float dist = 0.0f;
    EXPECT_EQ(api.SenseNearest(5.0f, 3, found, dist), Status::NotFound);
    EXPECT_EQ(found, kInvalidEntity);
}

TEST(GameApiSense, NearestRadiusIsInclusive) {
    Fixture f;
    f.SpawnTagged(5, 0, 0, 3);  // exactly at distance 5
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    EntityId found = kInvalidEntity;
    float dist = 0.0f;
    EXPECT_EQ(api.SenseNearest(5.0f, 3, found, dist), Status::Ok);  // boundary included
    EXPECT_NE(found, kInvalidEntity);
    EXPECT_FLOAT_EQ(dist, 5.0f);
}

TEST(GameApiSense, NearestExcludesNonFiniteAndAgreesWithRadius) {
    Fixture f;
    // An entity with a NaN coordinate (e.g. a physics blow-up) must not be picked, and the two
    // Sense calls must agree on excluding it.
    Entity bad = f.world.CreateEntity();
    auto& t = f.world.AddComponent<TransformComponent>(bad);
    t.position[0] = std::numeric_limits<float>::quiet_NaN();
    GameTag g;
    g.Set(3);
    f.world.AddComponent<GameTag>(bad, g);

    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    EntityId found = ToEntityId(bad);
    float dist = -1.0f;
    EXPECT_EQ(api.SenseNearest(1000.0f, 3, found, dist), Status::NotFound);
    EXPECT_EQ(found, kInvalidEntity);

    std::vector<EntityId> buf(8);
    uint32_t count = 99;
    EXPECT_EQ(api.SenseRadius(1000.0f, buf.data(), 8, count), Status::Ok);
    EXPECT_EQ(count, 0u);  // SenseRadius excludes it too — the two agree
}

// A scriptable IWorldQuery for testing the Raycast facade in isolation from physics.
struct MockWorldQuery : gameapi::IWorldQuery {
    RaycastResult next{};
    int calls = 0;
    RaycastResult Raycast(const float[3], const float[3], float) override {
        ++calls;
        return next;
    }
};

TEST(GameApiSense, RaycastDeniedWithoutSenseCapability) {
    Fixture f;
    MockWorldQuery query;
    GameApiConfig cfg;
    cfg.world = &f.world;
    cfg.clock = &f.clock;
    cfg.worldQuery = &query;
    cfg.self = ToEntityId(f.agent);
    cfg.capabilities = CapabilitySet().Grant(Capability::Observe);  // no Sense
    GameApi api(cfg);
    api.BeginTick();

    RaycastResult out{};
    const Vec3Abi o{0, 0, 0};
    const Vec3Abi d{1, 0, 0};
    EXPECT_EQ(api.Raycast(o, d, 10.0f, out), Status::PermissionDenied);
    EXPECT_EQ(query.calls, 0);
}

TEST(GameApiSense, RaycastUnsupportedWhenNoQueryWired) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());  // Fixture wires no worldQuery
    api.BeginTick();
    RaycastResult out{};
    const Vec3Abi o{0, 0, 0};
    const Vec3Abi d{1, 0, 0};
    EXPECT_EQ(api.Raycast(o, d, 10.0f, out), Status::Unsupported);
}

TEST(GameApiSense, RaycastDelegatesToWorldQuery) {
    Fixture f;
    MockWorldQuery query;
    query.next.hit = 1;
    query.next.entity = 4242;
    query.next.distance = 7.5f;
    query.next.point = {1, 2, 3};
    GameApiConfig cfg;
    cfg.world = &f.world;
    cfg.clock = &f.clock;
    cfg.worldQuery = &query;
    cfg.self = ToEntityId(f.agent);
    cfg.capabilities = CapabilitySet::PlayerDefault();
    GameApi api(cfg);
    api.BeginTick();

    RaycastResult out{};
    const Vec3Abi o{0, 0, 0};
    const Vec3Abi d{1, 0, 0};
    EXPECT_EQ(api.Raycast(o, d, 10.0f, out), Status::Ok);
    EXPECT_EQ(query.calls, 1);
    EXPECT_EQ(out.hit, 1u);
    EXPECT_EQ(out.entity, 4242u);
    EXPECT_FLOAT_EQ(out.distance, 7.5f);
    EXPECT_FLOAT_EQ(out.point.z, 3.0f);

    // NaN direction is rejected before the query is consulted.
    const Vec3Abi bad{std::numeric_limits<float>::quiet_NaN(), 0, 0};
    EXPECT_EQ(api.Raycast(o, bad, 10.0f, out), Status::InvalidArgument);
}

TEST(AbiDispatch, RaycastRoundTrip) {
    Fixture f;
    MockWorldQuery query;
    query.next.hit = 1;
    query.next.entity = 99;
    query.next.distance = 2.0f;
    GameApiConfig cfg;
    cfg.world = &f.world;
    cfg.clock = &f.clock;
    cfg.worldQuery = &query;
    cfg.self = ToEntityId(f.agent);
    cfg.capabilities = CapabilitySet::PlayerDefault();
    GameApi api(cfg);
    api.BeginTick();

    RaycastArgs args{Vec3Abi{0, 0, 0}, Vec3Abi{0, 0, 1}, 50.0f};
    RaycastResult result{};
    const Status s = AbiDispatch::HostCall(api, CallId::Raycast, &args, sizeof(args), &result, sizeof(result));
    EXPECT_EQ(s, Status::Ok);
    EXPECT_EQ(result.hit, 1u);
    EXPECT_EQ(result.entity, 99u);
}

// ---- W10: GetWaterState (Sense-gated water query into the ABI) ----

// A scriptable IWaterQuery for testing the GetWaterState facade in isolation from engine/water.
struct MockWaterQuery : gameapi::IWaterQuery {
    WaterStateResult next{};
    int calls = 0;
    WaterStateResult QueryWater(const float[3]) override {
        ++calls;
        return next;
    }
};

TEST(GameApiWater, GetWaterStateDeniedWithoutSenseCapability) {
    Fixture f;
    MockWaterQuery query;
    GameApiConfig cfg;
    cfg.world = &f.world;
    cfg.clock = &f.clock;
    cfg.waterQuery = &query;
    cfg.self = ToEntityId(f.agent);
    cfg.capabilities = CapabilitySet().Grant(Capability::Observe);  // no Sense
    GameApi api(cfg);
    api.BeginTick();

    WaterStateResult out{};
    EXPECT_EQ(api.GetWaterState(Vec3Abi{0, 0, 0}, out), Status::PermissionDenied);
    EXPECT_EQ(query.calls, 0);  // denied before the query is consulted
}

TEST(GameApiWater, GetWaterStateUnsupportedWhenNoQueryWired) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());  // Fixture wires no waterQuery
    api.BeginTick();
    WaterStateResult out{};
    EXPECT_EQ(api.GetWaterState(Vec3Abi{0, 0, 0}, out), Status::Unsupported);
}

TEST(GameApiWater, GetWaterStateDelegatesAndRejectsNaN) {
    Fixture f;
    MockWaterQuery query;
    query.next.inWater = 1;
    query.next.submerged = 1;
    query.next.surfaceHeight = 2.5f;
    query.next.submersionDepth = 1.25f;
    query.next.flowVelocity = {3.0f, 0.0f, -1.0f};
    query.next.flags = 0x6;  // some WaterFlags bits
    GameApiConfig cfg;
    cfg.world = &f.world;
    cfg.clock = &f.clock;
    cfg.waterQuery = &query;
    cfg.self = ToEntityId(f.agent);
    cfg.capabilities = CapabilitySet::PlayerDefault();
    GameApi api(cfg);
    api.BeginTick();

    WaterStateResult out{};
    EXPECT_EQ(api.GetWaterState(Vec3Abi{1, -1, 2}, out), Status::Ok);
    EXPECT_EQ(query.calls, 1);
    EXPECT_EQ(out.inWater, 1u);
    EXPECT_EQ(out.submerged, 1u);
    EXPECT_FLOAT_EQ(out.surfaceHeight, 2.5f);
    EXPECT_FLOAT_EQ(out.submersionDepth, 1.25f);
    EXPECT_FLOAT_EQ(out.flowVelocity.x, 3.0f);
    EXPECT_EQ(out.flags, 0x6u);

    // NaN point is rejected before the query is consulted.
    const Vec3Abi bad{std::numeric_limits<float>::quiet_NaN(), 0, 0};
    EXPECT_EQ(api.GetWaterState(bad, out), Status::InvalidArgument);
    EXPECT_EQ(query.calls, 1);  // not consulted again
}

TEST(AbiDispatch, GetWaterStateRoundTrip) {
    Fixture f;
    MockWaterQuery query;
    query.next.inWater = 1;
    query.next.submerged = 1;
    query.next.submersionDepth = 0.75f;
    GameApiConfig cfg;
    cfg.world = &f.world;
    cfg.clock = &f.clock;
    cfg.waterQuery = &query;
    cfg.self = ToEntityId(f.agent);
    cfg.capabilities = CapabilitySet::PlayerDefault();
    GameApi api(cfg);
    api.BeginTick();

    GetWaterStateArgs args{Vec3Abi{5, -2, 5}};
    WaterStateResult result{};
    const Status s = AbiDispatch::HostCall(api, CallId::GetWaterState, &args, sizeof(args), &result, sizeof(result));
    EXPECT_EQ(s, Status::Ok);
    EXPECT_EQ(result.submerged, 1u);
    EXPECT_FLOAT_EQ(result.submersionDepth, 0.75f);

    // A too-small ret buffer fails closed (BufferTooSmall), never a partial write.
    uint8_t tiny[4] = {0, 0, 0, 0};
    EXPECT_EQ(AbiDispatch::HostCall(api, CallId::GetWaterState, &args, sizeof(args), tiny, sizeof(tiny)),
              Status::BufferTooSmall);
}

TEST(GameApiActuate, MoveIntentResolvesDeterministically) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    DefaultIntentResolver resolver(&f.objectives);

    api.BeginTick();
    ASSERT_EQ(api.MoveTo(Vec3Abi{10, 0, 0}, 10.0f), Status::Ok);  // 10 u/s, dt=0.1 -> 1 u/tick
    resolver.Apply(f.world, api.Intents().Items());
    api.ClearIntents();
    DefaultIntentResolver::StepKinematics(f.world, f.clock.fixedDt);

    const TransformComponent* t = f.world.GetComponent<TransformComponent>(f.agent);
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->position[0], 1.0f);

    // After 10 more steps it arrives and the goal deactivates.
    for (int i = 0; i < 10; ++i)
        DefaultIntentResolver::StepKinematics(f.world, f.clock.fixedDt);
    t = f.world.GetComponent<TransformComponent>(f.agent);
    EXPECT_FLOAT_EQ(t->position[0], 10.0f);
    const MoveTarget* mt = f.world.GetComponent<MoveTarget>(f.agent);
    ASSERT_NE(mt, nullptr);
    EXPECT_EQ(mt->active, 0);
}

TEST(GameApiTasks, ReportProgressAdvancesObjectiveAfterResolve) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    DefaultIntentResolver resolver(&f.objectives);

    api.BeginTick();
    EXPECT_EQ(api.ReportProgress(7, 3), Status::Ok);
    int32_t state = -1;
    EXPECT_EQ(api.GetObjective(7, state), Status::NotFound);  // not applied yet

    resolver.Apply(f.world, api.Intents().Items());
    EXPECT_EQ(api.GetObjective(7, state), Status::Ok);
    EXPECT_EQ(state, 3);
}

TEST(GameApiRateLimit, HostCallBudgetEnforced) {
    Fixture f;
    GameApiConfig cfg;
    cfg.world = &f.world;
    cfg.clock = &f.clock;
    cfg.objectives = &f.objectives;
    cfg.self = ToEntityId(f.agent);
    cfg.capabilities = CapabilitySet::PlayerDefault();
    cfg.maxHostCallsPerTick = 2;
    GameApi api(cfg);

    api.BeginTick();
    uint64_t tick = 0;
    EXPECT_EQ(api.GetTick(tick), Status::Ok);
    EXPECT_EQ(api.GetTick(tick), Status::Ok);
    EXPECT_EQ(api.GetTick(tick), Status::RateLimited);  // budget exhausted
    api.BeginTick();                                    // resets
    EXPECT_EQ(api.GetTick(tick), Status::Ok);
}

// ---- Flat ABI dispatch round-trips (what the sandbox gateway drives) ----

TEST(AbiDispatch, MoveToRoundTrip) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    MoveToArgs args{Vec3Abi{5, 6, 7}, 2.5f};
    const Status s = AbiDispatch::HostCall(api, CallId::MoveTo, &args, sizeof(args), nullptr, 0);
    EXPECT_EQ(s, Status::Ok);
    ASSERT_EQ(api.Intents().Size(), 1u);
    const Intent& in = api.Intents().Items()[0];
    EXPECT_EQ(in.type, IntentType::MoveTo);
    EXPECT_FLOAT_EQ(in.vec.x, 5.0f);
    EXPECT_FLOAT_EQ(in.scalar, 2.5f);
}

TEST(AbiDispatch, GetPositionRoundTrip) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    EntityArg args{ToEntityId(f.agent)};
    PositionResult result{};
    const Status s = AbiDispatch::HostCall(api, CallId::GetPosition, &args, sizeof(args), &result, sizeof(result));
    EXPECT_EQ(s, Status::Ok);
    EXPECT_FLOAT_EQ(result.position.x, 0.0f);
}

TEST(AbiDispatch, QueryByTagEncodesHeaderAndIds) {
    Fixture f;
    f.SpawnTagged(1, 0, 0, 9);
    f.SpawnTagged(2, 0, 0, 9);
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();

    QueryByTagArgs args{9};
    alignas(8) uint8_t ret[sizeof(EntityListHeader) + 4 * sizeof(EntityId)] = {};
    const Status s = AbiDispatch::HostCall(api, CallId::QueryByTag, &args, sizeof(args), ret, sizeof(ret));
    EXPECT_EQ(s, Status::Ok);
    EntityListHeader header{};
    std::memcpy(&header, ret, sizeof(header));
    EXPECT_EQ(header.count, 2u);
    EXPECT_GE(header.capacity, 2u);
}

TEST(AbiDispatch, UnknownArgsTooShortRejected) {
    Fixture f;
    GameApi api = f.MakeApi(CapabilitySet::PlayerDefault());
    api.BeginTick();
    uint8_t tooShort[1] = {};
    const Status s = AbiDispatch::HostCall(api, CallId::MoveTo, tooShort, sizeof(tooShort), nullptr, 0);
    EXPECT_EQ(s, Status::InvalidArgument);
}

}  // namespace
