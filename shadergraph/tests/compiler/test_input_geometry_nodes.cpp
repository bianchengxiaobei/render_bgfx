#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

struct NodeExpectation {
    const char* type;
    const char* category;
    sg::ValueType outputType;
    std::vector<std::string> settings;
    std::vector<std::string> synonyms;
};

const NodeExpectation kNodes[] = {
    {"Position", "Input/Geometry", sg::ValueType::Vector3, {"space"}, {"location"}},
    {"Normal Vector", "Input/Geometry", sg::ValueType::Vector3, {"space"}, {"surface direction"}},
    {"Tangent Vector", "Input/Geometry", sg::ValueType::Vector3, {"space"}, {}},
    {"Bitangent Vector", "Input/Geometry", sg::ValueType::Vector3, {"space"}, {"binormal"}},
    {"View Direction", "Input/Geometry", sg::ValueType::Vector3, {"space"}, {"eye direction"}},
    {"View Vector", "Input/Geometry", sg::ValueType::Vector3, {"space"}, {"eye vector"}},
    {"Screen Position", "Input/Geometry", sg::ValueType::Vector4, {"mode"}, {}},
    {"Vertex Color", "Input/Geometry", sg::ValueType::Vector4, {}, {}},
    {"Vertex ID", "Input/Geometry", sg::ValueType::Float, {}, {}},
    {"Instance ID", "Input/Geometry", sg::ValueType::Float, {}, {}},
    {"UV", "Input/Geometry", sg::ValueType::Vector2, {"channel"},
     {"texcoords", "coords", "coordinates"}},
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

sg::detail::EmittedGraph emitWithSetting(const sg::NodeRegistry& registry, const char* type,
                                         const char* key, sg::Value value) {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::NodeInstance node = registry.instantiate(type, 1);
    node.settings[key] = std::move(value);
    graph.nodes.push_back(std::move(node));
    return emitGraph(registry, graph);
}

sg::detail::EmittedGraph emitSpace(const sg::NodeRegistry& registry, const char* type,
                                   const char* space) {
    return emitWithSetting(registry, type, "space",
                           sg::Value::makeString(sg::ValueType::String, space));
}

} // namespace

TEST_CASE("SG-6 Input/Geometry nodes expose canonical geometry metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->title == expected.type);
        CHECK(definition->settings == expected.settings);
        CHECK(definition->synonyms == expected.synonyms);
        REQUIRE_FALSE(definition->slots.empty());
        const sg::SlotSpec& output = definition->slots.back();
        REQUIRE(static_cast<int>(output.direction) ==
                static_cast<int>(sg::SlotDirection::Output));
        CHECK(static_cast<int>(output.type) == static_cast<int>(expected.outputType));
    }
}

TEST_CASE("SG-6 Input/Geometry nodes emit canonical world-space defaults") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const std::vector<std::pair<const char*, const char*>> defaults = {
        {"Position", "n1_s0 = gi.positionWS;"},
        {"Normal Vector", "n1_s0 = normalize(gi.normalWS);"},
        {"Tangent Vector", "n1_s0 = normalize(gi.tangentWS);"},
        {"Bitangent Vector", "n1_s0 = normalize(gi.bitangentWS);"},
        {"View Direction", "n1_s0 = normalize(gi.viewDirWS);"},
        {"View Vector", "n1_s0 = gi.viewDirWS;"},
        {"Screen Position", "n1_s0 = vec4(gi.screenPos.xy, 0.0, 0.0);"},
        {"Vertex Color", "n1_s0 = gi.vertexColor;"},
        {"Vertex ID", "n1_s0 = gi.vertexID;"},
        {"Instance ID", "n1_s0 = gi.instanceID;"},
        {"UV", "n1_s0 = gi.uv0;"},
    };
    for (const auto& [type, fragment] : defaults) {
        CAPTURE(type);
        const sg::detail::EmittedGraph emitted = emitSingle(registry, type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(fragment) != std::string::npos);
    }
}

TEST_CASE("SG-6 geometry IDs are forwarded from vertex builtins") {
    const auto readText = [](const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        std::ostringstream stream;
        stream << file.rdbuf();
        return stream.str();
    };

    const std::string vertex =
        readText(std::string(SG_ROOT) + "/shaders/shadergraph/forward_graph.vs.sc");
    CHECK(vertex.find("gi.vertexID = float(gl_VertexID);") != std::string::npos);
    CHECK(vertex.find("gi.instanceID = float(gl_InstanceID);") != std::string::npos);
    CHECK(vertex.find("v_vertexID = gi.vertexID;") != std::string::npos);
    CHECK(vertex.find("v_instanceID = gi.instanceID;") != std::string::npos);

    const std::string fragment =
        readText(std::string(SG_ROOT) + "/shaders/shadergraph/forward_graph.fs.sc");
    CHECK(fragment.find("gi.vertexID = v_vertexID;") != std::string::npos);
    CHECK(fragment.find("gi.instanceID = v_instanceID;") != std::string::npos);

    const std::string varying =
        readText(std::string(SG_ROOT) + "/shaders/shadergraph/varying_graph.def.sc");
    CHECK(varying.find("flat float v_vertexID") != std::string::npos);
    CHECK(varying.find("flat float v_instanceID") != std::string::npos);
}

