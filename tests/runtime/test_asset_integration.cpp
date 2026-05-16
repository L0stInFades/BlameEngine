#include "next/runtime/world.h"
#include "next/runtime/entity.h"
#include "next/runtime/component.h"
#include "next/runtime/asset/asset_manager.h"
#include "next/runtime/asset/asset_types.h"
#include "next/jobsystem/job_system.h"
#include "next/foundation/logger.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace Next {
namespace testing {

using ::testing::Test;

static void CopyFixedName(char* dst, size_t dstSize, const char* name) {
    if (!dst || dstSize == 0) {
        return;
    }

    std::memset(dst, 0, dstSize);
    if (!name) {
        return;
    }

    const size_t copySize = std::min(dstSize - 1, std::strlen(name));
    std::memcpy(dst, name, copySize);
}

static std::filesystem::path MakeTempPackagePath(const char* stem) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "next_asset_runtime_tests";
    std::filesystem::create_directories(dir);
    return dir / (std::string(stem) + "_" + std::to_string(now) + ".npkg");
}

class CurrentPathGuard {
public:
    explicit CurrentPathGuard(const std::filesystem::path& nextPath)
        : previousPath_(std::filesystem::current_path()) {
        std::filesystem::current_path(nextPath);
    }

    ~CurrentPathGuard() {
        std::error_code ec;
        std::filesystem::current_path(previousPath_, ec);
    }

private:
    std::filesystem::path previousPath_;
};

static bool WriteTexturePackageWithPayload(const std::filesystem::path& packagePath,
                                           const char* assetName,
                                           uint32_t width,
                                           uint32_t height,
                                           const uint8_t* pixels,
                                           size_t pixelBytes) {
    TextureHeader texture = {};
    texture.common.magic = AssetHeader::MAGIC;
    texture.common.version = AssetHeader::CURRENT_VERSION;
    texture.common.assetType = AssetType::Texture;
    texture.common.dataSize = static_cast<uint32_t>(pixelBytes);
    texture.common.checksum = 0;
    CopyFixedName(texture.common.name, sizeof(texture.common.name), assetName);
    texture.width = width;
    texture.height = height;
    texture.depth = 1;
    texture.mipLevels = 1;
    texture.arraySize = 1;
    texture.format = 28;
    texture.flags = 0;

    std::vector<uint8_t> assetBytes(sizeof(TextureHeader) + pixelBytes);
    std::memcpy(assetBytes.data(), &texture, sizeof(TextureHeader));
    if (pixelBytes != 0) {
        std::memcpy(assetBytes.data() + sizeof(TextureHeader), pixels, pixelBytes);
    }

    PackageHeader package = {};
    package.magic = PackageHeader::MAGIC;
    package.version = PackageHeader::CURRENT_VERSION;
    package.assetCount = 1;
    package.indexOffset = sizeof(PackageHeader);
    package.dataOffset = package.indexOffset + sizeof(AssetEntry);
    package.checksum = 0;
    CopyFixedName(package.name, sizeof(package.name), packagePath.stem().string().c_str());

    AssetEntry entry = {};
    entry.assetType = AssetType::Texture;
    entry.assetSize = static_cast<uint32_t>(assetBytes.size());
    entry.dataOffset = 0;
    entry.compressedSize = 0;
    entry.decompressedSize = entry.assetSize;
    CopyFixedName(entry.name, sizeof(entry.name), assetName);

    std::ofstream file(packagePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(&package), sizeof(package));
    file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    file.write(reinterpret_cast<const char*>(assetBytes.data()),
               static_cast<std::streamsize>(assetBytes.size()));
    return file.good();
}

static bool WriteSingleTexturePackage(const std::filesystem::path& packagePath,
                                      const char* assetName,
                                      const std::array<uint8_t, 16>& pixels) {
    return WriteTexturePackageWithPayload(packagePath,
                                          assetName,
                                          2,
                                          2,
                                          pixels.data(),
                                          pixels.size());
}

static bool WriteMeshPackageWithPayload(const std::filesystem::path& packagePath,
                                        const char* assetName,
                                        uint32_t vertexCount,
                                        uint32_t indexCount,
                                        uint32_t vertexStride,
                                        uint32_t indexType,
                                        uint32_t materialCount,
                                        const uint8_t* payload,
                                        size_t payloadBytes) {
    MeshHeader mesh = {};
    mesh.common.magic = AssetHeader::MAGIC;
    mesh.common.version = AssetHeader::CURRENT_VERSION;
    mesh.common.assetType = AssetType::Mesh;
    mesh.common.dataSize = static_cast<uint32_t>(payloadBytes);
    mesh.common.checksum = 0;
    CopyFixedName(mesh.common.name, sizeof(mesh.common.name), assetName);
    mesh.vertexCount = vertexCount;
    mesh.indexCount = indexCount;
    mesh.vertexStride = vertexStride;
    mesh.indexType = indexType;
    mesh.vertexFormat = 0;
    mesh.materialCount = materialCount;
    mesh.flags = 0;

    std::vector<uint8_t> assetBytes(sizeof(MeshHeader) + payloadBytes);
    std::memcpy(assetBytes.data(), &mesh, sizeof(MeshHeader));
    if (payloadBytes != 0) {
        std::memcpy(assetBytes.data() + sizeof(MeshHeader), payload, payloadBytes);
    }

    PackageHeader package = {};
    package.magic = PackageHeader::MAGIC;
    package.version = PackageHeader::CURRENT_VERSION;
    package.assetCount = 1;
    package.indexOffset = sizeof(PackageHeader);
    package.dataOffset = package.indexOffset + sizeof(AssetEntry);
    package.checksum = 0;
    CopyFixedName(package.name, sizeof(package.name), packagePath.stem().string().c_str());

    AssetEntry entry = {};
    entry.assetType = AssetType::Mesh;
    entry.assetSize = static_cast<uint32_t>(assetBytes.size());
    entry.dataOffset = 0;
    entry.compressedSize = 0;
    entry.decompressedSize = entry.assetSize;
    CopyFixedName(entry.name, sizeof(entry.name), assetName);

    std::ofstream file(packagePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(&package), sizeof(package));
    file.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    file.write(reinterpret_cast<const char*>(assetBytes.data()),
               static_cast<std::streamsize>(assetBytes.size()));
    return file.good();
}

static std::vector<uint8_t> MakeMinimalMeshPayload() {
    const float vertex[3] = {0.0f, 0.0f, 0.0f};
    const uint16_t index = 0;
    const uint32_t submeshRange[2] = {0u, 1u};

    std::vector<uint8_t> payload(sizeof(vertex) + sizeof(index) + sizeof(submeshRange));
    uint8_t* ptr = payload.data();
    std::memcpy(ptr, vertex, sizeof(vertex));
    ptr += sizeof(vertex);
    std::memcpy(ptr, &index, sizeof(index));
    ptr += sizeof(index);
    std::memcpy(ptr, submeshRange, sizeof(submeshRange));
    return payload;
}

