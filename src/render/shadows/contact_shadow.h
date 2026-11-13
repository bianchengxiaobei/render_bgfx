#pragma once

#include <array>
#include <cstdint>

namespace hbg::render {

constexpr uint32_t kMaxContactShadowLights = 8;
constexpr uint32_t kContactShadowMaskBits = 8;
constexpr uint32_t kContactShadowMask = (1u << kContactShadowMaskBits) - 1u;
constexpr uint32_t kContactShadowFadeMask = 0xFFu;

struct ContactShadowSettings {
    bool enabled = false;
    bool directionalOnly = false;
    bool halfResolution = false;
    float length = 0.15f;
    float opacity = 1.0f;
    float distanceScaleFactor = 0.5f;
    float minDistance = 0.0f;
    float maxDistance = 50.0f;
    float fadeDistance = 5.0f;
    float fadeInDistance = 0.0f;
    float rayBias = 0.2f;
    float thicknessScale = 0.15f;
    uint32_t sampleCount = 10;
};

void sanitizeContactShadowSettings(ContactShadowSettings& settings);

std::array<uint32_t, 2> contactShadowResolution(uint32_t width, uint32_t height, bool halfResolution);
uint32_t contactShadowBit(uint32_t index);
uint32_t packContactShadowData(float fade, uint32_t mask);
void unpackContactShadowData(uint32_t packed, float& fade, uint32_t& mask);
bool contactShadowCoversLight(uint32_t pixelMask, uint32_t lightMask);
float computeContactShadowRayLength(float length, float distanceScaleFactor, float linearDepth);
float computeContactShadowFade(float linearDepth, const ContactShadowSettings& settings);
float computeContactShadowThickness(float thicknessScale);
bool contactShadowDistanceHit(float distance, float thickness);

} // namespace hbg::render
