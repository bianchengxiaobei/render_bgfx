#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace sg {

using NodeId = std::int32_t;
using SlotId = std::int32_t;

inline constexpr NodeId kInvalidNode = -1;
inline constexpr SlotId kInvalidSlot = -1;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    bool operator==(const Vec2& other) const = default;
};

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    bool operator==(const Vec3& other) const = default;
};

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    bool operator==(const Vec4& other) const = default;
};

using Color = Vec4;

struct Mat2 {
    std::array<float, 4> m{};

    static Mat2 identity();
    bool operator==(const Mat2& other) const = default;
};

struct Mat3 {
    std::array<float, 9> m{};

    static Mat3 identity();
    bool operator==(const Mat3& other) const = default;
};

struct Mat4 {
    std::array<float, 16> m{};

    static Mat4 identity();
    bool operator==(const Mat4& other) const = default;
};

enum class ValueType : std::uint8_t {
    SamplerState,
    DynamicMatrix,
    Matrix4,
    Matrix3,
    Matrix2,
    Texture2D,
    Texture2DArray,
    Texture3D,
    Cubemap,
    Gradient,
    DynamicVector,
    Vector4,
    Vector3,
    Vector2,
    Float,
    Dynamic,
    Boolean,
    Integer,
    VirtualTexture,
    PropertyConnectionState,
    External,
    Color,
    String,
};

enum class ConcreteValueType : std::uint8_t {
    SamplerState,
    Matrix4,
    Matrix3,
    Matrix2,
    Texture2D,
    Texture2DArray,
    Texture3D,
    Cubemap,
    Gradient,
    Vector4,
    Vector3,
    Vector2,
    Float,
    Boolean,
    Integer,
    VirtualTexture,
    PropertyConnectionState,
    External,
};

enum class Precision : std::uint8_t {
    Inherit,
    Single,
    Half,
    Graph,
};

enum class GraphPrecision : std::uint8_t {
    Single = 0,
    Graph = 1,
    Half = 2,
};

enum class ConcretePrecision : std::uint8_t {
    Single,
    Half,
};

enum class SlotDirection : std::uint8_t {
    Input,
    Output,
};

enum class SlotMode : std::uint8_t {
    Default,
    Literal,
    Connected,
};

enum class GraphKind : std::uint8_t {
    Graph,
    SubGraph,
};

enum class Severity : std::uint8_t {
    Info,
    Warning,
    Error,
};

enum class KeywordType : std::uint8_t {
    Boolean,
    Enum,
};

enum class KeywordDefinition : std::uint8_t {
    ShaderFeature,
    MultiCompile,
    Predefined,
    DynamicBranch,
};

enum class KeywordScope : std::uint8_t {
    Local,
    Global,
};

struct GradientKey {
    float time = 0.0f;
    Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};

    bool operator==(const GradientKey& other) const = default;
};

struct Gradient {
    std::vector<GradientKey> keys;

    bool operator==(const Gradient& other) const = default;
};

struct Value {
    using Payload = std::variant<std::monostate,
                                 bool,
                                 std::int64_t,
                                 float,
                                 Vec2,
                                 Vec3,
                                 Vec4,
                                 Mat2,
                                 Mat3,
                                 Mat4,
                                 std::string,
                                 Gradient>;

    ValueType type = ValueType::Float;
    Payload data = 0.0f;

    Value();
    Value(ValueType valueType, Payload payload);

    static Value makeFloat(float value);
    static Value makeBool(bool value);
    static Value makeInteger(std::int64_t value);
    static Value makeVector2(Vec2 value);
    static Value makeVector3(Vec3 value);
    static Value makeVector4(Vec4 value);
    static Value makeColor(Vec4 value);
    static Value makeMatrix2(Mat2 value);
    static Value makeMatrix3(Mat3 value);
    static Value makeMatrix4(Mat4 value);
    static Value makeString(ValueType valueType, std::string value);
    static Value makeGradient(Gradient value);

    bool isNumeric() const;
    bool isVector() const;
    bool isMatrix() const;
    bool isTexture() const;

    const float* asFloat() const;
    const bool* asBool() const;
    const std::int64_t* asInteger() const;
    const Vec2* asVector2() const;
    const Vec3* asVector3() const;
    const Vec4* asVector4() const;
    const Mat2* asMatrix2() const;
    const Mat3* asMatrix3() const;
    const Mat4* asMatrix4() const;
    const std::string* asString() const;
    const Gradient* asGradient() const;

    bool operator==(const Value& other) const;
};

bool approxEqual(const Value& lhs, const Value& rhs, float epsilon = 1e-6f);
bool approxEqual(const Vec2& lhs, const Vec2& rhs, float epsilon = 1e-6f);
bool approxEqual(const Vec3& lhs, const Vec3& rhs, float epsilon = 1e-6f);
bool approxEqual(const Vec4& lhs, const Vec4& rhs, float epsilon = 1e-6f);
bool approxEqual(const Mat2& lhs, const Mat2& rhs, float epsilon = 1e-6f);
bool approxEqual(const Mat3& lhs, const Mat3& rhs, float epsilon = 1e-6f);
bool approxEqual(const Mat4& lhs, const Mat4& rhs, float epsilon = 1e-6f);

struct Diagnostic {
    Severity severity = Severity::Error;
    std::string code;
    std::string message;
    NodeId node = kInvalidNode;
    SlotId slot = kInvalidSlot;
    std::string pass;
    std::string variant;

