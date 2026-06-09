#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "next/math/math.h"

// Water data model (the authored artifact + the per-cell wire record). Pure POD/STL, foundation-only:
// no UE5, no ECS, no game/. A designer builds a WaterBodyDef (WaterBuilder), a validator checks it
// (WaterValidator, fail-closed), the cook bakes the bodies overlapping a world cell into a flat
// WaterBodyInstance array (CellLayer::Water blob). Those bodies are the AUTHORITATIVE water the
// headless core owns: it owns the surface geometry (analytic Gerstner waves), buoyancy, flow, and
// submersion — UE5 only RENDERS the same surface from the same parameters. Deterministic by
// construction (the baked blob is byte-stable; surface eval is per-platform deterministic float math,
// matching engine/vegetation's stance).

namespace Next::water {

constexpr uint32_t kWaterSchemaVersion = 1;

// Gerstner components baked into one body's wire record. Eight gives a believable, queryable gameplay
// surface (ocean swell + multiple chop bands); a real renderer can layer finer cosmetic detail on top.
constexpr uint8_t kMaxWavesPerBody = 8;

// Deterministic, auditable ceilings (the validator / unpack reject anything past them).
constexpr uint32_t kMaxWaterBodiesPerCell = 4096;
constexpr uint32_t kMaxWaterBodiesPerScene = 1u << 20;  // 1M authored bodies (a generous scene ceiling)
// Finite ceiling on any body's bound coordinate magnitude (meters). Generous (> Earth's circumference,
// far beyond any real open world) yet rejects absurd/garbage bounds whose grid-cell span would overflow
// an int cast in the broadphase. The validator enforces it; the store also computes spans in double.
constexpr float kMaxWaterCoord = 5.0e7f;
constexpr float kDefaultWaterDensity = 1000.0f;  // fresh water, kg/m^3 (sea water ~1025)
constexpr float kMaxWaterDensity = 20000.0f;     // generous ceiling (mercury ~13546)
constexpr float kMaxWaveAmplitude = 1000.0f;     // meters; sanity ceiling

// Index into the UE5-side mesh/material registry, forwarded verbatim — the sim never interprets it.
using VisualStateId = uint32_t;

// What KIND of water a body is. Drives defaults and a couple of runtime rules (flood animates the
// surface height over time; ocean is effectively unbounded in XZ).
enum class WaterType : uint8_t {
    Ocean = 0,  // large/unbounded still-or-wavy body; the global sea
    Pool = 1,   // bounded still water (a pool, a tank, a flooded basement at rest)
    River = 2,  // bounded water with a steady flow velocity (a current that sweeps bodies)
    Flood = 3,  // bounded water whose surface RISES over time (floodRate) up to floodMaxHeight
    Lake = 4,   // bounded still (or lightly wavy) open water; like Pool but large/outdoor
};

// Per-body LOGICAL flags the gameplay sim cares about (not rendering). Stamped on the wire record so
// queries read them without the authored def.
enum WaterFlags : uint16_t {
    WaterNone = 0,
    WaterBuoyant = 1u << 0,       // applies buoyancy + drag to dynamic bodies overlapping it
    WaterConductive = 1u << 1,    // electrically conductive: shorts submerged electronics (hacking hazard)
    WaterLethal = 1u << 2,        // full submersion is lethal over time (drowning)
    WaterCurrent = 1u << 3,       // applies its flowVelocity as a current force to floating bodies
    WaterBreaksSight = 1u << 4,   // a submerged entity is hidden from line-of-sight sensors (stealth)
    WaterExtinguishes = 1u << 5,  // submersion puts out fire / cools (and shorts, with Conductive)
};

// One Gerstner wave. POD. The surface is the SUM of these (plus the body's still-water height). The
// SAME components drive the renderer, so sim and view agree on the water surface to the meter.
struct WaveComponent {
    float amplitude = 0.0f;             // meters (peak vertical displacement contribution)
    float wavelength = 1.0f;            // meters, > 0; wavenumber k = 2*pi / wavelength
    float direction[2] = {1.0f, 0.0f};  // XZ unit direction of travel (validator normalizes intent)
    float speed = 1.0f;                 // phase speed c (m/s); angular speed omega = c * k
    float steepness = 0.0f;             // Gerstner Q in [0,1]; 0 = plain sine (no horizontal pinch)
};
static_assert(sizeof(WaveComponent) == 24, "WaveComponent wire layout must stay stable");
static_assert(alignof(WaveComponent) == 4, "WaveComponent alignment must stay stable");
static_assert(std::is_trivially_copyable<WaveComponent>::value, "WaveComponent must be memcpy-safe");

// One placed body of water. POD with a fixed, asserted layout: it is BOTH the cook output and the
// wire record packed into the CellLayer::Water blob, so the same bytes stream to UE5 unchanged. A
// body may span many cells — it is recorded (with the same bodyId) in each cell it overlaps, and the
// runtime store de-duplicates by bodyId.
struct WaterBodyInstance {
    float boundsMin[3] = {0.0f, 0.0f, 0.0f};  // world-space AABB of the body's volume
    float boundsMax[3] = {0.0f, 0.0f, 0.0f};
    float surfaceHeight = 0.0f;                  // base still-water surface Y (Gerstner waves ride on this)
    float density = kDefaultWaterDensity;        // kg/m^3 (buoyancy scales with this)
    float flowVelocity[3] = {0.0f, 0.0f, 0.0f};  // m/s current (rivers); applied when WaterCurrent
    float linearDrag = 2.0f;                     // hydrodynamic linear damping rate (1/s, per submerged frac)
    float quadraticDrag = 1.0f;                  // quadratic (form) drag rate (per (m/s), per submerged frac)
    float floodRate = 0.0f;                      // m/s the surface rises (Flood only)
    float floodMaxHeight = 0.0f;                 // surface Y ceiling for Flood (>= surfaceHeight)
    WaveComponent waves[kMaxWavesPerBody] = {};
    uint32_t bodyId = 0;            // stable global id (1-based; de-dup key across overlapping cells). 0 invalid.
    uint32_t visual = 0;            // VisualStateId forwarded to UE5
    uint16_t flags = WaterBuoyant;  // WaterFlags
    uint8_t type = static_cast<uint8_t>(WaterType::Pool);  // WaterType
    uint8_t waveCount = 0;                                 // active entries in waves[] (0..kMaxWavesPerBody)
};
static_assert(sizeof(WaterBodyInstance) == 264, "WaterBodyInstance wire layout must stay stable");
static_assert(alignof(WaterBodyInstance) == 4, "WaterBodyInstance alignment must stay stable");
static_assert(std::is_trivially_copyable<WaterBodyInstance>::value,
              "WaterBodyInstance must be memcpy-safe for the cell blob");

constexpr uint32_t kInvalidWaterBody = 0;

// The authored body (designer-facing). Variable-length wave list; the cook bakes it into the fixed
// WaterBodyInstance (taking the first kMaxWavesPerBody waves). std::string id stays out of the wire
// record (the cook maps it to a 1-based bodyId).
struct WaterBodyDef {
    std::string id;  // stable, non-empty (e.g. "harbor-sea")
    WaterType type = WaterType::Pool;
    float boundsMin[3] = {-10.0f, -10.0f, -10.0f};
    float boundsMax[3] = {10.0f, 0.0f, 10.0f};
    float surfaceHeight = 0.0f;
    float density = kDefaultWaterDensity;
    float flowVelocity[3] = {0.0f, 0.0f, 0.0f};
    float linearDrag = 2.0f;
    float quadraticDrag = 1.0f;
    float floodRate = 0.0f;
    float floodMaxHeight = 0.0f;
    std::vector<WaveComponent> waves;  // cook keeps the first kMaxWavesPerBody
    uint32_t visual = 0;
    uint16_t flags = WaterBuoyant;
};

// The authored artifact for a whole region: an ordered list of bodies. The cook assigns each a
// 1-based bodyId by vector order (deterministic) and bakes the ones overlapping each world cell.
struct WaterSceneDef {
    std::string id;  // stable scene id (non-empty)
    std::string name;
    uint32_t schemaVersion = kWaterSchemaVersion;
    std::vector<WaterBodyDef> bodies;
};

// Convenience accessors (header-only, branch-free) used across the surface/volume math.
inline bool WaterBodyContainsXZ(const WaterBodyInstance& b, float x, float z) {
    return x >= b.boundsMin[0] && x <= b.boundsMax[0] && z >= b.boundsMin[2] && z <= b.boundsMax[2];
}

}  // namespace Next::water
