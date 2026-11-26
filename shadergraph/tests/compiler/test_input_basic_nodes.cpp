#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"
#include "sg/serialize.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace {

struct NodeExpectation {
    const char* type;
    const char* category;
    sg::ValueType outputType;
    std::vector<std::string> settings;
    std::vector<std::string> synonyms;
};

const NodeExpectation kNodes[] = {
    {"Boolean", "Input/Basic", sg::ValueType::Boolean, {"value"},
     {"switch", "true", "false", "on", "off"}},
    {"Color", "Input/Basic", sg::ValueType::Vector4, {"value"}, {"rgba"}},
    {"Float", "Input/Basic", sg::ValueType::Float, {"value"},
     {"Vector 1", "1", "v1", "vec1", "scalar"}},
    {"Integer", "Input/Basic", sg::ValueType::Float, {"value"}, {"whole number"}},
    {"Slider", "Input/Basic", sg::ValueType::Float, {"value"}, {}},
    {"Time", "Input/Basic", sg::ValueType::Float, {}, {}},
    {"Vector 2", "Input/Basic", sg::ValueType::Vector2, {"value"},
     {"2", "v2", "vec2", "float2"}},
    {"Vector 3", "Input/Basic", sg::ValueType::Vector3, {"value"},
     {"3", "v3", "vec3", "float3"}},
    {"Vector 4", "Input/Basic", sg::ValueType::Vector4, {"value"},
     {"4", "v4", "vec4", "float4"}},
    {"Constant", "Input/Basic", sg::ValueType::Float, {"constant"},
     {"pi", "tau", "phi"}},
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

sg::Graph graphWithNode(const sg::NodeRegistry& registry, sg::NodeInstance node) {
    (void)registry;
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    graph.nodes.push_back(std::move(node));
    return graph;
}

sg::detail::EmittedGraph emitSingle(const sg::NodeRegistry& registry, const char* type) {
    sg::Graph graph = graphWithNode(registry, registry.instantiate(type, 1));
    return emitGraph(registry, graph);
}

sg::detail::EmittedGraph emitWithSetting(const sg::NodeRegistry& registry, const char* type,
                                         const char* key, sg::Value value) {
    sg::NodeInstance node = registry.instantiate(type, 1);
    node.settings[key] = std::move(value);
    sg::Graph graph = graphWithNode(registry, std::move(node));
    return emitGraph(registry, graph);
}

} // namespace

TEST_CASE("SG-6 Input/Basic nodes expose canonical basic metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->title == expected.type);
        CHECK(definition->settings == expected.settings);
        CHECK(definition->synonyms == expected.synonyms);
        REQUIRE_FALSE(definition->slots.empty());
        const sg::SlotSpec& output = definition->slots.back();
        REQUIRE(static_cast<int>(output.direction) ==
                static_cast<int>(sg::SlotDirection::Output));
        CHECK(static_cast<int>(output.type) == static_cast<int>(expected.outputType));
    }

    const sg::NodeDef* time = registry.find("Time");
    REQUIRE(time != nullptr);
    REQUIRE(time->slots.size() == 5);
    CHECK(time->slots[0].name == "Time");
    CHECK(time->slots[1].name == "Sine Time");
    CHECK(time->slots[2].name == "Cosine Time");
    CHECK(time->slots[3].name == "Delta Time");
    CHECK(time->slots[4].name == "Smooth Delta");
}

