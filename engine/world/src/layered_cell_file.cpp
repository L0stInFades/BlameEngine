#include "next/streaming/layered_cell_file.h"

#include <cstring>

#include "next/compression/compression.h"

namespace Next {
namespace Streaming {
namespace {

Compression::Algorithm ToAlgorithm(CellFileCompression codec) {
    switch (codec) {
        case CellFileCompression::Zstd:
            return Compression::Algorithm::Zstd;
        case CellFileCompression::LZ4:
            return Compression::Algorithm::LZ4;
        case CellFileCompression::None:
            break;
    }
    return Compression::Algorithm::None;
}

void AppendBytes(std::vector<uint8_t>& dst, const void* src, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(src);
    dst.insert(dst.end(), p, p + n);
}

}  // namespace

std::vector<uint8_t> PackLayeredCell(const std::vector<LayeredCellChunkInput>& chunks) {
    const uint32_t layerCount = static_cast<uint32_t>(chunks.size());
    const size_t directoryBytes =
        sizeof(LayeredCellHeader) + static_cast<size_t>(layerCount) * sizeof(LayeredCellChunkEntry);

    std::vector<std::vector<uint8_t>> payloads(layerCount);
    std::vector<LayeredCellChunkEntry> entries(layerCount);

    uint64_t offset = directoryBytes;
    for (uint32_t i = 0; i < layerCount; ++i) {
        const LayeredCellChunkInput& in = chunks[i];
        const uint64_t rawSize = in.data.size();
        CellFileCompression usedCodec = CellFileCompression::None;
        std::vector<uint8_t>& payload = payloads[i];

        const Compression::Algorithm algo = ToAlgorithm(in.codec);
        bool compressed = false;
        if (algo != Compression::Algorithm::None && rawSize > 0 && Compression::IsAvailable(algo)) {
            const uint64_t bound = Compression::CompressBound(algo, rawSize);
            payload.resize(bound);
            const Compression::Result r = Compression::Compress(algo, in.data.data(), rawSize, payload.data(), bound);
            if (r.Succeeded() && r.bytesWritten < rawSize) {  // only keep if it actually shrank
                payload.resize(r.bytesWritten);
                usedCodec = in.codec;
                compressed = true;
            }
        }
        if (!compressed) {
            payload = in.data;  // store raw
            usedCodec = CellFileCompression::None;
        }

        LayeredCellChunkEntry& e = entries[i];
        e.layer = static_cast<uint32_t>(in.layer);
        e.codec = static_cast<uint32_t>(usedCodec);
        e.offset = offset;
        e.compressedSize = payload.size();
        e.decompressedSize = rawSize;
        offset += payload.size();
    }

    std::vector<uint8_t> blob;
    blob.reserve(static_cast<size_t>(offset));

    LayeredCellHeader header;
    header.layerCount = layerCount;
    AppendBytes(blob, &header, sizeof(header));
    for (const LayeredCellChunkEntry& e : entries) {
        AppendBytes(blob, &e, sizeof(e));
    }
    for (const std::vector<uint8_t>& p : payloads) {
        if (!p.empty()) {
            AppendBytes(blob, p.data(), p.size());
        }
    }
    return blob;
}

bool ParseLayeredCell(const uint8_t* data, size_t size, std::vector<LayeredCellChunk>& outChunks) {
    outChunks.clear();
    if (data == nullptr || size < sizeof(LayeredCellHeader)) {
        return false;
    }

    LayeredCellHeader header;
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != kLayeredCellMagic || header.version != kLayeredCellVersion ||
        header.headerSize != sizeof(LayeredCellHeader)) {
        return false;
    }

    const uint64_t directoryBytes = static_cast<uint64_t>(sizeof(LayeredCellHeader)) +
                                    static_cast<uint64_t>(header.layerCount) * sizeof(LayeredCellChunkEntry);
    if (directoryBytes > size) {
        return false;
    }

    outChunks.reserve(header.layerCount);
    for (uint32_t i = 0; i < header.layerCount; ++i) {
        LayeredCellChunkEntry e;
        std::memcpy(&e, data + sizeof(LayeredCellHeader) + static_cast<size_t>(i) * sizeof(LayeredCellChunkEntry),
                    sizeof(e));

        if (!IsSupportedCellFileCompression(e.codec)) {
            return false;
        }
        if (e.decompressedSize > kLayeredCellMaxChunkBytes) {
            return false;
        }
        if (e.offset < directoryBytes || e.offset > size) {
            return false;
        }
        if (e.compressedSize > static_cast<uint64_t>(size) - e.offset) {
            return false;
        }

        const CellFileCompression codec = static_cast<CellFileCompression>(e.codec);
        LayeredCellChunk chunk;
        chunk.layer = static_cast<CellLayer>(e.layer);
        chunk.codec = codec;

        const uint8_t* src = data + e.offset;
        if (codec == CellFileCompression::None) {
            if (e.compressedSize != e.decompressedSize) {
                return false;
            }
            chunk.data.assign(src, src + e.compressedSize);
        } else {
            const Compression::Algorithm algo = ToAlgorithm(codec);
            if (!Compression::IsAvailable(algo)) {
                return false;
            }
            chunk.data.resize(static_cast<size_t>(e.decompressedSize));
            const Compression::Result r =
                Compression::Decompress(algo, src, e.compressedSize, chunk.data.data(), e.decompressedSize);
            if (!r.Succeeded() || r.bytesWritten != e.decompressedSize) {
                return false;
            }
        }
        outChunks.push_back(std::move(chunk));
    }
    return true;
}

bool ExtractLayer(const uint8_t* data, size_t size, CellLayer layer, std::vector<uint8_t>& outBytes) {
    std::vector<LayeredCellChunk> chunks;
    if (!ParseLayeredCell(data, size, chunks)) {
        return false;
    }
    for (LayeredCellChunk& c : chunks) {
        if (c.layer == layer) {
            outBytes = std::move(c.data);
            return true;
        }
    }
    return false;
}

}  // namespace Streaming
}  // namespace Next
