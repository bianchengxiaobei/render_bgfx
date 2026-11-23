#include "asset/gltf_loader.h"

#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/texture_loader.h"
#include "core/log.h"
#include "core/math.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <nlohmann/json.hpp>

#if defined(HBG_HAS_DRACO) && HBG_HAS_DRACO
#include <draco/attributes/point_attribute.h>
#include <draco/compression/decode.h>
#include <draco/core/decoder_buffer.h>
#include <draco/mesh/mesh.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hbg::asset {
namespace {

struct Context {
    const cgltf_data* data = nullptr;
    std::filesystem::path baseDir;
    render::RenderScene scene;
    std::unordered_map<const cgltf_image*, int> imageCache;
    std::unordered_map<const cgltf_material*, uint32_t> materialPointerCache;
    std::unordered_map<MaterialKey, uint32_t, MaterialKeyHash> materialKeyCache;
    std::unordered_map<const cgltf_mesh*, uint32_t> meshCache;
    std::unordered_map<const cgltf_skin*, uint32_t> skinCache;
    std::unordered_map<const cgltf_node*, uint32_t> nodeIndices;
    std::vector<uint8_t> animatedNodes;
    std::unordered_map<const cgltf_primitive*, std::unordered_map<std::string, uint32_t>> dracoAttributeIds;
    uint32_t defaultMaterial = UINT32_MAX;
    bool dracoRequired = false;
    std::string fatalError;
};

std::string nameOr(const char* name, std::string fallback) {
    return name == nullptr || name[0] == '\0' ? std::move(fallback) : std::string(name);
}

Vec3 toVec3(const float* values) {
    return Vec3(values[0], values[1], values[2]);
}

Vec4 toVec4(const float* values) {
    return Vec4(values[0], values[1], values[2], values[3]);
}

Mat4 nodeWorld(const cgltf_node* node) {
    if (node == nullptr) {
        return Mat4::identity();
    }
    float matrix[16];
    cgltf_node_transform_world(node, matrix);
    return fromColumnMajor(matrix);
}

bool readFile(const std::filesystem::path& path, std::vector<uint8_t>& data) {
    std::ifstream file(path, std::ios::binary);
    if (!file.good()) {
        return false;
    }
    data.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    return !data.empty();
}

uint32_t readUint32LE(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) | (static_cast<uint32_t>(data[1]) << 8u) |
           (static_cast<uint32_t>(data[2]) << 16u) | (static_cast<uint32_t>(data[3]) << 24u);
}

bool loadGltfJson(const std::filesystem::path& path, nlohmann::json& document) {
    std::vector<uint8_t> bytes;
    if (!readFile(path, bytes)) {
        return false;
    }

    try {
        if (bytes.size() >= 12 && std::memcmp(bytes.data(), "glTF", 4) == 0) {
            if (bytes.size() < 20) {
                return false;
            }
            const uint32_t totalLength = readUint32LE(bytes.data() + 8);
            if (totalLength < 20u || totalLength > bytes.size() ||
                readUint32LE(bytes.data() + 16) != 0x4E4F534Au) {
                return false;
            }
            const uint32_t jsonLength = readUint32LE(bytes.data() + 12);
            if (jsonLength > totalLength - 20u || 20u + jsonLength > bytes.size()) {
                return false;
            }
            const uint8_t* begin = bytes.data() + 20;
            const uint8_t* end = begin + jsonLength;
            while (end > begin && (end[-1] == 0 || end[-1] == ' ' || end[-1] == '\r' || end[-1] == '\n' ||
                                    end[-1] == '\t')) {
                --end;
            }
            document = nlohmann::json::parse(begin, end);
        } else {
            document = nlohmann::json::parse(bytes.begin(), bytes.end());
        }
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

void collectDracoAttributeIds(const nlohmann::json& document, const cgltf_data* data,
                              std::unordered_map<const cgltf_primitive*, std::unordered_map<std::string, uint32_t>>&
                                  attributeIds) {
    const auto meshes = document.find("meshes");
    if (meshes == document.end() || !meshes->is_array()) {
        return;
    }
    for (std::size_t meshIndex = 0; meshIndex < meshes->size() && meshIndex < data->meshes_count; ++meshIndex) {
        const nlohmann::json& mesh = (*meshes)[meshIndex];
        if (!mesh.is_object()) {
            continue;
        }
        const auto primitives = mesh.find("primitives");
        if (primitives == mesh.end() || !primitives->is_array() ||
            primitives->size() > data->meshes[meshIndex].primitives_count) {
            continue;
        }
        for (std::size_t primitiveIndex = 0; primitiveIndex < primitives->size(); ++primitiveIndex) {
            const nlohmann::json& primitive = (*primitives)[primitiveIndex];
            if (!primitive.is_object()) {
                continue;
            }
            const auto extensions = primitive.find("extensions");
            if (extensions == primitive.end() || !extensions->is_object()) {
                continue;
            }
            const auto draco = extensions->find("KHR_draco_mesh_compression");
            if (draco == extensions->end() || !draco->is_object()) {
                continue;
            }
            const auto attributes = draco->find("attributes");
            if (attributes == draco->end() || !attributes->is_object()) {
                continue;
            }

            std::unordered_map<std::string, uint32_t> ids;
            for (auto iterator = attributes->begin(); iterator != attributes->end(); ++iterator) {
                if (!iterator.value().is_number_integer()) {
                    continue;
                }
                const int64_t value = iterator.value().get<int64_t>();
                if (value >= 0 && value <= UINT32_MAX) {
                    ids.emplace(iterator.key(), static_cast<uint32_t>(value));
                }
            }
            if (!ids.empty()) {
                attributeIds.emplace(&data->meshes[meshIndex].primitives[primitiveIndex], std::move(ids));
            }
        }
    }
}

int base64Value(char value) {
    if (value >= 'A' && value <= 'Z') {
        return value - 'A';
    }
    if (value >= 'a' && value <= 'z') {
        return value - 'a' + 26;
    }
    if (value >= '0' && value <= '9') {
        return value - '0' + 52;
    }
    if (value == '+') {
        return 62;
    }
    if (value == '/') {
        return 63;
    }
    return -1;
}

bool decodeBase64(std::string_view text, std::vector<uint8_t>& output) {
    output.clear();
    uint32_t accumulator = 0;
    int bits = 0;
    for (const char value : text) {
        if (value == '=') {
            break;
        }
        const int digit = base64Value(value);
        if (digit < 0) {
            if (value == '\r' || value == '\n' || value == ' ' || value == '\t') {
                continue;
            }
            return false;
        }
        accumulator = (accumulator << 6) | static_cast<uint32_t>(digit);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<uint8_t>((accumulator >> bits) & 0xffu));
        }
    }
    return true;
}

bool loadDataUri(std::string_view uri, std::vector<uint8_t>& data) {
    if (!uri.starts_with("data:")) {
        return false;
    }
    const std::size_t comma = uri.find(',');
    if (comma == std::string_view::npos) {
        return false;
    }
    const std::string_view header = uri.substr(0, comma);
    if (header.find(";base64") == std::string_view::npos) {
        return false;
    }
    return decodeBase64(uri.substr(comma + 1), data);
}

