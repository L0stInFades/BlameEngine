#pragma once

#include "next/rhi/resource.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Next {
namespace RHI {

static constexpr uint32_t kInvalidFrameGraphResourceIndex = std::numeric_limits<uint32_t>::max();
static constexpr uint32_t kInvalidFrameGraphPassIndex = std::numeric_limits<uint32_t>::max();
static constexpr size_t kMaxFrameGraphResources = 64;
static constexpr size_t kMaxFrameGraphPasses = 64;
static constexpr size_t kMaxFrameGraphPassDependencies = 8;
static constexpr size_t kMaxFrameGraphPassResourceAccesses = 16;
static constexpr size_t kMaxFrameGraphDependencies = kMaxFrameGraphPasses * kMaxFrameGraphPassDependencies;
static constexpr size_t kMaxFrameGraphAccesses = kMaxFrameGraphPasses * kMaxFrameGraphPassResourceAccesses;
static constexpr size_t kMaxFrameGraphTransitions = 256;

inline uint32_t FrameGraphRangeEndOffset(uint32_t offset, uint32_t count) {
    const uint32_t maxOffset = std::numeric_limits<uint32_t>::max();
    return count > maxOffset - offset ? maxOffset : offset + count;
}

inline bool FrameGraphResourceStateIsKnown(ResourceState state) {
    switch (state) {
        case ResourceState::Undefined:
        case ResourceState::Common:
        case ResourceState::CopySource:
        case ResourceState::CopyDestination:
        case ResourceState::VertexBuffer:
        case ResourceState::IndexBuffer:
        case ResourceState::ConstantBuffer:
        case ResourceState::RenderTarget:
        case ResourceState::DepthWrite:
        case ResourceState::ShaderRead:
        case ResourceState::ShaderWrite:
        case ResourceState::Present:
            return true;
        default:
            return false;
    }
}

enum class FrameGraphAccessType : uint8_t {
    Read = 0,
    Write,
    ReadWrite,
};

struct FrameGraphResourceHandle {
    uint32_t index = kInvalidFrameGraphResourceIndex;

    bool IsValid() const { return index != kInvalidFrameGraphResourceIndex; }
};

struct FrameGraphResourceDesc {
    const char* debugName = nullptr;
    ResourceType type = ResourceType::Unknown;
    ResourceUsageFlags usage = ResourceUsageFlag(ResourceUsage::None);
    ResourceState initialState = ResourceState::Undefined;
    bool imported = false;

    bool HasDebugName() const { return debugName && debugName[0] != '\0'; }
    bool HasKnownType() const { return type != ResourceType::Unknown; }
    bool HasUsage() const { return usage != ResourceUsageFlag(ResourceUsage::None); }
};

struct FrameGraphPassResourceAccess {
    FrameGraphResourceHandle resource;
    ResourceState state = ResourceState::Undefined;
    FrameGraphAccessType access = FrameGraphAccessType::Read;
    ShaderStageFlags shaderStages = 0;

    bool HasResource() const { return resource.IsValid(); }
    bool HasShaderStages() const { return shaderStages != 0; }
    bool Writes() const { return access == FrameGraphAccessType::Write || access == FrameGraphAccessType::ReadWrite; }
};

struct FrameGraphPassDependency {
    uint32_t passIndex = kInvalidFrameGraphPassIndex;

    bool IsValid() const { return passIndex != kInvalidFrameGraphPassIndex; }
};

struct FrameGraphPassDesc {
    const char* debugName = nullptr;
    QueueClass queueClass = QueueClass::Graphics;
    std::array<FrameGraphPassDependency, kMaxFrameGraphPassDependencies> dependencies{};
    uint32_t dependencyCount = 0;
    std::array<FrameGraphPassResourceAccess, kMaxFrameGraphPassResourceAccesses> accesses{};
    uint32_t accessCount = 0;

