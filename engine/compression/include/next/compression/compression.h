// Adapted from the O3DE Compression Gem's LZ4 block-compression shape.
// SPDX-License-Identifier: Apache-2.0 OR MIT

#pragma once

#include <cstdint>
#include <string>

namespace Next {
namespace Compression {

enum class Algorithm : uint32_t {
    None = 0,
    Zstd = 1,
    LZ4 = 2,
};

enum class ResultCode : uint32_t {
    Complete = 0,
    UnsupportedAlgorithm,
    InvalidArgument,
    OutputBufferTooSmall,
    CodecFailure,
};

struct Result {
    ResultCode code = ResultCode::Complete;
    uint64_t bytesWritten = 0;
    std::string message;

    bool Succeeded() const { return code == ResultCode::Complete; }
};

const char* AlgorithmName(Algorithm algorithm);
bool IsAvailable(Algorithm algorithm);

// Returns the worst-case output size for a compression pass. For decompression,
// callers must know the uncompressed size from package/cell metadata.
uint64_t CompressBound(Algorithm algorithm, uint64_t inputSize);

Result Compress(
    Algorithm algorithm,
    const void* input,
    uint64_t inputSize,
    void* output,
    uint64_t outputCapacity,
    int compressionLevel = 3);

Result Decompress(
    Algorithm algorithm,
    const void* input,
    uint64_t inputSize,
    void* output,
    uint64_t outputCapacity);

} // namespace Compression
} // namespace Next
