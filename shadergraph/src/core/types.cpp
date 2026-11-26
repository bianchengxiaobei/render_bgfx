#include "sg/types.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace sg {
namespace {

constexpr float kEpsilon = 1e-6f;

bool nearly(float lhs, float rhs, float epsilon) {
    return std::fabs(lhs - rhs) <= epsilon;
}

template <typename T>
bool equalArray(const T& lhs, const T& rhs, float epsilon) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (!nearly(lhs[i], rhs[i], epsilon)) {
            return false;
        }
    }
    return true;
}

bool isScalarVector(ConcreteValueType type) noexcept {
    return type == ConcreteValueType::Float || type == ConcreteValueType::Integer ||
           type == ConcreteValueType::Boolean || type == ConcreteValueType::Vector2 ||
           type == ConcreteValueType::Vector3 || type == ConcreteValueType::Vector4;
}

bool isMatrix(ConcreteValueType type) noexcept {
    return type == ConcreteValueType::Matrix2 || type == ConcreteValueType::Matrix3 ||
           type == ConcreteValueType::Matrix4;
}

} // namespace

Mat2 Mat2::identity() {
    Mat2 result;
    result.m[0] = 1.0f;
    result.m[3] = 1.0f;
    return result;
}

Mat3 Mat3::identity() {
    Mat3 result;
    result.m[0] = 1.0f;
    result.m[4] = 1.0f;
    result.m[8] = 1.0f;
    return result;
}

Mat4 Mat4::identity() {
    Mat4 result;
    result.m[0] = 1.0f;
    result.m[5] = 1.0f;
    result.m[10] = 1.0f;
    result.m[15] = 1.0f;
    return result;
}

Value::Value() = default;

Value::Value(ValueType valueType, Payload payload) : type(valueType), data(std::move(payload)) {}

Value Value::makeFloat(float value) {
    return Value(ValueType::Float, value);
}

Value Value::makeBool(bool value) {
    return Value(ValueType::Boolean, value);
}

Value Value::makeInteger(std::int64_t value) {
    return Value(ValueType::Integer, value);
}

Value Value::makeVector2(Vec2 value) {
    return Value(ValueType::Vector2, value);
}

Value Value::makeVector3(Vec3 value) {
    return Value(ValueType::Vector3, value);
}

Value Value::makeVector4(Vec4 value) {
    return Value(ValueType::Vector4, value);
}

Value Value::makeColor(Vec4 value) {
    return Value(ValueType::Color, value);
}

Value Value::makeMatrix2(Mat2 value) {
    return Value(ValueType::Matrix2, value);
}

Value Value::makeMatrix3(Mat3 value) {
    return Value(ValueType::Matrix3, value);
}

Value Value::makeMatrix4(Mat4 value) {
    return Value(ValueType::Matrix4, value);
}

Value Value::makeString(ValueType valueType, std::string value) {
    return Value(valueType, std::move(value));
}

Value Value::makeGradient(Gradient value) {
    return Value(ValueType::Gradient, std::move(value));
}

bool Value::isNumeric() const {
    return isScalarVector(concreteTypeOf(type).value_or(ConcreteValueType::External)) ||
           isMatrixType(type);
}

bool Value::isVector() const {
    return type == ValueType::Float || type == ValueType::Vector2 || type == ValueType::Vector3 ||
           type == ValueType::Vector4 || type == ValueType::Color;
}

bool Value::isMatrix() const {
    return isMatrixType(type);
}

bool Value::isTexture() const {
    return isTextureType(type);
}

const float* Value::asFloat() const {
    return std::get_if<float>(&data);
}

const bool* Value::asBool() const {
    return std::get_if<bool>(&data);
}

const std::int64_t* Value::asInteger() const {
    return std::get_if<std::int64_t>(&data);
}

const Vec2* Value::asVector2() const {
    return std::get_if<Vec2>(&data);
}

const Vec3* Value::asVector3() const {
    return std::get_if<Vec3>(&data);
}

