#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"
#include "sg/serialize.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

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

sg::Graph makeGraph() {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    return graph;
}

sg::NodeInstance floatNode(const sg::NodeRegistry& registry, sg::NodeId id, float value) {
    sg::NodeInstance node = registry.instantiate("Float", id);
    node.settings["value"] = sg::Value::makeFloat(value);
    return node;
}

} // namespace

TEST_CASE("Switch node exposes Unity Utility/Logic metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::NodeDef* definition = registry.find("Switch");
    REQUIRE(definition != nullptr);
    CHECK(definition->category == "Utility/Logic");
    CHECK(definition->title == "Switch");
    CHECK(definition->allowInMainGraph);
    CHECK(definition->allowInSubGraph);
    CHECK(definition->settings == std::vector<std::string>{"cases", "floorPredicate"});
    REQUIRE(definition->slots.size() == 29);
    CHECK(definition->slots[0].id == 0);
    CHECK(static_cast<int>(definition->slots[0].type) ==
          static_cast<int>(sg::ValueType::Float));
    CHECK(definition->slots[1].id == 1);
    CHECK(static_cast<int>(definition->slots[1].type) ==
          static_cast<int>(sg::ValueType::DynamicVector));
    CHECK(definition->slots[2].id == 2);
    CHECK(static_cast<int>(definition->slots[2].direction) ==
          static_cast<int>(sg::SlotDirection::Output));
    CHECK(static_cast<bool>(definition->updateSlots));
}

TEST_CASE("Switch node emits a comparison function and case slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    sg::NodeInstance node = registry.instantiate("Switch", 1);
    node.settings["cases"] =
        sg::Value::makeString(sg::ValueType::String, "Low|less|0.33;High|greaterOrEqual|0.66");
    REQUIRE(graph.addNode(std::move(node)));
    REQUIRE(graph.addNode(floatNode(registry, 2, 0.5f)));
    REQUIRE(graph.addNode(floatNode(registry, 3, 0.1f)));
    REQUIRE(graph.addNode(floatNode(registry, 4, 0.2f)));
    REQUIRE(graph.addNode(floatNode(registry, 5, 0.8f)));
    REQUIRE(graph.addEdge({2, 0, 1, 0}));
    REQUIRE(graph.addEdge({3, 0, 1, 1}));
    REQUIRE(graph.addEdge({4, 0, 1, 3}));
    REQUIRE(graph.addEdge({5, 0, 1, 4}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("void hbgSG_SwitchNode_1(float predicate, float fallback, "
                              "float case_A, float case_B, out float result)") != std::string::npos);
    CHECK(emitted.source.find("if (floor(predicate) < 0.33") != std::string::npos);
    CHECK(emitted.source.find("else if (floor(predicate) >= 0.66") != std::string::npos);
    CHECK(emitted.source.find("result = case_A;") != std::string::npos);
    CHECK(emitted.source.find("result = case_B;") != std::string::npos);
    CHECK(emitted.source.find("hbgSG_SwitchNode_1(n2_s0, n3_s0, n4_s0, n5_s0, n1_s2);") !=
          std::string::npos);

    const sg::NodeInstance* switchNode = graph.findNode(1);
    REQUIRE(switchNode != nullptr);
    REQUIRE(switchNode->slots.size() == 5);
    CHECK(switchNode->findSlot(1)->spec.name == "Fallback");
    CHECK(switchNode->findSlot(3)->spec.displayName == "A");
    CHECK(switchNode->findSlot(4)->spec.displayName == "B");
    CHECK(static_cast<int>(switchNode->findSlot(2)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Float));
}

TEST_CASE("Switch node propagates vector width to the function signature") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    sg::NodeInstance node = registry.instantiate("Switch", 1);
    node.settings["cases"] = sg::Value::makeString(sg::ValueType::String, "A|equal|0");
    REQUIRE(graph.addNode(std::move(node)));
    REQUIRE(graph.addNode(floatNode(registry, 2, 0.0f)));
    sg::NodeInstance fallback = registry.instantiate("Vector 3", 3);
    fallback.settings["value"] = sg::Value::makeVector3({0.1f, 0.2f, 0.3f});
    sg::NodeInstance caseA = registry.instantiate("Vector 3", 4);
    caseA.settings["value"] = sg::Value::makeVector3({0.7f, 0.8f, 0.9f});
    REQUIRE(graph.addNode(std::move(fallback)));
    REQUIRE(graph.addNode(std::move(caseA)));
    REQUIRE(graph.addEdge({2, 0, 1, 0}));
    REQUIRE(graph.addEdge({3, 0, 1, 1}));
    REQUIRE(graph.addEdge({4, 0, 1, 3}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("void hbgSG_SwitchNode_1(float predicate, vec3 fallback, "
                              "vec3 case_A, out vec3 result)") != std::string::npos);
    CHECK(emitted.source.find("hbgSG_SwitchNode_1(n2_s0, n3_s0, n4_s0, n1_s2);") !=
          std::string::npos);
    const sg::NodeInstance* switchNode = graph.findNode(1);
    REQUIRE(switchNode != nullptr);
    CHECK(static_cast<int>(switchNode->findSlot(2)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Vector3));
}

TEST_CASE("Switch node without cases returns the fallback") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    REQUIRE(graph.addNode(registry.instantiate("Switch", 1)));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("void hbgSG_SwitchNode_1(float predicate, float fallback, "
                              "out float result)") != std::string::npos);
    CHECK(emitted.source.find("result = fallback;") != std::string::npos);
    CHECK(emitted.source.find("hbgSG_SwitchNode_1(") != std::string::npos);
    CHECK(emitted.source.find("if (floor(predicate)") == std::string::npos);

    const sg::NodeInstance* node = graph.findNode(1);
    REQUIRE(node != nullptr);
    CHECK(node->slots.size() == 3);
}

