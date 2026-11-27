#include "render/pipeline/frame_settings.h"

#include <nlohmann/json.hpp>

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

namespace hbg::render {
namespace {

using Json = nlohmann::json;

constexpr FrameSettingsFieldInfo kFrameSettingsFields[] = {
    {FrameSettingsField::LitShaderMode, "LitShaderMode"},
    {FrameSettingsField::DepthPrepassWithDeferredRendering, "DepthPrepassWithDeferredRendering"},
    {FrameSettingsField::ClearGBuffers, "ClearGBuffers"},
    {FrameSettingsField::MSAAMode, "MSAAMode"},
    {FrameSettingsField::AlphaToMask, "AlphaToMask"},
    {FrameSettingsField::OpaqueObjects, "OpaqueObjects"},
    {FrameSettingsField::TransparentObjects, "TransparentObjects"},
    {FrameSettingsField::TransparentPrepass, "TransparentPrepass"},
    {FrameSettingsField::TransparentPostpass, "TransparentPostpass"},
    {FrameSettingsField::LowResTransparent, "LowResTransparent"},
    {FrameSettingsField::Refraction, "Refraction"},
    {FrameSettingsField::Water, "Water"},
    {FrameSettingsField::WaterDecals, "WaterDecals"},
    {FrameSettingsField::WaterExclusion, "WaterExclusion"},
    {FrameSettingsField::ComputeThickness, "ComputeThickness"},
    {FrameSettingsField::Decals, "Decals"},
    {FrameSettingsField::DecalLayers, "DecalLayers"},
    {FrameSettingsField::RenderingLayerMaskBuffer, "RenderingLayerMaskBuffer"},
    {FrameSettingsField::RayTracing, "RayTracing"},
    {FrameSettingsField::RaytracingVFX, "RaytracingVFX"},
    {FrameSettingsField::CustomPass, "CustomPass"},
    {FrameSettingsField::VariableRateShading, "VariableRateShading"},
    {FrameSettingsField::VirtualTexturing, "VirtualTexturing"},
    {FrameSettingsField::HighQualityLineRendering, "HighQualityLineRendering"},
    {FrameSettingsField::AsymmetricProjection, "AsymmetricProjection"},
    {FrameSettingsField::ScreenCoordOverride, "ScreenCoordOverride"},
    {FrameSettingsField::MotionVectors, "MotionVectors"},
    {FrameSettingsField::ObjectMotionVectors, "ObjectMotionVectors"},
    {FrameSettingsField::TransparentsWriteMotionVector, "TransparentsWriteMotionVector"},
    {FrameSettingsField::Distortion, "Distortion"},
    {FrameSettingsField::RoughDistortion, "RoughDistortion"},
    {FrameSettingsField::Postprocess, "Postprocess"},
    {FrameSettingsField::CustomPostProcess, "CustomPostProcess"},
    {FrameSettingsField::StopNaN, "StopNaN"},
    {FrameSettingsField::DepthOfField, "DepthOfField"},
    {FrameSettingsField::MotionBlur, "MotionBlur"},
    {FrameSettingsField::PaniniProjection, "PaniniProjection"},
    {FrameSettingsField::Bloom, "Bloom"},
    {FrameSettingsField::LensFlareScreenSpace, "LensFlareScreenSpace"},
    {FrameSettingsField::LensFlareDataDriven, "LensFlareDataDriven"},
    {FrameSettingsField::LensDistortion, "LensDistortion"},
    {FrameSettingsField::ChromaticAberration, "ChromaticAberration"},
    {FrameSettingsField::Vignette, "Vignette"},
    {FrameSettingsField::ColorGrading, "ColorGrading"},
    {FrameSettingsField::Tonemapping, "Tonemapping"},
    {FrameSettingsField::FilmGrain, "FilmGrain"},
    {FrameSettingsField::Dithering, "Dithering"},
    {FrameSettingsField::Antialiasing, "Antialiasing"},
    {FrameSettingsField::AfterPostprocess, "AfterPostprocess"},
    {FrameSettingsField::ZTestAfterPostProcessTAA, "ZTestAfterPostProcessTAA"},
    {FrameSettingsField::LODBiasMode, "LODBiasMode"},
    {FrameSettingsField::LODBias, "LODBias"},
    {FrameSettingsField::LODBiasQualityLevel, "LODBiasQualityLevel"},
    {FrameSettingsField::MaximumLODLevelMode, "MaximumLODLevelMode"},
    {FrameSettingsField::MaximumLODLevel, "MaximumLODLevel"},
    {FrameSettingsField::MaximumLODLevelQualityLevel, "MaximumLODLevelQualityLevel"},
    {FrameSettingsField::MaterialQualityLevel, "MaterialQualityLevel"},
    {FrameSettingsField::ShadowMaps, "ShadowMaps"},
    {FrameSettingsField::ContactShadows, "ContactShadows"},
    {FrameSettingsField::ScreenSpaceShadows, "ScreenSpaceShadows"},
    {FrameSettingsField::Shadowmask, "Shadowmask"},
    {FrameSettingsField::SSR, "SSR"},
    {FrameSettingsField::TransparentSSR, "TransparentSSR"},
    {FrameSettingsField::SSAO, "SSAO"},
    {FrameSettingsField::SSGI, "SSGI"},
    {FrameSettingsField::SubsurfaceScattering, "SubsurfaceScattering"},
    {FrameSettingsField::SssQualityMode, "SssQualityMode"},
    {FrameSettingsField::SssQualityLevel, "SssQualityLevel"},
    {FrameSettingsField::SssCustomSampleBudget, "SssCustomSampleBudget"},
    {FrameSettingsField::SssCustomDownsampleSteps, "SssCustomDownsampleSteps"},
    {FrameSettingsField::VolumetricClouds, "VolumetricClouds"},
    {FrameSettingsField::FullResolutionCloudsForSky, "FullResolutionCloudsForSky"},
    {FrameSettingsField::Transmission, "Transmission"},
    {FrameSettingsField::AtmosphericScattering, "AtmosphericScattering"},
    {FrameSettingsField::Volumetrics, "Volumetrics"},
    {FrameSettingsField::ReprojectionForVolumetrics, "ReprojectionForVolumetrics"},
    {FrameSettingsField::LightLayers, "LightLayers"},
    {FrameSettingsField::ExposureControl, "ExposureControl"},
    {FrameSettingsField::ReflectionProbe, "ReflectionProbe"},
    {FrameSettingsField::PlanarProbe, "PlanarProbe"},
    {FrameSettingsField::ReplaceDiffuseForIndirect, "ReplaceDiffuseForIndirect"},
    {FrameSettingsField::SkyReflection, "SkyReflection"},
    {FrameSettingsField::DirectSpecularLighting, "DirectSpecularLighting"},
    {FrameSettingsField::AdaptiveProbeVolume, "AdaptiveProbeVolume"},
    {FrameSettingsField::NormalizeReflectionProbeWithProbeVolume, "NormalizeReflectionProbeWithProbeVolume"},
    {FrameSettingsField::AsyncCompute, "AsyncCompute"},
    {FrameSettingsField::LightListAsync, "LightListAsync"},
    {FrameSettingsField::SSRAsync, "SSRAsync"},
    {FrameSettingsField::SSAOAsync, "SSAOAsync"},
    {FrameSettingsField::ContactShadowsAsync, "ContactShadowsAsync"},
    {FrameSettingsField::VolumeVoxelizationsAsync, "VolumeVoxelizationsAsync"},
    {FrameSettingsField::HighQualityLinesAsync, "HighQualityLinesAsync"},
    {FrameSettingsField::FPTLForForwardOpaque, "FPTLForForwardOpaque"},
    {FrameSettingsField::BigTilePrepass, "BigTilePrepass"},
    {FrameSettingsField::ComputeLightVariants, "ComputeLightVariants"},
    {FrameSettingsField::ComputeMaterialVariants, "ComputeMaterialVariants"},
};

constexpr FrameSettingsField kCameraDefaults[] = {
    FrameSettingsField::ShadowMaps,
    FrameSettingsField::ContactShadows,
    FrameSettingsField::Shadowmask,
    FrameSettingsField::ScreenSpaceShadows,
    FrameSettingsField::SSR,
    FrameSettingsField::TransparentSSR,
    FrameSettingsField::SSAO,
    FrameSettingsField::SSGI,
    FrameSettingsField::SubsurfaceScattering,
    FrameSettingsField::Transmission,
    FrameSettingsField::AtmosphericScattering,
    FrameSettingsField::Volumetrics,
    FrameSettingsField::ReprojectionForVolumetrics,
    FrameSettingsField::LightLayers,
    FrameSettingsField::ExposureControl,
    FrameSettingsField::LitShaderMode,
    FrameSettingsField::TransparentPrepass,
    FrameSettingsField::TransparentPostpass,
    FrameSettingsField::CustomPass,
    FrameSettingsField::VariableRateShading,
    FrameSettingsField::VirtualTexturing,
    FrameSettingsField::MotionVectors,
    FrameSettingsField::ObjectMotionVectors,
    FrameSettingsField::RenderingLayerMaskBuffer,
    FrameSettingsField::Decals,
    FrameSettingsField::DecalLayers,
    FrameSettingsField::Refraction,
    FrameSettingsField::Distortion,
    FrameSettingsField::RoughDistortion,
    FrameSettingsField::Postprocess,
    FrameSettingsField::CustomPostProcess,
    FrameSettingsField::StopNaN,
    FrameSettingsField::DepthOfField,
    FrameSettingsField::MotionBlur,
    FrameSettingsField::PaniniProjection,
    FrameSettingsField::Bloom,
    FrameSettingsField::LensFlareDataDriven,
    FrameSettingsField::LensDistortion,
    FrameSettingsField::LensFlareScreenSpace,
    FrameSettingsField::ChromaticAberration,
    FrameSettingsField::Vignette,
    FrameSettingsField::ColorGrading,
    FrameSettingsField::Tonemapping,
    FrameSettingsField::FilmGrain,
    FrameSettingsField::Dithering,
    FrameSettingsField::Antialiasing,
    FrameSettingsField::AfterPostprocess,
    FrameSettingsField::LowResTransparent,
    FrameSettingsField::ZTestAfterPostProcessTAA,
    FrameSettingsField::OpaqueObjects,
    FrameSettingsField::TransparentObjects,
    FrameSettingsField::AsyncCompute,
    FrameSettingsField::LightListAsync,
    FrameSettingsField::SSRAsync,
    FrameSettingsField::SSAOAsync,
    FrameSettingsField::ContactShadowsAsync,
    FrameSettingsField::VolumeVoxelizationsAsync,
    FrameSettingsField::HighQualityLineRendering,
    FrameSettingsField::HighQualityLinesAsync,
    FrameSettingsField::ComputeLightVariants,
    FrameSettingsField::ComputeMaterialVariants,
    FrameSettingsField::FPTLForForwardOpaque,
    FrameSettingsField::BigTilePrepass,
    FrameSettingsField::TransparentsWriteMotionVector,
    FrameSettingsField::ReflectionProbe,
    FrameSettingsField::PlanarProbe,
    FrameSettingsField::SkyReflection,
    FrameSettingsField::DirectSpecularLighting,
    FrameSettingsField::RayTracing,
    FrameSettingsField::RaytracingVFX,
    FrameSettingsField::AdaptiveProbeVolume,
    FrameSettingsField::VolumetricClouds,
    FrameSettingsField::Water,
    FrameSettingsField::WaterDecals,
    FrameSettingsField::WaterExclusion,
    FrameSettingsField::ComputeThickness,
};

constexpr FrameSettingsField kCustomOrBakedDefaults[] = {
    FrameSettingsField::ShadowMaps,
    FrameSettingsField::ContactShadows,
    FrameSettingsField::Shadowmask,
    FrameSettingsField::SSR,
    FrameSettingsField::SSAO,
    FrameSettingsField::SSGI,
    FrameSettingsField::SubsurfaceScattering,
    FrameSettingsField::Transmission,
    FrameSettingsField::AtmosphericScattering,
    FrameSettingsField::Volumetrics,
    FrameSettingsField::ReprojectionForVolumetrics,
    FrameSettingsField::LightLayers,
    FrameSettingsField::ExposureControl,
    FrameSettingsField::LitShaderMode,
    FrameSettingsField::TransparentPrepass,
    FrameSettingsField::TransparentPostpass,
    FrameSettingsField::CustomPass,
    FrameSettingsField::VirtualTexturing,
    FrameSettingsField::MotionVectors,
    FrameSettingsField::ObjectMotionVectors,
    FrameSettingsField::Decals,
    FrameSettingsField::DecalLayers,
    FrameSettingsField::Refraction,
    FrameSettingsField::Distortion,
    FrameSettingsField::RoughDistortion,
    FrameSettingsField::Postprocess,
    FrameSettingsField::CustomPostProcess,
    FrameSettingsField::AfterPostprocess,
    FrameSettingsField::OpaqueObjects,
    FrameSettingsField::TransparentObjects,
    FrameSettingsField::AsyncCompute,
    FrameSettingsField::LightListAsync,
    FrameSettingsField::SSRAsync,
    FrameSettingsField::SSAOAsync,
    FrameSettingsField::ContactShadowsAsync,
    FrameSettingsField::VolumeVoxelizationsAsync,
    FrameSettingsField::HighQualityLinesAsync,
    FrameSettingsField::ComputeLightVariants,
    FrameSettingsField::ComputeMaterialVariants,
    FrameSettingsField::FPTLForForwardOpaque,
    FrameSettingsField::BigTilePrepass,
    FrameSettingsField::ReflectionProbe,
    FrameSettingsField::RayTracing,
    FrameSettingsField::RaytracingVFX,
    FrameSettingsField::SkyReflection,
    FrameSettingsField::AdaptiveProbeVolume,
    FrameSettingsField::DirectSpecularLighting,
    FrameSettingsField::VolumetricClouds,
    FrameSettingsField::Water,
    FrameSettingsField::WaterExclusion,
};

constexpr FrameSettingsField kRealtimeReflectionDefaults[] = {
    FrameSettingsField::ShadowMaps,
    FrameSettingsField::ContactShadows,
    FrameSettingsField::Shadowmask,
    FrameSettingsField::SSR,
    FrameSettingsField::SSAO,
    FrameSettingsField::SSGI,
    FrameSettingsField::SubsurfaceScattering,
    FrameSettingsField::Transmission,
    FrameSettingsField::AtmosphericScattering,
    FrameSettingsField::Volumetrics,
    FrameSettingsField::ReprojectionForVolumetrics,
    FrameSettingsField::LightLayers,
    FrameSettingsField::ExposureControl,
    FrameSettingsField::LitShaderMode,
    FrameSettingsField::TransparentPrepass,
    FrameSettingsField::TransparentPostpass,
    FrameSettingsField::CustomPass,
    FrameSettingsField::VirtualTexturing,
    FrameSettingsField::Decals,
    FrameSettingsField::DecalLayers,
    FrameSettingsField::Refraction,
    FrameSettingsField::Distortion,
    FrameSettingsField::RoughDistortion,
    FrameSettingsField::Postprocess,
    FrameSettingsField::CustomPostProcess,
    FrameSettingsField::AfterPostprocess,
    FrameSettingsField::OpaqueObjects,
    FrameSettingsField::TransparentObjects,
    FrameSettingsField::AsyncCompute,
    FrameSettingsField::LightListAsync,
    FrameSettingsField::SSRAsync,
    FrameSettingsField::SSAOAsync,
    FrameSettingsField::ContactShadowsAsync,
    FrameSettingsField::VolumeVoxelizationsAsync,
    FrameSettingsField::HighQualityLineRendering,
    FrameSettingsField::HighQualityLinesAsync,
    FrameSettingsField::ComputeLightVariants,
    FrameSettingsField::ComputeMaterialVariants,
    FrameSettingsField::FPTLForForwardOpaque,
    FrameSettingsField::BigTilePrepass,
    FrameSettingsField::ReplaceDiffuseForIndirect,
    FrameSettingsField::SkyReflection,
    FrameSettingsField::DirectSpecularLighting,
    FrameSettingsField::VolumetricClouds,
    FrameSettingsField::Water,
    FrameSettingsField::WaterDecals,
    FrameSettingsField::WaterExclusion,
    FrameSettingsField::AdaptiveProbeVolume,
};

template <std::size_t N> void enableFields(FrameSettings& settings, const FrameSettingsField (&fields)[N]) {
    for (FrameSettingsField field : fields) {
        settings.setEnabled(field, true);
    }
}

std::string normalize(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (char ch : text) {
        const unsigned char value = static_cast<unsigned char>(ch);
        if (std::isalnum(value) != 0) {
            result.push_back(static_cast<char>(std::tolower(value)));
        }
    }
    return result;
}

bool parseMode(std::string_view text, LODBiasMode& value) {
    const std::string key = normalize(text);
    if (key == "fromqualitysettings") {
        value = LODBiasMode::FromQualitySettings;
    } else if (key == "scalequalitysettings") {
        value = LODBiasMode::ScaleQualitySettings;
    } else if (key == "overridequalitysettings") {
        value = LODBiasMode::OverrideQualitySettings;
    } else {
        return false;
    }
    return true;
}

bool parseMode(std::string_view text, MaximumLODLevelMode& value) {
    const std::string key = normalize(text);
    if (key == "fromqualitysettings") {
        value = MaximumLODLevelMode::FromQualitySettings;
    } else if (key == "offsetqualitysettings") {
        value = MaximumLODLevelMode::OffsetQualitySettings;
    } else if (key == "overridequalitysettings") {
        value = MaximumLODLevelMode::OverrideQualitySettings;
    } else {
        return false;
    }
    return true;
}

bool parseMode(std::string_view text, SssQualityMode& value) {
    const std::string key = normalize(text);
    if (key == "fromqualitysettings") {
        value = SssQualityMode::FromQualitySettings;
    } else if (key == "overridequalitysettings") {
        value = SssQualityMode::OverrideQualitySettings;
    } else {
        return false;
    }
    return true;
}

bool parseMode(std::string_view text, MSAAMode& value) {
    const std::string key = normalize(text);
    if (key == "fromhdrpasset" || key == "frompipeline") {
        value = MSAAMode::FromHDRPAsset;
    } else if (key == "none") {
        value = MSAAMode::None;
    } else if (key == "2x" || key == "msaa2x") {
        value = MSAAMode::MSAA2X;
    } else if (key == "4x" || key == "msaa4x") {
        value = MSAAMode::MSAA4X;
    } else if (key == "8x" || key == "msaa8x") {
        value = MSAAMode::MSAA8X;
    } else {
        return false;
    }
    return true;
}

bool parseMode(std::string_view text, MaterialQuality& value) {
    const std::string key = normalize(text);
    if (key == "low") {
        value = MaterialQuality::Low;
    } else if (key == "medium") {
        value = MaterialQuality::Medium;
    } else if (key == "high") {
        value = MaterialQuality::High;
    } else {
        return false;
    }
    return true;
}

bool readFloat(const Json& document, const char* key, float& value, std::string& error) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return true;
    }
    if (!it->is_number()) {
        error = std::string(key) + " must be a number";
        return false;
    }
    value = it->get<float>();
    return true;
}

