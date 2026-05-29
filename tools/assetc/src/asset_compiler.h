#pragma once

#include "next/runtime/asset/asset_types.h"
#include <string>
#include <vector>

namespace Next {

class AssetCompiler {
public:
    AssetCompiler();
    ~AssetCompiler();
    
    // Compile individual assets
    bool CompileMesh(const std::string& sourcePath, const std::string& outputPath);
    bool CompileTexture(const std::string& sourcePath, const std::string& outputPath);
    bool CompileMaterial(const std::string& sourcePath, const std::string& outputPath);
    bool CompileCell(const std::string& sourcePath,
                     const std::string& outputPath,
                     const std::string& compression = "none");
    
    // Create package from compiled assets.
    // compression: "auto" (compress each blob, keep if strictly smaller), "none",
    // "lz4", or "zstd". Defaults to "auto".
    bool CreatePackage(const std::string& packageName,
                      const std::vector<std::string>& assetFiles,
                      const std::string& outputPath,
                      const std::string& compression = "auto");
    
    // Generate test assets for CP3
    bool GenerateTestAssets(const std::string& outputDir);
    
private:
    // Helper functions
    bool WriteAssetFile(const std::string& path, const void* data, size_t size);
    bool ReadAssetFile(const std::string& path, std::vector<uint8_t>& data);
    
    // Test asset generation
    std::vector<uint8_t> GenerateTestMesh();
    std::vector<uint8_t> GenerateTestTexture();
    std::vector<uint8_t> GenerateDefaultNormalTexture();
    std::vector<uint8_t> GenerateDefaultMetallicRoughnessTexture();
    std::vector<uint8_t> GenerateDefaultEmissiveTexture();
    std::vector<uint8_t> GenerateDefaultOcclusionTexture();
    std::vector<uint8_t> GenerateTestMaterial();
    
    // Package creation
    bool WritePackage(const std::string& path,
                     const PackageHeader& header,
                     const std::vector<AssetEntry>& entries,
                     const std::vector<std::vector<uint8_t>>& assetData);
};

} // namespace Next