int addImage(Context& context, const cgltf_image* source) {
    if (source == nullptr) {
        return -1;
    }

    const auto cached = context.imageCache.find(source);
    if (cached != context.imageCache.end()) {
        return cached->second;
    }

    std::vector<uint8_t> bytes;
    bool loaded = false;
    if (source->buffer_view != nullptr) {
        const cgltf_buffer_view* view = source->buffer_view;
        if (view->buffer != nullptr && view->buffer->data != nullptr) {
            const auto* buffer = static_cast<const uint8_t*>(view->buffer->data);
            const uint8_t* begin = buffer + view->offset;
            bytes.assign(begin, begin + view->size);
            loaded = true;
        }
    } else if (source->uri != nullptr) {
        if (std::string_view(source->uri).starts_with("data:")) {
            loaded = loadDataUri(source->uri, bytes);
        } else {
            loaded = readFile(context.baseDir / source->uri, bytes);
        }
    }

    if (!loaded || bytes.empty()) {
        HBG_LOGW("gltf: failed to load image '{}'",
                 nameOr(source->name, source->uri != nullptr ? source->uri : "image"));
        context.imageCache.emplace(source, -1);
        return -1;
    }

    ImageAsset image;
    std::string error;
    if (!TextureLoader::loadFromMemory(bytes.data(), bytes.size(), image, &error)) {
        HBG_LOGW("gltf: decode image '{}' failed: {}",
                 nameOr(source->name, source->uri != nullptr ? source->uri : "image"), error);
        context.imageCache.emplace(source, -1);
        return -1;
    }
    image.name = nameOr(source->name, source->uri != nullptr ? source->uri : "image");

    const int index = static_cast<int>(context.scene.images.size());
    context.scene.images.push_back(std::move(image));
    context.imageCache.emplace(source, index);
    return index;
}

TextureWrap toWrapMode(cgltf_wrap_mode wrap) {
    switch (wrap) {
    case cgltf_wrap_mode_clamp_to_edge:
        return TextureWrap::Clamp;
    case cgltf_wrap_mode_mirrored_repeat:
        return TextureWrap::Mirror;
    case cgltf_wrap_mode_repeat:
    default:
        return TextureWrap::Repeat;
    }
}

TextureFilter toFilter(cgltf_filter_type filter) {
    switch (filter) {
    case cgltf_filter_type_nearest:
    case cgltf_filter_type_nearest_mipmap_nearest:
    case cgltf_filter_type_nearest_mipmap_linear:
        return TextureFilter::Nearest;
    case cgltf_filter_type_linear:
    case cgltf_filter_type_linear_mipmap_nearest:
    case cgltf_filter_type_linear_mipmap_linear:
    case cgltf_filter_type_undefined:
    default:
        return TextureFilter::Linear;
    }
}

TextureMipFilter toMipFilter(cgltf_filter_type filter) {
    switch (filter) {
    case cgltf_filter_type_nearest_mipmap_nearest:
    case cgltf_filter_type_linear_mipmap_nearest:
        return TextureMipFilter::Nearest;
    default:
        return TextureMipFilter::Linear;
    }
}

TextureSampler toSampler(const cgltf_sampler* source) {
    TextureSampler sampler;
    sampler.wrapS = TextureWrap::Repeat;
    sampler.wrapT = TextureWrap::Repeat;
    sampler.minFilter = TextureFilter::Linear;
    sampler.magFilter = TextureFilter::Linear;
    sampler.mipFilter = TextureMipFilter::Linear;
    if (source != nullptr) {
        sampler.wrapS = toWrapMode(source->wrap_s);
        sampler.wrapT = toWrapMode(source->wrap_t);
        sampler.minFilter = toFilter(source->min_filter);
        sampler.magFilter = toFilter(source->mag_filter);
        sampler.mipFilter = toMipFilter(source->min_filter);
    }
    return sampler;
}

TextureRef makeTextureRef(Context& context, const cgltf_texture_view& view) {
    TextureRef ref;
    ref.uvSet = view.texcoord;
    if (view.texture != nullptr) {
        ref.imageIndex = addImage(context, view.texture->basisu_image);
        if (ref.imageIndex < 0) {
            ref.imageIndex = addImage(context, view.texture->image);
        }
        ref.sampler = toSampler(view.texture->sampler);
    }
    return ref;
}

uint32_t addDefaultMaterial(Context& context) {
    if (context.defaultMaterial != UINT32_MAX) {
        return context.defaultMaterial;
    }

    Material material;
    material.name = "Default";
    const MaterialKey key = makeMaterialKey(material);
    const auto existing = context.materialKeyCache.find(key);
    if (existing != context.materialKeyCache.end()) {
        context.defaultMaterial = existing->second;
        return context.defaultMaterial;
    }

    context.defaultMaterial = static_cast<uint32_t>(context.scene.materials.size());
    context.scene.materials.push_back(std::move(material));
    context.materialKeyCache.emplace(key, context.defaultMaterial);
    return context.defaultMaterial;
}

uint32_t addMaterial(Context& context, const cgltf_material* source) {
    if (source == nullptr) {
        return addDefaultMaterial(context);
    }

    const auto cached = context.materialPointerCache.find(source);
    if (cached != context.materialPointerCache.end()) {
        return cached->second;
    }

    Material material;
    material.name = nameOr(source->name, "Material");
    material.type = source->unlit ? MaterialType::Unlit : MaterialType::Lit;
    material.baseColorFactor = toVec4(source->pbr_metallic_roughness.base_color_factor);
    material.metallic = source->pbr_metallic_roughness.metallic_factor;
    material.roughness = source->pbr_metallic_roughness.roughness_factor;
    material.emissiveFactor = toVec3(source->emissive_factor);
    material.emissiveStrength = source->has_emissive_strength ? source->emissive_strength.emissive_strength : 1.0f;
    material.normalScale = source->normal_texture.scale;
    material.occlusionStrength = source->occlusion_texture.scale;
    material.alphaCutoff = source->alpha_cutoff;
    material.doubleSided = source->double_sided != 0;

    switch (source->alpha_mode) {
    case cgltf_alpha_mode_mask:
        material.alphaMode = AlphaMode::Mask;
        material.renderQueue = kRenderQueueAlphaTest;
        break;
    case cgltf_alpha_mode_blend:
        material.alphaMode = AlphaMode::Blend;
        material.renderQueue = kRenderQueueTransparent;
        break;
    case cgltf_alpha_mode_opaque:
    default:
        material.alphaMode = AlphaMode::Opaque;
        material.renderQueue = kRenderQueueOpaque;
        break;
    }

    if (source->has_clearcoat) {
        material.features |= MaterialFeatureClearCoat;
    }
    if (source->has_transmission) {
        material.features |= MaterialFeatureTransmission;
    }
    if (source->has_ior) {
        material.features |= MaterialFeatureIor;
    }
    if (source->has_iridescence) {
        material.features |= MaterialFeatureIridescence;
    }
    if (source->has_anisotropy) {
        material.features |= MaterialFeatureAnisotropy;
    }
    if (source->has_emissive_strength) {
        material.features |= MaterialFeatureEmissiveStrength;
    }
    if (source->has_clearcoat) {
        material.clearCoat = source->clearcoat.clearcoat_factor;
        material.clearCoatRoughness = source->clearcoat.clearcoat_roughness_factor;
        material.clearCoatNormalScale = source->clearcoat.clearcoat_normal_texture.scale;
    }
    if (source->has_transmission) {
        material.transmission = source->transmission.transmission_factor;
    }
    if (source->has_ior) {
        material.ior = source->ior.ior;
    }
    if (source->has_iridescence) {
        material.iridescence = source->iridescence.iridescence_factor;
        material.iridescenceIor = source->iridescence.iridescence_ior;
        material.iridescenceThickness = source->iridescence.iridescence_thickness_max;
    }
    if (source->has_anisotropy) {
        material.anisotropy = source->anisotropy.anisotropy_strength;
        material.anisotropyRotation = source->anisotropy.anisotropy_rotation;
    }
    if (source->has_diffuse_transmission) {
        material.features |= MaterialFeatureSubsurface;
        material.subsurfaceMask = source->diffuse_transmission.diffuse_transmission_factor;
        material.thickness = 1.0f;
    }

    material.baseColorTexture = makeTextureRef(context, source->pbr_metallic_roughness.base_color_texture);
    material.metallicRoughnessTexture =
        makeTextureRef(context, source->pbr_metallic_roughness.metallic_roughness_texture);
    material.normalTexture = makeTextureRef(context, source->normal_texture);
    material.occlusionTexture = makeTextureRef(context, source->occlusion_texture);
    material.emissiveTexture = makeTextureRef(context, source->emissive_texture);

    const MaterialKey key = makeMaterialKey(material);
    const auto existing = context.materialKeyCache.find(key);
    if (existing != context.materialKeyCache.end()) {
        context.materialPointerCache.emplace(source, existing->second);
        return existing->second;
    }

    const uint32_t index = static_cast<uint32_t>(context.scene.materials.size());
    context.scene.materials.push_back(std::move(material));
    context.materialKeyCache.emplace(key, index);
    context.materialPointerCache.emplace(source, index);
    return index;
}

