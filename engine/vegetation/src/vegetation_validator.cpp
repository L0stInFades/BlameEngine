#include "next/vegetation/vegetation_validator.h"

#include <cmath>
#include <set>
#include <string>

namespace Next::vegetation {

const char* ToString(VegetationValidationCode code) {
    switch (code) {
        case VegetationValidationCode::EmptyVegetationId:
            return "EmptyVegetationId";
        case VegetationValidationCode::UnknownSchemaVersion:
            return "UnknownSchemaVersion";
        case VegetationValidationCode::NoSpecies:
            return "NoSpecies";
        case VegetationValidationCode::TooManySpecies:
            return "TooManySpecies";
        case VegetationValidationCode::NonPositiveMaxInstancesPerCell:
            return "NonPositiveMaxInstancesPerCell";
        case VegetationValidationCode::ReservedSpeciesId:
            return "ReservedSpeciesId";
        case VegetationValidationCode::DuplicateSpeciesId:
            return "DuplicateSpeciesId";
        case VegetationValidationCode::ZeroVisualId:
            return "ZeroVisualId";
        case VegetationValidationCode::NonFiniteDensity:
            return "NonFiniteDensity";
        case VegetationValidationCode::NonPositiveDensity:
            return "NonPositiveDensity";
        case VegetationValidationCode::DensityTooHigh:
            return "DensityTooHigh";
        case VegetationValidationCode::NonFiniteSpacing:
            return "NonFiniteSpacing";
        case VegetationValidationCode::NegativeSpacing:
            return "NegativeSpacing";
        case VegetationValidationCode::NonFiniteSlopeRange:
            return "NonFiniteSlopeRange";
        case VegetationValidationCode::SlopeOutOfRange:
            return "SlopeOutOfRange";
        case VegetationValidationCode::InvertedSlopeRange:
            return "InvertedSlopeRange";
        case VegetationValidationCode::NonFiniteAltitudeRange:
            return "NonFiniteAltitudeRange";
        case VegetationValidationCode::InvertedAltitudeRange:
            return "InvertedAltitudeRange";
        case VegetationValidationCode::NonFiniteScaleRange:
            return "NonFiniteScaleRange";
        case VegetationValidationCode::NonPositiveScale:
            return "NonPositiveScale";
        case VegetationValidationCode::InvertedScaleRange:
            return "InvertedScaleRange";
        case VegetationValidationCode::NonFiniteLogicalRadius:
            return "NonFiniteLogicalRadius";
        case VegetationValidationCode::NegativeLogicalRadius:
            return "NegativeLogicalRadius";
    }
    return "Unknown";
}

bool VegetationValidationReport::Has(VegetationValidationCode code) const {
    for (const VegetationValidationError& e : errors) {
        if (e.code == code) {
            return true;
        }
    }
    return false;
}

namespace {

bool Finite(float v) {
    return std::isfinite(v);
}

}  // namespace

VegetationValidationReport VegetationValidator::Validate(const VegetationDef& def) {
    using Code = VegetationValidationCode;
    VegetationValidationReport report;

    auto add = [&report](Code code, uint32_t index, std::string detail) {
        report.errors.push_back(VegetationValidationError{code, index, std::move(detail)});
    };

    // ---- Def-level (index = UINT32_MAX) ----
    if (def.id.empty()) {
        add(Code::EmptyVegetationId, UINT32_MAX, "vegetation id is empty");
    }
    if (def.schemaVersion != kVegetationSchemaVersion) {
        add(Code::UnknownSchemaVersion, UINT32_MAX, "schemaVersion=" + std::to_string(def.schemaVersion));
    }
    if (def.species.empty()) {
        add(Code::NoSpecies, UINT32_MAX, "no species declared");
    }
    if (def.species.size() > kMaxVegetationSpecies) {
        add(Code::TooManySpecies, UINT32_MAX, "species count=" + std::to_string(def.species.size()));
    }
    if (def.maxInstancesPerCell == 0) {
        add(Code::NonPositiveMaxInstancesPerCell, UINT32_MAX, "maxInstancesPerCell is 0");
    }

    // ---- Per species (in declaration order, so output is byte-stable) ----
    std::set<SpeciesId> seenIds;
    for (size_t i = 0; i < def.species.size(); ++i) {
        const VegetationSpecies& sp = def.species[i];
        const uint32_t idx = static_cast<uint32_t>(i);

        if (sp.id == kInvalidSpecies) {
            add(Code::ReservedSpeciesId, idx, "species id 0 is reserved");
        } else if (!seenIds.insert(sp.id).second) {
            add(Code::DuplicateSpeciesId, idx, "duplicate species id=" + std::to_string(sp.id));
        }

        if (sp.visual == 0) {
            add(Code::ZeroVisualId, idx, "visual id 0 -> nothing for UE5 to draw");
        }

        if (!Finite(sp.densityPerSqMeter)) {
            add(Code::NonFiniteDensity, idx, "density is NaN/Inf");
        } else if (sp.densityPerSqMeter <= 0.0f) {
            add(Code::NonPositiveDensity, idx, "density <= 0");
        } else if (sp.densityPerSqMeter > kMaxDensityPerSqMeter) {
            add(Code::DensityTooHigh, idx, "density exceeds the per-m^2 cap");
        }

        if (!Finite(sp.minSpacing)) {
            add(Code::NonFiniteSpacing, idx, "minSpacing is NaN/Inf");
        } else if (sp.minSpacing < 0.0f) {
            add(Code::NegativeSpacing, idx, "minSpacing < 0");
        }

        if (!Finite(sp.minSlopeDegrees) || !Finite(sp.maxSlopeDegrees)) {
            add(Code::NonFiniteSlopeRange, idx, "slope range is NaN/Inf");
        } else {
            if (sp.minSlopeDegrees < 0.0f || sp.maxSlopeDegrees > 90.0f) {
                add(Code::SlopeOutOfRange, idx, "slope range outside [0,90] degrees");
            }
            if (sp.minSlopeDegrees > sp.maxSlopeDegrees) {
                add(Code::InvertedSlopeRange, idx, "minSlope > maxSlope");
            }
        }

        if (!Finite(sp.minAltitude) || !Finite(sp.maxAltitude)) {
            add(Code::NonFiniteAltitudeRange, idx, "altitude range is NaN/Inf");
        } else if (sp.minAltitude > sp.maxAltitude) {
            add(Code::InvertedAltitudeRange, idx, "minAltitude > maxAltitude");
        }

        if (!Finite(sp.minScale) || !Finite(sp.maxScale)) {
            add(Code::NonFiniteScaleRange, idx, "scale range is NaN/Inf");
        } else {
            if (sp.minScale <= 0.0f) {
                add(Code::NonPositiveScale, idx, "minScale <= 0");
            }
            if (sp.minScale > sp.maxScale) {
                add(Code::InvertedScaleRange, idx, "minScale > maxScale");
            }
        }

        if (!Finite(sp.logicalRadius)) {
            add(Code::NonFiniteLogicalRadius, idx, "logicalRadius is NaN/Inf");
        } else if (sp.logicalRadius < 0.0f) {
            add(Code::NegativeLogicalRadius, idx, "logicalRadius < 0");
        }
    }

    return report;
}

}  // namespace Next::vegetation
