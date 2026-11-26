#include "vfx/graph.h"

#include <algorithm>
#include <iterator>

namespace vfx {
namespace {

const std::array<AttributeDesc, 15> kBuiltInAttributes = {{
    {"position", AttributeType::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, {}},
    {"velocity", AttributeType::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, {}},
    {"color", AttributeType::Float3, {1.0f, 1.0f, 1.0f, 0.0f}, {}},
    {"alpha", AttributeType::Float, {1.0f, 0.0f, 0.0f, 0.0f}, {}},
    {"size", AttributeType::Float, {0.1f, 0.0f, 0.0f, 0.0f}, {}},
    {"age", AttributeType::Float, {0.0f, 0.0f, 0.0f, 0.0f}, {}},
    {"lifetime", AttributeType::Float, {1.0f, 0.0f, 0.0f, 0.0f}, {}},
    {"seed", AttributeType::UInt, {}, {0u, 0u, 0u, 0u}},
    {"particleId", AttributeType::UInt, {}, {0u, 0u, 0u, 0u}},
    {"spawnIndex", AttributeType::UInt, {}, {0u, 0u, 0u, 0u}},
    {"alive", AttributeType::Bool, {}, {1u, 0u, 0u, 0u}},
    {"angle", AttributeType::Float, {0.0f, 0.0f, 0.0f, 0.0f}, {}},
    {"scale", AttributeType::Float3, {1.0f, 1.0f, 1.0f, 0.0f}, {}},
    {"targetPosition", AttributeType::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, {}},
    {"oldPosition", AttributeType::Float3, {0.0f, 0.0f, 0.0f, 0.0f}, {}},
}};

} // namespace

const Value* Value::find(std::string_view key) const noexcept {
    if (kind != Kind::Object) {
        return nullptr;
    }
    for (const auto& [name, value] : object) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

const Value* Block::parameter(std::string_view name) const noexcept {
    return parameters.find(name);
}

std::span<const AttributeDesc> builtInAttributes() noexcept {
    return std::span<const AttributeDesc>(kBuiltInAttributes.data(), kBuiltInAttributes.size());
}

const AttributeDesc* findBuiltInAttribute(std::string_view name) noexcept {
    const auto found = std::find_if(kBuiltInAttributes.begin(), kBuiltInAttributes.end(),
                                    [name](const AttributeDesc& attribute) {
                                        return attribute.name == name;
                                    });
    return found == kBuiltInAttributes.end() ? nullptr : &*found;
}

const char* toString(PropertyType type) noexcept {
    switch (type) {
    case PropertyType::Float:
        return "float";
    case PropertyType::Int:
        return "int";
    case PropertyType::Bool:
        return "bool";
    case PropertyType::Vector2:
        return "vector2";
    case PropertyType::Vector3:
        return "vector3";
    case PropertyType::Vector4:
        return "vector4";
    case PropertyType::Color:
        return "color";
    case PropertyType::Texture:
        return "texture";
    case PropertyType::Curve:
        return "curve";
    case PropertyType::Gradient:
        return "gradient";
    }
    return "unknown";
}

const char* toString(AttributeType type) noexcept {
    switch (type) {
    case AttributeType::Float:
        return "float";
    case AttributeType::Float2:
        return "float2";
    case AttributeType::Float3:
        return "float3";
    case AttributeType::Float4:
        return "float4";
    case AttributeType::UInt:
        return "uint";
    case AttributeType::Int:
        return "int";
    case AttributeType::Bool:
        return "bool";
    }
    return "unknown";
}

const char* toString(SystemSpace space) noexcept {
    switch (space) {
    case SystemSpace::World:
        return "world";
    case SystemSpace::Local:
        return "local";
    }
    return "unknown";
}

const char* toString(SortMode mode) noexcept {
    switch (mode) {
    case SortMode::None:
        return "none";
    case SortMode::Distance:
        return "distance";
    case SortMode::Custom:
        return "custom";
    case SortMode::CameraDepth:
        return "cameraDepth";
    case SortMode::YoungestInFront:
        return "youngest";
    }
    return "unknown";
}

const char* toString(SortOrder order) noexcept {
    switch (order) {
    case SortOrder::Ascending:
        return "ascending";
    case SortOrder::Descending:
        return "descending";
    }
    return "unknown";
}

const char* toString(BlendMode blend) noexcept {
    switch (blend) {
    case BlendMode::Alpha:
        return "alpha";
    case BlendMode::Additive:
        return "additive";
    case BlendMode::Premultiply:
        return "premultiply";
    case BlendMode::Opaque:
        return "opaque";
    }
    return "unknown";
}

const char* toString(OutputType type) noexcept {
    switch (type) {
    case OutputType::Quad:
        return "quad";
    }
    return "unknown";
}

std::uint8_t attributeComponentCount(AttributeType type) noexcept {
    switch (type) {
    case AttributeType::Float:
    case AttributeType::UInt:
    case AttributeType::Int:
    case AttributeType::Bool:
        return 1;
    case AttributeType::Float2:
        return 2;
    case AttributeType::Float3:
        return 3;
    case AttributeType::Float4:
        return 4;
    }
    return 0;
}

} // namespace vfx
