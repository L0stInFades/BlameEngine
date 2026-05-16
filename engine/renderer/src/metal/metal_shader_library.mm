#include "metal_shader_library.h"

#include "next/foundation/logger.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace Next {
namespace MetalBackend {

namespace {

constexpr const char* kDefaultDemoForwardDebugName = "NEXT demo forward shader";
constexpr const char* kDefaultDemoForwardVertexEntryPoint = "vertex_main";
constexpr const char* kDefaultDemoForwardFragmentEntryPoint = "fragment_main_material_srg";
constexpr const char* kMetalShaderDirectoryEnv = "NEXT_METAL_SHADER_DIR";
constexpr const char* kDemoForwardShaderManifestPathEnv = "NEXT_METAL_DEMO_FORWARD_MANIFEST";
constexpr const char* kDemoForwardShaderPathEnv = "NEXT_METAL_DEMO_FORWARD_SOURCE";
constexpr const char* kDemoForwardMetallibPathEnv = "NEXT_METAL_DEMO_FORWARD_METALLIB";

const char* NonEmptyEnvironmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value && value[0] != '\0' ? value : nullptr;
}

std::filesystem::path ExecutableDirectory() {
#if defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (size == 0) {
        return {};
    }

    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return {};
    }
    if (!path.empty() && path.back() == '\0') {
        path.pop_back();
    }

    std::error_code ec;
    std::filesystem::path executablePath = std::filesystem::weakly_canonical(path, ec);
    if (ec) {
        executablePath = std::filesystem::absolute(path, ec);
    }
    return ec ? std::filesystem::path(path).parent_path() : executablePath.parent_path();
#else
    return {};
#endif
}

void PushUniquePath(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path) {
    if (path.empty()) {
        return;
    }

    const std::filesystem::path normalized = path.lexically_normal();
    for (const auto& existing : paths) {
        if (existing == normalized) {
            return;
        }
    }
    paths.push_back(normalized);
}

void AddShaderDirectoryCandidates(std::vector<std::filesystem::path>& paths, const std::filesystem::path& root) {
    PushUniquePath(paths, root / "engine" / "renderer" / "shaders" / "metal");
    PushUniquePath(paths, root / "shaders" / "metal");
    PushUniquePath(paths, root / "Resources" / "shaders" / "metal");
}

std::filesystem::path CompileTimeShaderDirectory() {
#ifdef NEXT_METAL_SHADER_DIR
    return NEXT_METAL_SHADER_DIR;
#else
    return "engine/renderer/shaders/metal";
#endif
}

std::string FindRuntimeShaderFile(const char* fileName) {
    std::error_code ec;
    std::vector<std::filesystem::path> shaderDirectories;

    if (const char* runtimeShaderDirectory = NonEmptyEnvironmentValue(kMetalShaderDirectoryEnv)) {
        PushUniquePath(shaderDirectories, runtimeShaderDirectory);
    }

    std::filesystem::path probe = ExecutableDirectory();
    for (int i = 0; i < 8 && !probe.empty(); ++i) {
        AddShaderDirectoryCandidates(shaderDirectories, probe);
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }

    probe = std::filesystem::current_path(ec);
    for (int i = 0; i < 4 && !probe.empty(); ++i) {
        AddShaderDirectoryCandidates(shaderDirectories, probe);
        const std::filesystem::path parent = probe.parent_path();
        if (parent == probe) {
            break;
        }
        probe = parent;
    }

    PushUniquePath(shaderDirectories, CompileTimeShaderDirectory());

    for (const auto& directory : shaderDirectories) {
        const std::filesystem::path candidate = directory / fileName;
        if (std::filesystem::exists(candidate, ec)) {
            const std::filesystem::path absoluteCandidate = std::filesystem::absolute(candidate, ec);
            return (ec ? candidate : absoluteCandidate).lexically_normal().string();
        }
    }

    return (CompileTimeShaderDirectory() / fileName).lexically_normal().string();
}

const char* DefaultDemoForwardShaderManifestPath() {
    static std::string path;
    path = FindRuntimeShaderFile("demo_forward.shader_manifest");
    return path.c_str();
}

const char* DefaultDemoForwardShaderPath() {
    static std::string path;
    path = FindRuntimeShaderFile("demo_forward.metal");
    return path.c_str();
}

const char* DefaultDemoForwardMetallibPath() {
#ifdef NEXT_METAL_SHADER_LIBRARY_PATH
    return NEXT_METAL_SHADER_LIBRARY_PATH;
#else
    return nullptr;
#endif
}

