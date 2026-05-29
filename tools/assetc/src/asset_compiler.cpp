#include "asset_compiler.h"
#include "obj_loader.h"
#include "tga_loader.h"
#include "next/compression/compression.h"
#include "next/foundation/logger.h"
#include "next/streaming/cell_file_format.h"
#include <cctype>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_set>

namespace Next {

AssetCompiler::AssetCompiler() {
    NEXT_LOG_DEBUG("AssetCompiler created");
}

AssetCompiler::~AssetCompiler() {
    NEXT_LOG_DEBUG("AssetCompiler destroyed");
}

namespace {

static std::string AssetNameFromPath(const std::string& sourcePath, const std::string& outputPath) {
    std::filesystem::path out(outputPath);
    if (!out.stem().empty()) {
        return out.stem().string();
    }
    std::filesystem::path in(sourcePath);
    return in.stem().string();
}

static size_t FixedStringLength(const char* value, size_t maxSize) {
    const void* terminator = std::memchr(value, '\0', maxSize);
    if (!terminator) {
        return maxSize;
    }
    return static_cast<const char*>(terminator) - value;
}

static void CopyFixedStringBytes(char* dst, size_t dstSize, const char* src, size_t srcSize) {
    if (!dst || dstSize == 0) {
        return;
    }

    std::memset(dst, 0, dstSize);
    if (!src || srcSize == 0) {
        return;
    }

    const size_t copySize = std::min(dstSize - 1, srcSize);
    std::memcpy(dst, src, copySize);
}

template<size_t DstSize>
static void CopyFixedString(char (&dst)[DstSize], const std::string& value) {
    CopyFixedStringBytes(dst, DstSize, value.data(), value.size());
}

template<size_t DstSize, size_t SrcSize>
static void CopyFixedString(char (&dst)[DstSize], const char (&value)[SrcSize]) {
    CopyFixedStringBytes(dst, DstSize, value, FixedStringLength(value, SrcSize));
}

static bool WriteBytes(const std::string& outputPath, const std::vector<uint8_t>& bytes) {
    std::ofstream file(outputPath, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    file.close();
    return file.good();
}

static bool CheckedMulSize(size_t a, size_t b, size_t& out) {
    if (a != 0 && b > std::numeric_limits<size_t>::max() / a) {
        return false;
    }
    out = a * b;
    return true;
}

static bool CheckedAddSize(size_t a, size_t b, size_t& out) {
    if (a > std::numeric_limits<size_t>::max() - b) {
        return false;
    }
    out = a + b;
    return true;
}

static size_t AssetPayloadOffset(AssetType type) {
    switch (type) {
        case AssetType::Mesh:
            return sizeof(MeshHeader);
        case AssetType::Texture:
            return sizeof(TextureHeader);
        case AssetType::Material:
            return sizeof(MaterialHeader);
        default:
            return 0;
    }
}

template<typename HeaderT>
static bool StampAssetChecksum(std::vector<uint8_t>& bytes) {
    if (bytes.size() < sizeof(HeaderT)) {
        return false;
    }

    HeaderT header{};
    std::memcpy(&header, bytes.data(), sizeof(HeaderT));

    const size_t payloadSize = bytes.size() - sizeof(HeaderT);
    if (header.common.dataSize != payloadSize) {
        return false;
    }

    header.common.checksum = CalculateCRC64(bytes.data() + sizeof(HeaderT), payloadSize);
    std::memcpy(bytes.data(), &header, sizeof(HeaderT));
    return true;
}

static bool ValidateCompiledAssetChecksum(const AssetHeader& header, const std::vector<uint8_t>& bytes) {
    if (header.checksum == 0) {
        return true;
    }

    const size_t payloadOffset = AssetPayloadOffset(header.assetType);
    if (payloadOffset == 0 || bytes.size() < payloadOffset) {
        return false;
    }

    const size_t payloadSize = bytes.size() - payloadOffset;
    if (payloadSize != header.dataSize) {
        return false;
    }

    return ValidateAssetChecksum(header, bytes.data() + payloadOffset);
}

static bool GetPackageInputFileSize(const std::string& assetFile, uint64_t& outFileSize) {
    outFileSize = 0;
    std::error_code ec;
    const uintmax_t fileSize = std::filesystem::file_size(assetFile, ec);
    if (ec) {
        NEXT_LOG_ERROR("Failed to stat asset file: %s (%s)", assetFile.c_str(), ec.message().c_str());
        return false;
    }

    constexpr uint64_t maxPackageField = std::numeric_limits<uint32_t>::max();
    if (fileSize > static_cast<uintmax_t>(maxPackageField)) {
        NEXT_LOG_ERROR("Asset file too large for package entry: %s (%ju bytes)",
                       assetFile.c_str(),
                       static_cast<uintmax_t>(fileSize));
        return false;
    }

    outFileSize = static_cast<uint64_t>(fileSize);
    return true;
}

static std::vector<uint8_t> BuildPackagePayload(
    const std::vector<AssetEntry>& entries,
    const std::vector<std::vector<uint8_t>>& assetData) {
    size_t payloadSize = sizeof(AssetEntry) * entries.size();
    for (const auto& data : assetData) {
        payloadSize += data.size();
    }

    std::vector<uint8_t> payload(payloadSize);
    uint8_t* ptr = payload.data();
    const size_t indexBytes = sizeof(AssetEntry) * entries.size();
    if (indexBytes != 0) {
        std::memcpy(ptr, entries.data(), indexBytes);
        ptr += indexBytes;
    }

    for (const auto& data : assetData) {
        if (!data.empty()) {
            std::memcpy(ptr, data.data(), data.size());
            ptr += data.size();
        }
    }

    return payload;
}

// Extract the outgoing resource dependencies declared by an asset blob. Today only
// materials carry references (their TextureRef table); meshes and textures are leaves.
static std::vector<std::string> ExtractAssetDependencies(const std::vector<uint8_t>& blob,
                                                         const AssetHeader& header) {
    std::vector<std::string> deps;
    if (header.assetType != AssetType::Material || blob.size() < sizeof(MaterialHeader)) {
        return deps;
    }

    MaterialHeader material{};
    std::memcpy(&material, blob.data(), sizeof(MaterialHeader));

    // Bounds-check the texture-ref table before walking it.
    const size_t refBytes = static_cast<size_t>(material.textureCount) * sizeof(TextureRef);
    if (material.textureCount != 0 && refBytes / sizeof(TextureRef) != material.textureCount) {
        return deps; // multiplication overflow
    }
    if (blob.size() < sizeof(MaterialHeader) + refBytes) {
        return deps;
    }

    const uint8_t* refBase = blob.data() + sizeof(MaterialHeader);
    deps.reserve(material.textureCount);
    for (uint32_t i = 0; i < material.textureCount; ++i) {
        TextureRef ref{};
        std::memcpy(&ref, refBase + static_cast<size_t>(i) * sizeof(TextureRef), sizeof(TextureRef));
        char nameBuf[sizeof(ref.name) + 1] = {};
        std::memcpy(nameBuf, ref.name, sizeof(ref.name));
        if (nameBuf[0] != '\0') {
            deps.emplace_back(nameBuf);
        }
    }
    return deps;
}

static std::string JsonEscape(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    for (char c : value) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out += ' '; // collapse control characters; asset names are printable in practice
        } else {
            out += c;
        }
    }
    return out;
}

static const char* PackageCompressionName(uint32_t algorithm) {
    switch (static_cast<Compression::Algorithm>(algorithm)) {
        case Compression::Algorithm::LZ4:  return "lz4";
        case Compression::Algorithm::Zstd: return "zstd";
        default:                           return "none";
    }
}

// Emit a human-readable, queryable sidecar describing the package: per-asset size,
// compression and (forward) dependency edges, plus dangling-reference warnings. This is
// the first slice of the "resource graph + tools emit stats/deps/validation" discipline.
static void WritePackageManifest(const std::string& packagePath,
                                 const std::string& packageName,
                                 const std::vector<AssetEntry>& entries,
                                 const std::vector<std::vector<std::string>>& dependencies) {
    std::unordered_set<std::string> present;
    for (const auto& entry : entries) {
        present.insert(std::string(entry.name));
    }

    // The stable asset id is CRC64 of the canonical "<package-key>::<name>" string, where
    // package-key is the file stem the runtime registers the package under (AssetManager
    // resolves storage keys as "<stem>::<localName>"). Emitting it here makes the manifest
    // the bridge between offline cook identity and the runtime AssetHandle id.
    const std::string packageKeyPrefix =
        std::filesystem::path(packagePath).stem().string() + "::";

    uint64_t totalStored = 0;
    uint64_t totalDecompressed = 0;
    for (const auto& entry : entries) {
        totalStored += entry.assetSize;
        totalDecompressed += entry.decompressedSize;
    }

    // A referenced asset absent from this package is only a warning: it may legitimately
    // live in another package loaded at runtime. A future global cook can promote this.
    std::vector<std::string> warnings;
    for (size_t i = 0; i < entries.size(); ++i) {
        for (const std::string& dep : dependencies[i]) {
            if (present.find(dep) == present.end()) {
                warnings.push_back("Asset '" + std::string(entries[i].name) + "' references '" +
                                   dep + "' which is not present in package '" + packageName +
                                   "' (may be provided by another package)");
            }
        }
    }

    std::string json;
    json += "{\n";
    json += "  \"package\": \"" + JsonEscape(packageName) + "\",\n";
    json += "  \"formatVersion\": " + std::to_string(PackageHeader::CURRENT_VERSION) + ",\n";
    json += "  \"assetCount\": " + std::to_string(entries.size()) + ",\n";
    json += "  \"totalStoredBytes\": " + std::to_string(totalStored) + ",\n";
    json += "  \"totalDecompressedBytes\": " + std::to_string(totalDecompressed) + ",\n";
    json += "  \"assets\": [\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        const AssetEntry& entry = entries[i];
        const std::string assetKey = packageKeyPrefix + std::string(entry.name);
        uint64_t assetId = CalculateCRC64(assetKey.data(), assetKey.size());
        if (assetId == 0) {
            assetId = ~0ull; // mirror the runtime's reserved-zero remap
        }
        json += "    {\n";
        json += "      \"name\": \"" + JsonEscape(std::string(entry.name)) + "\",\n";
        json += "      \"id\": \"" + std::to_string(assetId) + "\",\n";
        json += "      \"type\": \"" + std::string(AssetTypeToString(entry.assetType)) + "\",\n";
        json += "      \"compression\": \"" + std::string(PackageCompressionName(entry.compressionAlgorithm)) + "\",\n";
        json += "      \"dataOffset\": " + std::to_string(entry.dataOffset) + ",\n";
        json += "      \"storedBytes\": " + std::to_string(entry.assetSize) + ",\n";
        json += "      \"decompressedBytes\": " + std::to_string(entry.decompressedSize) + ",\n";
        json += "      \"dependencies\": [";
        for (size_t d = 0; d < dependencies[i].size(); ++d) {
            if (d != 0) {
                json += ", ";
            }
            json += "\"" + JsonEscape(dependencies[i][d]) + "\"";
        }
        json += "]\n";
        json += (i + 1 < entries.size()) ? "    },\n" : "    }\n";
    }
    json += "  ],\n";
    json += "  \"warnings\": [";
    for (size_t w = 0; w < warnings.size(); ++w) {
        json += (w == 0) ? "\n    \"" : ",\n    \"";
        json += JsonEscape(warnings[w]) + "\"";
    }
    json += warnings.empty() ? "]\n" : "\n  ]\n";
    json += "}\n";

