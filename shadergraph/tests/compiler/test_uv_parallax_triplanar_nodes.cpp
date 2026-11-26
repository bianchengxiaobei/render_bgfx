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

const NodeExpectation kNodes[] = {
    {"Parallax Mapping", 4, 1},
    {"Parallax Occlusion Mapping", 10, 2},
    {"Triplanar", 6, 1},
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
    if (std::string(type) == "Parallax Mapping") {
        node.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_Height");
    } else if (std::string(type) == "Parallax Occlusion Mapping") {
        node.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_PomHeight");
    } else if (std::string(type) == "Triplanar") {
        node.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_Tri");
    }
    graph.nodes.push_back(std::move(node));
    return emitGraph(registry, graph);
}

} // namespace

TEST_CASE("SG-6 UV parallax and triplanar nodes are registered with stable slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == "UV");
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

    const sg::NodeDef* parallax = registry.find("Parallax Mapping");
    REQUIRE(parallax != nullptr);
    CHECK(parallax->settings == std::vector<std::string>{"texture", "channel"});
    CHECK_FALSE(parallax->hasPreview);
    const sg::NodeDef* pom = registry.find("Parallax Occlusion Mapping");
    REQUIRE(pom != nullptr);
    CHECK(pom->settings == std::vector<std::string>{"texture", "channel"});
    CHECK_FALSE(pom->hasPreview);
    const sg::NodeDef* triplanar = registry.find("Triplanar");
    REQUIRE(triplanar != nullptr);
    CHECK(triplanar->settings ==
          std::vector<std::string>{"texture", "textureType", "inputSpace", "normalOutputSpace"});
    CHECK(triplanar->hasPreview);

    const sg::NodeDef* pomDef = registry.find("Parallax Occlusion Mapping");
    REQUIRE(pomDef != nullptr);
    bool foundSteps = false;
    for (const sg::SlotSpec& slot : pomDef->slots) {
        if (slot.id == 5) {
            foundSteps = slot.defaultValue.has_value();
            if (foundSteps) {
                REQUIRE(slot.defaultValue->asFloat() != nullptr);
                CHECK(*slot.defaultValue->asFloat() == doctest::Approx(5.0f));
            }
        }
    }
    CHECK(foundSteps);
}

TEST_CASE("SG-6 UV parallax and triplanar nodes emit expected HLSL") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::detail::EmittedGraph parallax = emitSingle(registry, "Parallax Mapping");
    REQUIRE(parallax.ok);
    CHECK(parallax.source.find("hbg_parallax_scale_1") != std::string::npos);
    CHECK(parallax.source.find("texture2D(s_sg__Height") != std::string::npos);
    CHECK(parallax.source.find("0.42") != std::string::npos);
    CHECK(parallax.source.find(".g") != std::string::npos);

    sg::detail::EmittedGraph pom = emitSingle(registry, "Parallax Occlusion Mapping");
    REQUIRE(pom.ok);
    CHECK(pom.source.find("hbgSGDisplacementObjectScale") != std::string::npos);
    CHECK(pom.source.find("texture2DLod(s_sg__PomHeight") != std::string::npos);
    CHECK(pom.source.find("hbg_pom_num_steps_1") != std::string::npos);
    CHECK(pom.source.find("for (int stepIndex") != std::string::npos);
    CHECK(pom.source.find("hbg_pom_offset_1") != std::string::npos);
    CHECK(pom.source.find("saturate(") != std::string::npos);

    sg::detail::EmittedGraph triplanar = emitSingle(registry, "Triplanar");
    REQUIRE(triplanar.ok);
    CHECK(triplanar.source.find("hbg_tri_uv_1") != std::string::npos);
    CHECK(triplanar.source.find("texture2D(s_sg__Tri") != std::string::npos);
    CHECK(triplanar.source.find("hbg_tri_weights_1") != std::string::npos);
    CHECK(triplanar.source.find("dot(") != std::string::npos);
}

