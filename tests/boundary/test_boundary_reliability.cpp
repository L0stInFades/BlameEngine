// Reliable snapshot protocol (B1 fix, W13). The sim->UE5 transport is latest-wins and DROPS
// intermediate snapshots whenever the consumer renders slower than the sim publishes. The old
// publisher diffed against the last PUBLISHED frame and advanced its baseline unconditionally, so a
// dropped frame's spawn/despawn/move records were lost forever and the UE5 mirror desynced
// permanently. The fix: deltas are computed against the last ACKNOWLEDGED snapshot, with a keyframe
// fallback. These tests prove the mirror CONVERGES to the authoritative state even under heavy frame
// loss, and that a never-acking consumer simply receives keyframes (full, always-correct state).

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <vector>

#include "next/boundary/render_clock.h"
#include "next/boundary/snapshot.h"
#include "next/boundary/snapshot_publisher.h"
#include "next/boundary/snapshot_receiver.h"
#include "next/boundary/transport.h"
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

bool MirrorsEqual(const std::map<EntityId, EntityRenderState>& a, const std::map<EntityId, EntityRenderState>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (const auto& [id, sa] : a) {
        const auto it = b.find(id);
        if (it == b.end()) {
            return false;
        }
        const EntityRenderState& sb = it->second;
        if (sa.visual != sb.visual || sa.animState != sb.animState || !TransformPackedEqual(sa.xform, sb.xform)) {
            return false;
        }
    }
    return true;
}

// Ground truth: a fresh receiver fed a single keyframe of the final world (the full authoritative
// render set), so its mirror is "what UE5 should show", built through the same packing path.
std::map<EntityId, EntityRenderState> GroundTruthMirror(World& world) {
    SnapshotPublisher gt;  // Reliable; first build is a keyframe (baseline empty -> all spawns)
    SnapshotBlock kb;
    gt.BuildDelta(world, 0, 0.0, kb);
    SnapshotReceiver gtr;
    gtr.Apply(kb);
    return gtr.Mirror();
}

}  // namespace

TEST(SnapshotReliability, FirstSnapshotIsAKeyframe) {
    World world;
    SnapshotPublisher pub;  // Reliable (default)
    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);
    world.AddComponent<RenderableComponent>(e, Renderable(7));

    SnapshotBlock blk;
    pub.BuildDelta(world, 1, 0.0, blk);
    EXPECT_TRUE(blk.IsKeyframe());
    EXPECT_EQ(blk.baselineSequence, kNoBaseline);
    ASSERT_EQ(blk.spawns.size(), 1u);
    EXPECT_NE(blk.sequence, kNoBaseline);
}

TEST(SnapshotReliability, AckAdvancesBaselineAndCompresses) {
    World world;
    SnapshotPublisher pub;
    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);
    world.AddComponent<RenderableComponent>(e, Renderable(1));

    SnapshotReceiver rx;
    SnapshotBlock b1;
    pub.BuildDelta(world, 1, 0.0, b1);
    ASSERT_EQ(rx.Apply(b1), SnapshotReceiver::Result::AppliedKeyframe);
    EXPECT_TRUE(pub.Acknowledge(rx.AckSequence()));
    EXPECT_EQ(pub.AckedSequence(), b1.sequence);

    // Move it; the next delta is now against the acked baseline (a compact update, not a keyframe).
    world.GetComponent<TransformComponent>(e)->position[0] = 5.0f;
    SnapshotBlock b2;
    pub.BuildDelta(world, 2, 0.016, b2);
    EXPECT_FALSE(b2.IsKeyframe());
    EXPECT_EQ(b2.baselineSequence, b1.sequence);
    ASSERT_EQ(rx.Apply(b2), SnapshotReceiver::Result::AppliedDelta);
    EXPECT_TRUE(MirrorsEqual(rx.Mirror(), GroundTruthMirror(world)));
}

