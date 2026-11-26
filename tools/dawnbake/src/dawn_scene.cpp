#include "hbg/dawn/dawn_scene.h"

#include "asset/scene_loader.h"
#include "core/bounds.h"
#include "core/log.h"
#include "hbg/dawn/atlas_node.h"
#include "hbg/dawn/dawn_atlas.h"
#include "hbg/dawn/dawn_export.h"
#include "hbg/dawn/dawn_unwrap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hbg::dawn {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct SelectedObject {
    const render::RenderObject* object = nullptr;
    std::size_t meshInfoIndex = 0;
    uint32_t lightmapSize = 0;
};

struct ExportedLightMetadata {
    uint32_t sceneIndex = 0;
    render::LightType type = render::LightType::Directional;
    render::LightBakeMode bakeMode = render::LightBakeMode::Baked;
    render::LightMixedMode mixedMode = render::LightMixedMode::ShadowMask;
    bool castShadows = false;
    Bounds bounds;
};

bool boundsIntersect(const Bounds& a, const Bounds& b) {
    return a.valid() && b.valid() && a.min.x <= b.max.x && a.max.x >= b.min.x &&
           a.min.y <= b.max.y && a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

Bounds sceneBoundsToBounds(const SceneBoxBounds& bounds) {
    Bounds result;
    result.min = Vec3{bounds.min.x, bounds.min.y, bounds.min.z};
    result.max = Vec3{bounds.max.x, bounds.max.y, bounds.max.z};
    return result;
}

Bounds lightBoundsToBounds(const LightInfo& light) {
    Bounds result;
    result.min = Vec3{light.boundMin.x, light.boundMin.y, light.boundMin.z};
    result.max = Vec3{light.boundMax.x, light.boundMax.y, light.boundMax.z};
    return result;
}

BakingJobInputs makeSceneJobs(const DawnSceneExportOptions& options) {
    BakingJobInputs jobs;

    jobs.bakingParameters.bUseFastVoxelization = 0;

    auto& lightmap = jobs.bakingParameters.lightmap2DParameter;
    lightmap.denoiserMode = 0;
    lightmap.iterationNum = static_cast<int32_t>(options.samplesPerPixel);
    lightmap.maxDepth = static_cast<int32_t>(options.maxBounces);
    lightmap.superSampleFactor = 1;
    lightmap.maxLightmapResolution = static_cast<int32_t>(options.config.lightmap.maxLightmapSize);
    lightmap.penumbraShadowFraction = 0.3f;
    lightmap.skyOcclusionCompensateScale = 1.0f;
    lightmap.optixSmoothLevel = 0;
    lightmap.bGpuRaster = 0;
    lightmap.bUseLightmapDirection = options.config.lightmap.directional ? 1 : 0;
    lightmap.bSkyOcclusionDenoise = 0;

    auto& sdf = jobs.bakingParameters.sdfShadowParameter;
    sdf.maxUpsamplingFactor = 4;
    sdf.shadowRayFactor = 1.0f;
    sdf.denoiserMode = 0;
    sdf.shadowMaskSmoothLevel = 1;

    auto& shadow = jobs.shadowSettingParameter;
    shadow.approximateHighResTexelsPerMaxTransitionDistance = 1;
    shadow.maxTransitionDistanceWorldSpace = 0.5f;
    shadow.minDistanceFieldUpsampleFactor = 5;
    shadow.bAllowSignedDistanceFieldShadows = 1;

    auto& ray = jobs.bakingParameters.rayTracingParameter;
    ray.minNormalOffset = 0.5f;
    ray.maxRayOffset = 10.0f;
    ray.minRayOffset = 0.0001f;
    ray.normalOffsetSampleRadiusScale = 0.005f;
    ray.tangentOffsetSampleRadiusScale = 0.008f;

    auto& raster = jobs.bakingParameters.rasterizationParameter;
    raster.rasterizationBias = 1.0f;
    raster.smallestTexelRadius = 0.1f;

    jobs.bakingParameters.artifactParameter.bFillUnmappedTexel = 1;

    auto& bvh = jobs.bakingParameters.lightBvhParameter;
    bvh.directLightSampleMode = 2;
    bvh.maxPathIntensity = 50.0f;
    bvh.lightGridResolution = 256;
    bvh.maxLightCountPerGrid = 256;
    bvh.virtualLightDirectIntensity = 1.0f;
    bvh.virtualLightIndirectIntensity = 1.0f;
    bvh.virtualLightSampleMode = 1;

    return jobs;
}

bool materialHasBakedEmission(const asset::Material& material) {
    if (material.emissiveStrength <= 0.0f) {
        return false;
    }
    if (material.emissiveTexture.valid()) {
        return true;
    }
    return material.emissiveFactor.x > 0.0f || material.emissiveFactor.y > 0.0f ||
           material.emissiveFactor.z > 0.0f;
}

bool meshHasBakedEmission(const render::RenderScene& scene, const asset::Mesh& mesh) {
    if (mesh.submeshes.empty()) {
        return !scene.materials.empty() && materialHasBakedEmission(scene.materials.front());
    }
    for (const asset::SubMesh& submesh : mesh.submeshes) {
        if (submesh.materialIndex < scene.materials.size() &&
            materialHasBakedEmission(scene.materials[submesh.materialIndex])) {
            return true;
        }
    }
    return false;
}

bool objectHasBlendMaterial(const render::RenderScene& scene, const asset::Mesh& mesh) {
    if (mesh.submeshes.empty()) {
        return !scene.materials.empty() && scene.materials.front().alphaMode == asset::AlphaMode::Blend;
    }
    for (const asset::SubMesh& submesh : mesh.submeshes) {
        if (submesh.materialIndex >= scene.materials.size()) {
            return true;
        }
        if (scene.materials[submesh.materialIndex].alphaMode == asset::AlphaMode::Blend) {
            return true;
        }
    }
    return false;
}

void remapSubmeshes(const asset::Mesh& source, asset::Mesh& unwrapped) {
    if (source.submeshes.empty()) {
        unwrapped.submeshes.clear();
        return;
    }

    std::vector<asset::SubMesh> remapped;
    remapped.reserve(source.submeshes.size());
    uint32_t offset = 0;
    for (const asset::SubMesh& submesh : source.submeshes) {
        if (offset + submesh.indexCount > unwrapped.indices.size()) {
            throw std::runtime_error("xatlas output index count is smaller than the source submesh range");
        }
        remapped.push_back(asset::SubMesh{offset, submesh.indexCount, submesh.materialIndex, submesh.bounds});
        offset += submesh.indexCount;
    }
    if (offset != unwrapped.indices.size()) {
        throw std::runtime_error("xatlas output does not preserve the source index count");
    }
    unwrapped.submeshes = std::move(remapped);
}

void ensureTangents(asset::Mesh& mesh) {
    if (!mesh.hasNormals) {
        mesh.generateNormals();
    }
    if (!mesh.hasUv0) {
        for (asset::MeshVertex& vertex : mesh.vertices) {
            vertex.texcoord0 = mesh.hasUv1 ? vertex.texcoord1 : Vec2{0.0f, 0.0f};
        }
        mesh.hasUv0 = true;
    }
    if (!mesh.hasTangents) {
        mesh.generateTangents();
    }
    if (!mesh.hasTangents) {
        throw std::runtime_error("failed to generate tangents for mesh: " + mesh.name);
    }
}

asset::ImageAsset makeConstantSkyImage(const Vec3& color, const char* name) {
    asset::ImageAsset image;
    image.name = name;
    image.width = 2;
    image.height = 1;
    image.format = gfx::TextureFormat::RGBA32F;
    image.pixels.resize(sizeof(float) * 8u);
    float* pixels = reinterpret_cast<float*>(image.pixels.data());
    for (uint32_t pixel = 0; pixel < 2u; ++pixel) {
        pixels[pixel * 4u + 0u] = color.x;
        pixels[pixel * 4u + 1u] = color.y;
        pixels[pixel * 4u + 2u] = color.z;
        pixels[pixel * 4u + 3u] = 1.0f;
    }
    return image;
}

bool appendSky(SceneInfo& scene, const DawnSceneExportOptions& options, const render::SceneSky& sceneSky) {
    constexpr uint32_t kDefaultSkyCubeSize = 128u;
    const bool cliSky = !options.skyImage.empty();
    const bool sidecarSky = sceneSky.enabled && sceneSky.mode != render::SceneSkyMode::None;
    const bool active = cliSky || sidecarSky;

    SkyLightExportOptions skyOptions;
    skyOptions.cubeSize = kDefaultSkyCubeSize;
    skyOptions.indirectMultiplier = sceneSky.indirectMultiplier;
    skyOptions.indirectIntensity = sceneSky.indirectIntensity;
    skyOptions.shadowTranslucency = sceneSky.shadowTranslucency;

    const auto intensity = options.skyIntensitySet ? options.skyIntensity : sceneSky.intensity;
    const auto rotation = options.skyRotationSet ? options.skyRotationDegrees : sceneSky.rotationDegrees;
    skyOptions.intensity = intensity;

    if (!active) {
        asset::ImageAsset black = makeConstantSkyImage(Vec3{0.0f, 0.0f, 0.0f}, "blackSky");
        skyOptions.cubeSize = 8;
        skyOptions.srgbToLinear = false;
        skyOptions.color = Vec3{0.0f, 0.0f, 0.0f};
        scene.skyLight = exportSkyLight(&black, skyOptions);
        return false;
    }

    const render::SceneSkyMode mode = cliSky ? render::SceneSkyMode::Skybox : sceneSky.mode;
    if (mode == render::SceneSkyMode::Skybox) {
        const std::filesystem::path path = cliSky ? options.skyImage : sceneSky.texture;
        asset::ImageAsset sky;
        std::string error;
        if (!asset::TextureLoader::loadFromFile(path, sky, &error)) {
            throw std::runtime_error("failed to load sky image '" + path.string() + "': " + error);
        }
        skyOptions.color = sceneSky.color;
        skyOptions.splitRotation = true;
        skyOptions.sourceRotationEulerDegrees = sceneSky.adaptRotationEulerDegrees;
        skyOptions.transformRotationEulerDegrees = Vec3{0.0f, rotation, 0.0f};
        scene.skyLight = exportSkyLight(&sky, skyOptions);
    } else if (mode == render::SceneSkyMode::Trilight) {
        skyOptions.color = Vec3{1.0f, 1.0f, 1.0f};
        skyOptions.splitRotation = true;
        skyOptions.sourceRotationEulerDegrees = Vec3{0.0f, 0.0f, 0.0f};
        skyOptions.transformRotationEulerDegrees = sceneSky.adaptRotationEulerDegrees;
        scene.skyLight = exportGradientSkyLight(sceneSky.skyColor, sceneSky.equatorColor, sceneSky.groundColor,
                                                sceneSky.lerpExponent, skyOptions);
    } else {
        skyOptions.color = sceneSky.ambientColor;
        scene.skyLight = exportConstantSkyLight(Vec4{1.0f, 1.0f, 1.0f, 1.0f}, skyOptions);
    }
    return true;
}

void appendSkyLight(SceneInfo& scene, uint32_t instanceId) {
    const SkyLightInfo& sky = scene.skyLight;
    const float indirect = sky.skyData.indirectLightingIntensity;
    if (sky.skyData.color.x <= 0.0f && sky.skyData.color.y <= 0.0f && sky.skyData.color.z <= 0.0f &&
        indirect <= 0.0f) {
        return;
    }

    LightInfo light;
    light.flags = kLightFlagSky | kLightFlagCastShadow | kLightFlagDirectLighting;
    light.color = sky.skyData.color;
    light.attenuation = indirect;
    light.indirectColor = Float3{indirect, indirect, indirect};
    light.dimensions = Float3{static_cast<float>(sky.skyData.mipDimensions.x),
                              static_cast<float>(sky.skyData.mipDimensions.y),
                              static_cast<float>(sky.skyData.mipDimensions.z)};
    light.iesTextureSlice = -1;
    light.lightTextureMode = 0;
    light.shadowTranslucency = sky.skyData.shadowTranslucency;
    light.penumbraTint = Float4{0.0f, 0.0f, 0.0f, 0.0f};
    light.penumbraLuma = 0.0f;
    scene.lights.push_back(light);
    scene.lightGuids.push_back(Guid{0u, 0u, 0u, instanceId != 0u ? instanceId : 0x534B594Cu});
    scene.lightPowers.push_back(0.0f);
}

void setSceneBounds(SceneInfo& scene, const Bounds& bounds) {
    Bounds source = bounds;
    if (!source.valid()) {
        source = Bounds{Vec3{-1.0f, -1.0f, -1.0f}, Vec3{1.0f, 1.0f, 1.0f}};
    }
    const Vec3 center = source.center();
    const Vec3 extents = source.extents();
    const float radius = std::max(length(extents), 0.001f);
    scene.header.bounds = SceneBounds{Float3{center.x, center.y, center.z}, Float3{center.x, center.y, center.z},
                                     Float3{extents.x, extents.y, extents.z}, radius};
    scene.header.importanceBounds = scene.header.bounds;
}

}  // namespace