    const FrameGraphPassDependency* GetDependency(size_t index) const {
        return index < dependencyCount && index < dependencies.size() ? &dependencies[index] : nullptr;
    }
    const FrameGraphPassResourceAccess* GetAccess(size_t index) const {
        return index < accessCount && index < accesses.size() ? &accesses[index] : nullptr;
    }
    bool HasDebugName() const { return debugName && debugName[0] != '\0'; }
    bool HasDependencies() const { return dependencyCount != 0; }
    bool HasAccesses() const { return accessCount != 0; }
    bool HasCompleteDependencyTable() const {
        if (dependencyCount > dependencies.size()) {
            return false;
        }
        for (size_t i = 0; i < dependencyCount; ++i) {
            if (!dependencies[i].IsValid()) {
                return false;
            }
        }
        return true;
    }
    bool HasCompleteAccessTable() const {
        if (!HasAccesses() || accessCount > accesses.size()) {
            return false;
        }
        for (size_t i = 0; i < accessCount; ++i) {
            if (!accesses[i].HasResource()) {
                return false;
            }
        }
        return true;
    }
};

struct FrameGraphDesc {
    std::array<FrameGraphResourceDesc, kMaxFrameGraphResources> resources{};
    uint32_t resourceCount = 0;
    std::array<FrameGraphPassDesc, kMaxFrameGraphPasses> passes{};
    uint32_t passCount = 0;

    const FrameGraphResourceDesc* GetResource(FrameGraphResourceHandle handle) const {
        return handle.index < resourceCount && handle.index < resources.size() ? &resources[handle.index] : nullptr;
    }
    const FrameGraphPassDesc* GetPass(size_t index) const {
        return index < passCount && index < passes.size() ? &passes[index] : nullptr;
    }
    bool HasResources() const { return resourceCount != 0; }
    bool HasPasses() const { return passCount != 0; }
    bool HasCompleteResourceTable() const {
        if (!HasResources() || resourceCount > resources.size()) {
            return false;
        }
        for (size_t i = 0; i < resourceCount; ++i) {
            if (!resources[i].HasKnownType()) {
                return false;
            }
        }
        return true;
    }
    bool HasCompletePassTable() const {
        if (!HasPasses() || passCount > passes.size()) {
            return false;
        }
        for (size_t i = 0; i < passCount; ++i) {
            if (!passes[i].HasCompleteDependencyTable() || !passes[i].HasCompleteAccessTable()) {
                return false;
            }
        }
        return true;
    }
};

enum class FrameGraphDescriptorError : uint8_t {
    None = 0,
    MissingResource,
    TooManyResources,
    MissingPass,
    TooManyPasses,
    MissingPassAccess,
    TooManyPassAccesses,
    TooManyPassDependencies,
    InvalidPassDependency,
    DuplicatePassDependency,
    InvalidResourceIndex,
    InvalidResourceType,
    DuplicateResourceAccess,
    UnsupportedTargetState,
    UnsupportedQueueClass,
    InvalidAccessType,
    InvalidShaderStage,
    UnsupportedShaderStage,
    TransitionCapacityExceeded,
};

enum class FrameGraphExecutionError : uint8_t {
    None = 0,
    InvalidCompileResult,
    InvalidPassIndex,
    MissingResourceBinding,
    ResourceTypeMismatch,
    TransitionCapacityExceeded,
    InvalidResourceTransition,
};

struct FrameGraphDescriptorValidation {
    FrameGraphDescriptorError error = FrameGraphDescriptorError::None;
    uint32_t resourceIndex = kInvalidFrameGraphResourceIndex;
    uint32_t passIndex = 0;
    uint32_t accessIndex = 0;
    uint32_t conflictAccessIndex = 0;
    uint32_t dependencyIndex = 0;
    uint32_t conflictDependencyIndex = 0;
    uint32_t dependencyPassIndex = kInvalidFrameGraphPassIndex;
    ResourceState state = ResourceState::Undefined;
    QueueClass queueClass = QueueClass::Graphics;
    ShaderStageFlags shaderStages = 0;

    explicit operator bool() const { return error == FrameGraphDescriptorError::None; }
};

struct FrameGraphCompiledPassDependency {
    uint32_t passIndex = 0;
    uint32_t dependencyIndex = 0;
    uint32_t dependencyPassIndex = kInvalidFrameGraphPassIndex;

    bool IsValid() const { return dependencyPassIndex != kInvalidFrameGraphPassIndex; }
};

struct FrameGraphTransition {
    uint32_t passIndex = 0;
    uint32_t accessIndex = 0;
    uint32_t resourceIndex = kInvalidFrameGraphResourceIndex;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;
    QueueClass queueClass = QueueClass::Graphics;
    FrameGraphAccessType access = FrameGraphAccessType::Read;
    ShaderStageFlags shaderStages = 0;
};

struct FrameGraphCompiledAccess {
    uint32_t passIndex = 0;
    uint32_t accessIndex = 0;
    uint32_t resourceIndex = kInvalidFrameGraphResourceIndex;
    ResourceState state = ResourceState::Undefined;
    QueueClass queueClass = QueueClass::Graphics;
    FrameGraphAccessType access = FrameGraphAccessType::Read;
    ShaderStageFlags shaderStages = 0;

