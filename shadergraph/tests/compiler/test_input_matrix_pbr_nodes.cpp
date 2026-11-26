#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

namespace {

struct NodeExpectation {
    const char* type;
    const char* category;
    std::vector<std::string> settings;
    std::vector<std::string> synonyms;
    int slots;
};

const NodeExpectation kNodes[] = {
    {"Matrix 2x2", "Input/Matrix", {"row0", "row1"}, {}, 1},
    {"Matrix 3x3", "Input/Matrix", {"row0", "row1", "row2"}, {}, 1},
    {"Matrix 4x4", "Input/Matrix", {"row0", "row1", "row2", "row3"}, {}, 1},
    {"Transformation Matrix", "Input/Matrix", {"matrixType"}, {}, 1},
    {"Dielectric Specular", "Input/PBR", {"material", "range", "indexOfRefraction"},
     {"reflectance"}, 1},
    {"Metal Reflectance", "Input/PBR", {"material"}, {}, 1},
};

sg::detail::EmittedGraph emitGraph(const sg::NodeRegistry& registry, sg::Graph& graph) {
    REQUIRE(sg::concretize(graph, registry).ok);
    const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
    REQUIRE(target != nullptr);
    sg::detail::ActiveGraph active = sg::detail::collectActiveNodes(graph, registry, *target);
    sg::detail::PropertySet properties = sg::detail::collectProperties(graph, active, *target);
    REQUIRE(properties.ok);
    sg::detail::KeywordSet keywords =
        sg::detail::collectKeywords(graph, *target, sg::VariantPolicy::Used, 64);
    REQUIRE(keywords.ok);
    REQUIRE(keywords.variants.size() == 1);
    return sg::detail::emitSurface(graph, registry, active, *target, properties, keywords,
                                   keywords.variants[0]);
}

sg::detail::EmittedGraph emitNode(const sg::NodeRegistry& registry, const char* type,
                                  std::map<std::string, sg::Value> settings = {}) {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::NodeInstance node = registry.instantiate(type, 1);
    for (auto& [key, value] : settings) {
        node.settings[key] = std::move(value);
    }
    graph.nodes.push_back(std::move(node));
    return emitGraph(registry, graph);
}

} // namespace

TEST_CASE("SG-6 Input/Matrix and PBR nodes expose canonical metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->title == expected.type);
        CHECK(definition->settings == expected.settings);
        CHECK(definition->synonyms == expected.synonyms);
        CHECK(static_cast<int>(definition->slots.size()) == expected.slots);
    }

    const sg::NodeDef* matrix2 = registry.find("Matrix 2x2");
    REQUIRE(matrix2 != nullptr);
    CHECK(static_cast<int>(matrix2->slots[0].type) == static_cast<int>(sg::ValueType::Matrix2));
    const sg::NodeDef* matrix3 = registry.find("Matrix 3x3");
    REQUIRE(matrix3 != nullptr);
    CHECK(static_cast<int>(matrix3->slots[0].type) == static_cast<int>(sg::ValueType::Matrix3));
    const sg::NodeDef* matrix4 = registry.find("Matrix 4x4");
    REQUIRE(matrix4 != nullptr);
    CHECK(static_cast<int>(matrix4->slots[0].type) == static_cast<int>(sg::ValueType::Matrix4));
    const sg::NodeDef* transform = registry.find("Transformation Matrix");
    REQUIRE(transform != nullptr);
    CHECK(static_cast<int>(transform->slots[0].type) == static_cast<int>(sg::ValueType::Matrix4));
    CHECK_FALSE(transform->hasPreview);
}

