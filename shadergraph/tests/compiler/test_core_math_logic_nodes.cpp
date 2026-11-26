#include "compile/compile_internal.h"
#include "nodes/swizzle_mask.h"
#include "sg/concretize.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

struct NodeExpectation {
    const char* type;
    const char* category;
};

const NodeExpectation kNodes[] = {
    {"Append", "Channel"},
    {"Combine", "Channel"},
    {"Flip", "Channel"},
    {"Split", "Channel"},
    {"Swizzle", "Channel"},
    {"Absolute", "Math/Advanced"},
    {"Exponential", "Math/Advanced"},
    {"Length", "Math/Advanced"},
    {"Log", "Math/Advanced"},
    {"Modulo", "Math/Advanced"},
    {"Negate", "Math/Advanced"},
    {"Normalize", "Math/Advanced"},
    {"Posterize", "Math/Advanced"},
    {"Reciprocal", "Math/Advanced"},
    {"Reciprocal Square Root", "Math/Advanced"},
    {"Add", "Math/Basic"},
    {"Divide", "Math/Basic"},
    {"Multiply", "Math/Basic"},
    {"Power", "Math/Basic"},
    {"Square Root", "Math/Basic"},
    {"Subtract", "Math/Basic"},
    {"Inverse Lerp", "Math/Interpolation"},
    {"Lerp", "Math/Interpolation"},
    {"Smoothstep", "Math/Interpolation"},
    {"Clamp", "Math/Range"},
    {"Fraction", "Math/Range"},
    {"Maximum", "Math/Range"},
    {"Minimum", "Math/Range"},
    {"One Minus", "Math/Range"},
    {"Random Range", "Math/Range"},
    {"Remap", "Math/Range"},
    {"Saturate", "Math/Range"},
    {"Ceiling", "Math/Round"},
    {"Floor", "Math/Round"},
    {"Round", "Math/Round"},
    {"Sign", "Math/Round"},
    {"Step", "Math/Round"},
    {"Truncate", "Math/Round"},
    {"All", "Utility/Logic"},
    {"And", "Utility/Logic"},
    {"Any", "Utility/Logic"},
    {"Branch", "Utility/Logic"},
    {"Branch On Input Connection", "Utility/Logic"},
    {"Comparison", "Utility/Logic"},
    {"Is Front Face", "Utility/Logic"},
    {"Is Infinite", "Utility/Logic"},
    {"Is NaN", "Utility/Logic"},
    {"Nand", "Utility/Logic"},
    {"Not", "Utility/Logic"},
    {"Or", "Utility/Logic"},
};

struct ComparisonExpectation {
    const char* setting;
    const char* op;
};

const ComparisonExpectation kComparisons[] = {
    {"equal", "=="},
    {"not equal", "!="},
    {"less", "<"},
    {"less or equal", "<="},
    {"greater", ">"},
    {"greater or equal", ">="},
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
    REQUIRE(graph.addNode(registry.instantiate(type, 1)));
    return emitGraph(registry, graph);
}

} // namespace

TEST_CASE("SG-6 core math logic nodes are registered with stable categories") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->title == expected.type);
        CHECK_FALSE(definition->slots.empty());
    }

    const sg::NodeDef* swizzle = registry.find("Swizzle");
    REQUIRE(swizzle != nullptr);
    CHECK(swizzle->settings == std::vector<std::string>{"order"});
    CHECK(swizzle->synonyms == std::vector<std::string>{"swap", "reorder", "component mask"});
    CHECK(static_cast<bool>(swizzle->updateSlots));

    const sg::NodeDef* flip = registry.find("Flip");
    REQUIRE(flip != nullptr);
    CHECK(flip->settings ==
          std::vector<std::string>{"redChannel", "greenChannel", "blueChannel", "alphaChannel"});

    const sg::NodeDef* comparison = registry.find("Comparison");
    REQUIRE(comparison != nullptr);
    CHECK(comparison->settings == std::vector<std::string>{"comparison"});
    CHECK(comparison->synonyms ==
          std::vector<std::string>{"equal", "greater than", "less than"});
}

TEST_CASE("SG-6 core math logic nodes emit single-node graphs") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        const sg::detail::EmittedGraph emitted = emitSingle(registry, expected.type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find("n1_s") != std::string::npos);
    }
}

