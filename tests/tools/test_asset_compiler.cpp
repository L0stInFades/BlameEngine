#include "asset_compiler.h"
#include "next/foundation/logger.h"
#include "next/compression/compression.h"
#include "next/runtime/asset/asset_types.h"
#include "next/runtime/asset/package_container.h"
#include "next/streaming/cell_file_format.h"
#include "next/streaming/streaming_manager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace Next {
namespace testing {
namespace {

std::filesystem::path MakeTempAssetCompilerDir() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "next_asset_compiler_tests" /
        ("package_offsets_" + std::to_string(now));
    std::filesystem::create_directories(dir);
    return dir;
}

template<typename HeaderT>
HeaderT ReadHeader(const std::vector<uint8_t>& bytes) {
    HeaderT header{};
    std::memcpy(&header, bytes.data(), sizeof(HeaderT));
    return header;
}

void WriteBinaryFile(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(out.good());
    out << text;
    ASSERT_TRUE(out.good());
}

void WriteSparseBinaryFile(const std::filesystem::path& path,
                           const std::vector<uint8_t>& prefix,
                           uintmax_t logicalSize) {
    ASSERT_GE(logicalSize, prefix.size());
    WriteBinaryFile(path, prefix);

    std::error_code ec;
    std::filesystem::resize_file(path, logicalSize, ec);
    ASSERT_FALSE(ec) << ec.message();
    ASSERT_EQ(std::filesystem::file_size(path, ec), logicalSize);
    ASSERT_FALSE(ec) << ec.message();
}

std::vector<uint8_t> MakeMinimalMeshAssetBytes(const char* name) {
    MeshHeader mesh{};
    mesh.common.magic = AssetHeader::MAGIC;
    mesh.common.version = AssetHeader::CURRENT_VERSION;
    mesh.common.assetType = AssetType::Mesh;
    mesh.common.dataSize = 0;
    std::strncpy(mesh.common.name, name, sizeof(mesh.common.name) - 1);

    std::vector<uint8_t> bytes(sizeof(MeshHeader));
    std::memcpy(bytes.data(), &mesh, sizeof(mesh));
    return bytes;
}

std::vector<uint8_t> MakeTga24TopLeft(uint16_t width, uint16_t height, const std::vector<uint8_t>& bgrPixels) {
    std::vector<uint8_t> bytes(18 + bgrPixels.size(), 0);
    bytes[2] = 2; // uncompressed true-color
    bytes[12] = static_cast<uint8_t>(width & 0xffu);
    bytes[13] = static_cast<uint8_t>((width >> 8) & 0xffu);
    bytes[14] = static_cast<uint8_t>(height & 0xffu);
    bytes[15] = static_cast<uint8_t>((height >> 8) & 0xffu);
    bytes[16] = 24;
    bytes[17] = 0x20; // top-left origin
    std::memcpy(bytes.data() + 18, bgrPixels.data(), bgrPixels.size());
    return bytes;
}

std::vector<uint8_t> ReadBinaryFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    EXPECT_TRUE(in.good());
    if (!in.good()) {
        return {};
    }
    const std::streamsize size = in.tellg();
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.read(reinterpret_cast<char*>(bytes.data()), size);
    EXPECT_TRUE(in.good() || in.gcount() == size);
    return bytes;
}

template<typename HeaderT>
void ExpectAssetChecksumValid(const std::vector<uint8_t>& bytes) {
    ASSERT_GE(bytes.size(), sizeof(HeaderT));
    const HeaderT header = ReadHeader<HeaderT>(bytes);
    EXPECT_NE(header.common.checksum, 0u);
    ASSERT_EQ(bytes.size() - sizeof(HeaderT), header.common.dataSize);
    EXPECT_TRUE(ValidateAssetChecksum(header.common, bytes.data() + sizeof(HeaderT)));
}

bool FindPackageEntry(const std::vector<uint8_t>& packageBytes,
                      const PackageHeader& header,
                      const char* assetName,
                      AssetEntry& outEntry) {
    const size_t indexOffset = header.indexOffset;
    const size_t indexBytes = sizeof(AssetEntry) * static_cast<size_t>(header.assetCount);
    if (indexOffset > packageBytes.size() || indexBytes > packageBytes.size() - indexOffset) {
        return false;
    }

    for (uint32_t i = 0; i < header.assetCount; ++i) {
        AssetEntry entry{};
        std::memcpy(&entry,
                    packageBytes.data() + indexOffset + sizeof(AssetEntry) * static_cast<size_t>(i),
                    sizeof(AssetEntry));
        if (std::strncmp(entry.name, assetName, sizeof(entry.name)) == 0) {
            outEntry = entry;
            return true;
        }
    }

    return false;
}

bool RewritePackageEntry(std::vector<uint8_t>& packageBytes,
                         const PackageHeader& header,
                         const char* assetName,
                         const AssetEntry& replacement) {
    const size_t indexOffset = header.indexOffset;
    const size_t indexBytes = sizeof(AssetEntry) * static_cast<size_t>(header.assetCount);
    if (indexOffset > packageBytes.size() || indexBytes > packageBytes.size() - indexOffset) {
        return false;
    }

    for (uint32_t i = 0; i < header.assetCount; ++i) {
        const size_t entryOffset = indexOffset + sizeof(AssetEntry) * static_cast<size_t>(i);
        AssetEntry entry{};
        std::memcpy(&entry, packageBytes.data() + entryOffset, sizeof(AssetEntry));
        if (std::strncmp(entry.name, assetName, sizeof(entry.name)) == 0) {
            std::memcpy(packageBytes.data() + entryOffset, &replacement, sizeof(AssetEntry));
            return true;
        }
    }

    return false;
}

