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
    layout.add(bgfx::Attrib::TexCoord2, 3, bgfx::AttribType::Float);
    layout.end();
    return layout;
}

uint64_t textureKey(int imageIndex, bool srgb, bool normalMap, const asset::TextureSampler& sampler) {
    return (static_cast<uint64_t>(static_cast<uint32_t>(imageIndex)) << 32) |
           (static_cast<uint64_t>(asset::samplerStateKey(sampler)) << 2) |
           (normalMap ? 2u : 0u) | (srgb ? 1u : 0u);
}

uint64_t packedMaskKey(int metallicRoughnessImage, int occlusionImage, uint32_t samplerKey) {
    const uint64_t metallicRoughness =
        static_cast<uint64_t>(static_cast<uint32_t>(metallicRoughnessImage + 1)) & 0xffffffu;
    const uint64_t occlusion = static_cast<uint64_t>(static_cast<uint32_t>(occlusionImage + 1)) & 0xffffffu;
    return (metallicRoughness << 40) | (occlusion << 16) | (samplerKey & 0x7fu);
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
            gpuMesh.dynamicVertex = !mesh.morphTargets.empty() || !mesh.skinVertices.empty();
            std::vector<asset::MeshVertex> initialVertices = mesh.vertices;
            for (asset::MeshVertex& vertex : initialVertices) {
                vertex.previousPosition = vertex.position;
            }
            gfx::BufferDesc vertexDesc;
            vertexDesc.type = gpuMesh.dynamicVertex ? gfx::BufferType::DynamicVertex : gfx::BufferType::Vertex;
            vertexDesc.data = initialVertices.data();
            vertexDesc.size = static_cast<uint32_t>(initialVertices.size() * sizeof(asset::MeshVertex));
            vertexDesc.count = static_cast<uint32_t>(initialVertices.size());
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
        const bool hasMetallicRoughness = material.metallicRoughnessTexture.valid();
        const bool hasOcclusion = material.occlusionTexture.valid();
        const bool compatibleMaskSources =
            !hasMetallicRoughness || !hasOcclusion ||
            (material.metallicRoughnessTexture.uvSet == material.occlusionTexture.uvSet &&
             material.metallicRoughnessTexture.sampler == material.occlusionTexture.sampler);
        gfx::TextureHandle packedMask;
        if (material.maskMapPacked && (hasMetallicRoughness || hasOcclusion) && compatibleMaskSources) {
            packedMask = packedMaskTexture(resources, scene, material);
        }
        if (packedMask.valid()) {
            gpuMaterial.metallicRoughnessTexture = packedMask;
            gpuMaterial.occlusionTexture = packedMask;
        } else {
            gpuMaterial.metallicRoughnessTexture =
                textureForImage(resources, scene, material.metallicRoughnessTexture, false, false);
            gpuMaterial.occlusionTexture = textureForImage(resources, scene, material.occlusionTexture, false, false);
        }
        gpuMaterial.normalTexture = textureForImage(resources, scene, material.normalTexture, false, true);
        gpuMaterial.emissiveTexture = textureForImage(resources, scene, material.emissiveTexture, true, false);

        gpuMaterial.uniforms.baseColorFactor = material.baseColorFactor;
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
        gpuMaterial.uniforms.advanced2 =
            Vec4(material.iridescenceThickness, material.subsurfaceMask, static_cast<float>(material.features),
                 material.clearCoatNormalScale);

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
        gpuMaterial.features = material.features;

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

    HBG_LOGI("material system: {} meshes, {} materials, {} decals, {} textures, {} packed masks", m_meshes.size(),
             m_materials.size(), m_decals.size(), m_textureCache.size(), m_packedMaskCache.size());
    return true;
}