    bool operator==(const Diagnostic& other) const = default;
};

inline constexpr const char* kErrorUnknownNode = "SG_UNKNOWN_NODE";
inline constexpr const char* kErrorDuplicateId = "SG_DUPLICATE_ID";
inline constexpr const char* kErrorMissingInput = "SG_MISSING_INPUT";
inline constexpr const char* kErrorTypeMismatch = "SG_TYPE_MISMATCH";
inline constexpr const char* kErrorCycle = "SG_CYCLE";
inline constexpr const char* kErrorNameConflict = "SG_NAME_CONFLICT";
inline constexpr const char* kErrorTargetUnsupported = "SG_TARGET_UNSUPPORTED";
inline constexpr const char* kErrorKeywordLimit = "SG_KEYWORD_LIMIT";
inline constexpr const char* kErrorSubGraphDepth = "SG_SUBGRAPH_DEPTH";
inline constexpr const char* kErrorTypeUnresolved = "SG_TYPE_UNRESOLVED";
inline constexpr const char* kErrorInvalidEdge = "SG_INVALID_EDGE";
inline constexpr const char* kErrorUnknownField = "SG_UNKNOWN_FIELD";
inline constexpr const char* kErrorSchemaVersion = "SG_SCHEMA_VERSION";
inline constexpr const char* kErrorInvalidJson = "SG_INVALID_JSON";
inline constexpr const char* kErrorInvalidValue = "SG_INVALID_VALUE";
inline constexpr const char* kErrorDuplicateType = "SG_DUPLICATE_TYPE";
inline constexpr const char* kErrorBudgetExceeded = "SG_BUDGET_EXCEEDED";
inline constexpr const char* kErrorVariantLimit = "SG_VARIANT_LIMIT";
inline constexpr const char* kErrorTemplate = "SG_TEMPLATE_ERROR";
inline constexpr const char* kErrorShaderCompile = "SG_SHADER_COMPILE";
inline constexpr const char* kErrorIo = "SG_IO";

bool isDynamicType(ValueType type) noexcept;
bool isVectorType(ValueType type) noexcept;
bool isMatrixType(ValueType type) noexcept;
bool isTextureType(ValueType type) noexcept;
int channelCount(ValueType type) noexcept;
int channelCount(ConcreteValueType type) noexcept;
int matrixDimension(ValueType type) noexcept;
int matrixDimension(ConcreteValueType type) noexcept;
std::optional<ConcreteValueType> concreteTypeOf(ValueType type) noexcept;
ValueType valueTypeOf(ConcreteValueType type) noexcept;
bool isCompatible(ValueType inputType, ConcreteValueType outputType, bool outputTypeIsConnectionTestable = false) noexcept;
bool isIdentifier(std::string_view name) noexcept;
std::optional<std::string> virtualTextureLayerTextureTypeFromString(std::string_view text) noexcept;
std::optional<std::string> textureDefaultTypeFromString(std::string_view text) noexcept;
std::uint8_t textureDefaultTypeCode(std::string_view text) noexcept;
std::string normalizeSamplerAnisotropy(std::string_view text);
std::uint8_t samplerAnisotropyLevel(std::string_view text);

GraphPrecision toGraphPrecision(Precision precision, GraphPrecision inheritPrecision) noexcept;
ConcretePrecision toConcretePrecision(Precision precision, ConcretePrecision inheritPrecision,
                                      ConcretePrecision graphPrecision) noexcept;
ConcretePrecision toConcretePrecision(GraphPrecision precision, ConcretePrecision graphPrecision) noexcept;
GraphPrecision graphFallback(GraphPrecision precision, GraphPrecision graphPrecision) noexcept;

const char* toString(ValueType type) noexcept;
const char* toString(ConcreteValueType type) noexcept;
const char* toString(Precision precision) noexcept;
const char* toString(GraphPrecision precision) noexcept;
const char* toString(ConcretePrecision precision) noexcept;
const char* toString(SlotDirection direction) noexcept;
const char* toString(SlotMode mode) noexcept;
const char* toString(GraphKind kind) noexcept;
const char* toString(Severity severity) noexcept;
const char* toString(KeywordType type) noexcept;
const char* toString(KeywordDefinition definition) noexcept;
const char* toString(KeywordScope scope) noexcept;

std::optional<ValueType> valueTypeFromString(std::string_view text) noexcept;
std::optional<ConcreteValueType> concreteTypeFromString(std::string_view text) noexcept;
std::optional<Precision> precisionFromString(std::string_view text) noexcept;
std::optional<GraphPrecision> graphPrecisionFromString(std::string_view text) noexcept;
std::optional<ConcretePrecision> concretePrecisionFromString(std::string_view text) noexcept;
std::optional<SlotDirection> slotDirectionFromString(std::string_view text) noexcept;
std::optional<SlotMode> slotModeFromString(std::string_view text) noexcept;
std::optional<GraphKind> graphKindFromString(std::string_view text) noexcept;
std::optional<KeywordType> keywordTypeFromString(std::string_view text) noexcept;
std::optional<KeywordDefinition> keywordDefinitionFromString(std::string_view text) noexcept;
std::optional<KeywordScope> keywordScopeFromString(std::string_view text) noexcept;

} // namespace sg
