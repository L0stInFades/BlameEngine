#include "metal_device.h"
#include "metal_pipeline.h"
#include "next/rhi/pipeline.h"

#include <gtest/gtest.h>

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {
namespace testing {
namespace {

constexpr const char* kPipelineTestShaderSource = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
};

vertex VertexOut vertex_main(uint vertexId [[vertex_id]]) {
    const float2 positions[3] = {
        float2(0.0, 0.5),
        float2(-0.5, -0.5),
        float2(0.5, -0.5),
    };
    VertexOut out;
    out.position = float4(positions[vertexId], 0.0, 1.0);
    return out;
}

struct AttributeVertexIn {
    float2 position [[attribute(0)]];
    float4 color [[attribute(1)]];
};

vertex VertexOut vertex_attribute_main(AttributeVertexIn in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 0.0, 1.0);
    return out;
}

fragment float4 fragment_main() {
    return float4(1.0, 0.0, 0.0, 1.0);
}
)";

class MetalPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!device.Initialize()) {
            GTEST_SKIP() << "Metal device unavailable";
        }
        @autoreleasepool {
            NSError* error = nil;
            NSString* source = [NSString stringWithUTF8String:kPipelineTestShaderSource];
            library = [device.NativeDevice() newLibraryWithSource:source options:nil error:&error];
            ASSERT_NE(library, nil) << (error ? [[error localizedDescription] UTF8String] : "unknown error");
        }
    }

    void TearDown() override {
        library = nil;
        device.Shutdown();
    }

    MetalDevice device;
    id<MTLLibrary> library = nil;
};

} // namespace

