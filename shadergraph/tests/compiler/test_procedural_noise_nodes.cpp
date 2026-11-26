#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace {

struct NodeExpectation {
    const char* type;
    const char* category;
    int inputs;
    int outputs;
};

const NodeExpectation kNoiseNodes[] = {
    {"Simple Noise", "Procedural/Noise", 2, 1},
    {"Gradient Noise", "Procedural/Noise", 2, 1},
    {"Voronoi", "Procedural/Noise", 3, 2},
};

struct EmitExpectation {
    const char* type;
    const char* function;
    const char* hash;
};

const EmitExpectation kNoiseEmitNodes[] = {
    {"Simple Noise", "hbgSGSimpleNoiseDeterministic", "hbgSGHashTchou21"},
    {"Gradient Noise", "hbgSGGradientNoiseDeterministic", "hbgSGHashTchou21"},
    {"Voronoi", "hbgSGVoronoiDeterministic", "hbgSGHashTchou22"},
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

sg::detail::EmittedGraph emitSingle(const sg::NodeRegistry& registry, const char* type) {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    graph.nodes.push_back(registry.instantiate(type, 1));
    return emitGraph(registry, graph);
}

} // namespace

TEST_CASE("SG-6 procedural noise nodes are registered with stable slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNoiseNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->title == expected.type);
        int inputs = 0;
        int outputs = 0;
        for (const sg::SlotSpec& slot : definition->slots) {
            if (slot.direction == sg::SlotDirection::Input) {
                ++inputs;
            } else {
                ++outputs;
            }
        }
        CHECK(inputs == expected.inputs);
        CHECK(outputs == expected.outputs);
        CHECK(definition->settings == std::vector<std::string>{"hashType"});
    }

    const sg::NodeDef* simple = registry.find("Simple Noise");
    REQUIRE(simple != nullptr);
    REQUIRE(simple->slots.size() == 3);
    REQUIRE(simple->slots[1].defaultValue.has_value());
    REQUIRE(simple->slots[1].defaultValue->asFloat() != nullptr);
    CHECK(*simple->slots[1].defaultValue->asFloat() == doctest::Approx(500.0f));

    const sg::NodeDef* gradient = registry.find("Gradient Noise");
    REQUIRE(gradient != nullptr);
    REQUIRE(gradient->slots[1].defaultValue.has_value());
    REQUIRE(gradient->slots[1].defaultValue->asFloat() != nullptr);
    CHECK(*gradient->slots[1].defaultValue->asFloat() == doctest::Approx(10.0f));

    const sg::NodeDef* voronoi = registry.find("Voronoi");
    REQUIRE(voronoi != nullptr);
    REQUIRE(voronoi->slots.size() == 5);
    CHECK(static_cast<int>(voronoi->slots[3].direction) ==
          static_cast<int>(sg::SlotDirection::Output));
    CHECK(static_cast<int>(voronoi->slots[4].direction) ==
          static_cast<int>(sg::SlotDirection::Output));
    CHECK(static_cast<int>(voronoi->slots[3].type) ==
          static_cast<int>(sg::ValueType::Float));
    CHECK(static_cast<int>(voronoi->slots[4].type) ==
          static_cast<int>(sg::ValueType::Float));
}

TEST_CASE("SG-6 procedural noise nodes emit deterministic noise with a mesh UV fallback") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const EmitExpectation& expected : kNoiseEmitNodes) {
        CAPTURE(expected.type);
        sg::detail::EmittedGraph emitted = emitSingle(registry, expected.type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find("gi.uv0") != std::string::npos);
        CHECK(emitted.source.find(expected.function) != std::string::npos);
        CHECK(emitted.source.find(expected.hash) != std::string::npos);
    }

    sg::detail::EmittedGraph voronoi = emitSingle(registry, "Voronoi");
    REQUIRE(voronoi.ok);
    CHECK(voronoi.source.find("hbg_voronoi_1.x") != std::string::npos);
    CHECK(voronoi.source.find("hbg_voronoi_1.y") != std::string::npos);
}

TEST_CASE("SG-6 procedural noise hash settings affect codegen") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    struct LegacyExpectation {
        const char* type;
        const char* setting;
        const char* function;
        const char* hash;
    };
    const LegacyExpectation legacyNodes[] = {
        {"Simple Noise", "legacySine", "hbgSGSimpleNoiseLegacySine", "hbgSGHashLegacySine21"},
        {"Gradient Noise", "legacyMod", "hbgSGGradientNoiseLegacyMod", "hbgSGHashLegacyMod21"},
        {"Voronoi", "legacySine", "hbgSGVoronoiLegacySine", "hbgSGHashLegacySine22"},
    };
    for (const LegacyExpectation& expected : legacyNodes) {
        CAPTURE(expected.type);
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";
        sg::NodeInstance node = registry.instantiate(expected.type, 1);
        node.settings["hashType"] =
            sg::Value::makeString(sg::ValueType::String, expected.setting);
        REQUIRE(graph.addNode(std::move(node)));
        sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(expected.function) != std::string::npos);
        CHECK(emitted.source.find(expected.hash) != std::string::npos);
    }

    sg::Graph connected;
    connected.kind = sg::GraphKind::Graph;
    connected.target.id = "lit";
    REQUIRE(connected.addNode(registry.instantiate("UV", 1)));
    REQUIRE(connected.addNode(registry.instantiate("Voronoi", 2)));
    REQUIRE(connected.addEdge({1, 0, 2, 0}));
    sg::detail::EmittedGraph connectedGraph = emitGraph(registry, connected);
    REQUIRE(connectedGraph.ok);
    CHECK(connectedGraph.source.find("hbgSGVoronoiDeterministic(n1_s0") != std::string::npos);
}