TEST_CASE("SG-6 Position, normal, tangent and bitangent honor coordinate space") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const std::vector<std::tuple<const char*, const char*, const char*>> spaces = {
        {"Position", "object", "n1_s0 = gi.positionOS;"},
        {"Position", "view", "n1_s0 = mul(u_global_view, vec4(gi.positionWS, 1.0)).xyz;"},
        {"Position", "tangent", "n1_s0 = vec3(0.0, 0.0, 0.0);"},
        {"Position", "absolute world", "n1_s0 = gi.positionWS;"},
        {"Normal Vector", "object", "n1_s0 = normalize(gi.normalOS);"},
        {"Normal Vector", "view",
         "n1_s0 = normalize(mul(u_global_view, vec4(gi.normalWS, 0.0)).xyz);"},
        {"Normal Vector", "tangent",
         "n1_s0 = normalize(vec3(dot(gi.normalWS, gi.tangentWS),"},
        {"Tangent Vector", "object", "n1_s0 = normalize(gi.tangentOS);"},
        {"Tangent Vector", "view",
         "n1_s0 = normalize(mul(u_global_view, vec4(gi.tangentWS, 0.0)).xyz);"},
        {"Tangent Vector", "tangent", "n1_s0 = vec3(1.0, 0.0, 0.0);"},
        {"Bitangent Vector", "object", "n1_s0 = normalize(gi.bitangentOS);"},
        {"Bitangent Vector", "view",
         "n1_s0 = normalize(mul(u_global_view, vec4(gi.bitangentWS, 0.0)).xyz);"},
        {"Bitangent Vector", "tangent", "n1_s0 = vec3(0.0, 1.0, 0.0);"},
    };
    for (const auto& [type, space, fragment] : spaces) {
        CAPTURE(type);
        CAPTURE(space);
        const sg::detail::EmittedGraph emitted = emitSpace(registry, type, space);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(fragment) != std::string::npos);
    }
}

TEST_CASE("SG-6 View Direction and View Vector honor coordinate space") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const std::vector<std::tuple<const char*, const char*, const char*>> spaces = {
        {"View Direction", "object",
         "n1_s0 = normalize(mul(u_sg_modelInverse, vec4(gi.viewDirWS, 0.0)).xyz);"},
        {"View Direction", "view",
         "n1_s0 = normalize(mul(u_global_view, vec4(gi.viewDirWS, 0.0)).xyz);"},
        {"View Direction", "tangent",
         "n1_s0 = normalize(vec3(dot(gi.viewDirWS, gi.tangentWS),"},
        {"View Vector", "object",
         "n1_s0 = mul(u_sg_modelInverse, vec4(gi.viewDirWS, 0.0)).xyz;"},
        {"View Vector", "view",
         "n1_s0 = mul(u_global_view, vec4(gi.viewDirWS, 0.0)).xyz;"},
        {"View Vector", "tangent",
         "n1_s0 = vec3(dot(gi.viewDirWS, gi.tangentWS),"},
    };
    for (const auto& [type, space, fragment] : spaces) {
        CAPTURE(type);
        CAPTURE(space);
        const sg::detail::EmittedGraph emitted = emitSpace(registry, type, space);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(fragment) != std::string::npos);
    }
}

TEST_CASE("SG-6 Screen Position and UV settings emit configured modes") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const std::vector<std::pair<const char*, const char*>> modes = {
        {"raw", "n1_s0 = gi.screenPos;"},
        {"center", "n1_s0 = vec4(gi.screenPos.xy * 2.0 - 1.0, 0.0, 0.0);"},
        {"tiled", "n1_s0 = frac(vec4("},
        {"pixel", "n1_s0 = vec4(gi.screenPos.xy * u_global_screenSize.xy, 0.0, 0.0);"},
    };
    for (const auto& [mode, fragment] : modes) {
        CAPTURE(mode);
        const sg::detail::EmittedGraph emitted = emitWithSetting(
            registry, "Screen Position", "mode",
            sg::Value::makeString(sg::ValueType::String, mode));
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(fragment) != std::string::npos);
    }

    const sg::detail::EmittedGraph uv1 = emitWithSetting(
        registry, "UV", "channel", sg::Value::makeInteger(1));
    REQUIRE(uv1.ok);
    CHECK(uv1.source.find("n1_s0 = gi.uv1;") != std::string::npos);

    const sg::detail::EmittedGraph uv7 = emitWithSetting(
        registry, "UV", "channel", sg::Value::makeInteger(7));
    REQUIRE(uv7.ok);
    CHECK(uv7.source.find("n1_s0 = gi.uv0;") != std::string::npos);
}
