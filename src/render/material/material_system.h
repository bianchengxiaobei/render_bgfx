#pragma once

#include "asset/material.h"
#include "gfx/buffer.h"
#include "gfx/generated/uniforms.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/river/river_particles.h"
#include "render/river/river_particles_gpu.h"
#include "render/river/river_terrain.h"
#include "render/scene/render_scene.h"
#include "render/water/ocean_fft.h"
#include "render/water/ocean_fft_gpu.h"
#include "render/water/water_screen_grid.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <unordered_map>
#include <vector>

namespace hbg::render {

struct GpuMesh {
    gfx::BufferHandle vertexBuffer;
    gfx::BufferHandle indexBuffer;
    gfx::BufferHandle wireframeIndexBuffer;
    uint32_t wireframeIndexCount = 0;
};

struct GpuMaterial {
    gfx::TextureHandle baseColorTexture;
    gfx::TextureHandle metallicRoughnessTexture;
    gfx::TextureHandle normalTexture;
    gfx::TextureHandle occlusionTexture;
    gfx::TextureHandle emissiveTexture;
    gfx::generated::MaterialUniforms uniforms;
    uint64_t variantKey = 0;
    uint32_t features = 0;
};

struct GpuDecalTextures {
    gfx::TextureHandle albedo;
    gfx::TextureHandle normal;
    gfx::TextureHandle mask;
};

struct MaterialSamplers {
    gfx::UniformHandle baseColorMap;
    gfx::UniformHandle metallicRoughnessMap;
    gfx::UniformHandle normalMap;
    gfx::UniformHandle occlusionMap;
    gfx::UniformHandle emissiveMap;
    gfx::UniformHandle waterFft;
    gfx::UniformHandle riverParticleNormal;

    bool init(gfx::ResourceManager& resources);
    void shutdown(gfx::ResourceManager& resources);
};

class MaterialSystem {
  public:
    MaterialSystem() = default;
    ~MaterialSystem() = default;

    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;

    bool init(gfx::ResourceManager& resources, const RenderScene& scene, const std::filesystem::path& shaderRoot = {});
    bool ensureWireframeBuffers(gfx::ResourceManager& resources, const RenderScene& scene);
    bool updateWater(float timeSeconds, const Mat4& invViewProj, const Vec3& cameraPosition);
    bool updateRiver(float timeSeconds, float deltaSeconds, const Mat4& viewProj, const Mat4& invViewProj,
                     const Mat4& rasterInvViewProj, const Vec3& cameraPosition, uint16_t width, uint16_t height);
    void submitRiverDepthSnapshot(std::vector<float> depths, uint16_t width, uint16_t height, const Mat4& viewProj);
    void submitRiverParticleDepths(std::vector<float> depths, std::vector<RiverDepthQuery> queries,
                                   const Mat4& viewProj, uint16_t width, uint16_t height);
    bool riverDepthSnapshotMatches(const Mat4& viewProj, uint16_t width, uint16_t height) const;
    bool riverHasDepthSnapshot() const;
    RiverDepthRequestMode riverDepthRequestMode() const { return m_river.depthRequestMode(); }
    std::vector<RiverDepthQuery> takeRiverDepthQueries() { return m_river.takeDepthQueries(); }
    uint32_t riverParticleCapacity() const { return m_river.particleCapacity(); }
    bool riverParticlesActive() const { return m_river.valid(); }
    void submitRiverParticles(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                              gfx::TextureHandle depthTexture);
    bool rebuildWaterScreenGrid(gfx::ResourceManager& resources, RenderScene& scene, uint16_t width, uint16_t height);
    bool rebuildRiver(gfx::ResourceManager& resources, RenderScene& scene);
    void shutdown(gfx::ResourceManager& resources);

    const std::vector<GpuMesh>& meshes() const { return m_meshes; }
    const std::vector<GpuMaterial>& materials() const { return m_materials; }
    const std::vector<GpuDecalTextures>& decals() const { return m_decals; }
    const MaterialSamplers& samplers() const { return m_samplers; }
    gfx::TextureHandle waterFftTexture() const {
        if (m_oceanGpu.valid() && m_oceanGpu.texture().valid()) {
            return m_oceanGpu.texture();
        }
        return m_waterFftTexture.valid() ? m_waterFftTexture : m_waterFftFallback;
    }
    gfx::TextureHandle riverParticleNormalTexture() const { return m_riverGpu.normalTexture(); }
    gfx::TextureHandle fallbackNormalTexture() const { return m_normalFlat; }
    bool riverGpuActive() const { return m_riverGpu.valid(); }
    bool riverParticlePassReady() const { return m_riverGpu.ready(); }
    bool waterGpuActive() const { return m_oceanGpu.valid(); }
    float waterMaxSlopeVariance() const { return m_ocean.maxSlopeVariance(); }
    float waterSphereRadius() const { return m_waterActive ? m_waterSphereRadius : 0.0f; }
    const OceanFFT::SlopeVarianceLut& waterSlopeVarianceLut() const { return m_ocean.slopeVarianceLut(); }
    bool waterSlopeVarianceLutValid() const { return m_waterActive && m_ocean.slopeVarianceLutValid(); }

