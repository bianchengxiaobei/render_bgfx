#pragma once

#include <cstddef>
#include <cstdint>

namespace hbg::shader {

enum class PassId : uint8_t {
    Forward,
    ForwardAdvanced,
    ForwardInstanced,
    BindlessForwardInstanced,
    BakedGI,
    BakedGICompensate,
    BakedGIForwardComposite,
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
    GpuDrivenCull,
    GpuDrivenTransparentSort,
    ShadowMoments,
    ShadowSatHorizontal,
    ShadowSatVertical,
    Prepass,
    MotionVectors,
    TransparentMotionVectors,
    CameraMotionVectors,
    DepthPyramidCopy,
    DepthPyramidDownsample,
    DepthPyramidCompute,
    DepthPyramidMaxCompute,
    Sky,
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
    DebugLightmap,
    DebugAlbedo,
    DebugRoughness,
    DebugGBuffer,
    Wireframe,
    GBuffer,
    GBufferInstanced,
    GBufferClear,
    GBufferDecals,
    Decal,
    DecalClear,
    DeferredLighting,
    DeferredLightingAdvanced,
    DeferredLightingAdvancedIms,
    DeferredLightingVariant,
    DeferredLightingVariantIms,
    DeferredLightingVariantCompute,
    DeferredLightingVariantComputeIms,
    LitMaterialFlags,
    LitVariantIndirectClear,
    LitVariantIndirect,
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
    ShadowEvsmBlur,
    ShadowEvsmBlurMoments,
    ShadowCopyDepth,
    Skinning,
};

enum MaterialVariant : uint32_t {
    MaterialVariantNone = 0,
    MaterialVariantNormalMap = 1u << 0,
    MaterialVariantMaskMap = 1u << 1,
    MaterialVariantAlphaClip = 1u << 2,
    MaterialVariantDoubleSided = 1u << 3,
    MaterialVariantTransparent = 1u << 4,
};

constexpr uint32_t kMaterialVariantMask =
    MaterialVariantNormalMap | MaterialVariantMaskMap | MaterialVariantAlphaClip | MaterialVariantDoubleSided;

constexpr uint32_t kDeferredLightingVariantCount = 29;
constexpr uint64_t kLightListBufferProgramKey = 1ull << 63;
constexpr uint64_t kLightLayersProgramKey = 1ull << 62;
constexpr uint32_t kMaterialVariantCount = 16;

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