static bool WriteTextureMaterialPackage(const std::filesystem::path& packagePath,
                                        const char* textureName,
                                        const char* materialName,
                                        const char* albedoTextureName,
                                        const std::array<uint8_t, 16>& pixels) {
    TextureHeader texture = {};
    texture.common.magic = AssetHeader::MAGIC;
    texture.common.version = AssetHeader::CURRENT_VERSION;
    texture.common.assetType = AssetType::Texture;
    texture.common.dataSize = static_cast<uint32_t>(pixels.size());
    CopyFixedName(texture.common.name, sizeof(texture.common.name), textureName);
    texture.width = 2;
    texture.height = 2;
    texture.depth = 1;
    texture.mipLevels = 1;
    texture.arraySize = 1;
    texture.format = 28;

    std::vector<uint8_t> textureBytes(sizeof(TextureHeader) + pixels.size());
    std::memcpy(textureBytes.data(), &texture, sizeof(TextureHeader));
    std::memcpy(textureBytes.data() + sizeof(TextureHeader), pixels.data(), pixels.size());

    MaterialHeader material = {};
    material.common.magic = AssetHeader::MAGIC;
    material.common.version = AssetHeader::CURRENT_VERSION;
    material.common.assetType = AssetType::Material;
    CopyFixedName(material.common.name, sizeof(material.common.name), materialName);
    material.textureCount = 1;
    material.parameterCount = 1;
    material.shaderID = 1;
    material.flags = MaterialHeader::OPAQUE_FLAG;

    TextureRef textureRef = {};
    CopyFixedName(textureRef.name, sizeof(textureRef.name), albedoTextureName);
    textureRef.slot = 0;
    textureRef.type = TextureRef::ALBEDO;

    MaterialParam param = {};
    CopyFixedName(param.name, sizeof(param.name), "baseColor");
    param.type = MaterialParam::COLOR;
    param.value[0] = 0.8f;
    param.value[1] = 0.7f;
    param.value[2] = 0.6f;
    param.value[3] = 1.0f;

    material.common.dataSize = sizeof(TextureRef) + sizeof(MaterialParam);
    std::vector<uint8_t> materialBytes(sizeof(MaterialHeader) + material.common.dataSize);
    uint8_t* materialPtr = materialBytes.data();
    std::memcpy(materialPtr, &material, sizeof(MaterialHeader));
    materialPtr += sizeof(MaterialHeader);
    std::memcpy(materialPtr, &textureRef, sizeof(TextureRef));
    materialPtr += sizeof(TextureRef);
    std::memcpy(materialPtr, &param, sizeof(MaterialParam));

    PackageHeader package = {};
    package.magic = PackageHeader::MAGIC;
    package.version = PackageHeader::CURRENT_VERSION;
    package.assetCount = 2;
    package.indexOffset = sizeof(PackageHeader);
    package.dataOffset = package.indexOffset + sizeof(AssetEntry) * package.assetCount;
    CopyFixedName(package.name, sizeof(package.name), packagePath.stem().string().c_str());

    AssetEntry entries[2] = {};
    entries[0].assetType = AssetType::Texture;
    entries[0].assetSize = static_cast<uint32_t>(textureBytes.size());
    entries[0].dataOffset = 0;
    entries[0].decompressedSize = entries[0].assetSize;
    CopyFixedName(entries[0].name, sizeof(entries[0].name), textureName);

    entries[1].assetType = AssetType::Material;
    entries[1].assetSize = static_cast<uint32_t>(materialBytes.size());
    entries[1].dataOffset = static_cast<uint32_t>(textureBytes.size());
    entries[1].decompressedSize = entries[1].assetSize;
    CopyFixedName(entries[1].name, sizeof(entries[1].name), materialName);

    std::ofstream file(packagePath, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        return false;
    }

    file.write(reinterpret_cast<const char*>(&package), sizeof(package));
    file.write(reinterpret_cast<const char*>(entries), sizeof(entries));
    file.write(reinterpret_cast<const char*>(textureBytes.data()),
               static_cast<std::streamsize>(textureBytes.size()));
    file.write(reinterpret_cast<const char*>(materialBytes.data()),
               static_cast<std::streamsize>(materialBytes.size()));
    return file.good();
}

// Integration test for Asset Pipeline + ECS
class AssetIntegrationTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();

        // Initialize Asset Manager
        AssetManager::Instance().Initialize();
    }

    void TearDown() override {
        // Shutdown Asset Manager
        AssetManager::Instance().Shutdown();

        Logger::Shutdown();
    }

    World world_;
};

class AssetRuntimeLoadTest : public Test {
protected:
    void SetUp() override {
        Logger::Initialize();
        JobSystem::Instance().Initialize(1);
        AssetManager::Instance().Initialize();
    }

    void TearDown() override {
        AssetManager::Instance().PumpAsyncCallbacks();
        AssetManager::Instance().Shutdown();
        JobSystem::Instance().Shutdown();
        if (!packagePath_.empty()) {
            std::error_code ec;
            std::filesystem::remove(packagePath_, ec);
        }
        Logger::Shutdown();
    }

    std::filesystem::path packagePath_;
};

TEST_F(AssetRuntimeLoadTest, MeshAssetViewExposesLoadedPayload) {
    constexpr const char* kAssetName = "TinyMesh";
    const std::vector<uint8_t> payload = MakeMinimalMeshPayload();

    packagePath_ = MakeTempPackagePath("asset_mesh_payload_view");
    ASSERT_TRUE(WriteMeshPackageWithPayload(packagePath_,
                                            kAssetName,
                                            1,
                                            1,
                                            12,
                                            0,
                                            1,
                                            payload.data(),
                                            payload.size()));
    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));

    const std::string qualifiedName = packagePath_.stem().string() + "::" + kAssetName;
    AssetHandle handle = AssetManager::Instance().LoadAssetSync(qualifiedName);
    ASSERT_TRUE(handle.IsValid());

    MeshAssetView meshView;
    ASSERT_TRUE(AssetManager::Instance().GetMeshAssetView(handle, meshView));
    EXPECT_EQ(meshView.header.vertexCount, 1u);
    EXPECT_EQ(meshView.header.indexCount, 1u);
    EXPECT_EQ(meshView.header.vertexStride, 12u);
    ASSERT_EQ(meshView.payloadBytes, payload.size());
    ASSERT_NE(meshView.payload, nullptr);
    EXPECT_EQ(std::memcmp(meshView.payload, payload.data(), payload.size()), 0);

    TextureAssetView textureView;
    EXPECT_FALSE(AssetManager::Instance().GetTextureAssetView(handle, textureView));

    AssetManager::Instance().Release(handle);
    AssetManager::Instance().UnloadPackage(packagePath_.stem().string());
}

