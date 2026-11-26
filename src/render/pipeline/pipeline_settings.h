#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hbg::render {

enum class SupportedLitShaderMode : uint8_t {
    ForwardOnly = 1 << 0,
    DeferredOnly = 1 << 1,
    Both = (1 << 0) | (1 << 1),
};

enum class ColorBufferFormat : uint8_t {
    R11G11B10F,
    R16G16B16A16F,
};

enum class DepthBufferFormat : uint8_t {
    Auto,
    D24S8,
    D32F,
    D16,
};

enum class MSAASamples : uint8_t {
    None = 1,
    MSAA2X = 2,
    MSAA4X = 4,
    MSAA8X = 8,
};

struct PipelineSettings {
    SupportedLitShaderMode supportedLitShaderMode = SupportedLitShaderMode::ForwardOnly;
    ColorBufferFormat colorBufferFormat = ColorBufferFormat::R11G11B10F;
    DepthBufferFormat depthBufferFormat = DepthBufferFormat::Auto;
    MSAASamples msaaSampleCount = MSAASamples::None;

    bool supportShadows = true;
    bool supportShadowMask = false;
    bool supportGpuLightList = true;
    bool supportGpuLightListBuffers = true;
    bool supportGpuLightListIndirect = false;
    bool supportGpuSkinning = true;
    bool supportDepthPyramidCompute = true;
    bool supportGpuDrivenGeometry = false;
    bool supportLightListFeatureFlags = true;
    bool supportLitVariants = false;
    bool supportLitVariantTiles = false;
    bool supportCookieAtlas = true;
    uint32_t cookieAtlasSize = 1024;
    uint32_t lightListCapacity = 64;
    bool supportGpuShadowCulling = false;
    bool supportGpuShadowIndirect = false;
    bool supportAsyncGpuLightList = false;
    bool supportContactShadows = false;
    bool supportScreenSpaceShadows = false;
    bool supportSSR = false;
    bool supportSSRTransparent = false;
    bool supportSSAO = false;
    bool supportSSGI = false;
    bool supportSubsurfaceScattering = false;
    bool supportVolumetrics = false;
    bool supportVolumetricClouds = false;
    bool supportMotionVectors = true;
    bool supportTAA = false;
    bool supportBloom = false;
    bool supportColorGrading = false;
    bool supportDepthOfField = false;
    bool supportMotionBlur = false;
    bool supportFXAA = false;
    bool supportExposureHistogram = false;
    bool supportTransparentBackface = false;
    bool supportTransparentDepthPrepass = false;
    bool supportTransparentRefraction = false;
    bool supportRefractionDepthRejection = true;
    bool supportRefractionRoughness = false;
    bool supportTransparentDepthPostpass = false;
    bool supportLowResTransparent = false;
    bool supportDecals = false;
    bool supportDecalLayers = false;
    bool supportDistortion = false;
    bool supportLightLayers = false;
    bool supportWater = false;
    bool supportWaterDecals = false;
    bool supportWaterExclusion = false;
    bool supportRayTracing = false;
    bool supportScreenSpaceLensFlare = false;
    bool supportDataDrivenLensFlare = false;
    bool supportCustomPass = false;
    bool supportVFX = false;
    bool supportVFXInstancing = false;
    bool supportVFXTransparentSort = false;
    uint32_t vfxInstanceBatchSize = 0;
    bool supportVariableRateShading = false;
    bool supportVirtualTexturing = false;
    bool supportProbeVolume = false;
    bool supportHighQualityLineRendering = false;
    bool supportComputeThickness = false;
    bool supportAsyncCompute = false;
    bool renderingLayerMaskBuffer = false;

    static PipelineSettings defaults();
};

bool parsePipelineSettings(std::string_view json, PipelineSettings& settings, std::string& error);
bool loadPipelineSettings(const std::string& path, PipelineSettings& settings, std::string& error);
std::string pipelineSettingsToJson(const PipelineSettings& settings);

} // namespace hbg::render
