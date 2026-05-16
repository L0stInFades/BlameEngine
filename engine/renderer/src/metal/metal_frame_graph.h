#pragma once

#include "next/rhi/frame_graph.h"
#include "next/rhi/resource.h"

#include <array>
#include <cstdint>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

struct MetalFrameGraphResourceUsage {
    id<MTLResource> resource = nil;
    RHI::ResourceType type = RHI::ResourceType::Unknown;

    bool IsValid() const { return resource != nil && type != RHI::ResourceType::Unknown; }
    bool MatchesNativeType() const {
        if (!IsValid()) {
            return false;
        }
        switch (type) {
            case RHI::ResourceType::Buffer:
                return [(id)resource conformsToProtocol:@protocol(MTLBuffer)];
            case RHI::ResourceType::Texture:
                return [(id)resource conformsToProtocol:@protocol(MTLTexture)];
            case RHI::ResourceType::Unknown:
            default:
                return false;
        }
    }
    bool IsBuffer() const { return IsValid() && type == RHI::ResourceType::Buffer; }
    bool IsTexture() const { return IsValid() && type == RHI::ResourceType::Texture; }
};

struct MetalFrameGraphResourceUsageTable {
    std::array<MetalFrameGraphResourceUsage, RHI::kMaxFrameGraphResources> resources{};
    uint32_t resourceCount = 0;

    bool SetResource(uint32_t index, id<MTLResource> resource, RHI::ResourceType type) {
        MetalFrameGraphResourceUsage resourceUse;
        resourceUse.resource = resource;
        resourceUse.type = type;
        if (index >= resources.size() || !resourceUse.MatchesNativeType()) {
            return false;
        }
        resources[index] = resourceUse;
        if (index + 1 > resourceCount) {
            resourceCount = index + 1;
        }
        return true;
    }

    const MetalFrameGraphResourceUsage* GetResource(uint32_t index) const {
        return index < resourceCount && index < resources.size() ? &resources[index] : nullptr;
    }

    bool HasResources() const { return resourceCount != 0; }
    bool MatchesCompileResult(const RHI::FrameGraphCompileResult& compileResult) const {
        if (resourceCount != compileResult.resourceCount ||
            resourceCount > resources.size() ||
            compileResult.resourceCount > compileResult.resourceTypes.size()) {
            return false;
        }
        for (uint32_t i = 0; i < resourceCount; ++i) {
            if (resources[i].type != compileResult.GetResourceType(i)) {
                return false;
            }
        }
        return true;
    }
    bool HasCompleteResourceTable() const {
        if (resourceCount > resources.size()) {
            return false;
        }
        for (uint32_t i = 0; i < resourceCount; ++i) {
            if (!resources[i].MatchesNativeType()) {
                return false;
            }
        }
        return true;
    }
};

} // namespace MetalBackend
} // namespace Next