TEST_F(AssetRuntimeLoadTest, MeshLoadRejectsPayloadSmallerThanDeclaredLayout) {
    constexpr const char* kAssetName = "ShortMesh";
    const std::vector<uint8_t> payload = MakeMinimalMeshPayload();
    const size_t shortBytes = payload.size() - sizeof(uint32_t) * 2u;

    packagePath_ = MakeTempPackagePath("asset_short_mesh_payload");
    ASSERT_TRUE(WriteMeshPackageWithPayload(packagePath_,
                                            kAssetName,
                                            1,
                                            1,
                                            12,
                                            0,
                                            1,
                                            payload.data(),
                                            shortBytes));
    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));

    const std::string packageName = packagePath_.stem().string();
    const size_t failedLoadsBefore = AssetManager::Instance().GetStats().failedLoads;
    AssetHandle handle = AssetManager::Instance().LoadAssetSync(packageName + "::" + kAssetName);
    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(AssetManager::Instance().GetStats().failedLoads, failedLoadsBefore + 1);

    AssetManager::Instance().UnloadPackage(packageName);
}

TEST_F(AssetRuntimeLoadTest, MeshLoadRejectsPayloadSizeOverflow) {
    constexpr const char* kAssetName = "HugeMesh";
    const std::vector<uint8_t> payload = MakeMinimalMeshPayload();

    packagePath_ = MakeTempPackagePath("asset_huge_mesh_payload");
    ASSERT_TRUE(WriteMeshPackageWithPayload(packagePath_,
                                            kAssetName,
                                            std::numeric_limits<uint32_t>::max(),
                                            std::numeric_limits<uint32_t>::max(),
                                            std::numeric_limits<uint32_t>::max(),
                                            1,
                                            0,
                                            payload.data(),
                                            payload.size()));
    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));

    const std::string packageName = packagePath_.stem().string();
    const size_t failedLoadsBefore = AssetManager::Instance().GetStats().failedLoads;
    AssetHandle handle = AssetManager::Instance().LoadAssetSync(packageName + "::" + kAssetName);
    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(AssetManager::Instance().GetStats().failedLoads, failedLoadsBefore + 1);

    AssetManager::Instance().UnloadPackage(packageName);
}

TEST_F(AssetRuntimeLoadTest, TextureAssetViewExposesLoadedPayload) {
    constexpr const char* kAssetName = "AsyncTexture";
    const std::array<uint8_t, 16> pixels = {
        0x10, 0x20, 0x30, 0xff,
        0x40, 0x50, 0x60, 0xff,
        0x70, 0x80, 0x90, 0xff,
        0xa0, 0xb0, 0xc0, 0xff,
    };

    packagePath_ = MakeTempPackagePath("asset_payload_view");
    ASSERT_TRUE(WriteSingleTexturePackage(packagePath_, kAssetName, pixels));
    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));

    const std::string qualifiedName = packagePath_.stem().string() + "::" + kAssetName;
    AssetHandle handle = AssetManager::Instance().LoadAssetSync(qualifiedName);
    ASSERT_TRUE(handle.IsValid());

    TextureAssetView textureView;
    ASSERT_TRUE(AssetManager::Instance().GetTextureAssetView(handle, textureView));
    EXPECT_EQ(textureView.header.width, 2u);
    EXPECT_EQ(textureView.header.height, 2u);
    EXPECT_EQ(textureView.header.format, 28u);
    ASSERT_EQ(textureView.pixelBytes, pixels.size());
    ASSERT_NE(textureView.pixels, nullptr);
    EXPECT_EQ(std::memcmp(textureView.pixels, pixels.data(), pixels.size()), 0);

    MeshAssetView meshView;
    EXPECT_FALSE(AssetManager::Instance().GetMeshAssetView(handle, meshView));

    AssetManager::Instance().Release(handle);
    AssetManager::Instance().UnloadPackage(packagePath_.stem().string());
}

TEST_F(AssetRuntimeLoadTest, RelativePackageFilenameUsesStemForPackageName) {
    constexpr const char* kAssetName = "RelativeTexture";
    constexpr const char* kPackageFile = "relative_asset_package.npkg";
    constexpr const char* kPackageName = "relative_asset_package";
    const std::array<uint8_t, 16> pixels = {
        0x01, 0x20, 0x30, 0xff,
        0x04, 0x50, 0x60, 0xff,
        0x07, 0x80, 0x90, 0xff,
        0x0a, 0xb0, 0xc0, 0xff,
    };

    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path packageDir =
        std::filesystem::temp_directory_path() / ("next_asset_relative_package_" + std::to_string(now));
    std::filesystem::create_directories(packageDir);
    packagePath_ = packageDir / kPackageFile;
    ASSERT_TRUE(WriteSingleTexturePackage(packagePath_, kAssetName, pixels));

    {
        CurrentPathGuard cwd(packageDir);
        ASSERT_TRUE(AssetManager::Instance().LoadPackage(kPackageFile));
    }

    AssetHandle handle = AssetManager::Instance().LoadAssetSync(std::string(kPackageName) + "::" + kAssetName);
    ASSERT_TRUE(handle.IsValid());

    TextureAssetView textureView;
    ASSERT_TRUE(AssetManager::Instance().GetTextureAssetView(handle, textureView));
    EXPECT_EQ(textureView.header.width, 2u);
    EXPECT_EQ(textureView.header.height, 2u);
    ASSERT_EQ(textureView.pixelBytes, pixels.size());
    ASSERT_NE(textureView.pixels, nullptr);
    EXPECT_EQ(std::memcmp(textureView.pixels, pixels.data(), pixels.size()), 0);

    AssetManager::Instance().Release(handle);
    AssetManager::Instance().UnloadPackage(kPackageName);
    std::error_code ec;
    std::filesystem::remove_all(packageDir, ec);
    packagePath_.clear();
}

TEST_F(AssetRuntimeLoadTest, TextureLoadRejectsPayloadSmallerThanDeclaredRgba8Size) {
    constexpr const char* kAssetName = "ShortTexture";
    const std::array<uint8_t, 4> pixels = {0x10, 0x20, 0x30, 0xff};

    packagePath_ = MakeTempPackagePath("asset_short_texture_payload");
    ASSERT_TRUE(WriteTexturePackageWithPayload(packagePath_,
                                               kAssetName,
                                               2,
                                               2,
                                               pixels.data(),
                                               pixels.size()));
    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));

    const std::string packageName = packagePath_.stem().string();
    const size_t failedLoadsBefore = AssetManager::Instance().GetStats().failedLoads;
    AssetHandle handle = AssetManager::Instance().LoadAssetSync(packageName + "::" + kAssetName);
    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(AssetManager::Instance().GetStats().failedLoads, failedLoadsBefore + 1);

    AssetManager::Instance().UnloadPackage(packageName);
}

