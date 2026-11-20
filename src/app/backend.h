#pragma once

#include "render/atmosphere/volumetric_fog_settings.h"
#include "render/debug/debug_display.h"
#include "render/pipeline/frame_settings.h"
#include "render/postprocess/post_process_settings.h"
#include "render/shadows/shadow_settings.h"

#include <bgfx/bgfx.h>

#include <optional>
#include <string>
#include <string_view>

namespace hbg {

std::optional<bgfx::RendererType::Enum> parseBackend(std::string_view name);
std::string_view backendName(bgfx::RendererType::Enum type);

struct VolumetricFogOverrides {
    std::optional<bool> enabled;
    std::optional<float> meanFreePath;
    std::optional<float> baseHeight;
    std::optional<float> maximumHeight;
    std::optional<float> anisotropy;
    std::optional<float> depthExtent;
    std::optional<uint32_t> stepCount;
    std::optional<bool> directionalShadows;
    std::optional<float> resolutionScale;
    std::optional<bool> temporal;
    std::optional<float> temporalHistoryWeight;
    std::optional<bool> temporalJitter;
    std::optional<bool> punctualLights;
    std::optional<bool> punctualShadows;
    std::optional<uint32_t> maxPunctualLights;
    std::optional<bool> localVolumes;
};

struct ShadowOverrides {
    std::optional<render::ShadowFilter> directionalFilter;
    std::optional<render::ShadowFilter> punctualFilter;
    std::optional<float> maxDistance;
    std::optional<uint32_t> cascadeCount;
    std::optional<uint32_t> atlasSize;
    std::optional<uint32_t> maxRequests;
    std::optional<uint32_t> directionalResolution;
    std::optional<uint32_t> pointResolution;
    std::optional<uint32_t> spotResolution;
    std::optional<float> normalBias;
    std::optional<float> slopeBias;
    std::optional<float> shadowNearPlane;
    std::optional<float> directionalLightAngle;
    std::optional<float> directionalMaxDepthBias;
    std::optional<uint32_t> imsKernelSize;
    std::optional<float> softness;
    std::optional<uint32_t> blockerSamples;
    std::optional<uint32_t> filterSamples;
    std::optional<float> minFilterSize;
    std::optional<float> cascadeFade;
    std::optional<bool> microShadows;
    std::optional<float> microShadowOpacity;
    std::optional<bool> contactShadows;
    std::optional<float> contactShadowLength;
    std::optional<float> contactShadowOpacity;
    std::optional<uint32_t> contactShadowSamples;
    std::optional<bool> contactShadowHalfResolution;
    std::optional<bool> contactShadowDirectionalOnly;
    std::optional<float> shadowDimmer;
    std::optional<float> shadowFadeDistance;
    std::optional<Vec3> shadowTint;
    std::optional<bool> cachedDirectionalShadows;
    std::optional<bool> cachedPunctualShadows;
    std::optional<bool> cachedAreaShadows;
};

struct PostProcessOverrides {
    std::optional<render::ExposureMode> exposureMode;
    std::optional<float> fixedEv100;
    std::optional<float> compensation;
    std::optional<float> limitMin;
    std::optional<float> limitMax;
    std::optional<float> debugExposure;
    std::optional<render::TonemappingMode> tonemapping;
    std::optional<bool> bloom;
    std::optional<float> bloomThreshold;
    std::optional<float> bloomIntensity;
    std::optional<float> bloomScatter;
    std::optional<uint32_t> bloomResolution;
    std::optional<bool> bloomHighQualityPrefiltering;
    std::optional<bool> bloomHighQualityFiltering;
    std::optional<bool> colorGrading;
    std::optional<bool> depthOfField;
    std::optional<float> dofFocusDistance;
    std::optional<float> dofNearFocusStart;
    std::optional<float> dofNearFocusEnd;
    std::optional<float> dofFarFocusStart;
    std::optional<float> dofFarFocusEnd;
    std::optional<float> dofNearMaxBlur;
    std::optional<float> dofFarMaxBlur;
    std::optional<uint32_t> dofResolution;
    std::optional<bool> dofHighQuality;
    std::optional<bool> motionBlur;
    std::optional<float> motionBlurIntensity;
    std::optional<float> motionBlurMaxVelocity;
    std::optional<float> motionBlurMinVelocity;
    std::optional<bool> motionBlurCamera;
    std::optional<uint32_t> motionBlurSamples;
    std::optional<float> motionBlurDepthExtent;
    std::optional<bool> taa;
    std::optional<bool> fxaa;
    std::optional<uint32_t> taaQuality;
    std::optional<float> taaMotionRejection;
    std::optional<float> taaSharpening;
    std::optional<float> taaJitterScale;
};

struct AppOptions {
    bgfx::RendererType::Enum backend = bgfx::RendererType::Count;
    int width = 1280;
    int height = 720;
    bool vsync = true;
    bool verbose = false;
    bool bgfxDebug = false;
    bool gfxTest = false;
    bool vfxSpike = false;
    bool clusterStatsReadback = false;
    bool debugLightData = false;
    bool debugLightmapMask = false;
    bool debugWireframe = false;
    render::DebugDisplayMode debugDisplayMode = render::DebugDisplayMode::None;
    int debugLightClusterSlice = -1;
    bool disableLightCluster = false;
    bool disableShadows = false;
    bool debugUi = true;
    bool cameraOrbit = false;
    bool framesSpecified = false;
    std::optional<render::LitShaderMode> litShaderMode;
    std::optional<bool> litVariants;
    std::optional<bool> litVariantTiles;
    std::optional<bool> gpuLightList;
    std::optional<bool> gpuLightListBuffers;
    std::optional<bool> gpuLightListIndirect;
    std::optional<bool> lightListFeatureFlags;
    std::optional<bool> cookieAtlas;
    std::optional<uint32_t> cookieAtlasSize;
    std::optional<uint32_t> lightListCapacity;
    std::optional<bool> lightLayers;
    std::optional<bool> gpuShadowCulling;
    std::optional<bool> gpuShadowIndirect;
    std::optional<bool> asyncGpuLightList;
    std::optional<bool> ssao;
    std::optional<bool> ssaoTemporal;
    std::optional<bool> ssr;
    std::optional<bool> ssrTemporal;
    std::optional<bool> ssgi;
    std::optional<bool> ssgiTemporal;
    std::optional<bool> decals;
    std::optional<bool> reflectionProbes;
    std::optional<bool> vfx;
    bool vfxTest = false;
    bool vfxReference = false;
    std::string vfxReferencePath;
    std::string vfxReferenceGraphPath;
    float renderScale = 1.0f;
    std::string assetTestPath;
    std::string scenePath;
    std::string renderTestPath;
    std::string screenshotPath;
    std::string pipelineSettingsPath;
    std::string frameSettingsPath;
    std::string skyPath;
    float skyIntensity = 1.0f;
    float skyRotation = 0.0f;
    render::PostProcessSettings postProcess;
    PostProcessOverrides postProcessOverrides;
    render::VolumetricFogSettings volumetricFog;
    VolumetricFogOverrides volumetricFogOverrides;
    render::ShadowSettings shadowSettings;
    ShadowOverrides shadowOverrides;
    std::string shadowSettingsPath;
    std::string postProcessProfilePath;
    std::string volumetricFogProfilePath;
    int frames = 3;
};

struct CommandLineResult {
    bool ok = false;
    bool help = false;
    AppOptions options;
};

void applyPostProcessOverrides(const PostProcessOverrides& overrides, render::PostProcessSettings& settings);
void applyVolumetricFogOverrides(const VolumetricFogOverrides& overrides,
                                 render::VolumetricFogSettings& settings);
void applyShadowOverrides(const ShadowOverrides& overrides, render::ShadowSettings& settings);
CommandLineResult parseCommandLine(int argc, const char* const* argv);
const char* usage();

} // namespace hbg