const Vec4* Value::asVector4() const {
    return std::get_if<Vec4>(&data);
}

const Mat2* Value::asMatrix2() const {
    return std::get_if<Mat2>(&data);
}

const Mat3* Value::asMatrix3() const {
    return std::get_if<Mat3>(&data);
}

const Mat4* Value::asMatrix4() const {
    return std::get_if<Mat4>(&data);
}

const std::string* Value::asString() const {
    return std::get_if<std::string>(&data);
}

const Gradient* Value::asGradient() const {
    return std::get_if<Gradient>(&data);
}

bool Value::operator==(const Value& other) const {
    return type == other.type && data == other.data;
}

bool approxEqual(const Vec2& lhs, const Vec2& rhs, float epsilon) {
    return nearly(lhs.x, rhs.x, epsilon) && nearly(lhs.y, rhs.y, epsilon);
}

bool approxEqual(const Vec3& lhs, const Vec3& rhs, float epsilon) {
    return nearly(lhs.x, rhs.x, epsilon) && nearly(lhs.y, rhs.y, epsilon) &&
           nearly(lhs.z, rhs.z, epsilon);
}

bool approxEqual(const Vec4& lhs, const Vec4& rhs, float epsilon) {
    return nearly(lhs.x, rhs.x, epsilon) && nearly(lhs.y, rhs.y, epsilon) &&
           nearly(lhs.z, rhs.z, epsilon) && nearly(lhs.w, rhs.w, epsilon);
}

bool approxEqual(const Mat2& lhs, const Mat2& rhs, float epsilon) {
    return equalArray(lhs.m, rhs.m, epsilon);
}

bool approxEqual(const Mat3& lhs, const Mat3& rhs, float epsilon) {
    return equalArray(lhs.m, rhs.m, epsilon);
}

bool approxEqual(const Mat4& lhs, const Mat4& rhs, float epsilon) {
    return equalArray(lhs.m, rhs.m, epsilon);
}

bool approxEqual(const Value& lhs, const Value& rhs, float epsilon) {
    if (lhs.type != rhs.type) {
        return false;
    }
    if (const auto* value = std::get_if<float>(&lhs.data)) {
        const auto* other = std::get_if<float>(&rhs.data);
        return other != nullptr && nearly(*value, *other, epsilon);
    }
    if (const auto* value = std::get_if<Vec2>(&lhs.data)) {
        const auto* other = std::get_if<Vec2>(&rhs.data);
        return other != nullptr && approxEqual(*value, *other, epsilon);
    }
    if (const auto* value = std::get_if<Vec3>(&lhs.data)) {
        const auto* other = std::get_if<Vec3>(&rhs.data);
        return other != nullptr && approxEqual(*value, *other, epsilon);
    }
    if (const auto* value = std::get_if<Vec4>(&lhs.data)) {
        const auto* other = std::get_if<Vec4>(&rhs.data);
        return other != nullptr && approxEqual(*value, *other, epsilon);
    }
    if (const auto* value = std::get_if<Mat2>(&lhs.data)) {
        const auto* other = std::get_if<Mat2>(&rhs.data);
        return other != nullptr && approxEqual(*value, *other, epsilon);
    }
    if (const auto* value = std::get_if<Mat3>(&lhs.data)) {
        const auto* other = std::get_if<Mat3>(&rhs.data);
        return other != nullptr && approxEqual(*value, *other, epsilon);
    }
    if (const auto* value = std::get_if<Mat4>(&lhs.data)) {
        const auto* other = std::get_if<Mat4>(&rhs.data);
        return other != nullptr && approxEqual(*value, *other, epsilon);
    }
    if (const auto* value = std::get_if<Gradient>(&lhs.data)) {
        const auto* other = std::get_if<Gradient>(&rhs.data);
        if (other == nullptr || value->keys.size() != other->keys.size()) {
            return false;
        }
        for (std::size_t i = 0; i < value->keys.size(); ++i) {
            if (!nearly(value->keys[i].time, other->keys[i].time, epsilon) ||
                !approxEqual(value->keys[i].color, other->keys[i].color, epsilon)) {
                return false;
            }
        }
        return true;
    }
    return lhs.data == rhs.data;
}

