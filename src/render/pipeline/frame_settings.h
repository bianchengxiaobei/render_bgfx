#pragma once

#include "core/bitset128.h"
#include "render/pipeline/pipeline_settings.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace hbg::render {

enum class LitShaderMode : uint8_t {
    Forward,
    Deferred,
};

enum class LODBiasMode : uint8_t {
    FromQualitySettings,
    ScaleQualitySettings,
    OverrideQualitySettings,
};

enum class MaximumLODLevelMode : uint8_t {
    FromQualitySettings,
    OffsetQualitySettings,
    OverrideQualitySettings,
};

enum class SssQualityMode : uint8_t {
    FromQualitySettings,
    OverrideQualitySettings,
};

enum class MSAAMode : uint8_t {
    FromHDRPAsset = 0,
    None = 1,
    MSAA2X = 2,
    MSAA4X = 4,
    MSAA8X = 8,
};

enum class MaterialQuality : uint8_t {
    Low = 1 << 0,
    Medium = 1 << 1,
    High = 1 << 2,
};

enum class FrameSettingsRenderType : uint8_t {
    Camera,
    CustomOrBakedReflection,
    RealtimeReflection,
};

enum class FrameSettingsField : int16_t {
    None = -1,
    LitShaderMode = 0,
    DepthPrepassWithDeferredRendering = 1,
    OpaqueObjects = 2,
    TransparentObjects = 3,
    MSAAMode = 4,
    ClearGBuffers = 5,
    CustomPass = 6,
    VariableRateShading = 7,
    TransparentPrepass = 8,
    TransparentPostpass = 9,
    MotionVectors = 10,
    ObjectMotionVectors = 11,
    Decals = 12,
    Refraction = 13,
    Distortion = 14,
    Postprocess = 15,
    TransparentsWriteMotionVector = 16,
    AfterPostprocess = 17,
    LowResTransparent = 18,
    ZTestAfterPostProcessTAA = 19,
    ShadowMaps = 20,
    ContactShadows = 21,
    Shadowmask = 22,
    SSR = 23,
    SSAO = 24,
    Transmission = 26,
    AtmosphericScattering = 27,
    Volumetrics = 28,
    ReprojectionForVolumetrics = 29,
    LightLayers = 30,
    ExposureControl = 32,
    ReflectionProbe = 33,
    ScreenSpaceShadows = 34,
    PlanarProbe = 35,
    ReplaceDiffuseForIndirect = 36,
    SkyReflection = 37,
    DirectSpecularLighting = 38,
    CustomPostProcess = 39,
    AsyncCompute = 40,
    LightListAsync = 41,
    SSRAsync = 42,
    SSAOAsync = 43,
    ContactShadowsAsync = 44,
    VolumeVoxelizationsAsync = 45,
    SubsurfaceScattering = 46,
    SssQualityMode = 47,
    SssQualityLevel = 48,
    SssCustomSampleBudget = 49,
    RenderingLayerMaskBuffer = 50,
    SssCustomDownsampleSteps = 51,
    HighQualityLinesAsync = 52,
    AlphaToMask = 56,
    LODBiasMode = 60,
    LODBias = 61,
    MaximumLODLevelMode = 62,
    MaximumLODLevel = 63,
    LODBiasQualityLevel = 64,
    MaximumLODLevelQualityLevel = 65,
    MaterialQualityLevel = 66,
    RoughDistortion = 67,
    VirtualTexturing = 68,
    ScreenCoordOverride = 77,
    AsymmetricProjection = 78,
    VolumetricClouds = 79,
    StopNaN = 80,
    DepthOfField = 81,
    MotionBlur = 82,
    PaniniProjection = 83,
    Bloom = 84,
    LensDistortion = 85,
    ChromaticAberration = 86,
    Vignette = 87,
    ColorGrading = 88,
    FilmGrain = 89,
    Dithering = 90,
    Antialiasing = 91,
    RayTracing = 92,
    Tonemapping = 93,
    TransparentSSR = 94,
    SSGI = 95,
    DecalLayers = 96,
    LensFlareDataDriven = 97,
    FullResolutionCloudsForSky = 98,
    Water = 99,
    RaytracingVFX = 100,
    WaterExclusion = 101,
    WaterDecals = 102,
    HighQualityLineRendering = 103,
    LensFlareScreenSpace = 104,
    ComputeThickness = 119,
    FPTLForForwardOpaque = 120,
    BigTilePrepass = 121,
    ComputeLightVariants = 124,
    ComputeMaterialVariants = 125,
    NormalizeReflectionProbeWithProbeVolume = 126,
    AdaptiveProbeVolume = 127,
};

struct FrameSettingsFieldInfo {
    FrameSettingsField field;
    const char* name;
};

std::span<const FrameSettingsFieldInfo> frameSettingsFieldInfos();
const char* frameSettingsFieldName(FrameSettingsField field);
bool parseFrameSettingsField(std::string_view name, FrameSettingsField& field);

struct FrameSettingsOverrideMask {
    BitSet128 mask;

    bool isOverridden(FrameSettingsField field) const;
    void setOverridden(FrameSettingsField field, bool value = true);
};

struct FrameSettings {
    BitSet128 bitDatas;

    float lodBias = 1.0f;
    LODBiasMode lodBiasMode = LODBiasMode::FromQualitySettings;
    int lodBiasQualityLevel = 0;
    int maximumLODLevel = 0;
    MaximumLODLevelMode maximumLODLevelMode = MaximumLODLevelMode::FromQualitySettings;
    int maximumLODLevelQualityLevel = 0;
    SssQualityMode sssQualityMode = SssQualityMode::FromQualitySettings;
    int sssQualityLevel = 0;
    int sssCustomSampleBudget = 0;
    int sssCustomDownsampleSteps = 0;
    MSAAMode msaaMode = MSAAMode::None;
    MaterialQuality materialQuality = MaterialQuality::High;

    static FrameSettings defaults(FrameSettingsRenderType type);

    bool isEnabled(FrameSettingsField field) const;
    void setEnabled(FrameSettingsField field, bool value = true);

    LitShaderMode litShaderMode() const;
    void setLitShaderMode(LitShaderMode mode);
    MSAASamples resolveMsaaSamples(const PipelineSettings& pipeline) const;

    void overrideWith(const FrameSettings& overriding, const FrameSettingsOverrideMask& mask);
    void sanitize(const PipelineSettings& pipeline, bool reflection = false, bool planarReflection = false,
                  bool preview = false, bool sceneViewFog = true);

    friend bool operator==(const FrameSettings& lhs, const FrameSettings& rhs);
    friend bool operator!=(const FrameSettings& lhs, const FrameSettings& rhs) { return !(lhs == rhs); }
};

bool parseFrameSettings(std::string_view json, FrameSettings& settings, std::string& error);
bool loadFrameSettings(const std::string& path, FrameSettings& settings, std::string& error);
std::string frameSettingsToJson(const FrameSettings& settings);

} // namespace hbg::render