bool readInt(const Json& document, const char* key, int& value, std::string& error) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return true;
    }
    if (!it->is_number_integer()) {
        error = std::string(key) + " must be an integer";
        return false;
    }
    value = it->get<int>();
    return true;
}

template <typename Mode> bool readMode(const Json& document, const char* key, Mode& value, std::string& error) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return true;
    }
    if (!it->is_string() || !parseMode(it->get<std::string>(), value)) {
        error = std::string(key) + " has an invalid value";
        return false;
    }
    return true;
}

bool readMSAA(const Json& document, const char* key, MSAAMode& value, std::string& error) {
    const auto it = document.find(key);
    if (it == document.end()) {
        return true;
    }
    if (it->is_number_integer()) {
        switch (it->get<int>()) {
        case 0:
            value = MSAAMode::FromHDRPAsset;
            return true;
        case 1:
            value = MSAAMode::None;
            return true;
        case 2:
            value = MSAAMode::MSAA2X;
            return true;
        case 4:
            value = MSAAMode::MSAA4X;
            return true;
        case 8:
            value = MSAAMode::MSAA8X;
            return true;
        default:
            error = std::string(key) + " has an invalid value";
            return false;
        }
    }
    if (!it->is_string() || !parseMode(it->get<std::string>(), value)) {
        error = std::string(key) + " has an invalid value";
        return false;
    }
    return true;
}

