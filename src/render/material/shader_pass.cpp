#include "render/material/shader_pass.h"

namespace hbg::render {

std::string_view shaderPassName(ShaderPass pass) {
    switch (pass) {
    case ShaderPass::Forward:
        return "Forward";
    case ShaderPass::DepthOnly:
        return "DepthOnly";
    case ShaderPass::DepthForwardOnly:
        return "DepthForwardOnly";
    case ShaderPass::ForwardOnly:
        return "ForwardOnly";
    case ShaderPass::GBuffer:
        return "GBuffer";
    case ShaderPass::GBufferWithPrepass:
        return "GBufferWithPrepass";
    case ShaderPass::SRPDefaultUnlit:
        return "SRPDefaultUnlit";
    case ShaderPass::ShadowCaster:
        return "ShadowCaster";
    case ShaderPass::MotionVectors:
        return "MotionVectors";
    case ShaderPass::TransparentDepthPrepass:
        return "TransparentDepthPrepass";
    case ShaderPass::TransparentBackface:
        return "TransparentBackface";
    case ShaderPass::TransparentDepthPostpass:
        return "TransparentDepthPostpass";
    case ShaderPass::DistortionVectors:
        return "DistortionVectors";
    case ShaderPass::Count:
        break;
    }
    return {};
}

ShaderPassMask defaultShaderPassMask(const asset::Material& material) {
    ShaderPassMask mask = shaderPassMask(ShaderPass::ShadowCaster) | shaderPassMask(ShaderPass::MotionVectors);
    const bool transparent = material.alphaMode == asset::AlphaMode::Blend;

    if (material.type == asset::MaterialType::Unlit) {
        mask |= shaderPassMask(ShaderPass::ForwardOnly) | shaderPassMask(ShaderPass::DepthForwardOnly);
        if (transparent) {
            mask |= shaderPassMask(ShaderPass::DistortionVectors);
        }
        return mask;
    }

    mask |= shaderPassMask(ShaderPass::Forward);
    if (transparent) {
        mask |= shaderPassMask(ShaderPass::TransparentDepthPrepass) | shaderPassMask(ShaderPass::TransparentBackface) |
                shaderPassMask(ShaderPass::TransparentDepthPostpass);
    } else {
        mask |= shaderPassMask(ShaderPass::GBuffer) | shaderPassMask(ShaderPass::DepthOnly);
    }
    return mask;
}

} // namespace hbg::render