bool isDynamicType(ValueType type) noexcept {
    return type == ValueType::Dynamic || type == ValueType::DynamicVector ||
           type == ValueType::DynamicMatrix;
}

bool isVectorType(ValueType type) noexcept {
    return type == ValueType::Float || type == ValueType::Vector2 || type == ValueType::Vector3 ||
           type == ValueType::Vector4 || type == ValueType::Color;
}

bool isMatrixType(ValueType type) noexcept {
    return type == ValueType::Matrix2 || type == ValueType::Matrix3 || type == ValueType::Matrix4;
}

bool isTextureType(ValueType type) noexcept {
    return type == ValueType::Texture2D || type == ValueType::Texture2DArray ||
           type == ValueType::Texture3D || type == ValueType::Cubemap ||
           type == ValueType::VirtualTexture;
}

int channelCount(ValueType type) noexcept {
    switch (type) {
    case ValueType::Float:
    case ValueType::Boolean:
    case ValueType::Integer:
        return 1;
    case ValueType::Vector2:
        return 2;
    case ValueType::Vector3:
        return 3;
    case ValueType::Vector4:
    case ValueType::Color:
        return 4;
    default:
        return 0;
    }
}

int channelCount(ConcreteValueType type) noexcept {
    switch (type) {
    case ConcreteValueType::Float:
    case ConcreteValueType::Boolean:
    case ConcreteValueType::Integer:
        return 1;
    case ConcreteValueType::Vector2:
        return 2;
    case ConcreteValueType::Vector3:
        return 3;
    case ConcreteValueType::Vector4:
        return 4;
    default:
        return 0;
    }
}

int matrixDimension(ValueType type) noexcept {
    switch (type) {
    case ValueType::Matrix2:
        return 2;
    case ValueType::Matrix3:
        return 3;
    case ValueType::Matrix4:
        return 4;
    default:
        return 0;
    }
}

int matrixDimension(ConcreteValueType type) noexcept {
    switch (type) {
    case ConcreteValueType::Matrix2:
        return 2;
    case ConcreteValueType::Matrix3:
        return 3;
    case ConcreteValueType::Matrix4:
        return 4;
    default:
        return 0;
    }
}

std::optional<ConcreteValueType> concreteTypeOf(ValueType type) noexcept {
    switch (type) {
    case ValueType::SamplerState:
        return ConcreteValueType::SamplerState;
    case ValueType::Matrix2:
        return ConcreteValueType::Matrix2;
    case ValueType::Matrix3:
        return ConcreteValueType::Matrix3;
    case ValueType::Matrix4:
        return ConcreteValueType::Matrix4;
    case ValueType::Texture2D:
        return ConcreteValueType::Texture2D;
    case ValueType::Texture2DArray:
        return ConcreteValueType::Texture2DArray;
    case ValueType::Texture3D:
        return ConcreteValueType::Texture3D;
    case ValueType::Cubemap:
        return ConcreteValueType::Cubemap;
    case ValueType::Gradient:
        return ConcreteValueType::Gradient;
    case ValueType::Vector4:
        return ConcreteValueType::Vector4;
    case ValueType::Vector3:
        return ConcreteValueType::Vector3;
    case ValueType::Vector2:
        return ConcreteValueType::Vector2;
    case ValueType::Float:
        return ConcreteValueType::Float;
    case ValueType::Boolean:
        return ConcreteValueType::Boolean;
    case ValueType::Integer:
        return ConcreteValueType::Integer;
    case ValueType::VirtualTexture:
        return ConcreteValueType::VirtualTexture;
    case ValueType::PropertyConnectionState:
        return ConcreteValueType::PropertyConnectionState;
    case ValueType::External:
        return ConcreteValueType::External;
    case ValueType::Color:
        return ConcreteValueType::Vector4;
    default:
        return std::nullopt;
    }
}