const char* litShaderModeName(LitShaderMode mode) {
    return mode == LitShaderMode::Deferred ? "deferred" : "forward";
}

const char* lodBiasModeName(LODBiasMode mode) {
    switch (mode) {
    case LODBiasMode::FromQualitySettings:
        return "fromQualitySettings";
    case LODBiasMode::ScaleQualitySettings:
        return "scaleQualitySettings";
    case LODBiasMode::OverrideQualitySettings:
        return "overrideQualitySettings";
    }
    return "fromQualitySettings";
}

const char* maximumLODLevelModeName(MaximumLODLevelMode mode) {
    switch (mode) {
    case MaximumLODLevelMode::FromQualitySettings:
        return "fromQualitySettings";
    case MaximumLODLevelMode::OffsetQualitySettings:
        return "offsetQualitySettings";
    case MaximumLODLevelMode::OverrideQualitySettings:
        return "overrideQualitySettings";
    }
    return "fromQualitySettings";
}

const char* sssQualityModeName(SssQualityMode mode) {
    return mode == SssQualityMode::OverrideQualitySettings ? "overrideQualitySettings" : "fromQualitySettings";
}

const char* msaaModeName(MSAAMode mode) {
    switch (mode) {
    case MSAAMode::FromHDRPAsset:
        return "fromHDRPAsset";
    case MSAAMode::None:
        return "none";
    case MSAAMode::MSAA2X:
        return "2x";
    case MSAAMode::MSAA4X:
        return "4x";
    case MSAAMode::MSAA8X:
        return "8x";
    }
    return "none";
}