TEST_CASE("SG-6 Input/Basic nodes emit canonical defaults") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph boolean = emitSingle(registry, "Boolean");
    REQUIRE(boolean.ok);
    CHECK(boolean.source.find("n1_s0 = false;") != std::string::npos);

    const sg::detail::EmittedGraph color = emitSingle(registry, "Color");
    REQUIRE(color.ok);
    CHECK(color.source.find("n1_s0 = vec4(0.0, 0.0, 0.0, 0.0);") != std::string::npos);

    for (const char* type : {"Float", "Integer", "Slider"}) {
        CAPTURE(type);
        const sg::detail::EmittedGraph emitted = emitSingle(registry, type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find("n1_s0 = 0.0;") != std::string::npos);
    }

    const sg::detail::EmittedGraph vector2 = emitSingle(registry, "Vector 2");
    REQUIRE(vector2.ok);
    CHECK(vector2.source.find("n1_s0 = vec2(0.0, 0.0);") != std::string::npos);
    const sg::detail::EmittedGraph vector3 = emitSingle(registry, "Vector 3");
    REQUIRE(vector3.ok);
    CHECK(vector3.source.find("n1_s0 = vec3(0.0, 0.0, 0.0);") != std::string::npos);
    const sg::detail::EmittedGraph vector4 = emitSingle(registry, "Vector 4");
    REQUIRE(vector4.ok);
    CHECK(vector4.source.find("n1_s0 = vec4(0.0, 0.0, 0.0, 0.0);") != std::string::npos);

    const sg::detail::EmittedGraph constant = emitSingle(registry, "Constant");
    REQUIRE(constant.ok);
    CHECK(constant.source.find("n1_s0 = 3.14159") != std::string::npos);

    const sg::detail::EmittedGraph time = emitSingle(registry, "Time");
    REQUIRE(time.ok);
    CHECK(time.source.find("n1_s0 = gi.time;") != std::string::npos);
    CHECK(time.source.find("n1_s1 = sin(gi.time);") != std::string::npos);
    CHECK(time.source.find("n1_s2 = cos(gi.time);") != std::string::npos);
    CHECK(time.source.find("n1_s3 = gi.deltaTime;") != std::string::npos);
    CHECK(time.source.find("n1_s4 = gi.deltaTime;") != std::string::npos);
}

TEST_CASE("SG-6 Input/Basic nodes emit configured values") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph boolean =
        emitWithSetting(registry, "Boolean", "value", sg::Value::makeBool(true));
    REQUIRE(boolean.ok);
    CHECK(boolean.source.find("n1_s0 = true;") != std::string::npos);

    const sg::detail::EmittedGraph color = emitWithSetting(
        registry, "Color", "value", sg::Value::makeVector4({0.25f, 0.5f, 0.75f, 1.0f}));
    REQUIRE(color.ok);
    CHECK(color.source.find("n1_s0 = vec4(0.25, 0.5, 0.75, 1.0);") != std::string::npos);

    const sg::detail::EmittedGraph floating =
        emitWithSetting(registry, "Float", "value", sg::Value::makeFloat(2.5f));
    REQUIRE(floating.ok);
    CHECK(floating.source.find("n1_s0 = 2.5;") != std::string::npos);

    const sg::detail::EmittedGraph integer =
        emitWithSetting(registry, "Integer", "value", sg::Value::makeInteger(7));
    REQUIRE(integer.ok);
    CHECK(integer.source.find("n1_s0 = 7.0;") != std::string::npos);

    const sg::detail::EmittedGraph slider = emitWithSetting(
        registry, "Slider", "value", sg::Value::makeVector3({0.25f, 0.0f, 2.0f}));
    REQUIRE(slider.ok);
    CHECK(slider.source.find("n1_s0 = 0.25;") != std::string::npos);

    const sg::detail::EmittedGraph vector2 = emitWithSetting(
        registry, "Vector 2", "value", sg::Value::makeVector2({1.0f, 2.0f}));
    REQUIRE(vector2.ok);
    CHECK(vector2.source.find("n1_s0 = vec2(1.0, 2.0);") != std::string::npos);

    const sg::detail::EmittedGraph vector3 = emitWithSetting(
        registry, "Vector 3", "value", sg::Value::makeVector3({1.0f, 2.0f, 3.0f}));
    REQUIRE(vector3.ok);
    CHECK(vector3.source.find("n1_s0 = vec3(1.0, 2.0, 3.0);") != std::string::npos);

    const sg::detail::EmittedGraph vector4 = emitWithSetting(
        registry, "Vector 4", "value", sg::Value::makeVector4({1.0f, 2.0f, 3.0f, 4.0f}));
    REQUIRE(vector4.ok);
    CHECK(vector4.source.find("n1_s0 = vec4(1.0, 2.0, 3.0, 4.0);") != std::string::npos);
}

