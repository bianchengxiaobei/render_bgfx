#include "render/material/material_system.h"

#include "asset/texture_loader.h"
#include "core/log.h"
#include "render/river/river_terrain.h"
#include "shader/shader_key.h"

#include <bx/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace hbg::render {
namespace {

gfx::UniformHandle createSampler(gfx::ResourceManager& resources, const char* name) {
    return resources.createUniform({name, gfx::UniformType::Sampler, 1});
}

gfx::TextureHandle createColorTexture(gfx::ResourceManager& resources, uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                      bool srgb, const char* name) {
    const std::array<uint8_t, 4> pixel = {r, g, b, a};
    gfx::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = gfx::TextureFormat::RGBA8;
    desc.data = pixel.data();
    desc.dataSize = static_cast<uint32_t>(pixel.size());
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | (srgb ? BGFX_TEXTURE_SRGB : 0);
    desc.name = name;
    return resources.createTexture(desc);
}

bool uploadRiverHalfTexture(gfx::ResourceManager& resources, gfx::TextureHandle texture,
                            const std::vector<float>& pixels, uint16_t width, uint16_t height, uint8_t mip,
                            const char* name) {
    std::vector<uint16_t> texels(pixels.size());
    for (std::size_t index = 0; index < pixels.size(); ++index) {
        texels[index] = bx::halfFromFloat(pixels[index]);
    }
    const uint16_t pitch = static_cast<uint16_t>(static_cast<std::size_t>(width) * 4u * sizeof(uint16_t));
    if (!resources.updateTexture(texture, texels.data(), static_cast<uint32_t>(texels.size() * sizeof(uint16_t)),
                                 width, height, pitch, 0, mip)) {
        HBG_LOGE("material system: failed to upload river texture '{}'", name);
        return false;
    }
    return true;
}

gfx::VertexLayout meshLayout() {
    gfx::VertexLayout layout;
    layout.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
    layout.add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float);
    layout.add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float);
    layout.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
    layout.add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float);
    layout.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Float);
    layout.end();
    return layout;
}

uint64_t textureKey(int imageIndex, bool srgb) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(imageIndex)) << 1) | (srgb ? 1u : 0u);
}

uint64_t indexHash(const std::vector<uint32_t>& indices) {
    uint64_t hash = 1469598103934665603ull;
    for (const uint32_t value : indices) {
        hash ^= value;
        hash *= 1099511628211ull;
    }
    return hash;
}

} // namespace

bool MaterialSamplers::init(gfx::ResourceManager& resources) {
    baseColorMap = createSampler(resources, "s_baseColorMap");
    metallicRoughnessMap = createSampler(resources, "s_metallicRoughnessMap");
    normalMap = createSampler(resources, "s_normalMap");
    occlusionMap = createSampler(resources, "s_occlusionMap");
    emissiveMap = createSampler(resources, "s_emissiveMap");
    waterFft = createSampler(resources, "s_waterFFT");
    riverParticleNormal = createSampler(resources, "s_riverParticleNormal");
    return baseColorMap.valid() && metallicRoughnessMap.valid() && normalMap.valid() && occlusionMap.valid() &&
           emissiveMap.valid() && waterFft.valid() && riverParticleNormal.valid();
}

void MaterialSamplers::shutdown(gfx::ResourceManager& resources) {
    if (baseColorMap.valid()) {
        resources.destroyUniform(baseColorMap);
    }
    if (metallicRoughnessMap.valid()) {
        resources.destroyUniform(metallicRoughnessMap);
    }
    if (normalMap.valid()) {
        resources.destroyUniform(normalMap);
    }
    if (occlusionMap.valid()) {
        resources.destroyUniform(occlusionMap);
    }
    if (emissiveMap.valid()) {
        resources.destroyUniform(emissiveMap);
    }
    if (waterFft.valid()) {
        resources.destroyUniform(waterFft);
    }
    if (riverParticleNormal.valid()) {
        resources.destroyUniform(riverParticleNormal);
    }
    baseColorMap = {};
    metallicRoughnessMap = {};
    normalMap = {};
    occlusionMap = {};
    emissiveMap = {};
    waterFft = {};
    riverParticleNormal = {};
}

gfx::BufferHandle MaterialSystem::acquireIndexBuffer(gfx::ResourceManager& resources,
                                                     const std::vector<uint32_t>& indices, const std::string& name) {
    const uint64_t hash = indexHash(indices);
    std::vector<SharedIndexBuffer>& bucket = m_indexBufferBuckets[hash];
    for (const SharedIndexBuffer& entry : bucket) {
        if (entry.source != nullptr && *entry.source == indices) {
            ++m_reusedIndexBuffers;
            m_indexBytesSaved += static_cast<uint64_t>(indices.size()) * sizeof(uint32_t);
            return entry.handle;
        }
    }

    gfx::BufferDesc desc;
    desc.type = gfx::BufferType::Index;
    desc.data = indices.data();
    desc.size = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
    desc.count = static_cast<uint32_t>(indices.size());
    desc.flags = BGFX_BUFFER_INDEX32;
    desc.name = name;
    const gfx::BufferHandle handle = resources.createBuffer(desc);
    if (handle.valid()) {
        m_ownedIndexBuffers.push_back(handle);
        bucket.push_back({&indices, handle});
    }
    return handle;
}

gfx::BufferHandle MaterialSystem::createIndexBuffer(gfx::ResourceManager& resources,
                                                    const std::vector<uint32_t>& indices, const std::string& name,
                                                    bool cacheable) {
    if (cacheable) {
        return acquireIndexBuffer(resources, indices, name);
    }

    gfx::BufferDesc desc;
    desc.type = gfx::BufferType::Index;
    desc.data = indices.data();
    desc.size = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
    desc.count = static_cast<uint32_t>(indices.size());
    desc.flags = BGFX_BUFFER_INDEX32;
    desc.name = name;
    const gfx::BufferHandle handle = resources.createBuffer(desc);
    if (handle.valid()) {
        m_ownedIndexBuffers.push_back(handle);
    }
    return handle;
}

gfx::BufferHandle MaterialSystem::createWaterVertexBuffer(gfx::ResourceManager& resources, const asset::Mesh& mesh,
                                                          const std::string& name, bool computeWrite) {
    const gfx::VertexLayout layout = meshLayout();
    gfx::BufferDesc desc;
    desc.type = computeWrite ? gfx::BufferType::StorageVertex : gfx::BufferType::DynamicVertex;
    desc.data = computeWrite ? nullptr : mesh.vertices.data();
    desc.size = computeWrite ? 0u : static_cast<uint32_t>(mesh.vertices.size() * sizeof(asset::MeshVertex));
    desc.count = static_cast<uint32_t>(mesh.vertices.size());
    desc.layout = &layout;
    desc.flags = computeWrite ? BGFX_BUFFER_COMPUTE_WRITE : BGFX_BUFFER_NONE;
    desc.name = name;
    return resources.createBuffer(desc);
}