TEST_F(AssetRuntimeLoadTest, TextureLoadRejectsRgba8PayloadSizeOverflow) {
    constexpr const char* kAssetName = "HugeTexture";
    const std::array<uint8_t, 16> pixels = {};

    packagePath_ = MakeTempPackagePath("asset_huge_texture_payload");
    ASSERT_TRUE(WriteTexturePackageWithPayload(packagePath_,
                                               kAssetName,
                                               std::numeric_limits<uint32_t>::max(),
                                               std::numeric_limits<uint32_t>::max(),
                                               pixels.data(),
                                               pixels.size()));
    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));

    const std::string packageName = packagePath_.stem().string();
    const size_t failedLoadsBefore = AssetManager::Instance().GetStats().failedLoads;
    AssetHandle handle = AssetManager::Instance().LoadAssetSync(packageName + "::" + kAssetName);
    EXPECT_FALSE(handle.IsValid());
    EXPECT_EQ(AssetManager::Instance().GetStats().failedLoads, failedLoadsBefore + 1);

    AssetManager::Instance().UnloadPackage(packageName);
}

TEST_F(AssetRuntimeLoadTest, MaterialAssetViewExposesTextureRefsAndParameters) {
    constexpr const char* kTextureName = "TestChecker";
    constexpr const char* kMaterialName = "TestPBR";
    const std::array<uint8_t, 16> pixels = {
        0x11, 0x22, 0x33, 0xff,
        0x44, 0x55, 0x66, 0xff,
        0x77, 0x88, 0x99, 0xff,
        0xaa, 0xbb, 0xcc, 0xff,
    };

    packagePath_ = MakeTempPackagePath("asset_material_view");
    ASSERT_TRUE(WriteTextureMaterialPackage(packagePath_, kTextureName, kMaterialName, kTextureName, pixels));
    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));

    const std::string packageName = packagePath_.stem().string();
    AssetHandle materialHandle = AssetManager::Instance().LoadAssetSync(packageName + "::" + kMaterialName);
    ASSERT_TRUE(materialHandle.IsValid());

    MaterialAssetView materialView;
    ASSERT_TRUE(AssetManager::Instance().GetMaterialAssetView(materialHandle, materialView));
    EXPECT_EQ(materialView.header.textureCount, 1u);
    EXPECT_EQ(materialView.header.parameterCount, 1u);

    const TextureRef* refs = materialView.GetTextureRefs();
    ASSERT_NE(refs, nullptr);
    EXPECT_STREQ(refs[0].name, kTextureName);
    EXPECT_EQ(refs[0].type, static_cast<uint32_t>(TextureRef::ALBEDO));

    TextureRef albedoRef = {};
    ASSERT_TRUE(materialView.FindTextureRef(TextureRef::ALBEDO, albedoRef));
    EXPECT_STREQ(albedoRef.name, kTextureName);

    const MaterialParam* params = materialView.GetParameters();
    ASSERT_NE(params, nullptr);
    EXPECT_STREQ(params[0].name, "baseColor");
    EXPECT_EQ(params[0].type, static_cast<uint32_t>(MaterialParam::COLOR));
    EXPECT_FLOAT_EQ(params[0].value[0], 0.8f);
    EXPECT_FLOAT_EQ(params[0].value[3], 1.0f);

    AssetHandle textureHandle = AssetManager::Instance().LoadAssetSync(packageName + "::" + albedoRef.name);
    ASSERT_TRUE(textureHandle.IsValid());
    TextureAssetView textureView;
    ASSERT_TRUE(AssetManager::Instance().GetTextureAssetView(textureHandle, textureView));
    ASSERT_EQ(textureView.pixelBytes, pixels.size());
    ASSERT_NE(textureView.pixels, nullptr);
    EXPECT_EQ(std::memcmp(textureView.pixels, pixels.data(), pixels.size()), 0);

    AssetManager::Instance().Release(textureHandle);
    AssetManager::Instance().Release(materialHandle);
    AssetManager::Instance().UnloadPackage(packageName);
}

TEST_F(AssetRuntimeLoadTest, MaterialAssetViewRejectsUnterminatedRefAndParamNames) {
    std::vector<uint8_t> payload(sizeof(TextureRef) + sizeof(MaterialParam));
    auto* ref = reinterpret_cast<TextureRef*>(payload.data());
    auto* param = reinterpret_cast<MaterialParam*>(payload.data() + sizeof(TextureRef));

    std::memset(ref->name, 'T', sizeof(ref->name));
    ref->type = TextureRef::ALBEDO;
    CopyFixedName(param->name, sizeof(param->name), "baseColor");
    param->type = MaterialParam::COLOR;

    MaterialAssetView view{};
    view.header.textureCount = 1;
    view.header.parameterCount = 1;
    view.payload = payload.data();
    view.payloadBytes = payload.size();

    EXPECT_FALSE(view.HasValidTextureRefs());
    EXPECT_EQ(view.GetTextureRefs(), nullptr);
    TextureRef outRef{};
    EXPECT_FALSE(view.FindTextureRef(TextureRef::ALBEDO, outRef));
    EXPECT_FALSE(view.HasValidPayload());

    CopyFixedName(ref->name, sizeof(ref->name), "TestChecker");
    std::memset(param->name, 'P', sizeof(param->name));

    EXPECT_TRUE(view.HasValidTextureRefs());
    EXPECT_FALSE(view.HasValidParameters());
    EXPECT_EQ(view.GetParameters(), nullptr);
    EXPECT_FALSE(view.HasValidPayload());
}

TEST_F(AssetRuntimeLoadTest, MaterialAssetViewRejectsInvalidRefAndParamTypes) {
    std::vector<uint8_t> payload(sizeof(TextureRef) + sizeof(MaterialParam));
    auto* ref = reinterpret_cast<TextureRef*>(payload.data());
    auto* param = reinterpret_cast<MaterialParam*>(payload.data() + sizeof(TextureRef));

    CopyFixedName(ref->name, sizeof(ref->name), "TestChecker");
    ref->slot = 0;
    ref->type = TextureRef::ALBEDO;
    CopyFixedName(param->name, sizeof(param->name), "baseColor");
    param->type = MaterialParam::COLOR;

    MaterialAssetView view{};
    view.header.textureCount = 1;
    view.header.parameterCount = 1;
    view.payload = payload.data();
    view.payloadBytes = payload.size();

    EXPECT_TRUE(view.HasValidPayload());

    ref->type = static_cast<uint32_t>(TextureRef::OCCLUSION) + 1;
    EXPECT_FALSE(view.HasValidTextureRefs());
    EXPECT_EQ(view.GetTextureRefs(), nullptr);
    EXPECT_FALSE(view.HasValidPayload());

    ref->type = TextureRef::ALBEDO;
    ref->slot = static_cast<uint32_t>(TextureRef::OCCLUSION) + 1;
    EXPECT_FALSE(view.HasValidTextureRefs());
    EXPECT_FALSE(view.HasValidPayload());

    ref->slot = 0;
    param->type = static_cast<uint32_t>(MaterialParam::COLOR) + 1;
    EXPECT_TRUE(view.HasValidTextureRefs());
    EXPECT_FALSE(view.HasValidParameters());
    EXPECT_EQ(view.GetParameters(), nullptr);
    EXPECT_FALSE(view.HasValidPayload());
}

