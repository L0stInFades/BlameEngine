#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "next/streaming/cell_file_format.h"  // CellFileCompression
#include "next/vegetation/vegetation_def.h"
#include "next/vegetation/vegetation_validator.h"

// Vegetation cook (ADR-0014). Offline production of a streaming-ready vegetation cell: validate a
// VegetationDef (fail-closed) -> deterministic ScatterCell over a terrain source -> PackCell ->
// wrap as a layered cell file carrying the CellLayer::Vegetation chunk. This is the bridge from the
// pure core (next_vegetation) to the streaming format (next_world); it lives in the integration lib
// next_vegetation_world so the core stays dependency-light.

namespace Next::vegetation {

struct CookResult {
    bool ok = false;
    std::vector<uint8_t> bytes;         // layered cell file bytes (Vegetation chunk) when ok
    VegetationValidationReport report;  // populated (and ok=false) when the def is rejected
    size_t instanceCount = 0;
};

// Cook one world cell. Terrain-agnostic (any ITerrainSampler). Deterministic: same (def, terrain,
// cell, cellSize, codec) => byte-identical `bytes` (golden-hashable). Fail-closed: an invalid def
// yields ok=false with a non-empty report and NO bytes.
CookResult CookVegetationCell(const VegetationDef& def, const ITerrainSampler& terrain, int32_t cellX, int32_t cellZ,
                              float cellSize,
                              Next::Streaming::CellFileCompression codec = Next::Streaming::CellFileCompression::None);

// Write bytes to a binary file (truncating). Returns false on IO failure.
bool WriteCellFile(const std::string& path, const std::vector<uint8_t>& bytes);

// Parse a VegetationDef from the line-based text format. Returns false with `outError` (line-numbered)
// on a malformed line. The produced def is NOT validated here — the cook validates it fail-closed.
//
// Format (whitespace-separated; '#' starts a comment; keys after a `species` line target that species):
//   id <stableId>
//   name <free text...>
//   seed <uint64>
//   maxInstancesPerCell <uint32>
//   species <visualId>
//     density <perSqMeter>
//     spacing <meters>
//     slope <minDeg> <maxDeg>
//     altitude <minY> <maxY>
//     scale <min> <max>
//     radius <logicalRadius>
//     mask <uint32>
//     flags blocksLOS,destructible,alignToSlope
bool ParseVegetationDefText(const std::string& text, VegetationDef& outDef, std::string& outError);

}  // namespace Next::vegetation