    bool HasShaderStages() const { return shaderStages != 0; }
    bool Writes() const { return access == FrameGraphAccessType::Write || access == FrameGraphAccessType::ReadWrite; }
};

struct FrameGraphPassCompileInfo {
    uint32_t dependencyOffset = 0;
    uint32_t dependencyCount = 0;
    uint32_t transitionOffset = 0;
    uint32_t transitionCount = 0;
    uint32_t accessOffset = 0;
    uint32_t accessCount = 0;
    QueueClass queueClass = QueueClass::Graphics;

    bool HasDependencies() const { return dependencyCount != 0; }
    bool HasTransitions() const { return transitionCount != 0; }
    bool HasAccesses() const { return accessCount != 0; }
    uint32_t DependencyEndOffset() const {
        return FrameGraphRangeEndOffset(dependencyOffset, dependencyCount);
    }
    uint32_t TransitionEndOffset() const {
        return FrameGraphRangeEndOffset(transitionOffset, transitionCount);
    }
    uint32_t AccessEndOffset() const {
        return FrameGraphRangeEndOffset(accessOffset, accessCount);
    }
};

struct FrameGraphCompileResult {
    FrameGraphDescriptorValidation validation;
    std::array<ResourceType, kMaxFrameGraphResources> resourceTypes{};
    uint32_t resourceCount = 0;
    std::array<FrameGraphCompiledPassDependency, kMaxFrameGraphDependencies> dependencies{};
    uint32_t dependencyCount = 0;
    std::array<FrameGraphCompiledAccess, kMaxFrameGraphAccesses> accesses{};
    uint32_t accessCount = 0;
    std::array<FrameGraphTransition, kMaxFrameGraphTransitions> transitions{};
    uint32_t transitionCount = 0;
    std::array<FrameGraphPassCompileInfo, kMaxFrameGraphPasses> passes{};
    uint32_t passCount = 0;
    std::array<ResourceState, kMaxFrameGraphResources> finalResourceStates{};

