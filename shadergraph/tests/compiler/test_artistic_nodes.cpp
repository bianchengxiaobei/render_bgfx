#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

namespace {

struct NodeExpectation {
    const char* type;
    int inputs;
    int outputs;
};

const NodeExpectation kArtisticNodes[] = {
    {"Contrast", 2, 1},
    {"Saturation", 2, 1},
    {"Hue", 2, 1},
    {"White Balance", 3, 1},
    {"Replace Color", 5, 1},
    {"Invert Colors", 1, 1},
    {"Channel Mixer", 1, 1},
};

struct EmitExpectation {
    const char* type;
    const char* fragment;
};

const EmitExpectation kArtisticEmitNodes[] = {
    {"Contrast", "pow(0.5, 2.2)"},
    {"Saturation", "0.2126729"},
    {"Hue", "hbg_hue_hsv_1"},
    {"White Balance", "hbg_wb_lms2lin_1"},
    {"Replace Color", "hbg_replace_distance_1"},
    {"Invert Colors", "abs("},
    {"Channel Mixer", "vec3(1.0, 0.0, 0.0)"},
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

TEST_CASE("SG-6 artistic adjustment nodes are registered with stable slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kArtisticNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == "Artistic/Adjustment");
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

    const sg::NodeDef* hue = registry.find("Hue");
    REQUIRE(hue != nullptr);
    CHECK(hue->settings == std::vector<std::string>{"range"});
    const sg::NodeDef* invert = registry.find("Invert Colors");
    REQUIRE(invert != nullptr);
    CHECK(invert->settings ==
          std::vector<std::string>{"redChannel", "greenChannel", "blueChannel", "alphaChannel"});
    CHECK(static_cast<int>(invert->slots[0].type) ==
          static_cast<int>(sg::ValueType::DynamicVector));
    CHECK(static_cast<int>(invert->slots[1].type) ==
          static_cast<int>(sg::ValueType::DynamicVector));
    const sg::NodeDef* mixer = registry.find("Channel Mixer");
    REQUIRE(mixer != nullptr);
    CHECK(mixer->settings == std::vector<std::string>{"outRed", "outGreen", "outBlue"});
    const sg::NodeDef* replace = registry.find("Replace Color");
    REQUIRE(replace != nullptr);
    bool hasFuzziness = false;
    for (const sg::SlotSpec& slot : replace->slots) {
        if (slot.id == 5) {
            hasFuzziness = slot.direction == sg::SlotDirection::Input;
        }
    }
    CHECK(hasFuzziness);
}

TEST_CASE("SG-6 artistic adjustment nodes emit expected HLSL") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const EmitExpectation& expected : kArtisticEmitNodes) {
        CAPTURE(expected.type);
        sg::detail::EmittedGraph emitted = emitSingle(registry, expected.type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(expected.fragment) != std::string::npos);
    }

    sg::detail::EmittedGraph hue = emitSingle(registry, "Hue");
    REQUIRE(hue.ok);
    CHECK(hue.source.find("/ 360.0") != std::string::npos);
}

TEST_CASE("SG-6 artistic adjustment settings affect codegen") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::Graph hueGraph;
    hueGraph.kind = sg::GraphKind::Graph;
    hueGraph.target.id = "lit";
    sg::NodeInstance hue = registry.instantiate("Hue", 1);
    hue.settings["range"] = sg::Value::makeString(sg::ValueType::String, "normalized");
    REQUIRE(hueGraph.addNode(std::move(hue)));
    sg::detail::EmittedGraph hueEmitted = emitGraph(registry, hueGraph);
    REQUIRE(hueEmitted.ok);
    CHECK(hueEmitted.source.find("/ 360.0") == std::string::npos);

    sg::Graph invertGraph;
    invertGraph.kind = sg::GraphKind::Graph;
    invertGraph.target.id = "lit";
    REQUIRE(invertGraph.addNode(registry.instantiate("Vector4", 1)));
    sg::NodeInstance invert = registry.instantiate("Invert Colors", 2);
    invert.settings["redChannel"] = sg::Value::makeBool(true);
    invert.settings["greenChannel"] = sg::Value::makeBool(true);
    invert.settings["blueChannel"] = sg::Value::makeBool(true);
    invert.settings["alphaChannel"] = sg::Value::makeBool(false);
    REQUIRE(invertGraph.addNode(std::move(invert)));
    REQUIRE(invertGraph.addEdge({1, 0, 2, 0}));
    sg::detail::EmittedGraph invertEmitted = emitGraph(registry, invertGraph);
    REQUIRE(invertEmitted.ok);
    CHECK(invertEmitted.source.find("vec4(1.0, 1.0, 1.0, 0.0)") != std::string::npos);

    sg::Graph mixerGraph;
    mixerGraph.kind = sg::GraphKind::Graph;
    mixerGraph.target.id = "lit";
    sg::NodeInstance mixer = registry.instantiate("Channel Mixer", 1);
    mixer.settings["outRed"] =
        sg::Value::makeVector3(sg::Vec3{0.0f, 1.0f, 0.0f});
    mixer.settings["outGreen"] =
        sg::Value::makeVector3(sg::Vec3{1.0f, 0.0f, 0.0f});
    mixer.settings["outBlue"] =
        sg::Value::makeVector3(sg::Vec3{0.0f, 0.0f, 1.0f});
    REQUIRE(mixerGraph.addNode(std::move(mixer)));
    sg::detail::EmittedGraph mixerEmitted = emitGraph(registry, mixerGraph);
    REQUIRE(mixerEmitted.ok);
    CHECK(mixerEmitted.source.find("vec3(0.0, 1.0, 0.0)") != std::string::npos);
    CHECK(mixerEmitted.source.find("vec3(1.0, 0.0, 0.0)") != std::string::npos);
}
