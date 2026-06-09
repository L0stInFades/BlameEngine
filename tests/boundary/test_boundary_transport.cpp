// Cross-process snapshot transport (W15): the flat wire codec + the DatagramTransport carrying the
// full reliable protocol (delta-vs-acked + keyframe + acks) over a serialized, lossy datagram link.
// Proves the bytes round-trip, decode FAIL-CLOSED on corruption, and that the UE5 mirror converges to
// the authoritative world across the "wire" even when datagrams (snapshots AND acks) are dropped —
// because the consumer re-sends its cumulative ack each frame, dropped acks self-heal.

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <utility>
#include <vector>

#include "next/boundary/datagram_transport.h"
#include "next/boundary/snapshot.h"
#include "next/boundary/snapshot_codec.h"
#include "next/boundary/snapshot_publisher.h"
#include "next/boundary/snapshot_receiver.h"
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

std::map<EntityId, EntityRenderState> GroundTruthMirror(World& world) {
    SnapshotPublisher gt;
    SnapshotBlock kb;
    gt.BuildDelta(world, 0, 0.0, kb);
    SnapshotReceiver gtr;
    gtr.Apply(kb);
    return gtr.Mirror();
}

class ManualDatagram final : public IDatagram {
public:
    bool Send(const uint8_t* data, size_t len) override {
        sent.emplace_back(data, data + len);
        return true;
    }

    bool Recv(std::vector<uint8_t>& out) override {
        if (incoming_.empty()) {
            return false;
        }
        out = std::move(incoming_.front());
        incoming_.pop_front();
        return true;
    }

    void Inject(std::vector<uint8_t> frame) { incoming_.push_back(std::move(frame)); }

    std::vector<std::vector<uint8_t>> sent;

private:
    std::deque<std::vector<uint8_t>> incoming_;
};

}  // namespace

TEST(SnapshotCodec, RoundTripPreservesAllRecords) {
    SnapshotBlock in;
    in.Reset(123, 4.5);
    in.sequence = 9;
    in.baselineSequence = 8;
    in.spawns.push_back(SpawnRecord{1, 7, TransformPacked{{1, 2, 3}, {10, 20, 30, 32767}, 1024}, 2});
    in.updates.push_back(UpdateRecord{2, TransformPacked{{4, 5, 6}, {0, 0, 0, 32767}, 2048}, 3});
    in.despawns.push_back(5);
    in.despawns.push_back(9);

    std::vector<uint8_t> bytes;
    EncodeSnapshot(in, bytes);
    EXPECT_EQ(bytes.size(),
              sizeof(SnapshotWireHeader) + 1 * sizeof(SpawnRecord) + 1 * sizeof(UpdateRecord) + 2 * sizeof(EntityId));

    SnapshotBlock out;
    ASSERT_TRUE(DecodeSnapshot(bytes.data(), bytes.size(), out));
    EXPECT_EQ(out.tick, 123u);
    EXPECT_DOUBLE_EQ(out.simTimeSeconds, 4.5);
    EXPECT_EQ(out.sequence, 9u);
    EXPECT_EQ(out.baselineSequence, 8u);
    ASSERT_EQ(out.spawns.size(), 1u);
    EXPECT_EQ(out.spawns[0].visual, 7u);
    EXPECT_EQ(out.spawns[0].animState, 2u);
    EXPECT_TRUE(TransformPackedEqual(out.spawns[0].xform, in.spawns[0].xform));
    ASSERT_EQ(out.updates.size(), 1u);
    EXPECT_EQ(out.updates[0].animState, 3u);
    ASSERT_EQ(out.despawns.size(), 2u);
    EXPECT_EQ(out.despawns[1], 9u);
}

