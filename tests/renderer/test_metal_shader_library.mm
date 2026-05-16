#include "metal_device.h"
#include "metal_shader_library.h"

#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

constexpr const char* kExistingMetallibPath = "/tmp/next_engine_existing_demo_forward.metallib";
constexpr const char* kInvalidMetallibPath = "/tmp/next_engine_invalid_demo_forward.metallib";
constexpr const char* kMissingMetallibPath = "/tmp/next_engine_missing_demo_forward.metallib";
constexpr const char* kMissingSourcePath = "/tmp/next_engine_missing_demo_forward.metal";
constexpr const char* kShaderDirectoryEnv = "NEXT_METAL_SHADER_DIR";
constexpr const char* kManifestOverrideEnv = "NEXT_METAL_DEMO_FORWARD_MANIFEST";
constexpr const char* kSourceOverrideEnv = "NEXT_METAL_DEMO_FORWARD_SOURCE";
constexpr const char* kMetallibOverrideEnv = "NEXT_METAL_DEMO_FORWARD_METALLIB";
constexpr const char* kShaderDirectoryOverride = "/tmp/next_engine_shader_dir_override";
constexpr const char* kManifestOverridePath = "/tmp/next_engine_demo_forward.shader_manifest";
constexpr const char* kMissingManifestOverridePath = "/tmp/next_engine_missing_demo_forward.shader_manifest";
constexpr const char* kManifestSourcePath = "/tmp/next_engine_manifest_demo_forward.metal";
constexpr const char* kManifestMetallibPath = "/tmp/next_engine_manifest_demo_forward.metallib";
constexpr const char* kOverrideSourcePath = "/tmp/next_engine_override_demo_forward.metal";
constexpr const char* kOverrideMetallibPath = "/tmp/next_engine_override_demo_forward.metallib";
constexpr const char* kRelativeManifestDirectory = "next_engine_relative_manifest";
constexpr const char* kRelativeManifestPath = "next_engine_relative_manifest/demo_forward.shader_manifest";
constexpr const char* kRelativeOverrideSourcePath = "./next_engine_override_demo_forward.metal";
constexpr const char* kRelativeOverrideMetallibPath = "next_engine_outputs/../next_engine_override_demo_forward.metallib";

class ScopedEnvironmentVariable {
public:
    ScopedEnvironmentVariable(const char* name, const char* value)
        : name_(name) {
        const char* previous = std::getenv(name_);
        if (previous) {
            hadPrevious_ = true;
            previous_ = previous;
        }

        if (value) {
            setenv(name_, value, 1);
        } else {
            unsetenv(name_);
        }
    }

    ~ScopedEnvironmentVariable() {
        if (hadPrevious_) {
            setenv(name_, previous_.c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_ = nullptr;
    bool hadPrevious_ = false;
    std::string previous_;
};

class MetalShaderLibraryInputTest : public ::testing::Test {
protected:
    void SetUp() override { CleanupTemporaryFiles(); }
    void TearDown() override { CleanupTemporaryFiles(); }

private:
    static void CleanupTemporaryFiles() {
        std::remove(kExistingMetallibPath);
        std::remove(kInvalidMetallibPath);
        std::remove(kMissingMetallibPath);
        std::remove(kMissingSourcePath);
        std::remove(kManifestOverridePath);
        std::remove(kMissingManifestOverridePath);
        std::remove(kManifestSourcePath);
        std::remove(kManifestMetallibPath);
        std::remove(kOverrideSourcePath);
        std::remove(kOverrideMetallibPath);
        std::filesystem::remove_all(kShaderDirectoryOverride);
        std::filesystem::remove_all(kRelativeManifestDirectory);
    }
};

void WriteDemoForwardMaterialSrgArgumentLayout(std::ofstream& manifest) {
    manifest << "materialShaderResourceGroupUniformArgumentIndex=0\n";
    manifest << "materialShaderResourceGroupTextureArgumentBaseIndex=1\n";
    manifest << "materialShaderResourceGroupSamplerArgumentBaseIndex=6\n";
}

void ExpectDemoForwardMaterialSrgArgumentLayout(const MetalShaderLibraryManifest& manifest) {
    EXPECT_EQ(manifest.materialShaderResourceGroupUniformArgumentIndex,
              kMetalDemoForwardMaterialShaderResourceGroupUniformArgumentIndex);
    EXPECT_EQ(manifest.materialShaderResourceGroupTextureArgumentBaseIndex,
              kMetalDemoForwardMaterialShaderResourceGroupTextureArgumentBaseIndex);
    EXPECT_EQ(manifest.materialShaderResourceGroupSamplerArgumentBaseIndex,
              kMetalDemoForwardMaterialShaderResourceGroupSamplerArgumentBaseIndex);
}

void ExpectDemoForwardMaterialSrgArgumentLayout(const MetalShaderLibraryDesc& desc) {
    EXPECT_EQ(desc.materialShaderResourceGroupUniformArgumentIndex,
              kMetalDemoForwardMaterialShaderResourceGroupUniformArgumentIndex);
    EXPECT_EQ(desc.materialShaderResourceGroupTextureArgumentBaseIndex,
              kMetalDemoForwardMaterialShaderResourceGroupTextureArgumentBaseIndex);
    EXPECT_EQ(desc.materialShaderResourceGroupSamplerArgumentBaseIndex,
              kMetalDemoForwardMaterialShaderResourceGroupSamplerArgumentBaseIndex);
}

void ExpectInvalidMaterialSrgArgumentLayout(const MetalShaderLibraryDesc& desc) {
    EXPECT_EQ(desc.materialShaderResourceGroupUniformArgumentIndex, kMetalShaderManifestInvalidBindingIndex);
    EXPECT_EQ(desc.materialShaderResourceGroupTextureArgumentBaseIndex, kMetalShaderManifestInvalidBindingIndex);
    EXPECT_EQ(desc.materialShaderResourceGroupSamplerArgumentBaseIndex, kMetalShaderManifestInvalidBindingIndex);
}

} // namespace

TEST_F(MetalShaderLibraryInputTest, LoadsManifestAndResolvesRelativePaths) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "# test manifest\n";
        manifest << "version = 1\n";
        manifest << "debugName = Manifest shader\n";
        manifest << "source = next_engine_manifest_demo_forward.metal\n";
        manifest << "metallib = next_engine_manifest_demo_forward.metallib\n";
        manifest << "vertexEntry = manifest_vertex\n";
        manifest << "fragmentEntry = manifest_fragment\n";
        manifest << "materialLayout = material_srg_v1\n";
        manifest << "pipelineLayout = demo_forward_pipeline_v1\n";
        manifest << "requiredArgumentBufferTier = tier1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex = 2\n";
        manifest << "materialShaderResourceGroupUniformArgumentIndex = 0\n";
        manifest << "materialShaderResourceGroupTextureArgumentBaseIndex = 1\n";
        manifest << "materialShaderResourceGroupSamplerArgumentBaseIndex = 6\n";
    }

