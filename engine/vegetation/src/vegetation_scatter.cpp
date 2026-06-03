#include "next/vegetation/vegetation_scatter.h"

#include <cmath>
#include <unordered_map>

#include "next/vegetation/vegetation_rng.h"

namespace Next::vegetation {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Key for the per-species min-spacing acceleration grid (cells of side = minSpacing).
struct SpacingKey {
    int32_t x;
    int32_t z;
    bool operator==(const SpacingKey& o) const { return x == o.x && z == o.z; }
};

struct SpacingKeyHash {
    size_t operator()(const SpacingKey& k) const {
        const uint64_t packed = (static_cast<uint64_t>(static_cast<uint32_t>(k.x)) << 32) ^ static_cast<uint32_t>(k.z);
        return static_cast<size_t>(Splitmix64(packed));
    }
};

}  // namespace

std::vector<VegetationInstance> ScatterCell(const VegetationDef& def, const ITerrainSampler& terrain, int32_t cellX,
                                            int32_t cellZ, float cellSize) {
    std::vector<VegetationInstance> out;
    if (cellSize <= 0.0f || def.maxInstancesPerCell == 0) {
        return out;
    }

    const float originX = static_cast<float>(cellX) * cellSize;
    const float originZ = static_cast<float>(cellZ) * cellSize;
    const Next::Vec3 up(0.0f, 1.0f, 0.0f);

    bool capped = false;  // cell-level: the per-cell cap stops ALL species, not just the current one
    for (const VegetationSpecies& sp : def.species) {
        if (capped) {
            break;
        }
        if (!(sp.densityPerSqMeter > 0.0f)) {  // also rejects NaN
            continue;
        }

        // Grid step from density, never tighter than the separation radius; then retiled to divide
        // the cell evenly so neighbouring cells share no seam (their grids meet edge-to-edge).
        float step = 1.0f / std::sqrt(sp.densityPerSqMeter);
        if (sp.minSpacing > step) {
            step = sp.minSpacing;
        }
        if (!(step > 0.0f) || step > cellSize) {  // NaN, or spacing larger than the whole cell
            step = cellSize;
        }
        int32_t gridDim = static_cast<int32_t>(cellSize / step + 0.5f);
        if (gridDim < 1) {
            gridDim = 1;
        }
        const float tile = cellSize / static_cast<float>(gridDim);

        // Slope thresholds as cosines (cos decreases with angle): a slope <= maxSlope is exactly
        // cos(slope) >= cos(maxSlope). Computed once per species — no per-candidate acos.
        const float cosSteepest = std::cos(sp.maxSlopeDegrees * kPi / 180.0f);
        const float cosFlattest = std::cos(sp.minSlopeDegrees * kPi / 180.0f);

        const bool useSpacing = sp.minSpacing > 0.0f;
        const float spacingSq = sp.minSpacing * sp.minSpacing;
        std::unordered_map<SpacingKey, std::vector<uint32_t>, SpacingKeyHash> spacingGrid;

        for (int32_t i = 0; i < gridDim && !capped; ++i) {
            for (int32_t j = 0; j < gridDim; ++j) {
                // One deterministic stream per (cell, species, node) — order-independent.
                const uint64_t seed =
                    NodeSeed(def.masterSeed, cellX, cellZ, sp.id, static_cast<uint32_t>(i), static_cast<uint32_t>(j));
                DetRng rng(seed);

                // Jitter the candidate anywhere within its tile.
                const float worldX = originX + (static_cast<float>(i) + rng.NextFloat01()) * tile;
                const float worldZ = originZ + (static_cast<float>(j) + rng.NextFloat01()) * tile;

                const TerrainSample s = terrain.SampleAt(worldX, worldZ);
                if (s.height < sp.minAltitude || s.height > sp.maxAltitude) {
                    continue;
                }

                Next::Vec3 n = s.normal.Normalize();
                if (n.Dot(n) < 0.25f) {
                    n = up;  // degenerate terrain normal -> treat as flat ground (avoids a (0,0,0) normal)
                }
                const float slopeCos = n.Dot(up);
                if (slopeCos < cosSteepest || slopeCos > cosFlattest) {
                    continue;  // too steep, or (when minSlope>0) too flat
                }

                if (sp.requiredMask != 0 && (s.mask & sp.requiredMask) == 0) {
                    continue;
                }

                int32_t gx = 0;
                int32_t gz = 0;
                if (useSpacing) {
                    gx = static_cast<int32_t>(std::floor(worldX / sp.minSpacing));
                    gz = static_cast<int32_t>(std::floor(worldZ / sp.minSpacing));
                    bool tooClose = false;
                    for (int32_t dx = -1; dx <= 1 && !tooClose; ++dx) {
                        for (int32_t dz = -1; dz <= 1 && !tooClose; ++dz) {
                            const auto it = spacingGrid.find(SpacingKey{gx + dx, gz + dz});
                            if (it == spacingGrid.end()) {
                                continue;
                            }
                            for (const uint32_t idx : it->second) {
                                const float ddx = out[idx].position[0] - worldX;
                                const float ddz = out[idx].position[2] - worldZ;
                                if (ddx * ddx + ddz * ddz < spacingSq) {
                                    tooClose = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (tooClose) {
                        continue;
                    }
                }

                VegetationInstance inst;
                inst.position[0] = worldX;
                inst.position[1] = s.height;
                inst.position[2] = worldZ;
                inst.normal[0] = n.x;
                inst.normal[1] = n.y;
                inst.normal[2] = n.z;
                inst.rotationY = rng.NextRange(0.0f, 2.0f * kPi);
                inst.scale = (sp.maxScale > sp.minScale) ? rng.NextRange(sp.minScale, sp.maxScale) : sp.minScale;
                inst.logicalRadius = sp.logicalRadius;
                inst.visual = sp.visual;
                inst.instanceId = static_cast<uint32_t>(out.size());  // dense per-cell ordinal (deterministic)
                inst.species = sp.id;
                inst.flags = sp.flags;

                if (out.size() >= def.maxInstancesPerCell) {
                    capped = true;
                    break;  // hard per-cell cap reached -> stop BEFORE exceeding it
                }
                const uint32_t newIndex = static_cast<uint32_t>(out.size());
                out.push_back(inst);
                if (useSpacing) {
                    spacingGrid[SpacingKey{gx, gz}].push_back(newIndex);
                }
            }
        }
    }

    return out;
}

}  // namespace Next::vegetation