// The headline B1 proof: with the sim publishing every tick and the consumer rendering only every Kth
// tick (the transport dropping the rest), the mirror still converges EXACTLY to the world.
TEST(SnapshotReliability, MirrorConvergesUnderHeavyFrameLoss) {
    World world;
    std::vector<Entity> ents;
    for (int i = 0; i < 8; ++i) {
        Entity e = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(e);
        t.position[0] = static_cast<float>(i);
        world.AddComponent<RenderableComponent>(e, Renderable(static_cast<VisualStateId>(i + 1)));
        ents.push_back(e);
    }

    SnapshotPublisher pub;  // Reliable
    InProcessTransport transport;
    SnapshotReceiver rx;

    constexpr int kConsumerEvery = 3;  // consumer is 3x slower -> 2 of every 3 snapshots are dropped
    int spawnVisual = 100;
    for (int tick = 1; tick <= 300; ++tick) {
        // Sim tick start: process any acks the consumer sent (so the next delta compresses correctly).
        SnapshotSequence ack = 0;
        while (transport.PopAck(ack)) {
            pub.Acknowledge(ack);
        }
        // Mutate the world: move one entity, periodically spawn / despawn -> exercises all 3 record kinds.
        if (!ents.empty()) {
            world.GetComponent<TransformComponent>(ents[tick % ents.size()])->position[0] += 1.0f;
        }
        if (tick % 7 == 0) {
            Entity e = world.CreateEntity();
            world.AddComponent<TransformComponent>(e);
            world.AddComponent<RenderableComponent>(e, Renderable(static_cast<VisualStateId>(spawnVisual++)));
            ents.push_back(e);
        }
        if (tick % 11 == 0 && ents.size() > 4) {
            world.DestroyEntity(ents.front());
            ents.erase(ents.begin());
        }
        pub.PublishTo(transport, world, static_cast<uint64_t>(tick), tick * 0.016);

        // Consumer runs slowly: acquire the FRESHEST (dropping intermediates), apply, ack.
        if (tick % kConsumerEvery == 0) {
            const SnapshotBlock* s = transport.AcquireSnapshot();
            if (s != nullptr) {
                const SnapshotReceiver::Result r = rx.Apply(*s);
                if (r == SnapshotReceiver::Result::AppliedKeyframe || r == SnapshotReceiver::Result::AppliedDelta) {
                    transport.PushAck(rx.AckSequence());
                }
            }
        }
    }

    // Settle: world now static; let the consumer drain to the latest. A few iterations suffice because
    // each cycle advances the acked baseline by one and the consumer applies the freshest.
    for (int i = 0; i < 8; ++i) {
        SnapshotSequence ack = 0;
        while (transport.PopAck(ack)) {
            pub.Acknowledge(ack);
        }
        pub.PublishTo(transport, world, 1000 + i, 0.0);
        const SnapshotBlock* s = transport.AcquireSnapshot();
        if (s != nullptr) {
            const SnapshotReceiver::Result r = rx.Apply(*s);
            if (r == SnapshotReceiver::Result::AppliedKeyframe || r == SnapshotReceiver::Result::AppliedDelta) {
                transport.PushAck(rx.AckSequence());
            }
        }
    }

    // The mirror EXACTLY equals the authoritative render set — no permanent desync despite the drops.
    EXPECT_TRUE(MirrorsEqual(rx.Mirror(), GroundTruthMirror(world)));
    EXPECT_GT(rx.EntityCount(), 0u);
}

// The dropped-ack BACKSTOP (W13 audit fix): after a baseline is established, EVERY subsequent ack is
// lost AND the consumer never re-acks. Without the keyframe-fallback the publisher would diff against the
// stale baseline forever while the consumer (having advanced past it) skips every delta — a permanent
// desync/livelock. The publisher's keyframe fallback (fires once unacked snapshots pile up) must break it.
TEST(SnapshotReliability, KeyframeFallbackRecoversWhenEveryAckIsLost) {
    World world;
    std::vector<Entity> ents;
    for (int i = 0; i < 5; ++i) {
        Entity e = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(e);
        t.position[0] = static_cast<float>(i);
        world.AddComponent<RenderableComponent>(e, Renderable(static_cast<VisualStateId>(i + 1)));
        ents.push_back(e);
    }
    SnapshotPublisher pub;
    SnapshotReceiver rx;

    // Establish a baseline: the first keyframe is applied and acked once (this single ack gets through).
    SnapshotBlock kb;
    pub.BuildDelta(world, 1, 0.0, kb);
    ASSERT_EQ(rx.Apply(kb), SnapshotReceiver::Result::AppliedKeyframe);
    ASSERT_TRUE(pub.Acknowledge(rx.AckSequence()));

    // From here EVERY ack is lost (Acknowledge is never called again) and the consumer does NOT re-ack.
    int v = 100;
    for (int tick = 2; tick <= 90; ++tick) {
        world.GetComponent<TransformComponent>(ents[tick % ents.size()])->position[0] += 1.0f;
        if (tick % 6 == 0) {
            Entity e = world.CreateEntity();
            world.AddComponent<TransformComponent>(e);
            world.AddComponent<RenderableComponent>(e, Renderable(static_cast<VisualStateId>(v++)));
            ents.push_back(e);
        }
        SnapshotBlock blk;
        pub.BuildDelta(world, static_cast<uint64_t>(tick), tick * 0.016, blk);
        rx.Apply(blk);  // ack intentionally dropped (never Acknowledge)
    }
    // The periodic keyframe fallback reconverges the mirror despite every ack after the first being lost.
    EXPECT_TRUE(MirrorsEqual(rx.Mirror(), GroundTruthMirror(world)));
}

