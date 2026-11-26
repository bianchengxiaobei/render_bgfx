#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {

struct NodeExpectation {
    const char* type;
    const char* category;
    std::vector<std::string> synonyms;
    int slots;
};

const NodeExpectation kNodes[] = {
    {"Camera", "Input/Scene",
     {"position", "direction", "orthographic", "near plane", "far plane", "width", "height"}, 8},
    {"Eye Index", "Input/Scene", {"stereo", "3d"}, 1},
    {"Object", "Input/Scene", {"position", "scale", "bounds", "size"}, 5},
    {"Screen", "Input/Scene", {}, 2},
    {"Scene Color", "Input/Scene", {"screen buffer"}, 2},
    {"Scene Depth", "Input/Scene", {"zbuffer", "zdepth"}, 2},
    {"Scene Depth Difference", "Input/Scene", {"zbuffer", "zdepth", "difference"}, 1},
    {"Fog", "Input/Scene", {}, 3},
    {"Ambient", "Input/Lighting", {"scene color"}, 3},
    {"Baked GI", "Input/Lighting", {"global illumination"}, 5},
    {"Main Light Direction", "Input/Lighting", {"sun"}, 1},
    {"Reflection Probe", "Input/Lighting", {"light probe", "cube map", "environment"}, 4},
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

sg::detail::EmittedGraph emitNode(const sg::NodeRegistry& registry, const char* type,
                                  const char* targetId = "lit",
                                  std::map<std::string, sg::Value> settings = {}) {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = targetId;
    sg::NodeInstance node = registry.instantiate(type, 1);
    for (auto& [key, value] : settings) {
        node.settings[key] = std::move(value);
    }
    graph.nodes.push_back(std::move(node));
    return emitGraph(registry, graph, targetId);
}

} // namespace

TEST_CASE("SG-6 Input/Scene and Lighting nodes expose canonical metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->title == expected.type);
        CHECK(definition->synonyms == expected.synonyms);
        CHECK(static_cast<int>(definition->slots.size()) == expected.slots);
    }

    const sg::NodeDef* camera = registry.find("Camera");
    REQUIRE(camera != nullptr);
    CHECK(camera->slots[0].name == "Position");
    CHECK(camera->slots[1].name == "Direction");
    CHECK(camera->slots[5].name == "Z Buffer Sign");
    CHECK(camera->slots[7].name == "Height");

    const sg::NodeDef* object = registry.find("Object");
    REQUIRE(object != nullptr);
    CHECK(object->slots[0].name == "Position");
    CHECK(object->slots[1].name == "Scale");
    CHECK(object->slots[4].name == "Bounds Size");

    const sg::NodeDef* ambient = registry.find("Ambient");
    REQUIRE(ambient != nullptr);
    CHECK(ambient->slots[0].name == "Color/Sky");
    CHECK(ambient->slots[1].name == "Equator");
    CHECK(ambient->slots[2].name == "Ground");

    const sg::NodeDef* baked = registry.find("Baked GI");
    REQUIRE(baked != nullptr);
    CHECK(baked->settings == std::vector<std::string>{"applyScaling"});
    CHECK(baked->slots[0].id == 0);
    CHECK(baked->slots[0].name == "NormalWS");
    CHECK(baked->slots[1].id == 2);
    CHECK(baked->slots[1].name == "PositionWS");
    CHECK(baked->slots[2].id == 3);
    CHECK(baked->slots[2].name == "StaticUV");
    CHECK(baked->slots[3].id == 4);
    CHECK(baked->slots[3].name == "DynamicUV");
    CHECK(baked->slots[4].id == 1);
    CHECK(baked->slots[4].name == "Out");

    const sg::NodeDef* probe = registry.find("Reflection Probe");
    REQUIRE(probe != nullptr);
    CHECK(probe->slots[0].name == "ViewDir");
    CHECK(probe->slots[1].name == "Normal");
    CHECK(probe->slots[2].name == "LOD");
    CHECK(probe->slots[3].id == 3);
    CHECK(probe->slots[3].name == "Out");

    CHECK_FALSE(registry.find("Eye Index")->hasPreview);
    CHECK_FALSE(registry.find("Baked GI")->hasPreview);
    CHECK_FALSE(registry.find("Main Light Direction")->hasPreview);
}

