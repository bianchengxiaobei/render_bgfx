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

const NodeExpectation kNormalNodes[] = {
    {"Normal Blend", 2, 1},
    {"Normal Strength", 2, 1},
    {"Normal Reconstruct Z", 1, 1},
    {"Normal From Height", 2, 1},
    {"Normal From Texture", 5, 1},
    {"Normal Unpack", 1, 1},
};

struct EmitExpectation {
    const char* type;
    const char* fragment;
};

const EmitExpectation kNormalEmitNodes[] = {
    {"Normal Blend", "hbgSGSafeNormalize(vec3("},
    {"Normal Strength", "lerp(1.0,"},
    {"Normal Reconstruct Z", "hbg_normal_reconstruct_z_1"},
    {"Normal From Height", "hbg_normal_height_surface_1"},
    {"Normal From Texture", "hbg_normal_texture_va_1"},
    {"Normal Unpack", "hbg_normal_unpack_xy_1"},
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
    sg::NodeInstance node = registry.instantiate(type, 1);
    if (std::string(type) == "Normal From Texture") {
        node.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_Height");
    }
    graph.nodes.push_back(std::move(node));
    return emitGraph(registry, graph);
}

} // namespace

TEST_CASE("SG-6 artistic normal nodes are registered with stable slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNormalNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == "Artistic/Normal");
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

    const sg::NodeDef* blend = registry.find("Normal Blend");
    REQUIRE(blend != nullptr);
    CHECK(blend->settings == std::vector<std::string>{"mode"});
    const sg::NodeDef* fromHeight = registry.find("Normal From Height");
    REQUIRE(fromHeight != nullptr);
    CHECK(fromHeight->settings == std::vector<std::string>{"outputSpace"});
    const sg::NodeDef* fromTexture = registry.find("Normal From Texture");
    REQUIRE(fromTexture != nullptr);
    CHECK(fromTexture->settings == std::vector<std::string>{"texture"});
    const sg::NodeDef* unpack = registry.find("Normal Unpack");
    REQUIRE(unpack != nullptr);
    CHECK(unpack->settings == std::vector<std::string>{"space"});
    const sg::NodeDef* strength = registry.find("Normal From Height");
    REQUIRE(strength != nullptr);
    REQUIRE(strength->slots[1].defaultValue.has_value());
    REQUIRE(strength->slots[1].defaultValue->asFloat() != nullptr);
    CHECK(*strength->slots[1].defaultValue->asFloat() == doctest::Approx(0.01f));
}

TEST_CASE("SG-6 artistic normal nodes emit expected HLSL") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const EmitExpectation& expected : kNormalEmitNodes) {
        CAPTURE(expected.type);
        sg::detail::EmittedGraph emitted = emitSingle(registry, expected.type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(expected.fragment) != std::string::npos);
    }

    sg::detail::EmittedGraph fromHeight = emitSingle(registry, "Normal From Height");
    REQUIRE(fromHeight.ok);
    CHECK(fromHeight.source.find("hbgSGTransformWorldToTangent") != std::string::npos);
    sg::detail::EmittedGraph fromTexture = emitSingle(registry, "Normal From Texture");
    REQUIRE(fromTexture.ok);
    CHECK(fromTexture.source.find("s_sg__Height") != std::string::npos);
}

TEST_CASE("SG-6 artistic normal settings affect codegen") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::Graph blendGraph;
    blendGraph.kind = sg::GraphKind::Graph;
    blendGraph.target.id = "lit";
    sg::NodeInstance blend = registry.instantiate("Normal Blend", 1);
    blend.settings["mode"] = sg::Value::makeString(sg::ValueType::String, "reoriented");
    REQUIRE(blendGraph.addNode(std::move(blend)));
    sg::detail::EmittedGraph blendEmitted = emitGraph(registry, blendGraph);
    REQUIRE(blendEmitted.ok);
    CHECK(blendEmitted.source.find("hbg_normal_blend_t_1") != std::string::npos);

    sg::Graph heightGraph;
    heightGraph.kind = sg::GraphKind::Graph;
    heightGraph.target.id = "lit";
    sg::NodeInstance height = registry.instantiate("Normal From Height", 1);
    height.settings["outputSpace"] = sg::Value::makeString(sg::ValueType::String, "world");
    REQUIRE(heightGraph.addNode(std::move(height)));
    sg::detail::EmittedGraph heightEmitted = emitGraph(registry, heightGraph);
    REQUIRE(heightEmitted.ok);
    CHECK(heightEmitted.source.find("= hbgSGTransformWorldToTangent(") == std::string::npos);

    sg::Graph unpackGraph;
    unpackGraph.kind = sg::GraphKind::Graph;
    unpackGraph.target.id = "lit";
    sg::NodeInstance unpack = registry.instantiate("Normal Unpack", 1);
    unpack.settings["space"] = sg::Value::makeString(sg::ValueType::String, "object");
    REQUIRE(unpackGraph.addNode(std::move(unpack)));
    sg::detail::EmittedGraph unpackEmitted = emitGraph(registry, unpackGraph);
    REQUIRE(unpackEmitted.ok);
    CHECK(unpackEmitted.source.find("hbg_normal_unpack_xy_1") == std::string::npos);
    CHECK(unpackEmitted.source.find("* 2.0 - 1.0") != std::string::npos);
}
