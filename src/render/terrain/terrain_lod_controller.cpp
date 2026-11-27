#include "render/terrain/terrain_lod_controller.h"

#include "asset/texture_loader.h"
#include "core/bounds.h"
#include "core/log.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace hbg::render {
namespace {

constexpr std::size_t kHeightmapHeaderSize = 20;

gfx::TextureHandle createVegetationAtlas(gfx::ResourceManager& resources, const std::filesystem::path& path,
                                         uint32_t columns, uint32_t rows, uint32_t& layerSize,
                                         uint32_t& atlasColumns, uint32_t& atlasRows) {
    asset::ImageAsset atlas;
    if (!asset::TextureLoader::loadFromFile(path, atlas, nullptr) || atlas.format != gfx::TextureFormat::RGBA8) {
        HBG_LOGW("vegetation atlas: cannot load '{}'", path.string());
        return {};
    }
    atlasColumns = columns != 0 ? columns : 19;
    atlasRows = rows != 0 ? rows : (362 + atlasColumns - 1) / atlasColumns;
    if (atlas.width % atlasColumns != 0 || atlas.height % atlasRows != 0 ||
        atlas.width / atlasColumns != atlas.height / atlasRows ||
        atlas.width > 65535 || atlas.height > 65535) {
        HBG_LOGW("vegetation atlas: invalid layout {}x{} for {}x{} cells", atlas.width, atlas.height,
                 atlasColumns, atlasRows);
        return {};
    }
    layerSize = atlas.width / atlasColumns;
    const gfx::TextureHandle handle = asset::TextureLoader::createTexture(resources, atlas, false, "trees3D_atlas");
    if (!handle.valid()) {
        HBG_LOGW("vegetation atlas: failed to create texture");
        return {};
    }
    HBG_LOGI("vegetation atlas: {}x{} ({}x{} cells of {}, {} bytes)", atlas.width, atlas.height,
             atlasColumns, atlasRows, layerSize, atlas.pixels.size());
    return handle;
}

asset::Mesh makeSharedTerrainMesh(int gridSize, uint32_t materialIndex, const std::string& name) {
    asset::Mesh mesh;
    mesh.name = name;
    mesh.hasNormals = true;
    mesh.hasTangents = true;
    mesh.hasUv0 = true;

    const uint32_t n = static_cast<uint32_t>(std::clamp(gridSize, 2, 129));
    mesh.vertices.reserve(static_cast<std::size_t>(n) * n + 4u * n);
    const float denominator = static_cast<float>(n - 1);
    for (uint32_t z = 0; z < n; ++z) {
        for (uint32_t x = 0; x < n; ++x) {
            asset::MeshVertex vertex;
            vertex.position = Vec3(static_cast<float>(x) / denominator, 0.0f,
                                   static_cast<float>(z) / denominator);
            vertex.normal = Vec3(0.0f, 1.0f, 0.0f);
            vertex.tangent = Vec4(1.0f, 0.0f, 0.0f, -1.0f);
            vertex.texcoord0 = Vec2(vertex.position.x, vertex.position.z);
            mesh.vertices.push_back(vertex);
        }
    }

    const uint32_t cells = n - 1;
    for (uint32_t z = 0; z < cells; ++z) {
        for (uint32_t x = 0; x < cells; ++x) {
            const uint32_t a = z * n + x;
            const uint32_t b = a + 1;
            const uint32_t c = a + n;
            const uint32_t d = c + 1;
            mesh.indices.insert(mesh.indices.end(), {a, c, b, b, c, d});
        }
    }

    const auto addSkirt = [&](const std::vector<uint32_t>& top, bool south) {
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const uint32_t index : top) {
            asset::MeshVertex vertex = mesh.vertices[index];
            vertex.position.y = 1.0f;
            vertex.texcoord0.y = 2.0f;
            mesh.vertices.push_back(vertex);
        }
        for (std::size_t corner = 0; corner + 1 < top.size(); ++corner) {
            const uint32_t t0 = top[corner];
            const uint32_t t1 = top[corner + 1];
            const uint32_t l0 = base + static_cast<uint32_t>(corner);
            const uint32_t l1 = l0 + 1;
            if (south) {
                mesh.indices.insert(mesh.indices.end(), {t0, t1, l0, t1, l1, l0});
            } else {
                mesh.indices.insert(mesh.indices.end(), {t0, l0, t1, t1, l0, l1});
            }
        }
    };

    std::vector<uint32_t> south(n);
    std::vector<uint32_t> north(n);
    std::vector<uint32_t> west(n);
    std::vector<uint32_t> east(n);
    for (uint32_t index = 0; index < n; ++index) {
        south[index] = index;
        north[index] = (n - 1) * n + index;
        west[index] = index * n;
        east[index] = index * n + n - 1;
    }
    addSkirt(south, true);
    addSkirt(north, false);
    addSkirt(west, false);
    addSkirt(east, false);

    mesh.bounds.min = Vec3(0.0f, 0.0f, 0.0f);
    mesh.bounds.max = Vec3(1.0f, 0.0f, 1.0f);
    mesh.submeshes.push_back(
        {0, static_cast<uint32_t>(mesh.indices.size()), materialIndex, mesh.bounds});
    return mesh;
}

