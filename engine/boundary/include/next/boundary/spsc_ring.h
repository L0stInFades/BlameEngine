#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

// Bounded lock-free single-producer / single-consumer ring (ADR-0006). Backs the append-only
// event (sim→UE5) and command (UE5→sim) channels. One thread pushes, one pops; the hot path is
// wait-free. Capacity is rounded up to a power of two so index wrapping is a mask. A full ring
// drops the push and reports it (callers log drops rather than block — no cross-thread stall).

namespace Next::boundary {

template<typename T>
class SpscRing {
public:
    explicit SpscRing(size_t minCapacity = 1024) {
        size_t cap = 1;
        while (cap < minCapacity) {
            cap <<= 1;
        }
        buffer_.resize(cap);
        mask_ = cap - 1;
    }

    // Producer side. Returns false if the ring is full (the item is dropped).
    bool Push(const T& value) {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t next = head + 1;
        // The acquire-load of tail_ is a CORRECTNESS invariant, not a style choice: when the ring
        // is full, head & mask_ aliases tail & mask_, so this establishes happens-before to the
        // consumer's prior read of that slot and prevents overwriting an element still being
        // copied out. Do NOT weaken it to relaxed.
        if (next - tail_.load(std::memory_order_acquire) > buffer_.size()) {
            return false;  // full
        }
        buffer_[head & mask_] = value;
        head_.store(next, std::memory_order_release);
        return true;
    }

    // Consumer side. Returns false if empty.
    bool Pop(T& out) {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;  // empty
        }
        out = buffer_[tail & mask_];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    bool Empty() const { return tail_.load(std::memory_order_acquire) == head_.load(std::memory_order_acquire); }
    size_t Capacity() const { return buffer_.size(); }

private:
    std::vector<T> buffer_;
    size_t mask_ = 0;
    // Keep the producer-written head and consumer-written tail on separate cache lines: sharing a
    // line would ping-pong it between cores and defeat the wait-free hot path's purpose.
    alignas(64) std::atomic<size_t> head_{0};  // producer writes
    alignas(64) std::atomic<size_t> tail_{0};  // consumer reads
};

}  // namespace Next::boundary
