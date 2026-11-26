#include "sg/node_library.h"
#include "sg/subgraph.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace {

sg::SlotSpec slotSpec(sg::SlotId id, std::string name, sg::SlotDirection direction,
                      sg::ValueType type) {
    sg::SlotSpec slot;
    slot.id = id;
    slot.name = name;
    slot.displayName = name;
    slot.direction = direction;
    slot.type = type;
    return slot;
}

sg::NodeRegistry makeRegistry() {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::NodeDef output;
    output.type = "SubGraphOutput";
    output.title = "SubGraphOutput";
    output.category = "Master";
    output.slots = {
        slotSpec(1, "Out", sg::SlotDirection::Input, sg::ValueType::DynamicVector),
        slotSpec(2, "Out 2", sg::SlotDirection::Input, sg::ValueType::DynamicVector),
    };
    REQUIRE(registry.add(std::move(output)));
    return registry;
}

sg::Property makeTintProperty() {
    sg::Property property;
    property.name = "Tint";
    property.refName = "_Tint";
    property.type = sg::ValueType::Vector4;
    property.defaultValue = sg::Value::makeVector4({1.0f, 1.0f, 1.0f, 1.0f});
    return property;
}

sg::Graph makeMultiplyChild(const sg::NodeRegistry& registry) {
    sg::Graph child;
    child.kind = sg::GraphKind::SubGraph;
    child.name = "Child";
    REQUIRE(child.addProperty(makeTintProperty()));

    sg::NodeInstance property = registry.instantiate("Property", 1);
    property.setSetting("property", sg::Value::makeString(sg::ValueType::String, "_Tint"));
    sg::NodeInstance constant = registry.instantiate("Float", 2);
    constant.setSetting("value", sg::Value::makeFloat(0.5f));
    sg::NodeInstance multiply = registry.instantiate("Multiply", 3);
    sg::NodeInstance output = registry.instantiate("SubGraphOutput", 4);
    REQUIRE(child.addNode(std::move(property)));
    REQUIRE(child.addNode(std::move(constant)));
    REQUIRE(child.addNode(std::move(multiply)));
    REQUIRE(child.addNode(std::move(output)));
    REQUIRE(child.addEdge({1, 0, 3, 0}));
    REQUIRE(child.addEdge({2, 0, 3, 1}));
    REQUIRE(child.addEdge({3, 2, 4, 1}));
    child.outputs.push_back({"Out", sg::ValueType::Float, 1});
    return child;
}

sg::Graph makePassthroughChild(const sg::NodeRegistry& registry) {
    sg::Graph child;
    child.kind = sg::GraphKind::SubGraph;
    child.name = "Passthrough";
    REQUIRE(child.addProperty(makeTintProperty()));
    sg::NodeInstance property = registry.instantiate("Property", 1);
    property.setSetting("property", sg::Value::makeString(sg::ValueType::String, "_Tint"));
    sg::NodeInstance output = registry.instantiate("SubGraphOutput", 2);
    REQUIRE(child.addNode(std::move(property)));
    REQUIRE(child.addNode(std::move(output)));
    REQUIRE(child.addEdge({1, 0, 2, 1}));
    child.outputs.push_back({"Out", sg::ValueType::Vector4, 1});
    return child;
}

sg::Graph makeMultiChild(const sg::NodeRegistry& registry) {
    sg::Graph child;
    child.kind = sg::GraphKind::SubGraph;
    child.name = "Multi";
    sg::Property tint = makeTintProperty();
    sg::Property scale;
    scale.name = "Scale";
    scale.refName = "_Scale";
    scale.type = sg::ValueType::Float;
    scale.defaultValue = sg::Value::makeFloat(1.0f);
    REQUIRE(child.addProperty(tint));
    REQUIRE(child.addProperty(scale));

    sg::NodeInstance tintNode = registry.instantiate("Property", 1);
    tintNode.setSetting("property", sg::Value::makeString(sg::ValueType::String, "_Tint"));
    sg::NodeInstance scaleNode = registry.instantiate("Property", 2);
    scaleNode.setSetting("property", sg::Value::makeString(sg::ValueType::String, "_Scale"));
    sg::NodeInstance constant = registry.instantiate("Float", 3);
    constant.setSetting("value", sg::Value::makeFloat(2.0f));
    sg::NodeInstance add = registry.instantiate("Add", 4);
    sg::NodeInstance multiply = registry.instantiate("Multiply", 5);
    sg::NodeInstance output = registry.instantiate("SubGraphOutput", 6);
    REQUIRE(child.addNode(std::move(tintNode)));
    REQUIRE(child.addNode(std::move(scaleNode)));
    REQUIRE(child.addNode(std::move(constant)));
    REQUIRE(child.addNode(std::move(add)));
    REQUIRE(child.addNode(std::move(multiply)));
    REQUIRE(child.addNode(std::move(output)));
    REQUIRE(child.addEdge({1, 0, 4, 0}));
    REQUIRE(child.addEdge({2, 0, 4, 1}));
    REQUIRE(child.addEdge({4, 2, 6, 1}));
    REQUIRE(child.addEdge({1, 0, 5, 0}));
    REQUIRE(child.addEdge({3, 0, 5, 1}));
    REQUIRE(child.addEdge({5, 2, 6, 2}));
    child.outputs.push_back({"Out", sg::ValueType::Float, 1});
    child.outputs.push_back({"Out 2", sg::ValueType::Float, 2});
    return child;
}

} // namespace

