#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

namespace {

struct NodeExpectation {
    const char* type;
    const char* category;
    std::vector<std::string> settings;
    std::vector<std::string> synonyms;
    int slots;
};

const NodeExpectation kNodes[] = {
    {"Sample Texture 2D", "Input/Texture",
     {"texture", "textureType", "normalMapSpace", "mipSamplingMode",
      "enableGlobalMipBias"},
     {"tex2d"}, 12},
    {"Sample Texture 2D LOD", "Input/Texture",
     {"texture", "textureType", "normalMapSpace"}, {"tex2dlod"}, 9},
    {"Sample Texture 2D Array", "Input/Texture",
     {"texture", "mipSamplingMode", "enableGlobalMipBias"},
     {"stack", "pile", "tex2darray"}, 13},
    {"Sample Texture 3D", "Input/Texture", {"texture", "mipSamplingMode"},
     {"volume", "tex3d"}, 9},
    {"Sample Cubemap", "Input/Texture", {"texture"}, {"texcube"}, 5},
    {"Sample Reflected Cubemap", "Input/Texture", {"texture"}, {}, 6},
    {"Texture 2D Asset", "Input/Texture", {"texture"}, {}, 1},
    {"Texture 2D Array Asset", "Input/Texture", {"texture"}, {"stack", "pile"}, 1},
    {"Texture 3D Asset", "Input/Texture", {"texture"}, {"volume"}, 1},
    {"Cubemap Asset", "Input/Texture", {"texture"}, {}, 1},
    {"Sampler State", "Input/Texture", {"filter", "wrap", "anisotropic"}, {}, 1},
    {"Procedural Virtual Texture", "Input/Texture",
     {"vtName", "layers", "shaderDeclaration"}, {}, 1},
    {"Sample Virtual Texture", "Input/Texture",
     {"addressMode", "lodCalculation", "sampleQuality", "enableGlobalMipBias", "noFeedback"},
     {"buffer"}, 10},
    {"Texture Size", "Input/Texture", {"texture"}, {"texel size"}, 5},
    {"Gather Texture 2D", "Input/Texture", {"texture"}, {}, 9},
    {"Calculate Level Of Detail Texture 2D", "Input/Texture", {"texture", "clamp"}, {}, 4},
    {"Split Texture Transform", "Input/Texture", {"texture"}, {}, 4},
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

sg::detail::PropertySet collectTextureProperties(const sg::NodeRegistry& registry,
                                                sg::Graph& graph) {
    REQUIRE(sg::concretize(graph, registry).ok);
    const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
    REQUIRE(target != nullptr);
    sg::detail::ActiveGraph active = sg::detail::collectActiveNodes(graph, registry, *target);
    REQUIRE(active.ok);
    return sg::detail::collectProperties(graph, active, *target);
}

sg::detail::EmittedGraph emitTextureNode(
    const sg::NodeRegistry& registry, const char* type,
    std::map<std::string, sg::Value> settings = {},
    sg::ValueType propertyType = sg::ValueType::Texture2D,
    const char* propertyRef = "_MainTex", bool isHDR = false) {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property texture;
    texture.name = propertyRef;
    texture.refName = propertyRef;
    texture.type = propertyType;
    texture.isHDR = isHDR;
    texture.defaultValue = sg::Value::makeString(propertyType, "tex.png");
    graph.properties.push_back(texture);
    sg::NodeInstance node = registry.instantiate(type, 1);
    node.settings["texture"] = sg::Value::makeString(sg::ValueType::String, propertyRef);
    for (auto& [key, value] : settings) {
        node.settings[key] = std::move(value);
    }
    graph.nodes.push_back(std::move(node));
    return emitGraph(registry, graph);
}

} // namespace

TEST_CASE("SG-6 Input/Texture nodes expose canonical texture metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    for (const NodeExpectation& expected : kNodes) {
        CAPTURE(expected.type);
        const sg::NodeDef* definition = registry.find(expected.type);
        REQUIRE(definition != nullptr);
        CHECK(definition->category == expected.category);
        CHECK(definition->title == expected.type);
        CHECK(definition->settings == expected.settings);
        CHECK(definition->synonyms == expected.synonyms);
        CHECK(static_cast<int>(definition->slots.size()) == expected.slots);
    }

    const sg::NodeDef* size = registry.find("Texture Size");
    REQUIRE(size != nullptr);
    CHECK(size->slots[0].name == "Width");
    CHECK(size->slots[1].name == "Texture");
    CHECK(static_cast<int>(size->slots[1].type) ==
          static_cast<int>(sg::ValueType::Texture2D));
    CHECK(size->slots[2].name == "Height");
    CHECK(size->slots[3].name == "Texel Width");
    CHECK(size->slots[4].name == "Texel Height");

    const sg::NodeDef* lod = registry.find("Calculate Level Of Detail Texture 2D");
    REQUIRE(lod != nullptr);
    CHECK(lod->slots[0].name == "LOD");
    CHECK(lod->slots[1].name == "Texture");
    CHECK(lod->slots[2].name == "UV");
    CHECK(lod->slots[3].name == "Sampler");

    const sg::NodeDef* sample2d = registry.find("Sample Texture 2D");
    REQUIRE(sample2d != nullptr);
    REQUIRE(sample2d->slots.size() == 12);
    CHECK(sample2d->slots[0].id == 0);
    CHECK(sample2d->slots[0].name == "RGBA");
    CHECK(sample2d->slots[1].id == 1);
    CHECK(sample2d->slots[1].name == "Texture");
    CHECK(static_cast<int>(sample2d->slots[1].type) ==
          static_cast<int>(sg::ValueType::Texture2D));
    CHECK(sample2d->slots[2].id == 2);
    CHECK(sample2d->slots[2].name == "UV");
    CHECK(sample2d->slots[3].id == 3);
    CHECK(sample2d->slots[3].name == "Sampler");
    CHECK(sample2d->slots[4].id == 4);
    CHECK(sample2d->slots[7].id == 7);
    CHECK(sample2d->slots[8].id == 8);
    CHECK(sample2d->slots[8].name == "Bias");
    CHECK(sample2d->slots[8].hidden);
    CHECK(sample2d->slots[9].name == "LOD");
    CHECK(sample2d->slots[10].name == "DDX");
    CHECK(sample2d->slots[11].name == "DDY");