void ExpectCompiledCellPayload(
    const std::vector<uint8_t>& fileBytes,
    const std::vector<uint8_t>& payload,
    Streaming::CellFileCompression compression,
    Compression::Algorithm algorithm) {
    ASSERT_GE(fileBytes.size(), sizeof(Streaming::CellFileHeader));
    const Streaming::CellFileHeader header = ReadHeader<Streaming::CellFileHeader>(fileBytes);
    EXPECT_EQ(header.magic, Streaming::kCellFileMagic);
    EXPECT_EQ(header.version, Streaming::kCellFileVersion);
    EXPECT_EQ(header.headerSize, Streaming::kCellFileHeaderSize);
    EXPECT_EQ(header.compressionType, static_cast<uint32_t>(compression));
    EXPECT_EQ(header.decompressedSize, payload.size());
    ASSERT_EQ(header.headerSize + header.compressedSize, fileBytes.size());

    const uint8_t* body = fileBytes.data() + header.headerSize;
    if (compression == Streaming::CellFileCompression::None) {
        EXPECT_EQ(header.compressedSize, payload.size());
        ASSERT_EQ(header.compressedSize, payload.size());
        EXPECT_EQ(std::memcmp(body, payload.data(), payload.size()), 0);
        return;
    }

    ASSERT_TRUE(Compression::IsAvailable(algorithm));
    std::vector<uint8_t> decompressed(payload.size());
    const Compression::Result result = Compression::Decompress(
        algorithm,
        body,
        header.compressedSize,
        decompressed.data(),
        decompressed.size());
    ASSERT_TRUE(result.Succeeded()) << result.message;
    EXPECT_EQ(result.bytesWritten, payload.size());
    EXPECT_EQ(std::memcmp(decompressed.data(), payload.data(), payload.size()), 0);
}

void ExpectStreamingManagerLoadsCellPayload(
    const std::filesystem::path& cellDirectory,
    const std::vector<uint8_t>& payload) {
    using namespace Streaming;
    using Next::Vec3;

    const CellCoord coord{0, 0};
    const std::filesystem::path cellPath = cellDirectory / "cell_0_0.ncell";
    ASSERT_TRUE(std::filesystem::exists(cellPath));
    const uint64_t diskBytes = static_cast<uint64_t>(std::filesystem::file_size(cellPath));

    StreamingManager mgr;
    StreamingManagerConfig cfg;
    cfg.memoryBudgetMB = 64;
    cfg.loadRadius = 0.0f;
    cfg.unloadRadius = 128.0f;
    cfg.maxConcurrentLoads = 4;
    cfg.maxConcurrentUnloads = 4;
    cfg.enablePrediction = false;
    cfg.cellDataDirectory = cellDirectory.wstring();
    cfg.allowPlaceholderCellLoad = false;

    ASSERT_TRUE(mgr.Initialize(cfg));
    mgr.LoadCell(coord, 1.0f);

    for (int i = 0; i < 200 && !mgr.IsCellLoaded(coord); ++i) {
        mgr.Update(0.016f, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, -1.0f), Vec3(0.0f, 0.0f, 0.0f));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ASSERT_TRUE(mgr.IsCellLoaded(coord));

    StreamingCellInfo info;
    ASSERT_TRUE(mgr.GetCellInfo(coord, info));
    EXPECT_TRUE(info.IsLoaded());
    EXPECT_EQ(info.dataSize, diskBytes);
    EXPECT_EQ(info.memorySize, payload.size());

    CellData* cell = mgr.GetCell(coord);
    ASSERT_NE(cell, nullptr);
    auto layerIt = cell->layers.find(CellLayer::StaticMesh);
    ASSERT_TRUE(layerIt != cell->layers.end());
    ASSERT_TRUE(layerIt->second.HasData());
    ASSERT_EQ(layerIt->second.size, payload.size());
    EXPECT_EQ(std::memcmp(layerIt->second.data, payload.data(), payload.size()), 0);

    const IOStatistics ioStats = mgr.GetIOStatistics();
    EXPECT_TRUE(ioStats.HasReadBytes());
    EXPECT_TRUE(ioStats.HasDecompressedBytes());
    EXPECT_FALSE(ioStats.HasFailures());

    mgr.Shutdown();
}

} // namespace

class AssetCompilerTest : public ::testing::Test {
protected:
    void SetUp() override {
        Logger::Initialize();
        tempDir_ = MakeTempAssetCompilerDir();
    }

    void TearDown() override {
        if (!tempDir_.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(tempDir_, ec);
        }
        Logger::Shutdown();
    }

    std::filesystem::path tempDir_;
};

TEST(AssetChecksumTest, CRC64UsesEcmaKnownVector) {
    const char* payload = "123456789";
    EXPECT_EQ(CalculateCRC64(payload, std::strlen(payload)), 0x6C40DF5F0B497347ULL);
}

TEST_F(AssetCompilerTest, RejectsUnterminatedAssetNamesWithoutUnsafeLogging) {
    AssetHeader header{};
    header.magic = AssetHeader::MAGIC;
    header.version = AssetHeader::CURRENT_VERSION;
    header.assetType = AssetType::Texture;
    header.dataSize = 1;
    header.checksum = 1;
    std::memset(header.name, 'A', sizeof(header.name));

    EXPECT_FALSE(header.HasValidName());
    EXPECT_FALSE(header.Validate());

    const uint8_t payload = 0;
    EXPECT_FALSE(ValidateAssetChecksum(header, &payload));

    TextureHeader texture{};
    texture.common = header;
    texture.width = 4;
    texture.height = 4;
    texture.depth = 1;
    texture.mipLevels = 1;
    texture.arraySize = 1;
    texture.format = 28;
    EXPECT_FALSE(ValidateTextureHeader(texture));
}

