#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "next/streaming/cell_file_format.h"  // CellFileCompression
#include "next/water/water_def.h"
#include "next/water/water_validator.h"

// Water cook (ADR-0015). Offline production of a streaming-ready water cell: validate a WaterSceneDef
// (fail-closed) -> select the bodies whose footprint overlaps this world cell -> bake each into a flat
// WaterBodyInstance (stable 1-based bodyId = its index in the scene, so a body spanning many cells gets
// the SAME id and the runtime store de-duplicates) -> PackCell -> wrap as a layered cell carrying the
// CellLayer::Water chunk. The bridge from the pure core (next_water) to the streaming format.

namespace Next::water {

struct WaterCookResult {
    bool ok = false;
    std::vector<uint8_t> bytes;    // layered cell file (Water chunk) when ok
    WaterValidationReport report;  // populated (and ok=false) when the scene is rejected
    size_t bodyCount = 0;          // bodies baked into this cell
};

// Cook one world cell. Deterministic: same (scene, cell, cellSize, codec) => byte-identical bytes
// (golden-hashable). Fail-closed: an invalid scene yields ok=false with a non-empty report and NO bytes.
// The cell's XZ footprint is [cellX*cellSize, (cellX+1)*cellSize) x [cellZ*cellSize, (cellZ+1)*cellSize);
// a body is included iff its XZ AABB overlaps it (recorded whole, not clipped — the store de-dups).
WaterCookResult CookWaterCell(const WaterSceneDef& scene, int32_t cellX, int32_t cellZ, float cellSize,
                              Next::Streaming::CellFileCompression codec = Next::Streaming::CellFileCompression::None);

// Bake one authored body into its fixed wire record with the given stable id (keeps the first
// kMaxWavesPerBody waves). Exposed for tests / tooling.
WaterBodyInstance BakeBody(const WaterBodyDef& def, uint32_t bodyId);

bool WriteWaterCellFile(const std::string& path, const std::vector<uint8_t>& bytes);

// Like WriteWaterCellFile, but MERGES into any existing layered cell at `path`: layers already present
// that are NOT in `newLayeredCellBytes` (e.g. a Vegetation chunk) are PRESERVED; same-layer chunks are
// replaced. So cooking water for a cell that already carries vegetation does NOT clobber it. Falls back
// to a plain write when no readable layered cell exists at `path`. Returns false only on write failure
// (or if the freshly-cooked bytes fail to parse, which should never happen).
bool WriteWaterCellFileMerged(const std::string& path, const std::vector<uint8_t>& newLayeredCellBytes);

// Parse a line-based water scene def (see ADR-0015 / assetc usage). FAIL-CLOSED on a malformed line.
bool ParseWaterDefText(const std::string& text, WaterSceneDef& outScene, std::string& outError);

}  // namespace Next::water