void appendTerrainIndirectSource(const asset::Mesh& source, uint32_t meshIndex, uint32_t materialIndex,
                                 asset::Mesh& combined, std::vector<TerrainIndirectSource>& sources) {
    if (source.vertices.empty() || source.indices.empty()) {
        return;
    }
    const uint32_t vertexBase = static_cast<uint32_t>(combined.vertices.size());
    const uint32_t indexBase = static_cast<uint32_t>(combined.indices.size());
    combined.vertices.insert(combined.vertices.end(), source.vertices.begin(), source.vertices.end());
    combined.indices.reserve(combined.indices.size() + source.indices.size());
    for (const uint32_t index : source.indices) {
        combined.indices.push_back(index + vertexBase);
    }
    combined.submeshes.push_back(
        {indexBase, static_cast<uint32_t>(source.indices.size()), materialIndex, source.bounds});
    sources.push_back({meshIndex, indexBase, vertexBase});
}

} // namespace

TerrainLodController::~TerrainLodController() {
    shutdown();
}

void TerrainLodController::shutdown() {
    m_tree.setHeightRangeProvider({});
    m_tileStreamer.close();
    m_heightmapCache.close();
    m_heightmapSampler.close();
    m_materialTiles.close();
    if (m_resources != nullptr && m_terrainHeightTexture.valid()) {
        m_resources->destroyTexture(m_terrainHeightTexture);
    }
    if (m_resources != nullptr && m_vegetationAtlasTexture.valid()) {
        m_resources->destroyTexture(m_vegetationAtlasTexture);
    }
    m_terrainHeightTexture = {};
    m_vegetationAtlasTexture = {};
    m_resources = nullptr;
    m_sharedGeometry = false;
    m_skirtDepth = 0.0f;
    m_asyncTiles = false;
    m_materialTilesActive = false;
    m_active = false;
    m_groundHeightSampled = false;
    m_lastGroundCamera = Vec3{};
    m_prefetchCameraInitialized = false;
    m_lastPrefetchCamera = Vec3{};
}

std::string TerrainLodController::nodeName(const std::string& prefix, int level, int tx, int ty) {
    return prefix + std::to_string(level) + "_" + std::to_string(tx) + "_" + std::to_string(ty);
}

