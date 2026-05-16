#pragma once

#include "next/rhi/types.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace Next {
namespace RHI {

static constexpr size_t kMaxShaderResourceGroupBindings = 16;
static constexpr uint32_t kInvalidShaderResourceBindingIndex = 0xffffffffu;
static constexpr uint32_t kMaxShaderResourceBindingIndex = 1023;

enum class ShaderResourceBindingType : uint8_t {
    ConstantBuffer = 0,
    ReadOnlyBuffer,
    ReadWriteBuffer,
    Texture,
    StorageTexture,
    Sampler,
};

struct ShaderResourceBindingDesc {
    ShaderResourceBindingType type = ShaderResourceBindingType::Texture;
    ShaderStageFlags shaderStages = ShaderStageFlag(ShaderStage::Fragment);
    uint32_t bindingIndex = kInvalidShaderResourceBindingIndex;
    uint32_t bindingCount = 1;
    const char* debugName = nullptr;

    bool HasShaderStages() const { return shaderStages != 0; }
    bool HasBindingRange() const {
        return bindingIndex != kInvalidShaderResourceBindingIndex && bindingCount != 0 &&
            bindingIndex <= kMaxShaderResourceBindingIndex &&
            bindingCount - 1 <= kMaxShaderResourceBindingIndex - bindingIndex;
    }
    uint32_t LastBindingIndex() const {
        return HasBindingRange() ? bindingIndex + bindingCount - 1 : kInvalidShaderResourceBindingIndex;
    }
    bool ContainsBindingIndex(uint32_t index) const {
        return HasBindingRange() && index >= bindingIndex && index <= LastBindingIndex();
    }
};

struct ShaderResourceGroupLayoutDesc {
    const char* debugName = nullptr;
    std::array<ShaderResourceBindingDesc, kMaxShaderResourceGroupBindings> bindings{};
    uint32_t bindingCount = 0;

    const ShaderResourceBindingDesc* GetBinding(size_t index) const {
        return index < bindingCount && index < bindings.size() ? &bindings[index] : nullptr;
    }
    bool HasBindings() const { return bindingCount != 0; }
    bool HasCompleteBindingTable() const {
        if (!HasBindings() || bindingCount > bindings.size()) {
            return false;
        }
        for (size_t i = 0; i < bindingCount; ++i) {
            if (!bindings[i].HasBindingRange()) {
                return false;
            }
        }
        return true;
    }
};

enum class ShaderResourceGroupLayoutError : uint8_t {
    None = 0,
    MissingBinding,
    TooManyBindings,
    InvalidBindingType,
    MissingShaderStage,
    InvalidShaderStage,
    InvalidBindingIndex,
    EmptyBindingRange,
    BindingRangeOverflow,
    OverlappingBindingRange,
};

struct ShaderResourceGroupLayoutValidation {
    ShaderResourceGroupLayoutError error = ShaderResourceGroupLayoutError::None;
    uint32_t bindingIndex = 0;
    uint32_t conflictBindingIndex = 0;
    ShaderResourceBindingType bindingType = ShaderResourceBindingType::Texture;
    ShaderStageFlags shaderStages = 0;
    uint32_t registerIndex = 0;
    uint32_t bindingCount = 0;

    explicit operator bool() const { return error == ShaderResourceGroupLayoutError::None; }
};

const char* ShaderResourceBindingTypeName(ShaderResourceBindingType type);
const char* ShaderResourceGroupLayoutErrorName(ShaderResourceGroupLayoutError error);
ShaderResourceGroupLayoutValidation ValidateShaderResourceGroupLayoutDesc(
    const ShaderResourceGroupLayoutDesc& desc);

} // namespace RHI
} // namespace Next
