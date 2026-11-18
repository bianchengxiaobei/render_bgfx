#include "app/backend.h"

#include "core/log.h"
#include "render/lighting/gpu_light_data.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>

namespace hbg {
namespace {

std::string toLower(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return result;
}

std::optional<std::string_view> optionValue(std::string_view arg, std::string_view name) {
    if (arg.size() > name.size() && arg.substr(0, name.size()) == name && arg[name.size()] == '=') {
        return arg.substr(name.size() + 1);
    }
    return std::nullopt;
}

bool parseDimension(std::string_view text, int& out) {
    int value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end || value <= 0 || value > 16384) {
        return false;
    }
    out = value;
    return true;
}

bool parseInt(std::string_view text, int& out) {
    int value = 0;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end) {
        return false;
    }
    out = value;
    return true;
}

bool parseFloat(std::string_view text, float& out) {
    if (text.empty()) {
        return false;
    }
    const std::string value(text);
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || end != value.c_str() + value.size() || !std::isfinite(parsed)) {
        return false;
    }
    out = parsed;
    return true;
}

bool parseVec3(std::string_view text, Vec3& out) {
    std::array<std::string_view, 3> parts{};
    std::size_t index = 0;
    std::size_t start = 0;
    while (start <= text.size() && index < parts.size()) {
        const std::size_t comma = text.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        parts[index++] = text.substr(start, end - start);
        if (comma == std::string_view::npos) {
            break;
        }
        start = comma + 1;
    }
    if (index != 3) {
        return false;
    }
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    if (!parseFloat(parts[0], x) || !parseFloat(parts[1], y) || !parseFloat(parts[2], z)) {
        return false;
    }
    out = Vec3(x, y, z);
    return true;
}

bool parseRenderScale(std::string_view text, float& out) {
    if (text.empty()) {
        return false;
    }
    const std::string value(text);
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    if (end == value.c_str() || end != value.c_str() + value.size() || !std::isfinite(parsed) || parsed < 0.1f ||
        parsed > 1.0f) {
        return false;
    }
    out = parsed;
    return true;
}

std::optional<render::ExposureMode> parseExposureMode(std::string_view text) {
    const std::string key = toLower(text);
    if (key == "fixed") {
        return render::ExposureMode::Fixed;
    }
    if (key == "auto" || key == "automatic") {
        return render::ExposureMode::Automatic;
    }
    if (key == "histogram" || key == "auto-histogram" || key == "automatichistogram") {
        return render::ExposureMode::Histogram;
    }
    return std::nullopt;
}

std::optional<render::TonemappingMode> parseTonemappingMode(std::string_view text) {
    const std::string key = toLower(text);
    if (key == "none" || key == "off") {
        return render::TonemappingMode::None;
    }
    if (key == "neutral") {
        return render::TonemappingMode::Neutral;
    }
    if (key == "aces") {
        return render::TonemappingMode::ACES;
    }
    return std::nullopt;
}

bool parseAntialiasing(std::string_view text, bool& taa, bool& fxaa) {
    const std::string key = toLower(text);
    if (key == "off" || key == "none") {
        taa = false;
        fxaa = false;
        return true;
    }
    if (key == "taa") {
        taa = true;
        fxaa = false;
        return true;
    }
    if (key == "fxaa") {
        taa = false;
        fxaa = true;
        return true;
    }
    return false;
}

std::optional<render::LitShaderMode> parseLitShaderMode(std::string_view text) {
    const std::string key = toLower(text);
    if (key == "forward") {
        return render::LitShaderMode::Forward;
    }
    if (key == "deferred") {
        return render::LitShaderMode::Deferred;
    }
    return std::nullopt;
}

bool parseShadowQuality(std::string_view text, render::ShadowFilter& directional, render::ShadowFilter& punctual) {
    const std::string key = toLower(text);
    if (key == "off" || key == "none") {
        directional = render::ShadowFilter::Off;
        punctual = render::ShadowFilter::Off;
        return true;
    }
    if (key == "low") {
        directional = render::ShadowFilter::Pcf5x5;
        punctual = render::ShadowFilter::Pcf3x3;
        return true;
    }
    if (key == "medium") {
        directional = render::ShadowFilter::Pcf7x7;
        punctual = render::ShadowFilter::Pcf5x5;
        return true;
    }
    if (key == "high" || key == "pcss") {
        directional = render::ShadowFilter::Pcss;
        punctual = render::ShadowFilter::Pcss;
        return true;
    }
    return false;
}

std::optional<render::DebugDisplayMode> parseDebugDisplayMode(std::string_view text) {
    const std::string key = toLower(text);
    if (key == "off" || key == "none") {
        return render::DebugDisplayMode::None;
    }
    if (key == "light-data" || key == "lightdata") {
        return render::DebugDisplayMode::LightData;
    }
    if (key == "normal" || key == "normal-buffer") {
        return render::DebugDisplayMode::Normal;
    }
    if (key == "motion-vectors" || key == "motion" || key == "motionvectors") {
        return render::DebugDisplayMode::MotionVectors;
    }
    if (key == "depth") {
        return render::DebugDisplayMode::Depth;
    }
    if (key == "depth-pyramid" || key == "depthpyramid") {
        return render::DebugDisplayMode::DepthPyramid;
    }
    if (key == "light-cluster" || key == "lightcluster" || key == "cluster" || key == "cluster-tiles") {
        return render::DebugDisplayMode::LightCluster;
    }
    if (key == "directional-shadow-atlas" || key == "shadow-atlas-directional" || key == "directional-shadows") {
        return render::DebugDisplayMode::ShadowAtlasDirectional;
    }
    if (key == "punctual-shadow-atlas" || key == "shadow-atlas-punctual" || key == "punctual-shadows") {
        return render::DebugDisplayMode::ShadowAtlasPunctual;
    }
    if (key == "shadow-cascade" || key == "cascade-debug") {
        return render::DebugDisplayMode::ShadowCascade;
    }
    if (key == "shadow-filter" || key == "shadow-filter-debug") {
        return render::DebugDisplayMode::ShadowFilter;
    }
    if (key == "albedo") {
        return render::DebugDisplayMode::Albedo;
    }
    if (key == "roughness") {
        return render::DebugDisplayMode::Roughness;
    }
    if (key == "metallic" || key == "metal") {
        return render::DebugDisplayMode::Metallic;
    }
    if (key == "ao" || key == "gbuffer-ao" || key == "material-ao") {
        return render::DebugDisplayMode::AmbientOcclusion;
    }
    if (key == "emissive" || key == "emission") {
        return render::DebugDisplayMode::Emissive;
    }
    if (key == "ssao" || key == "ambient-occlusion" || key == "ambientocclusion" ||
        key == "ssao-denoised") {
        return render::DebugDisplayMode::SSAO;
    }
    if (key == "ssao-raw" || key == "ambient-occlusion-raw" || key == "ambientocclusionraw") {
        return render::DebugDisplayMode::SSAORaw;
    }
    if (key == "ssao-history" || key == "ambient-occlusion-history" || key == "ambientocclusionhistory") {
        return render::DebugDisplayMode::SSAOHistory;
    }
    if (key == "ssr" || key == "reflections" || key == "screen-space-reflections" ||
        key == "ssr-denoised") {
        return render::DebugDisplayMode::SSR;
    }
    if (key == "ssr-raw" || key == "reflections-raw" || key == "screen-space-reflections-raw") {
        return render::DebugDisplayMode::SSRRaw;
    }
    if (key == "ssr-history" || key == "reflections-history" || key == "screen-space-reflections-history") {
        return render::DebugDisplayMode::SSRHistory;
    }
    if (key == "ssgi" || key == "global-illumination" || key == "screen-space-global-illumination" ||
        key == "ssgi-denoised") {
        return render::DebugDisplayMode::SSGI;
    }
    if (key == "ssgi-raw" || key == "global-illumination-raw" || key == "screen-space-global-illumination-raw") {
        return render::DebugDisplayMode::SSGIRaw;
    }
    if (key == "ssgi-history" || key == "global-illumination-history" ||
        key == "screen-space-global-illumination-history") {
        return render::DebugDisplayMode::SSGIHistory;
    }
    if (key == "decals" || key == "dbuffer") {
        return render::DebugDisplayMode::Decals;
    }
    if (key == "contact-shadows" || key == "contact" || key == "contactshadow") {
        return render::DebugDisplayMode::ContactShadows;
    }
    if (key == "exposure" || key == "exposure-histogram") {
        return render::DebugDisplayMode::Exposure;
    }
    if (key == "color-grading" || key == "colorgrading" || key == "grading" || key == "lut") {
        return render::DebugDisplayMode::ColorGrading;
    }
    if (key == "bloom") {
        return render::DebugDisplayMode::Bloom;
    }
    if (key == "taa-history" || key == "taahistory" || key == "taa_history") {
        return render::DebugDisplayMode::TaaHistory;
    }
    if (key == "dof" || key == "depth-of-field" || key == "depthoffield") {
        return render::DebugDisplayMode::Dof;
    }
    if (key == "fxaa" || key == "fxaa-edges" || key == "fxaaedge") {
        return render::DebugDisplayMode::Fxaa;
    }
    return std::nullopt;
}

} // namespace

