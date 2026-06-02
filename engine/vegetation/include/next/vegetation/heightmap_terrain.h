#pragma once

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "next/vegetation/vegetation_def.h"

// A real terrain source for vegetation scatter (ADR-0014): a regular heightmap sampled bilinearly,
// with surface normals from the height gradient and an optional paint mask. This is what proves the
// scatter works on actual varying terrain — slope exclusion, altitude bands, on-surface placement —
// not just FlatTerrainSampler. Headless, deterministic, header-only.

namespace Next::vegetation {

class HeightmapTerrainSampler : public ITerrainSampler {
public:
    // heights: row-major width*height samples (meters). The sample grid covers world XZ starting at
    // (originX, originZ), `spacing` meters between samples. Optional `mask` (same dims) is returned as
    // the sample mask (default all-bits-set). Out-of-range samples clamp to the nearest edge.
    HeightmapTerrainSampler(int32_t width, int32_t height, float spacing, float originX, float originZ,
                            std::vector<float> heights, std::vector<uint32_t> mask = {})
        : width_(width > 0 ? width : 1),
          height_(height > 0 ? height : 1),
          spacing_(spacing > 0.0f ? spacing : 1.0f),
          originX_(originX),
          originZ_(originZ),
          heights_(std::move(heights)),
          mask_(std::move(mask)) {}

    TerrainSample SampleAt(float worldX, float worldZ) const override {
        TerrainSample s;
        s.height = SampleHeight(worldX, worldZ);

        // Normal from central differences over one sample step (slope of the bilinear field).
        const float e = spacing_;
        const float dhx = (SampleHeight(worldX + e, worldZ) - SampleHeight(worldX - e, worldZ)) / (2.0f * e);
        const float dhz = (SampleHeight(worldX, worldZ + e) - SampleHeight(worldX, worldZ - e)) / (2.0f * e);
        s.normal = Next::Vec3(-dhx, 1.0f, -dhz);  // scatter normalizes it

        s.mask = SampleMask(worldX, worldZ);
        return s;
    }

private:
    static int32_t Clampi(int32_t v, int32_t lo, int32_t hi) {
        if (v < lo) {
            return lo;
        }
        return v > hi ? hi : v;
    }
    static float Clampf(float v, float lo, float hi) {
        if (v < lo) {
            return lo;
        }
        return v > hi ? hi : v;
    }

    float At(int32_t x, int32_t z) const {
        return heights_[(static_cast<size_t>(z) * static_cast<size_t>(width_)) + static_cast<size_t>(x)];
    }

    float SampleHeight(float worldX, float worldZ) const {
        if (heights_.empty()) {
            return 0.0f;
        }
        const float gx = (worldX - originX_) / spacing_;
        const float gz = (worldZ - originZ_) / spacing_;
        const int32_t x0 = Clampi(static_cast<int32_t>(std::floor(gx)), 0, width_ - 1);
        const int32_t z0 = Clampi(static_cast<int32_t>(std::floor(gz)), 0, height_ - 1);
        const int32_t x1 = Clampi(x0 + 1, 0, width_ - 1);
        const int32_t z1 = Clampi(z0 + 1, 0, height_ - 1);
        const float fx = Clampf(gx - std::floor(gx), 0.0f, 1.0f);
        const float fz = Clampf(gz - std::floor(gz), 0.0f, 1.0f);
        const float a = At(x0, z0) + ((At(x1, z0) - At(x0, z0)) * fx);
        const float b = At(x0, z1) + ((At(x1, z1) - At(x0, z1)) * fx);
        return a + ((b - a) * fz);
    }

    uint32_t SampleMask(float worldX, float worldZ) const {
        if (mask_.empty()) {
            return 0xFFFFFFFFu;
        }
        const int32_t x =
            Clampi(static_cast<int32_t>(std::floor(((worldX - originX_) / spacing_) + 0.5f)), 0, width_ - 1);
        const int32_t z =
            Clampi(static_cast<int32_t>(std::floor(((worldZ - originZ_) / spacing_) + 0.5f)), 0, height_ - 1);
        return mask_[(static_cast<size_t>(z) * static_cast<size_t>(width_)) + static_cast<size_t>(x)];
    }

    int32_t width_;
    int32_t height_;
    float spacing_;
    float originX_;
    float originZ_;
    std::vector<float> heights_;
    std::vector<uint32_t> mask_;
};

}  // namespace Next::vegetation
