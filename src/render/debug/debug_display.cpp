#include "render/debug/debug_display.h"

namespace hbg::render {
namespace {

constexpr DebugDisplayMode kModes[] = {
    DebugDisplayMode::None,
    DebugDisplayMode::LightData,
    DebugDisplayMode::Normal,
    DebugDisplayMode::MotionVectors,
    DebugDisplayMode::Depth,
    DebugDisplayMode::DepthPyramid,
    DebugDisplayMode::LightCluster,
    DebugDisplayMode::ShadowAtlasDirectional,
    DebugDisplayMode::ShadowAtlasPunctual,
    DebugDisplayMode::Albedo,
    DebugDisplayMode::Roughness,
    DebugDisplayMode::Metallic,
    DebugDisplayMode::AmbientOcclusion,
    DebugDisplayMode::Emissive,
    DebugDisplayMode::SSAO,
    DebugDisplayMode::SSAORaw,
    DebugDisplayMode::SSAOHistory,
    DebugDisplayMode::SSR,
    DebugDisplayMode::SSRRaw,
    DebugDisplayMode::SSRHistory,
    DebugDisplayMode::SSGI,
    DebugDisplayMode::SSGIRaw,
    DebugDisplayMode::SSGIHistory,
    DebugDisplayMode::Decals,
    DebugDisplayMode::Exposure,
    DebugDisplayMode::ColorGrading,
    DebugDisplayMode::Bloom,
    DebugDisplayMode::TaaHistory,
    DebugDisplayMode::Dof,
    DebugDisplayMode::Fxaa,
    DebugDisplayMode::ContactShadows,
    DebugDisplayMode::ShadowCascade,
    DebugDisplayMode::ShadowFilter,
};

constexpr const char* kModeNames[] = {
    "Off",
    "Light Data Texture",
    "Normal Buffer",
    "Motion Vectors",
    "Depth",
    "Depth Pyramid",
    "Light Cluster (Tiles)",
    "Directional Shadow Atlas",
    "Punctual Shadow Atlas",
    "Albedo",
    "Roughness",
    "Metallic",
    "Ambient Occlusion",
    "Emissive",
    "Screen Space Ambient Occlusion (Denoised)",
    "Screen Space Ambient Occlusion Raw",
    "Screen Space Ambient Occlusion History",
    "Screen Space Reflections (Denoised)",
    "Screen Space Reflections Raw",
    "Screen Space Reflections History",
    "Screen Space Global Illumination (Denoised)",
    "Screen Space Global Illumination Raw",
    "Screen Space Global Illumination History",
    "Decals",
    "Exposure",
    "Color Grading LUT",
    "Bloom",
    "TAA History",
    "Depth of Field CoC",
    "FXAA Edges",
    "Contact Shadows",
    "Shadow Cascade",
    "Shadow Filter",
};

static_assert(sizeof(kModes) / sizeof(kModes[0]) == sizeof(kModeNames) / sizeof(kModeNames[0]));

} // namespace

const char* debugDisplayModeName(DebugDisplayMode mode) {
    for (std::size_t index = 0; index < debugDisplayModeCount(); ++index) {
        if (kModes[index] == mode) {
            return kModeNames[index];
        }
    }
    return kModeNames[0];
}

std::size_t debugDisplayModeCount() {
    return sizeof(kModes) / sizeof(kModes[0]);
}

const char* debugDisplayModeNameAt(std::size_t index) {
    return index < debugDisplayModeCount() ? kModeNames[index] : kModeNames[0];
}

DebugDisplayMode debugDisplayModeAt(std::size_t index) {
    return index < debugDisplayModeCount() ? kModes[index] : DebugDisplayMode::None;
}

} // namespace hbg::render