std::optional<bgfx::RendererType::Enum> parseBackend(std::string_view name) {
    const std::string lower = toLower(name);

    if (lower.empty() || lower == "auto" || lower == "count") {
        return bgfx::RendererType::Count;
    }
    if (lower == "d3d11" || lower == "dx11" || lower == "direct3d11") {
        return bgfx::RendererType::Direct3D11;
    }
    if (lower == "d3d12" || lower == "dx12" || lower == "direct3d12") {
        return bgfx::RendererType::Direct3D12;
    }
    if (lower == "vulkan" || lower == "vk") {
        return bgfx::RendererType::Vulkan;
    }
    if (lower == "opengl" || lower == "gl") {
        return bgfx::RendererType::OpenGL;
    }
    if (lower == "metal") {
        return bgfx::RendererType::Metal;
    }

    return std::nullopt;
}

std::string_view backendName(bgfx::RendererType::Enum type) {
    switch (type) {
    case bgfx::RendererType::Count:
        return "auto";
    case bgfx::RendererType::Direct3D11:
        return "d3d11";
    case bgfx::RendererType::Direct3D12:
        return "d3d12";
    case bgfx::RendererType::Vulkan:
        return "vulkan";
    case bgfx::RendererType::OpenGL:
        return "opengl";
    case bgfx::RendererType::Metal:
        return "metal";
    default:
        return "unknown";
    }
}

void applyPostProcessOverrides(const PostProcessOverrides& overrides, render::PostProcessSettings& settings) {
    if (overrides.exposureMode) {
        settings.exposureMode = *overrides.exposureMode;
    }
    if (overrides.fixedEv100) {
        settings.fixedEv100 = *overrides.fixedEv100;
    }
    if (overrides.compensation) {
        settings.compensation = *overrides.compensation;
    }
    if (overrides.limitMin) {
        settings.limitMin = *overrides.limitMin;
    }
    if (overrides.limitMax) {
        settings.limitMax = *overrides.limitMax;
    }
    if (overrides.debugExposure) {
        settings.debugExposure = *overrides.debugExposure;
    }
    if (overrides.tonemapping) {
        settings.tonemapping = *overrides.tonemapping;
    }
    if (overrides.taa) {
        settings.taa.enabled = *overrides.taa;
        if (*overrides.taa) {
            settings.fxaa.enabled = false;
        }
    }
    if (overrides.taaQuality) {
        settings.taa.quality = *overrides.taaQuality;
    }
    if (overrides.taaMotionRejection) {
        settings.taa.motionVectorRejection = *overrides.taaMotionRejection;
    }
    if (overrides.taaSharpening) {
        settings.taa.sharpening = *overrides.taaSharpening;
    }
    if (overrides.taaJitterScale) {
        settings.taa.jitterScale = *overrides.taaJitterScale;
    }
    if (overrides.fxaa) {
        settings.fxaa.enabled = *overrides.fxaa;
        if (*overrides.fxaa) {
            settings.taa.enabled = false;
        }
    }
    if (overrides.bloom) {
        settings.bloom.enabled = *overrides.bloom;
    }
    if (overrides.bloomThreshold) {
        settings.bloom.threshold = *overrides.bloomThreshold;
    }
    if (overrides.bloomIntensity) {
        settings.bloom.intensity = *overrides.bloomIntensity;
    }
    if (overrides.bloomScatter) {
        settings.bloom.scatter = *overrides.bloomScatter;
    }
    if (overrides.bloomResolution) {
        settings.bloom.resolution = *overrides.bloomResolution;
    }
    if (overrides.bloomHighQualityPrefiltering) {
        settings.bloom.highQualityPrefiltering = *overrides.bloomHighQualityPrefiltering;
    }
    if (overrides.bloomHighQualityFiltering) {
        settings.bloom.highQualityFiltering = *overrides.bloomHighQualityFiltering;
    }
    if (overrides.colorGrading) {
        settings.colorGrading.enabled = *overrides.colorGrading;
    }
    if (overrides.depthOfField) {
        settings.depthOfField.enabled = *overrides.depthOfField;
    }
    if (overrides.dofFocusDistance) {
        settings.depthOfField.focusDistance = *overrides.dofFocusDistance;
    }
    if (overrides.dofNearFocusStart) {
        settings.depthOfField.nearFocusStart = *overrides.dofNearFocusStart;
    }
    if (overrides.dofNearFocusEnd) {
        settings.depthOfField.nearFocusEnd = *overrides.dofNearFocusEnd;
    }
    if (overrides.dofFarFocusStart) {
        settings.depthOfField.farFocusStart = *overrides.dofFarFocusStart;
    }
    if (overrides.dofFarFocusEnd) {
        settings.depthOfField.farFocusEnd = *overrides.dofFarFocusEnd;
    }
    if (overrides.dofNearMaxBlur) {
        settings.depthOfField.nearMaxBlur = *overrides.dofNearMaxBlur;
    }
    if (overrides.dofFarMaxBlur) {
        settings.depthOfField.farMaxBlur = *overrides.dofFarMaxBlur;
    }
    if (overrides.dofResolution) {
        settings.depthOfField.resolution = *overrides.dofResolution;
    }
    if (overrides.dofHighQuality) {
        settings.depthOfField.highQuality = *overrides.dofHighQuality;
    }
    if (overrides.motionBlur) {
        settings.motionBlur.enabled = *overrides.motionBlur;
    }
    if (overrides.motionBlurIntensity) {
        settings.motionBlur.intensity = *overrides.motionBlurIntensity;
    }
    if (overrides.motionBlurMaxVelocity) {
        settings.motionBlur.maximumVelocity = *overrides.motionBlurMaxVelocity;
    }
    if (overrides.motionBlurMinVelocity) {
        settings.motionBlur.minimumVelocity = *overrides.motionBlurMinVelocity;
    }
    if (overrides.motionBlurCamera) {
        settings.motionBlur.cameraMotionBlur = *overrides.motionBlurCamera;
    }
    if (overrides.motionBlurSamples) {
        settings.motionBlur.sampleCount = *overrides.motionBlurSamples;
    }
    if (overrides.motionBlurDepthExtent) {
        settings.motionBlur.depthComparisonExtent = *overrides.motionBlurDepthExtent;
    }
}

void applyVolumetricFogOverrides(const VolumetricFogOverrides& overrides,
                                 render::VolumetricFogSettings& settings) {
    if (overrides.enabled) {
        settings.enabled = *overrides.enabled;
    }
    if (overrides.meanFreePath) {
        settings.meanFreePath = *overrides.meanFreePath;
    }
    if (overrides.baseHeight) {
        settings.baseHeight = *overrides.baseHeight;
    }
    if (overrides.maximumHeight) {
        settings.maximumHeight = *overrides.maximumHeight;
    }
    if (overrides.anisotropy) {
        settings.anisotropy = *overrides.anisotropy;
    }
    if (overrides.depthExtent) {
        settings.depthExtent = *overrides.depthExtent;
    }
    if (overrides.stepCount) {
        settings.stepCount = *overrides.stepCount;
    }
    if (overrides.directionalShadows) {
        settings.directionalShadows = *overrides.directionalShadows;
    }
    if (overrides.resolutionScale) {
        settings.resolutionScale = *overrides.resolutionScale;
    }
    if (overrides.temporal) {
        settings.temporal = *overrides.temporal;
    }
    if (overrides.temporalHistoryWeight) {
        settings.temporalHistoryWeight = *overrides.temporalHistoryWeight;
    }
    if (overrides.temporalJitter) {
        settings.temporalJitter = *overrides.temporalJitter;
    }
    if (overrides.punctualLights) {
        settings.punctualLights = *overrides.punctualLights;
    }
    if (overrides.punctualShadows) {
        settings.punctualShadows = *overrides.punctualShadows;
    }
    if (overrides.maxPunctualLights) {
        settings.maxPunctualLights = *overrides.maxPunctualLights;
    }
    if (overrides.localVolumes) {
        settings.localVolumes = *overrides.localVolumes;
    }
}