TEST_CASE("Sub-graph node exposes Unity Utility metadata and dynamic placeholders") {
    sg::NodeRegistry registry = makeRegistry();
    const sg::NodeDef* definition = registry.find("Sub-graph");
    REQUIRE(definition != nullptr);
    CHECK(definition->category == "Utility");
    CHECK(definition->title == "Sub-graph");
    CHECK(definition->settings == std::vector<std::string>{"subgraph"});
    CHECK(definition->allowInMainGraph);
    CHECK(definition->allowInSubGraph);
    CHECK(definition->hasPreview);
    REQUIRE(definition->slots.size() == 8);
    CHECK(definition->slots[0].id == 0);
    CHECK(static_cast<int>(definition->slots[0].direction) ==
          static_cast<int>(sg::SlotDirection::Input));
    CHECK_FALSE(definition->slots[0].hidden);
    CHECK(definition->slots[1].id == 2);
    CHECK(definition->slots[1].hidden);
    CHECK(definition->slots[4].id == 1);
    CHECK(static_cast<int>(definition->slots[4].direction) ==
          static_cast<int>(sg::SlotDirection::Output));
    CHECK_FALSE(definition->slots[4].hidden);
    CHECK(registry.find("SubGraph") != nullptr);
    CHECK(registry.find("Subgraph") != nullptr);
}

TEST_CASE("Sub-graph node expands external inputs and rewires outputs") {
    sg::NodeRegistry registry = makeRegistry();
    sg::SubGraphLibrary library;
    library.add("Child", makeMultiplyChild(registry));

    sg::Graph parent;
    parent.name = "Parent";
    parent.target.id = "lit";
    sg::NodeInstance input = registry.instantiate("Float", 1);
    input.setSetting("value", sg::Value::makeFloat(0.25f));
    sg::NodeInstance subgraph = registry.instantiate("Sub-graph", 2);
    subgraph.setSetting("subgraph",
                        sg::Value::makeString(sg::ValueType::String, "Child"));
    sg::NodeInstance consumer = registry.instantiate("Preview", 3);
    REQUIRE(parent.addNode(std::move(input)));
    REQUIRE(parent.addNode(std::move(subgraph)));
    REQUIRE(parent.addNode(std::move(consumer)));
    REQUIRE(parent.addEdge({1, 0, 2, 0}));
    REQUIRE(parent.addEdge({2, 1, 3, 0}));

    const sg::SubGraphExpansion expanded = sg::expandSubgraphs(parent, registry, library);
    REQUIRE(expanded.ok);
    CHECK(expanded.graph.findNode(2) == nullptr);
    CHECK(expanded.graph.findProperty("sg2__Tint") == nullptr);

    const sg::Edge* output = expanded.graph.incomingEdge(3, 0);
    REQUIRE(output != nullptr);
    CHECK(output->fromNode == 5);
    CHECK(output->fromSlot == 2);
    const sg::Edge* external = expanded.graph.incomingEdge(5, 0);
    REQUIRE(external != nullptr);
    CHECK(external->fromNode == 1);
    const sg::Edge* constant = expanded.graph.incomingEdge(5, 1);
    REQUIRE(constant != nullptr);
    CHECK(constant->fromNode == 4);
}

