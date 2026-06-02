#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "next/vegetation/vegetation_def.h"

namespace Next::vegetation {

// Every way a VegetationDef can be defective. The validator is TOTAL and FAIL-CLOSED: it accumulates
// ALL violations (never early-exits) and the scatter/authoring path refuses a def whose report is
// non-empty. A malformed def is rejected here, at authoring time, not discovered as silently-empty
// cells or runaway instance counts at runtime.
enum class VegetationValidationCode : uint16_t {
    EmptyVegetationId,
    UnknownSchemaVersion,
    NoSpecies,
    TooManySpecies,
    NonPositiveMaxInstancesPerCell,

    ReservedSpeciesId,  // id == 0
    DuplicateSpeciesId,
    ZeroVisualId,  // visual == 0 -> nothing for UE5 to draw

    NonFiniteDensity,
    NonPositiveDensity,
    DensityTooHigh,  // > kMaxDensityPerSqMeter (runaway instance counts)

    NonFiniteSpacing,
    NegativeSpacing,

    NonFiniteSlopeRange,
    SlopeOutOfRange,     // not within [0, 90] degrees
    InvertedSlopeRange,  // min > max

    NonFiniteAltitudeRange,
    InvertedAltitudeRange,  // min > max

    NonFiniteScaleRange,
    NonPositiveScale,    // minScale <= 0
    InvertedScaleRange,  // min > max

    NonFiniteLogicalRadius,
    NegativeLogicalRadius,
};

const char* ToString(VegetationValidationCode code);

struct VegetationValidationError {
    VegetationValidationCode code;
    uint32_t speciesIndex = UINT32_MAX;  // index into VegetationDef.species, or UINT32_MAX (def-scoped)
    std::string detail;                  // deterministic, human-readable
};

struct VegetationValidationReport {
    std::vector<VegetationValidationError> errors;  // deterministic order: def fields, then species by index
    bool Ok() const { return errors.empty(); }
    bool Has(VegetationValidationCode code) const;
};

class VegetationValidator {
public:
    // Pure: no mutation. Emits errors in a fixed pass order so output is byte-stable.
    static VegetationValidationReport Validate(const VegetationDef& def);
};

}  // namespace Next::vegetation