bool TerrainLodController::load(const std::string& scenePath, RenderScene& scene,
                                gfx::ResourceManager* resources, std::string* error) {
    m_active = false;
    m_asyncTiles = false;
    m_objectByName.clear();
    m_lastError.clear();
    m_tree.setHeightRangeProvider({});
    m_tileStreamer.close();
    m_heightmapCache.close();
    m_heightmapSampler.close();
    m_materialTiles.close();
    if (m_resources != nullptr && m_terrainHeightTexture.valid()) {
        m_resources->destroyTexture(m_terrainHeightTexture);
    }
    if (m_resources != nullptr && m_vegetationAtlasTexture.valid()) {
        m_resources->destroyTexture(m_vegetationAtlasTexture);
    }
    m_terrainHeightTexture = {};
    m_vegetationAtlasTexture = {};
    m_resources = nullptr;
    m_sharedGeometry = false;
    m_skirtDepth = 0.0f;
    m_materialTilesActive = false;
    m_groundHeightSampled = false;
    m_lastGroundCamera = Vec3{};
    m_prefetchCameraInitialized = false;
    m_lastPrefetchCamera = Vec3{};
    scene.terrainHeight = {};
    scene.terrainWorld = Vec4(0.0f, 0.0f, 0.0f, 0.0f);

    std::filesystem::path sidecar(scenePath);
    sidecar.replace_extension(".terrain.json");
    std::ifstream file(sidecar);
    if (!file) {
        return false;
    }

    nlohmann::json json;
    try {
        file >> json;
    } catch (const std::exception& exception) {
        m_lastError = std::string("terrain lod json: ") + exception.what();
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    const float world = json.value("world_size", 50000.0f);
    float ox = -world * 0.5f;
    float oy = -world * 0.5f;
    if (json.contains("origin") && json["origin"].is_array() && json["origin"].size() == 2) {
        ox = json["origin"][0].get<float>();
        oy = json["origin"][1].get<float>();
    }

    m_prefix = json.value("name_prefix", std::string("TerrainLOD_"));
    m_groundHeight = json.value("ground_height", 0.0f);
    m_worldSize = world;
    m_originX = ox;
    m_originZ = oy;
    m_maxLevel = json.value("max_level", 4);
    m_frustumCulling = json.value("frustum_culling", true);
    TerrainQuadTreeSettings settings;
    settings.maxLevel = json.value("max_level", 4);
    settings.splitFactor = json.value("split_factor", 2.0f);
    settings.viewportHeight = json.value("viewport_height", 1080.0f);
    settings.fovYRadians = json.value("fov_y_degrees", 55.0f) * 3.14159265f / 180.0f;
    settings.splitInvisibleQuads = json.value("split_invisible_quads", false);
    settings.horizonCulling = json.value("horizon_culling", true);

    const float zmin = json.value("zmin", 0.0f);
    const float zmax = json.value("zmax", 5000.0f);
    m_tree.clearNodeHeightRanges();
    const auto bounds = json.find("node_bounds");
    if (bounds != json.end() && bounds->is_object()) {
        for (const auto& entry : bounds->items()) {
            if (!entry.value().is_array() || entry.value().size() != 2) {
                continue;
            }
            int level = 0;
            int tx = 0;
            int ty = 0;
            char first = 0;
            char second = 0;
            std::istringstream key(entry.key());
            if (!(key >> level >> first >> tx >> second >> ty) || first != '_' || second != '_') {
                continue;
            }
            const float nodeZmin = entry.value()[0].get<float>();
            const float nodeZmax = entry.value()[1].get<float>();
            if (std::isfinite(nodeZmin) && std::isfinite(nodeZmax)) {
                m_tree.setNodeHeightRange(level, tx, ty, nodeZmin, nodeZmax);
            }
        }
    }

    const std::string heightmapFile = json.value("heightmap", std::string());
    std::filesystem::path heightmapPath;
    float skirtDepth = 0.0f;
    if (!heightmapFile.empty()) {
        heightmapPath = std::filesystem::path(heightmapFile);
        if (heightmapPath.is_relative()) {
            heightmapPath = std::filesystem::path(scenePath).parent_path() / heightmapPath;
        }
        skirtDepth = json.value("skirt_depth", 0.0f);
        m_skirtDepth = skirtDepth;
        const uint32_t tileSize = json.value("heightmap_tile_size", 64u);
        const uint32_t capacity = json.value("heightmap_cache_tiles", 64u);

        std::string samplerError;
        if (m_heightmapSampler.load(heightmapPath, &samplerError)) {
            m_heightmapSampler.setWorld(ox, oy, world, skirtDepth);
        } else {
            HBG_LOGW("terrain camera ground sampler disabled: {}", samplerError);
        }

        std::string streamError;
        if (m_tileStreamer.init(heightmapPath, ox, oy, world, skirtDepth, tileSize, capacity, resources,
                                &streamError)) {
            m_asyncTiles = true;
            m_tree.setHeightRangeProvider(
                [this](int level, int tx, int ty, float& nodeZmin, float& nodeZmax) {
                    return m_tileStreamer.query(level, tx, ty, nodeZmin, nodeZmax);
                });
            HBG_LOGI("terrain heightmap streamer: {}x{} scale={} tile={} capacity={} gpu={}",
                     m_tileStreamer.width(), m_tileStreamer.height(), m_tileStreamer.heightScale(), tileSize,
                     capacity, m_tileStreamer.gpuStorage().valid() ? "true" : "false");
        } else {
            std::string cacheError;
            if (m_heightmapCache.load(heightmapPath, &cacheError)) {
                m_heightmapCache.setWorld(ox, oy, world, skirtDepth);
                m_heightmapCache.setCapacity(capacity);
                m_tree.setHeightRangeProvider(
                    [this](int level, int tx, int ty, float& nodeZmin, float& nodeZmax) {
                        return m_heightmapCache.query(level, tx, ty, nodeZmin, nodeZmax);
                    });
                HBG_LOGW("heightmap tile streamer disabled ({}), using synchronous tile cache", streamError);
            } else {
                HBG_LOGW("heightmap tile cache disabled: {} / {}", streamError, cacheError);
            }
        }
    }

    m_tree.init(ox, oy, world, zmin, zmax, settings);

    for (uint32_t index = 0; index < scene.objects.size(); ++index) {
        const std::string& name = scene.objects[index].name;
        if (name.rfind(m_prefix, 0) != 0) {
            continue;
        }
        int level = 0;
        int tx = 0;
        int ty = 0;
        char first = 0;
        char second = 0;
        std::istringstream stream(name.substr(m_prefix.size()));
        if ((stream >> level >> first >> tx >> second >> ty) && first == '_' && second == '_') {
            m_objectByName.emplace(name, index);
        }
    }

    bool hasWaterObject = false;
    for (const RenderObject& object : scene.objects) {
        if (object.name == "Water") {
            hasWaterObject = true;
            break;
        }
    }
    const bool waterOnly = m_objectByName.empty() && json.value("water_fft", false) && hasWaterObject;
    if (m_objectByName.empty() && !waterOnly) {
        m_lastError = "terrain lod sidecar found but no matching objects";
        if (error != nullptr) {
            *error = m_lastError;
        }
        return false;
    }

    scene.vegetation.clear();
    scene.vegetationImpostorMaterials.clear();
    scene.vegetationAtlas = {};
    scene.vegetationAtlasColumns = 0;
    scene.vegetationAtlasRows = 0;
    scene.vegetationAtlasLayerSize = 0;
    scene.waterMeshIndex = UINT32_MAX;
    scene.waterMaterialIndex = UINT32_MAX;
    scene.waterObjectIndex = UINT32_MAX;
    scene.waterFftEnabled = json.value("water_fft", false);
    scene.waterFftTileSize = json.value("water_fft_tile_size", 5488.0f);
    scene.waterFftAmplitude = json.value("water_fft_amplitude", 1.0f);
    scene.waterFftUpdateHz = json.value("water_fft_update_hz", 12.0f);
    scene.waterSphereRadius = json.value("water_sphere_radius", 0.0f);
    for (uint32_t index = 0; index < scene.objects.size(); ++index) {
        const RenderObject& object = scene.objects[index];
        if (object.name != "Water" || object.meshIndex >= scene.meshes.size()) {
            continue;
        }
        const asset::Mesh& mesh = scene.meshes[object.meshIndex];
        if (mesh.submeshes.empty() ||
            mesh.submeshes.front().materialIndex >= scene.materials.size()) {
            continue;
        }
        scene.waterObjectIndex = index;
        scene.waterMeshIndex = object.meshIndex;
        scene.waterMaterialIndex = mesh.submeshes.front().materialIndex;
        break;
    }
    if (waterOnly) {
        m_active = false;
        HBG_LOGI("terrain lod: water-only scene, radius={:.1f}", scene.waterSphereRadius);
        return true;
    }
    uint32_t treeMesh[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t treeMaterial[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t treeObject[2] = {UINT32_MAX, UINT32_MAX};
    for (uint32_t index = 0; index < scene.objects.size(); ++index) {
        RenderObject& object = scene.objects[index];
        int species = -1;
        if (object.name == "VegetationMesh_Deciduous") {
            species = 0;
        } else if (object.name == "VegetationMesh_Conifer") {
            species = 1;
        }
        if (species < 0) {
            continue;
        }
        object.visible = false;
        treeObject[species] = index;
        treeMesh[species] = object.meshIndex;
        if (object.meshIndex < scene.meshes.size()) {
            const asset::Mesh& mesh = scene.meshes[object.meshIndex];
            if (!mesh.submeshes.empty()) {
                treeMaterial[species] = mesh.submeshes.front().materialIndex;
            }
        }
    }

    std::string vegetationFile = json.value("vegetation", std::string());
    scene.vegetationMaxDistance = json.value("vegetation_max_distance", scene.vegetationMaxDistance);
    scene.vegetationTileSize = json.value("vegetation_tile_size", scene.vegetationTileSize);
    if (!vegetationFile.empty() &&
        (treeMesh[0] < scene.meshes.size() || treeMesh[1] < scene.meshes.size())) {
        std::filesystem::path vegetationPath(vegetationFile);
        if (vegetationPath.is_relative()) {
            vegetationPath = std::filesystem::path(scenePath).parent_path() / vegetationPath;
        }
        std::ifstream vegetationStream(vegetationPath);
        if (vegetationStream.good()) {
            nlohmann::json vegetation;
            try {
                vegetationStream >> vegetation;
            } catch (const std::exception&) {
                vegetation = nlohmann::json();
            }
            const auto trees = vegetation.find("trees");
            if (trees != vegetation.end() && trees->is_array()) {
                scene.vegetation.reserve(trees->size());
                for (const auto& entry : *trees) {
                    if (!entry.is_array() || entry.size() < 6) {
                        continue;
                    }
                    const float x = entry[0].get<float>();
                    const float y = entry[1].get<float>();
                    const float z = entry[2].get<float>();
                    const float scale = entry[3].get<float>();
                    const float rotation = entry[4].get<float>();
                    const int species = entry[5].get<int>() == 0 ? 0 : 1;
                    if (species < 0 || treeMesh[species] >= scene.meshes.size() ||
                        treeMaterial[species] >= scene.materials.size()) {
                        continue;
                    }
                    const asset::Mesh& mesh = scene.meshes[treeMesh[species]];
                    if (mesh.submeshes.empty()) {
                        continue;
                    }
                    const float c = std::cos(rotation) * scale;
                    const float s = std::sin(rotation) * scale;
                    VegetationInstance instance;
                    instance.world.values = {
                        c, 0.0f, -s, 0.0f,
                        0.0f, scale, 0.0f, 0.0f,
                        s, 0.0f, c, 0.0f,
                        x, y, z, 1.0f,
                    };
                    const Bounds& localBounds =
                        mesh.submeshes.front().bounds.valid() ? mesh.submeshes.front().bounds : mesh.bounds;
                    if (localBounds.valid()) {
                        instance.worldBounds = transformBounds(instance.world, localBounds);
                    } else {
                        instance.worldBounds.expand(Vec3(x, y, z));
                    }
                    instance.objectIndex = treeObject[species];
                    instance.meshIndex = treeMesh[species];
                    instance.materialIndex = treeMaterial[species];
                    instance.indexOffset = mesh.submeshes.front().indexOffset;
                    instance.indexCount = mesh.submeshes.front().indexCount;
                    scene.vegetation.push_back(instance);
                }
                HBG_LOGI("terrain vegetation: {} instances from '{}'", scene.vegetation.size(),
                         vegetationPath.string());
            }
        } else {
            HBG_LOGW("terrain vegetation: cannot open '{}'", vegetationPath.string());
        }
    }

    if (json.value("vegetation_impostor", false) && resources != nullptr) {
        std::string atlasFile = json.value("vegetation_texture", std::string());
        if (!atlasFile.empty()) {
            std::filesystem::path atlasPath(atlasFile);
            if (atlasPath.is_relative()) {
                atlasPath = std::filesystem::path(scenePath).parent_path() / atlasPath;
            }
            const uint32_t columns = json.value("vegetation_atlas_columns", 19u);
            const uint32_t rows = json.value("vegetation_atlas_rows", 20u);
            uint32_t layerSize = 0;
            uint32_t atlasColumns = 0;
            uint32_t atlasRows = 0;
            const gfx::TextureHandle atlas =
                createVegetationAtlas(*resources, atlasPath, columns, rows, layerSize, atlasColumns,
                                      atlasRows);
            if (atlas.valid()) {
                m_vegetationAtlasTexture = atlas;
                scene.vegetationAtlas = atlas;
                scene.vegetationAtlasColumns = atlasColumns;
                scene.vegetationAtlasRows = atlasRows;
                scene.vegetationAtlasLayerSize = layerSize;
                for (uint32_t species = 0; species < 2; ++species) {
                    if (treeMaterial[species] < scene.materials.size()) {
                        scene.materials[treeMaterial[species]].doubleSided = false;
                        scene.vegetationImpostorMaterials.push_back({treeMaterial[species], species});
                    }
                }
            }
        }
    }

    m_resources = resources;
    if (!heightmapPath.empty() && resources != nullptr) {
        const auto createHeightTexture = [&]() -> bool {
            std::ifstream heightFile(heightmapPath, std::ios::binary);
            if (!heightFile) {
                HBG_LOGW("terrain shared geometry: cannot open heightmap '{}'", heightmapPath.string());
                return false;
            }
            char magic[8] = {};
            uint32_t width = 0;
            uint32_t height = 0;
            float scale = 1.0f;
            heightFile.read(magic, sizeof(magic));
            heightFile.read(reinterpret_cast<char*>(&width), sizeof(width));
            heightFile.read(reinterpret_cast<char*>(&height), sizeof(height));
            heightFile.read(reinterpret_cast<char*>(&scale), sizeof(scale));
            if (!heightFile || std::memcmp(magic, "HBGHMAP1", 8) != 0 || width != height || width < 2 ||
                width > 65535 || !std::isfinite(scale) || scale <= 0.0f) {
                HBG_LOGW("terrain shared geometry: invalid heightmap '{}'", heightmapPath.string());
                return false;
            }

            std::vector<uint16_t> raw(static_cast<std::size_t>(width) * height);
            heightFile.read(reinterpret_cast<char*>(raw.data()),
                            static_cast<std::streamsize>(raw.size() * sizeof(uint16_t)));
            if (heightFile.gcount() != static_cast<std::streamsize>(raw.size() * sizeof(uint16_t))) {
                HBG_LOGW("terrain shared geometry: short heightmap '{}'", heightmapPath.string());
                return false;
            }
            std::vector<float> heights(raw.size());
            for (std::size_t index = 0; index < raw.size(); ++index) {
                heights[index] = static_cast<float>(raw[index]) * scale;
            }

            gfx::TextureDesc desc;
            desc.width = static_cast<uint16_t>(width);
            desc.height = static_cast<uint16_t>(height);
            desc.format = gfx::TextureFormat::R32F;
            desc.flags = BGFX_TEXTURE_NONE;
            desc.data = heights.data();
            desc.dataSize = static_cast<uint32_t>(heights.size() * sizeof(float));
            desc.name = "terrain.heightmap";
            m_terrainHeightTexture = resources->createTexture(desc);
            if (!m_terrainHeightTexture.valid()) {
                HBG_LOGW("terrain shared geometry: failed to create heightmap texture");
                return false;
            }

            scene.terrainHeight = m_terrainHeightTexture;
            scene.terrainWorld =
                Vec4(ox, oy, world, 1.0f / static_cast<float>(width - 1));
            return true;
        };

        if (createHeightTexture()) {
            const auto firstObject = m_objectByName.begin();
            const RenderObject& first = scene.objects[firstObject->second];
            uint32_t materialIndex = UINT32_MAX;
            if (first.meshIndex < scene.meshes.size() && !scene.meshes[first.meshIndex].submeshes.empty()) {
                materialIndex = scene.meshes[first.meshIndex].submeshes.front().materialIndex;
            }

            const int coarseGrid = std::clamp(json.value("terrain_coarse_grid", 33), 2, 129);
            const int fineGrid = std::clamp(json.value("terrain_fine_grid", 65), 2, 129);
            const int fineFromLevel = json.value("terrain_fine_from_level", 3);
            if (materialIndex < scene.materials.size()) {
                const auto findMesh = [&](const std::string& name) -> uint32_t {
                    if (name.empty()) {
                        return UINT32_MAX;
                    }
                    for (std::size_t index = 0; index < scene.meshes.size(); ++index) {
                        if (scene.meshes[index].name == name && !scene.meshes[index].submeshes.empty()) {
                            return static_cast<uint32_t>(index);
                        }
                    }
                    return UINT32_MAX;
                };
                const uint32_t assetCoarseMesh =
                    findMesh(json.value("terrain_coarse_mesh", std::string()));
                const uint32_t assetFineMesh =
                    findMesh(json.value("terrain_fine_mesh", std::string()));
                const bool assetShared = assetCoarseMesh != UINT32_MAX && assetFineMesh != UINT32_MAX;

                std::unordered_map<int, uint32_t> meshByGrid;
                uint32_t coarseMeshIndex = UINT32_MAX;
                uint32_t fineMeshIndex = UINT32_MAX;
                const auto sharedMesh = [&](int grid) -> uint32_t {
                    if (assetShared) {
                        return grid >= fineGrid ? assetFineMesh : assetCoarseMesh;
                    }
                    const auto found = meshByGrid.find(grid);
                    if (found != meshByGrid.end()) {
                        return found->second;
                    }
                    const uint32_t index = static_cast<uint32_t>(scene.meshes.size());
                    scene.meshes.push_back(makeSharedTerrainMesh(
                        grid, materialIndex, "TerrainSharedGrid" + std::to_string(grid)));
                    meshByGrid.emplace(grid, index);
                    return index;
                };

                const auto boundsIt = json.find("node_bounds");
                const float rootZmin = json.value("zmin", -100.0f);
                const float rootZmax = json.value("zmax", 1500.0f);
                std::vector<uint8_t> replacedMeshes(scene.meshes.size(), 0);
                for (const auto& entry : m_objectByName) {
                    int level = 0;
                    int tx = 0;
                    int ty = 0;
                    char firstChar = 0;
                    char secondChar = 0;
                    std::istringstream stream(entry.first.substr(m_prefix.size()));
                    if (!(stream >> level >> firstChar >> tx >> secondChar >> ty) || firstChar != '_' ||
                        secondChar != '_') {
                        continue;
                    }
                    RenderObject& object = scene.objects[entry.second];
                    const bool fineNode = level >= fineFromLevel;
                    const uint32_t targetMesh = sharedMesh(fineNode ? fineGrid : coarseGrid);
                    if (fineNode) {
                        fineMeshIndex = targetMesh;
                    } else {
                        coarseMeshIndex = targetMesh;
                    }
                    const bool keepAssetMesh = assetShared && (object.meshIndex == assetCoarseMesh ||
                                                               object.meshIndex == assetFineMesh);
                    if (object.meshIndex != targetMesh && !keepAssetMesh &&
                        object.meshIndex < replacedMeshes.size()) {
                        replacedMeshes[object.meshIndex] = 1;
                    }
                    object.meshIndex = targetMesh;
                    object.useWorldBounds = true;
                    object.terrainNode = Vec4(ox + static_cast<float>(tx) * (world / static_cast<float>(1 << level)),
                                              oy + static_cast<float>(ty) * (world / static_cast<float>(1 << level)),
                                              world / static_cast<float>(1 << level), skirtDepth);

                    float nodeZmin = rootZmin;
                    float nodeZmax = rootZmax;
                    if (boundsIt != json.end() && boundsIt->is_object()) {
                        const auto node = boundsIt->find(entry.first);
                        if (node != boundsIt->end() && node->is_array() && node->size() == 2) {
                            const float zminValue = (*node)[0].get<float>();
                            const float zmaxValue = (*node)[1].get<float>();
                            if (std::isfinite(zminValue) && std::isfinite(zmaxValue)) {
                                nodeZmin = zminValue;
                                nodeZmax = zmaxValue;
                            }
                        }
                    }
                    const float size = world / static_cast<float>(1 << level);
                    const float nodeX = ox + static_cast<float>(tx) * size;
                    const float nodeZ = oy + static_cast<float>(ty) * size;
                    object.worldBounds.min = Vec3(nodeX, nodeZmin, nodeZ);
                    object.worldBounds.max = Vec3(nodeX + size, nodeZmax, nodeZ + size);
                }
                for (std::size_t meshIndex = 0; meshIndex < replacedMeshes.size(); ++meshIndex) {
                    if (replacedMeshes[meshIndex] != 0) {
                        scene.meshes[meshIndex] = asset::Mesh{};
                    }
                }

                scene.terrainIndirectMesh = UINT32_MAX;
                scene.terrainIndirectSources.clear();
                if (coarseMeshIndex != UINT32_MAX && fineMeshIndex != UINT32_MAX &&
                    coarseMeshIndex < scene.meshes.size() && fineMeshIndex < scene.meshes.size()) {
                    asset::Mesh indirect;
                    indirect.name = "TerrainSharedGridIndirect";
                    indirect.hasNormals = true;
                    indirect.hasTangents = true;
                    indirect.hasUv0 = true;
                    appendTerrainIndirectSource(scene.meshes[coarseMeshIndex], coarseMeshIndex, materialIndex,
                                                indirect, scene.terrainIndirectSources);
                    if (fineMeshIndex != coarseMeshIndex) {
                        appendTerrainIndirectSource(scene.meshes[fineMeshIndex], fineMeshIndex, materialIndex,
                                                    indirect, scene.terrainIndirectSources);
                    }
                    if (!indirect.vertices.empty() && !indirect.indices.empty()) {
                        indirect.recomputeBounds();
                        const uint32_t indirectMesh = static_cast<uint32_t>(scene.meshes.size());
                        scene.meshes.push_back(std::move(indirect));
                        scene.terrainIndirectMesh = indirectMesh;
                        HBG_LOGI("terrain indirect mesh: mesh={} vertices={} indices={} sources={}",
                                 indirectMesh, scene.meshes[indirectMesh].vertices.size(),
                                 scene.meshes[indirectMesh].indices.size(),
                                 scene.terrainIndirectSources.size());
                    }
                }

                scene.recomputeBounds();
                m_sharedGeometry = true;
                HBG_LOGI("terrain shared geometry: coarse={} fine={} fineFrom={} material={} objects={} source={}",
                         coarseGrid, fineGrid, fineFromLevel, scene.materials[materialIndex].name,
                         m_objectByName.size(), assetShared ? "asset" : "runtime");
            } else {
                HBG_LOGW("terrain shared geometry: terrain material not found");
                resources->destroyTexture(m_terrainHeightTexture);
                m_terrainHeightTexture = {};
                scene.terrainHeight = {};
                scene.terrainWorld = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
            }
        }
    }

    const bool materialTilesEnabled = json.value("material_tiles", true);
    for (const auto& entry : m_objectByName) {
        if (!materialTilesEnabled) {
            break;
        }
        const RenderObject& object = scene.objects[entry.second];
        if (object.meshIndex >= scene.meshes.size()) {
            continue;
        }
        const asset::Mesh& mesh = scene.meshes[object.meshIndex];
        if (mesh.submeshes.empty() || mesh.submeshes.front().materialIndex >= scene.materials.size()) {
            continue;
        }
        const uint32_t materialIndex = mesh.submeshes.front().materialIndex;
        const asset::Material& material = scene.materials[materialIndex];
        if (!material.baseColorTexture.valid() || !material.normalTexture.valid()) {
            continue;
        }
        const int albedoIndex = material.baseColorTexture.imageIndex;
        const int normalIndex = material.normalTexture.imageIndex;
        if (albedoIndex < 0 || normalIndex < 0 || static_cast<std::size_t>(albedoIndex) >= scene.images.size() ||
            static_cast<std::size_t>(normalIndex) >= scene.images.size()) {
            continue;
        }

        if (resources == nullptr) {
            break;
        }
        const uint32_t materialTileSize = json.value("material_tile_size", 128u);
        const uint32_t materialTileCapacity = json.value("material_tile_cache_tiles", 128u);
        TerrainMaterialTileStreamer::OrthoParams ortho;
        const std::string orthoMode = json.value("material_ortho_source", std::string("baked"));
        if (orthoMode == "detail") {
            ortho.mode = TerrainMaterialTileStreamer::OrthoMode::Detail;
        } else if (orthoMode == "procedural") {
            ortho.mode = TerrainMaterialTileStreamer::OrthoMode::Procedural;
        }
        ortho.strength = json.value("material_ortho_strength", 0.0f);
        ortho.scale = json.value("material_ortho_scale", 2.0f);
        ortho.hsv = json.value("material_ortho_hsv", false);
        ortho.quadtree = json.value("material_quadtree", false);
        const auto readColor = [&](const char* key, Vec4& target) {
            const auto found = json.find(key);
            if (found == json.end() || !found->is_array() || found->size() < 3) {
                return;
            }
            target.x = (*found)[0].get<float>() / 255.0f;
            target.y = (*found)[1].get<float>() / 255.0f;
            target.z = (*found)[2].get<float>() / 255.0f;
            if (found->size() > 3) {
                target.w = (*found)[3].get<float>() / 255.0f;
            }
        };
        readColor("material_ortho_root", ortho.rootColor);
        readColor("material_ortho_color", ortho.noiseColor);
        const std::string noiseText = json.value("material_ortho_noise", std::string());
        if (!noiseText.empty()) {
            std::stringstream noiseStream(noiseText);
            std::string value;
            while (std::getline(noiseStream, value, ',')) {
                try {
                    ortho.noiseAmp.push_back(std::stof(value));
                } catch (...) {
                }
            }
        }
        std::string tileError;
        if (m_materialTiles.init(scene.images[static_cast<std::size_t>(albedoIndex)],
                                 scene.images[static_cast<std::size_t>(normalIndex)], ox, oy, world,
                                 materialTileSize, materialTileCapacity, ortho, *resources, &tileError)) {
            scene.materials[materialIndex].features |= asset::MaterialFeatureTerrainTiles;
            m_materialTilesActive = true;
            HBG_LOGI("terrain material tiles: material={} tile={} slots={}", material.name, materialTileSize,
                     materialTileCapacity);
        } else {
            HBG_LOGW("terrain material tiles disabled: {}", tileError);
        }
        break;
    }

    m_active = true;
    HBG_LOGI("terrain lod: world={} origin=({}, {}) maxLevel={} splitFactor={} objects={}",
             world, ox, oy, settings.maxLevel, settings.splitFactor, m_objectByName.size());
    return true;
}

void TerrainLodController::update(RenderScene& scene, const Vec3& cameraPosition,
                                  const std::array<Vec4, 6>* frustumPlanes,
                                  const Vec3* cameraForward) {
    if (!m_active) {
        return;
    }
    bool cameraMoved = false;
    if (!m_prefetchCameraInitialized) {
        m_lastPrefetchCamera = cameraPosition;
        m_prefetchCameraInitialized = true;
    } else if (length(cameraPosition - m_lastPrefetchCamera) > 0.2f) {
        m_lastPrefetchCamera = cameraPosition;
        cameraMoved = true;
    }
    if (m_asyncTiles) {
        m_tileStreamer.poll();
    }
    if (m_materialTilesActive) {
        m_materialTiles.poll();
        scene.terrainMaterialTiles = m_materialTiles.texture();
        scene.terrainMaterialTileMap = m_materialTiles.tileMap();
    }

    if (!m_groundHeightSampled || length(cameraPosition - m_lastGroundCamera) > 0.1f) {
        float sampledGround = 0.0f;
        if (m_heightmapSampler.sampleHeight(cameraPosition.x, cameraPosition.z, sampledGround)) {
            const bool firstSample = !m_groundHeightSampled;
            m_groundHeight = sampledGround;
            m_groundHeightSampled = true;
            m_lastGroundCamera = cameraPosition;
            if (firstSample) {
                HBG_LOGI("terrain camera ground height: {} at ({}, {})", sampledGround, cameraPosition.x,
                         cameraPosition.z);
            }
        }
    }

    for (const auto& entry : m_objectByName) {
        if (entry.second < scene.objects.size()) {
            scene.objects[entry.second].visible = false;
            scene.objects[entry.second].terrainTile = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
            scene.objects[entry.second].terrainNode = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    m_tree.update(cameraPosition, m_groundHeight, m_frustumCulling ? frustumPlanes : nullptr,
                  cameraForward);
    std::vector<const TerrainQuadNode*> leaves;
    m_tree.collectLeaves(leaves);
    m_visibleObjects = 0;
    for (std::size_t order = 0; order < leaves.size(); ++order) {
        const TerrainQuadNode* leaf = leaves[order];
        if (!leaf->visible) {
            continue;
        }
        const auto found = m_objectByName.find(nodeName(m_prefix, leaf->level, leaf->tx, leaf->ty));
        if (found == m_objectByName.end()) {
            continue;
        }
        RenderObject& object = scene.objects[found->second];
        object.visible = true;
        object.sortingOrder = static_cast<int>(order);
        if (m_sharedGeometry) {
            object.terrainNode = Vec4(leaf->ox, leaf->oy, leaf->size, m_skirtDepth);
            object.worldBounds.min = Vec3(leaf->ox, leaf->zmin, leaf->oy);
            object.worldBounds.max = Vec3(leaf->xmax(), leaf->zmax, leaf->ymax());
        }
        if (m_materialTilesActive) {
            m_materialTiles.request(leaf->level, leaf->tx, leaf->ty);
            TerrainMaterialTileStreamer::Binding binding;
            if (m_materialTiles.bind(leaf->level, leaf->tx, leaf->ty, binding)) {
                object.terrainTile =
                    Vec4(static_cast<float>(binding.layer), binding.originX, binding.originZ, binding.size);
            }
        }
        ++m_visibleObjects;
    }

    if (cameraMoved && m_materialTilesActive) {
        uint32_t budget = m_materialTiles.freeSlots();
        for (const TerrainQuadNode* leaf : leaves) {
            if (budget == 0) {
                break;
            }
            if (!leaf->visible || leaf->level >= m_maxLevel) {
                continue;
            }
            for (int dy = 0; dy < 2 && budget > 0; ++dy) {
                for (int dx = 0; dx < 2 && budget > 0; ++dx) {
                    if (m_materialTiles.prefetch(leaf->level + 1, leaf->tx * 2 + dx, leaf->ty * 2 + dy)) {
                        --budget;
                    }
                }
            }
        }
    }

    if (m_materialTilesActive) {
        const float splitDistance = m_tree.splitDistance();
        const float k = std::ceil(splitDistance);
        scene.terrainQuadInfo = Vec4(m_worldSize, splitDistance, 2.0f * k, 4.0f * k + 2.0f);
        scene.terrainCamera =
            Vec4(cameraPosition.x - m_originX, cameraPosition.z - m_originZ,
                 std::max(0.0f, cameraPosition.y - m_groundHeight), static_cast<float>(m_maxLevel));
        scene.terrainMapInfo =
            Vec4(m_materialTiles.quadtreeActive() ? 1.0f : 0.0f,
                 static_cast<float>(m_materialTiles.textureSize()),
                 static_cast<float>(m_materialTiles.contentSize()), 2.0f);
        m_materialTiles.refreshTileMap(splitDistance, cameraPosition.x - m_originX,
                                       cameraPosition.z - m_originZ, m_maxLevel);
    }
}

} // namespace hbg::render
