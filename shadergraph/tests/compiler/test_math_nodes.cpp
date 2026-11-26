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

const NodeExpectation kMathNodes[] = {
    {"Arccosine", "Math/Trigonometry", 1, 1},
    {"Arcsine", "Math/Trigonometry", 1, 1},
    {"Arctangent", "Math/Trigonometry", 1, 1},
    {"Arctangent2", "Math/Trigonometry", 2, 1},
    {"Cosine", "Math/Trigonometry", 1, 1},
    {"Degrees To Radians", "Math/Trigonometry", 1, 1},
    {"Hyperbolic Cosine", "Math/Trigonometry", 1, 1},
    {"Hyperbolic Sine", "Math/Trigonometry", 1, 1},
    {"Hyperbolic Tangent", "Math/Trigonometry", 1, 1},
    {"Radians To Degrees", "Math/Trigonometry", 1, 1},
    {"Sine", "Math/Trigonometry", 1, 1},
    {"Tangent", "Math/Trigonometry", 1, 1},
    {"Sawtooth Wave", "Math/Wave", 1, 1},
    {"Triangle Wave", "Math/Wave", 1, 1},
    {"Square Wave", "Math/Wave", 1, 1},
    {"Noise Sine Wave", "Math/Wave", 2, 1},
    {"DDX", "Math/Derivative", 1, 1},
    {"DDXY", "Math/Derivative", 1, 1},
    {"DDY", "Math/Derivative", 1, 1},
    {"Dot Product", "Math/Vector", 2, 1},
    {"Cross Product", "Math/Vector", 2, 1},
    {"Distance", "Math/Vector", 2, 1},
    {"Fresnel Effect", "Math/Vector", 3, 1},
    {"Projection", "Math/Vector", 2, 1},
    {"Reflection", "Math/Vector", 2, 1},
    {"Rejection", "Math/Vector", 2, 1},
    {"Rotate About Axis", "Math/Vector", 3, 1},
    {"Sphere Mask", "Math/Vector", 4, 1},
    {"Refract", "Math/Vector", 4, 2},
};

struct DynamicExpectation {
    const char* type;
    int outputSlot;
    sg::ConcreteValueType outputType;
};

const NodeExpectation kMatrixNodes[] = {
    {"Matrix Construction", "Math/Matrix", 4, 3},
    {"Matrix Determinant", "Math/Matrix", 1, 1},
    {"Matrix Split", "Math/Matrix", 1, 4},
    {"Matrix Transpose", "Math/Matrix", 1, 1},
};

const DynamicExpectation kDynamicNodes[] = {
    {"Sine", 1, sg::ConcreteValueType::Vector3},
    {"Arccosine", 1, sg::ConcreteValueType::Vector3},
    {"Degrees To Radians", 1, sg::ConcreteValueType::Vector3},
    {"Hyperbolic Cosine", 1, sg::ConcreteValueType::Vector3},
    {"Arctangent2", 2, sg::ConcreteValueType::Vector3},
    {"Noise Sine Wave", 2, sg::ConcreteValueType::Vector3},
    {"Sawtooth Wave", 1, sg::ConcreteValueType::Vector3},
    {"DDX", 1, sg::ConcreteValueType::Vector3},
    {"DDXY", 1, sg::ConcreteValueType::Vector3},
    {"DDY", 1, sg::ConcreteValueType::Vector3},
    {"Dot Product", 2, sg::ConcreteValueType::Float},
    {"Distance", 2, sg::ConcreteValueType::Float},
    {"Projection", 2, sg::ConcreteValueType::Vector3},
    {"Reflection", 2, sg::ConcreteValueType::Vector3},
    {"Rejection", 2, sg::ConcreteValueType::Vector3},
    {"Sphere Mask", 4, sg::ConcreteValueType::Float},
};

struct EmitExpectation {
    const char* type;
    const char* fragment;
};

