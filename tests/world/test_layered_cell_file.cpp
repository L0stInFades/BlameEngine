#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "next/streaming/layered_cell_file.h"

using namespace Next::Streaming;

namespace {

std::vector<uint8_t> Bytes(std::initializer_list<int> vals) {
    std::vector<uint8_t> v;
    v.reserve(vals.size());
    for (int x : vals) {
        v.push_back(static_cast<uint8_t>(x));
    }
    return v;
}

// A payload that compresses well (long run), so the compressed path is actually exercised.
std::vector<uint8_t> Compressible(size_t n) {
    return std::vector<uint8_t>(n, 0xABu);
}

LayeredCellChunkInput MakeInput(CellLayer layer, CellFileCompression codec, std::vector<uint8_t> data) {
    LayeredCellChunkInput in;
    in.layer = layer;
    in.codec = codec;
    in.data = std::move(data);
    return in;
}

}  // namespace

TEST(LayeredCell, HeaderLayoutStable) {
    EXPECT_EQ(sizeof(LayeredCellHeader), 16u);
    EXPECT_EQ(sizeof(LayeredCellChunkEntry), 32u);
}

TEST(LayeredCell, RoundTripTwoLayersUncompressed) {
    std::vector<LayeredCellChunkInput> inputs;
    inputs.push_back(MakeInput(CellLayer::StaticMesh, CellFileCompression::None, Bytes({1, 2, 3, 4, 5})));
    inputs.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, Bytes({9, 8, 7})));

    const std::vector<uint8_t> blob = PackLayeredCell(inputs);

    std::vector<LayeredCellChunk> chunks;
    ASSERT_TRUE(ParseLayeredCell(blob.data(), blob.size(), chunks));
    ASSERT_EQ(chunks.size(), 2u);
    EXPECT_EQ(chunks[0].layer, CellLayer::StaticMesh);
    EXPECT_EQ(chunks[0].data, Bytes({1, 2, 3, 4, 5}));
    EXPECT_EQ(chunks[1].layer, CellLayer::Vegetation);
    EXPECT_EQ(chunks[1].data, Bytes({9, 8, 7}));
}

TEST(LayeredCell, ExtractLayerFindsAndMisses) {
    std::vector<LayeredCellChunkInput> inputs;
    inputs.push_back(MakeInput(CellLayer::StaticMesh, CellFileCompression::None, Bytes({1, 2})));
    inputs.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, Bytes({3, 4, 5, 6})));
    const std::vector<uint8_t> blob = PackLayeredCell(inputs);

    std::vector<uint8_t> veg;
    ASSERT_TRUE(ExtractLayer(blob.data(), blob.size(), CellLayer::Vegetation, veg));
    EXPECT_EQ(veg, Bytes({3, 4, 5, 6}));

    std::vector<uint8_t> missing;
    EXPECT_FALSE(ExtractLayer(blob.data(), blob.size(), CellLayer::Collision, missing));
}

TEST(LayeredCell, DeterministicGoldenBytes) {
    std::vector<LayeredCellChunkInput> a;
    a.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, Bytes({4, 4, 4, 4})));
    a.push_back(MakeInput(CellLayer::Collision, CellFileCompression::None, Bytes({1})));

    std::vector<LayeredCellChunkInput> b;
    b.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, Bytes({4, 4, 4, 4})));
    b.push_back(MakeInput(CellLayer::Collision, CellFileCompression::None, Bytes({1})));

    const std::vector<uint8_t> blobA = PackLayeredCell(a);
    const std::vector<uint8_t> blobB = PackLayeredCell(b);
    EXPECT_EQ(blobA, blobB);  // same inputs -> byte-identical (stable golden output)
}