    explicit operator bool() const { return static_cast<bool>(validation); }
    const FrameGraphCompiledPassDependency* GetDependency(size_t index) const {
        return index < dependencyCount && index < dependencies.size() ? &dependencies[index] : nullptr;
    }
    const FrameGraphCompiledAccess* GetAccess(size_t index) const {
        return index < accessCount && index < accesses.size() ? &accesses[index] : nullptr;
    }
    const FrameGraphTransition* GetTransition(size_t index) const {
        return index < transitionCount && index < transitions.size() ? &transitions[index] : nullptr;
    }
    const FrameGraphPassCompileInfo* GetPass(size_t index) const {
        return index < passCount && index < passes.size() ? &passes[index] : nullptr;
    }
    ResourceType GetResourceType(FrameGraphResourceHandle handle) const {
        return handle.index < resourceCount && handle.index < resourceTypes.size()
            ? resourceTypes[handle.index]
            : ResourceType::Unknown;
    }
    ResourceType GetResourceType(uint32_t resourceIndex) const {
        return resourceIndex < resourceCount && resourceIndex < resourceTypes.size()
            ? resourceTypes[resourceIndex]
            : ResourceType::Unknown;
    }
    bool HasResources() const { return resourceCount != 0; }
    bool HasCompleteResourceTable() const {
        if (!HasResources() || resourceCount > resourceTypes.size() ||
            resourceCount > finalResourceStates.size()) {
            return false;
        }
        for (size_t i = 0; i < resourceCount; ++i) {
            if (resourceTypes[i] == ResourceType::Unknown ||
                !FrameGraphResourceStateIsKnown(finalResourceStates[i])) {
                return false;
            }
        }
        return true;
    }
    bool HasDependencies() const { return dependencyCount != 0; }
    bool HasCompleteDependencyTable() const {
        if (dependencyCount > dependencies.size() || passCount > passes.size()) {
            return false;
        }
        for (size_t i = 0; i < dependencyCount; ++i) {
            const FrameGraphCompiledPassDependency& dependency = dependencies[i];
            if (!dependency.IsValid() || dependency.passIndex >= passCount ||
                dependency.dependencyPassIndex >= dependency.passIndex ||
                dependency.dependencyIndex >= kMaxFrameGraphPassDependencies) {
                return false;
            }
            const FrameGraphPassCompileInfo& pass = passes[dependency.passIndex];
            if (pass.dependencyOffset > dependencyCount ||
                pass.dependencyCount > dependencyCount - pass.dependencyOffset ||
                dependency.dependencyIndex >= pass.dependencyCount ||
                i < pass.dependencyOffset ||
                i >= pass.dependencyOffset + pass.dependencyCount) {
                return false;
            }
        }
        return true;
    }
    bool HasAccesses() const { return accessCount != 0; }
    bool HasCompleteAccessTable() const {
        if (accessCount > accesses.size() || passCount > passes.size() ||
            resourceCount > resourceTypes.size()) {
            return false;
        }
        for (size_t i = 0; i < accessCount; ++i) {
            const FrameGraphCompiledAccess& access = accesses[i];
            if (access.passIndex >= passCount ||
                access.accessIndex >= kMaxFrameGraphPassResourceAccesses ||
                access.resourceIndex == kInvalidFrameGraphResourceIndex ||
                access.resourceIndex >= resourceCount) {
                return false;
            }
            const FrameGraphPassCompileInfo& pass = passes[access.passIndex];
            if (pass.accessOffset > accessCount ||
                pass.accessCount > accessCount - pass.accessOffset ||
                access.accessIndex >= pass.accessCount ||
                access.queueClass != pass.queueClass ||
                i < pass.accessOffset ||
                i >= pass.accessOffset + pass.accessCount) {
                return false;
            }
        }
        return true;
    }
    bool HasTransitions() const { return transitionCount != 0; }
    bool HasCompleteTransitionTable() const {
        if (transitionCount > transitions.size() || passCount > passes.size() ||
            resourceCount > resourceTypes.size()) {
            return false;
        }
        for (size_t i = 0; i < transitionCount; ++i) {
            const FrameGraphTransition& transition = transitions[i];
            if (transition.passIndex >= passCount ||
                transition.accessIndex >= kMaxFrameGraphPassResourceAccesses ||
                transition.resourceIndex == kInvalidFrameGraphResourceIndex ||
                transition.resourceIndex >= resourceCount) {
                return false;
            }
            const FrameGraphPassCompileInfo& pass = passes[transition.passIndex];
            if (pass.transitionOffset > transitionCount ||
                pass.transitionCount > transitionCount - pass.transitionOffset ||
                transition.accessIndex >= pass.accessCount ||
                transition.queueClass != pass.queueClass ||
                i < pass.transitionOffset ||
                i >= pass.transitionOffset + pass.transitionCount) {
                return false;
            }
        }
        return true;
    }
    bool HasPasses() const { return passCount != 0; }
    bool HasCompletePassTable() const {
        if (!HasPasses() || passCount > passes.size() ||
            dependencyCount > dependencies.size() ||
            transitionCount > transitions.size() ||
            accessCount > accesses.size()) {
            return false;
        }
        uint32_t dependencyOffset = 0;
        uint32_t transitionOffset = 0;
        uint32_t accessOffset = 0;
        for (size_t i = 0; i < passCount; ++i) {
            const FrameGraphPassCompileInfo& pass = passes[i];
            if (pass.dependencyOffset != dependencyOffset ||
                pass.transitionOffset != transitionOffset ||
                pass.accessOffset != accessOffset) {
                return false;
            }
            if (pass.dependencyOffset > dependencyCount ||
                pass.transitionOffset > transitionCount ||
                pass.accessOffset > accessCount ||
                pass.dependencyCount > dependencyCount - pass.dependencyOffset ||
                pass.transitionCount > transitionCount - pass.transitionOffset ||
                pass.accessCount > accessCount - pass.accessOffset) {
                return false;
            }
            dependencyOffset += pass.dependencyCount;
            transitionOffset += pass.transitionCount;
            accessOffset += pass.accessCount;
        }
        return dependencyOffset == dependencyCount &&
            transitionOffset == transitionCount &&
            accessOffset == accessCount;
    }
    bool HasCompleteCompileTables() const {
        return HasCompleteResourceTable() &&
            HasCompleteDependencyTable() &&
            HasCompleteAccessTable() &&
            HasCompleteTransitionTable() &&
            HasCompletePassTable();
    }
    ResourceState GetFinalResourceState(FrameGraphResourceHandle handle) const {
        return handle.index < resourceCount && handle.index < finalResourceStates.size()
            ? finalResourceStates[handle.index]
            : ResourceState::Undefined;
    }
};

struct FrameGraphExecutionValidation {
    FrameGraphExecutionError error = FrameGraphExecutionError::None;
    uint32_t transitionIndex = 0;
    uint32_t resourceIndex = kInvalidFrameGraphResourceIndex;
    uint32_t passIndex = 0;
    uint32_t accessIndex = 0;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;
    QueueClass queueClass = QueueClass::Graphics;
    ShaderStageFlags shaderStages = 0;
    ResourceType expectedResourceType = ResourceType::Unknown;
    ResourceType actualResourceType = ResourceType::Unknown;
    FrameGraphDescriptorValidation frameGraphValidation;
    ResourceTransitionValidation resourceTransitionValidation;