TEST_F(AssetCompilerTest, CompileTextureImportsUncompressedTga24) {
    AssetCompiler compiler;

    const std::filesystem::path input = tempDir_ / "tiny.tga";
    const std::filesystem::path output = tempDir_ / "tiny.texture";
    const std::vector<uint8_t> bgrPixels = {
        0, 0, 255,
        0, 255, 0,
    };
    WriteBinaryFile(input, MakeTga24TopLeft(2, 1, bgrPixels));

    ASSERT_TRUE(compiler.CompileTexture(input.string(), output.string()));

    const std::vector<uint8_t> textureBytes = ReadBinaryFile(output);
    ASSERT_EQ(textureBytes.size(), sizeof(TextureHeader) + 8u);
    const TextureHeader texture = ReadHeader<TextureHeader>(textureBytes);
    EXPECT_EQ(texture.common.assetType, AssetType::Texture);
    EXPECT_STREQ(texture.common.name, "tiny");
    EXPECT_TRUE(ValidateTextureHeader(texture));
    EXPECT_EQ(texture.width, 2u);
    EXPECT_EQ(texture.height, 1u);
    EXPECT_EQ(texture.common.dataSize, 8u);
    ExpectAssetChecksumValid<TextureHeader>(textureBytes);

    const uint8_t expectedPixels[] = {
        255, 0, 0, 255,
        0, 255, 0, 255,
    };
    EXPECT_EQ(std::memcmp(textureBytes.data() + sizeof(TextureHeader), expectedPixels, sizeof(expectedPixels)), 0);
}