TEST(SnapshotCodec, DecodeFailsClosed) {
    SnapshotBlock in;
    in.Reset(1, 0.0);
    in.sequence = 1;
    in.spawns.push_back(SpawnRecord{1, 1, TransformPacked{}, 0});
    std::vector<uint8_t> bytes;
    EncodeSnapshot(in, bytes);

    SnapshotBlock out;
    EXPECT_FALSE(DecodeSnapshot(nullptr, 0, out));
    EXPECT_FALSE(DecodeSnapshot(bytes.data(), bytes.size() - 1, out));  // truncated
    {
        std::vector<uint8_t> longer = bytes;
        longer.push_back(0);
        EXPECT_FALSE(DecodeSnapshot(longer.data(), longer.size(), out));  // trailing garbage
    }
    {
        std::vector<uint8_t> bad = bytes;
        bad[0] ^= 0xFF;  // corrupt magic
        EXPECT_FALSE(DecodeSnapshot(bad.data(), bad.size(), out));
    }
    {
        std::vector<uint8_t> bad = bytes;
        // Set an enormous spawnCount (offset 40 in the header) -> rejected before allocation.
        const uint32_t huge = 0xFFFFFFFFu;
        std::memcpy(bad.data() + 40, &huge, sizeof(huge));
        EXPECT_FALSE(DecodeSnapshot(bad.data(), bad.size(), out));
    }
}

TEST(DatagramTransport, EventCommandAckRoundTripOverLink) {
    InMemoryDatagramLink link;
    DatagramTransport sim(&link.SimSide());
    DatagramTransport view(&link.ViewSide());

    // sim -> view event
    EXPECT_TRUE(sim.PushEvent(GameEvent{0x1234, 42, {1.0f, 2.0f, 3.0f, 4.0f}}));
    GameEvent e{};
    ASSERT_TRUE(view.PopEvent(e));
    EXPECT_EQ(e.type, 0x1234u);
    EXPECT_EQ(e.subject, 42u);
    EXPECT_FLOAT_EQ(e.params[2], 3.0f);

    // view -> sim command
    EXPECT_TRUE(view.PushCommand(InputCmd{7, {9.0f, 0, 0, 0}}));
    InputCmd c{};
    ASSERT_TRUE(sim.PopCommand(c));
    EXPECT_EQ(c.type, 7u);
    EXPECT_FLOAT_EQ(c.a[0], 9.0f);

    // view -> sim ack
    EXPECT_TRUE(view.PushAck(99));
    SnapshotSequence ack = 0;
    ASSERT_TRUE(sim.PopAck(ack));
    EXPECT_EQ(ack, 99u);
}

TEST(DatagramTransport, EventCommandAckUseVersionedWirePayloads) {
    ManualDatagram wire;
    DatagramTransport sender(&wire);

    ASSERT_TRUE(sender.PushEvent(GameEvent{0x1234, 42, {1.0f, 2.0f, 3.0f, 4.0f}}));
    ASSERT_TRUE(sender.PushCommand(InputCmd{7, {9.0f, 8.0f, 7.0f, 6.0f}}));
    ASSERT_TRUE(sender.PushAck(99));
    ASSERT_EQ(wire.sent.size(), 3u);

    EXPECT_EQ(wire.sent[0].size(), 1u + 16u + 28u);  // outer kind + header + event fields, no host pad
    EXPECT_EQ(wire.sent[1].size(), 1u + 16u + 20u);  // outer kind + header + command fields
    EXPECT_EQ(wire.sent[2].size(), 1u + 16u + 8u);   // outer kind + header + ack sequence

    ManualDatagram receiverWire;
    DatagramTransport receiver(&receiverWire);
    receiverWire.Inject(wire.sent[0]);
    receiverWire.Inject(wire.sent[1]);
    receiverWire.Inject(wire.sent[2]);

    GameEvent e{};
    ASSERT_TRUE(receiver.PopEvent(e));
    EXPECT_EQ(e.type, 0x1234u);
    EXPECT_EQ(e.subject, 42u);
    EXPECT_FLOAT_EQ(e.params[3], 4.0f);

    InputCmd c{};
    ASSERT_TRUE(receiver.PopCommand(c));
    EXPECT_EQ(c.type, 7u);
    EXPECT_FLOAT_EQ(c.a[1], 8.0f);

    SnapshotSequence ack = 0;
    ASSERT_TRUE(receiver.PopAck(ack));
    EXPECT_EQ(ack, 99u);
}