TEST(LayeredCell, ChunkOrderPreserved) {
    std::vector<LayeredCellChunkInput> inputs;
    inputs.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, Bytes({1})));
    inputs.push_back(MakeInput(CellLayer::StaticMesh, CellFileCompression::None, Bytes({2, 2})));
    inputs.push_back(MakeInput(CellLayer::Collision, CellFileCompression::None, Bytes({3, 3, 3})));
    const std::vector<uint8_t> blob = PackLayeredCell(inputs);

    std::vector<LayeredCellChunk> chunks;
    ASSERT_TRUE(ParseLayeredCell(blob.data(), blob.size(), chunks));
    ASSERT_EQ(chunks.size(), 3u);
    EXPECT_EQ(chunks[0].layer, CellLayer::Vegetation);
    EXPECT_EQ(chunks[1].layer, CellLayer::StaticMesh);
    EXPECT_EQ(chunks[2].layer, CellLayer::Collision);
}

TEST(LayeredCell, EmptyChunkRoundTrips) {
    std::vector<LayeredCellChunkInput> inputs;
    inputs.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, {}));
    const std::vector<uint8_t> blob = PackLayeredCell(inputs);

    std::vector<LayeredCellChunk> chunks;
    ASSERT_TRUE(ParseLayeredCell(blob.data(), blob.size(), chunks));
    ASSERT_EQ(chunks.size(), 1u);
    EXPECT_TRUE(chunks[0].data.empty());
}

TEST(LayeredCell, RejectsCorruption) {
    std::vector<LayeredCellChunkInput> inputs;
    inputs.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, Bytes({1, 2, 3, 4})));
    const std::vector<uint8_t> blob = PackLayeredCell(inputs);

    std::vector<LayeredCellChunk> chunks;
    // Too small for a header.
    EXPECT_FALSE(ParseLayeredCell(blob.data(), 4, chunks));
    // Truncated mid-payload.
    EXPECT_FALSE(ParseLayeredCell(blob.data(), blob.size() - 1, chunks));
    // Bad magic.
    std::vector<uint8_t> badMagic = blob;
    badMagic[0] ^= 0xFFu;
    EXPECT_FALSE(ParseLayeredCell(badMagic.data(), badMagic.size(), chunks));
    // Corrupt the first chunk entry's offset (bytes 16..23 = entry.layer/codec, 24..31 = offset).
    std::vector<uint8_t> badOffset = blob;
    badOffset[24] = 0xFFu;  // push offset out of range
    badOffset[25] = 0xFFu;
    EXPECT_FALSE(ParseLayeredCell(badOffset.data(), badOffset.size(), chunks));
}

TEST(LayeredCell, CompressedRoundTripWhenAvailable) {
    // Only assert the codec path when a codec is actually built in (asan preset has LZ4+Zstd).
    for (CellFileCompression codec : {CellFileCompression::Zstd, CellFileCompression::LZ4}) {
        std::vector<LayeredCellChunkInput> inputs;
        inputs.push_back(MakeInput(CellLayer::Vegetation, codec, Compressible(4096)));
        const std::vector<uint8_t> blob = PackLayeredCell(inputs);

        std::vector<LayeredCellChunk> chunks;
        ASSERT_TRUE(ParseLayeredCell(blob.data(), blob.size(), chunks));
        ASSERT_EQ(chunks.size(), 1u);
        EXPECT_EQ(chunks[0].data, Compressible(4096));  // decompresses back to the original
    }
}

// ---- Stage 0 building blocks for async per-chunk streaming (ParseLayeredCellDirectory + DecodeChunk) ----

TEST(LayeredCell, DirectoryAndChunkDecodeMatchWholeFileParse) {
    std::vector<LayeredCellChunkInput> inputs;
    inputs.push_back(MakeInput(CellLayer::StaticMesh, CellFileCompression::None, Bytes({1, 2, 3, 4, 5})));
    inputs.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, Bytes({9, 8, 7, 6})));
    const std::vector<uint8_t> blob = PackLayeredCell(inputs);

    std::vector<LayeredCellChunkEntry> entries;
    ASSERT_TRUE(ParseLayeredCellDirectory(blob.data(), blob.size(), blob.size(), entries));
    ASSERT_EQ(entries.size(), 2u);
    for (const LayeredCellChunkEntry& e : entries) {
        std::vector<uint8_t> decoded;
        ASSERT_TRUE(DecodeLayeredCellChunk(e, blob.data() + e.offset, static_cast<size_t>(e.compressedSize), decoded));
        std::vector<uint8_t> viaExtract;
        ASSERT_TRUE(ExtractLayer(blob.data(), blob.size(), static_cast<CellLayer>(e.layer), viaExtract));
        EXPECT_EQ(decoded, viaExtract);  // building blocks reproduce the whole-file path
    }
}

