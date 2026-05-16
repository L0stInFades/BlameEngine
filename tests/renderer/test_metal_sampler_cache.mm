#include "metal_resource.h"

#include <gtest/gtest.h>

#include <limits>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

class MetalSamplerCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!device.Initialize()) {
            GTEST_SKIP() << "Metal device unavailable";
        }
    }

    void TearDown() override {
        device.Shutdown();
    }

    MetalDevice device;
};

RHI::SamplerDesc MakeMaterialSamplerDesc(const char* debugName) {
    RHI::SamplerDesc desc;
    desc.minFilter = RHI::SamplerFilter::Linear;
    desc.magFilter = RHI::SamplerFilter::Linear;
    desc.mipFilter = RHI::SamplerMipFilter::NotMipmapped;
    desc.addressU = RHI::SamplerAddressMode::Repeat;
    desc.addressV = RHI::SamplerAddressMode::Repeat;
    desc.addressW = RHI::SamplerAddressMode::Repeat;
    desc.anisotropyEnabled = true;
    desc.maxAnisotropy = 4;
    desc.debugName = debugName;
    return desc;
}

} // namespace

TEST_F(MetalSamplerCacheTest, ReusesEquivalentDescriptorsIgnoringDebugLabels) {
    MetalSamplerCache cache;

    RHI::SamplerDesc base = MakeMaterialSamplerDesc("first material sampler");
    id<MTLSamplerState> first = cache.GetOrCreate(device, base);
    ASSERT_NE(first, nil);
    EXPECT_EQ(cache.GetCachedSamplerCount(), 1u);

    RHI::SamplerDesc sameKey = base;
    sameKey.debugName = "same sampler different label";
    id<MTLSamplerState> second = cache.GetOrCreate(device, sameKey);
    EXPECT_EQ(second, first);
    EXPECT_EQ(cache.GetCachedSamplerCount(), 1u);

    RHI::SamplerDesc different = base;
    different.addressU = RHI::SamplerAddressMode::ClampToEdge;
    id<MTLSamplerState> third = cache.GetOrCreate(device, different);
    ASSERT_NE(third, nil);
    EXPECT_NE(third, first);
    EXPECT_EQ(cache.GetCachedSamplerCount(), 2u);

    cache.Shutdown(&device, 2);
    EXPECT_EQ(cache.GetCachedSamplerCount(), 0u);
    EXPECT_EQ(device.PendingReleaseCount(), 2u);

    device.CollectReleasedResources(5, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalSamplerCacheTest, ReusesEquivalentDescriptorsWithNanLodFields) {
    MetalSamplerCache cache;

    RHI::SamplerDesc base = MakeMaterialSamplerDesc("nan lod sampler");
    base.minLod = std::numeric_limits<float>::quiet_NaN();
    base.maxLod = std::numeric_limits<float>::quiet_NaN();
    base.mipLodBias = std::numeric_limits<float>::quiet_NaN();

    id<MTLSamplerState> first = cache.GetOrCreate(device, base);
    ASSERT_NE(first, nil);
    EXPECT_EQ(cache.GetCachedSamplerCount(), 1u);

    RHI::SamplerDesc sameKey = base;
    sameKey.debugName = "same nan lod sampler different label";
    id<MTLSamplerState> second = cache.GetOrCreate(device, sameKey);
    EXPECT_EQ(second, first);
    EXPECT_EQ(cache.GetCachedSamplerCount(), 1u);

    RHI::SamplerDesc different = base;
    different.mipLodBias = 0.0f;
    id<MTLSamplerState> third = cache.GetOrCreate(device, different);
    ASSERT_NE(third, nil);
    EXPECT_NE(third, first);
    EXPECT_EQ(cache.GetCachedSamplerCount(), 2u);

    cache.Shutdown(&device, 2);
    EXPECT_EQ(cache.GetCachedSamplerCount(), 0u);
    EXPECT_EQ(device.PendingReleaseCount(), 2u);

    device.CollectReleasedResources(5, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