TEST_F(MetalPipelineTest, CreatesAndReleasesDescriptorBackedPipelineState) {
    MetalRenderPipeline pipeline;
    RHI::GraphicsPipelineDesc desc =
        RHI::MakeGraphicsPipelineDesc("vertex_main",
                                      "fragment_main",
                                      RHI::Format::BGRA8Unorm,
                                      RHI::Format::Depth32Float,
                                      "test pipeline");
    desc.rasterState.cullMode = RHI::CullMode::Back;
    desc.rasterState.fillMode = RHI::FillMode::Wireframe;
    desc.rasterState.frontFace = RHI::FrontFaceWinding::Clockwise;
    desc.rasterState.depthBias = -2;
    desc.rasterState.depthBiasClamp = 4.0f;
    desc.rasterState.depthBiasSlopeScale = 1.25f;
    desc.rasterState.depthClipEnabled = false;
    desc.colorAttachments[0].blendEnabled = true;
    desc.colorAttachments[0].sourceColorBlendFactor = RHI::BlendFactor::SourceAlpha;
    desc.colorAttachments[0].destinationColorBlendFactor = RHI::BlendFactor::OneMinusSourceAlpha;
    desc.colorAttachments[0].sourceAlphaBlendFactor = RHI::BlendFactor::One;
    desc.colorAttachments[0].destinationAlphaBlendFactor = RHI::BlendFactor::OneMinusSourceAlpha;

    ASSERT_TRUE(pipeline.Initialize(device, library, desc));
    EXPECT_TRUE(pipeline.IsReady());
    ASSERT_NE(pipeline.NativePipelineState(), nil);
    ASSERT_NE(pipeline.NativeDepthStencilState(), nil);
    EXPECT_STREQ([pipeline.NativePipelineState().label UTF8String], "test pipeline");
    EXPECT_STREQ([pipeline.NativeDepthStencilState().label UTF8String], "test pipeline depth-stencil");
    EXPECT_EQ(pipeline.PrimitiveType(), MTLPrimitiveTypeTriangle);
    EXPECT_EQ(pipeline.GetDesc().rasterState.fillMode, RHI::FillMode::Wireframe);
    EXPECT_EQ(pipeline.GetDesc().rasterState.depthBias, -2);
    EXPECT_FLOAT_EQ(pipeline.GetDesc().rasterState.depthBiasClamp, 4.0f);
    EXPECT_FLOAT_EQ(pipeline.GetDesc().rasterState.depthBiasSlopeScale, 1.25f);
    EXPECT_FALSE(pipeline.GetDesc().rasterState.depthClipEnabled);
    EXPECT_TRUE(pipeline.GetDesc().colorAttachments[0].blendEnabled);
    EXPECT_EQ(pipeline.GetDesc().colorAttachments[0].sourceColorBlendFactor,
              RHI::BlendFactor::SourceAlpha);
    EXPECT_TRUE(RHI::GraphicsPipelineDescEquals(pipeline.GetDesc(), desc));

    pipeline.Shutdown(&device, 4);
    EXPECT_FALSE(pipeline.IsReady());
    EXPECT_EQ(pipeline.NativePipelineState(), nil);
    EXPECT_EQ(pipeline.NativeDepthStencilState(), nil);
    EXPECT_EQ(device.PendingReleaseCount(), 2u);

    device.CollectReleasedResources(7, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalPipelineTest, CreatesPipelineWithVertexInputDescriptor) {
    MetalRenderPipeline pipeline;
    RHI::GraphicsPipelineDesc desc =
        RHI::MakeGraphicsPipelineDesc("vertex_attribute_main",
                                      "fragment_main",
                                      RHI::Format::BGRA8Unorm,
                                      RHI::Format::Depth32Float,
                                      "test vertex input pipeline");
    desc.vertexInput.bufferCount = 1;
    desc.vertexInput.buffers[0].stride = 24;
    desc.vertexInput.buffers[0].stepFunction = RHI::VertexStepFunction::PerVertex;
    desc.vertexInput.buffers[0].stepRate = 1;
    desc.vertexInput.attributeCount = 2;
    desc.vertexInput.attributes[0].location = 0;
    desc.vertexInput.attributes[0].bufferIndex = 0;
    desc.vertexInput.attributes[0].format = RHI::VertexFormat::Float32x2;
    desc.vertexInput.attributes[0].offset = 0;
    desc.vertexInput.attributes[1].location = 1;
    desc.vertexInput.attributes[1].bufferIndex = 0;
    desc.vertexInput.attributes[1].format = RHI::VertexFormat::Float32x4;
    desc.vertexInput.attributes[1].offset = 8;

    ASSERT_TRUE(pipeline.Initialize(device, library, desc));
    EXPECT_TRUE(pipeline.IsReady());
    EXPECT_EQ(pipeline.GetDesc().vertexInput.bufferCount, 1u);
    EXPECT_EQ(pipeline.GetDesc().vertexInput.attributeCount, 2u);
    EXPECT_TRUE(RHI::GraphicsPipelineDescEquals(pipeline.GetDesc(), desc));

    pipeline.Shutdown(&device, 4);
    EXPECT_EQ(device.PendingReleaseCount(), 2u);
    device.CollectReleasedResources(7, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalPipelineTest, CreatesPipelineWithMergedDepthStencilFormat) {
    MetalRenderPipeline pipeline;
    RHI::GraphicsPipelineDesc desc =
        RHI::MakeGraphicsPipelineDesc("vertex_main",
                                      "fragment_main",
                                      RHI::Format::BGRA8Unorm,
                                      RHI::Format::Depth32FloatStencil8,
                                      "test merged depth-stencil pipeline");
    desc.depthStencilState.stencilTestEnabled = true;
    desc.depthStencilState.stencilReadMask = 0x7f;
    desc.depthStencilState.stencilWriteMask = 0x3f;
    desc.depthStencilState.frontStencil.compare = RHI::CompareFunction::Equal;
    desc.depthStencilState.frontStencil.stencilFailOp = RHI::StencilOperation::Replace;
    desc.depthStencilState.frontStencil.passOp = RHI::StencilOperation::IncrementWrap;
    desc.depthStencilState.backStencil.compare = RHI::CompareFunction::NotEqual;
    desc.depthStencilState.backStencil.depthFailOp = RHI::StencilOperation::DecrementClamp;

    ASSERT_TRUE(pipeline.Initialize(device, library, desc));
    EXPECT_TRUE(pipeline.IsReady());
    EXPECT_EQ(pipeline.GetDesc().depthStencilFormat, RHI::Format::Depth32FloatStencil8);
    EXPECT_TRUE(pipeline.GetDesc().depthStencilState.stencilTestEnabled);

    pipeline.Shutdown(&device, 4);
    EXPECT_EQ(device.PendingReleaseCount(), 2u);
    device.CollectReleasedResources(7, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalPipelineTest, CreatesMultisamplePipelineWhenSupported) {
    if (![device.NativeDevice() supportsTextureSampleCount:4]) {
        GTEST_SKIP() << "4x MSAA unsupported by this Metal device";
    }

    MetalRenderPipeline pipeline;
    RHI::GraphicsPipelineDesc desc =
        RHI::MakeGraphicsPipelineDesc("vertex_main",
                                      "fragment_main",
                                      RHI::Format::BGRA8Unorm,
                                      RHI::Format::Depth32Float,
                                      "test multisample pipeline");
    desc.multisampleState.sampleCount = 4;
    desc.multisampleState.alphaToCoverageEnabled = true;

    ASSERT_TRUE(pipeline.Initialize(device, library, desc));
    EXPECT_TRUE(pipeline.IsReady());
    EXPECT_EQ(pipeline.GetDesc().multisampleState.sampleCount, 4u);
    EXPECT_TRUE(pipeline.GetDesc().multisampleState.alphaToCoverageEnabled);

    pipeline.Shutdown(&device, 4);
    EXPECT_EQ(device.PendingReleaseCount(), 2u);
    device.CollectReleasedResources(7, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalPipelineTest, CacheReusesEquivalentDescriptorsAndTracksStats) {
    MetalPipelineCache cache;
    RHI::GraphicsPipelineDesc desc =
        RHI::MakeGraphicsPipelineDesc("vertex_main",
                                      "fragment_main",
                                      RHI::Format::BGRA8Unorm,
                                      RHI::Format::Depth32Float,
                                      "first label");
    RHI::GraphicsPipelineDesc equivalent = desc;
    equivalent.debugName = "second label";
    equivalent.vertexShader.debugName = "ignored vertex label";
    equivalent.fragmentShader.debugName = "ignored fragment label";

    const MetalRenderPipeline* first = cache.FindOrCreate(device, library, desc);
    const MetalRenderPipeline* second = cache.FindOrCreate(device, library, equivalent);

    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
    MetalPipelineCacheStats stats = cache.GetStats();
    EXPECT_EQ(stats.cachedPipelineCount, 1u);
    EXPECT_EQ(stats.requestCount, 2u);
    EXPECT_EQ(stats.hitCount, 1u);
    EXPECT_EQ(stats.missCount, 1u);
    EXPECT_EQ(stats.failedCreateCount, 0u);
    EXPECT_TRUE(stats.HasCachedPipelines());

    RHI::GraphicsPipelineDesc different = desc;
    different.rasterState.depthClipEnabled = false;
    const MetalRenderPipeline* third = cache.FindOrCreate(device, library, different);

    ASSERT_NE(third, nullptr);
    EXPECT_NE(first, third);
    stats = cache.GetStats();
    EXPECT_EQ(stats.cachedPipelineCount, 2u);
    EXPECT_EQ(stats.requestCount, 3u);
    EXPECT_EQ(stats.hitCount, 1u);
    EXPECT_EQ(stats.missCount, 2u);

    RHI::GraphicsPipelineDesc differentBlend = desc;
    differentBlend.colorAttachments[0].blendEnabled = true;
    differentBlend.colorAttachments[0].sourceColorBlendFactor = RHI::BlendFactor::SourceAlpha;
    differentBlend.colorAttachments[0].destinationColorBlendFactor =
        RHI::BlendFactor::OneMinusSourceAlpha;
    const MetalRenderPipeline* fourth = cache.FindOrCreate(device, library, differentBlend);

    ASSERT_NE(fourth, nullptr);
    EXPECT_NE(first, fourth);
    EXPECT_NE(third, fourth);
    stats = cache.GetStats();
    EXPECT_EQ(stats.cachedPipelineCount, 3u);
    EXPECT_EQ(stats.requestCount, 4u);
    EXPECT_EQ(stats.hitCount, 1u);
    EXPECT_EQ(stats.missCount, 3u);

    cache.Shutdown(&device, 5);
    stats = cache.GetStats();
    EXPECT_EQ(stats.cachedPipelineCount, 0u);
    EXPECT_FALSE(stats.HasCachedPipelines());
    EXPECT_EQ(device.PendingReleaseCount(), 6u);

    device.CollectReleasedResources(8, true);
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalPipelineTest, RejectsMissingShaderEntryPoint) {
    MetalRenderPipeline pipeline;
    const RHI::GraphicsPipelineDesc desc =
        RHI::MakeGraphicsPipelineDesc("missing_vertex",
                                      "fragment_main",
                                      RHI::Format::BGRA8Unorm,
                                      RHI::Format::Depth32Float);

    EXPECT_FALSE(pipeline.Initialize(device, library, desc));
    EXPECT_FALSE(pipeline.IsReady());
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalPipelineTest, RejectsNonUtf8ShaderEntryPointNames) {
    const char invalidEntryPoint[] = {'v', static_cast<char>(0xff), '\0'};

    MetalRenderPipeline pipeline;
    RHI::GraphicsPipelineDesc desc =
        RHI::MakeGraphicsPipelineDesc(invalidEntryPoint,
                                      "fragment_main",
                                      RHI::Format::BGRA8Unorm,
                                      RHI::Format::Depth32Float);
    EXPECT_TRUE(RHI::ValidateGraphicsPipelineDesc(desc));
    EXPECT_FALSE(pipeline.Initialize(device, library, desc));
    EXPECT_FALSE(pipeline.IsReady());

    desc = RHI::MakeGraphicsPipelineDesc("vertex_main",
                                         invalidEntryPoint,
                                         RHI::Format::BGRA8Unorm,
                                         RHI::Format::Depth32Float);
    EXPECT_TRUE(RHI::ValidateGraphicsPipelineDesc(desc));
    EXPECT_FALSE(pipeline.Initialize(device, library, desc));
    EXPECT_FALSE(pipeline.IsReady());
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

TEST_F(MetalPipelineTest, CacheTracksFailedCreatesWithoutCachingInvalidPipeline) {
    MetalPipelineCache cache;
    const RHI::GraphicsPipelineDesc desc =
        RHI::MakeGraphicsPipelineDesc("missing_vertex",
                                      "fragment_main",
                                      RHI::Format::BGRA8Unorm,
                                      RHI::Format::Depth32Float);

    EXPECT_EQ(cache.FindOrCreate(device, library, desc), nullptr);

    const MetalPipelineCacheStats stats = cache.GetStats();
    EXPECT_EQ(stats.cachedPipelineCount, 0u);
    EXPECT_EQ(stats.requestCount, 1u);
    EXPECT_EQ(stats.hitCount, 0u);
    EXPECT_EQ(stats.missCount, 1u);
    EXPECT_EQ(stats.failedCreateCount, 1u);
    EXPECT_FALSE(stats.HasCachedPipelines());
    EXPECT_EQ(device.PendingReleaseCount(), 0u);
}

} // namespace testing
} // namespace MetalBackend
} // namespace Next