TEST_F(AssetCompilerTest, CompileTextureRejectsTruncatedTga) {
    AssetCompiler compiler;

    const std::filesystem::path input = tempDir_ / "truncated.tga";
    const std::filesystem::path output = tempDir_ / "truncated.texture";
    const std::vector<uint8_t> onePixelOnly = {0, 0, 255};
    WriteBinaryFile(input, MakeTga24TopLeft(2, 1, onePixelOnly));

    EXPECT_FALSE(compiler.CompileTexture(input.string(), output.string()));
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST_F(AssetCompilerTest, CompileMeshImportsObjTriangleWithAttributes) {
    AssetCompiler compiler;

    const std::filesystem::path input = tempDir_ / "triangle.obj";
    const std::filesystem::path output = tempDir_ / "triangle.mesh";
    WriteTextFile(input,
                  "v 0 0 0\n"
                  "v 1 0 0\n"
                  "v 0 1 0\n"
                  "vt 0 0\n"
                  "vt 1 0\n"
                  "vt 0 1\n"
                  "vn 0 0 1\n"
                  "f 1/1/1 2/2/1 3/3/1\n");

    ASSERT_TRUE(compiler.CompileMesh(input.string(), output.string()));

    const std::vector<uint8_t> meshBytes = ReadBinaryFile(output);
    ASSERT_GE(meshBytes.size(), sizeof(MeshHeader));
    const MeshHeader mesh = ReadHeader<MeshHeader>(meshBytes);
    EXPECT_EQ(mesh.common.assetType, AssetType::Mesh);
    EXPECT_STREQ(mesh.common.name, "triangle");
    EXPECT_TRUE(ValidateMeshHeader(mesh));
    EXPECT_EQ(mesh.vertexCount, 3u);
    EXPECT_EQ(mesh.indexCount, 3u);
    EXPECT_EQ(mesh.vertexStride, 8u * sizeof(float));
    EXPECT_EQ(mesh.indexType, 0u);
    EXPECT_EQ(mesh.materialCount, 1u);
    EXPECT_EQ(mesh.flags & MeshHeader::HAS_NORMALS, MeshHeader::HAS_NORMALS);
    EXPECT_EQ(mesh.flags & MeshHeader::HAS_UVS, MeshHeader::HAS_UVS);
    EXPECT_FLOAT_EQ(mesh.boundingBox[0], 0.0f);
    EXPECT_FLOAT_EQ(mesh.boundingBox[1], 0.0f);
    EXPECT_FLOAT_EQ(mesh.boundingBox[2], 0.0f);
    EXPECT_FLOAT_EQ(mesh.boundingBox[3], 1.0f);
    EXPECT_FLOAT_EQ(mesh.boundingBox[4], 1.0f);
    EXPECT_FLOAT_EQ(mesh.boundingBox[5], 0.0f);
    ExpectAssetChecksumValid<MeshHeader>(meshBytes);
}

TEST_F(AssetCompilerTest, CompileMeshRejectsMalformedObjVertexPosition) {
    AssetCompiler compiler;

    const std::filesystem::path input = tempDir_ / "bad_position.obj";
    const std::filesystem::path output = tempDir_ / "bad_position.mesh";
    WriteTextFile(input,
                  "v nope 0 0\n"
                  "v 1 0 0\n"
                  "v 0 1 0\n"
                  "f 1 2 3\n");

    EXPECT_FALSE(compiler.CompileMesh(input.string(), output.string()));
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST_F(AssetCompilerTest, CompileMeshRejectsMalformedObjFaceReference) {
    AssetCompiler compiler;

    const std::filesystem::path input = tempDir_ / "bad_face.obj";
    const std::filesystem::path output = tempDir_ / "bad_face.mesh";
    WriteTextFile(input,
                  "v 0 0 0\n"
                  "v 1 0 0\n"
                  "v 0 1 0\n"
                  "vt 0 0\n"
                  "vn 0 0 1\n"
                  "f 1/2/1 2/1/1 3/1/1\n");

    EXPECT_FALSE(compiler.CompileMesh(input.string(), output.string()));
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST_F(AssetCompilerTest, CompileMeshUsesUint32IndicesForLargeObj) {
    AssetCompiler compiler;

    constexpr uint32_t kVertexCount = static_cast<uint32_t>(std::numeric_limits<uint16_t>::max()) + 3u;
    std::ostringstream obj;
    for (uint32_t i = 0; i < kVertexCount; ++i) {
        obj << "v " << (i % 257u) << ' ' << ((i / 257u) % 257u) << ' ' << (i / (257u * 257u)) << '\n';
    }
    for (uint32_t i = 1; i + 2u <= kVertexCount; i += 3u) {
        obj << "f " << i << ' ' << (i + 1u) << ' ' << (i + 2u) << '\n';
    }

    const std::filesystem::path input = tempDir_ / "large.obj";
    const std::filesystem::path output = tempDir_ / "large.mesh";
    WriteTextFile(input, obj.str());

    ASSERT_TRUE(compiler.CompileMesh(input.string(), output.string()));

    const std::vector<uint8_t> meshBytes = ReadBinaryFile(output);
    ASSERT_GE(meshBytes.size(), sizeof(MeshHeader));
    const MeshHeader mesh = ReadHeader<MeshHeader>(meshBytes);
    EXPECT_TRUE(ValidateMeshHeader(mesh));
    EXPECT_EQ(mesh.vertexCount, kVertexCount);
    EXPECT_EQ(mesh.indexCount, kVertexCount);
    EXPECT_EQ(mesh.indexType, 1u);
    EXPECT_EQ(mesh.flags & MeshHeader::HAS_NORMALS, MeshHeader::HAS_NORMALS);
    EXPECT_EQ(mesh.flags & MeshHeader::HAS_UVS, 0u);
    EXPECT_EQ(mesh.common.dataSize,
              kVertexCount * static_cast<uint32_t>(8u * sizeof(float)) +
                  kVertexCount * static_cast<uint32_t>(sizeof(uint32_t)) +
                  static_cast<uint32_t>(sizeof(uint32_t) * 2u));
    ExpectAssetChecksumValid<MeshHeader>(meshBytes);
}

TEST_F(AssetCompilerTest, GeneratedPackageUsesCorrectAssetOffsets) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    const std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GE(packageBytes.size(), sizeof(PackageHeader));
    const PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    EXPECT_NE(packageHeader.checksum, 0u);
    EXPECT_EQ(packageHeader.checksum,
              CalculateCRC64(packageBytes.data() + sizeof(PackageHeader),
                             packageBytes.size() - sizeof(PackageHeader)));

    std::shared_ptr<PackageContainer> package = PackageContainer::LoadFromFile(packagePath.string());
    ASSERT_NE(package, nullptr);
    ASSERT_TRUE(package->Validate());

    const AssetEntry* cubeEntry = package->GetAssetEntry("TestCube");
    const AssetEntry* checkerEntry = package->GetAssetEntry("TestChecker");
    const AssetEntry* normalEntry = package->GetAssetEntry("DefaultNormal");
    const AssetEntry* metallicRoughnessEntry = package->GetAssetEntry("DefaultMetallicRoughness");
    const AssetEntry* emissiveEntry = package->GetAssetEntry("DefaultEmissive");
    const AssetEntry* occlusionEntry = package->GetAssetEntry("DefaultOcclusion");
    const AssetEntry* materialEntry = package->GetAssetEntry("TestPBR");
    ASSERT_NE(cubeEntry, nullptr);
    ASSERT_NE(checkerEntry, nullptr);
    ASSERT_NE(normalEntry, nullptr);
    ASSERT_NE(metallicRoughnessEntry, nullptr);
    ASSERT_NE(emissiveEntry, nullptr);
    ASSERT_NE(occlusionEntry, nullptr);
    ASSERT_NE(materialEntry, nullptr);

    EXPECT_EQ(cubeEntry->dataOffset, 0u);
    EXPECT_EQ(checkerEntry->dataOffset, cubeEntry->assetSize);
    EXPECT_EQ(normalEntry->dataOffset, cubeEntry->assetSize + checkerEntry->assetSize);
    EXPECT_EQ(metallicRoughnessEntry->dataOffset,
              cubeEntry->assetSize + checkerEntry->assetSize + normalEntry->assetSize);
    EXPECT_EQ(emissiveEntry->dataOffset,
              cubeEntry->assetSize + checkerEntry->assetSize + normalEntry->assetSize +
                  metallicRoughnessEntry->assetSize);
    EXPECT_EQ(occlusionEntry->dataOffset,
              cubeEntry->assetSize + checkerEntry->assetSize + normalEntry->assetSize +
                  metallicRoughnessEntry->assetSize + emissiveEntry->assetSize);
    EXPECT_EQ(materialEntry->dataOffset,
              cubeEntry->assetSize + checkerEntry->assetSize + normalEntry->assetSize +
                  metallicRoughnessEntry->assetSize + emissiveEntry->assetSize + occlusionEntry->assetSize);

    std::vector<uint8_t> cubeBytes;
    ASSERT_TRUE(package->ReadAssetData("TestCube", cubeBytes));
    ExpectAssetChecksumValid<MeshHeader>(cubeBytes);

    std::vector<uint8_t> checkerBytes;
    ASSERT_TRUE(package->ReadAssetData("TestChecker", checkerBytes));
    ExpectAssetChecksumValid<TextureHeader>(checkerBytes);
    ASSERT_GE(checkerBytes.size(), sizeof(TextureHeader));
    TextureHeader texture = ReadHeader<TextureHeader>(checkerBytes);
    EXPECT_EQ(texture.common.assetType, AssetType::Texture);
    EXPECT_STREQ(texture.common.name, "TestChecker");
    EXPECT_TRUE(ValidateTextureHeader(texture));
    EXPECT_EQ(texture.width, 4u);
    EXPECT_EQ(texture.height, 4u);
    EXPECT_EQ(checkerBytes.size() - sizeof(TextureHeader), 64u);

    std::vector<uint8_t> normalBytes;
    ASSERT_TRUE(package->ReadAssetData("DefaultNormal", normalBytes));
    ExpectAssetChecksumValid<TextureHeader>(normalBytes);
    ASSERT_GE(normalBytes.size(), sizeof(TextureHeader));
    TextureHeader normal = ReadHeader<TextureHeader>(normalBytes);
    EXPECT_EQ(normal.common.assetType, AssetType::Texture);
    EXPECT_STREQ(normal.common.name, "DefaultNormal");
    EXPECT_TRUE(ValidateTextureHeader(normal));
    EXPECT_EQ(normal.width, 4u);
    EXPECT_EQ(normal.height, 4u);
    EXPECT_EQ(normal.flags, 0u);
    EXPECT_EQ(normalBytes.size() - sizeof(TextureHeader), 64u);

    std::vector<uint8_t> metallicRoughnessBytes;
    ASSERT_TRUE(package->ReadAssetData("DefaultMetallicRoughness", metallicRoughnessBytes));
    ExpectAssetChecksumValid<TextureHeader>(metallicRoughnessBytes);
    ASSERT_GE(metallicRoughnessBytes.size(), sizeof(TextureHeader));
    TextureHeader metallicRoughness = ReadHeader<TextureHeader>(metallicRoughnessBytes);
    EXPECT_EQ(metallicRoughness.common.assetType, AssetType::Texture);
    EXPECT_STREQ(metallicRoughness.common.name, "DefaultMetallicRoughness");
    EXPECT_TRUE(ValidateTextureHeader(metallicRoughness));
    EXPECT_EQ(metallicRoughness.width, 4u);
    EXPECT_EQ(metallicRoughness.height, 4u);
    EXPECT_EQ(metallicRoughness.flags, 0u);
    EXPECT_EQ(metallicRoughnessBytes.size() - sizeof(TextureHeader), 64u);

    std::vector<uint8_t> emissiveBytes;
    ASSERT_TRUE(package->ReadAssetData("DefaultEmissive", emissiveBytes));
    ExpectAssetChecksumValid<TextureHeader>(emissiveBytes);
    ASSERT_GE(emissiveBytes.size(), sizeof(TextureHeader));
    TextureHeader emissive = ReadHeader<TextureHeader>(emissiveBytes);
    EXPECT_EQ(emissive.common.assetType, AssetType::Texture);
    EXPECT_STREQ(emissive.common.name, "DefaultEmissive");
    EXPECT_TRUE(ValidateTextureHeader(emissive));
    EXPECT_EQ(emissive.width, 4u);
    EXPECT_EQ(emissive.height, 4u);
    EXPECT_EQ(emissive.flags, 0u);
    EXPECT_EQ(emissiveBytes.size() - sizeof(TextureHeader), 64u);

    std::vector<uint8_t> occlusionBytes;
    ASSERT_TRUE(package->ReadAssetData("DefaultOcclusion", occlusionBytes));
    ExpectAssetChecksumValid<TextureHeader>(occlusionBytes);
    ASSERT_GE(occlusionBytes.size(), sizeof(TextureHeader));
    TextureHeader occlusion = ReadHeader<TextureHeader>(occlusionBytes);
    EXPECT_EQ(occlusion.common.assetType, AssetType::Texture);
    EXPECT_STREQ(occlusion.common.name, "DefaultOcclusion");
    EXPECT_TRUE(ValidateTextureHeader(occlusion));
    EXPECT_EQ(occlusion.width, 4u);
    EXPECT_EQ(occlusion.height, 4u);
    EXPECT_EQ(occlusion.flags, 0u);
    EXPECT_EQ(occlusionBytes.size() - sizeof(TextureHeader), 64u);

    std::vector<uint8_t> materialBytes;
    ASSERT_TRUE(package->ReadAssetData("TestPBR", materialBytes));
    ExpectAssetChecksumValid<MaterialHeader>(materialBytes);
    ASSERT_GE(materialBytes.size(), sizeof(MaterialHeader));
    MaterialHeader material = ReadHeader<MaterialHeader>(materialBytes);
    EXPECT_EQ(material.common.assetType, AssetType::Material);
    EXPECT_STREQ(material.common.name, "TestPBR");
    EXPECT_TRUE(ValidateMaterialHeader(material));
    EXPECT_EQ(material.textureCount, 5u);
    ASSERT_GE(materialBytes.size(), sizeof(MaterialHeader) + material.textureCount * sizeof(TextureRef));
    const auto* refs = reinterpret_cast<const TextureRef*>(materialBytes.data() + sizeof(MaterialHeader));
    EXPECT_STREQ(refs[0].name, "TestChecker");
    EXPECT_EQ(refs[0].type, static_cast<uint32_t>(TextureRef::ALBEDO));
    EXPECT_TRUE(package->HasAsset(refs[0].name));
    EXPECT_STREQ(refs[1].name, "DefaultNormal");
    EXPECT_EQ(refs[1].type, static_cast<uint32_t>(TextureRef::NORMAL));
    EXPECT_TRUE(package->HasAsset(refs[1].name));
    EXPECT_STREQ(refs[2].name, "DefaultMetallicRoughness");
    EXPECT_EQ(refs[2].type, static_cast<uint32_t>(TextureRef::METALLIC_ROUGHNESS));
    EXPECT_TRUE(package->HasAsset(refs[2].name));
    EXPECT_STREQ(refs[3].name, "DefaultEmissive");
    EXPECT_EQ(refs[3].type, static_cast<uint32_t>(TextureRef::EMISSIVE));
    EXPECT_TRUE(package->HasAsset(refs[3].name));
    EXPECT_STREQ(refs[4].name, "DefaultOcclusion");
    EXPECT_EQ(refs[4].type, static_cast<uint32_t>(TextureRef::OCCLUSION));
    EXPECT_TRUE(package->HasAsset(refs[4].name));
}

TEST_F(AssetCompilerTest, PackageChecksumRejectsCorruptedPayload) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));
    packageBytes.back() ^= 0x5au;
    WriteBinaryFile(packagePath, packageBytes);

    EXPECT_EQ(PackageContainer::LoadFromFile(packagePath.string()), nullptr);
}

