#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

namespace Next {

// Asset type enumeration
enum class AssetType : uint32_t {
    Unknown = 0,
    Mesh = 1,
    Texture = 2,
    Material = 3,
    
    Count
};

// Common asset header (matches specification)
struct AssetHeader {
    uint32_t magic;           // 'NEXT' (0x4E455854)
    uint32_t version;         // Format version
    AssetType assetType;      // Asset type
    uint32_t dataSize;        // Size of data block
    uint64_t checksum;        // CRC64 checksum
    char name[64];            // Null-terminated asset name
    
    static constexpr uint32_t MAGIC = 0x4E455854; // 'NEXT'
    static constexpr uint32_t CURRENT_VERSION = 1;

    bool HasValidName() const {
        return name[0] != '\0' && std::memchr(name, '\0', sizeof(name)) != nullptr;
    }

    bool Validate() const {
        return magic == MAGIC && 
               version == CURRENT_VERSION &&
               static_cast<uint32_t>(assetType) > 0 &&
               static_cast<uint32_t>(assetType) < static_cast<uint32_t>(AssetType::Count) &&
               HasValidName();
    }
};

// Mesh asset specific header
struct MeshHeader {
    AssetHeader common;
    
    uint32_t vertexCount;
    uint32_t indexCount;
    uint32_t vertexStride;    // Size of one vertex in bytes
    uint32_t indexType;       // 0 = uint16, 1 = uint32
    uint32_t vertexFormat;    // Vertex layout format ID
    float boundingBox[6];     // minX, minY, minZ, maxX, maxY, maxZ
    uint32_t materialCount;   // Number of submeshes
    uint32_t flags;           // Mesh flags
    
    enum Flags {
        HAS_NORMALS = 1 << 0,
        HAS_UVS = 1 << 1,
        HAS_TANGENTS = 1 << 2,
        HAS_COLORS = 1 << 3,
    };
};

// Texture asset specific header
struct TextureHeader {
    AssetHeader common;
    
    uint32_t width;
    uint32_t height;
    uint32_t depth;           // For 3D textures
    uint32_t mipLevels;
    uint32_t arraySize;       // For texture arrays
    uint32_t format;          // DXGI_FORMAT enum
    uint32_t flags;           // Texture flags
    
    enum Flags {
        SRGB = 1 << 0,
        CUBEMAP = 1 << 1,
        VOLUME = 1 << 2,
        COMPRESSED = 1 << 3,
    };
};

// Material asset specific header
struct MaterialHeader {
    AssetHeader common;
    
    uint32_t textureCount;    // Number of texture references
    uint32_t parameterCount;  // Number of material parameters
    uint32_t shaderID;        // Shader identifier
    uint32_t flags;           // Material flags
    
    enum Flags {
        // Use *_FLAG names to avoid collisions with Win32 macros (OPAQUE/TRANSPARENT from wingdi.h).
        OPAQUE_FLAG = 1 << 0,
        TRANSPARENT_FLAG = 1 << 1,
        DOUBLE_SIDED = 1 << 2,
        UNLIT = 1 << 3,
    };
};

// Texture reference in material
struct TextureRef {
    char name[64];           // Texture asset name
    uint32_t slot;           // Texture slot index
    uint32_t type;           // Texture type
    
    enum Type {
        ALBEDO = 0,
        NORMAL = 1,
        METALLIC_ROUGHNESS = 2,
        EMISSIVE = 3,
        OCCLUSION = 4,
    };
};

// Material parameter
struct MaterialParam {
    char name[32];           // Parameter name
    uint32_t type;           // Parameter type
    
    enum Type {
        FLOAT = 0,
        VEC2 = 1,
        VEC3 = 2,
        VEC4 = 3,
        COLOR = 4,
    };
    
    float value[4];          // Parameter value
};

// Package container header
struct PackageHeader {
    uint32_t magic;           // 'NPKG' (0x4E504B47)
    uint32_t version;         // Package format version
    uint32_t assetCount;      // Number of assets
    uint32_t indexOffset;     // Offset to asset index
    uint32_t dataOffset;      // Offset to asset data
    uint64_t checksum;        // CRC64 checksum
    char name[64];            // Package name

    static constexpr uint32_t MAGIC = 0x4E504B47; // 'NPKG'
    // v2: AssetEntry gained compressionAlgorithm and per-entry payloads may be
    // LZ4/Zstd compressed. v1 packages (uncompressed, smaller AssetEntry stride)
    // are rejected by the loader and must be re-cooked with next_assetc.
    static constexpr uint32_t CURRENT_VERSION = 2;

    bool HasValidName() const {
        return name[0] != '\0' &&
               std::memchr(name, '\0', sizeof(name)) != nullptr;
    }
    
    bool Validate() const {
        return magic == MAGIC &&
               version == CURRENT_VERSION &&
               indexOffset >= sizeof(PackageHeader) &&
               dataOffset > indexOffset &&
               HasValidName();
    }
};

// Asset entry in package index.
//
// Size-field semantics (package format v2):
//   dataOffset           Byte offset of this entry's stored bytes within the data
//                        section. Entries chain: entry[i+1].dataOffset ==
//                        entry[i].dataOffset + entry[i].assetSize.
//   assetSize            Number of bytes physically stored at dataOffset (i.e. the
//                        post-compression size). This is what the loader reads off disk.
//   compressionAlgorithm Codec for the stored bytes. Matches Next::Compression::Algorithm:
//                        0 = None, 1 = Zstd, 2 = LZ4. None means stored uncompressed.
//   compressedSize       When compressed, equals assetSize (the stored compressed byte
//                        count); 0 when uncompressed.
//   decompressedSize     Logical asset blob size (AssetHeader + type header + payload)
//                        after decompression; equals assetSize when uncompressed.
struct AssetEntry {
    AssetType assetType;
    uint32_t assetSize;            // bytes stored on disk (post-compression)
    uint32_t dataOffset;           // relative to data section
    uint32_t compressionAlgorithm; // Next::Compression::Algorithm (0=None, 1=Zstd, 2=LZ4)
    uint32_t compressedSize;       // == assetSize when compressed, 0 when uncompressed
    uint32_t decompressedSize;     // logical size; == assetSize when uncompressed
    char name[64];                 // Asset name
};

// Utility helpers
const char* AssetTypeToString(AssetType type);
AssetType StringToAssetType(const std::string& str);
bool ValidateMeshHeader(const MeshHeader& header);
bool ValidateTextureHeader(const TextureHeader& header);
bool ValidateMaterialHeader(const MaterialHeader& header);
uint64_t CalculateCRC64(const void* data, size_t size);
bool ValidateAssetChecksum(const AssetHeader& header, const void* data);

} // namespace Next
