#pragma once

#include "next/water/water_def.h"

// Analytic water surface (Gerstner waves). This is the AUTHORITATIVE surface geometry the headless
// core owns and that UE5 reproduces from the SAME WaveComponents — so gameplay (buoyancy, submersion,
// what the player sees) and rendering agree to the meter. Pure functions, deterministic, no state.
//
// Gerstner model: a point with rest position (x0,z0) is displaced to
//   P.x = x0 - sum( Q_i * A_i * D_i.x * sin(phi_i) )
//   P.z = z0 - sum( Q_i * A_i * D_i.z * sin(phi_i) )
//   P.y = base + sum( A_i * cos(phi_i) )
// with phi_i = k_i*(D_i . (x0,z0)) - omega_i*t,  k_i = 2*pi/wavelength_i,  omega_i = speed_i*k_i.
// A height query at a WORLD (x,z) inverts the horizontal pinch by fixed-point iteration, so the
// returned height is correct at (x,z) — not a cheap "evaluate the cosine at the grid point" stub.

namespace Next::water {

struct SurfaceSample {
    float height = 0.0f;                   // world Y of the water surface at (x,z,t)
    float normal[3] = {0.0f, 1.0f, 0.0f};  // unit surface normal
};

// Still-water surface Y, accounting for Flood rise: min(surfaceHeight + floodRate*t, floodMaxHeight)
// for a Flood body, else the body's surfaceHeight. The base the Gerstner sum rides on.
float EffectiveSurfaceHeight(const WaterBodyInstance& body, double timeSeconds);

// FAST surface height: base + the undisplaced vertical Gerstner sum at (x,z) (no horizontal-pinch
// inversion). This is what the per-tick buoyancy system uses — one evaluation per body per tick, the
// dominant cost — and it is exact when steepness == 0 and within a wave-amplitude of the truth
// otherwise (the body bobs with the surface either way). Deterministic (uses DetSin/DetCos).
float SampleHeightFast(const WaterBodyInstance& body, float x, float z, double timeSeconds);

// ACCURATE surface height at world (x,z): inverts the Gerstner horizontal displacement by fixed-point
// iteration so the height is correct AT (x,z). Use for precise gameplay point queries / raycasts.
// Deterministic.
float SurfaceHeightAt(const WaterBodyInstance& body, float x, float z, double timeSeconds);

// Height + analytic unit normal of the wavy surface at (x,z,t) in a single evaluation.
SurfaceSample SurfaceSampleAt(const WaterBodyInstance& body, float x, float z, double timeSeconds);

// --- W9: compiled wave-set for REPEATED sampling of the same body at one time (e.g. a boat's 4 hull
// corners per tick). It hoists the per-wave invariants — k = 2*pi/wavelength and omega = speed*k, the
// double divide+mul that WrapPhase otherwise redoes on every sample — and the still-water base, out of
// the per-sample loop. Build ONCE per (body, time), then sample many points. SampleHeightFast over a
// CompiledWaves is BIT-IDENTICAL to the per-component SampleHeightFast (same doubles, same operands,
// same summation order); it simply stops recomputing the constants. Determinism/replay are unaffected.
struct CompiledWave {
    double k;      // 2*pi / wavelength
    double omega;  // speed * k
    float amplitude;
    float dirX;
    float dirZ;
    float steepness;
};
struct CompiledWaves {
    CompiledWave waves[kMaxWavesPerBody];
    uint8_t count = 0;  // valid waves (wavelength > 0), in the body's original order
    float base = 0.0f;  // EffectiveSurfaceHeight(body, timeSeconds) at compile time
};
CompiledWaves CompileWaves(const WaterBodyInstance& body, double timeSeconds);
float SampleHeightFast(const CompiledWaves& compiled, float x, float z, double timeSeconds);

// --- W9: LOD cosmetic height. NON-AUTHORITATIVE. Sums only the `maxWaves` LARGEST-amplitude components,
// for the RENDERER / far-field tessellation where evaluating all components at thousands of vertices is
// wasteful and a sub-meter height error is invisible. The dropped components are the smallest-amplitude
// ones, so the error is bounded by their summed amplitude. NEVER use this for buoyancy/gameplay: it is
// not bit-identical to the authoritative surface and would break determinism/replay if it fed the sim.
float SampleHeightLOD(const WaterBodyInstance& body, float x, float z, double timeSeconds, int maxWaves);

}  // namespace Next::water