    const sg::NodeDef* sample2dLod = registry.find("Sample Texture 2D LOD");
    REQUIRE(sample2dLod != nullptr);
    REQUIRE(sample2dLod->slots.size() == 9);
    CHECK(sample2dLod->slots[0].id == 0);
    CHECK(sample2dLod->slots[1].id == 1);
    CHECK(sample2dLod->slots[1].name == "Texture");
    CHECK(sample2dLod->slots[2].id == 2);
    CHECK(sample2dLod->slots[2].name == "UV");
    CHECK(sample2dLod->slots[3].id == 3);
    CHECK(sample2dLod->slots[3].name == "Sampler");
    CHECK(sample2dLod->slots[4].id == 4);
    CHECK(sample2dLod->slots[4].name == "LOD");
    CHECK(sample2dLod->slots[5].id == 5);
    CHECK(sample2dLod->slots[8].id == 8);

    const sg::NodeDef* array = registry.find("Sample Texture 2D Array");
    REQUIRE(array != nullptr);
    CHECK(array->slots[0].name == "RGBA");
    CHECK(array->slots[1].name == "Texture Array");
    CHECK(static_cast<int>(array->slots[1].type) ==
          static_cast<int>(sg::ValueType::Texture2DArray));
    CHECK(array->slots[2].name == "UV");
    CHECK(array->slots[3].name == "Sampler");
    CHECK(array->slots[8].name == "Index");
    CHECK(array->slots[9].name == "Bias");
    CHECK(array->slots[10].name == "LOD");
    CHECK(array->slots[11].name == "DDX");
    CHECK(array->slots[12].name == "DDY");

    const sg::NodeDef* volume = registry.find("Sample Texture 3D");
    REQUIRE(volume != nullptr);
    CHECK(volume->slots[0].name == "RGBA");
    CHECK(volume->slots[1].name == "Texture");
    CHECK(static_cast<int>(volume->slots[1].type) ==
          static_cast<int>(sg::ValueType::Texture3D));
    CHECK(volume->slots[2].name == "UV");
    CHECK(volume->slots[3].name == "Sampler");
    CHECK(volume->slots[4].name == "R");
    CHECK(volume->slots[7].name == "A");
    CHECK(volume->slots[8].id == 4);
    CHECK(volume->slots[8].name == "LOD");

    const sg::NodeDef* reflected = registry.find("Sample Reflected Cubemap");
    REQUIRE(reflected != nullptr);
    CHECK(reflected->slots[0].name == "Out");
    CHECK(reflected->slots[1].name == "Cube");
    CHECK(static_cast<int>(reflected->slots[1].type) ==
          static_cast<int>(sg::ValueType::Cubemap));
    CHECK(reflected->slots[2].name == "ViewDir");
    CHECK(reflected->slots[3].name == "Normal");
    CHECK(reflected->slots[4].name == "LOD");
    CHECK(reflected->slots[5].name == "Sampler");

    const sg::NodeDef* arrayAsset = registry.find("Texture 2D Array Asset");
    REQUIRE(arrayAsset != nullptr);
    CHECK(static_cast<int>(arrayAsset->slots[0].type) == static_cast<int>(sg::ValueType::Texture2DArray));

    const sg::NodeDef* split = registry.find("Split Texture Transform");
    REQUIRE(split != nullptr);
    CHECK(split->slots[1].name == "Tiling");
    CHECK(split->slots[2].name == "Offset");
    CHECK(split->slots[3].name == "Texture Only");

    const sg::NodeDef* pvt = registry.find("Procedural Virtual Texture");
    REQUIRE(pvt != nullptr);
    CHECK(static_cast<int>(pvt->slots[0].type) ==
          static_cast<int>(sg::ValueType::VirtualTexture));

    const sg::NodeDef* sampleVt = registry.find("Sample Virtual Texture");
    REQUIRE(sampleVt != nullptr);
    CHECK(sampleVt->slots[1].name == "VT");
    CHECK(sampleVt->slots[6].name == "Out");
    CHECK(sampleVt->slots[9].name == "Out4");
}

TEST_CASE("SG-6 advanced texture sample nodes emit typed samples") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph array = emitTextureNode(
        registry, "Sample Texture 2D Array", {}, sg::ValueType::Texture2DArray, "_ArrayTex");
    REQUIRE(array.ok);
    CHECK(array.source.find("SAMPLER2DARRAY(s_sg__ArrayTex,") != std::string::npos);
    CHECK(array.source.find("texture2DArray(s_sg__ArrayTex, vec3(") != std::string::npos);
    CHECK(array.source.find("n1_s0 = t_sample_1;") != std::string::npos);
    CHECK(array.source.find("n1_s7 = t_sample_1.w;") != std::string::npos);

    const sg::detail::EmittedGraph volume = emitTextureNode(
        registry, "Sample Texture 3D", {}, sg::ValueType::Texture3D, "_VolumeTex");
    REQUIRE(volume.ok);
    CHECK(volume.source.find("SAMPLER3D(s_sg__VolumeTex,") != std::string::npos);
    CHECK(volume.source.find("texture3D(s_sg__VolumeTex,") != std::string::npos);
    CHECK(volume.source.find("n1_s0 = t_sample_1;") != std::string::npos);
    CHECK(volume.source.find("n1_s8 = t_sample_1.w;") != std::string::npos);

    const sg::detail::EmittedGraph cube = emitTextureNode(
        registry, "Sample Cubemap", {}, sg::ValueType::Cubemap, "_CubeTex");
    REQUIRE(cube.ok);
    CHECK(cube.source.find("SAMPLERCUBE(s_sg__CubeTex,") != std::string::npos);
    CHECK(cube.source.find("textureCubeLod(s_sg__CubeTex,") != std::string::npos);
    CHECK(cube.source.find("n1_s0 = t_sample_1;") != std::string::npos);

    const sg::detail::EmittedGraph reflected = emitTextureNode(
        registry, "Sample Reflected Cubemap", {}, sg::ValueType::Cubemap, "_CubeTex");
    REQUIRE(reflected.ok);
    CHECK(reflected.source.find("reflect(-") != std::string::npos);
    CHECK(reflected.source.find("textureCubeLod(s_sg__CubeTex,") != std::string::npos);
    CHECK(reflected.source.find("n1_s0 = t_sample_1;") != std::string::npos);
}

