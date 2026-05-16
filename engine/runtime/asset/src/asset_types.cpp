#include "next/runtime/asset/asset_types.h"
#include "next/foundation/logger.h"
#include <string>

namespace Next {

namespace {

constexpr uint64_t kCrc64EcmaPolynomial = 0x42F0E1EBA9EA3693ULL;

const char* SafeAssetName(const AssetHeader& header) {
    return header.HasValidName() ? header.name : "<invalid>";
}

} // namespace

// Utility functions for asset types

const char* AssetTypeToString(AssetType type) {
    switch (type) {
        case AssetType::Unknown: return "Unknown";
        case AssetType::Mesh: return "Mesh";
        case AssetType::Texture: return "Texture";
        case AssetType::Material: return "Material";
        default: return "Invalid";
    }
}

AssetType StringToAssetType(const std::string& str) {
    if (str == "Mesh") return AssetType::Mesh;
    if (str == "Texture") return AssetType::Texture;
    if (str == "Material") return AssetType::Material;
    return AssetType::Unknown;
}

bool ValidateMeshHeader(const MeshHeader& header) {
    const char* name = SafeAssetName(header.common);
    if (!header.common.Validate()) {
        NEXT_LOG_ERROR("Invalid common header in mesh: %s", name);
        return false;
    }
    
    if (header.vertexCount == 0) {
        NEXT_LOG_ERROR("Mesh has zero vertices: %s", name);
        return false;
    }
    
    if (header.indexCount == 0) {
        NEXT_LOG_ERROR("Mesh has zero indices: %s", name);
        return false;
    }
    
    if (header.vertexStride < 12) { // At least position (3 floats)
        NEXT_LOG_ERROR("Mesh vertex stride too small: %s", name);
        return false;
    }
    
    // Validate bounding box
    if (header.boundingBox[0] > header.boundingBox[3] ||
        header.boundingBox[1] > header.boundingBox[4] ||
        header.boundingBox[2] > header.boundingBox[5]) {
        NEXT_LOG_WARNING("Mesh has invalid bounding box: %s", name);
    }
    
    return true;
}

bool ValidateTextureHeader(const TextureHeader& header) {
    const char* name = SafeAssetName(header.common);
    if (!header.common.Validate()) {
        NEXT_LOG_ERROR("Invalid common header in texture: %s", name);
        return false;
    }
    
    if (header.width == 0 || header.height == 0) {
        NEXT_LOG_ERROR("Texture has zero dimensions: %s", name);
        return false;
    }
    
    if (header.mipLevels == 0) {
        NEXT_LOG_ERROR("Texture has zero mip levels: %s", name);
        return false;
    }
    
    if (header.arraySize == 0) {
        NEXT_LOG_ERROR("Texture has zero array size: %s", name);
        return false;
    }
    
    // Basic format validation
    if (header.format > 200) { // Arbitrary limit for DXGI_FORMAT
        NEXT_LOG_WARNING("Texture has unusual format: %s", name);
    }
    
    return true;
}

bool ValidateMaterialHeader(const MaterialHeader& header) {
    const char* name = SafeAssetName(header.common);
    if (!header.common.Validate()) {
        NEXT_LOG_ERROR("Invalid common header in material: %s", name);
        return false;
    }
    
    // Materials can have zero textures (unlit materials)
    // Materials can have zero parameters (default values)
    
    return true;
}

uint64_t CalculateCRC64(const void* data, size_t size) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(data);
    uint64_t crc = 0;

    if (!bytes && size != 0) {
        return crc;
    }

    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint64_t>(bytes[i]) << 56;
        for (int j = 0; j < 8; ++j) {
            if ((crc & 0x8000000000000000ULL) != 0) {
                crc = (crc << 1) ^ kCrc64EcmaPolynomial;
            } else {
                crc <<= 1;
            }
        }
    }

    return crc;
}

bool ValidateAssetChecksum(const AssetHeader& header, const void* data) {
    const char* name = SafeAssetName(header);
    if (!header.Validate()) {
        NEXT_LOG_ERROR("Invalid asset header for checksum validation: %s", name);
        return false;
    }

    uint64_t calculated = CalculateCRC64(data, header.dataSize);
    if (calculated != header.checksum) {
        NEXT_LOG_ERROR("Checksum mismatch for asset: %s (expected: %llx, got: %llx)", 
                     name, header.checksum, calculated);
        return false;
    }
    
    return true;
}

} // namespace Next
