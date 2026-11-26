#include "compile/compile_internal.h"
#include "nodes/custom_function.h"
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

sg::NodeInstance customNode(const sg::NodeRegistry& registry, sg::NodeId id) {
    sg::NodeInstance node = registry.instantiate("Custom Function", id);
    node.settings["sourceType"] = sg::Value::makeString(sg::ValueType::String, "string");
    node.settings["functionName"] = sg::Value::makeString(sg::ValueType::String, "HbgCustomAdd");
    node.settings["functionBody"] =
        sg::Value::makeString(sg::ValueType::String, "Out = vec3(A + B, 0.0, 0.0);");
    node.settings["inputSlots"] =
        sg::Value::makeString(sg::ValueType::String, "A|float;B|float");
    node.settings["outputSlots"] =
        sg::Value::makeString(sg::ValueType::String, "Out|vector3");
    return node;
}

} // namespace

TEST_CASE("Custom Function exposes Unity Utility metadata and placeholder slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::NodeDef* definition = registry.find("Custom Function");
    REQUIRE(definition != nullptr);
    CHECK(definition->category == "Utility");
    CHECK(definition->title == "Custom Function");
    CHECK(definition->allowInMainGraph);
    CHECK(definition->allowInSubGraph);
    CHECK(definition->hasPreview);
    CHECK(definition->settings ==
          std::vector<std::string>{"sourceType", "functionName", "functionSource", "functionBody",
                                   "inputSlots", "outputSlots", "functionSourceUsePragmas"});
    CHECK(definition->synonyms == std::vector<std::string>{"code", "hlsl", "custom"});
    CHECK(static_cast<bool>(definition->updateSlots));
    REQUIRE(definition->slots.size() == 32);
    CHECK(definition->slots.front().id == 0);
    CHECK(definition->slots.front().hidden);
    CHECK(definition->slots[16].id == 100);
    CHECK(static_cast<int>(definition->slots[16].direction) ==
          static_cast<int>(sg::SlotDirection::Output));
    CHECK(registry.find("CustomFunction") != nullptr);
}

TEST_CASE("Custom Function emits a generated function and calls it with slot arguments") {
    const std::vector<sg::CustomFunctionSlotSpec> parsed =
        sg::parseCustomFunctionSlots("Out|vector3");
    REQUIRE(parsed.size() == 1);
    CHECK(static_cast<int>(parsed.front().type) == static_cast<int>(sg::ValueType::Vector3));
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    REQUIRE(graph.addNode(customNode(registry, 1)));
    REQUIRE(graph.addNode(floatNode(registry, 2, 0.1f)));
    REQUIRE(graph.addNode(floatNode(registry, 3, 0.2f)));
    REQUIRE(graph.addNode(registry.instantiate("BlockBaseColor", 100)));
    REQUIRE(graph.addEdge({2, 0, 1, 0}));
    REQUIRE(graph.addEdge({3, 0, 1, 1}));
    REQUIRE(graph.addEdge({1, 100, 100, 0}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("void HbgCustomAdd(float A, float B, out vec3 Out)") !=
          std::string::npos);
    CHECK(emitted.source.find("Out = vec3(A + B, 0.0, 0.0);") != std::string::npos);
    CHECK(emitted.source.find("HbgCustomAdd(n2_s0, n3_s0, n1_s100);") != std::string::npos);

    const sg::NodeInstance* node = graph.findNode(1);
    REQUIRE(node != nullptr);
    REQUIRE(node->slots.size() == 3);
    CHECK(node->findSlot(0)->spec.name == "A");
    CHECK(node->findSlot(1)->spec.name == "B");
    CHECK(node->findSlot(100)->spec.name == "Out");
    CHECK(static_cast<int>(node->findSlot(100)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Vector3));
}

TEST_CASE("Custom Function supports an included HLSL file source") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    sg::NodeInstance node = registry.instantiate("Custom Function", 1);
    node.settings["sourceType"] = sg::Value::makeString(sg::ValueType::String, "file");
    node.settings["functionName"] = sg::Value::makeString(sg::ValueType::String, "HbgCustomFile");
    node.settings["functionSource"] =
        sg::Value::makeString(sg::ValueType::String, "shadergraph/custom_function_test.hlsl");
    node.settings["inputSlots"] =
        sg::Value::makeString(sg::ValueType::String, "A|float");
    node.settings["outputSlots"] =
        sg::Value::makeString(sg::ValueType::String, "Out|vector3");
    REQUIRE(graph.addNode(std::move(node)));
    REQUIRE(graph.addNode(floatNode(registry, 2, 0.5f)));
    REQUIRE(graph.addNode(registry.instantiate("BlockBaseColor", 100)));
    REQUIRE(graph.addEdge({2, 0, 1, 0}));
    REQUIRE(graph.addEdge({1, 100, 100, 0}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("#include \"shadergraph/custom_function_test.hlsl\"") !=
          std::string::npos);
    CHECK(emitted.source.find("HbgCustomFile(n2_s0, n1_s100);") != std::string::npos);
}

TEST_CASE("Custom Function emits the checked-in string and file fixtures") {
    const std::filesystem::path root(SG_ROOT);
    const std::vector<std::pair<std::string, std::string>> fixtures = {
        {"custom_function", "HbgCustomAdd"},
        {"custom_function_file", "HbgCustomFile"},
    };
    for (const auto& [name, function] : fixtures) {
        sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
        sg::GraphLoadResult loaded = sg::loadGraph(
            root / ("tests/shadergraph/assets/" + name + ".sgraph.json"), registry);
        REQUIRE(loaded.ok);
        const sg::detail::EmittedGraph emitted = emitGraph(registry, loaded.graph);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(function + "(") != std::string::npos);
    }
}

TEST_CASE("Custom Function rejects an unset function name") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    sg::NodeInstance node = customNode(registry, 1);
    node.settings.erase("functionName");
    REQUIRE(graph.addNode(std::move(node)));
    REQUIRE(graph.addNode(floatNode(registry, 2, 0.1f)));
    REQUIRE(graph.addNode(floatNode(registry, 3, 0.2f)));
    REQUIRE(graph.addNode(registry.instantiate("BlockBaseColor", 100)));
    REQUIRE(graph.addEdge({2, 0, 1, 0}));
    REQUIRE(graph.addEdge({3, 0, 1, 1}));
    REQUIRE(graph.addEdge({1, 100, 100, 0}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    CHECK_FALSE(emitted.ok);
    CHECK(std::any_of(emitted.diagnostics.begin(), emitted.diagnostics.end(),
                      [](const sg::Diagnostic& diagnostic) {
                          return diagnostic.code == sg::kErrorInvalidValue &&
                                 diagnostic.message.find("function name") != std::string::npos;
                      }));
}