TEST_CASE("Sub-graph node maps multiple inputs and outputs in declaration order") {
    sg::NodeRegistry registry = makeRegistry();
    sg::SubGraphLibrary library;
    library.add("Multi", makeMultiChild(registry));

    sg::Graph parent;
    parent.name = "Parent";
    parent.target.id = "lit";
    sg::NodeInstance tint = registry.instantiate("Vector 4", 1);
    sg::NodeInstance scale = registry.instantiate("Float", 2);
    scale.setSetting("value", sg::Value::makeFloat(0.5f));
    sg::NodeInstance subgraph = registry.instantiate("Sub-graph", 3);
    subgraph.setSetting("subgraph", sg::Value::makeString(sg::ValueType::String, "Multi"));
    sg::NodeInstance first = registry.instantiate("Preview", 4);
    sg::NodeInstance second = registry.instantiate("Preview", 5);
    REQUIRE(parent.addNode(std::move(tint)));
    REQUIRE(parent.addNode(std::move(scale)));
    REQUIRE(parent.addNode(std::move(subgraph)));
    REQUIRE(parent.addNode(std::move(first)));
    REQUIRE(parent.addNode(std::move(second)));
    REQUIRE(parent.addEdge({1, 0, 3, 0}));
    REQUIRE(parent.addEdge({2, 0, 3, 2}));
    REQUIRE(parent.addEdge({3, 1, 4, 0}));
    REQUIRE(parent.addEdge({3, 5, 5, 0}));

    const sg::SubGraphExpansion expanded = sg::expandSubgraphs(parent, registry, library);
    REQUIRE(expanded.ok);
    CHECK(expanded.graph.findNode(3) == nullptr);
    CHECK(expanded.graph.findProperty("sg3__Tint") == nullptr);
    CHECK(expanded.graph.findProperty("sg3__Scale") == nullptr);

    const sg::Edge* firstOutput = expanded.graph.incomingEdge(4, 0);
    REQUIRE(firstOutput != nullptr);
    CHECK(firstOutput->fromNode == 7);
    CHECK(firstOutput->fromSlot == 2);
    const sg::Edge* secondOutput = expanded.graph.incomingEdge(5, 0);
    REQUIRE(secondOutput != nullptr);
    CHECK(secondOutput->fromNode == 8);
    CHECK(secondOutput->fromSlot == 2);

    const sg::Edge* addTint = expanded.graph.incomingEdge(7, 0);
    REQUIRE(addTint != nullptr);
    CHECK(addTint->fromNode == 1);
    const sg::Edge* addScale = expanded.graph.incomingEdge(7, 1);
    REQUIRE(addScale != nullptr);
    CHECK(addScale->fromNode == 2);
    const sg::Edge* multiplyTint = expanded.graph.incomingEdge(8, 0);
    REQUIRE(multiplyTint != nullptr);
    CHECK(multiplyTint->fromNode == 1);
    const sg::Edge* multiplyConstant = expanded.graph.incomingEdge(8, 1);
    REQUIRE(multiplyConstant != nullptr);
    CHECK(multiplyConstant->fromNode == 6);
}

TEST_CASE("Sub-graph node passes an external property directly to an output") {
    sg::NodeRegistry registry = makeRegistry();
    sg::SubGraphLibrary library;
    library.add("Passthrough", makePassthroughChild(registry));

    sg::Graph parent;
    parent.name = "Parent";
    parent.target.id = "lit";
    sg::NodeInstance input = registry.instantiate("Float", 1);
    input.setSetting("value", sg::Value::makeFloat(0.75f));
    sg::NodeInstance subgraph = registry.instantiate("Sub-graph", 2);
    subgraph.setSetting("subgraph",
                        sg::Value::makeString(sg::ValueType::String, "Passthrough"));
    sg::NodeInstance consumer = registry.instantiate("Preview", 3);
    REQUIRE(parent.addNode(std::move(input)));
    REQUIRE(parent.addNode(std::move(subgraph)));
    REQUIRE(parent.addNode(std::move(consumer)));
    REQUIRE(parent.addEdge({1, 0, 2, 0}));
    REQUIRE(parent.addEdge({2, 1, 3, 0}));

    const sg::SubGraphExpansion expanded = sg::expandSubgraphs(parent, registry, library);
    REQUIRE(expanded.ok);
    CHECK(expanded.graph.findNode(2) == nullptr);
    CHECK(expanded.graph.findProperty("sg2__Tint") == nullptr);
    const sg::Edge* output = expanded.graph.incomingEdge(3, 0);
    REQUIRE(output != nullptr);
    CHECK(output->fromNode == 1);
    CHECK(output->fromSlot == 0);
}