const EmitExpectation kEmitNodes[] = {
    {"Arccosine", "acos("},
    {"Arcsine", "asin("},
    {"Arctangent", "atan("},
    {"Arctangent2", "atan2("},
    {"Cosine", "cos("},
    {"Degrees To Radians", "radians("},
    {"Hyperbolic Cosine", "cosh("},
    {"Hyperbolic Sine", "sinh("},
    {"Hyperbolic Tangent", "tanh("},
    {"Radians To Degrees", "degrees("},
    {"Sine", "sin("},
    {"Tangent", "tan("},
    {"Sawtooth Wave", "floor(0.5 +"},
    {"Triangle Wave", "abs(2.0 *"},
    {"Square Wave", "round(frac("},
    {"Noise Sine Wave", "43758.5453"},
    {"DDX", "ddx("},
    {"DDXY", "abs(ddx("},
    {"DDY", "ddy("},
    {"Dot Product", "n1_s2 = ("},
    {"Cross Product", "cross("},
    {"Distance", "distance("},
    {"Fresnel Effect", "saturate(dot(normalize("},
    {"Projection", "/ max("},
    {"Reflection", " - 2.0 * "},
    {"Rejection", "/ max("},
    {"Rotate About Axis", "hbg_rot_s_1"},
    {"Sphere Mask", "distance("},
    {"Refract", "hbg_refract_eta_1"},
    {"Transform", "mul(u_model[0], vec4("},
    {"Matrix Construction", "mat4("},
    {"Matrix Determinant", "determinant("},
    {"Matrix Split", "vec2("},
    {"Matrix Transpose", "transpose("},
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

TEST_CASE("SG-6 math nodes are registered with stable slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kMathNodes) {
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

    const sg::NodeDef* dot = registry.find("Dot Product");
    REQUIRE(dot != nullptr);
    REQUIRE(dot->slots.size() == 3);
    CHECK(static_cast<int>(dot->slots[2].type) == static_cast<int>(sg::ValueType::Float));
    CHECK(registry.find("DotProduct") != nullptr);
    CHECK(registry.find("Arctangent2") != nullptr);

    const sg::NodeDef* noise = registry.find("Noise Sine Wave");
    REQUIRE(noise != nullptr);
    CHECK(noise->typeVersion == 1);
    CHECK(static_cast<int>(noise->slots[1].type) == static_cast<int>(sg::ValueType::Vector2));

    const sg::NodeDef* rotate = registry.find("Rotate About Axis");
    REQUIRE(rotate != nullptr);
    CHECK(rotate->settings.size() == 1);
    CHECK(rotate->settings[0] == "unit");

    const sg::NodeDef* refract = registry.find("Refract");
    REQUIRE(refract != nullptr);
    REQUIRE(refract->slots.size() == 6);
    CHECK(static_cast<int>(refract->slots[4].direction) == static_cast<int>(sg::SlotDirection::Output));
    CHECK(static_cast<int>(refract->slots[4].type) == static_cast<int>(sg::ValueType::Vector3));
    CHECK(static_cast<int>(refract->slots[5].type) == static_cast<int>(sg::ValueType::Float));
    CHECK(refract->settings.size() == 1);
    CHECK(refract->settings[0] == "mode");
}

TEST_CASE("SG-6 math nodes propagate dynamic vector types") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const DynamicExpectation& expected : kDynamicNodes) {
        CAPTURE(expected.type);
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";
        REQUIRE(graph.addNode(registry.instantiate("Vector3", 1)));
        REQUIRE(graph.addNode(registry.instantiate(expected.type, 2)));
        REQUIRE(graph.addEdge({1, 0, 2, 0}));
        REQUIRE(sg::concretize(graph, registry).ok);
        const sg::SlotInstance* output = graph.findSlot(2, expected.outputSlot);
        REQUIRE(output != nullptr);
        CHECK(static_cast<int>(output->concreteType) == static_cast<int>(expected.outputType));
    }
}

TEST_CASE("SG-6 math nodes emit expected HLSL") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const EmitExpectation& expected : kEmitNodes) {
        CAPTURE(expected.type);
        sg::detail::EmittedGraph emitted = emitSingle(registry, expected.type);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find("hbgEvaluateGraphSurface") != std::string::npos);
        CHECK(emitted.source.find(expected.fragment) != std::string::npos);
    }
}

