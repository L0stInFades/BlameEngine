#pragma once

#include <cstdint>

// Deterministic, platform-independent hashing + PRNG for vegetation scatter (ADR-0014). Integer-only
// ops produce identical bytes on every platform/compiler — unlike the <random> distributions, which
// are NOT portable. The scatter seed derives from def.masterSeed + the WORLD-SPACE cell coordinate
// (not generation order), so a cell scatters identically no matter when it streams in. This is the
// reproducibility the headless/UE5 boundary requires (and mirrors how UE PCG derives its seed from
// world position). NOT cryptographic and NOT a security boundary — that is the WASM sandbox's job.

namespace Next::vegetation {

// splitmix64 finalizer — strong avalanche; used both to mix seeds and to advance a stream.
inline uint64_t Splitmix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// Fold one value into a running seed so distinct tuples land on well-separated streams.
inline uint64_t HashCombine(uint64_t seed, uint64_t value) {
    return Splitmix64(seed ^ Splitmix64(value));
}

// The seed for one scatter grid node. Order-independent in the node indices: any (cell, species, i, j)
// maps to a fixed stream, so iteration order never changes the result.
inline uint64_t NodeSeed(uint64_t masterSeed, int32_t cellX, int32_t cellZ, uint32_t species, uint32_t i, uint32_t j) {
    uint64_t s = masterSeed + 0x1000000000000001ull;  // keep a zero masterSeed from degenerating
    s = HashCombine(s, static_cast<uint64_t>(static_cast<uint32_t>(cellX)));
    s = HashCombine(s, static_cast<uint64_t>(static_cast<uint32_t>(cellZ)));
    s = HashCombine(s, static_cast<uint64_t>(species));
    s = HashCombine(s, (static_cast<uint64_t>(i) << 32) | static_cast<uint64_t>(j));
    return s;
}

// A tiny deterministic stream: seed from a NodeSeed, then pull uniform floats/ints in a fixed order.
struct DetRng {
    uint64_t state;
    explicit DetRng(uint64_t seed) : state(seed != 0 ? seed : 0x0123456789ABCDEFull) {}

    uint64_t NextU64() {
        state = Splitmix64(state);
        return state;
    }

    // Uniform float in [0,1): the 24 high bits give an integer exactly representable in a float,
    // divided by 2^24. Portable and bias-free at float precision.
    float NextFloat01() {
        const uint64_t bits24 = NextU64() >> 40;          // top 24 of 64 bits
        return static_cast<float>(bits24) / 16777216.0f;  // / 2^24 -> [0,1)
    }

    float NextRange(float lo, float hi) { return lo + (hi - lo) * NextFloat01(); }
};

}  // namespace Next::vegetation