bool MaterialSystem::init(gfx::ResourceManager& resources, const RenderScene& scene,
                          const std::filesystem::path& shaderRoot) {
    m_resources = &resources;
    m_ocean = OceanFFT{};
    m_waterFftTexture = {};
    m_waterFftFallback = {};
    m_riverBaseColorTexture = {};
    m_riverNormalTexture = {};
    m_riverWaveBaseTexture = {};
    m_river = RiverParticleSystem{};
    m_riverMeshIndex = UINT32_MAX;
    m_riverObjectIndex = UINT32_MAX;
    m_riverVertexCount = 0;
    m_riverIndexCount = 0;
    m_riverWaveFrameUploaded = UINT32_MAX;
    m_riverParticleLogged = false;
    m_riverLeavingLogged = false;
    m_riverNearLogged = false;
    m_riverOutsideLogged = false;
    m_riverTerrainBaseVertices.clear();
    m_riverTerrainVertices.clear();
    m_riverTerrainDelta.clear();
    m_riverTerrainResidual.reset();
    m_riverTerrainMeshIndex = UINT32_MAX;
    m_riverTerrainObjectIndex = UINT32_MAX;
    m_riverTerrainLogged = false;
    m_riverTerrainCacheLogged = false;
    m_riverTerrainDeformed = false;
    m_waterVertices.clear();
    m_waterBaseVertices.clear();
    m_waterMeshIndex = UINT32_MAX;
    m_waterBaseHeight = 0.5f;
    m_waterSphereRadius = 0.0f;
    m_waterGridWidth = 0;
    m_waterGridHeight = 0;
    m_waterUvScale = Vec2(1.0f, 1.0f);
    m_waterUvOrigin = Vec2(0.0f, 0.0f);
    m_waterSource = WaterScreenGridSource{};
    m_waterCameraValid = false;
    m_waterActive = false;
    if (m_oceanGpu.valid()) {
        m_oceanGpu.shutdown(resources);
    }
    if (m_riverGpu.valid()) {
        m_riverGpu.shutdown(resources);
    }
    if (!m_samplers.init(resources)) {
        HBG_LOGE("material system: failed to create material samplers");
        return false;
    }

    m_whiteSrgb = createColorTexture(resources, 255, 255, 255, 255, true, "material_white_srgb");
    m_whiteLinear = createColorTexture(resources, 255, 255, 255, 255, false, "material_white_linear");
    m_normalFlat = createColorTexture(resources, 128, 128, 255, 255, false, "material_normal_flat");
    const std::array<float, 20> waterFftFallback = {};
    gfx::TextureDesc waterFftFallbackDesc;
    waterFftFallbackDesc.width = 1;
    waterFftFallbackDesc.height = 1;
    waterFftFallbackDesc.layers = 5;
    waterFftFallbackDesc.format = gfx::TextureFormat::RGBA32F;
    waterFftFallbackDesc.flags = BGFX_SAMPLER_UVW_CLAMP;
    waterFftFallbackDesc.data = waterFftFallback.data();
    waterFftFallbackDesc.dataSize = static_cast<uint32_t>(waterFftFallback.size() * sizeof(float));
    waterFftFallbackDesc.name = "material_water_fft_fallback";
    m_waterFftFallback = resources.createTexture(waterFftFallbackDesc);
    if (!m_whiteSrgb.valid() || !m_whiteLinear.valid() || !m_normalFlat.valid() || !m_waterFftFallback.valid()) {
        HBG_LOGE("material system: failed to create fallback textures");
        return false;
    }

    if (scene.river.enabled) {
        if (!m_river.init(scene.river.graph, scene.river.mesh, scene.river.particles)) {
            HBG_LOGE("material system: failed to initialize river particle system");
            return false;
        }
        const uint32_t riverSize = m_river.textureSize();
        gfx::TextureDesc baseDesc;
        baseDesc.width = static_cast<uint16_t>(riverSize);
        baseDesc.height = static_cast<uint16_t>(riverSize);
        baseDesc.format = gfx::TextureFormat::RGBA8;
        baseDesc.flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_MIRROR | BGFX_SAMPLER_W_CLAMP | BGFX_TEXTURE_SRGB;
        baseDesc.name = "river_bed_color";
        m_riverBaseColorTexture = resources.createTexture(baseDesc);

        gfx::TextureDesc normalDesc = baseDesc;
        normalDesc.flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_MIRROR | BGFX_SAMPLER_W_CLAMP;
        normalDesc.flags &= ~BGFX_TEXTURE_SRGB;
        normalDesc.flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
        normalDesc.format = gfx::TextureFormat::RGBA16F;
        normalDesc.mipCount = static_cast<uint8_t>(std::max<std::size_t>(1u, m_river.baseNormalMips().size()));
        normalDesc.name = "river_wave_normal";
        m_riverNormalTexture = resources.createTexture(normalDesc);

        gfx::TextureDesc waveDesc = baseDesc;
        waveDesc.flags = BGFX_SAMPLER_W_CLAMP;
        waveDesc.flags &= ~BGFX_TEXTURE_SRGB;
        waveDesc.flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
        waveDesc.format = gfx::TextureFormat::RGBA16F;
        waveDesc.mipCount = static_cast<uint8_t>(std::max<std::size_t>(1u, m_river.baseNormalMips().size()));
        waveDesc.name = "river_wave_base";
        m_riverWaveBaseTexture = resources.createTexture(waveDesc);
        if (!m_riverBaseColorTexture.valid() || !m_riverNormalTexture.valid() || !m_riverWaveBaseTexture.valid()) {
            HBG_LOGE("material system: failed to create river textures");
            return false;
        }
        const auto uploadRiverTexture = [&](gfx::TextureHandle texture, const std::vector<uint8_t>& pixels,
                                            const char* name) {
            if (!resources.updateTexture(texture, pixels.data(), static_cast<uint32_t>(pixels.size()),
                                         static_cast<uint16_t>(riverSize), static_cast<uint16_t>(riverSize),
                                         static_cast<uint16_t>(riverSize * 4))) {
                HBG_LOGE("material system: failed to upload river texture '{}'", name);
                return false;
            }
            return true;
        };
        if (!uploadRiverTexture(m_riverBaseColorTexture, m_river.baseColorPixels(), "river_bed_color") ||
            !uploadRiverNormal() || !uploadRiverWaveBase()) {
            return false;
        }
        if (m_riverGpu.init(resources, m_river, m_riverWaveBaseTexture, shaderRoot)) {
            m_river.setCpuNormalSplatEnabled(false);
            HBG_LOGI("river particle CPU splat: deferred to GPU residual");
        } else {
            HBG_LOGW("river particle GPU splat unavailable; CPU normals remain active");
        }
        HBG_LOGI("river texture: {}x{} baseColor={} normal={} waveBase={} waveMips={} normalMips={} particleGpu={} "
                 "particles={}",
                 riverSize, riverSize, m_riverBaseColorTexture.valid(), m_riverNormalTexture.valid(),
                 m_riverWaveBaseTexture.valid(), waveDesc.mipCount, normalDesc.mipCount, m_riverGpu.valid(),
                 m_river.particleCount());
    }

    const gfx::VertexLayout layout = meshLayout();
    const bool riverTerrainActive = scene.river.enabled && scene.river.editable && scene.river.terrain.enabled &&
                                    scene.river.terrain.meshIndex < scene.meshes.size();
    std::vector<uint8_t> usedMeshes(scene.meshes.size(), 0);
    for (const RenderObject& object : scene.objects) {
        if (object.meshIndex < usedMeshes.size()) {
            usedMeshes[object.meshIndex] = 1;
        }
    }
    if (scene.terrainIndirectMesh < usedMeshes.size()) {
        usedMeshes[scene.terrainIndirectMesh] = 1;
    }
    m_meshes.reserve(scene.meshes.size());
    for (std::size_t meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
        const asset::Mesh& mesh = scene.meshes[meshIndex];
        GpuMesh gpuMesh;
        if (usedMeshes[meshIndex] == 0) {
            m_meshes.push_back(gpuMesh);
            continue;
        }
        const bool dynamicWater = scene.waterFftEnabled && meshIndex == scene.waterMeshIndex;
        const bool dynamicRiver = scene.river.enabled && scene.river.editable && meshIndex == scene.river.meshIndex;
        const bool dynamicRiverTerrain = riverTerrainActive && meshIndex == scene.river.terrain.meshIndex;
        if (dynamicWater) {
            m_waterVertices = mesh.vertices;
            m_waterBaseVertices = mesh.vertices;
            m_waterBaseHeight = mesh.vertices.empty() ? 0.5f : mesh.vertices.front().position.y;
        }
        if (dynamicRiverTerrain) {
            m_riverTerrainBaseVertices = mesh.vertices;
            m_riverTerrainVertices = mesh.vertices;
            m_riverTerrainMeshIndex = static_cast<uint32_t>(meshIndex);
            m_riverTerrainObjectIndex = scene.river.terrain.objectIndex;
            RiverTerrainResidualSettings residualSettings;
            residualSettings.leafTiles = scene.river.terrain.residualTiles;
            if (m_riverTerrainResidual.initialize(m_riverTerrainBaseVertices, residualSettings)) {
                const RiverTerrainResidualStats& stats = m_riverTerrainResidual.stats();
                HBG_LOGI("river terrain residual cache: grid={}x{} leaf_tiles={} levels={} samples={}",
                         stats.gridWidth, stats.gridHeight, stats.leafTiles, stats.levels, stats.samplesPerTile);
            } else {
                HBG_LOGW("river terrain residual cache: terrain grid is not a regular square grid; "
                         "using per-vertex residual");
            }
        }
        if (!mesh.vertices.empty()) {
            gfx::BufferDesc vertexDesc;
            vertexDesc.type = (dynamicWater || dynamicRiver || dynamicRiverTerrain) ? gfx::BufferType::DynamicVertex
                                                                                    : gfx::BufferType::Vertex;
            vertexDesc.data = dynamicWater          ? m_waterVertices.data()
                              : dynamicRiverTerrain ? m_riverTerrainVertices.data()
                                                    : mesh.vertices.data();
            vertexDesc.size = static_cast<uint32_t>(mesh.vertices.size() * sizeof(asset::MeshVertex));
            vertexDesc.count = static_cast<uint32_t>(mesh.vertices.size());
            vertexDesc.layout = &layout;
            vertexDesc.name = mesh.name + ".vb";
            gpuMesh.vertexBuffer = resources.createBuffer(vertexDesc);
        }
        if (!mesh.indices.empty()) {
            gpuMesh.indexBuffer = createIndexBuffer(resources, mesh.indices, mesh.name + ".ib",
                                                    !dynamicWater && !dynamicRiver && !dynamicRiverTerrain);
        }
        if (!gpuMesh.vertexBuffer.valid() || !gpuMesh.indexBuffer.valid()) {
            HBG_LOGE("material system: failed to create GPU buffers for mesh '{}'", mesh.name);
            return false;
        }
        if (dynamicWater) {
            m_waterMeshIndex = static_cast<uint32_t>(meshIndex);
        }
        if (dynamicRiver) {
            m_riverMeshIndex = static_cast<uint32_t>(meshIndex);
            m_riverObjectIndex = scene.river.objectIndex;
            m_riverVertexCount = static_cast<uint32_t>(mesh.vertices.size());
            m_riverIndexCount = static_cast<uint32_t>(mesh.indices.size());
        }
        m_meshes.push_back(gpuMesh);
    }

    uint32_t activeMeshes = 0;
    uint64_t vertexBytes = 0;
    for (std::size_t meshIndex = 0; meshIndex < scene.meshes.size(); ++meshIndex) {
        if (usedMeshes[meshIndex] != 0) {
            ++activeMeshes;
            vertexBytes += scene.meshes[meshIndex].vertices.size() * sizeof(asset::MeshVertex);
        }
    }
    HBG_LOGI("material system: scene_meshes={} gpu_meshes={} skipped_meshes={} vertex_bytes={}", scene.meshes.size(),
             activeMeshes, scene.meshes.size() - activeMeshes, vertexBytes);

    m_waterSphereRadius = scene.waterSphereRadius;
    if (scene.waterFftEnabled && m_waterMeshIndex != UINT32_MAX && !m_waterVertices.empty()) {
        int gridWidth = 0;
        const float firstZ = m_waterBaseVertices.front().position.z;
        while (gridWidth < static_cast<int>(m_waterBaseVertices.size()) &&
               std::abs(m_waterBaseVertices[static_cast<std::size_t>(gridWidth)].position.z - firstZ) <= 1.0e-5f) {
            ++gridWidth;
        }
        int gridHeight = 0;
        if (gridWidth >= 2 && m_waterBaseVertices.size() % static_cast<std::size_t>(gridWidth) == 0) {
            gridHeight = static_cast<int>(m_waterBaseVertices.size() / static_cast<std::size_t>(gridWidth));
        }
        if (gridHeight >= 2) {
            m_waterGridWidth = gridWidth;
            m_waterGridHeight = gridHeight;
            float minX = m_waterBaseVertices.front().position.x;
            float maxX = minX;
            float minZ = m_waterBaseVertices.front().position.z;
            float maxZ = minZ;
            float minU = m_waterBaseVertices.front().texcoord0.x;
            float maxU = minU;
            float minV = m_waterBaseVertices.front().texcoord0.y;
            float maxV = minV;
            for (const asset::MeshVertex& vertex : m_waterBaseVertices) {
                minX = std::min(minX, vertex.position.x);
                maxX = std::max(maxX, vertex.position.x);
                minZ = std::min(minZ, vertex.position.z);
                maxZ = std::max(maxZ, vertex.position.z);
                minU = std::min(minU, vertex.texcoord0.x);
                maxU = std::max(maxU, vertex.texcoord0.x);
                minV = std::min(minV, vertex.texcoord0.y);
                maxV = std::max(maxV, vertex.texcoord0.y);
            }
            if (maxX > minX && maxZ > minZ) {
                m_waterUvScale = Vec2((maxU - minU) / (maxX - minX), (maxV - minV) / (maxZ - minZ));
                m_waterUvOrigin = Vec2(minU - minX * m_waterUvScale.x, minV - minZ * m_waterUvScale.y);
            }
            m_waterSource.bounds = {};
            for (const asset::MeshVertex& vertex : m_waterBaseVertices) {
                m_waterSource.bounds.expand(vertex.position);
            }
            m_waterSource.baseHeight = m_waterBaseHeight;
            m_waterSource.uvMin = Vec2(minU, minV);
            m_waterSource.uvMax = Vec2(maxU, maxV);
            m_waterSource.materialIndex = scene.meshes[m_waterMeshIndex].submeshes.empty()
                                              ? 0u
                                              : scene.meshes[m_waterMeshIndex].submeshes.front().materialIndex;
        }
        OceanFFTSettings oceanSettings;
        oceanSettings.tileSize = scene.waterFftTileSize;
        oceanSettings.amplitude = scene.waterFftAmplitude;
        oceanSettings.updateHz = scene.waterFftUpdateHz;
        oceanSettings.resolution = 128;
        m_waterActive = m_ocean.init(oceanSettings);
        if (!m_waterActive) {
            HBG_LOGW("material system: FFT ocean initialization failed; water stays flat");
        } else {
            bool gpuActive = m_oceanGpu.init(resources, m_ocean, shaderRoot);
            if (gpuActive) {
                GpuMesh& waterGpuMesh = m_meshes[m_waterMeshIndex];
                const gfx::BufferHandle previous = waterGpuMesh.vertexBuffer;
                const gfx::BufferHandle gpuBuffer = createWaterVertexBuffer(
                    resources, scene.meshes[m_waterMeshIndex], scene.meshes[m_waterMeshIndex].name + ".vb.gpu", true);
                if (gpuBuffer.valid()) {
                    if (previous.valid()) {
                        resources.destroyBuffer(previous);
                    }
                    waterGpuMesh.vertexBuffer = gpuBuffer;
                } else {
                    HBG_LOGW("material system: failed to create GPU water vertex buffer; using CPU FFT");
                    m_oceanGpu.shutdown(resources);
                    gpuActive = false;
                }
            }
            if (!gpuActive) {
                createWaterFftTexture();
            }
            HBG_LOGI("material system: FFT ocean mesh={} vertices={} grid={}x{} baseHeight={:.2f} path={}",
                     m_waterMeshIndex, m_waterVertices.size(), m_waterGridWidth, m_waterGridHeight, m_waterBaseHeight,
                     gpuActive ? "gpu" : "cpu");
        }
    } else if (scene.waterFftEnabled) {
        HBG_LOGW("material system: water FFT requested but water mesh/material was not found");
    }
    m_materials.reserve(scene.materials.size());
    HBG_LOGI("material system: vegetation atlas={} impostor_materials={}", scene.vegetationAtlas.valid(),
             scene.vegetationImpostorMaterials.size());
    for (std::size_t materialIndex = 0; materialIndex < scene.materials.size(); ++materialIndex) {
        const asset::Material& material = scene.materials[materialIndex];
        GpuMaterial gpuMaterial;
        const bool riverMaterial = scene.river.enabled && materialIndex == scene.river.materialIndex;
        if (riverMaterial) {
            gpuMaterial.baseColorTexture = m_riverBaseColorTexture;
            gpuMaterial.normalTexture = m_riverNormalTexture;
            gpuMaterial.metallicRoughnessTexture = m_whiteLinear;
            gpuMaterial.occlusionTexture = m_whiteLinear;
            gpuMaterial.emissiveTexture = m_whiteSrgb;
        } else {
            gpuMaterial.baseColorTexture = textureForImage(resources, scene, material.baseColorTexture, true, false);
            gpuMaterial.metallicRoughnessTexture =
                textureForImage(resources, scene, material.metallicRoughnessTexture, false, false);
            gpuMaterial.normalTexture = textureForImage(resources, scene, material.normalTexture, false, true);
            gpuMaterial.occlusionTexture = textureForImage(resources, scene, material.occlusionTexture, false, false);
            gpuMaterial.emissiveTexture = textureForImage(resources, scene, material.emissiveTexture, true, false);
        }

        gpuMaterial.uniforms.baseColorFactor = riverMaterial ? Vec4(1.0f, 1.0f, 1.0f, 1.0f) : material.baseColorFactor;
        gpuMaterial.uniforms.metallicRoughness =
            Vec4(material.metallic, material.roughness, material.normalScale, material.alphaCutoff);
        gpuMaterial.uniforms.emissiveFactor =
            Vec4(material.emissiveFactor.x * material.emissiveStrength,
                 material.emissiveFactor.y * material.emissiveStrength,
                 material.emissiveFactor.z * material.emissiveStrength, material.occlusionStrength);
        gpuMaterial.uniforms.advanced0 =
            Vec4(material.clearCoat, material.clearCoatRoughness, material.transmission, material.thickness);
        gpuMaterial.uniforms.advanced1 =
            Vec4(material.anisotropy, material.anisotropyRotation, material.ior, material.iridescence);
        gpuMaterial.uniforms.advanced2 = Vec4(material.iridescenceThickness, material.subsurfaceMask,
                                              static_cast<float>(material.features), material.clearCoatNormalScale);
        gpuMaterial.uniforms.riverParticleParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        gpuMaterial.uniforms.riverRefractionParams = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
        if (riverMaterial) {
            gpuMaterial.uniforms.riverParticleParams.y = scene.river.particles.waveSlopeFactor;
            gpuMaterial.uniforms.riverParticleParams.w = scene.river.refractionStrength;
            gpuMaterial.uniforms.riverRefractionParams = Vec4(scene.river.absorptionStrength,
                                                             scene.river.reflectionStrength, 1.0f, 0.0f);
            if (m_riverGpu.valid() && scene.river.editable) {
                gpuMaterial.uniforms.riverParticleParams.x = 1.0f;
                gpuMaterial.uniforms.riverParticleParams.z = scene.river.particles.waveLength;
            }
        }
        if (scene.vegetationAtlas.valid()) {
            for (const VegetationImpostorMaterial& impostor : scene.vegetationImpostorMaterials) {
                if (impostor.materialIndex != materialIndex) {
                    continue;
                }
                gpuMaterial.baseColorTexture = scene.vegetationAtlas;
                gpuMaterial.uniforms.baseColorFactor =
                    impostor.species == 0 ? Vec4(0.02f, 0.10f, 0.015f, 1.0f) : Vec4(0.025f, 0.09f, 0.01f, 1.0f);
                gpuMaterial.uniforms.advanced1.y = 1000.0f + static_cast<float>(impostor.species);
                break;
            }
        }

        uint32_t variant = shader::MaterialVariantNone;
        if (gpuMaterial.normalTexture.valid()) {
            variant |= shader::MaterialVariantNormalMap;
        }
        if (material.metallicRoughnessTexture.valid() || material.occlusionTexture.valid()) {
            variant |= shader::MaterialVariantMaskMap;
        }
        if (material.alphaMode == asset::AlphaMode::Mask) {
            variant |= shader::MaterialVariantAlphaClip;
        }
        if (material.doubleSided) {
            variant |= shader::MaterialVariantDoubleSided;
        }
        if (material.alphaMode == asset::AlphaMode::Blend) {
            variant |= shader::MaterialVariantTransparent;
        }
        gpuMaterial.variantKey = variant;
        gpuMaterial.features = material.features;
        if (riverMaterial) {
            gpuMaterial.features |= asset::MaterialFeatureWater;
            gpuMaterial.uniforms.advanced2.z = static_cast<float>(gpuMaterial.features);
        }
        if (scene.waterFftEnabled && materialIndex == scene.waterMaterialIndex) {
            gpuMaterial.features |= asset::MaterialFeatureWater;
            gpuMaterial.variantKey |= shader::MaterialVariantWaterFft;
            gpuMaterial.uniforms.advanced2.z = static_cast<float>(gpuMaterial.features);
            const std::array<float, 4>& gridSizes = m_ocean.gridSizes();
            gpuMaterial.uniforms.waterFftGridSizes = Vec4(gridSizes[0], gridSizes[1], gridSizes[2], gridSizes[3]);
            gpuMaterial.uniforms.waterSphere = Vec4(m_waterSphereRadius, m_waterBaseHeight, 0.0f, 0.0f);
        }

        m_materials.push_back(gpuMaterial);
    }

    const auto textureForDecal = [&](int imageIndex, bool srgb, bool normalMap) {
        if (imageIndex < 0) {
            return gfx::TextureHandle{};
        }
        asset::TextureRef ref;
        ref.imageIndex = imageIndex;
        return textureForImage(resources, scene, ref, srgb, normalMap);
    };

    m_decals.reserve(scene.decals.size());
    for (const render::Decal& decal : scene.decals) {
        GpuDecalTextures textures;
        textures.albedo = textureForDecal(decal.albedoImage, true, false);
        textures.normal = textureForDecal(decal.normalImage, false, true);
        textures.mask = textureForDecal(decal.maskImage, false, false);
        m_decals.push_back(textures);
    }

    HBG_LOGI("material system: {} meshes, {} materials, {} decals, {} textures, {} unique index buffers "
             "({} reused, {} bytes saved)",
             m_meshes.size(), m_materials.size(), m_decals.size(), m_textureCache.size(), m_ownedIndexBuffers.size(),
             m_reusedIndexBuffers, m_indexBytesSaved);
    return true;
}

