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

const NodeExpectation kNodes[] = {
    {"Blend", "Artistic/Blend", 3, 1},
    {"Dither", "Artistic/Filter", 2, 1},
    {"Fade Transition", "Artistic/Filter", 3, 1},
    {"Channel Mask", "Artistic/Mask", 1, 1},
    {"Color Mask", "Artistic/Mask", 4, 1},
    {"Colorspace Conversion", "Artistic/Utility", 1, 1},
};

struct EmitExpectation {
    const char* type;
    const char* fragment;
};

const EmitExpectation kEmitNodes[] = {
    {"Blend", "hbg_blend_result1_1"},
    {"Dither", "hbg_dither_thresholds_1"},
    {"Fade Transition", "hbgSGFadeTransition"},
    {"Channel Mask", "n1_s1 = 0.0"},
    {"Color Mask", "hbg_color_mask_distance_1"},
    {"Colorspace Conversion", "hbg_cs_in_1"},
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

TEST_CASE("SG-6 artistic filter mask utility nodes are registered with stable slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
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

    const sg::NodeDef* blend = registry.find("Blend");
    REQUIRE(blend != nullptr);
    CHECK(blend->settings == std::vector<std::string>{"mode"});
    const sg::NodeDef* dither = registry.find("Dither");
    REQUIRE(dither != nullptr);
    CHECK(dither->settings.empty());
    const sg::NodeDef* fade = registry.find("Fade Transition");
    REQUIRE(fade != nullptr);
    CHECK(fade->settings.empty());
    const sg::NodeDef* channelMask = registry.find("Channel Mask");
    REQUIRE(channelMask != nullptr);
    CHECK(channelMask->settings == std::vector<std::string>{"channelMask"});
    const sg::NodeDef* colorMask = registry.find("Color Mask");
    REQUIRE(colorMask != nullptr);
    CHECK(colorMask->settings.empty());
    const sg::NodeDef* conversion = registry.find("Colorspace Conversion");
    REQUIRE(conversion != nullptr);
    CHECK(conversion->settings == std::vector<std::string>{"from", "to"});
}

TEST_CASE("SG-6 artistic filter mask utility nodes emit expected HLSL") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const EmitExpectation& expected : kEmitNodes) {
        CAPTURE(expected.type);
        sg::detail::EmittedGraph emitted = emitSingle(registry, expected.type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(expected.fragment) != std::string::npos);
    }

    sg::detail::EmittedGraph dither = emitSingle(registry, "Dither");
    REQUIRE(dither.ok);
    CHECK(dither.source.find("u_global_screenSize.xy") != std::string::npos);
    CHECK(dither.source.find("uint(") != std::string::npos);

    sg::detail::EmittedGraph fade = emitSingle(registry, "Fade Transition");
    REQUIRE(fade.ok);
    CHECK(fade.source.find("fadeValue * (fadeContrast + 1.0)") != std::string::npos);

    sg::detail::EmittedGraph colorMask = emitSingle(registry, "Color Mask");
    REQUIRE(colorMask.ok);
    CHECK(colorMask.source.find("max(") != std::string::npos);
    CHECK(colorMask.source.find("1e-5") != std::string::npos);
}

TEST_CASE("SG-6 artistic filter mask utility settings affect codegen") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::Graph blendGraph;
    blendGraph.kind = sg::GraphKind::Graph;
    blendGraph.target.id = "lit";
    sg::NodeInstance blend = registry.instantiate("Blend", 1);
    blend.settings["mode"] = sg::Value::makeString(sg::ValueType::String, "multiply");
    REQUIRE(blendGraph.addNode(std::move(blend)));
    sg::detail::EmittedGraph blendEmitted = emitGraph(registry, blendGraph);
    REQUIRE(blendEmitted.ok);
    CHECK(blendEmitted.source.find("hbg_blend_result1_1") == std::string::npos);
    CHECK(blendEmitted.source.find(" * ") != std::string::npos);

    sg::Graph vividGraph;
    vividGraph.kind = sg::GraphKind::Graph;
    vividGraph.target.id = "lit";
    sg::NodeInstance vivid = registry.instantiate("Blend", 1);
    vivid.settings["mode"] = sg::Value::makeString(sg::ValueType::String, "Vivid Light");
    REQUIRE(vividGraph.addNode(std::move(vivid)));
    sg::detail::EmittedGraph vividEmitted = emitGraph(registry, vividGraph);
    REQUIRE(vividEmitted.ok);
    CHECK(vividEmitted.source.find("hbg_blend_base_1") != std::string::npos);
    CHECK(vividEmitted.source.find("clamp(") != std::string::npos);

    sg::Graph maskGraph;
    maskGraph.kind = sg::GraphKind::Graph;
    maskGraph.target.id = "lit";
    REQUIRE(maskGraph.addNode(registry.instantiate("Vector4", 1)));
    sg::NodeInstance mask = registry.instantiate("Channel Mask", 2);
    mask.settings["channelMask"] = sg::Value::makeInteger(3);
    REQUIRE(maskGraph.addNode(std::move(mask)));
    REQUIRE(maskGraph.addEdge({1, 0, 2, 0}));
    sg::detail::EmittedGraph maskEmitted = emitGraph(registry, maskGraph);
    REQUIRE(maskEmitted.ok);
    CHECK(maskEmitted.source.find("vec4(n1_s0.x, n1_s0.y, 0.0, 0.0)") != std::string::npos);

    sg::Graph conversionGraph;
    conversionGraph.kind = sg::GraphKind::Graph;
    conversionGraph.target.id = "lit";
    sg::NodeInstance conversion = registry.instantiate("Colorspace Conversion", 1);
    conversion.settings["from"] = sg::Value::makeString(sg::ValueType::String, "linear");
    conversion.settings["to"] = sg::Value::makeString(sg::ValueType::String, "hsv");
    REQUIRE(conversionGraph.addNode(std::move(conversion)));
    sg::detail::EmittedGraph conversionEmitted = emitGraph(registry, conversionGraph);
    REQUIRE(conversionEmitted.ok);
    CHECK(conversionEmitted.source.find("1.192092896e-07") != std::string::npos);
    CHECK(conversionEmitted.source.find("step(") != std::string::npos);
    CHECK(conversionEmitted.source.find("6.0 * ") != std::string::npos);
}