bool MaterialSystem::updateDeformedMeshes(RenderScene& scene) {
    if (m_resources == nullptr || m_meshes.size() != scene.meshes.size()) {
        return false;
    }

    const std::vector<Mat4> nodeWorlds =
        scene.skins.empty() ? std::vector<Mat4>{} : asset::evaluateNodeWorlds(scene);
    std::vector<uint8_t> updated(m_meshes.size(), 0);
    std::vector<asset::MeshVertex> deformed;
    std::vector<Mat4> jointMatrices;
    for (render::RenderObject& object : scene.objects) {
        if (object.meshIndex >= scene.meshes.size()) {
            continue;
        }
        const asset::Mesh& mesh = scene.meshes[object.meshIndex];
        const bool hasMorph = !mesh.morphTargets.empty();
        const bool hasSkin =
            object.skinIndex >= 0 && static_cast<std::size_t>(object.skinIndex) < scene.skins.size() &&
            !mesh.skinVertices.empty();
        if (!hasMorph && !hasSkin) {
            continue;
        }
        GpuMesh& gpuMesh = m_meshes[object.meshIndex];
        if (!gpuMesh.dynamicVertex || !gpuMesh.vertexBuffer.valid()) {
            continue;
        }
        if (updated[object.meshIndex] != 0) {
            HBG_LOGW("material system: deformed mesh '{}' is shared by multiple render objects", mesh.name);
            continue;
        }

        if (hasMorph) {
            mesh.applyMorphWeights(object.morphWeights, deformed);
        } else {
            deformed = mesh.vertices;
        }

        if (hasSkin) {
            const asset::Skin& skin = scene.skins[static_cast<std::size_t>(object.skinIndex)];
            jointMatrices.assign(skin.joints.size(), Mat4::identity());
            const Mat4 meshWorld = object.nodeIndex >= 0 &&
                                           static_cast<std::size_t>(object.nodeIndex) < nodeWorlds.size()
                                       ? nodeWorlds[static_cast<std::size_t>(object.nodeIndex)]
                                       : object.world;
            const Mat4 inverseMeshWorld = hbg::inverse(meshWorld);
            for (std::size_t jointIndex = 0; jointIndex < skin.joints.size(); ++jointIndex) {
                const uint32_t jointNode = skin.joints[jointIndex];
                if (jointNode >= nodeWorlds.size() || jointIndex >= skin.inverseBindMatrices.size()) {
                    continue;
                }
                jointMatrices[jointIndex] =
                    inverseMeshWorld * nodeWorlds[jointNode] * skin.inverseBindMatrices[jointIndex];
            }
            mesh.applySkinning(jointMatrices, deformed);
        }

        if (deformed.size() != mesh.vertices.size()) {
            continue;
        }
        const Bounds localBounds = asset::Mesh::computeBounds(deformed);
        if (gpuMesh.previousPositions.size() != deformed.size()) {
            gpuMesh.previousPositions.resize(deformed.size());
            for (std::size_t vertex = 0; vertex < deformed.size(); ++vertex) {
                gpuMesh.previousPositions[vertex] = deformed[vertex].position;
            }
        }
        for (std::size_t vertex = 0; vertex < deformed.size(); ++vertex) {
            deformed[vertex].previousPosition = gpuMesh.previousPositions[vertex];
        }
        if (!m_resources->updateBuffer(gpuMesh.vertexBuffer, deformed.data(),
                                       static_cast<uint32_t>(deformed.size() * sizeof(asset::MeshVertex)))) {
            HBG_LOGE("material system: failed to update deformed mesh '{}'", mesh.name);
            return false;
        }
        if (localBounds.valid()) {
            object.worldBounds = transformBounds(object.world, localBounds);
        }
        for (std::size_t vertex = 0; vertex < deformed.size(); ++vertex) {
            gpuMesh.previousPositions[vertex] = deformed[vertex].position;
        }
        updated[object.meshIndex] = 1;
    }
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

gfx::TextureHandle MaterialSystem::packedMaskTexture(gfx::ResourceManager& resources, const RenderScene& scene,
                                                     const asset::Material& material) {
    const auto imageFor = [&scene](const asset::TextureRef& ref) -> const asset::ImageAsset* {
        if (!ref.valid() || static_cast<std::size_t>(ref.imageIndex) >= scene.images.size()) {
            return nullptr;
        }
        return &scene.images[static_cast<std::size_t>(ref.imageIndex)];
    };
    const asset::ImageAsset* metallicRoughness = imageFor(material.metallicRoughnessTexture);
    const asset::ImageAsset* occlusion = imageFor(material.occlusionTexture);
    if (metallicRoughness == nullptr && occlusion == nullptr) {
        return {};
    }

    const asset::TextureSampler& sampler = material.metallicRoughnessTexture.valid()
                                               ? material.metallicRoughnessTexture.sampler
                                               : material.occlusionTexture.sampler;
    const uint64_t key = packedMaskKey(material.metallicRoughnessTexture.imageIndex,
                                       material.occlusionTexture.imageIndex,
                                       asset::samplerStateKey(sampler));
    const auto cached = m_packedMaskCache.find(key);
    if (cached != m_packedMaskCache.end()) {
        return cached->second;
    }

    asset::ImageAsset packed;
    std::string error;
    if (!asset::TextureLoader::packOrmMask(occlusion, metallicRoughness, packed, &error)) {
        HBG_LOGD("material system: packing mask for '{}' skipped: {}", material.name, error);
        return {};
    }
    packed.name = material.name.empty() ? "OrmMask" : material.name + ".OrmMask";
    const gfx::TextureHandle texture =
        asset::TextureLoader::createTexture(resources, packed, false, sampler, packed.name);
    if (!texture.valid()) {
        HBG_LOGW("material system: failed to upload packed mask '{}'", packed.name);
        return {};
    }
    m_packedMaskCache.emplace(key, texture);
    return texture;
}

gfx::TextureHandle MaterialSystem::textureForImage(gfx::ResourceManager& resources, const RenderScene& scene,
                                                   const asset::TextureRef& ref, bool srgb, bool normalMap) {
    if (!ref.valid() || static_cast<std::size_t>(ref.imageIndex) >= scene.images.size()) {
        return normalMap ? m_normalFlat : (srgb ? m_whiteSrgb : m_whiteLinear);
    }

    const uint64_t key = textureKey(ref.imageIndex, srgb, normalMap, ref.sampler);
    const auto cached = m_textureCache.find(key);
    if (cached != m_textureCache.end()) {
        return cached->second;
    }

    const asset::ImageAsset& image = scene.images[static_cast<std::size_t>(ref.imageIndex)];
    const gfx::TextureHandle texture =
        asset::TextureLoader::createTexture(resources, image, srgb, ref.sampler, image.name);
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

    for (const auto& [key, texture] : m_packedMaskCache) {
        (void)key;
        if (texture.valid()) {
            resources.destroyTexture(texture);
        }
    }
    m_packedMaskCache.clear();

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
    m_decals.clear();
    m_resources = nullptr;
}

} // namespace hbg::render