bool FileExists(const char* path) {
    if (!path || path[0] == '\0') {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    return file.good();
}

std::string Trim(const std::string& value) {
    const std::string whitespace = " \t\r\n";
    const std::size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return {};
    }

    const std::size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

bool ParseManifestVersion(const std::string& value, uint32_t* outVersion) {
    if (outVersion) {
        *outVersion = 0;
    }
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return false;
    }

    uint64_t parsed = 0;
    for (char c : trimmed) {
        if (c < '0' || c > '9') {
            return false;
        }
        parsed = parsed * 10u + static_cast<uint64_t>(c - '0');
        if (parsed > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
    }

    if (outVersion) {
        *outVersion = static_cast<uint32_t>(parsed);
    }
    return true;
}

bool ParseManifestUint32(const std::string& value, uint32_t* outValue) {
    if (outValue) {
        *outValue = 0;
    }
    const std::string trimmed = Trim(value);
    if (trimmed.empty()) {
        return false;
    }

    uint64_t parsed = 0;
    for (char c : trimmed) {
        if (c < '0' || c > '9') {
            return false;
        }
        parsed = parsed * 10u + static_cast<uint64_t>(c - '0');
        if (parsed > std::numeric_limits<uint32_t>::max()) {
            return false;
        }
    }

    if (outValue) {
        *outValue = static_cast<uint32_t>(parsed);
    }
    return true;
}

bool ParseArgumentBufferTierName(const std::string& value, RHI::ArgumentBufferTier* outTier) {
    if (outTier) {
        *outTier = RHI::ArgumentBufferTier::Unsupported;
    }

    const std::string trimmed = Trim(value);
    if (trimmed == RHI::ArgumentBufferTierName(RHI::ArgumentBufferTier::Unsupported)) {
        return true;
    }
    if (trimmed == RHI::ArgumentBufferTierName(RHI::ArgumentBufferTier::Tier1)) {
        if (outTier) {
            *outTier = RHI::ArgumentBufferTier::Tier1;
        }
        return true;
    }
    if (trimmed == RHI::ArgumentBufferTierName(RHI::ArgumentBufferTier::Tier2)) {
        if (outTier) {
            *outTier = RHI::ArgumentBufferTier::Tier2;
        }
        return true;
    }
    return false;
}

bool IsKnownManifestKey(const std::string& key) {
    return key == "version" ||
           key == "debugName" ||
           key == "source" ||
           key == "metallib" ||
           key == "vertexEntry" ||
           key == "fragmentEntry" ||
           key == "materialLayout" ||
           key == "pipelineLayout" ||
           key == "requiredArgumentBufferTier" ||
           key == "materialShaderResourceGroupArgumentBufferIndex" ||
           key == "materialShaderResourceGroupUniformArgumentIndex" ||
           key == "materialShaderResourceGroupTextureArgumentBaseIndex" ||
           key == "materialShaderResourceGroupSamplerArgumentBaseIndex";
}

bool ManifestKeyRequiresValue(const std::string& key) {
    return key == "version" ||
           key == "source" ||
           key == "metallib" ||
           key == "vertexEntry" ||
           key == "fragmentEntry" ||
           key == "materialLayout" ||
           key == "pipelineLayout" ||
           key == "requiredArgumentBufferTier" ||
           key == "materialShaderResourceGroupArgumentBufferIndex" ||
           key == "materialShaderResourceGroupUniformArgumentIndex" ||
           key == "materialShaderResourceGroupTextureArgumentBaseIndex" ||
           key == "materialShaderResourceGroupSamplerArgumentBaseIndex";
}

bool RecordManifestKey(std::vector<std::string>& parsedKeys,
                       const std::string& key,
                       uint32_t lineNumber,
                       const char* manifestPath) {
    for (const std::string& parsedKey : parsedKeys) {
        if (parsedKey == key) {
            NEXT_LOG_ERROR("Duplicate Metal shader manifest key '%s' on line %u in '%s'",
                           key.c_str(),
                           lineNumber,
                           manifestPath);
            return false;
        }
    }

    parsedKeys.push_back(key);
    return true;
}

std::string NormalizePath(const char* rawPath) {
    if (!rawPath || rawPath[0] == '\0') {
        return {};
    }

    std::error_code ec;
    const std::filesystem::path path(rawPath);
    const std::filesystem::path absolutePath = std::filesystem::absolute(path, ec);
    return (ec ? path : absolutePath).lexically_normal().string();
}

std::string NormalizeManifestPath(const char* manifestPath) {
    return NormalizePath(manifestPath);
}

std::string NormalizeShaderInputPath(const char* inputPath) {
    return NormalizePath(inputPath);
}

bool PathIsInsideDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) {
    const std::filesystem::path normalizedPath = path.lexically_normal();
    const std::filesystem::path normalizedDirectory = directory.lexically_normal();

    auto pathIt = normalizedPath.begin();
    const auto pathEnd = normalizedPath.end();
    for (const auto& directoryPart : normalizedDirectory) {
        if (pathIt == pathEnd || *pathIt != directoryPart) {
            return false;
        }
        ++pathIt;
    }
    return true;
}

