#include "render/shadows/contact_shadow.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {
namespace {

float finiteOr(float value, float fallback) {
    return std::isfinite(value) ? value : fallback;
}

float saturate(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

void sanitizeContactShadowSettings(ContactShadowSettings& settings) {
    settings.length = std::clamp(finiteOr(settings.length, 0.15f), 0.0f, 1.0f);
    settings.opacity = std::clamp(finiteOr(settings.opacity, 1.0f), 0.0f, 1.0f);
    settings.distanceScaleFactor =
        std::clamp(finiteOr(settings.distanceScaleFactor, 0.5f), 0.0f, 1.0f);
    settings.minDistance = std::max(finiteOr(settings.minDistance, 0.0f), 0.0f);
    settings.maxDistance = std::max(finiteOr(settings.maxDistance, 50.0f), 0.0f);
    if (settings.maxDistance <= settings.minDistance) {
        settings.maxDistance = settings.minDistance + 0.01f;
    }
    settings.fadeDistance = std::max(finiteOr(settings.fadeDistance, 5.0f), 0.0f);
    settings.fadeInDistance = std::max(finiteOr(settings.fadeInDistance, 0.0f), 0.0f);
    settings.rayBias = std::clamp(finiteOr(settings.rayBias, 0.2f), 0.0f, 1.0f);
    settings.thicknessScale = std::clamp(finiteOr(settings.thicknessScale, 0.15f), 0.02f, 1.0f);
    settings.sampleCount = std::clamp(settings.sampleCount, 4u, 64u);
}

std::array<uint32_t, 2> contactShadowResolution(uint32_t width, uint32_t height, bool halfResolution) {
    const uint32_t scale = halfResolution ? 2u : 1u;
    return {
        std::max((width + scale - 1u) / scale, 1u),
        std::max((height + scale - 1u) / scale, 1u),
    };
}

uint32_t contactShadowBit(uint32_t index) {
    return index < kMaxContactShadowLights ? (1u << index) : 0u;
}

uint32_t packContactShadowData(float fade, uint32_t mask) {
    const uint32_t fadeByte = static_cast<uint32_t>(saturate(fade) * 255.0f + 0.5f);
    return ((fadeByte & kContactShadowFadeMask) << kContactShadowMaskBits) | (mask & kContactShadowMask);
}

void unpackContactShadowData(uint32_t packed, float& fade, uint32_t& mask) {
    fade = static_cast<float>(packed >> kContactShadowMaskBits) / 255.0f;
    mask = packed & kContactShadowMask;
}

bool contactShadowCoversLight(uint32_t pixelMask, uint32_t lightMask) {
    return lightMask != 0u && (pixelMask & lightMask) != 0u;
}

float computeContactShadowRayLength(float length, float distanceScaleFactor, float linearDepth) {
    return std::max(length, 0.0f) * std::max(0.5f, std::max(linearDepth, 0.0f) * std::max(distanceScaleFactor, 0.0f));
}

float computeContactShadowFade(float linearDepth, const ContactShadowSettings& settings) {
    if (linearDepth < settings.minDistance || linearDepth > settings.maxDistance) {
        return 0.0f;
    }
    const float fadeIn = settings.fadeInDistance > 1.0e-4f
                             ? saturate((linearDepth - settings.minDistance) / settings.fadeInDistance)
                             : 1.0f;
    const float fadeOut = settings.fadeDistance > 1.0e-4f
                              ? saturate((settings.maxDistance - linearDepth) / settings.fadeDistance)
                              : (linearDepth < settings.maxDistance ? 1.0f : 0.0f);
    return fadeIn * fadeOut;
}

float computeContactShadowThickness(float thicknessScale) {
    return std::max(0.004f, std::max(thicknessScale, 0.0f) * 0.1f);
}

bool contactShadowDistanceHit(float distance, float thickness) {
    return distance >= 0.0f && distance < thickness;
}

} // namespace hbg::render
