#include "metal_gpu_resource.h"

#include "metal_conversions.h"
#include "next/foundation/logger.h"

#include <cstdio>
#include <limits>

#import <Foundation/Foundation.h>

namespace Next {
namespace MetalBackend {
namespace {

void StoreDebugName(char* output, size_t outputSize, const char* debugName, const char* fallback) {
    std::snprintf(output, outputSize, "%s", (debugName && debugName[0] != '\0') ? debugName : fallback);
}

NSString* MakeLabel(const char* debugName) {
    return (debugName && debugName[0] != '\0') ? [NSString stringWithUTF8String:debugName] : nil;
}

} // namespace

MetalBuffer::~MetalBuffer() {
    Shutdown();
}

bool MetalBuffer::Initialize(MetalDevice& device, const RHI::BufferDesc& desc) {
    if (!device.NativeDevice()) {
        NEXT_LOG_ERROR("Cannot initialize Metal buffer without a device");
        return false;
    }

    const RHI::ResourceDescriptorValidation validation = RHI::ValidateBufferDesc(desc);
    if (!validation) {
        NEXT_LOG_ERROR("Invalid Metal buffer descriptor: %s",
                       RHI::ResourceDescriptorErrorName(validation.error));
        return false;
    }

    if (desc.sizeBytes > static_cast<uint64_t>(std::numeric_limits<NSUInteger>::max())) {
        NEXT_LOG_ERROR("Metal buffer descriptor is too large: %llu bytes",
                       static_cast<unsigned long long>(desc.sizeBytes));
        return false;
    }

    Shutdown();

    desc_ = desc;
    currentState_ = desc.initialState;
    StoreDebugName(debugName_, sizeof(debugName_), desc.debugName, "NEXT Metal buffer");
    desc_.debugName = debugName_;

    @autoreleasepool {
        buffer_ = [device.NativeDevice() newBufferWithLength:static_cast<NSUInteger>(desc.sizeBytes)
                                                     options:ToMetalResourceOptions(desc.memory)];
        if (!buffer_) {
            NEXT_LOG_ERROR("Failed to create Metal buffer '%s' (%llu bytes)",
                           debugName_,
                           static_cast<unsigned long long>(desc.sizeBytes));
            Shutdown();
            return false;
        }

        buffer_.label = MakeLabel(debugName_);
        return true;
    }
}

void MetalBuffer::Shutdown(MetalDevice* device, uint64_t submittedFrameIndex) {
    if (device && buffer_) {
        device->QueueForRelease(buffer_, submittedFrameIndex);
    }

    buffer_ = nil;
    desc_ = {};
    currentState_ = RHI::ResourceState::Undefined;
    debugName_[0] = '\0';
}

void* MetalBuffer::Contents() const {
    return buffer_ ? [buffer_ contents] : nullptr;
}

MetalTexture::~MetalTexture() {
    Shutdown();
}

bool MetalTexture::Initialize(MetalDevice& device, const RHI::TextureDesc& desc) {
    if (!device.NativeDevice()) {
        NEXT_LOG_ERROR("Cannot initialize Metal texture without a device");
        return false;
    }

    const RHI::ResourceDescriptorValidation validation = RHI::ValidateTextureDesc(desc);
    if (!validation) {
        NEXT_LOG_ERROR("Invalid Metal texture descriptor: %s",
                       RHI::ResourceDescriptorErrorName(validation.error));
        return false;
    }

    const MTLPixelFormat pixelFormat = ToMetalPixelFormat(desc.format);
    if (pixelFormat == MTLPixelFormatInvalid) {
        NEXT_LOG_ERROR("Invalid Metal texture format for '%s'",
                       desc.debugName ? desc.debugName : "NEXT Metal texture");
        return false;
    }

    Shutdown();

    desc_ = desc;
    currentState_ = desc.initialState;
    usage_ = ToMetalTextureUsage(desc.usage);
    StoreDebugName(debugName_, sizeof(debugName_), desc.debugName, "NEXT Metal texture");
    desc_.debugName = debugName_;

    @autoreleasepool {
        MTLTextureDescriptor* textureDesc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pixelFormat
                                                               width:desc.extent.width
                                                              height:desc.extent.height
                                                           mipmapped:NO];
        if (desc.sampleCount > 1) {
            textureDesc.textureType = MTLTextureType2DMultisample;
            textureDesc.sampleCount = desc.sampleCount;
        }
        textureDesc.storageMode = ToMetalStorageMode(desc.memory);
        textureDesc.usage = usage_;

        texture_ = [device.NativeDevice() newTextureWithDescriptor:textureDesc];
        if (!texture_) {
            NEXT_LOG_ERROR("Failed to create Metal texture '%s' (%ux%u)",
                           debugName_,
                           desc.extent.width,
                           desc.extent.height);
            Shutdown();
            return false;
        }

        texture_.label = MakeLabel(debugName_);
        return true;
    }
}

void MetalTexture::Shutdown(MetalDevice* device, uint64_t submittedFrameIndex) {
    if (device && texture_) {
        device->QueueForRelease(texture_, submittedFrameIndex);
    }

    texture_ = nil;
    desc_ = {};
    currentState_ = RHI::ResourceState::Undefined;
    usage_ = MTLTextureUsageUnknown;
    debugName_[0] = '\0';
}

} // namespace MetalBackend
} // namespace Next
