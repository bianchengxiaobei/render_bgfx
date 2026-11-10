#pragma once

#include "asset/material.h"

#include <cstdint>
#include <string_view>

namespace hbg::render {

enum class ShaderPass : uint8_t {
    Forward,
    DepthOnly,
    DepthForwardOnly,
    ForwardOnly,
    GBuffer,
    GBufferWithPrepass,
    SRPDefaultUnlit,
    ShadowCaster,
    MotionVectors,
    TransparentDepthPrepass,
    TransparentBackface,
    TransparentDepthPostpass,
    DistortionVectors,
    Count,
};

using ShaderPassMask = uint32_t;

inline constexpr ShaderPassMask kShaderPassMaskNone = 0;
inline constexpr ShaderPassMask kShaderPassMaskAll = 0xffffffffu;
inline constexpr ShaderPassMask kForwardPassMask =
    (1u << static_cast<uint32_t>(ShaderPass::Forward)) | (1u << static_cast<uint32_t>(ShaderPass::ForwardOnly));
inline constexpr ShaderPassMask kAllForwardOpaquePassMask =
    kForwardPassMask | (1u << static_cast<uint32_t>(ShaderPass::SRPDefaultUnlit));
inline constexpr ShaderPassMask kAllTransparentPassMask =
    (1u << static_cast<uint32_t>(ShaderPass::TransparentBackface)) | kForwardPassMask |
    (1u << static_cast<uint32_t>(ShaderPass::SRPDefaultUnlit));
inline constexpr ShaderPassMask kDepthPrepassPassMask =
    (1u << static_cast<uint32_t>(ShaderPass::DepthOnly)) |
    (1u << static_cast<uint32_t>(ShaderPass::DepthForwardOnly));
inline constexpr ShaderPassMask kMotionVectorsPassMask = 1u << static_cast<uint32_t>(ShaderPass::MotionVectors);
constexpr ShaderPassMask shaderPassMask(ShaderPass pass) {
    return 1u << static_cast<uint32_t>(pass);
}

std::string_view shaderPassName(ShaderPass pass);
ShaderPassMask defaultShaderPassMask(const asset::Material& material);

} // namespace hbg::render
