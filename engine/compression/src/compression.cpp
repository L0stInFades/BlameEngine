// Adapted from the O3DE Compression Gem's LZ4 block-compression shape.
// SPDX-License-Identifier: Apache-2.0 OR MIT

#include "next/compression/compression.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <sstream>
#include <utility>

#ifdef NEXT_HAS_LZ4
#include <lz4.h>
#endif

#ifdef NEXT_HAS_ZSTD
#include <zstd.h>
#endif

namespace Next {
namespace Compression {
namespace {

Result Fail(ResultCode code, std::string message) {
    Result result;
    result.code = code;
    result.message = std::move(message);
    return result;
}

Result Complete(uint64_t bytesWritten) {
    Result result;
    result.bytesWritten = bytesWritten;
    return result;
}

bool ValidateBuffers(const void* input, uint64_t inputSize, void* output, uint64_t outputCapacity, Result& result) {
    if ((!input && inputSize != 0) || (!output && outputCapacity != 0)) {
        result = Fail(ResultCode::InvalidArgument, "input/output buffer pointer is null");
        return false;
    }
    if (inputSize > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        result = Fail(ResultCode::InvalidArgument, "input is too large for the current block codec");
        return false;
    }
    if (outputCapacity > static_cast<uint64_t>(std::numeric_limits<int>::max())) {
        result = Fail(ResultCode::InvalidArgument, "output buffer is too large for the current block codec");
        return false;
    }
    return true;
}

} // namespace

const char* AlgorithmName(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::None: return "none";
        case Algorithm::Zstd: return "zstd";
        case Algorithm::LZ4:  return "lz4";
        default:              return "unknown";
    }
}

bool IsAvailable(Algorithm algorithm) {
    switch (algorithm) {
        case Algorithm::None:
            return true;
        case Algorithm::Zstd:
#ifdef NEXT_HAS_ZSTD
            return true;
#else
            return false;
#endif
        case Algorithm::LZ4:
#ifdef NEXT_HAS_LZ4
            return true;
#else
            return false;
#endif
        default:
            return false;
    }
}

uint64_t CompressBound(Algorithm algorithm, uint64_t inputSize) {
    switch (algorithm) {
        case Algorithm::None:
            return inputSize;
        case Algorithm::Zstd:
#ifdef NEXT_HAS_ZSTD
            return static_cast<uint64_t>(ZSTD_compressBound(static_cast<size_t>(inputSize)));
#else
            return 0;
#endif
        case Algorithm::LZ4:
#ifdef NEXT_HAS_LZ4
            if (inputSize > static_cast<uint64_t>(LZ4_MAX_INPUT_SIZE)) {
                return 0;
            }
            return static_cast<uint64_t>(LZ4_compressBound(static_cast<int>(inputSize)));
#else
            return 0;
#endif
        default:
            return 0;
    }
}

Result Compress(
    Algorithm algorithm,
    const void* input,
    uint64_t inputSize,
    void* output,
    uint64_t outputCapacity,
    int compressionLevel) {
    Result validation;
    if (!ValidateBuffers(input, inputSize, output, outputCapacity, validation)) {
        return validation;
    }

    if (!IsAvailable(algorithm)) {
        return Fail(ResultCode::UnsupportedAlgorithm, std::string(AlgorithmName(algorithm)) + " backend is not available");
    }

    if (algorithm == Algorithm::None) {
        if (outputCapacity < inputSize) {
            return Fail(ResultCode::OutputBufferTooSmall, "output buffer cannot fit uncompressed data");
        }
        if (inputSize != 0) {
            std::memcpy(output, input, static_cast<size_t>(inputSize));
        }
        return Complete(inputSize);
    }

    if (outputCapacity == 0) {
        return Fail(ResultCode::OutputBufferTooSmall, "output buffer is empty");
    }

    switch (algorithm) {
        case Algorithm::Zstd: {
#ifdef NEXT_HAS_ZSTD
            const size_t compressedSize = ZSTD_compress(
                output,
                static_cast<size_t>(outputCapacity),
                input,
                static_cast<size_t>(inputSize),
                compressionLevel);
            if (ZSTD_isError(compressedSize)) {
                return Fail(ResultCode::CodecFailure, ZSTD_getErrorName(compressedSize));
            }
            return Complete(static_cast<uint64_t>(compressedSize));
#else
            break;
#endif
        }
        case Algorithm::LZ4: {
#ifdef NEXT_HAS_LZ4
            const int worstCaseCompressedSize = LZ4_compressBound(static_cast<int>(inputSize));
            if (worstCaseCompressedSize == 0) {
                return Fail(ResultCode::InvalidArgument, "input is too large for LZ4 block compression");
            }
            if (outputCapacity < static_cast<uint64_t>(worstCaseCompressedSize)) {
                std::ostringstream message;
                message << "output buffer capacity " << outputCapacity
                        << " is below LZ4 worst-case bound " << worstCaseCompressedSize;
                return Fail(ResultCode::OutputBufferTooSmall, message.str());
            }

            const int compressedSize = LZ4_compress_default(
                static_cast<const char*>(input),
                static_cast<char*>(output),
                static_cast<int>(inputSize),
                static_cast<int>(outputCapacity));
            if (compressedSize == 0) {
                return Fail(ResultCode::CodecFailure, "LZ4_compress_default failed");
            }
            return Complete(static_cast<uint64_t>(compressedSize));
#else
            break;
#endif
        }
        default:
            break;
    }

    return Fail(ResultCode::UnsupportedAlgorithm, "unsupported compression algorithm");
}

Result Decompress(
    Algorithm algorithm,
    const void* input,
    uint64_t inputSize,
    void* output,
    uint64_t outputCapacity) {
    Result validation;
    if (!ValidateBuffers(input, inputSize, output, outputCapacity, validation)) {
        return validation;
    }

    if (!IsAvailable(algorithm)) {
        return Fail(ResultCode::UnsupportedAlgorithm, std::string(AlgorithmName(algorithm)) + " backend is not available");
    }

    if (algorithm == Algorithm::None) {
        if (outputCapacity < inputSize) {
            return Fail(ResultCode::OutputBufferTooSmall, "output buffer cannot fit uncompressed data");
        }
        if (inputSize != 0) {
            std::memcpy(output, input, static_cast<size_t>(inputSize));
        }
        return Complete(inputSize);
    }

    if (outputCapacity == 0) {
        return Fail(ResultCode::OutputBufferTooSmall, "decompression buffer is empty");
    }

    switch (algorithm) {
        case Algorithm::Zstd: {
#ifdef NEXT_HAS_ZSTD
            const size_t decompressedSize = ZSTD_decompress(
                output,
                static_cast<size_t>(outputCapacity),
                input,
                static_cast<size_t>(inputSize));
            if (ZSTD_isError(decompressedSize)) {
                return Fail(ResultCode::CodecFailure, ZSTD_getErrorName(decompressedSize));
            }
            return Complete(static_cast<uint64_t>(decompressedSize));
#else
            break;
#endif
        }
        case Algorithm::LZ4: {
#ifdef NEXT_HAS_LZ4
            const int decompressedSize = LZ4_decompress_safe(
                static_cast<const char*>(input),
                static_cast<char*>(output),
                static_cast<int>(inputSize),
                static_cast<int>(outputCapacity));
            if (decompressedSize < 0) {
                return Fail(ResultCode::CodecFailure, "LZ4_decompress_safe failed; buffer too small or source malformed");
            }
            return Complete(static_cast<uint64_t>(decompressedSize));
#else
            break;
#endif
        }
        default:
            break;
    }

    return Fail(ResultCode::UnsupportedAlgorithm, "unsupported compression algorithm");
}

} // namespace Compression
} // namespace Next
