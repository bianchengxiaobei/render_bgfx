#pragma once

#include "asset/material.h"

#include <cstdint>

namespace hbg::render {

constexpr uint32_t kLitVariantCount = 29;
constexpr uint32_t kLitVariantStencilMask = 0x7fu;

inline constexpr uint32_t kLitMaterialStandard = 1u << 0;
inline constexpr uint32_t kLitMaterialSubsurface = 1u << 2;
inline constexpr uint32_t kLitMaterialTransmission = 1u << 3;
inline constexpr uint32_t kLitMaterialAnisotropy = 1u << 4;
inline constexpr uint32_t kLitMaterialIridescence = 1u << 5;
inline constexpr uint32_t kLitMaterialClearCoat = 1u << 6;

inline constexpr uint32_t kLitLightPunctual = 1u << 12;
inline constexpr uint32_t kLitLightArea = 1u << 13;
inline constexpr uint32_t kLitLightDirectional = 1u << 14;
inline constexpr uint32_t kLitLightShadows = 1u << 15;

constexpr uint32_t kLitMaterialAnySss = kLitMaterialStandard | kLitMaterialSubsurface | kLitMaterialTransmission;
constexpr uint32_t kLitMaterialAll = kLitMaterialStandard | kLitMaterialSubsurface | kLitMaterialTransmission |
                                     kLitMaterialAnisotropy | kLitMaterialIridescence | kLitMaterialClearCoat;

constexpr uint32_t kLitFeatureVariantFlags[kLitVariantCount] = {
    kLitMaterialStandard,
    kLitMaterialStandard | kLitLightDirectional,
    kLitMaterialStandard | kLitLightDirectional | kLitLightPunctual,
    kLitMaterialStandard | kLitLightDirectional | kLitLightArea,
    kLitMaterialStandard | kLitLightDirectional | kLitLightPunctual | kLitLightArea,
    kLitMaterialStandard | kLitLightDirectional | kLitLightShadows,
    kLitMaterialStandard | kLitLightDirectional | kLitLightPunctual | kLitLightShadows,
    kLitMaterialStandard | kLitLightDirectional | kLitLightPunctual | kLitLightArea | kLitLightShadows,
    kLitMaterialAnySss,
    kLitMaterialAnySss | kLitLightDirectional,
    kLitMaterialAnySss | kLitLightDirectional | kLitLightPunctual,
    kLitMaterialAnySss | kLitLightDirectional | kLitLightArea,
    kLitMaterialAnySss | kLitLightDirectional | kLitLightPunctual | kLitLightArea,
    kLitMaterialStandard | kLitMaterialAnisotropy,
    kLitMaterialStandard | kLitMaterialAnisotropy | kLitLightDirectional,
    kLitMaterialStandard | kLitMaterialAnisotropy | kLitLightDirectional | kLitLightPunctual,
    kLitMaterialStandard | kLitMaterialAnisotropy | kLitLightDirectional | kLitLightArea,
    kLitMaterialStandard | kLitMaterialAnisotropy | kLitLightDirectional | kLitLightPunctual | kLitLightArea,
    kLitMaterialStandard | kLitMaterialClearCoat,
    kLitMaterialStandard | kLitMaterialClearCoat | kLitLightDirectional,
    kLitMaterialStandard | kLitMaterialClearCoat | kLitLightDirectional | kLitLightPunctual,
    kLitMaterialStandard | kLitMaterialClearCoat | kLitLightDirectional | kLitLightArea,
    kLitMaterialStandard | kLitMaterialClearCoat | kLitLightDirectional | kLitLightPunctual | kLitLightArea,
    kLitMaterialStandard | kLitMaterialIridescence,
    kLitMaterialStandard | kLitMaterialIridescence | kLitLightDirectional,
    kLitMaterialStandard | kLitMaterialIridescence | kLitLightDirectional | kLitLightPunctual,
    kLitMaterialStandard | kLitMaterialIridescence | kLitLightDirectional | kLitLightArea,
    kLitMaterialStandard | kLitMaterialIridescence | kLitLightDirectional | kLitLightPunctual | kLitLightArea,
    kLitMaterialAll | kLitLightDirectional | kLitLightPunctual | kLitLightArea | kLitLightShadows,
};

constexpr uint32_t litMaterialFlags(uint32_t features) {
    uint32_t flags = kLitMaterialStandard;
    if ((features & asset::MaterialFeatureClearCoat) != 0u) {
        flags |= kLitMaterialClearCoat;
    }
    if ((features & asset::MaterialFeatureTransmission) != 0u) {
        flags |= kLitMaterialTransmission;
    }
    if ((features & asset::MaterialFeatureSubsurface) != 0u) {
        flags |= kLitMaterialSubsurface;
    }
    if ((features & asset::MaterialFeatureAnisotropy) != 0u) {
        flags |= kLitMaterialAnisotropy;
    }
    if ((features & asset::MaterialFeatureIridescence) != 0u) {
        flags |= kLitMaterialIridescence;
    }
    return flags;
}

constexpr uint32_t litVariantFor(uint32_t materialFlags, uint32_t lightFlags) {
    const uint32_t featureFlags = materialFlags | lightFlags;
    for (uint32_t variant = 0; variant < kLitVariantCount; ++variant) {
        if ((featureFlags & kLitFeatureVariantFlags[variant]) == featureFlags) {
            return variant;
        }
    }
    return kLitVariantCount - 1;
}

constexpr uint32_t litVariantForMaterial(uint32_t features, uint32_t lightFlags) {
    return litVariantFor(litMaterialFlags(features), lightFlags);
}

constexpr uint32_t litVariantStencilRef(uint32_t variant) {
    return 1u | ((variant % kLitVariantCount) << 1u);
}

} // namespace hbg::render
