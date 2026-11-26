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

const NodeExpectation kProceduralNodes[] = {
    {"Checkerboard", "Procedural", 4, 1},
    {"Ellipse", "Procedural/Shape", 3, 1},
    {"Polygon", "Procedural/Shape", 4, 1},
    {"Rectangle", "Procedural/Shape", 3, 1},
    {"Rounded Polygon", "Procedural/Shape", 5, 1},
    {"Rounded Rectangle", "Procedural/Shape", 4, 1},
};

struct EmitExpectation {
    const char* type;
    const char* fragment;
};

const EmitExpectation kProceduralEmitNodes[] = {
    {"Checkerboard", "ddx("},
    {"Ellipse", "fwidth("},
    {"Polygon", "atan2("},
    {"Rectangle", "min(hbg_rect_d_"},
    {"Rounded Polygon", "hbg_rp_dist_c_"},
    {"Rounded Rectangle", "hbg_rounded_rect_radius_"},
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

TEST_CASE("SG-6 procedural shape nodes are registered with stable slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kProceduralNodes) {
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

    const sg::NodeDef* rectangle = registry.find("Rectangle");
    REQUIRE(rectangle != nullptr);
    CHECK(rectangle->settings.size() == 1);
    CHECK(rectangle->settings[0] == "clampType");
    const sg::NodeDef* roundedPolygon = registry.find("Rounded Polygon");
    REQUIRE(roundedPolygon != nullptr);
    REQUIRE(roundedPolygon->slots.size() == 6);
    CHECK(static_cast<int>(roundedPolygon->slots[5].direction) ==
          static_cast<int>(sg::SlotDirection::Output));
    CHECK(static_cast<int>(roundedPolygon->slots[5].type) ==
          static_cast<int>(sg::ValueType::Float));
}

TEST_CASE("SG-6 procedural nodes emit expected HLSL with a mesh UV fallback") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const EmitExpectation& expected : kProceduralEmitNodes) {
        CAPTURE(expected.type);
        sg::detail::EmittedGraph emitted = emitSingle(registry, expected.type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find("gi.uv0") != std::string::npos);
        CHECK(emitted.source.find(expected.fragment) != std::string::npos);
    }
}

TEST_CASE("SG-6 procedural nodes use connected UVs and settings affect codegen") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::Graph connected;
    connected.kind = sg::GraphKind::Graph;
    connected.target.id = "lit";
    REQUIRE(connected.addNode(registry.instantiate("UV", 1)));
    REQUIRE(connected.addNode(registry.instantiate("Rectangle", 2)));
    REQUIRE(connected.addEdge({1, 0, 2, 0}));
    sg::detail::EmittedGraph connectedGraph = emitGraph(registry, connected);
    REQUIRE(connectedGraph.ok);
    CHECK(connectedGraph.source.find("n1_s0 * 2.0") != std::string::npos);

    sg::Graph rectangle;
    rectangle.kind = sg::GraphKind::Graph;
    rectangle.target.id = "lit";
    sg::NodeInstance rectangleNode = registry.instantiate("Rectangle", 1);
    rectangleNode.settings["clampType"] =
        sg::Value::makeString(sg::ValueType::String, "nicest");
    REQUIRE(rectangle.addNode(std::move(rectangleNode)));
    sg::detail::EmittedGraph rectangleGraph = emitGraph(registry, rectangle);
    REQUIRE(rectangleGraph.ok);
    CHECK(rectangleGraph.source.find("hbg_rect_f_1 = min(fwidth(") != std::string::npos);
    CHECK(rectangleGraph.source.find("min(hbg_rect_o_1") != std::string::npos);

    sg::Graph checkerboard;
    checkerboard.kind = sg::GraphKind::Graph;
    checkerboard.target.id = "lit";
    REQUIRE(checkerboard.addNode(registry.instantiate("Checkerboard", 1)));
    sg::detail::EmittedGraph checkerboardGraph = emitGraph(registry, checkerboard);
    REQUIRE(checkerboardGraph.ok);
    CHECK(checkerboardGraph.source.find("lerp(vec3(0.2") != std::string::npos);
}