bool unpackAccessor(const cgltf_accessor* accessor, std::vector<float>& data, std::size_t neededComponents,
                    const std::unordered_map<const cgltf_accessor*, std::vector<float>>* decoded = nullptr) {
    if (accessor == nullptr || accessor->count == 0) {
        return false;
    }
    const std::size_t components = cgltf_num_components(accessor->type);
    if (components < neededComponents) {
        return false;
    }
    if (decoded != nullptr) {
        const auto iterator = decoded->find(accessor);
        if (iterator != decoded->end()) {
            if (iterator->second.size() < accessor->count * neededComponents) {
                return false;
            }
            data = iterator->second;
            return true;
        }
    }
    const std::size_t required = accessor->count * components;
    data.resize(required);
    const std::size_t written = cgltf_accessor_unpack_floats(accessor, data.data(), required);
    if (written < required) {
        data.resize(written);
    }
    return written >= accessor->count * neededComponents;
}

const cgltf_accessor* findAttribute(const cgltf_primitive& primitive, cgltf_attribute_type type, int index = 0) {
    for (cgltf_size attributeIndex = 0; attributeIndex < primitive.attributes_count; ++attributeIndex) {
        const cgltf_attribute& attribute = primitive.attributes[attributeIndex];
        if (attribute.type == type && attribute.index == index) {
            return attribute.data;
        }
    }
    return nullptr;
}

const cgltf_accessor* findAttributeByName(const cgltf_primitive& primitive, const char* name) {
    if (name == nullptr) {
        return nullptr;
    }
    for (cgltf_size attributeIndex = 0; attributeIndex < primitive.attributes_count; ++attributeIndex) {
        const cgltf_attribute& attribute = primitive.attributes[attributeIndex];
        if (attribute.name != nullptr && std::strcmp(attribute.name, name) == 0) {
            return attribute.data;
        }
    }
    return nullptr;
}

struct SkinAttributeSet {
    const cgltf_accessor* joints = nullptr;
    const cgltf_accessor* weights = nullptr;
    std::vector<float> jointData;
    std::vector<float> weightData;
    bool valid = false;
};

const cgltf_accessor* findMorphAttribute(const cgltf_morph_target& target, cgltf_attribute_type type) {
    for (cgltf_size attributeIndex = 0; attributeIndex < target.attributes_count; ++attributeIndex) {
        const cgltf_attribute& attribute = target.attributes[attributeIndex];
        if (attribute.type == type) {
            return attribute.data;
        }
    }
    return nullptr;
}

bool decodeDracoPrimitive(const cgltf_primitive& primitive,
                          const std::unordered_map<std::string, uint32_t>* attributeIds,
                          std::unordered_map<const cgltf_accessor*, std::vector<float>>& attributes,
                          std::vector<uint32_t>& indices, bool& hasIndices, std::string* error) {
#if defined(HBG_HAS_DRACO) && HBG_HAS_DRACO
    if (attributeIds == nullptr) {
        *error = "Draco attribute unique ids are missing";
        return false;
    }
    const cgltf_draco_mesh_compression& compression = primitive.draco_mesh_compression;
    if (compression.buffer_view == nullptr || compression.buffer_view->buffer == nullptr ||
        compression.buffer_view->buffer->data == nullptr || compression.buffer_view->size == 0) {
        *error = "KHR_draco_mesh_compression has no compressed data";
        return false;
    }

    const auto* buffer = static_cast<const uint8_t*>(compression.buffer_view->buffer->data);
    const uint8_t* compressed = buffer + compression.buffer_view->offset;
    draco::DecoderBuffer decoderBuffer;
    decoderBuffer.Init(reinterpret_cast<const char*>(compressed), compression.buffer_view->size);

    draco::Decoder decoder;
    const auto geometryType = decoder.GetEncodedGeometryType(&decoderBuffer);
    if (!geometryType.ok()) {
        *error = "Draco failed to inspect geometry: " + geometryType.status().error_msg_string();
        return false;
    }
    if (geometryType.value() != draco::TRIANGULAR_MESH) {
        *error = "Draco geometry is not a triangular mesh";
        return false;
    }

    auto meshResult = decoder.DecodeMeshFromBuffer(&decoderBuffer);
    if (!meshResult.ok()) {
        *error = "Draco decode failed: " + meshResult.status().error_msg_string();
        return false;
    }
    std::unique_ptr<draco::Mesh> mesh = std::move(meshResult).value();
    if (mesh == nullptr || mesh->num_points() == 0 || mesh->num_faces() == 0) {
        *error = "Draco mesh is empty";
        return false;
    }

    const uint32_t pointCount = mesh->num_points();
    for (cgltf_size attributeIndex = 0; attributeIndex < compression.attributes_count; ++attributeIndex) {
        const cgltf_attribute& compressionAttribute = compression.attributes[attributeIndex];
        const cgltf_accessor* accessor = findAttributeByName(primitive, compressionAttribute.name);
        if (accessor == nullptr) {
            *error = "Draco attribute is not present in the primitive";
            return false;
        }
        if (accessor->count != pointCount) {
            *error = "Draco attribute accessor count does not match decoded point count";
            return false;
        }

        const std::size_t components = cgltf_num_components(accessor->type);
        const auto uniqueId = compressionAttribute.name != nullptr
                                  ? attributeIds->find(compressionAttribute.name)
                                  : attributeIds->end();
        if (uniqueId == attributeIds->end()) {
            *error = "Draco attribute unique id is missing";
            return false;
        }
        const draco::PointAttribute* attribute = mesh->GetAttributeByUniqueId(uniqueId->second);
        if (attribute == nullptr) {
            *error = "Draco attribute " + std::to_string(uniqueId->second) + " was not found";
            return false;
        }
        if (attribute->num_components() < components) {
            *error = "Draco attribute " +
                     std::string(compressionAttribute.name != nullptr ? compressionAttribute.name : "?") +
                     " has " + std::to_string(attribute->num_components()) + " components, accessor expects " +
                     std::to_string(components);
            return false;
        }

        std::vector<float> values(static_cast<std::size_t>(pointCount) * components);
        for (uint32_t point = 0; point < pointCount; ++point) {
            float* destination = values.data() + static_cast<std::size_t>(point) * components;
            if (!attribute->ConvertValue(attribute->mapped_index(draco::PointIndex(point)),
                                         static_cast<int8_t>(components), destination)) {
                *error = "Draco attribute conversion failed";
                return false;
            }
        }
        attributes.emplace(accessor, std::move(values));
    }

    indices.resize(static_cast<std::size_t>(mesh->num_faces()) * 3u);
    for (uint32_t face = 0; face < mesh->num_faces(); ++face) {
        const draco::Mesh::Face& triangle = mesh->face(draco::FaceIndex(face));
        for (uint32_t corner = 0; corner < 3; ++corner) {
            const uint32_t point = triangle[corner].value();
            if (point >= pointCount) {
                *error = "Draco face references an invalid point";
                return false;
            }
            indices[static_cast<std::size_t>(face) * 3u + corner] = point;
        }
    }
    hasIndices = true;
    return true;
#else
    (void)primitive;
    (void)attributeIds;
    (void)attributes;
    (void)indices;
    (void)hasIndices;
    *error = "Draco support is disabled";
    return false;
#endif
}