ValueType valueTypeOf(ConcreteValueType type) noexcept {
    switch (type) {
    case ConcreteValueType::SamplerState:
        return ValueType::SamplerState;
    case ConcreteValueType::Matrix4:
        return ValueType::Matrix4;
    case ConcreteValueType::Matrix3:
        return ValueType::Matrix3;
    case ConcreteValueType::Matrix2:
        return ValueType::Matrix2;
    case ConcreteValueType::Texture2D:
        return ValueType::Texture2D;
    case ConcreteValueType::Texture2DArray:
        return ValueType::Texture2DArray;
    case ConcreteValueType::Texture3D:
        return ValueType::Texture3D;
    case ConcreteValueType::Cubemap:
        return ValueType::Cubemap;
    case ConcreteValueType::Gradient:
        return ValueType::Gradient;
    case ConcreteValueType::Vector4:
        return ValueType::Vector4;
    case ConcreteValueType::Vector3:
        return ValueType::Vector3;
    case ConcreteValueType::Vector2:
        return ValueType::Vector2;
    case ConcreteValueType::Float:
        return ValueType::Float;
    case ConcreteValueType::Boolean:
        return ValueType::Boolean;
    case ConcreteValueType::Integer:
        return ValueType::Integer;
    case ConcreteValueType::VirtualTexture:
        return ValueType::VirtualTexture;
    case ConcreteValueType::PropertyConnectionState:
        return ValueType::PropertyConnectionState;
    case ConcreteValueType::External:
        return ValueType::External;
    }
    return ValueType::External;
}

bool isCompatible(ValueType inputType, ConcreteValueType outputType, bool outputTypeIsConnectionTestable) noexcept {
    if (inputType == ValueType::PropertyConnectionState) {
        return outputTypeIsConnectionTestable;
    }
    if (inputType == ValueType::Dynamic || inputType == ValueType::DynamicVector) {
        if (inputType == ValueType::DynamicVector) {
            return isScalarVector(outputType);
        }
        return isScalarVector(outputType) || isMatrix(outputType);
    }
    if (inputType == ValueType::DynamicMatrix) {
        return isMatrix(outputType);
    }
    if (inputType == ValueType::Float || inputType == ValueType::Integer ||
        inputType == ValueType::Vector2 || inputType == ValueType::Vector3 ||
        inputType == ValueType::Vector4 || inputType == ValueType::Color) {
        return isScalarVector(outputType);
    }
    if (inputType == ValueType::Boolean) {
        return outputType == ConcreteValueType::Boolean ||
               outputType == ConcreteValueType::Float ||
               outputType == ConcreteValueType::Integer ||
               outputType == ConcreteValueType::Vector2 ||
               outputType == ConcreteValueType::Vector3 ||
               outputType == ConcreteValueType::Vector4;
    }
    if (isMatrixType(inputType)) {
        return isMatrix(outputType);
    }
    return concreteTypeOf(inputType).value_or(ConcreteValueType::External) == outputType;
}

std::optional<std::string> virtualTextureLayerTextureTypeFromString(std::string_view text) noexcept {
    std::string normalized;
    normalized.reserve(text.size());
    for (char character : text) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            normalized.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
    }
    if (normalized == "default") {
        return std::string("default");
    }
    if (normalized == "normaltangentspace" || normalized == "normalts") {
        return std::string("normalTangentSpace");
    }
    if (normalized == "normalobjectspace" || normalized == "normalos") {
        return std::string("normalObjectSpace");
    }
    return std::nullopt;
}

std::optional<std::string> textureDefaultTypeFromString(std::string_view text) noexcept {
    std::string normalized;
    normalized.reserve(text.size());
    for (char character : text) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            normalized.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
    }
    if (normalized == "white") {
        return std::string("white");
    }
    if (normalized == "black") {
        return std::string("black");
    }
    if (normalized == "grey" || normalized == "gray") {
        return std::string("grey");
    }
    if (normalized == "bump" || normalized == "normal" || normalized == "normalmap") {
        return std::string("bump");
    }
    if (normalized == "lineargrey" || normalized == "lineargray") {
        return std::string("linearGrey");
    }
    if (normalized == "red") {
        return std::string("red");
    }
    return std::nullopt;
}

