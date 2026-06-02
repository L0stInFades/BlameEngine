#pragma once

#include <cstdint>
#include <string>

#include "next/vegetation/vegetation_def.h"

namespace Next::vegetation {

// Fluent, designer-facing authoring API. Builds a VegetationDef in memory; assigns dense 1-based
// SpeciesIds. The With* setters apply to the LAST species added. This is the C++ authoring surface
// today; a future text/binary loader would emit the same VegetationDef.
class VegetationBuilder {
public:
    explicit VegetationBuilder(std::string id, std::string displayName = {});

    VegetationBuilder& WithMasterSeed(uint64_t seed);
    VegetationBuilder& WithMaxInstancesPerCell(uint32_t maxInstances);

    // Define a new species; returns its stable id. Subsequent With* target this species.
    SpeciesId AddSpecies(VisualStateId visual);

    VegetationBuilder& WithDensity(float perSqMeter);
    VegetationBuilder& WithSpacing(float minSpacing);
    VegetationBuilder& WithSlopeRange(float minDegrees, float maxDegrees);
    VegetationBuilder& WithAltitudeRange(float minY, float maxY);
    VegetationBuilder& WithScaleRange(float minScale, float maxScale);
    VegetationBuilder& WithLogicalRadius(float radius);
    VegetationBuilder& WithRequiredMask(uint32_t mask);
    VegetationBuilder& BlocksLineOfSight(bool enable = true);
    VegetationBuilder& Destructible(bool enable = true);
    VegetationBuilder& AlignToSlope(bool enable = true);

    const VegetationDef& Def() const { return def_; }
    VegetationDef Take() { return std::move(def_); }

private:
    VegetationSpecies& Last();
    void SetFlag(uint16_t bit, bool enable);

    VegetationDef def_;
};

}  // namespace Next::vegetation
