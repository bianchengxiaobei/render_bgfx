#include "vfx/expression.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vfx {

const Property* ExpressionContext::findProperty(std::string_view name) const noexcept {
    for (const Property& property : m_graph->properties) {
        if (property.name == name) {
            return &property;
        }
    }
    return nullptr;
}

const AttributeDesc* ExpressionContext::findAttribute(std::string_view name) const noexcept {
    if (const AttributeDesc* builtIn = findBuiltInAttribute(name)) {
        return builtIn;
    }
    for (const AttributeDesc& attribute : m_graph->attributes) {
        if (attribute.name == name) {
            return &attribute;
        }
    }
    return nullptr;
}

namespace {

constexpr std::size_t kMaxExpressionDepth = 64;

std::string childPath(std::string_view path, std::string_view name) {
    if (path.empty()) {
        return std::string(name);
    }
    std::string result(path);
    result.push_back('.');
    result.append(name);
    return result;
}

bool isFloatVector(ExprType type) noexcept {
    return type == ExprType::Float2 || type == ExprType::Float3 || type == ExprType::Float4;
}

const char* hlslType(ExprType type) noexcept {
    switch (type) {
    case ExprType::Float:
        return "float";
    case ExprType::Float2:
        return "float2";
    case ExprType::Float3:
        return "float3";
    case ExprType::Float4:
        return "float4";
    case ExprType::UInt:
        return "uint";
    case ExprType::Int:
        return "int";
    case ExprType::Bool:
        return "bool";
    case ExprType::Invalid:
        break;
    }
    return "invalid";
}

std::string formatFloat(double value) {
    const float f = static_cast<float>(value);
    char buffer[64] = {};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), f, std::chars_format::general);
    std::string text;
    if (result.ec == std::errc()) {
        text.assign(buffer, result.ptr);
    } else {
        text = std::to_string(f);
    }
    if (text.find_first_of(".eE") == std::string::npos) {
        text += ".0";
    }
    return text;
}

std::string formatLiteral(ExprType type, const ExpressionValue& value) {
    switch (type) {
    case ExprType::Float:
        return formatFloat(value.components[0]);
    case ExprType::Float2:
        return "float2(" + formatFloat(value.components[0]) + ", " + formatFloat(value.components[1]) + ")";
    case ExprType::Float3:
        return "float3(" + formatFloat(value.components[0]) + ", " + formatFloat(value.components[1]) + ", " +
               formatFloat(value.components[2]) + ")";
    case ExprType::Float4:
        return "float4(" + formatFloat(value.components[0]) + ", " + formatFloat(value.components[1]) + ", " +
               formatFloat(value.components[2]) + ", " + formatFloat(value.components[3]) + ")";
    case ExprType::UInt:
        return std::to_string(static_cast<std::uint32_t>(value.components[0])) + "u";
    case ExprType::Int:
        return std::to_string(static_cast<std::int32_t>(value.components[0]));
    case ExprType::Bool:
        return value.components[0] != 0.0 ? "true" : "false";
    case ExprType::Invalid:
        break;
    }
    return "0.0";
}

ExpressionValue makeValue(ExprType type, std::array<double, 4> components) {
    ExpressionValue value;
    value.type = type;
    value.components = components;
    return value;
}

std::shared_ptr<const ExpressionNode> makeLiteral(ExprType type, const ExpressionValue& value) {
    auto node = std::make_shared<ExpressionNode>();
    node->op = ExpressionOp::Literal;
    node->type = type;
    node->constant = true;
    node->value = value;
    node->hlsl = formatLiteral(type, value);
    return node;
}

std::shared_ptr<const ExpressionNode> makeOp(ExpressionOp op, ExprType type,
                                             std::vector<std::shared_ptr<const ExpressionNode>> inputs,
                                             std::string code, std::string name = {}, std::string mask = {}) {
    auto node = std::make_shared<ExpressionNode>();
    node->op = op;
    node->type = type;
    node->inputs = std::move(inputs);
    node->name = std::move(name);
    node->mask = std::move(mask);
    node->hlsl = std::move(code);
    return node;
}

std::shared_ptr<const ExpressionNode> makeFolded(ExpressionOp op, ExprType type,
                                                 std::vector<std::shared_ptr<const ExpressionNode>> inputs,
                                                 const ExpressionValue& value) {
    auto node = std::make_shared<ExpressionNode>();
    node->op = op;
    node->type = type;
    node->constant = true;
    node->value = value;
    node->inputs = std::move(inputs);
    node->hlsl = formatLiteral(type, value);
    return node;
}

bool isIntegral(double value) {
    return std::isfinite(value) && std::floor(value) == value;
}

