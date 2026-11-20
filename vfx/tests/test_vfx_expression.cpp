#include <doctest/doctest.h>

#include "vfx/expression.h"
#include "vfx/graph.h"

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

vfx::Value number(double value) {
    vfx::Value result;
    result.kind = vfx::Value::Kind::Number;
    result.number = value;
    return result;
}

vfx::Value boolean(bool value) {
    vfx::Value result;
    result.kind = vfx::Value::Kind::Bool;
    result.boolean = value;
    return result;
}

vfx::Value array(std::initializer_list<double> values) {
    vfx::Value result;
    result.kind = vfx::Value::Kind::Array;
    for (double value : values) {
        result.array.push_back(number(value));
    }
    return result;
}

vfx::Value expression(std::string op, std::vector<std::pair<std::string, vfx::Value>> arguments) {
    auto value = std::make_shared<vfx::Expression>();
    value->op = std::move(op);
    value->arguments = std::move(arguments);
    vfx::Value result;
    result.kind = vfx::Value::Kind::Expression;
    result.expression = std::move(value);
    return result;
}

vfx::Value attributeValue(std::string name) {
    vfx::Value value;
    value.kind = vfx::Value::Kind::String;
    value.string = std::move(name);
    return expression("attr", {{"name", std::move(value)}});
}

vfx::Value property(const std::string& name) {
    vfx::Value value;
    value.kind = vfx::Value::Kind::String;
    value.string = name;
    return expression("property", {{"name", std::move(value)}});
}

vfx::Property makeProperty(std::string name, vfx::PropertyType type) {
    vfx::Property property;
    property.name = std::move(name);
    property.type = type;
    return property;
}

vfx::Graph makeGraph() {
    vfx::Graph graph;
    graph.name = "Expressions";
    graph.properties.push_back(makeProperty("Size", vfx::PropertyType::Float));
    graph.properties.push_back(makeProperty("Count", vfx::PropertyType::Int));
    graph.properties.push_back(makeProperty("Tint", vfx::PropertyType::Color));
    graph.properties.push_back(makeProperty("MainTex", vfx::PropertyType::Texture));
    graph.properties.push_back(makeProperty("SizeCurve", vfx::PropertyType::Curve));
    graph.properties.push_back(makeProperty("ColorRamp", vfx::PropertyType::Gradient));
    graph.attributes.push_back(vfx::AttributeDesc{"user0", vfx::AttributeType::Float4, {}, {}});
    return graph;
}

vfx::CompiledExpression compile(const vfx::Value& value, vfx::ExprType expected = vfx::ExprType::Invalid) {
    static const vfx::Graph graph = makeGraph();
    const vfx::ExpressionContext context(graph);
    return vfx::compileExpression(value, context, "root", expected);
}

} // namespace

TEST_CASE("vfx expression type helpers") {
    CHECK(std::string(vfx::toString(vfx::ExprType::Float3)) == "float3");
    CHECK(vfx::expressionComponentCount(vfx::ExprType::Float) == 1);
    CHECK(vfx::expressionComponentCount(vfx::ExprType::Float4) == 4);
    CHECK(vfx::isFloatExpressionType(vfx::ExprType::Float3));
    CHECK_FALSE(vfx::isFloatExpressionType(vfx::ExprType::UInt));
    CHECK(vfx::isNumericExpressionType(vfx::ExprType::Int));
    CHECK_FALSE(vfx::isNumericExpressionType(vfx::ExprType::Bool));
    CHECK(static_cast<int>(vfx::expressionScalarType(vfx::ExprType::Float2)) == static_cast<int>(vfx::ExprType::Float));
    CHECK(static_cast<int>(vfx::expressionVectorType(vfx::ExprType::Float, 3)) == static_cast<int>(vfx::ExprType::Float3));
}