bool MaterialSystem::createWaterFftTexture() {
    if (m_resources == nullptr || !m_ocean.valid()) {
        return false;
    }
    if (m_waterFftTexture.valid()) {
        return true;
    }

    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(m_ocean.resolution());
    desc.height = static_cast<uint16_t>(m_ocean.resolution());
    desc.layers = static_cast<uint16_t>(m_ocean.textureLayerCount());
    desc.mipCount = static_cast<uint8_t>(m_ocean.mipLevelCount());
    desc.format = gfx::TextureFormat::RGBA32F;
    desc.name = "material_water_fft";
    m_waterFftTexture = m_resources->createTexture(desc);
    if (!m_waterFftTexture.valid()) {
        HBG_LOGW("material system: failed to create water FFT texture; water normals use fallback");
        return false;
    }
    if (!uploadWaterFftTexture()) {
        HBG_LOGW("material system: failed to upload water FFT texture; water normals use fallback");
        m_resources->destroyTexture(m_waterFftTexture);
        m_waterFftTexture = {};
        return false;
    }
    HBG_LOGI("material system: water FFT texture={}x{} layers={} mips={}", desc.width, desc.height, desc.layers,
             desc.mipCount);
    return true;
}

bool MaterialSystem::uploadWaterFftTexture() {
    if (m_resources == nullptr || !m_waterFftTexture.valid()) {
        return false;
    }

    bool uploaded = true;
    m_ocean.visitTextureLayers([&](uint16_t layer, uint8_t mip, const float* data, uint16_t width, uint16_t height) {
        const uint32_t pitch = static_cast<uint32_t>(width) * 4u * sizeof(float);
        const uint32_t size = pitch * static_cast<uint32_t>(height);
        uploaded = m_resources->updateTexture(m_waterFftTexture, data, size, width, height,
                                              static_cast<uint16_t>(pitch), layer, mip) &&
                   uploaded;
    });
    return uploaded;
}