const char* materialQualityName(MaterialQuality quality) {
    switch (quality) {
    case MaterialQuality::Low:
        return "low";
    case MaterialQuality::Medium:
        return "medium";
    case MaterialQuality::High:
        return "high";
    }
    return "high";
}

} // namespace

std::span<const FrameSettingsFieldInfo> frameSettingsFieldInfos() {
    return std::span<const FrameSettingsFieldInfo>(kFrameSettingsFields);
}

const char* frameSettingsFieldName(FrameSettingsField field) {
    for (const FrameSettingsFieldInfo& info : kFrameSettingsFields) {
        if (info.field == field) {
            return info.name;
        }
    }
    return "None";
}

bool parseFrameSettingsField(std::string_view name, FrameSettingsField& field) {
    for (const FrameSettingsFieldInfo& info : kFrameSettingsFields) {
        if (name == info.name) {
            field = info.field;
            return true;
        }
    }
    return false;
}

bool FrameSettingsOverrideMask::isOverridden(FrameSettingsField field) const {
    return field != FrameSettingsField::None && mask.test(static_cast<std::size_t>(field));
}

void FrameSettingsOverrideMask::setOverridden(FrameSettingsField field, bool value) {
    if (field != FrameSettingsField::None) {
        mask.set(static_cast<std::size_t>(field), value);
    }
}

