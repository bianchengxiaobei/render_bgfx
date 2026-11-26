#pragma once

#include "hbg/dawn/dawn_config.h"
#include "hbg/dawn/dawn_package.h"
#include "hbg/dawn/dawn_protocol.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace hbg::render {
struct RenderScene;
}

namespace hbg::dawn {

struct DawnSceneExportOptions {
    DawnConfig config;
    std::filesystem::path sidecar;
    std::filesystem::path skyImage;
    bool loadSidecar = true;
    bool includeAllOpaque = false;
    float scaleInLightmap = 1.0f;
    float skyIntensity = 1.0f;
    bool skyIntensitySet = false;
    float skyRotationDegrees = 0.0f;
    bool skyRotationSet = false;
    uint32_t samplesPerPixel = 1024;
    uint32_t maxBounces = 4;
    std::filesystem::path blenderExecutable;
    uint32_t blenderTimeoutSeconds = 600;
};

struct DawnScenePackage {
    SceneInfo scene;
    BakingJobInputs jobs;
    LightmapSidecar lightmap;
    std::size_t objectCount = 0;
    std::size_t meshCount = 0;
    std::size_t atlasCount = 0;
};

DawnScenePackage buildDawnScenePackage(const std::filesystem::path& scenePath, const DawnSceneExportOptions& options,
                                       render::RenderScene* preloadedScene = nullptr);

std::vector<SdfShadowGroupInput> packSdfShadowGroups(const std::vector<SdfShadowInput>& jobs, uint32_t packSize);

std::vector<SdfShadowGroupInput> packSdfShadowGroups(const std::vector<SdfShadowInput>& jobs, uint32_t packSize,
                                                     const std::vector<MappingGroupInput>& lightmapGroups);

std::vector<MappingGroupInput> packMappingGroups(const std::vector<Lightmap2DInput>& jobs, uint32_t packSize);

std::vector<MappingSplitInput> splitOversizedLightmaps(const std::vector<Lightmap2DInput>& jobs, uint32_t packSize);

}  // namespace hbg::dawn