std::optional<std::uint32_t> toUInt(double value) {
    if (!isIntegral(value) || value < 0.0 || value > static_cast<double>(std::numeric_limits<std::uint32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value);
}

std::optional<std::int32_t> toInt(double value) {
    if (!isIntegral(value) || value < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
        value > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
        return std::nullopt;
    }
    return static_cast<std::int32_t>(value);
}

std::optional<ExpressionOp> expressionOpFromString(std::string_view op) {
    static constexpr std::pair<std::string_view, ExpressionOp> kOps[] = {
        {"attr", ExpressionOp::Attribute},
        {"attribute", ExpressionOp::Attribute},
        {"property", ExpressionOp::Property},
        {"time", ExpressionOp::Time},
        {"dt", ExpressionOp::DeltaTime},
        {"lifetime_ratio", ExpressionOp::LifetimeRatio},
        {"add", ExpressionOp::Add},
        {"sub", ExpressionOp::Subtract},
        {"mul", ExpressionOp::Multiply},
        {"div", ExpressionOp::Divide},
        {"min", ExpressionOp::Min},
        {"max", ExpressionOp::Max},
        {"clamp", ExpressionOp::Clamp},
        {"lerp", ExpressionOp::Lerp},
        {"remap", ExpressionOp::Remap},
        {"sin", ExpressionOp::Sin},
        {"cos", ExpressionOp::Cos},
        {"abs", ExpressionOp::Abs},
        {"saturate", ExpressionOp::Saturate},
        {"random", ExpressionOp::Random},
        {"noise", ExpressionOp::Noise},
        {"sample_curve", ExpressionOp::SampleCurve},
        {"sample_gradient", ExpressionOp::SampleGradient},
        {"sample_texture", ExpressionOp::SampleTexture},
        {"select", ExpressionOp::Select},
        {"swizzle", ExpressionOp::Swizzle},
    };
    for (const auto& [name, value] : kOps) {
        if (name == op) {
            return value;
        }
    }
    return std::nullopt;
}

ExprType expressionType(PropertyType type) noexcept {
    switch (type) {
    case PropertyType::Float:
        return ExprType::Float;
    case PropertyType::Int:
        return ExprType::Int;
    case PropertyType::Bool:
        return ExprType::Bool;
    case PropertyType::Vector2:
        return ExprType::Float2;
    case PropertyType::Vector3:
        return ExprType::Float3;
    case PropertyType::Vector4:
    case PropertyType::Color:
        return ExprType::Float4;
    case PropertyType::Texture:
    case PropertyType::Curve:
    case PropertyType::Gradient:
        break;
    }
    return ExprType::Invalid;
}

ExprType expressionType(AttributeType type) noexcept {
    switch (type) {
    case AttributeType::Float:
        return ExprType::Float;
    case AttributeType::Float2:
        return ExprType::Float2;
    case AttributeType::Float3:
        return ExprType::Float3;
    case AttributeType::Float4:
        return ExprType::Float4;
    case AttributeType::UInt:
        return ExprType::UInt;
    case AttributeType::Int:
        return ExprType::Int;
    case AttributeType::Bool:
        return ExprType::Bool;
    }
    return ExprType::Invalid;
}

bool unifyTypes(ExprType a, ExprType b, ExprType& out) noexcept {
    if (a == ExprType::Invalid || b == ExprType::Invalid) {
        return false;
    }
    if (a == b) {
        out = a;
        return true;
    }
    if (!isNumericExpressionType(a) || !isNumericExpressionType(b)) {
        return false;
    }
    const bool aVector = isFloatVector(a);
    const bool bVector = isFloatVector(b);
    if (aVector && bVector) {
        if (expressionComponentCount(a) != expressionComponentCount(b)) {
            return false;
        }
        out = a;
        return true;
    }
    if (aVector || bVector) {
        out = aVector ? a : b;
        return true;
    }
    if (a == ExprType::Float || b == ExprType::Float) {
        out = ExprType::Float;
    } else if (a == ExprType::Int || b == ExprType::Int) {
        out = ExprType::Int;
    } else {
        out = ExprType::UInt;
    }
    return true;
}

std::optional<ExpressionValue> foldBinary(ExpressionOp op, ExprType type, const ExpressionValue& a,
                                          const ExpressionValue& b) {
    ExpressionValue result;
    result.type = type;
    const std::uint8_t components = expressionComponentCount(type);
    for (std::uint8_t i = 0; i < components; ++i) {
        if (type == ExprType::Float || isFloatVector(type)) {
            const float av = static_cast<float>(a.components[i]);
            const float bv = static_cast<float>(b.components[i]);
            float value = 0.0f;
            switch (op) {
            case ExpressionOp::Add:
                value = av + bv;
                break;
            case ExpressionOp::Subtract:
                value = av - bv;
                break;
            case ExpressionOp::Multiply:
                value = av * bv;
                break;
            case ExpressionOp::Divide:
                value = av / bv;
                break;
            case ExpressionOp::Min:
                value = std::min(av, bv);
                break;
            case ExpressionOp::Max:
                value = std::max(av, bv);
                break;
            default:
                return std::nullopt;
            }
            if (!std::isfinite(value)) {
                return std::nullopt;
            }
            result.components[i] = value;
        } else if (type == ExprType::UInt) {
            const std::uint32_t av = static_cast<std::uint32_t>(a.components[i]);
            const std::uint32_t bv = static_cast<std::uint32_t>(b.components[i]);
            std::uint32_t value = 0u;
            switch (op) {
            case ExpressionOp::Add:
                value = av + bv;
                break;
            case ExpressionOp::Subtract:
                value = av - bv;
                break;
            case ExpressionOp::Multiply:
                value = av * bv;
                break;
            case ExpressionOp::Divide:
                value = bv == 0u ? 0u : av / bv;
                break;
            case ExpressionOp::Min:
                value = std::min(av, bv);
                break;
            case ExpressionOp::Max:
                value = std::max(av, bv);
                break;
            default:
                return std::nullopt;
            }
            result.components[i] = static_cast<double>(value);
        } else if (type == ExprType::Int) {
            const auto av = static_cast<std::int32_t>(a.components[i]);
            const auto bv = static_cast<std::int32_t>(b.components[i]);
            std::int32_t value = 0;
            switch (op) {
            case ExpressionOp::Add:
                value = static_cast<std::int32_t>(static_cast<std::uint32_t>(av) +
                                                  static_cast<std::uint32_t>(bv));
                break;
            case ExpressionOp::Subtract:
                value = static_cast<std::int32_t>(static_cast<std::uint32_t>(av) -
                                                  static_cast<std::uint32_t>(bv));
                break;
            case ExpressionOp::Multiply:
                value = static_cast<std::int32_t>(static_cast<std::uint32_t>(av) *
                                                  static_cast<std::uint32_t>(bv));
                break;
            case ExpressionOp::Divide:
                if (bv == 0 || (av == std::numeric_limits<std::int32_t>::min() && bv == -1)) {
                    value = 0;
                } else {
                    value = av / bv;
                }
                break;
            case ExpressionOp::Min:
                value = std::min(av, bv);
                break;
            case ExpressionOp::Max:
                value = std::max(av, bv);
                break;
            default:
                return std::nullopt;
            }
            result.components[i] = static_cast<double>(value);
        } else {
            return std::nullopt;
        }
    }
    return result;
}

struct Compiler {
    explicit Compiler(const ExpressionContext& context) : context(context) {}

    std::shared_ptr<const ExpressionNode> compile(const Value& value, const std::string& path,
                                                   ExprType expected, std::size_t depth) {
        if (depth > kMaxExpressionDepth) {
            fail(path, "expression nesting depth exceeds 64");
            return nullptr;
        }
        if (!errors.empty()) {
            return nullptr;
        }

        switch (value.kind) {
        case Value::Kind::Number:
            return coerce(compileNumber(value.number, path, expected), expected, path);
        case Value::Kind::Bool:
            return coerce(makeLiteral(ExprType::Bool, makeValue(ExprType::Bool, {value.boolean ? 1.0 : 0.0})),
                          expected, path);
        case Value::Kind::Array:
            return compileArray(value, path, expected, depth);
        case Value::Kind::Expression:
            if (value.expression == nullptr) {
                fail(path, "expression is empty");
                return nullptr;
            }
            return coerce(compileExpression(*value.expression, path, depth), expected, path);
        case Value::Kind::Invalid:
        case Value::Kind::String:
        case Value::Kind::Object:
            fail(path, "expected a number, boolean, array, or expression");
            return nullptr;
        }
        return nullptr;
    }

    std::shared_ptr<const ExpressionNode> compileNumber(double number, const std::string& path, ExprType expected) {
        if (!std::isfinite(number)) {
            fail(path, "number must be finite");
            return nullptr;
        }
        if (expected == ExprType::UInt) {
            const auto converted = toUInt(number);
            if (!converted.has_value()) {
                fail(path, "expected a uint literal");
                return nullptr;
            }
            return makeLiteral(ExprType::UInt, makeValue(ExprType::UInt, {static_cast<double>(*converted)}));
        }
        if (expected == ExprType::Int) {
            const auto converted = toInt(number);
            if (!converted.has_value()) {
                fail(path, "expected an int literal");
                return nullptr;
            }
            return makeLiteral(ExprType::Int, makeValue(ExprType::Int, {static_cast<double>(*converted)}));
        }
        if (expected == ExprType::Bool) {
            return makeLiteral(ExprType::Bool, makeValue(ExprType::Bool, {number != 0.0 ? 1.0 : 0.0}));
        }
        return makeLiteral(ExprType::Float, makeValue(ExprType::Float, {static_cast<float>(number)}));
    }

    std::shared_ptr<const ExpressionNode> compileArray(const Value& value, const std::string& path,
                                                       ExprType expected, std::size_t depth) {
        (void)depth;
        if (value.array.empty()) {
            fail(path, "array literal must not be empty");
            return nullptr;
        }
        if (value.array.size() > 4) {
            fail(path, "array literal supports at most 4 components");
            return nullptr;
        }

        ExprType type = ExprType::Invalid;
        if (isFloatVector(expected) || expected == ExprType::Float) {
            if (value.array.size() != expressionComponentCount(expected)) {
                fail(path, "array length does not match " + std::string(toString(expected)));
                return nullptr;
            }
            type = expected;
        } else if (expected == ExprType::UInt || expected == ExprType::Int || expected == ExprType::Bool) {
            if (value.array.size() != 1) {
                fail(path, "scalar expression requires a one-element array");
                return nullptr;
            }
            type = expected;
        } else {
            switch (value.array.size()) {
            case 1:
                type = ExprType::Float;
                break;
            case 2:
                type = ExprType::Float2;
                break;
            case 3:
                type = ExprType::Float3;
                break;
            case 4:
                type = ExprType::Float4;
                break;
            default:
                fail(path, "array literal supports 1 to 4 components");
                return nullptr;
            }
        }

        ExpressionValue result;
        result.type = type;
        for (std::size_t i = 0; i < value.array.size(); ++i) {
            const Value& item = value.array[i];
            const std::string itemPath = path + "[" + std::to_string(i) + "]";
            if (item.kind != Value::Kind::Number || !std::isfinite(item.number)) {
                fail(itemPath, "array literal must contain finite numbers");
                return nullptr;
            }
            if (type == ExprType::UInt) {
                const auto converted = toUInt(item.number);
                if (!converted.has_value()) {
                    fail(itemPath, "expected a uint literal");
                    return nullptr;
                }
                result.components[i] = static_cast<double>(*converted);
            } else if (type == ExprType::Int) {
                const auto converted = toInt(item.number);
                if (!converted.has_value()) {
                    fail(itemPath, "expected an int literal");
                    return nullptr;
                }
                result.components[i] = static_cast<double>(*converted);
            } else if (type == ExprType::Bool) {
                result.components[i] = item.number != 0.0 ? 1.0 : 0.0;
            } else {
                result.components[i] = static_cast<float>(item.number);
            }
        }
        return coerce(makeLiteral(type, result), expected, path);
    }

    std::shared_ptr<const ExpressionNode> compileExpression(const Expression& expression, const std::string& path,
                                                             std::size_t depth) {
        const auto op = expressionOpFromString(expression.op);
        if (!op.has_value()) {
            fail(childPath(path, "op"), "unknown expression op '" + expression.op + "'");
            return nullptr;
        }

        switch (*op) {
        case ExpressionOp::Attribute:
            return compileAttribute(expression, path);
        case ExpressionOp::Property:
            return compileProperty(expression, path);
        case ExpressionOp::Time:
        case ExpressionOp::DeltaTime:
        case ExpressionOp::LifetimeRatio:
            return compileBuiltIn(*op, expression, path);
        case ExpressionOp::Add:
        case ExpressionOp::Subtract:
        case ExpressionOp::Multiply:
        case ExpressionOp::Divide:
        case ExpressionOp::Min:
        case ExpressionOp::Max:
            return compileBinary(*op, expression, path, depth);
        case ExpressionOp::Clamp:
            return compileClamp(expression, path, depth);
        case ExpressionOp::Lerp:
            return compileLerp(expression, path, depth);
        case ExpressionOp::Remap:
            return compileRemap(expression, path, depth);
        case ExpressionOp::Sin:
        case ExpressionOp::Cos:
        case ExpressionOp::Abs:
        case ExpressionOp::Saturate:
            return compileUnary(*op, expression, path, depth);
        case ExpressionOp::Random:
            return compileRandom(expression, path, depth);
        case ExpressionOp::Noise:
            return compileNoise(expression, path, depth);
        case ExpressionOp::SampleCurve:
        case ExpressionOp::SampleGradient:
            return compileSample(*op, expression, path, depth);
        case ExpressionOp::SampleTexture:
            fail(path, "sample_texture is not supported until V1.1");
            return nullptr;
        case ExpressionOp::Select:
            return compileSelect(expression, path, depth);
        case ExpressionOp::Swizzle:
            return compileSwizzle(expression, path, depth);
        case ExpressionOp::Literal:
        case ExpressionOp::Cast:
        case ExpressionOp::Splat:
            break;
        }
        fail(path, "unsupported expression op");
        return nullptr;
    }

    std::shared_ptr<const ExpressionNode> compileAttribute(const Expression& expression, const std::string& path) {
        if (!checkArguments(expression, path, {"name"})) {
            return nullptr;
        }
        const Value* nameValue = argument(expression, "name");
        if (nameValue == nullptr || nameValue->kind != Value::Kind::String) {
            fail(childPath(path, "name"), "must be an attribute name");
            return nullptr;
        }
        const AttributeDesc* attribute = context.findAttribute(nameValue->string);
        if (attribute == nullptr) {
            fail(childPath(path, "name"), "unknown attribute '" + nameValue->string + "'");
            return nullptr;
        }
        const ExprType type = expressionType(attribute->type);
        if (type == ExprType::Invalid) {
            fail(childPath(path, "name"), "attribute type is not supported");
            return nullptr;
        }
        return makeOp(ExpressionOp::Attribute, type, {}, "VFX_ATTR(" + attribute->name + ")", attribute->name);
    }

    std::shared_ptr<const ExpressionNode> compileProperty(const Expression& expression, const std::string& path) {
        if (!checkArguments(expression, path, {"name"})) {
            return nullptr;
        }
        const Value* nameValue = argument(expression, "name");
        if (nameValue == nullptr || nameValue->kind != Value::Kind::String) {
            fail(childPath(path, "name"), "must be a property name");
            return nullptr;
        }
        const Property* property = context.findProperty(nameValue->string);
        if (property == nullptr) {
            fail(childPath(path, "name"), "unknown property '" + nameValue->string + "'");
            return nullptr;
        }
        const ExprType type = expressionType(property->type);
        if (type == ExprType::Invalid) {
            fail(childPath(path, "name"),
                 "property '" + property->name + "' cannot be used as a scalar expression");
            return nullptr;
        }
        return makeOp(ExpressionOp::Property, type, {}, "VFX_PROP(" + property->name + ")", property->name);
    }

    std::shared_ptr<const ExpressionNode> compileBuiltIn(ExpressionOp op, const Expression& expression,
                                                           const std::string& path) {
        if (!checkArguments(expression, path, {})) {
            return nullptr;
        }
        switch (op) {
        case ExpressionOp::Time:
            return makeOp(op, ExprType::Float, {}, "VFX_TIME");
        case ExpressionOp::DeltaTime:
            return makeOp(op, ExprType::Float, {}, "VFX_DT");
        case ExpressionOp::LifetimeRatio:
            return makeOp(op, ExprType::Float, {}, "VFX_LIFETIME_RATIO");
        default:
            break;
        }
        fail(path, "unsupported built-in expression");
        return nullptr;
    }

    std::shared_ptr<const ExpressionNode> compileBinary(ExpressionOp op, const Expression& expression,
                                                         const std::string& path, std::size_t depth) {
        if (!checkArguments(expression, path, {"a", "b"})) {
            return nullptr;
        }
        const std::string pathA = childPath(path, "a");
        const std::string pathB = childPath(path, "b");
        auto a = compileRequired(expression, "a", pathA, depth);
        auto b = compileRequired(expression, "b", pathB, depth);
        if (!a || !b) {
            return nullptr;
        }
        ExprType type = ExprType::Invalid;
        if (!unifyTypes(a->type, b->type, type)) {
            fail(pathB, "cannot combine " + std::string(toString(a->type)) + " and " +
                            std::string(toString(b->type)));
            return nullptr;
        }
        a = coerce(a, type, pathA);
        b = coerce(b, type, pathB);
        if (!a || !b) {
            return nullptr;
        }

        std::vector<std::shared_ptr<const ExpressionNode>> inputs{a, b};
        if (a->constant && b->constant) {
            const auto folded = foldBinary(op, type, a->value, b->value);
            if (folded.has_value()) {
                return makeFolded(op, type, std::move(inputs), *folded);
            }
        }

        std::string code;
        switch (op) {
        case ExpressionOp::Add:
            code = "(" + a->hlsl + " + " + b->hlsl + ")";
            break;
        case ExpressionOp::Subtract:
            code = "(" + a->hlsl + " - " + b->hlsl + ")";
            break;
        case ExpressionOp::Multiply:
            code = "(" + a->hlsl + " * " + b->hlsl + ")";
            break;
        case ExpressionOp::Divide:
            code = "(" + a->hlsl + " / " + b->hlsl + ")";
            break;
        case ExpressionOp::Min:
            code = type == ExprType::UInt ? "(" + a->hlsl + " < " + b->hlsl + " ? " + a->hlsl + " : " + b->hlsl + ")"
                                          : "min(" + a->hlsl + ", " + b->hlsl + ")";
            break;
        case ExpressionOp::Max:
            code = type == ExprType::UInt ? "(" + a->hlsl + " > " + b->hlsl + " ? " + a->hlsl + " : " + b->hlsl + ")"
                                          : "max(" + a->hlsl + ", " + b->hlsl + ")";
            break;
        default:
            break;
        }
        return makeOp(op, type, std::move(inputs), std::move(code));
    }

    std::shared_ptr<const ExpressionNode> compileClamp(const Expression& expression, const std::string& path,
                                                        std::size_t depth) {
        if (!checkArguments(expression, path, {"a", "b", "c"})) {
            return nullptr;
        }
        auto a = compileRequired(expression, "a", childPath(path, "a"), depth);
        auto b = compileRequired(expression, "b", childPath(path, "b"), depth);
        auto c = compileRequired(expression, "c", childPath(path, "c"), depth);
        if (!a || !b || !c) {
            return nullptr;
        }
        ExprType type = ExprType::Invalid;
        if (!unifyTypes(a->type, b->type, type) || !unifyTypes(type, c->type, type)) {
            fail(childPath(path, "c"), "cannot combine clamp argument types");
            return nullptr;
        }
        if (!isFloatExpressionType(type)) {
            fail(path, "clamp requires float arguments");
            return nullptr;
        }
        a = coerce(a, type, childPath(path, "a"));
        b = coerce(b, type, childPath(path, "b"));
        c = coerce(c, type, childPath(path, "c"));
        if (!a || !b || !c) {
            return nullptr;
        }

        std::vector<std::shared_ptr<const ExpressionNode>> inputs{a, b, c};
        if (a->constant && b->constant && c->constant) {
            ExpressionValue folded;
            folded.type = type;
            for (std::uint8_t i = 0; i < expressionComponentCount(type); ++i) {
                folded.components[i] = std::clamp(static_cast<float>(a->value.components[i]),
                                                  static_cast<float>(b->value.components[i]),
                                                  static_cast<float>(c->value.components[i]));
            }
            return makeFolded(ExpressionOp::Clamp, type, std::move(inputs), folded);
        }
        const std::string code = "clamp(" + a->hlsl + ", " + b->hlsl + ", " + c->hlsl + ")";
        return makeOp(ExpressionOp::Clamp, type, std::move(inputs), code);
    }

    std::shared_ptr<const ExpressionNode> compileLerp(const Expression& expression, const std::string& path,
                                                       std::size_t depth) {
        if (!checkArguments(expression, path, {"a", "b", "t"})) {
            return nullptr;
        }
        auto a = compileRequired(expression, "a", childPath(path, "a"), depth);
        auto b = compileRequired(expression, "b", childPath(path, "b"), depth);
        auto t = compileRequired(expression, "t", childPath(path, "t"), depth);
        if (!a || !b || !t) {
            return nullptr;
        }
        ExprType type = ExprType::Invalid;
        if (!unifyTypes(a->type, b->type, type)) {
            fail(childPath(path, "b"), "cannot combine lerp endpoints");
            return nullptr;
        }
        if (!isFloatExpressionType(type)) {
            fail(path, "lerp requires float arguments");
            return nullptr;
        }
        a = coerce(a, type, childPath(path, "a"));
        b = coerce(b, type, childPath(path, "b"));
        t = coerce(t, type, childPath(path, "t"));
        if (!a || !b || !t) {
            return nullptr;
        }

        std::vector<std::shared_ptr<const ExpressionNode>> inputs{a, b, t};
        if (a->constant && b->constant && t->constant) {
            ExpressionValue folded;
            folded.type = type;
            for (std::uint8_t i = 0; i < expressionComponentCount(type); ++i) {
                const float av = static_cast<float>(a->value.components[i]);
                const float bv = static_cast<float>(b->value.components[i]);
                const float tv = static_cast<float>(t->value.components[i]);
                folded.components[i] = av + (bv - av) * tv;
            }
            return makeFolded(ExpressionOp::Lerp, type, std::move(inputs), folded);
        }
        const std::string code = "lerp(" + a->hlsl + ", " + b->hlsl + ", " + t->hlsl + ")";
        return makeOp(ExpressionOp::Lerp, type, std::move(inputs), code);
    }

    std::shared_ptr<const ExpressionNode> compileRemap(const Expression& expression, const std::string& path,
                                                        std::size_t depth) {
        if (!checkArguments(expression, path, {"x", "inMin", "inMax", "outMin", "outMax"})) {
            return nullptr;
        }
        auto x = compileRequired(expression, "x", childPath(path, "x"), depth);
        auto inMin = compileRequired(expression, "inMin", childPath(path, "inMin"), depth);
        auto inMax = compileRequired(expression, "inMax", childPath(path, "inMax"), depth);
        auto outMin = compileRequired(expression, "outMin", childPath(path, "outMin"), depth);
        auto outMax = compileRequired(expression, "outMax", childPath(path, "outMax"), depth);
        if (!x || !inMin || !inMax || !outMin || !outMax) {
            return nullptr;
        }
        ExprType type = ExprType::Invalid;
        if (!unifyTypes(x->type, inMin->type, type) || !unifyTypes(type, inMax->type, type) ||
            !unifyTypes(type, outMin->type, type) || !unifyTypes(type, outMax->type, type)) {
            fail(childPath(path, "outMax"), "cannot combine remap argument types");
            return nullptr;
        }
        if (!isFloatExpressionType(type)) {
            fail(path, "remap requires float arguments");
            return nullptr;
        }
        x = coerce(x, type, childPath(path, "x"));
        inMin = coerce(inMin, type, childPath(path, "inMin"));
        inMax = coerce(inMax, type, childPath(path, "inMax"));
        outMin = coerce(outMin, type, childPath(path, "outMin"));
        outMax = coerce(outMax, type, childPath(path, "outMax"));
        if (!x || !inMin || !inMax || !outMin || !outMax) {
            return nullptr;
        }

        std::vector<std::shared_ptr<const ExpressionNode>> inputs{x, inMin, inMax, outMin, outMax};
        if (x->constant && inMin->constant && inMax->constant && outMin->constant && outMax->constant) {
            ExpressionValue folded;
            folded.type = type;
            for (std::uint8_t i = 0; i < expressionComponentCount(type); ++i) {
                const float xv = static_cast<float>(x->value.components[i]);
                const float imin = static_cast<float>(inMin->value.components[i]);
                const float imax = static_cast<float>(inMax->value.components[i]);
                const float omin = static_cast<float>(outMin->value.components[i]);
                const float omax = static_cast<float>(outMax->value.components[i]);
                const float denominator = imax - imin;
                if (denominator == 0.0f) {
                    fail(path, "remap input range must not be zero");
                    return nullptr;
                }
                folded.components[i] = omin + (xv - imin) * (omax - omin) / denominator;
            }
            if (std::all_of(folded.components.begin(), folded.components.begin() + expressionComponentCount(type),
                            [](double component) { return std::isfinite(component); })) {
                return makeFolded(ExpressionOp::Remap, type, std::move(inputs), folded);
            }
        }
        const std::string code = "(" + outMin->hlsl + " + (" + x->hlsl + " - " + inMin->hlsl + ") * (" +
                                 outMax->hlsl + " - " + outMin->hlsl + ") / (" + inMax->hlsl + " - " +
                                 inMin->hlsl + "))";
        return makeOp(ExpressionOp::Remap, type, std::move(inputs), code);
    }

    std::shared_ptr<const ExpressionNode> compileUnary(ExpressionOp op, const Expression& expression,
                                                        const std::string& path, std::size_t depth) {
        if (!checkArguments(expression, path, {"x"})) {
            return nullptr;
        }
        auto x = compileRequired(expression, "x", childPath(path, "x"), depth);
        if (!x) {
            return nullptr;
        }
        if (op == ExpressionOp::Abs) {
            if (x->type != ExprType::Float && !isFloatVector(x->type) && x->type != ExprType::Int) {
                fail(path, "abs requires a float or int argument");
                return nullptr;
            }
        } else if (!isFloatExpressionType(x->type)) {
            fail(path, std::string(toString(op)) + " requires a float argument");
            return nullptr;
        }

        std::vector<std::shared_ptr<const ExpressionNode>> inputs{x};
        if (x->constant) {
            ExpressionValue folded;
            folded.type = x->type;
            for (std::uint8_t i = 0; i < expressionComponentCount(x->type); ++i) {
                const double value = x->value.components[i];
                switch (op) {
                case ExpressionOp::Sin:
                    folded.components[i] = std::sin(static_cast<float>(value));
                    break;
                case ExpressionOp::Cos:
                    folded.components[i] = std::cos(static_cast<float>(value));
                    break;
                case ExpressionOp::Abs:
                    folded.components[i] = std::abs(value);
                    break;
                case ExpressionOp::Saturate:
                    folded.components[i] = std::clamp(static_cast<float>(value), 0.0f, 1.0f);
                    break;
                default:
                    break;
                }
            }
            return makeFolded(op, x->type, std::move(inputs), folded);
        }

        std::string code;
        switch (op) {
        case ExpressionOp::Sin:
            code = "sin(" + x->hlsl + ")";
            break;
        case ExpressionOp::Cos:
            code = "cos(" + x->hlsl + ")";
            break;
        case ExpressionOp::Abs:
            code = "abs(" + x->hlsl + ")";
            break;
        case ExpressionOp::Saturate:
            code = "saturate(" + x->hlsl + ")";
            break;
        default:
            break;
        }
        return makeOp(op, x->type, std::move(inputs), std::move(code));
    }

    std::shared_ptr<const ExpressionNode> compileRandom(const Expression& expression, const std::string& path,
                                                         std::size_t depth) {
        if (!checkArguments(expression, path, {"min", "max", "seed"})) {
            return nullptr;
        }
        auto min = compileRequired(expression, "min", childPath(path, "min"), depth);
        auto max = compileRequired(expression, "max", childPath(path, "max"), depth);
        if (!min || !max) {
            return nullptr;
        }
        ExprType type = ExprType::Invalid;
        if (!unifyTypes(min->type, max->type, type)) {
            fail(childPath(path, "max"), "cannot combine random bounds");
            return nullptr;
        }
        if (!isFloatExpressionType(type)) {
            fail(path, "random requires float bounds");
            return nullptr;
        }
        min = coerce(min, type, childPath(path, "min"));
        max = coerce(max, type, childPath(path, "max"));
        if (!min || !max) {
            return nullptr;
        }

        std::shared_ptr<const ExpressionNode> seed;
        if (argument(expression, "seed") != nullptr) {
            seed = compileRequired(expression, "seed", childPath(path, "seed"), depth, ExprType::UInt);
        }
        if (!errors.empty()) {
            return nullptr;
        }
        const std::string seedCode = seed != nullptr ? seed->hlsl : "0u";
        const std::string randomCode = type == ExprType::Float ? "VFX_RAND(" + seedCode + ")"
                                                               : "VFX_RAND4(" + seedCode + ")." +
                                                                     std::string(componentMask(type));
        std::vector<std::shared_ptr<const ExpressionNode>> inputs{min, max};
        if (seed != nullptr) {
            inputs.push_back(seed);
        }
        const std::string code = "lerp(" + min->hlsl + ", " + max->hlsl + ", " + randomCode + ")";
        return makeOp(ExpressionOp::Random, type, std::move(inputs), code);
    }

    std::shared_ptr<const ExpressionNode> compileNoise(const Expression& expression, const std::string& path,
                                                        std::size_t depth) {
        if (!checkArguments(expression, path, {"position", "frequency", "seed"})) {
            return nullptr;
        }
        auto position = compileRequired(expression, "position", childPath(path, "position"), depth);
        auto frequency = compileRequired(expression, "frequency", childPath(path, "frequency"), depth);
        if (!position || !frequency) {
            return nullptr;
        }
        if (position->type != ExprType::Float && !isFloatVector(position->type)) {
            fail(childPath(path, "position"), "noise position must be float, float2, or float3");
            return nullptr;
        }
        if (frequency->type != ExprType::Float &&
            expressionComponentCount(frequency->type) != expressionComponentCount(position->type)) {
            fail(childPath(path, "frequency"), "noise frequency must be scalar or match the position");
            return nullptr;
        }
        if (!isFloatExpressionType(frequency->type)) {
            fail(childPath(path, "frequency"), "noise frequency must be a float type");
            return nullptr;
        }

        std::shared_ptr<const ExpressionNode> seed;
        if (argument(expression, "seed") != nullptr) {
            seed = compileRequired(expression, "seed", childPath(path, "seed"), depth, ExprType::UInt);
        }
        if (!errors.empty()) {
            return nullptr;
        }
        const std::string seedCode = seed != nullptr ? seed->hlsl : "0u";
        std::vector<std::shared_ptr<const ExpressionNode>> inputs{position, frequency};
        if (seed != nullptr) {
            inputs.push_back(seed);
        }
        const std::string code = "VFX_NOISE(" + position->hlsl + ", " + frequency->hlsl + ", " + seedCode + ")";
        return makeOp(ExpressionOp::Noise, ExprType::Float, std::move(inputs), code);
    }

    std::shared_ptr<const ExpressionNode> compileSample(ExpressionOp op, const Expression& expression,
                                                         const std::string& path, std::size_t depth) {
        const char* nameKey = op == ExpressionOp::SampleCurve ? "curve" : "gradient";
        if (!checkArguments(expression, path, {nameKey, "t"})) {
            return nullptr;
        }
        const Value* nameValue = argument(expression, nameKey);
        if (nameValue == nullptr || nameValue->kind != Value::Kind::String) {
            fail(childPath(path, nameKey), "must be a property name");
            return nullptr;
        }
        const Property* property = context.findProperty(nameValue->string);
        if (property == nullptr) {
            fail(childPath(path, nameKey), "unknown property '" + nameValue->string + "'");
            return nullptr;
        }
        const PropertyType required = op == ExpressionOp::SampleCurve ? PropertyType::Curve : PropertyType::Gradient;
        if (property->type != required) {
            fail(childPath(path, nameKey),
                 "property '" + property->name + "' has the wrong type for this sampler");
            return nullptr;
        }
        auto t = compileRequired(expression, "t", childPath(path, "t"), depth, ExprType::Float);
        if (!t) {
            return nullptr;
        }
        const ExprType type = op == ExpressionOp::SampleCurve ? ExprType::Float : ExprType::Float4;
        std::vector<std::shared_ptr<const ExpressionNode>> inputs{t};
        const std::string code = (op == ExpressionOp::SampleCurve ? "VFX_SAMPLE_CURVE(" : "VFX_SAMPLE_GRADIENT(") +
                                 property->name + ", " + t->hlsl + ")";
        return makeOp(op, type, std::move(inputs), code, property->name);
    }

    std::shared_ptr<const ExpressionNode> compileSelect(const Expression& expression, const std::string& path,
                                                         std::size_t depth) {
        if (!checkArguments(expression, path, {"condition", "a", "b"})) {
            return nullptr;
        }
        auto condition = compileRequired(expression, "condition", childPath(path, "condition"), depth, ExprType::Bool);
        auto a = compileRequired(expression, "a", childPath(path, "a"), depth);
        auto b = compileRequired(expression, "b", childPath(path, "b"), depth);
        if (!condition || !a || !b) {
            return nullptr;
        }
        ExprType type = ExprType::Invalid;
        if (!unifyTypes(a->type, b->type, type)) {
            fail(childPath(path, "b"), "cannot combine select branches");
            return nullptr;
        }
        a = coerce(a, type, childPath(path, "a"));
        b = coerce(b, type, childPath(path, "b"));
        if (!a || !b) {
            return nullptr;
        }
        std::vector<std::shared_ptr<const ExpressionNode>> inputs{condition, a, b};
        if (condition->constant) {
            const auto& selected = condition->value.components[0] != 0.0 ? *a : *b;
            if (selected.constant) {
                return makeFolded(ExpressionOp::Select, type, std::move(inputs), selected.value);
            }
        }
        const std::string code = "((" + condition->hlsl + ") ? (" + a->hlsl + ") : (" + b->hlsl + "))";
        return makeOp(ExpressionOp::Select, type, std::move(inputs), code);
    }

    std::shared_ptr<const ExpressionNode> compileSwizzle(const Expression& expression, const std::string& path,
                                                          std::size_t depth) {
        if (!checkArguments(expression, path, {"value", "mask"})) {
            return nullptr;
        }
        auto value = compileRequired(expression, "value", childPath(path, "value"), depth);
        if (!value) {
            return nullptr;
        }
        const Value* maskValue = argument(expression, "mask");
        if (maskValue == nullptr || maskValue->kind != Value::Kind::String) {
            fail(childPath(path, "mask"), "must be a swizzle mask");
            return nullptr;
        }
        const std::string& mask = maskValue->string;
        if (mask.empty() || mask.size() > 4) {
            fail(childPath(path, "mask"), "swizzle mask length must be 1 to 4");
            return nullptr;
        }
        if (!isFloatExpressionType(value->type)) {
            fail(childPath(path, "value"), "swizzle requires a float value");
            return nullptr;
        }
        const std::uint8_t sourceComponents = expressionComponentCount(value->type);
        const char* maskChars = "xyzw";
        for (char c : mask) {
            const char* found = std::find(maskChars, maskChars + 4, c);
            if (found == maskChars + 4 || static_cast<std::uint8_t>(found - maskChars) >= sourceComponents) {
                fail(childPath(path, "mask"), "invalid swizzle component in mask");
                return nullptr;
            }
        }
        const ExprType type = expressionVectorType(ExprType::Float, static_cast<std::uint8_t>(mask.size()));
        std::vector<std::shared_ptr<const ExpressionNode>> inputs{value};
        if (value->constant) {
            ExpressionValue folded;
            folded.type = type;
            for (std::size_t i = 0; i < mask.size(); ++i) {
                const std::uint8_t component = static_cast<std::uint8_t>(mask[i] - 'x');
                folded.components[i] = value->value.components[component];
            }
            return makeFolded(ExpressionOp::Swizzle, type, std::move(inputs), folded);
        }
        const std::string code = "(" + value->hlsl + ")." + mask;
        return makeOp(ExpressionOp::Swizzle, type, std::move(inputs), code, {}, mask);
    }

    std::shared_ptr<const ExpressionNode> compileRequired(const Expression& expression, std::string_view name,
                                                           const std::string& path, std::size_t depth,
                                                           ExprType expected = ExprType::Invalid) {
        const Value* value = argument(expression, name);
        if (value == nullptr) {
            fail(path, "is required");
            return nullptr;
        }
        return compile(*value, path, expected, depth + 1);
    }

    std::shared_ptr<const ExpressionNode> coerce(const std::shared_ptr<const ExpressionNode>& node, ExprType expected,
                                                  const std::string& path) {
        if (!node || !errors.empty()) {
            return nullptr;
        }
        if (expected == ExprType::Invalid || node->type == expected) {
            return node;
        }
        if (node->type == ExprType::Bool || expected == ExprType::Invalid) {
            fail(path, "cannot convert " + std::string(toString(node->type)) + " to " +
                            std::string(toString(expected)));
            return nullptr;
        }
        if (expected == ExprType::Bool) {
            if (!isNumericExpressionType(node->type) || isFloatVector(node->type)) {
                fail(path, "boolean conversion requires a scalar numeric value");
                return nullptr;
            }
            return makeCast(node, expected, path);
        }
        if (isFloatVector(expected)) {
            if (!isNumericExpressionType(node->type) || isFloatVector(node->type)) {
                fail(path, "cannot convert " + std::string(toString(node->type)) + " to " +
                                std::string(toString(expected)));
                return nullptr;
            }
            auto scalar = coerce(node, expressionScalarType(expected), path);
            if (!scalar) {
                return nullptr;
            }
            return makeSplat(scalar, expected);
        }
        if (isFloatVector(node->type)) {
            fail(path, "cannot convert " + std::string(toString(node->type)) + " to " +
                            std::string(toString(expected)));
            return nullptr;
        }
        if (!isNumericExpressionType(node->type)) {
            fail(path, "cannot convert " + std::string(toString(node->type)) + " to " +
                            std::string(toString(expected)));
            return nullptr;
        }
        return makeCast(node, expected, path);
    }

    std::shared_ptr<const ExpressionNode> makeCast(const std::shared_ptr<const ExpressionNode>& node,
                                                    ExprType expected, const std::string& path) {
        ExpressionValue folded;
        folded.type = expected;
        if (node->constant) {
            const double value = node->value.components[0];
            if (expected == ExprType::Float) {
                folded.components[0] = static_cast<float>(value);
            } else if (expected == ExprType::UInt) {
                const auto converted = toUInt(value);
                if (!converted.has_value()) {
                    fail(path, "constant cannot be represented as uint");
                    return nullptr;
                }
                folded.components[0] = static_cast<double>(*converted);
            } else if (expected == ExprType::Int) {
                const auto converted = toInt(value);
                if (!converted.has_value()) {
                    fail(path, "constant cannot be represented as int");
                    return nullptr;
                }
                folded.components[0] = static_cast<double>(*converted);
            } else if (expected == ExprType::Bool) {
                folded.components[0] = value != 0.0 ? 1.0 : 0.0;
            }
            return makeFolded(ExpressionOp::Cast, expected, {node}, folded);
        }
        const std::string code = "(" + std::string(hlslType(expected)) + ")(" + node->hlsl + ")";
        return makeOp(ExpressionOp::Cast, expected, {node}, code);
    }

    std::shared_ptr<const ExpressionNode> makeSplat(const std::shared_ptr<const ExpressionNode>& node,
                                                     ExprType expected) {
        const std::uint8_t components = expressionComponentCount(expected);
        ExpressionValue folded;
        folded.type = expected;
        if (node->constant) {
            for (std::uint8_t i = 0; i < components; ++i) {
                folded.components[i] = node->value.components[0];
            }
            return makeFolded(ExpressionOp::Splat, expected, {node}, folded);
        }
        std::string code = std::string(hlslType(expected)) + "(" + node->hlsl;
        for (std::uint8_t i = 1; i < components; ++i) {
            code += ", " + node->hlsl;
        }
        code += ")";
        return makeOp(ExpressionOp::Splat, expected, {node}, std::move(code));
    }

    bool checkArguments(const Expression& expression, const std::string& path,
                        std::initializer_list<std::string_view> allowed) {
        for (const auto& [name, value] : expression.arguments) {
            (void)value;
            bool known = false;
            for (std::string_view candidate : allowed) {
                if (name == candidate) {
                    known = true;
                    break;
                }
            }
            if (!known) {
                fail(childPath(path, name), "unknown expression argument");
                return false;
            }
        }
        return true;
    }

    static const Value* argument(const Expression& expression, std::string_view name) noexcept {
        for (const auto& [argumentName, value] : expression.arguments) {
            if (argumentName == name) {
                return &value;
            }
        }
        return nullptr;
    }

    static const char* componentMask(ExprType type) noexcept {
        switch (type) {
        case ExprType::Float2:
            return "xy";
        case ExprType::Float3:
            return "xyz";
        case ExprType::Float4:
            return "xyzw";
        case ExprType::Float:
        case ExprType::UInt:
        case ExprType::Int:
        case ExprType::Bool:
        case ExprType::Invalid:
            break;
        }
        return "x";
    }

    void fail(std::string path, std::string message) {
        if (errors.empty()) {
            errors.push_back(ExpressionError{std::move(path), std::move(message)});
        }
    }

    const ExpressionContext& context;
    std::vector<ExpressionError> errors;
};

} // namespace

