#pragma once

#include <cstdint>

namespace Next {
namespace Streaming {

enum class CellFileCompression : uint32_t {
    None = 0,
    Zstd = 1,
    LZ4 = 2,
};

constexpr uint32_t kCellFileMagic =
    static_cast<uint32_t>('N') |
    (static_cast<uint32_t>('C') << 8u) |
    (static_cast<uint32_t>('E') << 16u) |
    (static_cast<uint32_t>('L') << 24u);
constexpr uint16_t kCellFileVersion = 1;
constexpr uint16_t kCellFileHeaderSize = 32;

struct CellFileHeader {
    uint32_t magic = kCellFileMagic;
    uint16_t version = kCellFileVersion;
    uint16_t headerSize = kCellFileHeaderSize;
    uint32_t compressionType = static_cast<uint32_t>(CellFileCompression::None);
    uint32_t reserved = 0;
    uint64_t compressedSize = 0;
    uint64_t decompressedSize = 0;
};

static_assert(sizeof(CellFileHeader) == kCellFileHeaderSize, "CellFileHeader layout must stay stable");

inline bool IsSupportedCellFileCompression(uint32_t compressionType) {
    switch (static_cast<CellFileCompression>(compressionType)) {
        case CellFileCompression::None:
        case CellFileCompression::Zstd:
        case CellFileCompression::LZ4:
            return true;
    }
    return false;
}

inline CellFileHeader MakeCellFileHeader(
    CellFileCompression compression,
    uint64_t compressedSize,
    uint64_t decompressedSize) {
    CellFileHeader header;
    header.compressionType = static_cast<uint32_t>(compression);
    header.compressedSize = compressedSize;
    header.decompressedSize = decompressedSize;
    return header;
}

} // namespace Streaming
} // namespace Next