bool ResolveManifestInputPath(const char* manifestPath,
                              const std::string& key,
                              const std::string& value,
                              uint32_t lineNumber,
                              std::string* outPath) {
    if (outPath) {
        outPath->clear();
    }
    if (value.empty()) {
        return true;
    }

    const std::filesystem::path inputPath(value);
    if (inputPath.is_absolute()) {
        NEXT_LOG_ERROR("Metal shader manifest key '%s' on line %u in '%s' must use a manifest-relative path",
                       key.c_str(),
                       lineNumber,
                       manifestPath);
        return false;
    }

    const std::filesystem::path manifestDirectory =
        std::filesystem::path(manifestPath).parent_path().lexically_normal();
    const std::filesystem::path resolvedPath = (manifestDirectory / inputPath).lexically_normal();
    if (!PathIsInsideDirectory(resolvedPath, manifestDirectory)) {
        NEXT_LOG_ERROR("Metal shader manifest key '%s' on line %u in '%s' escapes the manifest directory: '%s'",
                       key.c_str(),
                       lineNumber,
                       manifestPath,
                       value.c_str());
        return false;
    }

    if (outPath) {
        *outPath = resolvedPath.string();
    }
    return true;
}

const char* EmptyToNull(const std::string& value) {
    return value.empty() ? nullptr : value.c_str();
}

NSString* MakeShaderLibraryLabel(const char* debugName) {
    return (debugName && debugName[0] != '\0') ? [NSString stringWithUTF8String:debugName] : nil;
}

void SetShaderLibraryLabel(id<MTLLibrary> library, const char* debugName) {
    if (!library) {
        return;
    }

    if (NSString* label = MakeShaderLibraryLabel(debugName)) {
        library.label = label;
    }
}

bool ShaderLibraryHasEntryPoint(id<MTLLibrary> library,
                                const std::string& entryPoint,
                                const char* stageName,
                                const char* debugName) {
    if (!library) {
        return false;
    }
    if (entryPoint.empty()) {
        NEXT_LOG_ERROR("Metal shader library '%s' is missing a %s entry point name",
                       debugName ? debugName : "unnamed",
                       stageName);
        return false;
    }

    @autoreleasepool {
        NSString* functionName = [NSString stringWithUTF8String:entryPoint.c_str()];
        if (!functionName) {
            NEXT_LOG_ERROR("Metal shader %s entry point is not valid UTF-8: '%s'",
                           stageName,
                           entryPoint.c_str());
            return false;
        }

        id<MTLFunction> function = [library newFunctionWithName:functionName];
        if (!function) {
            NEXT_LOG_ERROR("Metal shader library '%s' does not contain %s entry point '%s'",
                           debugName ? debugName : "unnamed",
                           stageName,
                           entryPoint.c_str());
            return false;
        }
        return true;
    }
}

bool ValidateShaderLibraryEntryPoints(id<MTLLibrary> library, const MetalShaderLibraryDesc& desc) {
    const char* debugName = EmptyToNull(desc.debugName);
    const bool hasVertex = ShaderLibraryHasEntryPoint(library, desc.vertexEntryPoint, "vertex", debugName);
    const bool hasFragment = ShaderLibraryHasEntryPoint(library, desc.fragmentEntryPoint, "fragment", debugName);
    return hasVertex && hasFragment;
}

bool ValidateDemoForwardMaterialLayout(const MetalShaderLibraryManifest& manifest) {
    if (manifest.materialLayout == kMetalDemoForwardMaterialLayoutName) {
        return true;
    }

    NEXT_LOG_ERROR("Metal demo forward shader manifest '%s' declared materialLayout '%s' (expected '%s')",
                   manifest.manifestPath.c_str(),
                   manifest.materialLayout.empty() ? "<missing>" : manifest.materialLayout.c_str(),
                   kMetalDemoForwardMaterialLayoutName);
    return false;
}

bool ValidateDemoForwardPipelineLayout(const MetalShaderLibraryManifest& manifest) {
    if (manifest.pipelineLayout == kMetalDemoForwardPipelineLayoutName) {
        return true;
    }

    NEXT_LOG_ERROR("Metal demo forward shader manifest '%s' declared pipelineLayout '%s' (expected '%s')",
                   manifest.manifestPath.c_str(),
                   manifest.pipelineLayout.empty() ? "<missing>" : manifest.pipelineLayout.c_str(),
                   kMetalDemoForwardPipelineLayoutName);
    return false;
}

