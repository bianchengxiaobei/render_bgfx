#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"
#include "sg/serialize.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

sg::detail::EmittedGraph emitPrepared(sg::Graph& graph, const sg::NodeRegistry& registry) {
    std::vector<sg::Diagnostic> prep;
    REQUIRE(sg::detail::preparePropertyNodes(graph, prep));
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

void checkPropertyType(const sg::NodeRegistry& registry, const char* name, sg::ValueType type,
                       sg::Value value, const char* declaration, const char* expression) {
    CAPTURE(name);
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property property;
    property.name = "Amount";
    property.refName = "_Amount";
    property.type = type;
    property.defaultValue = std::move(value);
    REQUIRE(graph.addProperty(std::move(property)));
    sg::NodeInstance node = registry.instantiate("Property", 1);
    node.settings["property"] = sg::Value::makeString(sg::ValueType::String, "_Amount");
    REQUIRE(graph.addNode(std::move(node)));

    const sg::detail::EmittedGraph emitted = emitPrepared(graph, registry);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("uniform vec4 u_sg__Amount;") != std::string::npos);
    CHECK(emitted.source.find(std::string(declaration) + " n1_s0;") != std::string::npos);
    CHECK(emitted.source.find(std::string("n1_s0 = ") + expression + ";") != std::string::npos);
}

} // namespace

TEST_CASE("Property node exposes Unity Input/Property metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::NodeDef* definition = registry.find("Property");
    REQUIRE(definition != nullptr);
    CHECK(definition->category == "Input");
    CHECK(definition->title == "Property");
    CHECK(definition->allowInMainGraph);
    CHECK(definition->allowInSubGraph);
    CHECK(definition->settings == std::vector<std::string>{"property"});
    REQUIRE(definition->slots.size() == 1);
    CHECK(static_cast<int>(definition->slots.front().direction) ==
          static_cast<int>(sg::SlotDirection::Output));
    CHECK(definition->slots.front().name == "Out");
    CHECK(static_cast<int>(definition->slots.front().type) ==
          static_cast<int>(sg::ValueType::DynamicVector));
}

TEST_CASE("Property node emits scalar vector and color property types") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    checkPropertyType(registry, "Float", sg::ValueType::Float, sg::Value::makeFloat(0.5f), "float",
                      "u_sg__Amount.x");
    checkPropertyType(registry, "Boolean", sg::ValueType::Boolean, sg::Value::makeBool(true),
                      "bool", "bool(u_sg__Amount.x)");
    checkPropertyType(registry, "Integer", sg::ValueType::Integer, sg::Value::makeInteger(2),
                      "int", "int(u_sg__Amount.x)");
    checkPropertyType(registry, "Vector2", sg::ValueType::Vector2,
                      sg::Value::makeVector2({1.0f, 2.0f}), "vec2", "u_sg__Amount.xy");
    checkPropertyType(registry, "Vector3", sg::ValueType::Vector3,
                      sg::Value::makeVector3({1.0f, 2.0f, 3.0f}), "vec3",
                      "u_sg__Amount.xyz");
    checkPropertyType(registry, "Vector4", sg::ValueType::Vector4,
                      sg::Value::makeVector4({1.0f, 2.0f, 3.0f, 4.0f}), "vec4",
                      "u_sg__Amount");
    checkPropertyType(registry, "Color", sg::ValueType::Color,
                      sg::Value::makeColor({1.0f, 0.5f, 0.25f, 1.0f}), "vec4", "u_sg__Amount");
}

