#include "render/material/material_system.h"

#include "asset/texture_loader.h"
#include "core/log.h"
#include "shader/shader_key.h"

#include <array>
#include <cstdint>

namespace hbg::render {
namespace {

gfx::UniformHandle createSampler(gfx::ResourceManager& resources, const char* name) {
    return resources.createUniform({name, gfx::UniformType::Sampler, 1});
}

gfx::TextureHandle createColorTexture(gfx::ResourceManager& resources, uint8_t r, uint8_t g, uint8_t b,
                                      uint8_t a, bool srgb, const char* name) {
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

} // namespace

bool MaterialSamplers::init(gfx::ResourceManager& resources) {
    baseColorMap = createSampler(resources, "s_baseColorMap");
    metallicRoughnessMap = createSampler(resources, "s_metallicRoughnessMap");
    normalMap = createSampler(resources, "s_normalMap");
    occlusionMap = createSampler(resources, "s_occlusionMap");
    emissiveMap = createSampler(resources, "s_emissiveMap");
    return baseColorMap.valid() && metallicRoughnessMap.valid() && normalMap.valid() &&
           occlusionMap.valid() && emissiveMap.valid();
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
    baseColorMap = {};
    metallicRoughnessMap = {};
    normalMap = {};
    occlusionMap = {};
    emissiveMap = {};
}

bool MaterialSystem::init(gfx::ResourceManager& resources, const RenderScene& scene) {
    m_resources = &resources;
    if (!m_samplers.init(resources)) {
        HBG_LOGE("material system: failed to create material samplers");
        return false;
    }

    m_whiteSrgb = createColorTexture(resources, 255, 255, 255, 255, true, "material_white_srgb");
    m_whiteLinear = createColorTexture(resources, 255, 255, 255, 255, false, "material_white_linear");
    m_normalFlat = createColorTexture(resources, 128, 128, 255, 255, false, "material_normal_flat");
    if (!m_whiteSrgb.valid() || !m_whiteLinear.valid() || !m_normalFlat.valid()) {
        HBG_LOGE("material system: failed to create fallback textures");
        return false;
    }

    const gfx::VertexLayout layout = meshLayout();
    m_meshes.reserve(scene.meshes.size());
    for (const asset::Mesh& mesh : scene.meshes) {
        GpuMesh gpuMesh;
        if (!mesh.vertices.empty()) {
            gfx::BufferDesc vertexDesc;
            vertexDesc.type = gfx::BufferType::Vertex;
            vertexDesc.data = mesh.vertices.data();
            vertexDesc.size = static_cast<uint32_t>(mesh.vertices.size() * sizeof(asset::MeshVertex));
            vertexDesc.count = static_cast<uint32_t>(mesh.vertices.size());
            vertexDesc.layout = &layout;
            vertexDesc.name = mesh.name + ".vb";
            gpuMesh.vertexBuffer = resources.createBuffer(vertexDesc);
        }
        if (!mesh.indices.empty()) {
            gfx::BufferDesc indexDesc;
            indexDesc.type = gfx::BufferType::Index;
            indexDesc.data = mesh.indices.data();
            indexDesc.size = static_cast<uint32_t>(mesh.indices.size() * sizeof(uint32_t));
            indexDesc.count = static_cast<uint32_t>(mesh.indices.size());
            indexDesc.flags = BGFX_BUFFER_INDEX32;
            indexDesc.name = mesh.name + ".ib";
            gpuMesh.indexBuffer = resources.createBuffer(indexDesc);
        }
        if (!gpuMesh.vertexBuffer.valid() || !gpuMesh.indexBuffer.valid()) {
            HBG_LOGE("material system: failed to create GPU buffers for mesh '{}'", mesh.name);
            return false;
        }
        m_meshes.push_back(gpuMesh);
    }

    m_materials.reserve(scene.materials.size());
    for (const asset::Material& material : scene.materials) {
        GpuMaterial gpuMaterial;
        gpuMaterial.baseColorTexture = textureForImage(resources, scene, material.baseColorTexture, true, false);
        gpuMaterial.metallicRoughnessTexture =
            textureForImage(resources, scene, material.metallicRoughnessTexture, false, false);
        gpuMaterial.normalTexture = textureForImage(resources, scene, material.normalTexture, false, true);
        gpuMaterial.occlusionTexture = textureForImage(resources, scene, material.occlusionTexture, false, false);
        gpuMaterial.emissiveTexture = textureForImage(resources, scene, material.emissiveTexture, true, false);

        gpuMaterial.uniforms.baseColorFactor = material.baseColorFactor;
        gpuMaterial.uniforms.metallicRoughness =
            Vec4(material.metallic, material.roughness, material.normalScale, material.alphaCutoff);
        gpuMaterial.uniforms.emissiveFactor =
            Vec4(material.emissiveFactor.x * material.emissiveStrength,
                 material.emissiveFactor.y * material.emissiveStrength,
                 material.emissiveFactor.z * material.emissiveStrength, material.occlusionStrength);

        uint32_t variant = shader::MaterialVariantNone;
        if (material.normalTexture.valid()) {
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

        m_materials.push_back(gpuMaterial);
    }

    HBG_LOGI("material system: {} meshes, {} materials, {} textures", m_meshes.size(), m_materials.size(),
             m_textureCache.size());
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
    const gfx::TextureHandle texture =
        asset::TextureLoader::createTexture(resources, image, srgb, image.name);
    if (!texture.valid()) {
        HBG_LOGW("material system: failed to upload image '{}', using fallback", image.name);
        return normalMap ? m_normalFlat : (srgb ? m_whiteSrgb : m_whiteLinear);
    }
    m_textureCache.emplace(key, texture);
    return texture;
}

void MaterialSystem::shutdown(gfx::ResourceManager& resources) {
    for (const auto& [key, texture] : m_textureCache) {
        (void)key;
        if (texture.valid()) {
            resources.destroyTexture(texture);
        }
    }
    m_textureCache.clear();

    if (m_normalFlat.valid()) {
        resources.destroyTexture(m_normalFlat);
    }
    if (m_whiteLinear.valid()) {
        resources.destroyTexture(m_whiteLinear);
    }
    if (m_whiteSrgb.valid()) {
        resources.destroyTexture(m_whiteSrgb);
    }
    m_normalFlat = {};
    m_whiteLinear = {};
    m_whiteSrgb = {};

    for (GpuMesh& mesh : m_meshes) {
        if (mesh.wireframeIndexBuffer.valid()) {
            resources.destroyBuffer(mesh.wireframeIndexBuffer);
        }
        if (mesh.indexBuffer.valid()) {
            resources.destroyBuffer(mesh.indexBuffer);
        }
        if (mesh.vertexBuffer.valid()) {
            resources.destroyBuffer(mesh.vertexBuffer);
        }
    }
    m_meshes.clear();

    m_samplers.shutdown(resources);
    m_materials.clear();
    m_resources = nullptr;
}

} // namespace hbg::render