uint32_t addMesh(Context& context, const cgltf_mesh* source) {
    const auto cached = context.meshCache.find(source);
    if (cached != context.meshCache.end()) {
        return cached->second;
    }

    Mesh mesh;
    mesh.name = nameOr(source->name, "Mesh");
    bool allNormals = true;
    bool allTangents = true;

    for (cgltf_size primitiveIndex = 0; primitiveIndex < source->primitives_count; ++primitiveIndex) {
        const cgltf_primitive& primitive = source->primitives[primitiveIndex];
        if (primitive.type != cgltf_primitive_type_triangles) {
            HBG_LOGW("gltf: mesh '{}' primitive {} is not a triangle list", mesh.name, primitiveIndex);
            continue;
        }

        const cgltf_accessor* positions = findAttribute(primitive, cgltf_attribute_type_position);
        if (positions == nullptr || positions->count == 0) {
            continue;
        }

        std::unordered_map<const cgltf_accessor*, std::vector<float>> dracoAttributes;
        std::vector<uint32_t> dracoIndices;
        bool dracoHasIndices = false;
        const std::unordered_map<const cgltf_accessor*, std::vector<float>>* decodedAttributes = nullptr;
        if (primitive.has_draco_mesh_compression) {
            const auto ids = context.dracoAttributeIds.find(&primitive);
            const auto* attributeIds = ids == context.dracoAttributeIds.end() ? nullptr : &ids->second;
            std::string decodeError;
            if (decodeDracoPrimitive(primitive, attributeIds, dracoAttributes, dracoIndices, dracoHasIndices,
                                     &decodeError)) {
                decodedAttributes = &dracoAttributes;
            } else {
                const std::string message =
                    "gltf: mesh '" + mesh.name + "' primitive " + std::to_string(primitiveIndex) + ": " + decodeError;
                if (context.dracoRequired) {
                    context.fatalError = message;
                    return UINT32_MAX;
                }
                HBG_LOGW("{}; using uncompressed accessors", message);
            }
        }

        std::vector<float> positionData;
        if (!unpackAccessor(positions, positionData, 3, decodedAttributes)) {
            continue;
        }

        const cgltf_accessor* normals = findAttribute(primitive, cgltf_attribute_type_normal);
        const cgltf_accessor* tangents = findAttribute(primitive, cgltf_attribute_type_tangent);
        const cgltf_accessor* uv0 = findAttribute(primitive, cgltf_attribute_type_texcoord, 0);
        const cgltf_accessor* uv1 = findAttribute(primitive, cgltf_attribute_type_texcoord, 1);
        const cgltf_accessor* colors = findAttribute(primitive, cgltf_attribute_type_color, 0);
        std::vector<float> normalData;
        std::vector<float> tangentData;
        std::vector<float> uv0Data;
        std::vector<float> uv1Data;
        std::vector<float> colorData;

        const bool hasNormals = unpackAccessor(normals, normalData, 3, decodedAttributes);
        const bool hasTangents = unpackAccessor(tangents, tangentData, 4, decodedAttributes);
        const bool hasUv0 = unpackAccessor(uv0, uv0Data, 2, decodedAttributes);
        const bool hasUv1 = unpackAccessor(uv1, uv1Data, 2, decodedAttributes);
        const bool hasColors =
            unpackAccessor(colors, colorData, 3, decodedAttributes) ||
            unpackAccessor(colors, colorData, 4, decodedAttributes);

        std::map<int, SkinAttributeSet> skinSets;
        for (cgltf_size attributeIndex = 0; attributeIndex < primitive.attributes_count; ++attributeIndex) {
            const cgltf_attribute& attribute = primitive.attributes[attributeIndex];
            if (attribute.type != cgltf_attribute_type_joints && attribute.type != cgltf_attribute_type_weights) {
                continue;
            }
            if (attribute.index < 0) {
                continue;
            }
            SkinAttributeSet& set = skinSets[attribute.index];
            const cgltf_accessor*& slot =
                attribute.type == cgltf_attribute_type_joints ? set.joints : set.weights;
            if (slot == nullptr) {
                slot = attribute.data;
            }
        }

        std::size_t validSkinSetCount = 0;
        for (auto& [setIndex, set] : skinSets) {
            if (set.joints == nullptr || set.weights == nullptr) {
                HBG_LOGW("gltf: mesh '{}' primitive {} influence set {} is missing {}; ignored", mesh.name,
                         primitiveIndex, setIndex, set.joints == nullptr ? "JOINTS" : "WEIGHTS");
                continue;
            }
            const bool hasJoints = unpackAccessor(set.joints, set.jointData, 4, decodedAttributes);
            const bool hasWeights = unpackAccessor(set.weights, set.weightData, 4, decodedAttributes);
            set.valid = hasJoints && hasWeights && set.jointData.size() >= positions->count * 4 &&
                        set.weightData.size() >= positions->count * 4;
            if (!set.valid) {
                HBG_LOGW("gltf: mesh '{}' primitive {} has invalid influence set {}", mesh.name, primitiveIndex,
                         setIndex);
            } else {
                ++validSkinSetCount;
            }
        }

        const auto skinSetZero = skinSets.find(0);
        const bool hasSkinInfluences = skinSetZero != skinSets.end() && skinSetZero->second.valid;
        if (!hasSkinInfluences) {
            for (const auto& [setIndex, set] : skinSets) {
                if (set.valid) {
                    HBG_LOGW("gltf: mesh '{}' primitive {} has influence set {} without a valid set 0; ignored",
                             mesh.name, primitiveIndex, setIndex);
                    break;
                }
            }
        }

        allNormals = allNormals && hasNormals;
        allTangents = allTangents && hasTangents;

        const cgltf_size primitiveTargetCount = primitive.targets_count;
        if (mesh.morphTargets.empty() && primitiveTargetCount > 0) {
            mesh.morphTargets.resize(primitiveTargetCount);
            for (cgltf_size targetIndex = 0; targetIndex < primitiveTargetCount; ++targetIndex) {
                std::string targetName = "Target " + std::to_string(targetIndex);
                if (source->target_names != nullptr && targetIndex < source->target_names_count &&
                    source->target_names[targetIndex] != nullptr && source->target_names[targetIndex][0] != '\0') {
                    targetName = source->target_names[targetIndex];
                }
                mesh.morphTargets[targetIndex].name = std::move(targetName);
            }
            mesh.morphWeights.assign(primitiveTargetCount, 0.0f);
            if (source->weights != nullptr && source->weights_count == primitiveTargetCount) {
                std::copy(source->weights, source->weights + primitiveTargetCount, mesh.morphWeights.begin());
            }
        }
        bool parseTargets = primitiveTargetCount > 0;
        if (parseTargets && primitiveTargetCount != mesh.morphTargets.size()) {
            HBG_LOGW("gltf: mesh '{}' primitive {} has {} morph targets, expected {}", mesh.name, primitiveIndex,
                     primitiveTargetCount, mesh.morphTargets.size());
            parseTargets = false;
        }

        const std::size_t vertexOffset = mesh.vertices.size();
        const std::size_t vertexCount = positions->count;
        mesh.vertices.resize(vertexOffset + vertexCount);

        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
            MeshVertex& output = mesh.vertices[vertexOffset + vertex];
            const std::size_t positionIndex = vertex * 3;
            output.position =
                Vec3(positionData[positionIndex + 0], positionData[positionIndex + 1], positionData[positionIndex + 2]);
            output.previousPosition = output.position;

            if (hasNormals) {
                const std::size_t index = vertex * 3;
                output.normal = Vec3(normalData[index + 0], normalData[index + 1], normalData[index + 2]);
            }
            if (hasTangents) {
                const std::size_t index = vertex * 4;
                output.tangent = Vec4(tangentData[index + 0], tangentData[index + 1], tangentData[index + 2],
                                      tangentData[index + 3]);
            }
            if (hasUv0) {
                const std::size_t index = vertex * 2;
                output.texcoord0 = Vec2(uv0Data[index + 0], uv0Data[index + 1]);
            }
            if (hasUv1) {
                const std::size_t index = vertex * 2;
                output.texcoord1 = Vec2(uv1Data[index + 0], uv1Data[index + 1]);
            }
            if (hasColors) {
                const std::size_t components = colorData.size() / vertexCount;
                const std::size_t index = vertex * components;
                output.color = Vec4(colorData[index + 0], colorData[index + 1], colorData[index + 2],
                                    components >= 4 ? colorData[index + 3] : 1.0f);
            }
        }

        if (hasSkinInfluences) {
            if (mesh.skinVertices.size() < vertexOffset + vertexCount) {
                mesh.skinVertices.resize(vertexOffset + vertexCount);
            }
            mesh.skinInfluences.reserve(mesh.skinInfluences.size() +
                                        vertexCount * validSkinSetCount * kSkinInfluencesPerSet);
            std::size_t normalizedSkinWeights = 0;
            std::size_t zeroSkinWeights = 0;
            for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
                SkinVertex& skin = mesh.skinVertices[vertexOffset + vertex];
                skin = SkinVertex{};
                skin.influenceOffset = static_cast<uint32_t>(mesh.skinInfluences.size());
                const std::size_t index = vertex * kSkinInfluencesPerSet;
                uint32_t influenceCount = 0;
                for (const auto& entry : skinSets) {
                    const SkinAttributeSet& set = entry.second;
                    if (!set.valid) {
                        continue;
                    }
                    for (std::size_t influence = 0; influence < kSkinInfluencesPerSet; ++influence) {
                        SkinInfluence output;
                        output.joint = static_cast<uint16_t>(
                            std::clamp(set.jointData[index + influence], 0.0f, 65535.0f));
                        output.weight = set.weightData[index + influence];
                        mesh.skinInfluences.push_back(output);
                        ++influenceCount;
                    }
                }
                skin.influenceCount = influenceCount;

                float totalWeight = 0.0f;
                for (uint32_t influence = 0; influence < influenceCount; ++influence) {
                    totalWeight += mesh.skinInfluences[skin.influenceOffset + influence].weight;
                }
                if (totalWeight > 1.0e-8f) {
                    if (std::abs(totalWeight - 1.0f) > 1.0e-5f) {
                        const float inverseWeight = 1.0f / totalWeight;
                        for (uint32_t influence = 0; influence < influenceCount; ++influence) {
                            mesh.skinInfluences[skin.influenceOffset + influence].weight *= inverseWeight;
                        }
                        ++normalizedSkinWeights;
                    }
                } else {
                    ++zeroSkinWeights;
                }
            }
            if (normalizedSkinWeights > 0) {
                HBG_LOGW("gltf: mesh '{}' primitive {} normalized skin weights for {} vertices", mesh.name,
                         primitiveIndex, normalizedSkinWeights);
            }
            if (zeroSkinWeights > 0) {
                HBG_LOGW("gltf: mesh '{}' primitive {} has non-positive skin weight sum for {} vertices", mesh.name,
                         primitiveIndex, zeroSkinWeights);
            }
        } else if (!mesh.skinVertices.empty()) {
            mesh.skinVertices.resize(vertexOffset + vertexCount);
        }

        mesh.hasNormals = mesh.hasNormals || hasNormals;
        mesh.hasTangents = mesh.hasTangents || hasTangents;
        mesh.hasUv0 = mesh.hasUv0 || hasUv0;
        mesh.hasUv1 = mesh.hasUv1 || hasUv1;
        mesh.hasColors = mesh.hasColors || hasColors;

        for (std::size_t targetIndex = 0; targetIndex < mesh.morphTargets.size(); ++targetIndex) {
            MorphTarget& target = mesh.morphTargets[targetIndex];
            target.positionDeltas.resize(vertexOffset + vertexCount);
            target.normalDeltas.resize(vertexOffset + vertexCount);
            target.tangentDeltas.resize(vertexOffset + vertexCount);
            if (!parseTargets || targetIndex >= primitive.targets_count) {
                continue;
            }

            const cgltf_morph_target& sourceTarget = primitive.targets[targetIndex];
            const cgltf_accessor* positionDeltas = findMorphAttribute(sourceTarget, cgltf_attribute_type_position);
            std::vector<float> deltaData;
            if (unpackAccessor(positionDeltas, deltaData, 3) && deltaData.size() >= vertexCount * 3) {
                for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
                    const std::size_t index = vertex * 3;
                    target.positionDeltas[vertexOffset + vertex] =
                        Vec3(deltaData[index + 0], deltaData[index + 1], deltaData[index + 2]);
                }
            }

            const cgltf_accessor* normalDeltas = findMorphAttribute(sourceTarget, cgltf_attribute_type_normal);
            if (unpackAccessor(normalDeltas, deltaData, 3) && deltaData.size() >= vertexCount * 3) {
                target.hasNormals = true;
                for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
                    const std::size_t index = vertex * 3;
                    target.normalDeltas[vertexOffset + vertex] =
                        Vec3(deltaData[index + 0], deltaData[index + 1], deltaData[index + 2]);
                }
            }

            const cgltf_accessor* tangentDeltas = findMorphAttribute(sourceTarget, cgltf_attribute_type_tangent);
            if (unpackAccessor(tangentDeltas, deltaData, 3) && deltaData.size() >= vertexCount * 3) {
                target.hasTangents = true;
                for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
                    const std::size_t index = vertex * 3;
                    target.tangentDeltas[vertexOffset + vertex] =
                        Vec4(deltaData[index + 0], deltaData[index + 1], deltaData[index + 2], 0.0f);
                }
            }
        }

        SubMesh subMesh;
        subMesh.indexOffset = static_cast<uint32_t>(mesh.indices.size());
        subMesh.materialIndex = addMaterial(context, primitive.material);
        Bounds primitiveBounds;
        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
            primitiveBounds.expand(mesh.vertices[vertexOffset + vertex].position);
        }
        subMesh.bounds = primitiveBounds;
        mesh.bounds.expand(primitiveBounds);

        if (dracoHasIndices) {
            mesh.indices.reserve(mesh.indices.size() + dracoIndices.size());
            for (const uint32_t index : dracoIndices) {
                if (index < vertexCount) {
                    mesh.indices.push_back(static_cast<uint32_t>(vertexOffset + index));
                }
            }
        } else if (primitive.indices != nullptr) {
            const cgltf_size count = primitive.indices->count;
            mesh.indices.reserve(mesh.indices.size() + count);
            for (cgltf_size index = 0; index < count; ++index) {
                const cgltf_size value = cgltf_accessor_read_index(primitive.indices, index);
                if (value < vertexCount) {
                    mesh.indices.push_back(static_cast<uint32_t>(vertexOffset + value));
                }
            }
        } else {
            mesh.indices.reserve(mesh.indices.size() + vertexCount);
            for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
                mesh.indices.push_back(static_cast<uint32_t>(vertexOffset + vertex));
            }
        }
        subMesh.indexCount = static_cast<uint32_t>(mesh.indices.size()) - subMesh.indexOffset;
        mesh.submeshes.push_back(subMesh);
    }

    if (mesh.hasUv0) {
        for (cgltf_size primitiveIndex = 0; primitiveIndex < source->primitives_count; ++primitiveIndex) {
            const cgltf_primitive& primitive = source->primitives[primitiveIndex];
            if (primitive.type == cgltf_primitive_type_triangles &&
                findAttribute(primitive, cgltf_attribute_type_texcoord, 0) == nullptr) {
                allTangents = false;
            }
        }
    } else {
        allTangents = false;
    }

    mesh.recomputeBounds();
    if (!allNormals) {
        mesh.hasNormals = false;
        mesh.generateNormals();
    }
    if (!allTangents) {
        mesh.hasTangents = false;
        mesh.generateTangents();
    }

    const uint32_t index = static_cast<uint32_t>(context.scene.meshes.size());
    context.scene.meshes.push_back(std::move(mesh));
    context.meshCache.emplace(source, index);
    return index;
}

