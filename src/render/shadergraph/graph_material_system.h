#pragma once

#include "asset/material.h"
#include "gfx/encoder.h"
#include "gfx/resource_manager.h"
#include "render/scene/render_scene.h"
#include "render/shadergraph/graph_binding.h"
#include "render/shadergraph/graph_program_cache.h"
#include "render/shadergraph/sgb_asset.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace hbg::render {

class GraphMaterialSystem {
  public:
    GraphMaterialSystem() = default;
    ~GraphMaterialSystem() = default;

    GraphMaterialSystem(const GraphMaterialSystem&) = delete;
    GraphMaterialSystem& operator=(const GraphMaterialSystem&) = delete;

    bool init(gfx::ResourceManager& resources, RenderScene& scene, std::string* error = nullptr);
    void shutdown(gfx::ResourceManager& resources);
    void poll(gfx::ResourceManager& resources, RenderScene& scene);
    void setGlobalMipBias(float bias) { m_globalMipBias = bias; }

    bool hasMaterial(uint32_t materialIndex) const;
    bool hasAnyMaterial() const { return m_enabledCount != 0; }
    bool isUnlit(uint32_t materialIndex) const;
    uint64_t variantKey(uint32_t materialIndex) const;
    const graph::Asset* asset(uint32_t materialIndex) const;
    gfx::ProgramHandle program(uint32_t materialIndex, graph::PassKind pass);
    bool bind(uint32_t materialIndex, gfx::Encoder& encoder, const Mat4& model,
              gfx::TextureHandle sceneColor = gfx::TextureHandle{},
              gfx::TextureHandle sceneDepth = gfx::TextureHandle{}) const;

  private:
    struct Runtime {
        bool enabled = false;
        bool unlit = false;
        std::filesystem::path path;
        std::shared_ptr<graph::Asset> asset;
        std::unique_ptr<graph::GraphBinding> binding;
        uint64_t variantKey = 0;
    };

    std::shared_ptr<graph::Asset> loadAsset(const std::filesystem::path& path, std::string& error);
    std::filesystem::path resolvePath(const asset::Material& material, const RenderScene& scene) const;
    bool configureRuntime(Runtime& runtime, asset::Material& material, const RenderScene& scene,
                          std::string& error);
    void destroyRuntime(Runtime& runtime);

    gfx::ResourceManager* m_resources = nullptr;
    graph::GraphUniformCache m_uniforms;
    graph::GraphProgramCache m_programs;
    graph::GraphBindingFallbacks m_fallbacks;
    gfx::UniformHandle m_sceneColorSampler;
    gfx::UniformHandle m_sceneDepthSampler;
    gfx::UniformHandle m_modelInverseUniform;
    float m_globalMipBias = 0.0f;
    std::unordered_map<std::string, std::shared_ptr<graph::Asset>> m_assets;
    std::unordered_set<std::string> m_failedReloads;
    std::vector<Runtime> m_materials;
    bool m_initialized = false;
    uint32_t m_enabledCount = 0;
};

} // namespace hbg::render
