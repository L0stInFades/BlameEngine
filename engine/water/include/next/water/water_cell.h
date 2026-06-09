#pragma once

#include <cstdint>
#include <vector>

#include "next/water/water_def.h"

// CellLayer::Water payload: a flat, VERSIONED, native-endian blob = header + a contiguous
// WaterBodyInstance array. The bytes the core hands across the sim->UE5 boundary (UE5 reads them to
// place/animate its water surfaces) and that the runtime WaterStore indexes. Mirrors the ethos of
// engine/vegetation's vegetation_cell.h and engine/world's cell_file_format.h: magic + version + size,
// fail-closed parse, an allocation ceiling checked BEFORE sizing.
//
// WIRE VERSIONING + MIGRATION (W28). The header carries an explicit version AND the on-disk record
// stride, so the reader never assumes the bytes match the currently-compiled struct. UnpackCell
// DISPATCHES on the version:
//   v1 (legacy): 24-byte header, no stride field; body records are the frozen v1 layout.
//   v2 (current): 28-byte header carrying bodyStride; body records are the current WaterBodyInstance.
// A v1 blob (an asset cooked before the format moved to v2) STILL loads — it is decoded by the v1
// reader and migrated to the current in-memory form. An UNKNOWN/newer version fails closed (no silent
// partial parse). This is the migration seam: when the header or WaterBodyInstance changes, bump
// kWaterCellVersion, add a vN decoder + a body migrator, and KEEP the older decoders + their frozen
// sizes. PackCellLegacyV1 exists so the migration path is exercised by a real round-trip test.

namespace Next::water {

constexpr uint32_t kWaterCellMagic = static_cast<uint32_t>('N') | (static_cast<uint32_t>('W') << 8u) |
                                     (static_cast<uint32_t>('T') << 16u) | (static_cast<uint32_t>('R') << 24u);

// Bump on ANY change to the on-disk header or body record. Older versions stay readable via migration.
constexpr uint16_t kWaterCellVersion = 2;
constexpr uint16_t kWaterCellVersionV1 = 1;  // legacy; still decoded + migrated to the current layout

// Frozen on-disk sizes per published version. NEVER edit a published constant — it documents bytes
// already written to disk; add a new constant when you add a new version.
constexpr uint16_t kWaterCellHeaderSizeV1 = 24;
constexpr uint16_t kWaterCellHeaderSizeV2 = 28;
constexpr uint32_t kWaterBodyRecordSizeV1 = 264;  // sizeof the v1 WaterBodyInstance layout

struct WaterCellHeader {
    uint32_t magic = kWaterCellMagic;
    uint16_t version = kWaterCellVersion;
    uint16_t headerSize = kWaterCellHeaderSizeV2;
    uint32_t bodyCount = 0;
    int32_t cellX = 0;
    int32_t cellZ = 0;
    float cellSize = 0.0f;
    uint32_t bodyStride = static_cast<uint32_t>(sizeof(WaterBodyInstance));  // on-disk body record size
};
static_assert(sizeof(WaterCellHeader) == 28, "WaterCellHeader layout must stay stable");
static_assert(alignof(WaterCellHeader) == 4, "WaterCellHeader alignment must stay stable");

struct WaterCellData {
    WaterCellHeader header{};                 // reflects the ON-DISK provenance (version/headerSize/bodyStride as read)
    std::vector<WaterBodyInstance> bodies{};  // ALWAYS migrated to the current in-memory layout
};

// Serialize bodies into a CURRENT (v2) cell blob (header + array). Deterministic; byte-stable for
// equal inputs (no padding in the header, POD bodies).
std::vector<uint8_t> PackCell(int32_t cellX, int32_t cellZ, float cellSize,
                              const std::vector<WaterBodyInstance>& bodies);

// Serialize bodies into the LEGACY v1 blob layout (24-byte header, no stride field). Only for migration
// tests / one-off downgrade tooling; production cooking always emits the current version via PackCell.
std::vector<uint8_t> PackCellLegacyV1(int32_t cellX, int32_t cellZ, float cellSize,
                                      const std::vector<WaterBodyInstance>& bodies);

// Parse a cell blob of ANY known version, migrating older layouts to the current in-memory form.
// FAIL-CLOSED: returns false (leaving `out` unspecified) on bad magic, an UNKNOWN/newer version, a
// header size that disagrees with the version, a body count past kMaxWaterBodiesPerCell (checked before
// sizing, so a crafted header can't drive a pathological allocation), a body stride that disagrees with
// the version, or a length that does not equal header + count*stride.
bool UnpackCell(const uint8_t* data, size_t size, WaterCellData& out);

}  // namespace Next::water