TEST_CASE("SG-6 Vector nodes expose Unity component input slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const auto check = [&](const char* type, std::vector<const char*> components) {
        CAPTURE(type);
        const sg::NodeDef* definition = registry.find(type);
        REQUIRE(definition != nullptr);
        REQUIRE(definition->slots.size() == components.size() + 1);
        for (std::size_t index = 0; index < components.size(); ++index) {
            const sg::SlotSpec& slot = definition->slots[index];
            CHECK(slot.id == static_cast<sg::SlotId>(index + 1));
            CHECK(slot.name == components[index]);
            CHECK(static_cast<int>(slot.direction) == static_cast<int>(sg::SlotDirection::Input));
            CHECK(static_cast<int>(slot.type) == static_cast<int>(sg::ValueType::Float));
        }
        CHECK(definition->slots.back().id == 0);
        CHECK(static_cast<int>(definition->slots.back().direction) == static_cast<int>(sg::SlotDirection::Output));
    };

    check("Vector 2", {"X", "Y"});
    check("Vector 3", {"X", "Y", "Z"});
    check("Vector 4", {"X", "Y", "Z", "W"});
}

TEST_CASE("SG-6 Vector nodes combine connected component inputs") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.target.id = "lit";
    graph.nodes.push_back(registry.instantiate("Vector 4", 1));
    const float values[] = {0.1f, 0.2f, 0.3f, 0.4f};
    for (sg::SlotId slot = 1; slot <= 4; ++slot) {
        const sg::NodeId id = static_cast<sg::NodeId>(slot + 1);
        sg::NodeInstance component = registry.instantiate("Float", id);
        component.settings["value"] = sg::Value::makeFloat(values[slot - 1]);
        graph.nodes.push_back(std::move(component));
        REQUIRE(graph.addEdge({id, 0, 1, slot}));
    }

    REQUIRE(graph.incomingEdge(1, 1) != nullptr);
    REQUIRE(graph.incomingEdge(1, 4) != nullptr);
    REQUIRE(sg::concretize(graph, registry).ok);
    const sg::NodeInstance* componentVector = graph.findNode(1);
    REQUIRE(componentVector != nullptr);
    REQUIRE(componentVector->findSlot(1) != nullptr);
    CHECK(componentVector->findSlot(1)->connected);
    CHECK(static_cast<int>(componentVector->findSlot(1)->state.mode) ==
          static_cast<int>(sg::SlotMode::Connected));
    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(graph.incomingEdge(1, 1) != nullptr);
    CHECK(emitted.source.find("n1_s0 = vec4(n2_s0, n3_s0, n4_s0, n5_s0);") !=
          std::string::npos);
}

TEST_CASE("SG-6 Vector components prefer explicit literals over the value setting") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::NodeInstance vector = registry.instantiate("Vector 4", 1);
    vector.settings["value"] = sg::Value::makeVector4({1.0f, 2.0f, 3.0f, 4.0f});
    sg::SlotInstance* y = vector.findSlot(2);
    REQUIRE(y != nullptr);
    y->state.mode = sg::SlotMode::Literal;
    y->state.value = sg::Value::makeFloat(9.0f);

    sg::Graph graph = graphWithNode(registry, std::move(vector));
    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("n1_s0 = vec4(1.0, 9.0, 3.0, 4.0);") !=
          std::string::npos);
}

TEST_CASE("SG-6 Vector nodes emit the checked-in component graph") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/input_vector_components.sgraph.json",
                      registry);
    REQUIRE(loaded.ok);
    const sg::detail::EmittedGraph emitted = emitGraph(registry, loaded.graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("n1_s0 = vec4(n2_s0, n3_s0, n4_s0, n5_s0);") !=
          std::string::npos);
}

TEST_CASE("SG-6 Constant supports every math constant") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const std::vector<std::pair<const char*, const char*>> constants = {
        {"pi", "3.14159"},
        {"tau", "6.283185"},
        {"phi", "1.618034"},
        {"e", "2.71828"},
        {"sqrt2", "1.41421"},
    };
    for (const auto& [setting, fragment] : constants) {
        CAPTURE(setting);
        const sg::detail::EmittedGraph emitted = emitWithSetting(
            registry, "Constant", "constant",
            sg::Value::makeString(sg::ValueType::String, setting));
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(fragment) != std::string::npos);
    }
}