bool MaterialSystem::updateWater(float timeSeconds, const Mat4& invViewProj, const Vec3& cameraPosition) {
    if (!m_waterActive || m_resources == nullptr || m_waterMeshIndex >= m_meshes.size()) {
        return false;
    }
    if (m_oceanGpu.valid()) {
        const GpuMesh& gpuMesh = m_meshes[m_waterMeshIndex];
        const bool updated = m_oceanGpu.update(*m_resources, m_ocean, gpuMesh.vertexBuffer,
                                               static_cast<uint32_t>(m_waterVertices.size()), m_waterGridWidth,
                                               m_waterGridHeight, m_waterBaseHeight, invViewProj, cameraPosition,
                                               m_waterUvScale, m_waterUvOrigin, timeSeconds, m_waterSphereRadius);
        m_waterLastInvViewProj = invViewProj;
        m_waterLastCameraPosition = cameraPosition;
        m_waterCameraValid = updated;
        return updated;
    }
    const bool fftUpdated = m_ocean.update(timeSeconds);
    if (fftUpdated) {
        uploadWaterFftTexture();
    }
    const bool cameraChanged =
        !m_waterCameraValid ||
        !std::equal(invViewProj.values.begin(), invViewProj.values.end(), m_waterLastInvViewProj.values.begin()) ||
        cameraPosition != m_waterLastCameraPosition;
    if (!fftUpdated && !cameraChanged) {
        return true;
    }
    if (m_waterGridWidth >= 2 && m_waterGridHeight >= 2) {
        m_ocean.applyToScreenGrid(m_waterVertices, m_waterBaseHeight, m_waterGridWidth, m_waterGridHeight, invViewProj,
                                  cameraPosition, m_waterUvScale, m_waterUvOrigin, m_waterSphereRadius);
    } else {
        m_ocean.applyToVertices(m_waterVertices, m_waterBaseHeight, &m_waterBaseVertices);
    }
    const GpuMesh& mesh = m_meshes[m_waterMeshIndex];
    const bool uploaded =
        m_resources->updateBuffer(mesh.vertexBuffer, m_waterVertices.data(),
                                  static_cast<uint32_t>(m_waterVertices.size() * sizeof(asset::MeshVertex)));
    m_waterLastInvViewProj = invViewProj;
    m_waterLastCameraPosition = cameraPosition;
    m_waterCameraValid = uploaded;
    return uploaded;
}

