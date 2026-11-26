#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <string>

namespace {

struct NodeExpectation {
    const char* type;
    const char* category;
    int inputs;
    int outputs;
};

const NodeExpectation kUvNodes[] = {
    {"Tiling And Offset", "UV", 3, 1},
    {"Rotate", "UV", 3, 1},
    {"Polar Coordinates", "UV", 4, 1},
    {"Radial Shear", "UV", 4, 1},
    {"Spherize", "UV", 4, 1},
    {"Twirl", "UV", 4, 1},
    {"Flipbook", "UV", 4, 1},
};

struct EmitExpectation {
    const char* type;
    const char* fragment;
};

const EmitExpectation kUvEmitNodes[] = {
    {"Tiling And Offset", "gi.uv0 * "},
    {"Rotate", "sincos("},
    {"Polar Coordinates", "atan2("},
    {"Radial Shear", "vec2(hbg_uv_delta_"},
    {"Spherize", "hbg_uv_d4_"},
    {"Twirl", "hbg_uv_angle_"},
    {"Flipbook", "fmod("},
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

TEST_CASE("SG-6 UV nodes are registered with stable slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kUvNodes) {
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
    }

    CHECK(registry.find("TilingAndOffset") != nullptr);
    const sg::NodeDef* rotate = registry.find("Rotate");
    REQUIRE(rotate != nullptr);
    CHECK(rotate->settings.size() == 1);
    CHECK(rotate->settings[0] == "unit");
    const sg::NodeDef* flipbook = registry.find("Flipbook");
    REQUIRE(flipbook != nullptr);
    CHECK(flipbook->settings.size() == 2);
    CHECK(flipbook->settings[0] == "invertX");
    CHECK(flipbook->settings[1] == "invertY");
    REQUIRE(flipbook->slots.size() == 5);
    CHECK(static_cast<int>(flipbook->slots[4].direction) ==
          static_cast<int>(sg::SlotDirection::Output));
}

TEST_CASE("SG-6 UV nodes emit expected HLSL with a mesh UV fallback") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const EmitExpectation& expected : kUvEmitNodes) {
        CAPTURE(expected.type);
        sg::detail::EmittedGraph emitted = emitSingle(registry, expected.type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(expected.fragment) != std::string::npos);
    }
}

TEST_CASE("SG-6 UV nodes use connected UVs and settings affect codegen") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::Graph connected;
    connected.kind = sg::GraphKind::Graph;
    connected.target.id = "lit";
    REQUIRE(connected.addNode(registry.instantiate("UV", 1)));
    REQUIRE(connected.addNode(registry.instantiate("Tiling And Offset", 2)));
    REQUIRE(connected.addEdge({1, 0, 2, 0}));
    sg::detail::EmittedGraph connectedGraph = emitGraph(registry, connected);
    REQUIRE(connectedGraph.ok);
    CHECK(connectedGraph.source.find("n1_s0 * ") != std::string::npos);

    sg::Graph rotate;
    rotate.kind = sg::GraphKind::Graph;
    rotate.target.id = "lit";
    sg::NodeInstance rotateNode = registry.instantiate("Rotate", 1);
    rotateNode.settings["unit"] = sg::Value::makeString(sg::ValueType::String, "degrees");
    REQUIRE(rotate.addNode(std::move(rotateNode)));
    sg::detail::EmittedGraph rotateGraph = emitGraph(registry, rotate);
    REQUIRE(rotateGraph.ok);
    CHECK(rotateGraph.source.find("radians(") != std::string::npos);

    sg::Graph flipbook;
    flipbook.kind = sg::GraphKind::Graph;
    flipbook.target.id = "lit";
    sg::NodeInstance flipbookNode = registry.instantiate("Flipbook", 1);
    flipbookNode.settings["invertX"] = sg::Value::makeBool(true);
    flipbookNode.settings["invertY"] = sg::Value::makeBool(false);
    REQUIRE(flipbook.addNode(std::move(flipbookNode)));
    sg::detail::EmittedGraph flipbookGraph = emitGraph(registry, flipbook);
    REQUIRE(flipbookGraph.ok);
    CHECK(flipbookGraph.source.find("hbg_fb_width_1 - (") != std::string::npos);
    CHECK(flipbookGraph.source.find("hbg_fb_base_1)") != std::string::npos);
}
