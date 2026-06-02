#pragma once

#include <cstdint>
#include <vector>

#include "next/vegetation/vegetation_def.h"

// CellLayer::Vegetation payload (ADR-0014): a flat, versioned, native-endian blob = header + a
// contiguous VegetationInstance array. This is the bridge from the headless scatter to streaming and
// to UE5 — the bytes the core hands across the sim->UE5 boundary, where UE5 reads them into HISM /
// Nanite / foliage. Mirrors the ethos of engine/world's cell_file_format.h (magic + version + size,
// fail-closed parse). Plus read-only spatial queries the gameplay sim runs on a loaded cell.

namespace Next::vegetation {

constexpr uint32_t kVegetationCellMagic = static_cast<uint32_t>('N') | (static_cast<uint32_t>('V') << 8u) |
                                          (static_cast<uint32_t>('E') << 16u) | (static_cast<uint32_t>('G') << 24u);
constexpr uint16_t kVegetationCellVersion = 1;

struct VegetationCellHeader {
    uint32_t magic = kVegetationCellMagic;
    uint16_t version = kVegetationCellVersion;
    uint16_t headerSize = 24;
    uint32_t instanceCount = 0;
    int32_t cellX = 0;
    int32_t cellZ = 0;
    float cellSize = 0.0f;
};

static_assert(sizeof(VegetationCellHeader) == 24, "VegetationCellHeader layout must stay stable");
static_assert(alignof(VegetationCellHeader) == 4, "VegetationCellHeader alignment must stay stable");

struct VegetationCellData {
    VegetationCellHeader header{};
    std::vector<VegetationInstance> instances{};
};

// Serialize instances into a cell blob (header + array). cellSize/cellX/cellZ are recorded for the
// consumer's bookkeeping; they do not change the instance bytes.
std::vector<uint8_t> PackCell(int32_t cellX, int32_t cellZ, float cellSize,
                              const std::vector<VegetationInstance>& instances);

// Parse a cell blob. FAIL-CLOSED: returns false (leaving `out` unspecified) on any mismatch — bad
// magic, unknown version, wrong header size, or a length that does not equal header + count*stride.
bool UnpackCell(const uint8_t* data, size_t size, VegetationCellData& out);

// Instance ids whose XZ position is within `radius` of (x,z). Deterministic order (ascending instance
// index). Squared distance, no sqrt. The backbone of "what vegetation is near here" for AI / cover /
// line-of-sight. `radius` <= 0 returns empty.
std::vector<uint32_t> QueryRadiusXZ(const std::vector<VegetationInstance>& instances, float x, float z, float radius);

}  // namespace Next::vegetation