void applyShadowOverrides(const ShadowOverrides& overrides, render::ShadowSettings& settings) {
    if (overrides.directionalFilter) {
        settings.directionalFilter = *overrides.directionalFilter;
    }
    if (overrides.punctualFilter) {
        settings.punctualFilter = *overrides.punctualFilter;
    }
    if (overrides.maxDistance) {
        settings.maxDistance = *overrides.maxDistance;
    }
    if (overrides.cascadeCount) {
        settings.cascadeCount = *overrides.cascadeCount;
    }
    if (overrides.atlasSize) {
        settings.atlasSize = *overrides.atlasSize;
    }
    if (overrides.maxRequests) {
        settings.maxRequests = *overrides.maxRequests;
    }
    if (overrides.directionalResolution) {
        settings.directionalResolution = *overrides.directionalResolution;
    }
    if (overrides.pointResolution) {
        settings.pointResolution = *overrides.pointResolution;
    }
    if (overrides.spotResolution) {
        settings.spotResolution = *overrides.spotResolution;
    }
    if (overrides.normalBias) {
        settings.normalBias = *overrides.normalBias;
    }
    if (overrides.slopeBias) {
        settings.slopeBias = *overrides.slopeBias;
    }
    if (overrides.shadowNearPlane) {
        settings.shadowNearPlane = *overrides.shadowNearPlane;
    }
    if (overrides.softness) {
        settings.softness = *overrides.softness;
    }
    if (overrides.blockerSamples) {
        settings.blockerSamples = *overrides.blockerSamples;
    }
    if (overrides.filterSamples) {
        settings.filterSamples = *overrides.filterSamples;
    }
    if (overrides.minFilterSize) {
        settings.minFilterSize = *overrides.minFilterSize;
    }
    if (overrides.cascadeFade) {
        settings.cascadeFade = *overrides.cascadeFade;
    }
    if (overrides.microShadows) {
        settings.microShadows = *overrides.microShadows;
    }
    if (overrides.microShadowOpacity) {
        settings.microShadowOpacity = *overrides.microShadowOpacity;
    }
    if (overrides.contactShadows) {
        settings.contactShadows.enabled = *overrides.contactShadows;
    }
    if (overrides.contactShadowLength) {
        settings.contactShadows.length = *overrides.contactShadowLength;
    }
    if (overrides.contactShadowOpacity) {
        settings.contactShadows.opacity = *overrides.contactShadowOpacity;
    }
    if (overrides.contactShadowSamples) {
        settings.contactShadows.sampleCount = *overrides.contactShadowSamples;
    }
    if (overrides.contactShadowHalfResolution) {
        settings.contactShadows.halfResolution = *overrides.contactShadowHalfResolution;
    }
    if (overrides.contactShadowDirectionalOnly) {
        settings.contactShadows.directionalOnly = *overrides.contactShadowDirectionalOnly;
    }
    if (overrides.shadowDimmer) {
        settings.shadowDimmer = *overrides.shadowDimmer;
    }
    if (overrides.shadowFadeDistance) {
        settings.shadowFadeDistance = *overrides.shadowFadeDistance;
    }
    if (overrides.shadowTint) {
        settings.shadowTint = *overrides.shadowTint;
    }
    if (overrides.cachedDirectionalShadows) {
        settings.cachedDirectionalShadows = *overrides.cachedDirectionalShadows;
    }
    if (overrides.cachedPunctualShadows) {
        settings.cachedPunctualShadows = *overrides.cachedPunctualShadows;
    }
    if (overrides.cachedAreaShadows) {
        settings.cachedAreaShadows = *overrides.cachedAreaShadows;
    }
}

