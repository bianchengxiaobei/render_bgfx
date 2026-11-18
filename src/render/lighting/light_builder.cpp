#include "render/lighting/light_builder.h"
#include "render/shadows/contact_shadow.h"

#include <algorithm>
#include <cmath>

namespace hbg::render {
namespace {

Vec3 worldTranslation(const Mat4& matrix) {
    return Vec3(matrix.values[12], matrix.values[13], matrix.values[14]);
}

float lightImportance(const Light& light, const Vec3& cameraPosition) {
    const float intensity = std::max(light.intensity, 0.0f);
    if (light.type == LightType::Directional) {
        return intensity;
    }
    const float distanceSq =
        dot(worldTranslation(light.world) - cameraPosition, worldTranslation(light.world) - cameraPosition);
    const float range = std::max(light.range, 0.0f);
    return intensity * (range * range) / (distanceSq + 1.0f);
}

uint32_t lightFlags(GpuLightType type, bool castShadows) {
    return static_cast<uint32_t>(type) | (castShadows ? (1u << 3) : 0u);
}

bool isArea(const Light& light) {
    return light.type == LightType::Area;
}

GpuLightType areaLightType(const Light& light) {
    switch (light.areaShape) {
    case LightAreaShape::Tube:
        return GpuLightType::AreaTube;
    case LightAreaShape::Disc:
        return GpuLightType::AreaDisc;
    case LightAreaShape::Rectangle:
    default:
        return GpuLightType::AreaRectangle;
    }
}

LightCategory lightCategory(const Light& light) {
    return isArea(light) ? LightCategory::Area : LightCategory::Punctual;
}

uint32_t lightFeatures(const Light& light) {
    if (light.type == LightType::Directional) {
        return kLightFeatureDirectional;
    }
    return isArea(light) ? kLightFeatureArea : kLightFeaturePunctual;
}

} // namespace

void LightBuilder::build(const RenderScene& scene, const Vec3& cameraPosition, const Vec3& cameraForward,
                         float cameraFar, bool contactShadowsEnabled, bool directionalOnly,
                         const ShadowSettings* shadowSettings, uint32_t lightCapacity, bool supportLightLayers) {
    *m_data = {};
    m_shadowCaster = nullptr;
    m_selectedLights.clear();

    std::vector<const Light*> directionalLights;
    std::vector<const Light*> localLights;
    directionalLights.reserve(scene.lights.size());
    localLights.reserve(scene.lights.size());

    for (const Light& light : scene.lights) {
        if (light.type == LightType::Directional) {
            directionalLights.push_back(&light);
            continue;
        }

        const Vec3 position = worldTranslation(light.world);
        const Vec3 toLight = position - cameraPosition;
        const float range = std::max(light.range, 0.0f);
        if (dot(toLight, cameraForward) + range < 0.0f) {
            continue;
        }
        if (cameraFar > 0.0f && length(toLight) > cameraFar + range) {
            continue;
        }
        localLights.push_back(&light);
    }

    std::stable_sort(directionalLights.begin(), directionalLights.end(),
                     [](const Light* lhs, const Light* rhs) { return lhs->intensity > rhs->intensity; });
    std::stable_sort(localLights.begin(), localLights.end(),
                     [&cameraPosition](const Light* lhs, const Light* rhs) {
                         return lightImportance(*lhs, cameraPosition) > lightImportance(*rhs, cameraPosition);
                     });

    if (directionalLights.empty() && localLights.empty()) {
        m_fallbackLight.name = "fallback directional";
        m_fallbackLight.type = LightType::Directional;
        m_fallbackLight.color = Vec3(1.0f, 1.0f, 1.0f);
        m_fallbackLight.intensity = 4.0f;
        m_fallbackLight.castShadows = false;
        m_fallbackLight.world = Mat4::identity();
        directionalLights.push_back(&m_fallbackLight);
    }

    const Light* primaryDirectional = directionalLights.empty() ? nullptr : directionalLights.front();
    if (primaryDirectional != nullptr && primaryDirectional->castShadows && primaryDirectional->intensity > 0.0f) {
        m_shadowCaster = primaryDirectional;
    }

    const Light* selectedDirectional = primaryDirectional;
    if (selectedDirectional != nullptr) {
        m_data->directionalCount = 1;
        m_data->hasDirectional = true;
        m_data->primaryDirection = normalize(transformDirection(selectedDirectional->world, Vec3(0.0f, 0.0f, 1.0f)));
        m_data->primaryColor = selectedDirectional->color;
        m_data->primaryIntensity = std::max(selectedDirectional->intensity, 0.0f);
    }

    const uint32_t capacity = sanitizeLightListCapacity(lightCapacity);
    const uint32_t maxLocal = capacity > m_data->directionalCount ? capacity - m_data->directionalCount : 0;
    const uint32_t selectedLocalCount = std::min<uint32_t>(static_cast<uint32_t>(localLights.size()), maxLocal);
    for (uint32_t index = 0; index < selectedLocalCount; ++index) {
        if (isArea(*localLights[index])) {
            ++m_data->areaCount;
        } else {
            ++m_data->punctualCount;
        }
    }
    m_data->lightCount = m_data->directionalCount + selectedLocalCount;
    m_data->truncatedLightCount =
        static_cast<uint32_t>(directionalLights.size() + localLights.size()) > m_data->lightCount
            ? static_cast<uint32_t>(directionalLights.size() + localLights.size()) - m_data->lightCount
            : 0;

    const ShadowSettings fallbackShadowSettings;
    const ShadowSettings& globalShadow = shadowSettings != nullptr ? *shadowSettings : fallbackShadowSettings;
    uint32_t nextContactIndex = 1;
    auto writeLight = [this, supportLightLayers](uint32_t index, const Light& light, bool castShadows,
                                                 uint32_t contactMask, const LightShadowSettings& shadow) {
        const bool directional = light.type == LightType::Directional;
        const bool spot = light.type == LightType::Spot;
        const bool area = isArea(light);
        const Vec3 position = directional ? Vec3(0.0f, 0.0f, 0.0f) : worldTranslation(light.world);
        const Vec3 forward = directional ? normalize(transformDirection(light.world, Vec3(0.0f, 0.0f, 1.0f)))
                                        : (spot || area ? normalize(transformDirection(light.world, Vec3(0.0f, 0.0f, -1.0f)))
                                                        : Vec3(0.0f, 0.0f, 0.0f));
        Vec3 right(0.0f, 0.0f, 0.0f);
        Vec3 up(0.0f, 0.0f, 0.0f);
        if (directional || area) {
            right = normalize(transformDirection(light.world, Vec3(1.0f, 0.0f, 0.0f)));
            up = normalize(transformDirection(light.world, Vec3(0.0f, 1.0f, 0.0f)));
            right = normalize(right - forward * dot(right, forward));
            up = up - forward * dot(up, forward) - right * dot(up, right);
            up = normalize(up);
        }
        const bool tubeArea = area && light.areaShape == LightAreaShape::Tube;
        const float halfWidth = area ? std::max(light.areaWidth, 0.0f) * 0.5f : 0.0f;
        const float halfHeight = area && !tubeArea ? std::max(light.areaHeight, 0.0f) * 0.5f : 0.0f;
        const float cosOuter = spot ? std::clamp(std::cos(light.outerConeAngle), -1.0f, 1.0f) : 0.0f;
        const float cosInner = spot ? std::clamp(std::cos(light.innerConeAngle), -1.0f, 1.0f) : 0.0f;
        const GpuLightType type = directional ? GpuLightType::Directional
                                              : (spot ? GpuLightType::Spot
                                                      : (area ? areaLightType(light) : GpuLightType::Point));
        const int32_t shadowIndex = (directional && &light == m_shadowCaster) ? 0 : -1;
        const uint32_t flags = lightFlags(type, castShadows);

        const uint32_t base = index * kGpuLightTexels;
        m_data->texels[base + 0] = Vec4(position.x, position.y, position.z, std::max(light.range, 0.0f));
        m_data->texels[base + 1] = Vec4(light.color.x, light.color.y, light.color.z, std::max(light.intensity, 0.0f));
        m_data->texels[base + 2] = Vec4(forward.x, forward.y, forward.z, cosOuter);
        m_data->texels[base + 3] =
            Vec4(static_cast<float>(shadowIndex), cosInner, static_cast<float>(contactMask), static_cast<float>(flags));
        m_data->texels[base + 4] = Vec4(shadow.tint.x, shadow.tint.y, shadow.tint.z, shadow.dimmer);
        m_data->texels[base + 5] =
            Vec4(shadow.fadeDistance, shadow.penumbraTint ? 1.0f : 0.0f, light.iesCut,
                 area ? light.areaShadowCone : 0.0f);
        const uint32_t featureFlags = lightFeatures(light);
        m_data->texels[base + 6] = Vec4(static_cast<float>(supportLightLayers ? light.lightLayerMask : kRenderingLayersMask),
                                       static_cast<float>(lightCategory(light)),
                                       static_cast<float>(featureFlags), 0.0f);
        const uint32_t cookieShapeFlags =
            static_cast<uint32_t>(light.areaShape) | (light.hasIes ? (1u << 3) : 0u);
        m_data->texels[base + 7] =
            Vec4(static_cast<float>(light.cookieMode), static_cast<float>(light.cookieType), 0.0f,
                 static_cast<float>(cookieShapeFlags));
        m_data->texels[base + 11] = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        m_data->texels[base + 8] = Vec4(right.x, right.y, right.z, halfWidth);
        m_data->texels[base + 9] = Vec4(up.x, up.y, up.z, halfHeight);
        m_data->texels[base + 10] = Vec4(light.cookieScale.x, light.cookieScale.y, light.cookieOffset.x,
                                         light.cookieOffset.y);
        for (uint32_t sample = 0; sample < kLightIesSampleCount; ++sample) {
            m_data->texels[base + kLightIesBaseTexel + sample].x = light.iesSamples[sample];
        }
    };

    uint32_t lightIndex = 0;
    if (selectedDirectional != nullptr) {
        const LightShadowSettings shadow = resolveLightShadow(*selectedDirectional, globalShadow);
        const uint32_t contactMask = contactShadowsEnabled && selectedDirectional->castShadows &&
                                             shadow.contactShadows
                                         ? contactShadowBit(0)
                                         : 0u;
        m_selectedLights.push_back(selectedDirectional);
        writeLight(lightIndex++, *selectedDirectional, selectedDirectional == m_shadowCaster, contactMask, shadow);
    }
    for (uint32_t index = 0; index < selectedLocalCount; ++index) {
        const Light& light = *localLights[index];
        const LightShadowSettings shadow = resolveLightShadow(light, globalShadow);
        m_selectedLights.push_back(&light);
        uint32_t contactMask = 0u;
        if (contactShadowsEnabled && !directionalOnly && !isArea(light) && light.castShadows &&
            shadow.contactShadows) {
            contactMask = contactShadowBit(nextContactIndex);
            if (contactMask != 0u) {
                ++nextContactIndex;
            }
        }
        const bool castShadows =
            isArea(light) ? (light.areaShape == LightAreaShape::Rectangle && light.castShadows) : light.castShadows;
        writeLight(lightIndex++, light, castShadows, contactMask, shadow);
    }

    if (selectedDirectional == &m_fallbackLight) {
        const Vec3 fallbackDirection = normalize(Vec3(0.35f, 0.7f, 0.6f));
        m_data->primaryDirection = fallbackDirection;
        m_data->texels[2] = Vec4(fallbackDirection.x, fallbackDirection.y, fallbackDirection.z, 0.0f);
    }

    m_data->shadowedCount = m_shadowCaster != nullptr ? 1u : 0u;
}

void LightBuilder::setShadowIndex(uint32_t lightIndex, int32_t shadowIndex) {
    if (lightIndex >= m_selectedLights.size()) {
        return;
    }
    m_data->texels[lightIndex * kGpuLightTexels + 3].x = static_cast<float>(shadowIndex);
}

void LightBuilder::setShadowDimmer(uint32_t lightIndex, float dimmer) {
    if (lightIndex >= m_selectedLights.size()) {
        return;
    }
    m_data->texels[lightIndex * kGpuLightTexels + 4].w = std::clamp(dimmer, 0.0f, 1.0f);
}

void LightBuilder::setCookieAtlas(uint32_t lightIndex, uint32_t tileIndex, Vec4 scaleOffset) {
    if (lightIndex >= m_selectedLights.size() || tileIndex >= kCookieAtlasTileCapacity) {
        return;
    }
    m_data->texels[lightIndex * kGpuLightTexels + 7].z = static_cast<float>(tileIndex + 1u);
    m_data->texels[lightIndex * kGpuLightTexels + 11] = scaleOffset;
}

} // namespace hbg::render