TEST_F(AssetCompilerTest, AssetChecksumRejectsCorruptedAssetPayload) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    packageHeader.checksum = 0;
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));

    AssetEntry checkerEntry{};
    ASSERT_TRUE(FindPackageEntry(packageBytes, packageHeader, "TestChecker", checkerEntry));
    const size_t assetOffset = static_cast<size_t>(packageHeader.dataOffset) + checkerEntry.dataOffset;
    ASSERT_GE(checkerEntry.assetSize, sizeof(TextureHeader) + 1);
    ASSERT_LT(assetOffset + sizeof(TextureHeader), packageBytes.size());

    packageBytes[assetOffset + sizeof(TextureHeader)] ^= 0x5au;
    WriteBinaryFile(packagePath, packageBytes);

    std::shared_ptr<PackageContainer> package = PackageContainer::LoadFromFile(packagePath.string());
    ASSERT_NE(package, nullptr);

    std::vector<uint8_t> checkerBytes;
    EXPECT_FALSE(package->ReadAssetData("TestChecker", checkerBytes));
}

TEST_F(AssetCompilerTest, PackageReadRejectsInvalidAssetHeaderWithoutChecksum) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    packageHeader.checksum = 0;
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));

    AssetEntry checkerEntry{};
    ASSERT_TRUE(FindPackageEntry(packageBytes, packageHeader, "TestChecker", checkerEntry));
    const size_t assetOffset = static_cast<size_t>(packageHeader.dataOffset) + checkerEntry.dataOffset;
    ASSERT_LE(assetOffset, packageBytes.size());
    ASSERT_LE(sizeof(AssetHeader), packageBytes.size() - assetOffset);

    AssetHeader assetHeader{};
    std::memcpy(&assetHeader, packageBytes.data() + assetOffset, sizeof(assetHeader));
    assetHeader.magic = 0;
    assetHeader.checksum = 0;
    std::memcpy(packageBytes.data() + assetOffset, &assetHeader, sizeof(assetHeader));
    WriteBinaryFile(packagePath, packageBytes);

    std::shared_ptr<PackageContainer> package = PackageContainer::LoadFromFile(packagePath.string());
    ASSERT_NE(package, nullptr);

    std::vector<uint8_t> checkerBytes;
    EXPECT_FALSE(package->ReadAssetData("TestChecker", checkerBytes));
}