TEST_CASE("Property node emits matrix property outputs") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const auto checkMatrix = [&](sg::ValueType type, sg::Value value, const char* declaration,
                                 const char* expression) {
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";
        sg::Property property;
        property.name = "Matrix";
        property.refName = "_Matrix";
        property.type = type;
        property.defaultValue = std::move(value);
        REQUIRE(graph.addProperty(std::move(property)));
        sg::NodeInstance node = registry.instantiate("Property", 1);
        node.settings["property"] = sg::Value::makeString(sg::ValueType::String, "_Matrix");
        REQUIRE(graph.addNode(std::move(node)));

        const sg::detail::EmittedGraph emitted = emitPrepared(graph, registry);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find("uniform mat4 u_sg__Matrix;") != std::string::npos);
        CHECK(emitted.source.find(std::string(declaration) + " n1_s0;") != std::string::npos);
        CHECK(emitted.source.find(std::string("n1_s0 = ") + expression + ";") !=
              std::string::npos);
    };

    checkMatrix(sg::ValueType::Matrix2,
                sg::Value::makeMatrix2(sg::Mat2{{1.0f, 2.0f, 3.0f, 4.0f}}), "mat2",
                "mat2(u_sg__Matrix[0].xy, u_sg__Matrix[0].zw)");
    checkMatrix(sg::ValueType::Matrix3,
                sg::Value::makeMatrix3(sg::Mat3{{1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f}}),
                "mat3",
                "mat3(u_sg__Matrix[0].xyz, vec3(u_sg__Matrix[0].w, u_sg__Matrix[1].xy), "
                "vec3(u_sg__Matrix[1].zw, u_sg__Matrix[2].x))");
    checkMatrix(sg::ValueType::Matrix4, sg::Value::makeMatrix4(sg::Mat4::identity()), "mat4",
                "u_sg__Matrix");

    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property property;
    property.name = "Matrix";
    property.refName = "_Matrix";
    property.type = sg::ValueType::Matrix4;
    property.defaultValue = sg::Value::makeMatrix4(sg::Mat4::identity());
    REQUIRE(graph.addProperty(std::move(property)));
    sg::NodeInstance node = registry.instantiate("Property", 1);
    node.settings["property"] = sg::Value::makeString(sg::ValueType::String, "_Matrix");
    REQUIRE(graph.addNode(std::move(node)));
    REQUIRE(graph.addNode(registry.instantiate("Matrix Split", 2)));
    REQUIRE(graph.addEdge({1, 0, 2, 0}));
    const sg::detail::EmittedGraph split = emitPrepared(graph, registry);
    REQUIRE(split.ok);
    CHECK(split.source.find("n2_s1 = n1_s0[0];") != std::string::npos);
}

TEST_CASE("Property node emits gradient property outputs") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Gradient gradient;
    gradient.keys.push_back({0.0f, sg::Vec4{1.0f, 0.0f, 0.0f, 1.0f}});
    gradient.keys.push_back({1.0f, sg::Vec4{0.0f, 0.0f, 1.0f, 1.0f}});
    sg::Property property;
    property.name = "Gradient";
    property.refName = "_Gradient";
    property.type = sg::ValueType::Gradient;
    property.defaultValue = sg::Value::makeGradient(gradient);
    REQUIRE(graph.addProperty(std::move(property)));

    sg::NodeInstance propertyNode = registry.instantiate("Property", 1);
    propertyNode.settings["property"] =
        sg::Value::makeString(sg::ValueType::String, "_Gradient");
    REQUIRE(graph.addNode(std::move(propertyNode)));
    REQUIRE(graph.addNode(registry.instantiate("Sample Gradient", 2)));
    sg::NodeInstance time = registry.instantiate("Float", 3);
    time.settings["value"] = sg::Value::makeFloat(0.5f);
    REQUIRE(graph.addNode(std::move(time)));
    REQUIRE(graph.addEdge({1, 0, 2, 0}));
    REQUIRE(graph.addEdge({3, 0, 2, 1}));

    const sg::detail::EmittedGraph emitted = emitPrepared(graph, registry);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("HbgSGGradient n1_s0;") != std::string::npos);
    CHECK(emitted.source.find(
              "n1_s0 = hbgSGNewGradient(0, 2, 2, vec4(1.0, 0.0, 0.0, 0.0), "
              "vec4(0.0, 0.0, 1.0, 1.0),") != std::string::npos);
    CHECK(emitted.source.find("n2_s2 = hbgSGSampleGradient(n1_s0, n3_s0);") !=
          std::string::npos);
}

