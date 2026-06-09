// Two-thread stress for the lock-free boundary primitives (W16). The SpscRing and TripleBuffer back
// the sim<->UE5 channels; their correctness rests entirely on the acquire/release ordering that pairs
// the atomic head/tail/mid with the NON-atomic slot accesses. A single-threaded test can never observe
// a broken memory order. This test runs a real producer thread against a real consumer thread so that:
//   * under TSan (the `tsan` preset / CI job) any missing happens-before on the slot memory is reported
//     as a data race — e.g. weakening SpscRing's acquire-load of tail_ to relaxed would trip here;
//   * under any build the LOGICAL contract is checked: SPSC delivers every item exactly once, in order;
//     the TripleBuffer never tears a frame and never hands back an older frame than one already seen.

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <thread>

#include "next/boundary/spsc_ring.h"
#include "next/boundary/triple_buffer.h"

using namespace Next::boundary;

namespace {

// A payload whose fields must stay mutually consistent — a torn read shows up as check != seq*2+1.
struct Item {
    uint64_t seq = 0;
    uint64_t check = 0;
};

// A frame whose three fields are always written equal; a torn TripleBuffer read shows up as a != b/c.
struct Frame {
    uint64_t a = 0;
    uint64_t b = 0;
    uint64_t c = 0;
};

}  // namespace

// SPSC ring: a small ring hammered by 200k items forces many full/empty transitions (maximal
// interleaving). The consumer must receive every item exactly once, strictly in order, untorn.
TEST(BoundaryConcurrency, SpscRingDeliversEveryItemInOrder) {
    constexpr uint64_t kN = 200000;
    SpscRing<Item> ring(1024);
    std::atomic<bool> consumerFailed{false};
    std::atomic<uint64_t> received{0};

    std::thread consumer([&] {
        uint64_t expected = 0;
        Item out;
        while (expected < kN) {
            if (ring.Pop(out)) {
                if (out.seq != expected || out.check != (out.seq * 2u + 1u)) {
                    consumerFailed.store(true);
                    return;
                }
                ++expected;
                received.store(expected, std::memory_order_relaxed);
            } else {
                std::this_thread::yield();  // empty; let the producer run
            }
        }
    });

    std::thread producer([&] {
        for (uint64_t i = 0; i < kN; ++i) {
            Item it{i, i * 2u + 1u};
            while (!ring.Push(it)) {
                std::this_thread::yield();  // full; let the consumer drain
            }
        }
    });

    producer.join();
    consumer.join();
    EXPECT_FALSE(consumerFailed.load()) << "out-of-order / torn / duplicated item";
    EXPECT_EQ(received.load(), kN);  // every item delivered exactly once
}

// Triple buffer: the producer publishes monotonically increasing frames; the consumer may drop frames
// (a slow reader) but must NEVER see a torn frame and NEVER see one older than one already observed.
TEST(BoundaryConcurrency, TripleBufferNeverTearsOrGoesBackward) {
    constexpr uint64_t kN = 100000;
    TripleBuffer<Frame> buf;
    std::atomic<bool> producerDone{false};
    std::atomic<bool> consumerFailed{false};
    std::atomic<uint64_t> maxSeen{0};

    std::thread consumer([&] {
        uint64_t last = 0;
        auto observe = [&](const Frame* f) {
            if (f == nullptr) {
                return;
            }
            if (f->a != f->b || f->b != f->c) {
                consumerFailed.store(true);  // torn read across the publish boundary
                return;
            }
            if (f->a < last) {
                consumerFailed.store(true);  // went backward
                return;
            }
            last = f->a;
            maxSeen.store(last, std::memory_order_relaxed);
        };
        while (!producerDone.load(std::memory_order_acquire)) {
            observe(buf.Acquire());
        }
        observe(buf.Acquire());  // final drain: the last published frame is always readable
    });

    std::thread producer([&] {
        for (uint64_t frame = 1; frame <= kN; ++frame) {
            buf.Write() = Frame{frame, frame, frame};
            buf.Publish();
        }
        producerDone.store(true, std::memory_order_release);
    });

    producer.join();
    consumer.join();
    EXPECT_FALSE(consumerFailed.load()) << "torn frame or backward frame observed";
    EXPECT_GT(maxSeen.load(), 0u);  // the consumer actually saw frames
    EXPECT_LE(maxSeen.load(), kN);  // and never a value the producer never wrote
    EXPECT_EQ(maxSeen.load(), kN);  // the final frame is observable after the producer finishes
}