TEST_F(AssetCompilerTest, PackageReadRejectsAssetTypeMismatchWithIndex) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    packageHeader.checksum = 0;
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));

    AssetEntry checkerEntry{};
    ASSERT_TRUE(FindPackageEntry(packageBytes, packageHeader, "TestChecker", checkerEntry));
    checkerEntry.assetType = AssetType::Mesh;
    ASSERT_TRUE(RewritePackageEntry(packageBytes, packageHeader, "TestChecker", checkerEntry));
    WriteBinaryFile(packagePath, packageBytes);

    std::shared_ptr<PackageContainer> package = PackageContainer::LoadFromFile(packagePath.string());
    ASSERT_NE(package, nullptr);

    std::vector<uint8_t> checkerBytes;
    EXPECT_FALSE(package->ReadAssetData("TestChecker", checkerBytes));
}

TEST_F(AssetCompilerTest, PackageReadRejectsAssetNameMismatchWithIndex) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    packageHeader.checksum = 0;
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));

    AssetEntry checkerEntry{};
    ASSERT_TRUE(FindPackageEntry(packageBytes, packageHeader, "TestChecker", checkerEntry));
    const size_t assetOffset = static_cast<size_t>(packageHeader.dataOffset) + checkerEntry.dataOffset;
    ASSERT_LE(assetOffset, packageBytes.size());
    ASSERT_LE(sizeof(AssetHeader), packageBytes.size() - assetOffset);

    AssetHeader assetHeader{};
    std::memcpy(&assetHeader, packageBytes.data() + assetOffset, sizeof(assetHeader));
    const char mismatchName[] = "MismatchedChecker";
    std::memset(assetHeader.name, 0, sizeof(assetHeader.name));
    std::memcpy(assetHeader.name, mismatchName, sizeof(mismatchName));
    std::memcpy(packageBytes.data() + assetOffset, &assetHeader, sizeof(assetHeader));
    WriteBinaryFile(packagePath, packageBytes);

    std::shared_ptr<PackageContainer> package = PackageContainer::LoadFromFile(packagePath.string());
    ASSERT_NE(package, nullptr);

    std::vector<uint8_t> checkerBytes;
    EXPECT_FALSE(package->ReadAssetData("TestChecker", checkerBytes));
}

TEST_F(AssetCompilerTest, PackageLoaderRejectsDataOffsetOutsideFile) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    packageHeader.dataOffset = static_cast<uint32_t>(packageBytes.size() + 1);
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));
    WriteBinaryFile(packagePath, packageBytes);

    EXPECT_EQ(PackageContainer::LoadFromFile(packagePath.string()), nullptr);
}

TEST_F(AssetCompilerTest, PackageLoaderRejectsTruncatedDataSection) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    packageHeader.checksum = 0;
    ASSERT_GT(packageBytes.size(), static_cast<size_t>(packageHeader.dataOffset));
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));
    packageBytes.pop_back();
    WriteBinaryFile(packagePath, packageBytes);

    EXPECT_EQ(PackageContainer::LoadFromFile(packagePath.string()), nullptr);
}

TEST_F(AssetCompilerTest, PackageLoaderRejectsUnsupportedCompressedEntry) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    packageHeader.checksum = 0;
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));

    AssetEntry checkerEntry{};
    ASSERT_TRUE(FindPackageEntry(packageBytes, packageHeader, "TestChecker", checkerEntry));
    checkerEntry.compressedSize = checkerEntry.assetSize - 1;
    ASSERT_TRUE(RewritePackageEntry(packageBytes, packageHeader, "TestChecker", checkerEntry));
    WriteBinaryFile(packagePath, packageBytes);

    EXPECT_EQ(PackageContainer::LoadFromFile(packagePath.string()), nullptr);
}

