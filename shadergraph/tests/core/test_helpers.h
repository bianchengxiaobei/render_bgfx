#pragma once

#include "sg/graph.h"
#include "sg/registry.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace sgt {

using namespace sg;

inline SlotSpec slot(SlotId id, std::string name, SlotDirection direction, ValueType type,
                     std::optional<Value> fallback = std::nullopt) {
    SlotSpec result;
    result.id = id;
    result.name = std::move(name);
    result.displayName = result.name;
    result.direction = direction;
    result.type = type;
    result.defaultValue = std::move(fallback);
    return result;
}

inline NodeDef definition(std::string type, std::vector<SlotSpec> slots) {
    NodeDef result;
    result.type = std::move(type);
    result.title = result.type;
    result.slots = std::move(slots);
    return result;
}

inline NodeRegistry makeRegistry() {
    NodeRegistry registry;
    CHECK(registry.add(definition("Static",
                                  {slot(0, "V1", SlotDirection::Output, ValueType::Float),
                                   slot(1, "V2", SlotDirection::Output, ValueType::Vector2),
                                   slot(2, "V3", SlotDirection::Output, ValueType::Vector3),
                                   slot(3, "V4", SlotDirection::Output, ValueType::Vector4),
                                   slot(4, "B", SlotDirection::Output, ValueType::Boolean),
                                   slot(5, "T", SlotDirection::Output, ValueType::Texture2D),
                                   slot(6, "M2", SlotDirection::Output, ValueType::Matrix2),
                                   slot(7, "M3", SlotDirection::Output, ValueType::Matrix3),
                                   slot(8, "M4", SlotDirection::Output, ValueType::Matrix4)})));
    CHECK(registry.add(definition("Dynamic",
                                  {slot(0, "A", SlotDirection::Input, ValueType::DynamicVector),
                                   slot(1, "B", SlotDirection::Input, ValueType::DynamicVector),
                                   slot(2, "Out", SlotDirection::Output, ValueType::DynamicVector)})));
    CHECK(registry.add(definition("DynamicMatrix",
                                  {slot(0, "A", SlotDirection::Input, ValueType::DynamicMatrix),
                                   slot(1, "B", SlotDirection::Input, ValueType::DynamicMatrix),
                                   slot(2, "Out", SlotDirection::Output, ValueType::DynamicMatrix)})));
    CHECK(registry.add(definition("FloatIn",
                                  {slot(0, "In", SlotDirection::Input, ValueType::Float),
                                   slot(1, "Out", SlotDirection::Output, ValueType::Float)})));
    CHECK(registry.add(definition("RequiredIn",
                                  {slot(0, "In", SlotDirection::Input, ValueType::Float)})));
    CHECK(registry.add(definition("TextureToFloat",
                                  {slot(0, "T", SlotDirection::Output, ValueType::Texture2D),
                                   slot(1, "V", SlotDirection::Output, ValueType::Float)})));
    CHECK(registry.add(definition("Constant",
                                  {slot(0, "Out", SlotDirection::Output, ValueType::Float)})));
    CHECK(registry.add(definition("Constant4",
                                  {slot(0, "Out", SlotDirection::Output, ValueType::Vector4)})));
    CHECK(registry.add(definition("Consumer",
                                  {slot(0, "In", SlotDirection::Input, ValueType::Vector4)})));
    CHECK(registry.add(definition("Property",
                                  {slot(0, "Out", SlotDirection::Output, ValueType::Vector4)})));
    CHECK(registry.add(definition("SubGraphOutput",
                                  {slot(1, "Out", SlotDirection::Input, ValueType::Vector4)})));
    CHECK(registry.add(definition("SubGraph",
                                  {slot(0, "Tint", SlotDirection::Input, ValueType::Vector4),
                                   slot(1, "Out", SlotDirection::Output, ValueType::Vector4)})));
    return registry;
}

inline Graph makeGraph() {
    Graph graph;
    graph.name = "Test";
    graph.target.id = "lit";
    graph.target.subTarget = "lit";
    return graph;
}

inline void addNodes(Graph& graph, const NodeRegistry& registry,
                     std::initializer_list<std::pair<std::string, NodeId>> nodes) {
    for (const auto& [type, id] : nodes) {
        NodeInstance node = registry.instantiate(type, id);
        REQUIRE(graph.addNode(std::move(node)));
    }
}

inline bool hasCode(const std::vector<Diagnostic>& diagnostics, const char* code) {
    for (const Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

inline Graph makeSubgraphChild(const NodeRegistry& registry, const std::string& name = "Child") {
    Graph child;
    child.kind = GraphKind::SubGraph;
    child.name = name;
    Property property;
    property.name = "Tint";
    property.refName = "_Tint";
    property.type = ValueType::Vector4;
    property.defaultValue = Value::makeVector4({1.0f, 1.0f, 1.0f, 1.0f});
    REQUIRE(child.addProperty(property));

    NodeInstance propertyNode = registry.instantiate("Property", 1);
    propertyNode.setSetting("refName", Value::makeString(ValueType::String, "_Tint"));
    NodeInstance constant = registry.instantiate("Constant4", 2);
    NodeInstance output = registry.instantiate("SubGraphOutput", 3);
    REQUIRE(child.addNode(std::move(propertyNode)));
    REQUIRE(child.addNode(std::move(constant)));
    REQUIRE(child.addNode(std::move(output)));
    REQUIRE(child.addEdge({2, 0, 3, 1}));
    child.outputs.push_back({"Out", ValueType::Vector4, 1});
    return child;
}

} // namespace sgt