// A consumer that NEVER acks keeps receiving keyframes (full state), so the latest is always correct.
TEST(SnapshotReliability, NeverAckedSnapshotsAreKeyframesAndStayCorrect) {
    World world;
    Entity e = world.CreateEntity();
    auto& t = world.AddComponent<TransformComponent>(e);
    world.AddComponent<RenderableComponent>(e, Renderable(3));

    SnapshotPublisher pub;
    SnapshotReceiver rx;
    for (int tick = 1; tick <= 20; ++tick) {
        t.position[0] = static_cast<float>(tick);  // moving, never acked
        SnapshotBlock blk;
        pub.BuildDelta(world, static_cast<uint64_t>(tick), 0.0, blk);
        EXPECT_TRUE(blk.IsKeyframe()) << "no ack -> baseline never advances -> every frame is a keyframe";
        ASSERT_EQ(rx.Apply(blk), SnapshotReceiver::Result::AppliedKeyframe);
    }
    EXPECT_TRUE(MirrorsEqual(rx.Mirror(), GroundTruthMirror(world)));
}

// W14: the client follows the server's authoritative tick/seconds from snapshots, monotonically — a
// stale/reordered snapshot can never move client time (or the mirror) backward.
TEST(SnapshotAuthority, ReceiverFollowsServerTimeMonotonically) {
    SnapshotReceiver rx;
    EXPECT_EQ(rx.ServerTick(), 0u);
    EXPECT_DOUBLE_EQ(rx.ServerSeconds(), 0.0);

    SnapshotBlock k;
    k.Reset(10, 1.0);
    k.sequence = 1;
    k.baselineSequence = kNoBaseline;
    k.spawns.push_back(SpawnRecord{1, 5, TransformPacked{}, 0});
    ASSERT_EQ(rx.Apply(k), SnapshotReceiver::Result::AppliedKeyframe);
    EXPECT_EQ(rx.ServerTick(), 10u);
    EXPECT_DOUBLE_EQ(rx.ServerSeconds(), 1.0);

    SnapshotBlock d;
    d.Reset(20, 2.0);
    d.sequence = 2;
    d.baselineSequence = 1;
    ASSERT_EQ(rx.Apply(d), SnapshotReceiver::Result::AppliedDelta);
    EXPECT_EQ(rx.ServerTick(), 20u);
    EXPECT_DOUBLE_EQ(rx.ServerSeconds(), 2.0);

    // A late, reordered snapshot (older sequence + older time) — even posing as a keyframe — is rejected
    // and CANNOT regress authoritative time or reset the mirror.
    SnapshotBlock stale;
    stale.Reset(5, 0.5);
    stale.sequence = 1;
    stale.baselineSequence = kNoBaseline;
    stale.spawns.push_back(SpawnRecord{999, 1, TransformPacked{}, 0});
    EXPECT_EQ(rx.Apply(stale), SnapshotReceiver::Result::SkippedStale);
    EXPECT_EQ(rx.ServerTick(), 20u);            // unchanged
    EXPECT_DOUBLE_EQ(rx.ServerSeconds(), 2.0);  // unchanged
    EXPECT_EQ(rx.Mirror().count(999), 0u);      // the stale keyframe did NOT reset/insert
}

// W14: the render clock follows server time but NEVER runs past it (no extrapolation past authority),
// and snaps forward rather than fast-forwarding through stale time after a stall.
TEST(SnapshotAuthority, RenderClockNeverRunsPastAuthority) {
    const double delay = 0.1;
    RenderClock clock(delay, /*maxBehind*/ 0.5);

    // Server time advances; render time chases the (delayed) authority but never exceeds it and never
    // moves backward.
    double server = 1.0;
    double prev = -1.0;
    for (int i = 0; i < 200; ++i) {
        const double r = clock.Tick(0.016, server);
        EXPECT_LE(r, server - delay + 1e-9) << "render ran past the delayed authority";
        EXPECT_GE(r, prev - 1e-9) << "render time went backward";
        prev = r;
        server += 0.016;
    }
    // After steady chasing it sits right at the interpolation target (server - delay), not ahead.
    EXPECT_NEAR(clock.RenderSeconds(), server - delay, 0.02);

    // A long stall then a big jump in server time: the clock snaps forward to within maxBehind, it
    // does NOT replay all the skipped seconds one frame-dt at a time.
    const double jumped = server + 100.0;
    const double r = clock.Tick(0.016, jumped);
    EXPECT_GE(r, (jumped - delay) - 0.5 - 1e-9);
    EXPECT_LE(r, jumped - delay + 1e-9);
}

TEST(SnapshotReliability, StaleAckIsIgnored) {
    World world;
    Entity e = world.CreateEntity();
    world.AddComponent<TransformComponent>(e);
    world.AddComponent<RenderableComponent>(e, Renderable(1));

    SnapshotPublisher pub;
    SnapshotBlock b1;
    pub.BuildDelta(world, 1, 0.0, b1);
    SnapshotBlock b2;
    pub.BuildDelta(world, 2, 0.016, b2);
    EXPECT_TRUE(pub.Acknowledge(b2.sequence));   // ack the newer
    EXPECT_FALSE(pub.Acknowledge(b1.sequence));  // older ack ignored
    EXPECT_FALSE(pub.Acknowledge(b2.sequence));  // duplicate ignored
    EXPECT_EQ(pub.AckedSequence(), b2.sequence);
}