TEST_CASE("Property node emits the checked-in property_gradient graph") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/property_gradient.sgraph.json", registry);
    REQUIRE(loaded.ok);
    const sg::detail::EmittedGraph emitted = emitPrepared(loaded.graph, registry);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("HbgSGGradient n1_s0;") != std::string::npos);
    CHECK(emitted.source.find("n2_s2 = hbgSGSampleGradient(n1_s0, n3_s0);") !=
          std::string::npos);
}

TEST_CASE("Property node outputs Texture2D for Sample Texture 2D") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    texture.defaultValue =
        sg::Value::makeString(sg::ValueType::Texture2D, "textures/grid.png");
    REQUIRE(graph.addProperty(std::move(texture)));

    sg::NodeInstance propertyNode = registry.instantiate("Property", 1);
    propertyNode.settings["property"] =
        sg::Value::makeString(sg::ValueType::String, "_MainTex");
    REQUIRE(graph.addNode(std::move(propertyNode)));
    REQUIRE(graph.addNode(registry.instantiate("Sample Texture 2D", 2)));
    REQUIRE(graph.addNode(registry.instantiate("UV", 3)));
    REQUIRE(graph.addNode(registry.instantiate("BlockBaseColor", 100)));
    REQUIRE(graph.addEdge({1, 0, 2, 1}));
    REQUIRE(graph.addEdge({3, 0, 2, 2}));
    REQUIRE(graph.addEdge({2, 0, 100, 0}));

    const sg::detail::EmittedGraph emitted = emitPrepared(graph, registry);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("SAMPLER2D(s_sg__MainTex, 0);") != std::string::npos);
    CHECK(emitted.source.find(
              "t_sample_2 = hbgSGSampleTexture2DBias(s_sg__MainTex, n3_s0, "
              "u_sg_globalMipBias.x);") != std::string::npos);
    CHECK(emitted.source.find("n2_s0 = t_sample_2;") != std::string::npos);
}

TEST_CASE("Property node outputs Texture2D for Sample Texture 2D LOD") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    texture.defaultValue =
        sg::Value::makeString(sg::ValueType::Texture2D, "textures/grid.png");
    REQUIRE(graph.addProperty(std::move(texture)));

    sg::NodeInstance propertyNode = registry.instantiate("Property", 1);
    propertyNode.settings["property"] =
        sg::Value::makeString(sg::ValueType::String, "_MainTex");
    REQUIRE(graph.addNode(std::move(propertyNode)));
    REQUIRE(graph.addNode(registry.instantiate("Sample Texture 2D LOD", 2)));
    sg::NodeInstance lod = registry.instantiate("Float", 3);
    lod.settings["value"] = sg::Value::makeFloat(1.0f);
    REQUIRE(graph.addNode(std::move(lod)));
    REQUIRE(graph.addNode(registry.instantiate("UV", 4)));
    REQUIRE(graph.addNode(registry.instantiate("BlockBaseColor", 100)));
    REQUIRE(graph.addEdge({1, 0, 2, 1}));
    REQUIRE(graph.addEdge({4, 0, 2, 2}));
    REQUIRE(graph.addEdge({3, 0, 2, 4}));
    REQUIRE(graph.addEdge({2, 0, 100, 0}));

    const sg::detail::EmittedGraph emitted = emitPrepared(graph, registry);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("t_sample_2 = texture2DLod(s_sg__MainTex, n4_s0, n3_s0);") !=
          std::string::npos);
    CHECK(emitted.source.find("n2_s0 = t_sample_2;") != std::string::npos);
}

TEST_CASE("Property node emits the checked-in property_texture graph") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/property_texture.sgraph.json", registry);
    REQUIRE(loaded.ok);
    const sg::detail::EmittedGraph emitted = emitPrepared(loaded.graph, registry);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("SAMPLER2D(s_sg__MainTex, 0);") != std::string::npos);
    CHECK(emitted.source.find(
              "t_sample_2 = hbgSGSampleTexture2DBias(s_sg__MainTex, n3_s0, "
              "u_sg_globalMipBias.x);") != std::string::npos);
    CHECK(emitted.source.find("n2_s0 = t_sample_2;") != std::string::npos);
}

