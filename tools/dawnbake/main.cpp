#include "hbg/dawn/dawn_atlas.h"
#include "hbg/dawn/dawn_config.h"
#ifdef HBG_DAWN_GRPC
#include "hbg/dawn/dawn_agent_client.h"

#include "Agent.grpc.pb.h"

#include <grpcpp/grpcpp.h>

#ifdef GetMessage
#undef GetMessage
#endif
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif
#endif
#include "hbg/dawn/dawn_export.h"
#include "hbg/dawn/dawn_gzip.h"
#include "hbg/dawn/dawn_launch.h"
#include "hbg/dawn/dawn_package.h"
#include "hbg/dawn/dawn_scene.h"
#if defined(HBG_DAWN_HAVE_PARCEL) && HBG_DAWN_HAVE_PARCEL
#include "hbg/dawn/dawn_parcel.h"
#endif
#include "hbg/dawn/dawn_serialize.h"
#include "hbg/dawn/dawn_unwrap.h"
#include "hbg/dawn/dawn_uv_dump.h"
#include "hbg/dawn/lightmap_atlas.h"
#include "hbg/dawn/lightmap_dds.h"
#include "hbg/dawn/lightmap_result.h"
#include "hbg/dawn/shadowmask_result.h"

#include "asset/mesh.h"
#include "render/scene/render_scene.h"

#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/math.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace hbg::dawn;
using namespace hbg::asset;
using namespace hbg::render;

namespace asset = hbg::asset;
namespace render = hbg::render;
namespace gfx = hbg::gfx;

struct SizeCheck {
    const char* name;
    size_t actual;
    size_t expected;
};

void printUsage() {
    std::cout << "hbg_dawnbake - Dawn GI protocol tool\n\n"
              << "Usage:\n"
              << "  hbg_dawnbake --proto-test\n"
              << "  hbg_dawnbake --mesh-test\n"
              << "  hbg_dawnbake --material-test\n"
              << "  hbg_dawnbake --light-test\n"
              << "  hbg_dawnbake --sky-test\n"
              << "  hbg_dawnbake --uv-test\n"
              << "  hbg_dawnbake --atlas-test\n"
              << "  hbg_dawnbake --package-test\n"
              << "  hbg_dawnbake --process-test\n"
              << "  hbg_dawnbake --run-dawn <job-dir> <guid32hex> [--dawn <Dawn.exe>] [--timeout <seconds>] [--log "
                 "<file>] [--keep-job]\n"
              << "  hbg_dawnbake --run-dawn <guid32hex> --job-dir <dir> [--dawn <Dawn.exe>] [--timeout <seconds>]\n"
              << "  hbg_dawnbake --bake <job-dir> <guid32hex> [--dawn <Dawn.exe>] [--timeout <seconds>] "
                 "[--result-grace <ms>] [--keep-job] [--atlas-out <dir>]\n"
              << "  hbg_dawnbake --bake <guid32hex> --job-dir <dir> [--dawn <Dawn.exe>] [--timeout <seconds>] "
                 "[--atlas-out <dir>]\n"
              << "  hbg_dawnbake --bake <job-dir> <guid32hex> --agent <http://127.0.0.1:5009> "
                 "[--agent-files <http://127.0.0.1:5011>] [--agent-download-results] [--agent-force-download] "
                 "[--atlas-out <dir>]\n"
              << "  hbg_dawnbake --agent-upload <agent-address> <job-id> <local-file> <remote-path>\n"
              << "  hbg_dawnbake --agent-cache-upload <agent-address> <job-id> <hash> <remote-path> "
                 "[<hash> <remote-path> ...]\n"
              << "  hbg_dawnbake --agent-multi-upload <agent-address> <job-id> <hash> <remote-path> <local-file> "
                 "[<hash> <remote-path> <local-file> ...]\n"
              << "  hbg_dawnbake --agent-transfer-job <agent-address> <job-id> <another-agent-address> "
                 "<remote-path> ...\n"
              << "  hbg_dawnbake --agent-download <agent-address> <job-id> <remote-path> <local-file>\n"
              << "  hbg_dawnbake --agent-spec-test\n"
              << "  hbg_dawnbake --agent-upload-test\n"
              << "  hbg_dawnbake --agent-cache-test\n"
              << "  hbg_dawnbake --agent-multi-upload-test\n"
              << "  hbg_dawnbake --agent-transfer-test\n"
              << "  hbg_dawnbake --agent-download-test\n"
              << "  hbg_dawnbake --agent-multi-download-test\n"
              << "  hbg_dawnbake --agent-download-files-test\n"
              << "  hbg_dawnbake --result-test <fake-dawn.exe>\n"
              << "  hbg_dawnbake --job-lifecycle-test\n"
              << "  hbg_dawnbake --dump-uv=<out.png>\n"
              << "  hbg_dawnbake --golden-test [dir]\n"
              << "  hbg_dawnbake --export-min <dir> [guid32hex]\n"
              << "  hbg_dawnbake --export-t2 <dir> [guid32hex]\n"
              << "  hbg_dawnbake --export-t1 <dir> [guid32hex]\n"
              << "  hbg_dawnbake --export-falloff <dir> [guid32hex]\n"
              << "  hbg_dawnbake --export-t1-color <dir> [guid32hex]\n"
              << "  hbg_dawnbake --export-t3-tile <dir> <tile-index-0..3> [guid32hex]\n"
              << "  hbg_dawnbake --export-t6 <dir> [guid32hex]\n"
              << "  hbg_dawnbake --export-scene <scene> <out-dir> [guid32hex] [--sidecar <hdrp.json>] "
                 "[--config <dawn.json>] [--sky <hdr|exr>] [--sky-intensity <v>] [--sky-rotation <deg>] "
                 "[--blender <exe>] [--blender-timeout <seconds>] [--all-objects] [--samples <n>] [--bounces <n>]\n"
              << "  hbg_dawnbake --scene=<scene> --out=<dir> [--sidecar=<hdrp.json>] "
                 "[--config=<dawn.json>] [--sky=<hdr|exr>] [--sky-intensity=<v>] [--sky-rotation=<deg>] "
                 "[--blender=<exe>] [--blender-timeout=<seconds>] [--guid=<guid32hex>] [--all-objects] "
                 "[--samples=<n>] [--bounces=<n>]\n"
              << "  scene: .gltf/.glb, Parcel/Assimp .fbx/.obj/.dae/.3ds/.ply/.stl, Blender .blend\n"
              << "  hbg_dawnbake --t6-test\n"
              << "  hbg_dawnbake --scene-export-test\n"
#if defined(HBG_DAWN_HAVE_PARCEL) && HBG_DAWN_HAVE_PARCEL
              << "  hbg_dawnbake --parcel-test\n"
#endif
              << "  hbg_dawnbake --dawn-config-test [path]\n"
              << "  hbg_dawnbake --protocol-version-test\n"
              << "  hbg_dawnbake --write-tile-atlas <out-dir> <atlas-size> <edge-padding> "
                 "<file.lmgz> <x> <y> ...\n"
              << "  hbg_dawnbake --write-job-atlas <job-dir> <guid32hex> <lightmap.json> <out-dir> "
                 "[edge-padding]\n"
              << "  hbg_dawnbake --verify-job-atlas <job-dir> <guid32hex> <lightmap.json> <atlas-dir>\n"
              << "  hbg_dawnbake --write-lightmap-dds <file.lmgz> <out-dir> <atlas-size> <x> <y>\n"
              << "  hbg_dawnbake --write-empty <dir> <guid32hex>\n"
              << "  hbg_dawnbake --write-min <dir> <guid32hex>\n"
              << "  hbg_dawnbake --roundtrip <file.scene|scenegz> <file.jobs|jobgz>\n"
              << "  hbg_dawnbake --lmgz-test\n"
              << "  hbg_dawnbake --lmgz-bind-test\n"
              << "  hbg_dawnbake --sdfgz-test\n"
              << "  hbg_dawnbake --split-test\n"
              << "  hbg_dawnbake --job-atlas-test\n"
              << "  hbg_dawnbake --sh-atlas-test\n"
              << "  hbg_dawnbake --sh-atlas-dds-test\n"
              << "  hbg_dawnbake --sh-atlas-dds-verify-test\n"
              << "  hbg_dawnbake --sidecar-merge-test\n"
              << "  hbg_dawnbake --merge-sidecar <scene.hdrp.json> <lightmap.json> [<out.hdrp.json>]\n"
              << "  hbg_dawnbake --verify-sh-atlas-dds <file.lmgz> <dds-dir> <atlas-index> <target-x> <target-y> "
                 "<atlas-size>\n"
              << "  hbg_dawnbake --inspect-lmgz <file.lmgz>\n"
              << "  hbg_dawnbake --inspect-sdfgz <file.sdfgz>\n"
              << "  hbg_dawnbake --verify-lmgz <job-dir> <guid32hex>\n"
              << "  hbg_dawnbake --verify-sdfgz <job-dir> <guid32hex>\n"
              << "  hbg_dawnbake --inspect <file.scene|scenegz|jobs|jobgz|mesh|meshgz|mat|matgz>\n\n"
              << "The tool writes protocol version 1002: raw .scene/.jobs and gzip\n"
              << "v2.<guid>.scenegz/.jobgz (deflate when zlib is linked) plus meshgz/matgz packages.\n";
}

bool isHexGuid(const std::string& value) {
    if (value.size() != 32) {
        return false;
    }
    for (char c : value) {
        const bool hex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        if (!hex) {
            return false;
        }
    }
    return true;
}

std::string environmentValue(const char* name) {
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }
    std::string result(value);
    std::free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
#endif
}

void setEnvironmentValue(const char* name, const std::string& value) {
#ifdef _MSC_VER
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) {
        unsetenv(name);
    } else {
        setenv(name, value.c_str(), 1);
    }
#endif
}

Matrix4x4 identityMatrix() {
    Matrix4x4 matrix{};
    for (int i = 0; i < 4; ++i) {
        matrix.mm[i * 4 + i] = 1.0f;
    }
    return matrix;
}

Texture2D<MaterialColor> solidTexture(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    Texture2D<MaterialColor> texture;
    texture.header.sizeX = 1;
    texture.header.sizeY = 1;
    texture.colors.push_back(MaterialColor{r, g, b, a});
    return texture;
}

SceneInfo buildMinimalScene() {
    SceneInfo scene;

    MeshInfo mesh;
    mesh.header.guid = Guid{1, 0, 0, 0};
    mesh.header.numTriangles = 2;
    mesh.header.numVertices = 4;
    mesh.header.numElements = 1;
    mesh.header.lightmapUVIndex = 1;
    mesh.header.lightmapUVDensity = 1.0f;
    mesh.header.lightmapTexelDensity = 1.0f;
    mesh.elements.push_back(MeshElementInfo{0, 2, 0, 0});

    const Float3 positions[4] = {
        {-1.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, -1.0f},
    };
    for (const Float3& position : positions) {
        mesh.vertices.push_back(Float4{position.x, position.y, position.z, 1.0f});
        mesh.normals.push_back(Float4{0.0f, 1.0f, 0.0f, 0.0f});
        // Dawn's exporter swaps tangent/bitangent on purpose (DawnMeshExporter.cs:773-775).
        mesh.tangents.push_back(Float4{0.0f, 0.0f, 1.0f, 0.0f});
        mesh.bitangents.push_back(Float4{1.0f, 0.0f, 0.0f, 0.0f});
    }

    for (uint16_t index : {uint16_t{0}, uint16_t{1}, uint16_t{2}, uint16_t{0}, uint16_t{2}, uint16_t{3}}) {
        mesh.index16.push_back(index);
    }

    const std::vector<Float2> uv = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
    mesh.texcoords.push_back(uv);
    mesh.texcoords.push_back(uv);
    mesh.texcoords.push_back(uv);
    scene.meshes.push_back(std::move(mesh));
    scene.header.numMeshes = 1;

    MaterialInfo material;
    material.header.shadingModelId = 0;
    material.header.blendMode = 0;
    material.header.bTwoSided = 0;
    material.header.flags = 0;
    material.header.opacityMaskClipValue = 0.5f;
    material.header.diffuseBoost = 1.0f;
    material.header.emissiveBoost = 1.0f;
    material.diffuseTexture = solidTexture(255, 255, 255, 255);
    material.emissiveTexture = solidTexture(0, 0, 0, 255);
    material.normalTexture = solidTexture(128, 128, 255, 255);
    material.specularTexture = solidTexture(0, 128, 255, 255);
    scene.materials.push_back(std::move(material));
    scene.header.numMaterials = 1;

    MeshInstanceInfo instance;
    instance.header.levelGuid = Guid{};
    instance.header.guid = Guid{2, 0, 0, 0};
    instance.header.transform = identityMatrix();
    instance.header.inverseTransform = identityMatrix();
    instance.header.inverseTransposeTransform = identityMatrix();
    instance.header.lightmapId = 0;
    instance.header.boundingBox = SceneBoxBounds{{-1.0f, 0.0f, -1.0f}, {1.0f, 0.0f, 1.0f}};
    instance.header.meshIndex = 0;
    instance.header.texcoordIndex = 0;
    instance.header.flags = 0;
    instance.header.diffuseBoost = 1.0f;
    instance.header.emissiveBoost = 1.0f;
    instance.header.rayOffset = 0.0f;
    instance.header.fullyOccludedSamplesFraction = 1.0f;
    instance.header.meshLightAttenuationRadius = 0.0f;
    instance.header.meshInstanceVisibilityMask = 1;
    instance.header.meshInstanceVisibilityMaskInHlodScene = 1;
    instance.header.lodLevelInHlodScene = 0;
    instance.header.uv2MapSize = UInt2{64, 64};
    instance.header.uv2Offset = UInt2{0, 0};
    instance.materialOverrides.push_back(0);
    scene.meshInstances.push_back(std::move(instance));
    scene.header.numMeshInstances = 1;
    scene.meshGuids.push_back(Guid{2, 0, 0, 0});
    scene.header.numMeshGuids = 1;

    scene.skyLight.skyData.color = Float3{0.05f, 0.05f, 0.08f};
    scene.skyLight.skyData.mipDimensions = Int3{1, 1, 1};
    scene.skyLight.skyData.envColor = Float3{0.05f, 0.05f, 0.08f};
    scene.skyLight.skyData.transform = identityMatrix();
    scene.skyLight.skyData.indirectLightingIntensity = 1.0f;
    scene.skyLight.envCube.push_back(Float4{0.05f, 0.05f, 0.08f, 1.0f});

    LightInfo light;
    light.position = Float3{0.0f, 10.0f, 0.0f};
    light.normal = Float3{0.5f, -1.0f, 0.5f};
    light.dPdu = Float3{1.0f, 0.0f, 0.0f};
    light.dPdv = Float3{0.0f, 0.0f, 1.0f};
    light.color = Float3{3.0f, 3.0f, 3.0f};
    light.dimensions = Float3{0.0f, 0.0f, 0.0f};
    light.shaping = Float2{0.0f, 0.0f};
    light.attenuation = 1.0f;
    light.flags = kLightFlagDirectLighting | kLightFlagIndirectLighting | kLightFlagDirectional;
    light.iesTextureSlice = -1;
    light.indirectColor = Float3{3.0f, 3.0f, 3.0f};
    light.boundMin = Float3{-1000.0f, -1000.0f, -1000.0f};
    light.boundMax = Float3{1000.0f, 1000.0f, 1000.0f};
    scene.lights.push_back(light);
    scene.header.numLights = 1;
    scene.lightGuids.push_back(Guid{3, 0, 0, 0});
    scene.lightPowers.push_back(1.0f);

    scene.header.bounds =
        SceneBounds{Float3{0.0f, 0.0f, 0.0f}, Float3{0.0f, 0.0f, 0.0f}, Float3{1.0f, 0.01f, 1.0f}, 1.5f};
    scene.header.importanceBounds = scene.header.bounds;

    return scene;
}

BakingJobInputs buildMinimalJobs() {
    BakingJobInputs jobs;

    jobs.bakingParameters.bUseFastVoxelization = 0;

    auto& lightmap = jobs.bakingParameters.lightmap2DParameter;
    lightmap.denoiserMode = 0;
    lightmap.iterationNum = 64;
    lightmap.maxDepth = 3;
    lightmap.superSampleFactor = 1;
    lightmap.maxLightmapResolution = 64;
    lightmap.penumbraShadowFraction = 0.3f;
    lightmap.skyOcclusionCompensateScale = 1.0f;
    lightmap.optixSmoothLevel = 0;
    lightmap.bGpuRaster = 0;
    lightmap.bUseLightmapDirection = 1;
    lightmap.bSkyOcclusionDenoise = 0;

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

    Lightmap2DInput input;
    input.jobId = Guid{0, 0, 0, 5};
    input.meshId = Guid{2, 0, 0, 0};
    input.size = Int2{64, 64};
    input.lightmapUVIndex = 1;
    std::memset(&input.overrideBakingParams, 0, sizeof(input.overrideBakingParams));
    jobs.lightmap2DJobs.push_back(input);

    return jobs;
}

struct ExportedSceneBundle {
    SceneInfo scene;
    BakingJobInputs jobs;
    LightmapSidecar lightmap;
};

ExportedSceneBundle buildExportedScene() {
    asset::Mesh mesh;
    mesh.name = "Quad";
    mesh.vertices.resize(4);

    const hbg::Vec3 positions[4] = {
        {-1.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, -1.0f},
    };
    const hbg::Vec2 uvs[4] = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {1.0f, 0.0f},
    };
    for (std::size_t index = 0; index < 4; ++index) {
        asset::MeshVertex& vertex = mesh.vertices[index];
        vertex.position = positions[index];
        vertex.normal = hbg::Vec3{0.0f, 1.0f, 0.0f};
        vertex.tangent = hbg::Vec4{1.0f, 0.0f, 0.0f, 1.0f};
        vertex.texcoord0 = uvs[index];
        vertex.texcoord1 = uvs[index];
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.submeshes.push_back(asset::SubMesh{0, 6, 0});
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    mesh.hasUv1 = true;
    mesh.recomputeBounds();

    asset::Material material;
    material.name = "QuadMaterial";
    material.baseColorFactor = hbg::Vec4{0.8f, 0.8f, 0.8f, 1.0f};
    material.metallic = 0.0f;
    material.roughness = 0.5f;

    render::RenderScene imageScene;
    render::RenderObject object;
    object.name = "Quad";
    object.meshIndex = 0;
    object.world = hbg::Mat4::identity();
    object.worldBounds = mesh.bounds;
    MeshExportOptions meshOptions;
    meshOptions.castShadows = true;
    const uint32_t lightmapSize = 64u;
    meshOptions.uv2MapSize = UInt2{lightmapSize, lightmapSize};
    LightmapPackingSettings lightmapPacking;
    LightmapPackingResult lightmapPackingResult;
    if (!packLightmapAtlases({lightmapSize}, lightmapPacking, lightmapPackingResult)) {
        throw std::runtime_error("failed to pack the D3 lightmap sidecar atlas");
    }
    const LightmapPlacement& quadPlacement = lightmapPackingResult.placements[0];
    meshOptions.uv2Offset = UInt2{quadPlacement.rect.x, quadPlacement.rect.y};
    const MeshInfo meshInfo = exportMesh(mesh, 0, makeMeshGuid(0), meshOptions);
    const MaterialInfo materialInfo = exportMaterial(material, imageScene, MaterialExportOptions{});
    const MeshInstanceInfo instance = exportMeshInstance(mesh, object, 0, 0, meshOptions);

    ExportedSceneBundle bundle;
    bundle.scene.meshes.push_back(meshInfo);
    bundle.scene.materials.push_back(materialInfo);
    bundle.scene.meshInstances.push_back(instance);
    bundle.scene.meshGuids.push_back(instance.header.guid);
    bundle.scene.header.numMeshes = 1;
    bundle.scene.header.numMaterials = 1;
    bundle.scene.header.numMeshInstances = 1;
    bundle.scene.header.numMeshGuids = 1;

    bundle.scene.header.bounds =
        SceneBounds{Float3{0.0f, 0.0f, 0.0f}, Float3{0.0f, 0.0f, 0.0f}, Float3{1.0f, 0.01f, 1.0f}, 1.41421356f};
    bundle.scene.header.importanceBounds = bundle.scene.header.bounds;

    render::Light light;
    light.name = "Sun";
    light.type = render::LightType::Directional;
    light.color = hbg::Vec3{1.0f, 1.0f, 1.0f};
    light.intensity = 3.0f;
    light.castShadows = true;
    const LightFullInfo lightInfo = exportLight(light, LightExportOptions{}, 0);
    bundle.scene.lights.push_back(lightInfo.lightData);
    bundle.scene.lightGuids.push_back(lightInfo.lightGuid);
    bundle.scene.lightPowers.push_back(lightInfo.lightPower);
    bundle.scene.header.numLights = 1;

    asset::ImageAsset skyImage;
    skyImage.name = "Sky";
    skyImage.width = 2;
    skyImage.height = 1;
    skyImage.format = gfx::TextureFormat::RGBA32F;
    const float skyPixel[4] = {0.05f, 0.05f, 0.08f, 1.0f};
    skyImage.pixels.resize(sizeof(skyPixel) * 2u);
    std::memcpy(skyImage.pixels.data(), skyPixel, sizeof(skyPixel));
    std::memcpy(skyImage.pixels.data() + sizeof(skyPixel), skyPixel, sizeof(skyPixel));
    SkyLightExportOptions skyOptions;
    skyOptions.cubeSize = 8;
    skyOptions.srgbToLinear = false;
    bundle.scene.skyLight = exportSkyLight(&skyImage, skyOptions);

    bundle.jobs = buildMinimalJobs();
    bundle.jobs.lightmap2DJobs[0].meshId = instance.header.guid;
    bundle.jobs.lightmap2DJobs[0].size = Int2{static_cast<int32_t>(lightmapSize), static_cast<int32_t>(lightmapSize)};
    bundle.jobs.lightmap2DJobs[0].lightmapUVIndex = 1;
    bundle.jobs.lightmap2DJobs[0].flags = kLightmapFlagDirect | kLightmapFlagIndirect | kLightmapFlagDirection |
                                          kLightmapFlagBilinearFilter | kLightmapFlagBmStatic;
    bundle.jobs.partialJobsTotalNum.numLightmap2DJobs = 1;

    bundle.lightmap.texelPerUnit = 6.0f;
    for (std::size_t index = 0; index < lightmapPackingResult.atlases.size(); ++index) {
        const LightmapAtlasPlan& atlas = lightmapPackingResult.atlases[index];
        bundle.lightmap.atlases.push_back(LightmapSidecarAtlas{"lm" + std::to_string(index), atlas.size, 3, "rgba16f"});
    }
    bundle.lightmap.objects.push_back(LightmapSidecarObject{
        object.name,
        quadPlacement.atlasIndex,
        quadPlacement.rect.x,
        quadPlacement.rect.y,
        quadPlacement.rect.size,
        quadPlacement.scale,
        true,
    });
    return bundle;
}

ExportedSceneBundle buildUvAxisScene(const DawnConfig& config) {
    asset::Mesh mesh;
    mesh.name = "T2Floor";
    mesh.vertices.resize(4);

    const hbg::Vec3 positions[4] = {
        {-2.0f, 0.0f, -2.0f},
        {-2.0f, 0.0f, 2.0f},
        {2.0f, 0.0f, 2.0f},
        {2.0f, 0.0f, -2.0f},
    };
    const hbg::Vec2 uvs[4] = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {1.0f, 0.0f},
    };
    for (std::size_t index = 0; index < 4; ++index) {
        asset::MeshVertex& vertex = mesh.vertices[index];
        vertex.position = positions[index];
        vertex.normal = hbg::Vec3{0.0f, 1.0f, 0.0f};
        vertex.tangent = hbg::Vec4{1.0f, 0.0f, 0.0f, 1.0f};
        vertex.texcoord0 = uvs[index];
        vertex.texcoord1 = hbg::Vec2{uvs[index].x, config.lightmap.uv2VFlip ? 1.0f - uvs[index].y : uvs[index].y};
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.submeshes.push_back(asset::SubMesh{0, 6, 0});
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    mesh.hasUv1 = true;
    mesh.recomputeBounds();

    asset::Material material;
    material.name = "T2White";
    material.baseColorFactor = hbg::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    material.metallic = 0.0f;
    material.roughness = 1.0f;

    render::RenderScene imageScene;
    render::RenderObject object;
    object.name = "T2Floor";
    object.meshIndex = 0;
    object.world = hbg::Mat4::identity();
    object.worldBounds = mesh.bounds;

    const uint32_t lightmapSize = 128u;
    MeshExportOptions meshOptions;
    meshOptions.castShadows = true;
    meshOptions.normalizeUv2 = false;
    meshOptions.uv2MapSize = UInt2{lightmapSize, lightmapSize};

    LightmapPackingSettings packingSettings;
    packingSettings.atlasSize = config.lightmap.atlasSize;
    packingSettings.maxAtlases = config.lightmap.maxAtlases;
    packingSettings.padding = config.lightmap.padding;
    LightmapPackingResult packingResult;
    if (!packLightmapAtlases({lightmapSize}, packingSettings, packingResult)) {
        throw std::runtime_error("failed to pack the T2 lightmap atlas");
    }
    const LightmapPlacement& placement = packingResult.placements[0];
    meshOptions.uv2Offset = UInt2{placement.rect.x, placement.rect.y};

    MaterialExportOptions materialOptions;
    materialOptions.srgbToLinear = config.lighting.colorSpace == "linear";
    const MeshInfo meshInfo = exportMesh(mesh, 0, makeMeshGuid(0), meshOptions);
    const MaterialInfo materialInfo = exportMaterial(material, imageScene, materialOptions);
    const MeshInstanceInfo instance = exportMeshInstance(mesh, object, 0, 0, meshOptions);

    ExportedSceneBundle bundle;
    bundle.scene.meshes.push_back(meshInfo);
    bundle.scene.materials.push_back(materialInfo);
    bundle.scene.meshInstances.push_back(instance);
    bundle.scene.meshGuids.push_back(instance.header.guid);
    bundle.scene.header.numMeshes = 1;
    bundle.scene.header.numMaterials = 1;
    bundle.scene.header.numMeshInstances = 1;
    bundle.scene.header.numMeshGuids = 1;
    bundle.scene.header.bounds =
        SceneBounds{Float3{0.0f, 0.0f, 0.0f}, Float3{-2.0f, 0.0f, -2.0f}, Float3{2.0f, 2.0f, 2.0f}, 3.0f};
    bundle.scene.header.importanceBounds = bundle.scene.header.bounds;

    const auto addPointLight = [&](const char* name, hbg::Vec3 position, hbg::Vec3 color, uint32_t index) {
        render::Light light;
        light.name = name;
        light.type = render::LightType::Point;
        light.color = color;
        light.intensity = 24.0f * config.lighting.lightUnitScale;
        light.range = 7.0f;
        light.castShadows = false;
        light.world = hbg::Mat4::identity();
        light.world[12] = position.x;
        light.world[13] = position.y;
        light.world[14] = position.z;
        LightExportOptions lightOptions;
        lightOptions.srgbToLinear = config.lighting.colorSpace == "linear";
        const LightFullInfo lightInfo = exportLight(light, lightOptions, index);
        bundle.scene.lights.push_back(lightInfo.lightData);
        bundle.scene.lightGuids.push_back(lightInfo.lightGuid);
        bundle.scene.lightPowers.push_back(lightInfo.lightPower);
    };
    addPointLight("T2Red", hbg::Vec3{2.5f, 2.0f, 0.0f}, hbg::Vec3{1.0f, 0.0f, 0.0f}, 0);
    addPointLight("T2Green", hbg::Vec3{0.0f, 2.0f, 2.5f}, hbg::Vec3{0.0f, 1.0f, 0.0f}, 1);
    bundle.scene.header.numLights = 2;

    asset::ImageAsset skyImage;
    skyImage.name = "T2Sky";
    skyImage.width = 2;
    skyImage.height = 1;
    skyImage.format = gfx::TextureFormat::RGBA32F;
    const float skyPixel[4] = {0.01f, 0.01f, 0.01f, 1.0f};
    skyImage.pixels.resize(sizeof(skyPixel) * 2u);
    std::memcpy(skyImage.pixels.data(), skyPixel, sizeof(skyPixel));
    std::memcpy(skyImage.pixels.data() + sizeof(skyPixel), skyPixel, sizeof(skyPixel));
    SkyLightExportOptions skyOptions;
    skyOptions.cubeSize = 8;
    skyOptions.srgbToLinear = false;
    bundle.scene.skyLight = exportSkyLight(&skyImage, skyOptions);

    bundle.jobs = buildMinimalJobs();
    bundle.jobs.lightmap2DJobs[0].meshId = instance.header.guid;
    bundle.jobs.lightmap2DJobs[0].size = Int2{static_cast<int32_t>(lightmapSize), static_cast<int32_t>(lightmapSize)};
    bundle.jobs.lightmap2DJobs[0].lightmapUVIndex = 1;
    const uint32_t directionFlag = config.lightmap.directional ? kLightmapFlagDirection : 0u;
    bundle.jobs.lightmap2DJobs[0].flags = kLightmapFlagDirect | kLightmapFlagIndirect | directionFlag |
                                          kLightmapFlagBilinearFilter | kLightmapFlagBmStatic;
    bundle.jobs.partialJobsTotalNum.numLightmap2DJobs = 1;

    bundle.lightmap.texelPerUnit = 32.0f;
    for (std::size_t index = 0; index < packingResult.atlases.size(); ++index) {
        const LightmapAtlasPlan& atlas = packingResult.atlases[index];
        bundle.lightmap.atlases.push_back(LightmapSidecarAtlas{"lm" + std::to_string(index), atlas.size,
                                                               config.lightmap.layers, config.lightmap.format});
    }
    bundle.lightmap.objects.push_back(LightmapSidecarObject{
        object.name, placement.atlasIndex, placement.rect.x, placement.rect.y, placement.rect.size, placement.scale,
        true});
    return bundle;
}

ExportedSceneBundle buildPointFalloffScene() {
    asset::Mesh mesh;
    mesh.name = "D73Floor";
    mesh.vertices.resize(4);

    const hbg::Vec3 positions[4] = {
        {-2.0f, 0.0f, -2.0f},
        {-2.0f, 0.0f, 2.0f},
        {2.0f, 0.0f, 2.0f},
        {2.0f, 0.0f, -2.0f},
    };
    const hbg::Vec2 uvs[4] = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {1.0f, 0.0f},
    };
    for (std::size_t index = 0; index < 4; ++index) {
        asset::MeshVertex& vertex = mesh.vertices[index];
        vertex.position = positions[index];
        vertex.normal = hbg::Vec3{0.0f, 1.0f, 0.0f};
        vertex.tangent = hbg::Vec4{1.0f, 0.0f, 0.0f, 1.0f};
        vertex.texcoord0 = uvs[index];
        vertex.texcoord1 = uvs[index];
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.submeshes.push_back(asset::SubMesh{0, 6, 0});
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    mesh.hasUv1 = true;
    mesh.recomputeBounds();

    asset::Material material;
    material.name = "D73White";
    material.baseColorFactor = hbg::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    material.metallic = 0.0f;
    material.roughness = 1.0f;

    render::RenderScene imageScene;
    render::RenderObject object;
    object.name = "D73Floor";
    object.meshIndex = 0;
    object.world = hbg::Mat4::identity();
    object.worldBounds = mesh.bounds;

    const uint32_t lightmapSize = 128u;
    MeshExportOptions meshOptions;
    meshOptions.castShadows = true;
    meshOptions.normalizeUv2 = false;
    meshOptions.uv2MapSize = UInt2{lightmapSize, lightmapSize};

    LightmapPackingSettings packingSettings;
    LightmapPackingResult packingResult;
    if (!packLightmapAtlases({lightmapSize}, packingSettings, packingResult)) {
        throw std::runtime_error("failed to pack the D7.3 lightmap atlas");
    }
    const LightmapPlacement& placement = packingResult.placements[0];
    meshOptions.uv2Offset = UInt2{placement.rect.x, placement.rect.y};

    const MeshInfo meshInfo = exportMesh(mesh, 0, makeMeshGuid(0), meshOptions);
    const MaterialInfo materialInfo = exportMaterial(material, imageScene, MaterialExportOptions{});
    const MeshInstanceInfo instance = exportMeshInstance(mesh, object, 0, 0, meshOptions);

    ExportedSceneBundle bundle;
    bundle.scene.meshes.push_back(meshInfo);
    bundle.scene.materials.push_back(materialInfo);
    bundle.scene.meshInstances.push_back(instance);
    bundle.scene.meshGuids.push_back(instance.header.guid);
    bundle.scene.header.numMeshes = 1;
    bundle.scene.header.numMaterials = 1;
    bundle.scene.header.numMeshInstances = 1;
    bundle.scene.header.numMeshGuids = 1;
    bundle.scene.header.bounds =
        SceneBounds{Float3{0.0f, 0.0f, 0.0f}, Float3{0.0f, 0.0f, 0.0f}, Float3{2.0f, 4.0f, 2.0f}, 4.89897949f};
    bundle.scene.header.importanceBounds = bundle.scene.header.bounds;

    const auto addPointLight = [&](const char* name, hbg::Vec3 position, hbg::Vec3 color, uint32_t index) {
        render::Light light;
        light.name = name;
        light.type = render::LightType::Point;
        light.color = color;
        light.intensity = 24.0f;
        light.range = 10.0f;
        light.castShadows = false;
        light.world = hbg::Mat4::identity();
        light.world[12] = position.x;
        light.world[13] = position.y;
        light.world[14] = position.z;
        const LightFullInfo lightInfo = exportLight(light, LightExportOptions{}, index);
        bundle.scene.lights.push_back(lightInfo.lightData);
        bundle.scene.lightGuids.push_back(lightInfo.lightGuid);
        bundle.scene.lightPowers.push_back(lightInfo.lightPower);
    };
    addPointLight("D73Red1m", hbg::Vec3{0.0f, 1.0f, 0.0f}, hbg::Vec3{1.0f, 0.0f, 0.0f}, 0);
    addPointLight("D73Green2m", hbg::Vec3{0.0f, 2.0f, 0.0f}, hbg::Vec3{0.0f, 1.0f, 0.0f}, 1);
    addPointLight("D73Blue4m", hbg::Vec3{0.0f, 4.0f, 0.0f}, hbg::Vec3{0.0f, 0.0f, 1.0f}, 2);
    bundle.scene.header.numLights = 3;

    asset::ImageAsset skyImage;
    skyImage.name = "D73BlackSky";
    skyImage.width = 2;
    skyImage.height = 1;
    skyImage.format = gfx::TextureFormat::RGBA32F;
    const float skyPixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    skyImage.pixels.resize(sizeof(skyPixel) * 2u);
    std::memcpy(skyImage.pixels.data(), skyPixel, sizeof(skyPixel));
    std::memcpy(skyImage.pixels.data() + sizeof(skyPixel), skyPixel, sizeof(skyPixel));
    SkyLightExportOptions skyOptions;
    skyOptions.cubeSize = 8;
    skyOptions.srgbToLinear = false;
    bundle.scene.skyLight = exportSkyLight(&skyImage, skyOptions);

    bundle.jobs = buildMinimalJobs();
    bundle.jobs.lightmap2DJobs[0].meshId = instance.header.guid;
    bundle.jobs.lightmap2DJobs[0].size = Int2{static_cast<int32_t>(lightmapSize), static_cast<int32_t>(lightmapSize)};
    bundle.jobs.lightmap2DJobs[0].lightmapUVIndex = 1;
    bundle.jobs.lightmap2DJobs[0].flags = kLightmapFlagDirect | kLightmapFlagDirection |
                                          kLightmapFlagBilinearFilter | kLightmapFlagBmStatic;
    bundle.jobs.partialJobsTotalNum.numLightmap2DJobs = 1;

    bundle.lightmap.texelPerUnit = 32.0f;
    for (std::size_t index = 0; index < packingResult.atlases.size(); ++index) {
        const LightmapAtlasPlan& atlas = packingResult.atlases[index];
        bundle.lightmap.atlases.push_back(LightmapSidecarAtlas{"lm" + std::to_string(index), atlas.size, 3, "rgba16f"});
    }
    bundle.lightmap.objects.push_back(LightmapSidecarObject{
        object.name, placement.atlasIndex, placement.rect.x, placement.rect.y, placement.rect.size, placement.scale,
        true});
    return bundle;
}

ExportedSceneBundle buildDirectionalScene(hbg::Vec3 lightColor = hbg::Vec3{1.0f, 1.0f, 1.0f},
                                         float lightIntensity = 1.0f, bool srgbToLinear = false) {
    asset::Mesh mesh;
    mesh.name = "T1Floor";
    mesh.vertices.resize(4);

    const hbg::Vec3 positions[4] = {
        {-2.0f, 0.0f, -2.0f},
        {-2.0f, 0.0f, 2.0f},
        {2.0f, 0.0f, 2.0f},
        {2.0f, 0.0f, -2.0f},
    };
    const hbg::Vec2 uvs[4] = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {1.0f, 0.0f},
    };
    for (std::size_t index = 0; index < 4; ++index) {
        asset::MeshVertex& vertex = mesh.vertices[index];
        vertex.position = positions[index];
        vertex.normal = hbg::Vec3{0.0f, 1.0f, 0.0f};
        vertex.tangent = hbg::Vec4{1.0f, 0.0f, 0.0f, 1.0f};
        vertex.texcoord0 = uvs[index];
        vertex.texcoord1 = uvs[index];
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.submeshes.push_back(asset::SubMesh{0, 6, 0});
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    mesh.hasUv1 = true;
    mesh.recomputeBounds();

    asset::Material material;
    material.name = "T1White";
    material.baseColorFactor = hbg::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    material.metallic = 0.0f;
    material.roughness = 1.0f;

    render::RenderScene imageScene;
    render::RenderObject object;
    object.name = "T1Floor";
    object.meshIndex = 0;
    object.world = hbg::Mat4::identity();
    object.worldBounds = mesh.bounds;

    const uint32_t lightmapSize = 64u;
    MeshExportOptions meshOptions;
    meshOptions.castShadows = true;
    meshOptions.normalizeUv2 = false;
    meshOptions.uv2MapSize = UInt2{lightmapSize, lightmapSize};

    LightmapPackingSettings packingSettings;
    LightmapPackingResult packingResult;
    if (!packLightmapAtlases({lightmapSize}, packingSettings, packingResult)) {
        throw std::runtime_error("failed to pack the T1 lightmap atlas");
    }
    const LightmapPlacement& placement = packingResult.placements[0];
    meshOptions.uv2Offset = UInt2{placement.rect.x, placement.rect.y};

    const MeshInfo meshInfo = exportMesh(mesh, 0, makeMeshGuid(0), meshOptions);
    const MaterialInfo materialInfo = exportMaterial(material, imageScene, MaterialExportOptions{});
    const MeshInstanceInfo instance = exportMeshInstance(mesh, object, 0, 0, meshOptions);

    ExportedSceneBundle bundle;
    bundle.scene.meshes.push_back(meshInfo);
    bundle.scene.materials.push_back(materialInfo);
    bundle.scene.meshInstances.push_back(instance);
    bundle.scene.meshGuids.push_back(instance.header.guid);
    bundle.scene.header.numMeshes = 1;
    bundle.scene.header.numMaterials = 1;
    bundle.scene.header.numMeshInstances = 1;
    bundle.scene.header.numMeshGuids = 1;
    bundle.scene.header.bounds =
        SceneBounds{Float3{0.0f, 0.0f, 0.0f}, Float3{-2.0f, 0.0f, -2.0f}, Float3{2.0f, 0.1f, 2.0f}, 3.0f};
    bundle.scene.header.importanceBounds = bundle.scene.header.bounds;

    render::Light light;
    light.name = "T1PlusY";
    light.type = render::LightType::Directional;
    light.color = lightColor;
    light.intensity = lightIntensity;
    light.castShadows = false;
    light.world = hbg::Mat4::identity();
    light.world[5] = 0.0f;
    light.world[6] = -1.0f;
    light.world[9] = 1.0f;
    light.world[10] = 0.0f;
    LightExportOptions lightOptions;
    lightOptions.srgbToLinear = srgbToLinear;
    const LightFullInfo lightInfo = exportLight(light, lightOptions, 0);
    bundle.scene.lights.push_back(lightInfo.lightData);
    bundle.scene.lightGuids.push_back(lightInfo.lightGuid);
    bundle.scene.lightPowers.push_back(lightInfo.lightPower);
    bundle.scene.header.numLights = 1;

    asset::ImageAsset skyImage;
    skyImage.name = "T1BlackSky";
    skyImage.width = 2;
    skyImage.height = 1;
    skyImage.format = gfx::TextureFormat::RGBA32F;
    const float skyPixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    skyImage.pixels.resize(sizeof(skyPixel) * 2u);
    std::memcpy(skyImage.pixels.data(), skyPixel, sizeof(skyPixel));
    std::memcpy(skyImage.pixels.data() + sizeof(skyPixel), skyPixel, sizeof(skyPixel));
    SkyLightExportOptions skyOptions;
    skyOptions.cubeSize = 8;
    skyOptions.srgbToLinear = false;
    bundle.scene.skyLight = exportSkyLight(&skyImage, skyOptions);

    bundle.jobs = buildMinimalJobs();
    bundle.jobs.lightmap2DJobs[0].meshId = instance.header.guid;
    bundle.jobs.lightmap2DJobs[0].size = Int2{static_cast<int32_t>(lightmapSize), static_cast<int32_t>(lightmapSize)};
    bundle.jobs.lightmap2DJobs[0].lightmapUVIndex = 1;
    bundle.jobs.lightmap2DJobs[0].flags = kLightmapFlagDirect | kLightmapFlagDirection |
                                          kLightmapFlagBilinearFilter | kLightmapFlagBmStatic;
    bundle.jobs.partialJobsTotalNum.numLightmap2DJobs = 1;

    bundle.lightmap.texelPerUnit = 16.0f;
    for (std::size_t index = 0; index < packingResult.atlases.size(); ++index) {
        const LightmapAtlasPlan& atlas = packingResult.atlases[index];
        bundle.lightmap.atlases.push_back(LightmapSidecarAtlas{"lm" + std::to_string(index), atlas.size, 3, "rgba16f"});
    }
    bundle.lightmap.objects.push_back(LightmapSidecarObject{
        object.name, placement.atlasIndex, placement.rect.x, placement.rect.y, placement.rect.size, placement.scale,
        true});
    return bundle;
}

ExportedSceneBundle buildSeamTileScene(uint32_t tileIndex) {
    constexpr uint32_t kLightmapSize = 64u;
    const hbg::Vec3 centers[4] = {
        {-1.0f, 0.0f, -1.0f},
        {1.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 1.0f},
    };
    if (tileIndex >= 4u) {
        throw std::runtime_error("tile index must be in 0..3");
    }

    asset::Mesh mesh;
    mesh.name = "T3Tile";
    mesh.vertices.resize(4);
    const hbg::Vec3 positions[4] = {
        {-1.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, -1.0f},
    };
    const hbg::Vec2 uvs[4] = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {1.0f, 0.0f},
    };
    for (std::size_t index = 0; index < 4; ++index) {
        asset::MeshVertex& vertex = mesh.vertices[index];
        vertex.position = positions[index];
        vertex.normal = hbg::Vec3{0.0f, 1.0f, 0.0f};
        vertex.tangent = hbg::Vec4{1.0f, 0.0f, 0.0f, 1.0f};
        vertex.texcoord0 = uvs[index];
        vertex.texcoord1 = uvs[index];
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.submeshes.push_back(asset::SubMesh{0, 6, 0});
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    mesh.hasUv1 = true;
    mesh.recomputeBounds();

    asset::Material material;
    material.name = "T1White";
    material.baseColorFactor = hbg::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    material.metallic = 0.0f;
    material.roughness = 1.0f;

    render::RenderScene imageScene;
    MeshExportOptions meshOptions;
    meshOptions.castShadows = true;
    meshOptions.normalizeUv2 = false;
    meshOptions.uv2MapSize = UInt2{kLightmapSize, kLightmapSize};
    meshOptions.uv2Offset = UInt2{2u, 2u};
    const MeshInfo meshInfo = exportMesh(mesh, 0, makeMeshGuid(0), meshOptions);

    render::RenderObject object;
    object.name = "T3Tile" + std::to_string(tileIndex);
    object.meshIndex = 0;
    object.world = hbg::Mat4::identity();
    object.world.values[12] = centers[tileIndex].x;
    object.world.values[14] = centers[tileIndex].z;
    object.worldBounds = hbg::Bounds{mesh.bounds.min + centers[tileIndex], mesh.bounds.max + centers[tileIndex]};
    const MeshInstanceInfo instance = exportMeshInstance(mesh, object, 0, 0, meshOptions);

    ExportedSceneBundle bundle;
    bundle.scene.meshes.push_back(meshInfo);
    bundle.scene.materials.push_back(exportMaterial(material, imageScene, MaterialExportOptions{}));
    bundle.scene.meshInstances.push_back(instance);
    bundle.scene.meshGuids.push_back(instance.header.guid);
    bundle.scene.header.numMeshes = 1u;
    bundle.scene.header.numMaterials = 1u;
    bundle.scene.header.numMeshInstances = 1u;
    bundle.scene.header.numMeshGuids = 1u;
    bundle.scene.header.bounds =
        SceneBounds{Float3{0.0f, 0.0f, 0.0f}, Float3{-2.0f, 0.0f, -2.0f}, Float3{2.0f, 0.1f, 2.0f}, 3.0f};
    bundle.scene.header.importanceBounds = bundle.scene.header.bounds;

    render::Light light;
    light.name = "T3Center";
    light.type = render::LightType::Point;
    light.color = hbg::Vec3{1.0f, 1.0f, 1.0f};
    light.intensity = 24.0f;
    light.range = 10.0f;
    light.castShadows = false;
    light.world = hbg::Mat4::identity();
    light.world.values[13] = 3.0f;
    const LightFullInfo lightInfo = exportLight(light, LightExportOptions{}, 0);
    bundle.scene.lights.push_back(lightInfo.lightData);
    bundle.scene.lightGuids.push_back(lightInfo.lightGuid);
    bundle.scene.lightPowers.push_back(lightInfo.lightPower);
    bundle.scene.header.numLights = 1u;

    asset::ImageAsset skyImage;
    skyImage.name = "T3BlackSky";
    skyImage.width = 2;
    skyImage.height = 1;
    skyImage.format = gfx::TextureFormat::RGBA32F;
    const float skyPixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    skyImage.pixels.resize(sizeof(skyPixel) * 2u);
    std::memcpy(skyImage.pixels.data(), skyPixel, sizeof(skyPixel));
    std::memcpy(skyImage.pixels.data() + sizeof(skyPixel), skyPixel, sizeof(skyPixel));
    SkyLightExportOptions skyOptions;
    skyOptions.cubeSize = 8;
    skyOptions.srgbToLinear = false;
    bundle.scene.skyLight = exportSkyLight(&skyImage, skyOptions);

    bundle.jobs = buildMinimalJobs();
    bundle.jobs.lightmap2DJobs.clear();
    Lightmap2DInput input;
    input.jobId = Guid{0u, 0u, 0u, 5u};
    input.meshId = instance.header.guid;
    input.size = Int2{static_cast<int32_t>(kLightmapSize), static_cast<int32_t>(kLightmapSize)};
    input.lightmapUVIndex = 1;
    std::memset(&input.overrideBakingParams, 0, sizeof(input.overrideBakingParams));
    input.flags = kLightmapFlagDirect | kLightmapFlagDirection | kLightmapFlagBilinearFilter | kLightmapFlagBmStatic;
    bundle.jobs.lightmap2DJobs.push_back(input);
    bundle.jobs.partialJobsTotalNum.numLightmap2DJobs = 1u;

    bundle.lightmap.texelPerUnit = 16.0f;
    bundle.lightmap.atlases.push_back(LightmapSidecarAtlas{"lm0", 128u, 3, "rgba16f"});
    bundle.lightmap.objects.push_back(LightmapSidecarObject{
        object.name, 0u, 2u, 2u, kLightmapSize, 1.0f, true});
    return bundle;
}

struct T6Metrics {
    uint32_t objects = 0;
    uint32_t atlasSize = 0;
    uint32_t atlasCount = 0;
    double packMs = 0.0;
    uint64_t vramBytes = 0;
};

constexpr uint32_t kT6ObjectCount = 100u;
constexpr uint32_t kT6LightmapSize = 64u;
constexpr uint32_t kT6AtlasSize = 2048u;
constexpr uint32_t kT6GridSize = 10u;
constexpr float kT6GridSpacing = 2.2f;
constexpr uint64_t kT6VramLimitBytes = 100ull * 1024ull * 1024ull;

std::filesystem::path defaultDawnConfigPath() {
#ifdef HBG_DAWN_PROJECT_ROOT
    return std::filesystem::path(HBG_DAWN_PROJECT_ROOT) / "config" / "dawn.json";
#else
    return std::filesystem::path("config") / "dawn.json";
#endif
}

DawnConfig loadDawnConfigOrThrow(const std::filesystem::path& path = defaultDawnConfigPath()) {
    DawnConfig config;
    std::string error;
    if (!loadDawnConfig(path, config, error)) {
        throw std::runtime_error("failed to load dawn config: " + error);
    }
    return config;
}

ExportedSceneBundle buildT6Scene(T6Metrics* metrics, const DawnConfig& config) {
    asset::Mesh mesh;
    mesh.name = "T6Floor";
    mesh.vertices.resize(4);
    const hbg::Vec3 positions[4] = {
        {-1.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, -1.0f},
    };
    const hbg::Vec2 uvs[4] = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {1.0f, 0.0f},
    };
    for (std::size_t index = 0; index < 4; ++index) {
        asset::MeshVertex& vertex = mesh.vertices[index];
        vertex.position = positions[index];
        vertex.normal = hbg::Vec3{0.0f, 1.0f, 0.0f};
        vertex.tangent = hbg::Vec4{1.0f, 0.0f, 0.0f, 1.0f};
        vertex.texcoord0 = uvs[index];
        vertex.texcoord1 = uvs[index];
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.submeshes.push_back(asset::SubMesh{0, 6, 0});
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    mesh.hasUv1 = true;
    mesh.recomputeBounds();

    asset::Material material;
    material.name = "T6White";
    material.baseColorFactor = hbg::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    material.metallic = 0.0f;
    material.roughness = 1.0f;
    render::RenderScene imageScene;
    const MaterialInfo materialInfo = exportMaterial(material, imageScene, MaterialExportOptions{});

    std::vector<uint32_t> lightmapSizes(kT6ObjectCount, kT6LightmapSize);
    LightmapPackingSettings packingSettings;
    packingSettings.atlasSize = config.lightmap.atlasSize;
    packingSettings.maxAtlases = config.lightmap.maxAtlases;
    packingSettings.padding = config.lightmap.padding;
    LightmapPackingResult packingResult;
    const auto packStart = std::chrono::steady_clock::now();
    if (!packLightmapAtlases(lightmapSizes, packingSettings, packingResult)) {
        throw std::runtime_error("failed to pack the T6 lightmap atlases");
    }
    const auto packEnd = std::chrono::steady_clock::now();

    ExportedSceneBundle bundle;
    bundle.scene.materials.push_back(materialInfo);
    bundle.jobs = buildMinimalJobs();
    bundle.jobs.lightmap2DJobs.clear();
    bundle.jobs.lightmap2DJobs.reserve(kT6ObjectCount);

    const float gridOffset = (static_cast<float>(kT6GridSize - 1u) * 0.5f) * kT6GridSpacing;
    const float sceneExtent = gridOffset + 1.0f;
    for (uint32_t index = 0; index < kT6ObjectCount; ++index) {
        const uint32_t gridX = index % kT6GridSize;
        const uint32_t gridZ = index / kT6GridSize;
        const hbg::Vec3 center{static_cast<float>(gridX) * kT6GridSpacing - gridOffset, 0.0f,
                               static_cast<float>(gridZ) * kT6GridSpacing - gridOffset};

        const LightmapPlacement& placement = packingResult.placements[index];
        MeshExportOptions meshOptions;
        meshOptions.castShadows = true;
        meshOptions.normalizeUv2 = false;
        meshOptions.uv2MapSize = UInt2{kT6LightmapSize, kT6LightmapSize};
        meshOptions.uv2Offset = UInt2{placement.rect.x, placement.rect.y};
        const MeshInfo meshInfo = exportMesh(mesh, 0, makeMeshGuid(index), meshOptions);

        render::RenderObject object;
        object.name = "T6Object" + std::to_string(index);
        object.meshIndex = index;
        object.world = hbg::Mat4::identity();
        object.world.values[12] = center.x;
        object.world.values[14] = center.z;
        object.worldBounds = hbg::Bounds{mesh.bounds.min + center, mesh.bounds.max + center};
        const MeshInstanceInfo instance = exportMeshInstance(mesh, object, index, index, meshOptions);

        bundle.scene.meshes.push_back(meshInfo);
        bundle.scene.meshInstances.push_back(instance);
        bundle.scene.meshGuids.push_back(instance.header.guid);

        Lightmap2DInput input;
        input.jobId = Guid{0u, 0u, index, 5u};
        input.meshId = instance.header.guid;
        input.size = Int2{static_cast<int32_t>(kT6LightmapSize), static_cast<int32_t>(kT6LightmapSize)};
        input.lightmapUVIndex = 1;
        std::memset(&input.overrideBakingParams, 0, sizeof(input.overrideBakingParams));
        const uint32_t directionFlag = config.lightmap.directional ? kLightmapFlagDirection : 0u;
        input.flags = kLightmapFlagDirect | directionFlag | kLightmapFlagBilinearFilter | kLightmapFlagBmStatic;
        bundle.jobs.lightmap2DJobs.push_back(input);

        bundle.lightmap.objects.push_back(LightmapSidecarObject{object.name, placement.atlasIndex, placement.rect.x,
                                                                placement.rect.y, placement.rect.size, placement.scale,
                                                                true});
    }

    bundle.scene.header.numMeshes = kT6ObjectCount;
    bundle.scene.header.numMaterials = 1u;
    bundle.scene.header.numMeshInstances = kT6ObjectCount;
    bundle.scene.header.numMeshGuids = kT6ObjectCount;
    const float sceneRadius = std::sqrt(2.0f * sceneExtent * sceneExtent + 0.01f);
    bundle.scene.header.bounds = SceneBounds{Float3{0.0f, 0.0f, 0.0f}, Float3{0.0f, 0.0f, 0.0f},
                                             Float3{sceneExtent, 0.1f, sceneExtent}, sceneRadius};
    bundle.scene.header.importanceBounds = bundle.scene.header.bounds;

    render::Light light;
    light.name = "T6Center";
    light.type = render::LightType::Point;
    light.color = hbg::Vec3{1.0f, 1.0f, 1.0f};
    light.intensity = 1000.0f;
    light.range = 100.0f;
    light.castShadows = false;
    light.world = hbg::Mat4::identity();
    light.world.values[13] = 12.0f;
    const LightFullInfo lightInfo = exportLight(light, LightExportOptions{}, 0);
    bundle.scene.lights.push_back(lightInfo.lightData);
    bundle.scene.lightGuids.push_back(lightInfo.lightGuid);
    bundle.scene.lightPowers.push_back(lightInfo.lightPower);
    bundle.scene.header.numLights = 1u;

    asset::ImageAsset skyImage;
    skyImage.name = "T6BlackSky";
    skyImage.width = 2;
    skyImage.height = 1;
    skyImage.format = gfx::TextureFormat::RGBA32F;
    const float skyPixel[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    skyImage.pixels.resize(sizeof(skyPixel) * 2u);
    std::memcpy(skyImage.pixels.data(), skyPixel, sizeof(skyPixel));
    std::memcpy(skyImage.pixels.data() + sizeof(skyPixel), skyPixel, sizeof(skyPixel));
    SkyLightExportOptions skyOptions;
    skyOptions.cubeSize = 8;
    skyOptions.srgbToLinear = false;
    bundle.scene.skyLight = exportSkyLight(&skyImage, skyOptions);

    bundle.jobs.partialJobsTotalNum.numLightmap2DJobs = kT6ObjectCount;
    bundle.lightmap.texelPerUnit = 16.0f;
    for (std::size_t index = 0; index < packingResult.atlases.size(); ++index) {
        const LightmapAtlasPlan& atlas = packingResult.atlases[index];
        bundle.lightmap.atlases.push_back(LightmapSidecarAtlas{"lm" + std::to_string(index), atlas.size,
                                                               config.lightmap.layers, config.lightmap.format});
    }

    if (metrics != nullptr) {
        metrics->objects = kT6ObjectCount;
        metrics->atlasSize = packingResult.atlases.empty() ? 0u : packingResult.atlases.front().size;
        metrics->atlasCount = static_cast<uint32_t>(packingResult.atlases.size());
        metrics->packMs = std::chrono::duration<double, std::milli>(packEnd - packStart).count();
        metrics->vramBytes = 0u;
        for (const LightmapAtlasPlan& atlas : packingResult.atlases) {
            metrics->vramBytes += static_cast<uint64_t>(atlas.size) * atlas.size * config.lightmap.layers * 4u *
                                  sizeof(uint16_t);
        }
    }
    return bundle;
}

int exportSeamTilePackage(const std::filesystem::path& directory, uint32_t tileIndex, const std::string& guid) {
    if (!isHexGuid(guid)) {
        throw std::runtime_error("guid must be 32 hex characters");
    }
    ExportedSceneBundle bundle = buildSeamTileScene(tileIndex);
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);
    std::cout << "wrote " << files.scene.string() << "\n";
    std::cout << "wrote " << files.jobs.string() << "\n";
    std::cout << "wrote " << files.lightmap.string() << "\n";
    return 0;
}

int exportDirectionalPackage(const std::filesystem::path& directory, const std::string& guid) {
    if (!isHexGuid(guid)) {
        throw std::runtime_error("guid must be 32 hex characters");
    }
    ExportedSceneBundle bundle = buildDirectionalScene();
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);
    std::cout << "wrote " << files.scene.string() << "\n";
    std::cout << "wrote " << files.jobs.string() << "\n";
    std::cout << "wrote " << files.lightmap.string() << "\n";
    return 0;
}

int exportDirectionalColorPackage(const std::filesystem::path& directory, const std::string& guid) {
    if (!isHexGuid(guid)) {
        throw std::runtime_error("guid must be 32 hex characters");
    }
    ExportedSceneBundle bundle = buildDirectionalScene(hbg::Vec3{0.8f, 0.6f, 0.4f}, 0.5f, true);
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);
    std::cout << "wrote " << files.scene.string() << "\n";
    std::cout << "wrote " << files.jobs.string() << "\n";
    std::cout << "wrote " << files.lightmap.string() << "\n";
    return 0;
}

int exportPointFalloffPackage(const std::filesystem::path& directory, const std::string& guid) {
    if (!isHexGuid(guid)) {
        throw std::runtime_error("guid must be 32 hex characters");
    }
    ExportedSceneBundle bundle = buildPointFalloffScene();
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);
    std::cout << "wrote " << files.scene.string() << "\n";
    std::cout << "wrote " << files.jobs.string() << "\n";
    std::cout << "wrote " << files.lightmap.string() << "\n";
    return 0;
}

int exportT6Package(const std::filesystem::path& directory, const std::string& guid) {
    if (!isHexGuid(guid)) {
        throw std::runtime_error("guid must be 32 hex characters");
    }
    const DawnConfig config = loadDawnConfigOrThrow();
    T6Metrics metrics;
    ExportedSceneBundle bundle = buildT6Scene(&metrics, config);
    const auto writeStart = std::chrono::steady_clock::now();
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);
    const double writeMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - writeStart).count();
    std::printf(
        "T6: objects=%u pack_ms=%.3f package_ms=%.3f jobs=%zu atlas=%u count=%u vram_mib=%.2f vram_bytes=%llu\n",
        metrics.objects, metrics.packMs, writeMs, bundle.jobs.lightmap2DJobs.size(), metrics.atlasSize,
        metrics.atlasCount, static_cast<double>(metrics.vramBytes) / (1024.0 * 1024.0),
        static_cast<unsigned long long>(metrics.vramBytes));
    std::cout << "wrote " << files.scene.string() << "\n";
    std::cout << "wrote " << files.jobs.string() << "\n";
    std::cout << "wrote " << files.lightmap.string() << "\n";
    return 0;
}

std::string defaultSceneGuid(const std::filesystem::path& scenePath) {
    const std::string text = std::filesystem::absolute(scenePath).lexically_normal().string();
    uint32_t hash[4] = {0x811C9DC5u, 0x9E3779B9u, 0x85EBCA6Bu, 0xC2B2AE35u};
    for (const char character : text) {
        const uint32_t value = static_cast<uint8_t>(character);
        for (uint32_t index = 0; index < 4; ++index) {
            hash[index] = (hash[index] ^ (value + index)) * 0x01000193u;
        }
    }
    return formatGuid(Guid{hash[0], hash[1], hash[2], hash[3]});
}

bool readOptionValue(const std::string& argument, const char* prefix, std::string& value) {
    const std::string key = std::string(prefix) + "=";
    if (argument.rfind(key, 0) != 0) {
        return false;
    }
    value = argument.substr(key.size());
    return true;
}

int exportGenericSceneCommand(int argc, char** argv) {
    std::filesystem::path gltf;
    std::filesystem::path outDirectory;
    std::filesystem::path sidecar;
    std::filesystem::path skyImage;
    std::filesystem::path configPath;
    std::filesystem::path blenderExecutable;
    uint32_t blenderTimeoutSeconds = 600;
    std::string guid;
    bool includeAllObjects = false;
    bool loadSidecar = true;
    float skyIntensity = 1.0f;
    bool skyIntensitySet = false;
    float skyRotation = 0.0f;
    bool skyRotationSet = false;
    uint32_t samples = 1024;
    uint32_t bounces = 4;

    int index = 1;
    if (std::string(argv[1]) == "--export-scene") {
        if (argc < 4) {
            throw std::runtime_error("--export-scene requires <scene> <out-dir>");
        }
        gltf = argv[2];
        outDirectory = argv[3];
        index = 4;
    }

    while (index < argc) {
        const std::string argument = argv[index++];
        std::string value;
        if (readOptionValue(argument, "--scene", value)) {
            gltf = value;
        } else if (readOptionValue(argument, "--out", value)) {
            outDirectory = value;
        } else if (readOptionValue(argument, "--sidecar", value)) {
            sidecar = value;
        } else if (readOptionValue(argument, "--config", value)) {
            configPath = value;
        } else if (readOptionValue(argument, "--guid", value)) {
            guid = value;
        } else if (readOptionValue(argument, "--sky", value)) {
            skyImage = value;
        } else if (readOptionValue(argument, "--sky-intensity", value)) {
            skyIntensity = std::stof(value);
            skyIntensitySet = true;
        } else if (readOptionValue(argument, "--sky-rotation", value)) {
            skyRotation = std::stof(value);
            skyRotationSet = true;
        } else if (readOptionValue(argument, "--blender", value)) {
            blenderExecutable = value;
        } else if (readOptionValue(argument, "--blender-timeout", value)) {
            blenderTimeoutSeconds = static_cast<uint32_t>(std::stoul(value));
        } else if (readOptionValue(argument, "--samples", value)) {
            samples = static_cast<uint32_t>(std::stoul(value));
        } else if (readOptionValue(argument, "--bounces", value)) {
            bounces = static_cast<uint32_t>(std::stoul(value));
        } else if (argument == "--all-objects") {
            includeAllObjects = true;
        } else if (argument == "--no-sidecar") {
            loadSidecar = false;
            sidecar.clear();
        } else if (argument == "--sidecar" || argument == "--config" || argument == "--guid" ||
                   argument == "--sky" || argument == "--sky-intensity" || argument == "--sky-rotation" ||
                   argument == "--blender" || argument == "--blender-timeout" ||
                   argument == "--samples" || argument == "--bounces") {
            if (index >= argc) {
                throw std::runtime_error(argument + " requires a value");
            }
            value = argv[index++];
            if (argument == "--sidecar") {
                sidecar = value;
            } else if (argument == "--config") {
                configPath = value;
            } else if (argument == "--guid") {
                guid = value;
            } else if (argument == "--sky") {
                skyImage = value;
            } else if (argument == "--sky-intensity") {
                skyIntensity = std::stof(value);
                skyIntensitySet = true;
            } else if (argument == "--sky-rotation") {
                skyRotation = std::stof(value);
                skyRotationSet = true;
            } else if (argument == "--blender") {
                blenderExecutable = value;
            } else if (argument == "--blender-timeout") {
                blenderTimeoutSeconds = static_cast<uint32_t>(std::stoul(value));
            } else if (argument == "--samples") {
                samples = static_cast<uint32_t>(std::stoul(value));
            } else {
                bounces = static_cast<uint32_t>(std::stoul(value));
            }
        } else if (argument == "--scene") {
            if (index >= argc) {
                throw std::runtime_error("--scene requires a scene path");
            }
            gltf = argv[index++];
        } else if (argument == "--out") {
            if (index >= argc) {
                throw std::runtime_error("--out requires a directory");
            }
            outDirectory = argv[index++];
        } else if (argument.size() == 32u && isHexGuid(argument)) {
            guid = argument;
        } else {
            throw std::runtime_error("unknown --export-scene argument: " + argument);
        }
    }

    if (gltf.empty() || outDirectory.empty()) {
        throw std::runtime_error("scene export requires both --scene and --out");
    }
    if (samples == 0u || bounces == 0u) {
        throw std::runtime_error("--samples and --bounces must be positive");
    }
    if (!std::isfinite(skyIntensity) || skyIntensity < 0.0f) {
        throw std::runtime_error("--sky-intensity must be a finite non-negative number");
    }
    if (!std::isfinite(skyRotation)) {
        throw std::runtime_error("--sky-rotation must be finite");
    }
    if (guid.empty()) {
        guid = defaultSceneGuid(gltf);
    }
    if (!isHexGuid(guid)) {
        throw std::runtime_error("guid must be 32 hex characters");
    }

    DawnSceneExportOptions options;
    options.config = loadDawnConfigOrThrow(configPath.empty() ? defaultDawnConfigPath() : configPath);
    options.sidecar = sidecar;
    options.skyImage = skyImage;
    options.loadSidecar = loadSidecar;
    options.includeAllOpaque = includeAllObjects;
    options.skyIntensity = skyIntensity;
    options.skyIntensitySet = skyIntensitySet;
    options.skyRotationDegrees = skyRotation;
    options.skyRotationSet = skyRotationSet;
    options.samplesPerPixel = samples;
    options.maxBounces = bounces;
    options.blenderExecutable = blenderExecutable;
    options.blenderTimeoutSeconds = blenderTimeoutSeconds;
    DawnScenePackage package;
#if defined(HBG_DAWN_HAVE_PARCEL) && HBG_DAWN_HAVE_PARCEL
    if (isParcelScenePath(gltf)) {
        package = buildDawnScenePackageFromParcel(gltf, options);
    } else
#endif
    {
        package = buildDawnScenePackage(gltf, options);
    }
    const PackageFiles files = writeDawnPackage(outDirectory, guid, package.scene, package.jobs, &package.lightmap);
    std::printf("scene export: objects=%zu meshes=%zu atlases=%zu jobs=%zu guid=%s\n", package.objectCount,
                package.meshCount, package.atlasCount, package.jobs.lightmap2DJobs.size(), guid.c_str());
    std::cout << "wrote " << files.scene.string() << "\n";
    std::cout << "wrote " << files.jobs.string() << "\n";
    std::cout << "wrote " << files.lightmap.string() << "\n";
    return 0;
}

int sceneExportTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const auto near = [](float value, float expected, float tolerance = 1.0e-5f) {
        return std::fabs(value - expected) <= tolerance;
    };
    constexpr float kPi = 3.14159265358979323846f;

    const std::filesystem::path asset =
        std::filesystem::path(HBG_DAWN_PROJECT_ROOT) / "vfx/tests/assets/vfx_lit.gltf";
    require(std::filesystem::is_regular_file(asset), "scene export test asset is missing");

    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "hbg_dawnbake_scene_export_test";
    std::error_code removeError;
    std::filesystem::remove_all(directory, removeError);

    DawnSceneExportOptions options;
    options.config = loadDawnConfigOrThrow();
    options.samplesPerPixel = 8;
    options.maxBounces = 2;
    const DawnScenePackage package = buildDawnScenePackage(asset, options);
    require(package.objectCount > 0u, "scene export produced no objects");
    require(package.meshCount > 0u, "scene export produced no meshes");
    require(package.atlasCount > 0u, "scene export produced no atlases");
    require(package.jobs.lightmap2DJobs.size() == package.objectCount, "scene export job count mismatch");
    require(package.lightmap.objects.size() == package.objectCount, "scene export sidecar count mismatch");
    require(package.scene.meshInstances.size() == package.objectCount, "scene export instance count mismatch");

    const std::string guid = "CAFEBABECAFEBABECAFEBABECAFEBABE";
    const PackageFiles files = writeDawnPackage(directory, guid, package.scene, package.jobs, &package.lightmap);
    require(std::filesystem::is_regular_file(files.scene), "scene export did not write .scene");
    require(std::filesystem::is_regular_file(files.jobs), "scene export did not write .jobs");
    require(std::filesystem::is_regular_file(files.lightmap), "scene export did not write lightmap sidecar");

    const SceneInfo readBack = readSceneFile(files.scene);
    const BakingJobInputs jobsBack = readBakingJobInputsFile(files.jobs);
    require(readBack.meshes.size() == package.meshCount, "scene export mesh readback mismatch");
    require(readBack.meshInstances.size() == package.objectCount, "scene export instance readback mismatch");
    require(jobsBack.lightmap2DJobs.size() == package.objectCount, "scene export job readback mismatch");
    for (const MeshInstanceInfo& instance : readBack.meshInstances) {
        require(instance.header.uv2MapSize.x > 0u && instance.header.uv2MapSize.y > 0u,
                "scene export instance has no UV2 map size");
    }

    const std::filesystem::path sidecar = directory / "scene_export_test.hdrp.json";
    {
        std::ofstream file(sidecar);
        file << R"({"objects":[{"name":"Mesh","contributeGI":true},{"name":"ShadowCaster","contributeGI":false}],)"
                R"("sky":{"mode":"flat","instanceId":305419897,"ambientColor":[0.25,0.5,1.0],"intensity":2.0,"indirectMultiplier":0.5,"shadowTranslucency":0.75}})";
    }
    DawnSceneExportOptions giOptions = options;
    giOptions.sidecar = sidecar;
    const DawnScenePackage giPackage = buildDawnScenePackage(asset, giOptions);
    require(giPackage.objectCount == 1u, "contributeGI sidecar did not filter objects");
    require(giPackage.lightmap.objects.size() == 1u && giPackage.lightmap.objects[0].name == "Mesh",
            "contributeGI sidecar selected the wrong object");

    const SkyLightInfoHeader& flatSky = giPackage.scene.skyLight.skyData;
    require(near(flatSky.color.x, 0.5f) && near(flatSky.color.y, 1.0f) && near(flatSky.color.z, 2.0f),
            "flat sky color/intensity mismatch");
    require(flatSky.mipDimensions.x == 8 && flatSky.mipDimensions.y == 128 && flatSky.mipDimensions.z == 0,
            "flat sky mip dimensions mismatch");
    require(near(flatSky.indirectLightingIntensity, 0.5f), "flat sky indirect intensity mismatch");
    require(near(flatSky.shadowTranslucency, 0.75f), "flat sky shadow translucency mismatch");
    require(near(giPackage.scene.skyLight.envCube[0].x, 1.0f / kPi) &&
                near(giPackage.scene.skyLight.envCube[0].y, 1.0f / kPi) &&
                near(giPackage.scene.skyLight.envCube[0].z, 1.0f / kPi) &&
                near(giPackage.scene.skyLight.envCube[0].w, 1.0f / kPi),
            "flat sky env cube mismatch");
    require(giPackage.scene.lights.size() == 2u, "flat sidecar sky did not append a sky light");
    std::size_t flatSkyIndex = giPackage.scene.lights.size();
    for (std::size_t index = 0; index < giPackage.scene.lights.size(); ++index) {
        if ((giPackage.scene.lights[index].flags & kLightFlagTypeMask) == kLightFlagSky) {
            flatSkyIndex = index;
            break;
        }
    }
    require(flatSkyIndex < giPackage.scene.lights.size(), "sidecar sky light missing");
    const LightInfo& flatSkyLight = giPackage.scene.lights[flatSkyIndex];
    require((flatSkyLight.flags & kLightFlagTypeMask) == kLightFlagSky, "sidecar sky light type mismatch");
    require((flatSkyLight.flags & kLightFlagCastShadow) != 0u, "sidecar sky light cast shadow missing");
    require((flatSkyLight.flags & kLightFlagDirectLighting) != 0u,
            "sidecar sky light direct lighting missing");
    require(near(flatSkyLight.attenuation, 0.5f), "sidecar sky light attenuation mismatch");
    require(near(flatSkyLight.color.x, 0.5f) && near(flatSkyLight.color.y, 1.0f) &&
                near(flatSkyLight.color.z, 2.0f),
            "sidecar sky light color mismatch");
    require(near(flatSkyLight.indirectColor.x, 0.5f), "sidecar sky light indirect color mismatch");
    require(flatSkyLight.dimensions.x == 8.0f && flatSkyLight.dimensions.y == 128.0f,
            "sidecar sky light dimensions mismatch");
    require(giPackage.scene.lightGuids[flatSkyIndex].d == 305419897u,
            "sidecar sky light instanceId guid mismatch");
    require(giPackage.scene.lightPowers[flatSkyIndex] == 0.0f, "sidecar sky light power must be zero");
    require((giPackage.scene.lights.front().flags & kLightFlagTypeMask) == kLightFlagDirectional,
            "directional light must precede the sky light");

    {
        std::ofstream file(sidecar);
        file << R"({"objects":[{"name":"Mesh","contributeGI":true},{"name":"ShadowCaster","contributeGI":false}],)"
                R"("sky":{"mode":"trilight","skyColor":[0.2,0.4,0.8],"equatorColor":[0.1,0.2,0.3],)"
                R"("groundColor":[0.8,0.4,0.2],"lerpExp":1.0,"intensity":1.5,"indirectIntensity":0.25}})";
    }
    const DawnScenePackage gradientPackage = buildDawnScenePackage(asset, giOptions);
    const SkyLightInfoHeader& gradientSky = gradientPackage.scene.skyLight.skyData;
    require(near(gradientSky.color.x, 1.5f) && near(gradientSky.color.y, 1.5f) &&
                near(gradientSky.color.z, 1.5f),
            "trilight sky color/intensity mismatch");
    require(gradientSky.mipDimensions.x == 8 && gradientSky.mipDimensions.y == 128,
            "trilight sky mip dimensions mismatch");
    constexpr uint32_t kGradientFaceSize = 128u;
    constexpr uint32_t kGradientFacePixels = kGradientFaceSize * kGradientFaceSize;
    const Float4 topPixel =
        gradientPackage.scene.skyLight.envCube[2u * kGradientFacePixels + kGradientFaceSize * 64u + 64u];
    const Float4 bottomPixel =
        gradientPackage.scene.skyLight.envCube[3u * kGradientFacePixels + kGradientFaceSize * 64u + 64u];
    require(near(topPixel.x, 0.2f / kPi, 1.0e-3f) && near(topPixel.z, 0.8f / kPi, 1.0e-3f),
            "trilight top sky color mismatch");
    require(near(bottomPixel.x, 0.8f / kPi, 1.0e-3f) && near(bottomPixel.z, 0.2f / kPi, 1.0e-3f),
            "trilight ground color mismatch");
    const LightInfo* gradientSkyLight = nullptr;
    for (const LightInfo& light : gradientPackage.scene.lights) {
        if ((light.flags & kLightFlagTypeMask) == kLightFlagSky) {
            gradientSkyLight = &light;
            break;
        }
    }
    require(gradientSkyLight != nullptr, "trilight sky light missing");
    require(near(gradientSkyLight->attenuation, 0.25f),
            "trilight sky light attenuation mismatch");

    {
        std::ofstream file(sidecar);
        file << R"({"objects":[{"name":"Mesh","contributeGI":true},{"name":"ShadowCaster","contributeGI":false}],)"
                R"("sky":{"mode":"skybox","enabled":false}})";
    }
    const DawnScenePackage disabledSkyPackage = buildDawnScenePackage(asset, giOptions);
    require(disabledSkyPackage.scene.lights.size() == 1u, "disabled sidecar sky appended a sky light");
    require(near(disabledSkyPackage.scene.skyLight.skyData.color.x, 0.0f),
            "disabled sidecar sky must use the black placeholder");

    {
        std::ofstream file(sidecar);
        file << R"({"objects":[{"name":"Mesh","contributeGI":true},{"name":"ShadowCaster","contributeGI":false}],)"
                R"("sky":{"mode":"skybox"}})";
    }
    bool rejectedMissingSkyTexture = false;
    try {
        (void)buildDawnScenePackage(asset, giOptions);
    } catch (const std::exception&) {
        rejectedMissingSkyTexture = true;
    }
    require(rejectedMissingSkyTexture, "skybox sidecar without a texture was not rejected");

    {
        std::ofstream file(sidecar);
        file << R"({"objects":[{"name":"Mesh","contributeGI":false},{"name":"ShadowCaster","contributeGI":false}]})";
    }
    bool rejectedAllExcluded = false;
    try {
        (void)buildDawnScenePackage(asset, giOptions);
    } catch (const std::exception&) {
        rejectedAllExcluded = true;
    }
    require(rejectedAllExcluded, "sidecar excluded all objects but export did not fail");

    {
        std::ofstream file(sidecar);
        file << R"({"objects":[{"name":"Mesh","contributeGI":true},{"name":"ShadowCaster","contributeGI":true}],)"
                R"("lights":[{"name":"Sun","bakeMode":"mixed","mixedMode":"shadowmask","castShadows":true}]})";
    }
    const DawnScenePackage shadowPackage = buildDawnScenePackage(asset, giOptions);
    require(shadowPackage.scene.lights.size() == 1u, "shadowmask sidecar light count mismatch");
    require((shadowPackage.scene.lights[0].flags & kLightFlagTypeMask) == kLightFlagDirectional,
            "shadowmask sidecar light must be directional");
    require((shadowPackage.scene.lights[0].flags & kLightFlagDirectLighting) == 0u,
            "mixed shadowmask light must not bake direct lighting");
    require(shadowPackage.scene.meshInstances.size() == 2u, "shadowmask instance count mismatch");
    require(shadowPackage.jobs.lightmap2DJobs.size() == 2u, "shadowmask lightmap job count mismatch");
    require(shadowPackage.jobs.mappingGroupJobs.size() == 1u, "lightmap group packing count mismatch");
    require(shadowPackage.jobs.mappingGroupJobsCount == 1u, "lightmap group count mismatch");
    require(shadowPackage.jobs.partialJobsTotalNum.numMappingGroupJobs == 1u,
            "lightmap partial group count mismatch");
    {
        const MappingGroupInput& group = shadowPackage.jobs.mappingGroupJobs.front();
        require(group.jobId.a == 0u && group.jobId.b == 0u && group.jobId.c == 0u &&
                    group.jobId.d == 0xFFFFFFFFu,
                "lightmap group id mismatch");
        require(group.groupedSize == static_cast<int32_t>(giOptions.config.lightmap.taskPackSize),
                "lightmap group size mismatch");
        std::vector<int32_t> groupedJobs;
        for (const Int3& offset : group.mappingOffsets) {
            groupedJobs.push_back(offset.z);
        }
        std::sort(groupedJobs.begin(), groupedJobs.end());
        require(groupedJobs == std::vector<int32_t>{0, 1}, "lightmap group job indices mismatch");
    }
    require(shadowPackage.jobs.sdfShadowJobs.size() == 2u, "shadowmask SDF job count mismatch");
    require(shadowPackage.jobs.sdfShadowJobsCount == 2u, "shadowmask SDF count mismatch");
    require(shadowPackage.jobs.partialJobsTotalNum.numSdfShadowJobs == 2u,
            "shadowmask partial SDF count mismatch");
    require(shadowPackage.jobs.bakingParameters.sdfShadowParameter.maxUpsamplingFactor == 4 &&
                shadowPackage.jobs.bakingParameters.sdfShadowParameter.shadowRayFactor == 1.0f &&
                shadowPackage.jobs.bakingParameters.sdfShadowParameter.denoiserMode == 0 &&
                shadowPackage.jobs.bakingParameters.sdfShadowParameter.shadowMaskSmoothLevel == 1 &&
                shadowPackage.jobs.shadowSettingParameter.bAllowSignedDistanceFieldShadows == 1 &&
                shadowPackage.jobs.shadowSettingParameter.minDistanceFieldUpsampleFactor == 5,
            "shadowmask SDF baking parameters mismatch");
    for (std::size_t index = 0; index < shadowPackage.scene.meshInstances.size(); ++index) {
        const MeshInstanceInfo& instance = shadowPackage.scene.meshInstances[index];
        require(instance.relevantLights == std::vector<uint32_t>{0u},
                "shadowmask relevant light index mismatch");
        require((shadowPackage.jobs.lightmap2DJobs[index].flags & kLightmapFlagDirect) == 0u,
                "shadowmask lightmap job must not bake direct lighting");
        require((shadowPackage.jobs.lightmap2DJobs[index].flags & kLightmapFlagPadding) != 0u,
                "lightmap padding flag missing");

        const SdfShadowInput& shadow = shadowPackage.jobs.sdfShadowJobs[index];
        require(shadow.header.jobId.a == instance.header.guid.a &&
                    shadow.header.jobId.b == 1u && shadow.header.jobId.c == index &&
                    shadow.header.jobId.d == instance.header.guid.d,
                "shadowmask job id mismatch");
        require(shadow.header.meshId.a == instance.header.guid.a &&
                    shadow.header.meshId.b == instance.header.guid.b &&
                    shadow.header.meshId.c == instance.header.guid.c &&
                    shadow.header.meshId.d == instance.header.guid.d,
                "shadowmask mesh id mismatch");
        require((shadow.header.flags & kLightmapFlagBilinearFilter) != 0u,
                "shadowmask bilinear flag missing");
        require((shadow.header.flags & kLightmapFlagPadding) != 0u,
                "shadowmask padding flag missing");
        require((shadow.header.flags & kLightmapFlagPadding) ==
                    (shadowPackage.jobs.lightmap2DJobs[index].flags & kLightmapFlagPadding),
                "shadowmask/lightmap padding flags mismatch");
        require(shadow.header.size.x == shadowPackage.jobs.lightmap2DJobs[index].size.x &&
                    shadow.header.size.y == shadowPackage.jobs.lightmap2DJobs[index].size.y,
                "shadowmask size mismatch");
        require(shadow.header.upsampleFactor == 4 && shadow.header.lightmapUVIndex == 1,
                "shadowmask upsample/uv index mismatch");
        require(shadow.lights.size() == 1u && shadow.shadowmaskLights.size() == 1u,
                "shadowmask light guid count mismatch");
        const Guid& lightGuid = shadowPackage.scene.lightGuids[0];
        require(shadow.lights[0].a == lightGuid.a && shadow.lights[0].b == lightGuid.b &&
                    shadow.lights[0].c == lightGuid.c && shadow.lights[0].d == lightGuid.d,
                "shadowmask light guid mismatch");
        const Guid& maskGuid = shadow.shadowmaskLights[0];
        require(maskGuid.a == lightGuid.a && maskGuid.b == lightGuid.b &&
                    maskGuid.c == lightGuid.c && maskGuid.d == lightGuid.d,
                "shadowmask shadowmask-light guid mismatch");
    }
    require(shadowPackage.jobs.sdfShadowGroupJobs.size() == 1u,
            "shadowmask SDF group packing count mismatch");
    {
        const SdfShadowGroupInput& group = shadowPackage.jobs.sdfShadowGroupJobs.front();
        require(group.jobId.a == 102u && group.jobId.b == 1u && group.jobId.c == 0u &&
                    group.jobId.d == 0xFFFFFFFFu,
                "shadowmask SDF group id mismatch");
        require(group.groupSize == static_cast<int32_t>(giOptions.config.lightmap.taskPackSize),
                "shadowmask SDF group size mismatch");
        require(group.shadowJobIndices.size() == 2u && group.mappingOffsets.size() == 2u,
                "shadowmask SDF packed group job count mismatch");
        std::vector<int32_t> packedJobs = group.shadowJobIndices;
        std::sort(packedJobs.begin(), packedJobs.end());
        require(packedJobs == std::vector<int32_t>{0, 1}, "shadowmask SDF packed job indices mismatch");
        const MappingGroupInput& lightmapGroup = shadowPackage.jobs.mappingGroupJobs.front();
        for (const Int3& offset : group.mappingOffsets) {
            const auto lightmapOffset = std::find_if(
                lightmapGroup.mappingOffsets.begin(), lightmapGroup.mappingOffsets.end(),
                [&](const Int3& candidate) { return candidate.z == offset.z; });
            require(lightmapOffset != lightmapGroup.mappingOffsets.end() && lightmapOffset->x == offset.x &&
                        lightmapOffset->y == offset.y,
                    "shadowmask SDF packed offset must match lightmap group offset");
        }
    }

    const std::filesystem::path shadowDirectory = directory / "shadowmask";
    const PackageFiles shadowFiles =
        writeDawnPackage(shadowDirectory, "0123456789ABCDEF0123456789ABCDEF",
                         shadowPackage.scene, shadowPackage.jobs, &shadowPackage.lightmap);
    const SceneInfo shadowSceneBack = readSceneFile(shadowFiles.scene);
    const BakingJobInputs shadowJobsBack = readBakingJobInputsFile(shadowFiles.jobs);
    require(shadowSceneBack.meshInstances.size() == 2u &&
                shadowSceneBack.meshInstances[0].relevantLights == std::vector<uint32_t>{0u},
            "shadowmask relevant lights did not round-trip");
    require(shadowJobsBack.sdfShadowJobs.size() == 2u &&
                shadowJobsBack.sdfShadowJobs[1].header.jobId.c == 1u &&
                shadowJobsBack.sdfShadowJobs[1].lights.size() == 1u,
            "shadowmask SDF job did not round-trip");
    require(shadowJobsBack.sdfShadowGroupJobs.size() == 1u &&
                shadowJobsBack.sdfShadowGroupJobs[0].shadowJobIndices.size() == 2u &&
                shadowJobsBack.sdfShadowGroupJobs[0].mappingOffsets.size() == 2u &&
                shadowJobsBack.sdfShadowGroupJobs[0].groupSize ==
                    static_cast<int32_t>(giOptions.config.lightmap.taskPackSize),
            "shadowmask SDF group did not round-trip");
    require(shadowJobsBack.mappingGroupJobs.size() == 1u &&
                shadowJobsBack.mappingGroupJobs[0].mappingOffsets.size() == 2u &&
                shadowJobsBack.mappingGroupJobs[0].groupedSize ==
                    static_cast<int32_t>(giOptions.config.lightmap.taskPackSize),
            "lightmap group did not round-trip");

    std::printf("scene export test OK: objects=%zu meshes=%zu atlases=%zu jobs=%zu mapping_groups=%zu "
                "sdf_groups=%zu gi_objects=%zu sky=%s\n",
                package.objectCount, package.meshCount, package.atlasCount, package.jobs.lightmap2DJobs.size(),
                shadowPackage.jobs.mappingGroupJobs.size(), shadowPackage.jobs.sdfShadowGroupJobs.size(),
                giPackage.objectCount, "flat+trilight");
    std::filesystem::remove_all(directory, removeError);
    return 0;
}

#if defined(HBG_DAWN_HAVE_PARCEL) && HBG_DAWN_HAVE_PARCEL
int parcelImportTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const auto requireNear = [&require](float value, float expected, const char* message) {
        require(std::fabs(value - expected) <= 1.0e-5f, message);
    };
    const std::filesystem::path root = std::filesystem::path(HBG_DAWN_PROJECT_ROOT);
    const std::filesystem::path asset = root / "tests/assets/parcel_triangle.obj";
    DawnSceneExportOptions options;
    options.config = loadDawnConfigOrThrow();
    const DawnScenePackage package = buildDawnScenePackageFromParcel(asset, options);
    require(package.objectCount == 1u, "Parcel OBJ object count mismatch");
    require(package.meshCount == 1u, "Parcel OBJ mesh count mismatch");
    require(package.jobs.lightmap2DJobs.size() == 1u, "Parcel OBJ job count mismatch");

    for (const char* name : {"parcel_triangle.stl", "parcel_triangle.ply",
                             "parcel_triangle.dae", "parcel_triangle.3ds"}) {
        const DawnScenePackage formatPackage =
            buildDawnScenePackageFromParcel(root / "tests/assets" / name, options);
        require(formatPackage.objectCount == 1u, "Parcel format object count mismatch");
        require(formatPackage.meshCount == 1u, "Parcel format mesh count mismatch");
        require(formatPackage.jobs.lightmap2DJobs.size() == 1u,
                "Parcel format job count mismatch");
    }

#ifdef HBG_BLENDER_EXECUTABLE
    bool blendTested = false;
    const std::filesystem::path blendAsset = root / "tests/assets/parcel_triangle.blend";
    if (std::filesystem::is_regular_file(blendAsset) &&
        std::filesystem::is_regular_file(std::filesystem::path(HBG_BLENDER_EXECUTABLE))) {
        const DawnScenePackage blendPackage =
            buildDawnScenePackageFromParcel(blendAsset, options);
        require(blendPackage.objectCount == 1u, "Parcel BLEND object count mismatch");
        require(blendPackage.meshCount == 1u, "Parcel BLEND mesh count mismatch");
        require(blendPackage.jobs.lightmap2DJobs.size() == 1u, "Parcel BLEND job count mismatch");

        std::error_code relativeError;
        const std::filesystem::path relativeBlend =
            std::filesystem::relative(blendAsset, relativeError);
        if (!relativeError && relativeBlend.is_relative()) {
            const DawnScenePackage relativePackage =
                buildDawnScenePackageFromParcel(relativeBlend, options);
            require(relativePackage.objectCount == 1u,
                    "Parcel relative BLEND object count mismatch");
            require(relativePackage.meshCount == 1u, "Parcel relative BLEND mesh count mismatch");
            require(relativePackage.jobs.lightmap2DJobs.size() == 1u,
                    "Parcel relative BLEND job count mismatch");
        }

        const std::filesystem::path sidecarBlendDirectory =
            std::filesystem::temp_directory_path() / "hbg_dawnbake" / "blend_sidecar";
        std::error_code sidecarError;
        std::filesystem::remove_all(sidecarBlendDirectory, sidecarError);
        std::filesystem::create_directories(sidecarBlendDirectory, sidecarError);
        const std::filesystem::path sidecarBlend = sidecarBlendDirectory / "blend_sidecar.blend";
        const std::filesystem::path sidecarJson = sidecarBlendDirectory / "blend_sidecar.hdrp.json";
        if (!sidecarError &&
            std::filesystem::copy_file(blendAsset, sidecarBlend, sidecarError) &&
            !sidecarError &&
            std::filesystem::copy_file(root / "tests/assets/parcel_sidecar.hdrp.json", sidecarJson,
                                       sidecarError) &&
            !sidecarError) {
            const DawnScenePackage sidecarBlendPackage =
                buildDawnScenePackageFromParcel(sidecarBlend, options);
            require(sidecarBlendPackage.objectCount == 1u,
                    "Parcel BLEND sidecar object count mismatch");
            require(sidecarBlendPackage.scene.lights.size() == 1u,
                    "Parcel BLEND source sidecar was not applied");
            require(sidecarBlendPackage.scene.skyLight.envCube.size() == 6u * 128u * 128u,
                    "Parcel BLEND source sidecar cube size mismatch");
        }
        std::filesystem::remove_all(sidecarBlendDirectory, sidecarError);
        blendTested = true;
    }
#else
    constexpr bool blendTested = false;
#endif

    const std::filesystem::path sidecarScene = root / "tests/assets/parcel_sidecar.obj";
    const DawnScenePackage sidecarPackage =
        buildDawnScenePackageFromParcel(sidecarScene, options);
    require(sidecarPackage.objectCount == 1u, "Parcel sidecar object count mismatch");
    require(sidecarPackage.meshCount == 1u, "Parcel sidecar mesh count mismatch");
    require(sidecarPackage.jobs.lightmap2DJobs.size() == 1u, "Parcel sidecar job count mismatch");
    require(sidecarPackage.scene.lights.size() == 1u, "Parcel sidecar sky light missing");
    require(sidecarPackage.scene.skyLight.envCube.size() == 6u * 128u * 128u,
            "Parcel sidecar sky cube size mismatch");
    require(sidecarPackage.scene.skyLight.skyData.color.x == 0.5f &&
                sidecarPackage.scene.skyLight.skyData.color.y == 1.0f &&
                sidecarPackage.scene.skyLight.skyData.color.z == 1.5f,
            "Parcel sidecar sky color mismatch");

    const std::filesystem::path textureScene = root / "tests/assets/parcel_texture_scene.obj";
    const DawnScenePackage texturePackage =
        buildDawnScenePackageFromParcel(textureScene, options);
    require(texturePackage.objectCount == 1u, "Parcel PNG object count mismatch");
    require(texturePackage.meshCount == 1u, "Parcel PNG mesh count mismatch");
    require(texturePackage.jobs.lightmap2DJobs.size() == 1u, "Parcel PNG job count mismatch");
    require(!texturePackage.scene.materials.empty(), "Parcel PNG scene has no material");
    require(texturePackage.scene.materials.front().diffuseTexture.header.sizeX == 4u,
            "Parcel PNG texture was not imported");
    require(texturePackage.scene.materials.front().diffuseTexture.colors.size() == 16u,
            "Parcel PNG texture payload mismatch");
    require(!texturePackage.scene.meshInstances.empty() &&
                (texturePackage.scene.meshInstances.front().header.flags &
                 kMeshInstanceFlagUseEmissionLighting) != 0u,
            "Parcel emissive material did not set the instance emission flag");

    const std::filesystem::path giSidecarScene = root / "tests/assets/parcel_gi_scene.obj";
    DawnSceneExportOptions noSidecarOptions = options;
    noSidecarOptions.loadSidecar = false;
    const DawnScenePackage unfilteredGiPackage =
        buildDawnScenePackageFromParcel(giSidecarScene, noSidecarOptions);
    require(unfilteredGiPackage.objectCount == 2u,
            "Parcel GI fixture must contain two objects before filtering");
    const DawnScenePackage giSidecarPackage =
        buildDawnScenePackageFromParcel(giSidecarScene, options);
    require(giSidecarPackage.objectCount == 1u,
            "Parcel sidecar contributeGI did not filter objects");
    require(giSidecarPackage.jobs.lightmap2DJobs.size() == 1u,
            "Parcel sidecar contributeGI job count mismatch");

    const std::filesystem::path lightScene = root / "tests/assets/parcel_light.fbx";
    const DawnScenePackage lightPackage =
        buildDawnScenePackageFromParcel(lightScene, options);
    require(lightPackage.objectCount == 1u && lightPackage.jobs.lightmap2DJobs.size() == 1u,
            "Parcel light scene object/job mismatch");
    require(lightPackage.scene.lights.size() == 1u, "Parcel light was not imported");
    const LightInfo& parcelLight = lightPackage.scene.lights.front();
    require((parcelLight.flags & kLightFlagTypeMask) == kLightFlagPoint,
            "Parcel light type mismatch");
    require((parcelLight.flags & kLightFlagCastShadow) != 0u,
            "Parcel light sidecar castShadows missing");
    requireNear(parcelLight.attenuation, 0.25f, "Parcel light sidecar range mismatch");
    requireNear(parcelLight.dimensions.x, 0.75f, "Parcel light source radius mismatch");
    requireNear(parcelLight.dimensions.y, 0.75f, "Parcel light source soft radius mismatch");
    requireNear(parcelLight.dimensions.z, 1.25f, "Parcel light source length mismatch");
    requireNear(parcelLight.shadowTranslucency, 0.5f,
                "Parcel light sidecar shadowTranslucency mismatch");
    require(parcelLight.bEnablePenumbra == 1u, "Parcel light sidecar penumbra missing");
    requireNear(parcelLight.penumbraTint.x, 0.1f, "Parcel light penumbra tint mismatch");
    requireNear(parcelLight.penumbraLuma, 0.75f, "Parcel light penumbra luma mismatch");
    requireNear(parcelLight.color.x, 500.0f, "Parcel light color x was multiplied twice");
    requireNear(parcelLight.color.y, 250.0f, "Parcel light color y was multiplied twice");
    requireNear(parcelLight.color.z, 125.0f, "Parcel light color z was multiplied twice");
    requireNear(parcelLight.indirectColor.x, 125.0f,
                "Parcel light indirect multiplier x mismatch");
    requireNear(parcelLight.indirectColor.y, 62.5f,
                "Parcel light indirect multiplier y mismatch");
    requireNear(parcelLight.indirectColor.z, 31.25f,
                "Parcel light indirect multiplier z mismatch");
    require(parcelLight.iesTextureSlice == 0, "Parcel light cookie slice mismatch");
    require(parcelLight.lightTextureMode == 1, "Parcel light cookie mode mismatch");
    require(lightPackage.scene.cookieTextureDatas.size() == 1u,
            "Parcel light cookie texture missing");
    require(lightPackage.scene.cookieTextureDatas.front().header.sizeX == 8u &&
                lightPackage.scene.cookieTextureDatas.front().header.sizeY == 8u,
            "Parcel light cookie texture size mismatch");

    const std::filesystem::path iesScene = root / "tests/assets/parcel_ies.fbx";
    const DawnScenePackage iesPackage =
        buildDawnScenePackageFromParcel(iesScene, options);
    require(iesPackage.scene.lights.size() == 1u, "Parcel IES light was not imported");
    const LightInfo& iesLight = iesPackage.scene.lights.front();
    require((iesLight.flags & kLightFlagTypeMask) == kLightFlagPoint,
            "Parcel IES light type mismatch");
    require(iesLight.lightTextureMode == 1, "Parcel IES texture mode mismatch");
    require(iesLight.iesTextureSlice == 0, "Parcel IES texture slice mismatch");
    require(iesPackage.scene.cookieTextureDatas.size() == 1u,
            "Parcel IES cookie texture missing");
    const auto& iesCookie = iesPackage.scene.cookieTextureDatas.front();
    require(iesCookie.header.sizeX == 64u && iesCookie.header.sizeY == 64u,
            "Parcel IES point cookie size mismatch");
    const auto iesPixel = [&iesCookie](uint32_t x, uint32_t y) {
        return iesCookie.colors[static_cast<std::size_t>(y) * iesCookie.header.sizeX + x].x;
    };
    require(iesPixel(32, 63) > 0.85f, "Parcel IES cookie lost its up-facing intensity");
    require(iesPixel(32, 32) < 0.05f, "Parcel IES cookie should fall off at 90 degrees");
    require(iesPixel(32, 0) < 0.05f, "Parcel IES cookie should be dark downward");

    const std::filesystem::path iesSpotScene = root / "tests/assets/parcel_ies_spot.fbx";
    const DawnScenePackage iesSpotPackage =
        buildDawnScenePackageFromParcel(iesSpotScene, options);
    require(iesSpotPackage.scene.lights.size() == 1u, "Parcel IES spot was not imported");
    const LightInfo& iesSpotLight = iesSpotPackage.scene.lights.front();
    require((iesSpotLight.flags & kLightFlagTypeMask) == kLightFlagSpot,
            "Parcel IES spot type mismatch");
    require(iesSpotLight.lightTextureMode == 1, "Parcel IES spot texture mode mismatch");
    require(iesSpotLight.iesTextureSlice == 0, "Parcel IES spot texture slice mismatch");
    require(iesSpotPackage.scene.cookieTextureDatas.size() == 1u,
            "Parcel IES spot cookie texture missing");
    const auto& iesSpotCookie = iesSpotPackage.scene.cookieTextureDatas.front();
    require(iesSpotCookie.header.sizeX == 8u && iesSpotCookie.header.sizeY == 8u,
            "Parcel IES spot cookie size mismatch");
    const auto iesSpotPixel = [&iesSpotCookie](uint32_t x, uint32_t y) {
        return iesSpotCookie.colors[static_cast<std::size_t>(y) * iesSpotCookie.header.sizeX + x].x;
    };
    require(iesSpotPixel(3, 3) > 0.8f, "Parcel IES spot cookie lost central intensity");
    require(iesSpotPixel(0, 0) < 0.05f, "Parcel IES spot cookie should fall off at the edge");

    const std::filesystem::path areaLightScene = root / "tests/assets/parcel_area.fbx";
    const DawnScenePackage areaLightPackage =
        buildDawnScenePackageFromParcel(areaLightScene, options);
    require(areaLightPackage.objectCount == 1u &&
                areaLightPackage.jobs.lightmap2DJobs.size() == 1u,
            "Parcel area light scene object/job mismatch");
    require(areaLightPackage.scene.lights.size() == 1u, "Parcel area light was not imported");
    const LightInfo& areaLight = areaLightPackage.scene.lights.front();
    require((areaLight.flags & kLightFlagTypeMask) == kLightFlagRect,
            "Parcel area light type mismatch");
    requireNear(areaLight.dimensions.x, 2.0f, "Parcel area light width mismatch");
    requireNear(areaLight.dimensions.y, 2.0f, "Parcel area light height mismatch");
    requireNear(areaLight.dimensions.z, 1.0f, "Parcel area light attention radius mismatch");
    requireNear(areaLight.shaping.x, 0.70710678f, "Parcel area light barn door angle mismatch");
    requireNear(areaLight.shaping.y, 3.0f, "Parcel area light barn door length mismatch");
    requireNear(areaLight.attenuation, 0.2f, "Parcel area light range mismatch");
    require((areaLight.flags & kLightFlagNonInverseSquareFalloff) == 0u,
            "Parcel area light must be inverse-square");
    require((areaLight.flags & kLightFlagBakeryFakeFalloff) != 0u,
            "Parcel area light bakery-fake flag missing");
    requireNear(areaLight.penumbraTint.x, 0.0f,
                "Parcel area light disabled penumbra tint mismatch");
    requireNear(areaLight.penumbraLuma, 0.0f,
                "Parcel area light disabled penumbra luma mismatch");

    const std::filesystem::path discLightScene =
        root / "tests/assets/parcel_disc_area.fbx";
    const DawnScenePackage discLightPackage =
        buildDawnScenePackageFromParcel(discLightScene, options);
    require(discLightPackage.scene.lights.size() == 1u,
            "Parcel disc area light was not imported");
    const LightInfo& discLight = discLightPackage.scene.lights.front();
    require((discLight.flags & kLightFlagTypeMask) == kLightFlagDisc,
            "Parcel disc area light type mismatch");
    require((discLight.flags & kLightFlagCastShadow) != 0u,
            "Parcel disc area light castShadows missing");
    requireNear(discLight.dimensions.x, 2.0f, "Parcel disc area light width mismatch");
    requireNear(discLight.dimensions.y, 2.0f, "Parcel disc area light height mismatch");
    requireNear(discLight.dimensions.z, 10.0f,
                "Parcel disc area light attention diameter mismatch");
    requireNear(discLight.shaping.x, 0.70710678f,
                "Parcel disc area light barn door angle mismatch");
    requireNear(discLight.shaping.y, 3.0f,
                "Parcel disc area light barn door length mismatch");
    requireNear(discLight.attenuation, 0.2f, "Parcel disc area light range mismatch");
    require((discLight.flags & kLightFlagNonInverseSquareFalloff) == 0u,
            "Parcel disc area light must be inverse-square");
    require((discLight.flags & kLightFlagBakeryFalloff) != 0u,
            "Parcel disc area light bakery-physical flag missing");
    requireNear(discLight.penumbraTint.x, 0.0f,
                "Parcel disc area light disabled penumbra tint mismatch");
    requireNear(discLight.penumbraLuma, 0.0f,
                "Parcel disc area light disabled penumbra luma mismatch");

    const std::filesystem::path spotLightScene = root / "tests/assets/parcel_spot.fbx";
    const DawnScenePackage spotLightPackage =
        buildDawnScenePackageFromParcel(spotLightScene, options);
    require(spotLightPackage.scene.lights.size() == 1u, "Parcel spot light was not imported");
    const LightInfo& spotLight = spotLightPackage.scene.lights.front();
    require((spotLight.flags & kLightFlagTypeMask) == kLightFlagSpot,
            "Parcel spot light type mismatch");
    require((spotLight.flags & kLightFlagCastShadow) != 0u,
            "Parcel spot light castShadows missing");
    requireNear(spotLight.dimensions.x, 0.4f, "Parcel spot source radius mismatch");
    requireNear(spotLight.dimensions.y, 0.4f, "Parcel spot source soft radius mismatch");
    requireNear(spotLight.dimensions.z, 0.8f, "Parcel spot source length mismatch");
    requireNear(spotLight.shaping.x, 0.8660254f, "Parcel spot outer cone half-angle mismatch");
    requireNear(spotLight.shaping.y, 17.284851f, "Parcel spot cone falloff mismatch");
    requireNear(spotLight.attenuation, 1.0f / 6.0f, "Parcel spot range mismatch");

    const std::filesystem::path directionalLightScene =
        root / "tests/assets/parcel_directional.fbx";
    const DawnScenePackage directionalLightPackage =
        buildDawnScenePackageFromParcel(directionalLightScene, options);
    require(directionalLightPackage.scene.lights.size() == 1u,
            "Parcel directional light was not imported");
    const LightInfo& directionalLight = directionalLightPackage.scene.lights.front();
    require((directionalLight.flags & kLightFlagTypeMask) == kLightFlagDirectional,
            "Parcel directional light type mismatch");
    require((directionalLight.flags & kLightFlagCastShadow) != 0u,
            "Parcel directional light castShadows missing");
    requireNear(directionalLight.dimensions.x, 0.08715574f,
                "Parcel directional light source angle mismatch");
    requireNear(directionalLight.attenuation, 1.0f,
                "Parcel directional light attenuation mismatch");
    requireNear(directionalLight.shadowTranslucency, 0.25f,
                "Parcel directional light shadowTranslucency mismatch");

    const std::filesystem::path embeddedFbx =
        root / "tests/assets/parcel_embedded_texture.fbx";
    const DawnScenePackage embeddedPackage =
        buildDawnScenePackageFromParcel(embeddedFbx, options);
    require(embeddedPackage.objectCount == 1u, "Parcel embedded FBX object count mismatch");
    require(embeddedPackage.meshCount == 1u, "Parcel embedded FBX mesh count mismatch");
    require(!embeddedPackage.scene.materials.empty(), "Parcel embedded FBX has no material");
    require(embeddedPackage.scene.materials.front().diffuseTexture.header.sizeX > 1u,
            "Parcel embedded FBX texture was not imported");

    const std::filesystem::path embeddedExrFbx =
        root / "tests/assets/parcel_embedded_exr.fbx";
    const DawnScenePackage embeddedExrPackage =
        buildDawnScenePackageFromParcel(embeddedExrFbx, options);
    require(embeddedExrPackage.objectCount == 1u,
            "Parcel embedded EXR FBX object count mismatch");
    require(!embeddedExrPackage.scene.materials.empty(),
            "Parcel embedded EXR FBX has no material");
    const Texture2D<MaterialColor>& embeddedExrDiffuse =
        embeddedExrPackage.scene.materials.front().diffuseTexture;
    require(embeddedExrDiffuse.header.sizeX == 4u && embeddedExrDiffuse.header.sizeY == 4u,
            "Parcel embedded EXR texture was not imported");
    require(embeddedExrDiffuse.colors.size() == 16u,
            "Parcel embedded EXR texture payload mismatch");

    const std::filesystem::path isolatedExrDirectory =
        std::filesystem::temp_directory_path() / "hbg_dawnbake" / "embedded_exr";
    std::error_code isolatedError;
    std::filesystem::remove_all(isolatedExrDirectory, isolatedError);
    std::filesystem::create_directories(isolatedExrDirectory, isolatedError);
    const std::filesystem::path isolatedExrFbx = isolatedExrDirectory / "embedded_exr.fbx";
    std::filesystem::copy_file(embeddedExrFbx, isolatedExrFbx, isolatedError);
    require(!isolatedError, "Parcel embedded EXR fixture copy failed");
    const DawnScenePackage isolatedExrPackage =
        buildDawnScenePackageFromParcel(isolatedExrFbx, options);
    require(!isolatedExrPackage.scene.materials.empty(),
            "Parcel isolated embedded EXR has no material");
    const Texture2D<MaterialColor>& isolatedExrDiffuse =
        isolatedExrPackage.scene.materials.front().diffuseTexture;
    require(isolatedExrDiffuse.header.sizeX == 4u && isolatedExrDiffuse.header.sizeY == 4u &&
                isolatedExrDiffuse.colors.size() == 16u,
            "Parcel embedded EXR fallback requires an external texture");
    std::filesystem::remove_all(isolatedExrDirectory, isolatedError);

    const std::filesystem::path exrScene = root / "tests/assets/parcel_exr_scene.obj";
    const DawnScenePackage exrPackage = buildDawnScenePackageFromParcel(exrScene, options);
    require(exrPackage.objectCount == 1u, "Parcel EXR object count mismatch");
    require(!exrPackage.scene.materials.empty(), "Parcel EXR scene has no material");
    const Texture2D<MaterialColor>& exrDiffuse =
        exrPackage.scene.materials.front().diffuseTexture;
    require(exrDiffuse.header.sizeX == 4u, "Parcel EXR texture was not imported");
    require(exrDiffuse.colors.size() == 16u, "Parcel EXR texture payload mismatch");
    std::printf("Parcel import test OK: objects=%zu meshes=%zu jobs=%zu formats=stl+ply+dae+3ds "
                "blend=%s sidecar_light=%zu parcel_light=1 png_texture=%ux%u embedded_texture=%ux%u "
                "embedded_exr_texture=%ux%u exr_texture=%ux%u\n",
                package.objectCount, package.meshCount, package.jobs.lightmap2DJobs.size(),
                blendTested ? "tested" : "skipped", sidecarPackage.scene.lights.size(),
                texturePackage.scene.materials.front().diffuseTexture.header.sizeX,
                texturePackage.scene.materials.front().diffuseTexture.header.sizeY,
                embeddedPackage.scene.materials.front().diffuseTexture.header.sizeX,
                embeddedPackage.scene.materials.front().diffuseTexture.header.sizeY,
                embeddedExrDiffuse.header.sizeX, embeddedExrDiffuse.header.sizeY,
                exrPackage.scene.materials.front().diffuseTexture.header.sizeX,
                exrPackage.scene.materials.front().diffuseTexture.header.sizeY);
    return 0;
}
#endif

int t6PerfTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const DawnConfig config = loadDawnConfigOrThrow();
    T6Metrics metrics;
    const ExportedSceneBundle bundle = buildT6Scene(&metrics, config);
    require(metrics.objects == kT6ObjectCount, "T6 object count mismatch");
    require(metrics.atlasCount == 1u && metrics.atlasSize == kT6AtlasSize, "T6 must fit one configured atlas");
    require(metrics.atlasSize == config.lightmap.atlasSize, "T6 atlas size does not match dawn config");
    require(bundle.jobs.lightmap2DJobs.size() == kT6ObjectCount, "T6 lightmap job count mismatch");
    require(bundle.lightmap.objects.size() == kT6ObjectCount, "T6 sidecar object count mismatch");
    require(metrics.packMs < 2000.0, "T6 packing exceeded 2 s");
    require(metrics.vramBytes < kT6VramLimitBytes, "T6 lightmap VRAM exceeded 100 MiB");
    std::printf("t6 perf test OK: objects=%u pack_ms=%.3f atlas=%u count=%u vram_mib=%.2f vram_bytes=%llu\n",
                metrics.objects, metrics.packMs, metrics.atlasSize, metrics.atlasCount,
                static_cast<double>(metrics.vramBytes) / (1024.0 * 1024.0),
                static_cast<unsigned long long>(metrics.vramBytes));
    return 0;
}

int dawnConfigTest(const std::filesystem::path& path) {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const auto near = [](float value, float expected) {
        return std::fabs(value - expected) <= 1.0e-6f;
    };
    DawnConfig config;
    std::string error;
    if (!loadDawnConfig(path, config, error)) {
        throw std::runtime_error("failed to load dawn config: " + error);
    }
    require(config.version == 1, "dawn config version mismatch");
    require(!config.lightmap.uv2VFlip, "dawn config must not flip UV2 V");
    require(!config.lighting.lightPowerAffectsRadiance, "light power must not scale radiance");
    require(config.lightmap.format == "rgba16f", "dawn config lightmap format mismatch");
    require(config.lightmap.layers == 3u, "dawn config lightmap layer count mismatch");
    require(config.lighting.colorSpace == "linear", "dawn config color space mismatch");
    require(config.lighting.pointFalloff == "inverse-square", "dawn config point falloff mismatch");
    require(near(config.lighting.directionalLightmapScale, 1.0f), "directional lightmap scale mismatch");
    require(near(config.lighting.lightUnitScale, 1.0f), "light unit scale mismatch");

    LightmapUvOptions uvOptions;
    LightmapSizeSettings sizeSettings;
    LightmapPackingSettings packingSettings;
    LightmapAtlasOptions atlasOptions;
    require(near(config.lightmap.texelsPerUnit, uvOptions.texelsPerUnit), "UV texelsPerUnit mismatch");
    require(near(config.lightmap.texelsPerUnit, sizeSettings.texelsPerUnit), "size texelsPerUnit mismatch");
    require(config.lightmap.padding == uvOptions.padding, "UV padding mismatch");
    require(config.lightmap.maxLightmapSize == sizeSettings.maxLightmapSize, "max lightmap size mismatch");
    require(config.lightmap.atlasSize == packingSettings.atlasSize, "atlas size mismatch");
    require(config.lightmap.maxAtlases == packingSettings.maxAtlases, "max atlases mismatch");
    require(config.lightmap.padding == packingSettings.padding, "packing padding mismatch");
    require(config.lightmap.dilationIterations == atlasOptions.dilationIterations, "dilation count mismatch");
    require(config.lightmap.atlasSize == kT6AtlasSize, "dawn config atlas size no longer matches T6");
    require(config.lightmap.padding == 2u, "dawn config padding no longer matches T6");
    require(config.lightmap.splitLightmaps, "dawn config must enable oversized lightmap splitting");

    DawnConfig invalid;
    std::string invalidError;
    require(!parseDawnConfig("{}", invalid, invalidError), "empty dawn config was accepted");
    require(!parseDawnConfig(R"({"version":1,"unknown":true})", invalid, invalidError),
            "unknown dawn config key was accepted");
    require(!parseDawnConfig(R"({"version":1,"lightmap":{},"lighting":{}})", invalid, invalidError),
            "incomplete dawn config was accepted");

    std::printf("dawn config OK: path=%s atlas=%u texels_per_unit=%.1f padding=%u layers=%u\n", path.string().c_str(),
                config.lightmap.atlasSize, config.lightmap.texelsPerUnit, config.lightmap.padding,
                config.lightmap.layers);
    return 0;
}

int exportUvAxisPackage(const std::filesystem::path& directory, const std::string& guid) {
    if (!isHexGuid(guid)) {
        throw std::runtime_error("guid must be 32 hex characters");
    }
    const DawnConfig config = loadDawnConfigOrThrow();
    ExportedSceneBundle bundle = buildUvAxisScene(config);
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);
    std::cout << "wrote " << files.scene.string() << "\n";
    std::cout << "wrote " << files.jobs.string() << "\n";
    std::cout << "wrote " << files.lightmap.string() << "\n";
    return 0;
}

int meshExportTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    asset::Mesh mesh;
    mesh.name = "Quad";
    mesh.vertices.resize(4);

    const hbg::Vec3 positions[4] = {
        {-1.0f, 0.0f, -1.0f},
        {-1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, 1.0f},
        {1.0f, 0.0f, -1.0f},
    };
    const hbg::Vec2 uvs[4] = {
        {0.0f, 0.0f},
        {0.0f, 1.0f},
        {1.0f, 1.0f},
        {1.0f, 0.0f},
    };

    for (std::size_t i = 0; i < 4; ++i) {
        asset::MeshVertex& vertex = mesh.vertices[i];
        vertex.position = positions[i];
        vertex.normal = hbg::Vec3{0.0f, 1.0f, 0.0f};
        vertex.tangent = hbg::Vec4{1.0f, 0.0f, 0.0f, 1.0f};
        vertex.texcoord0 = uvs[i];
        vertex.texcoord1 = uvs[i];
    }

    mesh.indices = {0, 1, 2, 0, 2, 3};
    asset::SubMesh submesh;
    submesh.indexOffset = 0;
    submesh.indexCount = 6;
    submesh.materialIndex = 0;
    mesh.submeshes.push_back(submesh);
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;
    mesh.hasUv1 = true;
    mesh.recomputeBounds();

    MeshExportOptions options;
    options.castShadows = true;
    options.uv2MapSize = UInt2{64, 64};

    const MeshInfo info = exportMesh(mesh, 0, Guid{1, 0, 0, 0}, options);
    require(info.header.numTriangles == 2, "unexpected triangle count");
    require(info.header.numVertices == 4, "unexpected vertex count");
    require(info.header.numElements == 1, "unexpected element count");
    require(info.header.lightmapUVIndex == 1, "unexpected lightmap UV index");
    require(info.elements.size() == 1, "unexpected element array size");
    require(info.elements[0].primitiveCount == 2, "unexpected primitive count");
    require(info.elements[0].startIndex == 0, "unexpected start index");
    require(info.elements[0].materialIndex == 0xFFFFu, "element material index must be 0xFFFF");

    const std::vector<uint16_t> expectedIndices = {0, 2, 1, 0, 3, 2};
    require(info.index16 == expectedIndices, "index winding must swap the last two indices");
    require(info.index32.empty(), "expected 16-bit indices");

    require(info.tangents.size() == 4 && info.bitangents.size() == 4, "missing tangent buffers");
    require(info.tangents[0].x == 0.0f && info.tangents[0].y == 0.0f && info.tangents[0].z == 1.0f,
            "tangent must be cross(T, N)");
    require(info.bitangents[0].x == 1.0f && info.bitangents[0].y == 0.0f && info.bitangents[0].z == 0.0f,
            "bitangent must be the authored tangent");
    require(info.normals[0].x == 0.0f && info.normals[0].y == 1.0f && info.normals[0].z == 0.0f,
            "normal was not preserved");

    require(info.texcoords.size() == 3, "expected three texcoord buffers");
    require(info.texcoords[1][2].x == 1.0f && info.texcoords[1][2].y == 1.0f, "normalized UV2 was not preserved");

    render::RenderScene scene;
    scene.meshes.push_back(mesh);
    render::RenderObject object;
    object.name = "Quad";
    object.meshIndex = 0;
    object.world = hbg::Mat4::identity();
    object.worldBounds = mesh.bounds;

    options.useEmissionLighting = true;
    const MeshInstanceInfo instance = exportMeshInstance(mesh, object, 0, 0, options);
    require(instance.header.meshIndex == 0, "unexpected instance mesh index");
    require(instance.header.lightmapId == 0, "LightmapId must equal the mesh index");
    require((instance.header.flags & kMeshInstanceFlagShadowCast) != 0, "shadow cast flag missing");
    require((instance.header.flags & kMeshInstanceFlagUseEmissionLighting) != 0,
            "emission lighting flag missing");
    require(instance.materialOverrides.size() == 1 && instance.materialOverrides[0] == 0,
            "material override was not copied");
    require(instance.header.transform.mm[0] == 1.0f && instance.header.transform.mm[5] == 1.0f &&
                instance.header.transform.mm[10] == 1.0f && instance.header.transform.mm[15] == 1.0f,
            "instance transform is not identity");
    require(instance.header.boundingBox.min.x == mesh.bounds.min.x &&
                instance.header.boundingBox.max.z == mesh.bounds.max.z,
            "instance bounds were not copied");
    require(instance.header.uv2MapSize.x == 64 && instance.header.uv2MapSize.y == 64, "UV2MapSize was not copied");

    std::cout << "mesh export test OK\n";
    return 0;
}

int materialExportTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const auto requireNear = [](float value, float expected, const char* message) {
        if (std::fabs(value - expected) > 1.0e-3f) {
            throw std::runtime_error(message);
        }
    };

    asset::ImageAsset normalImage;
    normalImage.name = "Normal";
    normalImage.width = 1;
    normalImage.height = 1;
    normalImage.pixels = {128, 128, 255, 255};

    asset::ImageAsset metallicRoughnessImage;
    metallicRoughnessImage.name = "MetallicRoughness";
    metallicRoughnessImage.width = 1;
    metallicRoughnessImage.height = 1;
    metallicRoughnessImage.pixels = {0, 128, 255, 255};

    asset::ImageAsset grayImage;
    grayImage.name = "Gray";
    grayImage.width = 1;
    grayImage.height = 1;
    grayImage.pixels = {128, 128, 128, 255};

    render::RenderScene scene;
    scene.images = {normalImage, metallicRoughnessImage, grayImage};

    asset::Material material;
    material.name = "TestMaterial";
    material.alphaMode = asset::AlphaMode::Opaque;
    material.doubleSided = true;
    material.baseColorFactor = hbg::Vec4{0.5f, 0.25f, 0.75f, 1.0f};
    material.metallic = 0.5f;
    material.roughness = 0.5f;
    material.emissiveFactor = hbg::Vec3{2.0f, 0.0f, 0.0f};
    material.emissiveStrength = 1.0f;
    material.normalTexture.imageIndex = 0;
    material.metallicRoughnessTexture.imageIndex = 1;

    MaterialExportOptions options;
    const MaterialInfo info = exportMaterial(material, scene, options);
    require(info.header.blendMode == 0, "opaque blend mode mismatch");
    require(info.header.bTwoSided == 1, "two-sided flag mismatch");
    require((info.header.flags & kMaterialFlagTwoSide) != 0, "MATERIAL_FLAGS_TWOSIDE missing");
    require((info.header.flags & kMaterialFlagSurfaceDomain) == 0, "surface domain must be off");
    requireNear(info.header.opacityMaskClipValue, 0.0f, "opaque opacity mask must be zero");
    require(info.diffuseTexture.header.format == kTextureFormatUByte4, "diffuse format must be UBYTE4");
    require(info.diffuseTexture.header.sizeX == 1 && info.diffuseTexture.header.sizeY == 1,
            "diffuse constant size mismatch");
    require(info.diffuseTexture.colors[0].x == 128 && info.diffuseTexture.colors[0].y == 64 &&
                info.diffuseTexture.colors[0].z == 191 && info.diffuseTexture.colors[0].w == 255,
            "diffuse base color factor was not quantized as expected");
    require(info.specularTexture.colors[0].x == 128 && info.specularTexture.colors[0].y == 64 &&
                info.specularTexture.colors[0].z == 128 && info.specularTexture.colors[0].w == 255,
            "specular packing mismatch");
    require(info.normalTexture.colors[0].x == 128 && info.normalTexture.colors[0].y == 128 &&
                info.normalTexture.colors[0].z == 255 && info.normalTexture.colors[0].w == 255,
            "normal map encoding mismatch");
    requireNear(info.header.emissiveBoost, 2.0f / 3.14159265358979323846f, "emissive boost mismatch");
    require(info.emissiveTexture.colors[0].x == 255 && info.emissiveTexture.colors[0].y == 0 &&
                info.emissiveTexture.colors[0].z == 0,
            "emissive texture was not normalized");
    require(info.transmissionTexture.colors.empty(), "opaque material must not have transmission");

    MaterialExportOptions flipOptions;
    flipOptions.flipNormalY = true;
    const MaterialInfo flipped = exportMaterial(material, scene, flipOptions);
    require(flipped.normalTexture.colors[0].y == 127, "normal Y flip mismatch");

    asset::Material masked = material;
    masked.alphaMode = asset::AlphaMode::Mask;
    masked.alphaCutoff = 0.3f;
    masked.baseColorFactor = hbg::Vec4{1.0f, 1.0f, 1.0f, 0.5f};
    masked.emissiveFactor = hbg::Vec3{0.0f, 0.0f, 0.0f};
    masked.normalTexture = {};
    masked.metallicRoughnessTexture = {};
    const MaterialInfo maskInfo = exportMaterial(masked, scene, options);
    require(maskInfo.header.blendMode == 1, "mask blend mode mismatch");
    requireNear(maskInfo.header.opacityMaskClipValue, 0.3f, "mask cutoff mismatch");
    require((maskInfo.header.flags & kMaterialFlagSurfaceDomain) != 0, "mask surface domain missing");
    require(maskInfo.transmissionTexture.colors[0].x == 128 && maskInfo.transmissionTexture.colors[0].y == 128 &&
                maskInfo.transmissionTexture.colors[0].z == 128 && maskInfo.transmissionTexture.colors[0].w == 128,
            "mask transmission must use the alpha channel");

    asset::Material blended = masked;
    blended.alphaMode = asset::AlphaMode::Blend;
    const MaterialInfo blendInfo = exportMaterial(blended, scene, options);
    require(blendInfo.header.blendMode == 2, "blend blend mode mismatch");
    require(blendInfo.transmissionTexture.colors[0].x == 128 && blendInfo.transmissionTexture.colors[0].w == 128,
            "blend transmission mismatch");

    asset::Material tinted = material;
    tinted.baseColorFactor = hbg::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    tinted.baseColorTexture.imageIndex = 2;
    tinted.normalTexture = {};
    tinted.metallicRoughnessTexture = {};
    tinted.emissiveFactor = hbg::Vec3{0.0f, 0.0f, 0.0f};
    const MaterialInfo tintedInfo = exportMaterial(tinted, scene, options);
    require(tintedInfo.diffuseTexture.colors[0].x == 55, "base color sRGB to linear conversion mismatch");

    std::cout << "material export test OK\n";
    return 0;
}

int lightExportTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const auto requireNear = [](float value, float expected, const char* message) {
        if (std::fabs(value - expected) > 1.0e-3f) {
            throw std::runtime_error(message);
        }
    };
    const auto requireFlag = [](uint32_t flags, uint32_t flag, bool expected, const char* message) {
        if (((flags & flag) != 0) != expected) {
            throw std::runtime_error(message);
        }
    };

    LightExportOptions options;
    options.indirectIntensity = 0.5f;

    render::Light directional;
    directional.name = "Sun";
    directional.type = render::LightType::Directional;
    directional.color = hbg::Vec3{0.5f, 0.25f, 0.75f};
    directional.intensity = 2.0f;
    directional.castShadows = true;
    directional.shadowTranslucency = 0.35f;
    directional.enablePenumbra = true;
    directional.penumbraTint = hbg::Vec4{0.1f, 0.2f, 0.3f, 0.4f};
    directional.penumbraLuma = 0.75f;
    const LightFullInfo sun = exportLight(directional, options, 0);
    require((sun.lightData.flags & kLightFlagTypeMask) == kLightFlagDirectional, "directional type flag mismatch");
    requireFlag(sun.lightData.flags, kLightFlagCastShadow, true, "directional shadow flag missing");
    requireFlag(sun.lightData.flags, kLightFlagNonInverseSquareFalloff, true,
                "directional non-inverse-square flag missing");
    requireFlag(sun.lightData.flags, kLightFlagHasStaticLighting, true, "static lighting flag missing");
    requireFlag(sun.lightData.flags, kLightFlagIndirectLighting, true, "indirect lighting flag missing");
    requireFlag(sun.lightData.flags, kLightFlagIndirectLightingProbe, true, "indirect probe flag missing");
    requireFlag(sun.lightData.flags, kLightFlagDirectLighting, true, "direct lighting flag missing");
    requireFlag(sun.lightData.flags, kLightFlagDirectLightingProbe, true, "direct probe flag missing");
    requireNear(sun.lightData.normal.z, 1.0f, "directional normal must point toward the light");
    requireNear(sun.lightData.color.x, 1.0f, "directional color x mismatch");
    requireNear(sun.lightData.color.y, 0.5f, "directional color y mismatch");
    requireNear(sun.lightData.color.z, 1.5f, "directional color z mismatch");
    requireNear(sun.lightData.indirectColor.x, 0.5f, "directional indirect color mismatch");
    requireNear(sun.lightData.attenuation, 1.0f, "directional attenuation mismatch");
    requireNear(sun.lightData.shadowTranslucency, 0.35f, "directional shadow translucency mismatch");
    require(sun.lightData.bEnablePenumbra == 1u, "directional penumbra flag mismatch");
    requireNear(sun.lightData.penumbraTint.x, 0.1f, "directional penumbra tint x mismatch");
    requireNear(sun.lightData.penumbraTint.w, 0.4f, "directional penumbra tint w mismatch");
    requireNear(sun.lightData.penumbraLuma, 0.75f, "directional penumbra luma mismatch");
    requireNear(sun.lightData.dimensions.x, 0.0f, "directional source angle mismatch");
    requireNear(sun.lightPower, 0.38175f, "directional light power mismatch");
    require(sun.lightGuid.d != 0u, "directional light guid missing");

    render::Light idLight = directional;
    idLight.instanceId = 0x12345678u;
    const LightFullInfo idLightInfo = exportLight(idLight, options, 0);
    require(idLightInfo.lightGuid.d == 0x12345678u,
            "explicit light instanceId did not populate LightGuid.D");

    render::Light warmSun = directional;
    warmSun.useColorTemperature = true;
    warmSun.colorTemperature = 2000.0f;
    const LightFullInfo warmSunInfo = exportLight(warmSun, options, 0);
    requireNear(warmSunInfo.lightData.color.x, 1.0f,
                "color temperature changed red channel");
    requireNear(warmSunInfo.lightData.color.y, 0.2683693f,
                "color temperature green channel mismatch");
    requireNear(warmSunInfo.lightData.color.z, 0.0817886f,
                "color temperature blue channel mismatch");

    render::Light hardSun = directional;
    hardSun.shadowType = render::LightShadowType::Hard;
    const LightFullInfo hardSunInfo = exportLight(hardSun, options, 0);
    requireNear(hardSunInfo.lightData.dimensions.x, 0.0f,
                "hard-shadow directional source angle must be zero");
    requireFlag(hardSunInfo.lightData.flags, kLightFlagCastShadow, true,
                "hard-shadow directional cast flag missing");

    render::Light hardPoint;
    hardPoint.name = "HardPoint";
    hardPoint.castShadows = true;
    hardPoint.shadowType = render::LightShadowType::Hard;
    hardPoint.sourceRadius = 0.5f;
    hardPoint.sourceLength = 1.0f;
    const LightFullInfo hardPointInfo = exportLight(hardPoint, options, 0);
    requireNear(hardPointInfo.lightData.dimensions.x, 0.0f,
                "hard-shadow point radius must be zero");
    requireNear(hardPointInfo.lightData.dimensions.z, 0.0f,
                "hard-shadow point length must be zero");

    render::Light noShadow = hardPoint;
    noShadow.shadowType = render::LightShadowType::None;
    const LightFullInfo noShadowInfo = exportLight(noShadow, options, 0);
    requireFlag(noShadowInfo.lightData.flags, kLightFlagCastShadow, false,
                "shadowType none must clear cast-shadow flag");

    render::Light mixedBake;
    mixedBake.name = "MixedBake";
    mixedBake.bakeMode = render::LightBakeMode::Mixed;
    mixedBake.lightingChannel = 2;
    const LightFullInfo mixedShadowMask = exportLight(mixedBake, options, 0);
    requireFlag(mixedShadowMask.lightData.flags, kLightFlagStationary, true,
                "mixed shadowmask stationary flag missing");
    requireFlag(mixedShadowMask.lightData.flags, kLightFlagDistanceFieldShadow, true,
                "mixed shadowmask distance-field flag missing");
    requireFlag(mixedShadowMask.lightData.flags, kLightFlagDirectLighting, false,
                "mixed shadowmask must not bake direct lighting");
    require((mixedShadowMask.lightData.flags & kLightFlagLightingChannelMask) == 2u,
            "mixed shadowmask lighting channel mismatch");

    mixedBake.mixedMode = render::LightMixedMode::IndirectOnly;
    const LightFullInfo mixedIndirectOnly = exportLight(mixedBake, options, 0);
    requireFlag(mixedIndirectOnly.lightData.flags, kLightFlagStationary, true,
                "mixed indirect-only stationary flag missing");
    requireFlag(mixedIndirectOnly.lightData.flags, kLightFlagDistanceFieldShadow, false,
                "mixed indirect-only must not set distance-field shadow");
    requireFlag(mixedIndirectOnly.lightData.flags, kLightFlagDirectLighting, false,
                "mixed indirect-only must not bake direct lighting");

    render::Light realtimeBake;
    realtimeBake.name = "RealtimeBake";
    realtimeBake.bakeMode = render::LightBakeMode::Realtime;
    const LightFullInfo realtimeInfo = exportLight(realtimeBake, options, 0);
    requireFlag(realtimeInfo.lightData.flags, kLightFlagStationary, false,
                "realtime light must not set stationary");
    requireFlag(realtimeInfo.lightData.flags, kLightFlagDirectLighting, false,
                "realtime light must not bake direct lighting");

    render::Light wideSun = directional;
    wideSun.lightSourceAngle = 10.0f;
    const LightFullInfo wideSunInfo = exportLight(wideSun, options, 0);
    requireNear(wideSunInfo.lightData.dimensions.x, 0.08715574f,
                "directional per-light source angle mismatch");

    const LightFullInfo sunAgain = exportLight(directional, options, 0);
    require(sunAgain.lightGuid.d == sun.lightGuid.d, "light guid must be stable");

    render::Light point;
    point.name = "Lamp";
    point.type = render::LightType::Point;
    point.color = hbg::Vec3{1.0f, 1.0f, 1.0f};
    point.intensity = 4.0f;
    point.range = 8.0f;
    point.world.values[12] = 1.0f;
    point.world.values[13] = 2.0f;
    point.world.values[14] = 3.0f;
    const LightFullInfo lamp = exportLight(point, options, 1);
    require((lamp.lightData.flags & kLightFlagTypeMask) == kLightFlagPoint, "point type flag mismatch");
    requireFlag(lamp.lightData.flags, kLightFlagNonInverseSquareFalloff, false,
                "point inverse-square flag mismatch");
    requireNear(lamp.lightData.attenuation, 0.125f, "point attenuation mismatch");
    requireNear(lamp.lightData.falloffExponent, 1.0f, "point falloff exponent mismatch");
    requireNear(lamp.lightData.normal.z, -1.0f, "point normal mismatch");
    requireNear(lamp.lightData.boundMin.x, -7.0f, "point bound min x mismatch");
    requireNear(lamp.lightData.boundMax.y, 10.0f, "point bound max y mismatch");
    requireNear(lamp.lightData.shadowTranslucency, 1.0f, "point default shadow translucency mismatch");
    require(lamp.lightData.bEnablePenumbra == 0u, "point default penumbra flag mismatch");
    requireNear(lamp.lightData.penumbraTint.x, 0.0f, "point default penumbra tint mismatch");
    requireNear(lamp.lightData.penumbraLuma, 0.0f, "point default penumbra luma mismatch");
    requireNear(lamp.lightPower, 2.0f, "point light power mismatch");

    render::Light dimIndirect = point;
    dimIndirect.indirectMultiplier = 0.25f;
    const LightFullInfo dimIndirectInfo = exportLight(dimIndirect, options, 1);
    requireNear(dimIndirectInfo.lightData.indirectColor.x, 0.5f,
                "per-light indirect multiplier mismatch");
    requireNear(dimIndirectInfo.lightData.color.x, 4.0f,
                "per-light indirect multiplier changed direct color");

    render::Light bakery = point;
    bakery.falloffMode = render::LightFalloffMode::BakeryPhysical;
    const LightFullInfo bakeryInfo = exportLight(bakery, options, 1);
    requireFlag(bakeryInfo.lightData.flags, kLightFlagNonInverseSquareFalloff, true,
                "bakery-physical non-inverse-square flag missing");
    requireFlag(bakeryInfo.lightData.flags, kLightFlagBakeryFalloff, true,
                "bakery-physical falloff flag missing");
    requireFlag(bakeryInfo.lightData.flags, kLightFlagBakeryFakeFalloff, false,
                "bakery-physical must not set fake falloff");

    render::Light fakeFalloff = point;
    fakeFalloff.falloffMode = render::LightFalloffMode::BakeryFake;
    const LightFullInfo fakeFalloffInfo = exportLight(fakeFalloff, options, 1);
    requireFlag(fakeFalloffInfo.lightData.flags, kLightFlagNonInverseSquareFalloff, true,
                "bakery-fake non-inverse-square flag missing");
    requireFlag(fakeFalloffInfo.lightData.flags, kLightFlagBakeryFakeFalloff, true,
                "bakery-fake falloff flag missing");

    render::Light spot;
    spot.name = "Spot";
    spot.type = render::LightType::Spot;
    spot.color = hbg::Vec3{1.0f, 1.0f, 1.0f};
    spot.intensity = 1.0f;
    spot.range = 10.0f;
    spot.innerConeAngle = 0.0f;
    spot.outerConeAngle = 0.78539816339f;
    spot.world.values[12] = 1.0f;
    LightExportOptions spotOptions = options;
    spotOptions.sourceRadius = 0.5f;
    spotOptions.sourceLength = 1.0f;
    const LightFullInfo spotInfo = exportLight(spot, spotOptions, 2);
    require((spotInfo.lightData.flags & kLightFlagTypeMask) == kLightFlagSpot, "spot type flag mismatch");
    requireNear(spotInfo.lightData.dimensions.x, 0.5f, "spot source radius mismatch");
    requireNear(spotInfo.lightData.dimensions.z, 1.0f, "spot source length mismatch");
    requireNear(spotInfo.lightData.shaping.x, 0.70710678f, "spot cos outer mismatch");
    requireNear(spotInfo.lightData.shaping.y, 3.41421356f, "spot cone falloff mismatch");
    requireNear(spotInfo.lightData.attenuation, 0.1f, "spot attenuation mismatch");
    requireNear(spotInfo.lightData.falloffExponent, 8.0f, "spot falloff exponent mismatch");
    require(spotInfo.lightData.boundMin.x < 0.0f, "spot bounds must expand sideways");

    render::Light rect;
    rect.name = "Rect";
    rect.type = render::LightType::Area;
    rect.areaShape = render::LightAreaShape::Rectangle;
    rect.areaWidth = 2.0f;
    rect.areaHeight = 1.0f;
    rect.range = 5.0f;
    const LightFullInfo rectInfo = exportLight(rect, options, 3);
    require((rectInfo.lightData.flags & kLightFlagTypeMask) == kLightFlagRect, "rect type flag mismatch");
    requireFlag(rectInfo.lightData.flags, kLightFlagNonInverseSquareFalloff, false,
                "rect must be inverse-square");
    requireNear(rectInfo.lightData.dimensions.x, 2.0f, "rect width mismatch");
    requireNear(rectInfo.lightData.dimensions.y, 1.0f, "rect height mismatch");
    requireNear(rectInfo.lightData.dimensions.z, 1.0f, "rect depth mismatch");
    requireNear(rectInfo.lightData.shaping.x, 0.03489950f, "rect barn door angle mismatch");
    requireNear(rectInfo.lightData.attenuation, 0.2f, "rect attenuation mismatch");

    render::Light disc;
    disc.name = "Disc";
    disc.type = render::LightType::Area;
    disc.areaShape = render::LightAreaShape::Disc;
    disc.areaWidth = 3.0f;
    disc.range = 5.0f;
    const LightFullInfo discInfo = exportLight(disc, options, 4);
    require((discInfo.lightData.flags & kLightFlagTypeMask) == kLightFlagDisc, "disc type flag mismatch");
    requireNear(discInfo.lightData.dimensions.x, 3.0f, "disc width mismatch");
    requireNear(discInfo.lightData.dimensions.z, 10.0f, "disc attention radius mismatch");

    render::Light tube;
    tube.name = "Tube";
    tube.type = render::LightType::Area;
    tube.areaShape = render::LightAreaShape::Tube;
    tube.areaWidth = 2.0f;
    tube.areaHeight = 0.5f;
    tube.range = 5.0f;
    const LightFullInfo tubeInfo = exportLight(tube, options, 5);
    require((tubeInfo.lightData.flags & kLightFlagTypeMask) == kLightFlagRect, "tube fallback type mismatch");

    LightExportOptions linearOptions;
    linearOptions.srgbToLinear = true;
    render::Light srgb;
    srgb.name = "Srgb";
    srgb.type = render::LightType::Point;
    srgb.color = hbg::Vec3{0.5f, 0.5f, 0.5f};
    srgb.intensity = 1.0f;
    srgb.range = 1.0f;
    const LightFullInfo srgbInfo = exportLight(srgb, linearOptions, 6);
    requireNear(srgbInfo.lightData.color.x, 0.21404114f, "light sRGB to linear mismatch");

    std::cout << "light export test OK\n";
    return 0;
}

int skyExportTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const auto requireNear = [](float value, float expected, const char* message) {
        if (std::fabs(value - expected) > 1.0e-5f) {
            throw std::runtime_error(message);
        }
    };
    constexpr float kPi = 3.14159265358979323846f;

    asset::ImageAsset source;
    source.name = "Sky";
    source.width = 1;
    source.height = 1;
    source.format = gfx::TextureFormat::RGBA32F;
    const float texel[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    source.pixels.resize(sizeof(texel));
    std::memcpy(source.pixels.data(), texel, sizeof(texel));

    SkyLightExportOptions options;
    options.cubeSize = 2;
    options.color = hbg::Vec3{0.5f, 0.5f, 0.5f};
    options.intensity = 2.0f;
    options.indirectMultiplier = 0.5f;
    options.indirectIntensity = 0.5f;
    options.srgbToLinear = false;
    const SkyLightInfo sky = exportSkyLight(&source, options);
    require(sky.skyData.mipDimensions.x == 2 && sky.skyData.mipDimensions.y == 2 && sky.skyData.mipDimensions.z == 0,
            "sky mip dimensions mismatch");
    require(sky.envCube.size() == 24, "sky cubemap pixel count mismatch");
    requireNear(sky.skyData.color.x, 1.0f, "sky color mismatch");
    requireNear(sky.skyData.indirectLightingIntensity, 0.25f, "sky indirect intensity mismatch");
    for (const Float4& pixel : sky.envCube) {
        requireNear(pixel.x, 0.25f / kPi, "sky +X/R sample mismatch");
        requireNear(pixel.y, 0.5f / kPi, "sky G sample mismatch");
        requireNear(pixel.z, 0.75f / kPi, "sky B sample mismatch");
        requireNear(pixel.w, 1.0f / kPi, "sky alpha mismatch");
    }

    SkyLightExportOptions rotatedOptions = options;
    rotatedOptions.rotationEulerDegrees = hbg::Vec3{0.0f, 90.0f, 0.0f};
    const SkyLightInfo rotated = exportSkyLight(&source, rotatedOptions);
    requireNear(rotated.skyData.transform.mm[0], 0.0f, "sky rotation cos mismatch");
    requireNear(rotated.skyData.transform.mm[2], 1.0f, "sky rotation +sin mismatch");
    requireNear(rotated.skyData.transform.mm[8], -1.0f, "sky rotation -sin mismatch");

    asset::ImageAsset srgbSource;
    srgbSource.name = "SrgbSky";
    srgbSource.width = 1;
    srgbSource.height = 1;
    srgbSource.format = gfx::TextureFormat::RGBA8;
    srgbSource.pixels = {128, 128, 128, 255};
    SkyLightExportOptions srgbOptions;
    srgbOptions.cubeSize = 1;
    const SkyLightInfo srgb = exportSkyLight(&srgbSource, srgbOptions);
    requireNear(srgb.envCube[0].x, 0.21586050f / kPi, "sky sRGB to linear mismatch");

    const SkyLightInfo empty = exportSkyLight(nullptr, options);
    require(empty.skyData.mipDimensions.x == 1 && empty.skyData.mipDimensions.y == 1,
            "empty sky mip dimensions mismatch");
    require(empty.envCube.size() == 6, "empty sky pixel count mismatch");
    for (const Float4& pixel : empty.envCube) {
        requireNear(pixel.x, 0.0f, "empty sky must be black");
        requireNear(pixel.w, 0.0f, "empty sky alpha must be zero");
    }

    SkyLightExportOptions mipOptions = options;
    mipOptions.cubeSize = 8;
    const SkyLightInfo mipSky = exportSkyLight(&source, mipOptions);
    require(mipSky.skyData.mipDimensions.x == 4 && mipSky.skyData.mipDimensions.y == 8, "sky mip count mismatch");

    std::cout << "sky export test OK\n";
    return 0;
}

int uvUnwrapTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const auto requireNear = [](float value, float expected, const char* message) {
        if (std::fabs(value - expected) > 1.0e-3f) {
            throw std::runtime_error(message);
        }
    };

    asset::Mesh mesh;
    mesh.name = "UvQuad";
    mesh.vertices.resize(4);
    const hbg::Vec3 positions[4] = {
        {-1.0f, 0.0f, -0.5f},
        {-1.0f, 0.0f, 0.5f},
        {1.0f, 0.0f, 0.5f},
        {1.0f, 0.0f, -0.5f},
    };
    for (std::size_t index = 0; index < 4; ++index) {
        asset::MeshVertex& vertex = mesh.vertices[index];
        vertex.position = positions[index];
        vertex.normal = hbg::Vec3{0.0f, 1.0f, 0.0f};
        vertex.texcoord0 = hbg::Vec2{static_cast<float>(index & 1u), static_cast<float>((index >> 1u) & 1u)};
    }
    mesh.indices = {0, 1, 2, 0, 2, 3};
    mesh.submeshes.push_back(asset::SubMesh{0, 6, 0});
    mesh.hasNormals = true;
    mesh.hasUv0 = true;
    mesh.generateTangents();
    mesh.recomputeBounds();

    LightmapUvOptions options;
    options.texelsPerUnit = 6.0f;
    options.resolution = 0;
    options.padding = 2;
    const LightmapUvLayout layout = unwrapLightmapUv(mesh, options);

    require(layout.atlasWidth > 0u && layout.atlasHeight > 0u, "xatlas atlas is empty");
    // Measured with xatlas f700c779: resolution=0 + texelsPerUnit>0 produces one
    // automatically-sized atlas and reports atlasCount==1.
    std::printf("uv unwrap: atlasCount=%u width=%u height=%u charts=%u\n", layout.atlasCount, layout.atlasWidth,
                layout.atlasHeight, layout.chartCount);
    require(layout.atlasCount == 1u, "resolution=0 should produce one automatically-sized atlas");
    require(layout.chartCount > 0u, "xatlas produced no charts");
    require(layout.mesh.vertices.size() >= mesh.vertices.size(), "xatlas lost vertices");
    require(layout.mesh.indices.size() == mesh.indices.size(), "xatlas changed triangle count");
    require(layout.mesh.submeshes.size() == 1u && layout.mesh.submeshes[0].indexCount == 6u,
            "xatlas changed submesh ranges");
    require(layout.sourceVertex.size() == layout.mesh.vertices.size(), "source vertex map size mismatch");
    require(layout.uvTexels.size() == layout.mesh.vertices.size(), "raw UV count mismatch");
    require(layout.mesh.hasUv1, "unwrapped mesh must have UV1");
    const float expectedNaturalSize =
        std::ceil(std::max(layout.uvMaxTexels.x - layout.uvMinTexels.x, layout.uvMaxTexels.y - layout.uvMinTexels.y));
    requireNear(layout.naturalSize, expectedNaturalSize, "natural size mismatch");
    requireNear(layout.surfaceArea, 2.0f, "surface area mismatch");
    requireNear(layout.texelsPerUnit, 6.0f, "xatlas texelsPerUnit mismatch");

    const float uvScale =
        std::max(layout.uvMaxTexels.x - layout.uvMinTexels.x, layout.uvMaxTexels.y - layout.uvMinTexels.y);
    float maxRawU = 0.0f;
    float maxRawV = 0.0f;
    float maxNormalizedU = 0.0f;
    float maxNormalizedV = 0.0f;
    for (std::size_t index = 0; index < layout.mesh.vertices.size(); ++index) {
        const uint32_t source = layout.sourceVertex[index];
        require(source < mesh.vertices.size(), "xatlas source vertex out of range");
        require(layout.mesh.vertices[index].position == mesh.vertices[source].position,
                "xatlas remapped a vertex to the wrong position");

        const hbg::Vec2 raw = layout.uvTexels[index];
        const hbg::Vec2 uv = layout.mesh.vertices[index].texcoord1;
        require(raw.x >= 0.0f && raw.y >= 0.0f, "xatlas raw UV is negative");
        require(raw.x <= static_cast<float>(layout.atlasWidth) && raw.y <= static_cast<float>(layout.atlasHeight),
                "xatlas raw UV is outside the atlas");
        require(uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f, "normalized UV is outside [0,1]");
        // Uniform normalization keeps the chart aspect and texel density.
        requireNear((raw.x - layout.uvMinTexels.x) / uvScale, uv.x, "U was not normalized uniformly");
        requireNear((raw.y - layout.uvMinTexels.y) / uvScale, uv.y, "V was not normalized uniformly");
        maxRawU = std::max(maxRawU, raw.x);
        maxRawV = std::max(maxRawV, raw.y);
        maxNormalizedU = std::max(maxNormalizedU, uv.x);
        maxNormalizedV = std::max(maxNormalizedV, uv.y);
    }

    // With resolution=0, xatlas UVs are in texel units, not normalized [0,1].
    require(maxRawU > 1.0f || maxRawV > 1.0f, "resolution=0 did not produce texel-unit UVs");
    require(maxNormalizedU > 0.0f && maxNormalizedV > 0.0f, "UV bounds are degenerate");

    MeshExportOptions exportOptions;
    exportOptions.normalizeUv2 = false;
    const MeshInfo meshInfo = exportMesh(layout.mesh, 0, makeMeshGuid(0), exportOptions);
    require(meshInfo.texcoords.size() == 3u, "Dawn mesh UV buffer count mismatch");
    require(meshInfo.texcoords[1].size() == layout.mesh.vertices.size(), "Dawn mesh normalized UV count mismatch");
    for (std::size_t index = 0; index < meshInfo.texcoords[1].size(); ++index) {
        requireNear(meshInfo.texcoords[1][index].x, layout.mesh.vertices[index].texcoord1.x,
                    "exportMesh re-normalized generated UV2 x");
        requireNear(meshInfo.texcoords[1][index].y, layout.mesh.vertices[index].texcoord1.y,
                    "exportMesh re-normalized generated UV2 y");
    }

    std::string warning;
    LightmapUvLayout skipped;
    asset::Mesh emptyMesh;
    require(!tryUnwrapLightmapUv(emptyMesh, options, skipped, &warning), "empty mesh must be skipped");
    require(warning.find("empty mesh") != std::string::npos, "empty mesh warning missing");

    asset::Mesh noIndices = mesh;
    noIndices.indices.clear();
    noIndices.submeshes.clear();
    require(!tryUnwrapLightmapUv(noIndices, options, skipped, &warning), "mesh without indices must be skipped");
    require(warning.find("no triangle indices") != std::string::npos, "missing index warning missing");

    asset::Mesh zeroArea = mesh;
    zeroArea.vertices.resize(3);
    zeroArea.vertices[0].position = hbg::Vec3{0.0f, 0.0f, 0.0f};
    zeroArea.vertices[1].position = hbg::Vec3{1.0f, 0.0f, 0.0f};
    zeroArea.vertices[2].position = hbg::Vec3{2.0f, 0.0f, 0.0f};
    zeroArea.indices = {0, 1, 2};
    zeroArea.submeshes = {asset::SubMesh{0, 3, 0}};
    zeroArea.recomputeBounds();
    require(!tryUnwrapLightmapUv(zeroArea, options, skipped, &warning), "zero-area mesh must be skipped");
    require(warning.find("zero surface area") != std::string::npos, "zero-area warning missing");

    require(tryUnwrapLightmapUv(mesh, options, skipped, &warning), "valid mesh must not be skipped");

    std::printf("uv unwrap test OK: atlas=%ux%u charts=%u uvMax=(%.2f, %.2f) natural=%.1f area=%.1f\n",
                layout.atlasWidth, layout.atlasHeight, layout.chartCount, maxRawU, maxRawV, layout.naturalSize,
                layout.surfaceArea);
    return 0;
}

int atlasTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    LightmapUvLayout layout;
    layout.uvMinTexels = hbg::Vec2{0.0f, 0.0f};
    layout.uvMaxTexels = hbg::Vec2{12.0f, 6.0f};
    layout.surfaceArea = 2.0f;

    LightmapSizeSettings sizeSettings;
    sizeSettings.texelsPerUnit = 6.0f;
    sizeSettings.maxLightmapSize = 1024;
    sizeSettings.padding = true;

    // 2m x 1m quad: 6 * 12 * sqrt(2 / (12 * 6)) = 12.
    require(calculateLightmapSize(layout, 1.0f, 0, sizeSettings) == 12, "base lightmap size mismatch");
    require(calculateLightmapSize(layout, 2.0f, 0, sizeSettings) == 24, "scaleInLightmap mismatch");
    require(calculateLightmapSize(layout, 1.0f, 1, sizeSettings) == 6, "LOD scale mismatch");
    require(calculateLightmapSize(layout, 0.01f, 0, sizeSettings) == 4, "minimum clamp mismatch");
    require(calculateLightmapSize(layout, 1000.0f, 0, sizeSettings) == 1022, "padded maximum clamp mismatch");

    LightmapSizeSettings unpadded = sizeSettings;
    unpadded.padding = false;
    require(calculateLightmapSize(layout, 1000.0f, 0, unpadded) == 1024, "unpadded maximum clamp mismatch");

    LightmapSizeSettings small = sizeSettings;
    small.maxLightmapSize = 64;
    require(calculateLightmapSize(layout, 1000.0f, 0, small) == 62, "custom padded maximum mismatch");

    LightmapUvLayout degenerate = layout;
    degenerate.surfaceArea = 0.0f;
    require(calculateLightmapSize(degenerate, 1.0f, 0, sizeSettings) == 4, "degenerate size must clamp to 4");

    const std::vector<uint32_t> sizes = {64, 128, 32, 128, 256, 100, 64};
    LightmapAtlasLayout atlas;
    require(packLightmapSquares(sizes, 2048, 2, atlas), "shelf pack failed");
    require(atlas.size == 2048 && atlas.rects.size() == sizes.size(), "shelf pack output mismatch");

    for (std::size_t i = 0; i < atlas.rects.size(); ++i) {
        const LightmapRect& rect = atlas.rects[i];
        require(rect.size == sizes[i], "shelf pack changed rect size");
        require(rect.x >= 2 && rect.y >= 2, "shelf pack lost left/top padding");
        require(rect.x + rect.size + 2 <= atlas.size, "rect exceeds atlas width");
        require(rect.y + rect.size + 2 <= atlas.size, "rect exceeds atlas height");
    }
    for (std::size_t i = 0; i < atlas.rects.size(); ++i) {
        for (std::size_t j = i + 1; j < atlas.rects.size(); ++j) {
            const LightmapRect& a = atlas.rects[i];
            const LightmapRect& b = atlas.rects[j];
            const bool separated = a.x + a.size + 2 <= b.x || b.x + b.size + 2 <= a.x || a.y + a.size + 2 <= b.y ||
                                   b.y + b.size + 2 <= a.y;
            require(separated, "shelf pack rects overlap or violate padding");
        }
    }

    LightmapAtlasLayout again;
    require(packLightmapSquares(sizes, 2048, 2, again), "second shelf pack failed");
    require(again.rects.size() == atlas.rects.size(), "second pack size mismatch");
    for (std::size_t i = 0; i < atlas.rects.size(); ++i) {
        require(again.rects[i].x == atlas.rects[i].x && again.rects[i].y == atlas.rects[i].y,
                "shelf pack is not deterministic");
    }

    LightmapAtlasLayout emptyAtlas;
    require(packLightmapSquares({}, 2048, 2, emptyAtlas), "empty shelf pack failed");
    require(emptyAtlas.size == 2048 && emptyAtlas.rects.empty(), "empty atlas output mismatch");

    LightmapAtlasLayout overflow;
    require(!packLightmapSquares({2048}, 2048, 2, overflow), "oversize rect must fail");
    require(!packLightmapSquares({0}, 2048, 2, overflow), "zero-size rect must fail");

    LightmapPackingSettings packingSettings;
    packingSettings.atlasSize = 2048;
    packingSettings.maxAtlases = 2;
    packingSettings.padding = 2;
    packingSettings.minScale = 0.5f;

    const auto verifyPacking = [&require](const LightmapPackingResult& result, const std::vector<uint32_t>& sourceSizes,
                                          const LightmapPackingSettings& settings) {
        require(result.placements.size() == sourceSizes.size(), "atlas packing count mismatch");
        require(!result.atlases.empty(), "atlas packing returned no atlas");
        std::vector<std::vector<std::size_t>> perAtlas(result.atlases.size());
        for (std::size_t i = 0; i < result.placements.size(); ++i) {
            const LightmapPlacement& placement = result.placements[i];
            require(placement.atlasIndex < result.atlases.size(), "atlas index out of range");
            require(placement.scale >= settings.minScale - 1.0e-5f && placement.scale <= 1.0f + 1.0e-5f,
                    "atlas scale out of range");
            const uint32_t expectedSize =
                static_cast<uint32_t>(std::max(4.0, std::ceil(static_cast<double>(sourceSizes[i]) * placement.scale)));
            require(placement.rect.size == expectedSize, "atlas rect size mismatch");
            const LightmapAtlasPlan& atlas = result.atlases[placement.atlasIndex];
            require(atlas.size == settings.atlasSize, "atlas size mismatch");
            require(std::fabs(atlas.scale - placement.scale) <= 1.0e-6f, "atlas scale mismatch");
            require(placement.rect.x >= settings.padding && placement.rect.y >= settings.padding,
                    "atlas rect lost padding");
            require(placement.rect.x + placement.rect.size + settings.padding <= atlas.size,
                    "atlas rect exceeds width");
            require(placement.rect.y + placement.rect.size + settings.padding <= atlas.size,
                    "atlas rect exceeds height");
            perAtlas[placement.atlasIndex].push_back(i);
        }
        for (const std::vector<std::size_t>& indices : perAtlas) {
            require(!indices.empty(), "atlas has no placements");
            for (std::size_t i = 0; i < indices.size(); ++i) {
                for (std::size_t j = i + 1; j < indices.size(); ++j) {
                    const LightmapRect& a = result.placements[indices[i]].rect;
                    const LightmapRect& b = result.placements[indices[j]].rect;
                    const bool separated =
                        a.x + a.size + settings.padding <= b.x || b.x + b.size + settings.padding <= a.x ||
                        a.y + a.size + settings.padding <= b.y || b.y + b.size + settings.padding <= a.y;
                    require(separated, "atlas rects overlap or violate padding");
                }
            }
        }
    };

    const std::vector<uint32_t> oneAtlasSizes = {256, 128, 64};
    LightmapPackingResult oneAtlas;
    require(packLightmapAtlases(oneAtlasSizes, packingSettings, oneAtlas), "one-atlas packing failed");
    require(oneAtlas.atlases.size() == 1 && oneAtlas.atlases[0].scale == 1.0f,
            "one-atlas packing should keep full scale");
    verifyPacking(oneAtlas, oneAtlasSizes, packingSettings);

    LightmapPackingSettings oneAtlasLimit = packingSettings;
    oneAtlasLimit.maxAtlases = 1;
    const std::vector<uint32_t> scaledOneSizes = {1400, 700, 700};
    LightmapPackingResult scaledOne;
    require(packLightmapAtlases(scaledOneSizes, oneAtlasLimit, scaledOne), "scaled one-atlas packing failed");
    require(scaledOne.atlases.size() == 1, "scaled packing must stay in one atlas");
    require(scaledOne.atlases[0].scale < 1.0f && scaledOne.atlases[0].scale >= 0.5f, "scaled one-atlas scale mismatch");
    verifyPacking(scaledOne, scaledOneSizes, oneAtlasLimit);

    const std::vector<uint32_t> twoAtlasSizes = {1500, 1500, 1500, 1500, 1500};
    LightmapPackingResult twoAtlas;
    require(packLightmapAtlases(twoAtlasSizes, packingSettings, twoAtlas), "two-atlas packing failed");
    require(twoAtlas.atlases.size() == 2, "two-atlas packing should use two atlases");
    require(twoAtlas.atlases[0].scale < 1.0f && twoAtlas.atlases[0].scale >= 0.5f, "two-atlas scale mismatch");
    verifyPacking(twoAtlas, twoAtlasSizes, packingSettings);

    require(!packLightmapAtlases({1500, 1500, 1500, 1500, 1500}, oneAtlasLimit, twoAtlas),
            "one-atlas limit should reject five large blocks");
    require(!packLightmapAtlases({1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500, 1500}, packingSettings, twoAtlas),
            "two-atlas limit should reject nine large blocks");

    LightmapPackingResult emptyPacked;
    require(packLightmapAtlases({}, packingSettings, emptyPacked) && emptyPacked.atlases.empty(),
            "empty atlas packing failed");
    require(!packLightmapAtlases({0}, packingSettings, emptyPacked), "zero-size block must fail");
    LightmapPackingSettings noAtlas = packingSettings;
    noAtlas.maxAtlases = 0;
    require(!packLightmapAtlases({128}, noAtlas, emptyPacked), "zero atlas limit must fail");

    std::vector<uint32_t> stressSizes;
    stressSizes.reserve(100u);
    for (uint32_t index = 0u; index < 100u; ++index) {
        stressSizes.push_back(16u + (index * 37u) % 113u);
    }
    LightmapPackingResult stressPacked;
    require(packLightmapAtlases(stressSizes, packingSettings, stressPacked), "100-block stress packing failed");
    require(stressPacked.atlases.size() == 1u, "100-block stress test should fit one atlas");
    verifyPacking(stressPacked, stressSizes, packingSettings);

    std::vector<Lightmap2DInput> mappingJobs(3u);
    mappingJobs[0].size = Int2{16, 16};
    mappingJobs[1].size = Int2{64, 64};
    mappingJobs[2].size = Int2{32, 32};
    const std::vector<MappingGroupInput> mappingGroups = packMappingGroups(mappingJobs, 128u);
    require(mappingGroups.size() == 1u && mappingGroups[0].mappingOffsets.size() == 3u,
            "mapping group AtlasNode packing count mismatch");
    require(mappingGroups[0].mappingOffsets[0].z == 0 && mappingGroups[0].mappingOffsets[0].x == 0 &&
                mappingGroups[0].mappingOffsets[0].y == 0,
            "mapping group smallest job offset mismatch");
    require(mappingGroups[0].mappingOffsets[1].z == 2 && mappingGroups[0].mappingOffsets[1].x == 0 &&
                mappingGroups[0].mappingOffsets[1].y == 16,
            "mapping group middle job offset mismatch");
    require(mappingGroups[0].mappingOffsets[2].z == 1 && mappingGroups[0].mappingOffsets[2].x == 32 &&
                mappingGroups[0].mappingOffsets[2].y == 16,
            "mapping group largest job offset mismatch");

    std::printf("atlas test OK: size=12/24/6/4/1022, pixels=%zu, atlases=%zu scale=%.3f, stress=%zu\n",
                atlas.rects.size(), twoAtlas.atlases.size(), twoAtlas.atlases[0].scale, stressSizes.size());
    return 0;
}

int dumpUvCommand(const std::filesystem::path& path) {
    asset::Mesh cube;
    cube.name = "UvDebugCube";
    const hbg::Vec3 normals[6] = {
        {1.0f, 0.0f, 0.0f},  {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f},  {0.0f, 0.0f, -1.0f},
    };
    const hbg::Vec3 corners[6][4] = {
        {{0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}},
        {{-0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, 0.5f}},
        {{-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}},
        {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}},
        {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}},
        {{0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}},
    };
    for (uint32_t face = 0; face < 6u; ++face) {
        for (uint32_t corner = 0; corner < 4u; ++corner) {
            asset::MeshVertex vertex;
            vertex.position = corners[face][corner];
            vertex.normal = normals[face];
            vertex.texcoord0 = hbg::Vec2{0.0f, 0.0f};
            cube.vertices.push_back(vertex);
        }
        const uint32_t base = face * 4u;
        cube.indices.push_back(base + 0u);
        cube.indices.push_back(base + 1u);
        cube.indices.push_back(base + 2u);
        cube.indices.push_back(base + 0u);
        cube.indices.push_back(base + 2u);
        cube.indices.push_back(base + 3u);
    }
    cube.submeshes.push_back(asset::SubMesh{0, static_cast<uint32_t>(cube.indices.size()), 0});
    cube.hasNormals = true;
    cube.recomputeBounds();

    LightmapUvOptions uvOptions;
    uvOptions.resolution = 0;
    uvOptions.texelsPerUnit = 32.0f;
    uvOptions.padding = 2;
    const LightmapUvLayout uvLayout = unwrapLightmapUv(cube, uvOptions);

    LightmapSizeSettings sizeSettings;
    sizeSettings.texelsPerUnit = 32.0f;
    const uint32_t lightmapSize = static_cast<uint32_t>(calculateLightmapSize(uvLayout, 1.0f, 0, sizeSettings));

    LightmapPackingSettings packingSettings;
    LightmapPackingResult packingResult;
    if (!packLightmapAtlases({lightmapSize}, packingSettings, packingResult)) {
        throw std::runtime_error("failed to pack the UV dump atlas");
    }
    const LightmapPlacement& placement = packingResult.placements[0];
    const LightmapUvDumpObject object{"Cube", &uvLayout.mesh, placement.atlasIndex, placement.rect};
    if (!writeLightmapUvDump(path, {object}, packingResult.atlases, 1024)) {
        throw std::runtime_error("failed to write UV dump PNG");
    }

    std::cout << "wrote " << path.string() << " atlas=" << packingResult.atlases[0].size << " rect=" << placement.rect.x
              << "," << placement.rect.y << "," << placement.rect.size << " charts=" << uvLayout.chartCount << "\n";
    return 0;
}

int exportPackageCommand(const std::filesystem::path& directory, const std::string& guid) {
    if (!isHexGuid(guid)) {
        throw std::runtime_error("guid must be 32 hex characters");
    }
    ExportedSceneBundle bundle = buildExportedScene();
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);
    std::cout << "wrote " << files.sceneGz.string() << " (" << files.sceneBytes << " raw bytes)\n";
    std::cout << "wrote " << files.jobsGz.string() << " (" << files.jobBytes << " raw bytes)\n";
    std::cout << "wrote " << files.lightmap.string() << "\n";
    for (const auto& path : files.meshGz) {
        std::cout << "wrote " << path.string() << "\n";
    }
    for (const auto& path : files.materialGz) {
        std::cout << "wrote " << path.string() << "\n";
    }
    return 0;
}

int packageTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "hbg_dawnbake_package_test";
    std::filesystem::remove_all(directory);

    ExportedSceneBundle bundle = buildExportedScene();
    const std::string guid = "11111111222222223333333344444444";
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);
    require(std::filesystem::exists(files.sceneGz), "scenegz missing");
    require(std::filesystem::exists(files.jobsGz), "jobgz missing");
    require(std::filesystem::exists(files.scene), "local scene missing");
    require(std::filesystem::exists(files.jobs), "local jobs missing");
    require(std::filesystem::exists(files.lightmap), "lightmap sidecar missing");
    require(files.meshGz.size() == 1u, "meshgz count mismatch");
    require(files.materialGz.size() == 1u, "matgz count mismatch");
    require(std::filesystem::exists(files.meshGz[0]), "meshgz missing");
    require(std::filesystem::exists(files.materialGz[0]), "matgz missing");

    const std::vector<uint8_t> sceneGz = readFile(files.sceneGz);
    require(sceneGz.size() > 18u && sceneGz[0] == 0x1Fu && sceneGz[1] == 0x8Bu && sceneGz[2] == 0x08u,
            "scenegz gzip header mismatch");
    const std::vector<uint8_t> sceneBytes = gzipDecompress(sceneGz);
    require(sceneBytes == readFile(files.scene), "scenegz payload differs from local scene");
#ifdef HBG_DAWN_HAVE_ZLIB
    require(sceneGz.size() < gzipCompressStored(sceneBytes).size(), "scenegz is not deflated");
#endif
    Reader sceneReader(sceneBytes);
    require(sceneReader.version() == kExportVersion, "scene version mismatch");
    SceneInfo scene;
    readScene(sceneReader, scene, kExportVersion);
    require(sceneReader.remaining() == 0u, "scene has trailing bytes");
    require(scene.header.numMeshes == 1u && scene.header.numMaterials == 1u, "scene mesh/material count mismatch");
    require(scene.header.numMeshInstances == 1u && scene.header.numMeshGuids == 1u, "scene instance count mismatch");
    require(scene.header.numLights == 1u && scene.lights.size() == 1u, "scene light count mismatch");
    require(scene.meshInstances.size() == 1u, "scene instance missing");
    require(scene.skyLight.envCube.size() == 6u * 8u * 8u, "scene sky cubemap size mismatch");

    const std::vector<uint8_t> jobGz = readFile(files.jobsGz);
    require(jobGz.size() > 18u && jobGz[0] == 0x1Fu && jobGz[1] == 0x8Bu && jobGz[2] == 0x08u,
            "jobgz gzip header mismatch");
    const std::vector<uint8_t> jobBytes = gzipDecompress(jobGz);
    require(jobBytes == readFile(files.jobs), "jobgz payload differs from local jobs");
#ifdef HBG_DAWN_HAVE_ZLIB
    require(jobGz.size() < gzipCompressStored(jobBytes).size(), "jobgz is not deflated");
#endif
    Reader jobReader(jobBytes);
    require(jobReader.version() == kExportVersion, "job version mismatch");
    BakingJobInputs jobs;
    readBakingJobInputs(jobReader, jobs, kExportVersion);
    require(jobReader.remaining() == 0u, "jobs have trailing bytes");
    require(jobs.lightmap2DJobs.size() == 1u, "lightmap job count mismatch");
    require((jobs.lightmap2DJobs[0].flags & kLightmapFlagIndirect) != 0u &&
                (jobs.lightmap2DJobs[0].flags & kLightmapFlagDirect) != 0u &&
                (jobs.lightmap2DJobs[0].flags & kLightmapFlagDirection) != 0u &&
                (jobs.lightmap2DJobs[0].flags & kLightmapFlagBilinearFilter) != 0u &&
                (jobs.lightmap2DJobs[0].flags & kLightmapFlagBmStatic) != 0u,
            "lightmap job flags mismatch");
    const Guid& jobMeshId = jobs.lightmap2DJobs[0].meshId;
    const Guid& sceneMeshId = scene.meshInstances[0].header.guid;
    require(jobMeshId.a == sceneMeshId.a && jobMeshId.b == sceneMeshId.b && jobMeshId.c == sceneMeshId.c &&
                jobMeshId.d == sceneMeshId.d,
            "lightmap job mesh id mismatch");
    require(jobs.partialJobsTotalNum.numLightmap2DJobs == 1u, "partial job count mismatch");

    const std::vector<uint8_t> meshBytes = gzipDecompress(readFile(files.meshGz[0]));
    Reader meshReader(meshBytes);
    require(meshReader.version() == kExportVersion, "mesh version mismatch");
    MeshInfo mesh;
    readMesh(meshReader, mesh, kExportVersion);
    require(meshReader.remaining() == 0u, "mesh has trailing bytes");
    require(mesh.header.numTriangles == 2u && mesh.index16.size() == 6u, "mesh triangle/index mismatch");
    require(mesh.elements.size() == 1u && mesh.texcoords.size() == 3u, "mesh element/uv mismatch");

    const std::vector<uint8_t> materialBytes = gzipDecompress(readFile(files.materialGz[0]));
    Reader materialReader(materialBytes);
    require(materialReader.version() == kExportVersion, "material version mismatch");
    MaterialInfo material;
    readMaterial(materialReader, material, kExportVersion);
    require(materialReader.remaining() == 0u, "material has trailing bytes");
    require(material.diffuseTexture.header.format == kTextureFormatUByte4, "material diffuse format mismatch");
    require(material.diffuseTexture.colors.size() == 1u, "material diffuse size mismatch");
    require(material.specularTexture.colors.size() == 1u, "material specular size mismatch");
    bool hashNonZero = false;
    for (uint8_t value : material.header.hash.hash) {
        hashNonZero = hashNonZero || value != 0u;
    }
    require(hashNonZero, "material hash missing");

    const std::vector<uint8_t> sidecarBytes = readFile(files.lightmap);
    const std::string sidecarText(sidecarBytes.begin(), sidecarBytes.end());
    const nlohmann::json sidecar = nlohmann::json::parse(sidecarText);
    const nlohmann::json& lightmap = sidecar.at("lightmap");
    require(lightmap.at("version").get<uint32_t>() == 1u, "lightmap sidecar version mismatch");
    require(std::fabs(lightmap.at("texelPerUnit").get<float>() - 6.0f) < 1.0e-6f,
            "lightmap sidecar texelPerUnit mismatch");
    require(lightmap.at("atlases").size() == 1u, "lightmap sidecar atlas count mismatch");
    const nlohmann::json& atlas = lightmap.at("atlases").at(0);
    require(atlas.at("file") == "lm0" && atlas.at("size") == 2048u && atlas.at("layers") == 3u &&
                atlas.at("format") == "rgba16f",
            "lightmap sidecar atlas metadata mismatch");
    require(lightmap.at("objects").contains("Quad"), "lightmap sidecar object missing");
    const nlohmann::json& quad = lightmap.at("objects").at("Quad");
    require(quad.at("atlas") == 0u, "lightmap sidecar atlas index mismatch");
    require(quad.at("rect") == nlohmann::json::array({2u, 2u, 64u, 64u}), "lightmap sidecar rect mismatch");
    require(std::fabs(quad.at("st").at(0).get<float>() - 64.0f / 2048.0f) < 1.0e-6f &&
                std::fabs(quad.at("st").at(1).get<float>() - 64.0f / 2048.0f) < 1.0e-6f &&
                std::fabs(quad.at("st").at(2).get<float>() - 2.0f / 2048.0f) < 1.0e-6f &&
                std::fabs(quad.at("st").at(3).get<float>() - 2.0f / 2048.0f) < 1.0e-6f,
            "lightmap sidecar st mismatch");
    require(std::fabs(quad.at("scale").get<float>() - 1.0f) < 1.0e-6f && quad.at("contributeGI").get<bool>(),
            "lightmap sidecar object flags mismatch");
    require(scene.meshInstances[0].header.uv2MapSize.x == 64u && scene.meshInstances[0].header.uv2MapSize.y == 64u,
            "scene UV2 map size mismatch");
    require(scene.meshInstances[0].header.uv2Offset.x == 2u && scene.meshInstances[0].header.uv2Offset.y == 2u,
            "scene UV2 offset mismatch");

    std::filesystem::remove_all(directory);
    std::cout << "package export test OK\n";
    return 0;
}

std::string readTextFile(const std::filesystem::path& path) {
    const std::vector<uint8_t> bytes = readFile(path);
    return std::string(bytes.begin(), bytes.end());
}

int sidecarMergeTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const auto requireThrows = [](const auto& action, const char* needle) {
        try {
            action();
        } catch (const std::exception& exception) {
            if (std::string(exception.what()).find(needle) == std::string::npos) {
                throw std::runtime_error(std::string("unexpected error: ") + exception.what());
            }
            return;
        }
        throw std::runtime_error(std::string("expected error containing: ") + needle);
    };
    const auto writeText = [](const std::filesystem::path& path, const std::string& text) {
        writeFile(path, std::vector<uint8_t>(text.begin(), text.end()));
    };

    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "hbg_dawnbake_sidecar_merge_test";
    std::filesystem::remove_all(directory);

    ExportedSceneBundle bundle = buildExportedScene();
    const std::string guid = "11111111222222223333333344444444";
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);

    const std::filesystem::path scenePath = directory / "scene.hdrp.json";
    const std::string sceneText = R"({
  "hdrp": {"supportDecals": true},
  "materials": {"Quad": {"metallic": 0.25}},
  "lights": [{"name": "Sun", "type": "directional"}],
  "objects": [{"name": "Quad", "lightLayerMask": 1}],
  "custom": {"nested": [1, 2, 3]},
  "lightmap": {"stale": true}
})";
    writeText(scenePath, sceneText);

    const std::filesystem::path mergedPath = directory / "nested" / "merged.hdrp.json";
    mergeLightmapSidecar(scenePath, files.lightmap, mergedPath);
    require(std::filesystem::exists(mergedPath), "merged sidecar missing");

    const nlohmann::json original = nlohmann::json::parse(sceneText);
    nlohmann::json originalWithoutLightmap = original;
    originalWithoutLightmap.erase("lightmap");
    const nlohmann::json merged = nlohmann::json::parse(readTextFile(mergedPath));
    nlohmann::json mergedWithoutLightmap = merged;
    mergedWithoutLightmap.erase("lightmap");
    require(mergedWithoutLightmap == originalWithoutLightmap, "existing sidecar fields were not preserved");
    require(merged.at("objects").is_array() && merged.at("objects").at(0).at("name") == "Quad",
            "scene objects field changed");

    const nlohmann::json fragment = nlohmann::json::parse(readTextFile(files.lightmap));
    require(merged.at("lightmap") == fragment.at("lightmap"), "merged lightmap section mismatch");
    require(!merged.at("lightmap").contains("stale"), "existing lightmap section was not replaced");
    require(merged.at("lightmap").at("objects").contains("Quad"), "merged lightmap object missing");
    require(merged.at("lightmap").at("objects").at("Quad").at("rect") == nlohmann::json::array({2u, 2u, 64u, 64u}),
            "merged lightmap rect mismatch");

    mergeLightmapSidecar(mergedPath, files.lightmap, mergedPath);
    require(nlohmann::json::parse(readTextFile(mergedPath)) == merged, "sidecar merge is not idempotent");

    const std::filesystem::path inPlacePath = directory / "inplace.hdrp.json";
    writeText(inPlacePath, sceneText);
    mergeLightmapSidecar(inPlacePath, files.lightmap, inPlacePath);
    const nlohmann::json inPlace = nlohmann::json::parse(readTextFile(inPlacePath));
    require(inPlace.at("lightmap") == fragment.at("lightmap"), "in-place sidecar merge failed");

    const std::filesystem::path badRoot = directory / "bad_root.hdrp.json";
    writeText(badRoot, "[]\n");
    requireThrows([&] { mergeLightmapSidecar(badRoot, files.lightmap, directory / "bad_root_out.hdrp.json"); },
                  "root must be an object");

    const std::filesystem::path missingLightmap = directory / "missing_lightmap.json";
    writeText(missingLightmap, "{\"foo\": 1}\n");
    requireThrows(
        [&] { mergeLightmapSidecar(scenePath, missingLightmap, directory / "missing_lightmap_out.hdrp.json"); },
        "has no lightmap object");

    const std::filesystem::path malformed = directory / "malformed.json";
    writeText(malformed, "{\n");
    requireThrows([&] { mergeLightmapSidecar(scenePath, malformed, directory / "malformed_out.hdrp.json"); },
                  "failed to parse lightmap sidecar");

    const std::filesystem::path invalidRect = directory / "invalid_rect.json";
    writeText(
        invalidRect,
        R"({"lightmap":{"version":1,"texelPerUnit":6,"atlases":[{"file":"lm0","size":8,"layers":3,"format":"rgba16f"}],"objects":{"Quad":{"atlas":0,"rect":[7,0,4,4]}}}})");
    requireThrows([&] { mergeLightmapSidecar(scenePath, invalidRect, directory / "invalid_rect_out.hdrp.json"); },
                  "rect is outside its atlas");

    requireThrows(
        [&] {
            mergeLightmapSidecar(directory / "missing.hdrp.json", files.lightmap, directory / "missing_out.hdrp.json");
        },
        "failed to read scene sidecar");

    std::filesystem::remove_all(directory);
    std::printf("sidecar merge test OK: atlases=%zu objects=%zu\n", fragment.at("lightmap").at("atlases").size(),
                fragment.at("lightmap").at("objects").size());
    return 0;
}

int processDriverTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawnbake_process_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    const std::string comspecValue = environmentValue("COMSPEC");
    const std::filesystem::path comspec = !comspecValue.empty() ? std::filesystem::path(comspecValue)
                                                                : std::filesystem::path("C:/Windows/System32/cmd.exe");
    require(std::filesystem::is_regular_file(comspec), "COMSPEC executable missing");

    std::string error;
    std::vector<std::string> arguments;
    require(buildDawnArguments(directory, "11111111222222223333333344444444", &arguments, &error),
            "buildDawnArguments failed");
    require(arguments.size() == 4u, "Dawn argument count mismatch");
    require(arguments[0] == "-local" && arguments[1] == "-localdirectory", "Dawn local arguments mismatch");
    require(!arguments[2].empty() && arguments[2].back() == '\\', "localdirectory must end with a backslash");
    require(arguments[3] == "11111111222222223333333344444444", "Dawn GUID argument mismatch");
    require(!buildDawnArguments(directory, "not-a-guid", &arguments, &error), "invalid GUID must be rejected");

    ProcessOptions ok;
    ok.executable = comspec;
    ok.arguments = {"/c", "echo launch-ok & exit /b 7"};
    ok.workingDirectory = directory;
    ok.logPath = directory / "process.log";
    ok.timeout = std::chrono::seconds(10);
    const ProcessResult result = runProcess(ok);
    require(result.status == ProcessStatus::Completed, "process did not complete");
    require(result.exitCode == 7u, "process exit code mismatch");
    require(readTextFile(result.logPath).find("launch-ok") != std::string::npos, "process log was not captured");

    ProcessOptions slow;
    slow.executable = comspec;
    slow.arguments = {"/c", "ping -n 10 127.0.0.1 >nul"};
    slow.workingDirectory = directory;
    slow.logPath = directory / "timeout.log";
    slow.timeout = std::chrono::milliseconds(250);
    const ProcessResult timedOut = runProcess(slow);
    require(timedOut.status == ProcessStatus::TimedOut, "timeout was not reported");
    require(timedOut.timedOut, "timeout flag missing");
    require(timedOut.elapsedMs < 5000u, "timed-out process was not killed promptly");

    DawnLaunchOptions badGuid;
    badGuid.dawnExecutable = comspec;
    badGuid.jobDirectory = directory;
    badGuid.guid = "bad";
    const ProcessResult badGuidResult = runDawn(badGuid);
    require(badGuidResult.status == ProcessStatus::Failed && badGuidResult.error.find("guid") != std::string::npos,
            "runDawn did not reject invalid GUID");

    DawnLaunchOptions missingInput;
    missingInput.dawnExecutable = comspec;
    missingInput.jobDirectory = directory;
    missingInput.guid = "11111111222222223333333344444444";
    const ProcessResult missingInputResult = runDawn(missingInput);
    require(missingInputResult.status == ProcessStatus::Failed &&
                missingInputResult.error.find("scene") != std::string::npos,
            "runDawn did not report missing local scene");

    DawnLaunchOptions missingExe;
    missingExe.dawnExecutable = directory / "missing-dawn.exe";
    missingExe.jobDirectory = directory;
    missingExe.guid = "11111111222222223333333344444444";
    const ProcessResult missingExeResult = runDawn(missingExe);
    require(missingExeResult.status == ProcessStatus::Failed &&
                missingExeResult.error.find("not found") != std::string::npos,
            "runDawn did not report missing executable");

    std::printf("process driver test OK: exit=%u timeout=%llums log=%s\n", result.exitCode,
                static_cast<unsigned long long>(timedOut.elapsedMs), result.logPath.string().c_str());
    return 0;
}

int runDawnCommand(int argc, char** argv);

bool writeJobAtlasFromDirectory(const std::filesystem::path& jobDirectory, const std::string& guid,
                                const std::filesystem::path& sidecarPath,
                                const std::filesystem::path& outputDirectory, uint32_t edgePadding,
                                std::size_t* atlasCount, std::size_t* blockCount, std::string* error);

std::string guidHex(const Guid& guid) {
    char buffer[33] = {};
    std::snprintf(buffer, sizeof(buffer), "%08X%08X%08X%08X", guid.a, guid.b, guid.c, guid.d);
    return buffer;
}

DawnLaunchOptions parseDawnLaunchOptions(int argc, char** argv, bool allowResultGrace, const char* command) {
    if (argc < 3) {
        throw std::runtime_error(std::string(command) + " requires <guid32hex> and a job directory");
    }

    DawnLaunchOptions options;
    options.timeout = std::chrono::minutes(10);
    int optionStart = 3;
    if (argc >= 4 && isHexGuid(argv[3])) {
        options.jobDirectory = argv[2];
        options.guid = argv[3];
        optionStart = 4;
    } else if (isHexGuid(argv[2])) {
        options.guid = argv[2];
    } else {
        throw std::runtime_error(std::string(command) + " requires <job-dir> <guid32hex> or <guid32hex> "
                                                        "--job-dir <dir>");
    }

    for (int index = optionStart; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--dawn" && index + 1 < argc) {
            options.dawnExecutable = argv[++index];
        } else if (argument == "--agent" && index + 1 < argc) {
            options.agentAddress = argv[++index];
        } else if (argument == "--agent-files" && index + 1 < argc) {
            options.agentFileAddress = argv[++index];
        } else if (argument == "--job-dir" && index + 1 < argc) {
            options.jobDirectory = argv[++index];
        } else if (argument.rfind("--job-dir=", 0) == 0) {
            options.jobDirectory = argument.substr(10);
        } else if (argument == "--keep-job") {
            options.keepJob = true;
        } else if (argument == "--agent-download-results") {
            options.downloadAgentResults = true;
        } else if (argument == "--agent-force-download") {
            options.forceAgentDownload = true;
        } else if (argument == "--timeout" && index + 1 < argc) {
            const int seconds = std::stoi(argv[++index]);
            if (seconds <= 0) {
                throw std::runtime_error("--timeout must be positive");
            }
            options.timeout = std::chrono::seconds(seconds);
        } else if (argument == "--log" && index + 1 < argc) {
            options.logPath = argv[++index];
        } else if (argument == "--atlas-out" && index + 1 < argc) {
            options.atlasOutputDirectory = argv[++index];
        } else if (argument.rfind("--atlas-out=", 0) == 0) {
            options.atlasOutputDirectory = argument.substr(12);
        } else if (argument == "--result-grace" && allowResultGrace && index + 1 < argc) {
            const int milliseconds = std::stoi(argv[++index]);
            if (milliseconds < 0) {
                throw std::runtime_error("--result-grace must be non-negative");
            }
            options.resultGrace = std::chrono::milliseconds(milliseconds);
        } else {
            throw std::runtime_error(std::string("unknown ") + command + " argument: " + argument);
        }
    }

    if (options.jobDirectory.empty()) {
        throw std::runtime_error(std::string(command) + " requires --job-dir <dir> when no job directory is supplied");
    }
    if (options.dawnExecutable.empty() && options.agentAddress.empty()) {
        const std::string environment = environmentValue("HBG_DAWN_EXE");
        if (!environment.empty()) {
            options.dawnExecutable = environment;
        }
    }
    if (options.dawnExecutable.empty() && options.agentAddress.empty()) {
        throw std::runtime_error(
            "Dawn executable is required (--dawn <Dawn.exe> or HBG_DAWN_EXE) unless --agent is used");
    }
    if (options.downloadAgentResults && (options.agentAddress.empty() || options.agentFileAddress.empty())) {
        throw std::runtime_error("--agent-download-results requires --agent <deamon> and --agent-files <agent>");
    }
    if (options.forceAgentDownload && !options.downloadAgentResults) {
        throw std::runtime_error("--agent-force-download requires --agent-download-results");
    }
    return options;
}

int runDawnCommand(int argc, char** argv) {
    const DawnLaunchOptions options = parseDawnLaunchOptions(argc, argv, false, "--run-dawn");
    JobWorkspace workspace;
    std::string workspaceError;
    if (!prepareJobWorkspace(options.jobDirectory, options.guid, options.keepJob, &workspace, &workspaceError)) {
        throw std::runtime_error(workspaceError);
    }
    std::cout << "Job dir: " << workspace.path.string() << " ("
              << (workspace.created ? (options.keepJob ? "created, kept" : "created, temporary") : "reused") << ")\n";

    DawnLaunchOptions launch = options;
    launch.jobDirectory = workspace.path;
    const ProcessResult result = runDawn(launch);

    std::cout << "Dawn launch: "
              << (result.status == ProcessStatus::Completed  ? "completed"
                  : result.status == ProcessStatus::TimedOut ? "timed out"
                                                             : "failed")
              << " exit=" << result.exitCode << " elapsed=" << result.elapsedMs << "ms\n";
    if (!result.logPath.empty()) {
        std::cout << "Dawn log: " << result.logPath.string() << "\n";
    }
    if (!result.error.empty()) {
        std::cerr << "error: " << result.error << "\n";
    }
    if (!result.logTail.empty() && result.status != ProcessStatus::Completed) {
        std::cout << result.logTail;
        if (result.logTail.back() != '\n') {
            std::cout << '\n';
        }
    }
    if (result.status == ProcessStatus::TimedOut) {
        return 3;
    }
    if (result.status == ProcessStatus::Failed) {
        return 1;
    }
    return static_cast<int>(result.exitCode);
}

std::vector<std::filesystem::path> expectedDawnResults(const std::filesystem::path& jobDirectory,
                                                       const std::string& guid) {
    std::string lowerGuid = guid;
    std::transform(lowerGuid.begin(), lowerGuid.end(), lowerGuid.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::filesystem::path rawJobs = jobDirectory / (lowerGuid + ".jobs");
    const std::filesystem::path gzipJobs = jobDirectory / ("v2." + lowerGuid + ".jobgz");
    const std::filesystem::path jobsPath =
        std::filesystem::is_regular_file(rawJobs) ? rawJobs : gzipJobs;
    const BakingJobInputs jobs = readBakingJobInputsFile(jobsPath);

    std::vector<bool> splitLightmapJobs(jobs.lightmap2DJobs.size(), false);
    for (const MappingSplitInput& split : jobs.mappingSplitJobs) {
        if (split.mappingOffsets.z >= 0 &&
            static_cast<std::size_t>(split.mappingOffsets.z) < splitLightmapJobs.size()) {
            splitLightmapJobs[static_cast<std::size_t>(split.mappingOffsets.z)] = true;
        }
    }
    std::vector<std::filesystem::path> results;
    results.reserve(jobs.lightmap2DJobs.size() + jobs.mappingSplitJobs.size() + jobs.sdfShadowJobs.size());
    for (std::size_t index = 0; index < jobs.lightmap2DJobs.size(); ++index) {
        if (!splitLightmapJobs[index]) {
            results.push_back(jobDirectory / lightmapResultFileName(jobs.lightmap2DJobs[index].jobId));
        }
    }
    for (const MappingSplitInput& split : jobs.mappingSplitJobs) {
        results.push_back(jobDirectory / lightmapResultFileName(split.jobId));
    }
    for (const SdfShadowInput& job : jobs.sdfShadowJobs) {
        results.push_back(jobDirectory / shadowmaskResultFileName(job.header.jobId));
    }
    return results;
}

std::string lowercaseGuid(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

std::filesystem::path findJobObjectFile(const std::filesystem::path& directory, const std::string& guid,
                                        const std::string& rawExtension, const std::string& gzipExtension) {
    const std::string lowerGuid = lowercaseGuid(guid);
    const std::vector<std::filesystem::path> candidates = {
        directory / (lowerGuid + rawExtension),
        directory / ("v2." + lowerGuid + gzipExtension),
    };
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    throw std::runtime_error("missing " + rawExtension + " or " + gzipExtension + " for " + guid + " in " +
                             directory.string());
}

std::filesystem::path findLightmapSidecar(const std::filesystem::path& directory, const std::string& guid) {
    const std::vector<std::filesystem::path> candidates = {
        directory / (lowercaseGuid(guid) + ".lightmap.json"),
        directory / (guid + ".lightmap.json"),
    };
    for (const std::filesystem::path& candidate : candidates) {
        if (std::filesystem::is_regular_file(candidate)) {
            return candidate;
        }
    }
    return {};
}

bool loadLightmap2DOutputs(const std::filesystem::path& jobDirectory, const BakingJobInputs& jobs,
                           const std::string& guid, std::vector<Lightmap2DOutput>* outputs,
                           std::vector<std::size_t>* splitCounts, std::string* error) {
    try {
        std::vector<std::vector<const MappingSplitInput*>> splitsByJob(jobs.lightmap2DJobs.size());
        for (const MappingSplitInput& split : jobs.mappingSplitJobs) {
            if (split.mappingOffsets.z < 0 ||
                static_cast<std::size_t>(split.mappingOffsets.z) >= splitsByJob.size()) {
                if (error != nullptr) {
                    *error = "split lightmap for " + guid + " has invalid lightmap index " +
                             std::to_string(split.mappingOffsets.z);
                }
                return false;
            }
            splitsByJob[static_cast<std::size_t>(split.mappingOffsets.z)].push_back(&split);
        }

        outputs->clear();
        splitCounts->clear();
        outputs->reserve(jobs.lightmap2DJobs.size());
        splitCounts->reserve(jobs.lightmap2DJobs.size());
        for (std::size_t jobIndex = 0; jobIndex < jobs.lightmap2DJobs.size(); ++jobIndex) {
            const Lightmap2DInput& input = jobs.lightmap2DJobs[jobIndex];
            const std::vector<const MappingSplitInput*>& jobSplits = splitsByJob[jobIndex];
            if (jobSplits.empty()) {
                const std::filesystem::path resultPath = jobDirectory / lightmapResultFileName(input.jobId);
                if (!std::filesystem::is_regular_file(resultPath)) {
                    if (error != nullptr) {
                        *error = "missing lightmap result " + resultPath.string();
                    }
                    return false;
                }
                outputs->push_back(readLightmap2DOutputFile(resultPath));
                splitCounts->push_back(0u);
                continue;
            }

            std::vector<Lightmap2DOutput> splitStorage;
            splitStorage.reserve(jobSplits.size());
            for (const MappingSplitInput* split : jobSplits) {
                const std::filesystem::path splitPath = jobDirectory / lightmapResultFileName(split->jobId);
                if (!std::filesystem::is_regular_file(splitPath)) {
                    if (error != nullptr) {
                        *error = "missing split lightmap result " + splitPath.string();
                    }
                    return false;
                }
                splitStorage.push_back(readLightmap2DOutputFile(splitPath));
            }
            std::vector<const Lightmap2DOutput*> splitOutputs;
            splitOutputs.reserve(splitStorage.size());
            for (const Lightmap2DOutput& splitOutput : splitStorage) {
                splitOutputs.push_back(&splitOutput);
            }
            Lightmap2DOutput merged;
            std::string mergeError;
            if (!mergeLightmapSplitResults(input, jobSplits, splitOutputs, &merged, &mergeError)) {
                if (error != nullptr) {
                    *error = "split lightmap " + guidHex(input.jobId) + ": " + mergeError;
                }
                return false;
            }
            outputs->push_back(std::move(merged));
            splitCounts->push_back(jobSplits.size());
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool verifyLightmap2DResultSet(const std::filesystem::path& jobDirectory, const std::string& guid, bool verbose,
                               std::string* error) {
    try {
        const BakingJobInputs jobs = readBakingJobInputsFile(findJobObjectFile(jobDirectory, guid, ".jobs", ".jobgz"));
        const SceneInfo scene = readSceneFile(findJobObjectFile(jobDirectory, guid, ".scene", ".scenegz"));

        std::vector<Lightmap2DOutput> outputs;
        std::vector<std::size_t> splitCounts;
        if (!loadLightmap2DOutputs(jobDirectory, jobs, guid, &outputs, &splitCounts, error)) {
            return false;
        }

        std::size_t totalSamples = 0;
        std::size_t mappedSamples = 0;
        for (std::size_t jobIndex = 0; jobIndex < jobs.lightmap2DJobs.size(); ++jobIndex) {
            const Lightmap2DInput& input = jobs.lightmap2DJobs[jobIndex];
            const Lightmap2DOutput& output = outputs[jobIndex];
            const std::filesystem::path resultPath =
                splitCounts[jobIndex] == 0u
                    ? jobDirectory / lightmapResultFileName(input.jobId)
                    : jobDirectory / ("split." + guidHex(input.jobId) + ".lmgz");

            Lightmap2DResultBinding binding;
            std::string bindError;
            if (!bindLightmap2DResult(scene, jobs, output, &binding, &bindError)) {
                if (error != nullptr) {
                    *error = resultPath.filename().string() + ": " + bindError;
                }
                return false;
            }

            std::size_t mapped = 0;
            for (const Lightmap2DSample& sample : output.lightmapData) {
                if (sample.bIsMapped != 0u) {
                    ++mapped;
                }
            }
            totalSamples += output.lightmapData.size();
            mappedSamples += mapped;
            if (verbose) {
                const MeshInstanceInfo& instance = scene.meshInstances[binding.instanceIndex];
                std::cout << "Lightmap result: " << resultPath.filename().string() << " job=" << binding.jobIndex
                          << " instance=" << binding.instanceIndex << " meshIndex=" << instance.header.meshIndex
                          << " size=" << output.header.size.x << "x" << output.header.size.y
                          << " samples=" << output.lightmapData.size() << " mapped=" << mapped;
                if (splitCounts[jobIndex] != 0u) {
                    std::cout << " split=" << splitCounts[jobIndex];
                }
                std::cout << "\n";
            }
        }

        if (verbose) {
            std::cout << "Lightmap results: jobs=" << jobs.lightmap2DJobs.size() << " samples=" << totalSamples
                      << " mapped=" << mappedSamples << "\n";
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

bool verifySdfShadowResultSet(const std::filesystem::path& jobDirectory, const std::string& guid, bool verbose,
                              std::string* error) {
    try {
        const BakingJobInputs jobs = readBakingJobInputsFile(findJobObjectFile(jobDirectory, guid, ".jobs", ".jobgz"));
        const SceneInfo scene = readSceneFile(findJobObjectFile(jobDirectory, guid, ".scene", ".scenegz"));

        std::size_t totalSamples = 0;
        std::size_t totalMapped = 0;
        for (const SdfShadowInput& input : jobs.sdfShadowJobs) {
            const std::filesystem::path resultPath = jobDirectory / shadowmaskResultFileName(input.header.jobId);
            if (!std::filesystem::is_regular_file(resultPath)) {
                if (error != nullptr) {
                    *error = "missing SDF shadow result " + resultPath.string();
                }
                return false;
            }
            const SdfShadowOutput output = readSdfShadowOutputFile(resultPath);
            SdfShadowResultBinding binding;
            std::string bindError;
            if (!bindSdfShadowResult(scene, jobs, output, &binding, &bindError)) {
                if (error != nullptr) {
                    *error = resultPath.filename().string() + ": " + bindError;
                }
                return false;
            }

            std::size_t mapped = 0;
            std::size_t samples = 0;
            for (const SdfShadowMap& map : output.shadowMaps) {
                samples += map.shadowData.size();
                for (const SdfShadowSample& sample : map.shadowData) {
                    if (sample.isMapped != 0u) {
                        ++mapped;
                    }
                }
            }
            totalSamples += samples;
            totalMapped += mapped;
            if (verbose) {
                std::cout << "SDF shadow result: " << resultPath.filename().string() << " job=" << binding.jobIndex
                          << " instance=" << binding.instanceIndex << " size=" << output.header.size.x << "x"
                          << output.header.size.y << " maps=" << output.shadowMaps.size() << " samples=" << samples
                          << " mapped=" << mapped << "\n";
            }
        }

        if (verbose) {
            std::cout << "SDF shadow results: jobs=" << jobs.sdfShadowJobs.size() << " samples=" << totalSamples
                      << " mapped=" << totalMapped << "\n";
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

int completeDawnBake(const DawnLaunchOptions& options, const std::string& guid) {
    std::string verifyError;
    if (!verifyLightmap2DResultSet(options.jobDirectory, guid, false, &verifyError)) {
        std::cerr << "error: " << verifyError << "\n";
        return 1;
    }
    if (!verifySdfShadowResultSet(options.jobDirectory, guid, false, &verifyError)) {
        std::cerr << "error: " << verifyError << "\n";
        return 1;
    }
    if (!options.atlasOutputDirectory.empty()) {
        const std::filesystem::path sidecar = findLightmapSidecar(options.jobDirectory, guid);
        if (sidecar.empty()) {
            std::cerr << "error: missing lightmap sidecar for " << guid << " in " << options.jobDirectory.string()
                      << "\n";
            return 1;
        }
        std::size_t atlasCount = 0;
        std::size_t blockCount = 0;
        std::string atlasError;
        if (!writeJobAtlasFromDirectory(options.jobDirectory, guid, sidecar, options.atlasOutputDirectory,
                                        options.atlasEdgePadding, &atlasCount, &blockCount, &atlasError)) {
            std::cerr << "error: " << atlasError << "\n";
            return 1;
        }
        std::cout << "Dawn atlas: atlases=" << atlasCount << " blocks=" << blockCount
                  << " edgePadding=" << options.atlasEdgePadding << " dir=" << options.atlasOutputDirectory.string()
                  << "\n";
    }
    std::cout << "Dawn results: validated\n";
    return 0;
}

#ifdef HBG_DAWN_GRPC
AgentJobSpec buildAgentJobSpec(const DawnLaunchOptions& options, const std::string& guid) {
    const std::filesystem::path jobsPath =
        findJobObjectFile(options.jobDirectory, guid, ".jobs", ".jobgz");
    const BakingJobInputs jobs = readBakingJobInputsFile(jobsPath);

    AgentJobSpec spec;
    spec.address = options.agentAddress;
    std::string jobId = guid;
    std::transform(jobId.begin(), jobId.end(), jobId.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    spec.missionId = guid;
    spec.jobId = jobId;
    spec.jobDirectory = std::filesystem::absolute(options.jobDirectory);
    spec.localExecutablePath = options.dawnExecutable;
    spec.fileCacheDirectory = spec.jobDirectory.string();
    spec.agentFileAddress = options.agentFileAddress;
    if (options.downloadAgentResults) {
        spec.downloadDirectory = spec.jobDirectory;
        spec.forceDownloadResults = options.forceAgentDownload;
    }

    const std::filesystem::path executableName = options.dawnExecutable.filename();
    if (!executableName.empty()) {
        spec.exeFiles.push_back(executableName.string());
    }

    for (const auto& entry : std::filesystem::directory_iterator(options.jobDirectory)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::string extension = entry.path().extension().string();
        if (extension == ".scene" || extension == ".jobs" || extension == ".scenegz" || extension == ".jobgz" ||
            extension == ".meshgz" || extension == ".matgz") {
            spec.jobFiles.push_back(entry.path().filename().string());
        }
    }

    const auto clampTaskCost = [](uint64_t value) {
        const uint64_t limit = static_cast<uint64_t>(std::numeric_limits<int32_t>::max() - 1);
        return static_cast<int32_t>(std::min<uint64_t>(value, limit));
    };
    const uint64_t samples = static_cast<uint64_t>(
        std::max(jobs.bakingParameters.lightmap2DParameter.iterationNum, 1));
    const uint64_t bounces = static_cast<uint64_t>(
        std::max(jobs.bakingParameters.lightmap2DParameter.maxDepth, 0) + 1);
    const uint64_t lightmapGroupScale = samples * bounces;
    const uint64_t sdfGroupScale = static_cast<uint64_t>(
        static_cast<double>(samples) * jobs.bakingParameters.sdfShadowParameter.shadowRayFactor *
        std::max(jobs.bakingParameters.sdfShadowParameter.maxUpsamplingFactor, 1));

    std::vector<bool> packedLightmapJobs(jobs.lightmap2DJobs.size(), false);
    for (const MappingGroupInput& group : jobs.mappingGroupJobs) {
        for (const Int3& offset : group.mappingOffsets) {
            if (offset.z < 0 || static_cast<std::size_t>(offset.z) >= jobs.lightmap2DJobs.size()) {
                throw std::runtime_error("lightmap group references an invalid job index");
            }
            packedLightmapJobs[static_cast<std::size_t>(offset.z)] = true;
        }
    }
    std::vector<bool> splitLightmapJobs(jobs.lightmap2DJobs.size(), false);
    for (const MappingSplitInput& split : jobs.mappingSplitJobs) {
        if (split.mappingOffsets.z < 0 ||
            static_cast<std::size_t>(split.mappingOffsets.z) >= jobs.lightmap2DJobs.size()) {
            throw std::runtime_error("lightmap split references an invalid job index");
        }
        splitLightmapJobs[static_cast<std::size_t>(split.mappingOffsets.z)] = true;
    }
    for (std::size_t index = 0; index < jobs.lightmap2DJobs.size(); ++index) {
        if (packedLightmapJobs[index] || splitLightmapJobs[index]) {
            continue;
        }
        const Lightmap2DInput& job = jobs.lightmap2DJobs[index];
        AgentTaskSpec task;
        task.taskId = guidHex(job.jobId);
        task.parameters = "LIGHTMAP2D";
        task.cost = clampTaskCost(static_cast<uint64_t>(std::max(job.size.x, 0)) *
                                  static_cast<uint64_t>(std::max(job.size.y, 0)) * samples);
        task.resultFiles.push_back(lightmapResultFileName(job.jobId));
        spec.tasks.push_back(std::move(task));
    }
    for (const MappingGroupInput& group : jobs.mappingGroupJobs) {
        if (group.mappingOffsets.empty()) {
            throw std::runtime_error("lightmap group has no jobs");
        }
        AgentTaskSpec task;
        task.taskId = guidHex(group.jobId);
        task.parameters = "LIGHTMAP2D_GROUP";
        task.cost = clampTaskCost(static_cast<uint64_t>(std::max(group.groupedSize, 0)) *
                                  static_cast<uint64_t>(std::max(group.groupedSize, 0)) *
                                  lightmapGroupScale);
        for (const Int3& offset : group.mappingOffsets) {
            task.resultFiles.push_back(
                lightmapResultFileName(jobs.lightmap2DJobs[static_cast<std::size_t>(offset.z)].jobId));
        }
        spec.tasks.push_back(std::move(task));
    }
    for (const SdfShadowGroupInput& group : jobs.sdfShadowGroupJobs) {
        AgentTaskSpec task;
        task.taskId = guidHex(group.jobId);
        task.parameters = "SHADOWMASK_GROUP";
        uint64_t lightCount = 0;
        for (const int32_t index : group.shadowJobIndices) {
            if (index < 0 || static_cast<std::size_t>(index) >= jobs.sdfShadowJobs.size()) {
                throw std::runtime_error("SDF shadow group references an invalid job index");
            }
            lightCount = std::max<uint64_t>(
                lightCount, jobs.sdfShadowJobs[static_cast<std::size_t>(index)].shadowmaskLights.size());
            task.resultFiles.push_back(
                shadowmaskResultFileName(jobs.sdfShadowJobs[static_cast<std::size_t>(index)].header.jobId));
        }
        task.cost = clampTaskCost(static_cast<uint64_t>(std::max(group.groupSize, 0)) *
                                  static_cast<uint64_t>(std::max(group.groupSize, 0)) * lightCount *
                                  sdfGroupScale);
        spec.tasks.push_back(std::move(task));
    }
    for (const MappingSplitInput& split : jobs.mappingSplitJobs) {
        AgentTaskSpec task;
        task.taskId = guidHex(split.jobId);
        task.parameters = "LIGHTMAP2D_SPLIT";
        task.cost = clampTaskCost(static_cast<uint64_t>(std::max(split.splitSize, 0)) *
                                  static_cast<uint64_t>(std::max(split.splitSize, 0)) * lightmapGroupScale);
        task.resultFiles.push_back(lightmapResultFileName(split.jobId));
        spec.tasks.push_back(std::move(task));
    }
    return spec;
}
#endif

#ifdef HBG_DAWN_GRPC
int runAgentBake(const DawnLaunchOptions& options, const std::string& guid) {
    const AgentJobSpec spec = buildAgentJobSpec(options, guid);
    const auto bakeStart = std::chrono::steady_clock::now();
    AgentClient client(options.agentAddress);
    const AgentJobResult result = client.runJob(spec, options.timeout, options.onProgress);
    const auto bakeElapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::steady_clock::now() - bakeStart)
                                   .count();

    std::size_t available = 0;
    for (const AgentTaskSpec& task : spec.tasks) {
        for (const std::string& file : task.resultFiles) {
            if (std::filesystem::is_regular_file(options.jobDirectory / file)) {
                ++available;
                break;
            }
        }
    }
    const std::size_t total = spec.tasks.size();
    std::cout << "Dawn bake total: elapsed_ms=" << bakeElapsedMs << " tasks=" << total << "\n";
    const double percent = total > 0u ? 100.0 * static_cast<double>(available) / static_cast<double>(total) : 0.0;
    std::cout << "DAWN_PROGRESS phase=" << (result.error == DawnBakeError::None ? "done" : "failed")
              << " completed=" << available << " total=" << total << " percent=" << std::fixed << std::setprecision(1)
              << percent << "\n";
    std::cout << "Dawn agent: " << dawnBakeErrorName(result.error) << " (" << static_cast<int32_t>(result.error)
              << ") ack=" << result.ackCode << " tasks=" << result.completedTasks << "/" << result.totalTasks << "\n";
    if (!result.message.empty()) {
        std::cerr << "error: " << result.message << "\n";
    }
    if (result.error != DawnBakeError::None) {
        return 1;
    }
    return completeDawnBake(options, guid);
}
#endif

#ifdef HBG_DAWN_GRPC
int agentUploadCommand(int argc, char** argv) {
    if (argc != 6) {
        throw std::runtime_error("--agent-upload requires <agent-address> <job-id> <local-file> <remote-path>");
    }
    const std::string address = argv[2];
    const std::string jobId = argv[3];
    AgentClient client(address);
    std::string error;
    if (!client.uploadFile(address, jobId, argv[4], argv[5], std::chrono::minutes(10), &error)) {
        throw std::runtime_error(error.empty() ? "agent upload failed" : error);
    }
    std::cout << "agent upload OK: " << argv[4] << " -> " << argv[5] << "\n";
    return 0;
}

int agentCacheUploadCommand(int argc, char** argv) {
    if (argc < 6 || (argc - 4) % 2 != 0) {
        throw std::runtime_error(
            "--agent-cache-upload requires <agent-address> <job-id> <hash> <remote-path> [<hash> <remote-path> ...]");
    }
    const std::string address = argv[2];
    const std::string jobId = argv[3];
    std::vector<AgentCacheFile> files;
    for (int i = 4; i < argc; i += 2) {
        files.push_back(AgentCacheFile{argv[i], argv[i + 1]});
    }
    AgentClient client(address);
    std::string error;
    if (!client.batchUploadFileInServerCache(address, jobId, files, std::chrono::minutes(10), &error)) {
        throw std::runtime_error(error.empty() ? "agent cache upload failed" : error);
    }
    std::cout << "agent cache upload OK: " << files.size() << " file(s)\n";
    return 0;
}

int agentTransferCommand(int argc, char** argv) {
    if (argc < 6) {
        throw std::runtime_error(
            "--agent-transfer-job requires <agent-address> <job-id> <another-agent-address> <remote-path> ...");
    }
    const std::string address = argv[2];
    const std::string jobId = argv[3];
    const std::string anotherAgent = argv[4];
    std::vector<std::string> paths(argv + 5, argv + argc);
    AgentClient client(address);
    std::string error;
    if (!client.uploadJobFilesToAnotherAgent(address, jobId, anotherAgent, paths, std::chrono::minutes(10), &error)) {
        throw std::runtime_error(error.empty() ? "agent job transfer failed" : error);
    }
    std::cout << "agent job transfer OK: " << paths.size() << " file(s)\n";
    return 0;
}

int agentMultiUploadCommand(int argc, char** argv) {
    if (argc < 7 || (argc - 4) % 3 != 0) {
        throw std::runtime_error(
            "--agent-multi-upload requires <agent-address> <job-id> <hash> <remote-path> <local-file> "
            "[<hash> <remote-path> <local-file> ...]");
    }
    const std::string address = argv[2];
    const std::string jobId = argv[3];
    std::vector<AgentUploadFile> files;
    for (int i = 4; i < argc; i += 3) {
        AgentUploadFile file;
        file.hash = argv[i];
        file.remotePath = argv[i + 1];
        file.data = readFile(argv[i + 2]);
        files.push_back(std::move(file));
    }
    AgentClient client(address);
    std::string error;
    if (!client.uploadMultiFiles(address, jobId, files, std::chrono::minutes(10), &error)) {
        throw std::runtime_error(error.empty() ? "agent multi upload failed" : error);
    }
    std::cout << "agent multi upload OK: " << files.size() << " file(s)\n";
    return 0;
}

int agentDownloadCommand(int argc, char** argv) {
    if (argc != 6) {
        throw std::runtime_error("--agent-download requires <agent-address> <job-id> <remote-path> <local-file>");
    }
    const std::string address = argv[2];
    const std::string jobId = argv[3];
    AgentClient client(address);
    std::string error;
    if (!client.downloadFile(address, jobId, argv[4], argv[5], std::chrono::minutes(10), &error)) {
        throw std::runtime_error(error.empty() ? "agent download failed" : error);
    }
    std::cout << "agent download OK: " << argv[4] << " -> " << argv[5] << "\n";
    return 0;
}
#endif

int bakeDawnCommand(int argc, char** argv) {
    DawnLaunchOptions options = parseDawnLaunchOptions(argc, argv, true, "--bake");
    JobWorkspace workspace;
    std::string workspaceError;
    if (!prepareJobWorkspace(options.jobDirectory, options.guid, options.keepJob, &workspace, &workspaceError)) {
        throw std::runtime_error(workspaceError);
    }
    std::cout << "Job dir: " << workspace.path.string() << " ("
              << (workspace.created ? (options.keepJob ? "created, kept" : "created, temporary") : "reused") << ")\n";

    std::string guid = options.guid;
    std::transform(guid.begin(), guid.end(), guid.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    options.guid = guid;
    options.jobDirectory = workspace.path;
    options.expectedResults = expectedDawnResults(options.jobDirectory, guid);
    options.onProgress = [](std::size_t completed, std::size_t total) {
        const double percent = total > 0u ? 100.0 * static_cast<double>(completed) / static_cast<double>(total) : 0.0;
        std::cout << "DAWN_PROGRESS phase=building completed=" << completed << " total=" << total
                  << " percent=" << std::fixed << std::setprecision(1) << percent << "\n"
                  << std::flush;
    };

#ifdef HBG_DAWN_GRPC
    if (!options.agentAddress.empty()) {
        return runAgentBake(options, guid);
    }
#else
    if (!options.agentAddress.empty()) {
        throw std::runtime_error("this build has no gRPC Agent support (configure with -DHBG_BUILD_DAWN_GRPC=ON)");
    }
#endif

    const DawnRunResult result = runDawnJob(options);
    const std::size_t completed = result.resultFiles.size();
    const std::size_t total = result.expectedResults.size();
    const double percent = total > 0u ? 100.0 * static_cast<double>(completed) / static_cast<double>(total) : 0.0;
    std::cout << "DAWN_PROGRESS phase=" << (result.error == DawnBakeError::None ? "done" : "failed")
              << " completed=" << completed << " total=" << total << " percent=" << std::fixed << std::setprecision(1)
              << percent << "\n";
    std::cout << "Dawn job: " << dawnBakeErrorName(result.error) << " (" << static_cast<int32_t>(result.error)
              << ") exit=" << result.process.exitCode << " elapsed=" << result.process.elapsedMs << "ms\n";
    for (const std::filesystem::path& expected : result.expectedResults) {
        const bool found =
            std::find(result.resultFiles.begin(), result.resultFiles.end(), expected) != result.resultFiles.end();
        std::cout << "Dawn result: " << expected.string() << (found ? " found" : " missing") << "\n";
    }
    if (!result.process.logPath.empty()) {
        std::cout << "Dawn log: " << result.process.logPath.string() << "\n";
    }
    if (!result.process.error.empty()) {
        std::cerr << "error: " << result.process.error << "\n";
    }
    if (result.error != DawnBakeError::None) {
        return 1;
    }
    return completeDawnBake(options, guid);
}

int resultPollTest(const std::filesystem::path& fakeDawn) {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    require(std::filesystem::is_regular_file(fakeDawn), "fake Dawn executable missing");
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawnbake_result_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    const std::string guid = "11111111222222223333333344444444";
    writeFile(directory / (guid + ".scene"), std::vector<uint8_t>{1u, 2u, 3u, 4u});
    writeFile(directory / (guid + ".jobs"), std::vector<uint8_t>{1u, 2u, 3u, 4u});
    const std::filesystem::path resultPath = directory / "v2.00000010000000000000000000000000.lmgz";

    DawnLaunchOptions options;
    options.dawnExecutable = fakeDawn;
    options.jobDirectory = directory;
    options.guid = guid;
    options.expectedResults = {resultPath};
    options.timeout = std::chrono::seconds(15);
    options.resultGrace = std::chrono::milliseconds(500);

    setEnvironmentValue("HBG_FAKE_DAWN_RESULT", resultPath.string());
    setEnvironmentValue("HBG_FAKE_DAWN_SLEEP_MS", "0");
    std::vector<std::size_t> progress;
    options.onProgress = [&progress](std::size_t completed, std::size_t) { progress.push_back(completed); };
    const DawnRunResult success = runDawnJob(options);
    require(success.error == DawnBakeError::None, "result polling did not report success");
    require(success.resultFiles.size() == 1u && std::filesystem::is_regular_file(resultPath),
            "result file was not found");
    require(!progress.empty() && progress.back() == 1u, "progress callback did not reach completion");
    require(readTextFile(success.process.logPath).find("fake dawn") != std::string::npos,
            "fake Dawn log was not captured");

    std::filesystem::remove(resultPath);
    setEnvironmentValue("HBG_FAKE_DAWN_RESULT", "");
    const DawnRunResult missing = runDawnJob(options);
    require(missing.error == DawnBakeError::ResultImportFailure, "missing result was not mapped");
    require(missing.missingResults.size() == 1u, "missing result was not listed");

    setEnvironmentValue("HBG_FAKE_DAWN_SLEEP_MS", "3000");
    options.timeout = std::chrono::milliseconds(150);
    options.resultGrace = std::chrono::milliseconds(0);
    const DawnRunResult timedOut = runDawnJob(options);
    require(timedOut.error == DawnBakeError::JobKilled, "timeout was not mapped to JOB_KILLED");
    require(timedOut.process.elapsedMs < 5000u, "timed-out fake Dawn was not killed");
    setEnvironmentValue("HBG_FAKE_DAWN_SLEEP_MS", "");

    std::printf("result poll test OK: success=%s missing=%s timeout=%s\n", dawnBakeErrorName(success.error),
                dawnBakeErrorName(missing.error), dawnBakeErrorName(timedOut.error));
    return 0;
}

int jobLifecycleTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawnbake_jobs_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    const std::string guid = "11111111222222223333333344444444";
    std::string error;
    std::filesystem::path autoPath;

    {
        JobWorkspace workspace;
        require(prepareJobWorkspace(directory / "auto", guid, false, &workspace, &error), "auto job workspace failed");
        require(workspace.created, "auto job workspace was not marked created");
        autoPath = workspace.path;
        require(std::filesystem::is_directory(autoPath), "auto job workspace missing");
    }
    require(!std::filesystem::exists(autoPath), "temporary job workspace was not removed");

    std::filesystem::path keepPath;
    {
        JobWorkspace workspace;
        require(prepareJobWorkspace(directory / "keep", guid, true, &workspace, &error), "kept job workspace failed");
        keepPath = workspace.path;
    }
    require(std::filesystem::is_directory(keepPath), "kept job workspace was removed");
    std::filesystem::remove_all(keepPath);

    const std::filesystem::path existing = directory / "existing";
    std::filesystem::create_directories(existing);
    {
        JobWorkspace workspace;
        require(prepareJobWorkspace(existing, guid, false, &workspace, &error), "existing job workspace failed");
        require(!workspace.created, "existing job workspace was marked created");
    }
    require(std::filesystem::is_directory(existing), "existing job workspace was removed");

    std::filesystem::path temporaryPath;
    std::filesystem::remove_all(std::filesystem::temp_directory_path() / "hbg_dawnbake" / "jobs" / guid);
    {
        JobWorkspace workspace;
        require(prepareJobWorkspace({}, guid, false, &workspace, &error), "default job workspace failed");
        temporaryPath = workspace.path;
        require(std::filesystem::is_directory(temporaryPath), "default job workspace missing");
    }
    require(!std::filesystem::exists(temporaryPath), "default job workspace was not removed");

    std::printf("job lifecycle test OK: auto-remove, keep, existing-preserve, default-temp\n");
    return 0;
}

Guid jsonGuid(const nlohmann::json& value, const char* name) {
    if (!value.is_array() || value.size() != 4) {
        throw std::runtime_error(std::string(name) + " must be a four-element array");
    }
    return Guid{value[0].get<uint32_t>(), value[1].get<uint32_t>(), value[2].get<uint32_t>(), value[3].get<uint32_t>()};
}

bool allZero(const uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        if (data[i] != 0u) {
            return false;
        }
    }
    return true;
}

struct GoldenChecks {
    std::size_t fields = 0;

    void require(bool condition, const std::string& message) {
        ++fields;
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    void u32(const char* name, uint32_t actual, uint32_t expected) {
        require(actual == expected,
                std::string(name) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
    }

    void i32(const char* name, int32_t actual, int32_t expected) {
        require(actual == expected,
                std::string(name) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
    }

    void u64(const char* name, uint64_t actual, uint64_t expected) {
        require(actual == expected,
                std::string(name) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
    }

    void f32(const char* name, float actual, float expected) {
        require(std::fabs(actual - expected) <= 1.0e-6f,
                std::string(name) + ": expected " + std::to_string(expected) + ", actual " + std::to_string(actual));
    }

    void f2(const char* name, const Float2& actual, const Float2& expected) {
        require(std::fabs(actual.x - expected.x) <= 1.0e-6f && std::fabs(actual.y - expected.y) <= 1.0e-6f,
                std::string(name) + " mismatch");
    }

    void f3(const char* name, const Float3& actual, const Float3& expected) {
        require(std::fabs(actual.x - expected.x) <= 1.0e-6f && std::fabs(actual.y - expected.y) <= 1.0e-6f &&
                    std::fabs(actual.z - expected.z) <= 1.0e-6f,
                std::string(name) + " mismatch");
    }

    void f4(const char* name, const Float4& actual, const Float4& expected) {
        require(std::fabs(actual.x - expected.x) <= 1.0e-6f && std::fabs(actual.y - expected.y) <= 1.0e-6f &&
                    std::fabs(actual.z - expected.z) <= 1.0e-6f && std::fabs(actual.w - expected.w) <= 1.0e-6f,
                std::string(name) + " mismatch");
    }

    void guid(const char* name, const Guid& actual, const Guid& expected) {
        require(actual.a == expected.a && actual.b == expected.b && actual.c == expected.c && actual.d == expected.d,
                std::string(name) + " mismatch");
    }

    void color(const char* name, const MaterialColor& actual, const MaterialColor& expected) {
        require(actual.x == expected.x && actual.y == expected.y && actual.z == expected.z && actual.w == expected.w,
                std::string(name) + " mismatch");
    }

    void identity(const char* name, const Matrix4x4& matrix) {
        for (int i = 0; i < 16; ++i) {
            const float expected = (i % 5 == 0) ? 1.0f : 0.0f;
            require(std::fabs(matrix.mm[i] - expected) <= 1.0e-6f,
                    std::string(name) + "[" + std::to_string(i) + "] mismatch");
        }
    }
};

int protocolVersionTest() {
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawn_protocol_version_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);

    const auto requireUnsupported = [&](int32_t version, const char* kind) {
        Writer writer;
        writer.version(version);
        const std::filesystem::path path = directory / (std::string(kind) + ".bin");
        writeFile(path, writer.bytes());

        bool rejected = false;
        std::string message;
        try {
            if (std::strcmp(kind, "scene") == 0) {
                readSceneFile(path);
            } else {
                readBakingJobInputsFile(path);
            }
        } catch (const std::exception& error) {
            rejected = true;
            message = error.what();
        }
        require(rejected, std::string(kind) + " version " + std::to_string(version) + " was accepted");
        require(message.find(std::string(kind) + " version " + std::to_string(version) + " is not supported") !=
                    std::string::npos,
                std::string(kind) + " version error is not actionable: " + message);
    };

    requireUnsupported(kExportVersion1000 - 1, "scene");
    requireUnsupported(kExportVersion + 1, "scene");
    requireUnsupported(kExportVersion1000 - 1, "jobs");
    requireUnsupported(kExportVersion + 1, "jobs");

    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    std::printf("protocol version test OK: supported=%d..%d\n", kExportVersion1000, kExportVersion);
    return 0;
}

#ifdef HBG_DAWN_GRPC
int agentSpecTest() {
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawn_agent_spec_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    const std::string guid = "55556666777788889999aaaabbbbcccc";
    ExportedSceneBundle bundle = buildExportedScene();
    SdfShadowInput shadow;
    shadow.header.jobId = Guid{0u, 1u, 0u, 0u};
    shadow.header.meshId = bundle.scene.meshInstances.front().header.guid;
    shadow.header.flags = kLightmapFlagBilinearFilter;
    shadow.header.size = Int2{64, 64};
    shadow.header.upsampleFactor = 4;
    shadow.header.lightmapUVIndex = 1;
    shadow.lights.push_back(bundle.scene.lightGuids.front());
    shadow.shadowmaskLights.push_back(bundle.scene.lightGuids.front());
    bundle.jobs.sdfShadowJobs.push_back(shadow);
    SdfShadowGroupInput shadowGroup;
    shadowGroup.jobId = Guid{102u, 1u, 0u, 0xFFFFFFFFu};
    shadowGroup.groupSize = 64;
    shadowGroup.upsampleFactor = 4;
    shadowGroup.shadowJobIndices.push_back(0);
    shadowGroup.mappingOffsets.push_back(Int3{0, 0, 0});
    bundle.jobs.sdfShadowGroupJobs.push_back(shadowGroup);
    bundle.jobs.sdfShadowJobsCount = 1u;
    bundle.jobs.sdfShadowGroupJobsCount = 1u;
    bundle.jobs.partialJobsTotalNum.numSdfShadowJobs = 1u;
    bundle.jobs.partialJobsTotalNum.numSdfShadowGroupJobs = 1u;
    bundle.jobs.bakingParameters.sdfShadowParameter.maxUpsamplingFactor = 4;
    bundle.jobs.bakingParameters.sdfShadowParameter.shadowRayFactor = 1.0f;
    MappingGroupInput lightmapGroup;
    lightmapGroup.jobId = Guid{0u, 0u, 0u, 0xFFFFFFFFu};
    lightmapGroup.groupedSize = 64;
    lightmapGroup.mappingOffsets.push_back(Int3{0, 0, 0});
    bundle.jobs.mappingGroupJobs.push_back(lightmapGroup);
    bundle.jobs.mappingGroupJobsCount = 1u;
    bundle.jobs.partialJobsTotalNum.numMappingGroupJobs = 1u;
    Lightmap2DInput oversizedLightmap;
    oversizedLightmap.jobId = Guid{0u, 0u, 1u, 5u};
    oversizedLightmap.meshId = bundle.scene.meshInstances.front().header.guid;
    oversizedLightmap.size = Int2{128, 128};
    oversizedLightmap.lightmapUVIndex = 1;
    oversizedLightmap.flags = kLightmapFlagDirect | kLightmapFlagIndirect | kLightmapFlagBilinearFilter;
    bundle.jobs.lightmap2DJobs.push_back(oversizedLightmap);
    Lightmap2DInput splitLightmap = oversizedLightmap;
    splitLightmap.jobId = Guid{0u, 0u, 2u, 5u};
    bundle.jobs.lightmap2DJobs.push_back(splitLightmap);
    MappingSplitInput splitJob;
    splitJob.jobId = Guid{0u, 103u, 0u, 2u};
    splitJob.splitSize = 64;
    splitJob.overlapSize = 3;
    splitJob.mappingOffsets = Int3{0, 0, 2};
    bundle.jobs.mappingSplitJobs.push_back(splitJob);
    bundle.jobs.mappingSplitJobsCount = 1u;
    bundle.jobs.partialJobsTotalNum.numLightmap2DJobs = 3u;
    bundle.jobs.partialJobsTotalNum.numMappingSplitJobs = 1u;
    const PackageFiles files = writeDawnPackage(directory, guid, bundle.scene, bundle.jobs, &bundle.lightmap);
    std::filesystem::remove(files.scene);
    std::filesystem::remove(files.jobs);
    require(!std::filesystem::exists(files.scene) && !std::filesystem::exists(files.jobs),
            "agent spec fixture still has raw scene/jobs");

    DawnLaunchOptions options;
    options.jobDirectory = directory;
    options.agentAddress = "http://127.0.0.1:5009";
    const AgentJobSpec spec = buildAgentJobSpec(options, guid);

    std::string expectedJobId = guid;
    std::transform(expectedJobId.begin(), expectedJobId.end(), expectedJobId.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    require(spec.missionId == guid, "agent mission id should stay lower-case");
    require(spec.jobId == expectedJobId, "agent job id must use original %08X upper-case form");
    require(spec.tasks.size() == 4u, "agent task count mismatch");
    require(spec.tasks[0].taskId == "00000000000000000000000100000005" &&
                spec.tasks[0].parameters == "LIGHTMAP2D",
            "agent individual lightmap task id/parameters mismatch");
    require(spec.tasks[0].resultFiles.size() == 1u &&
                spec.tasks[0].resultFiles[0] == "v2.00000000000000000000000100000005.lmgz" &&
                spec.tasks[0].cost == 128 * 128 * 64,
            "agent individual lightmap result/cost mismatch");
    require(spec.tasks[1].taskId == "000000000000000000000000FFFFFFFF" &&
                spec.tasks[1].parameters == "LIGHTMAP2D_GROUP",
            "agent lightmap group task id/parameters mismatch");
    require(spec.tasks[1].resultFiles.size() == 1u &&
                spec.tasks[1].resultFiles[0] == "v2.00000000000000000000000000000005.lmgz" &&
                spec.tasks[1].cost == 64 * 64 * 64 * 4,
            "agent lightmap group result/cost mismatch");
    require(spec.tasks[2].taskId == "000000660000000100000000FFFFFFFF" &&
                spec.tasks[2].parameters == "SHADOWMASK_GROUP",
            "agent shadowmask group task id/parameters mismatch");
    require(spec.tasks[2].resultFiles.size() == 1u &&
                spec.tasks[2].resultFiles[0] == "v2.00000000000000010000000000000000.sdfgz" &&
                spec.tasks[2].cost == 64 * 64 * 64 * 4,
            "agent shadowmask result/cost mismatch");
    require(spec.tasks[3].taskId == "00000000000000670000000000000002" &&
                spec.tasks[3].parameters == "LIGHTMAP2D_SPLIT",
            "agent split lightmap task id/parameters mismatch");
    require(spec.tasks[3].resultFiles.size() == 1u &&
                spec.tasks[3].resultFiles[0] == "v2.00000000000000670000000000000002.lmgz" &&
                spec.tasks[3].cost == 64 * 64 * 64 * 4,
            "agent split lightmap result/cost mismatch");

    bool hasSceneGz = false;
    bool hasJobsGz = false;
    for (const std::string& file : spec.jobFiles) {
        hasSceneGz = hasSceneGz || file.find(".scenegz") != std::string::npos;
        hasJobsGz = hasJobsGz || file.find(".jobgz") != std::string::npos;
    }
    require(hasSceneGz && hasJobsGz, "agent job files must include scenegz/jobgz");
    require(spec.jobFiles.size() == 4u, "agent job files should only contain gzip side/cache files");

    const std::vector<std::filesystem::path> results = expectedDawnResults(directory, guid);
    require(results.size() == 4u, "agent expected result count mismatch");
    require(results[0].filename().string() == "v2.00000000000000000000000000000005.lmgz",
            "agent lightmap result name mismatch");
    require(results[1].filename().string() == "v2.00000000000000000000000100000005.lmgz",
            "agent oversized lightmap result name mismatch");
    require(results[2].filename().string() == "v2.00000000000000670000000000000002.lmgz",
            "agent split lightmap result name mismatch");
    require(results[3].filename().string() == "v2.00000000000000010000000000000000.sdfgz",
            "agent shadowmask result name mismatch");

    std::printf("agent spec test OK: job=%s tasks=%zu files=%zu\n", spec.jobId.c_str(), spec.tasks.size(),
                spec.jobFiles.size());
    return 0;
}

class FakeAgentService final : public DAWNProto::AgentService::Service {
  public:
    grpc::Status UploadFile(grpc::ServerContext*,
                            grpc::ServerReaderWriter<DAWNProto::UploadFileAck, DAWNProto::UploadFileReq>* stream) override {
        DAWNProto::UploadFileReq header;
        if (!stream->Read(&header)) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, "missing upload header");
        }
        if (!header.has_jobid() || !header.has_filepath() || !header.has_filesize() || !header.has_hash()) {
            DAWNProto::UploadFileAck ack;
            ack.set_type(DAWNProto::UploadFileAck_UploadFileAckType_Fail);
            ack.set_message("upload header missing required field");
            stream->Write(ack);
            return grpc::Status::OK;
        }

        const bool fromCache = header.filepath() == "cached";
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_uploadJobId = header.jobid();
            m_uploadPath = header.filepath();
            m_uploadHash = header.hash();
            m_uploadSize = header.filesize();
            m_uploadData.clear();
            m_uploadFromCache = fromCache;
        }
        if (header.filepath() == "fail") {
            DAWNProto::UploadFileAck ack;
            ack.set_type(DAWNProto::UploadFileAck_UploadFileAckType_Fail);
            ack.set_message("upload failure");
            stream->Write(ack);
            return grpc::Status::OK;
        }

        DAWNProto::UploadFileAck ready;
        ready.set_type(fromCache ? DAWNProto::UploadFileAck_UploadFileAckType_DoneInCache
                                 : DAWNProto::UploadFileAck_UploadFileAckType_NotInCache);
        if (!stream->Write(ready)) {
            return grpc::Status::OK;
        }
        if (fromCache) {
            return grpc::Status::OK;
        }

        std::string data;
        DAWNProto::UploadFileReq chunk;
        while (stream->Read(&chunk)) {
            if (!chunk.has_filedata() || !chunk.has_filedatadone() || chunk.filedata().empty()) {
                DAWNProto::UploadFileAck ack;
                ack.set_type(DAWNProto::UploadFileAck_UploadFileAckType_Fail);
                ack.set_message("invalid upload data message");
                stream->Write(ack);
                return grpc::Status::OK;
            }
            data.append(chunk.filedata());
            if (chunk.filedatadone()) {
                break;
            }
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_uploadData = data;
        }
        DAWNProto::UploadFileAck done;
        if (data.size() != header.filesize()) {
            done.set_type(DAWNProto::UploadFileAck_UploadFileAckType_Fail);
            done.set_message("uploaded size mismatch");
        } else {
            done.set_type(DAWNProto::UploadFileAck_UploadFileAckType_Done);
        }
        stream->Write(done);
        return grpc::Status::OK;
    }

    grpc::Status BatchUploadFileInServerCache(grpc::ServerContext*,
                                               const DAWNProto::BatchUploadFilesInServerCacheReq* request,
                                               DAWNProto::BatchUploadFilesInServerCacheAck* response) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobId = request->jobid();
        m_files.clear();
        for (const DAWNProto::UploadFileInfo& file : request->files()) {
            m_files.push_back(AgentCacheFile{file.hash(), file.filepath()});
            DAWNProto::UploadFileAck* ack = response->add_acks();
            if (file.hash() == "miss") {
                ack->set_type(DAWNProto::UploadFileAck_UploadFileAckType_NotInCache);
            } else if (file.hash() == "fail") {
                ack->set_type(DAWNProto::UploadFileAck_UploadFileAckType_Fail);
                ack->set_message("cache failure");
            } else {
                ack->set_type(DAWNProto::UploadFileAck_UploadFileAckType_DoneInCache);
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status UploadMultiFiles(grpc::ServerContext*, const DAWNProto::UploadMultiFilesReq* request,
                                  DAWNProto::UploadMultiFilesAck* response) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_multiJobId = request->jobid();
        m_multiFiles.clear();
        for (const DAWNProto::UploadFileInfo& file : request->files()) {
            AgentUploadFile upload;
            upload.hash = file.hash();
            upload.remotePath = file.filepath();
            upload.data.assign(file.filedata().begin(), file.filedata().end());
            m_multiFiles.push_back(std::move(upload));
            DAWNProto::UploadFileAck* ack = response->add_acks();
            if (file.hash() == "fail") {
                ack->set_type(DAWNProto::UploadFileAck_UploadFileAckType_Fail);
                ack->set_message("multi upload failure");
            } else {
                ack->set_type(DAWNProto::UploadFileAck_UploadFileAckType_Done);
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status UploadJobFilesToAnotherAgent(grpc::ServerContext*,
                                               const DAWNProto::UploadToAnotherAgentReq* request,
                                               DAWNProto::GeneralAck* response) override {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_transferJobId = request->jobid();
        m_transferAgent = request->anotheragentaddress();
        m_transferPaths.assign(request->filepaths().begin(), request->filepaths().end());
        response->set_code(DAWNProto::OK);
        for (const std::string& path : m_transferPaths) {
            if (path == "fail") {
                response->set_code(DAWNProto::Fail);
                response->set_message("transfer failure");
            }
        }
        return grpc::Status::OK;
    }

    grpc::Status DownloadFile(grpc::ServerContext*, const DAWNProto::DownloadFileReq* request,
                              grpc::ServerWriter<DAWNProto::DownloadFileAck>* writer) override {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_downloadJobId = request->jobid();
            m_downloadPath = request->filepath();
        }
        if (request->filepath() == "missing") {
            DAWNProto::DownloadFileAck ack;
            ack.set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_Fail_FileNotFound);
            ack.set_message("download file not found");
            writer->Write(ack);
            return grpc::Status::OK;
        }
        if (request->filepath() == "fail") {
            DAWNProto::DownloadFileAck ack;
            ack.set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_Fail);
            ack.set_message("download failed");
            writer->Write(ack);
            return grpc::Status::OK;
        }
        if (request->filepath() == "short") {
            DAWNProto::DownloadFileAck ack;
            ack.set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone);
            ack.set_filedata("short");
            ack.set_filehash("A0F4EA7D91495DF92BBAC2E2149DFB850FE81396");
            writer->Write(ack);
            return grpc::Status::OK;
        }

        DAWNProto::DownloadFileAck first;
        first.set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_Data);
        first.set_filedata(request->filepath() == "empty" ? "" : "hello ");
        if (!writer->Write(first)) {
            return grpc::Status::OK;
        }
        if (request->filepath() != "empty") {
            DAWNProto::DownloadFileAck second;
            second.set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_Data);
            second.set_filedata("world");
            if (!writer->Write(second)) {
                return grpc::Status::OK;
            }
        }
        DAWNProto::DownloadFileAck done;
        done.set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone);
        done.set_filehash(request->filepath() == "empty" ? "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709"
                                                         : "2AAE6C35C94FCFB415DBE95F408B9CE91EE846ED");
        writer->Write(done);
        return grpc::Status::OK;
    }

    std::pair<std::string, std::vector<AgentCacheFile>> snapshot() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {m_jobId, m_files};
    }

    grpc::Status DownloadMultiFiles(grpc::ServerContext*,
                                    const DAWNProto::DownloadMultiFilesReq* request,
                                    grpc::ServerWriter<DAWNProto::DownloadMultiFilesAck>* writer) override {
        if (request->jobid() == "MULTI_UNSUPPORTED_JOB") {
            return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "multi download is not supported");
        }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_multiDownloadJobId = request->jobid();
            m_multiDownloadPaths.assign(request->filepathes().begin(), request->filepathes().end());
        }
        DAWNProto::DownloadMultiFilesAck ack;
        for (const std::string& path : request->filepathes()) {
            if (path == "missing") {
                DAWNProto::DownloadFileAck* item = ack.add_items();
                item->set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_Fail_FileNotFound);
                item->set_filename(path);
                item->set_message("multi download file not found");
                writer->Write(ack);
                return grpc::Status::OK;
            }
            if (path == "fail") {
                DAWNProto::DownloadFileAck* item = ack.add_items();
                item->set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_Fail);
                item->set_filename(path);
                item->set_message("multi download failed");
                writer->Write(ack);
                return grpc::Status::OK;
            }
            if (path == "errmsg") {
                ack.set_errmsg("multi download stream failure");
                writer->Write(ack);
                return grpc::Status::OK;
            }
            if (path == "badhash") {
                DAWNProto::DownloadFileAck* item = ack.add_items();
                item->set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone);
                item->set_filename(path);
                item->set_filedata("hello world");
                item->set_filehash("0000000000000000000000000000000000000000");
                continue;
            }
            if (path == "empty") {
                DAWNProto::DownloadFileAck* item = ack.add_items();
                item->set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone);
                item->set_filename(path);
                item->set_filehash("DA39A3EE5E6B4B0D3255BFEF95601890AFD80709");
                continue;
            }
            if (path == "short") {
                DAWNProto::DownloadFileAck* item = ack.add_items();
                item->set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone);
                item->set_filename(path);
                item->set_filedata("short");
                item->set_filehash("A0F4EA7D91495DF92BBAC2E2149DFB850FE81396");
                continue;
            }
            DAWNProto::DownloadFileAck* data = ack.add_items();
            data->set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_Data);
            data->set_filename(path);
            data->set_filedata("hello ");
            DAWNProto::DownloadFileAck* done = ack.add_items();
            done->set_type(DAWNProto::DownloadFileAck_DownloadFileAckType_DataDone);
            done->set_filename(path);
            done->set_filedata("world");
            done->set_filehash("2AAE6C35C94FCFB415DBE95F408B9CE91EE846ED");
        }
        writer->Write(ack);
        return grpc::Status::OK;
    }

    struct UploadSnapshot {
        std::string jobId;
        std::string path;
        std::string hash;
        uint64_t size = 0;
        std::string data;
        bool fromCache = false;
    };

    UploadSnapshot uploadSnapshot() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {m_uploadJobId, m_uploadPath, m_uploadHash, m_uploadSize, m_uploadData, m_uploadFromCache};
    }

    std::pair<std::string, std::string> downloadSnapshot() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {m_downloadJobId, m_downloadPath};
    }

    std::pair<std::string, std::vector<AgentUploadFile>> multiSnapshot() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {m_multiJobId, m_multiFiles};
    }

    std::pair<std::string, std::vector<std::string>> multiDownloadSnapshot() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {m_multiDownloadJobId, m_multiDownloadPaths};
    }

    struct TransferSnapshot {
        std::string jobId;
        std::string anotherAgent;
        std::vector<std::string> paths;
    };

    TransferSnapshot transferSnapshot() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return {m_transferJobId, m_transferAgent, m_transferPaths};
    }

  private:
    std::mutex m_mutex;
    std::string m_jobId;
    std::vector<AgentCacheFile> m_files;
    std::string m_uploadJobId;
    std::string m_uploadPath;
    std::string m_uploadHash;
    uint64_t m_uploadSize = 0;
    std::string m_uploadData;
    bool m_uploadFromCache = false;
    std::string m_downloadJobId;
    std::string m_downloadPath;
    std::string m_multiJobId;
    std::vector<AgentUploadFile> m_multiFiles;
    std::string m_multiDownloadJobId;
    std::vector<std::string> m_multiDownloadPaths;
    std::string m_transferJobId;
    std::string m_transferAgent;
    std::vector<std::string> m_transferPaths;
};

int agentUploadTest() {
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    FakeAgentService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    require(server != nullptr && port != 0, "failed to start fake Agent upload server");
    const std::string address = "http://127.0.0.1:" + std::to_string(port);

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawn_agent_upload_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    const auto writeText = [](const std::filesystem::path& path, const std::string& text) {
        writeFile(path, std::vector<uint8_t>(text.begin(), text.end()));
    };

    AgentClient client(address);
    const std::filesystem::path hello = directory / "hello.txt";
    writeText(hello, "hello world");
    std::string error;
    require(client.uploadFile(address, "UPLOAD_JOB", hello, "remote/hello", std::chrono::seconds(5), &error),
            "uploadFile should succeed: " + error);
    auto uploaded = service.uploadSnapshot();
    require(uploaded.jobId == "UPLOAD_JOB" && uploaded.path == "remote/hello", "upload header metadata mismatch");
    require(uploaded.hash == "2AAE6C35C94FCFB415DBE95F408B9CE91EE846ED", "upload SHA1 mismatch: " + uploaded.hash);
    require(uploaded.size == 11u && uploaded.data == "hello world", "upload payload mismatch");
    require(!uploaded.fromCache, "non-cached upload reported as cache hit");

    const std::filesystem::path empty = directory / "empty.txt";
    writeText(empty, "");
    error.clear();
    require(client.uploadFile(address, "UPLOAD_JOB", empty, "remote/empty", std::chrono::seconds(5), &error),
            "empty uploadFile should succeed: " + error);
    uploaded = service.uploadSnapshot();
    require(uploaded.size == 0u && uploaded.data.empty(), "empty upload payload mismatch");
    require(uploaded.hash == "DA39A3EE5E6B4B0D3255BFEF95601890AFD80709", "empty SHA1 mismatch");

    const std::filesystem::path large = directory / "large.bin";
    const std::vector<uint8_t> largeData(3u * 1024u * 1024u + 1024u, static_cast<uint8_t>('A'));
    writeFile(large, largeData);
    error.clear();
    require(client.uploadFile(address, "UPLOAD_JOB", large, "remote/large", std::chrono::seconds(5), &error),
            "large uploadFile should succeed: " + error);
    uploaded = service.uploadSnapshot();
    require(uploaded.size == largeData.size(), "large upload size mismatch");
    require(uploaded.data.size() == largeData.size(), "large upload data length mismatch");
    require(uploaded.data == std::string(largeData.begin(), largeData.end()), "large upload payload mismatch");

    const std::filesystem::path cached = directory / "cached.txt";
    writeText(cached, "short");
    error.clear();
    require(client.uploadFile(address, "UPLOAD_JOB", cached, "cached", std::chrono::seconds(5), &error),
            "cache-hit uploadFile should succeed: " + error);
    uploaded = service.uploadSnapshot();
    require(uploaded.fromCache && uploaded.path == "cached", "cache-hit upload metadata mismatch");
    require(uploaded.data.empty(), "cache-hit upload should not send file data");

    const std::filesystem::path failed = directory / "failed.txt";
    writeText(failed, "fail");
    error.clear();
    require(!client.uploadFile(address, "UPLOAD_JOB", failed, "fail", std::chrono::seconds(5), &error),
            "server upload failure should fail");
    require(error.find("upload failure") != std::string::npos, "upload failure message missing: " + error);

    error.clear();
    require(!client.uploadFile(address, "UPLOAD_JOB", directory / "missing.txt", "remote/missing",
                               std::chrono::seconds(5), &error),
            "missing upload source should fail");
    require(error.find("cannot open upload source") != std::string::npos, "missing upload error missing: " + error);

    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
    server->Wait();
    std::printf("agent upload test OK: bytes=11 empty=1 cache=1 fail=1\n");
    return 0;
}
int agentCacheTest() {
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    {
        AgentClient invalidClient("http://127.0.0.1:1");
        std::string error;
        require(!invalidClient.batchUploadFileInServerCache("http://127.0.0.1:1", "JOB", {},
                                                            std::chrono::seconds(1), &error),
                "empty cache upload should be rejected");
    }

    FakeAgentService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    require(server != nullptr && port != 0, "failed to start fake Agent cache server");
    const std::string address = "http://127.0.0.1:" + std::to_string(port);

    {
    AgentClient cacheClient(address);
    std::string error;
    const std::vector<AgentCacheFile> goodFiles{{"ok0", "scene.scenegz"}, {"ok1", "jobs.jobgz"}};
    error.clear();
    require(cacheClient.batchUploadFileInServerCache(address, "CACHE_JOB", goodFiles, std::chrono::seconds(5),
                                                      &error),
            "cache upload should succeed: " + error);

    const auto snapshot = service.snapshot();
    require(snapshot.first == "CACHE_JOB", "cache upload job id mismatch");
    require(snapshot.second.size() == goodFiles.size(), "cache upload file count mismatch");
    for (std::size_t i = 0; i < goodFiles.size(); ++i) {
        require(snapshot.second[i].hash == goodFiles[i].hash, "cache upload hash mismatch");
        require(snapshot.second[i].remotePath == goodFiles[i].remotePath, "cache upload path mismatch");
    }

    const std::vector<AgentCacheFile> missFiles{{"ok0", "scene.scenegz"}, {"miss", "jobs.jobgz"}};
    error.clear();
    require(!cacheClient.batchUploadFileInServerCache(address, "CACHE_JOB", missFiles, std::chrono::seconds(5),
                                                       &error),
            "cache miss should fail");
    require(error.find("jobs.jobgz") != std::string::npos && error.find("NotInCache") != std::string::npos,
            "cache miss error is not actionable: " + error);

    const std::vector<AgentCacheFile> failFiles{{"fail", "fail.scenegz"}};
    error.clear();
    require(!cacheClient.batchUploadFileInServerCache(address, "CACHE_JOB", failFiles, std::chrono::seconds(5),
                                                       &error),
            "per-file cache failure should fail");
    require(error.find("cache failure") != std::string::npos, "cache failure message missing: " + error);
    std::printf("agent cache test OK: files=%zu\n", goodFiles.size());
    }

    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
    server->Wait();
    return 0;
}

int agentMultiUploadTest() {
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    AgentClient client("http://127.0.0.1:1");
    std::string error;
    require(!client.uploadMultiFiles("http://127.0.0.1:1", "JOB", {}, std::chrono::seconds(1), &error),
            "empty multi upload should be rejected");

    FakeAgentService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    require(server != nullptr && port != 0, "failed to start fake Agent multi-upload server");
    const std::string address = "http://127.0.0.1:" + std::to_string(port);

    AgentClient uploadClient(address);
    const std::vector<AgentUploadFile> files{
        {"ok0", "scene.scenegz", {1u, 2u, 3u}},
        {"ok1", "jobs.jobgz", {}},
    };
    error.clear();
    require(uploadClient.uploadMultiFiles(address, "MULTI_JOB", files, std::chrono::seconds(5), &error),
            "multi upload should succeed: " + error);
    const auto request = service.multiSnapshot();
    require(request.first == "MULTI_JOB", "multi upload job id mismatch");
    require(request.second.size() == files.size(), "multi upload file count mismatch");
    for (std::size_t i = 0; i < files.size(); ++i) {
        require(request.second[i].hash == files[i].hash, "multi upload hash mismatch");
        require(request.second[i].remotePath == files[i].remotePath, "multi upload path mismatch");
        require(request.second[i].data == files[i].data, "multi upload payload mismatch");
    }

    const std::vector<AgentUploadFile> failFiles{{"fail", "bad.scenegz", {9u}}};
    error.clear();
    require(!uploadClient.uploadMultiFiles(address, "MULTI_JOB", failFiles, std::chrono::seconds(5), &error),
            "per-file multi upload failure should fail");
    require(error.find("bad.scenegz") != std::string::npos && error.find("multi upload failure") != std::string::npos,
            "multi upload failure is not actionable: " + error);

    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
    server->Wait();
    std::printf("agent multi upload test OK: files=%zu\n", files.size());
    return 0;
}

int agentTransferTest() {
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    {
        AgentClient invalidClient("http://127.0.0.1:1");
        std::string error;
        require(!invalidClient.uploadJobFilesToAnotherAgent("http://127.0.0.1:1", "JOB", "http://other:5011", {},
                                                            std::chrono::seconds(1), &error),
                "empty job transfer should be rejected");
    }

    FakeAgentService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    require(server != nullptr && port != 0, "failed to start fake Agent transfer server");
    const std::string address = "http://127.0.0.1:" + std::to_string(port);

    {
    AgentClient transferClient(address);
    std::string error;
    const std::vector<std::string> paths{"scene.scenegz", "jobs.jobgz"};
    error.clear();
    require(transferClient.uploadJobFilesToAnotherAgent(address, "TRANSFER_JOB", "http://other:5011", paths,
                                                         std::chrono::seconds(5), &error),
            "job transfer should succeed: " + error);
    const auto request = service.transferSnapshot();
    require(request.jobId == "TRANSFER_JOB", "transfer job id mismatch");
    require(request.anotherAgent == "http://other:5011", "transfer target agent mismatch");
    require(request.paths == paths, "transfer file paths mismatch");

    error.clear();
    require(!transferClient.uploadJobFilesToAnotherAgent(address, "TRANSFER_JOB", "http://other:5011", {"fail"},
                                                          std::chrono::seconds(5), &error),
            "failed job transfer should fail");
    require(error.find("transfer failure") != std::string::npos, "transfer failure message missing: " + error);
    std::printf("agent transfer test OK: files=%zu\n", paths.size());
    }

    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
    server->Wait();
    return 0;
}

int agentDownloadTest() {
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    FakeAgentService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    require(server != nullptr && port != 0, "failed to start fake Agent download server");
    const std::string address = "http://127.0.0.1:" + std::to_string(port);

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawn_agent_download_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    AgentClient client(address);
    std::string error;
    const std::filesystem::path target = directory / "result.lmgz";
    require(client.downloadFile(address, "DOWNLOAD_JOB", "remote/result.lmgz", target, std::chrono::seconds(5),
                                &error),
            "download should succeed: " + error);
    require(readTextFile(target) == "hello world", "downloaded payload mismatch");
    const auto request = service.downloadSnapshot();
    require(request.first == "DOWNLOAD_JOB", "download job id mismatch");
    require(request.second == "remote/result.lmgz", "download path mismatch");

    const std::filesystem::path missingTarget = directory / "missing.lmgz";
    error.clear();
    require(!client.downloadFile(address, "DOWNLOAD_JOB", "missing", missingTarget, std::chrono::seconds(5), &error),
            "missing download should fail");
    require(error.find("download file not found") != std::string::npos, "missing download error is not actionable: " + error);
    require(!std::filesystem::exists(missingTarget), "failed download left a target file");

    const std::filesystem::path failTarget = directory / "fail.lmgz";
    error.clear();
    require(!client.downloadFile(address, "DOWNLOAD_JOB", "fail", failTarget, std::chrono::seconds(5), &error),
            "failed download should fail");
    require(error.find("download failed") != std::string::npos, "download failure message missing: " + error);
    require(!std::filesystem::exists(failTarget), "failed download left a target file");

    const std::filesystem::path emptyTarget = directory / "empty.lmgz";
    error.clear();
    require(client.downloadFile(address, "DOWNLOAD_JOB", "empty", emptyTarget, std::chrono::seconds(5), &error),
            "empty download should succeed: " + error);
    require(std::filesystem::is_regular_file(emptyTarget) && std::filesystem::file_size(emptyTarget) == 0u,
            "empty download payload mismatch");

    const std::filesystem::path shortTarget = directory / "short.lmgz";
    error.clear();
    require(client.downloadFile(address, "DOWNLOAD_JOB", "short", shortTarget, std::chrono::seconds(5), &error),
            "short download should succeed: " + error);
    require(readTextFile(shortTarget) == "short", "DataDone payload was not written");

    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
    server->Wait();
    std::printf("agent download test OK: bytes=11\n");
    return 0;
}

int agentMultiDownloadTest() {
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    FakeAgentService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    require(server != nullptr && port != 0, "failed to start fake Agent multi-download server");
    const std::string address = "http://127.0.0.1:" + std::to_string(port);

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawn_agent_multi_download_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    AgentClient client(address);
    const std::vector<AgentDownloadFile> downloads = {
        {"alpha", directory / "alpha.lmgz"},
        {"empty", directory / "empty.lmgz"},
        {"short", directory / "short.lmgz"},
    };
    std::string error;
    require(client.downloadMultiFiles(address, "MULTI_DOWNLOAD_JOB", downloads, std::chrono::seconds(5), &error),
            "multi download should succeed: " + error);
    require(readTextFile(downloads[0].localPath) == "hello world", "multi download payload mismatch");
    require(std::filesystem::is_regular_file(downloads[1].localPath) &&
                std::filesystem::file_size(downloads[1].localPath) == 0u,
            "multi download empty payload mismatch");
    require(readTextFile(downloads[2].localPath) == "short", "multi download DataDone payload was not written");

    const auto request = service.multiDownloadSnapshot();
    require(request.first == "MULTI_DOWNLOAD_JOB", "multi download job id mismatch");
    require(request.second == std::vector<std::string>{"alpha", "empty", "short"},
            "multi download file paths mismatch");

    const std::vector<AgentDownloadFile> missing = {
        {"missing", directory / "missing.lmgz"},
    };
    error.clear();
    require(!client.downloadMultiFiles(address, "MULTI_DOWNLOAD_JOB", missing, std::chrono::seconds(5), &error),
            "missing multi download should fail");
    require(error.find("multi download file not found") != std::string::npos,
            "multi download missing error is not actionable: " + error);
    require(!std::filesystem::exists(missing[0].localPath), "failed multi download left a target file");

    const std::vector<AgentDownloadFile> failed = {
        {"fail", directory / "fail.lmgz"},
    };
    error.clear();
    require(!client.downloadMultiFiles(address, "MULTI_DOWNLOAD_JOB", failed, std::chrono::seconds(5), &error),
            "failed multi download should fail");
    require(error.find("multi download failed") != std::string::npos,
            "multi download failure message missing: " + error);

    const std::vector<AgentDownloadFile> streamError = {
        {"errmsg", directory / "errmsg.lmgz"},
    };
    error.clear();
    require(!client.downloadMultiFiles(address, "MULTI_DOWNLOAD_JOB", streamError, std::chrono::seconds(5), &error),
            "multi download errMsg should fail");
    require(error.find("multi download stream failure") != std::string::npos,
            "multi download errMsg missing: " + error);

    const std::vector<AgentDownloadFile> badHash = {
        {"badhash", directory / "badhash.lmgz"},
    };
    error.clear();
    require(!client.downloadMultiFiles(address, "MULTI_DOWNLOAD_JOB", badHash, std::chrono::seconds(5), &error),
            "multi download hash mismatch should fail");
    require(error.find("fileHash mismatch") != std::string::npos,
            "multi download hash error missing: " + error);
    require(!std::filesystem::exists(badHash[0].localPath), "hash mismatch left a target file");

    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
    server->Wait();
    std::printf("agent multi-download test OK: files=%zu\n", downloads.size());
    return 0;
}

int agentDownloadFilesTest() {
    const auto require = [](bool condition, const std::string& message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    FakeAgentService service;
    grpc::ServerBuilder builder;
    int port = 0;
    builder.AddListeningPort("127.0.0.1:0", grpc::InsecureServerCredentials(), &port);
    builder.RegisterService(&service);
    std::unique_ptr<grpc::Server> server = builder.BuildAndStart();
    require(server != nullptr && port != 0, "failed to start fake Agent download-files server");
    const std::string address = "http://127.0.0.1:" + std::to_string(port);

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawn_agent_download_files_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    AgentClient client(address);
    const std::vector<AgentDownloadFile> downloads = {
        {"alpha", directory / "alpha.lmgz"},
        {"empty", directory / "empty.lmgz"},
        {"short", directory / "short.lmgz"},
    };
    std::string error;
    require(client.downloadFiles(address, "DOWNLOAD_FILES_JOB", downloads, std::chrono::seconds(5), &error),
            "batch download should succeed: " + error);
    require(readTextFile(downloads[0].localPath) == "hello world", "batch download payload mismatch");
    require(std::filesystem::is_regular_file(downloads[1].localPath) &&
                std::filesystem::file_size(downloads[1].localPath) == 0u,
            "batch download empty payload mismatch");
    require(readTextFile(downloads[2].localPath) == "short", "batch download DataDone payload was not written");
    const auto request = service.multiDownloadSnapshot();
    require(request.first == "DOWNLOAD_FILES_JOB", "batch download job id mismatch");
    require(request.second == std::vector<std::string>{"alpha", "empty", "short"},
            "batch download file paths mismatch");

    const std::vector<AgentDownloadFile> fallback = {
        {"short", directory / "fallback_short.lmgz"},
        {"empty", directory / "fallback_empty.lmgz"},
    };
    error.clear();
    require(client.downloadFiles(address, "MULTI_UNSUPPORTED_JOB", fallback, std::chrono::seconds(5), &error),
            "single-file fallback should succeed: " + error);
    require(readTextFile(fallback[0].localPath) == "short", "fallback single download payload mismatch");
    require(std::filesystem::is_regular_file(fallback[1].localPath) &&
                std::filesystem::file_size(fallback[1].localPath) == 0u,
            "fallback single download empty payload mismatch");

    server->Shutdown(std::chrono::system_clock::now() + std::chrono::seconds(1));
    server->Wait();
    std::printf("agent download files test OK: multi=%zu fallback=%zu\n", downloads.size(), fallback.size());
    return 0;
}
#endif

int goldenStructTest(const std::filesystem::path& dir) {
    const nlohmann::json manifest = nlohmann::json::parse(readTextFile(dir / "expected.json"));
    const std::string sceneName = manifest.at("files").at("scene").at("name").get<std::string>();
    const std::string jobsName = manifest.at("files").at("jobs").at("name").get<std::string>();
    const uint64_t expectedSceneBytes = manifest.at("files").at("scene").at("bytes").get<uint64_t>();
    const uint64_t expectedJobsBytes = manifest.at("files").at("jobs").at("bytes").get<uint64_t>();

    const std::vector<uint8_t> sceneBytes = readFile(dir / sceneName);
    const std::vector<uint8_t> jobsBytes = readFile(dir / jobsName);

    GoldenChecks checks;
    checks.require(sceneName == manifest.at("sceneGuid").get<std::string>() + ".scene",
                   "scene file name does not match sceneGuid");
    checks.u64("file.scene.bytes", sceneBytes.size(), expectedSceneBytes);
    checks.u64("file.jobs.bytes", jobsBytes.size(), expectedJobsBytes);

    Reader sceneReader(sceneBytes);
    const int32_t sceneVersion = sceneReader.version();
    checks.i32("scene.version", sceneVersion, kExportVersion);
    SceneInfo scene;
    readScene(sceneReader, scene, sceneVersion);
    const std::size_t sceneRemaining = sceneReader.remaining();
    checks.u64("scene.remaining", sceneRemaining, 0u);

    checks.u32("scene.numLights", scene.header.numLights, 1u);
    checks.u32("scene.numMeshes", scene.header.numMeshes, 1u);
    checks.u32("scene.numMaterials", scene.header.numMaterials, 1u);
    checks.u32("scene.numLandscapes", scene.header.numLandscapes, 0u);
    checks.u32("scene.numLightBvhLights", scene.header.numLightBvhLights, 0u);
    checks.u32("scene.numMeshInstances", scene.header.numMeshInstances, 1u);
    checks.u32("scene.numMeshGuids", scene.header.numMeshGuids, 1u);
    checks.f3("scene.bounds.origin", scene.header.bounds.origin, Float3{0.0f, 0.0f, 0.0f});
    checks.f3("scene.bounds.center", scene.header.bounds.sceneCenterPosition, Float3{0.0f, 0.0f, 0.0f});
    checks.f3("scene.bounds.extent", scene.header.bounds.boxExtent, Float3{1.0f, 0.01f, 1.0f});
    checks.f32("scene.bounds.radius", scene.header.bounds.sphereRadius, 1.5f);
    checks.f3("scene.importance.origin", scene.header.importanceBounds.origin, Float3{0.0f, 0.0f, 0.0f});
    checks.f3("scene.importance.center", scene.header.importanceBounds.sceneCenterPosition, Float3{0.0f, 0.0f, 0.0f});
    checks.f3("scene.importance.extent", scene.header.importanceBounds.boxExtent, Float3{1.0f, 0.01f, 1.0f});
    checks.f32("scene.importance.radius", scene.header.importanceBounds.sphereRadius, 1.5f);
    checks.require(scene.importanceVolumes.empty(), "scene.importanceVolumes is not empty");
    checks.require(scene.densityVolumes.empty(), "scene.densityVolumes is not empty");
    checks.require(scene.detailVolumes.empty(), "scene.detailVolumes is not empty");

    checks.require(scene.meshes.size() == 1u, "scene.meshes.size");
    const MeshInfo& mesh = scene.meshes.front();
    const auto& meshExpected = manifest.at("scene").at("mesh");
    checks.guid("scene.mesh.guid", mesh.header.guid, jsonGuid(meshExpected.at("guid"), "scene.mesh.guid"));
    checks.u32("scene.mesh.lodIndex", mesh.header.lodIndex, 0u);
    checks.u32("scene.mesh.triangles", mesh.header.numTriangles, meshExpected.at("triangles").get<uint32_t>());
    checks.u32("scene.mesh.vertices", mesh.header.numVertices, meshExpected.at("vertices").get<uint32_t>());
    checks.u32("scene.mesh.elements", mesh.header.numElements, meshExpected.at("elements").get<uint32_t>());
    checks.i32("scene.mesh.lightmapUVIndex", mesh.header.lightmapUVIndex,
               meshExpected.at("lightmapUVIndex").get<int32_t>());
    checks.f32("scene.mesh.lightmapUVDensity", mesh.header.lightmapUVDensity, 1.0f);
    checks.f32("scene.mesh.lightmapTexelDensity", mesh.header.lightmapTexelDensity, 1.0f);
    checks.u32("scene.mesh.indexInSceneInfo", mesh.header.indexInSceneInfo, 0u);
    const std::string indexKind = meshExpected.at("index").get<std::string>();
    checks.require(indexKind == "uint16", "scene.mesh.index expected uint16");
    checks.require(mesh.index16 == std::vector<uint16_t>({0, 1, 2, 0, 2, 3}), "scene.mesh.index16 mismatch");
    checks.require(mesh.index32.empty(), "scene.mesh.index32 is not empty");
    checks.require(mesh.elements.size() == 1u, "scene.mesh.elements.size");
    checks.u32("scene.mesh.element.startIndex", mesh.elements.front().startIndex, 0u);
    checks.u32("scene.mesh.element.primitiveCount", mesh.elements.front().primitiveCount, 2u);
    checks.u32("scene.mesh.element.materialIndex", mesh.elements.front().materialIndex, 0u);
    checks.u32("scene.mesh.element.flags", mesh.elements.front().flags, 0u);

    checks.require(mesh.vertices.size() == 4u, "scene.mesh.vertices.size");
    checks.require(mesh.normals.size() == 4u, "scene.mesh.normals.size");
    checks.require(mesh.tangents.size() == 4u, "scene.mesh.tangents.size");
    checks.require(mesh.bitangents.size() == 4u, "scene.mesh.bitangents.size");
    const Float4 positions[4] = {
        {-1.0f, 0.0f, -1.0f, 1.0f},
        {-1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, -1.0f, 1.0f},
    };
    const Float4 normal{0.0f, 1.0f, 0.0f, 0.0f};
    const Float4 tangent{0.0f, 0.0f, 1.0f, 0.0f};
    const Float4 bitangent{1.0f, 0.0f, 0.0f, 0.0f};
    for (std::size_t i = 0; i < 4u; ++i) {
        checks.f4("scene.mesh.position", mesh.vertices[i], positions[i]);
        checks.f4("scene.mesh.normal", mesh.normals[i], normal);
        checks.f4("scene.mesh.tangent", mesh.tangents[i], tangent);
        checks.f4("scene.mesh.bitangent", mesh.bitangents[i], bitangent);
    }

    checks.require(mesh.texcoords.size() == 3u, "scene.mesh.texcoords.size");
    const std::vector<Float2> uv = {{0.0f, 0.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}};
    for (std::size_t buffer = 0; buffer < mesh.texcoords.size(); ++buffer) {
        checks.require(mesh.texcoords[buffer].size() == uv.size(), "scene.mesh.texcoord size");
        for (std::size_t i = 0; i < uv.size(); ++i) {
            checks.f2("scene.mesh.texcoord", mesh.texcoords[buffer][i], uv[i]);
        }
    }

    checks.require(scene.materials.size() == 1u, "scene.materials.size");
    const MaterialInfo& material = scene.materials.front();
    const auto& materialExpected = manifest.at("scene").at("material");
    checks.require(allZero(material.header.hash.hash, sizeof(material.header.hash.hash)),
                   "scene.material.hash is not zero");
    checks.i32("scene.material.shadingModelId", material.header.shadingModelId, 0);
    checks.u32("scene.material.blendMode", material.header.blendMode, 0u);
    checks.u32("scene.material.twoSided", material.header.bTwoSided, 0u);
    checks.u32("scene.material.flags", material.header.flags, 0u);
    checks.f32("scene.material.opacityMaskClipValue", material.header.opacityMaskClipValue, 0.5f);
    checks.u32("scene.material.indexInSceneInfo", material.header.indexInSceneInfo, 0u);
    checks.f32("scene.material.diffuseBoost", material.header.diffuseBoost, 1.0f);
    checks.f32("scene.material.emissiveBoost", material.header.emissiveBoost, 1.0f);
    const uint32_t materialSizeX = materialExpected.at("size").at(0).get<uint32_t>();
    const uint32_t materialSizeY = materialExpected.at("size").at(1).get<uint32_t>();
    checks.u32("scene.material.diffuse.sizeX", material.diffuseTexture.header.sizeX, materialSizeX);
    checks.u32("scene.material.diffuse.sizeY", material.diffuseTexture.header.sizeY, materialSizeY);
    checks.u32("scene.material.diffuse.wrapMode", material.diffuseTexture.header.wrapMode, 0u);
    checks.u32("scene.material.diffuse.format", material.diffuseTexture.header.format, 0u);
    checks.require(material.diffuseTexture.colors.size() == 1u, "scene.material.diffuse.colors.size");
    checks.color("scene.material.diffuse.color", material.diffuseTexture.colors.front(),
                 MaterialColor{255, 255, 255, 255});
    checks.u32("scene.material.emissive.sizeX", material.emissiveTexture.header.sizeX, materialSizeX);
    checks.u32("scene.material.emissive.sizeY", material.emissiveTexture.header.sizeY, materialSizeY);
    checks.require(material.emissiveTexture.colors.size() == 1u, "scene.material.emissive.colors.size");
    checks.color("scene.material.emissive.color", material.emissiveTexture.colors.front(), MaterialColor{0, 0, 0, 255});
    checks.u32("scene.material.normal.sizeX", material.normalTexture.header.sizeX, materialSizeX);
    checks.u32("scene.material.normal.sizeY", material.normalTexture.header.sizeY, materialSizeY);
    checks.require(material.normalTexture.colors.size() == 1u, "scene.material.normal.colors.size");
    checks.color("scene.material.normal.color", material.normalTexture.colors.front(),
                 MaterialColor{128, 128, 255, 255});
    checks.u32("scene.material.specular.sizeX", material.specularTexture.header.sizeX, materialSizeX);
    checks.u32("scene.material.specular.sizeY", material.specularTexture.header.sizeY, materialSizeY);
    checks.require(material.specularTexture.colors.size() == 1u, "scene.material.specular.colors.size");
    checks.color("scene.material.specular.color", material.specularTexture.colors.front(),
                 MaterialColor{0, 128, 255, 255});
    checks.u32("scene.material.transmission.sizeX", material.transmissionTexture.header.sizeX, 0u);
    checks.u32("scene.material.transmission.sizeY", material.transmissionTexture.header.sizeY, 0u);
    checks.require(material.transmissionTexture.colors.empty(), "scene.material.transmission is not empty");
    std::size_t populatedTextures = 0u;
    populatedTextures += material.diffuseTexture.colors.empty() ? 0u : 1u;
    populatedTextures += material.transmissionTexture.colors.empty() ? 0u : 1u;
    populatedTextures += material.emissiveTexture.colors.empty() ? 0u : 1u;
    populatedTextures += material.normalTexture.colors.empty() ? 0u : 1u;
    populatedTextures += material.specularTexture.colors.empty() ? 0u : 1u;
    checks.u32("scene.material.textures", static_cast<uint32_t>(populatedTextures),
               materialExpected.at("textures").get<uint32_t>());

    checks.require(scene.meshInstances.size() == 1u, "scene.meshInstances.size");
    const MeshInstanceInfo& instance = scene.meshInstances.front();
    const auto& instanceExpected = manifest.at("scene").at("instance");
    checks.guid("scene.instance.levelGuid", instance.header.levelGuid, Guid{0, 0, 0, 0});
    checks.guid("scene.instance.guid", instance.header.guid,
                jsonGuid(instanceExpected.at("guid"), "scene.instance.guid"));
    checks.identity("scene.instance.transform", instance.header.transform);
    checks.identity("scene.instance.inverseTransform", instance.header.inverseTransform);
    checks.identity("scene.instance.inverseTransposeTransform", instance.header.inverseTransposeTransform);
    checks.u32("scene.instance.lightmapId", instance.header.lightmapId,
               instanceExpected.at("lightmapId").get<uint32_t>());
    checks.f3("scene.instance.bounds.min", instance.header.boundingBox.min, Float3{-1.0f, 0.0f, -1.0f});
    checks.f3("scene.instance.bounds.max", instance.header.boundingBox.max, Float3{1.0f, 0.0f, 1.0f});
    checks.u32("scene.instance.meshIndex", instance.header.meshIndex, instanceExpected.at("meshIndex").get<uint32_t>());
    checks.u32("scene.instance.texcoordIndex", instance.header.texcoordIndex, 0u);
    checks.u32("scene.instance.flags", instance.header.flags, 0u);
    checks.f32("scene.instance.diffuseBoost", instance.header.diffuseBoost, 1.0f);
    checks.f32("scene.instance.emissiveBoost", instance.header.emissiveBoost, 1.0f);
    checks.f32("scene.instance.rayOffset", instance.header.rayOffset, 0.0f);
    checks.f32("scene.instance.fullyOccludedSamplesFraction", instance.header.fullyOccludedSamplesFraction, 1.0f);
    checks.f32("scene.instance.meshLightAttenuationRadius", instance.header.meshLightAttenuationRadius, 0.0f);
    checks.u32("scene.instance.visibilityMask", instance.header.meshInstanceVisibilityMask, 1u);
    checks.u32("scene.instance.visibilityMaskHlod", instance.header.meshInstanceVisibilityMaskInHlodScene, 1u);
    checks.i32("scene.instance.lodLevel", instance.header.lodLevelInHlodScene, 0);
    checks.u32("scene.instance.uv2MapSize.x", instance.header.uv2MapSize.x,
               instanceExpected.at("uv2MapSize").at(0).get<uint32_t>());
    checks.u32("scene.instance.uv2MapSize.y", instance.header.uv2MapSize.y,
               instanceExpected.at("uv2MapSize").at(1).get<uint32_t>());
    checks.u32("scene.instance.uv2Offset.x", instance.header.uv2Offset.x,
               instanceExpected.at("uv2Offset").at(0).get<uint32_t>());
    checks.u32("scene.instance.uv2Offset.y", instance.header.uv2Offset.y,
               instanceExpected.at("uv2Offset").at(1).get<uint32_t>());
    checks.require(instance.materialOverrides == std::vector<uint32_t>({0u}),
                   "scene.instance.materialOverrides mismatch");
    checks.require(instance.relevantLights.empty(), "scene.instance.relevantLights is not empty");
    checks.require(scene.meshGuids.size() == 1u, "scene.meshGuids.size");
    checks.guid("scene.meshGuids[0]", scene.meshGuids.front(), instance.header.guid);

    checks.require(scene.lights.size() == 1u, "scene.lights.size");
    const LightInfo& light = scene.lights.front();
    const auto& lightExpected = manifest.at("scene").at("light");
    checks.guid("scene.light.guid", scene.lightGuids.front(), jsonGuid(lightExpected.at("guid"), "scene.light.guid"));
    checks.u32("scene.light.type", light.flags & kLightFlagTypeMask, kLightFlagDirectional);
    checks.u32("scene.light.flags", light.flags, lightExpected.at("flags").get<uint32_t>());
    checks.f3("scene.light.position", light.position, Float3{0.0f, 10.0f, 0.0f});
    checks.f3("scene.light.normal", light.normal, Float3{0.5f, -1.0f, 0.5f});
    checks.f3("scene.light.dPdu", light.dPdu, Float3{1.0f, 0.0f, 0.0f});
    checks.f3("scene.light.dPdv", light.dPdv, Float3{0.0f, 0.0f, 1.0f});
    checks.f3("scene.light.color", light.color,
              Float3{lightExpected.at("color").at(0).get<float>(), lightExpected.at("color").at(1).get<float>(),
                     lightExpected.at("color").at(2).get<float>()});
    checks.f3("scene.light.dimensions", light.dimensions, Float3{0.0f, 0.0f, 0.0f});
    checks.f2("scene.light.shaping", light.shaping, Float2{0.0f, 0.0f});
    checks.f32("scene.light.attenuation", light.attenuation, 1.0f);
    checks.f32("scene.light.falloffExponent", light.falloffExponent, 0.0f);
    checks.i32("scene.light.iesTextureSlice", light.iesTextureSlice, -1);
    checks.f3("scene.light.indirectColor", light.indirectColor, Float3{3.0f, 3.0f, 3.0f});
    checks.f3("scene.light.boundMin", light.boundMin, Float3{-1000.0f, -1000.0f, -1000.0f});
    checks.f3("scene.light.boundMax", light.boundMax, Float3{1000.0f, 1000.0f, 1000.0f});
    checks.i32("scene.light.lightTextureMode", light.lightTextureMode, 0);
    checks.f32("scene.light.shadowTranslucency", light.shadowTranslucency, 0.0f);
    checks.u32("scene.light.bEnablePenumbra", light.bEnablePenumbra, 0u);
    checks.f4("scene.light.penumbraTint", light.penumbraTint, Float4{1.0f, 1.0f, 1.0f, 1.0f});
    checks.f32("scene.light.penumbraLuma", light.penumbraLuma, 1.0f);
    checks.require(allZero(light.padding, sizeof(light.padding)), "scene.light.padding is not zero");
    checks.require(scene.lightPowers.size() == 1u, "scene.lightPowers.size");
    checks.f32("scene.light.power", scene.lightPowers.front(), 1.0f);
    checks.require(scene.iesTextureDatas.empty(), "scene.iesTextureDatas is not empty");
    checks.require(scene.cookieTextureDatas.empty(), "scene.cookieTextureDatas is not empty");

    const int32_t skyCubeSize = manifest.at("scene").at("skyLight").at("cubeSize").get<int32_t>();
    checks.i32("scene.sky.cubeSize", scene.skyLight.skyData.mipDimensions.y, skyCubeSize);
    checks.i32("scene.sky.mip.x", scene.skyLight.skyData.mipDimensions.x, 1);
    checks.i32("scene.sky.mip.z", scene.skyLight.skyData.mipDimensions.z, 1);
    checks.f3("scene.sky.color", scene.skyLight.skyData.color, Float3{0.05f, 0.05f, 0.08f});
    checks.f3("scene.sky.envColor", scene.skyLight.skyData.envColor, Float3{0.05f, 0.05f, 0.08f});
    checks.identity("scene.sky.transform", scene.skyLight.skyData.transform);
    checks.f32("scene.sky.indirectIntensity", scene.skyLight.skyData.indirectLightingIntensity, 1.0f);
    checks.f32("scene.sky.shadowTranslucency", scene.skyLight.skyData.shadowTranslucency, 0.0f);
    checks.u32("scene.sky.flags", scene.skyLight.skyData.flags, 0u);
    checks.require(scene.skyLight.envCube.size() == 1u, "scene.sky.envCube.size");
    checks.f4("scene.sky.envCube[0]", scene.skyLight.envCube.front(), Float4{0.05f, 0.05f, 0.08f, 1.0f});

    checks.require(scene.landscapes.empty(), "scene.landscapes is not empty");
    checks.require(scene.scenePartitions.empty(), "scene.scenePartitions is not empty");
    checks.require(scene.sceneNoPartitionInfo.meshIndices.empty(), "scene.noPartition.meshIndices");
    checks.require(scene.sceneNoPartitionInfo.meshGuid.empty(), "scene.noPartition.meshGuid");
    checks.require(scene.sceneNoPartitionInfo.meshLodIndex.empty(), "scene.noPartition.meshLodIndex");
    checks.require(scene.sceneNoPartitionInfo.materialIndices.empty(), "scene.noPartition.materialIndices");
    checks.require(scene.sceneNoPartitionInfo.materialHash.empty(), "scene.noPartition.materialHash");
    checks.require(scene.sceneNoPartitionInfo.landscapeIndices.empty(), "scene.noPartition.landscapeIndices");
    checks.require(scene.sceneNoPartitionInfo.landscapeGuid.empty(), "scene.noPartition.landscapeGuid");
    checks.require(scene.sceneNoPartitionInfo.lightIndices.empty(), "scene.noPartition.lightIndices");
    checks.require(scene.sceneNoPartitionInfo.lights.empty(), "scene.noPartition.lights");
    checks.require(scene.sceneNoPartitionInfo.meshInstanceIndices.empty(), "scene.noPartition.meshInstanceIndices");
    checks.require(scene.sceneNoPartitionInfo.meshInstances.empty(), "scene.noPartition.meshInstances");

    Reader jobsReader(jobsBytes);
    const int32_t jobsVersion = jobsReader.version();
    checks.i32("jobs.version", jobsVersion, kExportVersion);
    BakingJobInputs jobs;
    readBakingJobInputs(jobsReader, jobs, jobsVersion);
    const std::size_t jobsRemaining = jobsReader.remaining();
    checks.u64("jobs.remaining", jobsRemaining, 0u);

    const auto& jobsExpected = manifest.at("jobs");
    checks.u32("jobs.lightmap2D", static_cast<uint32_t>(jobs.lightmap2DJobs.size()),
               jobsExpected.at("lightmap2D").get<uint32_t>());
    checks.require(jobs.lightmap2DJobs.size() == 1u, "jobs.lightmap2DJobs.size");
    const Lightmap2DInput& input = jobs.lightmap2DJobs.front();
    checks.guid("jobs.jobID", input.jobId, jsonGuid(jobsExpected.at("jobID"), "jobs.jobID"));
    checks.guid("jobs.meshID", input.meshId, jsonGuid(jobsExpected.at("meshID"), "jobs.meshID"));
    checks.guid("jobs.meshID.instance", input.meshId, instance.header.guid);
    checks.u32("jobs.flags", input.flags, 0u);
    checks.u32("jobs.isHlodActorMapping", input.isHlodActorMapping, 0u);
    checks.u32("jobs.scenePartitionIndex", input.scenePartitionIndex, 0u);
    checks.i32("jobs.padding", input.padding, 0);
    checks.i32("jobs.size.x", input.size.x, jobsExpected.at("size").at(0).get<int32_t>());
    checks.i32("jobs.size.y", input.size.y, jobsExpected.at("size").at(1).get<int32_t>());
    checks.u32("jobs.size.instance.x", static_cast<uint32_t>(input.size.x), instance.header.uv2MapSize.x);
    checks.u32("jobs.size.instance.y", static_cast<uint32_t>(input.size.y), instance.header.uv2MapSize.y);
    checks.u32("jobs.lightmapUVIndex", input.lightmapUVIndex, jobsExpected.at("lightmapUVIndex").get<uint32_t>());
    checks.u32("jobs.lightmapUVIndex.mesh", input.lightmapUVIndex, static_cast<uint32_t>(mesh.header.lightmapUVIndex));
    constexpr std::size_t overrideSize = sizeof(Lightmap2DOverrideBakingParams);
    bool overrideZero = true;
    std::size_t overrideDiff = 0;
    const uint8_t* overrideBytes = reinterpret_cast<const uint8_t*>(&input.overrideBakingParams);
    for (std::size_t i = 0; i < overrideSize; ++i) {
        if (overrideBytes[i] != 0u) {
            overrideZero = false;
            overrideDiff = i;
            break;
        }
    }
    checks.require(overrideZero, "jobs.overrideBakingParams is not zero at byte " + std::to_string(overrideDiff));

    checks.u32("jobs.baking.useFastVoxelization", jobs.bakingParameters.bUseFastVoxelization, 0u);
    checks.i32("jobs.baking.lightmap.denoiserMode", jobs.bakingParameters.lightmap2DParameter.denoiserMode, 0);
    checks.i32("jobs.baking.lightmap.iterationNum", jobs.bakingParameters.lightmap2DParameter.iterationNum, 64);
    checks.i32("jobs.baking.lightmap.maxDepth", jobs.bakingParameters.lightmap2DParameter.maxDepth, 3);
    checks.i32("jobs.baking.lightmap.superSampleFactor", jobs.bakingParameters.lightmap2DParameter.superSampleFactor,
               1);
    checks.f32("jobs.baking.lightmap.penumbraShadowFraction",
               jobs.bakingParameters.lightmap2DParameter.penumbraShadowFraction, 0.3f);
    checks.f32("jobs.baking.lightmap.skyOcclusionCompensateScale",
               jobs.bakingParameters.lightmap2DParameter.skyOcclusionCompensateScale, 1.0f);
    checks.i32("jobs.baking.lightmap.maxLightmapResolution",
               jobs.bakingParameters.lightmap2DParameter.maxLightmapResolution, 64);
    checks.u32("jobs.baking.lightmap.bGpuRaster", jobs.bakingParameters.lightmap2DParameter.bGpuRaster, 0u);
    checks.u32("jobs.baking.lightmap.bUseLightmapDirection",
               jobs.bakingParameters.lightmap2DParameter.bUseLightmapDirection, 1u);
    checks.u32("jobs.baking.lightmap.bSkyOcclusionDenoise",
               jobs.bakingParameters.lightmap2DParameter.bSkyOcclusionDenoise, 0u);
    checks.i32("jobs.baking.lightmap.optixSmoothLevel", jobs.bakingParameters.lightmap2DParameter.optixSmoothLevel, 0);
    checks.f32("jobs.baking.ray.minNormalOffset", jobs.bakingParameters.rayTracingParameter.minNormalOffset, 0.5f);
    checks.f32("jobs.baking.ray.maxRayOffset", jobs.bakingParameters.rayTracingParameter.maxRayOffset, 10.0f);
    checks.f32("jobs.baking.ray.minRayOffset", jobs.bakingParameters.rayTracingParameter.minRayOffset, 0.0001f);
    checks.f32("jobs.baking.ray.normalOffsetSampleRadiusScale",
               jobs.bakingParameters.rayTracingParameter.normalOffsetSampleRadiusScale, 0.005f);
    checks.f32("jobs.baking.ray.tangentOffsetSampleRadiusScale",
               jobs.bakingParameters.rayTracingParameter.tangentOffsetSampleRadiusScale, 0.008f);
    checks.f32("jobs.baking.raster.bias", jobs.bakingParameters.rasterizationParameter.rasterizationBias, 1.0f);
    checks.f32("jobs.baking.raster.smallestTexelRadius",
               jobs.bakingParameters.rasterizationParameter.smallestTexelRadius, 0.1f);
    checks.u32("jobs.baking.raster.conservative",
               jobs.bakingParameters.rasterizationParameter.bUseConservativeTexelRasterization, 0u);
    checks.u32("jobs.baking.raster.maxWeight", jobs.bakingParameters.rasterizationParameter.bUseMaxWeight, 0u);
    checks.i32("jobs.baking.artifact.seamFixed", jobs.bakingParameters.artifactParameter.bSeamFixed, 0);
    checks.u32("jobs.baking.artifact.fillUnmapped", jobs.bakingParameters.artifactParameter.bFillUnmappedTexel, 1u);
    checks.u32("jobs.baking.bvh.directLightSampleMode", jobs.bakingParameters.lightBvhParameter.directLightSampleMode,
               2u);
    checks.f32("jobs.baking.bvh.maxPathIntensity", jobs.bakingParameters.lightBvhParameter.maxPathIntensity, 50.0f);
    checks.u32("jobs.baking.bvh.lightGridResolution", jobs.bakingParameters.lightBvhParameter.lightGridResolution,
               256u);
    checks.u32("jobs.baking.bvh.maxLightCountPerGrid", jobs.bakingParameters.lightBvhParameter.maxLightCountPerGrid,
               256u);
    checks.f32("jobs.baking.bvh.virtualLightDirectIntensity",
               jobs.bakingParameters.lightBvhParameter.virtualLightDirectIntensity, 1.0f);
    checks.f32("jobs.baking.bvh.virtualLightIndirectIntensity",
               jobs.bakingParameters.lightBvhParameter.virtualLightIndirectIntensity, 1.0f);
    checks.u32("jobs.baking.bvh.virtualLightSampleMode", jobs.bakingParameters.lightBvhParameter.virtualLightSampleMode,
               1u);
    checks.u32("jobs.baking.prtHQ", jobs.bakingParameters.bPrtHqEnabled, 0u);
    checks.u32("jobs.baking.prtLQ", jobs.bakingParameters.bPrtLqEnabled, 0u);
    checks.u32("jobs.debug.dawnLogLevel", jobs.debugInput.debugInput100.dawnLogLevel, 0u);
    checks.require(jobs.precomputedVisibilityParameter.cameraTrackPositions.empty(),
                   "jobs.precomputedVisibility.cameraTrackPositions is not empty");
    checks.require(jobs.precomputedVisibilityParameter.header.numCellDistributionBuckets == 0,
                   "jobs.precomputedVisibility.header is not zero");

    const std::pair<const char*, uint32_t> jobZeros[] = {
        {"jobs.sdfShadowJobsCount", jobs.sdfShadowJobsCount},
        {"jobs.sdfShadowGroupJobsCount", jobs.sdfShadowGroupJobsCount},
        {"jobs.shadowDepthMapJobsCount", jobs.shadowDepthMapJobsCount},
        {"jobs.customLightProbeJobsCount", jobs.customLightProbeJobsCount},
        {"jobs.importanceVolumeLightProbeJobsCount", jobs.importanceVolumeLightProbeJobsCount},
        {"jobs.detailVolumeLightProbeJobsCount", jobs.detailVolumeLightProbeJobsCount},
        {"jobs.visibilityBucketGuidsCount", jobs.visibilityBucketGuidsCount},
        {"jobs.volumetricLightmapTaskGuidsCount", jobs.volumetricLightmapTaskGuidsCount},
        {"jobs.customVolumetricLightmapJobsCount", jobs.customVolumetricLightmapJobsCount},
        {"jobs.mappingGroupJobsCount", jobs.mappingGroupJobsCount},
        {"jobs.mappingSplitJobsCount", jobs.mappingSplitJobsCount},
        {"jobs.seamFixGroupJobsCount", jobs.seamFixGroupJobsCount},
        {"jobs.scenePartitionTasksCount", jobs.scenePartitionTasksCount},
        {"jobs.lightmapOcclusionJobsCount", jobs.lightmapOcclusionJobsCount},
        {"jobs.occlusionGroupJobsCount", jobs.occlusionGroupJobsCount},
        {"jobs.textureMappingJobsCount", jobs.textureMappingJobsCount},
    };
    for (const auto& [name, value] : jobZeros) {
        checks.u32(name, value, 0u);
    }
    checks.u32("jobs.partial.lightmap2D", jobs.partialJobsTotalNum.numLightmap2DJobs, 0u);
    checks.u32("jobs.partial.sdfShadow", jobs.partialJobsTotalNum.numSdfShadowJobs, 0u);
    checks.u32("jobs.partial.textureMapping", jobs.partialJobsTotalNum.numTextureMappingJobs, 0u);
    checks.u32("jobs.partial.lightmapOcclusion", jobs.partialJobsTotalNum.numLightmapOcclusionJobs, 0u);
    checks.u32("jobs.partial.detailVolumeProbe", jobs.partialJobsTotalNum.numDetailVolumeLightProbeJobs, 0u);
    checks.u32("jobs.partial.importanceVolumeProbe", jobs.partialJobsTotalNum.numImportanceVolumeLightProbeJobs, 0u);
    checks.u32("jobs.partial.customProbe", jobs.partialJobsTotalNum.numCustomLightProbeJobs, 0u);
    checks.u32("jobs.partial.volumetricTask", jobs.partialJobsTotalNum.numVolumetricLightmapTaskGuids, 0u);
    checks.u32("jobs.partial.customVolumetric", jobs.partialJobsTotalNum.numCustomVolumetricLightmapJobs, 0u);
    checks.u32("jobs.partial.mappingGroup", jobs.partialJobsTotalNum.numMappingGroupJobs, 0u);
    checks.u32("jobs.partial.mappingSplit", jobs.partialJobsTotalNum.numMappingSplitJobs, 0u);
    checks.u32("jobs.partial.sdfShadowGroup", jobs.partialJobsTotalNum.numSdfShadowGroupJobs, 0u);
    checks.u32("jobs.partial.lightmapOcclusionGroup", jobs.partialJobsTotalNum.numLightmapOcclusionGroupJobs, 0u);

    const std::pair<const char*, std::size_t> sizeChecks[] = {
        {"FGuidInfo", sizeof(Guid)},
        {"FSHAHashInfo", sizeof(ShaHash)},
        {"FLightProfileInfo", sizeof(LightProfile)},
        {"FLightInfo", sizeof(LightInfo)},
        {"FLightFullInfo", sizeof(LightFullInfo)},
        {"FSkyLightInfoHeader", sizeof(SkyLightInfoHeader)},
        {"FTexture2DInfoHeader", sizeof(Texture2DInfoHeader)},
        {"FMaterialColor", sizeof(MaterialColor)},
        {"FMaterialInfoHeader", sizeof(MaterialInfoHeader)},
        {"FSceneBounds", sizeof(SceneBounds)},
        {"FSceneBoxBounds", sizeof(SceneBoxBounds)},
        {"FMeshElementInfo", sizeof(MeshElementInfo)},
        {"FMeshInfoHeader", sizeof(MeshInfoHeader)},
        {"FMeshInstanceInfoHeader", sizeof(MeshInstanceInfoHeader)},
        {"FLandscapeInfoHeader", sizeof(LandscapeInfoHeader)},
        {"FSceneInfoHeader", sizeof(SceneInfoHeader)},
        {"FLightmap2DInput", sizeof(Lightmap2DInput)},
        {"FLightmap2DSample", sizeof(Lightmap2DSample)},
        {"FLightmap2DOutputHeader", sizeof(Lightmap2DOutputHeader)},
        {"Matrix4x4", sizeof(Matrix4x4)},
        {"float2", sizeof(Float2)},
        {"float3", sizeof(Float3)},
        {"float4", sizeof(Float4)},
    };
    for (const auto& [name, actual] : sizeChecks) {
        checks.u64(name, static_cast<uint64_t>(actual), manifest.at("sizes").at(name).get<uint64_t>());
    }

    std::printf("golden scene: version=%d bytes=%zu remaining=%zu\n", sceneVersion, sceneBytes.size(), sceneRemaining);
    std::printf("golden jobs: version=%d bytes=%zu remaining=%zu\n", jobsVersion, jobsBytes.size(), jobsRemaining);
    std::printf("golden structure test OK (%zu fields)\n", checks.fields);
    return 0;
}

void writeObject(const std::filesystem::path& path, const SceneInfo& scene) {
    Writer writer;
    writer.version(kExportVersion);
    writeScene(writer, scene, kExportVersion);
    writeFile(path, writer.bytes());
    std::cout << path.filename().string() << "  " << writer.size() << " bytes\n";
}

void writeObject(const std::filesystem::path& path, const BakingJobInputs& jobs) {
    Writer writer;
    writer.version(kExportVersion);
    writeBakingJobInputs(writer, jobs, kExportVersion);
    writeFile(path, writer.bytes());
    std::cout << path.filename().string() << "  " << writer.size() << " bytes\n";
}

int protoTest() {
    const SizeCheck checks[] = {
        {"Float2", sizeof(Float2), 8},
        {"Float3", sizeof(Float3), 12},
        {"Float4", sizeof(Float4), 16},
        {"Guid", sizeof(Guid), 16},
        {"ShaHash", sizeof(ShaHash), 20},
        {"LightProfile", sizeof(LightProfile), 65536},
        {"SceneBounds", sizeof(SceneBounds), 40},
        {"SceneBoxBounds", sizeof(SceneBoxBounds), 24},
        {"LightInfo", sizeof(LightInfo), 176},
        {"SkyLightInfoHeader", sizeof(SkyLightInfoHeader), 112},
        {"MaterialInfoHeader", sizeof(MaterialInfoHeader), 52},
        {"MeshInfoHeader", sizeof(MeshInfoHeader), 48},
        {"MeshInstanceInfoHeader", sizeof(MeshInstanceInfoHeader), 312},
        {"LandscapeInfoHeader", sizeof(LandscapeInfoHeader), 172},
        {"SceneInfoHeader", sizeof(SceneInfoHeader), 108},
        {"Lightmap2DInput", sizeof(Lightmap2DInput), 160},
        {"Lightmap2DSample", sizeof(Lightmap2DSample), 192},
        {"Lightmap2DOutputHeader", sizeof(Lightmap2DOutputHeader), 48},
        {"BakingJobParameters", sizeof(BakingJobParameters), 232},
        {"Lightmap2DOverrideBakingParams", sizeof(Lightmap2DOverrideBakingParams), 100},
        {"PartialJobsTotalNum", sizeof(PartialJobsTotalNum), 52},
    };

    bool ok = true;
    for (const SizeCheck& check : checks) {
        const bool pass = check.actual == check.expected;
        std::printf("%-34s %6zu  expected %6zu  %s\n", check.name, check.actual, check.expected,
                    pass ? "OK" : "MISMATCH");
        ok = ok && pass;
    }
    std::printf("version=%d\n", kExportVersion);
    return ok ? 0 : 1;
}

int writeEmpty(const std::filesystem::path& dir, const std::string& guid) {
    if (!isHexGuid(guid)) {
        std::cerr << "guid must be exactly 32 hex characters\n";
        return 2;
    }
    std::filesystem::create_directories(dir);
    writeObject(dir / (guid + ".scene"), SceneInfo{});
    writeObject(dir / (guid + ".jobs"), BakingJobInputs{});
    return 0;
}

int writeMinimal(const std::filesystem::path& dir, const std::string& guid) {
    if (!isHexGuid(guid)) {
        std::cerr << "guid must be exactly 32 hex characters\n";
        return 2;
    }
    std::filesystem::create_directories(dir);
    writeObject(dir / (guid + ".scene"), buildMinimalScene());
    writeObject(dir / (guid + ".jobs"), buildMinimalJobs());
    return 0;
}

void printFirstDiff(const std::vector<uint8_t>& expected, const std::vector<uint8_t>& actual) {
    const size_t count = std::min(expected.size(), actual.size());
    for (size_t i = 0; i < count; ++i) {
        if (expected[i] != actual[i]) {
            std::printf("first diff at %zu: expected 0x%02X, actual 0x%02X\n", i, expected[i], actual[i]);
            return;
        }
    }
    if (expected.size() != actual.size()) {
        std::printf("size differs: expected %zu, actual %zu\n", expected.size(), actual.size());
    }
}

int roundtrip(const std::filesystem::path& scenePath, const std::filesystem::path& jobsPath) {
    const std::vector<uint8_t> sceneFileBytes = readFile(scenePath);
    const std::vector<uint8_t> jobsFileBytes = readFile(jobsPath);
    const bool sceneGzip = sceneFileBytes.size() >= 2 && sceneFileBytes[0] == 0x1F && sceneFileBytes[1] == 0x8B;
    const bool jobsGzip = jobsFileBytes.size() >= 2 && jobsFileBytes[0] == 0x1F && jobsFileBytes[1] == 0x8B;
    const std::vector<uint8_t> sceneBytes = sceneGzip ? gzipDecompress(sceneFileBytes) : sceneFileBytes;
    const std::vector<uint8_t> jobsBytes = jobsGzip ? gzipDecompress(jobsFileBytes) : jobsFileBytes;

    Reader sceneReader(sceneBytes);
    const int32_t sceneVersion = sceneReader.version();
    SceneInfo scene;
    readScene(sceneReader, scene, sceneVersion);

    Writer sceneWriter;
    sceneWriter.version(sceneVersion);
    writeScene(sceneWriter, scene, sceneVersion);

    Reader jobsReader(jobsBytes);
    const int32_t jobsVersion = jobsReader.version();
    BakingJobInputs jobs;
    readBakingJobInputs(jobsReader, jobs, jobsVersion);

    Writer jobsWriter;
    jobsWriter.version(jobsVersion);
    writeBakingJobInputs(jobsWriter, jobs, jobsVersion);

    const bool scenePayloadOk = sceneWriter.bytes() == sceneBytes;
    const bool jobsPayloadOk = jobsWriter.bytes() == jobsBytes;
    const bool sceneGzipStored = sceneGzip && gzipCompressStored(sceneBytes) == sceneFileBytes;
    const bool jobsGzipStored = jobsGzip && gzipCompressStored(jobsBytes) == jobsFileBytes;
    const bool sceneOk = scenePayloadOk;
    const bool jobsOk = jobsPayloadOk;
    std::printf("%s: version=%d bytes=%zu gzip=%s roundtrip=%s\n", scenePath.filename().string().c_str(), sceneVersion,
                sceneBytes.size(), sceneGzip ? (sceneGzipStored ? "stored" : "deflate") : "no", sceneOk ? "OK" : "DIFF");
    if (!scenePayloadOk) {
        printFirstDiff(sceneBytes, sceneWriter.bytes());
    }
    std::printf("%s: version=%d bytes=%zu gzip=%s roundtrip=%s\n", jobsPath.filename().string().c_str(), jobsVersion,
                jobsBytes.size(), jobsGzip ? (jobsGzipStored ? "stored" : "deflate") : "no", jobsOk ? "OK" : "DIFF");
    if (!jobsPayloadOk) {
        printFirstDiff(jobsBytes, jobsWriter.bytes());
    }
    return (sceneOk && jobsOk) ? 0 : 1;
}

int inspect(const std::filesystem::path& path) {
    const std::vector<uint8_t> fileBytes = readFile(path);
    const bool gzip = fileBytes.size() >= 2 && fileBytes[0] == 0x1F && fileBytes[1] == 0x8B;
    const std::vector<uint8_t> bytes = gzip ? gzipDecompress(fileBytes) : fileBytes;
    Reader reader(bytes);
    const int32_t version = reader.version();
    std::printf("%s: version=%d bytes=%zu gzip=%s\n", path.filename().string().c_str(), version, bytes.size(),
                gzip ? "yes" : "no");
    const std::string extension = path.extension().string();
    if (extension == ".scene" || extension == ".scenegz") {
        SceneInfo scene;
        readScene(reader, scene, version);
        std::printf("  lights=%u meshes=%u materials=%u instances=%u lightmapInstances=%u\n", scene.header.numLights,
                    scene.header.numMeshes, scene.header.numMaterials, scene.header.numMeshInstances,
                    static_cast<unsigned>(scene.meshInstances.size()));
        for (std::size_t index = 0; index < scene.lightGuids.size(); ++index) {
            std::printf("  light[%zu] guid=%s\n", index, formatGuid(scene.lightGuids[index]).c_str());
        }
        for (std::size_t index = 0; index < scene.meshes.size(); ++index) {
            const MeshInfo& mesh = scene.meshes[index];
            std::printf("  mesh[%zu] guid=%s indexInSceneInfo=%u uv=%d texcoords=%zu\n", index,
                        formatGuid(mesh.header.guid).c_str(), mesh.header.indexInSceneInfo,
                        mesh.header.lightmapUVIndex, mesh.texcoords.size());
        }
        for (std::size_t index = 0; index < scene.meshInstances.size(); ++index) {
            const MeshInstanceInfo& instance = scene.meshInstances[index];
            std::printf("  instance[%zu] guid=%s mesh=%u lightmapId=%u uv2Map=%u,%u uv2Offset=%u,%u\n", index,
                        formatGuid(instance.header.guid).c_str(), instance.header.meshIndex, instance.header.lightmapId,
                        instance.header.uv2MapSize.x, instance.header.uv2MapSize.y, instance.header.uv2Offset.x,
                        instance.header.uv2Offset.y);
        }
        std::printf("  consumed=%zu remaining=%zu\n", reader.offset(), reader.remaining());
    } else if (extension == ".mesh" || extension == ".meshgz") {
        MeshInfo mesh;
        readMesh(reader, mesh, version);
        std::printf("  triangles=%u vertices=%u indices16=%zu indices32=%zu elements=%zu texcoords=%zu\n",
                    mesh.header.numTriangles, mesh.header.numVertices, mesh.index16.size(), mesh.index32.size(),
                    mesh.elements.size(), mesh.texcoords.size());
        std::printf("  consumed=%zu remaining=%zu\n", reader.offset(), reader.remaining());
    } else if (extension == ".mat" || extension == ".matgz") {
        MaterialInfo material;
        readMaterial(reader, material, version);
        std::printf(
            "  blend=%u twoSided=%u diffuse=%ux%u fmt=%u transmission=%zu emissive=%zu normal=%zu specular=%zu\n",
            material.header.blendMode, material.header.bTwoSided, material.diffuseTexture.header.sizeX,
            material.diffuseTexture.header.sizeY, material.diffuseTexture.header.format,
            material.transmissionTexture.colors.size(), material.emissiveTexture.colors.size(),
            material.normalTexture.colors.size(), material.specularTexture.colors.size());
        std::printf("  consumed=%zu remaining=%zu\n", reader.offset(), reader.remaining());
    } else {
        BakingJobInputs jobs;
        readBakingJobInputs(reader, jobs, version);
        std::printf("  lightmap2DJobs=%zu sdfShadowJobs=%zu sdfShadowGroupJobs=%zu mappingGroupJobs=%zu "
                    "mappingSplitJobs=%zu consumed=%zu remaining=%zu\n",
                    jobs.lightmap2DJobs.size(), jobs.sdfShadowJobs.size(), jobs.sdfShadowGroupJobs.size(),
                    jobs.mappingGroupJobs.size(), jobs.mappingSplitJobs.size(), reader.offset(), reader.remaining());
        for (std::size_t index = 0; index < jobs.lightmap2DJobs.size(); ++index) {
            const Lightmap2DInput& lightmap = jobs.lightmap2DJobs[index];
            std::printf("  lightmap2D[%zu] job=%s mesh=%s size=%dx%d flags=0x%X\n", index,
                        formatGuid(lightmap.jobId).c_str(), formatGuid(lightmap.meshId).c_str(), lightmap.size.x,
                        lightmap.size.y, lightmap.flags);
        }
        for (const MappingSplitInput& split : jobs.mappingSplitJobs) {
            std::printf("  mappingSplit job=%s size=%d overlap=%d offset=%d,%d,%d\n",
                        formatGuid(split.jobId).c_str(), split.splitSize, split.overlapSize,
                        split.mappingOffsets.x, split.mappingOffsets.y, split.mappingOffsets.z);
        }
        for (const SdfShadowInput& shadow : jobs.sdfShadowJobs) {
            std::printf("  sdfShadow job=%s mesh=%s size=%dx%d flags=0x%X upsample=%d lights=%zu\n",
                        formatGuid(shadow.header.jobId).c_str(), formatGuid(shadow.header.meshId).c_str(),
                        shadow.header.size.x, shadow.header.size.y, shadow.header.flags,
                        shadow.header.upsampleFactor, shadow.lights.size());
            for (const Guid& lightGuid : shadow.shadowmaskLights) {
                std::printf("    shadowmaskLight=%s\n", formatGuid(lightGuid).c_str());
            }
        }
        for (const SdfShadowGroupInput& group : jobs.sdfShadowGroupJobs) {
            std::printf("  sdfShadowGroup job=%s size=%d upsample=%d jobs=%zu offsets=%zu\n",
                        formatGuid(group.jobId).c_str(), group.groupSize, group.upsampleFactor,
                        group.shadowJobIndices.size(), group.mappingOffsets.size());
            for (const Int3& offset : group.mappingOffsets) {
                std::printf("    offset=%d,%d,%d\n", offset.x, offset.y, offset.z);
            }
        }
        for (const MappingGroupInput& group : jobs.mappingGroupJobs) {
            std::printf("  mappingGroup guid=%s size=%d offsets=%zu\n", formatGuid(group.jobId).c_str(),
                        group.groupedSize, group.mappingOffsets.size());
            for (const Int3& offset : group.mappingOffsets) {
                std::printf("    offset=%d,%d,%d\n", offset.x, offset.y, offset.z);
            }
        }
    }
    return 0;
}

int lightmap2DOutputTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    Lightmap2DOutput output;
    output.header.jobId = Guid{0x11111111u, 0x22222222u, 0x33333333u, 0x44444444u};
    output.header.meshId = Guid{0xAAAABBBBu, 0xCCCCDDDDu, 0xEEEEFFFFu, 0x01020304u};
    output.header.size = Int2{4, 2};
    output.header.executionTime = 1.25;
    output.jobInputIndex = 7;
    output.bHasPrtHqTextures = 1;
    output.bHasPrtLqTextures = 0;
    output.bHasStaticTextures = 1;
    output.lights.push_back(Guid{1u, 2u, 3u, 4u});
    output.lightmapData.resize(8);
    for (std::size_t i = 0; i < output.lightmapData.size(); ++i) {
        Lightmap2DSample& sample = output.lightmapData[i];
        const float value = static_cast<float>(i);
        sample.shVectorR = Float4{value + 0.1f, value + 0.2f, value + 0.3f, value + 0.4f};
        sample.shVectorG = Float4{value + 1.1f, value + 1.2f, value + 1.3f, value + 1.4f};
        sample.shVectorB = Float4{value + 2.1f, value + 2.2f, value + 2.3f, value + 2.4f};
        sample.incidentLighting = Float4{value + 3.1f, value + 3.2f, value + 3.3f, value + 3.4f};
        sample.bIsMapped = static_cast<uint8_t>(i % 2u);
    }

    Writer writer;
    writeLightmap2DOutput(writer, output);
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "hbg_dawnbake_lmgz_reader_test.lmgz";
    std::filesystem::remove(path);
    writeGzipFile(path, writer.bytes());

    const Lightmap2DOutput decoded = readLightmap2DOutputFile(path);
    std::filesystem::remove(path);

    require(decoded.header.jobId.a == output.header.jobId.a && decoded.header.jobId.b == output.header.jobId.b &&
                decoded.header.jobId.c == output.header.jobId.c && decoded.header.jobId.d == output.header.jobId.d,
            "lmgz job id mismatch");
    require(decoded.header.meshId.a == output.header.meshId.a && decoded.header.meshId.b == output.header.meshId.b &&
                decoded.header.meshId.c == output.header.meshId.c && decoded.header.meshId.d == output.header.meshId.d,
            "lmgz mesh id mismatch");
    require(decoded.header.size.x == 4 && decoded.header.size.y == 2, "lmgz size mismatch");
    require(std::fabs(decoded.header.executionTime - 1.25) <= 1.0e-9, "lmgz execution time mismatch");
    require(decoded.jobInputIndex == 7, "lmgz job input index mismatch");
    require(decoded.bHasPrtHqTextures == 1 && decoded.bHasPrtLqTextures == 0 && decoded.bHasStaticTextures == 1,
            "lmgz output flags mismatch");
    require(decoded.lights.size() == 1, "lmgz light count mismatch");
    require(decoded.lightmapData.size() == 8, "lmgz sample count mismatch");
    require(decoded.lightmapData[3].incidentLighting.x == output.lightmapData[3].incidentLighting.x &&
                decoded.lightmapData[3].incidentLighting.y == output.lightmapData[3].incidentLighting.y &&
                decoded.lightmapData[3].incidentLighting.z == output.lightmapData[3].incidentLighting.z &&
                decoded.lightmapData[3].incidentLighting.w == output.lightmapData[3].incidentLighting.w,
            "lmgz incident lighting mismatch");
    require(decoded.lightmapData[3].bIsMapped == 1, "lmgz mapped flag mismatch");

    std::printf("lmgz reader test OK: raw=%zu samples=%zu lights=%zu size=%dx%d\n", writer.size(),
                decoded.lightmapData.size(), decoded.lights.size(), decoded.header.size.x, decoded.header.size.y);
    return 0;
}

int lightmap2DBindingTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    SceneInfo scene = buildMinimalScene();
    BakingJobInputs jobs = buildMinimalJobs();
    jobs.lightmap2DJobs[0].size = Int2{4, 2};
    const Lightmap2DInput& input = jobs.lightmap2DJobs[0];

    Lightmap2DOutput output;
    output.header.jobId = input.jobId;
    output.header.meshId = input.meshId;
    output.header.size = input.size;
    output.header.executionTime = 1.5;
    output.lightmapData.resize(8);
    output.lightmapData[0].bIsMapped = 1;

    Lightmap2DResultBinding binding;
    std::string error;
    require(bindLightmap2DResult(scene, jobs, output, &binding, &error), "valid lightmap result did not bind");
    require(binding.jobIndex == 0u && binding.instanceIndex == 0u, "lightmap result binding index mismatch");
    require(binding.input == &jobs.lightmap2DJobs[0] && binding.output == &output,
            "lightmap result binding pointer mismatch");
    require(lightmapResultFileName(input.jobId) == "v2.00000000000000000000000000000005.lmgz",
            "lightmap result file name mismatch");
    require(validateLightmap2DResult(output, input, &error), "valid lightmap result did not validate");

    Lightmap2DOutput wrongJob = output;
    wrongJob.header.jobId.a = 99u;
    require(!validateLightmap2DResult(wrongJob, input, &error) && error.find("JobID") != std::string::npos,
            "lightmap result did not reject a mismatched JobID");
    require(!bindLightmap2DResult(scene, jobs, wrongJob, &binding, &error) &&
                error.find("no lightmap job") != std::string::npos,
            "lightmap result did not reject an unknown JobID");

    Lightmap2DOutput wrongMesh = output;
    wrongMesh.header.meshId.a = 99u;
    require(!validateLightmap2DResult(wrongMesh, input, &error) && error.find("MeshID") != std::string::npos,
            "lightmap result did not reject a mismatched MeshID");
    require(!bindLightmap2DResult(scene, jobs, wrongMesh, &binding, &error) &&
                error.find("no mesh instance") != std::string::npos,
            "lightmap result did not reject an unknown MeshID");

    Lightmap2DOutput wrongSize = output;
    wrongSize.header.size.x = 5;
    require(!validateLightmap2DResult(wrongSize, input, &error) && error.find("result size") != std::string::npos,
            "lightmap result did not reject a mismatched size");

    Lightmap2DInput paddedInput = input;
    paddedInput.flags |= kLightmapFlagPadding;
    Lightmap2DOutput paddedOutput = output;
    paddedOutput.header.size = Int2{6, 4};
    paddedOutput.lightmapData.resize(24);
    require(validateLightmap2DResult(paddedOutput, paddedInput, &error),
            "padded lightmap result did not validate against the original importer size");
    paddedOutput.header.size = input.size;
    paddedOutput.lightmapData.resize(8);
    require(!validateLightmap2DResult(paddedOutput, paddedInput, &error) &&
                error.find("+ padding") != std::string::npos,
            "padded lightmap result did not reject an unpadded size");

    Lightmap2DOutput shortData = output;
    shortData.lightmapData.pop_back();
    require(!validateLightmap2DResult(shortData, input, &error) && error.find("sample count") != std::string::npos,
            "lightmap result did not reject a mismatched sample count");

    Lightmap2DInput zeroSize = input;
    zeroSize.size = Int2{0, 2};
    Lightmap2DOutput zeroOutput = output;
    zeroOutput.header.size = zeroSize.size;
    zeroOutput.lightmapData.clear();
    require(!validateLightmap2DResult(zeroOutput, zeroSize, &error) && error.find("positive") != std::string::npos,
            "lightmap result did not reject a non-positive size");

    BakingJobInputs duplicateJobs = jobs;
    duplicateJobs.lightmap2DJobs.push_back(input);
    require(!bindLightmap2DResult(scene, duplicateJobs, output, &binding, &error) &&
                error.find("duplicate lightmap job") != std::string::npos,
            "lightmap result did not reject a duplicate JobID");

    SceneInfo duplicateScene = scene;
    duplicateScene.meshInstances.push_back(scene.meshInstances.front());
    require(!bindLightmap2DResult(duplicateScene, jobs, output, &binding, &error) &&
                error.find("duplicate mesh instance") != std::string::npos,
            "lightmap result did not reject a duplicate MeshID");

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawnbake_lmgz_bind_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    const std::string guid = "11111111222222223333333344444444";
    Writer sceneWriter;
    sceneWriter.version(kExportVersion);
    writeScene(sceneWriter, scene, kExportVersion);
    writeFile(directory / (guid + ".scene"), sceneWriter.bytes());
    Writer jobsWriter;
    jobsWriter.version(kExportVersion);
    writeBakingJobInputs(jobsWriter, jobs, kExportVersion);
    writeFile(directory / (guid + ".jobs"), jobsWriter.bytes());
    Writer outputWriter;
    writeLightmap2DOutput(outputWriter, output);
    const std::filesystem::path resultPath = directory / lightmapResultFileName(input.jobId);
    writeGzipFile(resultPath, outputWriter.bytes());

    require(verifyLightmap2DResultSet(directory, guid, false, &error), "lightmap result set did not verify");
    std::filesystem::remove(resultPath);
    require(!verifyLightmap2DResultSet(directory, guid, false, &error) &&
                error.find("missing lightmap result") != std::string::npos,
            "lightmap result set did not reject a missing result");

    std::filesystem::remove(directory / (guid + ".scene"));
    std::filesystem::remove(directory / (guid + ".jobs"));
    writeGzipFile(directory / ("v2." + guid + ".scenegz"), sceneWriter.bytes());
    writeGzipFile(directory / ("v2." + guid + ".jobgz"), jobsWriter.bytes());
    writeGzipFile(resultPath, outputWriter.bytes());
    require(verifyLightmap2DResultSet(directory, guid, false, &error),
            "lightmap result set did not verify gzip scene/jobs");

    std::printf("lmgz binding test OK: jobs=%zu instances=%zu\n", jobs.lightmap2DJobs.size(),
                scene.meshInstances.size());
    return 0;
}

int sdfShadowOutputTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    SceneInfo scene = buildMinimalScene();
    BakingJobInputs jobs = buildMinimalJobs();
    const Guid jobGuid{0x12345678u, 0x9ABCDEF0u, 0x11223344u, 0x55667788u};
    const Guid lightGuid{0x0BADF00Du, 0x13572468u, 0x24681357u, 0x99887766u};
    const Guid secondLightGuid{0xF00DBABEu, 0x24681357u, 0x13572468u, 0x11223344u};
    const Guid meshGuid = scene.meshInstances.front().header.guid;

    SdfShadowInput input;
    input.header.jobId = jobGuid;
    input.header.meshId = meshGuid;
    input.header.flags = kLightmapFlagBilinearFilter | kLightmapFlagPadding;
    input.header.size = Int2{4, 2};
    input.header.upsampleFactor = 4;
    input.header.lightmapUVIndex = 1;
    input.lights.push_back(lightGuid);
    input.lights.push_back(secondLightGuid);
    input.shadowmaskLights.push_back(lightGuid);
    input.shadowmaskLights.push_back(secondLightGuid);
    jobs.sdfShadowJobs.push_back(input);
    jobs.sdfShadowJobsCount = 1u;
    jobs.partialJobsTotalNum.numSdfShadowJobs = 1u;

    SdfShadowOutput output;
    output.header.jobId = jobGuid;
    output.header.meshId = meshGuid;
    output.header.size = Int2{6, 4};
    output.header.executionTime = 2.5;
    output.shadowMaps.resize(2);
    output.shadowMaps[0].lightId = lightGuid;
    output.shadowMaps[1].lightId = secondLightGuid;
    for (std::size_t mapIndex = 0; mapIndex < output.shadowMaps.size(); ++mapIndex) {
        output.shadowMaps[mapIndex].shadowData.resize(24);
        for (std::size_t index = 0; index < output.shadowMaps[mapIndex].shadowData.size(); ++index) {
            SdfShadowSample& sample = output.shadowMaps[mapIndex].shadowData[index];
            sample.distance = static_cast<float>(index) * 0.25f + static_cast<float>(mapIndex);
            sample.penumbraSize = static_cast<float>(index) * 0.5f + static_cast<float>(mapIndex);
            sample.isMapped = static_cast<uint8_t>(index % 2u);
        }
    }

    std::string error;
    require(validateSdfShadowResult(output, input, &error), "valid SDF result did not validate");
    SdfShadowResultBinding binding;
    require(bindSdfShadowResult(scene, jobs, output, &binding, &error), "valid SDF result did not bind");
    require(binding.jobIndex == 0u && binding.instanceIndex == 0u, "SDF binding index mismatch");
    require(binding.input == &jobs.sdfShadowJobs[0] && binding.output == &output,
            "SDF binding pointer mismatch");
    require(shadowmaskResultFileName(jobGuid) == "v2.123456789ABCDEF01122334455667788.sdfgz",
            "SDF result file name mismatch");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "hbg_dawnbake_sdfgz_reader_test.sdfgz";
    std::filesystem::remove(path);
    Writer writer;
    writeSdfShadowOutput(writer, output);
    writeGzipFile(path, writer.bytes());
    const SdfShadowOutput decoded = readSdfShadowOutputFile(path);
    std::filesystem::remove(path);
    require(decoded.header.jobId.a == jobGuid.a && decoded.header.jobId.b == jobGuid.b &&
                decoded.header.jobId.c == jobGuid.c && decoded.header.jobId.d == jobGuid.d,
            "sdfgz job id mismatch");
    require(decoded.header.size.x == 6 && decoded.header.size.y == 4, "sdfgz size mismatch");
    require(decoded.shadowMaps.size() == 2u && decoded.shadowMaps[0].shadowData.size() == 24u &&
                decoded.shadowMaps[1].shadowData.size() == 24u,
            "sdfgz sample count mismatch");
    require(decoded.shadowMaps[0].shadowData[3].distance == output.shadowMaps[0].shadowData[3].distance &&
                decoded.shadowMaps[0].shadowData[3].penumbraSize ==
                    output.shadowMaps[0].shadowData[3].penumbraSize &&
                decoded.shadowMaps[0].shadowData[3].isMapped == 1u &&
                decoded.shadowMaps[1].shadowData[3].distance == output.shadowMaps[1].shadowData[3].distance,
            "sdfgz sample payload mismatch");

    SdfShadowOutput wrongJob = output;
    wrongJob.header.jobId.a = 99u;
    require(!validateSdfShadowResult(wrongJob, input, &error) && error.find("JobID") != std::string::npos,
            "SDF result did not reject a mismatched JobID");
    SdfShadowOutput wrongMesh = output;
    wrongMesh.header.meshId.a = 99u;
    require(!validateSdfShadowResult(wrongMesh, input, &error) && error.find("MeshID") != std::string::npos,
            "SDF result did not reject a mismatched MeshID");
    SdfShadowOutput wrongSize = output;
    wrongSize.header.size.x = 5;
    require(!validateSdfShadowResult(wrongSize, input, &error) &&
                error.find("result size") != std::string::npos,
            "SDF result did not reject a mismatched size");
    SdfShadowOutput shortData = output;
    shortData.shadowMaps[0].shadowData.pop_back();
    require(!validateSdfShadowResult(shortData, input, &error) &&
                error.find("sample count") != std::string::npos,
            "SDF result did not reject a mismatched sample count");
    SdfShadowOutput wrongLight = output;
    wrongLight.shadowMaps[1].lightId = lightGuid;
    require(!validateSdfShadowResult(wrongLight, input, &error) &&
                error.find("LightID") != std::string::npos,
            "SDF result did not reject a mismatched LightID");
    require(decoded.shadowMaps[0].lightId.a == lightGuid.a && decoded.shadowMaps[0].lightId.b == lightGuid.b &&
                decoded.shadowMaps[0].lightId.c == lightGuid.c && decoded.shadowMaps[0].lightId.d == lightGuid.d &&
                decoded.shadowMaps[1].lightId.a == secondLightGuid.a &&
                decoded.shadowMaps[1].lightId.b == secondLightGuid.b &&
                decoded.shadowMaps[1].lightId.c == secondLightGuid.c &&
                decoded.shadowMaps[1].lightId.d == secondLightGuid.d,
            "sdfgz light id mismatch");
    SdfShadowOutput missingMap = output;
    missingMap.shadowMaps.clear();
    require(!validateSdfShadowResult(missingMap, input, &error) &&
                error.find("light count") != std::string::npos,
            "SDF result did not reject a missing light map");
    SdfShadowOutput unknownJob = output;
    unknownJob.header.jobId.a = 99u;
    require(!bindSdfShadowResult(scene, jobs, unknownJob, &binding, &error) &&
                error.find("no SDF job") != std::string::npos,
            "SDF result did not reject an unknown JobID");
    BakingJobInputs duplicateJobs = jobs;
    duplicateJobs.sdfShadowJobs.push_back(input);
    require(!bindSdfShadowResult(scene, duplicateJobs, output, &binding, &error) &&
                error.find("duplicate SDF job") != std::string::npos,
            "SDF result did not reject a duplicate JobID");

    std::vector<SdfShadowInput> packJobs(4);
    packJobs[0].header.size = Int2{48, 48};
    packJobs[1].header.size = Int2{29, 29};
    packJobs[2].header.size = Int2{512, 512};
    packJobs[3].header.size = Int2{311, 311};
    const std::vector<SdfShadowGroupInput> packed = packSdfShadowGroups(packJobs, 256u);
    require(packed.size() == 3u, "SDF group packing count mismatch");
    require(packed[0].jobId.c == 0u && packed[0].groupSize == 311 &&
                packed[0].shadowJobIndices == std::vector<int32_t>{3} && packed[0].mappingOffsets.size() == 1u &&
                packed[0].mappingOffsets[0].x == 0 && packed[0].mappingOffsets[0].y == 0 &&
                packed[0].mappingOffsets[0].z == 3,
            "SDF single group packing mismatch");
    require(packed[1].jobId.c == 1u && packed[1].groupSize == 512 &&
                packed[1].shadowJobIndices == std::vector<int32_t>{2} && packed[1].mappingOffsets.size() == 1u &&
                packed[1].mappingOffsets[0].z == 2,
            "SDF oversized group packing mismatch");
    require(packed[2].jobId.c == 2u && packed[2].groupSize == 256 &&
                packed[2].shadowJobIndices == std::vector<int32_t>{1, 0} &&
                packed[2].mappingOffsets.size() == 2u && packed[2].mappingOffsets[0].x == 0 &&
                packed[2].mappingOffsets[0].y == 0 && packed[2].mappingOffsets[0].z == 1 &&
                packed[2].mappingOffsets[1].x == 0 && packed[2].mappingOffsets[1].y == 29 &&
                packed[2].mappingOffsets[1].z == 0,
            "SDF packed group mapping mismatch");

    MappingGroupInput lightmapGroup;
    lightmapGroup.groupedSize = 128;
    lightmapGroup.mappingOffsets = {Int3{0, 0, 0}, Int3{0, 48, 1}};
    const std::vector<SdfShadowGroupInput> inherited = packSdfShadowGroups(packJobs, 256u, {lightmapGroup});
    require(inherited.size() == 3u && inherited[0].groupSize == 311 &&
                inherited[0].shadowJobIndices == std::vector<int32_t>{3} &&
                inherited[1].groupSize == 512 && inherited[1].shadowJobIndices == std::vector<int32_t>{2},
            "SDF inherited single group mismatch");
    require(inherited[2].groupSize == 128 && inherited[2].shadowJobIndices == std::vector<int32_t>{0, 1} &&
                inherited[2].mappingOffsets.size() == 2u && inherited[2].mappingOffsets[0].x == 0 &&
                inherited[2].mappingOffsets[0].y == 0 && inherited[2].mappingOffsets[0].z == 0 &&
                inherited[2].mappingOffsets[1].x == 0 && inherited[2].mappingOffsets[1].y == 48 &&
                inherited[2].mappingOffsets[1].z == 1,
            "SDF inherited lightmap group offsets mismatch");

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawnbake_sdfgz_bind_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    const std::string sceneGuid = "AABBCCDDEEFF00112233445566778899";
    Writer sceneWriter;
    sceneWriter.version(kExportVersion);
    writeScene(sceneWriter, scene, kExportVersion);
    writeFile(directory / (lowercaseGuid(sceneGuid) + ".scene"), sceneWriter.bytes());
    Writer jobsWriter;
    jobsWriter.version(kExportVersion);
    writeBakingJobInputs(jobsWriter, jobs, kExportVersion);
    writeFile(directory / (lowercaseGuid(sceneGuid) + ".jobs"), jobsWriter.bytes());
    const std::filesystem::path resultPath = directory / shadowmaskResultFileName(jobGuid);
    writeGzipFile(resultPath, writer.bytes());
    require(verifySdfShadowResultSet(directory, sceneGuid, false, &error),
            "SDF shadow result set did not verify");
    std::filesystem::remove(resultPath);
    require(!verifySdfShadowResultSet(directory, sceneGuid, false, &error) &&
                error.find("missing SDF shadow result") != std::string::npos,
            "SDF shadow result set did not reject a missing result");
    std::filesystem::remove_all(directory);

    std::printf("sdfgz test OK: jobs=%zu maps=%zu samples=%zu size=%dx%d\n", jobs.sdfShadowJobs.size(),
                decoded.shadowMaps.size(),
                decoded.shadowMaps[0].shadowData.size() * decoded.shadowMaps.size(), decoded.header.size.x,
                decoded.header.size.y);
    return 0;
}

int lightmapAtlasImageTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    SceneInfo scene = buildMinimalScene();
    BakingJobInputs jobs = buildMinimalJobs();
    jobs.lightmap2DJobs[0].size = Int2{4, 2};
    jobs.lightmap2DJobs[0].flags = 0;
    const Lightmap2DInput& input = jobs.lightmap2DJobs[0];

    Lightmap2DOutput output;
    output.header.jobId = input.jobId;
    output.header.meshId = input.meshId;
    output.header.size = input.size;
    output.lightmapData.resize(8);
    for (std::size_t index = 0; index < output.lightmapData.size(); ++index) {
        const float value = static_cast<float>(index) + 0.5f;
        Lightmap2DSample& sample = output.lightmapData[index];
        sample.shVectorR = Float4{value, value + 1.0f, value + 2.0f, value + 3.0f};
        sample.incidentLighting = Float4{value + 4.0f, value + 5.0f, value + 6.0f, value + 7.0f};
        sample.bIsMapped = index == 0u ? uint8_t{1} : uint8_t{0};
    }

    const LightmapAtlasResult result{&output, LightmapAtlasPlacement{0u, 1u, 1u, 4u, 2u}};
    LightmapAtlasImageSet images;
    std::string error;
    require(buildLightmapAtlasImages(scene, jobs, {result}, {8u}, &images, &error, LightmapAtlasOptions{0u}),
            "lightmap SH atlas build failed");
    require(images.atlases.size() == 1u && images.atlases[0].size == 8u, "lightmap atlas output mismatch");
    require(images.texelsCopied == output.lightmapData.size(), "lightmap atlas copied texel count mismatch");
    const std::size_t copiedTexels = images.texelsCopied;
    const LightmapAtlasImage& image = images.atlases[0];
    const auto texelOffset = [](uint32_t x, uint32_t y) {
        return (static_cast<std::size_t>(y) * 8u + static_cast<std::size_t>(x)) * 4u;
    };
    const Lightmap2DSample& source = output.lightmapData[5];
    const std::size_t target = texelOffset(2u, 2u);
    require(image.sh[0][target + 0u] == bx::halfFromFloat(source.incidentLighting.x) &&
                image.sh[0][target + 1u] == bx::halfFromFloat(source.incidentLighting.y) &&
                image.sh[0][target + 2u] == bx::halfFromFloat(source.incidentLighting.z) &&
                image.sh[0][target + 3u] == bx::halfFromFloat(source.incidentLighting.w),
            "lightmap incident channel mismatch");
    require(image.sh[1][target + 0u] == bx::halfFromFloat(source.shVectorR.x) &&
                image.sh[1][target + 3u] == bx::halfFromFloat(source.shVectorR.w) &&
                image.sh[2][target + 0u] == 0u && image.sh[2][target + 3u] == 0u,
            "lightmap direction channel mismatch");
    require(image.mapped[static_cast<std::size_t>(1u) * 8u + 1u] == 1u, "lightmap atlas mapped texel missing");
    require(image.mapped[static_cast<std::size_t>(1u) * 8u + 2u] == 0u, "lightmap atlas unmapped texel mismatch");
    const std::size_t untouched = texelOffset(0u, 0u);
    require(image.sh[0][untouched + 0u] == 0u && image.sh[0][untouched + 3u] == 0u,
            "lightmap atlas did not clear unused texels");

    Lightmap2DInput paddedInput = input;
    paddedInput.flags |= kLightmapFlagPadding;
    paddedInput.padding = 1;
    BakingJobInputs paddedJobs = jobs;
    paddedJobs.lightmap2DJobs[0] = paddedInput;

    Lightmap2DOutput paddedOutput = output;
    paddedOutput.header.size = Int2{6, 4};
    paddedOutput.lightmapData.resize(24);
    for (std::size_t index = 0; index < paddedOutput.lightmapData.size(); ++index) {
        const float value = static_cast<float>(index) + 20.5f;
        Lightmap2DSample& sample = paddedOutput.lightmapData[index];
        sample.shVectorR = Float4{value, value + 1.0f, value + 2.0f, value + 3.0f};
        sample.incidentLighting = Float4{value + 4.0f, value + 5.0f, value + 6.0f, value + 7.0f};
        sample.bIsMapped = uint8_t{1};
    }
    LightmapAtlasImageSet paddedImages;
    require(buildLightmapAtlasImages(scene, paddedJobs,
                                     {LightmapAtlasResult{&paddedOutput, LightmapAtlasPlacement{0u, 1u, 1u, 4u, 2u}}},
                                     {8u}, &paddedImages, &error),
            "padded lightmap SH atlas build failed");
    const std::size_t paddedTarget = texelOffset(0u, 0u);
    require(paddedImages.atlases[0].sh[0][paddedTarget + 0u] ==
                bx::halfFromFloat(paddedOutput.lightmapData[0].incidentLighting.x),
            "padded lightmap atlas origin mismatch");
    const std::size_t paddedLast = texelOffset(5u, 3u);
    require(paddedImages.atlases[0].sh[0][paddedLast + 0u] ==
                bx::halfFromFloat(paddedOutput.lightmapData[23].incidentLighting.x),
            "padded lightmap atlas padded extent mismatch");

    LightmapAtlasImageSet dilatedImages;
    require(buildLightmapAtlasImages(scene, jobs, {result}, {8u}, &dilatedImages, &error, LightmapAtlasOptions{1u}),
            "lightmap SH atlas dilation failed");
    const LightmapAtlasImage& dilated = dilatedImages.atlases[0];
    const Lightmap2DSample& mappedSource = output.lightmapData[0];
    const std::size_t firstNeighbor = texelOffset(2u, 1u);
    require(dilated.mapped[static_cast<std::size_t>(1u) * 8u + 2u] == 1u &&
                dilated.mapped[static_cast<std::size_t>(2u) * 8u + 1u] == 1u &&
                dilated.mapped[static_cast<std::size_t>(2u) * 8u + 2u] == 1u,
            "lightmap SH atlas dilation mask mismatch");
    require(dilated.sh[0][firstNeighbor + 0u] == bx::halfFromFloat(mappedSource.incidentLighting.x) &&
                dilated.sh[0][firstNeighbor + 3u] == bx::halfFromFloat(mappedSource.incidentLighting.w) &&
                dilated.sh[1][firstNeighbor + 1u] == bx::halfFromFloat(mappedSource.shVectorR.y),
            "lightmap atlas dilation value mismatch");
    require(dilated.mapped[static_cast<std::size_t>(1u) * 8u + 3u] == 0u,
            "lightmap SH atlas dilation exceeded one iteration");

    Lightmap2DOutput dilationOutput = output;
    dilationOutput.lightmapData[1].bIsMapped = 1u;
    LightmapAtlasImageSet averagedImages;
    require(buildLightmapAtlasImages(scene, jobs, {LightmapAtlasResult{&dilationOutput, result.placement}}, {8u},
                                     &averagedImages, &error, LightmapAtlasOptions{1u}),
            "lightmap SH atlas neighbor average failed");
    const Lightmap2DSample& secondSource = output.lightmapData[1];
    const std::size_t averagedNeighbor = texelOffset(1u, 2u);
    require(averagedImages.atlases[0].sh[0][averagedNeighbor + 0u] ==
                    bx::halfFromFloat((mappedSource.incidentLighting.x + secondSource.incidentLighting.x) * 0.5f) &&
                averagedImages.atlases[0].sh[0][averagedNeighbor + 3u] ==
                    bx::halfFromFloat((mappedSource.incidentLighting.w + secondSource.incidentLighting.w) * 0.5f),
            "lightmap atlas dilation did not average valid neighbors");

    LightmapAtlasOptions edgeOptions;
    edgeOptions.edgePadding = 2u;
    LightmapAtlasImageSet edgeImages;
    require(buildLightmapAtlasImages(scene, jobs,
                                     {LightmapAtlasResult{&output, LightmapAtlasPlacement{0u, 2u, 2u, 4u, 2u}}},
                                     {8u}, &edgeImages, &error, edgeOptions),
            "lightmap SH atlas edge padding build failed");
    require(edgeImages.atlases[0].mapped[0u] == 1u &&
                edgeImages.atlases[0].mapped[static_cast<std::size_t>(2u) * 8u + 0u] == 1u,
            "lightmap SH atlas edge padding ring was not filled");
    require(edgeImages.atlases[0].sh[0][0u] == bx::halfFromFloat(mappedSource.incidentLighting.x),
            "lightmap SH atlas edge padding value mismatch");

    Lightmap2DOutput emptyOutput = output;
    for (Lightmap2DSample& sample : emptyOutput.lightmapData) {
        sample.shVectorR = Float4{};
        sample.incidentLighting = Float4{};
        sample.bIsMapped = 0u;
    }
    LightmapAtlasImageSet isolatedImages;
    require(buildLightmapAtlasImages(scene, jobs,
                                     {LightmapAtlasResult{&output, LightmapAtlasPlacement{0u, 0u, 0u, 4u, 2u}},
                                      LightmapAtlasResult{&emptyOutput, LightmapAtlasPlacement{0u, 4u, 0u, 4u, 2u}}},
                                     {8u}, &isolatedImages, &error, LightmapAtlasOptions{4u}),
            "lightmap SH atlas isolated dilation failed");
    for (uint32_t y = 0; y < 2u; ++y) {
        for (uint32_t x = 0; x < 4u; ++x) {
            require(isolatedImages.atlases[0].mapped[static_cast<std::size_t>(y) * 8u + x] == 1u,
                    "lightmap SH atlas block did not dilate within its rect");
        }
        for (uint32_t x = 4u; x < 8u; ++x) {
            const std::size_t index = static_cast<std::size_t>(y) * 8u + x;
            require(isolatedImages.atlases[0].mapped[index] == 0u && isolatedImages.atlases[0].sh[0][index * 4u] == 0u,
                    "lightmap SH atlas dilation crossed a block boundary");
        }
    }

    require(!buildLightmapAtlasImages(scene, jobs,
                                      {LightmapAtlasResult{&output, LightmapAtlasPlacement{0u, 5u, 1u, 4u, 2u}}}, {8u},
                                      &images, &error) &&
                error.find("outside atlas") != std::string::npos,
            "lightmap atlas did not reject an out-of-bounds rect");
    require(!buildLightmapAtlasImages(scene, jobs,
                                      {LightmapAtlasResult{&output, LightmapAtlasPlacement{1u, 1u, 1u, 4u, 2u}}}, {8u},
                                      &images, &error) &&
                error.find("references atlas") != std::string::npos,
            "lightmap atlas did not reject an invalid atlas index");
    require(!buildLightmapAtlasImages(scene, jobs,
                                      {LightmapAtlasResult{&output, LightmapAtlasPlacement{0u, 1u, 1u, 3u, 2u}}}, {8u},
                                      &images, &error) &&
                error.find("does not match input size") != std::string::npos,
            "lightmap atlas did not reject a mismatched rect size");
    require(!buildLightmapAtlasImages(scene, paddedJobs,
                                      {LightmapAtlasResult{&paddedOutput, LightmapAtlasPlacement{0u, 0u, 0u, 4u, 2u}}},
                                      {8u}, &images, &error) &&
                error.find("padding") != std::string::npos,
            "lightmap atlas did not reject a rect without padding room");
    require(!buildLightmapAtlasImages(scene, jobs, {result, result}, {8u}, &images, &error) &&
                error.find("overlaps another result") != std::string::npos,
            "lightmap atlas did not reject overlapping results");
    require(!buildLightmapAtlasImages(scene, jobs, {LightmapAtlasResult{nullptr, {}}}, {8u}, &images, &error) &&
                error.find("no output") != std::string::npos,
            "lightmap atlas did not reject a null output");

    std::printf("sh atlas test OK: atlases=%zu size=%u texels=%zu\n", images.atlases.size(), images.atlases[0].size,
                copiedTexels);
    return 0;
}

int lightmapAtlasDdsTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawnbake_sh_atlas_dds_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    LightmapAtlasImageSet images;
    images.atlases.resize(2u);
    for (std::size_t atlasIndex = 0u; atlasIndex < images.atlases.size(); ++atlasIndex) {
        LightmapAtlasImage& image = images.atlases[atlasIndex];
        image.size = atlasIndex == 0u ? 4u : 2u;
        const std::size_t pixels = static_cast<std::size_t>(image.size) * image.size;
        for (std::size_t channel = 0u; channel < image.sh.size(); ++channel) {
            image.sh[channel].resize(pixels * 4u);
            for (std::size_t texel = 0u; texel < pixels; ++texel) {
                for (std::size_t component = 0u; component < 4u; ++component) {
                    const float value =
                        static_cast<float>(atlasIndex * 1000u + channel * 100u + texel * 4u + component);
                    image.sh[channel][texel * 4u + component] = bx::halfFromFloat(value);
                }
            }
        }
        image.mapped.assign(pixels, uint8_t{1});
        images.texelsCopied += pixels;
    }

    std::string error;
    if (!writeLightmapAtlasDds(images, directory, &error)) {
        throw std::runtime_error("lightmap atlas DDS write failed: " + error);
    }
    require(lightmapAtlasName(0u) == "lm0" && lightmapAtlasName(1u) == "lm1", "lightmap atlas name mismatch");
    require(lightmapAtlasDdsFileName(1u, 2u) == "lm1_sh2.dds", "lightmap DDS name mismatch");

    constexpr uint64_t kDdsHeaderBytes = 128u + 20u;
    std::size_t ddsFiles = 0u;
    uint64_t totalBytes = 0u;
    for (std::size_t atlasIndex = 0u; atlasIndex < images.atlases.size(); ++atlasIndex) {
        const LightmapAtlasImage& image = images.atlases[atlasIndex];
        const std::size_t pixels = static_cast<std::size_t>(image.size) * image.size;
        const std::size_t payloadBytes = pixels * 4u * sizeof(uint16_t);
        for (std::size_t channel = 0u; channel < image.sh.size(); ++channel) {
            const std::filesystem::path path =
                directory / lightmapAtlasDdsFileName(static_cast<uint32_t>(atlasIndex), static_cast<uint32_t>(channel));
            require(std::filesystem::exists(path), "lightmap DDS file is missing");
            const std::vector<uint8_t> bytes = readFile(path);
            require(bytes.size() == kDdsHeaderBytes + payloadBytes, "lightmap DDS size mismatch");
            totalBytes += bytes.size();
            ++ddsFiles;

            bimg::ImageContainer container{};
            bx::Error parseError;
            require(bimg::imageParse(container, bytes.data(), static_cast<uint32_t>(bytes.size()), &parseError),
                    "lightmap DDS parse failed");
            require(container.m_format == bimg::TextureFormat::RGBA16F, "lightmap DDS format mismatch");
            require(!container.m_srgb, "lightmap DDS must be linear");
            require(container.m_width == image.size && container.m_height == image.size,
                    "lightmap DDS dimensions mismatch");
            require(container.m_numMips == 1u && container.m_numLayers == 1u && !container.m_cubeMap,
                    "lightmap DDS mip/layer mismatch");

            bimg::ImageMip mip;
            require(bimg::imageGetRawData(container, 0u, 0u, bytes.data(), static_cast<uint32_t>(bytes.size()), mip),
                    "lightmap DDS raw data is missing");
            require(mip.m_width == image.size && mip.m_height == image.size && mip.m_size == payloadBytes,
                    "lightmap DDS mip layout mismatch");
            require(std::memcmp(mip.m_data, image.sh[channel].data(), payloadBytes) == 0, "lightmap DDS data mismatch");
        }
    }
    require(ddsFiles == images.atlases.size() * 3u, "lightmap DDS file count mismatch");

    LightmapAtlasImageSet emptyImages;
    error.clear();
    require(!writeLightmapAtlasDds(emptyImages, directory, &error) &&
                error.find("no lightmap atlases") != std::string::npos,
            "lightmap DDS writer did not reject empty images");

    LightmapAtlasImageSet brokenImages = images;
    brokenImages.atlases[0].sh[1].resize(1u);
    error.clear();
    require(!writeLightmapAtlasDds(brokenImages, directory, &error) && error.find("channel 1") != std::string::npos,
            "lightmap DDS writer did not reject a bad channel size");

    std::printf("sh atlas dds test OK: atlases=%zu files=%zu bytes=%llu\n", images.atlases.size(), ddsFiles,
                static_cast<unsigned long long>(totalBytes));
    return 0;
}

int lightmapAtlasDdsVerifyTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawnbake_sh_atlas_dds_verify_" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    SceneInfo scene = buildMinimalScene();
    BakingJobInputs jobs = buildMinimalJobs();
    jobs.lightmap2DJobs[0].size = Int2{4, 2};
    jobs.lightmap2DJobs[0].flags = 0;
    const Lightmap2DInput& input = jobs.lightmap2DJobs[0];

    Lightmap2DOutput output;
    output.header.jobId = input.jobId;
    output.header.meshId = input.meshId;
    output.header.size = input.size;
    output.lightmapData.resize(8);
    std::size_t mappedCount = 0u;
    for (std::size_t index = 0; index < output.lightmapData.size(); ++index) {
        const float value = static_cast<float>(index) + 0.25f;
        Lightmap2DSample& sample = output.lightmapData[index];
        sample.shVectorR = Float4{value, value + 1.0f, value + 2.0f, value + 3.0f};
        sample.incidentLighting = Float4{value + 4.0f, value + 5.0f, value + 6.0f, value + 7.0f};
        sample.bIsMapped = (index == 0u || index == 1u || index == 5u) ? uint8_t{1} : uint8_t{0};
        mappedCount += sample.bIsMapped != 0u ? 1u : 0u;
    }

    Writer outputWriter;
    writeLightmap2DOutput(outputWriter, output);
    const std::filesystem::path lmgzPath = directory / lightmapResultFileName(input.jobId);
    writeGzipFile(lmgzPath, outputWriter.bytes());
    const Lightmap2DOutput loaded = readLightmap2DOutputFile(lmgzPath);
    require(loaded.lightmapData.size() == output.lightmapData.size(),
            "lightmap DDS verify .lmgz sample count mismatch");

    LightmapAtlasImageSet images;
    std::string error;
    const LightmapAtlasPlacement placement{0u, 1u, 1u, 4u, 2u};
    require(buildLightmapAtlasImages(scene, jobs, {LightmapAtlasResult{&loaded, placement}}, {8u}, &images, &error),
            "lightmap DDS verify atlas build failed");
    require(writeLightmapAtlasDds(images, directory, &error), "lightmap DDS verify write failed");

    require(verifyLightmapAtlasDds(loaded, 0u, 1u, 1u, 8u, directory, &error),
            "lightmap DDS verify did not accept matching data");

    Lightmap2DOutput corrupted = loaded;
    corrupted.lightmapData[5].incidentLighting.z += 2.0f;
    error.clear();
    require(!verifyLightmapAtlasDds(corrupted, 0u, 1u, 1u, 8u, directory, &error) &&
                error.find("texel mismatch") != std::string::npos,
            "lightmap DDS verify did not reject a mismatched texel");

    std::printf("sh atlas dds verify test OK: samples=%zu mapped=%zu\n", loaded.lightmapData.size(), mappedCount);
    return 0;
}

int inspectLightmap2DOutput(const std::filesystem::path& path) {
    const Lightmap2DOutput output = readLightmap2DOutputFile(path);
    std::printf("%s: job=%08X%08X%08X%08X mesh=%08X%08X%08X%08X size=%dx%d samples=%zu lights=%zu\n",
                path.filename().string().c_str(), output.header.jobId.a, output.header.jobId.b, output.header.jobId.c,
                output.header.jobId.d, output.header.meshId.a, output.header.meshId.b, output.header.meshId.c,
                output.header.meshId.d, output.header.size.x, output.header.size.y, output.lightmapData.size(),
                output.lights.size());
    std::printf("  jobInputIndex=%d prtHQ=%u prtLQ=%u static=%u executionTime=%.6f\n", output.jobInputIndex,
                output.bHasPrtHqTextures, output.bHasPrtLqTextures, output.bHasStaticTextures,
                output.header.executionTime);
    return 0;
}

int inspectSdfShadowOutput(const std::filesystem::path& path) {
    const SdfShadowOutput output = readSdfShadowOutputFile(path);
    std::printf("%s: job=%08X%08X%08X%08X mesh=%08X%08X%08X%08X size=%dx%d maps=%zu executionTime=%.6f\n",
                path.filename().string().c_str(), output.header.jobId.a, output.header.jobId.b, output.header.jobId.c,
                output.header.jobId.d, output.header.meshId.a, output.header.meshId.b, output.header.meshId.c,
                output.header.meshId.d, output.header.size.x, output.header.size.y, output.shadowMaps.size(),
                output.header.executionTime);
    for (std::size_t index = 0; index < output.shadowMaps.size(); ++index) {
        const SdfShadowMap& map = output.shadowMaps[index];
        std::size_t mapped = 0;
        float minDistance = std::numeric_limits<float>::max();
        float maxDistance = std::numeric_limits<float>::lowest();
        for (const SdfShadowSample& sample : map.shadowData) {
            if (sample.isMapped != 0u) {
                ++mapped;
            }
            minDistance = sample.distance < minDistance ? sample.distance : minDistance;
            maxDistance = sample.distance > maxDistance ? sample.distance : maxDistance;
        }
        if (map.shadowData.empty()) {
            minDistance = 0.0f;
            maxDistance = 0.0f;
        }
        std::printf("  light[%zu]=%08X%08X%08X%08X samples=%zu mapped=%zu distance=[%.6f, %.6f]\n", index,
                    map.lightId.a, map.lightId.b, map.lightId.c, map.lightId.d, map.shadowData.size(), mapped,
                    minDistance, maxDistance);
    }
    return 0;
}

uint32_t parseUint32Argument(const char* text, const char* name);

int writeLightmapDdsCommand(int argc, char** argv) {
    if (argc != 7) {
        throw std::runtime_error("--write-lightmap-dds requires <file.lmgz> <out-dir> <atlas-size> <x> <y>");
    }
    const Lightmap2DOutput output = readLightmap2DOutputFile(argv[2]);
    if (output.header.size.x <= 0 || output.header.size.y <= 0) {
        throw std::runtime_error("lightmap result has an invalid size");
    }
    const uint32_t width = static_cast<uint32_t>(output.header.size.x);
    const uint32_t height = static_cast<uint32_t>(output.header.size.y);
    if (output.lightmapData.size() != static_cast<std::size_t>(width) * height) {
        throw std::runtime_error("lightmap result sample count does not match its size");
    }
    const uint32_t atlasSize = parseUint32Argument(argv[4], "atlas size");
    const uint32_t targetX = parseUint32Argument(argv[5], "target x");
    const uint32_t targetY = parseUint32Argument(argv[6], "target y");
    if (atlasSize == 0u || static_cast<uint64_t>(targetX) + width > atlasSize ||
        static_cast<uint64_t>(targetY) + height > atlasSize) {
        throw std::runtime_error("lightmap DDS target rect is outside the atlas");
    }

    LightmapAtlasImageSet images;
    images.atlases.resize(1u);
    LightmapAtlasImage& image = images.atlases[0];
    image.size = atlasSize;
    const std::size_t pixels = static_cast<std::size_t>(atlasSize) * atlasSize;
    for (std::vector<uint16_t>& channel : image.sh) {
        channel.assign(pixels * 4u, uint16_t{0});
    }
    image.mapped.assign(pixels, uint8_t{0});
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t source = static_cast<std::size_t>(y) * width + x;
            const std::size_t target = static_cast<std::size_t>(targetY + y) * atlasSize + (targetX + x);
            const Lightmap2DSample& sample = output.lightmapData[source];
            for (std::size_t channel = 0; channel < 3u; ++channel) {
                const Float4 value = lightmapAtlasTexel(sample, channel);
                image.sh[channel][target * 4u + 0u] = bx::halfFromFloat(value.x);
                image.sh[channel][target * 4u + 1u] = bx::halfFromFloat(value.y);
                image.sh[channel][target * 4u + 2u] = bx::halfFromFloat(value.z);
                image.sh[channel][target * 4u + 3u] = bx::halfFromFloat(value.w);
            }
            image.mapped[target] = sample.bIsMapped != 0u ? uint8_t{1} : uint8_t{0};
        }
    }

    std::string error;
    if (!writeLightmapAtlasDds(images, argv[3], &error)) {
        throw std::runtime_error(error);
    }
    std::printf("wrote lightmap DDS: atlas=%u rect=%u,%u size=%ux%u\n", atlasSize, targetX, targetY, width,
                height);
    return 0;
}

int writeTileAtlasCommand(int argc, char** argv) {
    if (argc < 8 || ((argc - 5) % 3) != 0) {
        throw std::runtime_error("--write-tile-atlas requires <out-dir> <atlas-size> <edge-padding> "
                                 "<file.lmgz> <x> <y> ...");
    }
    const uint32_t atlasSize = parseUint32Argument(argv[3], "atlas size");
    const uint32_t edgePadding = parseUint32Argument(argv[4], "edge padding");
    const std::size_t count = static_cast<std::size_t>((argc - 5) / 3);
    std::vector<Lightmap2DOutput> outputs;
    outputs.reserve(count);
    SceneInfo scene;
    BakingJobInputs jobs;
    std::vector<LightmapAtlasResult> results;
    results.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const int base = 5 + static_cast<int>(index) * 3;
        outputs.push_back(readLightmap2DOutputFile(argv[base]));
        Lightmap2DOutput& output = outputs.back();
        output.header.jobId = Guid{0u, 0u, static_cast<uint32_t>(index), 5u};
        output.header.meshId = Guid{0x4D455348u, 0u, 0u, static_cast<uint32_t>(index + 1u)};
        if (output.header.size.x <= 0 || output.header.size.y <= 0 ||
            output.lightmapData.size() !=
                static_cast<std::size_t>(output.header.size.x) * static_cast<std::size_t>(output.header.size.y)) {
            throw std::runtime_error("lightmap result has an invalid size");
        }
        MeshInstanceInfo instance;
        instance.header.guid = output.header.meshId;
        scene.meshInstances.push_back(instance);
        Lightmap2DInput input;
        input.jobId = output.header.jobId;
        input.meshId = output.header.meshId;
        input.size = output.header.size;
        jobs.lightmap2DJobs.push_back(input);
        const uint32_t x = parseUint32Argument(argv[base + 1], "target x");
        const uint32_t y = parseUint32Argument(argv[base + 2], "target y");
        results.push_back(LightmapAtlasResult{&outputs.back(), LightmapAtlasPlacement{
            0u, x, y, static_cast<uint32_t>(output.header.size.x), static_cast<uint32_t>(output.header.size.y)}});
    }
    std::error_code directoryError;
    std::filesystem::create_directories(argv[2], directoryError);
    LightmapAtlasImageSet images;
    LightmapAtlasOptions options;
    options.edgePadding = edgePadding;
    std::string error;
    if (!buildLightmapAtlasImages(scene, jobs, results, {atlasSize}, &images, &error, options)) {
        throw std::runtime_error(error);
    }
    if (!writeLightmapAtlasDds(images, argv[2], &error)) {
        throw std::runtime_error(error);
    }
    std::printf("wrote tile atlas: atlas=%u edgePadding=%u blocks=%zu\n", atlasSize, edgePadding, count);
    return 0;
}

bool buildJobAtlasResults(const SceneInfo& scene, const BakingJobInputs& jobs, const LightmapSidecar& sidecar,
                          const std::vector<Lightmap2DOutput>& outputs,
                          std::vector<LightmapAtlasResult>* results, std::string* error) {
    if (sidecar.atlases.empty()) {
        if (error != nullptr) {
            *error = "lightmap sidecar has no atlases";
        }
        return false;
    }
    if (jobs.lightmap2DJobs.size() != sidecar.objects.size()) {
        if (error != nullptr) {
            *error = "lightmap sidecar object count " + std::to_string(sidecar.objects.size()) +
                     " does not match lightmap job count " + std::to_string(jobs.lightmap2DJobs.size());
        }
        return false;
    }
    if (scene.meshInstances.size() != jobs.lightmap2DJobs.size()) {
        if (error != nullptr) {
            *error = "scene mesh instance count " + std::to_string(scene.meshInstances.size()) +
                     " does not match lightmap job count " + std::to_string(jobs.lightmap2DJobs.size());
        }
        return false;
    }

    results->clear();
    results->reserve(jobs.lightmap2DJobs.size());
    for (std::size_t index = 0; index < jobs.lightmap2DJobs.size(); ++index) {
        const Lightmap2DInput& input = jobs.lightmap2DJobs[index];
        const Lightmap2DOutput& output = outputs[index];
        const MeshInstanceInfo& instance = scene.meshInstances[index];
        if (formatGuid(input.meshId) != formatGuid(instance.header.guid)) {
            if (error != nullptr) {
                *error = "lightmap job " + std::to_string(index) + " mesh id " + formatGuid(input.meshId) +
                         " does not match mesh instance " + formatGuid(instance.header.guid);
            }
            return false;
        }
        if (input.size.x <= 0 || input.size.y <= 0 || output.header.size.x <= 0 ||
            output.header.size.y <= 0) {
            if (error != nullptr) {
                *error = "lightmap job " + std::to_string(index) + " has an invalid size";
            }
            return false;
        }
        if (instance.header.uv2MapSize.x != 0u || instance.header.uv2MapSize.y != 0u) {
            if (instance.header.uv2MapSize.x != static_cast<uint32_t>(input.size.x) ||
                instance.header.uv2MapSize.y != static_cast<uint32_t>(input.size.y)) {
                if (error != nullptr) {
                    *error = "lightmap job " + std::to_string(index) + " UV2 map size " +
                             std::to_string(instance.header.uv2MapSize.x) + "x" +
                             std::to_string(instance.header.uv2MapSize.y) + " does not match input size " +
                             std::to_string(input.size.x) + "x" + std::to_string(input.size.y);
                }
                return false;
            }
        }
        if (output.header.size.x != output.header.size.y ||
            output.header.size.x != input.size.x + ((input.flags & kLightmapFlagPadding) != 0u ? 2 : 0) ||
            output.header.size.y != input.size.y + ((input.flags & kLightmapFlagPadding) != 0u ? 2 : 0)) {
            if (error != nullptr) {
                *error = "lightmap job " + std::to_string(index) + " result size " +
                         std::to_string(output.header.size.x) + "x" + std::to_string(output.header.size.y) +
                         " does not match input size";
            }
            return false;
        }

        const uint32_t sidecarSize = static_cast<uint32_t>(input.size.x);
        const auto matchesObject = [&instance, sidecarSize](const LightmapSidecarObject& object) {
            return object.x == instance.header.uv2Offset.x && object.y == instance.header.uv2Offset.y &&
                   object.size == sidecarSize;
        };

        const LightmapSidecarObject* matchedObject = nullptr;
        if (index < sidecar.objects.size() && matchesObject(sidecar.objects[index])) {
            matchedObject = &sidecar.objects[index];
        }
        if (matchedObject == nullptr) {
            for (const LightmapSidecarObject& object : sidecar.objects) {
                if (!matchesObject(object)) {
                    continue;
                }
                if (matchedObject != nullptr) {
                    if (error != nullptr) {
                        *error = "lightmap job " + std::to_string(index) + " matches multiple sidecar objects (" +
                                 matchedObject->name + ", " + object.name +
                                 "); sidecar object order does not match lightmap job order";
                    }
                    return false;
                }
                matchedObject = &object;
            }
        }
        if (matchedObject == nullptr) {
            if (error != nullptr) {
                *error = "no sidecar object matches lightmap job " + std::to_string(index) + " at " +
                         std::to_string(instance.header.uv2Offset.x) + "," +
                         std::to_string(instance.header.uv2Offset.y) + " size " +
                         std::to_string(sidecarSize);
            }
            return false;
        }
        if (matchedObject->atlas >= sidecar.atlases.size()) {
            if (error != nullptr) {
                *error = "sidecar object " + matchedObject->name + " references an invalid atlas";
            }
            return false;
        }
        results->push_back(LightmapAtlasResult{&output, LightmapAtlasPlacement{
            matchedObject->atlas, matchedObject->x, matchedObject->y,
            static_cast<uint32_t>(input.size.x), static_cast<uint32_t>(input.size.y)}});
    }
    return true;
}

bool writeJobAtlasFromDirectory(const std::filesystem::path& jobDirectory, const std::string& guid,
                                const std::filesystem::path& sidecarPath,
                                const std::filesystem::path& outputDirectory, uint32_t edgePadding,
                                std::size_t* atlasCount, std::size_t* blockCount, std::string* error) {
    try {
        if (jobDirectory.empty()) {
            throw std::runtime_error("job atlas input directory is empty");
        }
        if (outputDirectory.empty()) {
            throw std::runtime_error("job atlas output directory is empty");
        }
        if (sidecarPath.empty()) {
            throw std::runtime_error("job atlas sidecar path is empty");
        }
        const BakingJobInputs jobs =
            readBakingJobInputsFile(findJobObjectFile(jobDirectory, guid, ".jobs", ".jobgz"));
        const SceneInfo scene = readSceneFile(findJobObjectFile(jobDirectory, guid, ".scene", ".scenegz"));
        const LightmapSidecar sidecar = readLightmapSidecar(sidecarPath);

        std::vector<Lightmap2DOutput> outputs;
        std::vector<std::size_t> splitCounts;
        if (!loadLightmap2DOutputs(jobDirectory, jobs, guid, &outputs, &splitCounts, error)) {
            return false;
        }
        std::vector<LightmapAtlasResult> results;
        if (!buildJobAtlasResults(scene, jobs, sidecar, outputs, &results, error)) {
            return false;
        }
        std::vector<uint32_t> atlasSizes;
        atlasSizes.reserve(sidecar.atlases.size());
        for (const LightmapSidecarAtlas& atlas : sidecar.atlases) {
            atlasSizes.push_back(atlas.size);
        }
        LightmapAtlasImageSet images;
        LightmapAtlasOptions options;
        options.edgePadding = edgePadding;
        if (!buildLightmapAtlasImages(scene, jobs, results, atlasSizes, &images, error, options)) {
            return false;
        }
        if (!writeLightmapAtlasDds(images, outputDirectory, error)) {
            return false;
        }
        if (atlasCount != nullptr) {
            *atlasCount = images.atlases.size();
        }
        if (blockCount != nullptr) {
            *blockCount = results.size();
        }
        return true;
    } catch (const std::exception& exception) {
        if (error != nullptr) {
            *error = exception.what();
        }
        return false;
    }
}

int writeJobAtlasCommand(int argc, char** argv) {
    if (argc != 6 && argc != 7) {
        throw std::runtime_error("--write-job-atlas requires <job-dir> <guid32hex> <lightmap.json> "
                                 "<out-dir> [edge-padding]");
    }
    if (!isHexGuid(argv[3])) {
        throw std::runtime_error("--write-job-atlas requires a 32-character hexadecimal GUID");
    }
    const uint32_t edgePadding = argc == 7 ? parseUint32Argument(argv[6], "edge padding") : 2u;
    std::size_t atlasCount = 0;
    std::size_t blockCount = 0;
    std::string error;
    if (!writeJobAtlasFromDirectory(argv[2], argv[3], argv[4], argv[5], edgePadding, &atlasCount, &blockCount,
                                    &error)) {
        throw std::runtime_error(error);
    }
    std::printf("wrote job atlas: atlases=%zu blocks=%zu edgePadding=%u\n", atlasCount, blockCount, edgePadding);
    return 0;
}

int verifyJobAtlasCommand(int argc, char** argv) {
    if (argc != 6) {
        throw std::runtime_error("--verify-job-atlas requires <job-dir> <guid32hex> <lightmap.json> <atlas-dir>");
    }
    if (!isHexGuid(argv[3])) {
        throw std::runtime_error("--verify-job-atlas requires a 32-character hexadecimal GUID");
    }
    const BakingJobInputs jobs = readBakingJobInputsFile(findJobObjectFile(argv[2], argv[3], ".jobs", ".jobgz"));
    const SceneInfo scene = readSceneFile(findJobObjectFile(argv[2], argv[3], ".scene", ".scenegz"));
    const LightmapSidecar sidecar = readLightmapSidecar(argv[4]);

    std::vector<Lightmap2DOutput> outputs;
    std::vector<std::size_t> splitCounts;
    std::string error;
    if (!loadLightmap2DOutputs(argv[2], jobs, argv[3], &outputs, &splitCounts, &error)) {
        throw std::runtime_error(error);
    }
    std::vector<LightmapAtlasResult> results;
    if (!buildJobAtlasResults(scene, jobs, sidecar, outputs, &results, &error)) {
        throw std::runtime_error(error);
    }

    std::vector<uint32_t> atlasSizes;
    atlasSizes.reserve(sidecar.atlases.size());
    for (const LightmapSidecarAtlas& atlas : sidecar.atlases) {
        atlasSizes.push_back(atlas.size);
    }
    std::size_t verifiedBlocks = 0u;
    for (std::size_t index = 0; index < results.size(); ++index) {
        const LightmapAtlasPlacement& placement = results[index].placement;
        const uint32_t padding = (jobs.lightmap2DJobs[index].flags & kLightmapFlagPadding) != 0u ? 1u : 0u;
        if (placement.x < padding || placement.y < padding) {
            throw std::runtime_error("job atlas result does not leave room for padding");
        }
        if (!verifyLightmapAtlasDds(outputs[index], placement.atlasIndex, placement.x - padding,
                                    placement.y - padding, atlasSizes[placement.atlasIndex], argv[5], &error)) {
            throw std::runtime_error(error);
        }
        ++verifiedBlocks;
    }
    std::printf("verified job atlas: atlases=%zu blocks=%zu\n", sidecar.atlases.size(), verifiedBlocks);
    return 0;
}

int verifyLightmap2DResultsCommand(const std::filesystem::path& jobDirectory, const std::string& guid) {
    std::string error;
    if (!verifyLightmap2DResultSet(jobDirectory, guid, true, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }
    return 0;
}

uint32_t parseUint32Argument(const char* text, const char* name) {
    if (text == nullptr || text[0] == '\0' || text[0] == '-') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + (text != nullptr ? text : ""));
    }
    std::size_t consumed = 0u;
    unsigned long value = 0u;
    try {
        value = std::stoul(text, &consumed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    if (consumed != std::strlen(text) || value > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<uint32_t>(value);
}

int mergeLightmapSidecarCommand(int argc, char** argv) {
    if (argc != 4 && argc != 5) {
        throw std::runtime_error("--merge-sidecar requires <scene.hdrp.json> <lightmap.json> [<out.hdrp.json>]");
    }
    const std::filesystem::path scene = argv[2];
    const std::filesystem::path lightmap = argv[3];
    const std::filesystem::path output = argc == 5 ? std::filesystem::path(argv[4]) : scene;
    mergeLightmapSidecar(scene, lightmap, output);
    std::cout << "merged " << lightmap.string() << " into " << output.string() << "\n";
    return 0;
}

int verifyLightmapAtlasDdsCommand(int argc, char** argv) {
    if (argc != 8) {
        throw std::runtime_error("--verify-sh-atlas-dds requires <file.lmgz> <dds-dir> <atlas-index> <target-x> "
                                 "<target-y> <atlas-size>");
    }
    const std::filesystem::path resultPath = argv[2];
    const std::filesystem::path directory = argv[3];
    const uint32_t atlasIndex = parseUint32Argument(argv[4], "atlas index");
    const uint32_t targetX = parseUint32Argument(argv[5], "target x");
    const uint32_t targetY = parseUint32Argument(argv[6], "target y");
    const uint32_t atlasSize = parseUint32Argument(argv[7], "atlas size");

    const Lightmap2DOutput output = readLightmap2DOutputFile(resultPath);
    std::string error;
    if (!verifyLightmapAtlasDds(output, atlasIndex, targetX, targetY, atlasSize, directory, &error)) {
        std::cerr << "error: " << error << "\n";
        return 1;
    }
    std::size_t mapped = 0u;
    for (const Lightmap2DSample& sample : output.lightmapData) {
        mapped += sample.bIsMapped != 0u ? 1u : 0u;
    }
    std::printf("sh atlas DDS verify OK: %s job=%s samples=%zu mapped=%zu\n", resultPath.filename().string().c_str(),
                formatGuid(output.header.jobId).c_str(), output.lightmapData.size(), mapped);
    return 0;
}

int lightmapSplitTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    std::vector<Lightmap2DInput> jobs(2);
    jobs[0].jobId = Guid{0u, 0u, 0u, 5u};
    jobs[0].size = Int2{128, 128};
    jobs[1].jobId = Guid{0u, 0u, 1u, 5u};
    jobs[1].size = Int2{64, 64};
    std::vector<MappingSplitInput> splits = splitOversizedLightmaps(jobs, 64u);
    require(splits.size() == 9u, "128x128 lightmap should split into 9 tiles");
    const int32_t expectedOffsets[3] = {0, 58, 116};
    for (std::size_t index = 0; index < splits.size(); ++index) {
        const MappingSplitInput& split = splits[index];
        require(split.splitSize == 64 && split.overlapSize == 3 && split.scenePartitionIndex == 0,
                "split size/overlap mismatch");
        require(split.jobId.a == 0u && split.jobId.b == 103u && split.jobId.c == index && split.jobId.d == 0u,
                "split JobID does not match original deterministic ordering");
        require(split.mappingOffsets.x == expectedOffsets[index / 3u] &&
                    split.mappingOffsets.y == expectedOffsets[index % 3u] && split.mappingOffsets.z == 0,
                "split mapping offset mismatch");
    }

    jobs[0].size = Int2{63, 63};
    jobs[0].flags |= kLightmapFlagPadding;
    jobs[1].size = Int2{32, 32};
    splits = splitOversizedLightmaps(jobs, 64u);
    require(splits.size() == 4u, "63x63 + padding lightmap should split into 4 tiles");
    require(splits[0].mappingOffsets.x == 0 && splits[0].mappingOffsets.y == 0 &&
                splits[1].mappingOffsets.x == 0 && splits[1].mappingOffsets.y == 58 &&
                splits[2].mappingOffsets.x == 58 && splits[2].mappingOffsets.y == 0 &&
                splits[3].mappingOffsets.x == 58 && splits[3].mappingOffsets.y == 58,
            "padded split mapping offset mismatch");

    Lightmap2DInput input;
    input.jobId = Guid{7u, 8u, 9u, 10u};
    input.meshId = Guid{11u, 12u, 13u, 14u};
    input.size = Int2{6, 4};
    std::vector<MappingSplitInput> mergeSplits(2);
    mergeSplits[0].jobId = Guid{0u, 103u, 0u, 0u};
    mergeSplits[0].splitSize = 4;
    mergeSplits[0].overlapSize = 1;
    mergeSplits[0].mappingOffsets = Int3{0, 0, 0};
    mergeSplits[1].jobId = Guid{0u, 103u, 1u, 0u};
    mergeSplits[1].splitSize = 4;
    mergeSplits[1].overlapSize = 1;
    mergeSplits[1].mappingOffsets = Int3{2, 0, 0};
    std::vector<Lightmap2DOutput> splitOutputs(2);
    for (std::size_t tile = 0; tile < splitOutputs.size(); ++tile) {
        splitOutputs[tile].header.jobId = mergeSplits[tile].jobId;
        splitOutputs[tile].header.meshId = input.meshId;
        splitOutputs[tile].header.size = Int2{4, 4};
        splitOutputs[tile].header.executionTime = static_cast<double>(tile + 1u);
        splitOutputs[tile].lightmapData.resize(16);
        for (int32_t y = 0; y < 4; ++y) {
            for (int32_t x = 0; x < 4; ++x) {
                splitOutputs[tile].lightmapData[static_cast<std::size_t>(y * 4 + x)].incidentLighting.x =
                    static_cast<float>(tile * 1000 + x * 10 + y);
                splitOutputs[tile].lightmapData[static_cast<std::size_t>(y * 4 + x)].bIsMapped = 1u;
            }
        }
    }
    std::vector<const MappingSplitInput*> splitPointers = {&mergeSplits[0], &mergeSplits[1]};
    std::vector<const Lightmap2DOutput*> outputPointers = {&splitOutputs[0], &splitOutputs[1]};
    Lightmap2DOutput merged;
    std::string error;
    require(mergeLightmapSplitResults(input, splitPointers, outputPointers, &merged, &error),
            "split lightmap merge failed");
    require(merged.header.size.x == 6 && merged.header.size.y == 4 && merged.lightmapData.size() == 24,
            "merged lightmap size mismatch");
    for (int32_t y = 0; y < 4; ++y) {
        for (int32_t x = 0; x < 6; ++x) {
            const float expected = x < 3 ? static_cast<float>(x * 10 + y)
                                         : static_cast<float>(1000 + (x - 2) * 10 + y);
            require(merged.lightmapData[static_cast<std::size_t>(y * 6 + x)].incidentLighting.x == expected,
                    "merged split texel mismatch");
        }
    }
    require(merged.header.executionTime == 3.0, "merged split execution time should sum");

    Lightmap2DOutput incomplete;
    require(!mergeLightmapSplitResults(input, {&mergeSplits[0]}, {&splitOutputs[0]}, &incomplete, &error) &&
                error.find("do not cover") != std::string::npos,
            "incomplete split coverage was accepted");
    Lightmap2DOutput wrongJob = splitOutputs[1];
    wrongJob.header.jobId = Guid{1u, 2u, 3u, 4u};
    require(!mergeLightmapSplitResults(input, splitPointers, {&splitOutputs[0], &wrongJob}, &incomplete, &error) &&
                error.find("JobID") != std::string::npos,
            "split JobID mismatch was accepted");

    std::printf("lightmap split test OK: tiles=%zu merged=%dx%d\n", splits.size(), merged.header.size.x,
                merged.header.size.y);
    return 0;
}

int lightmapJobAtlasTest() {
    const auto require = [](bool condition, const char* message) {
        if (!condition) {
            throw std::runtime_error(message);
        }
    };
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("hbg_dawnbake_job_atlas_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(directory);
    std::filesystem::create_directories(directory);
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{directory};

    std::string error;
    const std::string guid = "11223344556677889900AABBCCDDEEFF";
    SceneInfo scene;
    BakingJobInputs jobs;
    LightmapSidecar sidecar;
    sidecar.atlases = {LightmapSidecarAtlas{"lm0", 32u, 3u, "rgba16f"},
                       LightmapSidecarAtlas{"lm1", 32u, 3u, "rgba16f"}};
    const auto makeJob = [](uint32_t index, uint32_t size, uint32_t x, uint32_t y) {
        return std::array<uint32_t, 4>{index, size, x, y};
    };
    const std::array<std::array<uint32_t, 4>, 3> rectangles = {
        makeJob(0u, 4u, 2u, 2u), makeJob(1u, 6u, 8u, 2u), makeJob(2u, 4u, 16u, 2u)};
    for (const auto& rectangle : rectangles) {
        const uint32_t index = rectangle[0];
        const uint32_t size = rectangle[1];
        MeshInstanceInfo instance;
        instance.header.guid = Guid{0x4D455348u, 0u, 0u, index + 1u};
        instance.header.lightmapId = index;
        instance.header.uv2Offset = UInt2{rectangle[2], rectangle[3]};
        instance.header.uv2MapSize = UInt2{size, size};
        scene.meshInstances.push_back(instance);

        Lightmap2DInput input;
        input.jobId = Guid{0u, 0u, index, 5u};
        input.meshId = instance.header.guid;
        input.size = Int2{static_cast<int32_t>(size), static_cast<int32_t>(size)};
        input.flags = kLightmapFlagIndirect;
        if (index == 2u) {
            input.flags |= kLightmapFlagPadding;
        }
        jobs.lightmap2DJobs.push_back(input);

        sidecar.objects.push_back(LightmapSidecarObject{index == 0u ? "z0" : (index == 1u ? "a1" : "p2"),
                                                        index == 1u ? 1u : 0u, rectangle[2], rectangle[3], size,
                                                        1.0f, true});
    }

    uint32_t splitIndex = 0u;
    for (uint32_t jobIndex = 1u; jobIndex <= 2u; ++jobIndex) {
        for (int32_t y = 0; y < 2; ++y) {
            for (int32_t x = 0; x < 2; ++x) {
                MappingSplitInput split;
                split.jobId = Guid{0u, 103u, splitIndex++, jobIndex};
                split.splitSize = 4;
                split.overlapSize = 1;
                split.mappingOffsets = Int3{x * 2, y * 2, static_cast<int32_t>(jobIndex)};
                jobs.mappingSplitJobs.push_back(split);
            }
        }
    }
    jobs.mappingSplitJobsCount = static_cast<uint32_t>(jobs.mappingSplitJobs.size());
    jobs.partialJobsTotalNum.numMappingSplitJobs = jobs.mappingSplitJobsCount;

    const PackageFiles files = writeDawnPackage(directory, guid, scene, jobs, &sidecar);
    const LightmapSidecar readBack = readLightmapSidecar(files.lightmap);
    require(readBack.objects.size() == 3u && readBack.objects[0].name == "z0" &&
                readBack.objects[1].name == "a1" && readBack.objects[2].name == "p2",
            "lightmap sidecar object order was not preserved");

    SceneInfo duplicateScene;
    BakingJobInputs duplicateJobs;
    for (uint32_t index = 0; index < 2u; ++index) {
        MeshInstanceInfo instance;
        instance.header.guid = Guid{0x4455504Cu, 0u, 0u, index + 1u};
        instance.header.lightmapId = index;
        instance.header.uv2Offset = UInt2{2u, 2u};
        instance.header.uv2MapSize = UInt2{4u, 4u};
        duplicateScene.meshInstances.push_back(instance);
        Lightmap2DInput input;
        input.jobId = Guid{0u, 0u, index, 5u};
        input.meshId = instance.header.guid;
        input.size = Int2{4, 4};
        duplicateJobs.lightmap2DJobs.push_back(input);
    }
    std::vector<Lightmap2DOutput> duplicateOutputs(2u);
    for (Lightmap2DOutput& output : duplicateOutputs) {
        output.header.size = Int2{4, 4};
    }
    LightmapSidecar duplicateSidecar;
    duplicateSidecar.atlases = {LightmapSidecarAtlas{"lm0", 32u, 3u, "rgba16f"},
                                LightmapSidecarAtlas{"lm1", 32u, 3u, "rgba16f"}};
    duplicateSidecar.objects = {LightmapSidecarObject{"z0", 0u, 2u, 2u, 4u, 1.0f, true},
                                LightmapSidecarObject{"a1", 1u, 2u, 2u, 4u, 1.0f, true}};
    std::vector<LightmapAtlasResult> duplicateResults;
    require(buildJobAtlasResults(duplicateScene, duplicateJobs, duplicateSidecar, duplicateOutputs,
                                 &duplicateResults, &error),
            "duplicate sidecar placement mapping failed");
    require(duplicateResults.size() == 2u && duplicateResults[0].placement.atlasIndex == 0u &&
                duplicateResults[1].placement.atlasIndex == 1u,
            "multi-atlas duplicate placement mapped to the wrong atlas");

    SceneInfo paddedScene;
    MeshInstanceInfo paddedInstance;
    paddedInstance.header.guid = Guid{0x50444444u, 0u, 0u, 7u};
    paddedInstance.header.uv2Offset = UInt2{2u, 2u};
    paddedInstance.header.uv2MapSize = UInt2{4u, 4u};
    paddedScene.meshInstances.push_back(paddedInstance);
    BakingJobInputs paddedJobs;
    Lightmap2DInput paddedInput;
    paddedInput.jobId = Guid{0u, 0u, 7u, 5u};
    paddedInput.meshId = paddedInstance.header.guid;
    paddedInput.size = Int2{4, 4};
    paddedInput.flags = kLightmapFlagIndirect | kLightmapFlagPadding;
    paddedJobs.lightmap2DJobs.push_back(paddedInput);
    LightmapSidecar paddedSidecar;
    paddedSidecar.atlases = {LightmapSidecarAtlas{"lm0", 32u, 3u, "rgba16f"}};
    paddedSidecar.objects = {LightmapSidecarObject{"padded", 0u, 2u, 2u, 4u, 1.0f, true}};
    std::vector<Lightmap2DOutput> paddedOutputs(1u);
    paddedOutputs[0].header.jobId = paddedInput.jobId;
    paddedOutputs[0].header.meshId = paddedInput.meshId;
    paddedOutputs[0].header.size = Int2{6, 6};
    paddedOutputs[0].lightmapData.resize(36u);
    std::vector<LightmapAtlasResult> paddedResults;
    require(buildJobAtlasResults(paddedScene, paddedJobs, paddedSidecar, paddedOutputs, &paddedResults, &error),
            "padded job atlas result mapping failed");
    require(paddedResults.size() == 1u && paddedResults[0].placement.width == 4u &&
                paddedResults[0].placement.height == 4u,
            "padded job atlas placement must use the unpadded lightmap size");

    Lightmap2DOutput first;
    first.header.jobId = jobs.lightmap2DJobs[0].jobId;
    first.header.meshId = jobs.lightmap2DJobs[0].meshId;
    first.header.size = Int2{4, 4};
    first.lightmapData.resize(16u);
    for (std::size_t index = 0; index < first.lightmapData.size(); ++index) {
        first.lightmapData[index].incidentLighting = Float4{1.0f, 2.0f, 3.0f, 4.0f};
        first.lightmapData[index].shVectorR = Float4{5.0f, 6.0f, 7.0f, 8.0f};
        first.lightmapData[index].bIsMapped = 1u;
    }
    Writer writer;
    writeLightmap2DOutput(writer, first);
    writeGzipFile(directory / lightmapResultFileName(first.header.jobId), writer.take());

    for (const MappingSplitInput& jobSplit : jobs.mappingSplitJobs) {
        Lightmap2DOutput tile;
        tile.header.jobId = jobSplit.jobId;
        tile.header.meshId = jobs.lightmap2DJobs[static_cast<std::size_t>(jobSplit.mappingOffsets.z)].meshId;
        tile.header.size = Int2{4, 4};
        tile.lightmapData.resize(16u);
        for (int32_t y = 0; y < 4; ++y) {
            for (int32_t x = 0; x < 4; ++x) {
                const std::size_t index = static_cast<std::size_t>(y * 4 + x);
                tile.lightmapData[index].incidentLighting =
                    Float4{static_cast<float>(jobSplit.mappingOffsets.x * 100 + jobSplit.mappingOffsets.y * 10 +
                                              x * 2 + y),
                           0.0f, 0.0f, 0.0f};
                tile.lightmapData[index].bIsMapped = 1u;
            }
        }
        Writer tileWriter;
        writeLightmap2DOutput(tileWriter, tile);
        writeGzipFile(directory / lightmapResultFileName(tile.header.jobId), tileWriter.take());
    }

    const std::filesystem::path atlasDirectory = directory / "atlas";
    std::vector<std::string> arguments = {"hbg_dawnbake", "--write-job-atlas", directory.string(), guid,
                                          files.lightmap.string(), atlasDirectory.string(), "0"};
    std::vector<char*> argumentPointers;
    argumentPointers.reserve(arguments.size());
    for (std::string& argument : arguments) {
        argumentPointers.push_back(argument.data());
    }
    require(writeJobAtlasCommand(static_cast<int>(argumentPointers.size()), argumentPointers.data()) == 0,
            "job atlas command failed");

    std::vector<Lightmap2DOutput> outputs;
    std::vector<std::size_t> splitCounts;
    require(loadLightmap2DOutputs(directory, jobs, guid, &outputs, &splitCounts, &error),
            "job atlas split result load failed");
    require(splitCounts.size() == 3u && splitCounts[0] == 0u && splitCounts[1] == 4u &&
                splitCounts[2] == 4u,
            "job atlas split count mismatch");
    require(outputs[1].header.size.x == 6 && outputs[1].header.size.y == 6 &&
                outputs[1].lightmapData[5].incidentLighting.x == 206.0f,
            "job atlas split merge mismatch");
    require(outputs[2].header.size.x == 6 && outputs[2].header.size.y == 6,
            "padded split merge must keep the padded extent");
    require(verifyLightmapAtlasDds(outputs[0], 0u, 2u, 2u, 32u, atlasDirectory, &error),
            "job atlas first block verification failed");
    require(verifyLightmapAtlasDds(outputs[1], 1u, 8u, 2u, 32u, atlasDirectory, &error),
            "job atlas split block verification failed");
    require(verifyLightmapAtlasDds(outputs[2], 0u, 15u, 1u, 32u, atlasDirectory, &error),
            "padded job atlas block verification failed");
    arguments = {"hbg_dawnbake", "--verify-job-atlas", directory.string(), guid, files.lightmap.string(),
                 atlasDirectory.string()};
    argumentPointers.clear();
    for (std::string& argument : arguments) {
        argumentPointers.push_back(argument.data());
    }
    require(verifyJobAtlasCommand(static_cast<int>(argumentPointers.size()), argumentPointers.data()) == 0,
            "job atlas verification command failed");

    DawnLaunchOptions bakeOptions;
    bakeOptions.jobDirectory = directory;
    bakeOptions.atlasOutputDirectory = directory / "post_bake_atlas";
    require(completeDawnBake(bakeOptions, guid) == 0, "post-bake atlas generation failed");
    require(std::filesystem::is_regular_file(bakeOptions.atlasOutputDirectory / "lm0_sh0.dds") &&
                std::filesystem::is_regular_file(bakeOptions.atlasOutputDirectory / "lm1_sh2.dds"),
            "post-bake atlas output missing");
    std::printf("job atlas test OK: atlases=2 blocks=3 splitTiles=8\n");
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            printUsage();
            return 0;
        }
        const std::string command = argv[1];
        if (command == "--help" || command == "-h") {
            printUsage();
            return 0;
        }
        if (command == "--proto-test") {
            return protoTest();
        }
        if (command == "--mesh-test") {
            return meshExportTest();
        }
        if (command == "--material-test") {
            return materialExportTest();
        }
        if (command == "--light-test") {
            return lightExportTest();
        }
        if (command == "--sky-test") {
            return skyExportTest();
        }
#if defined(HBG_DAWN_HAVE_PARCEL) && HBG_DAWN_HAVE_PARCEL
        if (command == "--parcel-test") {
            return parcelImportTest();
        }
#endif
        if (command == "--uv-test") {
            return uvUnwrapTest();
        }
        if (command == "--atlas-test") {
            return atlasTest();
        }
        if (command == "--package-test") {
            return packageTest();
        }
        if (command == "--process-test") {
            return processDriverTest();
        }
        if (command == "--run-dawn" && argc >= 4) {
            return runDawnCommand(argc, argv);
        }
        if (command == "--bake" && argc >= 4) {
            return bakeDawnCommand(argc, argv);
        }
#ifdef HBG_DAWN_GRPC
        if (command == "--agent-upload" && argc == 6) {
            return agentUploadCommand(argc, argv);
        }
        if (command == "--agent-cache-upload") {
            return agentCacheUploadCommand(argc, argv);
        }
        if (command == "--agent-multi-upload") {
            return agentMultiUploadCommand(argc, argv);
        }
        if (command == "--agent-transfer-job") {
            return agentTransferCommand(argc, argv);
        }
        if (command == "--agent-download") {
            return agentDownloadCommand(argc, argv);
        }
        if (command == "--agent-spec-test") {
            return agentSpecTest();
        }
        if (command == "--agent-upload-test") {
            return agentUploadTest();
        }
        if (command == "--agent-cache-test") {
            return agentCacheTest();
        }
        if (command == "--agent-multi-upload-test") {
            return agentMultiUploadTest();
        }
        if (command == "--agent-transfer-test") {
            return agentTransferTest();
        }
        if (command == "--agent-download-test") {
            return agentDownloadTest();
        }
        if (command == "--agent-multi-download-test") {
            return agentMultiDownloadTest();
        }
        if (command == "--agent-download-files-test") {
            return agentDownloadFilesTest();
        }
#else
        if (command == "--agent-upload" || command == "--agent-cache-upload" || command == "--agent-multi-upload" ||
            command == "--agent-transfer-job" || command == "--agent-download" || command == "--agent-spec-test" ||
            command == "--agent-upload-test" ||
            command == "--agent-cache-test" || command == "--agent-multi-upload-test" ||
            command == "--agent-transfer-test" || command == "--agent-download-test" ||
            command == "--agent-multi-download-test" || command == "--agent-download-files-test") {
            throw std::runtime_error("this build has no gRPC Agent support (configure with -DHBG_BUILD_DAWN_GRPC=ON)");
        }
#endif
        if (command == "--result-test" && argc == 3) {
            return resultPollTest(argv[2]);
        }
        if (command == "--job-lifecycle-test") {
            return jobLifecycleTest();
        }
        if (command == "--protocol-version-test") {
            return protocolVersionTest();
        }
        if (command == "--golden-test" && (argc == 2 || argc == 3)) {
            const std::filesystem::path dir =
                argc == 3 ? std::filesystem::path(argv[2]) : std::filesystem::path("tests/dawn/golden/min_scene");
            return goldenStructTest(dir);
        }
        if (command.rfind("--dump-uv=", 0) == 0 && command.size() > 10u) {
            return dumpUvCommand(command.substr(10u));
        }
        if (command == "--dump-uv" && argc == 3) {
            return dumpUvCommand(argv[2]);
        }
        if (command == "--export-scene" || command.rfind("--scene=", 0) == 0) {
            return exportGenericSceneCommand(argc, argv);
        }
        if (command == "--export-min" && (argc == 3 || argc == 4)) {
            const std::string guid = argc == 4 ? argv[3] : "11111111222222223333333344444444";
            return exportPackageCommand(argv[2], guid);
        }
        if (command == "--write-empty" && argc == 4) {
            return writeEmpty(argv[2], argv[3]);
        }
        if (command == "--export-t2" && (argc == 3 || argc == 4)) {
            return exportUvAxisPackage(argv[2], argc == 4 ? argv[3] : "11111111222222223333333344444444");
        }
        if (command == "--export-t1" && (argc == 3 || argc == 4)) {
            return exportDirectionalPackage(argv[2], argc == 4 ? argv[3] : "11111111222222223333333344444444");
        }
        if (command == "--export-falloff" && (argc == 3 || argc == 4)) {
            return exportPointFalloffPackage(argv[2], argc == 4 ? argv[3] : "11111111222222223333333344444444");
        }
        if (command == "--export-t1-color" && (argc == 3 || argc == 4)) {
            return exportDirectionalColorPackage(argv[2], argc == 4 ? argv[3] : "11111111222222223333333344444444");
        }
        if (command == "--export-t3-tile" && (argc == 4 || argc == 5)) {
            const uint32_t tileIndex = parseUint32Argument(argv[3], "tile index");
            return exportSeamTilePackage(argv[2], tileIndex, argc == 5 ? argv[4] : "11111111222222223333333344444444");
        }
        if (command == "--export-t6" && (argc == 3 || argc == 4)) {
            return exportT6Package(argv[2], argc == 4 ? argv[3] : "11111111222222223333333344444444");
        }
        if (command == "--t6-test") {
            return t6PerfTest();
        }
        if (command == "--scene-export-test") {
            return sceneExportTest();
        }
        if (command == "--dawn-config-test" && (argc == 2 || argc == 3)) {
            return dawnConfigTest(argc == 3 ? std::filesystem::path(argv[2]) : defaultDawnConfigPath());
        }
        if (command == "--write-lightmap-dds") {
            return writeLightmapDdsCommand(argc, argv);
        }
        if (command == "--write-min" && argc == 4) {
            return writeMinimal(argv[2], argv[3]);
        }
        if (command == "--roundtrip" && argc == 4) {
            return roundtrip(argv[2], argv[3]);
        }
        if (command == "--lmgz-test") {
            return lightmap2DOutputTest();
        }
        if (command == "--lmgz-bind-test") {
            return lightmap2DBindingTest();
        }
        if (command == "--sdfgz-test") {
            return sdfShadowOutputTest();
        }
        if (command == "--split-test") {
            return lightmapSplitTest();
        }
        if (command == "--sh-atlas-test") {
            return lightmapAtlasImageTest();
        }
        if (command == "--sh-atlas-dds-test") {
            return lightmapAtlasDdsTest();
        }
        if (command == "--sh-atlas-dds-verify-test") {
            return lightmapAtlasDdsVerifyTest();
        }
        if (command == "--sidecar-merge-test") {
            return sidecarMergeTest();
        }
        if (command == "--merge-sidecar" || command == "--merge-lightmap-sidecar") {
            return mergeLightmapSidecarCommand(argc, argv);
        }
        if (command == "--write-tile-atlas") {
            return writeTileAtlasCommand(argc, argv);
        }
        if (command == "--write-job-atlas") {
            return writeJobAtlasCommand(argc, argv);
        }
        if (command == "--verify-job-atlas") {
            return verifyJobAtlasCommand(argc, argv);
        }
        if (command == "--job-atlas-test") {
            return lightmapJobAtlasTest();
        }
        if (command == "--verify-sh-atlas-dds") {
            return verifyLightmapAtlasDdsCommand(argc, argv);
        }
        if (command == "--verify-lmgz" && argc == 4) {
            if (!isHexGuid(argv[3])) {
                throw std::runtime_error("--verify-lmgz requires a 32-character hexadecimal GUID");
            }
            return verifyLightmap2DResultsCommand(argv[2], argv[3]);
        }
        if (command == "--inspect-lmgz" && argc == 3) {
            return inspectLightmap2DOutput(argv[2]);
        }
        if (command == "--inspect-sdfgz" && argc == 3) {
            return inspectSdfShadowOutput(argv[2]);
        }
        if (command == "--verify-sdfgz" && argc == 4) {
            if (!isHexGuid(argv[3])) {
                throw std::runtime_error("--verify-sdfgz requires a 32-character hexadecimal GUID");
            }
            std::string error;
            if (!verifySdfShadowResultSet(argv[2], argv[3], true, &error)) {
                throw std::runtime_error(error);
            }
            return 0;
        }
        if (command == "--inspect" && argc == 3) {
            return inspect(argv[2]);
        }
        printUsage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "error: " << error.what() << "\n";
        return 1;
    }
}
