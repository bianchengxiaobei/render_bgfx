#pragma once

#include "vfx/types.h"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vfx {

enum class PropertyType : std::uint8_t {
    Float,
    Int,
    Bool,
    Vector2,
    Vector3,
    Vector4,
    Color,
    Texture,
    Curve,
    Gradient,
};

enum class AttributeType : std::uint8_t {
    Float,
    Float2,
    Float3,
    Float4,
    UInt,
    Int,
    Bool,
};

enum class SystemSpace : std::uint8_t {
    World,
    Local,
};

enum class BoundsMode : std::uint8_t {
    Manual,
    Automatic,
};

inline constexpr std::uint32_t kCullSimulation = 1u << 0u;
inline constexpr std::uint32_t kCullBoundsUpdate = 1u << 1u;

enum class SortMode : std::uint8_t {
    None,
    Distance,
    Custom,
    CameraDepth,
    YoungestInFront,
};

enum class SortOrder : std::uint8_t {
    Ascending,
    Descending,
};

enum class OutputType : std::uint8_t {
    Quad,
};

struct CurveKey {
    float time = 0.0f;
    float value = 0.0f;
};

struct GradientKey {
    float time = 0.0f;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct Property {
    std::string name;
    PropertyType type = PropertyType::Float;
    std::array<float, 4> defaultFloat{};
    std::array<std::uint32_t, 4> defaultUint{};
    std::string defaultTexture;
    std::vector<CurveKey> curve;
    std::vector<GradientKey> gradient;
};

struct AttributeDesc {
    std::string name;
    AttributeType type = AttributeType::Float;
    std::array<float, 4> defaultFloat{};
    std::array<std::uint32_t, 4> defaultUint{};
};

struct Expression;

struct Value {
    enum class Kind : std::uint8_t {
        Invalid,
        Number,
        Bool,
        String,
        Array,
        Object,
        Expression,
    };

    Kind kind = Kind::Invalid;
    double number = 0.0;
    bool boolean = false;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value>> object;
    std::shared_ptr<const Expression> expression;

    bool isExpression() const noexcept { return kind == Kind::Expression; }
    bool isObject() const noexcept { return kind == Kind::Object; }
    const Value* find(std::string_view key) const noexcept;
};

struct Expression {
    std::string op;
    std::vector<std::pair<std::string, Value>> arguments;
};

struct Block {
    std::string type;
    bool enabled = true;
    Value parameters;

    const Value* parameter(std::string_view name) const noexcept;
};

struct Burst {
    float time = 0.0f;
    std::uint32_t count = 0;
    std::optional<float> period;
    std::optional<std::uint32_t> repeat;
};

struct SpawnerDesc {
    float rate = 0.0f;
    std::vector<Burst> bursts;
};

struct BoundsDesc {
    BoundsMode mode = BoundsMode::Manual;
    Vec3 center{};
    Vec3 extents{1.0f, 1.0f, 1.0f};
    Vec3 padding{};
};

struct SortDesc {
    SortMode mode = SortMode::None;
    SortOrder order = SortOrder::Ascending;
};

struct OutputDesc {
    OutputType type = OutputType::Quad;
    bool lit = false;
    BlendMode blend = BlendMode::Alpha;
    std::string texture;
    bool softParticles = false;
    bool castShadows = false;
    bool generateMotionVector = true;
    std::int32_t sortPriority = 0;
    float softDistance = 0.1f;
    float metallic = 0.0f;
    float roughness = 0.5f;
    std::string metallicProperty;
    std::string roughnessProperty;
    std::optional<SortDesc> sort;
    std::optional<SortOrder> sortOrder;
};

struct EventDesc {
    std::string name;
    Value attributes;
};

struct EventsDesc {
    std::vector<EventDesc> receive;
    std::vector<EventDesc> emit;
};

struct SystemDesc {
    std::string name;
    std::uint32_t requestedCapacity = 0;
    std::uint32_t capacity = 0;
    SystemSpace space = SystemSpace::World;
    BoundsDesc bounds;
    SortDesc sort;
    SpawnerDesc spawner;
    std::vector<Block> initBlocks;
    std::vector<Block> updateBlocks;
    EventsDesc events;
    std::vector<OutputDesc> outputs;
};

struct SubgraphDesc {
    std::string name;
    Value inputs;
    Value outputs;
    std::vector<Block> initBlocks;
    std::vector<Block> updateBlocks;
};

struct Graph {
    std::uint32_t version = 1;
    std::string name;
    std::uint32_t cullingFlags = 0;
    std::vector<Property> properties;
    std::vector<AttributeDesc> attributes;
    std::vector<SubgraphDesc> subgraphs;
    std::vector<SystemDesc> systems;
};

std::span<const AttributeDesc> builtInAttributes() noexcept;
const AttributeDesc* findBuiltInAttribute(std::string_view name) noexcept;
const char* toString(PropertyType type) noexcept;
const char* toString(AttributeType type) noexcept;
const char* toString(SystemSpace space) noexcept;
const char* toString(SortMode mode) noexcept;
const char* toString(SortOrder order) noexcept;
const char* toString(BlendMode blend) noexcept;
const char* toString(OutputType type) noexcept;
std::uint8_t attributeComponentCount(AttributeType type) noexcept;

} // namespace vfx