  private:
    struct SharedIndexBuffer {
        const std::vector<uint32_t>* source = nullptr;
        gfx::BufferHandle handle;
    };

    gfx::TextureHandle textureForImage(gfx::ResourceManager& resources, const RenderScene& scene,
                                       const asset::TextureRef& ref, bool srgb, bool normalMap);
    gfx::BufferHandle acquireIndexBuffer(gfx::ResourceManager& resources, const std::vector<uint32_t>& indices,
                                         const std::string& name);
    gfx::BufferHandle createIndexBuffer(gfx::ResourceManager& resources, const std::vector<uint32_t>& indices,
                                        const std::string& name, bool cacheable);
    gfx::BufferHandle createWaterVertexBuffer(gfx::ResourceManager& resources, const asset::Mesh& mesh,
                                              const std::string& name, bool computeWrite);
    bool createWaterFftTexture();
    bool uploadWaterFftTexture();
    bool uploadRiverWaveBase();
    bool uploadRiverNormal();

    gfx::ResourceManager* m_resources = nullptr;
    std::vector<GpuMesh> m_meshes;
    std::vector<GpuMaterial> m_materials;
    std::vector<GpuDecalTextures> m_decals;
    std::unordered_map<uint64_t, gfx::TextureHandle> m_textureCache;
    std::unordered_map<uint64_t, std::vector<SharedIndexBuffer>> m_indexBufferBuckets;
    std::vector<gfx::BufferHandle> m_ownedIndexBuffers;
    uint64_t m_indexBytesSaved = 0;
    uint32_t m_reusedIndexBuffers = 0;
    MaterialSamplers m_samplers;
    gfx::TextureHandle m_whiteSrgb;
    gfx::TextureHandle m_whiteLinear;
    gfx::TextureHandle m_normalFlat;
    gfx::TextureHandle m_waterFftTexture;
    gfx::TextureHandle m_waterFftFallback;
    gfx::TextureHandle m_riverBaseColorTexture;
    gfx::TextureHandle m_riverNormalTexture;
    gfx::TextureHandle m_riverWaveBaseTexture;
    RiverParticleSystem m_river;
    RiverParticleGpu m_riverGpu;
    uint32_t m_riverMeshIndex = UINT32_MAX;
    uint32_t m_riverObjectIndex = UINT32_MAX;
    uint32_t m_riverVertexCount = 0;
    uint32_t m_riverIndexCount = 0;
    uint32_t m_riverWaveFrameUploaded = UINT32_MAX;
    bool m_riverParticleLogged = false;
    bool m_riverLeavingLogged = false;
    bool m_riverNearLogged = false;
    bool m_riverOutsideLogged = false;
    OceanFFT m_ocean;
    OceanFftGpu m_oceanGpu;
    std::vector<asset::MeshVertex> m_riverTerrainBaseVertices;
    std::vector<asset::MeshVertex> m_riverTerrainVertices;
    std::vector<float> m_riverTerrainDelta;
    RiverTerrainResidualCache m_riverTerrainResidual;
    uint32_t m_riverTerrainMeshIndex = UINT32_MAX;
    uint32_t m_riverTerrainObjectIndex = UINT32_MAX;
    bool m_riverTerrainLogged = false;
    bool m_riverTerrainCacheLogged = false;
    bool m_riverTerrainDeformed = false;
    std::vector<asset::MeshVertex> m_waterVertices;
    std::vector<asset::MeshVertex> m_waterBaseVertices;
    uint32_t m_waterMeshIndex = UINT32_MAX;
    float m_waterBaseHeight = 0.5f;
    float m_waterSphereRadius = 0.0f;
    int m_waterGridWidth = 0;
    int m_waterGridHeight = 0;
    Vec2 m_waterUvScale{1.0f, 1.0f};
    Vec2 m_waterUvOrigin{0.0f, 0.0f};
    WaterScreenGridSource m_waterSource;
    Mat4 m_waterLastInvViewProj{};
    Vec3 m_waterLastCameraPosition{};
    bool m_waterCameraValid = false;
    bool m_waterActive = false;
};

} // namespace hbg::render
