#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"
#include "sg/validate.h"

#include <doctest/doctest.h>

#include <algorithm>
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
    {"Element Layout UV", "Input/UI", 0, 1},
    {"Element Opacity", "Input/UI", 0, 1},
    {"Element Texture Size", "Input/UI", 0, 4},
    {"Element Texture UV", "Input/UI", 0, 1},
    {"Element Tint", "Input/UI", 0, 1},
    {"SDF Text Parameters", "Input/UI", 0, 7},
    {"Default Solid", "UI", 0, 1},
    {"Default Texture", "UI", 2, 1},
    {"Default Gradient", "UI", 0, 1},
    {"Default SDF Text", "UI", 8, 1},
    {"Default Bitmap Text", "UI", 1, 1},
    {"Is Forced Gamma", "UI", 0, 1},
    {"Render Type", "UI", 0, 5},
    {"Render Type Branch", "UI", 5, 2},
    {"Sample Element Texture", "UI", 5, 4},
};

struct EmitExpectation {
    const char* type;
    const char* fragment;
};

struct PlatformExpectation {
    const char* type;
    const char* category;
    int inputs;
    int outputs;
    const char* target;
};

const PlatformExpectation kPlatformNodes[] = {
    {"Terrain Properties", "Terrain", 0, 3, "terrain"},
    {"Terrain Texture", "Terrain", 1, 9, "terrain"},
    {"Compute Deformation", "Input/Mesh Deformation", 0, 3, "mesh-deformation"},
    {"Linear Blend Skinning", "Input/Mesh Deformation", 3, 3, "mesh-deformation"},
    {"Instance", "Custom Interpolators", 0, 1, nullptr},
};

const EmitExpectation kEmitNodes[] = {
    {"Element Layout UV", "n1_s0 = gi.uv0;"},
    {"Element Opacity", "n1_s0 = 1.0;"},
    {"Element Texture Size", "n1_s3 = 1.0;"},
    {"Element Texture UV", "n1_s0 = gi.uv0;"},
    {"Element Tint", "n1_s0 = vec4(1.0, 1.0, 1.0, 1.0);"},
    {"SDF Text Parameters", "n1_s0 = vec4(0.0, 0.0, 0.0, 0.0);"},
    {"Default Solid", "n1_s0 = vec4(1.0, 1.0, 1.0, 1.0);"},
    {"Default Texture", "n1_s2 = vec4(1.0, 1.0, 1.0, 1.0);"},
    {"Default Gradient", "n1_s0 = vec4(1.0, 1.0, 1.0, 1.0);"},
    {"Default SDF Text", "n1_s1 = vec4(1.0, 1.0, 1.0, 1.0);"},
    {"Default Bitmap Text", "n1_s1 = vec4(1.0, 1.0, 1.0, 1.0);"},
    {"Is Forced Gamma", "n1_s0 = false;"},
    {"Render Type", "n1_s0 = true;"},
    {"Render Type Branch", "n1_s5 = vec4(1.0, 1.0, 1.0, 1.0).rgb;"},
    {"Sample Element Texture", "n1_s10 = vec4(1.0, 1.0, 1.0, 1.0);"},
    {"Terrain Properties", "n1_s2 = 4.0;"},
    {"Terrain Texture", "n1_s9 = vec4(1.0, 1.0, 1.0, 1.0);"},
    {"Compute Deformation", "n1_s0 = gi.positionOS;"},
    {"Linear Blend Skinning", "n1_s5 = vec3(1.0, 0.0, 0.0);"},
    {"Instance", "n1_s0 = vec4(0.0, 0.0, 0.0, 0.0);"},
};

sg::detail::EmittedGraph emitGraph(const sg::NodeRegistry& registry, sg::Graph& graph,
                                   const char* targetId) {
    REQUIRE(sg::concretize(graph, registry).ok);
    const sg::detail::TargetSpec* target = sg::detail::findTarget(targetId);
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

sg::detail::EmittedGraph emitSinglePreview(const sg::NodeRegistry& registry, const char* type) {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "preview";
    graph.nodes.push_back(registry.instantiate(type, 1));
    return emitGraph(registry, graph, "preview");
}

bool hasCode(const std::vector<sg::Diagnostic>& diagnostics, const char* code) {
    for (const sg::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.code == code) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("SG-6C UI nodes are registered as target specific") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->title == expected.type);
        CHECK(definition->isTargetSpecific);
        CHECK(definition->allowedTargets == std::vector<std::string>{"preview", "ui"});
        CHECK(std::find(definition->allowedTargets.begin(), definition->allowedTargets.end(),
                        "lit") == definition->allowedTargets.end());
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

    const sg::NodeDef* sample = registry.find("Sample Element Texture");
    REQUIRE(sample != nullptr);
    CHECK(sample->settings == std::vector<std::string>{"sampleCount", "mipSamplingMode"});
    CHECK_FALSE(sample->hasPreview);

    const sg::NodeDef* layout = registry.find("Element Layout UV");
    REQUIRE(layout != nullptr);
    CHECK_FALSE(layout->hasPreview);
    const sg::NodeDef* tint = registry.find("Element Tint");
    REQUIRE(tint != nullptr);
    CHECK(tint->synonyms == std::vector<std::string>{"color", "vertex color", "dynamic color"});

    for (const PlatformExpectation& expected : kPlatformNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->title == expected.type);
        CHECK(definition->isTargetSpecific);
        const std::vector<std::string> targets =
            expected.target == nullptr
                ? std::vector<std::string>{"preview"}
                : std::vector<std::string>{"preview", expected.target};
        CHECK(definition->allowedTargets == targets);
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

    const sg::NodeDef* terrainTexture = registry.find("Terrain Texture");
    REQUIRE(terrainTexture != nullptr);
    CHECK(terrainTexture->settings == std::vector<std::string>{"textureType"});
    const sg::NodeDef* interpolator = registry.find("Instance");
    REQUIRE(interpolator != nullptr);
    CHECK_FALSE(interpolator->allowInSubGraph);
    CHECK(interpolator->settings ==
          std::vector<std::string>{"customBlockNodeName", "type"});
}

TEST_CASE("SG-6C UI nodes are rejected by the lit target") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    REQUIRE(graph.addNode(registry.instantiate("Default Solid", 1)));
    const sg::ValidationResult result = sg::validate(graph, registry, true);
    CHECK_FALSE(result.ok);
    CHECK(hasCode(result.diagnostics, sg::kErrorTargetUnsupported));
}

TEST_CASE("SG-6C UI nodes emit preview fallbacks") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const EmitExpectation& expected : kEmitNodes) {
        CAPTURE(expected.type);
        const sg::detail::EmittedGraph emitted = emitSinglePreview(registry, expected.type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(expected.fragment) != std::string::npos);
    }
}

TEST_CASE("SG-6C UI nodes share a preview graph without codegen collisions") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "preview";
    sg::NodeId id = 1;
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        REQUIRE(graph.addNode(registry.instantiate(expected.type, id++)));
    }
    for (const PlatformExpectation& expected : kPlatformNodes) {
        CAPTURE(expected.type);
        REQUIRE(graph.addNode(registry.instantiate(expected.type, id++)));
    }
    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph, "preview");
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("GraphSurface hbgEvaluateGraphSurface") != std::string::npos);
    CHECK(emitted.source.find("n15_s13 = vec4(1.0, 1.0, 1.0, 1.0);") != std::string::npos);
    CHECK(emitted.source.find("n20_s0 = vec4(0.0, 0.0, 0.0, 0.0);") != std::string::npos);
}