bool ValidateDemoForwardRequiredArgumentBufferTier(const MetalShaderLibraryManifest& manifest) {
    if (manifest.requiredArgumentBufferTier == kMetalDemoForwardRequiredArgumentBufferTier) {
        return true;
    }

    NEXT_LOG_ERROR("Metal demo forward shader manifest '%s' declared requiredArgumentBufferTier '%s' (expected '%s')",
                   manifest.manifestPath.c_str(),
                   RHI::ArgumentBufferTierName(manifest.requiredArgumentBufferTier),
                   RHI::ArgumentBufferTierName(kMetalDemoForwardRequiredArgumentBufferTier));
    return false;
}

bool ValidateDemoForwardMaterialShaderResourceGroupArgumentBufferIndex(
    const MetalShaderLibraryManifest& manifest) {
    if (manifest.materialShaderResourceGroupArgumentBufferIndex ==
        kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex) {
        return true;
    }

    const std::string declared =
        manifest.materialShaderResourceGroupArgumentBufferIndex == kMetalShaderManifestInvalidBindingIndex
            ? "<missing>"
            : std::to_string(manifest.materialShaderResourceGroupArgumentBufferIndex);
    NEXT_LOG_ERROR("Metal demo forward shader manifest '%s' declared "
                   "materialShaderResourceGroupArgumentBufferIndex '%s' (expected %u)",
                   manifest.manifestPath.c_str(),
                   declared.c_str(),
                   kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex);
    return false;
}

std::string ManifestUint32OrMissing(uint32_t value) {
    return value == kMetalShaderManifestInvalidBindingIndex ? "<missing>" : std::to_string(value);
}

bool ValidateDemoForwardMaterialShaderResourceGroupArgumentLayout(
    const MetalShaderLibraryManifest& manifest) {
    if (manifest.materialShaderResourceGroupUniformArgumentIndex ==
            kMetalDemoForwardMaterialShaderResourceGroupUniformArgumentIndex &&
        manifest.materialShaderResourceGroupTextureArgumentBaseIndex ==
            kMetalDemoForwardMaterialShaderResourceGroupTextureArgumentBaseIndex &&
        manifest.materialShaderResourceGroupSamplerArgumentBaseIndex ==
            kMetalDemoForwardMaterialShaderResourceGroupSamplerArgumentBaseIndex) {
        return true;
    }

    const std::string uniformIndex =
        ManifestUint32OrMissing(manifest.materialShaderResourceGroupUniformArgumentIndex);
    const std::string textureBase =
        ManifestUint32OrMissing(manifest.materialShaderResourceGroupTextureArgumentBaseIndex);
    const std::string samplerBase =
        ManifestUint32OrMissing(manifest.materialShaderResourceGroupSamplerArgumentBaseIndex);
    NEXT_LOG_ERROR("Metal demo forward shader manifest '%s' declared material SRG argument layout "
                   "uniform=%s textureBase=%s samplerBase=%s (expected %u/%u/%u)",
                   manifest.manifestPath.c_str(),
                   uniformIndex.c_str(),
                   textureBase.c_str(),
                   samplerBase.c_str(),
                   kMetalDemoForwardMaterialShaderResourceGroupUniformArgumentIndex,
                   kMetalDemoForwardMaterialShaderResourceGroupTextureArgumentBaseIndex,
                   kMetalDemoForwardMaterialShaderResourceGroupSamplerArgumentBaseIndex);
    return false;
}

id<MTLLibrary> CreateShaderLibraryFromMetallibFile(MetalDevice& device,
                                                   const char* libraryPath,
                                                   const char* debugName) {
    if (!device.NativeDevice()) {
        NEXT_LOG_ERROR("Cannot load Metal shader library without a native device");
        return nil;
    }
    if (!libraryPath || libraryPath[0] == '\0') {
        return nil;
    }

    @autoreleasepool {
        NSString* metalLibraryPath = [NSString stringWithUTF8String:libraryPath];
        if (!metalLibraryPath) {
            NEXT_LOG_ERROR("Metal shader library path is not valid UTF-8: '%s'", libraryPath);
            return nil;
        }

        NSURL* metalLibraryUrl = [NSURL fileURLWithPath:metalLibraryPath];
        if (!metalLibraryUrl) {
            NEXT_LOG_ERROR("Failed to create URL for Metal shader library '%s'", libraryPath);
            return nil;
        }

        NSError* libraryError = nil;
        id<MTLLibrary> library = [device.NativeDevice() newLibraryWithURL:metalLibraryUrl
                                                                    error:&libraryError];
        if (!library) {
            NEXT_LOG_WARNING("Failed to load Metal shader library '%s' (%s): %s",
                             libraryPath,
                             debugName ? debugName : "unnamed",
                             libraryError ? [[libraryError localizedDescription] UTF8String] : "unknown error");
            return nil;
        }

        SetShaderLibraryLabel(library, debugName);
        return library;
    }
}

} // namespace