TEST_F(AssetRuntimeLoadTest, MaterialAssetViewRejectsDuplicateTextureTypesAndSlots) {
    std::vector<uint8_t> payload(sizeof(TextureRef) * 2 + sizeof(MaterialParam));
    auto* refs = reinterpret_cast<TextureRef*>(payload.data());
    auto* param = reinterpret_cast<MaterialParam*>(payload.data() + sizeof(TextureRef) * 2);

    CopyFixedName(refs[0].name, sizeof(refs[0].name), "BaseColor");
    refs[0].slot = 0;
    refs[0].type = TextureRef::ALBEDO;
    CopyFixedName(refs[1].name, sizeof(refs[1].name), "OtherBaseColor");
    refs[1].slot = 1;
    refs[1].type = TextureRef::ALBEDO;
    CopyFixedName(param->name, sizeof(param->name), "baseColor");
    param->type = MaterialParam::COLOR;

    MaterialAssetView view{};
    view.header.textureCount = 2;
    view.header.parameterCount = 1;
    view.payload = payload.data();
    view.payloadBytes = payload.size();

    EXPECT_FALSE(view.HasValidTextureRefs());
    EXPECT_EQ(view.GetTextureRefs(), nullptr);
    EXPECT_FALSE(view.HasValidPayload());

    refs[1].type = TextureRef::NORMAL;
    refs[1].slot = 0;
    EXPECT_FALSE(view.HasValidTextureRefs());
    EXPECT_FALSE(view.HasValidPayload());

    refs[1].slot = 1;
    EXPECT_TRUE(view.HasValidTextureRefs());
    EXPECT_TRUE(view.HasValidPayload());
}

TEST_F(AssetRuntimeLoadTest, MaterialLoadRejectsUnterminatedTextureRefName) {
    constexpr const char* kTextureName = "TestChecker";
    constexpr const char* kMaterialName = "TestPBR";
    const std::array<uint8_t, 16> pixels = {
        0x11, 0x22, 0x33, 0xff,
        0x44, 0x55, 0x66, 0xff,
        0x77, 0x88, 0x99, 0xff,
        0xaa, 0xbb, 0xcc, 0xff,
    };

    packagePath_ = MakeTempPackagePath("asset_bad_material_ref");
    ASSERT_TRUE(WriteTextureMaterialPackage(packagePath_, kTextureName, kMaterialName, kTextureName, pixels));

    const size_t materialRefNameOffset =
        sizeof(PackageHeader) + sizeof(AssetEntry) * 2 + sizeof(TextureHeader) + pixels.size() +
        sizeof(MaterialHeader);
    TextureRef refForSize{};
    std::array<char, sizeof(refForSize.name)> unterminatedName{};
    unterminatedName.fill('Z');

    std::fstream file(packagePath_, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.is_open());
    file.seekp(static_cast<std::streamoff>(materialRefNameOffset), std::ios::beg);
    file.write(unterminatedName.data(), static_cast<std::streamsize>(unterminatedName.size()));
    ASSERT_TRUE(file.good());
    file.close();

    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));
    const std::string packageName = packagePath_.stem().string();
    const size_t failedLoadsBefore = AssetManager::Instance().GetStats().failedLoads;

    AssetHandle materialHandle = AssetManager::Instance().LoadAssetSync(packageName + "::" + kMaterialName);
    EXPECT_FALSE(materialHandle.IsValid());
    EXPECT_EQ(AssetManager::Instance().GetStats().failedLoads, failedLoadsBefore + 1);

    AssetManager::Instance().UnloadPackage(packageName);
}

TEST_F(AssetRuntimeLoadTest, MaterialLoadRejectsInvalidTextureRefType) {
    constexpr const char* kTextureName = "TestChecker";
    constexpr const char* kMaterialName = "TestPBR";
    const std::array<uint8_t, 16> pixels = {
        0x11, 0x22, 0x33, 0xff,
        0x44, 0x55, 0x66, 0xff,
        0x77, 0x88, 0x99, 0xff,
        0xaa, 0xbb, 0xcc, 0xff,
    };

    packagePath_ = MakeTempPackagePath("asset_bad_material_ref_type");
    ASSERT_TRUE(WriteTextureMaterialPackage(packagePath_, kTextureName, kMaterialName, kTextureName, pixels));

    const size_t materialRefOffset =
        sizeof(PackageHeader) + sizeof(AssetEntry) * 2 + sizeof(TextureHeader) + pixels.size() +
        sizeof(MaterialHeader);

    TextureRef textureRef{};
    std::fstream file(packagePath_, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.is_open());
    file.seekg(static_cast<std::streamoff>(materialRefOffset), std::ios::beg);
    file.read(reinterpret_cast<char*>(&textureRef), sizeof(textureRef));
    ASSERT_TRUE(file.good());
    textureRef.type = static_cast<uint32_t>(TextureRef::OCCLUSION) + 1;
    file.seekp(static_cast<std::streamoff>(materialRefOffset), std::ios::beg);
    file.write(reinterpret_cast<const char*>(&textureRef), sizeof(textureRef));
    ASSERT_TRUE(file.good());
    file.close();

    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));
    const std::string packageName = packagePath_.stem().string();
    const size_t failedLoadsBefore = AssetManager::Instance().GetStats().failedLoads;

    AssetHandle materialHandle = AssetManager::Instance().LoadAssetSync(packageName + "::" + kMaterialName);
    EXPECT_FALSE(materialHandle.IsValid());
    EXPECT_EQ(AssetManager::Instance().GetStats().failedLoads, failedLoadsBefore + 1);

    AssetManager::Instance().UnloadPackage(packageName);
}

