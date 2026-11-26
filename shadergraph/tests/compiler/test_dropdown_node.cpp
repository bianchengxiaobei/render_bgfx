#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"
#include "sg/serialize.h"
#include "sg/subgraph.h"

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

sg::NodeRegistry makeSubgraphRegistry() {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::NodeDef output;
    output.type = "SubGraphOutput";
    output.title = "SubGraphOutput";
    output.category = "Master";
    sg::SlotSpec slot;
    slot.id = 1;
    slot.name = "Out";
    slot.displayName = "Out";
    slot.direction = sg::SlotDirection::Input;
    slot.type = sg::ValueType::DynamicVector;
    output.slots.push_back(std::move(slot));
    REQUIRE(registry.add(std::move(output)));
    return registry;
}

sg::Dropdown makeDropdown(int value = 0) {
    sg::Dropdown dropdown;
    dropdown.name = "Mode";
    dropdown.refName = "_MODE_SELECTION";
    dropdown.value = value;
    dropdown.entries = {{1, "A"}, {2, "B"}};
    return dropdown;
}

sg::NodeInstance floatNode(const sg::NodeRegistry& registry, sg::NodeId id, float value) {
    sg::NodeInstance node = registry.instantiate("Float", id);
    node.settings["value"] = sg::Value::makeFloat(value);
    return node;
}

} // namespace

TEST_CASE("Dropdown node exposes Unity Utility/Dropdown metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::NodeDef* definition = registry.find("Dropdown");
    REQUIRE(definition != nullptr);
    CHECK(definition->category == "Utility");
    CHECK(definition->title == "Dropdown");
    CHECK(definition->allowInSubGraph);
    CHECK(definition->canPropagateFloatLiteral);
    CHECK(definition->settings == std::vector<std::string>{"dropdown"});
    REQUIRE(definition->slots.size() == 17);
    CHECK(definition->slots.front().id == 0);
    CHECK(static_cast<int>(definition->slots.front().direction) ==
          static_cast<int>(sg::SlotDirection::Output));
    CHECK(static_cast<int>(definition->slots.front().type) ==
          static_cast<int>(sg::ValueType::DynamicVector));
    CHECK(definition->slots[1].hidden);
    CHECK(static_cast<bool>(definition->updateSlots));
}

TEST_CASE("Dropdown node expands entries and selects the active value") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    REQUIRE(graph.addDropdown(makeDropdown(1)));
    sg::NodeInstance node = registry.instantiate("Dropdown", 1);
    node.settings["dropdownRef"] =
        sg::Value::makeString(sg::ValueType::String, "_MODE_SELECTION");
    REQUIRE(graph.addNode(std::move(node)));
    REQUIRE(graph.addNode(floatNode(registry, 2, 0.25f)));
    REQUIRE(graph.addNode(floatNode(registry, 3, 0.75f)));
    REQUIRE(graph.addEdge({2, 0, 1, 1}));
    REQUIRE(graph.addEdge({3, 0, 1, 2}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("n1_s0 = n3_s0;") != std::string::npos);

    const sg::NodeInstance* dropdownNode = graph.findNode(1);
    REQUIRE(dropdownNode != nullptr);
    REQUIRE(dropdownNode->slots.size() == 3);
    CHECK(dropdownNode->findSlot(0)->spec.displayName == "Mode");
    CHECK(dropdownNode->findSlot(1)->spec.displayName == "A");
    CHECK(dropdownNode->findSlot(2)->spec.displayName == "B");
    CHECK(static_cast<int>(dropdownNode->findSlot(0)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Float));
    CHECK(dropdownNode->settings.find("dropdownRef") == dropdownNode->settings.end());
    REQUIRE(dropdownNode->settings.find("dropdown") != dropdownNode->settings.end());
    REQUIRE(dropdownNode->settings.at("dropdown").asString() != nullptr);
    CHECK(*dropdownNode->settings.at("dropdown").asString() == "_MODE_SELECTION");
}

TEST_CASE("Dropdown node clamps an out-of-range selection") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    REQUIRE(graph.addDropdown(makeDropdown(-4)));
    sg::NodeInstance node = registry.instantiate("Dropdown", 1);
    node.settings["dropdown"] =
        sg::Value::makeString(sg::ValueType::String, "_MODE_SELECTION");
    REQUIRE(graph.addNode(std::move(node)));
    REQUIRE(graph.addNode(floatNode(registry, 2, 0.25f)));
    REQUIRE(graph.addNode(floatNode(registry, 3, 0.75f)));
    REQUIRE(graph.addEdge({2, 0, 1, 1}));
    REQUIRE(graph.addEdge({3, 0, 1, 2}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("n1_s0 = n2_s0;") != std::string::npos);
}