MetalShaderLibraryDesc MetalShaderLibraryManifest::ToDesc() const {
    MetalShaderLibraryDesc desc;
    desc.manifestVersion = version;
    desc.debugName = debugName;
    desc.manifestPath = manifestPath;
    desc.metallibPath = metallibPath;
    desc.sourcePath = sourcePath;
    desc.vertexEntryPoint = vertexEntryPoint;
    desc.fragmentEntryPoint = fragmentEntryPoint;
    desc.materialLayout = materialLayout;
    desc.pipelineLayout = pipelineLayout;
    desc.requiredArgumentBufferTier = requiredArgumentBufferTier;
    desc.materialShaderResourceGroupArgumentBufferIndex = materialShaderResourceGroupArgumentBufferIndex;
    desc.materialShaderResourceGroupUniformArgumentIndex = materialShaderResourceGroupUniformArgumentIndex;
    desc.materialShaderResourceGroupTextureArgumentBaseIndex = materialShaderResourceGroupTextureArgumentBaseIndex;
    desc.materialShaderResourceGroupSamplerArgumentBaseIndex = materialShaderResourceGroupSamplerArgumentBaseIndex;
    return desc;
}

const char* DemoForwardShaderManifestPath() {
    if (const char* overridePath = NonEmptyEnvironmentValue(kDemoForwardShaderManifestPathEnv)) {
        return overridePath;
    }
    return DefaultDemoForwardShaderManifestPath();
}