FrameSettings FrameSettings::defaults(FrameSettingsRenderType type) {
    FrameSettings settings;
    switch (type) {
    case FrameSettingsRenderType::Camera:
        enableFields(settings, kCameraDefaults);
        break;
    case FrameSettingsRenderType::CustomOrBakedReflection:
        enableFields(settings, kCustomOrBakedDefaults);
        break;
    case FrameSettingsRenderType::RealtimeReflection:
        enableFields(settings, kRealtimeReflectionDefaults);
        break;
    }
    settings.lodBias = 1.0f;
    settings.msaaMode = MSAAMode::None;
    settings.materialQuality = MaterialQuality::High;
    return settings;
}

bool FrameSettings::isEnabled(FrameSettingsField field) const {
    return field != FrameSettingsField::None && bitDatas.test(static_cast<std::size_t>(field));
}

void FrameSettings::setEnabled(FrameSettingsField field, bool value) {
    if (field != FrameSettingsField::None) {
        bitDatas.set(static_cast<std::size_t>(field), value);
    }
}

LitShaderMode FrameSettings::litShaderMode() const {
    return isEnabled(FrameSettingsField::LitShaderMode) ? LitShaderMode::Deferred : LitShaderMode::Forward;
}

void FrameSettings::setLitShaderMode(LitShaderMode mode) {
    setEnabled(FrameSettingsField::LitShaderMode, mode == LitShaderMode::Deferred);
}

MSAASamples FrameSettings::resolveMsaaSamples(const PipelineSettings& pipeline) const {
    if (msaaMode == MSAAMode::FromHDRPAsset) {
        return pipeline.msaaSampleCount;
    }
    switch (msaaMode) {
    case MSAAMode::FromHDRPAsset:
        return pipeline.msaaSampleCount;
    case MSAAMode::None:
        return MSAASamples::None;
    case MSAAMode::MSAA2X:
        return MSAASamples::MSAA2X;
    case MSAAMode::MSAA4X:
        return MSAASamples::MSAA4X;
    case MSAAMode::MSAA8X:
        return MSAASamples::MSAA8X;
    }
    return MSAASamples::None;
}