const char* toString(ExprType type) noexcept {
    switch (type) {
    case ExprType::Invalid:
        return "invalid";
    case ExprType::Float:
        return "float";
    case ExprType::Float2:
        return "float2";
    case ExprType::Float3:
        return "float3";
    case ExprType::Float4:
        return "float4";
    case ExprType::UInt:
        return "uint";
    case ExprType::Int:
        return "int";
    case ExprType::Bool:
        return "bool";
    }
    return "invalid";
}

const char* toString(ExpressionOp op) noexcept {
    switch (op) {
    case ExpressionOp::Literal:
        return "literal";
    case ExpressionOp::Attribute:
        return "attr";
    case ExpressionOp::Property:
        return "property";
    case ExpressionOp::Time:
        return "time";
    case ExpressionOp::DeltaTime:
        return "dt";
    case ExpressionOp::LifetimeRatio:
        return "lifetime_ratio";
    case ExpressionOp::Add:
        return "add";
    case ExpressionOp::Subtract:
        return "sub";
    case ExpressionOp::Multiply:
        return "mul";
    case ExpressionOp::Divide:
        return "div";
    case ExpressionOp::Min:
        return "min";
    case ExpressionOp::Max:
        return "max";
    case ExpressionOp::Clamp:
        return "clamp";
    case ExpressionOp::Lerp:
        return "lerp";
    case ExpressionOp::Remap:
        return "remap";
    case ExpressionOp::Sin:
        return "sin";
    case ExpressionOp::Cos:
        return "cos";
    case ExpressionOp::Abs:
        return "abs";
    case ExpressionOp::Saturate:
        return "saturate";
    case ExpressionOp::Random:
        return "random";
    case ExpressionOp::Noise:
        return "noise";
    case ExpressionOp::SampleCurve:
        return "sample_curve";
    case ExpressionOp::SampleGradient:
        return "sample_gradient";
    case ExpressionOp::SampleTexture:
        return "sample_texture";
    case ExpressionOp::Select:
        return "select";
    case ExpressionOp::Swizzle:
        return "swizzle";
    case ExpressionOp::Cast:
        return "cast";
    case ExpressionOp::Splat:
        return "splat";
    }
    return "invalid";
}