bool LoadShaderLibraryManifest(const char* manifestPath, MetalShaderLibraryManifest* outManifest) {
    if (outManifest) {
        *outManifest = {};
    }
    if (!manifestPath || manifestPath[0] == '\0') {
        NEXT_LOG_ERROR("Cannot load Metal shader manifest without a path");
        return false;
    }

    std::ifstream file(manifestPath, std::ios::binary);
    if (!file) {
        NEXT_LOG_ERROR("Failed to open Metal shader manifest '%s'", manifestPath);
        return false;
    }

    MetalShaderLibraryManifest manifest;
    manifest.manifestPath = NormalizeManifestPath(manifestPath);
    std::vector<std::string> parsedKeys;
    bool hasVersion = false;
    std::string line;
    uint32_t lineNumber = 0;
    while (std::getline(file, line)) {
        ++lineNumber;

        const std::string trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        const std::size_t separator = trimmed.find('=');
        if (separator == std::string::npos) {
            NEXT_LOG_ERROR("Invalid Metal shader manifest line %u in '%s'", lineNumber, manifestPath);
            return false;
        }

        const std::string key = Trim(trimmed.substr(0, separator));
        const std::string value = Trim(trimmed.substr(separator + 1));
        if (key.empty()) {
            NEXT_LOG_ERROR("Invalid empty Metal shader manifest key on line %u in '%s'",
                           lineNumber,
                           manifestPath);
            return false;
        }
        if (ManifestKeyRequiresValue(key) && value.empty()) {
            NEXT_LOG_ERROR("Metal shader manifest key '%s' on line %u in '%s' must not be empty",
                           key.c_str(),
                           lineNumber,
                           manifestPath);
            return false;
        }
        if (IsKnownManifestKey(key) &&
            !RecordManifestKey(parsedKeys, key, lineNumber, manifestPath)) {
            return false;
        }

        if (key == "version") {
            uint32_t version = 0;
            if (!ParseManifestVersion(value, &version)) {
                NEXT_LOG_ERROR("Invalid Metal shader manifest version '%s' in '%s'",
                               value.c_str(),
                               manifestPath);
                return false;
            }
            manifest.version = version;
            hasVersion = true;
        } else if (key == "debugName") {
            manifest.debugName = value;
        } else if (key == "source") {
            if (!ResolveManifestInputPath(manifest.manifestPath.c_str(),
                                          key,
                                          value,
                                          lineNumber,
                                          &manifest.sourcePath)) {
                return false;
            }
        } else if (key == "metallib") {
            if (!ResolveManifestInputPath(manifest.manifestPath.c_str(),
                                          key,
                                          value,
                                          lineNumber,
                                          &manifest.metallibPath)) {
                return false;
            }
        } else if (key == "vertexEntry") {
            manifest.vertexEntryPoint = value;
        } else if (key == "fragmentEntry") {
            manifest.fragmentEntryPoint = value;
        } else if (key == "materialLayout") {
            manifest.materialLayout = value;
        } else if (key == "pipelineLayout") {
            manifest.pipelineLayout = value;
        } else if (key == "requiredArgumentBufferTier") {
            RHI::ArgumentBufferTier tier = RHI::ArgumentBufferTier::Unsupported;
            if (!ParseArgumentBufferTierName(value, &tier)) {
                NEXT_LOG_ERROR("Invalid Metal shader manifest requiredArgumentBufferTier '%s' in '%s'",
                               value.c_str(),
                               manifestPath);
                return false;
            }
            manifest.requiredArgumentBufferTier = tier;
        } else if (key == "materialShaderResourceGroupArgumentBufferIndex") {
            uint32_t bindingIndex = 0;
            if (!ParseManifestUint32(value, &bindingIndex)) {
                NEXT_LOG_ERROR(
                    "Invalid Metal shader manifest materialShaderResourceGroupArgumentBufferIndex '%s' in '%s'",
                    value.c_str(),
                    manifestPath);
                return false;
            }
            manifest.materialShaderResourceGroupArgumentBufferIndex = bindingIndex;
        } else if (key == "materialShaderResourceGroupUniformArgumentIndex") {
            uint32_t argumentIndex = 0;
            if (!ParseManifestUint32(value, &argumentIndex)) {
                NEXT_LOG_ERROR(
                    "Invalid Metal shader manifest materialShaderResourceGroupUniformArgumentIndex '%s' in '%s'",
                    value.c_str(),
                    manifestPath);
                return false;
            }
            manifest.materialShaderResourceGroupUniformArgumentIndex = argumentIndex;
        } else if (key == "materialShaderResourceGroupTextureArgumentBaseIndex") {
            uint32_t argumentIndex = 0;
            if (!ParseManifestUint32(value, &argumentIndex)) {
                NEXT_LOG_ERROR(
                    "Invalid Metal shader manifest materialShaderResourceGroupTextureArgumentBaseIndex '%s' in '%s'",
                    value.c_str(),
                    manifestPath);
                return false;
            }
            manifest.materialShaderResourceGroupTextureArgumentBaseIndex = argumentIndex;
        } else if (key == "materialShaderResourceGroupSamplerArgumentBaseIndex") {
            uint32_t argumentIndex = 0;
            if (!ParseManifestUint32(value, &argumentIndex)) {
                NEXT_LOG_ERROR(
                    "Invalid Metal shader manifest materialShaderResourceGroupSamplerArgumentBaseIndex '%s' in '%s'",
                    value.c_str(),
                    manifestPath);
                return false;
            }
            manifest.materialShaderResourceGroupSamplerArgumentBaseIndex = argumentIndex;
        } else {
            NEXT_LOG_WARNING("Ignoring unknown Metal shader manifest key '%s' in '%s'",
                             key.c_str(),
                             manifestPath);
        }
    }

    if (!hasVersion) {
        NEXT_LOG_ERROR("Metal shader manifest '%s' did not declare a version", manifestPath);
        return false;
    }
    if (manifest.version != kMetalShaderManifestVersion) {
        NEXT_LOG_ERROR("Unsupported Metal shader manifest version %u in '%s' (expected %u)",
                       manifest.version,
                       manifestPath,
                       kMetalShaderManifestVersion);
        return false;
    }
    if (manifest.metallibPath.empty() && manifest.sourcePath.empty()) {
        NEXT_LOG_ERROR("Metal shader manifest '%s' did not declare a source or metallib path", manifestPath);
        return false;
    }
    if (manifest.vertexEntryPoint.empty() || manifest.fragmentEntryPoint.empty()) {
        NEXT_LOG_ERROR("Metal shader manifest '%s' did not declare vertexEntry and fragmentEntry", manifestPath);
        return false;
    }

    if (outManifest) {
        *outManifest = std::move(manifest);
    }
    return true;
}