void FrameSettings::overrideWith(const FrameSettings& overriding, const FrameSettingsOverrideMask& mask) {
    bitDatas = (overriding.bitDatas & mask.mask) | (~mask.mask & bitDatas);
    if (mask.isOverridden(FrameSettingsField::LODBias)) {
        lodBias = overriding.lodBias;
    }
    if (mask.isOverridden(FrameSettingsField::LODBiasMode)) {
        lodBiasMode = overriding.lodBiasMode;
    }
    if (mask.isOverridden(FrameSettingsField::LODBiasQualityLevel)) {
        lodBiasQualityLevel = overriding.lodBiasQualityLevel;
    }
    if (mask.isOverridden(FrameSettingsField::MaximumLODLevel)) {
        maximumLODLevel = overriding.maximumLODLevel;
    }
    if (mask.isOverridden(FrameSettingsField::MaximumLODLevelMode)) {
        maximumLODLevelMode = overriding.maximumLODLevelMode;
    }
    if (mask.isOverridden(FrameSettingsField::MaximumLODLevelQualityLevel)) {
        maximumLODLevelQualityLevel = overriding.maximumLODLevelQualityLevel;
    }
    if (mask.isOverridden(FrameSettingsField::SssQualityMode)) {
        sssQualityMode = overriding.sssQualityMode;
    }
    if (mask.isOverridden(FrameSettingsField::SssQualityLevel)) {
        sssQualityLevel = overriding.sssQualityLevel;
    }
    if (mask.isOverridden(FrameSettingsField::SssCustomSampleBudget)) {
        sssCustomSampleBudget = overriding.sssCustomSampleBudget;
    }
    if (mask.isOverridden(FrameSettingsField::SssCustomDownsampleSteps)) {
        sssCustomDownsampleSteps = overriding.sssCustomDownsampleSteps;
    }
    if (mask.isOverridden(FrameSettingsField::MSAAMode)) {
        msaaMode = overriding.msaaMode;
    }
    if (mask.isOverridden(FrameSettingsField::MaterialQualityLevel)) {
        materialQuality = overriding.materialQuality;
    }
}

