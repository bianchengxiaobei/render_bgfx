#include "render/material/material_system.h"

#include "asset/texture_loader.h"
#include "core/log.h"
#include "render/material/material_texture_array.h"
#include "shader/shader_key.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>

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

uint32_t bindlessMaterialPixelBytes(gfx::TextureFormat format) {
    if (format == gfx::TextureFormat::RGBA8) {
        return 4;
    }
    if (format == gfx::TextureFormat::RGBA32F) {
        return 16;
    }
    return 0;
}

uint32_t bindlessMipDimension(uint32_t base, uint32_t mip) {
    uint32_t dimension = base;
    for (uint32_t level = 0; level < mip; ++level) {
        dimension = std::max(1u, dimension >> 1u);
    }
    return dimension;
}

uint32_t bindlessWrapIndex(int index, uint32_t size, asset::TextureWrap wrap) {
    if (size == 0) {
        return 0;
    }
    const int sizeValue = static_cast<int>(size);
    switch (wrap) {
    case asset::TextureWrap::Clamp:
        return static_cast<uint32_t>(std::clamp(index, 0, sizeValue - 1));
    case asset::TextureWrap::Mirror: {
        const int period = sizeValue * 2;
        int value = index % period;
        if (value < 0) {
            value += period;
        }
        return static_cast<uint32_t>(value < sizeValue ? value : period - value - 1);
    }
    case asset::TextureWrap::Repeat:
    default: {
        int value = index % sizeValue;
        if (value < 0) {
            value += sizeValue;
        }
        return static_cast<uint32_t>(value);
    }
    }
}