std::uint8_t expressionComponentCount(ExprType type) noexcept {
    switch (type) {
    case ExprType::Float:
    case ExprType::UInt:
    case ExprType::Int:
    case ExprType::Bool:
        return 1;
    case ExprType::Float2:
        return 2;
    case ExprType::Float3:
        return 3;
    case ExprType::Float4:
        return 4;
    case ExprType::Invalid:
        break;
    }
    return 0;
}

bool isFloatExpressionType(ExprType type) noexcept {
    return type == ExprType::Float || isFloatVector(type);
}

bool isNumericExpressionType(ExprType type) noexcept {
    return type == ExprType::Float || isFloatVector(type) || type == ExprType::UInt || type == ExprType::Int;
}

ExprType expressionScalarType(ExprType type) noexcept {
    if (isFloatVector(type)) {
        return ExprType::Float;
    }
    return type;
}

ExprType expressionVectorType(ExprType scalar, std::uint8_t components) noexcept {
    if (scalar != ExprType::Float) {
        return ExprType::Invalid;
    }
    switch (components) {
    case 1:
        return ExprType::Float;
    case 2:
        return ExprType::Float2;
    case 3:
        return ExprType::Float3;
    case 4:
        return ExprType::Float4;
    default:
        return ExprType::Invalid;
    }
}