TEST(DatagramTransport, EventCommandAckRejectMalformedWirePayloads) {
    ManualDatagram encoded;
    DatagramTransport sender(&encoded);
    ASSERT_TRUE(sender.PushEvent(GameEvent{1, 2, {3.0f, 4.0f, 5.0f, 6.0f}}));
    ASSERT_TRUE(sender.PushCommand(InputCmd{7, {8.0f, 9.0f, 10.0f, 11.0f}}));
    ASSERT_TRUE(sender.PushAck(77));
    ASSERT_EQ(encoded.sent.size(), 3u);

    ManualDatagram receiverWire;
    DatagramTransport receiver(&receiverWire);

    std::vector<uint8_t> badEventMagic = encoded.sent[0];
    badEventMagic[1] ^= 0xFF;  // corrupt inner magic; byte 0 is the outer kind
    receiverWire.Inject(std::move(badEventMagic));
    GameEvent e{};
    EXPECT_FALSE(receiver.PopEvent(e));

    std::vector<uint8_t> badCommandKind = encoded.sent[1];
    badCommandKind[1 + 8] = static_cast<uint8_t>(DatagramKind::Event);  // inner kind mismatches outer kind
    receiverWire.Inject(std::move(badCommandKind));
    InputCmd c{};
    EXPECT_FALSE(receiver.PopCommand(c));

    std::vector<uint8_t> truncatedAck = encoded.sent[2];
    truncatedAck.pop_back();
    receiverWire.Inject(std::move(truncatedAck));
    SnapshotSequence ack = 0;
    EXPECT_FALSE(receiver.PopAck(ack));

    std::vector<uint8_t> badAckPayloadSize = encoded.sent[2];
    std::memset(badAckPayloadSize.data() + 1 + 12, 0, sizeof(uint32_t));
    receiverWire.Inject(std::move(badAckPayloadSize));
    EXPECT_FALSE(receiver.PopAck(ack));

    receiverWire.Inject(encoded.sent[2]);
    ASSERT_TRUE(receiver.PopAck(ack));
    EXPECT_EQ(ack, 77u);
}

// The headline W15 proof: the mirror converges across a serialized, LOSSY datagram link. The consumer
// re-sends its cumulative ack every frame, so dropped acks self-heal; latest-wins + reliable deltas
// handle dropped snapshots.
// Drive a full publish/apply/ack loop over `link` (already configured with whatever loss), mutating the
// world each tick; then settle on a clean link. Returns true iff the mirror converged to the world.
// The consumer re-sends its cumulative ack every frame (standard netcode: dropped acks self-heal).
bool RunDatagramConvergence(InMemoryDatagramLink& link) {
    World world;
    std::vector<Entity> ents;
    for (int i = 0; i < 6; ++i) {
        Entity e = world.CreateEntity();
        auto& t = world.AddComponent<TransformComponent>(e);
        t.position[0] = static_cast<float>(i);
        world.AddComponent<RenderableComponent>(e, Renderable(static_cast<VisualStateId>(i + 1)));
        ents.push_back(e);
    }
    SnapshotPublisher pub;
    DatagramTransport simTx(&link.SimSide());
    DatagramTransport viewTx(&link.ViewSide());
    SnapshotReceiver rx;

    auto consumerStep = [&]() {
        const SnapshotBlock* s = viewTx.AcquireSnapshot();
        if (s != nullptr) {
            rx.Apply(*s);
        }
        if (rx.AckSequence() != kNoBaseline) {
            viewTx.PushAck(rx.AckSequence());  // cumulative re-ack every frame
        }
    };

    int spawnVisual = 200;
    for (int tick = 1; tick <= 400; ++tick) {
        SnapshotSequence ack = 0;
        while (simTx.PopAck(ack)) {
            pub.Acknowledge(ack);
        }
        world.GetComponent<TransformComponent>(ents[tick % ents.size()])->position[0] += 1.0f;
        if (tick % 9 == 0) {
            Entity e = world.CreateEntity();
            world.AddComponent<TransformComponent>(e);
            world.AddComponent<RenderableComponent>(e, Renderable(static_cast<VisualStateId>(spawnVisual++)));
            ents.push_back(e);
        }
        if (tick % 13 == 0 && ents.size() > 3) {
            world.DestroyEntity(ents.front());
            ents.erase(ents.begin());
        }
        pub.PublishTo(simTx, world, static_cast<uint64_t>(tick), tick * 0.016);
        consumerStep();
    }

    link.SetDropEveryNth(0);  // settle on a clean link
    for (int i = 0; i < 20; ++i) {
        SnapshotSequence ack = 0;
        while (simTx.PopAck(ack)) {
            pub.Acknowledge(ack);
        }
        pub.PublishTo(simTx, world, 1000 + i, 0.0);
        consumerStep();
    }
    return MirrorsEqual(rx.Mirror(), GroundTruthMirror(world)) && rx.EntityCount() > 0 && rx.ServerTick() > 0;
}