TEST_CASE("SG-6 texture asset nodes declare typed samplers") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const struct {
        const char* type;
        sg::ValueType propertyType;
        const char* ref;
        const char* declaration;
    } assets[] = {
        {"Texture 2D Asset", sg::ValueType::Texture2D, "_Tex2D",
         "SAMPLER2D(s_sg__Tex2D,"},
        {"Texture 2D Array Asset", sg::ValueType::Texture2DArray, "_TexArray",
         "SAMPLER2DARRAY(s_sg__TexArray,"},
        {"Texture 3D Asset", sg::ValueType::Texture3D, "_Tex3D", "SAMPLER3D(s_sg__Tex3D,"},
        {"Cubemap Asset", sg::ValueType::Cubemap, "_TexCube", "SAMPLERCUBE(s_sg__TexCube,"},
    };
    for (const auto& asset : assets) {
        CAPTURE(asset.type);
        const sg::detail::EmittedGraph emitted =
            emitTextureNode(registry, asset.type, {}, asset.propertyType, asset.ref);
        REQUIRE(emitted.ok);
        CHECK(emitted.source.find(asset.declaration) != std::string::npos);
    }
}

TEST_CASE("SG-6 virtual texture nodes emit layer fallback samples") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";

    sg::NodeInstance pvt = registry.instantiate("Procedural Virtual Texture", 1);
    pvt.settings["vtName"] = sg::Value::makeString(sg::ValueType::String, "_VT");
    pvt.settings["layers"] = sg::Value::makeInteger(2);
    pvt.settings["shaderDeclaration"] = sg::Value::makeString(sg::ValueType::String, "per material");
    sg::NodeInstance sample = registry.instantiate("Sample Virtual Texture", 2);
    sample.settings["addressMode"] = sg::Value::makeString(sg::ValueType::String, "Clamp");
    sample.settings["lodCalculation"] =
        sg::Value::makeString(sg::ValueType::String, "Automatic");
    graph.nodes.push_back(std::move(pvt));
    graph.nodes.push_back(std::move(sample));
    graph.edges.push_back(sg::Edge{1, 0, 2, 1});

    const sg::detail::PropertySet properties = collectTextureProperties(registry, graph);
    REQUIRE(properties.ok);
    const sg::detail::PropertyInfo* layer0 = properties.find("_VT_Layer0");
    const sg::detail::PropertyInfo* layer1 = properties.find("_VT_Layer1");
    REQUIRE(layer0 != nullptr);
    REQUIRE(layer1 != nullptr);
    CHECK(static_cast<int>(layer0->type) == static_cast<int>(sg::ValueType::Texture2D));
    CHECK(layer0->samplerWrap == "clamp");
    CHECK(layer1->samplerWrap == "clamp");

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("SAMPLER2D(s_sg__VT_Layer0,") != std::string::npos);
    CHECK(emitted.source.find("SAMPLER2D(s_sg__VT_Layer1,") != std::string::npos);
    CHECK(emitted.source.find("hbgSGSampleTexture2DBias(s_sg__VT_Layer0,") !=
          std::string::npos);
    CHECK(emitted.source.find("uniform vec4 u_sg_globalMipBias;") != std::string::npos);
    CHECK(emitted.source.find("u_sg_globalMipBias.x") != std::string::npos);
    CHECK(emitted.source.find("n2_s11 = t_vt_2_0;") != std::string::npos);
    CHECK(emitted.source.find("n2_s12 = t_vt_2_1;") != std::string::npos);
    CHECK(emitted.source.find("n2_s13 = vec4(0.0, 0.0, 0.0, 0.0);") !=
          std::string::npos);

    sg::Graph missing;
    missing.kind = sg::GraphKind::Graph;
    missing.target.id = "lit";
    missing.nodes.push_back(registry.instantiate("Sample Virtual Texture", 1));
    const sg::detail::EmittedGraph failed = emitGraph(registry, missing);
    CHECK_FALSE(failed.ok);
    bool missingInput = false;
    for (const sg::Diagnostic& diagnostic : failed.diagnostics) {
        if (diagnostic.code == sg::kErrorMissingInput &&
            diagnostic.message.find("Procedural Virtual Texture") != std::string::npos) {
            missingInput = true;
        }
    }
    CHECK(missingInput);
}