uint32_t bindlessWrapCode(asset::TextureWrap wrap) {
    return static_cast<uint32_t>(wrap) & 3u;
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
            gpuMesh.cpuVertexBuffer = resources.createBuffer(vertexDesc);
            gpuMesh.vertexBuffer = gpuMesh.cpuVertexBuffer;

            if (mesh.skinVertices.size() == mesh.vertices.size() && mesh.morphTargets.empty()) {
                std::vector<uint32_t> skinInfo;
                skinInfo.reserve(mesh.vertices.size() * 2u);
                for (const asset::SkinVertex& skin : mesh.skinVertices) {
                    skinInfo.push_back(skin.influenceOffset);
                    skinInfo.push_back(skin.influenceCount);
                }

                std::vector<uint32_t> skinInfluences;
                skinInfluences.reserve(mesh.skinInfluences.size() * 2u);
                for (const asset::SkinInfluence& influence : mesh.skinInfluences) {
                    uint32_t weightBits = 0;
                    std::memcpy(&weightBits, &influence.weight, sizeof(weightBits));
                    skinInfluences.push_back(static_cast<uint32_t>(influence.joint));
                    skinInfluences.push_back(weightBits);
                }

                gfx::BufferDesc baseDesc;
                baseDesc.type = gfx::BufferType::StorageVertex;
                baseDesc.data = initialVertices.data();
                baseDesc.size = static_cast<uint32_t>(initialVertices.size() * sizeof(asset::MeshVertex));
                baseDesc.count = static_cast<uint32_t>(initialVertices.size());
                baseDesc.layout = &layout;
                baseDesc.flags = BGFX_BUFFER_COMPUTE_READ;
                baseDesc.name = mesh.name + ".skinBase.vb";
                gpuMesh.skinBaseBuffer = resources.createBuffer(baseDesc);

                gfx::BufferDesc infoDesc;
                infoDesc.type = gfx::BufferType::Storage;
                infoDesc.data = skinInfo.data();
                infoDesc.size = static_cast<uint32_t>(skinInfo.size() * sizeof(uint32_t));
                infoDesc.count = static_cast<uint32_t>(skinInfo.size());
                infoDesc.flags = BGFX_BUFFER_COMPUTE_READ;
                infoDesc.name = mesh.name + ".skinInfo";
                gpuMesh.skinInfoBuffer = resources.createBuffer(infoDesc);

                gfx::BufferDesc influenceDesc;
                influenceDesc.type = gfx::BufferType::Storage;
                influenceDesc.data = skinInfluences.data();
                influenceDesc.size = static_cast<uint32_t>(skinInfluences.size() * sizeof(uint32_t));
                influenceDesc.count = static_cast<uint32_t>(skinInfluences.size());
                influenceDesc.flags = BGFX_BUFFER_COMPUTE_READ;
                influenceDesc.name = mesh.name + ".skinInfluences";
                gpuMesh.skinInfluenceBuffer = resources.createBuffer(influenceDesc);

                gfx::BufferDesc outputDesc;
                outputDesc.type = gfx::BufferType::StorageVertex;
                outputDesc.count = static_cast<uint32_t>(mesh.vertices.size());
                outputDesc.layout = &layout;
                outputDesc.flags = BGFX_BUFFER_COMPUTE_WRITE;
                outputDesc.name = mesh.name + ".skinOutput.vb";
                gpuMesh.skinOutputBuffer = resources.createBuffer(outputDesc);

                gpuMesh.gpuSkinCapable = gpuMesh.skinBaseBuffer.valid() && gpuMesh.skinInfoBuffer.valid() &&
                                          gpuMesh.skinInfluenceBuffer.valid() && gpuMesh.skinOutputBuffer.valid();
                if (!gpuMesh.gpuSkinCapable) {
                    HBG_LOGW("material system: GPU skin buffers unavailable for mesh '{}'", mesh.name);
                }
            }
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

bool MaterialSystem::bindlessReady() const {
    return m_bindlessTextures.valid() && m_bindlessVertexBuffer.valid() && m_bindlessIndexBuffer.valid();
}

bool MaterialSystem::ensureBindlessResources(const RenderScene& scene) {
    if (m_bindlessAttempted) {
        return bindlessReady();
    }
    m_bindlessAttempted = true;
    return buildBindlessResources(scene);
}

bool MaterialSystem::buildBindlessResources(const RenderScene& scene) {
    if (m_resources == nullptr || m_materials.empty() || scene.meshes.empty()) {
        return false;
    }
    if (m_materials.size() > 32u) {
        return false;
    }
    for (const asset::Material& material : scene.materials) {
        if (material.type != asset::MaterialType::Lit ||
            (material.alphaMode != asset::AlphaMode::Opaque &&
             material.alphaMode != asset::AlphaMode::Blend) ||
            material.doubleSided || material.features != 0 || material.hasGraphShader()) {
            return false;
        }
    }
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr || m_materials.size() > caps->limits.maxTextureLayers || m_materials.size() > UINT16_MAX) {
        return false;
    }

    const uint32_t layerCount = static_cast<uint32_t>(m_materials.size());
    const auto createMaterialArray = [this, &scene, layerCount, caps](
                                         const char* name, uint32_t channel, bool srgb, auto refFor,
                                         uint8_t fallbackR, uint8_t fallbackG, uint8_t fallbackB,
                                         uint8_t fallbackA) -> gfx::TextureHandle {
        const asset::TextureSampler* referenceSampler = nullptr;
        uint32_t width = 1;
        uint32_t height = 1;
        uint32_t layerBytes = 0;
        uint8_t mipCount = 1;
        gfx::TextureFormat format = gfx::TextureFormat::RGBA8;
        bool hasSource = false;
        bool compressed = false;
        bool heterogeneous = false;
        for (const asset::Material& material : scene.materials) {
            const asset::TextureRef& ref = refFor(material);
            if (!ref.valid()) {
                continue;
            }
            if (ref.uvSet != 0 || static_cast<std::size_t>(ref.imageIndex) >= scene.images.size()) {
                HBG_LOGW("bindless texture array '{}': invalid texture ref uvSet={} image={}", name, ref.uvSet,
                         ref.imageIndex);
                return {};
            }
            const asset::ImageAsset& image = scene.images[static_cast<std::size_t>(ref.imageIndex)];
            uint32_t imageLayerBytes = 0;
            if (!image.valid() || !isBindlessMaterialTextureFormat(image.format) ||
                image.width > caps->limits.maxTextureSize || image.height > caps->limits.maxTextureSize ||
                !bindlessMaterialTextureLayerBytes(image.width, image.height, image.mipCount, image.format,
                                                    imageLayerBytes) ||
                image.pixels.size() < imageLayerBytes) {
                HBG_LOGW("bindless texture array '{}': invalid image '{}' fmt={} {}x{} mips={} bytes={}/{}", name,
                         image.name, gfx::textureFormatName(image.format), image.width, image.height,
                         image.mipCount, image.pixels.size(), imageLayerBytes);
                return {};
            }
            if (isBindlessMaterialTextureCompressed(image.format) &&
                ((image.width % 4u) != 0 || (image.height % 4u) != 0)) {
                HBG_LOGW("bindless texture array '{}': unaligned compressed image '{}' {}x{}", name, image.name,
                         image.width, image.height);
                return {};
            }
            if (!hasSource) {
                referenceSampler = &ref.sampler;
                width = image.width;
                height = image.height;
                mipCount = image.mipCount;
                format = image.format;
                compressed = isBindlessMaterialTextureCompressed(format);
                hasSource = true;
            } else if (format != image.format || !(ref.sampler == *referenceSampler)) {
                HBG_LOGW("bindless texture array '{}': layout mismatch image='{}' mips={}/{} fmt={}/{} bytes={}",
                         name, image.name, image.mipCount, mipCount, gfx::textureFormatName(image.format),
                         gfx::textureFormatName(format), image.pixels.size());
                return {};
            }
            if (hasSource && (width != image.width || height != image.height)) {
                heterogeneous = true;
            }
            if (!heterogeneous && mipCount != image.mipCount) {
                HBG_LOGW("bindless texture array '{}': mip mismatch image='{}' mips={}/{}", name, image.name,
                         image.mipCount, mipCount);
                return {};
            }
            width = std::max(width, image.width);
            height = std::max(height, image.height);
            mipCount = std::max(mipCount, image.mipCount);
        }

        uint32_t arrayWidth = width;
        uint32_t arrayHeight = height;
        uint32_t arrayOffsetX = 0;
        uint32_t arrayOffsetY = 0;
        if (heterogeneous &&
            !bindlessMaterialTextureLayeredLayout(width, height, mipCount, format, arrayWidth, arrayHeight,
                                                  arrayOffsetX, arrayOffsetY)) {
            return {};
        }
        const uint8_t arrayMipCount = mipCount;
        if (arrayWidth > caps->limits.maxTextureSize || arrayHeight > caps->limits.maxTextureSize ||
            !bindlessMaterialTextureLayerBytes(arrayWidth, arrayHeight, arrayMipCount, format, layerBytes)) {
            HBG_LOGW("bindless texture array '{}': invalid array {}x{} mips={} fmt={}", name, arrayWidth,
                     arrayHeight, arrayMipCount, gfx::textureFormatName(format));
            return {};
        }

        const auto fillFallbackLayer = [&](uint8_t* destination, uint32_t targetWidth,
                                           uint32_t targetHeight) {
            if (compressed) {
                return false;
            }
            uint32_t mipWidth = targetWidth;
            uint32_t mipHeight = targetHeight;
            std::size_t offset = 0;
            const Vec4 fallback(static_cast<float>(fallbackR) / 255.0f,
                                static_cast<float>(fallbackG) / 255.0f,
                                static_cast<float>(fallbackB) / 255.0f,
                                static_cast<float>(fallbackA) / 255.0f);
            for (uint32_t mip = 0; mip < arrayMipCount; ++mip) {
                const std::size_t texels = static_cast<std::size_t>(mipWidth) * mipHeight;
                if (format == gfx::TextureFormat::RGBA8) {
                    for (std::size_t texel = 0; texel < texels; ++texel) {
                        uint8_t* pixel = destination + offset + texel * 4u;
                        pixel[0] = fallbackR;
                        pixel[1] = fallbackG;
                        pixel[2] = fallbackB;
                        pixel[3] = fallbackA;
                    }
                    offset += texels * 4u;
                } else if (format == gfx::TextureFormat::RGBA32F) {
                    float* floats = reinterpret_cast<float*>(destination + offset);
                    for (std::size_t texel = 0; texel < texels; ++texel) {
                        floats[texel * 4u + 0u] = fallback.x;
                        floats[texel * 4u + 1u] = fallback.y;
                        floats[texel * 4u + 2u] = fallback.z;
                        floats[texel * 4u + 3u] = fallback.w;
                    }
                    offset += texels * 16u;
                } else {
                    return false;
                }
                mipWidth = std::max(1u, mipWidth >> 1u);
                mipHeight = std::max(1u, mipHeight >> 1u);
            }
            return true;
        };

        const auto copyHeterogeneousLayer = [&](const asset::ImageAsset& image, uint8_t* destination,
                                                const asset::TextureSampler& sampler) {
            const uint32_t blockExtent = bindlessMaterialTextureBlockExtent(format);
            const uint32_t blockBytes = compressed ? bindlessMaterialTextureBlockBytes(format)
                                                   : bindlessMaterialPixelBytes(format);
            uint32_t sourceLayerBytes = 0;
            if (blockBytes == 0 ||
                !bindlessMaterialTextureLayerBytes(image.width, image.height, image.mipCount, format,
                                                   sourceLayerBytes) ||
                image.pixels.size() < sourceLayerBytes) {
                return false;
            }
            for (uint32_t mip = 0; mip < arrayMipCount; ++mip) {
                const uint32_t sourceMip =
                    std::min(mip, static_cast<uint32_t>(image.mipCount) - 1u);
                uint32_t sourceOffset = 0;
                uint32_t destinationOffset = 0;
                if (!bindlessMaterialTextureMipOffset(image.width, image.height, image.mipCount, sourceMip,
                                                       format, sourceOffset) ||
                    !bindlessMaterialTextureMipOffset(arrayWidth, arrayHeight, arrayMipCount, mip,
                                                       format, destinationOffset)) {
                    return false;
                }
                const uint32_t sourceWidth = bindlessMipDimension(image.width, sourceMip);
                const uint32_t sourceHeight = bindlessMipDimension(image.height, sourceMip);
                const uint32_t destinationWidth = bindlessMipDimension(arrayWidth, mip);
                const uint32_t destinationHeight = bindlessMipDimension(arrayHeight, mip);
                const uint32_t sourceBlocksW = (sourceWidth + blockExtent - 1u) / blockExtent;
                const uint32_t sourceBlocksH = (sourceHeight + blockExtent - 1u) / blockExtent;
                const uint32_t destinationBlocksW = (destinationWidth + blockExtent - 1u) / blockExtent;
                const uint32_t destinationBlocksH =
                    (destinationHeight + blockExtent - 1u) / blockExtent;
                const uint32_t destinationBlockOffsetX =
                    arrayOffsetX == 0 ? 0 : (arrayOffsetX >> mip) / blockExtent;
                const uint32_t destinationBlockOffsetY =
                    arrayOffsetY == 0 ? 0 : (arrayOffsetY >> mip) / blockExtent;
                const std::size_t sourceMipBytes = static_cast<std::size_t>(sourceBlocksW) *
                                                   sourceBlocksH * blockBytes;
                const std::size_t destinationMipBytes = static_cast<std::size_t>(destinationBlocksW) *
                                                        destinationBlocksH * blockBytes;
                if (sourceBlocksW == 0 || sourceBlocksH == 0 || destinationBlocksW == 0 ||
                    destinationBlocksH == 0 ||
                    static_cast<std::size_t>(sourceOffset) + sourceMipBytes > image.pixels.size() ||
                    static_cast<std::size_t>(destinationOffset) + destinationMipBytes > layerBytes) {
                    return false;
                }
                const uint8_t* source = image.pixels.data() + sourceOffset;
                uint8_t* destinationMip = destination + destinationOffset;
                for (uint32_t y = 0; y < destinationBlocksH; ++y) {
                    const uint32_t sourceY =
                        bindlessWrapIndex(static_cast<int>(y) - static_cast<int>(destinationBlockOffsetY),
                                          sourceBlocksH, sampler.wrapT);
                    const uint8_t* sourceRow =
                        source + static_cast<std::size_t>(sourceY) * sourceBlocksW * blockBytes;
                    uint8_t* destinationRow =
                        destinationMip + static_cast<std::size_t>(y) * destinationBlocksW * blockBytes;
                    for (uint32_t x = 0; x < destinationBlocksW; ++x) {
                        const uint32_t sourceX = bindlessWrapIndex(
                            static_cast<int>(x) - static_cast<int>(destinationBlockOffsetX), sourceBlocksW,
                            sampler.wrapS);
                        std::memcpy(destinationRow + static_cast<std::size_t>(x) * blockBytes,
                                    sourceRow + static_cast<std::size_t>(sourceX) * blockBytes, blockBytes);
                    }
                }
            }
            return true;
        };
        const auto setTextureMeta = [this, channel, arrayWidth, arrayHeight, arrayOffsetX, arrayOffsetY,
                                     heterogeneous](uint32_t layer, const asset::TextureRef* ref,
                                                    const asset::ImageAsset* image) {
            Vec2 scale(1.0f, 1.0f);
            Vec2 offset(0.0f, 0.0f);
            uint32_t wrapBits =
                (bindlessWrapCode(asset::TextureWrap::Clamp) |
                 (bindlessWrapCode(asset::TextureWrap::Clamp) << 2));
            if (ref != nullptr && image != nullptr) {
                if (heterogeneous) {
                    const float widthValue = static_cast<float>(arrayWidth);
                    const float heightValue = static_cast<float>(arrayHeight);
                    scale = Vec2(static_cast<float>(image->width) / widthValue,
                                 static_cast<float>(image->height) / heightValue);
                    offset = Vec2(static_cast<float>(arrayOffsetX) / widthValue,
                                  static_cast<float>(arrayOffsetY) / heightValue);
                }
                wrapBits = (bindlessWrapCode(ref->sampler.wrapS) |
                            (bindlessWrapCode(ref->sampler.wrapT) << 2));
            } else if (heterogeneous) {
                scale = Vec2(0.0f, 0.0f);
            }
            m_materials[layer].bindlessUvScale[channel] = scale;
            m_materials[layer].bindlessUvOffset[channel] = offset;
            m_materials[layer].bindlessWrapBits |= wrapBits << (channel * 4u);
            if (heterogeneous) {
                m_materials[layer].bindlessHeterogeneous = true;
            }
        };

        if (!hasSource) {
            const std::vector<uint8_t> pixel = {fallbackR, fallbackG, fallbackB, fallbackA};
            std::vector<uint8_t> pixels(static_cast<std::size_t>(layerCount) * pixel.size());
            for (uint32_t layer = 0; layer < layerCount; ++layer) {
                std::memcpy(pixels.data() + static_cast<std::size_t>(layer) * pixel.size(), pixel.data(),
                            pixel.size());
                setTextureMeta(layer, nullptr, nullptr);
            }
            gfx::TextureDesc desc;
            desc.width = 1;
            desc.height = 1;
            desc.layers = static_cast<uint16_t>(layerCount);
            desc.format = gfx::TextureFormat::RGBA8;
            desc.data = pixels.data();
            desc.dataSize = static_cast<uint32_t>(pixels.size());
            desc.flags = BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP | (srgb ? BGFX_TEXTURE_SRGB : 0);
            desc.name = name;
            return m_resources->createTexture(desc);
        }

        const uint64_t totalBytes = static_cast<uint64_t>(layerCount) * layerBytes;
        if (totalBytes == 0 || totalBytes > UINT32_MAX) {
            return {};
        }
        std::vector<uint8_t> pixels(static_cast<std::size_t>(totalBytes));
        for (uint32_t layer = 0; layer < layerCount; ++layer) {
            const asset::Material& material = scene.materials[static_cast<std::size_t>(layer)];
            const asset::TextureRef& ref = refFor(material);
            uint8_t* destination = pixels.data() + static_cast<std::size_t>(layer) * layerBytes;
            if (!ref.valid()) {
                if (!fillFallbackLayer(destination, arrayWidth, arrayHeight)) {
                    return {};
                }
                setTextureMeta(layer, nullptr, nullptr);
                continue;
            }
            const asset::ImageAsset& image = scene.images[static_cast<std::size_t>(ref.imageIndex)];
            if (heterogeneous) {
                if (!copyHeterogeneousLayer(image, destination, ref.sampler)) {
                    HBG_LOGW("bindless texture array '{}': failed heterogeneous copy '{}' {}x{} mips={}", name,
                             image.name, image.width, image.height, image.mipCount);
                    return {};
                }
            } else {
                if (image.pixels.size() < layerBytes) {
                    return {};
                }
                std::memcpy(destination, image.pixels.data(), layerBytes);
            }
            setTextureMeta(layer, &ref, &image);
        }

        gfx::TextureDesc desc;
        desc.width = static_cast<uint16_t>(arrayWidth);
        desc.height = static_cast<uint16_t>(arrayHeight);
        desc.layers = static_cast<uint16_t>(layerCount);
        desc.mipCount = arrayMipCount;
        desc.format = format;
        desc.data = pixels.data();
        desc.dataSize = static_cast<uint32_t>(pixels.size());
        const bool srgbFormat = format == gfx::TextureFormat::RGBA8 || format == gfx::TextureFormat::BC1 ||
                                format == gfx::TextureFormat::BC3 || format == gfx::TextureFormat::BC7;
        uint64_t samplerFlags = referenceSampler != nullptr
                                    ? asset::TextureLoader::samplerFlags(*referenceSampler)
                                    : (BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
        if (heterogeneous) {
            samplerFlags &= ~(static_cast<uint64_t>(BGFX_SAMPLER_U_MASK) |
                              static_cast<uint64_t>(BGFX_SAMPLER_V_MASK));
            samplerFlags |= BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
        }
        desc.flags = samplerFlags | (srgb && srgbFormat ? BGFX_TEXTURE_SRGB : 0);
        desc.name = name;
        return m_resources->createTexture(desc);
    };
    m_bindlessTextures.baseColor =
        createMaterialArray("materialBindless.baseColor", 0, true,
                            [](const asset::Material& material) -> const asset::TextureRef& {
                                return material.baseColorTexture;
                            },
                            255, 255, 255, 255);
    m_bindlessTextures.metallicRoughness =
        createMaterialArray("materialBindless.metallicRoughness", 1, false,
                            [](const asset::Material& material) -> const asset::TextureRef& {
                                return material.metallicRoughnessTexture;
                            },
                            255, 255, 255, 255);
    m_bindlessTextures.normal =
        createMaterialArray("materialBindless.normal", 2, false,
                            [](const asset::Material& material) -> const asset::TextureRef& {
                                return material.normalTexture;
                            },
                            128, 128, 255, 255);
    m_bindlessTextures.occlusion =
        createMaterialArray("materialBindless.occlusion", 3, false,
                            [](const asset::Material& material) -> const asset::TextureRef& {
                                return material.occlusionTexture;
                            },
                            255, 255, 255, 255);
    m_bindlessTextures.emissive =
        createMaterialArray("materialBindless.emissive", 4, true,
                            [](const asset::Material& material) -> const asset::TextureRef& {
                                return material.emissiveTexture;
                            },
                            255, 255, 255, 255);
    if (!m_bindlessTextures.valid()) {
        const std::array<gfx::TextureHandle, 5> textures = {
            m_bindlessTextures.baseColor, m_bindlessTextures.metallicRoughness, m_bindlessTextures.normal,
            m_bindlessTextures.occlusion, m_bindlessTextures.emissive,
        };
        for (gfx::TextureHandle texture : textures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
        }
        m_bindlessTextures = {};
        return false;
    }

    std::vector<asset::MeshVertex> vertices;
    std::vector<uint32_t> indices;
    for (std::size_t meshIndex = 0; meshIndex < m_meshes.size() && meshIndex < scene.meshes.size(); ++meshIndex) {
        GpuMesh& gpuMesh = m_meshes[meshIndex];
        const asset::Mesh& mesh = scene.meshes[meshIndex];
        gpuMesh.bindlessEligible = false;
        if (gpuMesh.dynamicVertex || mesh.vertices.empty() || mesh.indices.empty()) {
            continue;
        }
        gpuMesh.bindlessVertexBase = static_cast<uint32_t>(vertices.size());
        gpuMesh.bindlessIndexBase = static_cast<uint32_t>(indices.size());
        const std::size_t vertexOffset = vertices.size();
        vertices.resize(vertexOffset + mesh.vertices.size());
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
            asset::MeshVertex vertex = mesh.vertices[index];
            vertex.previousPosition = vertex.position;
            vertices[vertexOffset + index] = vertex;
        }
        indices.insert(indices.end(), mesh.indices.begin(), mesh.indices.end());
        gpuMesh.bindlessEligible = true;
    }

    if (vertices.empty() || indices.empty()) {
        return false;
    }

    const gfx::VertexLayout layout = meshLayout();
    gfx::BufferDesc vertexDesc;
    vertexDesc.type = gfx::BufferType::Vertex;
    vertexDesc.data = vertices.data();
    vertexDesc.size = static_cast<uint32_t>(vertices.size() * sizeof(asset::MeshVertex));
    vertexDesc.count = static_cast<uint32_t>(vertices.size());
    vertexDesc.layout = &layout;
    vertexDesc.name = "materialBindless.vertices";
    m_bindlessVertexBuffer = m_resources->createBuffer(vertexDesc);

    gfx::BufferDesc indexDesc;
    indexDesc.type = gfx::BufferType::Index;
    indexDesc.data = indices.data();
    indexDesc.size = static_cast<uint32_t>(indices.size() * sizeof(uint32_t));
    indexDesc.count = static_cast<uint32_t>(indices.size());
    indexDesc.flags = BGFX_BUFFER_INDEX32;
    indexDesc.name = "materialBindless.indices";
    m_bindlessIndexBuffer = m_resources->createBuffer(indexDesc);

    if (!m_bindlessVertexBuffer.valid() || !m_bindlessIndexBuffer.valid()) {
        if (m_bindlessVertexBuffer.valid()) {
            m_resources->destroyBuffer(m_bindlessVertexBuffer);
        }
        if (m_bindlessIndexBuffer.valid()) {
            m_resources->destroyBuffer(m_bindlessIndexBuffer);
        }
        m_bindlessVertexBuffer = {};
        m_bindlessIndexBuffer = {};
        return false;
    }
    return true;
}
bool MaterialSystem::updateGpuSkinMesh(uint32_t meshIndex, const asset::Mesh& mesh,
                                       const std::vector<Mat4>& jointMatrices) {
    if (m_resources == nullptr || meshIndex >= m_meshes.size() || jointMatrices.empty()) {
        return false;
    }

    GpuMesh& gpuMesh = m_meshes[meshIndex];
    if (!gpuMesh.gpuSkinCapable || !gpuMesh.skinBaseBuffer.valid() || !gpuMesh.skinInfoBuffer.valid() ||
        !gpuMesh.skinInfluenceBuffer.valid() || !gpuMesh.skinOutputBuffer.valid()) {
        return false;
    }

    const std::size_t matrixCount = jointMatrices.size() * 2u;
    const std::size_t floatCount = matrixCount * 16u;
    const gfx::Buffer* jointBuffer = m_resources->buffer(gpuMesh.skinJointBuffer);
    if (jointBuffer == nullptr || !jointBuffer->valid() || jointBuffer->count() != floatCount) {
        if (gpuMesh.skinJointBuffer.valid()) {
            m_resources->destroyBuffer(gpuMesh.skinJointBuffer);
        }
        gfx::BufferDesc jointDesc;
        jointDesc.type = gfx::BufferType::Storage;
        jointDesc.count = static_cast<uint32_t>(floatCount);
        jointDesc.flags = BGFX_BUFFER_COMPUTE_READ;
        jointDesc.name = mesh.name + ".skinJoints";
        gpuMesh.skinJointBuffer = m_resources->createBuffer(jointDesc);
        if (!gpuMesh.skinJointBuffer.valid()) {
            HBG_LOGE("material system: failed to create GPU skin joint buffer for '{}'", mesh.name);
            return false;
        }
    }

    if (gpuMesh.previousJointMatrices.size() != jointMatrices.size()) {
        gpuMesh.previousJointMatrices = jointMatrices;
    }

    std::vector<Mat4> palette;
    palette.reserve(matrixCount);
    palette.insert(palette.end(), jointMatrices.begin(), jointMatrices.end());
    palette.insert(palette.end(), gpuMesh.previousJointMatrices.begin(), gpuMesh.previousJointMatrices.end());
    if (!m_resources->updateBuffer(gpuMesh.skinJointBuffer, palette.data(),
                                   static_cast<uint32_t>(floatCount * sizeof(float)))) {
        HBG_LOGE("material system: failed to upload GPU skin joint palette for '{}'", mesh.name);
        return false;
    }

    gpuMesh.previousJointMatrices = jointMatrices;
    gpuMesh.vertexBuffer = gpuMesh.skinOutputBuffer;
    return true;
}

