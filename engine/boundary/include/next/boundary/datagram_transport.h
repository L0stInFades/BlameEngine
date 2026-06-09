#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "next/boundary/snapshot.h"
#include "next/boundary/transport.h"

// Cross-process / network snapshot transport (ADR-0006 W15). The same ISnapshotTransport surface the
// sim and UE5 already use, but the channels travel as serialized DATAGRAMS over an abstract link
// (IDatagram) instead of in-process rings. This is what turns the headless authoritative world into a
// dedicated server: swap InProcessTransport for DatagramTransport and the call sites do not change.
//
// It carries the SAME reliable protocol as the in-process path — snapshots are deltas vs the acked
// baseline with keyframe fallback (B1/W13) and the consumer acks back over the same link (W14) — so a
// dropped or reordered datagram (UDP) is harmless: snapshots are latest-wins (the freshest received
// wins, matching the triple buffer) and the reliable delta logic recovers any lost change.

namespace Next::boundary {

// One unreliable, unordered datagram pipe (UDP-shaped). Send is best-effort; Recv is non-blocking.
struct IDatagram {
    virtual ~IDatagram() = default;
    virtual bool Send(const uint8_t* data, size_t len) = 0;  // false if it could not be enqueued/sent
    virtual bool Recv(std::vector<uint8_t>& out) = 0;        // false if nothing is available right now
};

// Datagram message kinds (1-byte outer tag prefixed to every datagram so one link multiplexes all
// channels). Snapshot payloads carry SnapshotWireHeader; event/command/ack payloads carry a small
// versioned payload header before their field-wise little-endian body.
enum class DatagramKind : uint8_t {
    Snapshot = 1,  // sim -> view: a serialized SnapshotBlock (latest-wins on the receive side)
    Event = 2,     // sim -> view: a GameEvent
    Command = 3,   // view -> sim: an InputCmd
    Ack = 4,       // view -> sim: a SnapshotSequence acknowledgement
};

// ISnapshotTransport over an IDatagram. The SAME class serves both ends (each wraps its own endpoint);
// each end uses only its half of the interface (sim: publish/event/popcommand/popack; view: acquire/
// popevent/pushcommand/pushack). Pump() drains the link into per-kind queues on demand.
class DatagramTransport final : public ISnapshotTransport {
public:
    explicit DatagramTransport(IDatagram* link) : link_(link) {}

    // --- producer side (sim) ---
    SnapshotBlock& BeginWrite() override { return scratch_; }
    void PublishSnapshot() override;
    bool PushEvent(const GameEvent& e) override;
    bool PopCommand(InputCmd& out) override;
    bool PopAck(SnapshotSequence& outSeq) override;

    // --- consumer side (UE5) ---
    const SnapshotBlock* AcquireSnapshot() override;
    bool PopEvent(GameEvent& out) override;
    bool PushCommand(const InputCmd& c) override;
    bool PushAck(SnapshotSequence seq) override;

private:
    void Pump();  // drain the link into the per-kind queues, demuxing by tag

    IDatagram* link_;
    SnapshotBlock scratch_;         // producer fills this, PublishSnapshot serializes it
    std::vector<uint8_t> sendBuf_;  // reused send framing buffer
    std::vector<uint8_t> recvBuf_;  // reused recv buffer

    // Latest-wins snapshot, split so the contract holds: Pump() (called by any Pop*) only ever updates
    // `pending_`; AcquireSnapshot promotes pending_ -> held_ and returns &held_, which then stays valid
    // until the NEXT AcquireSnapshot (a PopEvent in between cannot overwrite it). `highestSeq_` is a
    // monotonic high-water mark that NEVER resets, so a reordered/late datagram (seq <= it) is dropped.
    SnapshotBlock pending_;                      // freshest decoded-but-unacquired snapshot
    bool hasPending_ = false;                    // a newer snapshot arrived since the last AcquireSnapshot()
    SnapshotBlock held_;                         // returned by AcquireSnapshot; valid until the next AcquireSnapshot()
    SnapshotSequence highestSeq_ = kNoBaseline;  // highest snapshot sequence ever accepted (monotonic)
    std::deque<GameEvent> events_;
    std::deque<InputCmd> commands_;
    std::deque<SnapshotSequence> acks_;
};

// In-memory loopback link for tests / single-host play: two endpoints whose Sends cross into the
// other's Recv queue. Optional PER-DIRECTION loss injection models a real UDP path. Each direction has
// its OWN send counter (so dropping every Nth sim->view datagram drops snapshots, and every Nth
// view->sim drops acks — independently, not whichever happens to land on a shared counter). Single-thread.
class InMemoryDatagramLink {
public:
    IDatagram& SimSide() { return sim_; }
    IDatagram& ViewSide() { return view_; }

    // Drop every Nth datagram in BOTH directions (0 = never). Models symmetric UDP loss.
    void SetDropEveryNth(int n) {
        dropSimToView_ = n;
        dropViewToSim_ = n;
    }
    // Targeted loss for tests: drop only snapshots (sim->view) or only acks/commands (view->sim).
    void SetDropSimToView(int n) { dropSimToView_ = n; }
    void SetDropViewToSim(int n) { dropViewToSim_ = n; }

private:
    class Endpoint final : public IDatagram {
    public:
        Endpoint(std::deque<std::vector<uint8_t>>* out, std::deque<std::vector<uint8_t>>* in, const int* dropEveryNth)
            : out_(out), in_(in), dropEveryNth_(dropEveryNth) {}
        bool Send(const uint8_t* data, size_t len) override {
            if (*dropEveryNth_ > 0 && (++sendCount_ % *dropEveryNth_) == 0) {
                return true;  // "sent" but dropped in flight (UDP loss)
            }
            out_->emplace_back(data, data + len);
            return true;
        }
        bool Recv(std::vector<uint8_t>& out) override {
            if (in_->empty()) {
                return false;
            }
            out = std::move(in_->front());
            in_->pop_front();
            return true;
        }

    private:
        std::deque<std::vector<uint8_t>>* out_;
        std::deque<std::vector<uint8_t>>* in_;
        const int* dropEveryNth_;
        int sendCount_ = 0;
    };

    std::deque<std::vector<uint8_t>> simToView_;
    std::deque<std::vector<uint8_t>> viewToSim_;
    int dropSimToView_ = 0;
    int dropViewToSim_ = 0;
    Endpoint sim_{&simToView_, &viewToSim_, &dropSimToView_};
    Endpoint view_{&viewToSim_, &simToView_, &dropViewToSim_};
};

}  // namespace Next::boundary