TEST_CASE("Dropdown node reports a missing graph dropdown") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    sg::NodeInstance node = registry.instantiate("Dropdown", 1);
    node.settings["dropdown"] =
        sg::Value::makeString(sg::ValueType::String, "_MISSING");
    REQUIRE(graph.addNode(std::move(node)));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    CHECK_FALSE(emitted.ok);
    CHECK(std::any_of(emitted.diagnostics.begin(), emitted.diagnostics.end(),
                      [](const sg::Diagnostic& diagnostic) {
                          return diagnostic.code == sg::kErrorMissingInput &&
                                 diagnostic.message.find("Dropdown node") != std::string::npos;
                      }));
}

TEST_CASE("Dropdown definitions serialize and round-trip") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph = makeGraph();
    REQUIRE(graph.addDropdown(makeDropdown(1)));
    const std::string text = sg::serialize(graph);
    const sg::GraphLoadResult loaded = sg::deserialize(text, registry);
    REQUIRE(loaded.ok);
    const sg::Dropdown* dropdown = loaded.graph.findDropdown("_MODE_SELECTION");
    REQUIRE(dropdown != nullptr);
    CHECK(dropdown->name == "Mode");
    CHECK(dropdown->value == 1);
    REQUIRE(dropdown->entries.size() == 2);
    CHECK(dropdown->entries[0].id == 1);
    CHECK(dropdown->entries[0].displayName == "A");
    CHECK(dropdown->entries[1].id == 2);
    CHECK(dropdown->entries[1].displayName == "B");
    CHECK(sg::serialize(loaded.graph) == text);
}

TEST_CASE("Sub-graph promotes dropdown definitions and remaps nodes") {
    sg::NodeRegistry registry = makeSubgraphRegistry();
    sg::Graph child;
    child.kind = sg::GraphKind::SubGraph;
    child.name = "DropdownChild";
    REQUIRE(child.addDropdown(makeDropdown(1)));

    sg::NodeInstance dropdown = registry.instantiate("Dropdown", 1);
    dropdown.settings["dropdown"] =
        sg::Value::makeString(sg::ValueType::String, "_MODE_SELECTION");
    REQUIRE(child.addNode(std::move(dropdown)));
    REQUIRE(child.addNode(floatNode(registry, 2, 0.25f)));
    REQUIRE(child.addNode(floatNode(registry, 3, 0.75f)));
    REQUIRE(child.addNode(registry.instantiate("SubGraphOutput", 4)));
    REQUIRE(child.addEdge({2, 0, 1, 1}));
    REQUIRE(child.addEdge({3, 0, 1, 2}));
    REQUIRE(child.addEdge({1, 0, 4, 1}));
    child.outputs.push_back({"Out", sg::ValueType::Float, 1});

    sg::SubGraphLibrary library;
    library.add("DropdownChild", std::move(child));

    sg::Graph parent = makeGraph();
    sg::NodeInstance subgraph = registry.instantiate("Sub-graph", 1);
    subgraph.settings["subgraph"] =
        sg::Value::makeString(sg::ValueType::String, "DropdownChild");
    REQUIRE(parent.addNode(std::move(subgraph)));
    REQUIRE(parent.addNode(registry.instantiate("Preview", 2)));
    REQUIRE(parent.addEdge({1, 1, 2, 0}));

    sg::SubGraphExpansion expanded = sg::expandSubgraphs(parent, registry, library);
    REQUIRE(expanded.ok);
    const sg::Dropdown* promoted = expanded.graph.findDropdown("sg1__MODE_SELECTION");
    REQUIRE(promoted != nullptr);
    CHECK(promoted->value == 1);

    const sg::NodeInstance* cloned = expanded.graph.findNode(3);
    REQUIRE(cloned != nullptr);
    REQUIRE(cloned->settings.find("dropdown") != cloned->settings.end());
    REQUIRE(cloned->settings.at("dropdown").asString() != nullptr);
    CHECK(*cloned->settings.at("dropdown").asString() == "sg1__MODE_SELECTION");

    const sg::detail::EmittedGraph emitted = emitGraph(registry, expanded.graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("n3_s0 = n5_s0;") != std::string::npos);
}

