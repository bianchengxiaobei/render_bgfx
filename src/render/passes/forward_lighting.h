#pragma once

#include "gfx/encoder.h"
#include "gfx/generated/uniforms.h"

#include <cstdint>

namespace hbg {

constexpr std::uint8_t kForwardLightingLightDataStage = 5;
constexpr std::uint8_t kForwardLightingDirectionalShadowStage = 6;
constexpr std::uint8_t kForwardLightingLightListStage = 7;
constexpr std::uint8_t kForwardLightingShadowContextStage = 8;
constexpr std::uint8_t kForwardLightingPunctualShadowStage = 9;
constexpr std::uint8_t kForwardLightingEnvironmentStage = 10;
constexpr std::uint8_t kForwardLightingContactShadowStage = 11;
constexpr std::uint8_t kForwardLightingProbeArrayStage = 14;
constexpr std::uint8_t kForwardLightingCookieAtlasStage = 15;

struct ForwardLightingContext {
    const gfx::generated::GlobalUniformBinder* globalBinder = nullptr;
    const gfx::generated::PassUniformBinder* passBinder = nullptr;
    const gfx::generated::ClusterUniformBinder* clusterBinder = nullptr;
    const gfx::generated::EnvironmentUniformBinder* environmentBinder = nullptr;
    const gfx::generated::ProbeUniformBinder* probeBinder = nullptr;
    const gfx::generated::ObjectUniformBinder* objectBinder = nullptr;

    gfx::generated::GlobalUniforms globalUniforms{};
    gfx::generated::PassUniforms passUniforms{};
    gfx::generated::ClusterUniforms clusterUniforms{};
    gfx::generated::EnvironmentUniforms environmentUniforms{};
    gfx::generated::ProbeUniforms probeUniforms{};
    gfx::generated::ObjectUniforms objectUniforms{};

    gfx::UniformHandle lightDataSampler{};
    gfx::UniformHandle directionalShadowSampler{};
    gfx::UniformHandle lightListSampler{};
    gfx::UniformHandle punctualShadowSampler{};
    gfx::UniformHandle environmentSampler{};
    gfx::UniformHandle contactShadowSampler{};
    gfx::UniformHandle probeArraySampler{};
    gfx::UniformHandle cookieAtlasSampler{};

    gfx::TextureHandle lightData{};
    gfx::TextureHandle directionalShadow{};
    gfx::TextureHandle lightListTexture{};
    gfx::BufferHandle lightListBuffer{};
    gfx::BufferHandle shadowContext{};
    gfx::TextureHandle punctualShadow{};
    gfx::TextureHandle environment{};
    gfx::TextureHandle environmentFallback{};
    gfx::TextureHandle contactShadow{};
    gfx::TextureHandle contactShadowFallback{};
    gfx::TextureHandle probeArray{};
    gfx::TextureHandle probeFallback{};
    gfx::TextureHandle cookieAtlas{};
    gfx::TextureHandle cookieFallback{};

    std::uint32_t directionalShadowFlags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    std::uint32_t environmentFlags = BGFX_SAMPLER_V_CLAMP;
    std::uint32_t cookieFlags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    bool gpuLightList = false;
    bool bindContactShadow = true;
};

inline void bindForwardLighting(gfx::Encoder& encoder, const ForwardLightingContext& context) {
    if (context.globalBinder != nullptr) {
        context.globalBinder->upload(context.globalUniforms);
    }
    if (context.passBinder != nullptr) {
        context.passBinder->upload(context.passUniforms);
    }
    if (context.clusterBinder != nullptr) {
        context.clusterBinder->upload(context.clusterUniforms);
    }
    if (context.environmentBinder != nullptr) {
        context.environmentBinder->upload(context.environmentUniforms);
    }
    if (context.probeBinder != nullptr) {
        context.probeBinder->upload(context.probeUniforms);
    }
    if (context.objectBinder != nullptr) {
        context.objectBinder->upload(context.objectUniforms);
    }

    if (context.lightDataSampler.valid() && context.lightData.valid()) {
        encoder.setTexture(5, context.lightDataSampler, context.lightData,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    if (context.directionalShadowSampler.valid() && context.directionalShadow.valid()) {
        encoder.setTexture(6, context.directionalShadowSampler, context.directionalShadow,
                           context.directionalShadowFlags);
    }
    if (context.gpuLightList && context.lightListBuffer.valid()) {
        encoder.setStorageBuffer(7, context.lightListBuffer, bgfx::Access::Read);
    } else if (context.lightListSampler.valid() && context.lightListTexture.valid()) {
        encoder.setTexture(7, context.lightListSampler, context.lightListTexture,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    if (context.shadowContext.valid()) {
        encoder.setStorageBuffer(8, context.shadowContext, bgfx::Access::Read);
    }
    if (context.punctualShadowSampler.valid() && context.punctualShadow.valid()) {
        encoder.setTexture(9, context.punctualShadowSampler, context.punctualShadow,
                           BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
    }
    if (context.environmentSampler.valid()) {
        const gfx::TextureHandle texture = context.environment.valid() ? context.environment
                                                                       : context.environmentFallback;
        if (texture.valid()) {
            encoder.setTexture(10, context.environmentSampler, texture, context.environmentFlags);
        }
    }
    if (context.bindContactShadow && context.contactShadowSampler.valid()) {
        const gfx::TextureHandle texture = context.contactShadow.valid() ? context.contactShadow
                                                                         : context.contactShadowFallback;
        if (texture.valid()) {
            encoder.setTexture(11, context.contactShadowSampler, texture,
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
        }
    }
    if (context.probeArraySampler.valid()) {
        const gfx::TextureHandle texture =
            context.probeArray.valid() ? context.probeArray : context.probeFallback;
        if (texture.valid()) {
            encoder.setTexture(14, context.probeArraySampler, texture, BGFX_SAMPLER_V_CLAMP);
        }
    }
    if (context.cookieAtlasSampler.valid()) {
        const gfx::TextureHandle texture =
            context.cookieAtlas.valid() ? context.cookieAtlas : context.cookieFallback;
        if (texture.valid()) {
            encoder.setTexture(15, context.cookieAtlasSampler, texture, context.cookieFlags);
        }
    }
}

} // namespace hbg