std::uint8_t textureDefaultTypeCode(std::string_view text) noexcept {
    const auto normalized = textureDefaultTypeFromString(text);
    if (!normalized) {
        return 0;
    }
    if (*normalized == "black") {
        return 1;
    }
    if (*normalized == "grey") {
        return 2;
    }
    if (*normalized == "bump") {
        return 3;
    }
    if (*normalized == "linearGrey") {
        return 4;
    }
    if (*normalized == "red") {
        return 5;
    }
    return 0;
}

std::string normalizeSamplerAnisotropy(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());
    for (char character : text) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0) {
            normalized.push_back(
                static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
        }
    }
    if (normalized.empty() || normalized == "none" || normalized == "0") {
        return normalized.empty() ? std::string() : std::string("none");
    }
    if (normalized == "x2" || normalized == "2" || normalized == "2x") {
        return std::string("x2");
    }
    if (normalized == "x4" || normalized == "4" || normalized == "4x") {
        return std::string("x4");
    }
    if (normalized == "x8" || normalized == "8" || normalized == "8x") {
        return std::string("x8");
    }
    if (normalized == "x16" || normalized == "16" || normalized == "16x") {
        return std::string("x16");
    }
    return std::string();
}

std::uint8_t samplerAnisotropyLevel(std::string_view text) {
    const std::string normalized = normalizeSamplerAnisotropy(text);
    if (normalized == "x2") {
        return 2;
    }
    if (normalized == "x4") {
        return 4;
    }
    if (normalized == "x8") {
        return 8;
    }
    if (normalized == "x16") {
        return 16;
    }
    return 0;
}

bool isIdentifier(std::string_view name) noexcept {
    if (name.empty()) {
        return false;
    }
    const auto isAlpha = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
    };
    const auto isDigit = [](char c) { return c >= '0' && c <= '9'; };
    if (!isAlpha(name.front())) {
        return false;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        if (!isAlpha(name[i]) && !isDigit(name[i])) {
            return false;
        }
    }
    return true;
}

GraphPrecision toGraphPrecision(Precision precision, GraphPrecision inheritPrecision) noexcept {
    switch (precision) {
    case Precision::Single:
        return GraphPrecision::Single;
    case Precision::Half:
        return GraphPrecision::Half;
    case Precision::Graph:
        return GraphPrecision::Graph;
    case Precision::Inherit:
        return inheritPrecision;
    }
    return inheritPrecision;
}

ConcretePrecision toConcretePrecision(Precision precision, ConcretePrecision inheritPrecision,
                                      ConcretePrecision graphPrecision) noexcept {
    switch (precision) {
    case Precision::Single:
        return ConcretePrecision::Single;
    case Precision::Half:
        return ConcretePrecision::Half;
    case Precision::Inherit:
        return inheritPrecision;
    case Precision::Graph:
        return graphPrecision;
    }
    return inheritPrecision;
}

ConcretePrecision toConcretePrecision(GraphPrecision precision, ConcretePrecision graphPrecision) noexcept {
    switch (precision) {
    case GraphPrecision::Single:
        return ConcretePrecision::Single;
    case GraphPrecision::Half:
        return ConcretePrecision::Half;
    case GraphPrecision::Graph:
        return graphPrecision;
    }
    return graphPrecision;
}

GraphPrecision graphFallback(GraphPrecision precision, GraphPrecision graphPrecision) noexcept {
    return precision == GraphPrecision::Graph ? graphPrecision : precision;
}