int nodeIndexOf(const Context& context, const cgltf_node* node) {
    if (node == nullptr) {
        return -1;
    }
    const auto iterator = context.nodeIndices.find(node);
    return iterator == context.nodeIndices.end() ? -1 : static_cast<int>(iterator->second);
}

void addSceneNodes(Context& context) {
    const cgltf_data* data = context.data;
    context.scene.nodes.reserve(data->nodes_count);
    context.nodeIndices.reserve(data->nodes_count);
    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
        context.nodeIndices.emplace(&data->nodes[nodeIndex], static_cast<uint32_t>(nodeIndex));
    }

    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
        const cgltf_node* source = &data->nodes[nodeIndex];
        SceneNode node;
        node.name = nameOr(source->name, "Node");
        if (source->has_matrix) {
            node.hasMatrix = true;
            node.matrix = fromColumnMajor(source->matrix);
        } else {
            node.translation = toVec3(source->translation);
            node.rotation = toVec4(source->rotation);
            node.scale = toVec3(source->scale);
        }
        node.baseTranslation = node.translation;
        node.baseRotation = node.rotation;
        node.baseScale = node.scale;
        if (source->mesh != nullptr && source->mesh->primitives_count > 0 &&
            source->mesh->primitives[0].targets_count > 0) {
            const cgltf_size targetCount = source->mesh->primitives[0].targets_count;
            if (source->weights != nullptr && source->weights_count == targetCount) {
                node.weights.assign(source->weights, source->weights + source->weights_count);
            } else if (source->mesh->weights != nullptr && source->mesh->weights_count == targetCount) {
                node.weights.assign(source->mesh->weights, source->mesh->weights + source->mesh->weights_count);
            } else {
                node.weights.assign(targetCount, 0.0f);
            }
            node.baseWeights = node.weights;
        }
        context.scene.nodes.push_back(std::move(node));
    }

    for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
        const cgltf_node* source = &data->nodes[nodeIndex];
        for (cgltf_size childIndex = 0; childIndex < source->children_count; ++childIndex) {
            const int child = nodeIndexOf(context, source->children[childIndex]);
            if (child >= 0) {
                context.scene.nodes[static_cast<std::size_t>(child)].parent = static_cast<int>(nodeIndex);
            }
        }
    }
    context.animatedNodes.assign(context.scene.nodes.size(), 0);
}