TEST_CASE("Property node resolves reference aliases display name and precision") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property property;
    property.name = "Lookup";
    property.refName = "_Lookup";
    property.type = sg::ValueType::Vector3;
    property.precision = sg::Precision::Half;
    property.defaultValue = sg::Value::makeVector3({1.0f, 2.0f, 3.0f});
    REQUIRE(graph.addProperty(std::move(property)));

    for (int index = 0; index < 3; ++index) {
        const char* keys[] = {"property", "propertyRef", "refName"};
        sg::NodeInstance node = registry.instantiate("Property", static_cast<sg::NodeId>(index + 1));
        node.settings[keys[index]] = sg::Value::makeString(sg::ValueType::String, "_Lookup");
        REQUIRE(graph.addNode(std::move(node)));
    }

    std::vector<sg::Diagnostic> diagnostics;
    REQUIRE(sg::detail::preparePropertyNodes(graph, diagnostics));
    CHECK(diagnostics.empty());
    for (int index = 0; index < 3; ++index) {
        CAPTURE(index);
        sg::NodeInstance* node = graph.findNode(static_cast<sg::NodeId>(index + 1));
        REQUIRE(node != nullptr);
        const auto propertySetting = node->settings.find("property");
        REQUIRE(propertySetting != node->settings.end());
        REQUIRE(propertySetting->second.asString() != nullptr);
        CHECK(*propertySetting->second.asString() == "_Lookup");
        CHECK(node->settings.find("propertyRef") == node->settings.end());
        CHECK(node->settings.find("refName") == node->settings.end());
        CHECK(static_cast<int>(node->precision) == static_cast<int>(sg::Precision::Half));
        const sg::SlotInstance* output = node->findSlot(0);
        REQUIRE(output != nullptr);
        CHECK(static_cast<int>(output->spec.type) == static_cast<int>(sg::ValueType::Vector3));
        CHECK(output->spec.displayName == "Lookup");
        CHECK(static_cast<int>(output->concreteType) ==
              static_cast<int>(sg::ConcreteValueType::Vector3));
    }
}

TEST_CASE("Property node reports missing and unsupported property references") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    {
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";
        REQUIRE(graph.addNode(registry.instantiate("Property", 1)));
        std::vector<sg::Diagnostic> diagnostics;
        CHECK_FALSE(sg::detail::preparePropertyNodes(graph, diagnostics));
        CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const sg::Diagnostic& diagnostic) {
            return diagnostic.code == sg::kErrorMissingInput;
        }));
    }
    {
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";
        sg::Property texture;
        texture.name = "Texture";
        texture.refName = "_Texture";
        texture.type = sg::ValueType::String;
        texture.defaultValue =
            sg::Value::makeString(sg::ValueType::String, "unsupported");
        REQUIRE(graph.addProperty(std::move(texture)));
        sg::NodeInstance propertyNode = registry.instantiate("Property", 1);
        propertyNode.settings["property"] =
            sg::Value::makeString(sg::ValueType::String, "_Texture");
        REQUIRE(graph.addNode(std::move(propertyNode)));
        REQUIRE(graph.addNode(registry.instantiate("Add", 2)));
        REQUIRE(graph.addEdge({1, 0, 2, 0}));
        std::vector<sg::Diagnostic> diagnostics;
        CHECK_FALSE(sg::detail::preparePropertyNodes(graph, diagnostics));
        CHECK(std::any_of(diagnostics.begin(), diagnostics.end(), [](const sg::Diagnostic& diagnostic) {
            return diagnostic.code == sg::kErrorTypeMismatch &&
                   diagnostic.message.find("cannot be wired") != std::string::npos;
        }));
    }
}

TEST_CASE("Property node emits the checked-in property_float graph") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/property_float.sgraph.json", registry);
    REQUIRE(loaded.ok);
    const sg::detail::EmittedGraph emitted = emitPrepared(loaded.graph, registry);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("uniform vec4 u_sg__Amount;") != std::string::npos);
    CHECK(emitted.source.find("n1_s0 = u_sg__Amount.x;") != std::string::npos);
    CHECK(emitted.source.find("n2_s0 = 0.25;") != std::string::npos);
    CHECK(emitted.source.find("n3_s2 = n1_s0 + n2_s0;") != std::string::npos);
}