CommandLineResult parseCommandLine(int argc, const char* const* argv) {
    CommandLineResult result;
    result.ok = true;

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i] != nullptr ? argv[i] : "";

        if (arg == "--help" || arg == "-h") {
            result.help = true;
            return result;
        }
        if (arg == "--no-vsync") {
            result.options.vsync = false;
            continue;
        }
        if (arg == "--vsync") {
            result.options.vsync = true;
            continue;
        }
        if (arg == "--verbose") {
            result.options.verbose = true;
            continue;
        }
        if (arg == "--bgfx-debug") {
            result.options.bgfxDebug = true;
            continue;
        }
        if (arg == "--gfx-test") {
            result.options.gfxTest = true;
            continue;
        }
        if (arg == "--cluster-stats-readback") {
            result.options.clusterStatsReadback = true;
            continue;
        }
        if (arg == "--debug-light-data") {
            result.options.debugLightData = true;
            result.options.debugDisplayMode = render::DebugDisplayMode::LightData;
            continue;
        }
        if (const auto value = optionValue(arg, "--debug-view")) {
            const auto mode = parseDebugDisplayMode(*value);
            if (!mode) {
                HBG_LOGE("unknown debug view '{}' (expected off, light-data, normal, motion-vectors, depth, "
                         "depth-pyramid, light-cluster, directional-shadow-atlas, punctual-shadow-atlas, "
                         "shadow-cascade, shadow-filter, albedo, roughness, metallic, ao, emissive, ssao, "
                         "ssao-raw, ssao-history, ssr, ssr-raw, ssr-history, ssgi, ssgi-raw, ssgi-history, "
                         "decals, exposure, color-grading, bloom, taa-history, dof or fxaa)",
                         *value);
                result.ok = false;
            } else {
                result.options.debugDisplayMode = *mode;
                result.options.debugLightData = *mode == render::DebugDisplayMode::LightData;
                if (*mode == render::DebugDisplayMode::SSAO ||
                    *mode == render::DebugDisplayMode::SSAORaw ||
                    *mode == render::DebugDisplayMode::SSAOHistory) {
                    result.options.ssao = true;
                }
                if (*mode == render::DebugDisplayMode::SSAOHistory) {
                    result.options.ssaoTemporal = true;
                }
                if (*mode == render::DebugDisplayMode::SSR ||
                    *mode == render::DebugDisplayMode::SSRRaw ||
                    *mode == render::DebugDisplayMode::SSRHistory) {
                    result.options.ssr = true;
                }
                if (*mode == render::DebugDisplayMode::SSRHistory) {
                    result.options.ssrTemporal = true;
                }
                if (*mode == render::DebugDisplayMode::SSGI ||
                    *mode == render::DebugDisplayMode::SSGIRaw ||
                    *mode == render::DebugDisplayMode::SSGIHistory) {
                    result.options.ssgi = true;
                }
                if (*mode == render::DebugDisplayMode::SSGIHistory) {
                    result.options.ssgiTemporal = true;
                }
                if (*mode == render::DebugDisplayMode::Decals) {
                    result.options.decals = true;
                }
                if (*mode == render::DebugDisplayMode::Fxaa) {
                    result.options.postProcess.fxaa.enabled = true;
                    result.options.postProcess.taa.enabled = false;
                    result.options.postProcessOverrides.fxaa = true;
                    result.options.postProcessOverrides.taa = false;
                }
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--debug-light-cluster-slice")) {
            int slice = -1;
            if (!parseInt(*value, slice) || slice < -2 || slice >= 32) {
                HBG_LOGE("invalid --debug-light-cluster-slice value '{}' (expected -2..31)", *value);
                result.ok = false;
            } else {
                result.options.debugLightClusterSlice = slice;
            }
            continue;
        }
        if (arg == "--deferred") {
            result.options.litShaderMode = render::LitShaderMode::Deferred;
            continue;
        }
        if (const auto value = optionValue(arg, "--lit-shader-mode")) {
            const auto mode = parseLitShaderMode(*value);
            if (!mode) {
                HBG_LOGE("unknown lit shader mode '{}' (expected forward or deferred)", *value);
                result.ok = false;
            } else {
                result.options.litShaderMode = *mode;
            }
            continue;
        }
        if (arg == "--ssao") {
            result.options.ssao = true;
            continue;
        }
        if (arg == "--no-ssao") {
            result.options.ssao = false;
            continue;
        }
        if (arg == "--ssao-temporal") {
            result.options.ssaoTemporal = true;
            continue;
        }
        if (arg == "--no-ssao-temporal") {
            result.options.ssaoTemporal = false;
            continue;
        }
        if (arg == "--ssr") {
            result.options.ssr = true;
            continue;
        }
        if (arg == "--no-ssr") {
            result.options.ssr = false;
            continue;
        }
        if (arg == "--ssr-temporal") {
            result.options.ssrTemporal = true;
            continue;
        }
        if (arg == "--no-ssr-temporal") {
            result.options.ssrTemporal = false;
            continue;
        }
        if (arg == "--ssgi") {
            result.options.ssgi = true;
            continue;
        }
        if (arg == "--no-ssgi") {
            result.options.ssgi = false;
            continue;
        }
        if (arg == "--ssgi-temporal") {
            result.options.ssgiTemporal = true;
            continue;
        }
        if (arg == "--no-ssgi-temporal") {
            result.options.ssgiTemporal = false;
            continue;
        }
        if (arg == "--decals") {
            result.options.decals = true;
            continue;
        }
        if (arg == "--no-decals") {
            result.options.decals = false;
            continue;
        }
        if (arg == "--reflection-probes") {
            result.options.reflectionProbes = true;
            continue;
        }
        if (arg == "--no-reflection-probes") {
            result.options.reflectionProbes = false;
            continue;
        }
        if (arg == "--volumetric-fog" || arg == "--fog") {
            result.options.volumetricFogOverrides.enabled = true;
            continue;
        }
        if (arg == "--no-volumetric-fog" || arg == "--no-fog") {
            result.options.volumetricFogOverrides.enabled = false;
            continue;
        }
        if (arg == "--fog-shadows") {
            result.options.volumetricFogOverrides.directionalShadows = true;
            continue;
        }
        if (arg == "--no-fog-shadows") {
            result.options.volumetricFogOverrides.directionalShadows = false;
            continue;
        }
        if (arg == "--fog-temporal") {
            result.options.volumetricFogOverrides.temporal = true;
            continue;
        }
        if (arg == "--no-fog-temporal") {
            result.options.volumetricFogOverrides.temporal = false;
            continue;
        }
        if (arg == "--fog-jitter") {
            result.options.volumetricFogOverrides.temporalJitter = true;
            continue;
        }
        if (arg == "--no-fog-jitter") {
            result.options.volumetricFogOverrides.temporalJitter = false;
            continue;
        }
        if (arg == "--fog-punctual-lights") {
            result.options.volumetricFogOverrides.punctualLights = true;
            continue;
        }
        if (arg == "--no-fog-punctual-lights") {
            result.options.volumetricFogOverrides.punctualLights = false;
            continue;
        }
        if (arg == "--fog-punctual-shadows") {
            result.options.volumetricFogOverrides.punctualShadows = true;
            continue;
        }
        if (arg == "--no-fog-punctual-shadows") {
            result.options.volumetricFogOverrides.punctualShadows = false;
            continue;
        }
        if (arg == "--fog-local-volumes") {
            result.options.volumetricFogOverrides.localVolumes = true;
            continue;
        }
        if (arg == "--no-fog-local-volumes") {
            result.options.volumetricFogOverrides.localVolumes = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-profile")) {
            if (value->empty()) {
                HBG_LOGE("--fog-profile requires a path");
                result.ok = false;
            } else {
                result.options.volumetricFogProfilePath = std::string(*value);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-mean-free-path")) {
            float meanFreePath = 400.0f;
            if (!parseFloat(*value, meanFreePath) || meanFreePath < 0.1f || meanFreePath > 1000000.0f) {
                HBG_LOGE("invalid --fog-mean-free-path value '{}'", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.meanFreePath = meanFreePath;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-base-height")) {
            float baseHeight = 0.0f;
            if (!parseFloat(*value, baseHeight)) {
                HBG_LOGE("invalid --fog-base-height value '{}'", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.baseHeight = baseHeight;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-maximum-height")) {
            float maximumHeight = 50.0f;
            if (!parseFloat(*value, maximumHeight)) {
                HBG_LOGE("invalid --fog-maximum-height value '{}'", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.maximumHeight = maximumHeight;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-anisotropy")) {
            float anisotropy = 0.5f;
            if (!parseFloat(*value, anisotropy) || anisotropy < -1.0f || anisotropy > 1.0f) {
                HBG_LOGE("invalid --fog-anisotropy value '{}' (expected -1..1)", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.anisotropy = anisotropy;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-depth-extent")) {
            float depthExtent = 64.0f;
            if (!parseFloat(*value, depthExtent) || depthExtent < 0.1f || depthExtent > 5000.0f) {
                HBG_LOGE("invalid --fog-depth-extent value '{}'", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.depthExtent = depthExtent;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-steps")) {
            int steps = 16;
            if (!parseInt(*value, steps) || steps < 4 || steps > 64) {
                HBG_LOGE("invalid --fog-steps value '{}' (expected 4..64)", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.stepCount = static_cast<uint32_t>(steps);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-resolution-scale")) {
            float scale = 0.5f;
            if (!parseFloat(*value, scale) || scale < 0.25f || scale > 1.0f) {
                HBG_LOGE("invalid --fog-resolution-scale value '{}' (expected 0.25..1)", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.resolutionScale = scale;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-scale")) {
            float scale = 0.5f;
            if (!parseFloat(*value, scale) || scale < 0.25f || scale > 1.0f) {
                HBG_LOGE("invalid --fog-scale value '{}' (expected 0.25..1)", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.resolutionScale = scale;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-temporal-weight")) {
            float weight = 0.85f;
            if (!parseFloat(*value, weight) || weight < 0.0f || weight > 0.95f) {
                HBG_LOGE("invalid --fog-temporal-weight value '{}' (expected 0..0.95)", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.temporalHistoryWeight = weight;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--fog-max-punctual-lights")) {
            int count = 8;
            if (!parseInt(*value, count) || count < 0 || count > 16) {
                HBG_LOGE("invalid --fog-max-punctual-lights value '{}' (expected 0..16)", *value);
                result.ok = false;
            } else {
                result.options.volumetricFogOverrides.maxPunctualLights = static_cast<uint32_t>(count);
            }
            continue;
        }
        if (arg == "--taa") {
            result.options.postProcess.taa.enabled = true;
            result.options.postProcess.fxaa.enabled = false;
            result.options.postProcessOverrides.taa = true;
            result.options.postProcessOverrides.fxaa = false;
            continue;
        }
        if (arg == "--no-taa") {
            result.options.postProcess.taa.enabled = false;
            result.options.postProcessOverrides.taa = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--taa-quality")) {
            int quality = 3;
            if (!parseInt(*value, quality) || quality < 1 || quality > 5) {
                HBG_LOGE("invalid --taa-quality value '{}' (expected 1..5)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.taaQuality = static_cast<uint32_t>(quality);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--taa-motion-rejection")) {
            float rejection = 0.1f;
            if (!parseFloat(*value, rejection) || rejection < 0.0f || rejection > 1.0f) {
                HBG_LOGE("invalid --taa-motion-rejection value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.taaMotionRejection = rejection;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--taa-sharpening")) {
            float sharpening = 0.0f;
            if (!parseFloat(*value, sharpening) || sharpening < 0.0f || sharpening > 1.0f) {
                HBG_LOGE("invalid --taa-sharpening value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.taaSharpening = sharpening;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--taa-jitter-scale")) {
            float scale = 1.0f;
            if (!parseFloat(*value, scale) || scale < 0.0f || scale > 1.0f) {
                HBG_LOGE("invalid --taa-jitter-scale value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.taaJitterScale = scale;
            }
            continue;
        }
        if (arg == "--fxaa") {
            result.options.postProcess.taa.enabled = false;
            result.options.postProcess.fxaa.enabled = true;
            result.options.postProcessOverrides.taa = false;
            result.options.postProcessOverrides.fxaa = true;
            continue;
        }
        if (arg == "--no-fxaa") {
            result.options.postProcess.fxaa.enabled = false;
            result.options.postProcessOverrides.fxaa = false;
            continue;
        }
        if (arg == "--bloom") {
            result.options.postProcess.bloom.enabled = true;
            result.options.postProcessOverrides.bloom = true;
            continue;
        }
        if (arg == "--no-bloom") {
            result.options.postProcess.bloom.enabled = false;
            result.options.postProcessOverrides.bloom = false;
            continue;
        }
        if (arg == "--bloom-high-quality-prefilter") {
            result.options.postProcessOverrides.bloomHighQualityPrefiltering = true;
            continue;
        }
        if (arg == "--no-bloom-high-quality-prefilter") {
            result.options.postProcessOverrides.bloomHighQualityPrefiltering = false;
            continue;
        }
        if (arg == "--bloom-high-quality-filter") {
            result.options.postProcessOverrides.bloomHighQualityFiltering = true;
            continue;
        }
        if (arg == "--no-bloom-high-quality-filter") {
            result.options.postProcessOverrides.bloomHighQualityFiltering = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--bloom-threshold")) {
            float threshold = 0.0f;
            if (!parseFloat(*value, threshold) || threshold < 0.0f || threshold > 100.0f) {
                HBG_LOGE("invalid --bloom-threshold value '{}' (expected 0..100)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.bloomThreshold = threshold;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--bloom-intensity")) {
            float intensity = 0.0f;
            if (!parseFloat(*value, intensity) || intensity < 0.0f || intensity > 1.0f) {
                HBG_LOGE("invalid --bloom-intensity value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.bloomIntensity = intensity;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--bloom-scatter")) {
            float scatter = 0.0f;
            if (!parseFloat(*value, scatter) || scatter < 0.0f || scatter > 1.0f) {
                HBG_LOGE("invalid --bloom-scatter value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.bloomScatter = scatter;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--bloom-resolution")) {
            if (*value == "half" || *value == "2") {
                result.options.postProcessOverrides.bloomResolution = 2u;
            } else if (*value == "quarter" || *value == "4") {
                result.options.postProcessOverrides.bloomResolution = 4u;
            } else {
                HBG_LOGE("invalid --bloom-resolution value '{}' (expected half or quarter)", *value);
                result.ok = false;
            }
            continue;
        }
        if (arg == "--color-grading") {
            result.options.postProcess.colorGrading.enabled = true;
            result.options.postProcessOverrides.colorGrading = true;
            continue;
        }
        if (arg == "--no-color-grading") {
            result.options.postProcess.colorGrading.enabled = false;
            result.options.postProcessOverrides.colorGrading = false;
            continue;
        }
        if (arg == "--dof") {
            result.options.postProcess.depthOfField.enabled = true;
            result.options.postProcessOverrides.depthOfField = true;
            continue;
        }
        if (arg == "--no-dof") {
            result.options.postProcess.depthOfField.enabled = false;
            result.options.postProcessOverrides.depthOfField = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--dof-focus-distance")) {
            float distance = 0.0f;
            if (!parseFloat(*value, distance) || distance <= 0.0f) {
                HBG_LOGE("invalid --dof-focus-distance value '{}' (expected > 0)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.dofFocusDistance = distance;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--dof-near-start")) {
            float distance = 0.0f;
            if (!parseFloat(*value, distance) || distance < 0.0f) {
                HBG_LOGE("invalid --dof-near-start value '{}' (expected >= 0)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.dofNearFocusStart = distance;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--dof-near-end")) {
            float distance = 0.0f;
            if (!parseFloat(*value, distance) || distance < 0.0f) {
                HBG_LOGE("invalid --dof-near-end value '{}' (expected >= 0)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.dofNearFocusEnd = distance;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--dof-far-start")) {
            float distance = 0.0f;
            if (!parseFloat(*value, distance) || distance < 0.0f) {
                HBG_LOGE("invalid --dof-far-start value '{}' (expected >= 0)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.dofFarFocusStart = distance;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--dof-far-end")) {
            float distance = 0.0f;
            if (!parseFloat(*value, distance) || distance < 0.0f) {
                HBG_LOGE("invalid --dof-far-end value '{}' (expected >= 0)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.dofFarFocusEnd = distance;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--dof-near-max-blur")) {
            float radius = 0.0f;
            if (!parseFloat(*value, radius) || radius < 0.0f || radius > 8.0f) {
                HBG_LOGE("invalid --dof-near-max-blur value '{}' (expected 0..8)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.dofNearMaxBlur = radius;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--dof-far-max-blur")) {
            float radius = 0.0f;
            if (!parseFloat(*value, radius) || radius < 0.0f || radius > 16.0f) {
                HBG_LOGE("invalid --dof-far-max-blur value '{}' (expected 0..16)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.dofFarMaxBlur = radius;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--dof-resolution")) {
            if (*value == "full" || *value == "1") {
                result.options.postProcessOverrides.dofResolution = 1u;
            } else if (*value == "half" || *value == "2") {
                result.options.postProcessOverrides.dofResolution = 2u;
            } else if (*value == "quarter" || *value == "4") {
                result.options.postProcessOverrides.dofResolution = 4u;
            } else {
                HBG_LOGE("invalid --dof-resolution value '{}' (expected full, half or quarter)", *value);
                result.ok = false;
            }
            continue;
        }
        if (arg == "--dof-high-quality") {
            result.options.postProcessOverrides.dofHighQuality = true;
            continue;
        }
        if (arg == "--no-dof-high-quality") {
            result.options.postProcessOverrides.dofHighQuality = false;
            continue;
        }
        if (arg == "--motion-blur") {
            result.options.postProcess.motionBlur.enabled = true;
            result.options.postProcessOverrides.motionBlur = true;
            continue;
        }
        if (arg == "--no-motion-blur") {
            result.options.postProcess.motionBlur.enabled = false;
            result.options.postProcessOverrides.motionBlur = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--motion-blur-intensity")) {
            float intensity = 0.0f;
            if (!parseFloat(*value, intensity) || intensity < 0.0f || intensity > 2.0f) {
                HBG_LOGE("invalid --motion-blur-intensity value '{}' (expected 0..2)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.motionBlurIntensity = intensity;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--motion-blur-max-velocity")) {
            float velocity = 0.0f;
            if (!parseFloat(*value, velocity) || velocity < 0.0f || velocity > 1500.0f) {
                HBG_LOGE("invalid --motion-blur-max-velocity value '{}' (expected 0..1500 pixels)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.motionBlurMaxVelocity = velocity;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--motion-blur-min-velocity")) {
            float velocity = 0.0f;
            if (!parseFloat(*value, velocity) || velocity < 0.0f || velocity > 64.0f) {
                HBG_LOGE("invalid --motion-blur-min-velocity value '{}' (expected 0..64 pixels)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.motionBlurMinVelocity = velocity;
            }
            continue;
        }
        if (arg == "--motion-blur-camera") {
            result.options.postProcessOverrides.motionBlurCamera = true;
            continue;
        }
        if (arg == "--no-motion-blur-camera") {
            result.options.postProcessOverrides.motionBlurCamera = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--motion-blur-samples")) {
            int samples = 0;
            if (!parseInt(*value, samples) || samples < 2 || samples > 32) {
                HBG_LOGE("invalid --motion-blur-samples value '{}' (expected 2..32)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.motionBlurSamples = static_cast<uint32_t>(samples);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--motion-blur-depth-extent")) {
            float extent = 0.0f;
            if (!parseFloat(*value, extent) || extent < 0.0f || extent > 20.0f) {
                HBG_LOGE("invalid --motion-blur-depth-extent value '{}' (expected 0..20)", *value);
                result.ok = false;
            } else {
                result.options.postProcessOverrides.motionBlurDepthExtent = extent;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--aa")) {
            bool taa = false;
            bool fxaa = false;
            if (!parseAntialiasing(*value, taa, fxaa)) {
                HBG_LOGE("unknown AA mode '{}' (expected off, taa or fxaa)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.taa.enabled = taa;
                result.options.postProcess.fxaa.enabled = fxaa;
                result.options.postProcessOverrides.taa = taa;
                result.options.postProcessOverrides.fxaa = fxaa;
            }
            continue;
        }
        if (arg == "--debug-wireframe") {
            result.options.debugWireframe = true;
            continue;
        }
        if (arg == "--no-shadows") {
            result.options.disableShadows = true;
            continue;
        }
        if (arg == "--directional-shadow-cache") {
            result.options.shadowOverrides.cachedDirectionalShadows = true;
            continue;
        }
        if (arg == "--no-directional-shadow-cache") {
            result.options.shadowOverrides.cachedDirectionalShadows = false;
            continue;
        }
        if (arg == "--shadow-cache") {
            result.options.shadowOverrides.cachedPunctualShadows = true;
            continue;
        }
        if (arg == "--no-shadow-cache") {
            result.options.shadowOverrides.cachedPunctualShadows = false;
            continue;
        }
        if (arg == "--area-shadow-cache") {
            result.options.shadowOverrides.cachedAreaShadows = true;
            continue;
        }
        if (arg == "--no-area-shadow-cache") {
            result.options.shadowOverrides.cachedAreaShadows = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-dimmer")) {
            float dimmer = 0.0f;
            if (!parseFloat(*value, dimmer) || dimmer < 0.0f || dimmer > 1.0f) {
                HBG_LOGE("invalid --shadow-dimmer value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.shadowDimmer = dimmer;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-fade-distance")) {
            float distance = 0.0f;
            if (!parseFloat(*value, distance) || distance < 0.0f || distance > 100000.0f) {
                HBG_LOGE("invalid --shadow-fade-distance value '{}' (expected 0..100000)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.shadowFadeDistance = distance;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-tint")) {
            Vec3 tint;
            if (!parseVec3(*value, tint) || tint.x < 0.0f || tint.x > 1.0f || tint.y < 0.0f ||
                tint.y > 1.0f || tint.z < 0.0f || tint.z > 1.0f) {
                HBG_LOGE("invalid --shadow-tint value '{}' (expected r,g,b in 0..1)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.shadowTint = tint;
            }
            continue;
        }
        if (arg == "--micro-shadows") {
            result.options.shadowOverrides.microShadows = true;
            continue;
        }
        if (arg == "--no-micro-shadows") {
            result.options.shadowOverrides.microShadows = false;
            continue;
        }
        if (arg == "--contact-shadows") {
            result.options.shadowOverrides.contactShadows = true;
            continue;
        }
        if (arg == "--no-contact-shadows") {
            result.options.shadowOverrides.contactShadows = false;
            continue;
        }
        if (arg == "--contact-shadow-half-res") {
            result.options.shadowOverrides.contactShadowHalfResolution = true;
            continue;
        }
        if (arg == "--no-contact-shadow-half-res") {
            result.options.shadowOverrides.contactShadowHalfResolution = false;
            continue;
        }
        if (arg == "--contact-shadow-directional-only") {
            result.options.shadowOverrides.contactShadowDirectionalOnly = true;
            continue;
        }
        if (arg == "--no-contact-shadow-directional-only") {
            result.options.shadowOverrides.contactShadowDirectionalOnly = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-profile")) {
            if (value->empty()) {
                HBG_LOGE("--shadow-profile requires a path");
                result.ok = false;
            } else {
                result.options.shadowSettingsPath = std::string(*value);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-quality")) {
            render::ShadowFilter directional = render::ShadowFilter::Pcf7x7;
            render::ShadowFilter punctual = render::ShadowFilter::Pcf5x5;
            if (!parseShadowQuality(*value, directional, punctual)) {
                HBG_LOGE("invalid --shadow-quality value '{}' (expected off, low, medium or high)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.directionalFilter = directional;
                result.options.shadowOverrides.punctualFilter = punctual;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-filter")) {
            render::ShadowFilter filter = render::ShadowFilter::Pcf3x3;
            if (!render::parseShadowFilter(*value, filter)) {
                HBG_LOGE("invalid --shadow-filter value '{}'", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.directionalFilter = filter;
                result.options.shadowOverrides.punctualFilter = filter;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-directional-filter")) {
            render::ShadowFilter filter = render::ShadowFilter::Pcf3x3;
            if (!render::parseShadowFilter(*value, filter)) {
                HBG_LOGE("invalid --shadow-directional-filter value '{}'", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.directionalFilter = filter;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-punctual-filter")) {
            render::ShadowFilter filter = render::ShadowFilter::Pcf3x3;
            if (!render::parseShadowFilter(*value, filter)) {
                HBG_LOGE("invalid --shadow-punctual-filter value '{}'", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.punctualFilter = filter;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-distance")) {
            float distance = 0.0f;
            if (!parseFloat(*value, distance) || distance < 0.01f || distance > 10000.0f) {
                HBG_LOGE("invalid --shadow-distance value '{}' (expected 0.01..10000)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.maxDistance = distance;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-cascades")) {
            int count = 0;
            if (!parseInt(*value, count) || count < 1 || count > 4) {
                HBG_LOGE("invalid --shadow-cascades value '{}' (expected 1..4)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.cascadeCount = static_cast<uint32_t>(count);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-atlas-size")) {
            int size = 0;
            if (!parseInt(*value, size) || (size != 1024 && size != 2048 && size != 4096)) {
                HBG_LOGE("invalid --shadow-atlas-size value '{}' (expected 1024, 2048 or 4096)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.atlasSize = static_cast<uint32_t>(size);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-max-requests")) {
            int count = 0;
            if (!parseInt(*value, count) || count < 1 || count > static_cast<int>(render::kMaxShadowRequests)) {
                HBG_LOGE("invalid --shadow-max-requests value '{}' (expected 1..{})", *value,
                         render::kMaxShadowRequests);
                result.ok = false;
            } else {
                result.options.shadowOverrides.maxRequests = static_cast<uint32_t>(count);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-directional-resolution")) {
            int resolution = 0;
            if (!parseInt(*value, resolution) || resolution < 256 || resolution > 4096) {
                HBG_LOGE("invalid --shadow-directional-resolution value '{}' (expected 256..4096)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.directionalResolution = static_cast<uint32_t>(resolution);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-point-resolution")) {
            int resolution = 0;
            if (!parseInt(*value, resolution) || resolution < 256 || resolution > 4096) {
                HBG_LOGE("invalid --shadow-point-resolution value '{}' (expected 256..4096)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.pointResolution = static_cast<uint32_t>(resolution);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-spot-resolution")) {
            int resolution = 0;
            if (!parseInt(*value, resolution) || resolution < 256 || resolution > 4096) {
                HBG_LOGE("invalid --shadow-spot-resolution value '{}' (expected 256..4096)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.spotResolution = static_cast<uint32_t>(resolution);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-normal-bias")) {
            float bias = 0.0f;
            if (!parseFloat(*value, bias) || bias < 0.0f || bias > 2.0f) {
                HBG_LOGE("invalid --shadow-normal-bias value '{}' (expected 0..2)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.normalBias = bias;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-slope-bias")) {
            float bias = 0.0f;
            if (!parseFloat(*value, bias) || bias < 0.0f || bias > 10.0f) {
                HBG_LOGE("invalid --shadow-slope-bias value '{}' (expected 0..10)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.slopeBias = bias;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-near-plane")) {
            float distance = 0.0f;
            if (!parseFloat(*value, distance) || distance < 0.01f || distance > 100.0f) {
                HBG_LOGE("invalid --shadow-near-plane value '{}' (expected 0.01..100)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.shadowNearPlane = distance;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-softness")) {
            float softness = 0.0f;
            if (!parseFloat(*value, softness) || softness < 0.0f || softness > 4.0f) {
                HBG_LOGE("invalid --shadow-softness value '{}' (expected 0..4)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.softness = softness;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-blocker-samples")) {
            int samples = 0;
            if (!parseInt(*value, samples) || samples < 1 || samples > 64) {
                HBG_LOGE("invalid --shadow-blocker-samples value '{}' (expected 1..64)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.blockerSamples = static_cast<uint32_t>(samples);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-filter-samples")) {
            int samples = 0;
            if (!parseInt(*value, samples) || samples < 1 || samples > 64) {
                HBG_LOGE("invalid --shadow-filter-samples value '{}' (expected 1..64)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.filterSamples = static_cast<uint32_t>(samples);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-min-filter-size")) {
            float size = 0.0f;
            if (!parseFloat(*value, size) || size < 0.0f || size > 1.0f) {
                HBG_LOGE("invalid --shadow-min-filter-size value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.minFilterSize = size;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--shadow-cascade-fade")) {
            float fade = 0.0f;
            if (!parseFloat(*value, fade) || fade < 0.0f || fade > 1.0f) {
                HBG_LOGE("invalid --shadow-cascade-fade value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.cascadeFade = fade;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--contact-shadow-length")) {
            float length = 0.0f;
            if (!parseFloat(*value, length) || length < 0.0f || length > 1.0f) {
                HBG_LOGE("invalid --contact-shadow-length value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.contactShadowLength = length;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--contact-shadow-opacity")) {
            float opacity = 0.0f;
            if (!parseFloat(*value, opacity) || opacity < 0.0f || opacity > 1.0f) {
                HBG_LOGE("invalid --contact-shadow-opacity value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.contactShadowOpacity = opacity;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--contact-shadow-samples")) {
            int samples = 0;
            if (!parseInt(*value, samples) || samples < 4 || samples > 64) {
                HBG_LOGE("invalid --contact-shadow-samples value '{}' (expected 4..64)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.contactShadowSamples = static_cast<uint32_t>(samples);
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--micro-shadow-opacity")) {
            float opacity = 0.0f;
            if (!parseFloat(*value, opacity) || opacity < 0.0f || opacity > 1.0f) {
                HBG_LOGE("invalid --micro-shadow-opacity value '{}' (expected 0..1)", *value);
                result.ok = false;
            } else {
                result.options.shadowOverrides.microShadowOpacity = opacity;
            }
            continue;
        }
        if (arg == "--gpu-light-list") {
            result.options.gpuLightList = true;
            continue;
        }
        if (arg == "--no-gpu-light-list") {
            result.options.gpuLightList = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--gpu-light-list-path")) {
            if (*value == "buffer") {
                result.options.gpuLightList = true;
                result.options.gpuLightListBuffers = true;
            } else if (*value == "texture") {
                result.options.gpuLightList = true;
                result.options.gpuLightListBuffers = false;
            } else {
                HBG_LOGE("invalid --gpu-light-list-path '{}' (expected texture or buffer)", *value);
                result.ok = false;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--light-capacity")) {
            int capacity = 0;
            if (!parseInt(*value, capacity) || !render::isValidLightListCapacity(static_cast<uint32_t>(capacity))) {
                HBG_LOGE("invalid --light-capacity value '{}' (expected 32, 64 or 128)", *value);
                result.ok = false;
            } else {
                result.options.lightListCapacity = static_cast<uint32_t>(capacity);
            }
            continue;
        }
        if (arg == "--light-layers") {
            result.options.lightLayers = true;
            continue;
        }
        if (arg == "--no-light-layers") {
            result.options.lightLayers = false;
            continue;
        }
        if (arg == "--gpu-light-list-buffers") {
            result.options.gpuLightListBuffers = true;
            continue;
        }
        if (arg == "--no-gpu-light-list-buffers") {
            result.options.gpuLightListBuffers = false;
            continue;
        }
        if (arg == "--gpu-light-list-indirect") {
            result.options.gpuLightListIndirect = true;
            continue;
        }
        if (arg == "--no-gpu-light-list-indirect") {
            result.options.gpuLightListIndirect = false;
            continue;
        }
        if (arg == "--light-list-feature-flags") {
            result.options.lightListFeatureFlags = true;
            continue;
        }
        if (arg == "--no-light-list-feature-flags") {
            result.options.lightListFeatureFlags = false;
            continue;
        }
        if (arg == "--cookie-atlas") {
            result.options.cookieAtlas = true;
            continue;
        }
        if (arg == "--no-cookie-atlas") {
            result.options.cookieAtlas = false;
            continue;
        }
        if (const auto value = optionValue(arg, "--cookie-atlas-size")) {
            int size = 0;
            if (!parseInt(*value, size) || (size != 512 && size != 1024 && size != 2048)) {
                HBG_LOGE("invalid --cookie-atlas-size value '{}' (expected 512, 1024 or 2048)", *value);
                result.ok = false;
            } else {
                result.options.cookieAtlasSize = static_cast<uint32_t>(size);
            }
            continue;
        }
        if (arg == "--gpu-shadow-culling") {
            result.options.gpuShadowCulling = true;
            continue;
        }
        if (arg == "--no-gpu-shadow-culling") {
            result.options.gpuShadowCulling = false;
            continue;
        }
        if (arg == "--gpu-shadow-indirect") {
            result.options.gpuShadowIndirect = true;
            continue;
        }
        if (arg == "--no-gpu-shadow-indirect") {
            result.options.gpuShadowIndirect = false;
            continue;
        }
        if (arg == "--async-gpu-light-list") {
            result.options.asyncGpuLightList = true;
            continue;
        }
        if (arg == "--no-async-gpu-light-list") {
            result.options.asyncGpuLightList = false;
            continue;
        }
        if (arg == "--no-light-cluster") {
            result.options.disableLightCluster = true;
            continue;
        }
        if (arg == "--debug-ui") {
            result.options.debugUi = true;
            continue;
        }
        if (arg == "--no-debug-ui") {
            result.options.debugUi = false;
            continue;
        }
        if (arg == "--camera-orbit") {
            result.options.cameraOrbit = true;
            continue;
        }

        if (const auto value = optionValue(arg, "--asset-test")) {
            if (value->empty()) {
                HBG_LOGE("--asset-test requires a path");
                result.ok = false;
            } else {
                result.options.assetTestPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--scene")) {
            if (value->empty()) {
                HBG_LOGE("--scene requires a path");
                result.ok = false;
            } else {
                result.options.scenePath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--render-test")) {
            if (value->empty()) {
                HBG_LOGE("--render-test requires a path");
                result.ok = false;
            } else {
                result.options.renderTestPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--frames")) {
            if (!parseDimension(*value, result.options.frames)) {
                HBG_LOGE("invalid --frames value '{}'", *value);
                result.ok = false;
            } else {
                result.options.framesSpecified = true;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--screenshot")) {
            if (value->empty()) {
                HBG_LOGE("--screenshot requires a path");
                result.ok = false;
            } else {
                result.options.screenshotPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--render-scale")) {
            if (!parseRenderScale(*value, result.options.renderScale)) {
                HBG_LOGE("invalid --render-scale value '{}' (expected 0.1..1.0)", *value);
                result.ok = false;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--postprocess-profile")) {
            if (value->empty()) {
                HBG_LOGE("--postprocess-profile requires a path");
                result.ok = false;
            } else {
                result.options.postProcessProfilePath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--pipeline-settings")) {
            if (value->empty()) {
                HBG_LOGE("--pipeline-settings requires a path");
                result.ok = false;
            } else {
                result.options.pipelineSettingsPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--frame-settings")) {
            if (value->empty()) {
                HBG_LOGE("--frame-settings requires a path");
                result.ok = false;
            } else {
                result.options.frameSettingsPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--sky")) {
            if (value->empty()) {
                HBG_LOGE("--sky requires a path");
                result.ok = false;
            } else {
                result.options.skyPath = std::string(*value);
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--sky-intensity")) {
            if (!parseFloat(*value, result.options.skyIntensity) || result.options.skyIntensity < 0.0f ||
                result.options.skyIntensity > 100.0f) {
                HBG_LOGE("invalid --sky-intensity value '{}' (expected 0..100)", *value);
                result.ok = false;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--sky-rotation")) {
            if (!parseFloat(*value, result.options.skyRotation)) {
                HBG_LOGE("invalid --sky-rotation value '{}'", *value);
                result.ok = false;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-mode")) {
            const auto mode = parseExposureMode(*value);
            if (!mode) {
                HBG_LOGE("unknown exposure mode '{}' (expected fixed, auto or histogram)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.exposureMode = *mode;
                result.options.postProcessOverrides.exposureMode = *mode;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-ev100")) {
            float ev100 = 0.0f;
            if (!parseFloat(*value, ev100) || ev100 < -20.0f || ev100 > 20.0f) {
                HBG_LOGE("invalid --exposure-ev100 value '{}' (expected -20..20)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.fixedEv100 = ev100;
                result.options.postProcessOverrides.fixedEv100 = ev100;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-compensation")) {
            float compensation = 0.0f;
            if (!parseFloat(*value, compensation) || compensation < -20.0f || compensation > 20.0f) {
                HBG_LOGE("invalid --exposure-compensation value '{}' (expected -20..20)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.compensation = compensation;
                result.options.postProcessOverrides.compensation = compensation;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-limit-min")) {
            float limitMin = 0.0f;
            if (!parseFloat(*value, limitMin) || limitMin < -20.0f || limitMin > 30.0f) {
                HBG_LOGE("invalid --exposure-limit-min value '{}' (expected -20..30)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.limitMin = limitMin;
                result.options.postProcessOverrides.limitMin = limitMin;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--exposure-limit-max")) {
            float limitMax = 0.0f;
            if (!parseFloat(*value, limitMax) || limitMax < -20.0f || limitMax > 30.0f) {
                HBG_LOGE("invalid --exposure-limit-max value '{}' (expected -20..30)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.limitMax = limitMax;
                result.options.postProcessOverrides.limitMax = limitMax;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--tonemap")) {
            const auto mode = parseTonemappingMode(*value);
            if (!mode) {
                HBG_LOGE("unknown tonemap mode '{}' (expected aces, neutral or none)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.tonemapping = *mode;
                result.options.postProcessOverrides.tonemapping = *mode;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--debug-exposure")) {
            float exposure = 0.0f;
            if (!parseFloat(*value, exposure) || exposure < -20.0f || exposure > 20.0f) {
                HBG_LOGE("invalid --debug-exposure value '{}' (expected -20..20)", *value);
                result.ok = false;
            } else {
                result.options.postProcess.debugExposure = exposure;
                result.options.postProcessOverrides.debugExposure = exposure;
            }
            continue;
        }

        if (const auto value = optionValue(arg, "--backend")) {
            const auto backend = parseBackend(*value);
            if (!backend) {
                HBG_LOGE("unknown backend '{}' (expected d3d11, d3d12, vulkan, opengl, metal or auto)", *value);
                result.ok = false;
                continue;
            }
            result.options.backend = *backend;
            continue;
        }
        if (arg == "--backend") {
            HBG_LOGE("--backend requires a value");
            result.ok = false;
            continue;
        }

        if (const auto value = optionValue(arg, "--width")) {
            if (!parseDimension(*value, result.options.width)) {
                HBG_LOGE("invalid --width value '{}'", *value);
                result.ok = false;
            }
            continue;
        }
        if (const auto value = optionValue(arg, "--height")) {
            if (!parseDimension(*value, result.options.height)) {
                HBG_LOGE("invalid --height value '{}'", *value);
                result.ok = false;
            }
            continue;
        }

        HBG_LOGE("unknown argument '{}'", arg);
        result.ok = false;
    }

    return result;
}

const char* usage() {
    return "hdrp-bgfx\n"
           "  --backend=<d3d11|d3d12|vulkan|opengl|metal|auto>  Select renderer backend (default: auto)\n"
           "  --width=<pixels>                                  Window width (default: 1280)\n"
           "  --height=<pixels>                                 Window height (default: 720)\n"
           "  --no-vsync                                        Disable vertical sync\n"
           "  --verbose                                         Enable debug logging (including bgfx trace)\n"
           "  --bgfx-debug                                      Enable bgfx debug/validation layers\n"
           "  --gfx-test                                        Run the M0.2 resource/draw smoke test and exit\n"
           "  --debug-ui / --no-debug-ui                        Enable/disable the interactive debug panel\n"
           "  --debug-light-data                                Start with the GPU light-data debug view\n"
           "  --debug-view=<off|light-data|normal|motion-vectors|depth|depth-pyramid|light-cluster|\n"
           "                directional-shadow-atlas|punctual-shadow-atlas|shadow-cascade|shadow-filter|\n"
           "                contact-shadows|albedo|roughness|metallic|ao|\n"
           "                emissive|ssao|ssao-raw|ssao-history|ssr|ssr-raw|ssr-history|\n"
           "                ssgi|ssgi-raw|ssgi-history|decals|exposure|color-grading|bloom|taa-history|dof|fxaa>\n"
           "                                                    Select a fullscreen debug view\n"
           "  --debug-light-cluster-slice=<n>                   Light-cluster debug slice (-2 = FPTL legacy, "
           "-1 = depth slice, 0..31 = fixed)\n"
           "  --debug-wireframe                                 Start with the wireframe overlay enabled\n"
           "  --camera-orbit                                   Orbit the render-test camera to exercise motion "
           "vectors\n"
           "  --no-shadows                                      Start with shadow sampling disabled\n"
           "  --directional-shadow-cache / --no-directional-shadow-cache  Enable/disable cached static directional shadows\n"
           "  --shadow-cache / --no-shadow-cache                Enable/disable cached static punctual shadows\n"
           "  --area-shadow-cache / --no-area-shadow-cache      Enable/disable cached static rectangle area shadows\n"
           "  --shadow-dimmer=<0..1>                            Global default shadow dimmer (per-light wins)\n"
           "  --shadow-fade-distance=<meters>                   Global default light shadow fade distance\n"
           "  --shadow-tint=<r,g,b>                             Global default shadow tint (0..1)\n"
           "  --shadow-profile=<path>                           Load ShadowSettings JSON\n"
           "  --shadow-quality=<off|low|medium|high>            Apply HDRP-like shadow quality presets\n"
           "  --shadow-filter=<off|pcf3x3|pcf5x5|pcf7x7|pcss>   Override both shadow filters\n"
           "  --shadow-directional-filter=<value>               Override directional shadow filter\n"
           "  --shadow-punctual-filter=<value>                  Override point/spot shadow filter\n"
           "  --shadow-distance=<meters>                        Maximum shadow distance (default 200)\n"
           "  --shadow-cascades=<1..4>                          Directional cascade count (default 4)\n"
           "  --shadow-cascade-fade=<0..1>                      Cascade distance fade border (default 0.1)\n"
           "  --shadow-atlas-size=<1024|2048|4096>              Shadow atlas size (default 2048)\n"
           "  --shadow-max-requests=<1..128>                    Maximum shadow requests (default 64)\n"
           "  --shadow-directional-resolution=<256..4096>       Directional cascade resolution (default 1024)\n"
           "  --shadow-point-resolution=<256..4096>             Point face resolution (default 256)\n"
           "  --shadow-spot-resolution=<256..4096>              Spot resolution (default 512)\n"
           "  --shadow-normal-bias=<0..2>                       Normal-bias scale (default 0.2)\n"
           "  --shadow-slope-bias=<0..10>                       Slope-scaled caster bias (default 0.5)\n"
           "  --shadow-near-plane=<0.01..100>                   Punctual shadow near plane (default 0.1)\n"
           "  --shadow-softness=<0..4>                          PCSS softness (default 0.5)\n"
           "  --shadow-blocker-samples=<1..64>                  PCSS blocker samples (default 16)\n"
           "  --shadow-filter-samples=<1..64>                   PCSS filter samples (default 24)\n"
           "  --shadow-min-filter-size=<0..1>                   PCSS minimum filter size (default 0.01)\n"
           "  --micro-shadows / --no-micro-shadows              Enable/disable directional micro shadows\n"
           "  --micro-shadow-opacity=<0..1>                     Micro-shadow opacity (default 1.0)\n"
           "  --contact-shadows / --no-contact-shadows          Enable/disable screen-space contact shadows\n"
           "  --contact-shadow-length=<0..1>                    Contact ray length in meters (default 0.15)\n"
           "  --contact-shadow-opacity=<0..1>                   Contact shadow opacity (default 1.0)\n"
           "  --contact-shadow-samples=<4..64>                  Contact ray-march samples (default 10)\n"
           "  --contact-shadow-half-res / --no-contact-shadow-half-res  Trace contact shadows at half resolution\n"
           "  --contact-shadow-directional-only                 Only evaluate the main directional light\n"
           "  --gpu-light-list / --no-gpu-light-list            Enable/disable the GPU light-list path\n"
            "  --gpu-light-list-path=<texture|buffer>            Select the GPU light-list storage path (default: buffer)\n"
            "  --gpu-light-list-buffers / --no-gpu-light-list-buffers  Enable/disable buffer storage (default: enabled)\n"
            "  --gpu-light-list-indirect / --no-gpu-light-list-indirect  Enable/disable indirect light-list dispatch\n"
           "  --light-list-feature-flags / --no-light-list-feature-flags  Enable/disable tile feature-flag category gating (default: enabled)\n"
           "  --cookie-atlas / --no-cookie-atlas                Enable native cookie atlas / use legacy 8x8 tiles (default: enabled)\n"
           "  --cookie-atlas-size <512|1024|2048>               Cookie atlas resolution (default: 1024)\n"
           "  --light-capacity=<32|64|128>                      Maximum light slots including directional (default: 64)\n"
           "  --light-layers / --no-light-layers                Enable/disable per-light/per-object light layers\n"
           "  --cluster-stats-readback                           Read back GPU cluster overflow and feature-flag stats after rendering\n"
           "  --gpu-shadow-culling / --no-gpu-shadow-culling    Enable/disable GPU shadow caster culling\n"
           "  --gpu-shadow-indirect / --no-gpu-shadow-indirect  Enable/disable GPU indirect shadow draw\n"
           "  --async-gpu-light-list / --no-async-gpu-light-list  Request async light-list execution (capability gated)\n"
           "  --no-light-cluster                                Use the 16x16 FPTL path for transparent/forward-only\n"
           "  --asset-test=<path>                               Load a glTF scene, log its contents and exit\n"
           "  --scene=<path>                                    Load a glTF scene and render it interactively\n"
           "  --render-test=<path>                              Render a glTF scene for --frames frames and exit\n"
           "  --frames=<count>                                  Render-test frames; with --scene, stop after count "
           "frames\n"
           "  --render-scale=<0.1..1.0>                         Dynamic-resolution render scale (default: 1.0)\n"
           "  --lit-shader-mode=<forward|deferred>              Override the lit shader mode\n"
           "  --deferred                                        Alias for --lit-shader-mode=deferred\n"
            "  --ssao / --no-ssao                                Enable/disable deferred SSAO\n"
            "  --ssao-temporal / --no-ssao-temporal              Enable/disable SSAO history reprojection\n"
            "  --ssr / --no-ssr                                  Enable/disable deferred SSR\n"
            "  --ssr-temporal / --no-ssr-temporal                Enable/disable SSR history reprojection\n"
           "  --ssgi / --no-ssgi                                Enable/disable deferred SSGI\n"
           "  --decals / --no-decals                            Enable/disable deferred DBuffer decals\n"
           "  --reflection-probes / --no-reflection-probes      Enable/disable static reflection probes\n"
           "  --volumetric-fog / --no-volumetric-fog            Enable/disable screen-space volumetric fog\n"
           "  --fog-profile=<path>                              Load VolumetricFogSettings JSON\n"
           "  --fog-mean-free-path=<meters>                     Fog attenuation distance (default 400)\n"
           "  --fog-base-height=<meters>                        Height-fog base (default 0)\n"
           "  --fog-maximum-height=<meters>                     Height-fog layer top (default 50)\n"
           "  --fog-anisotropy=<value>                          Cornette-Shanks anisotropy (default 0; profile 0.5)\n"
           "  --fog-depth-extent=<meters>                       Ray-march distance (default 64)\n"
           "  --fog-steps=<4..64>                               Ray-march steps (default 16)\n"
           "  --fog-shadows / --no-fog-shadows                  Enable/disable directional fog shadows\n"
           "  --fog-resolution-scale=<0.25..1>                  Low-resolution fog scale (default 0.5)\n"
           "  --fog-scale=<0.25..1>                             Alias for --fog-resolution-scale\n"
           "  --fog-temporal / --no-fog-temporal                Enable/disable fog reprojection\n"
           "  --fog-temporal-weight=<0..0.95>                   Temporal history weight (default 0.85)\n"
           "  --fog-jitter / --no-fog-jitter                    Enable/disable temporal step jitter\n"
           "  --fog-punctual-lights / --no-fog-punctual-lights  Include point/spot light scattering\n"
           "  --fog-punctual-shadows / --no-fog-punctual-shadows Enable/disable point/spot fog shadows\n"
           "  --fog-local-volumes / --no-fog-local-volumes      Include sidecar local fog volumes\n"
           "  --fog-max-punctual-lights=<0..16>                 Max point/spot lights per fog sample (default 8)\n"
           "  --screenshot=<path>                               Write the last frame to a PNG file\n"
           "  --pipeline-settings=<path>                        Load PipelineSettings JSON\n"
           "  --frame-settings=<path>                           Load FrameSettings JSON\n"
           "  --postprocess-profile=<path>                      Load a PostProcessProfile JSON\n"
           "  --aa=<off|taa|fxaa>                               Select post-process antialiasing\n"
           "  --taa / --no-taa                                  Enable/disable temporal antialiasing\n"
           "  --taa-quality=<1..5>                              TAA quality; 3 is HDRP-like 0.875 history\n"
           "  --taa-motion-rejection=<0..1>                     TAA motion vector rejection\n"
           "  --taa-sharpening=<0..1>                           Post-TAA sharpen strength\n"
           "  --taa-jitter-scale=<0..1>                         Scale the Halton jitter\n"
           "  --fxaa / --no-fxaa                                Enable/disable FXAA\n"
           "  --bloom / --no-bloom                              Enable/disable bloom\n"
           "  --bloom-threshold=<value>                         Bloom threshold in gamma space\n"
           "  --bloom-intensity=<0..1>                          Bloom intensity (HDRP 2^x-1 mapping)\n"
           "  --bloom-scatter=<0..1>                            Bloom mip scatter\n"
           "  --bloom-resolution=<half|quarter>                 Bloom mip base resolution\n"
           "  --bloom-high-quality-prefilter / --no-bloom-high-quality-prefilter\n"
           "                                                    Toggle HDRP 13-tap prefilter\n"
           "  --bloom-high-quality-filter / --no-bloom-high-quality-filter\n"
           "                                                    Toggle bicubic bloom upsampling\n"
           "  --color-grading / --no-color-grading              Enable/disable the Uber color grading path\n"
           "  --dof / --no-dof                                  Enable/disable depth of field\n"
           "  --dof-focus-distance=<value>                      Manual focus distance (reserved; HDRP manual ranges use the fields below)\n"
           "  --dof-near-start=<value> / --dof-near-end=<value>  Near focus range in world units\n"
           "  --dof-far-start=<value> / --dof-far-end=<value>    Far focus range in world units\n"
           "  --dof-near-max-blur=<0..8> / --dof-far-max-blur=<0..16>\n"
           "                                                    Maximum blur radius in 1080p pixels\n"
           "  --dof-resolution=<full|half|quarter>               DOF processing resolution\n"
           "  --dof-high-quality / --no-dof-high-quality         Use 12-tap instead of 8-tap gather\n"
           "  --motion-blur / --no-motion-blur                  Enable/disable motion blur\n"
           "  --motion-blur-intensity=<0..2>                    Motion vector multiplier\n"
           "  --motion-blur-max-velocity=<0..1500>              Maximum velocity in pixels\n"
           "  --motion-blur-min-velocity=<0..64>                Minimum velocity in pixels\n"
           "  --motion-blur-camera / --no-motion-blur-camera    Include/exclude camera motion\n"
           "  --motion-blur-samples=<2..32>                     Maximum sample count (even)\n"
           "  --motion-blur-depth-extent=<0..20>                Depth comparison scale in world units\n"
           "  --sky=<path>                                      Load an HDR equirectangular sky\n"
           "  --sky-intensity=<value>                           Sky/IBL intensity (default: 1.0)\n"
           "  --sky-rotation=<degrees>                          Rotate the sky around Y (default: 0)\n"
           "  --exposure-mode=<fixed|auto|histogram>            Exposure mode (default: fixed)\n"
           "  --exposure-ev100=<value>                          Fixed exposure EV100 (default: -1.263)\n"
           "  --exposure-compensation=<ev>                      Exposure compensation in EV\n"
           "  --exposure-limit-min=<ev> / --exposure-limit-max=<ev>\n"
           "                                                    Automatic exposure EV100 limits\n"
           "  --tonemap=<aces|neutral|none>                     Tonemapping mode (default: aces)\n"
           "  --debug-exposure=<ev>                             Debug exposure override in EV\n"
           "  --help                                            Show this help\n";
}

} // namespace hbg