void FrameSettings::sanitize(const PipelineSettings& pipeline, bool reflection, bool planarReflection, bool preview,
                             bool sceneViewFog) {
    const auto allowed = [this](FrameSettingsField field, bool condition) {
        if (!condition) {
            setEnabled(field, false);
        }
    };

    if (pipeline.supportedLitShaderMode == SupportedLitShaderMode::ForwardOnly) {
        setLitShaderMode(LitShaderMode::Forward);
    } else if (pipeline.supportedLitShaderMode == SupportedLitShaderMode::DeferredOnly) {
        setLitShaderMode(LitShaderMode::Deferred);
    }

    const bool opaqueObjects = isEnabled(FrameSettingsField::OpaqueObjects);
    const bool transparentObjects = isEnabled(FrameSettingsField::TransparentObjects);
    const bool temporalAccumulationAllowed = !reflection || planarReflection;

    if (litShaderMode() != LitShaderMode::Deferred) {
        setEnabled(FrameSettingsField::DepthPrepassWithDeferredRendering, false);
        setEnabled(FrameSettingsField::ClearGBuffers, false);
    }

    allowed(FrameSettingsField::ShadowMaps, pipeline.supportShadows && !preview);
    allowed(FrameSettingsField::Shadowmask, pipeline.supportShadowMask && !preview);
    allowed(FrameSettingsField::ContactShadows, pipeline.supportContactShadows && !preview);

    if (litShaderMode() != LitShaderMode::Forward || pipeline.supportRayTracing || pipeline.supportWater) {
        msaaMode = MSAAMode::None;
    }
    const bool msaa = msaaMode == MSAAMode::FromHDRPAsset ? pipeline.msaaSampleCount != MSAASamples::None
                                                          : msaaMode != MSAAMode::None;

    allowed(FrameSettingsField::ScreenSpaceShadows, pipeline.supportScreenSpaceShadows && opaqueObjects && !msaa);
    const bool rayTracing = isEnabled(FrameSettingsField::RayTracing) && pipeline.supportRayTracing && !preview &&
                            temporalAccumulationAllowed;
    allowed(FrameSettingsField::RayTracing, rayTracing);
    allowed(FrameSettingsField::RaytracingVFX, rayTracing);

    const bool ssr =
        isEnabled(FrameSettingsField::SSR) && pipeline.supportSSR && !msaa && !preview && temporalAccumulationAllowed;
    allowed(FrameSettingsField::SSR, ssr);
    allowed(FrameSettingsField::TransparentSSR,
            ssr && pipeline.supportSSRTransparent && transparentObjects && pipeline.supportTransparentDepthPrepass);
    allowed(FrameSettingsField::Refraction, transparentObjects && !preview);
    allowed(FrameSettingsField::SSAO, pipeline.supportSSAO && !preview && opaqueObjects && temporalAccumulationAllowed);
    allowed(FrameSettingsField::SSGI, pipeline.supportSSGI && !preview && opaqueObjects && temporalAccumulationAllowed);
    allowed(FrameSettingsField::SubsurfaceScattering, pipeline.supportSubsurfaceScattering);
    allowed(FrameSettingsField::VolumetricClouds, pipeline.supportVolumetricClouds && !preview);
    allowed(FrameSettingsField::FullResolutionCloudsForSky, isEnabled(FrameSettingsField::VolumetricClouds));

    const bool water = isEnabled(FrameSettingsField::Water) && isEnabled(FrameSettingsField::Refraction) &&
                       pipeline.supportWater && !preview;
    allowed(FrameSettingsField::Water, water);
    allowed(FrameSettingsField::WaterDecals, water && pipeline.supportWaterDecals);
    allowed(FrameSettingsField::WaterExclusion, water && pipeline.supportWaterExclusion);

    allowed(FrameSettingsField::LensFlareScreenSpace,
            pipeline.supportScreenSpaceLensFlare && isEnabled(FrameSettingsField::Bloom));
    allowed(FrameSettingsField::LensFlareDataDriven, pipeline.supportDataDrivenLensFlare);

    allowed(FrameSettingsField::AtmosphericScattering, sceneViewFog && !preview);
    allowed(FrameSettingsField::Volumetrics,
            pipeline.supportVolumetrics && isEnabled(FrameSettingsField::AtmosphericScattering));
    allowed(FrameSettingsField::ReprojectionForVolumetrics, !preview && temporalAccumulationAllowed);
    allowed(FrameSettingsField::RenderingLayerMaskBuffer, pipeline.renderingLayerMaskBuffer && !preview);
    allowed(FrameSettingsField::LightLayers, pipeline.supportLightLayers && !preview);
    allowed(FrameSettingsField::ExposureControl, (!reflection || planarReflection) && !preview);
    allowed(FrameSettingsField::Postprocess, !reflection && !preview);
    const bool postprocess = isEnabled(FrameSettingsField::Postprocess);
    allowed(FrameSettingsField::StopNaN, postprocess);
    allowed(FrameSettingsField::DepthOfField, postprocess && pipeline.supportDepthOfField);
    allowed(FrameSettingsField::MotionBlur,
            postprocess && pipeline.supportMotionBlur && isEnabled(FrameSettingsField::MotionVectors) &&
                pipeline.supportMotionVectors && !preview);
    allowed(FrameSettingsField::Bloom, postprocess && pipeline.supportBloom);
    allowed(FrameSettingsField::ColorGrading, postprocess && pipeline.supportColorGrading);
    allowed(FrameSettingsField::Antialiasing, postprocess && (pipeline.supportTAA || pipeline.supportFXAA));
    allowed(FrameSettingsField::TransparentPrepass,
            pipeline.supportTransparentDepthPrepass && !preview && transparentObjects);
    const bool motionVectors =
        isEnabled(FrameSettingsField::MotionVectors) && pipeline.supportMotionVectors && !preview;
    allowed(FrameSettingsField::MotionVectors, motionVectors);
    allowed(FrameSettingsField::ObjectMotionVectors, motionVectors && !preview);
    allowed(FrameSettingsField::TransparentsWriteMotionVector, motionVectors && !preview);
    allowed(FrameSettingsField::Decals, pipeline.supportDecals && !preview);
    allowed(FrameSettingsField::DecalLayers, pipeline.supportDecalLayers && isEnabled(FrameSettingsField::Decals));
    allowed(FrameSettingsField::TransparentPostpass,
            pipeline.supportTransparentDepthPostpass && !preview && transparentObjects);
    const bool distortion = isEnabled(FrameSettingsField::Distortion) && pipeline.supportDistortion && !preview;
    allowed(FrameSettingsField::Distortion, distortion);
    allowed(FrameSettingsField::RoughDistortion, distortion && !preview);
    allowed(FrameSettingsField::LowResTransparent, pipeline.supportLowResTransparent && transparentObjects);

    allowed(FrameSettingsField::AsyncCompute, pipeline.supportAsyncCompute);
    const bool async = isEnabled(FrameSettingsField::AsyncCompute);
    allowed(FrameSettingsField::LightListAsync, async);
    allowed(FrameSettingsField::SSRAsync, async);
    allowed(FrameSettingsField::SSAOAsync, async);
    allowed(FrameSettingsField::ContactShadowsAsync, async && !rayTracing);
    allowed(FrameSettingsField::VolumeVoxelizationsAsync, async);
    allowed(FrameSettingsField::HighQualityLinesAsync, async);

    allowed(FrameSettingsField::CustomPass, pipeline.supportCustomPass && !preview);
    allowed(FrameSettingsField::VariableRateShading,
            pipeline.supportCustomPass && pipeline.supportVariableRateShading && !preview);
    allowed(FrameSettingsField::CustomPostProcess, !preview);
    allowed(FrameSettingsField::FPTLForForwardOpaque, !msaa);
    allowed(FrameSettingsField::AdaptiveProbeVolume, pipeline.supportProbeVolume && !preview);
    allowed(FrameSettingsField::NormalizeReflectionProbeWithProbeVolume, pipeline.supportProbeVolume);
    allowed(FrameSettingsField::ReflectionProbe, !preview);
    allowed(FrameSettingsField::PlanarProbe, !preview);
    allowed(FrameSettingsField::SubsurfaceScattering, opaqueObjects);
    allowed(FrameSettingsField::VirtualTexturing, pipeline.supportVirtualTexturing);
    allowed(FrameSettingsField::HighQualityLineRendering, pipeline.supportHighQualityLineRendering);
    allowed(FrameSettingsField::ComputeThickness, pipeline.supportComputeThickness);
}

bool operator==(const FrameSettings& lhs, const FrameSettings& rhs) {
    return lhs.bitDatas == rhs.bitDatas && lhs.lodBias == rhs.lodBias && lhs.lodBiasMode == rhs.lodBiasMode &&
           lhs.lodBiasQualityLevel == rhs.lodBiasQualityLevel && lhs.maximumLODLevel == rhs.maximumLODLevel &&
           lhs.maximumLODLevelMode == rhs.maximumLODLevelMode &&
           lhs.maximumLODLevelQualityLevel == rhs.maximumLODLevelQualityLevel &&
           lhs.sssQualityMode == rhs.sssQualityMode && lhs.sssQualityLevel == rhs.sssQualityLevel &&
           lhs.sssCustomSampleBudget == rhs.sssCustomSampleBudget &&
           lhs.sssCustomDownsampleSteps == rhs.sssCustomDownsampleSteps && lhs.msaaMode == rhs.msaaMode &&
           lhs.materialQuality == rhs.materialQuality;
}