const char* toString(ValueType type) noexcept {
    switch (type) {
    case ValueType::SamplerState:
        return "samplerstate";
    case ValueType::DynamicMatrix:
        return "dynamicmatrix";
    case ValueType::Matrix4:
        return "matrix4";
    case ValueType::Matrix3:
        return "matrix3";
    case ValueType::Matrix2:
        return "matrix2";
    case ValueType::Texture2D:
        return "texture2d";
    case ValueType::Texture2DArray:
        return "texture2darray";
    case ValueType::Texture3D:
        return "texture3d";
    case ValueType::Cubemap:
        return "cubemap";
    case ValueType::Gradient:
        return "gradient";
    case ValueType::DynamicVector:
        return "dynamicvector";
    case ValueType::Vector4:
        return "vector4";
    case ValueType::Vector3:
        return "vector3";
    case ValueType::Vector2:
        return "vector2";
    case ValueType::Float:
        return "float";
    case ValueType::Dynamic:
        return "dynamic";
    case ValueType::Boolean:
        return "boolean";
    case ValueType::Integer:
        return "integer";
    case ValueType::VirtualTexture:
        return "virtualtexture";
    case ValueType::PropertyConnectionState:
        return "propertyconnectionstate";
    case ValueType::External:
        return "external";
    case ValueType::Color:
        return "color";
    case ValueType::String:
        return "string";
    }
    return "unknown";
}

const char* toString(ConcreteValueType type) noexcept {
    switch (type) {
    case ConcreteValueType::SamplerState:
        return "samplerstate";
    case ConcreteValueType::Matrix4:
        return "matrix4";
    case ConcreteValueType::Matrix3:
        return "matrix3";
    case ConcreteValueType::Matrix2:
        return "matrix2";
    case ConcreteValueType::Texture2D:
        return "texture2d";
    case ConcreteValueType::Texture2DArray:
        return "texture2darray";
    case ConcreteValueType::Texture3D:
        return "texture3d";
    case ConcreteValueType::Cubemap:
        return "cubemap";
    case ConcreteValueType::Gradient:
        return "gradient";
    case ConcreteValueType::Vector4:
        return "vector4";
    case ConcreteValueType::Vector3:
        return "vector3";
    case ConcreteValueType::Vector2:
        return "vector2";
    case ConcreteValueType::Float:
        return "float";
    case ConcreteValueType::Boolean:
        return "boolean";
    case ConcreteValueType::Integer:
        return "integer";
    case ConcreteValueType::VirtualTexture:
        return "virtualtexture";
    case ConcreteValueType::PropertyConnectionState:
        return "propertyconnectionstate";
    case ConcreteValueType::External:
        return "external";
    }
    return "unknown";
}

const char* toString(Precision precision) noexcept {
    switch (precision) {
    case Precision::Inherit:
        return "inherit";
    case Precision::Single:
        return "single";
    case Precision::Half:
        return "half";
    case Precision::Graph:
        return "graph";
    }
    return "inherit";
}

const char* toString(GraphPrecision precision) noexcept {
    switch (precision) {
    case GraphPrecision::Single:
        return "single";
    case GraphPrecision::Graph:
        return "graph";
    case GraphPrecision::Half:
        return "half";
    }
    return "graph";
}

const char* toString(ConcretePrecision precision) noexcept {
    return precision == ConcretePrecision::Half ? "half" : "single";
}

const char* toString(SlotDirection direction) noexcept {
    return direction == SlotDirection::Input ? "in" : "out";
}

const char* toString(SlotMode mode) noexcept {
    switch (mode) {
    case SlotMode::Default:
        return "default";
    case SlotMode::Literal:
        return "literal";
    case SlotMode::Connected:
        return "connected";
    }
    return "default";
}

const char* toString(GraphKind kind) noexcept {
    return kind == GraphKind::SubGraph ? "subgraph" : "graph";
}

const char* toString(Severity severity) noexcept {
    switch (severity) {
    case Severity::Info:
        return "info";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }
    return "error";
}

const char* toString(KeywordType type) noexcept {
    return type == KeywordType::Enum ? "enum" : "boolean";
}

