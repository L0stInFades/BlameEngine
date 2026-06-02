#pragma once

#include <cstdint>
#include <vector>

#include "next/streaming/cell_file_format.h"  // CellFileCompression
#include "next/streaming/world_partition.h"   // CellLayer

namespace Next {
namespace Streaming {

// Layered cell container (ADR-0014). A single cell file carrying MULTIPLE independently-loadable
// layer chunks (StaticMesh / Vegetation / Collision / ...), each optionally compressed with its own
// codec. This is the multi-layer format the streaming system needs to stream one layer (e.g.
// Vegetation) on its own. Distinct magic ('NCL2') from the v1 single-payload .ncell (NCEL) wrapper,
// which stays valid — nothing here rewrites the v1 path.
//
// Layout (native-endian, like cell_file_format.h):
//   LayeredCellHeader
//   LayeredCellChunkEntry [layerCount]      (the directory)
//   <chunk payloads, concatenated>          (each raw, or codec-compressed)

constexpr uint32_t kLayeredCellMagic = static_cast<uint32_t>('N') | (static_cast<uint32_t>('C') << 8u) |
                                       (static_cast<uint32_t>('L') << 16u) | (static_cast<uint32_t>('2') << 24u);
constexpr uint16_t kLayeredCellVersion = 1;

// Per-chunk decompressed-size ceiling — a corrupt entry can't trigger a pathological allocation.
constexpr uint64_t kLayeredCellMaxChunkBytes = 512ull * 1024ull * 1024ull;

struct LayeredCellHeader {
    uint32_t magic = kLayeredCellMagic;
    uint16_t version = kLayeredCellVersion;
    uint16_t headerSize = 16;
    uint32_t layerCount = 0;
    uint32_t reserved = 0;
};
static_assert(sizeof(LayeredCellHeader) == 16, "LayeredCellHeader layout must stay stable");

struct LayeredCellChunkEntry {
    uint32_t layer = 0;             // CellLayer
    uint32_t codec = 0;             // CellFileCompression actually used for this chunk
    uint64_t offset = 0;            // bytes from file start to this chunk's payload
    uint64_t compressedSize = 0;    // payload bytes on disk
    uint64_t decompressedSize = 0;  // bytes after decompression (== compressedSize when codec None)
};
static_assert(sizeof(LayeredCellChunkEntry) == 32, "LayeredCellChunkEntry layout must stay stable");

// One layer's decompressed bytes (parse output).
struct LayeredCellChunk {
    CellLayer layer = CellLayer::StaticMesh;
    CellFileCompression codec = CellFileCompression::None;  // codec used on disk
    std::vector<uint8_t> data;                              // decompressed
};

// Input to PackLayeredCell: a layer's decompressed bytes + the codec to attempt.
struct LayeredCellChunkInput {
    CellLayer layer = CellLayer::StaticMesh;
    CellFileCompression codec = CellFileCompression::None;  // desired; falls back to None if unavailable
    std::vector<uint8_t> data;
};

// Serialize chunks into a layered cell blob. Each chunk is compressed with its desired codec when the
// codec is available AND actually shrinks the data; otherwise it is stored raw and the entry records
// None. Deterministic given inputs + available codecs. Chunk order is preserved.
std::vector<uint8_t> PackLayeredCell(const std::vector<LayeredCellChunkInput>& chunks);

// Parse a layered cell blob into decompressed chunks. FAIL-CLOSED: returns false (clearing outChunks)
// on any inconsistency — bad magic/version/headerSize, directory past EOF, a chunk past EOF, an
// unsupported codec, an over-cap decompressed size, or a decode/size mismatch.
bool ParseLayeredCell(const uint8_t* data, size_t size, std::vector<LayeredCellChunk>& outChunks);

// Extract one layer's decompressed bytes (first match). Returns false if the layer is absent or the
// blob is malformed. Convenience over ParseLayeredCell for the streaming loader.
bool ExtractLayer(const uint8_t* data, size_t size, CellLayer layer, std::vector<uint8_t>& outBytes);

}  // namespace Streaming
}  // namespace Next
