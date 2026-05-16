#pragma once

#include "next/rhi/resource.h"
#include "next/rhi/pipeline.h"
#include "next/rhi/render_pass.h"
#include "next/rhi/shader_resource_group.h"
#include "next/rhi/command.h"

#import <Metal/Metal.h>

namespace Next {
namespace MetalBackend {

MTLPixelFormat ToMetalPixelFormat(RHI::Format format);
MTLStorageMode ToMetalStorageMode(RHI::ResourceMemory memory);
MTLResourceOptions ToMetalResourceOptions(RHI::ResourceMemory memory);
MTLTextureUsage ToMetalTextureUsage(RHI::ResourceUsageFlags usage);
MTLPrimitiveType ToMetalPrimitiveType(RHI::PrimitiveTopology topology);
MTLTriangleFillMode ToMetalTriangleFillMode(RHI::FillMode mode);
MTLDepthClipMode ToMetalDepthClipMode(bool depthClipEnabled);
MTLCullMode ToMetalCullMode(RHI::CullMode mode);
MTLWinding ToMetalWinding(RHI::FrontFaceWinding winding);
MTLVertexFormat ToMetalVertexFormat(RHI::VertexFormat format);
MTLVertexStepFunction ToMetalVertexStepFunction(RHI::VertexStepFunction stepFunction);
MTLIndexType ToMetalIndexType(RHI::IndexFormat format);
MTLViewport ToMetalViewport(const RHI::ViewportDesc& viewport);
MTLScissorRect ToMetalScissorRect(const RHI::ScissorRectDesc& scissor);
MTLLoadAction ToMetalLoadAction(RHI::AttachmentLoadAction action);
MTLStoreAction ToMetalStoreAction(RHI::AttachmentStoreAction action);
MTLStencilOperation ToMetalStencilOperation(RHI::StencilOperation operation);
MTLBlendFactor ToMetalBlendFactor(RHI::BlendFactor factor);
MTLBlendOperation ToMetalBlendOperation(RHI::BlendOperation operation);
MTLColorWriteMask ToMetalColorWriteMask(RHI::ColorWriteMaskFlags mask);
MTLSamplerMinMagFilter ToMetalSamplerFilter(RHI::SamplerFilter filter);
MTLSamplerMipFilter ToMetalSamplerMipFilter(RHI::SamplerMipFilter filter);
MTLSamplerAddressMode ToMetalSamplerAddressMode(RHI::SamplerAddressMode addressMode);
MTLSamplerBorderColor ToMetalSamplerBorderColor(RHI::SamplerBorderColor color);
MTLCompareFunction ToMetalCompareFunction(RHI::CompareFunction function);
MTLDataType ToMetalArgumentDataType(RHI::ShaderResourceBindingType type);
MTLBindingAccess ToMetalArgumentAccess(RHI::ShaderResourceBindingType type);
void ConfigureMetalColorAttachmentDescriptor(const RHI::ColorAttachmentDesc& desc,
                                             MTLRenderPipelineColorAttachmentDescriptor* colorDesc);
void ConfigureMetalVertexDescriptor(const RHI::VertexInputStateDesc& desc,
                                    MTLVertexDescriptor* vertexDesc);
void ConfigureMetalMultisampleDescriptor(const RHI::MultisampleStateDesc& desc,
                                         MTLRenderPipelineDescriptor* pipelineDesc);
void ConfigureMetalSamplerDescriptor(const RHI::SamplerDesc& desc, MTLSamplerDescriptor* samplerDesc);
void ConfigureMetalDepthStencilDescriptor(const RHI::DepthStencilStateDesc& desc,
                                          MTLDepthStencilDescriptor* depthStencilDesc);
bool ConfigureMetalArgumentDescriptor(const RHI::ShaderResourceBindingDesc& desc,
                                      uint32_t argumentIndex,
                                      MTLArgumentDescriptor* argumentDesc);
NSArray<MTLArgumentDescriptor*>* MakeMetalArgumentDescriptors(
    const RHI::ShaderResourceGroupLayoutDesc& layout);

} // namespace MetalBackend
} // namespace Next