TEST_CASE("SG-6 Scene nodes emit camera, object and screen inputs") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph camera = emitNode(registry, "Camera");
    REQUIRE(camera.ok);
    CHECK(camera.source.find("n1_s0 = u_global_cameraPos.xyz;") != std::string::npos);
    CHECK(camera.source.find("normalize(mul(u_global_invView, vec4(0.0, 0.0, -1.0, 0.0)).xyz)") !=
          std::string::npos);
    CHECK(camera.source.find("u_global_frustumPlanes[4].w") != std::string::npos);
    CHECK(camera.source.find("u_global_frustumPlanes[5].w") != std::string::npos);
    CHECK(camera.source.find("mul(u_global_proj, vec4(0.0, 0.0, 0.0, 1.0)).w") !=
          std::string::npos);
    CHECK(camera.source.find("n1_s7 =") != std::string::npos);

    const sg::detail::EmittedGraph object = emitNode(registry, "Object");
    REQUIRE(object.ok);
    CHECK(object.source.find("n1_s0 = hbg_sg_object_pos_1;") != std::string::npos);
    CHECK(object.source.find("mul(u_model[0], vec4(1.0, 0.0, 0.0, 0.0)).xyz") !=
          std::string::npos);
    CHECK(object.source.find("hbg_sg_object_scale_1 * 0.5") != std::string::npos);
    CHECK(object.source.find("n1_s2 = hbg_sg_object_pos_1 - hbg_sg_object_half_1;") !=
          std::string::npos);
    CHECK(object.source.find("n1_s3 = hbg_sg_object_pos_1 + hbg_sg_object_half_1;") !=
          std::string::npos);

    const sg::detail::EmittedGraph screen = emitNode(registry, "Screen");
    REQUIRE(screen.ok);
    CHECK(screen.source.find("n1_s0 = u_global_screenSize.x;") != std::string::npos);
    CHECK(screen.source.find("n1_s1 = u_global_screenSize.y;") != std::string::npos);

    const sg::detail::EmittedGraph eye = emitNode(registry, "Eye Index");
    REQUIRE(eye.ok);
    CHECK(eye.source.find("n1_s0 = 0.0;") != std::string::npos);
}

TEST_CASE("SG-6 Lighting nodes emit ambient, baked GI, main light and reflection probe") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph ambient = emitNode(registry, "Ambient");
    REQUIRE(ambient.ok);
    CHECK(ambient.source.find("vec3 hbgSGSampleSH9(vec3 normal)") != std::string::npos);
    CHECK(ambient.source.find("hbgSGSampleAmbient(vec3(0.0, 1.0, 0.0))") !=
          std::string::npos);
    CHECK(ambient.source.find("hbgSGSampleAmbient(vec3(1.0, 0.0, 0.0))") !=
          std::string::npos);
    CHECK(ambient.source.find("hbgSGSampleAmbient(vec3(0.0, -1.0, 0.0))") !=
          std::string::npos);

    const sg::detail::EmittedGraph baked = emitNode(registry, "Baked GI");
    REQUIRE(baked.ok);
    CHECK(baked.source.find("n1_s1 = hbgSGSampleAmbient(normalize(gi.normalWS));") !=
          std::string::npos);

    const sg::detail::EmittedGraph mainLight = emitNode(registry, "Main Light Direction");
    REQUIRE(mainLight.ok);
    CHECK(mainLight.source.find(
              "n1_s0 = u_pass_lightCounts.y > 0.5 ? u_pass_lightDirection.xyz : "
              "vec3(0.0, 0.0, 0.0);") != std::string::npos);

    const sg::detail::EmittedGraph preview =
        emitNode(registry, "Main Light Direction", "preview");
    REQUIRE(preview.ok);
    CHECK(preview.source.find("n1_s0 = vec3(-0.5, -0.5, 0.0);") != std::string::npos);

    const sg::detail::EmittedGraph probe = emitNode(registry, "Reflection Probe");
    REQUIRE(probe.ok);
    CHECK(probe.source.find("#include \"lighting/environment.sc\"") != std::string::npos);
    CHECK(probe.source.find("hbgSampleReflection(gi.positionWS, reflect(-normalize(gi.viewDirWS), "
                            "normalize(gi.normalWS))") != std::string::npos);
    CHECK(probe.source.find("n1_s3 = max(hbgSampleReflection") != std::string::npos);
}