    const std::string manifestPath = packagePath + ".manifest.json";
    std::ofstream out(manifestPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        NEXT_LOG_ERROR("Failed to open package manifest for writing: %s", manifestPath.c_str());
        return;
    }
    out.write(json.data(), static_cast<std::streamsize>(json.size()));
    if (!out.good()) {
        NEXT_LOG_ERROR("Failed while writing package manifest: %s", manifestPath.c_str());
        return;
    }
    for (const std::string& warning : warnings) {
        NEXT_LOG_WARNING("%s", warning.c_str());
    }
    NEXT_LOG_INFO("Wrote package manifest: %s (%zu assets, %zu warnings)",
                  manifestPath.c_str(), entries.size(), warnings.size());
}

} // namespace

bool AssetCompiler::CompileMesh(const std::string& sourcePath, const std::string& outputPath) {
    NEXT_LOG_INFO("Compiling mesh: %s -> %s", sourcePath.c_str(), outputPath.c_str());

    const std::filesystem::path src(sourcePath);
    const std::string ext = src.extension().string();

    if (ext != ".obj") {
        NEXT_LOG_ERROR("CompileMesh: only .obj is supported right now (got %s). Export OBJ from DCC as a workaround.", ext.c_str());
        return false;
    }

    ObjMesh mesh;
    std::string err;
    if (!LoadObjMesh(sourcePath, mesh, err)) {
        NEXT_LOG_ERROR("CompileMesh: OBJ import failed: %s", err.c_str());
        return false;
    }

    constexpr size_t maxAssetField = std::numeric_limits<uint32_t>::max();
    const size_t vertexCount = mesh.vertices.size();
    const size_t indexCount = mesh.indices.size();
    if (vertexCount > maxAssetField || indexCount > maxAssetField) {
        NEXT_LOG_ERROR("CompileMesh: mesh is too large for asset header fields: %s", sourcePath.c_str());
        return false;
    }

    const uint32_t indexType =
        (vertexCount > static_cast<size_t>(std::numeric_limits<uint16_t>::max())) ? 1u : 0u;
    const size_t indexStride = (indexType == 0) ? sizeof(uint16_t) : sizeof(uint32_t);

    size_t vertexBytes = 0;
    size_t indexBytes = 0;
    size_t payloadBytes = 0;
    size_t totalBytes = 0;
    const size_t submeshBytes = sizeof(uint32_t) * 2u;
    if (!CheckedMulSize(vertexCount, sizeof(ObjMeshVertex), vertexBytes) ||
        !CheckedMulSize(indexCount, indexStride, indexBytes) ||
        !CheckedAddSize(vertexBytes, indexBytes, payloadBytes) ||
        !CheckedAddSize(payloadBytes, submeshBytes, payloadBytes) ||
        payloadBytes > maxAssetField ||
        !CheckedAddSize(sizeof(MeshHeader), payloadBytes, totalBytes)) {
        NEXT_LOG_ERROR("CompileMesh: mesh payload is too large: %s", sourcePath.c_str());
        return false;
    }

    MeshHeader header;
    std::memset(&header, 0, sizeof(header));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Mesh;
    CopyFixedString(header.common.name, AssetNameFromPath(sourcePath, outputPath));

    header.vertexCount = static_cast<uint32_t>(vertexCount);
    header.indexCount = static_cast<uint32_t>(indexCount);
    header.vertexStride = sizeof(ObjMeshVertex); // pos + normal + uv
    header.indexType = indexType;
    header.vertexFormat = 0;
    header.boundingBox[0] = mesh.boundsMin[0];
    header.boundingBox[1] = mesh.boundsMin[1];
    header.boundingBox[2] = mesh.boundsMin[2];
    header.boundingBox[3] = mesh.boundsMax[0];
    header.boundingBox[4] = mesh.boundsMax[1];
    header.boundingBox[5] = mesh.boundsMax[2];
    header.materialCount = 1;
    header.flags = 0;
    if (mesh.hasNormals) header.flags |= MeshHeader::HAS_NORMALS;
    if (mesh.hasUVs) header.flags |= MeshHeader::HAS_UVS;

    const uint32_t submeshRange[2] = {0u, header.indexCount};

    header.common.dataSize = static_cast<uint32_t>(payloadBytes);
    header.common.checksum = 0;

    std::vector<uint8_t> out;
    out.resize(totalBytes);
    uint8_t* ptr = out.data();
    std::memcpy(ptr, &header, sizeof(MeshHeader));
    ptr += sizeof(MeshHeader);

    std::memcpy(ptr, mesh.vertices.data(), vertexBytes);
    ptr += vertexBytes;

    if (header.indexType == 0) {
        std::vector<uint16_t> idx16;
        idx16.reserve(mesh.indices.size());
        for (uint32_t v : mesh.indices) {
            if (v > std::numeric_limits<uint16_t>::max()) {
                NEXT_LOG_ERROR("CompileMesh: uint16 index out of range for %s", sourcePath.c_str());
                return false;
            }
            idx16.push_back(static_cast<uint16_t>(v));
        }
        std::memcpy(ptr, idx16.data(), idx16.size() * sizeof(uint16_t));
        ptr += idx16.size() * sizeof(uint16_t);
    } else {
        std::memcpy(ptr, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
        ptr += mesh.indices.size() * sizeof(uint32_t);
    }

    std::memcpy(ptr, submeshRange, submeshBytes);

    if (!StampAssetChecksum<MeshHeader>(out)) {
        NEXT_LOG_ERROR("CompileMesh: failed to stamp checksum for %s", outputPath.c_str());
        return false;
    }

    if (!WriteBytes(outputPath, out)) {
        NEXT_LOG_ERROR("CompileMesh: failed to write %s", outputPath.c_str());
        return false;
    }
    return true;
}

bool AssetCompiler::CompileTexture(const std::string& sourcePath, const std::string& outputPath) {
    NEXT_LOG_INFO("Compiling texture: %s -> %s", sourcePath.c_str(), outputPath.c_str());

    const std::filesystem::path src(sourcePath);
    const std::string ext = src.extension().string();
    if (ext != ".tga") {
        NEXT_LOG_ERROR("CompileTexture: only .tga is supported right now (got %s). Export TGA as a workaround.", ext.c_str());
        return false;
    }

    ImageRGBA8 img;
    std::string err;
    if (!LoadTgaRGBA8(sourcePath, img, err)) {
        NEXT_LOG_ERROR("CompileTexture: TGA import failed: %s", err.c_str());
        return false;
    }

    TextureHeader header;
    std::memset(&header, 0, sizeof(header));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Texture;
    CopyFixedString(header.common.name, AssetNameFromPath(sourcePath, outputPath));

    header.width = static_cast<uint32_t>(img.width);
    header.height = static_cast<uint32_t>(img.height);
    header.depth = 1;
    header.mipLevels = 1;
    header.arraySize = 1;
    header.format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    header.flags = 0;

    const size_t pixelBytes = img.pixels.size();
    header.common.dataSize = static_cast<uint32_t>(pixelBytes);
    header.common.checksum = 0;

    std::vector<uint8_t> out;
    out.resize(sizeof(TextureHeader) + pixelBytes);
    std::memcpy(out.data(), &header, sizeof(TextureHeader));
    std::memcpy(out.data() + sizeof(TextureHeader), img.pixels.data(), pixelBytes);

    if (!StampAssetChecksum<TextureHeader>(out)) {
        NEXT_LOG_ERROR("CompileTexture: failed to stamp checksum for %s", outputPath.c_str());
        return false;
    }

    if (!WriteBytes(outputPath, out)) {
        NEXT_LOG_ERROR("CompileTexture: failed to write %s", outputPath.c_str());
        return false;
    }

    return true;
}

bool AssetCompiler::CompileMaterial(const std::string& sourcePath, const std::string& outputPath) {
    NEXT_LOG_INFO("Compiling material: %s -> %s", sourcePath.c_str(), outputPath.c_str());
    
    // For CP3, generate a test material
    std::vector<uint8_t> materialData = GenerateTestMaterial();
    return WriteAssetFile(outputPath, materialData.data(), materialData.size());
}

bool AssetCompiler::CompileCell(
    const std::string& sourcePath,
    const std::string& outputPath,
    const std::string& compression) {
    NEXT_LOG_INFO("Compiling streaming cell: %s -> %s (%s)",
                  sourcePath.c_str(), outputPath.c_str(), compression.c_str());

    std::vector<uint8_t> payload;
    if (!ReadAssetFile(sourcePath, payload)) {
        NEXT_LOG_ERROR("CompileCell: failed to read source payload: %s", sourcePath.c_str());
        return false;
    }
    if (payload.empty()) {
        NEXT_LOG_ERROR("CompileCell: source payload is empty: %s", sourcePath.c_str());
        return false;
    }

    std::string mode = compression;
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    Streaming::CellFileCompression cellCompression = Streaming::CellFileCompression::None;
    Compression::Algorithm algorithm = Compression::Algorithm::None;
    bool shouldCompress = false;
    if (mode == "none" || mode == "raw" || mode.empty()) {
        cellCompression = Streaming::CellFileCompression::None;
    } else if (mode == "lz4") {
        cellCompression = Streaming::CellFileCompression::LZ4;
        algorithm = Compression::Algorithm::LZ4;
        shouldCompress = true;
    } else if (mode == "zstd" || mode == "zstandard") {
        cellCompression = Streaming::CellFileCompression::Zstd;
        algorithm = Compression::Algorithm::Zstd;
        shouldCompress = true;
    } else {
        NEXT_LOG_ERROR("CompileCell: unsupported compression mode: %s", compression.c_str());
        return false;
    }

    std::vector<uint8_t> body;
    if (shouldCompress) {
        if (!Compression::IsAvailable(algorithm)) {
            NEXT_LOG_ERROR("CompileCell: %s backend is unavailable", Compression::AlgorithmName(algorithm));
            return false;
        }
        const uint64_t bound = Compression::CompressBound(algorithm, payload.size());
        if (bound == 0) {
            NEXT_LOG_ERROR("CompileCell: invalid compression bound for %s", Compression::AlgorithmName(algorithm));
            return false;
        }
        body.resize(static_cast<size_t>(bound));
        const Compression::Result result = Compression::Compress(
            algorithm,
            payload.data(),
            payload.size(),
            body.data(),
            body.size());
        if (!result.Succeeded()) {
            NEXT_LOG_ERROR("CompileCell: compression failed: %s", result.message.c_str());
            return false;
        }
        body.resize(static_cast<size_t>(result.bytesWritten));
    } else {
        body = payload;
    }

    const Streaming::CellFileHeader header = Streaming::MakeCellFileHeader(
        cellCompression,
        body.size(),
        payload.size());

    std::vector<uint8_t> out(sizeof(header) + body.size());
    std::memcpy(out.data(), &header, sizeof(header));
    std::memcpy(out.data() + sizeof(header), body.data(), body.size());

    if (!WriteBytes(outputPath, out)) {
        NEXT_LOG_ERROR("CompileCell: failed to write %s", outputPath.c_str());
        return false;
    }

    NEXT_LOG_INFO("Compiled cell payload: %zu -> %zu bytes", payload.size(), body.size());
    return true;
}

bool AssetCompiler::CreatePackage(const std::string& packageName,
                                 const std::vector<std::string>& assetFiles,
                                 const std::string& outputPath,
                                 const std::string& compression) {
    NEXT_LOG_INFO("Creating package: %s with %zu assets (compression=%s)",
                  packageName.c_str(), assetFiles.size(), compression.c_str());

    if (assetFiles.empty()) {
        NEXT_LOG_ERROR("No assets specified for package");
        return false;
    }

    // Resolve the requested compression mode. "auto" prefers LZ4, then Zstd, and falls
    // back to storing uncompressed when no codec is available or compression doesn't help.
    std::string mode = compression;
    std::transform(mode.begin(), mode.end(), mode.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    Compression::Algorithm packageAlgorithm = Compression::Algorithm::None;
    if (mode == "none" || mode == "raw" || mode.empty()) {
        packageAlgorithm = Compression::Algorithm::None;
    } else if (mode == "lz4") {
        packageAlgorithm = Compression::Algorithm::LZ4;
    } else if (mode == "zstd" || mode == "zstandard") {
        packageAlgorithm = Compression::Algorithm::Zstd;
    } else if (mode == "auto") {
        if (Compression::IsAvailable(Compression::Algorithm::LZ4)) {
            packageAlgorithm = Compression::Algorithm::LZ4;
        } else if (Compression::IsAvailable(Compression::Algorithm::Zstd)) {
            packageAlgorithm = Compression::Algorithm::Zstd;
        }
    } else {
        NEXT_LOG_ERROR("Unsupported package compression mode: %s", compression.c_str());
        return false;
    }
    if (packageAlgorithm != Compression::Algorithm::None &&
        !Compression::IsAvailable(packageAlgorithm)) {
        NEXT_LOG_WARNING("Compression codec %s unavailable; storing package uncompressed",
                         Compression::AlgorithmName(packageAlgorithm));
        packageAlgorithm = Compression::Algorithm::None;
    }
    
    constexpr uint64_t maxPackageField = std::numeric_limits<uint32_t>::max();
    const uint64_t maxIndexBytes = maxPackageField - sizeof(PackageHeader);
    if (assetFiles.size() > maxIndexBytes / sizeof(AssetEntry)) {
        NEXT_LOG_ERROR("Too many assets for package index: %zu", assetFiles.size());
        return false;
    }

    std::vector<uint64_t> assetFileSizes;
    assetFileSizes.reserve(assetFiles.size());
    uint64_t packageDataBytes = 0;
    for (const auto& assetFile : assetFiles) {
        uint64_t fileSize = 0;
        if (!GetPackageInputFileSize(assetFile, fileSize)) {
            return false;
        }
        if (packageDataBytes > maxPackageField - fileSize) {
            NEXT_LOG_ERROR("Package data section is too large after adding asset file: %s",
                           assetFile.c_str());
            return false;
        }
        packageDataBytes += fileSize;
        assetFileSizes.push_back(fileSize);
    }

    std::vector<std::vector<uint8_t>> assetData;
    std::vector<AssetEntry> entries;
    std::vector<std::vector<std::string>> assetDependencies; // parallel to entries
    std::unordered_set<std::string> assetNames;
    
    uint64_t dataOffset = 0;
    
    for (size_t assetIndex = 0; assetIndex < assetFiles.size(); ++assetIndex) {
        const std::string& assetFile = assetFiles[assetIndex];
        const uint64_t expectedFileSize = assetFileSizes[assetIndex];
        std::vector<uint8_t> data;
        if (!ReadAssetFile(assetFile, data)) {
            NEXT_LOG_ERROR("Failed to read asset file: %s", assetFile.c_str());
            return false;
        }
        if (data.size() != expectedFileSize) {
            NEXT_LOG_ERROR("Asset file changed while packaging: %s", assetFile.c_str());
            return false;
        }
        
        if (data.size() < sizeof(AssetHeader)) {
            NEXT_LOG_ERROR("Asset file too small: %s", assetFile.c_str());
            return false;
        }
        
        // Extract asset info from header
        AssetHeader header;
        memcpy(&header, data.data(), sizeof(AssetHeader));
        
        if (!header.Validate()) {
            NEXT_LOG_ERROR("Invalid asset header in: %s", assetFile.c_str());
            return false;
        }

        if (!ValidateCompiledAssetChecksum(header, data)) {
            NEXT_LOG_ERROR("Checksum validation failed for asset file: %s", assetFile.c_str());
            return false;
        }

        // Extract forward dependencies from the uncompressed blob before it is (maybe) moved.
        std::vector<std::string> deps = ExtractAssetDependencies(data, header);

        // Determine the stored (optionally compressed) bytes for this asset blob.
        const uint32_t decompressedSize = static_cast<uint32_t>(data.size());
        std::vector<uint8_t> storedBytes;
        Compression::Algorithm entryAlgorithm = Compression::Algorithm::None;

        if (packageAlgorithm != Compression::Algorithm::None) {
            const uint64_t bound = Compression::CompressBound(packageAlgorithm, data.size());
            if (bound != 0) {
                std::vector<uint8_t> compressed(static_cast<size_t>(bound));
                const Compression::Result result = Compression::Compress(
                    packageAlgorithm,
                    data.data(),
                    data.size(),
                    compressed.data(),
                    compressed.size());
                // Keep compression only when it actually shrinks the blob.
                if (result.Succeeded() && result.bytesWritten < data.size()) {
                    compressed.resize(static_cast<size_t>(result.bytesWritten));
                    storedBytes = std::move(compressed);
                    entryAlgorithm = packageAlgorithm;
                }
            }
        }
        if (entryAlgorithm == Compression::Algorithm::None) {
            storedBytes = std::move(data);
        }

        const uint32_t storedSize = static_cast<uint32_t>(storedBytes.size());

        AssetEntry entry{};
        entry.assetType = header.assetType;
        entry.assetSize = storedSize;
        entry.dataOffset = static_cast<uint32_t>(dataOffset);
        entry.compressionAlgorithm = static_cast<uint32_t>(entryAlgorithm);
        entry.compressedSize = (entryAlgorithm == Compression::Algorithm::None) ? 0u : storedSize;
        entry.decompressedSize = decompressedSize;
        CopyFixedString(entry.name, header.name);

        const std::string assetName(entry.name);
        if (!assetNames.insert(assetName).second) {
            NEXT_LOG_ERROR("Duplicate asset name in package input: %s", assetName.c_str());
            return false;
        }

        if (entryAlgorithm != Compression::Algorithm::None) {
            NEXT_LOG_DEBUG("  Compressed %s: %u -> %u bytes (%s)",
                           entry.name, decompressedSize, storedSize,
                           Compression::AlgorithmName(entryAlgorithm));
        }

        entries.push_back(entry);
        assetData.push_back(std::move(storedBytes));
        assetDependencies.push_back(std::move(deps));

        dataOffset += storedSize;
    }
    
    // Create package header
    PackageHeader packageHeader{};
    packageHeader.magic = PackageHeader::MAGIC;
    packageHeader.version = PackageHeader::CURRENT_VERSION;
    packageHeader.assetCount = static_cast<uint32_t>(assetFiles.size());
    packageHeader.indexOffset = sizeof(PackageHeader);
    packageHeader.dataOffset = packageHeader.indexOffset + sizeof(AssetEntry) * packageHeader.assetCount;
    packageHeader.checksum = 0;
    CopyFixedString(packageHeader.name, packageName);
    
    if (!WritePackage(outputPath, packageHeader, entries, assetData)) {
        return false;
    }

    // Emit the queryable manifest sidecar (stats + forward dependency edges + warnings).
    WritePackageManifest(outputPath, packageName, entries, assetDependencies);
    return true;
}

bool AssetCompiler::GenerateTestAssets(const std::string& outputDir) {
    NEXT_LOG_INFO("Generating test assets in: %s", outputDir.c_str());
    
    // Create output directory if it doesn't exist
    std::filesystem::create_directories(outputDir);
    
    // Generate test assets
    std::vector<uint8_t> meshData = GenerateTestMesh();
    std::vector<uint8_t> textureData = GenerateTestTexture();
    std::vector<uint8_t> normalData = GenerateDefaultNormalTexture();
    std::vector<uint8_t> metallicRoughnessData = GenerateDefaultMetallicRoughnessTexture();
    std::vector<uint8_t> emissiveData = GenerateDefaultEmissiveTexture();
    std::vector<uint8_t> occlusionData = GenerateDefaultOcclusionTexture();
    std::vector<uint8_t> materialData = GenerateTestMaterial();
    
    // Write individual asset files
    std::string meshPath = outputDir + "/test_cube.mesh";
    std::string texturePath = outputDir + "/test_checker.texture";
    std::string normalPath = outputDir + "/default_normal.texture";
    std::string metallicRoughnessPath = outputDir + "/default_metallic_roughness.texture";
    std::string emissivePath = outputDir + "/default_emissive.texture";
    std::string occlusionPath = outputDir + "/default_occlusion.texture";
    std::string materialPath = outputDir + "/test_pbr.material";
    
    if (!WriteAssetFile(meshPath, meshData.data(), meshData.size())) {
        NEXT_LOG_ERROR("Failed to write mesh asset");
        return false;
    }
    
    if (!WriteAssetFile(texturePath, textureData.data(), textureData.size())) {
        NEXT_LOG_ERROR("Failed to write texture asset");
        return false;
    }

    if (!WriteAssetFile(normalPath, normalData.data(), normalData.size())) {
        NEXT_LOG_ERROR("Failed to write default normal texture asset");
        return false;
    }

    if (!WriteAssetFile(metallicRoughnessPath,
                        metallicRoughnessData.data(),
                        metallicRoughnessData.size())) {
        NEXT_LOG_ERROR("Failed to write default metallic roughness texture asset");
        return false;
    }

    if (!WriteAssetFile(emissivePath, emissiveData.data(), emissiveData.size())) {
        NEXT_LOG_ERROR("Failed to write default emissive texture asset");
        return false;
    }

    if (!WriteAssetFile(occlusionPath, occlusionData.data(), occlusionData.size())) {
        NEXT_LOG_ERROR("Failed to write default occlusion texture asset");
        return false;
    }
    
    if (!WriteAssetFile(materialPath, materialData.data(), materialData.size())) {
        NEXT_LOG_ERROR("Failed to write material asset");
        return false;
    }
    
    // Create a package containing all test assets
    std::vector<std::string> assetFiles = {
        meshPath,
        texturePath,
        normalPath,
        metallicRoughnessPath,
        emissivePath,
        occlusionPath,
        materialPath};
    std::string packagePath = outputDir + "/test_package.npkg";

    // Sample/test fixtures are stored uncompressed so packaging is deterministic and
    // byte offsets are stable for the asset-pipeline tests.
    if (!CreatePackage("TestPackage", assetFiles, packagePath, "none")) {
        NEXT_LOG_ERROR("Failed to create test package");
        return false;
    }
    
    NEXT_LOG_INFO("Test assets generated successfully");
    NEXT_LOG_INFO("  Mesh: %s", meshPath.c_str());
    NEXT_LOG_INFO("  Texture: %s", texturePath.c_str());
    NEXT_LOG_INFO("  Normal: %s", normalPath.c_str());
    NEXT_LOG_INFO("  MetallicRoughness: %s", metallicRoughnessPath.c_str());
    NEXT_LOG_INFO("  Emissive: %s", emissivePath.c_str());
    NEXT_LOG_INFO("  Occlusion: %s", occlusionPath.c_str());
    NEXT_LOG_INFO("  Material: %s", materialPath.c_str());
    NEXT_LOG_INFO("  Package: %s", packagePath.c_str());
    
    return true;
}

bool AssetCompiler::WriteAssetFile(const std::string& path, const void* data, size_t size) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        NEXT_LOG_ERROR("Failed to open file for writing: %s", path.c_str());
        return false;
    }
    
    file.write(reinterpret_cast<const char*>(data), size);
    file.close();
    
    NEXT_LOG_DEBUG("Wrote asset file: %s (%zu bytes)", path.c_str(), size);
    return true;
}