uint32_t addSkin(Context& context, const cgltf_skin* source) {
    if (source == nullptr) {
        return UINT32_MAX;
    }
    const auto cached = context.skinCache.find(source);
    if (cached != context.skinCache.end()) {
        return cached->second;
    }

    Skin skin;
    skin.name = nameOr(source->name, "Skin");
    skin.joints.assign(source->joints_count, UINT32_MAX);
    skin.inverseBindMatrices.assign(source->joints_count, Mat4::identity());

    for (cgltf_size jointIndex = 0; jointIndex < source->joints_count; ++jointIndex) {
        const int nodeIndex = nodeIndexOf(context, source->joints[jointIndex]);
        if (nodeIndex >= 0) {
            skin.joints[jointIndex] = static_cast<uint32_t>(nodeIndex);
        } else {
            HBG_LOGW("gltf: skin '{}' joint {} references an unknown node", skin.name, jointIndex);
        }
    }

    if (source->inverse_bind_matrices != nullptr) {
        std::vector<float> matrixData;
        if (unpackAccessor(source->inverse_bind_matrices, matrixData, 16) &&
            matrixData.size() >= source->joints_count * 16) {
            for (cgltf_size jointIndex = 0; jointIndex < source->joints_count; ++jointIndex) {
                skin.inverseBindMatrices[jointIndex] = fromColumnMajor(&matrixData[jointIndex * 16]);
            }
        } else {
            HBG_LOGW("gltf: skin '{}' has invalid inverse bind matrices", skin.name);
        }
    }

    const uint32_t index = static_cast<uint32_t>(context.scene.skins.size());
    context.scene.skins.push_back(std::move(skin));
    context.skinCache.emplace(source, index);
    return index;
}

