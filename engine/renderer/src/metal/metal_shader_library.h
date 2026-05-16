#pragma once

#include "metal_device.h"

#include <cstdint>
#include <limits>
#include <string>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

static constexpr uint32_t kMetalShaderManifestVersion = 1;
static constexpr uint32_t kMetalShaderManifestInvalidBindingIndex = std::numeric_limits<uint32_t>::max();
static constexpr const char* kMetalDemoForwardMaterialLayoutName = "material_srg_v1";
static constexpr const char* kMetalDemoForwardPipelineLayoutName = "demo_forward_pipeline_v1";
static constexpr RHI::ArgumentBufferTier kMetalDemoForwardRequiredArgumentBufferTier =
    RHI::ArgumentBufferTier::Tier1;
static constexpr uint32_t kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex = 2;
static constexpr uint32_t kMetalDemoForwardMaterialShaderResourceGroupUniformArgumentIndex = 0;
static constexpr uint32_t kMetalDemoForwardMaterialShaderResourceGroupTextureArgumentBaseIndex = 1;
static constexpr uint32_t kMetalDemoForwardMaterialShaderResourceGroupSamplerArgumentBaseIndex = 6;

enum class MetalShaderLibraryInputKind : uint8_t {
    None = 0,
    Metallib,
    Source,
};

struct MetalShaderLibraryInput {
    MetalShaderLibraryInputKind kind = MetalShaderLibraryInputKind::None;
    std::string path;

    bool IsValid() const { return kind != MetalShaderLibraryInputKind::None && !path.empty(); }
};

struct MetalShaderLibraryDesc {
    uint32_t manifestVersion = kMetalShaderManifestVersion;
    std::string debugName;
    std::string manifestPath;
    std::string metallibPath;
    std::string sourcePath;
    std::string vertexEntryPoint;
    std::string fragmentEntryPoint;
    std::string materialLayout;
    std::string pipelineLayout;
    RHI::ArgumentBufferTier requiredArgumentBufferTier = RHI::ArgumentBufferTier::Unsupported;
    uint32_t materialShaderResourceGroupArgumentBufferIndex = kMetalShaderManifestInvalidBindingIndex;
    uint32_t materialShaderResourceGroupUniformArgumentIndex = kMetalShaderManifestInvalidBindingIndex;
    uint32_t materialShaderResourceGroupTextureArgumentBaseIndex = kMetalShaderManifestInvalidBindingIndex;
    uint32_t materialShaderResourceGroupSamplerArgumentBaseIndex = kMetalShaderManifestInvalidBindingIndex;
};

struct MetalShaderLibraryManifest {
    uint32_t version = kMetalShaderManifestVersion;
    std::string debugName;
    std::string manifestPath;
    std::string metallibPath;
    std::string sourcePath;
    std::string vertexEntryPoint;
    std::string fragmentEntryPoint;
    std::string materialLayout;
    std::string pipelineLayout;
    RHI::ArgumentBufferTier requiredArgumentBufferTier = RHI::ArgumentBufferTier::Unsupported;
    uint32_t materialShaderResourceGroupArgumentBufferIndex = kMetalShaderManifestInvalidBindingIndex;
    uint32_t materialShaderResourceGroupUniformArgumentIndex = kMetalShaderManifestInvalidBindingIndex;
    uint32_t materialShaderResourceGroupTextureArgumentBaseIndex = kMetalShaderManifestInvalidBindingIndex;
    uint32_t materialShaderResourceGroupSamplerArgumentBaseIndex = kMetalShaderManifestInvalidBindingIndex;

    bool IsValid() const {
        return version == kMetalShaderManifestVersion &&
               (!metallibPath.empty() || !sourcePath.empty()) &&
               !vertexEntryPoint.empty() &&
               !fragmentEntryPoint.empty();
    }
    MetalShaderLibraryDesc ToDesc() const;
};

const char* DemoForwardShaderManifestPath();
const char* DemoForwardShaderPath();
const char* DemoForwardMetallibPath();
bool LoadShaderLibraryManifest(const char* manifestPath, MetalShaderLibraryManifest* outManifest);
MetalShaderLibraryDesc DemoForwardShaderLibraryDesc();
MetalShaderLibraryInput SelectShaderLibraryInput(const MetalShaderLibraryDesc& desc);
id<MTLLibrary> CreateShaderLibrary(MetalDevice& device, const MetalShaderLibraryDesc& desc);
id<MTLLibrary> CreateShaderLibrary(MetalDevice& device,
                                   const MetalShaderLibraryDesc& desc,
                                   MetalShaderLibraryInput* outInput);
id<MTLLibrary> CreateDemoForwardShaderLibrary(MetalDevice& device);
id<MTLLibrary> CreateShaderLibraryFromSourceFile(MetalDevice& device,
                                                 const char* sourcePath,
                                                 const char* debugName);

} // namespace MetalBackend
} // namespace Next