TEST(DatagramTransport, MirrorConvergesUnderSymmetricLoss) {
    InMemoryDatagramLink link;
    link.SetDropEveryNth(4);  // ~25% of BOTH snapshots and acks
    EXPECT_TRUE(RunDatagramConvergence(link));
}

TEST(DatagramTransport, MirrorConvergesWhenOnlySnapshotsDropped) {
    InMemoryDatagramLink link;
    link.SetDropSimToView(2);  // 50% of snapshots lost; acks clean -> reliable deltas recover the rest
    EXPECT_TRUE(RunDatagramConvergence(link));
}

TEST(DatagramTransport, MirrorConvergesWhenOnlyAcksDropped) {
    InMemoryDatagramLink link;
    link.SetDropViewToSim(2);  // 50% of acks lost; cumulative re-ack self-heals + keyframe fallback backstop
    EXPECT_TRUE(RunDatagramConvergence(link));
}

// W15-A/B invariants the FIFO convergence tests can't exercise: out-of-order datagrams (UDP reorders)
// and held-snapshot stability across a Pump. Hand-feed framed snapshots in arbitrary sequence order.
TEST(DatagramTransport, LatestWinsRejectsReorderAndKeepsHeldStable) {
    InMemoryDatagramLink link;
    DatagramTransport viewTx(&link.ViewSide());  // the consumer
    IDatagram& producer = link.SimSide();        // raw producer endpoint to inject framed datagrams

    auto sendSnap = [&](uint64_t seq, uint64_t tick) {
        SnapshotBlock b;
        b.Reset(tick, 0.0);
        b.sequence = seq;
        b.baselineSequence = kNoBaseline;  // keyframe (self-contained)
        b.spawns.push_back(SpawnRecord{1, 7, TransformPacked{}, 0});
        std::vector<uint8_t> body;
        EncodeSnapshot(b, body);
        std::vector<uint8_t> frame;
        frame.push_back(static_cast<uint8_t>(DatagramKind::Snapshot));
        frame.insert(frame.end(), body.begin(), body.end());
        producer.Send(frame.data(), frame.size());
    };

    // Reordered arrival in one batch: newer (5) then older (3). Latest-wins must surface 5, never 3.
    sendSnap(5, 50);
    sendSnap(3, 30);
    const SnapshotBlock* s = viewTx.AcquireSnapshot();
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->sequence, 5u);
    EXPECT_EQ(viewTx.AcquireSnapshot(), nullptr);  // the older (3) was dropped, nothing newer

    // A late OLDER datagram (4) arriving after we acquired 5 must NOT resurface (monotonic high-water).
    sendSnap(4, 40);
    EXPECT_EQ(viewTx.AcquireSnapshot(), nullptr);

    // Held-snapshot stability: with `s` still held, a newer snapshot (6) + a Pump via PopEvent must NOT
    // mutate the held block; it stays valid until the NEXT AcquireSnapshot.
    sendSnap(6, 60);
    GameEvent ev{};
    EXPECT_FALSE(viewTx.PopEvent(ev));  // pumps the link (decodes 6 into pending_, not the held block)
    EXPECT_EQ(s->sequence, 5u);         // held block unchanged across the Pump
    const SnapshotBlock* s2 = viewTx.AcquireSnapshot();
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(s2->sequence, 6u);  // now the newer one is promoted
}