void addAnimations(Context& context) {
    const cgltf_data* data = context.data;
    for (cgltf_size animationIndex = 0; animationIndex < data->animations_count; ++animationIndex) {
        const cgltf_animation& source = data->animations[animationIndex];
        AnimationClip clip;
        clip.name = nameOr(source.name, "Animation");

        for (cgltf_size channelIndex = 0; channelIndex < source.channels_count; ++channelIndex) {
            const cgltf_animation_channel& sourceChannel = source.channels[channelIndex];
            const int nodeIndex = nodeIndexOf(context, sourceChannel.target_node);
            if (nodeIndex < 0 || sourceChannel.sampler == nullptr) {
                continue;
            }

            AnimationPath path;
            std::size_t outputComponents = 0;
            switch (sourceChannel.target_path) {
            case cgltf_animation_path_type_translation:
                path = AnimationPath::Translation;
                outputComponents = 3;
                break;
            case cgltf_animation_path_type_rotation:
                path = AnimationPath::Rotation;
                outputComponents = 4;
                break;
            case cgltf_animation_path_type_scale:
                path = AnimationPath::Scale;
                outputComponents = 3;
                break;
            case cgltf_animation_path_type_weights:
                path = AnimationPath::Weights;
                break;
            default:
                continue;
            }

            AnimationInterpolation interpolation;
            switch (sourceChannel.sampler->interpolation) {
            case cgltf_interpolation_type_step:
                interpolation = AnimationInterpolation::Step;
                break;
            case cgltf_interpolation_type_linear:
                interpolation = AnimationInterpolation::Linear;
                break;
            case cgltf_interpolation_type_cubic_spline:
                interpolation = AnimationInterpolation::CubicSpline;
                break;
            default:
                continue;
            }

            const cgltf_accessor* input = sourceChannel.sampler->input;
            const cgltf_accessor* output = sourceChannel.sampler->output;
            if (input == nullptr || output == nullptr || input->type != cgltf_type_scalar ||
                input->count == 0 || output->count < input->count) {
                continue;
            }

            std::vector<float> timeData;
            if (!unpackAccessor(input, timeData, 1) || timeData.size() < input->count) {
                continue;
            }

            const bool cubic = interpolation == AnimationInterpolation::CubicSpline;
            if (path == AnimationPath::Weights) {
                const cgltf_node* targetNode = sourceChannel.target_node;
                if (targetNode == nullptr || targetNode->mesh == nullptr ||
                    targetNode->mesh->primitives_count == 0) {
                    continue;
                }
                const std::size_t targetCount = targetNode->mesh->primitives[0].targets_count;
                if (targetCount == 0) {
                    continue;
                }
                std::vector<float> weightData;
                if (!unpackAccessor(output, weightData, 1)) {
                    continue;
                }
                const std::size_t valuesPerKey = targetCount * (cubic ? 3u : 1u);
                if (weightData.size() < static_cast<std::size_t>(input->count) * valuesPerKey) {
                    continue;
                }
                bool sorted = true;
                for (cgltf_size keyIndex = 1; keyIndex < input->count; ++keyIndex) {
                    if (timeData[keyIndex] < timeData[keyIndex - 1]) {
                        sorted = false;
                        break;
                    }
                }
                if (!sorted) {
                    continue;
                }
                for (std::size_t targetIndex = 0; targetIndex < targetCount; ++targetIndex) {
                    AnimationChannel channel;
                    channel.node = static_cast<uint32_t>(nodeIndex);
                    channel.path = AnimationPath::Weights;
                    channel.interpolation = interpolation;
                    channel.targetIndex = static_cast<uint32_t>(targetIndex);
                    channel.times.assign(timeData.begin(), timeData.begin() + input->count);
                    channel.values.resize(input->count);
                    if (cubic) {
                        channel.inTangents.resize(input->count);
                        channel.outTangents.resize(input->count);
                    }
                    for (cgltf_size keyIndex = 0; keyIndex < input->count; ++keyIndex) {
                        const std::size_t base = static_cast<std::size_t>(keyIndex) * valuesPerKey;
                        if (cubic) {
                            channel.inTangents[keyIndex] =
                                Vec4(weightData[base + targetIndex], 0.0f, 0.0f, 0.0f);
                            channel.values[keyIndex] =
                                Vec4(weightData[base + targetCount + targetIndex], 0.0f, 0.0f, 0.0f);
                            channel.outTangents[keyIndex] =
                                Vec4(weightData[base + targetCount * 2u + targetIndex], 0.0f, 0.0f, 0.0f);
                        } else {
                            channel.values[keyIndex] =
                                Vec4(weightData[base + targetIndex], 0.0f, 0.0f, 0.0f);
                        }
                    }
                    clip.channels.push_back(std::move(channel));
                }
                clip.duration = std::max(clip.duration, timeData[input->count - 1]);
                context.animatedNodes[static_cast<std::size_t>(nodeIndex)] = 1;
                continue;
            }

            const std::size_t outputComponentCount = cgltf_num_components(output->type);
            if (outputComponentCount < outputComponents) {
                continue;
            }
            const std::size_t outputKeyCount = cubic ? static_cast<std::size_t>(input->count) * 3u : input->count;
            if (output->count < outputKeyCount) {
                continue;
            }
            std::vector<float> outputData;
            if (!unpackAccessor(output, outputData, outputComponents) ||
                outputData.size() < outputKeyCount * outputComponentCount) {
                continue;
            }

            AnimationChannel channel;
            channel.node = static_cast<uint32_t>(nodeIndex);
            channel.path = path;
            channel.interpolation = interpolation;
            channel.times.resize(input->count);
            channel.values.resize(input->count);
            if (cubic) {
                channel.inTangents.resize(input->count);
                channel.outTangents.resize(input->count);
            }
            const auto readValue = [&](std::size_t offset) {
                if (path == AnimationPath::Rotation) {
                    return Vec4(outputData[offset], outputData[offset + 1], outputData[offset + 2],
                                outputData[offset + 3]);
                }
                return Vec4(outputData[offset], outputData[offset + 1], outputData[offset + 2], 0.0f);
            };

            bool sorted = true;
            for (cgltf_size keyIndex = 0; keyIndex < input->count; ++keyIndex) {
                const float time = timeData[keyIndex];
                channel.times[keyIndex] = time;
                if (keyIndex > 0 && time < channel.times[keyIndex - 1]) {
                    sorted = false;
                    break;
                }
                const std::size_t keyOffset = static_cast<std::size_t>(keyIndex) * outputComponentCount;
                if (cubic) {
                    const std::size_t offset = keyOffset * 3u;
                    channel.inTangents[keyIndex] = readValue(offset);
                    channel.values[keyIndex] = readValue(offset + outputComponentCount);
                    channel.outTangents[keyIndex] = readValue(offset + outputComponentCount * 2u);
                } else {
                    channel.values[keyIndex] = readValue(keyOffset);
                }
            }
            if (!sorted || channel.times.empty()) {
                continue;
            }

            clip.duration = std::max(clip.duration, channel.times.back());
            context.animatedNodes[static_cast<std::size_t>(nodeIndex)] = 1;
            clip.channels.push_back(std::move(channel));
        }

        if (!clip.channels.empty()) {
            context.scene.animations.push_back(std::move(clip));
        }
    }
}

bool nodeOrAncestorAnimated(const Context& context, int nodeIndex) {
    int current = nodeIndex;
    std::size_t guard = 0;
    while (current >= 0 && static_cast<std::size_t>(current) < context.scene.nodes.size() &&
           guard++ < context.scene.nodes.size()) {
        if (context.animatedNodes[static_cast<std::size_t>(current)] != 0) {
            return true;
        }
        current = context.scene.nodes[static_cast<std::size_t>(current)].parent;
    }
    return false;
}

