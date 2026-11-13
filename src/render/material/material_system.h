#pragma once

#include "asset/material.h"
#include "gfx/buffer.h"
#include "gfx/generated/uniforms.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/scene/render_scene.h"

#include <cstdint>
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
    void shutdown(gfx::ResourceManager& resources);

    const std::vector<GpuMesh>& meshes() const { return m_meshes; }
    const std::vector<GpuMaterial>& materials() const { return m_materials; }
    const std::vector<GpuDecalTextures>& decals() const { return m_decals; }
    const MaterialSamplers& samplers() const { return m_samplers; }

  private:
    gfx::TextureHandle textureForImage(gfx::ResourceManager& resources, const RenderScene& scene,
                                       const asset::TextureRef& ref, bool srgb, bool normalMap);

    gfx::ResourceManager* m_resources = nullptr;
    std::vector<GpuMesh> m_meshes;
    std::vector<GpuMaterial> m_materials;
    std::vector<GpuDecalTextures> m_decals;
    std::unordered_map<uint64_t, gfx::TextureHandle> m_textureCache;
    MaterialSamplers m_samplers;
    gfx::TextureHandle m_whiteSrgb;
    gfx::TextureHandle m_whiteLinear;
    gfx::TextureHandle m_normalFlat;
};

} // namespace hbg::render
