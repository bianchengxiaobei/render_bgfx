#pragma once

#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/texture_loader.h"
#include "core/bounds.h"
#include "core/math.h"
#include "gfx/texture.h"
#include "render/atmosphere/local_fog_volume.h"
#include "render/scene/decal.h"
#include "render/scene/light.h"
#include "render/scene/reflection_probe.h"
#include "render/scene/render_object.h"
#include "render/river/river_spec.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hbg::render {

struct Camera {
    enum class Projection : uint8_t { Perspective, Orthographic };

    std::string name;
    Projection projection = Projection::Perspective;
    Mat4 world = Mat4::identity();
    float yfov = 1.0471975512f;
    float aspect = 1.7777778f;
    float nearClip = 0.1f;
    float farClip = 1000.0f;
    float xmag = 1.0f;
    float ymag = 1.0f;
};

struct TerrainIndirectSource {
    uint32_t meshIndex = UINT32_MAX;
    uint32_t indexBase = 0;
    uint32_t vertexBase = 0;
};

struct VegetationInstance {
    Mat4 world = Mat4::identity();
    Bounds worldBounds;
    uint32_t objectIndex = UINT32_MAX;
    uint32_t meshIndex = UINT32_MAX;
    uint32_t materialIndex = UINT32_MAX;
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
};

struct VegetationImpostorMaterial {
    uint32_t materialIndex = UINT32_MAX;
    uint32_t species = 0;
};

struct RenderScene {
    std::string sourcePath;
    std::vector<asset::Mesh> meshes;
    std::vector<asset::Material> materials;
    std::vector<asset::ImageAsset> images;
    std::vector<RenderObject> objects;
    std::vector<Light> lights;
    std::vector<LocalFogVolume> localFogVolumes;
    std::vector<Decal> decals;
    std::vector<ReflectionProbe> probes;
    std::vector<Camera> cameras;
    gfx::TextureHandle terrainMaterialTiles;
    gfx::TextureHandle terrainMaterialTileMap;
    gfx::TextureHandle terrainHeight;
    Vec4 terrainWorld{0.0f, 0.0f, 0.0f, 0.0f};
    Vec4 terrainQuadInfo{0.0f, 0.0f, 0.0f, 0.0f};
    Vec4 terrainCamera{0.0f, 0.0f, 0.0f, 0.0f};
    Vec4 terrainMapInfo{0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t terrainIndirectMesh = UINT32_MAX;
    std::vector<TerrainIndirectSource> terrainIndirectSources;
    std::vector<VegetationInstance> vegetation;
    std::vector<VegetationImpostorMaterial> vegetationImpostorMaterials;
    gfx::TextureHandle vegetationAtlas;
    uint32_t vegetationAtlasColumns = 0;
    uint32_t vegetationAtlasRows = 0;
    uint32_t vegetationAtlasLayerSize = 0;
    float vegetationMaxDistance = 5000.0f;
    float vegetationTileSize = 500.0f;
    uint32_t waterMeshIndex = UINT32_MAX;
    uint32_t waterMaterialIndex = UINT32_MAX;
    uint32_t waterObjectIndex = UINT32_MAX;
    bool waterFftEnabled = false;
    float waterFftTileSize = 5488.0f;
    float waterFftAmplitude = 1.0f;
    float waterFftUpdateHz = 12.0f;
    float waterSphereRadius = 0.0f;
    RiverSpec river;
    Bounds bounds;

    void recomputeBounds();
};

} // namespace hbg::render

