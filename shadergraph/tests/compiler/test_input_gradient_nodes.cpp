#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

namespace {

sg::Gradient makeGradient() {
    sg::Gradient gradient;
    gradient.keys.push_back({0.0f, sg::Vec4{1.0f, 0.0f, 0.0f, 1.0f}});
    gradient.keys.push_back({1.0f, sg::Vec4{0.0f, 0.0f, 1.0f, 1.0f}});
    return gradient;
}

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

sg::detail::EmittedGraph emitNode(const sg::NodeRegistry& registry, const char* type,
                                  std::map<std::string, sg::Value> settings = {}) {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::NodeInstance node = registry.instantiate(type, 1);
    for (auto& [key, value] : settings) {
        node.settings[key] = std::move(value);
    }
    graph.nodes.push_back(std::move(node));
    return emitGraph(registry, graph);
}

} // namespace

TEST_CASE("SG-6 Input/Gradient nodes expose canonical metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::NodeDef* gradient = registry.find("Gradient");
    REQUIRE(gradient != nullptr);
    CHECK(gradient->category == "Input/Gradient");
    CHECK(gradient->title == "Gradient");
    CHECK(gradient->synonyms == std::vector<std::string>{"ramp"});
    CHECK(gradient->settings == std::vector<std::string>{"gradient", "mode"});
    CHECK(static_cast<int>(gradient->slots.size()) == 1);
    CHECK(gradient->slots[0].id == 0);
    CHECK(gradient->slots[0].name == "Out");
    CHECK(static_cast<int>(gradient->slots[0].type) == static_cast<int>(sg::ValueType::Gradient));
    CHECK_FALSE(gradient->hasPreview);

    const sg::NodeDef* sample = registry.find("Sample Gradient");
    REQUIRE(sample != nullptr);
    CHECK(sample->category == "Input/Gradient");
    CHECK(sample->title == "Sample Gradient");
    CHECK(static_cast<int>(sample->slots.size()) == 3);
    CHECK(sample->slots[0].name == "Gradient");
    CHECK(static_cast<int>(sample->slots[0].type) == static_cast<int>(sg::ValueType::Gradient));
    CHECK_FALSE(sample->slots[0].supportsLiteral);
    CHECK(sample->slots[1].name == "Time");
    CHECK(sample->slots[2].id == 2);
    CHECK(sample->slots[2].name == "Out");
    CHECK(static_cast<int>(sample->slots[2].type) == static_cast<int>(sg::ValueType::Vector4));
    CHECK(sample->typeVersion == 1);

    const sg::NodeDef* blackbody = registry.find("Blackbody");
    REQUIRE(blackbody != nullptr);
    CHECK(blackbody->category == "Input/Gradient");
    CHECK(blackbody->title == "Blackbody");
    CHECK(static_cast<int>(blackbody->slots.size()) == 2);
    CHECK(blackbody->slots[0].name == "Temperature");
    REQUIRE(blackbody->slots[0].defaultValue.has_value());
    REQUIRE(blackbody->slots[0].defaultValue->asFloat() != nullptr);
    CHECK(*blackbody->slots[0].defaultValue->asFloat() == doctest::Approx(512.0f));
    CHECK(blackbody->slots[1].id == 1);
    CHECK(static_cast<int>(blackbody->slots[1].type) == static_cast<int>(sg::ValueType::Vector3));
}

TEST_CASE("SG-6 Gradient node emits Unity gradient key layout and mode") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    std::map<std::string, sg::Value> settings;
    settings["gradient"] = sg::Value::makeGradient(makeGradient());
    settings["mode"] = sg::Value::makeString(sg::ValueType::String, "fixed");
    const sg::detail::EmittedGraph emitted =
        emitNode(registry, "Gradient", std::move(settings));
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("struct HbgSGGradient") != std::string::npos);
    CHECK(emitted.source.find(
              "n1_s0 = hbgSGNewGradient(1, 2, 2, vec4(1.0, 0.0, 0.0, 0.0), "
              "vec4(0.0, 0.0, 1.0, 1.0), vec4(0.0, 0.0, 0.0, 0.0),") != std::string::npos);
    CHECK(emitted.source.find("vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 0.0)") !=
          std::string::npos);
}

TEST_CASE("SG-6 Sample Gradient samples a connected Gradient node") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";

    sg::NodeInstance gradient = registry.instantiate("Gradient", 1);
    gradient.settings["gradient"] = sg::Value::makeGradient(makeGradient());
    gradient.settings["mode"] = sg::Value::makeString(sg::ValueType::String, "perceptual");
    graph.nodes.push_back(std::move(gradient));
    graph.nodes.push_back(registry.instantiate("Sample Gradient", 2));
    graph.edges.push_back({1, 0, 2, 0});

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("hbgSGNewGradient(2, 2, 2,") != std::string::npos);
    CHECK(emitted.source.find("n2_s2 = hbgSGSampleGradient(n1_s0, 0.0);") !=
          std::string::npos);
    CHECK(emitted.source.find("hbgSGLinearToOklab") != std::string::npos);
    CHECK(emitted.source.find("hbgSGOklabToLinear") != std::string::npos);
}

TEST_CASE("SG-6 Sample Gradient and Blackbody keep Unity fallback and formula") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph sample = emitNode(registry, "Sample Gradient");
    REQUIRE(sample.ok);
    CHECK(sample.source.find(
              "n1_s2 = hbgSGSampleGradient(hbgSGNewGradient(0, 2, 2, "
              "vec4(1.0, 1.0, 1.0, 0.0), vec4(1.0, 1.0, 1.0, 1.0),") != std::string::npos);
    CHECK(sample.source.find("vec2(1.0, 0.0), vec2(1.0, 1.0), vec2(0.0, 0.0)") !=
          std::string::npos);

    const sg::detail::EmittedGraph blackbody = emitNode(registry, "Blackbody");
    REQUIRE(blackbody.ok);
    CHECK(blackbody.source.find("n1_s1 = vec3(255.0, 255.0, 255.0);") !=
          std::string::npos);
    CHECK(blackbody.source.find("56100000.0 * pow(hbg_sg_blackbody_1, (-3.0 / 2.0)) + 148.0") !=
          std::string::npos);
    CHECK(blackbody.source.find("100.04 * log(hbg_sg_blackbody_1) - 623.6") !=
          std::string::npos);
    CHECK(blackbody.source.find("35200000.0 * pow(hbg_sg_blackbody_1, (-3.0 / 2.0)) + 184.0") !=
          std::string::npos);
    CHECK(blackbody.source.find("194.18 * log(hbg_sg_blackbody_1) - 1448.6") !=
          std::string::npos);
    CHECK(blackbody.source.find("hbg_sg_blackbody_1 / 1000.0") != std::string::npos);
}
