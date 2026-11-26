#pragma once

#include "asset/material.h"
#include "gfx/buffer.h"
#include "gfx/generated/uniforms.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/scene/render_scene.h"

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace hbg::render {

struct GpuMesh {
    gfx::BufferHandle vertexBuffer;
    gfx::BufferHandle cpuVertexBuffer;
    gfx::BufferHandle indexBuffer;
    gfx::BufferHandle wireframeIndexBuffer;
    gfx::BufferHandle skinBaseBuffer;
    gfx::BufferHandle skinInfoBuffer;
    gfx::BufferHandle skinInfluenceBuffer;
    gfx::BufferHandle skinJointBuffer;
    gfx::BufferHandle skinOutputBuffer;
    std::vector<Mat4> previousJointMatrices;
    uint32_t wireframeIndexCount = 0;
    uint32_t bindlessVertexBase = 0;
    uint32_t bindlessIndexBase = 0;
    bool dynamicVertex = false;
    bool gpuSkinCapable = false;
    bool bindlessEligible = false;
    std::vector<Vec3> previousPositions;
};

struct GpuMaterial {
    gfx::TextureHandle baseColorTexture;
    gfx::TextureHandle metallicRoughnessTexture;
    gfx::TextureHandle normalTexture;
    gfx::TextureHandle occlusionTexture;
    gfx::TextureHandle emissiveTexture;
    gfx::generated::MaterialUniforms uniforms;
    std::array<Vec2, 5> bindlessUvScale = {Vec2(1.0f, 1.0f), Vec2(1.0f, 1.0f), Vec2(1.0f, 1.0f),
                                           Vec2(1.0f, 1.0f), Vec2(1.0f, 1.0f)};
    std::array<Vec2, 5> bindlessUvOffset = {Vec2(0.0f, 0.0f), Vec2(0.0f, 0.0f), Vec2(0.0f, 0.0f),
                                            Vec2(0.0f, 0.0f), Vec2(0.0f, 0.0f)};
    uint32_t bindlessWrapBits = 0;
    uint64_t variantKey = 0;
    uint32_t features = 0;
    bool bindlessHeterogeneous = false;
};

struct GpuDecalTextures {
    gfx::TextureHandle albedo;
    gfx::TextureHandle normal;
    gfx::TextureHandle mask;
};

struct GpuMaterialTextureArrays {
    gfx::TextureHandle baseColor;
    gfx::TextureHandle metallicRoughness;
    gfx::TextureHandle normal;
    gfx::TextureHandle occlusion;
    gfx::TextureHandle emissive;

    bool valid() const {
        return baseColor.valid() && metallicRoughness.valid() && normal.valid() && occlusion.valid() &&
               emissive.valid();
    }
};

struct MaterialSamplers {
    gfx::UniformHandle baseColorMap;
    gfx::UniformHandle metallicRoughnessMap;
    gfx::UniformHandle normalMap;
    gfx::UniformHandle occlusionMap;
    gfx::UniformHandle emissiveMap;

    bool init(gfx::ResourceManager& resources);
    void shutdown(gfx::ResourceManager& resources);
};

class MaterialSystem {
  public:
    MaterialSystem() = default;
    ~MaterialSystem() = default;

    MaterialSystem(const MaterialSystem&) = delete;
    MaterialSystem& operator=(const MaterialSystem&) = delete;

    bool init(gfx::ResourceManager& resources, const RenderScene& scene);
    bool ensureWireframeBuffers(gfx::ResourceManager& resources, const RenderScene& scene);
    bool updateDeformedMeshes(RenderScene& scene);
    void setGpuSkinningEnabled(bool enabled) { m_gpuSkinningEnabled = enabled; }
    bool gpuSkinningEnabled() const { return m_gpuSkinningEnabled; }
    const std::vector<uint32_t>& gpuSkinMeshes() const { return m_gpuSkinMeshes; }
    void shutdown(gfx::ResourceManager& resources);

    const std::vector<GpuMesh>& meshes() const { return m_meshes; }
    const std::vector<GpuMaterial>& materials() const { return m_materials; }
    const std::vector<GpuDecalTextures>& decals() const { return m_decals; }
    const MaterialSamplers& samplers() const { return m_samplers; }
    const GpuMaterialTextureArrays& bindlessTextures() const { return m_bindlessTextures; }
    gfx::BufferHandle bindlessVertexBuffer() const { return m_bindlessVertexBuffer; }
    gfx::BufferHandle bindlessIndexBuffer() const { return m_bindlessIndexBuffer; }
    bool bindlessReady() const;
    bool ensureBindlessResources(const RenderScene& scene);

  private:
    bool updateGpuSkinMesh(uint32_t meshIndex, const asset::Mesh& mesh,
                           const std::vector<Mat4>& jointMatrices);
    bool buildBindlessResources(const RenderScene& scene);
    gfx::TextureHandle textureForImage(gfx::ResourceManager& resources, const RenderScene& scene,
                                       const asset::TextureRef& ref, bool srgb, bool normalMap);
    gfx::TextureHandle packedMaskTexture(gfx::ResourceManager& resources, const RenderScene& scene,
                                         const asset::Material& material);

    gfx::ResourceManager* m_resources = nullptr;
    std::vector<GpuMesh> m_meshes;
    std::vector<uint32_t> m_gpuSkinMeshes;
    std::vector<GpuMaterial> m_materials;
    std::vector<GpuDecalTextures> m_decals;
    bool m_gpuSkinningEnabled = true;
    std::unordered_map<uint64_t, gfx::TextureHandle> m_textureCache;
    std::unordered_map<uint64_t, gfx::TextureHandle> m_packedMaskCache;
    MaterialSamplers m_samplers;
    gfx::TextureHandle m_whiteSrgb;
    gfx::TextureHandle m_whiteLinear;
    gfx::TextureHandle m_normalFlat;
    GpuMaterialTextureArrays m_bindlessTextures;
    gfx::BufferHandle m_bindlessVertexBuffer;
    gfx::BufferHandle m_bindlessIndexBuffer;
    bool m_bindlessAttempted = false;
};

} // namespace hbg::render