TEST_CASE("SG-6 VirtualTexture Property expands an explicit layer stack") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property virtualTexture;
    virtualTexture.name = "Virtual";
    virtualTexture.refName = "_VT";
    virtualTexture.type = sg::ValueType::VirtualTexture;
    virtualTexture.vtLayers = 3;
    virtualTexture.defaultValue = sg::Value::makeString(sg::ValueType::VirtualTexture, "");
    graph.properties.push_back(virtualTexture);

    sg::NodeInstance property = registry.instantiate("Property", 1);
    property.settings["property"] = sg::Value::makeString(sg::ValueType::String, "_VT");
    sg::NodeInstance sample = registry.instantiate("Sample Virtual Texture", 2);
    sample.settings["addressMode"] =
        sg::Value::makeString(sg::ValueType::String, "clamp");
    graph.nodes.push_back(std::move(property));
    graph.nodes.push_back(std::move(sample));
    graph.edges.push_back(sg::Edge{1, 0, 2, 1});

    const sg::detail::PropertySet properties = collectTextureProperties(registry, graph);
    REQUIRE(properties.ok);
    REQUIRE(properties.find("_VT") == nullptr);
    const sg::detail::PropertyInfo* layer2 = properties.find("_VT_Layer2");
    REQUIRE(layer2 != nullptr);
    CHECK(static_cast<int>(layer2->type) == static_cast<int>(sg::ValueType::Texture2D));
    CHECK(layer2->samplerWrap == "clamp");

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("SAMPLER2D(s_sg__VT_Layer0,") != std::string::npos);
    CHECK(emitted.source.find("SAMPLER2D(s_sg__VT_Layer1,") != std::string::npos);
    CHECK(emitted.source.find("SAMPLER2D(s_sg__VT_Layer2,") != std::string::npos);
    CHECK(emitted.source.find("hbgSGSampleTexture2DBias(s_sg__VT_Layer2,") !=
          std::string::npos);
    CHECK(emitted.source.find("u_sg_globalMipBias.x") != std::string::npos);
}

TEST_CASE("SG-6 VirtualTexture Property uses Unity fallback stack settings") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property virtualTexture;
    virtualTexture.name = "Virtual";
    virtualTexture.refName = "_VT";
    virtualTexture.type = sg::ValueType::VirtualTexture;
    virtualTexture.vtLayers = 3;
    virtualTexture.vtLayerRefNames = {"_VT_Albedo", "_VT_Normal", "_VT_Mask"};
    virtualTexture.vtLayerTextureTypes = {"default", "normalTangentSpace",
                                         "normalObjectSpace"};
    virtualTexture.defaultValue =
        sg::Value::makeString(sg::ValueType::VirtualTexture, "");
    graph.properties.push_back(virtualTexture);

    sg::NodeInstance property = registry.instantiate("Property", 1);
    property.settings["property"] = sg::Value::makeString(sg::ValueType::String, "_VT");
    sg::NodeInstance sample = registry.instantiate("Sample Virtual Texture", 2);
    sample.settings["addressMode"] = sg::Value::makeString(sg::ValueType::String, "clamp");
    sample.settings["lodCalculation"] =
        sg::Value::makeString(sg::ValueType::String, "Derivatives");
    sample.settings["sampleQuality"] = sg::Value::makeString(sg::ValueType::String, "low");
    sample.settings["enableGlobalMipBias"] = sg::Value::makeBool(false);
    sample.settings["noFeedback"] = sg::Value::makeBool(true);
    graph.nodes.push_back(std::move(property));
    graph.nodes.push_back(std::move(sample));
    graph.edges.push_back(sg::Edge{1, 0, 2, 1});

    const auto source = sg::detail::virtualTextureSource(graph, graph.nodes[1]);
    REQUIRE(source);
    CHECK(source->layerRefNames[0] == "_VT_Albedo");
    CHECK(source->layerRefNames[1] == "_VT_Normal");
    CHECK(source->layerRefNames[2] == "_VT_Mask");
    CHECK(source->layerTextureTypes[0] == "default");
    CHECK(source->layerTextureTypes[1] == "normalTangentSpace");
    CHECK(source->layerTextureTypes[2] == "normalObjectSpace");
    CHECK(source->sampleQuality == "low");
    CHECK_FALSE(source->enableGlobalMipBias);
    CHECK(source->noFeedback);

    const sg::detail::PropertySet properties = collectTextureProperties(registry, graph);
    REQUIRE(properties.ok);
    CHECK(properties.find("_VT_Albedo") != nullptr);
    CHECK(properties.find("_VT_Normal") != nullptr);
    CHECK(properties.find("_VT_Mask") != nullptr);

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("hbgSGVirtualTextureDerivativeLod(") != std::string::npos);
    CHECK(emitted.source.find("texture2DLod(s_sg__VT_Normal,") != std::string::npos);
    CHECK(emitted.source.find("hbgSGSampleTexture2DGrad(s_sg__VT_Normal,") ==
          std::string::npos);
    CHECK(emitted.source.find("t_vt_2_1.rgb = hbgSGUnpackNormal(t_vt_2_1);") !=
          std::string::npos);
    CHECK(emitted.source.find("t_vt_2_2.rgb = t_vt_2_2.rgb * 2.0 - 1.0;") !=
          std::string::npos);
    CHECK(emitted.source.find("uniform vec4 u_sg_globalMipBias;") == std::string::npos);
}