    MetalShaderLibraryManifest manifest;
    ASSERT_TRUE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_TRUE(manifest.IsValid());
    EXPECT_EQ(manifest.version, kMetalShaderManifestVersion);
    EXPECT_EQ(manifest.debugName, "Manifest shader");
    EXPECT_EQ(manifest.manifestPath, kManifestOverridePath);
    EXPECT_EQ(manifest.sourcePath, kManifestSourcePath);
    EXPECT_EQ(manifest.metallibPath, kManifestMetallibPath);
    EXPECT_EQ(manifest.vertexEntryPoint, "manifest_vertex");
    EXPECT_EQ(manifest.fragmentEntryPoint, "manifest_fragment");
    EXPECT_EQ(manifest.materialLayout, kMetalDemoForwardMaterialLayoutName);
    EXPECT_EQ(manifest.pipelineLayout, kMetalDemoForwardPipelineLayoutName);
    EXPECT_EQ(manifest.requiredArgumentBufferTier, kMetalDemoForwardRequiredArgumentBufferTier);
    EXPECT_EQ(manifest.materialShaderResourceGroupArgumentBufferIndex,
              kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex);
    ExpectDemoForwardMaterialSrgArgumentLayout(manifest);

    const MetalShaderLibraryDesc desc = manifest.ToDesc();
    EXPECT_EQ(desc.manifestVersion, kMetalShaderManifestVersion);
    EXPECT_EQ(desc.debugName, "Manifest shader");
    EXPECT_EQ(desc.manifestPath, kManifestOverridePath);
    EXPECT_EQ(desc.sourcePath, kManifestSourcePath);
    EXPECT_EQ(desc.metallibPath, kManifestMetallibPath);
    EXPECT_EQ(desc.vertexEntryPoint, "manifest_vertex");
    EXPECT_EQ(desc.fragmentEntryPoint, "manifest_fragment");
    EXPECT_EQ(desc.materialLayout, kMetalDemoForwardMaterialLayoutName);
    EXPECT_EQ(desc.pipelineLayout, kMetalDemoForwardPipelineLayoutName);
    EXPECT_EQ(desc.requiredArgumentBufferTier, kMetalDemoForwardRequiredArgumentBufferTier);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex,
              kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex);
    ExpectDemoForwardMaterialSrgArgumentLayout(desc);

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, LoadsRelativeManifestAndNormalizesResolvedPaths) {
    const std::filesystem::path manifestDirectory(kRelativeManifestDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(manifestDirectory));
    {
        std::ofstream manifest(kRelativeManifestPath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version = 1\n";
        manifest << "source = demo_forward.metal\n";
        manifest << "metallib = outputs/../demo_forward.metallib\n";
        manifest << "vertexEntry = relative_vertex\n";
        manifest << "fragmentEntry = relative_fragment\n";
    }

    MetalShaderLibraryManifest manifest;
    ASSERT_TRUE(LoadShaderLibraryManifest(kRelativeManifestPath, &manifest));

    const std::filesystem::path expectedManifest =
        std::filesystem::absolute(kRelativeManifestPath).lexically_normal();
    const std::filesystem::path expectedSource =
        std::filesystem::absolute(manifestDirectory / "demo_forward.metal").lexically_normal();
    const std::filesystem::path expectedMetallib =
        std::filesystem::absolute(manifestDirectory / "demo_forward.metallib").lexically_normal();

    EXPECT_EQ(std::filesystem::path(manifest.manifestPath), expectedManifest);
    EXPECT_EQ(std::filesystem::path(manifest.sourcePath), expectedSource);
    EXPECT_EQ(std::filesystem::path(manifest.metallibPath), expectedMetallib);
    EXPECT_EQ(manifest.vertexEntryPoint, "relative_vertex");
    EXPECT_EQ(manifest.fragmentEntryPoint, "relative_fragment");

    std::filesystem::remove_all(manifestDirectory);
}

TEST_F(MetalShaderLibraryInputTest, LoadsManifestWithSingleShaderInput) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "vertexEntry=source_only_vertex\n";
        manifest << "fragmentEntry=source_only_fragment\n";
    }

    MetalShaderLibraryManifest manifest;
    ASSERT_TRUE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_TRUE(manifest.IsValid());
    EXPECT_EQ(manifest.sourcePath, kManifestSourcePath);
    EXPECT_TRUE(manifest.metallibPath.empty());
    EXPECT_EQ(manifest.vertexEntryPoint, "source_only_vertex");
    EXPECT_EQ(manifest.fragmentEntryPoint, "source_only_fragment");

    {
        std::ofstream manifestFile(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifestFile.good());
        manifestFile << "version=1\n";
        manifestFile << "metallib=next_engine_manifest_demo_forward.metallib\n";
        manifestFile << "vertexEntry=metallib_only_vertex\n";
        manifestFile << "fragmentEntry=metallib_only_fragment\n";
    }

    ASSERT_TRUE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_TRUE(manifest.IsValid());
    EXPECT_TRUE(manifest.sourcePath.empty());
    EXPECT_EQ(manifest.metallibPath, kManifestMetallibPath);
    EXPECT_EQ(manifest.vertexEntryPoint, "metallib_only_vertex");
    EXPECT_EQ(manifest.fragmentEntryPoint, "metallib_only_fragment");

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsManifestInputPathsEscapingDirectory) {
    const std::filesystem::path manifestDirectory(kRelativeManifestDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(manifestDirectory));
    {
        std::ofstream manifest(kRelativeManifestPath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version = 1\n";
        manifest << "source = nested/../../outside.metal\n";
        manifest << "vertexEntry = vertex_main\n";
        manifest << "fragmentEntry = fragment_main\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kRelativeManifestPath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    {
        std::ofstream manifestFile(kRelativeManifestPath, std::ios::binary);
        ASSERT_TRUE(manifestFile.good());
        manifestFile << "version = 1\n";
        manifestFile << "source = demo_forward.metal\n";
        manifestFile << "metallib = ../outside.metallib\n";
        manifestFile << "vertexEntry = vertex_main\n";
        manifestFile << "fragmentEntry = fragment_main\n";
    }

    EXPECT_FALSE(LoadShaderLibraryManifest(kRelativeManifestPath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::filesystem::remove_all(manifestDirectory);
}

TEST_F(MetalShaderLibraryInputTest, RejectsAbsoluteManifestInputPaths) {
    const std::filesystem::path manifestDirectory(kRelativeManifestDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(manifestDirectory));
    {
        std::ofstream manifest(kRelativeManifestPath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version = 1\n";
        manifest << "source = " << kManifestSourcePath << "\n";
        manifest << "vertexEntry = vertex_main\n";
        manifest << "fragmentEntry = fragment_main\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kRelativeManifestPath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::filesystem::remove_all(manifestDirectory);
}

TEST_F(MetalShaderLibraryInputTest, DemoForwardDescUsesExplicitManifest) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Explicit manifest shader\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "metallib=next_engine_manifest_demo_forward.metallib\n";
        manifest << "vertexEntry=explicit_vertex\n";
        manifest << "fragmentEntry=explicit_fragment\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
        WriteDemoForwardMaterialSrgArgumentLayout(manifest);
    }

    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, kManifestOverridePath);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    EXPECT_STREQ(DemoForwardShaderManifestPath(), kManifestOverridePath);
    EXPECT_STREQ(DemoForwardShaderPath(), kManifestSourcePath);
    EXPECT_STREQ(DemoForwardMetallibPath(), kManifestMetallibPath);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestVersion, kMetalShaderManifestVersion);
    EXPECT_EQ(desc.debugName, "Explicit manifest shader");
    EXPECT_EQ(desc.manifestPath, kManifestOverridePath);
    EXPECT_EQ(desc.sourcePath, kManifestSourcePath);
    EXPECT_EQ(desc.metallibPath, kManifestMetallibPath);
    EXPECT_EQ(desc.vertexEntryPoint, "explicit_vertex");
    EXPECT_EQ(desc.fragmentEntryPoint, "explicit_fragment");
    EXPECT_EQ(desc.materialLayout, kMetalDemoForwardMaterialLayoutName);
    EXPECT_EQ(desc.pipelineLayout, kMetalDemoForwardPipelineLayoutName);
    EXPECT_EQ(desc.requiredArgumentBufferTier, kMetalDemoForwardRequiredArgumentBufferTier);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex,
              kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex);
    ExpectDemoForwardMaterialSrgArgumentLayout(desc);

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, ExplicitMissingMaterialLayoutDoesNotFallbackToDefaultShader) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Missing material layout shader\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "metallib=next_engine_manifest_demo_forward.metallib\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
        WriteDemoForwardMaterialSrgArgumentLayout(manifest);
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, kManifestOverridePath);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestPath, kManifestOverridePath);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, ExplicitMissingPipelineLayoutDoesNotFallbackToDefaultShader) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Missing pipeline layout shader\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "metallib=next_engine_manifest_demo_forward.metallib\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
        WriteDemoForwardMaterialSrgArgumentLayout(manifest);
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, kManifestOverridePath);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestPath, kManifestOverridePath);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, ExplicitMissingRequiredArgumentBufferTierDoesNotFallbackToDefaultShader) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Missing required argument buffer tier shader\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "metallib=next_engine_manifest_demo_forward.metallib\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
        WriteDemoForwardMaterialSrgArgumentLayout(manifest);
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, kManifestOverridePath);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestPath, kManifestOverridePath);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, ExplicitMissingMaterialSrgArgumentBufferIndexDoesNotFallbackToDefaultShader) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Missing material SRG argument buffer index shader\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "metallib=next_engine_manifest_demo_forward.metallib\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, kManifestOverridePath);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestPath, kManifestOverridePath);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, ExplicitMissingMaterialSrgArgumentLayoutDoesNotFallbackToDefaultShader) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Missing material SRG argument layout shader\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "metallib=next_engine_manifest_demo_forward.metallib\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, kManifestOverridePath);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestPath, kManifestOverridePath);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsManifestWithoutShaderInputs) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Empty shader manifest\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsUnsupportedManifestVersion) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=2\n";
        manifest << "debugName=Future shader manifest\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "vertexEntry=future_vertex\n";
        manifest << "fragmentEntry=future_fragment\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsManifestWithoutVersion) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsInvalidManifestVersion) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=latest\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsDuplicateManifestKeys) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "source=next_engine_manifest_demo_forward_duplicate.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsEmptyManifestKeys) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "=empty_key\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsEmptyRequiredManifestValues) {
    const auto writeManifest = [](const char* source,
                                  const char* metallib,
                                  const char* vertexEntry,
                                  const char* fragmentEntry,
                                  const char* materialLayout,
                                  const char* pipelineLayout,
                                  const char* requiredArgumentBufferTier,
                                  const char* materialSrgArgumentBufferIndex) -> bool {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        if (!manifest.good()) {
            return false;
        }
        manifest << "version=1\n";
        manifest << "source=" << source << "\n";
        manifest << "metallib=" << metallib << "\n";
        manifest << "vertexEntry=" << vertexEntry << "\n";
        manifest << "fragmentEntry=" << fragmentEntry << "\n";
        manifest << "materialLayout=" << materialLayout << "\n";
        manifest << "pipelineLayout=" << pipelineLayout << "\n";
        manifest << "requiredArgumentBufferTier=" << requiredArgumentBufferTier << "\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=" << materialSrgArgumentBufferIndex << "\n";
        WriteDemoForwardMaterialSrgArgumentLayout(manifest);
        return manifest.good();
    };

    struct Case {
        const char* name;
        const char* source;
        const char* metallib;
        const char* vertexEntry;
        const char* fragmentEntry;
        const char* materialLayout;
        const char* pipelineLayout;
        const char* requiredArgumentBufferTier;
        const char* materialSrgArgumentBufferIndex;
    };

    const Case cases[] = {
        {"empty source",
         "",
         "next_engine_manifest_demo_forward.metallib",
         "vertex_main",
         "fragment_main",
         "material_srg_v1",
         "demo_forward_pipeline_v1",
         "tier1",
         "2"},
        {"empty metallib",
         "next_engine_manifest_demo_forward.metal",
         "",
         "vertex_main",
         "fragment_main",
         "material_srg_v1",
         "demo_forward_pipeline_v1",
         "tier1",
         "2"},
        {"empty vertexEntry",
         "next_engine_manifest_demo_forward.metal",
         "next_engine_manifest_demo_forward.metallib",
         "",
         "fragment_main",
         "material_srg_v1",
         "demo_forward_pipeline_v1",
         "tier1",
         "2"},
        {"empty fragmentEntry",
         "next_engine_manifest_demo_forward.metal",
         "next_engine_manifest_demo_forward.metallib",
         "vertex_main",
         "",
         "material_srg_v1",
         "demo_forward_pipeline_v1",
         "tier1",
         "2"},
        {"empty materialLayout",
         "next_engine_manifest_demo_forward.metal",
         "next_engine_manifest_demo_forward.metallib",
         "vertex_main",
         "fragment_main",
         "",
         "demo_forward_pipeline_v1",
         "tier1",
         "2"},
        {"empty pipelineLayout",
         "next_engine_manifest_demo_forward.metal",
         "next_engine_manifest_demo_forward.metallib",
         "vertex_main",
         "fragment_main",
         "material_srg_v1",
         "",
         "tier1",
         "2"},
        {"empty requiredArgumentBufferTier",
         "next_engine_manifest_demo_forward.metal",
         "next_engine_manifest_demo_forward.metallib",
         "vertex_main",
         "fragment_main",
         "material_srg_v1",
         "demo_forward_pipeline_v1",
         "",
         "2"},
        {"empty materialShaderResourceGroupArgumentBufferIndex",
         "next_engine_manifest_demo_forward.metal",
         "next_engine_manifest_demo_forward.metallib",
         "vertex_main",
         "fragment_main",
         "material_srg_v1",
         "demo_forward_pipeline_v1",
         "tier1",
         ""},
    };

    for (const Case& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        ASSERT_TRUE(writeManifest(testCase.source,
                                  testCase.metallib,
                                  testCase.vertexEntry,
                                  testCase.fragmentEntry,
                                  testCase.materialLayout,
                                  testCase.pipelineLayout,
                                  testCase.requiredArgumentBufferTier,
                                  testCase.materialSrgArgumentBufferIndex));

        MetalShaderLibraryManifest manifest;
        EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
        EXPECT_FALSE(manifest.IsValid());
    }

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsEmptyMaterialSrgArgumentLayoutValues) {
    struct Case {
        const char* name;
        const char* uniformIndex;
        const char* textureBaseIndex;
        const char* samplerBaseIndex;
    };

    const Case cases[] = {
        {"empty materialShaderResourceGroupUniformArgumentIndex", "", "1", "6"},
        {"empty materialShaderResourceGroupTextureArgumentBaseIndex", "0", "", "6"},
        {"empty materialShaderResourceGroupSamplerArgumentBaseIndex", "0", "1", ""},
    };

    for (const Case& testCase : cases) {
        SCOPED_TRACE(testCase.name);
        {
            std::ofstream manifest(kManifestOverridePath, std::ios::binary);
            ASSERT_TRUE(manifest.good());
            manifest << "version=1\n";
            manifest << "source=next_engine_manifest_demo_forward.metal\n";
            manifest << "vertexEntry=vertex_main\n";
            manifest << "fragmentEntry=fragment_main_material_srg\n";
            manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
            manifest << "materialShaderResourceGroupUniformArgumentIndex=" << testCase.uniformIndex << "\n";
            manifest << "materialShaderResourceGroupTextureArgumentBaseIndex=" << testCase.textureBaseIndex << "\n";
            manifest << "materialShaderResourceGroupSamplerArgumentBaseIndex=" << testCase.samplerBaseIndex << "\n";
        }

        MetalShaderLibraryManifest manifest;
        EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
        EXPECT_FALSE(manifest.IsValid());
    }

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsInvalidRequiredArgumentBufferTier) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main\n";
        manifest << "requiredArgumentBufferTier=tier3\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsInvalidMaterialSrgArgumentBufferIndex) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=fragment_two\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsInvalidMaterialSrgArgumentLayoutIndex) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main\n";
        manifest << "materialShaderResourceGroupUniformArgumentIndex=uniform_zero\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, RejectsManifestWithoutEntryPoints) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Missing entry manifest\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
    }

    MetalShaderLibraryManifest manifest;
    EXPECT_FALSE(LoadShaderLibraryManifest(kManifestOverridePath, &manifest));
    EXPECT_FALSE(manifest.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, ExplicitUnsupportedManifestDoesNotFallbackToDefaultShader) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=2\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
        manifest << "vertexEntry=future_vertex\n";
        manifest << "fragmentEntry=future_fragment\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, kManifestOverridePath);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestPath, kManifestOverridePath);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, ExplicitInvalidManifestDoesNotFallbackToDefaultShader) {
    {
        std::ofstream manifest(kManifestOverridePath, std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Invalid explicit manifest\n";
        manifest << "source=next_engine_manifest_demo_forward.metal\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, kManifestOverridePath);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestVersion, kMetalShaderManifestVersion);
    EXPECT_EQ(desc.manifestPath, kManifestOverridePath);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::remove(kManifestOverridePath);
}

TEST_F(MetalShaderLibraryInputTest, ExplicitMissingManifestDoesNotFallbackToDefaultShader) {
    std::remove(kMissingManifestOverridePath);

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, kMissingManifestOverridePath);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestPath, kMissingManifestOverridePath);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());
}