TEST_CASE("SG-6 Scene Color, Scene Depth, Scene Depth Difference and Fog emit scene access") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::NodeDef* sceneColor = registry.find("Scene Color");
    REQUIRE(sceneColor != nullptr);
    CHECK(sceneColor->settings.empty());
    CHECK(static_cast<int>(sceneColor->slots.size()) == 2);
    CHECK(sceneColor->slots[0].id == 0);
    CHECK(sceneColor->slots[0].name == "UV");
    CHECK(sceneColor->slots[1].id == 1);
    CHECK(sceneColor->slots[1].name == "Out");
    CHECK(static_cast<int>(sceneColor->slots[1].type) == static_cast<int>(sg::ValueType::Vector3));
    CHECK_FALSE(sceneColor->hasPreview);

    const sg::NodeDef* sceneDepth = registry.find("Scene Depth");
    REQUIRE(sceneDepth != nullptr);
    CHECK(sceneDepth->settings == std::vector<std::string>{"mode"});
    CHECK(sceneDepth->slots[0].name == "UV");
    CHECK(sceneDepth->slots[1].id == 1);
    CHECK(sceneDepth->slots[1].name == "Out");
    CHECK(static_cast<int>(sceneDepth->slots[1].type) == static_cast<int>(sg::ValueType::Float));

    const sg::NodeDef* difference = registry.find("Scene Depth Difference");
    REQUIRE(difference != nullptr);
    CHECK(difference->settings == std::vector<std::string>{"mode"});
    CHECK(static_cast<int>(difference->slots.size()) == 1);
    CHECK(difference->slots[0].id == 0);
    CHECK(difference->slots[0].name == "Out");

    const sg::NodeDef* fog = registry.find("Fog");
    REQUIRE(fog != nullptr);
    CHECK(fog->slots[0].id == 0);
    CHECK(fog->slots[0].name == "Color");
    CHECK(fog->slots[1].id == 1);
    CHECK(fog->slots[1].name == "Density");
    CHECK(fog->slots[2].id == 2);
    CHECK(fog->slots[2].name == "Position");

    const sg::detail::EmittedGraph color = emitNode(registry, "Scene Color");
    REQUIRE(color.ok);
    CHECK(color.requiredFeatureMask == sg::detail::kGraphFeatureSceneColor);
    CHECK(color.source.find("SAMPLER2D(s_sceneColor, 0);") != std::string::npos);
    CHECK(color.source.find("n1_s1 = hbgSGSampleSceneColor(gi.screenPos.xy);") !=
          std::string::npos);

    const sg::detail::EmittedGraph raw = emitNode(
        registry, "Scene Depth", "lit",
        {{"mode", sg::Value::makeString(sg::ValueType::String, "raw")}});
    REQUIRE(raw.ok);
    CHECK(raw.requiredFeatureMask == sg::detail::kGraphFeatureSceneDepth);
    CHECK(raw.source.find("SAMPLER2D(s_sceneDepth, 0);") != std::string::npos);
    CHECK(raw.source.find("n1_s1 = hbgSGSampleSceneDepth(gi.screenPos.xy);") !=
          std::string::npos);

    const sg::detail::EmittedGraph linear = emitNode(
        registry, "Scene Depth", "lit",
        {{"mode", sg::Value::makeString(sg::ValueType::String, "linear01")}});
    REQUIRE(linear.ok);
    CHECK(linear.source.find("float hbg_sg_scene_depth_1 = hbgSGSampleSceneDepth("
                             "gi.screenPos.xy);") != std::string::npos);
    CHECK(linear.source.find("n1_s1 = hbgSGLinear01Depth(hbg_sg_scene_depth_1, "
                             "gi.screenPos.xy);") != std::string::npos);

    const sg::detail::EmittedGraph eye = emitNode(
        registry, "Scene Depth", "lit",
        {{"mode", sg::Value::makeString(sg::ValueType::String, "eye")}});
    REQUIRE(eye.ok);
    CHECK(eye.source.find("n1_s1 = hbgSGLinearEyeDepth(hbg_sg_scene_depth_1, "
                          "gi.screenPos.xy);") != std::string::npos);

    const sg::detail::EmittedGraph differenceRaw = emitNode(
        registry, "Scene Depth Difference", "lit",
        {{"mode", sg::Value::makeString(sg::ValueType::String, "raw")}});
    REQUIRE(differenceRaw.ok);
    CHECK(differenceRaw.requiredFeatureMask == sg::detail::kGraphFeatureSceneDepth);
    CHECK(differenceRaw.source.find("n1_s0 = hbg_sg_scene_raw_1 - gi.screenPos.z;") !=
          std::string::npos);

    const sg::detail::EmittedGraph differenceLinear = emitNode(
        registry, "Scene Depth Difference", "lit",
        {{"mode", sg::Value::makeString(sg::ValueType::String, "linear01")}});
    REQUIRE(differenceLinear.ok);
    CHECK(differenceLinear.source.find("n1_s0 = hbg_sg_scene_current_01_1 - "
                                       "hbg_sg_scene_01_1;") != std::string::npos);

    const sg::detail::EmittedGraph fogEmitted = emitNode(registry, "Fog");
    REQUIRE(fogEmitted.ok);
    CHECK(fogEmitted.requiredFeatureMask == sg::detail::kGraphFeatureFog);
    CHECK(fogEmitted.source.find("hbgSGFog(gi.positionOS, n1_s0, n1_s1);") !=
          std::string::npos);
    CHECK(fogEmitted.source.find("u_fog_params.z") != std::string::npos);
    CHECK(fogEmitted.source.find("u_fog_albedo.rgb") != std::string::npos);
}

TEST_CASE("SG-6 Scene nodes and Reflection Probe use disjoint texture stages") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    graph.nodes.push_back(registry.instantiate("Scene Color", 1));
    graph.nodes.push_back(registry.instantiate("Reflection Probe", 2));
    graph.nodes.push_back(registry.instantiate("BlockNormalTangent", 100));
    graph.nodes.push_back(registry.instantiate("BlockNormalWorld", 101));
    graph.edges.push_back({1, 1, 100, 0});
    graph.edges.push_back({2, 3, 101, 0});

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph, "lit");
    REQUIRE(emitted.ok);
    CHECK(emitted.requiredFeatureMask == sg::detail::kGraphFeatureSceneColor);
    CHECK(emitted.source.find("SAMPLER2D(s_sceneColor, 0);") != std::string::npos);
}

TEST_CASE("SG-6 Scene textures use the first free graph texture stage") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    graph.properties.push_back(std::move(texture));
    graph.nodes.push_back(registry.instantiate("Scene Color", 1));
    graph.nodes.push_back(registry.instantiate("BlockBaseColor", 100));
    graph.edges.push_back({1, 1, 100, 0});

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph, "lit");
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("SAMPLER2D(s_sceneColor, 1);") != std::string::npos);
}
