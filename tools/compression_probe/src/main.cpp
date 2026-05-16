#include "next/compression/compression.h"

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool RoundTrip(Next::Compression::Algorithm algorithm, const std::string& payload) {
    if (!Next::Compression::IsAvailable(algorithm)) {
        std::cout << Next::Compression::AlgorithmName(algorithm) << ": unavailable\n";
        return true;
    }

    const uint64_t bound = Next::Compression::CompressBound(algorithm, payload.size());
    if (bound == 0) {
        std::cerr << Next::Compression::AlgorithmName(algorithm) << ": invalid compression bound\n";
        return false;
    }

    std::vector<char> compressed(static_cast<size_t>(bound));
    const auto compress = Next::Compression::Compress(
        algorithm,
        payload.data(),
        payload.size(),
        compressed.data(),
        compressed.size());
    if (!compress.Succeeded()) {
        std::cerr << Next::Compression::AlgorithmName(algorithm)
                  << ": compression failed: " << compress.message << "\n";
        return false;
    }

    std::vector<char> decompressed(payload.size());
    const auto decompress = Next::Compression::Decompress(
        algorithm,
        compressed.data(),
        compress.bytesWritten,
        decompressed.data(),
        decompressed.size());
    if (!decompress.Succeeded()) {
        std::cerr << Next::Compression::AlgorithmName(algorithm)
                  << ": decompression failed: " << decompress.message << "\n";
        return false;
    }

    const bool ok = decompress.bytesWritten == payload.size() &&
        std::memcmp(decompressed.data(), payload.data(), payload.size()) == 0;
    if (!ok) {
        std::cerr << Next::Compression::AlgorithmName(algorithm) << ": roundtrip mismatch\n";
        return false;
    }

    std::cout << Next::Compression::AlgorithmName(algorithm)
              << ": " << payload.size() << " -> " << compress.bytesWritten
              << " -> " << decompress.bytesWritten << "\n";
    return true;
}

} // namespace

int main() {
    const std::string payload =
        "NEXT HackOps policy package: streaming cell, route risk, world state. "
        "NEXT HackOps policy package: streaming cell, route risk, world state. "
        "NEXT HackOps policy package: streaming cell, route risk, world state.";

    bool ok = true;
    ok = RoundTrip(Next::Compression::Algorithm::LZ4, payload) && ok;
    ok = RoundTrip(Next::Compression::Algorithm::Zstd, payload) && ok;

    if (!ok) {
        return 1;
    }

    std::cout << "NEXT_COMPRESSION_PROBE_OK\n";
    return 0;
}
