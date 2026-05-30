// sim↔UE5 boundary tests (ADR-0006): wait-free triple buffer + SPSC rings, and the snapshot
// publisher that diffs ECS state into spawn/update/despawn deltas. Covers the single-threaded
// semantics here; cross-thread SPSC correctness is additionally exercised under TSan/ASan in CI.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>

#include "next/boundary/snapshot.h"
#include "next/boundary/snapshot_publisher.h"
#include "next/boundary/spsc_ring.h"
#include "next/boundary/transport.h"
#include "next/boundary/triple_buffer.h"
#include "next/runtime/transform.h"
#include "next/runtime/world.h"

using namespace Next;
using namespace Next::boundary;

namespace {

RenderableComponent Renderable(VisualStateId visual, uint32_t anim = 0) {
    RenderableComponent r;
    r.visual = visual;
    r.animState = anim;
    return r;
}

// ---- triple buffer ----

TEST(TripleBuffer, NothingPublishedYieldsNullptr) {
    TripleBuffer<int> tb;
    EXPECT_EQ(tb.Acquire(), nullptr);
}

TEST(TripleBuffer, PublishThenAcquire) {
    TripleBuffer<int> tb;
    tb.Write() = 10;
    tb.Publish();
    const int* p = tb.Acquire();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 10);
    EXPECT_EQ(tb.Acquire(), nullptr);  // no new frame since
}

TEST(TripleBuffer, SlowConsumerSeesLatest) {
    TripleBuffer<int> tb;
    tb.Write() = 1;
    tb.Publish();
    tb.Write() = 2;
    tb.Publish();
    tb.Write() = 3;
    tb.Publish();  // producer ran ahead; consumer should jump to the freshest
    const int* p = tb.Acquire();
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(*p, 3);
}

// ---- SPSC ring ----

TEST(SpscRing, FifoOrder) {
    SpscRing<int> r(4);
    int out = 0;
    EXPECT_FALSE(r.Pop(out));
    EXPECT_TRUE(r.Push(1));
    EXPECT_TRUE(r.Push(2));
    EXPECT_TRUE(r.Pop(out));
    EXPECT_EQ(out, 1);
    EXPECT_TRUE(r.Pop(out));
    EXPECT_EQ(out, 2);
    EXPECT_FALSE(r.Pop(out));
}

TEST(SpscRing, FullDropsPush) {
    SpscRing<int> r(4);
    EXPECT_TRUE(r.Push(1));
    EXPECT_TRUE(r.Push(2));
    EXPECT_TRUE(r.Push(3));
    EXPECT_TRUE(r.Push(4));
    EXPECT_FALSE(r.Push(5));  // full
}

// ---- snapshot publisher ----

TEST(SnapshotPublisher, SpawnThenSteadyThenMoveThenDespawn) {
    World world;
    SnapshotPublisher pub;
    SnapshotBlock blk;

    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);
    world.AddComponent<RenderableComponent>(e, Renderable(7));

    pub.BuildDelta(world, 1, 0.0, blk);
    ASSERT_EQ(blk.spawns.size(), 1u);
    EXPECT_EQ(blk.spawns[0].visual, 7u);
    EXPECT_TRUE(blk.updates.empty());
    EXPECT_TRUE(blk.despawns.empty());

    // Static entity -> empty delta.
    pub.BuildDelta(world, 2, 0.016, blk);
    EXPECT_TRUE(blk.spawns.empty());
    EXPECT_TRUE(blk.updates.empty());
    EXPECT_TRUE(blk.despawns.empty());

    // Move it far enough to survive quantization -> one update.
    world.GetComponent<TransformComponent>(e)->position[0] = 5.0f;
    pub.BuildDelta(world, 3, 0.032, blk);
    EXPECT_TRUE(blk.spawns.empty());
    ASSERT_EQ(blk.updates.size(), 1u);
    EXPECT_EQ(blk.updates[0].id, static_cast<EntityId>(e));

    // Destroy it -> one despawn.
    world.DestroyEntity(e);
    pub.BuildDelta(world, 4, 0.048, blk);
    ASSERT_EQ(blk.despawns.size(), 1u);
    EXPECT_EQ(blk.despawns[0], static_cast<EntityId>(e));
    EXPECT_EQ(pub.TrackedCount(), 0u);
}

TEST(SnapshotPublisher, NonFiniteTransformIsStableNoSpuriousUpdate) {
    // A NaN coordinate must be sanitized before packing: otherwise NaN != NaN makes the entity
    // look "changed" every tick (spurious updates) and quantization becomes non-deterministic.
    World world;
    SnapshotPublisher pub;
    SnapshotBlock blk;
    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    t.position[0] = std::numeric_limits<float>::quiet_NaN();
    t.rotation[1] = std::numeric_limits<float>::quiet_NaN();
    world.AddComponent<RenderableComponent>(e, Renderable(1));

    pub.BuildDelta(world, 1, 0.0, blk);
    ASSERT_EQ(blk.spawns.size(), 1u);

    pub.BuildDelta(world, 2, 0.016, blk);  // nothing changed -> must be empty
    EXPECT_TRUE(blk.spawns.empty());
    EXPECT_TRUE(blk.updates.empty());
    EXPECT_TRUE(blk.despawns.empty());
}