namespace {

MetalShaderLibraryManifest BuildDemoForwardShaderManifest() {
    MetalShaderLibraryManifest manifest;
    manifest.debugName = kDefaultDemoForwardDebugName;

    const bool explicitManifestPath = NonEmptyEnvironmentValue(kDemoForwardShaderManifestPathEnv) != nullptr;
    const char* manifestPath = DemoForwardShaderManifestPath();
    manifest.manifestPath = NormalizeManifestPath(manifestPath);
    bool loadedManifestFromFile = false;
    if (FileExists(manifestPath)) {
        MetalShaderLibraryManifest fileManifest;
        if (LoadShaderLibraryManifest(manifestPath, &fileManifest)) {
            manifest = std::move(fileManifest);
            loadedManifestFromFile = true;
        } else {
            return manifest;
        }
    } else if (explicitManifestPath) {
        NEXT_LOG_ERROR("Requested Metal shader manifest '%s' does not exist", manifestPath);
        return manifest;
    }

    if (loadedManifestFromFile &&
        (!ValidateDemoForwardMaterialLayout(manifest) ||
         !ValidateDemoForwardPipelineLayout(manifest) ||
         !ValidateDemoForwardRequiredArgumentBufferTier(manifest) ||
         !ValidateDemoForwardMaterialShaderResourceGroupArgumentBufferIndex(manifest) ||
         !ValidateDemoForwardMaterialShaderResourceGroupArgumentLayout(manifest))) {
        MetalShaderLibraryManifest invalidManifest;
        invalidManifest.manifestPath = manifest.manifestPath;
        return invalidManifest;
    }

    if (manifest.debugName.empty()) {
        manifest.debugName = kDefaultDemoForwardDebugName;
    }
    if (manifest.vertexEntryPoint.empty()) {
        manifest.vertexEntryPoint = kDefaultDemoForwardVertexEntryPoint;
    }
    if (manifest.fragmentEntryPoint.empty()) {
        manifest.fragmentEntryPoint = kDefaultDemoForwardFragmentEntryPoint;
    }
    if (manifest.materialLayout.empty()) {
        manifest.materialLayout = kMetalDemoForwardMaterialLayoutName;
    }
    if (manifest.pipelineLayout.empty()) {
        manifest.pipelineLayout = kMetalDemoForwardPipelineLayoutName;
    }
    if (manifest.requiredArgumentBufferTier == RHI::ArgumentBufferTier::Unsupported) {
        manifest.requiredArgumentBufferTier = kMetalDemoForwardRequiredArgumentBufferTier;
    }
    if (manifest.materialShaderResourceGroupArgumentBufferIndex == kMetalShaderManifestInvalidBindingIndex) {
        manifest.materialShaderResourceGroupArgumentBufferIndex =
            kMetalDemoForwardMaterialShaderResourceGroupArgumentBufferIndex;
    }
    if (manifest.materialShaderResourceGroupUniformArgumentIndex == kMetalShaderManifestInvalidBindingIndex) {
        manifest.materialShaderResourceGroupUniformArgumentIndex =
            kMetalDemoForwardMaterialShaderResourceGroupUniformArgumentIndex;
    }
    if (manifest.materialShaderResourceGroupTextureArgumentBaseIndex == kMetalShaderManifestInvalidBindingIndex) {
        manifest.materialShaderResourceGroupTextureArgumentBaseIndex =
            kMetalDemoForwardMaterialShaderResourceGroupTextureArgumentBaseIndex;
    }
    if (manifest.materialShaderResourceGroupSamplerArgumentBaseIndex == kMetalShaderManifestInvalidBindingIndex) {
        manifest.materialShaderResourceGroupSamplerArgumentBaseIndex =
            kMetalDemoForwardMaterialShaderResourceGroupSamplerArgumentBaseIndex;
    }

    if (const char* overridePath = NonEmptyEnvironmentValue(kDemoForwardMetallibPathEnv)) {
        manifest.metallibPath = NormalizeShaderInputPath(overridePath);
    } else if (const char* builtMetallibPath = DefaultDemoForwardMetallibPath()) {
        if (!explicitManifestPath || manifest.metallibPath.empty()) {
            manifest.metallibPath = NormalizeShaderInputPath(builtMetallibPath);
        }
    }

    if (const char* overridePath = NonEmptyEnvironmentValue(kDemoForwardShaderPathEnv)) {
        manifest.sourcePath = NormalizeShaderInputPath(overridePath);
    } else if (manifest.sourcePath.empty()) {
        manifest.sourcePath = DefaultDemoForwardShaderPath();
    }

    return manifest;
}

const MetalShaderLibraryManifest& DemoForwardShaderManifestStorage() {
    static MetalShaderLibraryManifest manifest;
    manifest = BuildDemoForwardShaderManifest();
    return manifest;
}

} // namespace

const char* DemoForwardShaderPath() {
    return EmptyToNull(DemoForwardShaderManifestStorage().sourcePath);
}

const char* DemoForwardMetallibPath() {
    return EmptyToNull(DemoForwardShaderManifestStorage().metallibPath);
}

MetalShaderLibraryDesc DemoForwardShaderLibraryDesc() {
    return DemoForwardShaderManifestStorage().ToDesc();
}

