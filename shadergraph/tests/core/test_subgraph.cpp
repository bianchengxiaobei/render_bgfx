#include "test_helpers.h"

#include "sg/serialize.h"
#include "sg/subgraph.h"

#include <string>

TEST_CASE("subgraph expansion namespaces properties and rewires outputs") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sgt::Graph child = sgt::makeSubgraphChild(registry);
    sg::SubGraphLibrary library;
    library.add("Child", child);

    sgt::Graph parent = sgt::makeGraph();
    sgt::addNodes(parent, registry, {{"Constant", 1}, {"SubGraph", 2}, {"Consumer", 3}});
    parent.findNode(2)->setSetting("subgraph",
                                   sg::Value::makeString(sg::ValueType::String, "Child"));
    REQUIRE(parent.addEdge({1, 0, 2, 0}));
    REQUIRE(parent.addEdge({2, 1, 3, 0}));

    sg::SubGraphExpansion expanded = sg::expandSubgraphs(parent, registry, library);
    REQUIRE(expanded.ok);
    CHECK(expanded.graph.findNode(2) == nullptr);
    CHECK(expanded.graph.findProperty("sg2__Tint") == nullptr);
    REQUIRE(expanded.graph.findNode(4) != nullptr);
    const sg::Edge* incoming = expanded.graph.incomingEdge(3, 0);
    REQUIRE(incoming != nullptr);
    CHECK(incoming->fromNode == 4);
    CHECK(incoming->fromSlot == 0);
    CHECK(expanded.graph.edges.size() == 1);

    sgt::Graph promotedParent = sgt::makeGraph();
    sgt::addNodes(promotedParent, registry, {{"SubGraph", 2}, {"Consumer", 3}});
    promotedParent.findNode(2)->setSetting(
        "subgraph", sg::Value::makeString(sg::ValueType::String, "Child"));
    REQUIRE(promotedParent.addEdge({2, 1, 3, 0}));
    const sg::SubGraphExpansion promoted = sg::expandSubgraphs(promotedParent, registry, library);
    REQUIRE(promoted.ok);
    CHECK(promoted.graph.findProperty("sg2__Tint") != nullptr);

    sgt::Graph conflicting = promotedParent;
    sg::Property conflict;
    conflict.name = "Conflict";
    conflict.refName = "sg2__Tint";
    conflict.type = sg::ValueType::Vector4;
    REQUIRE(conflicting.addProperty(conflict));
    expanded = sg::expandSubgraphs(conflicting, registry, library);
    CHECK_FALSE(expanded.ok);
    CHECK(sgt::hasCode(expanded.diagnostics, sg::kErrorNameConflict));

    sgt::Graph recursive = child;
    sg::NodeInstance nested = registry.instantiate("SubGraph", 10);
    nested.setSetting("subgraph", sg::Value::makeString(sg::ValueType::String, "Recursive"));
    REQUIRE(recursive.addNode(std::move(nested)));
    library.add("Recursive", recursive);

    sgt::Graph parentRecursive = sgt::makeGraph();
    sgt::addNodes(parentRecursive, registry, {{"SubGraph", 1}});
    parentRecursive.findNode(1)->setSetting(
        "subgraph", sg::Value::makeString(sg::ValueType::String, "Recursive"));
    expanded = sg::expandSubgraphs(parentRecursive, registry, library);
    CHECK_FALSE(expanded.ok);
    CHECK(sgt::hasCode(expanded.diagnostics, sg::kErrorSubGraphDepth));
}

TEST_CASE("subgraphs serialize without a target") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    const sgt::Graph child = sgt::makeSubgraphChild(registry);
    const std::string text = sg::serialize(child);
    CHECK(text.find("\"target\"") == std::string::npos);
    const sg::GraphLoadResult loaded = sg::deserialize(text, registry);
    REQUIRE(loaded.ok);
    CHECK(static_cast<int>(loaded.graph.kind) == static_cast<int>(sg::GraphKind::SubGraph));
    CHECK(loaded.graph.outputs.size() == 1);
    CHECK(sg::serialize(loaded.graph) == text);
}

TEST_CASE("subgraph inputs rewire into internal property consumers") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sgt::Graph child = sgt::makeSubgraphChild(registry);
    sg::NodeInstance consumer = registry.instantiate("Consumer", 4);
    REQUIRE(child.addNode(std::move(consumer)));
    REQUIRE(child.addEdge({1, 0, 4, 0}));
    sg::SubGraphLibrary library;
    library.add("Child", child);

    sgt::Graph parent = sgt::makeGraph();
    sgt::addNodes(parent, registry, {{"Constant4", 1}, {"SubGraph", 2}, {"Consumer", 3}});
    parent.findNode(2)->setSetting("subgraph",
                                   sg::Value::makeString(sg::ValueType::String, "Child"));
    REQUIRE(parent.addEdge({1, 0, 2, 0}));
    REQUIRE(parent.addEdge({2, 1, 3, 0}));

    const sg::SubGraphExpansion expanded = sg::expandSubgraphs(parent, registry, library);
    REQUIRE(expanded.ok);
    CHECK(expanded.graph.findNode(2) == nullptr);
    REQUIRE(expanded.graph.findNode(5) != nullptr);
    const sg::Edge* external = expanded.graph.incomingEdge(5, 0);
    REQUIRE(external != nullptr);
    CHECK(external->fromNode == 1);
    CHECK(external->fromSlot == 0);
    const sg::Edge* output = expanded.graph.incomingEdge(3, 0);
    REQUIRE(output != nullptr);
    CHECK(output->fromNode == 4);
}

TEST_CASE("nested subgraphs expand recursively") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sg::SubGraphLibrary library;

    sgt::Graph grand = sgt::makeSubgraphChild(registry, "Grand");
    library.add("Grand", grand);

    sgt::Graph middle;
    middle.kind = sg::GraphKind::SubGraph;
    middle.name = "Middle";
    sg::NodeInstance middleSub = registry.instantiate("SubGraph", 1);
    middleSub.setSetting("subgraph", sg::Value::makeString(sg::ValueType::String, "Grand"));
    REQUIRE(middle.addNode(std::move(middleSub)));
    middle.outputs.push_back({"Out", sg::ValueType::Vector4, 1});
    sg::NodeInstance middleOutput = registry.instantiate("SubGraphOutput", 2);
    REQUIRE(middle.addNode(std::move(middleOutput)));
    REQUIRE(middle.addEdge({1, 1, 2, 1}));
    library.add("Middle", middle);

    sgt::Graph parent = sgt::makeGraph();
    sgt::addNodes(parent, registry, {{"SubGraph", 1}, {"Consumer", 2}});
    parent.findNode(1)->setSetting("subgraph",
                                   sg::Value::makeString(sg::ValueType::String, "Middle"));
    REQUIRE(parent.addEdge({1, 1, 2, 0}));

    const sg::SubGraphExpansion expanded = sg::expandSubgraphs(parent, registry, library);
    REQUIRE(expanded.ok);
    CHECK(expanded.graph.findNode(1) == nullptr);
    CHECK(expanded.graph.incomingEdge(2, 0) != nullptr);
}