TEST_CASE("SG-6 VirtualTexture noFeedback LOD is required for vertex displacement") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const auto buildGraph = [&registry](const char* lod, bool noFeedback) {
        sg::Graph graph;
        graph.kind = sg::GraphKind::Graph;
        graph.target.id = "lit";

        sg::NodeInstance pvt = registry.instantiate("Procedural Virtual Texture", 1);
        pvt.settings["vtName"] = sg::Value::makeString(sg::ValueType::String, "_VT");
        pvt.settings["layers"] = sg::Value::makeInteger(1);
        sg::NodeInstance sample = registry.instantiate("Sample Virtual Texture", 2);
        sample.settings["lodCalculation"] =
            sg::Value::makeString(sg::ValueType::String, lod);
        sample.settings["noFeedback"] = sg::Value::makeBool(noFeedback);
        graph.nodes.push_back(std::move(pvt));
        graph.nodes.push_back(std::move(sample));
        graph.nodes.push_back(registry.instantiate("Split", 3));
        graph.nodes.push_back(registry.instantiate("Vector 3", 4));
        graph.nodes.push_back(registry.instantiate("BlockPosition", 5));
        graph.edges.push_back(sg::Edge{1, 0, 2, 1});
        graph.edges.push_back(sg::Edge{2, 11, 3, 0});
        graph.edges.push_back(sg::Edge{3, 1, 4, 1});
        graph.edges.push_back(sg::Edge{3, 2, 4, 2});
        graph.edges.push_back(sg::Edge{3, 3, 4, 3});
        graph.edges.push_back(sg::Edge{4, 0, 5, 0});
        return graph;
    };

    sg::Graph fragmentOnlyGraph = buildGraph("automatic", false);
    const sg::detail::EmittedGraph fragmentOnly = emitGraph(registry, fragmentOnlyGraph);
    CHECK_FALSE(fragmentOnly.ok);
    bool vertexDiagnostic = false;
    for (const sg::Diagnostic& diagnostic : fragmentOnly.diagnostics) {
        if (diagnostic.message.find("vertex displacement") != std::string::npos) {
            vertexDiagnostic = true;
        }
    }
    CHECK(vertexDiagnostic);

    sg::Graph vertexAllowedGraph = buildGraph("lod", true);
    const sg::detail::EmittedGraph vertexAllowed = emitGraph(registry, vertexAllowedGraph);
    CHECK(vertexAllowed.ok);
    CHECK(vertexAllowed.source.find("HBG_SG_VERTEX_DISPLACEMENT") != std::string::npos);
}

TEST_CASE("SG-6 VirtualTexture low quality bias uses the computed LOD") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";

    sg::NodeInstance pvt = registry.instantiate("Procedural Virtual Texture", 1);
    pvt.settings["vtName"] = sg::Value::makeString(sg::ValueType::String, "_VT");
    pvt.settings["layers"] = sg::Value::makeInteger(1);
    sg::NodeInstance sample = registry.instantiate("Sample Virtual Texture", 2);
    sample.settings["lodCalculation"] = sg::Value::makeString(sg::ValueType::String, "bias");
    sample.settings["sampleQuality"] = sg::Value::makeString(sg::ValueType::String, "low");
    sample.settings["enableGlobalMipBias"] = sg::Value::makeBool(false);
    graph.nodes.push_back(std::move(pvt));
    graph.nodes.push_back(std::move(sample));
    graph.edges.push_back(sg::Edge{1, 0, 2, 1});

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("hbgSGVirtualTextureBiasLod(") != std::string::npos);
    CHECK(emitted.source.find("texture2DLod(s_sg__VT_Layer0,") != std::string::npos);
    CHECK(emitted.source.find("hbgSGSampleTexture2DBias(s_sg__VT_Layer0,") ==
          std::string::npos);
}

TEST_CASE("SG-6 Split Texture Transform emits identity tiling and offset") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::detail::EmittedGraph split =
        emitTextureNode(registry, "Split Texture Transform");
    REQUIRE(split.ok);
    CHECK(split.source.find("n1_s1 = vec2(1.0, 1.0);") != std::string::npos);
    CHECK(split.source.find("n1_s2 = vec2(0.0, 0.0);") != std::string::npos);
    CHECK(split.source.find("u_sg__MainTex_ST") == std::string::npos);
}

TEST_CASE("SG-6 texture property transform drives sample UVs and split outputs") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    texture.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "tex.png");
    texture.useTilingAndOffset = true;
    texture.tiling = {2.0f, 3.0f};
    texture.offset = {0.25f, 0.5f};
    graph.properties.push_back(texture);

    sg::NodeInstance property = registry.instantiate("Property", 1);
    property.settings["property"] = sg::Value::makeString(sg::ValueType::String, "_MainTex");
    sg::NodeInstance split = registry.instantiate("Split Texture Transform", 2);
    sg::NodeInstance direct =
        registry.instantiate("Sample Texture 2D", 3);
    sg::NodeInstance throughSplit =
        registry.instantiate("Sample Texture 2D", 4);
    graph.nodes.push_back(std::move(property));
    graph.nodes.push_back(std::move(split));
    graph.nodes.push_back(std::move(direct));
    graph.nodes.push_back(std::move(throughSplit));
    graph.edges.push_back(sg::Edge{1, 0, 2, 0});
    graph.edges.push_back(sg::Edge{1, 0, 3, 1});
    graph.edges.push_back(sg::Edge{2, 3, 4, 1});

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("uniform vec4 u_sg__MainTex_ST;") != std::string::npos);
    CHECK(emitted.source.find("n2_s1 = u_sg__MainTex_ST.xy;") != std::string::npos);
    CHECK(emitted.source.find("n2_s2 = u_sg__MainTex_ST.zw;") != std::string::npos);
    CHECK(emitted.source.find(
              "t_sample_3 = hbgSGSampleTexture2DBias(s_sg__MainTex, (gi.uv0 * "
              "u_sg__MainTex_ST.xy + u_sg__MainTex_ST.zw), u_sg_globalMipBias.x)") !=
          std::string::npos);
    CHECK(emitted.source.find(
              "t_sample_4 = hbgSGSampleTexture2DBias(s_sg__MainTex, gi.uv0, "
              "u_sg_globalMipBias.x)") != std::string::npos);
}