TEST_F(AssetCompilerTest, PackageLoaderRejectsMismatchedDecompressedSize) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    packageHeader.checksum = 0;
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));

    AssetEntry checkerEntry{};
    ASSERT_TRUE(FindPackageEntry(packageBytes, packageHeader, "TestChecker", checkerEntry));
    checkerEntry.decompressedSize = checkerEntry.assetSize + 1;
    ASSERT_TRUE(RewritePackageEntry(packageBytes, packageHeader, "TestChecker", checkerEntry));
    WriteBinaryFile(packagePath, packageBytes);

    EXPECT_EQ(PackageContainer::LoadFromFile(packagePath.string()), nullptr);
}

TEST_F(AssetCompilerTest, PackageLoaderRejectsUnterminatedPackageName) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    std::memset(packageHeader.name, 'P', sizeof(packageHeader.name));
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));
    WriteBinaryFile(packagePath, packageBytes);

    EXPECT_EQ(PackageContainer::LoadFromFile(packagePath.string()), nullptr);
}

TEST_F(AssetCompilerTest, PackageLoaderRejectsEmptyPackageName) {
    AssetCompiler compiler;
    ASSERT_TRUE(compiler.GenerateTestAssets(tempDir_.string()));

    const std::filesystem::path packagePath = tempDir_ / "test_package.npkg";
    std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GT(packageBytes.size(), sizeof(PackageHeader));

    PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    std::memset(packageHeader.name, 0, sizeof(packageHeader.name));
    std::memcpy(packageBytes.data(), &packageHeader, sizeof(packageHeader));
    WriteBinaryFile(packagePath, packageBytes);

    EXPECT_EQ(PackageContainer::LoadFromFile(packagePath.string()), nullptr);
}

TEST_F(AssetCompilerTest, CreatePackageRejectsDuplicateAssetNames) {
    AssetCompiler compiler;

    const std::vector<uint8_t> meshBytes = MakeMinimalMeshAssetBytes("DuplicateMesh");
    const std::filesystem::path firstAssetPath = tempDir_ / "duplicate_a.mesh";
    const std::filesystem::path secondAssetPath = tempDir_ / "duplicate_b.mesh";
    const std::filesystem::path packagePath = tempDir_ / "duplicate_package.npkg";
    WriteBinaryFile(firstAssetPath, meshBytes);
    WriteBinaryFile(secondAssetPath, meshBytes);

    const std::vector<std::string> assetFiles{firstAssetPath.string(), secondAssetPath.string()};
    EXPECT_FALSE(compiler.CreatePackage("duplicate_package", assetFiles, packagePath.string()));
    EXPECT_FALSE(std::filesystem::exists(packagePath));
}

TEST_F(AssetCompilerTest, CreatePackageRejectsEmptyAssetFile) {
    AssetCompiler compiler;

    const std::filesystem::path assetPath = tempDir_ / "empty.mesh";
    const std::filesystem::path packagePath = tempDir_ / "empty_package.npkg";
    WriteBinaryFile(assetPath, {});

    const std::vector<std::string> assetFiles{assetPath.string()};
    EXPECT_FALSE(compiler.CreatePackage("empty_package", assetFiles, packagePath.string()));
    EXPECT_FALSE(std::filesystem::exists(packagePath));
}

TEST_F(AssetCompilerTest, CreatePackageRejectsAssetFileTooLargeForPackageFields) {
    AssetCompiler compiler;

    const std::vector<uint8_t> meshBytes = MakeMinimalMeshAssetBytes("HugeMesh");
    const std::filesystem::path assetPath = tempDir_ / "huge.mesh";
    const std::filesystem::path packagePath = tempDir_ / "huge_package.npkg";
    const uintmax_t tooLargeForEntry =
        static_cast<uintmax_t>(std::numeric_limits<uint32_t>::max()) + 1u;
    WriteSparseBinaryFile(assetPath, meshBytes, tooLargeForEntry);

    const std::vector<std::string> assetFiles{assetPath.string()};
    EXPECT_FALSE(compiler.CreatePackage("huge_package", assetFiles, packagePath.string()));
    EXPECT_FALSE(std::filesystem::exists(packagePath));
}

TEST_F(AssetCompilerTest, CreatePackageRejectsDataSectionOffsetOverflowBeforeReadingAssets) {
    AssetCompiler compiler;

    const std::vector<uint8_t> meshBytes = MakeMinimalMeshAssetBytes("LargeMesh");
    const std::filesystem::path firstAssetPath = tempDir_ / "large_a.mesh";
    const std::filesystem::path secondAssetPath = tempDir_ / "large_b.mesh";
    const std::filesystem::path packagePath = tempDir_ / "large_package.npkg";
    const uintmax_t halfPlusOne =
        (static_cast<uintmax_t>(std::numeric_limits<uint32_t>::max()) / 2u) + 1u;
    WriteSparseBinaryFile(firstAssetPath, meshBytes, halfPlusOne);
    WriteSparseBinaryFile(secondAssetPath, meshBytes, halfPlusOne);

    const std::vector<std::string> assetFiles{firstAssetPath.string(), secondAssetPath.string()};
    EXPECT_FALSE(compiler.CreatePackage("large_package", assetFiles, packagePath.string()));
    EXPECT_FALSE(std::filesystem::exists(packagePath));
}

