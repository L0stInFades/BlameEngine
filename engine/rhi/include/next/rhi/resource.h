#pragma once

#include "next/rhi/types.h"

#include <cstddef>
#include <cstdint>

namespace Next {
namespace RHI {

class CommandContext;

struct BufferDesc {
    uint64_t sizeBytes = 0;
    ResourceMemory memory = ResourceMemory::DeviceLocal;
    ResourceUsageFlags usage = ResourceUsageFlag(ResourceUsage::None);
    ResourceState initialState = ResourceState::Undefined;
    const char* debugName = nullptr;
};

struct TextureDesc {
    Extent2D extent;
    Format format = Format::Unknown;
    uint32_t sampleCount = 1;
    ResourceMemory memory = ResourceMemory::DeviceLocal;
    ResourceUsageFlags usage = ResourceUsageFlag(ResourceUsage::None);
    ResourceState initialState = ResourceState::Undefined;
    const char* debugName = nullptr;
};

enum class ResourceDescriptorError : uint8_t {
    None = 0,
    EmptyBuffer,
    EmptyTextureExtent,
    MissingUsage,
    UnknownTextureFormat,
    TextureSizeOverflow,
    UnsupportedMemory,
    UnsupportedRenderTargetFormat,
    UnsupportedDepthStencilFormat,
    UnsupportedInitialState,
    UnsupportedSampleCount,
};

struct ResourceDescriptorValidation {
    ResourceDescriptorError error = ResourceDescriptorError::None;
    ResourceMemory memory = ResourceMemory::DeviceLocal;
    ResourceState initialState = ResourceState::Undefined;

    explicit operator bool() const { return error == ResourceDescriptorError::None; }
};

struct ResourcePoolMemoryStats {
    size_t liveResourceCount = 0;
    uint64_t liveBytes = 0;
    size_t peakResourceCount = 0;
    uint64_t peakBytes = 0;
    uint64_t budgetBytes = 0;
    uint64_t failedAllocationCount = 0;
    uint64_t failedAllocationBytes = 0;
};

struct ResourcePoolStats {
    ResourceType resourceType = ResourceType::Unknown;
    size_t liveResourceCount = 0;
    uint64_t liveBytes = 0;
    size_t peakResourceCount = 0;
    uint64_t peakBytes = 0;
    uint64_t failedAllocationCount = 0;
    uint64_t failedAllocationBytes = 0;
    ResourcePoolMemoryStats deviceLocal;
    ResourcePoolMemoryStats shared;
    ResourcePoolMemoryStats upload;
    ResourcePoolMemoryStats readback;
};

bool TextureDescEstimatedBytes(const TextureDesc& desc, uint64_t& outBytes);
const char* ResourceDescriptorErrorName(ResourceDescriptorError error);
ResourceDescriptorValidation ValidateBufferDesc(const BufferDesc& desc);
ResourceDescriptorValidation ValidateTextureDesc(const TextureDesc& desc);
ResourcePoolMemoryStats* FindResourcePoolMemoryStats(ResourcePoolStats& stats, ResourceMemory memory);
const ResourcePoolMemoryStats* FindResourcePoolMemoryStats(const ResourcePoolStats& stats, ResourceMemory memory);
bool SetResourcePoolMemoryBudget(ResourcePoolStats& stats, ResourceMemory memory, uint64_t budgetBytes);
bool ResourcePoolCanAllocate(const ResourcePoolStats& stats, ResourceMemory memory, uint64_t sizeBytes);
bool ResourcePoolMemoryBudgetRemaining(const ResourcePoolStats& stats, ResourceMemory memory, uint64_t& outBytes);
bool ResourcePoolMemoryIsOverBudget(const ResourcePoolStats& stats, ResourceMemory memory);
void RecordResourcePoolAllocation(ResourcePoolStats& stats, uint64_t sizeBytes);
void RecordResourcePoolAllocation(ResourcePoolStats& stats, ResourceMemory memory, uint64_t sizeBytes);
void RecordResourcePoolRelease(ResourcePoolStats& stats, uint64_t sizeBytes);
void RecordResourcePoolRelease(ResourcePoolStats& stats, ResourceMemory memory, uint64_t sizeBytes);
void RecordResourcePoolAllocationFailure(ResourcePoolStats& stats, uint64_t sizeBytes);
void RecordResourcePoolAllocationFailure(ResourcePoolStats& stats, ResourceMemory memory, uint64_t sizeBytes);

struct SamplerDesc {
    SamplerFilter minFilter = SamplerFilter::Linear;
    SamplerFilter magFilter = SamplerFilter::Linear;
    SamplerMipFilter mipFilter = SamplerMipFilter::NotMipmapped;
    SamplerAddressMode addressU = SamplerAddressMode::Repeat;
    SamplerAddressMode addressV = SamplerAddressMode::Repeat;
    SamplerAddressMode addressW = SamplerAddressMode::Repeat;
    SamplerBorderColor borderColor = SamplerBorderColor::OpaqueBlack;
    CompareFunction compareFunction = CompareFunction::Never;
    bool anisotropyEnabled = false;
    uint8_t maxAnisotropy = 1;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    float mipLodBias = 0.0f;
    const char* debugName = nullptr;
};

bool SamplerDescEquals(const SamplerDesc& lhs, const SamplerDesc& rhs);

class Resource {
public:
    virtual ~Resource() = default;

    virtual ResourceType GetResourceType() const = 0;
    virtual const char* GetDebugName() const = 0;
    virtual ResourceUsageFlags GetUsageFlags() const = 0;
    virtual ResourceState GetCurrentState() const = 0;
    virtual void SetCurrentState(ResourceState state) = 0;
};

struct ResourceTransition {
    Resource* resource = nullptr;
    ResourceState before = ResourceState::Undefined;
    ResourceState after = ResourceState::Undefined;
    QueueClass queueClass = QueueClass::Graphics;
    ShaderStageFlags shaderStages = 0;

    bool HasShaderStages() const { return shaderStages != 0; }
};

enum class ResourceTransitionError : uint8_t {
    None = 0,
    NullTransitionList,
    NullResource,
    StaleBeforeState,
    UnsupportedTargetState,
    UnsupportedQueueClass,
    DuplicateResource,
};

struct ResourceTransitionValidation {
    ResourceTransitionError error = ResourceTransitionError::None;
    size_t index = 0;
    size_t conflictIndex = 0;
    ResourceState current = ResourceState::Undefined;

    explicit operator bool() const { return error == ResourceTransitionError::None; }
};

bool ResourceUsageSupportsState(ResourceUsageFlags usage, ResourceState state);
bool ResourceStateSupportedOnQueue(ResourceState state, QueueClass queueClass);
const char* ResourceTransitionErrorName(ResourceTransitionError error);
ResourceTransition MakeResourceTransition(Resource& resource, ResourceState after);
ResourceTransition MakeResourceTransition(Resource& resource, ResourceState after, QueueClass queueClass);
ResourceTransition MakeResourceTransition(Resource& resource, ResourceState after, const CommandContext& context);
ResourceTransitionValidation ValidateResourceTransition(const ResourceTransition& transition);
ResourceTransitionValidation ValidateResourceTransitions(const ResourceTransition* transitions, size_t count);
bool ApplyResourceTransition(const ResourceTransition& transition);
bool ApplyResourceTransitions(const ResourceTransition* transitions, size_t count);
bool TransitionResource(Resource& resource, ResourceState after);
bool TransitionResource(Resource& resource, ResourceState after, const CommandContext& context);

} // namespace RHI
} // namespace Next
