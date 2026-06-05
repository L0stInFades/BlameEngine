#pragma once

#include <cstddef>

#include "next/boundary/snapshot.h"
#include "next/boundary/spsc_ring.h"
#include "next/boundary/triple_buffer.h"

// The boundary's three one-way channels behind a single interface (ADR-0006). Swapping the
// transport (in-process → shared memory → network) never changes the data model or the call
// sites: the sim always BeginWrite/PublishSnapshot/PushEvent/PopCommand; UE5 always
// AcquireSnapshot/PopEvent/PushCommand.

namespace Next::boundary {

struct ISnapshotTransport {
    virtual ~ISnapshotTransport() = default;

    // --- producer side (sim) ---
    virtual SnapshotBlock& BeginWrite() = 0;         // fill this block, then PublishSnapshot()
    virtual void PublishSnapshot() = 0;              // one atomic publish
    virtual bool PushEvent(const GameEvent& e) = 0;  // false if the event ring is full (dropped)
    virtual bool PopCommand(InputCmd& out) = 0;      // drain UE5→sim commands at the tick boundary

    // Drain a consumer acknowledgement (UE5→sim): the last snapshot sequence UE5 applied. The sim
    // feeds these to SnapshotPublisher::Acknowledge so deltas compress against confirmed state (B1).
    virtual bool PopAck(SnapshotSequence& outSeq) = 0;

    // --- consumer side (UE5) ---
    // Freshest block, or nullptr if none new. The pointer is valid ONLY until the next
    // AcquireSnapshot() call (the slot is then recycled); copy out anything retained across frames.
    virtual const SnapshotBlock* AcquireSnapshot() = 0;
    virtual bool PopEvent(GameEvent& out) = 0;        // drain sim→UE5 cosmetic events
    virtual bool PushCommand(const InputCmd& c) = 0;  // false if the command ring is full
    virtual bool PushAck(SnapshotSequence seq) = 0;   // ack the last applied snapshot to the sim
};

// First transport (ADR-0006 step 1): sim linked into the UE5 process; channels live in-process.
// Wait-free on both sides. Upgrading to shared memory / network only replaces this class.
class InProcessTransport final : public ISnapshotTransport {
public:
    explicit InProcessTransport(size_t eventCapacity = 1024, size_t commandCapacity = 1024)
        : events_(eventCapacity), commands_(commandCapacity) {}

    SnapshotBlock& BeginWrite() override { return snapshots_.Write(); }
    void PublishSnapshot() override { snapshots_.Publish(); }
    bool PushEvent(const GameEvent& e) override { return events_.Push(e); }
    bool PopCommand(InputCmd& out) override { return commands_.Pop(out); }
    bool PopAck(SnapshotSequence& outSeq) override { return acks_.Pop(outSeq); }

    const SnapshotBlock* AcquireSnapshot() override { return snapshots_.Acquire(); }
    bool PopEvent(GameEvent& out) override { return events_.Pop(out); }
    bool PushCommand(const InputCmd& c) override { return commands_.Push(c); }
    bool PushAck(SnapshotSequence seq) override { return acks_.Push(seq); }

private:
    TripleBuffer<SnapshotBlock> snapshots_;
    SpscRing<GameEvent> events_;
    SpscRing<InputCmd> commands_;
    SpscRing<SnapshotSequence> acks_;
};

}  // namespace Next::boundary