TEST_F(MetalShaderLibraryInputTest, DemoForwardPathsHonorEnvironmentOverrides) {
    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, kRelativeOverrideSourcePath);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, kRelativeOverrideMetallibPath);

    const std::filesystem::path expectedSourcePath =
        std::filesystem::absolute(kRelativeOverrideSourcePath).lexically_normal();
    const std::filesystem::path expectedMetallibPath =
        std::filesystem::absolute(kRelativeOverrideMetallibPath).lexically_normal();

    EXPECT_EQ(std::filesystem::path(DemoForwardShaderPath()), expectedSourcePath);
    EXPECT_EQ(std::filesystem::path(DemoForwardMetallibPath()), expectedMetallibPath);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(std::filesystem::path(desc.sourcePath), expectedSourcePath);
    EXPECT_EQ(std::filesystem::path(desc.metallibPath), expectedMetallibPath);
    EXPECT_EQ(desc.vertexEntryPoint, "vertex_main");
    EXPECT_EQ(desc.fragmentEntryPoint, "fragment_main_material_srg");
    EXPECT_EQ(desc.materialLayout, kMetalDemoForwardMaterialLayoutName);
    EXPECT_EQ(desc.pipelineLayout, kMetalDemoForwardPipelineLayoutName);
    EXPECT_EQ(desc.requiredArgumentBufferTier, kMetalDemoForwardRequiredArgumentBufferTier);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex,
              kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex);
    ExpectDemoForwardMaterialSrgArgumentLayout(desc);
}

