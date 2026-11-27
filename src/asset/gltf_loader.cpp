#include "asset/gltf_loader.h"

#include "asset/material.h"
#include "asset/mesh.h"
#include "asset/texture_loader.h"
#include "core/log.h"
#include "core/math.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
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
    uint32_t defaultMaterial = UINT32_MAX;
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

TextureRef makeTextureRef(Context& context, const cgltf_texture_view& view) {
    TextureRef ref;
    ref.uvSet = view.texcoord;
    if (view.texture != nullptr) {
        ref.imageIndex = addImage(context, view.texture->image);
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

bool unpackAccessor(const cgltf_accessor* accessor, std::vector<float>& data, std::size_t neededComponents) {
    if (accessor == nullptr || accessor->count == 0) {
        return false;
    }
    const std::size_t components = cgltf_num_components(accessor->type);
    if (components < neededComponents) {
        return false;
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

        std::vector<float> positionData;
        if (!unpackAccessor(positions, positionData, 3)) {
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

        const bool hasNormals = unpackAccessor(normals, normalData, 3);
        const bool hasTangents = unpackAccessor(tangents, tangentData, 4);
        const bool hasUv0 = unpackAccessor(uv0, uv0Data, 2);
        const bool hasUv1 = unpackAccessor(uv1, uv1Data, 2);
        const bool hasColors = unpackAccessor(colors, colorData, 3) || unpackAccessor(colors, colorData, 4);

        allNormals = allNormals && hasNormals;
        allTangents = allTangents && hasTangents;

        const std::size_t vertexOffset = mesh.vertices.size();
        const std::size_t vertexCount = positions->count;
        mesh.vertices.resize(vertexOffset + vertexCount);

        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
            MeshVertex& output = mesh.vertices[vertexOffset + vertex];
            const std::size_t positionIndex = vertex * 3;
            output.position =
                Vec3(positionData[positionIndex + 0], positionData[positionIndex + 1], positionData[positionIndex + 2]);

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

        mesh.hasNormals = mesh.hasNormals || hasNormals;
        mesh.hasTangents = mesh.hasTangents || hasTangents;
        mesh.hasUv0 = mesh.hasUv0 || hasUv0;
        mesh.hasUv1 = mesh.hasUv1 || hasUv1;
        mesh.hasColors = mesh.hasColors || hasColors;

        SubMesh subMesh;
        subMesh.indexOffset = static_cast<uint32_t>(mesh.indices.size());
        subMesh.materialIndex = addMaterial(context, primitive.material);
        Bounds primitiveBounds;
        for (std::size_t vertex = 0; vertex < vertexCount; ++vertex) {
            primitiveBounds.expand(mesh.vertices[vertexOffset + vertex].position);
        }
        subMesh.bounds = primitiveBounds;
        mesh.bounds.expand(primitiveBounds);

        if (primitive.indices != nullptr) {
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

void addNode(Context& context, const cgltf_node* node, std::unordered_set<const cgltf_node*>& visited) {
    if (node == nullptr || !visited.insert(node).second) {
        return;
    }

    const Mat4 world = nodeWorld(node);
    if (node->mesh != nullptr) {
        const uint32_t meshIndex = addMesh(context, node->mesh);
        if (meshIndex < context.scene.meshes.size()) {
            render::RenderObject object;
            object.name = nameOr(node->name, context.scene.meshes[meshIndex].name);
            object.visible = object.name.rfind("VegetationMesh_", 0) != 0;
            object.meshIndex = meshIndex;
            object.world = world;
            object.prevWorld = world;
            object.worldBounds = transformBounds(world, context.scene.meshes[meshIndex].bounds);
            context.scene.objects.push_back(std::move(object));
        }
    }

    if (node->camera != nullptr) {
        render::Camera camera;
        camera.name = nameOr(node->camera->name, nameOr(node->name, "Camera"));
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

    context.scene.recomputeBounds();
    cgltf_free(data);

    result.scene = std::make_unique<render::RenderScene>(std::move(context.scene));
    return result;
}

} // namespace hbg::asset