TEST_CASE("Switch node honors the floorPredicate setting") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    sg::NodeInstance node = registry.instantiate("Switch", 1);
    node.settings["cases"] = sg::Value::makeString(sg::ValueType::String, "A|equal|0");
    node.settings["floorPredicate"] = sg::Value::makeBool(false);
    REQUIRE(graph.addNode(std::move(node)));
    REQUIRE(graph.addNode(floatNode(registry, 2, 0.0f)));
    REQUIRE(graph.addNode(floatNode(registry, 3, 1.0f)));
    std::string edgeError;
    REQUIRE_MESSAGE(graph.addEdge({2, 0, 1, 0}, &edgeError), edgeError);
    REQUIRE_MESSAGE(graph.addEdge({3, 0, 1, 1}, &edgeError), edgeError);

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("if (predicate == 0.0)") != std::string::npos);
    CHECK(emitted.source.find("floor(predicate)") == std::string::npos);
}

TEST_CASE("Switch node rejects more than 26 cases") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    sg::NodeInstance node = registry.instantiate("Switch", 1);
    std::string cases;
    for (int index = 0; index < 27; ++index) {
        if (!cases.empty()) {
            cases += ";";
        }
        cases += "Case" + std::to_string(index) + "|equal|" + std::to_string(index);
    }
    node.settings["cases"] = sg::Value::makeString(sg::ValueType::String, cases);
    REQUIRE(graph.addNode(std::move(node)));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    CHECK_FALSE(emitted.ok);
    CHECK(std::any_of(emitted.diagnostics.begin(), emitted.diagnostics.end(),
                      [](const sg::Diagnostic& diagnostic) {
                          return diagnostic.code == sg::kErrorInvalidValue &&
                                 diagnostic.message.find("26 cases") != std::string::npos;
                      }));
    const sg::NodeInstance* switchNode = graph.findNode(1);
    REQUIRE(switchNode != nullptr);
    CHECK(switchNode->slots.size() == 29);
}

TEST_CASE("Preview node exposes Unity Utility/Preview metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::NodeDef* definition = registry.find("Preview");
    REQUIRE(definition != nullptr);
    CHECK(definition->category == "Utility");
    CHECK(definition->title == "Preview");
    CHECK(definition->hasPreview);
    CHECK(definition->allowInMainGraph);
    REQUIRE(definition->slots.size() == 2);
    CHECK(definition->slots[0].id == 0);
    CHECK(static_cast<int>(definition->slots[0].direction) ==
          static_cast<int>(sg::SlotDirection::Input));
    CHECK(definition->slots[1].id == 1);
    CHECK(static_cast<int>(definition->slots[1].direction) ==
          static_cast<int>(sg::SlotDirection::Output));
}