TEST_F(MetalShaderLibraryInputTest, DemoForwardDefaultsHonorRuntimeShaderDirectory) {
    const std::filesystem::path shaderDirectory(kShaderDirectoryOverride);
    std::filesystem::remove_all(shaderDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(shaderDirectory));

    {
        std::ofstream manifest(shaderDirectory / "demo_forward.shader_manifest", std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Runtime shader dir manifest\n";
        manifest << "source=demo_forward.metal\n";
        manifest << "vertexEntry=runtime_vertex\n";
        manifest << "fragmentEntry=runtime_fragment\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
        WriteDemoForwardMaterialSrgArgumentLayout(manifest);
    }
    {
        std::ofstream source(shaderDirectory / "demo_forward.metal", std::ios::binary);
        ASSERT_TRUE(source.good());
        source << "#include <metal_stdlib>\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, kShaderDirectoryOverride);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const std::filesystem::path expectedManifest =
        std::filesystem::absolute(shaderDirectory / "demo_forward.shader_manifest").lexically_normal();
    const std::filesystem::path expectedSource =
        std::filesystem::absolute(shaderDirectory / "demo_forward.metal").lexically_normal();

    EXPECT_EQ(std::filesystem::path(DemoForwardShaderManifestPath()), expectedManifest);
    EXPECT_EQ(std::filesystem::path(DemoForwardShaderPath()), expectedSource);

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestVersion, kMetalShaderManifestVersion);
    EXPECT_EQ(desc.debugName, "Runtime shader dir manifest");
    EXPECT_EQ(std::filesystem::path(desc.manifestPath), expectedManifest);
    EXPECT_EQ(std::filesystem::path(desc.sourcePath), expectedSource);
    EXPECT_EQ(desc.vertexEntryPoint, "runtime_vertex");
    EXPECT_EQ(desc.fragmentEntryPoint, "runtime_fragment");
    EXPECT_EQ(desc.materialLayout, kMetalDemoForwardMaterialLayoutName);
    EXPECT_EQ(desc.pipelineLayout, kMetalDemoForwardPipelineLayoutName);
    EXPECT_EQ(desc.requiredArgumentBufferTier, kMetalDemoForwardRequiredArgumentBufferTier);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex,
              kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex);
    ExpectDemoForwardMaterialSrgArgumentLayout(desc);

    std::filesystem::remove_all(shaderDirectory);
}