bool MaterialSystem::rebuildWaterScreenGrid(gfx::ResourceManager& resources, RenderScene& scene, uint16_t width,
                                            uint16_t height) {
    if (!m_waterActive || m_waterMeshIndex >= scene.meshes.size() || m_waterMeshIndex >= m_meshes.size() ||
        width == 0 || height == 0) {
        return false;
    }

    asset::Mesh& waterMesh = scene.meshes[m_waterMeshIndex];
    WaterScreenGridDesc desc;
    if (!configureWaterScreenGrid(waterMesh, width, height, &desc, &m_waterSource)) {
        HBG_LOGW("material system: water screen grid rebuild failed for viewport {}x{}", width, height);
        return false;
    }

    m_waterBaseVertices = waterMesh.vertices;
    m_waterVertices = waterMesh.vertices;
    m_waterGridWidth = desc.width;
    m_waterGridHeight = desc.height;
    m_waterCameraValid = false;
    m_oceanGpu.invalidateDisplacement();

    GpuMesh& gpuMesh = m_meshes[m_waterMeshIndex];
    if (gpuMesh.wireframeIndexBuffer.valid()) {
        resources.destroyBuffer(gpuMesh.wireframeIndexBuffer);
        gpuMesh.wireframeIndexBuffer = {};
    }
    gpuMesh.wireframeIndexCount = 0;
    if (gpuMesh.vertexBuffer.valid()) {
        resources.destroyBuffer(gpuMesh.vertexBuffer);
        gpuMesh.vertexBuffer = {};
    }
    if (gpuMesh.indexBuffer.valid()) {
        const auto owned = std::find(m_ownedIndexBuffers.begin(), m_ownedIndexBuffers.end(), gpuMesh.indexBuffer);
        if (owned != m_ownedIndexBuffers.end()) {
            m_ownedIndexBuffers.erase(owned);
        }
        resources.destroyBuffer(gpuMesh.indexBuffer);
        gpuMesh.indexBuffer = {};
    }

    gpuMesh.vertexBuffer = createWaterVertexBuffer(resources, waterMesh, waterMesh.name + ".vb", m_oceanGpu.valid());

    gfx::BufferDesc indexDesc;
    indexDesc.type = gfx::BufferType::Index;
    indexDesc.data = waterMesh.indices.data();
    indexDesc.size = static_cast<uint32_t>(waterMesh.indices.size() * sizeof(uint32_t));
    indexDesc.count = static_cast<uint32_t>(waterMesh.indices.size());
    indexDesc.flags = BGFX_BUFFER_INDEX32;
    indexDesc.name = waterMesh.name + ".ib";
    gpuMesh.indexBuffer = resources.createBuffer(indexDesc);
    if (gpuMesh.indexBuffer.valid()) {
        m_ownedIndexBuffers.push_back(gpuMesh.indexBuffer);
    }

    if (!gpuMesh.vertexBuffer.valid() || !gpuMesh.indexBuffer.valid()) {
        HBG_LOGE("material system: failed to recreate water GPU buffers after resize");
        return false;
    }

    HBG_LOGI("material system: water screen grid rebuilt for viewport {}x{} ({}x{}, {} vertices, {} indices)", width,
             height, m_waterGridWidth, m_waterGridHeight, m_waterVertices.size(), waterMesh.indices.size());
    return true;
}