TEST_CASE("SG-6 Swizzle parses Unity RGBA aliases and rejects invalid masks") {
    const sg::SwizzleMask rgba = sg::parseSwizzleMask("rgba");
    REQUIRE(rgba.valid);
    CHECK(rgba.mask == "xyzw");
    CHECK(rgba.inputCount == 4);
    CHECK(rgba.outputCount == 4);

    const sg::SwizzleMask zyx = sg::parseSwizzleMask("ZYX");
    REQUIRE(zyx.valid);
    CHECK(zyx.mask == "zyx");
    CHECK(zyx.inputCount == 3);
    CHECK(zyx.outputCount == 3);

    const sg::SwizzleMask invalid = sg::parseSwizzleMask("xyqa");
    CHECK_FALSE(invalid.valid);
    CHECK_FALSE(sg::parseSwizzleMask("").valid);
}

TEST_CASE("SG-6 Swizzle derives input and output widths from the mask") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::NodeInstance swizzle = registry.instantiate("Swizzle", 1);
    swizzle.settings["order"] = sg::Value::makeString(sg::ValueType::String, "zyx");
    REQUIRE(graph.addNode(std::move(swizzle)));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    const sg::NodeInstance* node = graph.findNode(1);
    REQUIRE(node != nullptr);
    REQUIRE(node->slots.size() == 2);
    CHECK(static_cast<int>(node->findSlot(0)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Vector3));
    CHECK(static_cast<int>(node->findSlot(1)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Vector3));
    CHECK(emitted.source.find("n1_s1 = vec3(") != std::string::npos);
}

TEST_CASE("SG-6 Swizzle reports an invalid mask") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::NodeInstance swizzle = registry.instantiate("Swizzle", 1);
    swizzle.settings["order"] = sg::Value::makeString(sg::ValueType::String, "xyqa");
    REQUIRE(graph.addNode(std::move(swizzle)));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    CHECK_FALSE(emitted.ok);
    CHECK(std::any_of(emitted.diagnostics.begin(), emitted.diagnostics.end(),
                      [](const sg::Diagnostic& diagnostic) {
                          return diagnostic.code == sg::kErrorInvalidValue &&
                                 diagnostic.message.find("Swizzle") != std::string::npos;
                      }));
}

TEST_CASE("SG-6 Flip emits Unity-style per-channel sign toggles") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";

    sg::NodeInstance vector = registry.instantiate("Vector 4", 1);
    vector.settings["value"] = sg::Value::makeVector4({0.1f, 0.2f, 0.3f, 0.4f});
    REQUIRE(graph.addNode(std::move(vector)));

    sg::NodeInstance flip = registry.instantiate("Flip", 2);
    flip.settings["redChannel"] = sg::Value::makeBool(true);
    flip.settings["greenChannel"] = sg::Value::makeBool(false);
    flip.settings["blueChannel"] = sg::Value::makeBool(true);
    flip.settings["alphaChannel"] = sg::Value::makeBool(false);
    REQUIRE(graph.addNode(std::move(flip)));
    REQUIRE(graph.addEdge({1, 0, 2, 0}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("(-1.0 * n1_s0.x)") != std::string::npos);
    CHECK(emitted.source.find("n1_s0.y") != std::string::npos);
    CHECK(emitted.source.find("(-1.0 * n1_s0.z)") != std::string::npos);
    CHECK(emitted.source.find("n1_s0.w") != std::string::npos);

    sg::Graph scalarGraph;
    scalarGraph.kind = sg::GraphKind::Graph;
    scalarGraph.target.id = "lit";
    sg::NodeInstance scalar = registry.instantiate("Float", 1);
    scalar.settings["value"] = sg::Value::makeFloat(0.5f);
    REQUIRE(scalarGraph.addNode(std::move(scalar)));
    sg::NodeInstance scalarFlip = registry.instantiate("Flip", 2);
    scalarFlip.settings["redChannel"] = sg::Value::makeBool(true);
    REQUIRE(scalarGraph.addNode(std::move(scalarFlip)));
    REQUIRE(scalarGraph.addEdge({1, 0, 2, 0}));

    const sg::detail::EmittedGraph scalarEmitted = emitGraph(registry, scalarGraph);
    REQUIRE(scalarEmitted.ok);
    CHECK(scalarEmitted.source.find("n2_s1 = (-1.0 * n1_s0);") != std::string::npos);
}

TEST_CASE("SG-6 Comparison emits every comparison operator") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const ComparisonExpectation& expected : kComparisons) {
        CAPTURE(expected.setting);
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";
        sg::NodeInstance node = registry.instantiate("Comparison", 1);
        node.settings["comparison"] =
            sg::Value::makeString(sg::ValueType::String, expected.setting);
        REQUIRE(graph.addNode(std::move(node)));
        const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(std::string("n1_s2 = (0.0 ") + expected.op) !=
              std::string::npos);
    }
}