TEST_CASE("vfx expression folds arithmetic") {
    const auto add = compile(expression("add", {{"a", number(2.0)}, {"b", number(3.0)}}));
    REQUIRE(add.ok());
    CHECK(static_cast<int>(add.root->type) == static_cast<int>(vfx::ExprType::Float));
    CHECK(add.root->constant);
    CHECK(add.root->value.components[0] == doctest::Approx(5.0));
    CHECK(add.root->hlsl == "5.0");

    const auto scale = compile(expression("mul", {{"a", array({1.0, 2.0, 3.0})}, {"b", number(2.0)}}));
    REQUIRE(scale.ok());
    CHECK(static_cast<int>(scale.root->type) == static_cast<int>(vfx::ExprType::Float3));
    CHECK(scale.root->constant);
    CHECK(scale.root->value.components[0] == doctest::Approx(2.0));
    CHECK(scale.root->value.components[1] == doctest::Approx(4.0));
    CHECK(scale.root->value.components[2] == doctest::Approx(6.0));
    CHECK(scale.root->hlsl == "float3(2.0, 4.0, 6.0)");
}

TEST_CASE("vfx expression reads properties and attributes") {
    const auto size = compile(property("Size"));
    REQUIRE(size.ok());
    CHECK(static_cast<int>(size.root->type) == static_cast<int>(vfx::ExprType::Float));
    CHECK_FALSE(size.root->constant);
    CHECK(size.root->hlsl == "VFX_PROP(Size)");

    const auto position = compile(attributeValue("position"));
    REQUIRE(position.ok());
    CHECK(static_cast<int>(position.root->type) == static_cast<int>(vfx::ExprType::Float3));
    CHECK(position.root->hlsl == "VFX_ATTR(position)");

    const auto user = compile(attributeValue("user0"));
    REQUIRE(user.ok());
    CHECK(static_cast<int>(user.root->type) == static_cast<int>(vfx::ExprType::Float4));
    CHECK(user.root->hlsl == "VFX_ATTR(user0)");

    const auto unknown = compile(attributeValue("missing"));
    REQUIRE_FALSE(unknown.ok());
    REQUIRE_FALSE(unknown.errors.empty());
    CHECK(unknown.errors[0].path == "root.name");
}

TEST_CASE("vfx expression rejects non-scalar properties") {
    const auto texture = compile(property("MainTex"));
    REQUIRE_FALSE(texture.ok());
    REQUIRE_FALSE(texture.errors.empty());
    CHECK(texture.errors[0].path == "root.name");
    CHECK(texture.errors[0].message.find("cannot be used") != std::string::npos);
}

TEST_CASE("vfx expression coerces scalars and expected types") {
    const auto splat = compile(number(2.0), vfx::ExprType::Float3);
    REQUIRE(splat.ok());
    CHECK(static_cast<int>(splat.root->type) == static_cast<int>(vfx::ExprType::Float3));
    CHECK(splat.root->constant);
    CHECK(splat.root->hlsl == "float3(2.0, 2.0, 2.0)");

    const auto vector = compile(expression("add", {{"a", attributeValue("position")}, {"b", number(2.0)}}));
    REQUIRE(vector.ok());
    CHECK(static_cast<int>(vector.root->type) == static_cast<int>(vfx::ExprType::Float3));
    CHECK_FALSE(vector.root->constant);
    CHECK(vector.root->hlsl == "(VFX_ATTR(position) + float3(2.0, 2.0, 2.0))");

    const auto cast = compile(property("Count"), vfx::ExprType::Float);
    REQUIRE(cast.ok());
    CHECK(static_cast<int>(cast.root->type) == static_cast<int>(vfx::ExprType::Float));
    CHECK(cast.root->hlsl == "(float)(VFX_PROP(Count))");

    const auto mismatch = compile(array({1.0, 2.0}), vfx::ExprType::Float3);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE_FALSE(mismatch.errors.empty());
    CHECK(mismatch.errors[0].message.find("length") != std::string::npos);
}

TEST_CASE("vfx expression rejects invalid mixed types") {
    const auto mixed = compile(expression("add", {{"a", attributeValue("alive")}, {"b", number(1.0)}}));
    REQUIRE_FALSE(mixed.ok());
    REQUIRE_FALSE(mixed.errors.empty());
    CHECK(mixed.errors[0].path == "root.b");
    CHECK(mixed.errors[0].message.find("bool") != std::string::npos);

    const auto badBranch = compile(expression("select", {{"condition", boolean(true)},
                                                          {"a", attributeValue("position")},
                                                          {"b", attributeValue("alive")}}));
    REQUIRE_FALSE(badBranch.ok());
    CHECK(badBranch.errors[0].path == "root.b");
}

