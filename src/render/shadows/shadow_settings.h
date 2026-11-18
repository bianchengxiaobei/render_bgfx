#pragma once

#include "render/scene/light.h"
#include "render/shadows/contact_shadow.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace hbg::render {

constexpr uint32_t kMaxDirectionalCascades = 4;
constexpr uint32_t kDefaultShadowRequests = 64;
constexpr uint32_t kMaxShadowRequests = 128;
constexpr uint32_t kDefaultShadowAtlasSize = 2048;
constexpr uint32_t kDefaultAreaShadowAtlasSize = 1024;
constexpr uint32_t kDefaultAreaShadowResolution = 512;

enum class ShadowFilter : uint8_t {
    Off,
    Pcf3x3,
    Pcf5x5,
    Pcf7x7,
    Pcss,
};

struct ShadowSettings {
    float maxDistance = 200.0f;
    uint32_t cascadeCount = kMaxDirectionalCascades;
    std::array<float, kMaxDirectionalCascades> cascadeSplits{0.10f, 0.25f, 0.50f, 1.0f};
    float cascadeFade = 0.10f;

    ShadowFilter directionalFilter = ShadowFilter::Pcf7x7;
    ShadowFilter punctualFilter = ShadowFilter::Pcf5x5;
    ShadowFilter areaFilter = ShadowFilter::Pcss;

    uint32_t directionalResolution = 1024;
    uint32_t pointResolution = 256;
    uint32_t spotResolution = 512;
    uint32_t areaResolution = kDefaultAreaShadowResolution;
    uint32_t atlasSize = kDefaultShadowAtlasSize;
    uint32_t areaAtlasSize = kDefaultAreaShadowAtlasSize;
    uint32_t maxRequests = kDefaultShadowRequests;
    float normalBias = 0.2f;
    float slopeBias = 0.5f;
    float shadowNearPlane = 0.1f;
    float softness = 0.5f;
    uint32_t blockerSamples = 16;
    uint32_t filterSamples = 24;
    float minFilterSize = 0.01f;
    bool microShadows = false;
    float microShadowOpacity = 1.0f;
    ContactShadowSettings contactShadows;
    bool cachedDirectionalShadows = false;
    bool cachedPunctualShadows = false;
    bool cachedAreaShadows = false;

    float shadowDimmer = 1.0f;
    float shadowFadeDistance = 10000.0f;
    Vec3 shadowTint{0.0f, 0.0f, 0.0f};
};

const char* shadowFilterName(ShadowFilter filter);
bool parseShadowFilter(std::string_view text, ShadowFilter& filter);
uint32_t shadowFilterTapMode(ShadowFilter filter);
const char* shadowUpdateModeName(ShadowUpdateMode mode);
bool parseShadowUpdateMode(std::string_view text, ShadowUpdateMode& mode);
void sanitizeLightShadowSettings(LightShadowSettings& settings);
LightShadowSettings resolveLightShadow(const Light& light, const ShadowSettings& settings);

void sanitizeShadowSettings(ShadowSettings& settings);
void computeShadowFade(float distance, float border, float& scale, float& bias);
bool parseShadowSettings(std::string_view json, ShadowSettings& settings, std::string& error);
bool loadShadowSettings(const std::string& path, ShadowSettings& settings, std::string& error);
std::string shadowSettingsToJson(const ShadowSettings& settings);

} // namespace hbg::render