TEST_F(AssetCompilerTest, CreatePackageTruncatesFixedNamesWithNullTerminators) {
    AssetCompiler compiler;

    MeshHeader mesh{};
    mesh.common.magic = AssetHeader::MAGIC;
    mesh.common.version = AssetHeader::CURRENT_VERSION;
    mesh.common.assetType = AssetType::Mesh;
    mesh.common.dataSize = 0;
    std::memset(mesh.common.name, 'A', sizeof(mesh.common.name));
    mesh.common.name[sizeof(mesh.common.name) - 1] = '\0';

    std::vector<uint8_t> meshBytes(sizeof(MeshHeader));
    std::memcpy(meshBytes.data(), &mesh, sizeof(mesh));

    const std::filesystem::path assetPath = tempDir_ / "long_name.mesh";
    const std::filesystem::path packagePath = tempDir_ / "long_name_package.npkg";
    WriteBinaryFile(assetPath, meshBytes);

    const std::string longPackageName(96, 'P');
    const std::vector<std::string> assetFiles{assetPath.string()};
    ASSERT_TRUE(compiler.CreatePackage(longPackageName, assetFiles, packagePath.string()));

    const std::vector<uint8_t> packageBytes = ReadBinaryFile(packagePath);
    ASSERT_GE(packageBytes.size(), sizeof(PackageHeader));
    const PackageHeader packageHeader = ReadHeader<PackageHeader>(packageBytes);
    EXPECT_EQ(packageHeader.name[sizeof(packageHeader.name) - 1], '\0');
    EXPECT_EQ(std::string(packageHeader.name), std::string(63, 'P'));

    const size_t indexOffset = packageHeader.indexOffset;
    ASSERT_LE(indexOffset, packageBytes.size());
    ASSERT_LE(sizeof(AssetEntry), packageBytes.size() - indexOffset);

    AssetEntry entry{};
    std::memcpy(&entry, packageBytes.data() + indexOffset, sizeof(entry));
    EXPECT_EQ(entry.name[sizeof(entry.name) - 1], '\0');
    EXPECT_EQ(std::string(entry.name), std::string(63, 'A'));

    std::shared_ptr<PackageContainer> package = PackageContainer::LoadFromFile(packagePath.string());
    ASSERT_NE(package, nullptr);
    EXPECT_TRUE(package->HasAsset(std::string(63, 'A')));
}

TEST_F(AssetCompilerTest, CompileCellWritesHeaderWrappedRawPayload) {
    AssetCompiler compiler;

    std::vector<uint8_t> payload(512);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>((i * 13u + 7u) & 0xffu);
    }

    const std::filesystem::path input = tempDir_ / "cell_payload.bin";
    const std::filesystem::path output = tempDir_ / "cell_0_0.ncell";
    WriteBinaryFile(input, payload);

    ASSERT_TRUE(compiler.CompileCell(input.string(), output.string(), "none"));
    const std::vector<uint8_t> fileBytes = ReadBinaryFile(output);
    ExpectCompiledCellPayload(
        fileBytes,
        payload,
        Streaming::CellFileCompression::None,
        Compression::Algorithm::None);
}

TEST_F(AssetCompilerTest, CompileCellRejectsEmptyInputPayload) {
    AssetCompiler compiler;

    const std::filesystem::path input = tempDir_ / "empty_cell_payload.bin";
    const std::filesystem::path output = tempDir_ / "empty_cell.ncell";
    WriteBinaryFile(input, {});

    EXPECT_FALSE(compiler.CompileCell(input.string(), output.string(), "none"));
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST_F(AssetCompilerTest, CompileCellWritesLZ4CompressedPayload) {
    if (!Compression::IsAvailable(Compression::Algorithm::LZ4)) {
        GTEST_SKIP() << "LZ4 backend is not available";
    }

    AssetCompiler compiler;
    std::vector<uint8_t> payload(4096);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>((i / 32u) & 0x0fu);
    }

    const std::filesystem::path input = tempDir_ / "cell_payload_lz4.bin";
    const std::filesystem::path output = tempDir_ / "cell_1_0.ncell";
    WriteBinaryFile(input, payload);

    ASSERT_TRUE(compiler.CompileCell(input.string(), output.string(), "lz4"));
    const std::vector<uint8_t> fileBytes = ReadBinaryFile(output);
    ExpectCompiledCellPayload(
        fileBytes,
        payload,
        Streaming::CellFileCompression::LZ4,
        Compression::Algorithm::LZ4);
}

TEST_F(AssetCompilerTest, CompileCellWritesZstdCompressedPayload) {
    if (!Compression::IsAvailable(Compression::Algorithm::Zstd)) {
        GTEST_SKIP() << "Zstd backend is not available";
    }

    AssetCompiler compiler;
    std::vector<uint8_t> payload(4096);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>((i / 64u) & 0x07u);
    }

    const std::filesystem::path input = tempDir_ / "cell_payload_zstd.bin";
    const std::filesystem::path output = tempDir_ / "cell_2_0.ncell";
    WriteBinaryFile(input, payload);

    ASSERT_TRUE(compiler.CompileCell(input.string(), output.string(), "zstd"));
    const std::vector<uint8_t> fileBytes = ReadBinaryFile(output);
    ExpectCompiledCellPayload(
        fileBytes,
        payload,
        Streaming::CellFileCompression::Zstd,
        Compression::Algorithm::Zstd);
}

TEST_F(AssetCompilerTest, CompileCellLZ4OutputLoadsThroughStreamingManager) {
    if (!Compression::IsAvailable(Compression::Algorithm::LZ4)) {
        GTEST_SKIP() << "LZ4 backend is not available";
    }

    AssetCompiler compiler;
    std::vector<uint8_t> payload(8192);
    for (size_t i = 0; i < payload.size(); ++i) {
        payload[i] = static_cast<uint8_t>((i / 128u) & 0x1fu);
    }

    const std::filesystem::path input = tempDir_ / "streaming_payload.bin";
    const std::filesystem::path output = tempDir_ / "cell_0_0.ncell";
    WriteBinaryFile(input, payload);

    ASSERT_TRUE(compiler.CompileCell(input.string(), output.string(), "lz4"));
    ExpectStreamingManagerLoadsCellPayload(tempDir_, payload);
}

} // namespace testing
} // namespace Next