TEST_CASE("Sub-graph applies per-instance dropdown selections") {
    sg::NodeRegistry registry = makeSubgraphRegistry();
    sg::Graph child;
    child.kind = sg::GraphKind::SubGraph;
    child.name = "DropdownChild";
    REQUIRE(child.addDropdown(makeDropdown(0)));

    sg::NodeInstance dropdown = registry.instantiate("Dropdown", 1);
    dropdown.settings["dropdown"] =
        sg::Value::makeString(sg::ValueType::String, "_MODE_SELECTION");
    REQUIRE(child.addNode(std::move(dropdown)));
    REQUIRE(child.addNode(floatNode(registry, 2, 0.25f)));
    REQUIRE(child.addNode(floatNode(registry, 3, 0.75f)));
    REQUIRE(child.addNode(registry.instantiate("SubGraphOutput", 4)));
    REQUIRE(child.addEdge({2, 0, 1, 1}));
    REQUIRE(child.addEdge({3, 0, 1, 2}));
    REQUIRE(child.addEdge({1, 0, 4, 1}));
    child.outputs.push_back({"Out", sg::ValueType::Float, 1});

    sg::SubGraphLibrary library;
    library.add("DropdownChild", std::move(child));

    sg::Graph parent = makeGraph();
    sg::NodeInstance first = registry.instantiate("Sub-graph", 1);
    first.settings["subgraph"] =
        sg::Value::makeString(sg::ValueType::String, "DropdownChild");
    first.settings["dropdown._MODE_SELECTION"] =
        sg::Value::makeString(sg::ValueType::String, "B");
    sg::NodeInstance second = registry.instantiate("Sub-graph", 5);
    second.settings["subgraph"] =
        sg::Value::makeString(sg::ValueType::String, "DropdownChild");
    REQUIRE(parent.addNode(std::move(first)));
    REQUIRE(parent.addNode(registry.instantiate("Preview", 2)));
    REQUIRE(parent.addNode(std::move(second)));
    REQUIRE(parent.addNode(registry.instantiate("Preview", 6)));
    REQUIRE(parent.addEdge({1, 1, 2, 0}));
    REQUIRE(parent.addEdge({5, 1, 6, 0}));

    sg::SubGraphExpansion expanded = sg::expandSubgraphs(parent, registry, library);
    REQUIRE(expanded.ok);
    const sg::Dropdown* firstDropdown = expanded.graph.findDropdown("sg1__MODE_SELECTION");
    REQUIRE(firstDropdown != nullptr);
    CHECK(firstDropdown->value == 1);
    const sg::Dropdown* secondDropdown = expanded.graph.findDropdown("sg5__MODE_SELECTION");
    REQUIRE(secondDropdown != nullptr);
    CHECK(secondDropdown->value == 0);

    const sg::detail::EmittedGraph emitted = emitGraph(registry, expanded.graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("n7_s0 = n9_s0;") != std::string::npos);
    CHECK(emitted.source.find("n10_s0 = n11_s0;") != std::string::npos);
}

TEST_CASE("Sub-graph invalid dropdown selection falls back to the graph value") {
    sg::NodeRegistry registry = makeSubgraphRegistry();
    sg::Graph child;
    child.kind = sg::GraphKind::SubGraph;
    child.name = "DropdownChild";
    REQUIRE(child.addDropdown(makeDropdown(1)));

    sg::NodeInstance dropdown = registry.instantiate("Dropdown", 1);
    dropdown.settings["dropdown"] =
        sg::Value::makeString(sg::ValueType::String, "_MODE_SELECTION");
    REQUIRE(child.addNode(std::move(dropdown)));
    REQUIRE(child.addNode(floatNode(registry, 2, 0.25f)));
    REQUIRE(child.addNode(floatNode(registry, 3, 0.75f)));
    REQUIRE(child.addNode(registry.instantiate("SubGraphOutput", 4)));
    REQUIRE(child.addEdge({2, 0, 1, 1}));
    REQUIRE(child.addEdge({3, 0, 1, 2}));
    REQUIRE(child.addEdge({1, 0, 4, 1}));
    child.outputs.push_back({"Out", sg::ValueType::Float, 1});

    sg::SubGraphLibrary library;
    library.add("DropdownChild", std::move(child));

    sg::Graph parent = makeGraph();
    sg::NodeInstance subgraph = registry.instantiate("Sub-graph", 1);
    subgraph.settings["subgraph"] =
        sg::Value::makeString(sg::ValueType::String, "DropdownChild");
    subgraph.settings["dropdown._MODE_SELECTION"] =
        sg::Value::makeString(sg::ValueType::String, "Missing");
    REQUIRE(parent.addNode(std::move(subgraph)));
    REQUIRE(parent.addNode(registry.instantiate("Preview", 2)));
    REQUIRE(parent.addEdge({1, 1, 2, 0}));

    sg::SubGraphExpansion expanded = sg::expandSubgraphs(parent, registry, library);
    REQUIRE(expanded.ok);
    const sg::Dropdown* promoted = expanded.graph.findDropdown("sg1__MODE_SELECTION");
    REQUIRE(promoted != nullptr);
    CHECK(promoted->value == 1);
    CHECK(std::any_of(expanded.diagnostics.begin(), expanded.diagnostics.end(),
                      [](const sg::Diagnostic& diagnostic) {
                          return diagnostic.severity == sg::Severity::Warning &&
                                 diagnostic.message.find("subgraph dropdown selection") !=
                                     std::string::npos;
                      }));
}

TEST_CASE("Dropdown node emits the checked-in dropdown_values graph") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/dropdown_values.sgraph.json", registry);
    REQUIRE(loaded.ok);
    const sg::detail::EmittedGraph emitted = emitGraph(registry, loaded.graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("n1_s0 = n3_s0;") != std::string::npos);
}
