#include "render/shadergraph/graph_binding.h"

#include "core/log.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <string>

namespace hbg::render::graph {
namespace {

std::string uniformName(const char* prefix, std::string_view refName) {
    std::string result(prefix);
    result += refName;
    return result;
}

const asset::GraphPropertyOverride* findPropertyOverride(const asset::Material& material, std::string_view refName) {
    for (const asset::GraphPropertyOverride& override : material.graphProperties) {
        if (override.refName == refName) {
            return &override;
        }
    }
    return nullptr;
}

const asset::GraphKeywordOverride* findKeywordOverride(const asset::Material& material, std::string_view refName) {
    for (const asset::GraphKeywordOverride& override : material.graphKeywords) {
        if (override.refName == refName) {
            return &override;
        }
    }
    return nullptr;
}

bool isTextureTransformOverride(const Asset& asset, std::string_view refName) {
    constexpr std::string_view suffix = "_ST";
    if (refName.size() <= suffix.size() ||
        refName.substr(refName.size() - suffix.size()) != suffix) {
        return false;
    }
    const PropertyDesc* property = asset.findProperty(refName.substr(0, refName.size() - suffix.size()));
    return property != nullptr && property->kind == PropertyKind::Texture2D &&
           property->useTilingAndOffset;
}

bool isTextureHdrDecodeOverride(const Asset& asset, std::string_view refName) {
    constexpr std::string_view suffix = "_HDR";
    if (refName.size() <= suffix.size() ||
        refName.substr(refName.size() - suffix.size()) != suffix) {
        return false;
    }
    const PropertyDesc* property = asset.findProperty(refName.substr(0, refName.size() - suffix.size()));
    return property != nullptr && property->isHDR;
}

int parseInteger(std::string_view text, int fallback) {
    int value = fallback;
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc{} ? value : fallback;
}

int keywordValue(const KeywordDesc& keyword, const asset::Material& material) {
    int defaultValue = 0;
    if (keyword.type != 0 && !keyword.entries.empty()) {
        const auto found = std::find_if(keyword.entries.begin(), keyword.entries.end(),
                                        [&](const KeywordEntryDesc& entry) {
                                            return entry.id == keyword.defaultValue;
                                        });
        defaultValue = found != keyword.entries.end()
                           ? static_cast<int>(found - keyword.entries.begin())
                           : 0;
    } else {
        defaultValue = keyword.defaultValue != 0 ? 1 : 0;
    }

    const asset::GraphKeywordOverride* override = findKeywordOverride(material, keyword.refName);
    if (override == nullptr) {
        return defaultValue;
    }
    if (keyword.type == 0) {
        const std::string value = override->value;
        if (value == "1" || value == "true" || value == "on") {
            return 1;
        }
        if (value == "0" || value == "false" || value == "off") {
            return 0;
        }
        HBG_LOGW("graph material: invalid boolean keyword override '{}={}'", keyword.refName, value);
        return defaultValue;
    }

    for (std::size_t index = 0; index < keyword.entries.size(); ++index) {
        if (keyword.entries[index].referenceName == override->value ||
            keyword.entries[index].displayName == override->value) {
            return static_cast<int>(index);
        }
    }
    const int id = parseInteger(override->value, -1);
    if (id >= 0) {
        const auto found = std::find_if(keyword.entries.begin(), keyword.entries.end(),
                                        [&](const KeywordEntryDesc& entry) { return entry.id == static_cast<uint32_t>(id); });
        if (found != keyword.entries.end()) {
            return static_cast<int>(found - keyword.entries.begin());
        }
    }
    HBG_LOGW("graph material: invalid enum keyword override '{}={}'", keyword.refName, override->value);
    return defaultValue;
}

uint64_t selectVariantKey(const Asset& asset, const asset::Material& material) {
    uint64_t key = 0;
    if (material.alphaMode == asset::AlphaMode::Mask) {
        key |= 1ull;
    }
    if (material.doubleSided) {
        key |= 2ull;
    }
    if (material.alphaMode == asset::AlphaMode::Blend) {
        key |= 4ull;
    }

    int bitOffset = 8;
    for (const KeywordDesc& keyword : asset.keywords()) {
        const int value = keywordValue(keyword, material);
        if (keyword.type != 0) {
            if (!keyword.entries.empty()) {
                key |= 1ull << (bitOffset + value);
            }
            bitOffset += std::max<int>(1, static_cast<int>(keyword.entries.size()));
        } else {
            if (value != 0) {
                key |= 1ull << bitOffset;
            }
            bitOffset += 1;
        }
    }
    return key;
}

Vec4 readVector(const PropertyDesc& property, const asset::GraphPropertyOverride* override) {
    Vec4 result(property.vector[0], property.vector[1], property.vector[2], property.vector[3]);
    if (override == nullptr) {
        return result;
    }
    const auto set = [&](std::size_t index, float value) {
        switch (index) {
        case 0:
            result.x = value;
            break;
        case 1:
            result.y = value;
            break;
        case 2:
            result.z = value;
            break;
        case 3:
            result.w = value;
            break;
        default:
            break;
        }
    };
    for (std::size_t index = 0; index < std::min<std::size_t>(override->count, 4); ++index) {
        set(index, override->values[index]);
    }
    return result;
}

Vec4 readTextureHdrDecode(const asset::GraphPropertyOverride* override) {
    Vec4 result(1.0f, 1.0f, 0.0f, 0.0f);
    if (override == nullptr) {
        return result;
    }
    for (std::size_t index = 0; index < std::min<std::size_t>(override->count, 4); ++index) {
        switch (index) {
        case 0:
            result.x = override->values[index];
            break;
        case 1:
            result.y = override->values[index];
            break;
        case 2:
            result.z = override->values[index];
            break;
        case 3:
            result.w = override->values[index];
            break;
        default:
            break;
        }
    }
    return result;
}

Mat4 readMatrix(const PropertyDesc& property, const asset::GraphPropertyOverride* override) {
    Mat4 result = Mat4::identity();
    std::copy(property.matrix.begin(), property.matrix.end(), result.values.begin());
    if (override == nullptr) {
        return result;
    }
    for (std::size_t index = 0; index < std::min<std::size_t>(override->count, 16); ++index) {
        result.values[index] = override->values[index];
    }
    return result;
}

uint32_t textureWidth(const gfx::ResourceManager& resources, gfx::TextureHandle texture) {
    const gfx::Texture* resource = resources.texture(texture);
    return resource != nullptr && resource->valid() ? resource->desc().width : 1u;
}

uint32_t textureHeight(const gfx::ResourceManager& resources, gfx::TextureHandle texture) {
    const gfx::Texture* resource = resources.texture(texture);
    return resource != nullptr && resource->valid() ? resource->desc().height : 1u;
}

gfx::TextureHandle fallbackForProperty(const PropertyDesc& property,
                                       const GraphBindingFallbacks& fallbacks) {
    if (property.kind == PropertyKind::Texture2D) {
        switch (property.defaultType) {
        case TextureDefaultType::Black:
            return fallbacks.textureBlack;
        case TextureDefaultType::Grey:
            return fallbacks.grey;
        case TextureDefaultType::Normal:
            return fallbacks.normal;
        case TextureDefaultType::LinearGrey:
            return fallbacks.linearGrey;
        case TextureDefaultType::Red:
            return fallbacks.red;
        case TextureDefaultType::White:
        default:
            return fallbacks.white;
        }
    }
    switch (property.kind) {
    case PropertyKind::Texture2DArray:
        return fallbacks.whiteArray;
    case PropertyKind::Texture3D:
        return fallbacks.white3D;
    case PropertyKind::Cubemap:
        return fallbacks.whiteCube;
    default:
        return fallbacks.white;
    }
}

std::optional<asset::LayeredTextureLayout> layeredLayoutForKind(PropertyKind kind) {
    switch (kind) {
    case PropertyKind::Texture2DArray:
        return asset::LayeredTextureLayout::Texture2DArray;
    case PropertyKind::Texture3D:
        return asset::LayeredTextureLayout::Texture3D;
    case PropertyKind::Cubemap:
        return asset::LayeredTextureLayout::Cubemap;
    default:
        return std::nullopt;
    }
}

uint32_t samplerFlagsForProperty(const PropertyDesc& property) {
    uint32_t flags = 0;
    if (property.samplerFilter == 1) {
        flags |= BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
    }
    if (property.samplerAnisotropy != 0) {
        flags |= BGFX_SAMPLER_MIN_ANISOTROPIC | BGFX_SAMPLER_MAG_ANISOTROPIC;
    }
    switch (property.samplerWrap) {
    case 1:
        flags |= BGFX_SAMPLER_UVW_CLAMP;
        break;
    case 2:
    case 3:
        flags |= BGFX_SAMPLER_UVW_MIRROR;
        break;
    default:
        break;
    }
    return flags;
}

} // namespace

uint64_t graphVariantKey(const Asset& asset, const asset::Material& material) {
    return selectVariantKey(asset, material);
}

Vec4 graphTextureTransform(const PropertyDesc& property, const asset::Material& material) {
    if (!property.useTilingAndOffset) {
        return Vec4(1.0f, 1.0f, 0.0f, 0.0f);
    }
    Vec4 result(property.tiling[0], property.tiling[1], property.offset[0], property.offset[1]);
    const asset::GraphPropertyOverride* override =
        findPropertyOverride(material, property.refName + "_ST");
    if (override == nullptr) {
        return result;
    }
    for (std::size_t index = 0; index < std::min<std::size_t>(override->count, 4); ++index) {
        switch (index) {
        case 0:
            result.x = override->values[index];
            break;
        case 1:
            result.y = override->values[index];
            break;
        case 2:
            result.z = override->values[index];
            break;
        case 3:
            result.w = override->values[index];
            break;
        default:
            break;
        }
    }
    return result;
}

bool GraphUniformCache::init(gfx::ResourceManager& resources) {
    m_resources = &resources;
    m_uniforms.clear();
    return true;
}

void GraphUniformCache::shutdown(gfx::ResourceManager& resources) {
    for (auto& [name, entry] : m_uniforms) {
        (void)name;
        if (entry.handle.valid()) {
            resources.destroyUniform(entry.handle);
        }
    }
    m_uniforms.clear();
    m_resources = nullptr;
}

gfx::UniformHandle GraphUniformCache::uniform(std::string_view name, gfx::UniformType type) {
    if (m_resources == nullptr || name.empty()) {
        return {};
    }
    const std::string key(name);
    const auto found = m_uniforms.find(key);
    if (found != m_uniforms.end()) {
        if (found->second.type != type) {
            HBG_LOGE("graph uniform '{}' requested with conflicting types", key);
            return {};
        }
        return found->second.handle;
    }
    const gfx::UniformHandle handle = m_resources->createUniform({key, type, 1});
    if (!handle.valid()) {
        return {};
    }
    m_uniforms.emplace(key, Entry{handle, type});
    return handle;
}

bool GraphBinding::init(gfx::ResourceManager& resources, GraphUniformCache& uniforms, const Asset& asset,
                        const asset::Material& material, const RenderScene& scene,
                        const GraphBindingFallbacks& fallbacks, std::string* error) {
    destroy(resources);
    m_resources = &resources;
    m_variantKey = graphVariantKey(asset, material);
    m_variantMissing = asset.findVariant(m_variantKey) == nullptr;

    const auto fail = [&](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        m_valid = false;
        return false;
    };

    m_globalMipBias = 0.0f;
    m_globalMipBiasUniform = uniforms.uniform("u_sg_globalMipBias", gfx::UniformType::Vec4);
    if (!m_globalMipBiasUniform.valid()) {
        return fail("failed to create graph global mip bias uniform");
    }

    for (const PropertyDesc& property : asset.properties()) {
        const asset::GraphPropertyOverride* override = findPropertyOverride(material, property.refName);
        if (isTextureKind(property.kind)) {
            TextureBinding binding;
            binding.stage = property.samplerStage >= 0 ? static_cast<uint8_t>(property.samplerStage) : 0;
            binding.samplerFlags = samplerFlagsForProperty(property);
            binding.sampler = uniforms.uniform(uniformName("s_sg_", property.refName), gfx::UniformType::Sampler);
            if (!binding.sampler.valid()) {
                return fail("failed to create graph sampler uniform for '" + property.refName + "'");
            }
            if (property.kind != PropertyKind::Gradient) {
                binding.texelSize = uniforms.uniform(uniformName("u_sg_", property.refName) + "_TexelSize",
                                                     gfx::UniformType::Vec4);
                if (!binding.texelSize.valid()) {
                    return fail("failed to create graph texel-size uniform for '" + property.refName + "'");
                }
            }
            if (property.useTilingAndOffset) {
                binding.scaleTranslate = uniforms.uniform(uniformName("u_sg_", property.refName) + "_ST",
                                                         gfx::UniformType::Vec4);
                if (!binding.scaleTranslate.valid()) {
                    return fail("failed to create graph texture transform uniform for '" +
                                property.refName + "'");
                }
                binding.scaleTranslateValue = graphTextureTransform(property, material);
            }
            if (property.isHDR) {
                binding.hdrDecode = uniforms.uniform(
                    uniformName("u_sg_", property.refName) + "_HDR", gfx::UniformType::Vec4);
                if (!binding.hdrDecode.valid()) {
                    return fail("failed to create graph HDR decode uniform for '" +
                                property.refName + "'");
                }
                binding.hdrDecodeValue = readTextureHdrDecode(
                    findPropertyOverride(material, property.refName + "_HDR"));
            }
            binding.texture = fallbackForProperty(property, fallbacks);
            const asset::GraphTextureOverride* textureOverride = nullptr;
            for (const asset::GraphTextureOverride& candidate : material.graphTextures) {
                if (candidate.refName == property.refName) {
                    textureOverride = &candidate;
                    break;
                }
            }
            if (textureOverride != nullptr) {
                std::vector<int> imageIndices = textureOverride->imageIndices;
                if (imageIndices.empty() && textureOverride->texture.valid()) {
                    imageIndices.push_back(textureOverride->texture.imageIndex);
                }
                if (const auto layout = layeredLayoutForKind(property.kind)) {
                    std::vector<const asset::ImageAsset*> images;
                    images.reserve(imageIndices.size());
                    for (int imageIndex : imageIndices) {
                        if (imageIndex < 0 ||
                            static_cast<std::size_t>(imageIndex) >= scene.images.size()) {
                            HBG_LOGW("graph material: layered texture '{}' has invalid image index {}",
                                     property.refName, imageIndex);
                            continue;
                        }
                        images.push_back(&scene.images[static_cast<std::size_t>(imageIndex)]);
                    }
                    asset::ImageAsset composed;
                    std::string composeError;
                    if (asset::TextureLoader::composeLayered(images, *layout, composed,
                                                             &composeError)) {
                        const gfx::TextureHandle texture =
                            asset::TextureLoader::createTexture(resources, composed, false,
                                                                textureOverride->texture.sampler,
                                                                property.refName);
                        if (texture.valid()) {
                            binding.texture = texture;
                            m_ownedTextures.push_back(texture);
                        } else {
                            HBG_LOGW("graph material: failed to upload layered texture '{}', using white fallback",
                                     property.refName);
                        }
                    } else {
                        HBG_LOGW("graph material: invalid layered texture '{}': {}; using white fallback",
                                 property.refName, composeError);
                    }
                } else if (!imageIndices.empty()) {
                    if (imageIndices.size() > 1) {
                        HBG_LOGW("graph material: 2D texture '{}' received {} images; using the first",
                                 property.refName, imageIndices.size());
                    }
                    const int imageIndex = imageIndices.front();
                    if (imageIndex >= 0 &&
                        static_cast<std::size_t>(imageIndex) < scene.images.size()) {
                        const asset::ImageAsset& image =
                            scene.images[static_cast<std::size_t>(imageIndex)];
                        const gfx::TextureHandle texture =
                            asset::TextureLoader::createTexture(resources, image, false,
                                                                textureOverride->texture.sampler,
                                                                image.name.empty() ? property.refName : image.name);
                        if (texture.valid()) {
                            binding.texture = texture;
                            m_ownedTextures.push_back(texture);
                        } else {
                            HBG_LOGW("graph material: failed to upload texture '{}', using white fallback",
                                     property.refName);
                        }
                    }
                }
            }
            if (!binding.texture.valid()) {
                binding.texture = fallbackForProperty(property, fallbacks);
            }
            if (!binding.texture.valid()) {
                return fail("no graph texture fallback is available");
            }
            const float width = static_cast<float>(textureWidth(resources, binding.texture));
            const float height = static_cast<float>(textureHeight(resources, binding.texture));
            binding.texelSizeValue = Vec4(1.0f / width, 1.0f / height, width, height);
            m_textures.push_back(binding);
            continue;
        }

        if (property.kind == PropertyKind::SamplerState) {
            continue;
        }

        ValueBinding binding;
        binding.kind = property.kind;
        binding.uniform = uniforms.uniform(uniformName("u_sg_", property.refName),
                                           isMatrixKind(property.kind) ? gfx::UniformType::Mat4
                                                                       : gfx::UniformType::Vec4);
        if (!binding.uniform.valid()) {
            return fail("failed to create graph uniform for '" + property.refName + "'");
        }
        if (isMatrixKind(property.kind)) {
            binding.matrix = readMatrix(property, override);
        } else {
            binding.vector = readVector(property, override);
        }
        m_values.push_back(binding);
    }

    for (const asset::GraphPropertyOverride& override : material.graphProperties) {
        if (asset.findProperty(override.refName) == nullptr &&
            !isTextureTransformOverride(asset, override.refName) &&
            !isTextureHdrDecodeOverride(asset, override.refName)) {
            HBG_LOGW("graph material: unknown property override '{}'", override.refName);
        }
    }
    for (const asset::GraphKeywordOverride& override : material.graphKeywords) {
        if (asset.findKeyword(override.refName) == nullptr) {
            HBG_LOGW("graph material: unknown keyword override '{}'", override.refName);
        }
    }

    m_valid = true;
    return true;
}

void GraphBinding::destroy(gfx::ResourceManager& resources) {
    for (gfx::TextureHandle texture : m_ownedTextures) {
        if (texture.valid()) {
            resources.destroyTexture(texture);
        }
    }
    m_ownedTextures.clear();
    m_values.clear();
    m_textures.clear();
    m_globalMipBiasUniform = {};
    m_globalMipBias = 0.0f;
    m_resources = nullptr;
    m_valid = false;
}

void GraphBinding::bind(gfx::Encoder& encoder) const {
    if (m_globalMipBiasUniform.valid()) {
        const Vec4 globalMipBias(m_globalMipBias, std::exp2(m_globalMipBias), 0.0f, 0.0f);
        encoder.setUniform(m_globalMipBiasUniform, &globalMipBias);
    }
    for (const ValueBinding& binding : m_values) {
        if (isMatrixKind(binding.kind)) {
            encoder.setUniform(binding.uniform, binding.matrix.values.data());
        } else {
            encoder.setUniform(binding.uniform, &binding.vector);
        }
    }
    for (const TextureBinding& binding : m_textures) {
        if (binding.sampler.valid() && binding.texture.valid()) {
            encoder.setTexture(binding.stage, binding.sampler, binding.texture, binding.samplerFlags);
        }
        if (binding.texelSize.valid()) {
            encoder.setUniform(binding.texelSize, &binding.texelSizeValue);
        }
        if (binding.scaleTranslate.valid()) {
            encoder.setUniform(binding.scaleTranslate, &binding.scaleTranslateValue);
        }
        if (binding.hdrDecode.valid()) {
            encoder.setUniform(binding.hdrDecode, &binding.hdrDecodeValue);
        }
    }
}

} // namespace hbg::render::graph
