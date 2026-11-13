#pragma once

#include "render/postprocess/post_process_settings.h"

#include <cstddef>
#include <cstdint>

namespace hbg::render {

enum class DebugDisplayMode : uint8_t {
    None,
    LightData,
    Normal,
    MotionVectors,
    Depth,
    DepthPyramid,
    LightCluster,
    ShadowAtlasDirectional,
    ShadowAtlasPunctual,
    Albedo,
    Roughness,
    Metallic,
    AmbientOcclusion,
    Emissive,
    SSAO,
    SSR,
    SSGI,
    Decals,
    Exposure,
    ColorGrading,
    Bloom,
    TaaHistory,
    Dof,
    Fxaa,
    ContactShadows,
    ShadowCascade,
    ShadowFilter,
};

struct DebugDisplaySettings {
    bool shadows = true;
    bool wireframe = false;
    bool gpuLightList = true;
    bool freezeTime = false;
    bool showPanel = true;
    float renderScale = 1.0f;
    DebugDisplayMode fullscreen = DebugDisplayMode::None;
    int lightClusterSlice = -1;
    PostProcessSettings postProcess;
};

const char* debugDisplayModeName(DebugDisplayMode mode);
std::size_t debugDisplayModeCount();
const char* debugDisplayModeNameAt(std::size_t index);
DebugDisplayMode debugDisplayModeAt(std::size_t index);

} // namespace hbg::render