TEST_F(AssetRuntimeLoadTest, MaterialLoadRejectsPayloadSizeMismatch) {
    constexpr const char* kTextureName = "TestChecker";
    constexpr const char* kMaterialName = "TestPBR";
    const std::array<uint8_t, 16> pixels = {
        0x11, 0x22, 0x33, 0xff,
        0x44, 0x55, 0x66, 0xff,
        0x77, 0x88, 0x99, 0xff,
        0xaa, 0xbb, 0xcc, 0xff,
    };

    packagePath_ = MakeTempPackagePath("asset_bad_material_size");
    ASSERT_TRUE(WriteTextureMaterialPackage(packagePath_, kTextureName, kMaterialName, kTextureName, pixels));

    const size_t materialOffset =
        sizeof(PackageHeader) + sizeof(AssetEntry) * 2 + sizeof(TextureHeader) + pixels.size();

    MaterialHeader materialHeader{};
    std::fstream file(packagePath_, std::ios::binary | std::ios::in | std::ios::out);
    ASSERT_TRUE(file.is_open());
    file.seekg(static_cast<std::streamoff>(materialOffset), std::ios::beg);
    file.read(reinterpret_cast<char*>(&materialHeader), sizeof(materialHeader));
    ASSERT_TRUE(file.good());
    materialHeader.common.dataSize = 1;
    file.seekp(static_cast<std::streamoff>(materialOffset), std::ios::beg);
    file.write(reinterpret_cast<const char*>(&materialHeader), sizeof(materialHeader));
    ASSERT_TRUE(file.good());
    file.close();

    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));
    const std::string packageName = packagePath_.stem().string();
    const size_t failedLoadsBefore = AssetManager::Instance().GetStats().failedLoads;

    AssetHandle materialHandle = AssetManager::Instance().LoadAssetSync(packageName + "::" + kMaterialName);
    EXPECT_FALSE(materialHandle.IsValid());
    EXPECT_EQ(AssetManager::Instance().GetStats().failedLoads, failedLoadsBefore + 1);

    AssetManager::Instance().UnloadPackage(packageName);
}

TEST_F(AssetRuntimeLoadTest, AssetStatsHelpersExposeSnapshotState) {
    AssetStats stats;
    EXPECT_EQ(stats.OutstandingAsyncOperationCount(), 0u);
    EXPECT_FALSE(stats.HasLoadedAssets());
    EXPECT_FALSE(stats.HasMemoryUsage());
    EXPECT_FALSE(stats.HasPendingLoads());
    EXPECT_FALSE(stats.HasPendingCallbacks());
    EXPECT_FALSE(stats.HasOutstandingAsyncOperations());
    EXPECT_FALSE(stats.HasFailures());

    stats.loadedAssets = 2;
    stats.totalMemory = 4096;
    stats.pendingLoads = 1;
    stats.pendingCallbacks = 3;
    stats.failedLoads = 1;

    EXPECT_EQ(stats.OutstandingAsyncOperationCount(), 4u);
    EXPECT_TRUE(stats.HasLoadedAssets());
    EXPECT_TRUE(stats.HasMemoryUsage());
    EXPECT_TRUE(stats.HasPendingLoads());
    EXPECT_TRUE(stats.HasPendingCallbacks());
    EXPECT_TRUE(stats.HasOutstandingAsyncOperations());
    EXPECT_TRUE(stats.HasFailures());
}

TEST_F(AssetRuntimeLoadTest, AsyncLoadCallbacksPumpOnCallingThread) {
    constexpr const char* kAssetName = "AsyncTexture";
    const std::array<uint8_t, 16> pixels = {
        0x01, 0x02, 0x03, 0xff,
        0x04, 0x05, 0x06, 0xff,
        0x07, 0x08, 0x09, 0xff,
        0x0a, 0x0b, 0x0c, 0xff,
    };

    packagePath_ = MakeTempPackagePath("asset_async_pump");
    ASSERT_TRUE(WriteSingleTexturePackage(packagePath_, kAssetName, pixels));
    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));

    const std::thread::id callingThread = std::this_thread::get_id();
    const std::string qualifiedName = packagePath_.stem().string() + "::" + kAssetName;
    bool callbackCalled = false;
    std::thread::id callbackThread;
    AssetHandle loadedHandle;

    AssetManager::Instance().LoadAssetAsync(
        qualifiedName,
        [&](const AssetLoadResult& result) {
            callbackCalled = true;
            callbackThread = std::this_thread::get_id();
            loadedHandle = result.handle;

            ASSERT_TRUE(result.success);
            ASSERT_TRUE(result.handle.IsValid());

            TextureAssetView textureView;
            ASSERT_TRUE(AssetManager::Instance().GetTextureAssetView(result.handle, textureView));
            EXPECT_EQ(textureView.header.width, 2u);
            EXPECT_EQ(textureView.header.height, 2u);
            ASSERT_EQ(textureView.pixelBytes, pixels.size());
            ASSERT_NE(textureView.pixels, nullptr);
            EXPECT_EQ(std::memcmp(textureView.pixels, pixels.data(), pixels.size()), 0);
        });

    EXPECT_FALSE(callbackCalled);
    JobSystem::Instance().WaitForAll();
    EXPECT_FALSE(callbackCalled);
    EXPECT_EQ(AssetManager::Instance().GetPendingAsyncCallbackCount(), 1u);
    AssetStats queuedStats = AssetManager::Instance().GetStats();
    EXPECT_EQ(queuedStats.pendingLoads, 0u);
    EXPECT_EQ(queuedStats.pendingCallbacks, 1u);
    EXPECT_EQ(queuedStats.OutstandingAsyncOperationCount(), 1u);
    EXPECT_FALSE(queuedStats.HasPendingLoads());
    EXPECT_TRUE(queuedStats.HasPendingCallbacks());
    EXPECT_TRUE(queuedStats.HasOutstandingAsyncOperations());

    EXPECT_EQ(AssetManager::Instance().PumpAsyncCallbacks(1), 1u);
    EXPECT_TRUE(callbackCalled);
    EXPECT_EQ(callbackThread, callingThread);
    EXPECT_EQ(AssetManager::Instance().GetPendingAsyncCallbackCount(), 0u);
    AssetStats pumpedStats = AssetManager::Instance().GetStats();
    EXPECT_EQ(pumpedStats.pendingCallbacks, 0u);
    EXPECT_FALSE(pumpedStats.HasPendingCallbacks());
    EXPECT_FALSE(pumpedStats.HasOutstandingAsyncOperations());

    if (loadedHandle.IsValid()) {
        AssetManager::Instance().Release(loadedHandle);
    }
    AssetManager::Instance().UnloadPackage(packagePath_.stem().string());
}