TEST_CASE("SG-6 Matrix nodes emit row-major values as GLSL matrices") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    std::map<std::string, sg::Value> matrix2;
    matrix2["row0"] = sg::Value::makeVector2({1.0f, 2.0f});
    matrix2["row1"] = sg::Value::makeVector2({3.0f, 4.0f});
    const sg::detail::EmittedGraph two =
        emitNode(registry, "Matrix 2x2", std::move(matrix2));
    REQUIRE(two.ok);
    CHECK(two.source.find("n1_s0 = mat2(1.0, 3.0, 2.0, 4.0);") != std::string::npos);

    std::map<std::string, sg::Value> matrix3;
    matrix3["row0"] = sg::Value::makeVector3({1.0f, 2.0f, 3.0f});
    matrix3["row1"] = sg::Value::makeVector3({4.0f, 5.0f, 6.0f});
    matrix3["row2"] = sg::Value::makeVector3({7.0f, 8.0f, 9.0f});
    const sg::detail::EmittedGraph three =
        emitNode(registry, "Matrix 3x3", std::move(matrix3));
    REQUIRE(three.ok);
    CHECK(three.source.find("n1_s0 = mat3(1.0, 4.0, 7.0, 2.0, 5.0, 8.0, 3.0, 6.0, 9.0);") !=
          std::string::npos);

    std::map<std::string, sg::Value> matrix4;
    matrix4["row0"] = sg::Value::makeVector4({1.0f, 2.0f, 3.0f, 4.0f});
    matrix4["row1"] = sg::Value::makeVector4({5.0f, 6.0f, 7.0f, 8.0f});
    matrix4["row2"] = sg::Value::makeVector4({9.0f, 10.0f, 11.0f, 12.0f});
    matrix4["row3"] = sg::Value::makeVector4({13.0f, 14.0f, 15.0f, 16.0f});
    const sg::detail::EmittedGraph four =
        emitNode(registry, "Matrix 4x4", std::move(matrix4));
    REQUIRE(four.ok);
    CHECK(four.source.find(
              "n1_s0 = mat4(1.0, 5.0, 9.0, 13.0, 2.0, 6.0, 10.0, 14.0, 3.0, 7.0, 11.0, "
              "15.0, 4.0, 8.0, 12.0, 16.0);") != std::string::npos);
}

TEST_CASE("SG-6 Transformation Matrix maps Unity matrix types to engine uniforms") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const std::pair<const char*, const char*> cases[] = {
        {"Model", "n1_s0 = u_model[0];"},
        {"InverseModel", "n1_s0 = u_sg_modelInverse;"},
        {"View", "n1_s0 = u_global_view;"},
        {"InverseView", "n1_s0 = u_global_invView;"},
        {"Projection", "n1_s0 = u_global_proj;"},
        {"InverseProjection", "n1_s0 = u_global_invProj;"},
        {"ViewProjection", "n1_s0 = u_global_viewProj;"},
        {"InverseViewProjection", "n1_s0 = u_global_invViewProj;"},
    };
    for (const auto& [setting, expected] : cases) {
        CAPTURE(setting);
        std::map<std::string, sg::Value> settings;
        settings["matrixType"] = sg::Value::makeString(sg::ValueType::String, setting);
        const sg::detail::EmittedGraph emitted =
            emitNode(registry, "Transformation Matrix", std::move(settings));
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(expected) != std::string::npos);
    }
}

TEST_CASE("SG-6 Dielectric Specular emits presets, common range and custom IOR") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph common = emitNode(registry, "Dielectric Specular");
    REQUIRE(common.ok);
    CHECK(common.source.find("n1_s0 = lerp(0.034, 0.048, 0.5);") != std::string::npos);

    std::map<std::string, sg::Value> water;
    water["material"] = sg::Value::makeString(sg::ValueType::String, "Water");
    const sg::detail::EmittedGraph waterEmitted =
        emitNode(registry, "Dielectric Specular", std::move(water));
    REQUIRE(waterEmitted.ok);
    CHECK(waterEmitted.source.find("n1_s0 = 0.02;") != std::string::npos);

    std::map<std::string, sg::Value> custom;
    custom["material"] = sg::Value::makeString(sg::ValueType::String, "Custom");
    custom["indexOfRefraction"] = sg::Value::makeFloat(1.5f);
    const sg::detail::EmittedGraph customEmitted =
        emitNode(registry, "Dielectric Specular", std::move(custom));
    REQUIRE(customEmitted.ok);
    CHECK(customEmitted.source.find(
              "n1_s0 = pow(1.5 - 1.0, 2.0) / pow(1.5 + 1.0, 2.0);") != std::string::npos);
}

TEST_CASE("SG-6 Metal Reflectance emits Unity material constants") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph iron = emitNode(registry, "Metal Reflectance");
    REQUIRE(iron.ok);
    CHECK(iron.source.find("n1_s0 = vec3(0.56, 0.57, 0.58);") != std::string::npos);

    std::map<std::string, sg::Value> gold;
    gold["material"] = sg::Value::makeString(sg::ValueType::String, "Gold");
    const sg::detail::EmittedGraph goldEmitted =
        emitNode(registry, "Metal Reflectance", std::move(gold));
    REQUIRE(goldEmitted.ok);
    CHECK(goldEmitted.source.find("n1_s0 = vec3(1.0, 0.766, 0.336);") != std::string::npos);
}