    bool HasShaderStages() const { return shaderStages != 0; }
    explicit operator bool() const { return error == FrameGraphExecutionError::None; }
};

struct FrameGraphResourceTransitionPlan {
    FrameGraphExecutionValidation validation;
    std::array<ResourceTransition, kMaxFrameGraphTransitions> transitions{};
    uint32_t transitionCount = 0;

    explicit operator bool() const { return static_cast<bool>(validation); }
    const ResourceTransition* GetTransition(size_t index) const {
        return index < transitionCount && index < transitions.size() ? &transitions[index] : nullptr;
    }
    bool HasTransitions() const { return transitionCount != 0; }
    bool HasCompleteTransitionTable() const { return transitionCount <= transitions.size(); }
};

const char* FrameGraphAccessTypeName(FrameGraphAccessType access);
const char* FrameGraphDescriptorErrorName(FrameGraphDescriptorError error);
const char* FrameGraphExecutionErrorName(FrameGraphExecutionError error);
FrameGraphResourceHandle MakeFrameGraphResourceHandle(uint32_t resourceIndex);
FrameGraphPassResourceAccess MakeFrameGraphPassResourceAccess(FrameGraphResourceHandle resource,
                                                              ResourceState state,
                                                              FrameGraphAccessType access,
                                                              ShaderStageFlags shaderStages = 0);
FrameGraphPassDependency MakeFrameGraphPassDependency(uint32_t passIndex);
bool AddFrameGraphResource(FrameGraphDesc& graph,
                           const FrameGraphResourceDesc& resource,
                           FrameGraphResourceHandle* outHandle = nullptr);
bool AddFrameGraphPassDependency(FrameGraphPassDesc& pass, const FrameGraphPassDependency& dependency);
bool AddFrameGraphPassAccess(FrameGraphPassDesc& pass, const FrameGraphPassResourceAccess& access);
bool AddFrameGraphPass(FrameGraphDesc& graph, const FrameGraphPassDesc& pass);
FrameGraphDescriptorValidation ValidateFrameGraphDesc(const FrameGraphDesc& graph);
FrameGraphCompileResult CompileFrameGraphTransitions(const FrameGraphDesc& graph);
FrameGraphResourceTransitionPlan BuildFrameGraphResourceTransitionPlan(
    const FrameGraphCompileResult& compileResult,
    const std::array<Resource*, kMaxFrameGraphResources>& resources);
FrameGraphResourceTransitionPlan BuildFrameGraphPassResourceTransitionPlan(
    const FrameGraphCompileResult& compileResult,
    uint32_t passIndex,
    const std::array<Resource*, kMaxFrameGraphResources>& resources);
bool ApplyFrameGraphResourceTransitionPlan(
    const FrameGraphCompileResult& compileResult,
    const std::array<Resource*, kMaxFrameGraphResources>& resources,
    FrameGraphExecutionValidation* outValidation = nullptr,
    uint32_t* outTransitionCount = nullptr);
bool ApplyFrameGraphPassResourceTransitionPlan(
    const FrameGraphCompileResult& compileResult,
    uint32_t passIndex,
    const std::array<Resource*, kMaxFrameGraphResources>& resources,
    FrameGraphExecutionValidation* outValidation = nullptr,
    uint32_t* outTransitionCount = nullptr);

} // namespace RHI
} // namespace Next