TEST_F(AssetRuntimeLoadTest, ConcurrentSyncLoadsShareSingleCacheEntry) {
    constexpr const char* kAssetName = "SharedTexture";
    constexpr uint32_t kWidth = 512;
    constexpr uint32_t kHeight = 512;
    constexpr size_t kPixelBytes = static_cast<size_t>(kWidth) * kHeight * 4u;
    constexpr size_t kLoadCount = 16;

    std::vector<uint8_t> pixels(kPixelBytes, 0x7f);
    packagePath_ = MakeTempPackagePath("asset_concurrent_shared");
    ASSERT_TRUE(WriteTexturePackageWithPayload(packagePath_,
                                               kAssetName,
                                               kWidth,
                                               kHeight,
                                               pixels.data(),
                                               pixels.size()));
    ASSERT_TRUE(AssetManager::Instance().LoadPackage(packagePath_.string()));

    const std::string qualifiedName = packagePath_.stem().string() + "::" + kAssetName;
    std::vector<AssetHandle> handles(kLoadCount);
    std::vector<std::thread> threads;
    std::atomic<size_t> readyCount{0};
    std::atomic<bool> start{false};

    threads.reserve(kLoadCount);
    for (size_t i = 0; i < kLoadCount; ++i) {
        threads.emplace_back([&, i]() {
            readyCount.fetch_add(1, std::memory_order_acq_rel);
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            handles[i] = AssetManager::Instance().LoadAssetSync(qualifiedName);
        });
    }

    while (readyCount.load(std::memory_order_acquire) != kLoadCount) {
        std::this_thread::yield();
    }
    start.store(true, std::memory_order_release);

    for (std::thread& thread : threads) {
        thread.join();
    }

    ASSERT_TRUE(handles.front().IsValid());
    const uint64_t sharedId = handles.front().GetID();
    for (const AssetHandle& handle : handles) {
        ASSERT_TRUE(handle.IsValid());
        EXPECT_EQ(handle.GetID(), sharedId);
    }

    const AssetStats stats = AssetManager::Instance().GetStats();
    EXPECT_EQ(stats.loadedAssets, 1u);
    EXPECT_EQ(stats.totalMemory, kPixelBytes);
    EXPECT_EQ(AssetManager::Instance().GetRefCount(handles.front()), kLoadCount);

    for (const AssetHandle& handle : handles) {
        AssetManager::Instance().Release(handle);
    }
    EXPECT_FALSE(AssetManager::Instance().IsAssetLoaded(qualifiedName));
    AssetManager::Instance().UnloadPackage(packagePath_.stem().string());
}

TEST_F(AssetRuntimeLoadTest, AsyncLoadFailureQueuesFailureCallbackAndClearsPendingState) {
    const std::string missingName = "missing_package::MissingTexture";
    const size_t failedLoadsBefore = AssetManager::Instance().GetStats().failedLoads;
    bool callbackCalled = false;
    AssetLoadResult observedResult;

    AssetManager::Instance().LoadAssetAsync(
        missingName,
        [&](const AssetLoadResult& result) {
            callbackCalled = true;
            observedResult = result;
        });

    EXPECT_FALSE(callbackCalled);
    JobSystem::Instance().WaitForAll();

    AssetStats queuedStats = AssetManager::Instance().GetStats();
    EXPECT_EQ(queuedStats.pendingLoads, 0u);
    EXPECT_EQ(queuedStats.pendingCallbacks, 1u);
    EXPECT_EQ(queuedStats.failedLoads, failedLoadsBefore + 1);
    EXPECT_TRUE(queuedStats.HasPendingCallbacks());
    EXPECT_TRUE(queuedStats.HasOutstandingAsyncOperations());
    EXPECT_TRUE(queuedStats.HasFailures());

    EXPECT_EQ(AssetManager::Instance().PumpAsyncCallbacks(1), 1u);
    EXPECT_TRUE(callbackCalled);
    EXPECT_FALSE(observedResult.success);
    EXPECT_FALSE(observedResult.handle.IsValid());
    EXPECT_NE(observedResult.errorMessage.find(missingName), std::string::npos);

    AssetStats pumpedStats = AssetManager::Instance().GetStats();
    EXPECT_EQ(pumpedStats.pendingLoads, 0u);
    EXPECT_EQ(pumpedStats.pendingCallbacks, 0u);
    EXPECT_FALSE(pumpedStats.HasOutstandingAsyncOperations());
    EXPECT_TRUE(pumpedStats.HasFailures());
}

TEST_F(AssetRuntimeLoadTest, PumpAsyncCallbacksContainsThrowingCallbackAndContinues) {
    bool secondCallbackCalled = false;

    AssetManager::Instance().LoadAssetAsync(
        "missing_package::FirstMissingTexture",
        [](const AssetLoadResult&) {
            throw std::runtime_error("callback failure");
        });
    AssetManager::Instance().LoadAssetAsync(
        "missing_package::SecondMissingTexture",
        [&](const AssetLoadResult& result) {
            secondCallbackCalled = true;
            EXPECT_FALSE(result.success);
        });

    JobSystem::Instance().WaitForAll();
    EXPECT_EQ(AssetManager::Instance().GetPendingAsyncCallbackCount(), 2u);
    EXPECT_EQ(AssetManager::Instance().PumpAsyncCallbacks(), 2u);
    EXPECT_TRUE(secondCallbackCalled);
    EXPECT_EQ(AssetManager::Instance().GetPendingAsyncCallbackCount(), 0u);
    EXPECT_EQ(AssetManager::Instance().GetStats().pendingLoads, 0u);
}

// Test entity with mesh renderer component
TEST_F(AssetIntegrationTest, EntityWithMeshRenderer) {
    // Create entity
    Entity entity = world_.CreateEntity();
    EXPECT_TRUE(world_.IsEntityValid(entity));

    // Add transform component
    auto& transform = world_.AddComponent<TransformComponent>(entity);
    transform.position[0] = 10.0f;
    transform.position[1] = 20.0f;
    transform.position[2] = 30.0f;

    // Add mesh renderer component
    auto& renderer = world_.AddComponent<MeshRendererComponent>(entity);

    // Set asset handles (using invalid handles for testing)
    renderer.mesh = MeshHandle();
    renderer.material = MaterialHandle();
    renderer.submeshIndex = 0;
    renderer.castShadows = true;
    renderer.receiveShadows = false;

    // Verify components were added
    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(world_.HasComponent<MeshRendererComponent>(entity));

    // Verify transform data
    auto* transformPtr = world_.GetComponent<TransformComponent>(entity);
    ASSERT_NE(transformPtr, nullptr);
    EXPECT_FLOAT_EQ(transformPtr->position[0], 10.0f);
    EXPECT_FLOAT_EQ(transformPtr->position[1], 20.0f);
    EXPECT_FLOAT_EQ(transformPtr->position[2], 30.0f);

    // Verify renderer data
    auto* rendererPtr = world_.GetComponent<MeshRendererComponent>(entity);
    ASSERT_NE(rendererPtr, nullptr);
    EXPECT_EQ(rendererPtr->submeshIndex, 0);
    EXPECT_TRUE(rendererPtr->castShadows);
    EXPECT_FALSE(rendererPtr->receiveShadows);
}