TEST_CASE("Preview node passes through the input value") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    REQUIRE(graph.addNode(registry.instantiate("Preview", 1)));
    REQUIRE(graph.addNode(floatNode(registry, 2, 0.7f)));
    REQUIRE(graph.addEdge({2, 0, 1, 0}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("n1_s1 = n2_s0;") != std::string::npos);

    const sg::NodeInstance* node = graph.findNode(1);
    REQUIRE(node != nullptr);
    CHECK(static_cast<int>(node->findSlot(1)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Float));
}

TEST_CASE("Switch node uses upstream float enum property cases") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();

    sg::Property mode;
    mode.name = "Mode";
    mode.refName = "_Mode";
    mode.type = sg::ValueType::Float;
    mode.floatMode = "enum";
    mode.enumNames = {"Low", "High"};
    mode.enumValues = {0, 3};
    mode.defaultValue = sg::Value::makeFloat(0.0f);
    REQUIRE(graph.addProperty(std::move(mode)));

    sg::NodeInstance property = registry.instantiate("Property", 2);
    property.settings["property"] = sg::Value::makeString(sg::ValueType::String, "_Mode");
    REQUIRE(graph.addNode(std::move(property)));
    REQUIRE(graph.addNode(registry.instantiate("Switch", 1)));
    REQUIRE(graph.addNode(floatNode(registry, 3, 0.25f)));
    REQUIRE(graph.addNode(floatNode(registry, 4, 0.5f)));
    REQUIRE(graph.addNode(floatNode(registry, 5, 0.75f)));
    REQUIRE(graph.addEdge({2, 0, 1, 0}));
    REQUIRE(graph.addEdge({3, 0, 1, 1}));
    REQUIRE(graph.addEdge({4, 0, 1, 3}));
    REQUIRE(graph.addEdge({5, 0, 1, 4}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    const sg::NodeInstance* switchNode = graph.findNode(1);
    REQUIRE(switchNode != nullptr);
    REQUIRE(switchNode->slots.size() == 5);
    CHECK(switchNode->findSlot(3)->spec.displayName == "Low");
    CHECK(switchNode->findSlot(4)->spec.displayName == "High");
    CHECK(emitted.source.find("if (floor(predicate) == 0.0)") != std::string::npos);
    CHECK(emitted.source.find("else if (floor(predicate) == 3.0)") != std::string::npos);
}

TEST_CASE("Switch node emits the checked-in switch_values graph") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/switch_values.sgraph.json", registry);
    REQUIRE(loaded.ok);
    const sg::detail::EmittedGraph emitted = emitGraph(registry, loaded.graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("hbgSG_SwitchNode_") != std::string::npos);
    CHECK(emitted.source.find("result = case_A;") != std::string::npos);
}

TEST_CASE("Switch node emits the checked-in switch_property graph") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/switch_property.sgraph.json", registry);
    REQUIRE(loaded.ok);
    const sg::detail::EmittedGraph emitted = emitGraph(registry, loaded.graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("if (floor(predicate) == 0.0)") != std::string::npos);
    CHECK(emitted.source.find("else if (floor(predicate) == 3.0)") != std::string::npos);
    CHECK(emitted.source.find("result = case_A;") != std::string::npos);
    CHECK(emitted.source.find("result = case_B;") != std::string::npos);
}

TEST_CASE("Keyword node emits the checked-in keyword_enum graph") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/keyword_enum.sgraph.json", registry);
    REQUIRE(loaded.ok);
    const sg::detail::EmittedGraph emitted = emitGraph(registry, loaded.graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("#if defined(_MODE_MODE_A)") != std::string::npos);
    CHECK(emitted.source.find("#elif defined(_MODE_MODE_B)") != std::string::npos);
}