TEST_CASE("vfx expression folds clamp lerp remap select and swizzle") {
    const auto clamped = compile(expression("clamp", {{"a", number(2.0)}, {"b", number(0.0)}, {"c", number(1.0)}}));
    REQUIRE(clamped.ok());
    CHECK(clamped.root->value.components[0] == doctest::Approx(1.0));

    const auto lerped = compile(expression("lerp", {{"a", number(0.0)}, {"b", number(10.0)}, {"t", number(0.25)}}));
    REQUIRE(lerped.ok());
    CHECK(lerped.root->value.components[0] == doctest::Approx(2.5));

    const auto remapped = compile(expression("remap", {{"x", number(5.0)},
                                                       {"inMin", number(0.0)},
                                                       {"inMax", number(10.0)},
                                                       {"outMin", number(0.0)},
                                                       {"outMax", number(100.0)}}));
    REQUIRE(remapped.ok());
    CHECK(remapped.root->value.components[0] == doctest::Approx(50.0));

    const auto selected = compile(expression("select", {{"condition", boolean(false)},
                                                        {"a", number(1.0)},
                                                        {"b", number(2.0)}}));
    REQUIRE(selected.ok());
    CHECK(selected.root->value.components[0] == doctest::Approx(2.0));

    const auto swizzled = compile(expression("swizzle", {{"value", array({1.0, 2.0, 3.0})}, {"mask", [] {
                                                               vfx::Value value;
                                                               value.kind = vfx::Value::Kind::String;
                                                               value.string = "zyx";
                                                               return value;
                                                           }()}}));
    REQUIRE(swizzled.ok());
    CHECK(static_cast<int>(swizzled.root->type) == static_cast<int>(vfx::ExprType::Float3));
    CHECK(swizzled.root->hlsl == "float3(3.0, 2.0, 1.0)");
}

TEST_CASE("vfx expression folds unary math") {
    const auto saturated = compile(expression("saturate", {{"x", number(2.0)}}));
    REQUIRE(saturated.ok());
    CHECK(saturated.root->value.components[0] == doctest::Approx(1.0));

    const auto absolute = compile(expression("abs", {{"x", number(-3.0)}}));
    REQUIRE(absolute.ok());
    CHECK(absolute.root->value.components[0] == doctest::Approx(3.0));

    const auto sine = compile(expression("sin", {{"x", number(0.0)}}));
    REQUIRE(sine.ok());
    CHECK(sine.root->value.components[0] == doctest::Approx(0.0));
}

TEST_CASE("vfx expression compiles random noise and built-ins deterministically") {
    const auto random = compile(expression("random", {{"min", number(0.0)}, {"max", number(1.0)},
                                                       {"seed", number(7.0)}}));
    REQUIRE(random.ok());
    CHECK(static_cast<int>(random.root->type) == static_cast<int>(vfx::ExprType::Float));
    CHECK_FALSE(random.root->constant);
    CHECK(random.root->hlsl == "lerp(0.0, 1.0, VFX_RAND(7u))");
    const auto randomAgain = compile(expression("random", {{"min", number(0.0)}, {"max", number(1.0)},
                                                            {"seed", number(7.0)}}));
    REQUIRE(randomAgain.ok());
    CHECK(randomAgain.root->hlsl == random.root->hlsl);

    const auto noise = compile(expression("noise", {{"position", attributeValue("position")},
                                                     {"frequency", number(2.0)},
                                                     {"seed", attributeValue("seed")}}));
    REQUIRE(noise.ok());
    CHECK(static_cast<int>(noise.root->type) == static_cast<int>(vfx::ExprType::Float));
    CHECK_FALSE(noise.root->constant);
    CHECK(noise.root->hlsl == "VFX_NOISE(VFX_ATTR(position), 2.0, VFX_ATTR(seed))");

    const auto time = compile(expression("time", {}));
    REQUIRE(time.ok());
    CHECK(time.root->hlsl == "VFX_TIME");
    const auto dt = compile(expression("dt", {}));
    REQUIRE(dt.ok());
    CHECK(dt.root->hlsl == "VFX_DT");
    const auto ratio = compile(expression("lifetime_ratio", {}));
    REQUIRE(ratio.ok());
    CHECK(ratio.root->hlsl == "VFX_LIFETIME_RATIO");
}

