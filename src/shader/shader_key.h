#pragma once

#include <cstddef>
#include <cstdint>

namespace hbg::shader {

enum class PassId : uint8_t {
    Forward,
    ForwardInstanced,
    Tonemap,
    PostProcessExposure,
    PostProcessExposurePrepass,
    PostProcessExposureReduce,
    PostProcessExposureReduceFinal,
    PostProcessHistogramClear,
    PostProcessHistogramGen,
    PostProcessHistogramReduce,
    ApplyExposure,
    DebugExposure,
    Final,
    UberPost,
    DebugColorGrading,
    BloomPrefilter,
    BloomBlur,
    BloomDownsample,
    BloomUpsample,
    DebugBloom,
    Taa,
    TaaSharpen,
    DebugTaaHistory,
    DofCoc,
    DofGather,
    DofCombine,
    DebugDofCoc,
    DebugFxaa,
    MotionBlur,
    Shadow,
    ShadowInstanced,
    ShadowCull,
    Prepass,
    MotionVectors,
    CameraMotionVectors,
    DepthPyramidCopy,
    DepthPyramidDownsample,
    RiverDepthPack,
    Sky,
    Atmosphere,
    VolumetricFog,
    VolumetricFogComposite,
    DebugLightData,
    DebugNormal,
    DebugMotionVectors,
    DebugDepth,
    DebugDepthPyramid,
    DebugLightCluster,
    DebugShadowAtlas,
    DebugColor,
    DebugAlbedo,
    DebugRoughness,
    DebugGBuffer,
    Wireframe,
    GBuffer,
    GBufferClear,
    GBufferDecals,
    Decal,
    DecalClear,
    DeferredLighting,
    DeferredLightingAdvanced,
    WaterSky,
    SSAO,
    SSAOBlur,
    SSAOTemporal,
    SSAOUpsample,
    DebugSSAO,
    SSR,
    SSRTemporal,
    SSRComposite,
    DebugSSR,
    DebugDecals,
    SSGI,
    SSGIDenoise,
    SSGITemporal,
    SSGIComposite,
    DebugSSGI,
    DebugSSGIRaw,
    ContactShadows,
    LightScreenAABB,
    LightBigTile,
    LightFptl,
    LightCluster,
    LightScreenAABBBuffer,
    LightBigTileBuffer,
    LightFptlBuffer,
    LightClusterBuffer,
    LightIndirectArgs,
    DebugContactShadows,
    DebugShadowCascade,
    DebugShadowFilter,
    Ui,
};

enum MaterialVariant : uint32_t {
    MaterialVariantNone = 0,
    MaterialVariantNormalMap = 1u << 0,
    MaterialVariantMaskMap = 1u << 1,
    MaterialVariantAlphaClip = 1u << 2,
    MaterialVariantDoubleSided = 1u << 3,
    MaterialVariantWaterFft = 1u << 4,
    MaterialVariantTransparent = 1u << 5,
};

constexpr uint32_t kMaterialVariantMask = MaterialVariantNormalMap | MaterialVariantMaskMap |
                                           MaterialVariantAlphaClip | MaterialVariantDoubleSided |
                                           MaterialVariantWaterFft;

constexpr uint64_t kLightListBufferProgramKey = 1ull << 63;
constexpr uint64_t kLightLayersProgramKey = 1ull << 62;
constexpr uint64_t kTerrainTilesProgramKey = 1ull << 61;
constexpr uint64_t kTerrainDeformProgramKey = 1ull << 60;
constexpr uint64_t kTerrainInstancedProgramKey = 1ull << 59;
constexpr uint64_t kObjectInstancedProgramKey = 1ull << 58;
constexpr uint64_t kRiverParticleProgramKey = 1ull << 57;
constexpr uint32_t kMaterialVariantCount = 32;

struct ProgramKey {
    PassId pass = PassId::Forward;
    uint64_t materialKey = 0;
    uint64_t pipelineKey = 0;

    friend bool operator==(const ProgramKey&, const ProgramKey&) = default;
};

struct ProgramKeyHash {
    std::size_t operator()(const ProgramKey& key) const noexcept {
        std::size_t seed = static_cast<std::size_t>(key.pass);
        seed ^= static_cast<std::size_t>(key.materialKey) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        seed ^= static_cast<std::size_t>(key.pipelineKey) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        return seed;
    }
};

} // namespace hbg::shader