bool isExpressionDagAcyclic(const ExpressionNode& root) {
    struct Frame {
        const ExpressionNode* node;
        std::size_t next;
    };

    std::vector<Frame> stack;
    std::unordered_set<const ExpressionNode*> active;
    std::unordered_set<const ExpressionNode*> done;
    stack.push_back(Frame{&root, 0});
    active.insert(&root);

    while (!stack.empty()) {
        Frame& frame = stack.back();
        if (frame.next < frame.node->inputs.size()) {
            const ExpressionNode* child = frame.node->inputs[frame.next++].get();
            if (child == nullptr) {
                return false;
            }
            if (done.find(child) != done.end()) {
                continue;
            }
            if (active.find(child) != active.end()) {
                return false;
            }
            active.insert(child);
            stack.push_back(Frame{child, 0});
        } else {
            active.erase(frame.node);
            done.insert(frame.node);
            stack.pop_back();
        }
    }
    return true;
}

CompiledExpression compileExpression(const Value& value, const ExpressionContext& context, std::string_view path,
                                     ExprType expected) {
    Compiler compiler(context);
    CompiledExpression result;
    result.root = compiler.compile(value, std::string(path), expected, 0);
    result.errors = std::move(compiler.errors);
    if (!result.errors.empty()) {
        result.root.reset();
    } else if (result.root != nullptr && !isExpressionDagAcyclic(*result.root)) {
        result.errors.push_back(ExpressionError{std::string(path), "expression graph contains a cycle"});
        result.root.reset();
    }
    return result;
}

} // namespace vfx
