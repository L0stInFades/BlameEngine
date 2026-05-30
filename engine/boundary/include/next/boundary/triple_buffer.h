#pragma once

#include <atomic>
#include <cstdint>

// Wait-free single-producer / single-consumer triple buffer (ADR-0006). The producer (sim) and
// consumer (UE5) each privately own one slot; the third is exchanged through `mid_` atomically,
// with a "fresh" bit signalling a new frame. The sim never waits on UE5 (a slow consumer just
// drops the middle frame); UE5 never waits on the sim (no fresh frame → it reuses the last one
// and keeps interpolating). The single synchronization point is one atomic exchange.

namespace Next::boundary {

template<typename T>
class TripleBuffer {
public:
    // Producer: write into this slot, then call Publish().
    T& Write() { return slot_[write_]; }

    // Producer: publish the written slot (one atomic exchange) and reclaim the slot the consumer
    // is not holding.
    void Publish() {
        const uint32_t prev = mid_.exchange(write_ | kFresh, std::memory_order_acq_rel);
        write_ = prev & kIdx;
    }

    // Consumer: fetch the freshest published slot, or nullptr if nothing new since the last
    // Acquire(). Hands back the consumer's previous slot for the producer to reuse.
    //
    // POINTER LIFETIME: the returned pointer is valid ONLY until the next Acquire() call. That
    // call returns the slot to the producer pool, which may then overwrite it (for T holding heap
    // storage like SnapshotBlock's vectors, that includes reallocating the backing buffers). To
    // retain data across frames (e.g. snapshot interpolation), COPY what you need out — never
    // latch this raw pointer across an Acquire().
    const T* Acquire() {
        if ((mid_.load(std::memory_order_acquire) & kFresh) == 0) {
            return nullptr;
        }
        const uint32_t prev = mid_.exchange(read_, std::memory_order_acq_rel);
        read_ = prev & kIdx;
        return &slot_[read_];
    }

private:
    static constexpr uint32_t kFresh = 0x4;
    static constexpr uint32_t kIdx = 0x3;

    T slot_[3]{};
    std::atomic<uint32_t> mid_{0};  // bits 0..1 = slot index, bit 2 = fresh
    uint32_t write_ = 1;
    uint32_t read_ = 2;
};

}  // namespace Next::boundary