MetalShaderLibraryInput SelectShaderLibraryInput(const MetalShaderLibraryDesc& desc) {
    if (FileExists(desc.metallibPath.c_str())) {
        return {MetalShaderLibraryInputKind::Metallib, desc.metallibPath};
    }
    if (FileExists(desc.sourcePath.c_str())) {
        return {MetalShaderLibraryInputKind::Source, desc.sourcePath};
    }
    return {};
}

id<MTLLibrary> CreateShaderLibrary(MetalDevice& device,
                                   const MetalShaderLibraryDesc& desc,
                                   MetalShaderLibraryInput* outInput) {
    if (outInput) {
        *outInput = {};
    }

    const MetalShaderLibraryInput input = SelectShaderLibraryInput(desc);
    const char* debugName = EmptyToNull(desc.debugName);
    switch (input.kind) {
        case MetalShaderLibraryInputKind::Metallib:
            if (id<MTLLibrary> library =
                    CreateShaderLibraryFromMetallibFile(device, input.path.c_str(), debugName)) {
                if (ValidateShaderLibraryEntryPoints(library, desc)) {
                    if (outInput) {
                        *outInput = input;
                    }
                    return library;
                }
                NEXT_LOG_WARNING("Metal shader library '%s' did not match requested entry points; trying source fallback",
                                 input.path.c_str());
            }
            if (FileExists(desc.sourcePath.c_str())) {
                id<MTLLibrary> library =
                    CreateShaderLibraryFromSourceFile(device, desc.sourcePath.c_str(), debugName);
                if (library && ValidateShaderLibraryEntryPoints(library, desc)) {
                    if (outInput) {
                        *outInput = {MetalShaderLibraryInputKind::Source, desc.sourcePath};
                    }
                    return library;
                }
            }
            return nil;
        case MetalShaderLibraryInputKind::Source:
            if (id<MTLLibrary> library =
                    CreateShaderLibraryFromSourceFile(device, input.path.c_str(), debugName)) {
                if (ValidateShaderLibraryEntryPoints(library, desc)) {
                    if (outInput) {
                        *outInput = input;
                    }
                    return library;
                }
            }
            return nil;
        case MetalShaderLibraryInputKind::None:
        default:
            NEXT_LOG_ERROR("Cannot create Metal shader library without a metallib or source path");
            return nil;
    }
}

id<MTLLibrary> CreateShaderLibrary(MetalDevice& device, const MetalShaderLibraryDesc& desc) {
    return CreateShaderLibrary(device, desc, nullptr);
}

id<MTLLibrary> CreateDemoForwardShaderLibrary(MetalDevice& device) {
    return CreateShaderLibrary(device, DemoForwardShaderLibraryDesc());
}

id<MTLLibrary> CreateShaderLibraryFromSourceFile(MetalDevice& device,
                                                 const char* sourcePath,
                                                 const char* debugName) {
    if (!device.NativeDevice()) {
        NEXT_LOG_ERROR("Cannot compile Metal shader library without a native device");
        return nil;
    }
    if (!sourcePath || sourcePath[0] == '\0') {
        NEXT_LOG_ERROR("Cannot compile Metal shader library without a source path");
        return nil;
    }

    std::ifstream file(sourcePath, std::ios::binary);
    if (!file) {
        NEXT_LOG_ERROR("Failed to open Metal shader source '%s'", sourcePath);
        return nil;
    }

    const std::string source((std::istreambuf_iterator<char>(file)),
                             std::istreambuf_iterator<char>());
    if (source.empty()) {
        NEXT_LOG_ERROR("Metal shader source '%s' is empty", sourcePath);
        return nil;
    }

    @autoreleasepool {
        NSString* shaderSource = [[NSString alloc] initWithBytes:source.data()
                                                          length:source.size()
                                                        encoding:NSUTF8StringEncoding];
        if (!shaderSource) {
            NEXT_LOG_ERROR("Metal shader source '%s' is not valid UTF-8", sourcePath);
            return nil;
        }

        NSError* shaderError = nil;
        id<MTLLibrary> library = [device.NativeDevice() newLibraryWithSource:shaderSource
                                                                     options:nil
                                                                       error:&shaderError];
        if (!library) {
            NEXT_LOG_ERROR("Failed to compile Metal shader source '%s' (%s): %s",
                           sourcePath,
                           debugName ? debugName : "unnamed",
                           shaderError ? [[shaderError localizedDescription] UTF8String] : "unknown error");
            return nil;
        }

        SetShaderLibraryLabel(library, debugName);
        return library;
    }
}

} // namespace MetalBackend
} // namespace Next