std::vector<SdfShadowGroupInput> packSdfShadowGroups(const std::vector<SdfShadowInput>& jobs, uint32_t packSize) {
    std::vector<SdfShadowGroupInput> groups;
    if (jobs.empty()) {
        return groups;
    }

    std::vector<std::size_t> sortedJobs(jobs.size());
    for (std::size_t index = 0; index < sortedJobs.size(); ++index) {
        sortedJobs[index] = index;
    }
    std::stable_sort(sortedJobs.begin(), sortedJobs.end(), [&](std::size_t lhs, std::size_t rhs) {
        const int32_t leftSide = std::max(jobs[lhs].header.size.x, jobs[lhs].header.size.y);
        const int32_t rightSide = std::max(jobs[rhs].header.size.x, jobs[rhs].header.size.y);
        if (leftSide != rightSide) {
            return leftSide < rightSide;
        }
        return lhs < rhs;
    });

    struct PackedGroup {
        explicit PackedGroup(uint32_t size) : root(static_cast<int>(size), static_cast<int>(size)) {}
        detail::AtlasNode root;
        std::vector<Int3> offsets;
        std::vector<int32_t> jobIndices;
    };
    std::vector<PackedGroup> packedGroups;
    std::vector<std::size_t> packableJobs;
    uint32_t groupIndex = 0;
    const auto makeSingleGroup = [&](std::size_t jobIndex) {
        const SdfShadowInput& shadow = jobs[jobIndex];
        SdfShadowGroupInput group;
        group.jobId.a = 102u;
        group.jobId.b = 1u;
        group.jobId.c = groupIndex++;
        group.jobId.d = 0xFFFFFFFFu;
        group.groupSize = std::max(shadow.header.size.x, shadow.header.size.y);
        group.upsampleFactor = 4;
        group.shadowJobIndices.push_back(static_cast<int32_t>(jobIndex));
        group.mappingOffsets.push_back(Int3{0, 0, static_cast<int32_t>(jobIndex)});
        groups.push_back(std::move(group));
    };

    for (std::size_t jobIndex : sortedJobs) {
        const SdfShadowInput& shadow = jobs[jobIndex];
        const int32_t jobPadding = (shadow.header.flags & kLightmapFlagPadding) != 0u ? 1 : 0;
        const uint64_t cellWidth =
            static_cast<uint64_t>(shadow.header.size.x) + 2ull * static_cast<uint64_t>(jobPadding);
        const uint64_t cellHeight =
            static_cast<uint64_t>(shadow.header.size.y) + 2ull * static_cast<uint64_t>(jobPadding);
        if (cellWidth >= packSize || cellHeight >= packSize) {
            makeSingleGroup(jobIndex);
        } else {
            packableJobs.push_back(jobIndex);
        }
    }

    for (std::size_t jobIndex : packableJobs) {
        const SdfShadowInput& shadow = jobs[jobIndex];
        const int32_t jobPadding = (shadow.header.flags & kLightmapFlagPadding) != 0u ? 1 : 0;
        const uint32_t cellWidth =
            static_cast<uint32_t>(shadow.header.size.x) + 2u * static_cast<uint32_t>(jobPadding);
        const uint32_t cellHeight =
            static_cast<uint32_t>(shadow.header.size.y) + 2u * static_cast<uint32_t>(jobPadding);
        if (packedGroups.empty()) {
            packedGroups.emplace_back(packSize);
        }
        PackedGroup* group = &packedGroups.back();
        detail::AtlasNodeRect rect;
        if (!group->root.insert(static_cast<int>(cellWidth), static_cast<int>(cellHeight), &rect)) {
            packedGroups.emplace_back(packSize);
            group = &packedGroups.back();
            if (!group->root.insert(static_cast<int>(cellWidth), static_cast<int>(cellHeight), &rect)) {
                continue;
            }
        }
        group->offsets.push_back(Int3{rect.x, rect.y, static_cast<int32_t>(jobIndex)});
        group->jobIndices.push_back(static_cast<int32_t>(jobIndex));
    }

    for (PackedGroup& packed : packedGroups) {
        SdfShadowGroupInput group;
        group.jobId.a = 102u;
        group.jobId.b = 1u;
        group.jobId.c = groupIndex++;
        group.jobId.d = 0xFFFFFFFFu;
        group.groupSize = static_cast<int32_t>(packSize);
        group.upsampleFactor = 4;
        group.shadowJobIndices = std::move(packed.jobIndices);
        group.mappingOffsets = std::move(packed.offsets);
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<SdfShadowGroupInput> packSdfShadowGroups(const std::vector<SdfShadowInput>& jobs, uint32_t packSize,
                                                     const std::vector<MappingGroupInput>& lightmapGroups) {
    std::vector<SdfShadowGroupInput> groups;
    if (jobs.empty()) {
        return groups;
    }
    (void)packSize;

    std::vector<uint8_t> grouped(jobs.size(), 0u);
    for (const MappingGroupInput& lightmapGroup : lightmapGroups) {
        for (const Int3& offset : lightmapGroup.mappingOffsets) {
            if (offset.z >= 0 && static_cast<std::size_t>(offset.z) < jobs.size()) {
                grouped[static_cast<std::size_t>(offset.z)] = 1u;
            }
        }
    }

    std::vector<std::size_t> sortedJobs(jobs.size());
    for (std::size_t index = 0; index < sortedJobs.size(); ++index) {
        sortedJobs[index] = index;
    }
    std::stable_sort(sortedJobs.begin(), sortedJobs.end(), [&](std::size_t lhs, std::size_t rhs) {
        const int32_t leftSide = std::max(jobs[lhs].header.size.x, jobs[lhs].header.size.y);
        const int32_t rightSide = std::max(jobs[rhs].header.size.x, jobs[rhs].header.size.y);
        if (leftSide != rightSide) {
            return leftSide < rightSide;
        }
        return lhs < rhs;
    });

    uint32_t groupIndex = 0;
    const auto makeSingleGroup = [&](std::size_t jobIndex) {
        const SdfShadowInput& shadow = jobs[jobIndex];
        SdfShadowGroupInput group;
        group.jobId.a = 102u;
        group.jobId.b = 1u;
        group.jobId.c = groupIndex++;
        group.jobId.d = 0xFFFFFFFFu;
        group.groupSize = std::max(shadow.header.size.x, shadow.header.size.y);
        group.upsampleFactor = 4;
        group.shadowJobIndices.push_back(static_cast<int32_t>(jobIndex));
        group.mappingOffsets.push_back(Int3{0, 0, static_cast<int32_t>(jobIndex)});
        groups.push_back(std::move(group));
    };

    for (std::size_t jobIndex : sortedJobs) {
        if (grouped[jobIndex] == 0u) {
            makeSingleGroup(jobIndex);
        }
    }

    std::vector<uint8_t> assigned(jobs.size(), 0u);
    for (const MappingGroupInput& lightmapGroup : lightmapGroups) {
        std::vector<Int3> offsets;
        std::vector<int32_t> jobIndices;
        for (const Int3& offset : lightmapGroup.mappingOffsets) {
            if (offset.z < 0 || static_cast<std::size_t>(offset.z) >= jobs.size() ||
                assigned[static_cast<std::size_t>(offset.z)] != 0u) {
                continue;
            }
            offsets.push_back(offset);
            jobIndices.push_back(offset.z);
            assigned[static_cast<std::size_t>(offset.z)] = 1u;
        }
        if (jobIndices.empty()) {
            continue;
        }
        SdfShadowGroupInput group;
        group.jobId.a = 102u;
        group.jobId.b = 1u;
        group.jobId.c = groupIndex++;
        group.jobId.d = 0xFFFFFFFFu;
        group.groupSize = lightmapGroup.groupedSize;
        group.upsampleFactor = 4;
        group.shadowJobIndices = std::move(jobIndices);
        group.mappingOffsets = std::move(offsets);
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<MappingGroupInput> packMappingGroups(const std::vector<Lightmap2DInput>& jobs, uint32_t packSize) {
    std::vector<MappingGroupInput> groups;
    std::vector<std::size_t> sortedJobs(jobs.size());
    for (std::size_t index = 0; index < sortedJobs.size(); ++index) {
        sortedJobs[index] = index;
    }
    std::stable_sort(sortedJobs.begin(), sortedJobs.end(), [&](std::size_t lhs, std::size_t rhs) {
        const int32_t leftSide = std::max(jobs[lhs].size.x, jobs[lhs].size.y);
        const int32_t rightSide = std::max(jobs[rhs].size.x, jobs[rhs].size.y);
        if (leftSide != rightSide) {
            return leftSide < rightSide;
        }
        return lhs < rhs;
    });

    struct PackedGroup {
        explicit PackedGroup(uint32_t size) : root(static_cast<int>(size), static_cast<int>(size)) {}
        detail::AtlasNode root;
        std::vector<Int3> offsets;
    };
    std::vector<PackedGroup> packedGroups;
    uint32_t groupIndex = 0;
    for (std::size_t jobIndex : sortedJobs) {
        const Lightmap2DInput& job = jobs[jobIndex];
        const int32_t jobPadding = (job.flags & kLightmapFlagPadding) != 0u ? 1 : 0;
        const uint64_t cellWidth =
            static_cast<uint64_t>(job.size.x) + 2ull * static_cast<uint64_t>(jobPadding);
        const uint64_t cellHeight =
            static_cast<uint64_t>(job.size.y) + 2ull * static_cast<uint64_t>(jobPadding);
        if (cellWidth >= packSize || cellHeight >= packSize) {
            continue;
        }
        if (packedGroups.empty()) {
            packedGroups.emplace_back(packSize);
        }
        PackedGroup* group = &packedGroups.back();
        detail::AtlasNodeRect rect;
        if (!group->root.insert(static_cast<int>(cellWidth), static_cast<int>(cellHeight), &rect)) {
            packedGroups.emplace_back(packSize);
            group = &packedGroups.back();
            if (!group->root.insert(static_cast<int>(cellWidth), static_cast<int>(cellHeight), &rect)) {
                continue;
            }
        }
        group->offsets.push_back(Int3{rect.x, rect.y, static_cast<int32_t>(jobIndex)});
    }

    for (PackedGroup& packed : packedGroups) {
        MappingGroupInput group;
        group.jobId.a = 0u;
        group.jobId.b = 0u;
        group.jobId.c = groupIndex++;
        group.jobId.d = 0xFFFFFFFFu;
        group.groupedSize = static_cast<int32_t>(packSize);
        group.mappingOffsets = std::move(packed.offsets);
        groups.push_back(std::move(group));
    }
    return groups;
}

std::vector<MappingSplitInput> splitOversizedLightmaps(const std::vector<Lightmap2DInput>& jobs, uint32_t packSize) {
    constexpr int32_t kLightmapSplitOverlap = 3;
    constexpr uint32_t kLightmapSplitJobType = 103u;
    std::vector<MappingSplitInput> splits;
    if (packSize <= static_cast<uint32_t>(2 * kLightmapSplitOverlap)) {
        return splits;
    }

    std::vector<std::size_t> sortedJobs(jobs.size());
    for (std::size_t index = 0; index < sortedJobs.size(); ++index) {
        sortedJobs[index] = index;
    }
    std::stable_sort(sortedJobs.begin(), sortedJobs.end(), [&](std::size_t lhs, std::size_t rhs) {
        const int32_t leftSide = std::max(jobs[lhs].size.x, jobs[lhs].size.y);
        const int32_t rightSide = std::max(jobs[rhs].size.x, jobs[rhs].size.y);
        if (leftSide != rightSide) {
            return leftSide > rightSide;
        }
        return lhs < rhs;
    });

    const int32_t splitSize = static_cast<int32_t>(packSize);
    const int32_t dataSize = splitSize - 2 * kLightmapSplitOverlap;
    uint32_t splitGroupIndex = 0;
    uint32_t splitJobIndex = 0;
    for (const std::size_t jobIndex : sortedJobs) {
        const Lightmap2DInput& job = jobs[jobIndex];
        const int32_t jobPadding = (job.flags & kLightmapFlagPadding) != 0u ? 1 : 0;
        const int32_t width = job.size.x + 2 * jobPadding;
        const int32_t height = job.size.y + 2 * jobPadding;
        if (width <= splitSize && height <= splitSize) {
            continue;
        }

        for (int32_t x = 0; x < width; x += dataSize) {
            for (int32_t y = 0; y < height; y += dataSize) {
                MappingSplitInput split;
                split.jobId = Guid{splitGroupIndex, kLightmapSplitJobType, splitJobIndex++,
                                   static_cast<uint32_t>(jobIndex)};
                split.splitSize = splitSize;
                split.overlapSize = kLightmapSplitOverlap;
                split.scenePartitionIndex = 0;
                split.mappingOffsets = Int3{x, y, static_cast<int32_t>(jobIndex)};
                splits.push_back(split);
                if (y + splitSize >= height) {
                    break;
                }
            }
            if (x + splitSize >= width) {
                break;
            }
        }
        ++splitGroupIndex;
    }
    return splits;
}

DawnScenePackage buildDawnScenePackage(const std::filesystem::path& scenePath,
                                       const DawnSceneExportOptions& options,
                                       render::RenderScene* preloadedScene) {
    if (scenePath.empty()) {
        throw std::runtime_error("scene export requires a scene path");
    }
    if (!std::filesystem::is_regular_file(scenePath)) {
        throw std::runtime_error("scene file not found: " + scenePath.string());
    }

    std::filesystem::path sidecar = options.sidecar;
    if (sidecar.empty()) {
        sidecar = scenePath.parent_path() / (scenePath.stem().string() + ".hdrp.json");
    }
    const bool sidecarExists = std::filesystem::is_regular_file(sidecar);
    if (!sidecarExists && !options.sidecar.empty()) {
        throw std::runtime_error("scene sidecar not found: " + sidecar.string());
    }

    asset::SceneLoadResult loaded;
    render::RenderScene* sourcePtr = preloadedScene;
    if (sourcePtr == nullptr) {
        asset::SceneLoadOptions loadOptions;
        loadOptions.sidecar = sidecar;
        loadOptions.loadSidecar = options.loadSidecar;
        loaded = asset::SceneLoader::load(scenePath, loadOptions);
        if (!loaded.ok()) {
            throw std::runtime_error("failed to load scene: " + loaded.error);
        }
        sourcePtr = loaded.scene.get();
    }
    render::RenderScene& source = *sourcePtr;
    source.recomputeBounds();

    if (source.materials.empty()) {
        asset::Material fallback;
        fallback.name = "DawnDefaultMaterial";
        source.materials.push_back(fallback);
    }

    bool hasExplicitGi = false;
    for (const render::RenderObject& object : source.objects) {
        hasExplicitGi = hasExplicitGi || object.hasContributeGI;
    }
    const bool includeAll = options.includeAllOpaque || !sidecarExists || !hasExplicitGi;
    if (includeAll && sidecarExists && !hasExplicitGi) {
        HBG_LOGW("scene '{}' has no contributeGI objects; exporting all opaque objects", scenePath.string());
    }

    std::vector<const render::RenderObject*> candidates;
    std::unordered_set<std::string> names;
    for (const render::RenderObject& object : source.objects) {
        if (object.meshIndex >= source.meshes.size()) {
            continue;
        }
        if (!includeAll && !object.contributeGI) {
            continue;
        }
        const asset::Mesh& mesh = source.meshes[object.meshIndex];
        if (mesh.vertices.empty() || mesh.indices.empty() || mesh.indices.size() % 3u != 0u) {
            continue;
        }
        if (includeAll && !object.contributeGI && objectHasBlendMaterial(source, mesh)) {
            continue;
        }
        if (object.name.empty()) {
            throw std::runtime_error("lightmap object has an empty name at index " +
                                     std::to_string(candidates.size()));
        }
        if (!names.insert(object.name).second) {
            throw std::runtime_error("lightmap object name is duplicated: " + object.name);
        }
        candidates.push_back(&object);
    }
    if (candidates.empty()) {
        throw std::runtime_error("no lightmap-compatible objects selected from " + scenePath.string());
    }

    DawnScenePackage package;
    package.scene.materials.reserve(source.materials.size());
    for (std::size_t index = 0; index < source.materials.size(); ++index) {
        MaterialInfo material = exportMaterial(source.materials[index], source, MaterialExportOptions{});
        material.header.indexInSceneInfo = static_cast<uint32_t>(index);
        package.scene.materials.push_back(std::move(material));
    }
    package.scene.header.numMaterials = static_cast<uint32_t>(package.scene.materials.size());

    std::unordered_map<uint32_t, std::size_t> meshInfoBySource;
    std::vector<SelectedObject> selected;
    selected.reserve(candidates.size());

    LightmapUvOptions uvOptions;
    uvOptions.texelsPerUnit = options.config.lightmap.texelsPerUnit;
    uvOptions.padding = options.config.lightmap.padding;
    uvOptions.bilinear = true;

    LightmapSizeSettings sizeSettings;
    sizeSettings.texelsPerUnit = options.config.lightmap.texelsPerUnit;
    sizeSettings.maxLightmapSize = options.config.lightmap.maxLightmapSize;

    std::vector<uint32_t> lightmapSizes;
    lightmapSizes.reserve(candidates.size());

    for (const render::RenderObject* object : candidates) {
        const asset::Mesh& sourceMesh = source.meshes[object->meshIndex];
        std::size_t meshIndex = 0;
        const auto existing = meshInfoBySource.find(object->meshIndex);
        if (existing == meshInfoBySource.end()) {
            asset::Mesh unwrapSource = sourceMesh;
            ensureTangents(unwrapSource);
            if (unwrapSource.vertices.empty() || unwrapSource.indices.empty()) {
                throw std::runtime_error("mesh is empty: " + sourceMesh.name);
            }

            LightmapUvLayout layout;
            if (!tryUnwrapLightmapUv(unwrapSource, uvOptions, layout)) {
                throw std::runtime_error("failed to generate UV2 for mesh: " + sourceMesh.name);
            }
            ensureTangents(layout.mesh);
            layout.mesh.hasUv1 = true;
            remapSubmeshes(unwrapSource, layout.mesh);

            MeshExportOptions meshOptions;
            meshOptions.normalizeUv2 = false;
            meshOptions.castShadows = true;
            const Guid meshGuid = makeMeshGuid(static_cast<uint32_t>(package.scene.meshes.size()));
            MeshInfo meshInfo = exportMesh(layout.mesh, 0, meshGuid, meshOptions);
            meshInfo.header.indexInSceneInfo = static_cast<uint32_t>(package.scene.meshes.size());
            meshIndex = package.scene.meshes.size();
            package.scene.meshes.push_back(std::move(meshInfo));
            meshInfoBySource.emplace(object->meshIndex, meshIndex);

            const int calculated = calculateLightmapSize(layout, options.scaleInLightmap, 0, sizeSettings);
            const uint32_t lightmapSize = static_cast<uint32_t>(std::max(calculated, 4));
            SelectedObject entry;
            entry.object = object;
            entry.meshInfoIndex = meshIndex;
            entry.lightmapSize = lightmapSize;
            selected.push_back(entry);
        } else {
            meshIndex = existing->second;
            uint32_t lightmapSize = 4;
            for (const SelectedObject& prior : selected) {
                if (prior.meshInfoIndex == meshIndex) {
                    lightmapSize = prior.lightmapSize;
                    break;
                }
            }
            SelectedObject entry;
            entry.object = object;
            entry.meshInfoIndex = meshIndex;
            entry.lightmapSize = lightmapSize;
            selected.push_back(entry);
        }
        lightmapSizes.push_back(selected.back().lightmapSize);
    }

    LightmapPackingSettings packingSettings;
    packingSettings.atlasSize = options.config.lightmap.atlasSize;
    packingSettings.maxAtlases = options.config.lightmap.maxAtlases;
    packingSettings.padding = options.config.lightmap.padding;
    LightmapPackingResult packing;
    if (!packLightmapAtlases(lightmapSizes, packingSettings, packing)) {
        throw std::runtime_error("failed to pack lightmap atlases; increase atlasSize or maxAtlases");
    }

    package.jobs = makeSceneJobs(options);
    package.jobs.lightmap2DJobs.clear();
    package.jobs.lightmap2DJobs.reserve(selected.size());
    package.jobs.partialJobsTotalNum.numLightmap2DJobs = static_cast<uint32_t>(selected.size());

    package.lightmap.texelPerUnit = options.config.lightmap.texelsPerUnit;
    for (std::size_t index = 0; index < packing.atlases.size(); ++index) {
        package.lightmap.atlases.push_back(LightmapSidecarAtlas{
            lightmapAtlasName(static_cast<uint32_t>(index)), packing.atlases[index].size,
            options.config.lightmap.layers, options.config.lightmap.format});
    }

    Bounds sceneBounds;
    for (std::size_t index = 0; index < selected.size(); ++index) {
        const render::RenderObject& object = *selected[index].object;
        const asset::Mesh& sourceMesh = source.meshes[object.meshIndex];
        const LightmapPlacement& placement = packing.placements[index];
        const uint32_t lightmapSize = selected[index].lightmapSize;

        MeshExportOptions instanceOptions;
        instanceOptions.normalizeUv2 = false;
        instanceOptions.castShadows = object.staticShadowCaster;
        instanceOptions.useEmissionLighting = meshHasBakedEmission(source, sourceMesh);
        instanceOptions.uv2MapSize = UInt2{lightmapSize, lightmapSize};
        instanceOptions.uv2Offset = UInt2{placement.rect.x, placement.rect.y};
        MeshInstanceInfo instance = exportMeshInstance(sourceMesh, object, static_cast<uint32_t>(selected[index].meshInfoIndex),
                                                       static_cast<uint32_t>(index), instanceOptions);
        instance.header.lightmapId = static_cast<uint32_t>(index);
        package.scene.meshInstances.push_back(instance);
        package.scene.meshGuids.push_back(instance.header.guid);

        Lightmap2DInput input;
        input.jobId = Guid{0u, 0u, static_cast<uint32_t>(index), 5u};
        input.meshId = instance.header.guid;
        input.size = Int2{static_cast<int32_t>(lightmapSize), static_cast<int32_t>(lightmapSize)};
        input.lightmapUVIndex = 1;
        std::memset(&input.overrideBakingParams, 0, sizeof(input.overrideBakingParams));
        input.flags = kLightmapFlagDirect | kLightmapFlagIndirect | kLightmapFlagBilinearFilter |
                      kLightmapFlagBmStatic;
        if (options.config.lightmap.padding > 0u) {
            input.flags |= kLightmapFlagPadding;
        }
        if (options.config.lightmap.directional) {
            input.flags |= kLightmapFlagDirection;
        }
        package.jobs.lightmap2DJobs.push_back(input);

        package.lightmap.objects.push_back(LightmapSidecarObject{
            object.name, placement.atlasIndex, placement.rect.x, placement.rect.y, placement.rect.size,
            placement.scale, true});

        if (object.worldBounds.valid()) {
            sceneBounds.expand(object.worldBounds);
        } else {
            sceneBounds.expand(transformBounds(object.world, sourceMesh.bounds));
        }
    }

    package.jobs.mappingGroupJobs =
        packMappingGroups(package.jobs.lightmap2DJobs, options.config.lightmap.taskPackSize);
    package.jobs.mappingGroupJobsCount = static_cast<uint32_t>(package.jobs.mappingGroupJobs.size());
    package.jobs.partialJobsTotalNum.numMappingGroupJobs = package.jobs.mappingGroupJobsCount;
    if (options.config.lightmap.splitLightmaps) {
        package.jobs.mappingSplitJobs =
            splitOversizedLightmaps(package.jobs.lightmap2DJobs, options.config.lightmap.taskPackSize);
    }
    package.jobs.mappingSplitJobsCount = static_cast<uint32_t>(package.jobs.mappingSplitJobs.size());
    package.jobs.partialJobsTotalNum.numMappingSplitJobs = package.jobs.mappingSplitJobsCount;

    package.scene.header.numMeshes = static_cast<uint32_t>(package.scene.meshes.size());
    package.scene.header.numMeshInstances = static_cast<uint32_t>(package.scene.meshInstances.size());
    package.scene.header.numMeshGuids = static_cast<uint32_t>(package.scene.meshGuids.size());
    setSceneBounds(package.scene, sceneBounds);

    const bool activeSky = appendSky(package.scene, options, source.sky);

    package.scene.lights.reserve(package.scene.lights.size() + source.lights.size() + 1u);
    package.scene.lightGuids.reserve(package.scene.lightGuids.size() + source.lights.size() + 1u);
    package.scene.lightPowers.reserve(package.scene.lightPowers.size() + source.lights.size() + 1u);
    std::vector<ExportedLightMetadata> lightMetadata;
    lightMetadata.reserve(source.lights.size());
    const auto appendLight = [&](std::size_t index) {
        const LightFullInfo light =
            exportLight(source.lights[index], LightExportOptions{},
                        static_cast<uint32_t>(index), &package.scene.cookieTextureDatas);
        ExportedLightMetadata metadata;
        metadata.sceneIndex = static_cast<uint32_t>(package.scene.lights.size());
        metadata.type = source.lights[index].type;
        metadata.bakeMode = source.lights[index].bakeMode;
        metadata.mixedMode = source.lights[index].mixedMode;
        metadata.castShadows = (light.lightData.flags & kLightFlagCastShadow) != 0u;
        metadata.bounds = lightBoundsToBounds(light.lightData);
        package.scene.lights.push_back(light.lightData);
        package.scene.lightGuids.push_back(light.lightGuid);
        package.scene.lightPowers.push_back(light.lightPower);
        lightMetadata.push_back(metadata);
    };
    for (std::size_t index = 0; index < source.lights.size(); ++index) {
        if (source.lights[index].type == render::LightType::Directional) {
            appendLight(index);
        }
    }
    if (activeSky) {
        appendSkyLight(package.scene, source.sky.instanceId);
    }
    for (std::size_t index = 0; index < source.lights.size(); ++index) {
        if (source.lights[index].type != render::LightType::Directional) {
            appendLight(index);
        }
    }

    const bool shadowMaskMode = std::any_of(
        lightMetadata.begin(), lightMetadata.end(), [](const ExportedLightMetadata& light) {
            return light.bakeMode == render::LightBakeMode::Mixed &&
                   light.mixedMode == render::LightMixedMode::ShadowMask;
        });
    if (shadowMaskMode) {
        for (std::size_t index = 0; index < package.scene.meshInstances.size(); ++index) {
            MeshInstanceInfo& instance = package.scene.meshInstances[index];
            const Bounds meshBounds = sceneBoundsToBounds(instance.header.boundingBox);
            bool directLighting = false;
            for (const ExportedLightMetadata& light : lightMetadata) {
                if (light.bakeMode == render::LightBakeMode::Realtime) {
                    continue;
                }
                const bool relevant = light.type == render::LightType::Directional ||
                                      boundsIntersect(meshBounds, light.bounds);
                if (!relevant) {
                    continue;
                }
                instance.relevantLights.push_back(light.sceneIndex);
                if (light.bakeMode == render::LightBakeMode::Baked) {
                    directLighting = true;
                }
            }
            if (!directLighting) {
                package.jobs.lightmap2DJobs[index].flags &= ~kLightmapFlagDirect;
            }
        }

        for (std::size_t index = 0; index < package.scene.meshInstances.size(); ++index) {
            const MeshInstanceInfo& instance = package.scene.meshInstances[index];
            const Lightmap2DInput& lightmapJob = package.jobs.lightmap2DJobs[index];
            const Bounds meshBounds = sceneBoundsToBounds(instance.header.boundingBox);

            SdfShadowInput shadow;
            shadow.header.jobId = instance.header.guid;
            shadow.header.jobId.b = 1u;
            shadow.header.jobId.c = static_cast<uint32_t>(index);
            shadow.header.meshId = instance.header.guid;
            shadow.header.flags = kLightmapFlagBilinearFilter;
            if (options.config.lightmap.padding > 0u) {
                shadow.header.flags |= kLightmapFlagPadding;
            }
            shadow.header.size = lightmapJob.size;
            shadow.header.upsampleFactor = 4;
            shadow.header.lightmapUVIndex = 1;
            for (const ExportedLightMetadata& light : lightMetadata) {
                if (light.bakeMode != render::LightBakeMode::Mixed || !light.castShadows) {
                    continue;
                }
                if (light.type != render::LightType::Directional &&
                    !boundsIntersect(meshBounds, light.bounds)) {
                    continue;
                }
                const Guid& guid = package.scene.lightGuids[light.sceneIndex];
                shadow.lights.push_back(guid);
                shadow.shadowmaskLights.push_back(guid);
            }
            package.jobs.sdfShadowJobs.push_back(std::move(shadow));
        }

        package.jobs.sdfShadowGroupJobs =
            packSdfShadowGroups(package.jobs.sdfShadowJobs, options.config.lightmap.taskPackSize,
                                package.jobs.mappingGroupJobs);
        package.jobs.sdfShadowJobsCount = static_cast<uint32_t>(package.jobs.sdfShadowJobs.size());
        package.jobs.sdfShadowGroupJobsCount = static_cast<uint32_t>(package.jobs.sdfShadowGroupJobs.size());
        package.jobs.partialJobsTotalNum.numSdfShadowJobs = package.jobs.sdfShadowJobsCount;
        package.jobs.partialJobsTotalNum.numSdfShadowGroupJobs = package.jobs.sdfShadowGroupJobsCount;
    }
    package.scene.header.numLights = static_cast<uint32_t>(package.scene.lights.size());

    package.objectCount = selected.size();
    package.meshCount = package.scene.meshes.size();
    package.atlasCount = package.lightmap.atlases.size();
    return package;
}

}  // namespace hbg::dawn