TEST_CASE("SG-6 parallax and triplanar nodes accept Property texture inputs") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::Graph parallaxGraph;
    parallaxGraph.kind = sg::GraphKind::Graph;
    parallaxGraph.target.id = "lit";
    sg::Property height;
    height.name = "Height";
    height.refName = "_Height";
    height.type = sg::ValueType::Texture2D;
    height.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "height.png");
    height.useTilingAndOffset = true;
    height.tiling = {2.0f, 2.0f};
    height.offset = {0.5f, 0.25f};
    parallaxGraph.properties.push_back(height);
    sg::NodeInstance heightProperty = registry.instantiate("Property", 1);
    heightProperty.settings["property"] =
        sg::Value::makeString(sg::ValueType::String, "_Height");
    sg::NodeInstance parallax = registry.instantiate("Parallax Mapping", 2);
    parallaxGraph.nodes.push_back(std::move(heightProperty));
    parallaxGraph.nodes.push_back(std::move(parallax));
    parallaxGraph.edges.push_back(sg::Edge{1, 0, 2, 1});
    const sg::detail::EmittedGraph parallaxEmitted = emitGraph(registry, parallaxGraph);
    REQUIRE(parallaxEmitted.ok);
    CHECK(parallaxEmitted.source.find("SAMPLER2D(s_sg__Height,") != std::string::npos);
    CHECK(parallaxEmitted.source.find("uniform vec4 u_sg__Height_ST;") !=
          std::string::npos);
    CHECK(parallaxEmitted.source.find(
              "texture2D(s_sg__Height, (gi.uv0 * u_sg__Height_ST.xy + "
              "u_sg__Height_ST.zw))") != std::string::npos);

    sg::Graph pomGraph;
    pomGraph.kind = sg::GraphKind::Graph;
    pomGraph.target.id = "lit";
    sg::Property pomHeight;
    pomHeight.name = "POM Height";
    pomHeight.refName = "_PomHeight";
    pomHeight.type = sg::ValueType::Texture2D;
    pomHeight.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "height.png");
    pomHeight.useTilingAndOffset = true;
    pomHeight.tiling = {3.0f, 1.0f};
    pomHeight.offset = {0.1f, 0.2f};
    pomGraph.properties.push_back(pomHeight);
    sg::NodeInstance pomProperty = registry.instantiate("Property", 1);
    pomProperty.settings["property"] =
        sg::Value::makeString(sg::ValueType::String, "_PomHeight");
    sg::NodeInstance pom = registry.instantiate("Parallax Occlusion Mapping", 2);
    pomGraph.nodes.push_back(std::move(pomProperty));
    pomGraph.nodes.push_back(std::move(pom));
    pomGraph.edges.push_back(sg::Edge{1, 0, 2, 2});
    const sg::detail::EmittedGraph pomEmitted = emitGraph(registry, pomGraph);
    REQUIRE(pomEmitted.ok);
    CHECK(pomEmitted.source.find("SAMPLER2D(s_sg__PomHeight,") != std::string::npos);
    CHECK(pomEmitted.source.find("uniform vec4 u_sg__PomHeight_ST;") !=
          std::string::npos);
    CHECK(pomEmitted.source.find(
              "hbg_pom_base_uv_2 = ((gi.uv0 * vec2(1.0, 1.0) + vec2(0.0, 0.0)) * "
              "u_sg__PomHeight_ST.xy + u_sg__PomHeight_ST.zw);") !=
          std::string::npos);

    sg::Graph triplanarGraph;
    triplanarGraph.kind = sg::GraphKind::Graph;
    triplanarGraph.target.id = "lit";
    sg::Property triTexture;
    triTexture.name = "Tri";
    triTexture.refName = "_Tri";
    triTexture.type = sg::ValueType::Texture2D;
    triTexture.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "tri.png");
    triplanarGraph.properties.push_back(triTexture);
    sg::NodeInstance triProperty = registry.instantiate("Property", 1);
    triProperty.settings["property"] =
        sg::Value::makeString(sg::ValueType::String, "_Tri");
    sg::NodeInstance triplanar = registry.instantiate("Triplanar", 2);
    triplanarGraph.nodes.push_back(std::move(triProperty));
    triplanarGraph.nodes.push_back(std::move(triplanar));
    triplanarGraph.edges.push_back(sg::Edge{1, 0, 2, 1});
    const sg::detail::EmittedGraph triplanarEmitted = emitGraph(registry, triplanarGraph);
    REQUIRE(triplanarEmitted.ok);
    CHECK(triplanarEmitted.source.find("SAMPLER2D(s_sg__Tri,") != std::string::npos);
}

