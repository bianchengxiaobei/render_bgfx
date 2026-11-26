#include "render/shadergraph/graph_material_system.h"

#include "core/log.h"

#include <bgfx/bgfx.h>

#include <algorithm>
#include <array>
#include <system_error>

namespace hbg::render {
namespace {

graph::Backend backendForRenderer() {
    switch (bgfx::getRendererType()) {
    case bgfx::RendererType::Vulkan:
        return graph::Backend::Spirv;
    case bgfx::RendererType::Direct3D11:
    case bgfx::RendererType::Direct3D12:
        return graph::Backend::Dxbc;
    default:
        HBG_LOGW("graph material: renderer '{}' has no graphed shader backend; using dxbc",
                 bgfx::getRendererName(bgfx::getRendererType()));
        return graph::Backend::Dxbc;
    }
}

gfx::TextureHandle createSolidTexture(gfx::ResourceManager& resources, uint8_t red, uint8_t green, uint8_t blue,
                                      uint8_t alpha, const char* name, uint16_t layers = 1, bool cubeMap = false,
                                      bool is3D = false, uint16_t depth = 1) {
    const uint32_t faces = cubeMap ? 6u : 1u;
    const uint32_t slices = is3D ? depth : 1u;
    const uint32_t layerCount = cubeMap ? 1u : layers;
    const uint32_t pixelCount = faces * slices * layerCount;
    std::vector<uint8_t> pixels(static_cast<std::size_t>(pixelCount) * 4u);
    for (uint32_t pixel = 0; pixel < pixelCount; ++pixel) {
        pixels[pixel * 4u + 0u] = red;
        pixels[pixel * 4u + 1u] = green;
        pixels[pixel * 4u + 2u] = blue;
        pixels[pixel * 4u + 3u] = alpha;
    }
    gfx::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.layers = layers;
    desc.depth = is3D ? depth : 1;
    desc.format = gfx::TextureFormat::RGBA8;
    desc.cubeMap = cubeMap;
    desc.is3D = is3D;
    desc.data = pixels.data();
    desc.dataSize = static_cast<uint32_t>(pixels.size());
    desc.flags = BGFX_SAMPLER_UVW_CLAMP;
    desc.name = name;
    return resources.createTexture(desc);
}

} // namespace

bool GraphMaterialSystem::init(gfx::ResourceManager& resources, RenderScene& scene, std::string* error) {
    shutdown(resources);
    m_resources = &resources;
    if (!m_uniforms.init(resources)) {
        if (error != nullptr) {
            *error = "failed to initialize graph uniform cache";
        }
        return false;
    }
    if (!m_programs.init(resources, backendForRenderer())) {
        if (error != nullptr) {
            *error = "failed to initialize graph program cache";
        }
        return false;
    }

    m_fallbacks.white = createSolidTexture(resources, 255, 255, 255, 255, "graph.fallback.white");
    m_fallbacks.black = createSolidTexture(resources, 0, 0, 0, 0, "graph.fallback.black");
    m_fallbacks.textureBlack =
        createSolidTexture(resources, 0, 0, 0, 255, "graph.fallback.textureBlack");
    m_fallbacks.grey =
        createSolidTexture(resources, 128, 128, 128, 255, "graph.fallback.grey");
    m_fallbacks.normal =
        createSolidTexture(resources, 128, 128, 255, 255, "graph.fallback.normal");
    m_fallbacks.linearGrey =
        createSolidTexture(resources, 128, 128, 128, 255, "graph.fallback.linearGrey");
    m_fallbacks.red =
        createSolidTexture(resources, 255, 0, 0, 255, "graph.fallback.red");
    m_fallbacks.whiteArray =
        createSolidTexture(resources, 255, 255, 255, 255, "graph.fallback.whiteArray", 2);
    m_fallbacks.white3D =
        createSolidTexture(resources, 255, 255, 255, 255, "graph.fallback.white3D", 1, false, true);
    m_fallbacks.whiteCube =
        createSolidTexture(resources, 255, 255, 255, 255, "graph.fallback.whiteCube", 1, true);
    if (!m_fallbacks.white.valid() || !m_fallbacks.black.valid() ||
        !m_fallbacks.textureBlack.valid() || !m_fallbacks.grey.valid() ||
        !m_fallbacks.normal.valid() || !m_fallbacks.linearGrey.valid() ||
        !m_fallbacks.red.valid() || !m_fallbacks.whiteArray.valid() ||
        !m_fallbacks.white3D.valid() || !m_fallbacks.whiteCube.valid()) {
        if (error != nullptr) {
            *error = "failed to create graph fallback textures";
        }
        shutdown(resources);
        return false;
    }
    m_sceneColorSampler = m_uniforms.uniform("s_sceneColor", gfx::UniformType::Sampler);
    m_sceneDepthSampler = m_uniforms.uniform("s_sceneDepth", gfx::UniformType::Sampler);
    m_modelInverseUniform = m_uniforms.uniform("u_sg_modelInverse", gfx::UniformType::Mat4);
    if (!m_sceneColorSampler.valid() || !m_sceneDepthSampler.valid() ||
        !m_modelInverseUniform.valid()) {
        if (error != nullptr) {
            *error = "failed to create graph binding uniforms";
        }
        shutdown(resources);
        return false;
    }

    m_materials.resize(scene.materials.size());
    m_enabledCount = 0;
    for (std::size_t materialIndex = 0; materialIndex < scene.materials.size(); ++materialIndex) {
        asset::Material& material = scene.materials[materialIndex];
        if (!material.hasGraphShader()) {
            continue;
        }
        Runtime& runtime = m_materials[materialIndex];
        runtime.path = resolvePath(material, scene);
        std::string loadError;
        runtime.asset = loadAsset(runtime.path, loadError);
        if (runtime.asset == nullptr) {
            HBG_LOGE("graph material '{}': {}", material.name, loadError);
            continue;
        }
        if (!graph::validateStageContract(*runtime.asset, &loadError)) {
            HBG_LOGE("graph material '{}': {}", material.name, loadError);
            runtime.asset.reset();
            continue;
        }
        if (!configureRuntime(runtime, material, scene, loadError)) {
            HBG_LOGE("graph material '{}': {}", material.name, loadError);
            destroyRuntime(runtime);
            continue;
        }
        material.type = runtime.unlit ? asset::MaterialType::Unlit : asset::MaterialType::Lit;
        ++m_enabledCount;
        HBG_LOGI("graph material '{}': target={} passes={} variant=0x{:x}", material.name,
                 runtime.asset->target(), runtime.asset->passes().size(), runtime.variantKey);
    }

    m_initialized = true;
    HBG_LOGI("graph material system: {} graph materials enabled (backend={})", m_enabledCount,
             m_programs.backend() == graph::Backend::Dxbc ? "dxbc" : "spirv");
    return true;
}

void GraphMaterialSystem::shutdown(gfx::ResourceManager& resources) {
    for (Runtime& runtime : m_materials) {
        destroyRuntime(runtime);
    }
    m_materials.clear();
    m_assets.clear();
    m_failedReloads.clear();
    m_programs.shutdown(resources);
    m_uniforms.shutdown(resources);
    m_sceneColorSampler = {};
    m_sceneDepthSampler = {};
    m_modelInverseUniform = {};
    for (gfx::TextureHandle* texture : {&m_fallbacks.white, &m_fallbacks.black,
                                        &m_fallbacks.textureBlack, &m_fallbacks.grey,
                                        &m_fallbacks.normal, &m_fallbacks.linearGrey,
                                        &m_fallbacks.red, &m_fallbacks.whiteArray,
                                        &m_fallbacks.white3D, &m_fallbacks.whiteCube}) {
        if (texture->valid()) {
            resources.destroyTexture(*texture);
        }
    }
    m_fallbacks = {};
    m_resources = nullptr;
    m_enabledCount = 0;
    m_initialized = false;
}

void GraphMaterialSystem::poll(gfx::ResourceManager& resources, RenderScene& scene) {
    if (!m_initialized || m_resources == nullptr) {
        return;
    }
    for (std::size_t materialIndex = 0; materialIndex < m_materials.size() && materialIndex < scene.materials.size();
         ++materialIndex) {
        Runtime& runtime = m_materials[materialIndex];
        if (runtime.asset == nullptr || runtime.path.empty()) {
            continue;
        }
        std::error_code timeError;
        const auto currentWrite = std::filesystem::last_write_time(runtime.path, timeError);
        if (timeError) {
            const std::string key = runtime.path.generic_string();
            if (m_failedReloads.emplace(key, true).second) {
                HBG_LOGW("graph material '{}': cannot stat '{}'", scene.materials[materialIndex].name,
                         runtime.path.string());
            }
            continue;
        }
        if (currentWrite <= runtime.asset->lastWriteTime()) {
            continue;
        }

        const std::string key = runtime.path.generic_string();
        std::shared_ptr<graph::Asset> replacement = std::make_shared<graph::Asset>();
        std::string error;
        if (!replacement->load(runtime.path, &error) ||
            !graph::validateStageContract(*replacement, &error)) {
            if (m_failedReloads.emplace(key, true).second) {
                HBG_LOGW("graph material '{}': hot reload failed, keeping previous asset: {}",
                         scene.materials[materialIndex].name, error);
            }
            continue;
        }

        Runtime updated;
        updated.path = runtime.path;
        updated.asset = replacement;
        if (!configureRuntime(updated, scene.materials[materialIndex], scene, error)) {
            if (m_failedReloads.emplace(key, true).second) {
                HBG_LOGW("graph material '{}': hot reload binding failed, keeping previous asset: {}",
                         scene.materials[materialIndex].name, error);
            }
            destroyRuntime(updated);
            continue;
        }

        const uint64_t previousHash = runtime.asset->contentHash();
        destroyRuntime(runtime);
        runtime = std::move(updated);
        scene.materials[materialIndex].type =
            runtime.unlit ? asset::MaterialType::Unlit : asset::MaterialType::Lit;
        m_assets[key] = replacement;
        m_failedReloads.erase(key);
        bool hashStillUsed = false;
        for (const Runtime& other : m_materials) {
            hashStillUsed = hashStillUsed || (other.asset != nullptr &&
                                               other.asset->contentHash() == previousHash);
        }
        if (!hashStillUsed) {
            m_programs.release(previousHash);
        }
        HBG_LOGI("graph material '{}': hot reloaded '{}'", scene.materials[materialIndex].name,
                 runtime.path.filename().string());
    }
    (void)resources;
}

bool GraphMaterialSystem::hasMaterial(uint32_t materialIndex) const {
    return materialIndex < m_materials.size() && m_materials[materialIndex].enabled;
}

bool GraphMaterialSystem::isUnlit(uint32_t materialIndex) const {
    return materialIndex < m_materials.size() && m_materials[materialIndex].unlit;
}

uint64_t GraphMaterialSystem::variantKey(uint32_t materialIndex) const {
    return materialIndex < m_materials.size() ? m_materials[materialIndex].variantKey : 0;
}

const graph::Asset* GraphMaterialSystem::asset(uint32_t materialIndex) const {
    if (materialIndex >= m_materials.size() || !m_materials[materialIndex].enabled) {
        return nullptr;
    }
    return m_materials[materialIndex].asset.get();
}

gfx::ProgramHandle GraphMaterialSystem::program(uint32_t materialIndex, graph::PassKind pass) {
    if (materialIndex >= m_materials.size() || !m_materials[materialIndex].enabled) {
        return {};
    }
    const Runtime& runtime = m_materials[materialIndex];
    return m_programs.program(*runtime.asset, pass, runtime.variantKey);
}

bool GraphMaterialSystem::bind(uint32_t materialIndex, gfx::Encoder& encoder, const Mat4& model,
                               gfx::TextureHandle sceneColor,
                               gfx::TextureHandle sceneDepth) const {
    if (materialIndex >= m_materials.size() || !m_materials[materialIndex].enabled ||
        m_materials[materialIndex].binding == nullptr ||
        m_materials[materialIndex].asset == nullptr) {
        return false;
    }
    const graph::Asset& asset = *m_materials[materialIndex].asset;
    if (m_modelInverseUniform.valid()) {
        const Mat4 modelInverse = inverse(model);
        encoder.setUniform(m_modelInverseUniform, modelInverse.data());
    }
    m_materials[materialIndex].binding->setGlobalMipBias(m_globalMipBias);
    m_materials[materialIndex].binding->bind(encoder);
    int nextStage = 0;
    for (const graph::PropertyDesc& property : asset.properties()) {
        if (property.samplerStage >= 0) {
            nextStage = std::max(nextStage, property.samplerStage + 1);
        }
    }
    const uint32_t features = asset.requiredFeatureMask();
    if ((features & graph::kGraphFeatureSceneColor) != 0 && nextStage <= 15) {
        const int stage = nextStage++;
        if (m_sceneColorSampler.valid()) {
            const gfx::TextureHandle texture =
                sceneColor.valid() ? sceneColor : m_fallbacks.black;
            if (texture.valid()) {
                encoder.setTexture(static_cast<uint8_t>(stage), m_sceneColorSampler, texture,
                                   BGFX_SAMPLER_UVW_CLAMP);
            }
        }
    }
    if ((features & graph::kGraphFeatureSceneDepth) != 0 && nextStage <= 15) {
        const int stage = nextStage++;
        if (m_sceneDepthSampler.valid()) {
            const gfx::TextureHandle texture =
                sceneDepth.valid() ? sceneDepth : m_fallbacks.white;
            if (texture.valid()) {
                encoder.setTexture(static_cast<uint8_t>(stage), m_sceneDepthSampler, texture,
                                   BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            }
        }
    }
    return true;
}

std::shared_ptr<graph::Asset> GraphMaterialSystem::loadAsset(const std::filesystem::path& path,
                                                             std::string& error) {
    const std::string key = path.generic_string();
    const auto found = m_assets.find(key);
    if (found != m_assets.end()) {
        return found->second;
    }
    auto asset = std::make_shared<graph::Asset>();
    if (!asset->load(path, &error)) {
        return nullptr;
    }
    m_assets.emplace(key, asset);
    return asset;
}

std::filesystem::path GraphMaterialSystem::resolvePath(const asset::Material& material,
                                                       const RenderScene& scene) const {
    if (material.graphShader.is_absolute()) {
        return material.graphShader.lexically_normal();
    }
    std::filesystem::path base = std::filesystem::path(scene.sourcePath).parent_path();
    if (base.empty()) {
        base = std::filesystem::current_path();
    }
    return (base / material.graphShader).lexically_normal();
}

bool GraphMaterialSystem::configureRuntime(Runtime& runtime, asset::Material& material, const RenderScene& scene,
                                           std::string& error) {
    if (runtime.asset == nullptr) {
        error = "graph asset is missing";
        return false;
    }
    runtime.unlit = runtime.asset->target() == "unlit";
    runtime.variantKey = 0;
    runtime.binding = std::make_unique<graph::GraphBinding>();
    if (!runtime.binding->init(*m_resources, m_uniforms, *runtime.asset, material, scene, m_fallbacks, &error)) {
        runtime.binding.reset();
        return false;
    }
    runtime.variantKey = runtime.binding->variantKey();
    runtime.enabled = true;
    return true;
}

void GraphMaterialSystem::destroyRuntime(Runtime& runtime) {
    if (runtime.binding != nullptr) {
        if (m_resources != nullptr) {
            runtime.binding->destroy(*m_resources);
        }
        runtime.binding.reset();
    }
    runtime.asset.reset();
    runtime.enabled = false;
    runtime.variantKey = 0;
}

} // namespace hbg::render
