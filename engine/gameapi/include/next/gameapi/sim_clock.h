#pragma once

#include <cstdint>

namespace Next::gameapi {

// The sim's deterministic clock. It is the ONLY source of time the Game API exposes — no wall
// clock ever reaches a caller (ADR-0007 determinism red line). Fixed-step: each tick advances
// the counter and the authoritative seconds by `fixedDt`.
struct SimClock {
    uint64_t tick = 0;
    double seconds = 0.0;
    double fixedDt = 1.0 / 60.0;

    void Advance() {
        ++tick;
        seconds += fixedDt;
    }
};

}  // namespace Next::gameapi
