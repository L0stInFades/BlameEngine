#pragma once

// Engine-owned DETERMINISTIC sine/cosine for the per-tick wave evaluation. The water surface is
// sampled every fixed tick on the authoritative sim (and, in a port, on the renderer), so the two
// must agree run-to-run and build-to-build. libm std::sin/cos are not guaranteed bit-identical across
// platforms/compilers (their last ULPs vary), so the HOT per-tick path uses this fixed routine instead.
// (This is stricter than the rest of the engine, which is only per-build deterministic — cook-time
// trig and one-off math elsewhere still use std::sin. Here it is cheap insurance on the replay path.)
//
// Implementation: argument reduction to [-pi, pi] via a deterministic round (multiply + truncate), a
// fold into [-pi/2, pi/2], then a 9th-order Taylor polynomial (max abs error ~1e-6 on the fold range).
// Uses only +, -, *, and an exact float<->int truncation — all IEEE-754 reproducible.

namespace Next::water {

inline float DetSin(float x) {
    constexpr float kInvTwoPi = 0.15915494309189533577f;
    constexpr float kTwoPi = 6.28318530717958647692f;
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kHalfPi = 1.57079632679489661923f;

    // a = x - round(x / 2pi) * 2pi  ->  a in [-pi, pi]
    const float scaled = x * kInvTwoPi;
    const float rounded = (scaled >= 0.0f) ? (scaled + 0.5f) : (scaled - 0.5f);
    const long long k = static_cast<long long>(rounded);  // truncation toward zero == round-to-nearest here
    float a = x - (static_cast<float>(k) * kTwoPi);

    // Fold [-pi,pi] into [-pi/2, pi/2] using sin(pi - a) == sin(a), where the Taylor series is accurate.
    if (a > kHalfPi) {
        a = kPi - a;
    } else if (a < -kHalfPi) {
        a = -kPi - a;
    }

    const float a2 = a * a;
    // sin(a) ~= a - a^3/6 + a^5/120 - a^7/5040 + a^9/362880  (Horner form)
    return a * (1.0f +
                (a2 * (-1.0f / 6.0f + (a2 * (1.0f / 120.0f + (a2 * (-1.0f / 5040.0f + (a2 * (1.0f / 362880.0f)))))))));
}

inline float DetCos(float x) {
    constexpr float kHalfPi = 1.57079632679489661923f;
    return DetSin(x + kHalfPi);
}

}  // namespace Next::water