TEST_CASE("SG-6 Sampler State overrides the connected texture sampler") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    texture.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "tex.png");
    graph.properties.push_back(texture);

    sg::NodeInstance sample = registry.instantiate("Sample Texture 2D", 1);
    sample.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_MainTex");
    sg::NodeInstance sampler = registry.instantiate("Sampler State", 2);
    sampler.settings["filter"] = sg::Value::makeString(sg::ValueType::String, "Point");
    sampler.settings["wrap"] = sg::Value::makeString(sg::ValueType::String, "Clamp");
    sampler.settings["anisotropic"] = sg::Value::makeString(sg::ValueType::String, "x8");
    graph.nodes.push_back(std::move(sample));
    graph.nodes.push_back(std::move(sampler));
    graph.edges.push_back(sg::Edge{2, 0, 1, 3});

    const sg::detail::PropertySet properties = collectTextureProperties(registry, graph);
    REQUIRE(properties.ok);
    const sg::detail::PropertyInfo* info = properties.find("_MainTex");
    REQUIRE(info != nullptr);
    CHECK(info->samplerFilter == "point");
    CHECK(info->samplerWrap == "clamp");
    CHECK(info->samplerAnisotropic == "x8");
}

TEST_CASE("SG-6 Sampler State overrides a texture sourced from Property") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    texture.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "tex.png");
    graph.properties.push_back(texture);

    sg::NodeInstance property = registry.instantiate("Property", 1);
    property.settings["property"] =
        sg::Value::makeString(sg::ValueType::String, "_MainTex");
    sg::NodeInstance sample = registry.instantiate("Sample Texture 2D", 2);
    sg::NodeInstance sampler = registry.instantiate("Sampler State", 3);
    sampler.settings["filter"] = sg::Value::makeString(sg::ValueType::String, "Point");
    sampler.settings["wrap"] = sg::Value::makeString(sg::ValueType::String, "Clamp");
    sampler.settings["anisotropic"] = sg::Value::makeString(sg::ValueType::String, "x4");
    graph.nodes.push_back(std::move(property));
    graph.nodes.push_back(std::move(sample));
    graph.nodes.push_back(std::move(sampler));
    graph.edges.push_back(sg::Edge{1, 0, 2, 1});
    graph.edges.push_back(sg::Edge{3, 0, 2, 3});

    const sg::detail::PropertySet properties = collectTextureProperties(registry, graph);
    REQUIRE(properties.ok);
    const sg::detail::PropertyInfo* info = properties.find("_MainTex");
    REQUIRE(info != nullptr);
    CHECK(info->samplerFilter == "point");
    CHECK(info->samplerWrap == "clamp");
    CHECK(info->samplerAnisotropic == "x4");
}

TEST_CASE("SG-6 SamplerState Property overrides the connected texture sampler") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    texture.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "tex.png");
    graph.properties.push_back(texture);
    sg::Property samplerProperty;
    samplerProperty.name = "Sampler";
    samplerProperty.refName = "_Sampler";
    samplerProperty.type = sg::ValueType::SamplerState;
    samplerProperty.samplerFilter = "point";
    samplerProperty.samplerWrap = "clamp";
    samplerProperty.samplerAnisotropic = "x4";
    graph.properties.push_back(samplerProperty);

    sg::NodeInstance sample = registry.instantiate("Sample Texture 2D", 1);
    sample.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_MainTex");
    sg::NodeInstance sampler = registry.instantiate("Property", 2);
    sampler.settings["property"] = sg::Value::makeString(sg::ValueType::String, "_Sampler");
    graph.nodes.push_back(std::move(sample));
    graph.nodes.push_back(std::move(sampler));
    graph.edges.push_back(sg::Edge{2, 0, 1, 3});

    const sg::detail::PropertySet properties = collectTextureProperties(registry, graph);
    REQUIRE(properties.ok);
    const sg::detail::PropertyInfo* info = properties.find("_MainTex");
    REQUIRE(info != nullptr);
    CHECK(info->samplerFilter == "point");
    CHECK(info->samplerWrap == "clamp");
    CHECK(info->samplerAnisotropic == "x4");

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("hbgSGSampleTexture2DBias(s_sg__MainTex,") !=
          std::string::npos);
    CHECK(emitted.source.find("u_sg_globalMipBias.x") != std::string::npos);
}

TEST_CASE("SG-6 texture properties keep their blackboard sampler settings") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    texture.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "tex.png");
    texture.samplerFilter = "trilinear";
    texture.samplerWrap = "mirror";
    texture.samplerAnisotropic = "x16";
    graph.properties.push_back(texture);

    sg::NodeInstance sample = registry.instantiate("Sample Texture 2D", 1);
    sample.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_MainTex");
    graph.nodes.push_back(std::move(sample));

    const sg::detail::PropertySet properties = collectTextureProperties(registry, graph);
    REQUIRE(properties.ok);
    const sg::detail::PropertyInfo* info = properties.find("_MainTex");
    REQUIRE(info != nullptr);
    CHECK(info->samplerFilter == "trilinear");
    CHECK(info->samplerWrap == "mirror");
    CHECK(info->samplerAnisotropic == "x16");
}

TEST_CASE("SG-6 Sample Texture 2D and LOD emit texture samples") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph sample = emitTextureNode(registry, "Sample Texture 2D");
    REQUIRE(sample.ok);
    CHECK(sample.source.find("t_sample_1 = hbgSGSampleTexture2DBias(s_sg__MainTex,") !=
          std::string::npos);
    CHECK(sample.source.find("u_sg_globalMipBias.x") != std::string::npos);
    CHECK(sample.source.find("n1_s0 = t_sample_1;") != std::string::npos);
    CHECK(sample.source.find("n1_s4 = t_sample_1.x;") != std::string::npos);
    CHECK(sample.source.find("n1_s7 = t_sample_1.w;") != std::string::npos);

    const sg::detail::EmittedGraph lod =
        emitTextureNode(registry, "Sample Texture 2D LOD");
    REQUIRE(lod.ok);
    CHECK(lod.source.find("t_sample_1 = texture2DLod(s_sg__MainTex,") != std::string::npos);
    CHECK(lod.source.find("n1_s0 = t_sample_1;") != std::string::npos);
    CHECK(lod.source.find("n1_s8 = t_sample_1.w;") != std::string::npos);
}

