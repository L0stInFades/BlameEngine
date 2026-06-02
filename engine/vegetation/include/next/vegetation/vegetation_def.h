#pragma once

#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "next/math/math.h"

// Vegetation data model (ADR-0014): the authored artifact (VegetationDef) plus the per-cell scatter
// output (VegetationInstance). Pure POD/STL with one thin terrain-sampler interface — no UE5, no ECS
// logic, no game/. A designer builds a VegetationDef (VegetationBuilder), a validator checks it
// (VegetationValidator), and the deterministic scatter (vegetation_scatter) turns it into instances
// per world cell. Those instances are the AUTHORITATIVE placement the headless core owns; UE5 only
// renders them. Integer-keyed and deterministic by construction.

namespace Next::vegetation {

constexpr uint32_t kVegetationSchemaVersion = 1;

// Deterministic, auditable ceilings (the validator rejects anything past them).
constexpr uint16_t kMaxVegetationSpecies = 1024;
constexpr float kMaxDensityPerSqMeter = 100.0f;  // 100 plants/m² is already extreme ground cover

// Species handle. 0 is the invalid sentinel; the first species a builder adds gets id 1.
using SpeciesId = uint16_t;
constexpr SpeciesId kInvalidSpecies = 0;

// Index into the UE5-side mesh/material registry, forwarded verbatim — the sim never interprets it.
// Kept as a plain alias (mirrors Next::boundary::VisualStateId) so engine/vegetation stays free of any
// boundary/UE5 dependency.
using VisualStateId = uint32_t;

// Per-instance LOGICAL flags the gameplay sim cares about (not rendering). Copied onto every spawned
// instance so spatial queries / line-of-sight / destruction can read them without the species table.
enum VegetationInstanceFlags : uint16_t {
    VegNone = 0,
    VegBlocksLineOfSight = 1u << 0,  // a sight/cover blocker for AI + the sandbox sense API
    VegDestructible = 1u << 1,       // may be removed by a gameplay intent (felled/burned)
    VegAlignToSlope = 1u << 2,       // renderer should tilt the instance to the terrain normal
};

struct VegetationSpecies {
    SpeciesId id = kInvalidSpecies;
    VisualStateId visual = 0;  // what UE5 draws (mesh/material variant id)

    // Placement rules.
    float densityPerSqMeter = 0.01f;  // target instances per square meter (pre-spacing/filter)
    float minSpacing = 1.0f;          // intra-species separation radius (meters); 0 = no separation
    float minSlopeDegrees = 0.0f;     // accept terrain whose slope is within [min,max] degrees
    float maxSlopeDegrees = 90.0f;
    float minAltitude = -1.0e9f;  // accept terrain height within [min,max] (world Y)
    float maxAltitude = 1.0e9f;
    float minScale = 1.0f;  // uniform scale drawn in [min,max]
    float maxScale = 1.0f;
    float logicalRadius = 0.0f;  // gameplay footprint (meters); 0 = a point
    uint32_t requiredMask = 0;   // if non-zero, terrain sample mask must AND non-zero to place here

    uint16_t flags = VegNone;  // VegetationInstanceFlags stamped onto every instance
};

// One placed plant. POD with a fixed, asserted layout: it is BOTH the scatter output and the wire
// record packed into the CellLayer::Vegetation blob, so the same bytes stream to UE5 unchanged.
struct VegetationInstance {
    float position[3] = {0.0f, 0.0f, 0.0f};  // world-space (x, terrainHeight, z)
    float normal[3] = {0.0f, 1.0f, 0.0f};    // terrain normal at the instance (renderer aligns to it)
    float rotationY = 0.0f;                  // yaw about world up (radians)
    float scale = 1.0f;                      // uniform
    float logicalRadius = 0.0f;              // gameplay footprint (meters), copied from the species
    uint32_t visual = 0;                     // VisualStateId (forwarded to UE5)
    uint32_t instanceId = 0;                 // per-cell ordinal 0..N-1 (unique in cell; global = + cell coord)
    uint16_t species = kInvalidSpecies;      // SpeciesId
    uint16_t flags = VegNone;                // VegetationInstanceFlags snapshot
};

static_assert(sizeof(VegetationInstance) == 48, "VegetationInstance wire layout must stay stable");
static_assert(alignof(VegetationInstance) == 4, "VegetationInstance alignment must stay stable");
static_assert(std::is_trivially_copyable<VegetationInstance>::value,
              "VegetationInstance must be memcpy-safe for the cell blob");

struct VegetationDef {
    std::string id;    // stable, non-empty (e.g. "temperate-forest")
    std::string name;  // display label (optional)
    uint32_t schemaVersion = kVegetationSchemaVersion;
    uint64_t masterSeed = 0;                 // seeds all scatter; same seed+def+cell => same instances
    uint32_t maxInstancesPerCell = 65536;    // hard per-cell cap (a deterministic ceiling)
    std::vector<VegetationSpecies> species;  // scatter order = vector order (deterministic)
};

// Terrain the scatter samples to decide height / slope / mask. Abstract so engine/vegetation stays
// decoupled from any concrete terrain source (heightmap, Jolt, analytic) and stays headless-testable.
struct TerrainSample {
    float height = 0.0f;                  // world Y at the queried (x,z)
    Next::Vec3 normal{0.0f, 1.0f, 0.0f};  // surface normal (need not be unit; scatter normalizes)
    uint32_t mask = 0xFFFFFFFFu;          // biome/paint mask bits at (x,z)
};

class ITerrainSampler {
public:
    virtual ~ITerrainSampler() = default;
    virtual TerrainSample SampleAt(float worldX, float worldZ) const = 0;
};

// Flat ground at y=0, up normal, all mask bits set — a sane default and test fixture.
class FlatTerrainSampler : public ITerrainSampler {
public:
    TerrainSample SampleAt(float /*worldX*/, float /*worldZ*/) const override { return TerrainSample{}; }
};

}  // namespace Next::vegetation