bool AssetCompiler::ReadAssetFile(const std::string& path, std::vector<uint8_t>& data) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        NEXT_LOG_ERROR("Failed to open file for reading: %s", path.c_str());
        return false;
    }
    
    const std::streamsize size = file.tellg();
    if (size < 0) {
        NEXT_LOG_ERROR("Failed to determine asset file size: %s", path.c_str());
        return false;
    }

    file.seekg(0, std::ios::beg);
    if (!file) {
        NEXT_LOG_ERROR("Failed to seek asset file for reading: %s", path.c_str());
        return false;
    }

    data.resize(static_cast<size_t>(size));
    if (size != 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
        NEXT_LOG_ERROR("Failed to read asset file payload: %s", path.c_str());
        data.clear();
        return false;
    }
    file.close();
    
    NEXT_LOG_DEBUG("Read asset file: %s (%zu bytes)", path.c_str(), size);
    return true;
}

std::vector<uint8_t> AssetCompiler::GenerateTestMesh() {
    // Generate a simple cube mesh (8 vertices, 36 indices)
    MeshHeader header;
    memset(&header, 0, sizeof(MeshHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Mesh;
    CopyFixedString(header.common.name, "TestCube");
    
    header.vertexCount = 8;
    header.indexCount = 36;
    header.vertexStride = 32; // Position (3 floats) + Normal (3 floats) + UV (2 floats)
    header.indexType = 0; // uint16
    header.vertexFormat = 0; // Simple format
    header.materialCount = 1;
    header.flags = MeshHeader::HAS_NORMALS | MeshHeader::HAS_UVS;
    
    // Bounding box for unit cube
    header.boundingBox[0] = -0.5f; // minX
    header.boundingBox[1] = -0.5f; // minY
    header.boundingBox[2] = -0.5f; // minZ
    header.boundingBox[3] = 0.5f;  // maxX
    header.boundingBox[4] = 0.5f;  // maxY
    header.boundingBox[5] = 0.5f;  // maxZ
    
    // Simple vertex data (position + normal + uv)
    struct Vertex {
        float pos[3];
        float normal[3];
        float uv[2];
    };
    
    std::vector<Vertex> vertices = {
        {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}}
    };
    
    // Cube indices (12 triangles)
    uint16_t indices[] = {
        0, 1, 2, 2, 3, 0, // front
        4, 5, 6, 6, 7, 4, // back
        0, 4, 7, 7, 3, 0, // left
        1, 5, 6, 6, 2, 1, // right
        3, 2, 6, 6, 7, 3, // top
        0, 1, 5, 5, 4, 0  // bottom
    };
    
    // Submesh range
    uint32_t submeshRange[] = {0, 36};
    
    // Calculate total size
    size_t vertexDataSize = vertices.size() * sizeof(Vertex);
    size_t indexDataSize = sizeof(indices);
    size_t submeshDataSize = sizeof(submeshRange);
    size_t totalSize = sizeof(MeshHeader) + vertexDataSize + indexDataSize + submeshDataSize;
    
    header.common.dataSize = static_cast<uint32_t>(totalSize - sizeof(MeshHeader));
    header.common.checksum = 0;
    
    // Build mesh data
    std::vector<uint8_t> meshData(totalSize);
    uint8_t* ptr = meshData.data();
    
    memcpy(ptr, &header, sizeof(MeshHeader));
    ptr += sizeof(MeshHeader);
    
    memcpy(ptr, vertices.data(), vertexDataSize);
    ptr += vertexDataSize;
    
    memcpy(ptr, indices, indexDataSize);
    ptr += indexDataSize;
    
    memcpy(ptr, submeshRange, submeshDataSize);

    if (!StampAssetChecksum<MeshHeader>(meshData)) {
        NEXT_LOG_ERROR("Generated test mesh checksum failed: %s", header.common.name);
        return {};
    }

    NEXT_LOG_DEBUG("Generated test mesh: %s (%zu bytes)", header.common.name, meshData.size());
    return meshData;
}

std::vector<uint8_t> AssetCompiler::GenerateTestTexture() {
    // Generate a simple 4x4 checkerboard texture
    TextureHeader header;
    memset(&header, 0, sizeof(TextureHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Texture;
    CopyFixedString(header.common.name, "TestChecker");
    
    header.width = 4;
    header.height = 4;
    header.depth = 1;
    header.mipLevels = 1;
    header.arraySize = 1;
    header.format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    header.flags = TextureHeader::SRGB;
    
    // Simple 4x4 checkerboard RGBA data
    uint32_t pixelData[] = {
        0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000,
        0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF,
        0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000,
        0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF
    };
    
    size_t pixelDataSize = sizeof(pixelData);
    size_t totalSize = sizeof(TextureHeader) + pixelDataSize;
    
    header.common.dataSize = static_cast<uint32_t>(pixelDataSize);
    header.common.checksum = 0;
    
    std::vector<uint8_t> textureData(totalSize);
    memcpy(textureData.data(), &header, sizeof(TextureHeader));
    memcpy(textureData.data() + sizeof(TextureHeader), pixelData, pixelDataSize);

    if (!StampAssetChecksum<TextureHeader>(textureData)) {
        NEXT_LOG_ERROR("Generated test texture checksum failed: %s", header.common.name);
        return {};
    }

    NEXT_LOG_DEBUG("Generated test texture: %s (%zu bytes)", header.common.name, textureData.size());
    return textureData;
}

std::vector<uint8_t> AssetCompiler::GenerateDefaultNormalTexture() {
    TextureHeader header;
    memset(&header, 0, sizeof(TextureHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Texture;
    CopyFixedString(header.common.name, "DefaultNormal");

    header.width = 4;
    header.height = 4;
    header.depth = 1;
    header.mipLevels = 1;
    header.arraySize = 1;
    header.format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    header.flags = 0;

    uint32_t pixelData[16];
    std::fill_n(pixelData, 16, 0xFFFF8080u);

    size_t pixelDataSize = sizeof(pixelData);
    size_t totalSize = sizeof(TextureHeader) + pixelDataSize;

    header.common.dataSize = static_cast<uint32_t>(pixelDataSize);
    header.common.checksum = 0;

    std::vector<uint8_t> textureData(totalSize);
    memcpy(textureData.data(), &header, sizeof(TextureHeader));
    memcpy(textureData.data() + sizeof(TextureHeader), pixelData, pixelDataSize);

    if (!StampAssetChecksum<TextureHeader>(textureData)) {
        NEXT_LOG_ERROR("Generated default normal texture checksum failed: %s", header.common.name);
        return {};
    }

    NEXT_LOG_DEBUG("Generated default normal texture: %s (%zu bytes)",
                   header.common.name,
                   textureData.size());
    return textureData;
}

std::vector<uint8_t> AssetCompiler::GenerateDefaultMetallicRoughnessTexture() {
    TextureHeader header;
    memset(&header, 0, sizeof(TextureHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Texture;
    CopyFixedString(header.common.name, "DefaultMetallicRoughness");

    header.width = 4;
    header.height = 4;
    header.depth = 1;
    header.mipLevels = 1;
    header.arraySize = 1;
    header.format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    header.flags = 0;

    uint32_t pixelData[16];
    std::fill_n(pixelData, 16, 0xFF804D00u);

    size_t pixelDataSize = sizeof(pixelData);
    size_t totalSize = sizeof(TextureHeader) + pixelDataSize;

    header.common.dataSize = static_cast<uint32_t>(pixelDataSize);
    header.common.checksum = 0;

    std::vector<uint8_t> textureData(totalSize);
    memcpy(textureData.data(), &header, sizeof(TextureHeader));
    memcpy(textureData.data() + sizeof(TextureHeader), pixelData, pixelDataSize);

    if (!StampAssetChecksum<TextureHeader>(textureData)) {
        NEXT_LOG_ERROR("Generated default metallic roughness texture checksum failed: %s", header.common.name);
        return {};
    }

    NEXT_LOG_DEBUG("Generated default metallic roughness texture: %s (%zu bytes)",
                   header.common.name,
                   textureData.size());
    return textureData;
}

std::vector<uint8_t> AssetCompiler::GenerateDefaultEmissiveTexture() {
    TextureHeader header;
    memset(&header, 0, sizeof(TextureHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Texture;
    CopyFixedString(header.common.name, "DefaultEmissive");

    header.width = 4;
    header.height = 4;
    header.depth = 1;
    header.mipLevels = 1;
    header.arraySize = 1;
    header.format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    header.flags = 0;

    uint32_t pixelData[16];
    std::fill_n(pixelData, 16, 0xFF000000u);

    size_t pixelDataSize = sizeof(pixelData);
    size_t totalSize = sizeof(TextureHeader) + pixelDataSize;

    header.common.dataSize = static_cast<uint32_t>(pixelDataSize);
    header.common.checksum = 0;

    std::vector<uint8_t> textureData(totalSize);
    memcpy(textureData.data(), &header, sizeof(TextureHeader));
    memcpy(textureData.data() + sizeof(TextureHeader), pixelData, pixelDataSize);

    if (!StampAssetChecksum<TextureHeader>(textureData)) {
        NEXT_LOG_ERROR("Generated default emissive texture checksum failed: %s", header.common.name);
        return {};
    }

    NEXT_LOG_DEBUG("Generated default emissive texture: %s (%zu bytes)",
                   header.common.name,
                   textureData.size());
    return textureData;
}

std::vector<uint8_t> AssetCompiler::GenerateDefaultOcclusionTexture() {
    TextureHeader header;
    memset(&header, 0, sizeof(TextureHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Texture;
    CopyFixedString(header.common.name, "DefaultOcclusion");

    header.width = 4;
    header.height = 4;
    header.depth = 1;
    header.mipLevels = 1;
    header.arraySize = 1;
    header.format = 28; // DXGI_FORMAT_R8G8B8A8_UNORM
    header.flags = 0;

    uint32_t pixelData[16];
    std::fill_n(pixelData, 16, 0xFFFFFFFFu);

    size_t pixelDataSize = sizeof(pixelData);
    size_t totalSize = sizeof(TextureHeader) + pixelDataSize;

    header.common.dataSize = static_cast<uint32_t>(pixelDataSize);
    header.common.checksum = 0;

    std::vector<uint8_t> textureData(totalSize);
    memcpy(textureData.data(), &header, sizeof(TextureHeader));
    memcpy(textureData.data() + sizeof(TextureHeader), pixelData, pixelDataSize);

    if (!StampAssetChecksum<TextureHeader>(textureData)) {
        NEXT_LOG_ERROR("Generated default occlusion texture checksum failed: %s", header.common.name);
        return {};
    }

    NEXT_LOG_DEBUG("Generated default occlusion texture: %s (%zu bytes)",
                   header.common.name,
                   textureData.size());
    return textureData;
}

std::vector<uint8_t> AssetCompiler::GenerateTestMaterial() {
    // Generate a simple PBR material
    MaterialHeader header;
    memset(&header, 0, sizeof(MaterialHeader));
    header.common.magic = AssetHeader::MAGIC;
    header.common.version = AssetHeader::CURRENT_VERSION;
    header.common.assetType = AssetType::Material;
    CopyFixedString(header.common.name, "TestPBR");
    
    header.textureCount = 5;
    header.parameterCount = 4;
    header.shaderID = 1; // PBR shader
    header.flags = MaterialHeader::OPAQUE_FLAG;
    
    // Texture references
    TextureRef textureRefs[5];
    memset(textureRefs, 0, sizeof(textureRefs));
    CopyFixedString(textureRefs[0].name, "TestChecker");
    textureRefs[0].slot = 0;
    textureRefs[0].type = TextureRef::ALBEDO;

    CopyFixedString(textureRefs[1].name, "DefaultNormal");
    textureRefs[1].slot = 1;
    textureRefs[1].type = TextureRef::NORMAL;

    CopyFixedString(textureRefs[2].name, "DefaultMetallicRoughness");
    textureRefs[2].slot = 2;
    textureRefs[2].type = TextureRef::METALLIC_ROUGHNESS;

    CopyFixedString(textureRefs[3].name, "DefaultEmissive");
    textureRefs[3].slot = 3;
    textureRefs[3].type = TextureRef::EMISSIVE;

    CopyFixedString(textureRefs[4].name, "DefaultOcclusion");
    textureRefs[4].slot = 4;
    textureRefs[4].type = TextureRef::OCCLUSION;
    
    // Material parameters
    MaterialParam params[4];
    memset(params, 0, sizeof(params));

    CopyFixedString(params[0].name, "metallic");
    params[0].type = MaterialParam::FLOAT;
    params[0].value[0] = 0.5f;

    CopyFixedString(params[1].name, "roughness");
    params[1].type = MaterialParam::FLOAT;
    params[1].value[0] = 0.3f;

    CopyFixedString(params[2].name, "baseColor");
    params[2].type = MaterialParam::COLOR;
    params[2].value[0] = 0.8f; // R
    params[2].value[1] = 0.8f; // G
    params[2].value[2] = 0.8f; // B
    params[2].value[3] = 1.0f; // A

    CopyFixedString(params[3].name, "emissive");
    params[3].type = MaterialParam::COLOR;
    params[3].value[0] = 0.0f;
    params[3].value[1] = 0.0f;
    params[3].value[2] = 0.0f;
    params[3].value[3] = 0.0f;
    
    // Calculate sizes
    size_t textureRefsSize = header.textureCount * sizeof(TextureRef);
    size_t paramsSize = header.parameterCount * sizeof(MaterialParam);
    size_t totalSize = sizeof(MaterialHeader) + textureRefsSize + paramsSize;
    
    header.common.dataSize = static_cast<uint32_t>(textureRefsSize + paramsSize);
    header.common.checksum = 0;
    
    // Build material data
    std::vector<uint8_t> materialData(totalSize);
    uint8_t* ptr = materialData.data();
    
    memcpy(ptr, &header, sizeof(MaterialHeader));
    ptr += sizeof(MaterialHeader);
    
    memcpy(ptr, textureRefs, textureRefsSize);
    ptr += textureRefsSize;
    
    memcpy(ptr, params, paramsSize);

    if (!StampAssetChecksum<MaterialHeader>(materialData)) {
        NEXT_LOG_ERROR("Generated test material checksum failed: %s", header.common.name);
        return {};
    }

    NEXT_LOG_DEBUG("Generated test material: %s (%zu bytes)", header.common.name, materialData.size());
    return materialData;
}

bool AssetCompiler::WritePackage(const std::string& path,
                                const PackageHeader& header,
                                const std::vector<AssetEntry>& entries,
                                const std::vector<std::vector<uint8_t>>& assetData) {
    std::ofstream file(path, std::ios::binary);
    if (!file.is_open()) {
        NEXT_LOG_ERROR("Failed to open package file: %s", path.c_str());
        return false;
    }

    std::vector<uint8_t> payload = BuildPackagePayload(entries, assetData);
    PackageHeader packageHeader = header;
    packageHeader.checksum = CalculateCRC64(payload.data(), payload.size());

    file.write(reinterpret_cast<const char*>(&packageHeader), sizeof(PackageHeader));
    if (!payload.empty()) {
        file.write(reinterpret_cast<const char*>(payload.data()),
                   static_cast<std::streamsize>(payload.size()));
    }
    
    file.close();
    
    NEXT_LOG_INFO("Package written: %s (%zu assets, %zu total bytes)",
                 path.c_str(),
                 entries.size(),
                 sizeof(PackageHeader) + payload.size());
    
    for (const auto& entry : entries) {
        NEXT_LOG_DEBUG("  Asset: %s (%s, %u bytes)", 
                      entry.name, 
                      AssetTypeToString(entry.assetType),
                      entry.assetSize);
    }
    
    return true;
}

} // namespace Next