TEST_CASE("SG-6 texture mip sampling modes match Unity sampling macros") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const auto mode = [](const char* value) {
        return std::map<std::string, sg::Value>{
            {"mipSamplingMode", sg::Value::makeString(sg::ValueType::String, value)}};
    };

    const sg::detail::EmittedGraph bias =
        emitTextureNode(registry, "Sample Texture 2D", mode("Bias"));
    REQUIRE(bias.ok);
    CHECK(bias.source.find("hbgSGSampleTexture2DBias(s_sg__MainTex,") != std::string::npos);
    CHECK(bias.source.find("texture2DLod(textureSampler, uv, max(bias, 0.0))") !=
          std::string::npos);

    const sg::detail::EmittedGraph gradient =
        emitTextureNode(registry, "Sample Texture 2D", mode("Gradient"));
    REQUIRE(gradient.ok);
    CHECK(gradient.source.find("hbgSGSampleTexture2DGrad(s_sg__MainTex,") !=
          std::string::npos);
    CHECK(gradient.source.find("texture2DGrad(textureSampler, uv, dx, dy)") !=
          std::string::npos);

    const sg::detail::EmittedGraph lod =
        emitTextureNode(registry, "Sample Texture 2D", mode("LOD"));
    REQUIRE(lod.ok);
    CHECK(lod.source.find("texture2DLod(s_sg__MainTex,") != std::string::npos);

    const sg::detail::EmittedGraph arrayLod = emitTextureNode(
        registry, "Sample Texture 2D Array", mode("LOD"), sg::ValueType::Texture2DArray,
        "_ArrayTex");
    REQUIRE(arrayLod.ok);
    CHECK(arrayLod.source.find("texture2DArrayLod(s_sg__ArrayTex, vec3(") !=
          std::string::npos);

    const sg::detail::EmittedGraph arrayBias = emitTextureNode(
        registry, "Sample Texture 2D Array", mode("Bias"), sg::ValueType::Texture2DArray,
        "_ArrayTex");
    REQUIRE(arrayBias.ok);
    CHECK(arrayBias.source.find("hbgSGSampleTexture2DArrayBias(s_sg__ArrayTex,") !=
          std::string::npos);

    const sg::detail::EmittedGraph arrayGradient = emitTextureNode(
        registry, "Sample Texture 2D Array", mode("Gradient"),
        sg::ValueType::Texture2DArray, "_ArrayTex");
    REQUIRE(arrayGradient.ok);
    CHECK(arrayGradient.source.find("hbgSGSampleTexture2DArrayGrad(s_sg__ArrayTex,") !=
          std::string::npos);

    const sg::detail::EmittedGraph volumeLod = emitTextureNode(
        registry, "Sample Texture 3D", mode("LOD"), sg::ValueType::Texture3D,
        "_VolumeTex");
    REQUIRE(volumeLod.ok);
    CHECK(volumeLod.source.find("texture3DLod(s_sg__VolumeTex,") != std::string::npos);
}

TEST_CASE("SG-6 enableGlobalMipBias matches Unity sampling macros") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph standard =
        emitTextureNode(registry, "Sample Texture 2D");
    REQUIRE(standard.ok);
    CHECK(standard.source.find("uniform vec4 u_sg_globalMipBias;") != std::string::npos);
    CHECK(standard.source.find("hbgSGSampleTexture2DBias(s_sg__MainTex,") !=
          std::string::npos);
    CHECK(standard.source.find("u_sg_globalMipBias.x") != std::string::npos);

    const sg::detail::EmittedGraph disabled = emitTextureNode(
        registry, "Sample Texture 2D",
        {{"enableGlobalMipBias", sg::Value::makeBool(false)}});
    REQUIRE(disabled.ok);
    CHECK(disabled.source.find("uniform vec4 u_sg_globalMipBias;") == std::string::npos);
    CHECK(disabled.source.find("t_sample_1 = texture2D(s_sg__MainTex,") !=
          std::string::npos);

    const sg::detail::EmittedGraph arrayDefault = emitTextureNode(
        registry, "Sample Texture 2D Array", {}, sg::ValueType::Texture2DArray,
        "_ArrayTex");
    REQUIRE(arrayDefault.ok);
    CHECK(arrayDefault.source.find("uniform vec4 u_sg_globalMipBias;") ==
          std::string::npos);
    CHECK(arrayDefault.source.find("texture2DArray(s_sg__ArrayTex,") !=
          std::string::npos);

    const sg::detail::EmittedGraph arrayBias = emitTextureNode(
        registry, "Sample Texture 2D Array",
        {{"enableGlobalMipBias", sg::Value::makeBool(true)}},
        sg::ValueType::Texture2DArray, "_ArrayTex");
    REQUIRE(arrayBias.ok);
    CHECK(arrayBias.source.find("uniform vec4 u_sg_globalMipBias;") !=
          std::string::npos);
    CHECK(arrayBias.source.find("hbgSGSampleTexture2DArrayBias(s_sg__ArrayTex,") !=
          std::string::npos);

    const sg::detail::EmittedGraph lod = emitTextureNode(
        registry, "Sample Texture 2D",
        {{"mipSamplingMode", sg::Value::makeString(sg::ValueType::String, "LOD")},
         {"enableGlobalMipBias", sg::Value::makeBool(true)}});
    REQUIRE(lod.ok);
    CHECK(lod.source.find("uniform vec4 u_sg_globalMipBias;") == std::string::npos);
    CHECK(lod.source.find("t_sample_1 = texture2DLod(s_sg__MainTex,") !=
          std::string::npos);
}