bool MaterialSystem::updateDeformedMeshes(RenderScene& scene) {
    if (m_resources == nullptr || m_meshes.size() != scene.meshes.size()) {
        return false;
    }

    m_gpuSkinMeshes.clear();
    const std::vector<Mat4> nodeWorlds =
        scene.skins.empty() ? std::vector<Mat4>{} : asset::evaluateNodeWorlds(scene);
    std::vector<uint8_t> updated(m_meshes.size(), 0);
    std::vector<asset::MeshVertex> deformed;
    std::vector<Mat4> jointMatrices;

    const auto computeJointMatrices = [&](const render::RenderObject& object, const asset::Skin& skin,
                                          std::vector<Mat4>& matrices) {
        matrices.assign(skin.joints.size(), Mat4::identity());
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
            matrices[jointIndex] =
                inverseMeshWorld * nodeWorlds[jointNode] * skin.inverseBindMatrices[jointIndex];
        }
    };

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
        if (!gpuMesh.dynamicVertex || !gpuMesh.cpuVertexBuffer.valid()) {
            continue;
        }
        if (updated[object.meshIndex] != 0) {
            HBG_LOGW("material system: deformed mesh '{}' is shared by multiple render objects", mesh.name);
            continue;
        }

        gpuMesh.vertexBuffer = gpuMesh.cpuVertexBuffer;
        if (hasSkin && !hasMorph && m_gpuSkinningEnabled && gpuMesh.gpuSkinCapable) {
            const asset::Skin& skin = scene.skins[static_cast<std::size_t>(object.skinIndex)];
            computeJointMatrices(object, skin, jointMatrices);
            if (!jointMatrices.empty() && updateGpuSkinMesh(object.meshIndex, mesh, jointMatrices)) {
                const Bounds localBounds = mesh.computeSkinnedBounds(jointMatrices);
                if (localBounds.valid()) {
                    object.worldBounds = transformBounds(object.world, localBounds);
                }
                updated[object.meshIndex] = 1;
                m_gpuSkinMeshes.push_back(object.meshIndex);
                continue;
            }
        }

        if (hasMorph) {
            mesh.applyMorphWeights(object.morphWeights, deformed);
        } else {
            deformed = mesh.vertices;
        }

        if (hasSkin) {
            const asset::Skin& skin = scene.skins[static_cast<std::size_t>(object.skinIndex)];
            computeJointMatrices(object, skin, jointMatrices);
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
        if (!m_resources->updateBuffer(gpuMesh.cpuVertexBuffer, deformed.data(),
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
    if (m_bindlessVertexBuffer.valid()) {
        resources.destroyBuffer(m_bindlessVertexBuffer);
    }
    if (m_bindlessIndexBuffer.valid()) {
        resources.destroyBuffer(m_bindlessIndexBuffer);
    }
    m_bindlessVertexBuffer = {};
    m_bindlessIndexBuffer = {};

    const std::array<gfx::TextureHandle, 5> bindlessTextures = {
        m_bindlessTextures.baseColor, m_bindlessTextures.metallicRoughness, m_bindlessTextures.normal,
        m_bindlessTextures.occlusion, m_bindlessTextures.emissive,
    };
    for (gfx::TextureHandle texture : bindlessTextures) {
        if (texture.valid()) {
            resources.destroyTexture(texture);
        }
    }
    m_bindlessTextures = {};

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

        const std::array<gfx::BufferHandle, 6> buffers = {
            mesh.cpuVertexBuffer,      mesh.skinBaseBuffer, mesh.skinInfoBuffer,
            mesh.skinInfluenceBuffer,  mesh.skinJointBuffer, mesh.skinOutputBuffer,
        };
        std::array<gfx::BufferHandle, 6> destroyed{};
        for (std::size_t index = 0; index < buffers.size(); ++index) {
            const gfx::BufferHandle buffer = buffers[index];
            if (!buffer.valid()) {
                continue;
            }
            bool duplicate = false;
            for (gfx::BufferHandle previous : destroyed) {
                if (previous == buffer) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                resources.destroyBuffer(buffer);
                destroyed[index] = buffer;
            }
        }

        if (mesh.vertexBuffer.valid()) {
            bool duplicate = false;
            for (gfx::BufferHandle buffer : buffers) {
                if (buffer == mesh.vertexBuffer) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                resources.destroyBuffer(mesh.vertexBuffer);
            }
        }
    }
    m_meshes.clear();

    m_samplers.shutdown(resources);
    m_materials.clear();
    m_decals.clear();
    m_bindlessAttempted = false;
    m_resources = nullptr;
}

} // namespace hbg::render