TEST(SnapshotPublisher, VisualSwapBecomesDespawnPlusSpawn) {
    World world;
    SnapshotPublisher pub;
    SnapshotBlock blk;

    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);
    world.AddComponent<RenderableComponent>(e, Renderable(1));
    pub.BuildDelta(world, 1, 0.0, blk);

    world.GetComponent<RenderableComponent>(e)->visual = 2;  // swap mesh variant
    pub.BuildDelta(world, 2, 0.016, blk);
    ASSERT_EQ(blk.despawns.size(), 1u);
    ASSERT_EQ(blk.spawns.size(), 1u);
    EXPECT_EQ(blk.spawns[0].visual, 2u);
    EXPECT_TRUE(blk.updates.empty());
}

TEST(SnapshotPublisher, VisualSwapAppliedInContractOrderKeepsActor) {
    World world;
    SnapshotPublisher pub;
    SnapshotBlock blk;
    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);
    world.AddComponent<RenderableComponent>(e, Renderable(1));

    // Model a UE5 consumer that honors the apply-order contract: despawns, then spawns.
    std::map<EntityId, VisualStateId> actors;
    auto apply = [&](const SnapshotBlock& b) {
        for (EntityId id : b.despawns)
            actors.erase(id);
        for (const SpawnRecord& s : b.spawns)
            actors[s.id] = s.visual;
    };

    pub.BuildDelta(world, 1, 0.0, blk);
    apply(blk);
    const EntityId id = static_cast<EntityId>(e);
    ASSERT_EQ(actors.count(id), 1u);
    EXPECT_EQ(actors[id], 1u);

    world.GetComponent<RenderableComponent>(e)->visual = 2;  // swap -> despawn(id)+spawn(id,2)
    pub.BuildDelta(world, 2, 0.016, blk);
    apply(blk);
    ASSERT_EQ(actors.count(id), 1u);  // still present after the swap...
    EXPECT_EQ(actors[id], 2u);        // ...with the new visual
}

TEST(SnapshotPublisher, SpawnsAreOrderedByEntityId) {
    World world;
    SnapshotPublisher pub;
    SnapshotBlock blk;
    for (int i = 0; i < 6; ++i) {
        Entity e = world.CreateEntity();
        world.AddComponent<TransformComponent>(e);
        world.AddComponent<RenderableComponent>(e, Renderable(static_cast<VisualStateId>(i)));
    }
    pub.BuildDelta(world, 1, 0.0, blk);
    ASSERT_EQ(blk.spawns.size(), 6u);
    EXPECT_TRUE(std::is_sorted(blk.spawns.begin(), blk.spawns.end(),
                               [](const SpawnRecord& a, const SpawnRecord& b) { return a.id < b.id; }));
}

// ---- transport integration ----

TEST(InProcessTransportIntegration, SnapshotEventCommandRoundTrip) {
    InProcessTransport transport;

    // sim publishes a snapshot + a cosmetic event
    SnapshotBlock& w = transport.BeginWrite();
    w.Reset(1, 0.0);
    w.spawns.push_back(SpawnRecord{42, 3, TransformPacked{}});
    transport.PublishSnapshot();
    EXPECT_TRUE(transport.PushEvent(GameEvent{1, 42, {0, 0, 0, 0}}));

    // UE5 consumes them
    const SnapshotBlock* s = transport.AcquireSnapshot();
    ASSERT_NE(s, nullptr);
    ASSERT_EQ(s->spawns.size(), 1u);
    EXPECT_EQ(s->spawns[0].id, 42u);
    GameEvent ev{};
    ASSERT_TRUE(transport.PopEvent(ev));
    EXPECT_EQ(ev.subject, 42u);

    // UE5 sends a command back; sim drains it
    EXPECT_TRUE(transport.PushCommand(InputCmd{9, {1, 2, 3, 4}}));
    InputCmd cmd{};
    ASSERT_TRUE(transport.PopCommand(cmd));
    EXPECT_EQ(cmd.type, 9u);
    EXPECT_FLOAT_EQ(cmd.a[0], 1.0f);
}

TEST(InProcessTransportIntegration, PublisherDrivesTransport) {
    World world;
    SnapshotPublisher pub;
    InProcessTransport transport;

    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);
    world.AddComponent<RenderableComponent>(e, Renderable(5));

    pub.PublishTo(transport, world, 1, 0.0);
    const SnapshotBlock* s = transport.AcquireSnapshot();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->tick, 1u);
    ASSERT_EQ(s->spawns.size(), 1u);
    EXPECT_EQ(s->spawns[0].visual, 5u);
}

}  // namespace