bool parseFrameSettings(std::string_view text, FrameSettings& settings, std::string& error) {
    settings = FrameSettings::defaults(FrameSettingsRenderType::Camera);
    error.clear();

    Json document;
    try {
        document = Json::parse(text.begin(), text.end());
    } catch (const Json::exception& exception) {
        error = exception.what();
        return false;
    }
    if (!document.is_object()) {
        error = "frame settings must be a JSON object";
        return false;
    }

    const auto fieldsIt = document.find("fields");
    if (fieldsIt != document.end()) {
        if (!fieldsIt->is_object()) {
            error = "fields must be an object";
            return false;
        }
        for (auto it = fieldsIt->begin(); it != fieldsIt->end(); ++it) {
            FrameSettingsField field = FrameSettingsField::None;
            if (!parseFrameSettingsField(it.key(), field)) {
                error = "unknown frame setting field '" + it.key() + "'";
                return false;
            }
            if (!it->is_boolean()) {
                error = "frame setting field '" + it.key() + "' must be a boolean";
                return false;
            }
            settings.setEnabled(field, it->get<bool>());
        }
    }

    for (auto it = document.begin(); it != document.end(); ++it) {
        const std::string& key = it.key();
        if (key == "fields") {
            continue;
        }
        if (key == "litShaderMode") {
            if (!it->is_string()) {
                error = "litShaderMode must be a string";
                return false;
            }
            const std::string mode = normalize(it->get<std::string>());
            if (mode == "forward") {
                settings.setLitShaderMode(LitShaderMode::Forward);
            } else if (mode == "deferred") {
                settings.setLitShaderMode(LitShaderMode::Deferred);
            } else {
                error = "litShaderMode must be forward or deferred";
                return false;
            }
        } else if (key == "lodBias") {
            if (!readFloat(document, key.c_str(), settings.lodBias, error)) {
                return false;
            }
        } else if (key == "lodBiasMode") {
            if (!readMode(document, key.c_str(), settings.lodBiasMode, error)) {
                return false;
            }
        } else if (key == "lodBiasQualityLevel") {
            if (!readInt(document, key.c_str(), settings.lodBiasQualityLevel, error)) {
                return false;
            }
        } else if (key == "maximumLODLevel") {
            if (!readInt(document, key.c_str(), settings.maximumLODLevel, error)) {
                return false;
            }
        } else if (key == "maximumLODLevelMode") {
            if (!readMode(document, key.c_str(), settings.maximumLODLevelMode, error)) {
                return false;
            }
        } else if (key == "maximumLODLevelQualityLevel") {
            if (!readInt(document, key.c_str(), settings.maximumLODLevelQualityLevel, error)) {
                return false;
            }
        } else if (key == "sssQualityMode") {
            if (!readMode(document, key.c_str(), settings.sssQualityMode, error)) {
                return false;
            }
        } else if (key == "sssQualityLevel") {
            if (!readInt(document, key.c_str(), settings.sssQualityLevel, error)) {
                return false;
            }
        } else if (key == "sssCustomSampleBudget") {
            if (!readInt(document, key.c_str(), settings.sssCustomSampleBudget, error)) {
                return false;
            }
        } else if (key == "sssCustomDownsampleSteps") {
            if (!readInt(document, key.c_str(), settings.sssCustomDownsampleSteps, error)) {
                return false;
            }
        } else if (key == "msaaMode") {
            if (!readMSAA(document, key.c_str(), settings.msaaMode, error)) {
                return false;
            }
        } else if (key == "materialQuality") {
            if (!it->is_string() || !parseMode(it->get<std::string>(), settings.materialQuality)) {
                error = "materialQuality must be low, medium or high";
                return false;
            }
        } else {
            error = "unknown frame setting '" + key + "'";
            return false;
        }
    }
    return true;
}

bool loadFrameSettings(const std::string& path, FrameSettings& settings, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "failed to open '" + path + "'";
        return false;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        error = "failed to read '" + path + "'";
        return false;
    }
    if (!parseFrameSettings(contents.str(), settings, error)) {
        error = path + ": " + error;
        return false;
    }
    return true;
}

std::string frameSettingsToJson(const FrameSettings& settings) {
    Json fields = Json::object();
    for (const FrameSettingsFieldInfo& info : kFrameSettingsFields) {
        fields[info.name] = settings.isEnabled(info.field);
    }

    Json document = Json::object();
    document["fields"] = fields;
    document["litShaderMode"] = litShaderModeName(settings.litShaderMode());
    document["lodBias"] = settings.lodBias;
    document["lodBiasMode"] = lodBiasModeName(settings.lodBiasMode);
    document["lodBiasQualityLevel"] = settings.lodBiasQualityLevel;
    document["maximumLODLevel"] = settings.maximumLODLevel;
    document["maximumLODLevelMode"] = maximumLODLevelModeName(settings.maximumLODLevelMode);
    document["maximumLODLevelQualityLevel"] = settings.maximumLODLevelQualityLevel;
    document["sssQualityMode"] = sssQualityModeName(settings.sssQualityMode);
    document["sssQualityLevel"] = settings.sssQualityLevel;
    document["sssCustomSampleBudget"] = settings.sssCustomSampleBudget;
    document["sssCustomDownsampleSteps"] = settings.sssCustomDownsampleSteps;
    document["msaaMode"] = msaaModeName(settings.msaaMode);
    document["materialQuality"] = materialQualityName(settings.materialQuality);
    return document.dump(2);
}

} // namespace hbg::render
