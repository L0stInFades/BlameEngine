#include "next/vegetation/vegetation_builder.h"

#include <utility>

namespace Next::vegetation {

VegetationBuilder::VegetationBuilder(std::string id, std::string displayName) {
    def_.id = std::move(id);
    def_.name = std::move(displayName);
}

VegetationBuilder& VegetationBuilder::WithMasterSeed(uint64_t seed) {
    def_.masterSeed = seed;
    return *this;
}

VegetationBuilder& VegetationBuilder::WithMaxInstancesPerCell(uint32_t maxInstances) {
    def_.maxInstancesPerCell = maxInstances;
    return *this;
}

SpeciesId VegetationBuilder::AddSpecies(VisualStateId visual) {
    VegetationSpecies sp;
    sp.id = static_cast<SpeciesId>(def_.species.size() + 1);  // dense, 1-based
    sp.visual = visual;
    def_.species.push_back(sp);
    return sp.id;
}

VegetationSpecies& VegetationBuilder::Last() {
    // A With* is only meaningful after AddSpecies. Guard defensively so we never deref an empty
    // vector; the resulting visual==0 species is then rejected by the validator.
    if (def_.species.empty()) {
        AddSpecies(0);
    }
    return def_.species.back();
}

void VegetationBuilder::SetFlag(uint16_t bit, bool enable) {
    VegetationSpecies& sp = Last();
    if (enable) {
        sp.flags = static_cast<uint16_t>(sp.flags | bit);
    } else {
        sp.flags = static_cast<uint16_t>(sp.flags & static_cast<uint16_t>(~bit));
    }
}

VegetationBuilder& VegetationBuilder::WithDensity(float perSqMeter) {
    Last().densityPerSqMeter = perSqMeter;
    return *this;
}

VegetationBuilder& VegetationBuilder::WithSpacing(float minSpacing) {
    Last().minSpacing = minSpacing;
    return *this;
}

VegetationBuilder& VegetationBuilder::WithSlopeRange(float minDegrees, float maxDegrees) {
    VegetationSpecies& sp = Last();
    sp.minSlopeDegrees = minDegrees;
    sp.maxSlopeDegrees = maxDegrees;
    return *this;
}

VegetationBuilder& VegetationBuilder::WithAltitudeRange(float minY, float maxY) {
    VegetationSpecies& sp = Last();
    sp.minAltitude = minY;
    sp.maxAltitude = maxY;
    return *this;
}

VegetationBuilder& VegetationBuilder::WithScaleRange(float minScale, float maxScale) {
    VegetationSpecies& sp = Last();
    sp.minScale = minScale;
    sp.maxScale = maxScale;
    return *this;
}

VegetationBuilder& VegetationBuilder::WithLogicalRadius(float radius) {
    Last().logicalRadius = radius;
    return *this;
}

VegetationBuilder& VegetationBuilder::WithRequiredMask(uint32_t mask) {
    Last().requiredMask = mask;
    return *this;
}

VegetationBuilder& VegetationBuilder::BlocksLineOfSight(bool enable) {
    SetFlag(VegBlocksLineOfSight, enable);
    return *this;
}

VegetationBuilder& VegetationBuilder::Destructible(bool enable) {
    SetFlag(VegDestructible, enable);
    return *this;
}

VegetationBuilder& VegetationBuilder::AlignToSlope(bool enable) {
    SetFlag(VegAlignToSlope, enable);
    return *this;
}

}  // namespace Next::vegetation