TEST_CASE("SG-6 UV parallax and triplanar settings affect codegen") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::Graph parallaxGraph;
    parallaxGraph.kind = sg::GraphKind::Graph;
    parallaxGraph.target.id = "lit";
    sg::NodeInstance parallax = registry.instantiate("Parallax Mapping", 1);
    parallax.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_Height");
    parallax.settings["channel"] = sg::Value::makeString(sg::ValueType::String, "red");
    REQUIRE(parallaxGraph.addNode(std::move(parallax)));
    sg::detail::EmittedGraph parallaxEmitted = emitGraph(registry, parallaxGraph);
    REQUIRE(parallaxEmitted.ok);
    CHECK(parallaxEmitted.source.find("texture2D(s_sg__Height") != std::string::npos);
    CHECK(parallaxEmitted.source.find(".g") == std::string::npos);

    sg::Graph pomGraph;
    pomGraph.kind = sg::GraphKind::Graph;
    pomGraph.target.id = "lit";
    sg::NodeInstance pom = registry.instantiate("Parallax Occlusion Mapping", 1);
    pom.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_PomHeight");
    pom.settings["channel"] = sg::Value::makeString(sg::ValueType::String, "blue");
    REQUIRE(pomGraph.addNode(std::move(pom)));
    sg::detail::EmittedGraph pomEmitted = emitGraph(registry, pomGraph);
    REQUIRE(pomEmitted.ok);
    CHECK(pomEmitted.source.find(".b") != std::string::npos);
    CHECK(pomEmitted.source.find("int(max(min(5.0, 256.0), 1.0))") != std::string::npos);

    sg::Graph triplanarGraph;
    triplanarGraph.kind = sg::GraphKind::Graph;
    triplanarGraph.target.id = "lit";
    sg::NodeInstance triplanar = registry.instantiate("Triplanar", 1);
    triplanar.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_Tri");
    triplanar.settings["textureType"] = sg::Value::makeString(sg::ValueType::String, "normal");
    REQUIRE(triplanarGraph.addNode(std::move(triplanar)));
    sg::detail::EmittedGraph triplanarEmitted = emitGraph(registry, triplanarGraph);
    REQUIRE(triplanarEmitted.ok);
    CHECK(triplanarEmitted.source.find("hbgSGUnpackNormal") != std::string::npos);
    CHECK(triplanarEmitted.source.find("hbgSGTransformWorldToTangent") != std::string::npos);
    CHECK(triplanarEmitted.source.find("mat3(") != std::string::npos);

    sg::Graph objectNormalGraph;
    objectNormalGraph.kind = sg::GraphKind::Graph;
    objectNormalGraph.target.id = "lit";
    sg::NodeInstance objectNormal = registry.instantiate("Triplanar", 2);
    objectNormal.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_Normal");
    objectNormal.settings["textureType"] =
        sg::Value::makeString(sg::ValueType::String, "normal");
    objectNormal.settings["inputSpace"] = sg::Value::makeString(sg::ValueType::String, "object");
    objectNormal.settings["normalOutputSpace"] =
        sg::Value::makeString(sg::ValueType::String, "world");
    REQUIRE(objectNormalGraph.addNode(std::move(objectNormal)));
    sg::detail::EmittedGraph objectNormalEmitted = emitGraph(registry, objectNormalGraph);
    REQUIRE(objectNormalEmitted.ok);
    CHECK(objectNormalEmitted.source.find("gi.positionOS") != std::string::npos);
    CHECK(objectNormalEmitted.source.find("u_model[0]") != std::string::npos);
}