TEST_CASE("SG-6 vector math uses vector intrinsics when connected") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const char* type : {"Dot Product", "Projection", "Reflection", "Rejection"}) {
        CAPTURE(type);
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";
        REQUIRE(graph.addNode(registry.instantiate("Vector3", 1)));
        REQUIRE(graph.addNode(registry.instantiate(type, 2)));
        REQUIRE(graph.addEdge({1, 0, 2, 0}));
        REQUIRE(sg::concretize(graph, registry).ok);

        const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
        REQUIRE(target != nullptr);
        sg::detail::ActiveGraph active =
            sg::detail::collectActiveNodes(graph, registry, *target);
        sg::detail::PropertySet properties =
            sg::detail::collectProperties(graph, active, *target);
        REQUIRE(properties.ok);
        sg::detail::KeywordSet keywords =
            sg::detail::collectKeywords(graph, *target, sg::VariantPolicy::Used, 64);
        REQUIRE(keywords.ok);
        REQUIRE(keywords.variants.size() == 1);
        sg::detail::EmittedGraph emitted =
            sg::detail::emitSurface(graph, registry, active, *target, properties, keywords,
                                    keywords.variants[0]);
        REQUIRE(emitted.ok);
        CHECK((emitted.source.find("dot(") != std::string::npos ||
               emitted.source.find("reflect(") != std::string::npos));
    }
}

TEST_CASE("SG-6 matrix nodes are registered and concretize from matrix inputs") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kMatrixNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->slots.size() == static_cast<std::size_t>(expected.inputs + expected.outputs));
    }

    const sg::NodeDef* transform = registry.find("Transform");
    REQUIRE(transform != nullptr);
    REQUIRE(transform->slots.size() == 2);
    CHECK(static_cast<int>(transform->slots[0].type) == static_cast<int>(sg::ValueType::Vector3));
    CHECK(transform->settings.size() == 4);

    const sg::NodeDef* construction = registry.find("Matrix Construction");
    REQUIRE(construction != nullptr);
    CHECK(static_cast<int>(construction->slots[4].type) == static_cast<int>(sg::ValueType::Matrix4));
    CHECK(static_cast<int>(construction->slots[5].type) == static_cast<int>(sg::ValueType::Matrix3));
    CHECK(static_cast<int>(construction->slots[6].type) == static_cast<int>(sg::ValueType::Matrix2));
    CHECK(construction->settings.size() == 1);
    CHECK(construction->settings[0] == "axis");

    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    REQUIRE(graph.addNode(registry.instantiate("Matrix Construction", 1)));
    REQUIRE(graph.addNode(registry.instantiate("Matrix Split", 2)));
    REQUIRE(graph.addEdge({1, 5, 2, 0}));
    REQUIRE(sg::concretize(graph, registry).ok);
    CHECK(static_cast<int>(graph.findSlot(2, 1)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Vector3));
    CHECK(static_cast<int>(graph.findSlot(2, 4)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Vector3));

    sg::Graph transpose;
    transpose.kind = sg::GraphKind::Graph;
    transpose.target.id = "lit";
    REQUIRE(transpose.addNode(registry.instantiate("Matrix Construction", 1)));
    REQUIRE(transpose.addNode(registry.instantiate("Matrix Transpose", 2)));
    REQUIRE(transpose.addEdge({1, 5, 2, 0}));
    REQUIRE(sg::concretize(transpose, registry).ok);
    CHECK(static_cast<int>(transpose.findSlot(2, 1)->concreteType) ==
          static_cast<int>(sg::ConcreteValueType::Matrix3));
}

TEST_CASE("SG-6 Transform inverse conversions use model inverse") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const auto emitTransform = [&](const char* from, const char* to, const char* type) {
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";
        sg::NodeInstance node = registry.instantiate("Transform", 1);
        node.settings["from"] = sg::Value::makeString(sg::ValueType::String, from);
        node.settings["to"] = sg::Value::makeString(sg::ValueType::String, to);
        node.settings["type"] = sg::Value::makeString(sg::ValueType::String, type);
        REQUIRE(graph.addNode(std::move(node)));
        return emitGraph(registry, graph);
    };

    const sg::detail::EmittedGraph worldToObjectPosition =
        emitTransform("world", "object", "position");
    REQUIRE(worldToObjectPosition.ok);
    CHECK(worldToObjectPosition.source.find("n1_s1 = mul(u_sg_modelInverse, vec4(") !=
          std::string::npos);

    const sg::detail::EmittedGraph worldToObjectDirection =
        emitTransform("world", "object", "direction");
    REQUIRE(worldToObjectDirection.ok);
    CHECK(worldToObjectDirection.source.find(
              "n1_s1 = normalize(mul(u_sg_modelInverse, vec4(") != std::string::npos);

    const sg::detail::EmittedGraph worldToObjectNormal =
        emitTransform("world", "object", "normal");
    REQUIRE(worldToObjectNormal.ok);
    CHECK(worldToObjectNormal.source.find(
              "n1_s1 = normalize(mul(vec4(") != std::string::npos);
    CHECK(worldToObjectNormal.source.find("), u_model[0]).xyz);") != std::string::npos);

    const sg::detail::EmittedGraph viewToObject =
        emitTransform("view", "object", "position");
    REQUIRE(viewToObject.ok);
    CHECK(viewToObject.source.find("u_global_invView") != std::string::npos);
    CHECK(viewToObject.source.find("u_sg_modelInverse") != std::string::npos);

    const sg::detail::EmittedGraph tangentToObject =
        emitTransform("tangent", "object", "direction");
    REQUIRE(tangentToObject.ok);
    CHECK(tangentToObject.source.find("gi.tangentWS") != std::string::npos);
    CHECK(tangentToObject.source.find("u_sg_modelInverse") != std::string::npos);

    const sg::detail::EmittedGraph objectToWorldNormal =
        emitTransform("object", "world", "normal");
    REQUIRE(objectToWorldNormal.ok);
    CHECK(objectToWorldNormal.source.find("mul(vec4(") != std::string::npos);
    CHECK(objectToWorldNormal.source.find(", 0.0), u_sg_modelInverse).xyz") !=
          std::string::npos);
}