TEST_F(MetalShaderLibraryInputTest, RuntimeShaderDirectoryMissingMaterialLayoutDoesNotFallbackToDefaults) {
    const std::filesystem::path shaderDirectory(kShaderDirectoryOverride);
    std::filesystem::remove_all(shaderDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(shaderDirectory));

    {
        std::ofstream manifest(shaderDirectory / "demo_forward.shader_manifest", std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Runtime shader dir missing material layout\n";
        manifest << "source=demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
        WriteDemoForwardMaterialSrgArgumentLayout(manifest);
    }
    {
        std::ofstream source(shaderDirectory / "demo_forward.metal", std::ios::binary);
        ASSERT_TRUE(source.good());
        source << "#include <metal_stdlib>\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, kShaderDirectoryOverride);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const std::filesystem::path expectedManifest =
        std::filesystem::absolute(shaderDirectory / "demo_forward.shader_manifest").lexically_normal();

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(std::filesystem::path(desc.manifestPath), expectedManifest);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::filesystem::remove_all(shaderDirectory);
}

TEST_F(MetalShaderLibraryInputTest, RuntimeShaderDirectoryMissingPipelineLayoutDoesNotFallbackToDefaults) {
    const std::filesystem::path shaderDirectory(kShaderDirectoryOverride);
    std::filesystem::remove_all(shaderDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(shaderDirectory));

    {
        std::ofstream manifest(shaderDirectory / "demo_forward.shader_manifest", std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Runtime shader dir missing pipeline layout\n";
        manifest << "source=demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
        WriteDemoForwardMaterialSrgArgumentLayout(manifest);
    }
    {
        std::ofstream source(shaderDirectory / "demo_forward.metal", std::ios::binary);
        ASSERT_TRUE(source.good());
        source << "#include <metal_stdlib>\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, kShaderDirectoryOverride);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const std::filesystem::path expectedManifest =
        std::filesystem::absolute(shaderDirectory / "demo_forward.shader_manifest").lexically_normal();

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(std::filesystem::path(desc.manifestPath), expectedManifest);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::filesystem::remove_all(shaderDirectory);
}

TEST_F(MetalShaderLibraryInputTest, RuntimeShaderDirectoryMissingRequiredArgumentBufferTierDoesNotFallbackToDefaults) {
    const std::filesystem::path shaderDirectory(kShaderDirectoryOverride);
    std::filesystem::remove_all(shaderDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(shaderDirectory));

    {
        std::ofstream manifest(shaderDirectory / "demo_forward.shader_manifest", std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Runtime shader dir missing required argument buffer tier\n";
        manifest << "source=demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
        WriteDemoForwardMaterialSrgArgumentLayout(manifest);
    }
    {
        std::ofstream source(shaderDirectory / "demo_forward.metal", std::ios::binary);
        ASSERT_TRUE(source.good());
        source << "#include <metal_stdlib>\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, kShaderDirectoryOverride);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const std::filesystem::path expectedManifest =
        std::filesystem::absolute(shaderDirectory / "demo_forward.shader_manifest").lexically_normal();

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(std::filesystem::path(desc.manifestPath), expectedManifest);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::filesystem::remove_all(shaderDirectory);
}

TEST_F(MetalShaderLibraryInputTest, RuntimeShaderDirectoryMissingMaterialSrgIndexDoesNotFallbackToDefaults) {
    const std::filesystem::path shaderDirectory(kShaderDirectoryOverride);
    std::filesystem::remove_all(shaderDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(shaderDirectory));

    {
        std::ofstream manifest(shaderDirectory / "demo_forward.shader_manifest", std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Runtime shader dir missing material SRG index\n";
        manifest << "source=demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
    }
    {
        std::ofstream source(shaderDirectory / "demo_forward.metal", std::ios::binary);
        ASSERT_TRUE(source.good());
        source << "#include <metal_stdlib>\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, kShaderDirectoryOverride);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const std::filesystem::path expectedManifest =
        std::filesystem::absolute(shaderDirectory / "demo_forward.shader_manifest").lexically_normal();

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(std::filesystem::path(desc.manifestPath), expectedManifest);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::filesystem::remove_all(shaderDirectory);
}

TEST_F(MetalShaderLibraryInputTest, RuntimeShaderDirectoryMissingMaterialSrgArgumentLayoutDoesNotFallbackToDefaults) {
    const std::filesystem::path shaderDirectory(kShaderDirectoryOverride);
    std::filesystem::remove_all(shaderDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(shaderDirectory));

    {
        std::ofstream manifest(shaderDirectory / "demo_forward.shader_manifest", std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=1\n";
        manifest << "debugName=Runtime shader dir missing material SRG argument layout\n";
        manifest << "source=demo_forward.metal\n";
        manifest << "vertexEntry=vertex_main\n";
        manifest << "fragmentEntry=fragment_main_material_srg\n";
        manifest << "materialLayout=material_srg_v1\n";
        manifest << "pipelineLayout=demo_forward_pipeline_v1\n";
        manifest << "requiredArgumentBufferTier=tier1\n";
        manifest << "materialShaderResourceGroupArgumentBufferIndex=2\n";
    }
    {
        std::ofstream source(shaderDirectory / "demo_forward.metal", std::ios::binary);
        ASSERT_TRUE(source.good());
        source << "#include <metal_stdlib>\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, kShaderDirectoryOverride);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const std::filesystem::path expectedManifest =
        std::filesystem::absolute(shaderDirectory / "demo_forward.shader_manifest").lexically_normal();

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(std::filesystem::path(desc.manifestPath), expectedManifest);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::filesystem::remove_all(shaderDirectory);
}

TEST_F(MetalShaderLibraryInputTest, RuntimeShaderDirectoryInvalidManifestDoesNotFallbackToDefaults) {
    const std::filesystem::path shaderDirectory(kShaderDirectoryOverride);
    std::filesystem::remove_all(shaderDirectory);
    ASSERT_TRUE(std::filesystem::create_directories(shaderDirectory));

    {
        std::ofstream manifest(shaderDirectory / "demo_forward.shader_manifest", std::ios::binary);
        ASSERT_TRUE(manifest.good());
        manifest << "version=2\n";
        manifest << "source=demo_forward.metal\n";
        manifest << "vertexEntry=future_vertex\n";
        manifest << "fragmentEntry=future_fragment\n";
    }
    {
        std::ofstream source(shaderDirectory / "demo_forward.metal", std::ios::binary);
        ASSERT_TRUE(source.good());
        source << "#include <metal_stdlib>\n";
    }

    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, kShaderDirectoryOverride);
    ScopedEnvironmentVariable manifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable sourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable metallibOverride(kMetallibOverrideEnv, nullptr);

    const std::filesystem::path expectedManifest =
        std::filesystem::absolute(shaderDirectory / "demo_forward.shader_manifest").lexically_normal();

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(std::filesystem::path(desc.manifestPath), expectedManifest);
    EXPECT_TRUE(desc.sourcePath.empty());
    EXPECT_TRUE(desc.metallibPath.empty());
    EXPECT_TRUE(desc.vertexEntryPoint.empty());
    EXPECT_TRUE(desc.fragmentEntryPoint.empty());
    EXPECT_TRUE(desc.materialLayout.empty());
    EXPECT_TRUE(desc.pipelineLayout.empty());
    EXPECT_EQ(desc.requiredArgumentBufferTier, RHI::ArgumentBufferTier::Unsupported);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex, kMetalShaderManifestInvalidBindingIndex);
    ExpectInvalidMaterialSrgArgumentLayout(desc);

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(input.IsValid());

    std::filesystem::remove_all(shaderDirectory);
}

TEST_F(MetalShaderLibraryInputTest, DemoForwardDefaultsPointToReadableStagedAssets) {
    ScopedEnvironmentVariable shaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable clearManifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearSourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearMetallibOverride(kMetallibOverrideEnv, nullptr);

    std::ifstream manifest(DemoForwardShaderManifestPath(), std::ios::binary);
    std::ifstream source(DemoForwardShaderPath(), std::ios::binary);

    EXPECT_TRUE(manifest.good());
    EXPECT_TRUE(source.good());

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    EXPECT_EQ(desc.manifestVersion, kMetalShaderManifestVersion);
    EXPECT_EQ(desc.manifestPath, DemoForwardShaderManifestPath());
    EXPECT_EQ(desc.sourcePath, DemoForwardShaderPath());
    EXPECT_EQ(desc.vertexEntryPoint, "vertex_main");
    EXPECT_EQ(desc.fragmentEntryPoint, "fragment_main_material_srg");
    EXPECT_EQ(desc.materialLayout, kMetalDemoForwardMaterialLayoutName);
    EXPECT_EQ(desc.pipelineLayout, kMetalDemoForwardPipelineLayoutName);
    EXPECT_EQ(desc.requiredArgumentBufferTier, kMetalDemoForwardRequiredArgumentBufferTier);
    EXPECT_EQ(desc.materialShaderResourceGroupArgumentBufferIndex,
              kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex);
    ExpectDemoForwardMaterialSrgArgumentLayout(desc);
}

TEST_F(MetalShaderLibraryInputTest, EmptyEnvironmentOverridesAreIgnored) {
    ScopedEnvironmentVariable clearShaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable clearManifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearSourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearMetallibOverride(kMetallibOverrideEnv, nullptr);
    const std::string defaultSourcePath = DemoForwardShaderPath();
    const char* defaultMetallibPathPointer = DemoForwardMetallibPath();
    const std::string defaultMetallibPath = defaultMetallibPathPointer ? defaultMetallibPathPointer : "";

    ScopedEnvironmentVariable emptySourceOverride(kSourceOverrideEnv, "");
    ScopedEnvironmentVariable emptyMetallibOverride(kMetallibOverrideEnv, "");

    EXPECT_STREQ(DemoForwardShaderPath(), defaultSourcePath.c_str());
    if (!defaultMetallibPath.empty()) {
        EXPECT_STREQ(DemoForwardMetallibPath(), defaultMetallibPath.c_str());
    } else {
        EXPECT_EQ(DemoForwardMetallibPath(), nullptr);
    }
}

TEST_F(MetalShaderLibraryInputTest, PrefersExistingMetallibPath) {
    {
        std::ofstream marker(kExistingMetallibPath, std::ios::binary);
        ASSERT_TRUE(marker.good());
        marker << "not a real metallib";
    }

    MetalShaderLibraryDesc desc;
    desc.debugName = "test shader";
    desc.metallibPath = kExistingMetallibPath;
    desc.sourcePath = "fallback.metal";

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::Metallib);
    EXPECT_EQ(input.path, kExistingMetallibPath);
    EXPECT_TRUE(input.IsValid());

    std::remove(kExistingMetallibPath);
}

TEST_F(MetalShaderLibraryInputTest, FallsBackToSourceWhenMetallibIsMissing) {
    ScopedEnvironmentVariable clearShaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable clearManifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearSourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearMetallibOverride(kMetallibOverrideEnv, nullptr);

    MetalShaderLibraryDesc desc;
    desc.debugName = "test shader";
    desc.metallibPath = kMissingMetallibPath;
    desc.sourcePath = DemoForwardShaderPath();

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::Source);
    EXPECT_EQ(input.path, DemoForwardShaderPath());
    EXPECT_TRUE(input.IsValid());
}

TEST_F(MetalShaderLibraryInputTest, RejectsEmptyDescriptor) {
    const MetalShaderLibraryInput input = SelectShaderLibraryInput({});
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_TRUE(input.path.empty());
    EXPECT_FALSE(input.IsValid());
}

TEST_F(MetalShaderLibraryInputTest, RejectsMissingSourceFallback) {
    MetalShaderLibraryDesc desc;
    desc.debugName = "test shader";
    desc.metallibPath = kMissingMetallibPath;
    desc.sourcePath = kMissingSourcePath;

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    EXPECT_EQ(input.kind, MetalShaderLibraryInputKind::None);
    EXPECT_TRUE(input.path.empty());
    EXPECT_FALSE(input.IsValid());
}

TEST_F(MetalShaderLibraryInputTest, CreatesDemoForwardLibraryThroughDescriptorSelection) {
    ScopedEnvironmentVariable clearShaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable clearManifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearSourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearMetallibOverride(kMetallibOverrideEnv, nullptr);

    MetalDevice device;
    if (!device.Initialize()) {
        GTEST_SKIP() << "Metal device unavailable";
    }

    const MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    MetalShaderLibraryInput loadedInput;
    id<MTLLibrary> library = CreateShaderLibrary(device, desc, &loadedInput);
    EXPECT_NE(library, nil);
    EXPECT_TRUE(loadedInput.IsValid());
    EXPECT_STREQ([[library label] UTF8String], desc.debugName.c_str());
    if (!desc.metallibPath.empty() && std::filesystem::exists(desc.metallibPath)) {
        EXPECT_EQ(loadedInput.kind, MetalShaderLibraryInputKind::Metallib);
        EXPECT_EQ(loadedInput.path, desc.metallibPath);
    } else {
        EXPECT_EQ(loadedInput.kind, MetalShaderLibraryInputKind::Source);
        EXPECT_EQ(loadedInput.path, DemoForwardShaderPath());
    }

    library = nil;
    device.Shutdown();
}

TEST_F(MetalShaderLibraryInputTest, RejectsCompiledLibraryWithMissingEntryPoints) {
    ScopedEnvironmentVariable clearShaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable clearManifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearSourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearMetallibOverride(kMetallibOverrideEnv, nullptr);

    MetalDevice device;
    if (!device.Initialize()) {
        GTEST_SKIP() << "Metal device unavailable";
    }

    MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    desc.vertexEntryPoint = "missing_vertex_entry";
    desc.fragmentEntryPoint = "missing_fragment_entry";

    MetalShaderLibraryInput loadedInput;
    id<MTLLibrary> library = CreateShaderLibrary(device, desc, &loadedInput);
    EXPECT_EQ(library, nil);
    EXPECT_EQ(loadedInput.kind, MetalShaderLibraryInputKind::None);
    EXPECT_FALSE(loadedInput.IsValid());

    device.Shutdown();
}

TEST_F(MetalShaderLibraryInputTest, FallsBackToSourceWhenExistingMetallibCannotLoad) {
    ScopedEnvironmentVariable clearShaderDirectoryOverride(kShaderDirectoryEnv, nullptr);
    ScopedEnvironmentVariable clearManifestOverride(kManifestOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearSourceOverride(kSourceOverrideEnv, nullptr);
    ScopedEnvironmentVariable clearMetallibOverride(kMetallibOverrideEnv, nullptr);

    {
        std::ofstream marker(kInvalidMetallibPath, std::ios::binary);
        ASSERT_TRUE(marker.good());
        marker << "not a real metallib";
    }

    MetalDevice device;
    if (!device.Initialize()) {
        std::remove(kInvalidMetallibPath);
        GTEST_SKIP() << "Metal device unavailable";
    }

    MetalShaderLibraryDesc desc = DemoForwardShaderLibraryDesc();
    desc.debugName = "test invalid metallib fallback";
    desc.metallibPath = kInvalidMetallibPath;

    MetalShaderLibraryInput loadedInput;
    id<MTLLibrary> library = CreateShaderLibrary(device, desc, &loadedInput);
    EXPECT_NE(library, nil);
    EXPECT_EQ(loadedInput.kind, MetalShaderLibraryInputKind::Source);
    EXPECT_EQ(loadedInput.path, DemoForwardShaderPath());
    EXPECT_TRUE(loadedInput.IsValid());
    EXPECT_STREQ([[library label] UTF8String], desc.debugName.c_str());

    library = nil;
    device.Shutdown();
    std::remove(kInvalidMetallibPath);
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