TEST(LayeredCell, ChunkOnlyBufferDecodes) {
    // Simulate an async partial read: read header+directory, then read JUST the chunk into a fresh buffer.
    std::vector<LayeredCellChunkInput> inputs;
    inputs.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, Bytes({4, 4, 4, 4, 4, 4})));
    const std::vector<uint8_t> blob = PackLayeredCell(inputs);

    std::vector<LayeredCellChunkEntry> entries;
    ASSERT_TRUE(ParseLayeredCellDirectory(blob.data(), blob.size(), blob.size(), entries));
    ASSERT_EQ(entries.size(), 1u);
    const LayeredCellChunkEntry& e = entries[0];

    const std::vector<uint8_t> chunkOnly(blob.begin() + static_cast<long>(e.offset),
                                         blob.begin() + static_cast<long>(e.offset + e.compressedSize));
    std::vector<uint8_t> decoded;
    ASSERT_TRUE(DecodeLayeredCellChunk(e, chunkOnly.data(), chunkOnly.size(), decoded));
    EXPECT_EQ(decoded, Bytes({4, 4, 4, 4, 4, 4}));
    EXPECT_FALSE(DecodeLayeredCellChunk(e, chunkOnly.data(), chunkOnly.size() - 1, decoded));  // wrong size
}

TEST(LayeredCell, ChunkOnlyCompressedDecodes) {
    for (CellFileCompression codec : {CellFileCompression::Zstd, CellFileCompression::LZ4}) {
        std::vector<LayeredCellChunkInput> inputs;
        inputs.push_back(MakeInput(CellLayer::Vegetation, codec, Compressible(4096)));
        const std::vector<uint8_t> blob = PackLayeredCell(inputs);

        std::vector<LayeredCellChunkEntry> entries;
        ASSERT_TRUE(ParseLayeredCellDirectory(blob.data(), blob.size(), blob.size(), entries));
        ASSERT_EQ(entries.size(), 1u);
        const LayeredCellChunkEntry& e = entries[0];
        const std::vector<uint8_t> chunkOnly(blob.begin() + static_cast<long>(e.offset),
                                             blob.begin() + static_cast<long>(e.offset + e.compressedSize));
        std::vector<uint8_t> decoded;
        ASSERT_TRUE(DecodeLayeredCellChunk(e, chunkOnly.data(), chunkOnly.size(), decoded));
        EXPECT_EQ(decoded, Compressible(4096));  // raw if codec unavailable, decompressed if available
    }
}

TEST(LayeredCell, DirectoryParseRejectsCorruption) {
    std::vector<LayeredCellChunkInput> inputs;
    inputs.push_back(MakeInput(CellLayer::Vegetation, CellFileCompression::None, Bytes({1, 2, 3})));
    const std::vector<uint8_t> blob = PackLayeredCell(inputs);

    std::vector<LayeredCellChunkEntry> entries;
    EXPECT_FALSE(ParseLayeredCellDirectory(blob.data(), 4, blob.size(), entries));  // too small for a header
    std::vector<uint8_t> badMagic = blob;
    badMagic[0] ^= 0xFFu;
    EXPECT_FALSE(ParseLayeredCellDirectory(badMagic.data(), badMagic.size(), badMagic.size(), entries));
    EXPECT_FALSE(ParseLayeredCellDirectory(blob.data(), blob.size(), 16, entries));  // fileSize too small for dir
}