bool MaterialSystem::rebuildRiver(gfx::ResourceManager& resources, RenderScene& scene) {
    if (!scene.river.enabled || m_riverMeshIndex == UINT32_MAX || m_riverMeshIndex >= scene.meshes.size() ||
        m_riverMeshIndex >= m_meshes.size()) {
        return false;
    }

    asset::Mesh mesh = buildRiverMesh(scene.river.graph, scene.river.mesh, scene.river.materialIndex, "RiverSurface");
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        HBG_LOGE("material system: rebuilt river mesh is empty");
        return false;
    }
    if (mesh.vertices.size() != m_riverVertexCount || mesh.indices.size() != m_riverIndexCount) {
        HBG_LOGE("material system: river mesh topology changed (vertices={} indices={})", mesh.vertices.size(),
                 mesh.indices.size());
        return false;
    }

    GpuMesh& gpuMesh = m_meshes[m_riverMeshIndex];
    if (!gpuMesh.vertexBuffer.valid() || !gpuMesh.indexBuffer.valid()) {
        return false;
    }
    if (!resources.updateBuffer(gpuMesh.vertexBuffer, mesh.vertices.data(),
                                static_cast<uint32_t>(mesh.vertices.size() * sizeof(asset::MeshVertex)))) {
        HBG_LOGE("material system: failed to update river GPU buffers");
        return false;
    }

    if (!m_river.init(scene.river.graph, scene.river.mesh, scene.river.particles)) {
        HBG_LOGE("material system: failed to rebuild river particle system");
        return false;
    }

    if (m_riverTerrainMeshIndex < m_meshes.size() && m_riverTerrainMeshIndex < scene.meshes.size()) {
        std::vector<float> exactDelta;
        const bool deltaChanged = computeRiverTerrainDelta(scene.river.graph, scene.river.mesh,
                                                           scene.river.terrain, m_riverTerrainBaseVertices, exactDelta);
        std::vector<asset::MeshVertex> exactVertices;
        applyRiverTerrainDelta(m_riverTerrainBaseVertices, exactDelta, scene.meshes[m_riverTerrainMeshIndex].indices,
                               exactVertices);
        if (exactVertices.size() != m_riverTerrainBaseVertices.size()) {
            exactVertices = m_riverTerrainBaseVertices;
        }

        bool terrainChanged = false;
        bool usedResidualCache = false;
        if (m_riverTerrainResidual.valid()) {
            std::vector<float> cachedDelta;
            if (m_riverTerrainResidual.readBack(m_riverTerrainBaseVertices, exactVertices, exactDelta, cachedDelta)) {
                m_riverTerrainDelta = std::move(cachedDelta);
                terrainChanged = applyRiverTerrainDelta(
                    m_riverTerrainBaseVertices, m_riverTerrainDelta,
                    scene.meshes[m_riverTerrainMeshIndex].indices, m_riverTerrainVertices);
                usedResidualCache = true;
            }
        }
        if (!usedResidualCache) {
            m_riverTerrainDelta = std::move(exactDelta);
            terrainChanged =
                applyRiverTerrainDelta(m_riverTerrainBaseVertices, m_riverTerrainDelta,
                                       scene.meshes[m_riverTerrainMeshIndex].indices, m_riverTerrainVertices);
        }

        if (usedResidualCache && (deltaChanged || terrainChanged) && !m_riverTerrainCacheLogged) {
            const RiverTerrainResidualStats& stats = m_riverTerrainResidual.stats();
            HBG_LOGI("river terrain residual readback: leaves={} parents={} cached_range=[{:.2f}, {:.2f}] "
                     "max_error={:.5f}",
                     stats.modifiedLeafTiles, stats.modifiedParentTiles, stats.reconstructedMin,
                     stats.reconstructedMax, stats.maxError);
            m_riverTerrainCacheLogged = true;
        }

        if (terrainChanged || m_riverTerrainDeformed) {
            GpuMesh& terrainMesh = m_meshes[m_riverTerrainMeshIndex];
            if (terrainMesh.vertexBuffer.valid() &&
                resources.updateBuffer(
                    terrainMesh.vertexBuffer, m_riverTerrainVertices.data(),
                    static_cast<uint32_t>(m_riverTerrainVertices.size() * sizeof(asset::MeshVertex)))) {
                scene.meshes[m_riverTerrainMeshIndex].vertices = m_riverTerrainVertices;
                scene.meshes[m_riverTerrainMeshIndex].recomputeBounds();
                if (m_riverTerrainObjectIndex < scene.objects.size()) {
                    scene.objects[m_riverTerrainObjectIndex].worldBounds = scene.meshes[m_riverTerrainMeshIndex].bounds;
                }
                m_riverTerrainDeformed = terrainChanged;
                if ((deltaChanged || terrainChanged) && !m_riverTerrainLogged) {
                    float deltaMin = 0.0f;
                    float deltaMax = 0.0f;
                    std::size_t changedVertices = 0;
                    for (float value : m_riverTerrainDelta) {
                        if (std::abs(value) <= 1.0e-5f) {
                            continue;
                        }
                        deltaMin = std::min(deltaMin, value);
                        deltaMax = std::max(deltaMax, value);
                        ++changedVertices;
                    }
                    HBG_LOGI("river terrain: residual delta min={:.2f} max={:.2f} changed={} uploaded={} cache={}",
                             deltaMin, deltaMax, changedVertices, m_riverTerrainVertices.size(), usedResidualCache);
                    m_riverTerrainLogged = true;
                }
            } else {
                HBG_LOGW("material system: failed to update river terrain GPU buffer");
            }
        }
    }

    const Bounds bounds = mesh.bounds;
    scene.meshes[m_riverMeshIndex] = std::move(mesh);
    if (m_riverObjectIndex < scene.objects.size()) {
        scene.objects[m_riverObjectIndex].worldBounds = bounds;
    }
    scene.recomputeBounds();

    const uint32_t size = m_river.textureSize();
    if (m_riverBaseColorTexture.valid()) {
        const std::vector<uint8_t>& pixels = m_river.baseColorPixels();
        resources.updateTexture(m_riverBaseColorTexture, pixels.data(), static_cast<uint32_t>(pixels.size()),
                                static_cast<uint16_t>(size), static_cast<uint16_t>(size),
                                static_cast<uint16_t>(size * 4));
    }
    if (m_riverNormalTexture.valid()) {
        if (!uploadRiverNormal()) {
            HBG_LOGW("material system: failed to upload rebuilt river normal mip chain");
            return false;
        }
    }
    if (m_riverWaveBaseTexture.valid()) {
        m_riverWaveFrameUploaded = UINT32_MAX;
        if (!uploadRiverWaveBase()) {
            HBG_LOGW("material system: failed to upload rebuilt river wave mip chain");
            return false;
        }
    }
    m_riverParticleLogged = false;
    m_riverLeavingLogged = false;
    m_riverNearLogged = false;
    m_riverOutsideLogged = false;
    return true;
}