void addNode(Context& context, const cgltf_node* node, std::unordered_set<const cgltf_node*>& visited) {
    if (node == nullptr || !visited.insert(node).second) {
        return;
    }

    const Mat4 world = nodeWorld(node);
    const int nodeIndex = nodeIndexOf(context, node);
    if (node->mesh != nullptr) {
        const uint32_t skinIndex = node->skin != nullptr ? addSkin(context, node->skin) : UINT32_MAX;
        uint32_t meshIndex = addMesh(context, node->mesh);
        if (meshIndex < context.scene.meshes.size()) {
            const std::string objectName = nameOr(node->name, context.scene.meshes[meshIndex].name);
            const bool hasMorphTargets = !context.scene.meshes[meshIndex].morphTargets.empty();
            const bool hasSkinning =
                skinIndex != UINT32_MAX && !context.scene.meshes[meshIndex].skinVertices.empty();
            if (node->skin != nullptr && !hasSkinning) {
                HBG_LOGW("gltf: node '{}' references a skin but its mesh has no JOINTS_n/WEIGHTS_n", objectName);
            }
            if (hasMorphTargets || hasSkinning) {
                Mesh clonedMesh = context.scene.meshes[meshIndex];
                clonedMesh.name = objectName;
                meshIndex = static_cast<uint32_t>(context.scene.meshes.size());
                context.scene.meshes.push_back(std::move(clonedMesh));
            }

            render::RenderObject object;
            object.name = objectName;
            object.nodeIndex = nodeIndex;
            object.skinIndex = hasSkinning ? static_cast<int>(skinIndex) : -1;
            object.meshIndex = meshIndex;
            if (hasMorphTargets) {
                const asset::Mesh& morphMesh = context.scene.meshes[meshIndex];
                if (node->weights != nullptr && node->weights_count == morphMesh.morphTargets.size()) {
                    object.morphWeights.assign(node->weights, node->weights + node->weights_count);
                } else {
                    object.morphWeights = morphMesh.morphWeights;
                }
            }
            object.world = world;
            object.prevWorld = world;
            object.worldBounds = hasSkinning ? context.scene.meshes[meshIndex].bounds
                                             : transformBounds(world, context.scene.meshes[meshIndex].bounds);
            object.staticShadowCaster = !hasSkinning && !nodeOrAncestorAnimated(context, nodeIndex);
            context.scene.objects.push_back(std::move(object));
        }
    }

    if (node->camera != nullptr) {
        render::Camera camera;
        camera.name = nameOr(node->camera->name, nameOr(node->name, "Camera"));
        camera.nodeIndex = nodeIndex;
        camera.world = world;
        if (node->camera->type == cgltf_camera_type_perspective) {
            const cgltf_camera_perspective& perspective = node->camera->data.perspective;
            camera.projection = render::Camera::Projection::Perspective;
            camera.yfov = perspective.yfov;
            camera.aspect = perspective.has_aspect_ratio ? perspective.aspect_ratio : 1.7777778f;
            camera.nearClip = perspective.znear;
            camera.farClip = perspective.has_zfar ? perspective.zfar : 1000.0f;
        } else if (node->camera->type == cgltf_camera_type_orthographic) {
            const cgltf_camera_orthographic& orthographic = node->camera->data.orthographic;
            camera.projection = render::Camera::Projection::Orthographic;
            camera.xmag = orthographic.xmag;
            camera.ymag = orthographic.ymag;
            camera.nearClip = orthographic.znear;
            camera.farClip = orthographic.zfar;
        }
        context.scene.cameras.push_back(std::move(camera));
    }

    if (node->light != nullptr) {
        render::Light light;
        light.name = nameOr(node->light->name, nameOr(node->name, "Light"));
        light.nodeIndex = nodeIndex;
        light.world = world;
        light.color = toVec3(node->light->color);
        light.intensity = node->light->intensity;
        light.range = node->light->range;
        light.innerConeAngle = node->light->spot_inner_cone_angle;
        light.outerConeAngle = node->light->spot_outer_cone_angle;
        light.castShadows = true;
        switch (node->light->type) {
        case cgltf_light_type_directional:
            light.type = render::LightType::Directional;
            break;
        case cgltf_light_type_spot:
            light.type = render::LightType::Spot;
            break;
        case cgltf_light_type_point:
        default:
            light.type = render::LightType::Point;
            break;
        }
        context.scene.lights.push_back(std::move(light));
    }

    for (cgltf_size childIndex = 0; childIndex < node->children_count; ++childIndex) {
        addNode(context, node->children[childIndex], visited);
    }
}

} // namespace

GltfLoadResult GltfLoader::load(const std::filesystem::path& path) {
    GltfLoadResult result;
    if (path.empty()) {
        result.error = "gltf path is empty";
        return result;
    }

    cgltf_options options{};
    cgltf_data* data = nullptr;
    const std::string pathString = path.string();
    cgltf_result status = cgltf_parse_file(&options, pathString.c_str(), &data);
    if (status != cgltf_result_success) {
        result.error = "cgltf_parse_file failed for " + pathString;
        return result;
    }

    status = cgltf_load_buffers(&options, data, pathString.c_str());
    if (status != cgltf_result_success) {
        cgltf_free(data);
        result.error = "cgltf_load_buffers failed for " + pathString;
        return result;
    }

    status = cgltf_validate(data);
    if (status != cgltf_result_success) {
        cgltf_free(data);
        result.error = "cgltf_validate failed for " + pathString;
        return result;
    }

    Context context;
    context.data = data;
    context.baseDir = path.parent_path();
    context.scene.sourcePath = pathString;
    for (cgltf_size extensionIndex = 0; extensionIndex < data->extensions_required_count; ++extensionIndex) {
        if (data->extensions_required[extensionIndex] != nullptr &&
            std::strcmp(data->extensions_required[extensionIndex], "KHR_draco_mesh_compression") == 0) {
            context.dracoRequired = true;
            break;
        }
    }

    bool hasDraco = false;
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count && !hasDraco; ++meshIndex) {
        for (cgltf_size primitiveIndex = 0; primitiveIndex < data->meshes[meshIndex].primitives_count; ++primitiveIndex) {
            if (data->meshes[meshIndex].primitives[primitiveIndex].has_draco_mesh_compression) {
                hasDraco = true;
                break;
            }
        }
    }
    if (hasDraco) {
        nlohmann::json document;
        if (loadGltfJson(path, document)) {
            collectDracoAttributeIds(document, data, context.dracoAttributeIds);
        } else {
            const std::string message = "gltf: failed to read Draco attribute ids from " + pathString;
            if (context.dracoRequired) {
                cgltf_free(data);
                result.error = message;
                return result;
            }
            HBG_LOGW("{}", message);
        }
    }

    addSceneNodes(context);
    addAnimations(context);

    std::unordered_set<const cgltf_node*> visited;
    if (data->scene != nullptr) {
        for (cgltf_size nodeIndex = 0; nodeIndex < data->scene->nodes_count; ++nodeIndex) {
            addNode(context, data->scene->nodes[nodeIndex], visited);
        }
    } else {
        for (cgltf_size nodeIndex = 0; nodeIndex < data->nodes_count; ++nodeIndex) {
            addNode(context, &data->nodes[nodeIndex], visited);
        }
    }

    if (!context.fatalError.empty()) {
        cgltf_free(data);
        result.error = std::move(context.fatalError);
        return result;
    }

    context.scene.recomputeBounds();
    cgltf_free(data);

    result.scene = std::make_unique<render::RenderScene>(std::move(context.scene));
    return result;
}

} // namespace hbg::asset