const char* toString(KeywordDefinition definition) noexcept {
    switch (definition) {
    case KeywordDefinition::ShaderFeature:
        return "shaderfeature";
    case KeywordDefinition::MultiCompile:
        return "multicompile";
    case KeywordDefinition::Predefined:
        return "predefined";
    case KeywordDefinition::DynamicBranch:
        return "dynamicbranch";
    }
    return "shaderfeature";
}

const char* toString(KeywordScope scope) noexcept {
    return scope == KeywordScope::Global ? "global" : "local";
}

std::optional<ValueType> valueTypeFromString(std::string_view text) noexcept {
    for (int i = 0; i <= static_cast<int>(ValueType::String); ++i) {
        const auto type = static_cast<ValueType>(i);
        if (text == toString(type)) {
            return type;
        }
    }
    return std::nullopt;
}

std::optional<ConcreteValueType> concreteTypeFromString(std::string_view text) noexcept {
    for (int i = 0; i <= static_cast<int>(ConcreteValueType::External); ++i) {
        const auto type = static_cast<ConcreteValueType>(i);
        if (text == toString(type)) {
            return type;
        }
    }
    return std::nullopt;
}

std::optional<Precision> precisionFromString(std::string_view text) noexcept {
    if (text == "inherit") {
        return Precision::Inherit;
    }
    if (text == "single") {
        return Precision::Single;
    }
    if (text == "half") {
        return Precision::Half;
    }
    if (text == "graph") {
        return Precision::Graph;
    }
    return std::nullopt;
}

std::optional<GraphPrecision> graphPrecisionFromString(std::string_view text) noexcept {
    if (text == "single") {
        return GraphPrecision::Single;
    }
    if (text == "half") {
        return GraphPrecision::Half;
    }
    if (text == "graph") {
        return GraphPrecision::Graph;
    }
    return std::nullopt;
}

std::optional<ConcretePrecision> concretePrecisionFromString(std::string_view text) noexcept {
    if (text == "single") {
        return ConcretePrecision::Single;
    }
    if (text == "half") {
        return ConcretePrecision::Half;
    }
    return std::nullopt;
}

std::optional<SlotDirection> slotDirectionFromString(std::string_view text) noexcept {
    if (text == "in" || text == "input") {
        return SlotDirection::Input;
    }
    if (text == "out" || text == "output") {
        return SlotDirection::Output;
    }
    return std::nullopt;
}

std::optional<SlotMode> slotModeFromString(std::string_view text) noexcept {
    if (text == "default") {
        return SlotMode::Default;
    }
    if (text == "literal") {
        return SlotMode::Literal;
    }
    if (text == "connected") {
        return SlotMode::Connected;
    }
    return std::nullopt;
}

std::optional<GraphKind> graphKindFromString(std::string_view text) noexcept {
    if (text == "graph") {
        return GraphKind::Graph;
    }
    if (text == "subgraph") {
        return GraphKind::SubGraph;
    }
    return std::nullopt;
}

std::optional<KeywordType> keywordTypeFromString(std::string_view text) noexcept {
    if (text == "boolean") {
        return KeywordType::Boolean;
    }
    if (text == "enum") {
        return KeywordType::Enum;
    }
    return std::nullopt;
}

std::optional<KeywordDefinition> keywordDefinitionFromString(std::string_view text) noexcept {
    if (text == "shaderfeature") {
        return KeywordDefinition::ShaderFeature;
    }
    if (text == "multicompile") {
        return KeywordDefinition::MultiCompile;
    }
    if (text == "predefined") {
        return KeywordDefinition::Predefined;
    }
    if (text == "dynamicbranch") {
        return KeywordDefinition::DynamicBranch;
    }
    return std::nullopt;
}

std::optional<KeywordScope> keywordScopeFromString(std::string_view text) noexcept {
    if (text == "local") {
        return KeywordScope::Local;
    }
    if (text == "global") {
        return KeywordScope::Global;
    }
    return std::nullopt;
}

} // namespace sg