TEST_CASE("SG-6 HDR texture properties emit Unity decode instructions") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();

    const sg::detail::EmittedGraph sample =
        emitTextureNode(registry, "Sample Texture 2D", {}, sg::ValueType::Texture2D,
                        "_HDRTex", true);
    REQUIRE(sample.ok);
    CHECK(sample.source.find("uniform vec4 u_sg__HDRTex_HDR;") != std::string::npos);
    CHECK(sample.source.find("if (u_sg__HDRTex_HDR.x > 0.0)") != std::string::npos);
    CHECK(sample.source.find(
              "t_sample_1 = hbgSGDecodeHDRSample(t_sample_1, u_sg__HDRTex_HDR);") !=
          std::string::npos);
    CHECK(sample.source.find("float alpha = decodeInstructions.w * (encoded.a - 1.0) + 1.0;") !=
          std::string::npos);

    const sg::detail::EmittedGraph array =
        emitTextureNode(registry, "Sample Texture 2D Array", {},
                        sg::ValueType::Texture2DArray, "_HDRArray", true);
    REQUIRE(array.ok);
    CHECK(array.source.find("uniform vec4 u_sg__HDRArray_HDR;") != std::string::npos);
    CHECK(array.source.find("hbgSGDecodeHDRSample(t_sample_1, u_sg__HDRArray_HDR)") !=
          std::string::npos);

    const sg::detail::EmittedGraph volume =
        emitTextureNode(registry, "Sample Texture 3D", {}, sg::ValueType::Texture3D,
                        "_HDRVolume", true);
    REQUIRE(volume.ok);
    CHECK(volume.source.find("hbgSGDecodeHDRSample(t_sample_1, u_sg__HDRVolume_HDR)") !=
          std::string::npos);

    const sg::detail::EmittedGraph cube =
        emitTextureNode(registry, "Sample Cubemap", {}, sg::ValueType::Cubemap,
                        "_HDRCube", true);
    REQUIRE(cube.ok);
    CHECK(cube.source.find("hbgSGDecodeHDRSample(t_sample_1, u_sg__HDRCube_HDR)") !=
          std::string::npos);

    const sg::detail::EmittedGraph regular =
        emitTextureNode(registry, "Sample Texture 2D");
    REQUIRE(regular.ok);
    CHECK(regular.source.find("u_sg__MainTex_HDR") == std::string::npos);
    CHECK(regular.source.find("hbgSGDecodeHDRSample") == std::string::npos);
}

TEST_CASE("SG-6 Sample Texture 2D unpacks tangent and object normal maps") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    std::map<std::string, sg::Value> normalSettings;
    normalSettings["textureType"] =
        sg::Value::makeString(sg::ValueType::String, "normal");
    normalSettings["normalMapSpace"] =
        sg::Value::makeString(sg::ValueType::String, "tangent");
    const sg::detail::EmittedGraph tangent =
        emitTextureNode(registry, "Sample Texture 2D", std::move(normalSettings));
    REQUIRE(tangent.ok);
    CHECK(tangent.source.find("hbgSGUnpackNormal(t_sample_1)") != std::string::npos);

    std::map<std::string, sg::Value> objectSettings;
    objectSettings["textureType"] =
        sg::Value::makeString(sg::ValueType::String, "normal");
    objectSettings["normalMapSpace"] =
        sg::Value::makeString(sg::ValueType::String, "object");
    const sg::detail::EmittedGraph object =
        emitTextureNode(registry, "Sample Texture 2D", std::move(objectSettings));
    REQUIRE(object.ok);
    CHECK(object.source.find("t_sample_1.rgb = t_sample_1.rgb * 2.0 - 1.0;") !=
          std::string::npos);
    CHECK(object.source.find("hbgSGUnpackNormal") == std::string::npos);
}

TEST_CASE("SG-6 Texture Size emits dimensions and texel size") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::detail::EmittedGraph size = emitTextureNode(registry, "Texture Size");
    REQUIRE(size.ok);
    CHECK(size.source.find("n1_s0 = u_sg__MainTex_TexelSize.z;") != std::string::npos);
    CHECK(size.source.find("n1_s2 = u_sg__MainTex_TexelSize.w;") != std::string::npos);
    CHECK(size.source.find("n1_s3 = u_sg__MainTex_TexelSize.x;") != std::string::npos);
    CHECK(size.source.find("n1_s4 = u_sg__MainTex_TexelSize.y;") != std::string::npos);
}

TEST_CASE("SG-6 Gather Texture 2D samples four offsets") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::detail::EmittedGraph gather = emitTextureNode(registry, "Gather Texture 2D");
    REQUIRE(gather.ok);
    int samples = 0;
    std::size_t offset = 0;
    while ((offset = gather.source.find("texture2DLod(s_sg__MainTex", offset)) !=
           std::string::npos) {
        ++samples;
        offset += 1;
    }
    CHECK(samples == 4);
    CHECK(gather.source.find("trunc(vec2(0.0, 0.0))") != std::string::npos);
    CHECK(gather.source.find("n1_s0 = vec4(n1_s5, n1_s6, n1_s7, n1_s8);") !=
          std::string::npos);
}

TEST_CASE("SG-6 Calculate Level Of Detail Texture 2D emits derivative fallback") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::detail::EmittedGraph lod =
        emitTextureNode(registry, "Calculate Level Of Detail Texture 2D");
    REQUIRE(lod.ok);
    CHECK(lod.source.find("ddx(") != std::string::npos);
    CHECK(lod.source.find("ddy(") != std::string::npos);
    CHECK(lod.source.find("0.5 * log2(") != std::string::npos);
    CHECK(lod.source.find("n1_s0 = min(n1_s0,") != std::string::npos);

    std::map<std::string, sg::Value> unclamped;
    unclamped["clamp"] = sg::Value::makeBool(false);
    const sg::detail::EmittedGraph noClamp = emitTextureNode(
        registry, "Calculate Level Of Detail Texture 2D", std::move(unclamped));
    REQUIRE(noClamp.ok);
    CHECK(noClamp.source.find("n1_s0 = min(n1_s0,") == std::string::npos);
}