// Test query for renderable entities
TEST_F(AssetIntegrationTest, QueryRenderableEntities) {
    // Create renderable entity
    Entity e1 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<MeshRendererComponent>(e1);

    // Create non-renderable entity (only transform)
    Entity e2 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e2);

    // Create another renderable entity
    Entity e3 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e3);
    world_.AddComponent<MeshRendererComponent>(e3);

    // Create entity with only mesh renderer (no transform)
    Entity e4 = world_.CreateEntity();
    world_.AddComponent<MeshRendererComponent>(e4);

    // Query for renderable entities (transform + mesh renderer)
    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();

    // Should find exactly 2 entities (e1 and e3)
    EXPECT_EQ(renderables.size(), 2u);

    // Verify e1 is in results
    bool foundE1 = std::find(renderables.begin(), renderables.end(), e1) != renderables.end();
    EXPECT_TRUE(foundE1);

    // Verify e3 is in results
    bool foundE3 = std::find(renderables.begin(), renderables.end(), e3) != renderables.end();
    EXPECT_TRUE(foundE3);

    // Verify e2 and e4 are NOT in results
    bool foundE2 = std::find(renderables.begin(), renderables.end(), e2) != renderables.end();
    EXPECT_FALSE(foundE2);

    bool foundE4 = std::find(renderables.begin(), renderables.end(), e4) != renderables.end();
    EXPECT_FALSE(foundE4);
}

// Test entity hierarchy with assets
TEST_F(AssetIntegrationTest, EntityHierarchyWithAssets) {
    // Create parent entity with mesh
    Entity parent = world_.CreateEntity();
    world_.AddComponent<NameComponent>(parent, "Parent");
    auto& parentTransform = world_.AddComponent<TransformComponent>(parent);
    parentTransform.position[0] = 0.0f;
    parentTransform.position[1] = 0.0f;
    parentTransform.position[2] = 0.0f;

    auto& parentRenderer = world_.AddComponent<MeshRendererComponent>(parent);
    parentRenderer.submeshIndex = 0;

    // Create child entity with mesh
    Entity child = world_.CreateEntity();
    world_.AddComponent<NameComponent>(child, "Child");
    auto& childTransform = world_.AddComponent<TransformComponent>(child);
    childTransform.position[0] = 1.0f;
    childTransform.position[1] = 0.0f;
    childTransform.position[2] = 0.0f;
    childTransform.parent = parent;

    auto& childRenderer = world_.AddComponent<MeshRendererComponent>(child);
    childRenderer.submeshIndex = 1;

    // Verify hierarchy
    auto* childTransformPtr = world_.GetComponent<TransformComponent>(child);
    ASSERT_NE(childTransformPtr, nullptr);
    EXPECT_EQ(childTransformPtr->parent, parent);

    // Query all entities with transform and mesh renderer
    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();

    // Should find both parent and child
    EXPECT_EQ(renderables.size(), 2u);

    bool foundParent = std::find(renderables.begin(), renderables.end(), parent) != renderables.end();
    bool foundChild = std::find(renderables.begin(), renderables.end(), child) != renderables.end();

    EXPECT_TRUE(foundParent);
    EXPECT_TRUE(foundChild);
}

// Test component removal with asset references
TEST_F(AssetIntegrationTest, ComponentRemovalWithAssets) {
    // Create entity with mesh renderer
    Entity entity = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(entity);
    world_.AddComponent<MeshRendererComponent>(entity);

    // Verify components exist
    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
    EXPECT_TRUE(world_.HasComponent<MeshRendererComponent>(entity));

    // Remove mesh renderer component
    world_.RemoveComponent<MeshRendererComponent>(entity);

    // Verify mesh renderer was removed but transform remains
    EXPECT_TRUE(world_.HasComponent<TransformComponent>(entity));
    EXPECT_FALSE(world_.HasComponent<MeshRendererComponent>(entity));

    // Query for renderable entities should now be empty
    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(renderables.size(), 0u);
}

// Test entity destruction with assets
TEST_F(AssetIntegrationTest, EntityDestructionWithAssets) {
    // Create entities with assets
    Entity e1 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e1);
    world_.AddComponent<MeshRendererComponent>(e1);

    Entity e2 = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(e2);
    world_.AddComponent<MeshRendererComponent>(e2);

    // Verify both exist
    EXPECT_TRUE(world_.IsEntityValid(e1));
    EXPECT_TRUE(world_.IsEntityValid(e2));

    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(renderables.size(), 2u);

    // Destroy one entity
    world_.DestroyEntity(e1);

    // Verify e1 is destroyed, e2 still exists
    EXPECT_FALSE(world_.IsEntityValid(e1));
    EXPECT_TRUE(world_.IsEntityValid(e2));

    // Query should now return only e2
    renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(renderables.size(), 1u);

    bool foundE2 = std::find(renderables.begin(), renderables.end(), e2) != renderables.end();
    EXPECT_TRUE(foundE2);
}

// Test multiple entities with same asset
TEST_F(AssetIntegrationTest, MultipleEntitiesWithSameAsset) {
    // Create multiple entities sharing the same mesh
    const int entityCount = 10;
    std::vector<Entity> entities;

    for (int i = 0; i < entityCount; ++i) {
        Entity e = world_.CreateEntity();
        world_.AddComponent<TransformComponent>(e);
        auto& renderer = world_.AddComponent<MeshRendererComponent>(e);

        // All entities reference the same mesh asset
        renderer.mesh = MeshHandle();
        renderer.submeshIndex = 0;

        // Position entities differently
        auto* transform = world_.GetComponent<TransformComponent>(e);
        transform->position[0] = static_cast<float>(i) * 2.0f;

        entities.push_back(e);
    }

    // Verify all entities created
    EXPECT_EQ(world_.GetEntityCount(), static_cast<size_t>(entityCount));

    // Query all renderable entities
    auto renderables = world_.QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
    EXPECT_EQ(renderables.size(), static_cast<size_t>(entityCount));

    // Verify each entity has unique transform
    for (int i = 0; i < entityCount; ++i) {
        auto* transform = world_.GetComponent<TransformComponent>(entities[i]);
        ASSERT_NE(transform, nullptr);
        EXPECT_FLOAT_EQ(transform->position[0], static_cast<float>(i) * 2.0f);
    }
}

// Test world update with renderable entities
TEST_F(AssetIntegrationTest, WorldUpdateWithRenderables) {
    // Create renderable entity
    Entity entity = world_.CreateEntity();
    world_.AddComponent<TransformComponent>(entity);
    world_.AddComponent<MeshRendererComponent>(entity);

    // Register a test system
    class TestRenderSystem : public System {
    public:
        int updateCount = 0;
        std::vector<Entity> renderables;

        void Update(float deltaTime) override {
            updateCount++;
            // Query for renderable entities
            renderables = world_->QueryEntitiesWith<TransformComponent, MeshRendererComponent>();
        }

        const char* GetName() const override { return "TestRenderSystem"; }
    };

    TestRenderSystem system;
    world_.RegisterSystem(&system);

    // Update world
    world_.Update(0.016f);

    // Verify system was updated
    EXPECT_EQ(system.updateCount, 1);

    // Verify system found renderable entities
    EXPECT_EQ(system.renderables.size(), 1u);
    EXPECT_EQ(system.renderables[0], entity);

    // Unregister system before world destruction
    world_.UnregisterSystem(&system);
}

} // namespace testing
} // namespace Next