TEST_CASE("vfx expression rejects texture sampling before V1.1") {
    const auto texture = compile(expression("sample_texture", {{"texture", [] {
                                                                     vfx::Value value;
                                                                     value.kind = vfx::Value::Kind::String;
                                                                     value.string = "MainTex";
                                                                     return value;
                                                                 }()},
                                                                    {"uv", number(0.0)}}));
    REQUIRE_FALSE(texture.ok());
    REQUIRE_FALSE(texture.errors.empty());
    CHECK(texture.errors[0].path == "root");
    CHECK(texture.errors[0].message.find("V1.1") != std::string::npos);
}

TEST_CASE("vfx expression samples curve and gradient properties") {
    const auto curve = compile(expression("sample_curve", {{"curve", [] {
                                                                vfx::Value value;
                                                                value.kind = vfx::Value::Kind::String;
                                                                value.string = "SizeCurve";
                                                                return value;
                                                            }()},
                                                           {"t", expression("lifetime_ratio", {})}}));
    REQUIRE(curve.ok());
    CHECK(static_cast<int>(curve.root->type) == static_cast<int>(vfx::ExprType::Float));
    CHECK_FALSE(curve.root->constant);
    CHECK(curve.root->hlsl == "VFX_SAMPLE_CURVE(SizeCurve, VFX_LIFETIME_RATIO)");

    const auto gradient = compile(expression("sample_gradient", {{"gradient", [] {
                                                                      vfx::Value value;
                                                                      value.kind = vfx::Value::Kind::String;
                                                                      value.string = "ColorRamp";
                                                                      return value;
                                                                  }()},
                                                                 {"t", number(0.5)}}));
    REQUIRE(gradient.ok());
    CHECK(static_cast<int>(gradient.root->type) == static_cast<int>(vfx::ExprType::Float4));
    CHECK(gradient.root->hlsl == "VFX_SAMPLE_GRADIENT(ColorRamp, 0.5)");
}

TEST_CASE("vfx expression reports argument and array errors") {
    const auto missing = compile(expression("add", {{"a", number(1.0)}}));
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.errors[0].path == "root.b");

    const auto unknownArgument = compile(expression("time", {{"x", number(1.0)}}));
    REQUIRE_FALSE(unknownArgument.ok());
    CHECK(unknownArgument.errors[0].path == "root.x");

    const auto empty = compile(array({}));
    REQUIRE_FALSE(empty.ok());
    CHECK(empty.errors[0].message.find("empty") != std::string::npos);

    const auto nonNumber = compile([] {
        vfx::Value value;
        value.kind = vfx::Value::Kind::Array;
        vfx::Value item;
        item.kind = vfx::Value::Kind::String;
        item.string = "x";
        value.array.push_back(std::move(item));
        return value;
    }());
    REQUIRE_FALSE(nonNumber.ok());
    CHECK(nonNumber.errors[0].path == "root[0]");
}

TEST_CASE("vfx expression dag detects cycles") {
    auto root = std::make_shared<vfx::ExpressionNode>();
    auto shared = std::make_shared<vfx::ExpressionNode>();
    root->inputs.push_back(shared);
    root->inputs.push_back(shared);
    CHECK(vfx::isExpressionDagAcyclic(*root));

    root->inputs.push_back(root);
    CHECK_FALSE(vfx::isExpressionDagAcyclic(*root));
    root->inputs.clear();
}

TEST_CASE("vfx expression rejects excessive nesting") {
    vfx::Value nested = number(1.0);
    for (int i = 0; i < 70; ++i) {
        nested = expression("add", {{"a", std::move(nested)}, {"b", number(1.0)}});
    }
    const auto result = compile(nested);
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].message.find("nesting") != std::string::npos);
}