bool MaterialSystem::ensureWireframeBuffers(gfx::ResourceManager& resources, const RenderScene& scene) {
    if (m_meshes.size() != scene.meshes.size()) {
        return false;
    }

    for (std::size_t meshIndex = 0; meshIndex < m_meshes.size(); ++meshIndex) {
        GpuMesh& gpuMesh = m_meshes[meshIndex];
        const asset::Mesh& mesh = scene.meshes[meshIndex];
        if (gpuMesh.wireframeIndexBuffer.valid() || mesh.indices.empty()) {
            continue;
        }

        std::vector<uint32_t> wireframeIndices;
        wireframeIndices.reserve(mesh.indices.size() * 2);
        for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
            const uint32_t a = mesh.indices[index + 0];
            const uint32_t b = mesh.indices[index + 1];
            const uint32_t c = mesh.indices[index + 2];
            wireframeIndices.push_back(a);
            wireframeIndices.push_back(b);
            wireframeIndices.push_back(b);
            wireframeIndices.push_back(c);
            wireframeIndices.push_back(c);
            wireframeIndices.push_back(a);
        }
        if (wireframeIndices.empty()) {
            continue;
        }

        gfx::BufferDesc desc;
        desc.type = gfx::BufferType::Index;
        desc.data = wireframeIndices.data();
        desc.size = static_cast<uint32_t>(wireframeIndices.size() * sizeof(uint32_t));
        desc.count = static_cast<uint32_t>(wireframeIndices.size());
        desc.flags = BGFX_BUFFER_INDEX32;
        desc.name = mesh.name + ".wire.ib";
        gpuMesh.wireframeIndexBuffer = resources.createBuffer(desc);
        gpuMesh.wireframeIndexCount = static_cast<uint32_t>(wireframeIndices.size());
        if (!gpuMesh.wireframeIndexBuffer.valid()) {
            HBG_LOGE("material system: failed to create wireframe index buffer for '{}'", mesh.name);
            return false;
        }
    }
    return true;
}

gfx::TextureHandle MaterialSystem::textureForImage(gfx::ResourceManager& resources, const RenderScene& scene,
                                                   const asset::TextureRef& ref, bool srgb, bool normalMap) {
    if (!ref.valid() || static_cast<std::size_t>(ref.imageIndex) >= scene.images.size()) {
        return normalMap ? m_normalFlat : (srgb ? m_whiteSrgb : m_whiteLinear);
    }

    const uint64_t key = textureKey(ref.imageIndex, srgb) ^ (normalMap ? 0x8000000000000000ULL : 0);
    const auto cached = m_textureCache.find(key);
    if (cached != m_textureCache.end()) {
        return cached->second;
    }

    const asset::ImageAsset& image = scene.images[static_cast<std::size_t>(ref.imageIndex)];
    const gfx::TextureHandle texture = asset::TextureLoader::createTexture(resources, image, srgb, image.name);
    if (!texture.valid()) {
        HBG_LOGW("material system: failed to upload image '{}', using fallback", image.name);
        return normalMap ? m_normalFlat : (srgb ? m_whiteSrgb : m_whiteLinear);
    }
    m_textureCache.emplace(key, texture);
    return texture;
}

bool MaterialSystem::uploadRiverWaveBase() {
    if (m_resources == nullptr || !m_riverWaveBaseTexture.valid()) {
        return false;
    }

    const uint32_t size = m_river.textureSize();
    const auto& mips = m_river.baseNormalMips();
    if (mips.empty()) {
        const std::vector<float>& pixels = m_river.baseNormalPixels();
        if (pixels.empty()) {
            return false;
        }
        return uploadRiverHalfTexture(*m_resources, m_riverWaveBaseTexture, pixels, static_cast<uint16_t>(size),
                                      static_cast<uint16_t>(size), 0, "river_wave_base");
    }

    for (uint32_t mip = 0; mip < mips.size(); ++mip) {
        const uint32_t mipSize = std::max(1u, size >> mip);
        const std::vector<float>& pixels = mips[mip];
        const std::size_t expected = static_cast<std::size_t>(mipSize) * mipSize * 4u;
        if (pixels.size() != expected) {
            HBG_LOGW("material system: river wave mip {} has {} texels, expected {}", mip, pixels.size(), expected);
            return false;
        }
        if (!uploadRiverHalfTexture(*m_resources, m_riverWaveBaseTexture, pixels,
                                    static_cast<uint16_t>(mipSize), static_cast<uint16_t>(mipSize),
                                    static_cast<uint8_t>(mip), "river_wave_base")) {
            return false;
        }
    }
    m_riverWaveFrameUploaded = m_river.waveFrame();
    return true;
}

bool MaterialSystem::uploadRiverNormal() {
    if (m_resources == nullptr || !m_riverNormalTexture.valid()) {
        return false;
    }
    const uint32_t size = m_river.textureSize();
    const auto& mips = m_river.normalMips();
    if (mips.empty()) {
        const std::vector<float>& pixels = m_river.normalPixels();
        if (pixels.empty()) {
            return false;
        }
        return uploadRiverHalfTexture(*m_resources, m_riverNormalTexture, pixels, static_cast<uint16_t>(size),
                                      static_cast<uint16_t>(size), 0, "river_wave_normal");
    }
    for (uint32_t mip = 0; mip < mips.size(); ++mip) {
        const uint32_t mipSize = std::max(1u, size >> mip);
        const std::vector<float>& pixels = mips[mip];
        const std::size_t expected = static_cast<std::size_t>(mipSize) * mipSize * 4u;
        if (pixels.size() != expected) {
            HBG_LOGW("material system: river normal mip {} has {} texels, expected {}", mip, pixels.size(), expected);
            return false;
        }
        if (!uploadRiverHalfTexture(*m_resources, m_riverNormalTexture, pixels, static_cast<uint16_t>(mipSize),
                                    static_cast<uint16_t>(mipSize), static_cast<uint8_t>(mip), "river_wave_normal")) {
            return false;
        }
    }
    return true;
}