TEST_CASE("SG-6 Transform screen conversions use clip-space helpers") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const auto emitTransform = [&](const char* from, const char* to, const char* type) {
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";
        sg::NodeInstance node = registry.instantiate("Transform", 1);
        node.settings["from"] = sg::Value::makeString(sg::ValueType::String, from);
        node.settings["to"] = sg::Value::makeString(sg::ValueType::String, to);
        node.settings["type"] = sg::Value::makeString(sg::ValueType::String, type);
        REQUIRE(graph.addNode(std::move(node)));
        return emitGraph(registry, graph);
    };

    const sg::detail::EmittedGraph worldToScreen =
        emitTransform("world", "screen", "position");
    REQUIRE(worldToScreen.ok);
    CHECK(worldToScreen.source.find("vec3 hbgSGWorldToScreen(vec3 world)") !=
          std::string::npos);
    CHECK(worldToScreen.source.find("u_global_viewProj") != std::string::npos);
    CHECK(worldToScreen.source.find("vec3(ndc.xy * 0.5 + 0.5, ndc.z)") !=
          std::string::npos);
    CHECK(worldToScreen.source.find("n1_s1 = hbgSGWorldToScreen(") !=
          std::string::npos);

    const sg::detail::EmittedGraph screenToWorld =
        emitTransform("screen", "world", "position");
    REQUIRE(screenToWorld.ok);
    CHECK(screenToWorld.source.find("vec3 hbgSGWorldFromScreen(vec3 screen)") !=
          std::string::npos);
    CHECK(screenToWorld.source.find("u_global_invViewProj") != std::string::npos);
    CHECK(screenToWorld.source.find("n1_s1 = hbgSGWorldFromScreen(") !=
          std::string::npos);

    const sg::detail::EmittedGraph screenToWorldDirection =
        emitTransform("screen", "world", "direction");
    REQUIRE(screenToWorldDirection.ok);
    CHECK(screenToWorldDirection.source.find(
              "vec3 hbgSGWorldDirectionFromScreen(vec3 screen)") != std::string::npos);
    CHECK(screenToWorldDirection.source.find(
              "vec4 clip = vec4(screen.xy * 2.0 - 1.0, screen.z, 0.0)") !=
          std::string::npos);
    CHECK(screenToWorldDirection.source.find(
              "n1_s1 = hbgSGWorldDirectionFromScreen(") != std::string::npos);

    const sg::detail::EmittedGraph worldToScreenDirection =
        emitTransform("world", "screen", "normal");
    REQUIRE(worldToScreenDirection.ok);
    CHECK(worldToScreenDirection.source.find(
              "hbgSGWorldDirectionToScreen(normalize(") != std::string::npos);

    const sg::detail::EmittedGraph screenToObject =
        emitTransform("screen", "object", "position");
    REQUIRE(screenToObject.ok);
    CHECK(screenToObject.source.find("hbgSGWorldFromScreen(") !=
          std::string::npos);
    CHECK(screenToObject.source.find("u_sg_modelInverse") != std::string::npos);

    const sg::detail::EmittedGraph objectToScreen =
        emitTransform("object", "screen", "position");
    REQUIRE(objectToScreen.ok);
    CHECK(objectToScreen.source.find("hbgSGWorldToScreen(mul(u_model[0], vec4(") !=
          std::string::npos);

    const sg::detail::EmittedGraph screenToView =
        emitTransform("screen", "view", "position");
    REQUIRE(screenToView.ok);
    CHECK(screenToView.source.find("hbgSGWorldFromScreen(") != std::string::npos);
    CHECK(screenToView.source.find("u_global_view") != std::string::npos);

    const sg::detail::EmittedGraph screenToTangent =
        emitTransform("screen", "tangent", "direction");
    REQUIRE(screenToTangent.ok);
    CHECK(screenToTangent.source.find("hbgSGWorldDirectionFromScreen(") !=
          std::string::npos);
    CHECK(screenToTangent.source.find("gi.tangentWS") != std::string::npos);

    const sg::detail::EmittedGraph viewToScreen =
        emitTransform("view", "screen", "position");
    REQUIRE(viewToScreen.ok);
    CHECK(viewToScreen.source.find("hbgSGWorldToScreen(mul(u_global_invView") !=
          std::string::npos);

    const sg::detail::EmittedGraph tangentToScreen =
        emitTransform("tangent", "screen", "position");
    REQUIRE(tangentToScreen.ok);
    CHECK(tangentToScreen.source.find("hbgSGWorldToScreen(gi.positionWS + (") !=
          std::string::npos);

    const sg::detail::EmittedGraph screenToAbsolute =
        emitTransform("screen", "absoluteWorld", "position");
    REQUIRE(screenToAbsolute.ok);
    CHECK(screenToAbsolute.source.find("hbgSGWorldFromScreen(") != std::string::npos);
}