bool MaterialSystem::updateRiver(float timeSeconds, float deltaSeconds, const Mat4& viewProj, const Mat4& invViewProj,
                                 const Mat4& rasterInvViewProj, const Vec3& cameraPosition, uint16_t width,
                                 uint16_t height) {
    if (!m_river.valid() || !m_riverNormalTexture.valid() || m_resources == nullptr) {
        return false;
    }
    m_river.update(timeSeconds, deltaSeconds, viewProj, invViewProj, cameraPosition, width, height);
    if (m_riverWaveBaseTexture.valid() && m_river.waveFrame() != m_riverWaveFrameUploaded) {
        if (!uploadRiverWaveBase()) {
            HBG_LOGW("material system: failed to upload river wave mip chain");
        }
        if (!m_river.cpuNormalSplatEnabled() && !uploadRiverNormal()) {
            HBG_LOGW("material system: failed to upload river normal mip chain");
        }
    }
    if (!m_riverParticleLogged && m_river.particleCount() > 0) {
        HBG_LOGI("river particles: active={} river={} leaving={} near={} outside={} depth={} fading={} screen={}x{} "
                 "texture={}",
                 m_river.particleCount(), m_river.worldParticleCount(), m_river.leavingParticleCount(),
                 m_river.nearParticleCount(), m_river.outsideParticleCount(), m_river.depthWorldParticleCount(),
                 m_river.fadingParticleCount(), width, height, m_river.textureSize());
        m_riverParticleLogged = true;
    }
    if (!m_riverLeavingLogged && m_river.leavingParticleCount() > 0) {
        HBG_LOGI("river particles: first LEAVING status active={} river={} leaving={} near={} outside={} fading={}",
                 m_river.particleCount(), m_river.worldParticleCount(), m_river.leavingParticleCount(),
                 m_river.nearParticleCount(), m_river.outsideParticleCount(), m_river.fadingParticleCount());
        m_riverLeavingLogged = true;
    }
    if (!m_riverNearLogged && m_river.nearKillCount() > 0) {
        HBG_LOGI("river particles: first NEAR kill active={} river={} leaving={} near={} near_killed={} "
                 "outside={} fading={}",
                 m_river.particleCount(), m_river.worldParticleCount(), m_river.leavingParticleCount(),
                 m_river.nearParticleCount(), m_river.nearKillCount(), m_river.outsideParticleCount(),
                 m_river.fadingParticleCount());
        m_riverNearLogged = true;
    }
    if (!m_riverOutsideLogged && m_river.outsideParticleCount() > 0) {
        HBG_LOGI("river particles: first OUTSIDE active={} river={} leaving={} near={} outside={} fading={}",
                 m_river.particleCount(), m_river.worldParticleCount(), m_river.leavingParticleCount(),
                 m_river.nearParticleCount(), m_river.outsideParticleCount(), m_river.fadingParticleCount());
        m_riverOutsideLogged = true;
    }
    if (m_riverGpu.valid() &&
        m_riverGpu.update(*m_resources, m_river, rasterInvViewProj, cameraPosition, width, height)) {
        return true;
    }
    if (m_river.normalPixels().empty()) {
        return false;
    }
    return uploadRiverNormal();
}

void MaterialSystem::submitRiverDepthSnapshot(std::vector<float> depths, uint16_t width, uint16_t height,
                                              const Mat4& viewProj) {
    m_river.setDepthSnapshot(std::move(depths), width, height, viewProj);
}

void MaterialSystem::submitRiverParticleDepths(std::vector<float> depths, std::vector<RiverDepthQuery> queries,
                                               const Mat4& viewProj, uint16_t width, uint16_t height) {
    m_river.submitParticleDepths(std::move(depths), std::move(queries), viewProj, width, height);
}

bool MaterialSystem::riverDepthSnapshotMatches(const Mat4& viewProj, uint16_t width, uint16_t height) const {
    return m_river.depthSnapshotMatches(viewProj, width, height);
}

bool MaterialSystem::riverHasDepthSnapshot() const { return m_river.hasDepthSnapshot(); }

void MaterialSystem::submitRiverParticles(gfx::Encoder& encoder, bgfx::ViewId view,
                                          gfx::FrameBufferHandle frameBuffer, gfx::TextureHandle depthTexture) {
    if (m_riverGpu.valid()) {
        m_riverGpu.submit(encoder, view, frameBuffer, depthTexture);
    }
}

void MaterialSystem::shutdown(gfx::ResourceManager& resources) {
    for (const auto& [key, texture] : m_textureCache) {
        (void)key;
        if (texture.valid()) {
            resources.destroyTexture(texture);
        }
    }
    m_textureCache.clear();

    m_oceanGpu.shutdown(resources);
    m_riverGpu.shutdown(resources);
    if (m_waterFftTexture.valid()) {
        resources.destroyTexture(m_waterFftTexture);
    }
    if (m_waterFftFallback.valid()) {
        resources.destroyTexture(m_waterFftFallback);
    }
    if (m_riverWaveBaseTexture.valid()) {
        resources.destroyTexture(m_riverWaveBaseTexture);
    }
    if (m_riverNormalTexture.valid()) {
        resources.destroyTexture(m_riverNormalTexture);
    }
    if (m_riverBaseColorTexture.valid()) {
        resources.destroyTexture(m_riverBaseColorTexture);
    }
    if (m_normalFlat.valid()) {
        resources.destroyTexture(m_normalFlat);
    }
    if (m_whiteLinear.valid()) {
        resources.destroyTexture(m_whiteLinear);
    }
    if (m_whiteSrgb.valid()) {
        resources.destroyTexture(m_whiteSrgb);
    }
    m_waterFftTexture = {};
    m_waterFftFallback = {};
    m_riverNormalTexture = {};
    m_riverWaveBaseTexture = {};
    m_riverBaseColorTexture = {};
    m_riverWaveFrameUploaded = UINT32_MAX;
    m_river = RiverParticleSystem{};
    m_riverMeshIndex = UINT32_MAX;
    m_riverObjectIndex = UINT32_MAX;
    m_riverVertexCount = 0;
    m_riverIndexCount = 0;
    m_riverTerrainBaseVertices.clear();
    m_riverTerrainVertices.clear();
    m_riverTerrainDelta.clear();
    m_riverTerrainResidual.reset();
    m_riverTerrainMeshIndex = UINT32_MAX;
    m_riverTerrainObjectIndex = UINT32_MAX;
    m_riverTerrainLogged = false;
    m_riverTerrainCacheLogged = false;
    m_riverTerrainDeformed = false;
    m_normalFlat = {};
    m_whiteLinear = {};
    m_whiteSrgb = {};

    for (GpuMesh& mesh : m_meshes) {
        if (mesh.wireframeIndexBuffer.valid()) {
            resources.destroyBuffer(mesh.wireframeIndexBuffer);
        }
        if (mesh.vertexBuffer.valid()) {
            resources.destroyBuffer(mesh.vertexBuffer);
        }
    }
    m_meshes.clear();

    for (const gfx::BufferHandle buffer : m_ownedIndexBuffers) {
        if (buffer.valid()) {
            resources.destroyBuffer(buffer);
        }
    }
    m_ownedIndexBuffers.clear();
    m_indexBufferBuckets.clear();
    m_indexBytesSaved = 0;
    m_reusedIndexBuffers = 0;

    m_samplers.shutdown(resources);
    m_materials.clear();
    m_decals.clear();
    m_ocean = OceanFFT{};
    m_waterVertices.clear();
    m_waterBaseVertices.clear();
    m_waterMeshIndex = UINT32_MAX;
    m_waterSphereRadius = 0.0f;
    m_waterGridWidth = 0;
    m_waterGridHeight = 0;
    m_waterSource = WaterScreenGridSource{};
    m_waterCameraValid = false;
    m_waterActive = false;
    m_resources = nullptr;
}

} // namespace hbg::render