TEST_CASE("SG-6 math node settings and Fresnel bindings affect codegen") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    sg::Graph fresnel;
    fresnel.kind = sg::GraphKind::Graph;
    fresnel.target.id = "lit";
    REQUIRE(fresnel.addNode(registry.instantiate("Vector3", 1)));
    REQUIRE(fresnel.addNode(registry.instantiate("Fresnel Effect", 2)));
    REQUIRE(fresnel.addEdge({1, 0, 2, 0}));
    sg::detail::EmittedGraph fresnelGraph = emitGraph(registry, fresnel);
    REQUIRE(fresnelGraph.ok);
    CHECK(fresnelGraph.source.find("dot(n1_s0, normalize(gi.viewDirWS))") != std::string::npos);

    sg::Graph rotate;
    rotate.kind = sg::GraphKind::Graph;
    rotate.target.id = "lit";
    sg::NodeInstance rotateNode = registry.instantiate("Rotate About Axis", 1);
    rotateNode.settings["unit"] = sg::Value::makeString(sg::ValueType::String, "degrees");
    REQUIRE(rotate.addNode(std::move(rotateNode)));
    sg::detail::EmittedGraph rotateGraph = emitGraph(registry, rotate);
    REQUIRE(rotateGraph.ok);
    CHECK(rotateGraph.source.find("radians(") != std::string::npos);

    sg::Graph refract;
    refract.kind = sg::GraphKind::Graph;
    refract.target.id = "lit";
    sg::NodeInstance refractNode = registry.instantiate("Refract", 1);
    refractNode.settings["mode"] = sg::Value::makeString(sg::ValueType::String, "critical");
    REQUIRE(refract.addNode(std::move(refractNode)));
    sg::detail::EmittedGraph refractGraph = emitGraph(registry, refract);
    REQUIRE(refractGraph.ok);
    CHECK(refractGraph.source.find(": reflect(") != std::string::npos);

    sg::Graph transform;
    transform.kind = sg::GraphKind::Graph;
    transform.target.id = "lit";
    sg::NodeInstance transformNode = registry.instantiate("Transform", 1);
    transformNode.settings["from"] = sg::Value::makeString(sg::ValueType::String, "world");
    transformNode.settings["to"] = sg::Value::makeString(sg::ValueType::String, "view");
    transformNode.settings["type"] = sg::Value::makeString(sg::ValueType::String, "direction");
    REQUIRE(transform.addNode(std::move(transformNode)));
    sg::detail::EmittedGraph transformGraph = emitGraph(registry, transform);
    REQUIRE(transformGraph.ok);
    CHECK(transformGraph.source.find("u_global_view") != std::string::npos);
    CHECK(transformGraph.source.find("normalize(") != std::string::npos);
}
