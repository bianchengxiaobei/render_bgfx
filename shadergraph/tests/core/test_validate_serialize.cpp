#include "test_helpers.h"

#include "sg/serialize.h"
#include "sg/validate.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

TEST_CASE("validation reports cycles targets and keyword limits") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sgt::Graph graph = sgt::makeGraph();
    sgt::addNodes(graph, registry, {{"FloatIn", 1}, {"FloatIn", 2}});
    REQUIRE(graph.addEdge({1, 1, 2, 0}));
    REQUIRE(graph.addEdge({2, 1, 1, 0}));
    sg::ValidationResult result = sg::validate(graph, registry);
    CHECK_FALSE(result.ok);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorCycle));

    sgt::NodeDef limited =
        sgt::definition("Limited", {sgt::slot(0, "Out", sg::SlotDirection::Output, sg::ValueType::Float)});
    limited.allowedTargets.push_back("unlit");
    CHECK(registry.add(limited));
    sgt::Graph target = sgt::makeGraph();
    sgt::addNodes(target, registry, {{"Limited", 1}});
    result = sg::validate(target, registry);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorTargetUnsupported));

    sgt::Graph keywords = sgt::makeGraph();
    for (int i = 0; i < 7; ++i) {
        sg::Keyword keyword;
        keyword.name = "K" + std::to_string(i);
        keyword.refName = "K" + std::to_string(i);
        REQUIRE(keywords.addKeyword(keyword));
    }
    result = sg::validate(keywords, registry);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorKeywordLimit));

    sgt::Graph unknown = sgt::makeGraph();
    REQUIRE(unknown.addNode(registry.instantiate("Missing", 1)));
    result = sg::validate(unknown, registry);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorUnknownNode));

    sgt::Graph duplicate = sgt::makeGraph();
    sgt::addNodes(duplicate, registry, {{"Constant", 1}});
    duplicate.nodes.push_back(duplicate.nodes.front());
    result = sg::validate(duplicate, registry);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorDuplicateId));
}

TEST_CASE("serialization is deterministic and migrates old schemas") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    sgt::Graph graph = sgt::makeGraph();
    graph.generatorVersion = "sg-1";
    sgt::addNodes(graph, registry, {{"Constant", 1}, {"FloatIn", 2}, {"FloatIn", 3}});
    REQUIRE(graph.addEdge({1, 0, 2, 0}));
    REQUIRE(graph.setSlotValue(3, 0, sg::SlotMode::Literal, sg::Value::makeFloat(3.0f)));

    sg::NodeInstance& node = *graph.findNode(2);
    node.precision = sg::Precision::Half;
    node.setSetting("mode", sg::Value::makeString(sg::ValueType::String, "x"));

    sg::Property property;
    property.name = "Color";
    property.refName = "_Color";
    property.type = sg::ValueType::Vector4;
    property.defaultValue = sg::Value::makeVector4({1, 0, 0, 1});
    REQUIRE(graph.addProperty(property));

    sg::Property mode;
    mode.name = "Mode";
    mode.refName = "_Mode";
    mode.type = sg::ValueType::Float;
    mode.floatMode = "enum";
    mode.enumNames = {"Low", "High"};
    mode.enumValues = {0, 3};
    mode.defaultValue = sg::Value::makeFloat(0.0f);
    REQUIRE(graph.addProperty(mode));

    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    texture.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "tex.png");
    texture.useTilingAndOffset = true;
    texture.isHDR = true;
    texture.defaultType = "bump";
    texture.samplerAnisotropic = "x8";
    texture.tiling = {2.0f, 3.0f};
    texture.offset = {0.25f, 0.5f};
    REQUIRE(graph.addProperty(texture));

    sg::Property virtualTexture;
    virtualTexture.name = "Virtual";
    virtualTexture.refName = "_VT";
    virtualTexture.type = sg::ValueType::VirtualTexture;
    virtualTexture.vtLayers = 3;
    virtualTexture.vtLayerRefNames = {"_VT_Albedo", "_VT_Normal", "_VT_Mask"};
    virtualTexture.vtLayerTextureTypes = {"default", "NormalTangentSpace",
                                         "normalObjectSpace"};
    virtualTexture.defaultValue =
        sg::Value::makeString(sg::ValueType::VirtualTexture, "");
    REQUIRE(graph.addProperty(virtualTexture));

    sg::Keyword keyword;
    keyword.name = "Mode";
    keyword.refName = "_MODE";
    keyword.type = sg::KeywordType::Enum;
    keyword.entries = {{1, "A", "MODE_A"}, {2, "B", "MODE_B"}};
    REQUIRE(graph.addKeyword(keyword));

    graph.groups.push_back({7, "Group", {10, 20}, {30, 40}, {1, 1, 1, 1}, {1, 2}});
    graph.notes.push_back({8, "Note", {1, 2}, {3, 4}, "text", "classic"});

    const std::string first = sg::serialize(graph);
    const std::string second = sg::serialize(graph);
    CHECK(first == second);
    graph.defaultPrecision = sg::ConcretePrecision::Half;
    const sg::GraphLoadResult half = sg::deserialize(sg::serialize(graph), registry);
    REQUIRE(half.ok);
    CHECK(static_cast<int>(half.graph.defaultPrecision) ==
          static_cast<int>(sg::ConcretePrecision::Half));
    graph.defaultPrecision = sg::ConcretePrecision::Single;
    graph.defaultGraphPrecision = sg::GraphPrecision::Graph;
    const sg::GraphLoadResult graphPrecision = sg::deserialize(sg::serialize(graph), registry);
    REQUIRE(graphPrecision.ok);
    CHECK(static_cast<int>(graphPrecision.graph.defaultGraphPrecision) ==
          static_cast<int>(sg::GraphPrecision::Graph));
    graph.defaultGraphPrecision = sg::GraphPrecision::Single;

    sg::GraphLoadResult loaded = sg::deserialize(first, registry);
    REQUIRE(loaded.ok);
    CHECK(sg::serialize(loaded.graph) == first);
    const sg::Property* loadedMode = loaded.graph.findProperty("_Mode");
    REQUIRE(loadedMode != nullptr);
    CHECK(loadedMode->floatMode == "enum");
    CHECK(loadedMode->enumNames == std::vector<std::string>{"Low", "High"});
    CHECK(loadedMode->enumValues == std::vector<int>{0, 3});
    const sg::Property* loadedTexture = loaded.graph.findProperty("_MainTex");
    REQUIRE(loadedTexture != nullptr);
    CHECK(loadedTexture->useTilingAndOffset);
    CHECK(loadedTexture->isHDR);
    CHECK(loadedTexture->defaultType == "bump");
    CHECK(loadedTexture->samplerAnisotropic == "x8");
    CHECK(loadedTexture->tiling.x == doctest::Approx(2.0f));
    CHECK(loadedTexture->tiling.y == doctest::Approx(3.0f));
    CHECK(loadedTexture->offset.x == doctest::Approx(0.25f));
    CHECK(loadedTexture->offset.y == doctest::Approx(0.5f));
    const sg::Property* loadedVirtualTexture = loaded.graph.findProperty("_VT");
    REQUIRE(loadedVirtualTexture != nullptr);
    CHECK(loadedVirtualTexture->vtLayers == 3);
    CHECK(loadedVirtualTexture->vtLayerRefNames ==
          std::vector<std::string>{"_VT_Albedo", "_VT_Normal", "_VT_Mask"});
    CHECK(loadedVirtualTexture->vtLayerTextureTypes ==
          std::vector<std::string>{"default", "normalTangentSpace", "normalObjectSpace"});

    sgt::Graph moved = graph;
    moved.nodes[0].position = {999, 999};
    moved.groups[0].position = {999, 999};
    CHECK(sg::contentHash(graph) == sg::contentHash(moved));
    moved.properties[0].defaultValue = sg::Value::makeVector4({0, 0, 0, 0});
    CHECK(sg::contentHash(graph) != sg::contentHash(moved));

    std::ifstream oldFixture(std::string(SG_SOURCE_DIR) + "/tests/fixtures/old_graph_v0.sgraph.json");
    std::ostringstream oldBuffer;
    oldBuffer << oldFixture.rdbuf();
    loaded = sg::deserialize(oldBuffer.str(), registry);
    REQUIRE(loaded.ok);
    CHECK(loaded.graph.schemaVersion == 1);
    CHECK(loaded.graph.target.id == "lit");
    CHECK(loaded.graph.findNode(1)->typeVersion == 1);
    CHECK(loaded.graph.findNode(1)->position == sg::Vec2{});

    const std::string newer =
        R"({"schemaVersion": 2, "kind": "graph", "name": "X", "target": {"id": "lit", "subTarget": "lit"}})";
    loaded = sg::deserialize(newer, registry);
    CHECK_FALSE(loaded.ok);
    CHECK(sgt::hasCode(loaded.diagnostics, sg::kErrorSchemaVersion));

    sg::NodeDef migrating;
    migrating.type = "Migrating";
    migrating.typeVersion = 2;
    migrating.slots.push_back(sgt::slot(0, "Out", sg::SlotDirection::Output, sg::ValueType::Float));
    migrating.migrate = [](sg::NodeInstance& node, int fromVersion, std::string&) {
        if (fromVersion < 2) {
            node.setSetting("migrated", sg::Value::makeBool(true));
        }
        return true;
    };
    CHECK(registry.add(migrating));
    loaded = sg::deserialize(
        R"({"schemaVersion":1,"kind":"graph","name":"M","target":{"id":"lit","subTarget":"lit"},"nodes":[{"id":1,"type":"Migrating","typeVersion":1}]})",
        registry);
    REQUIRE(loaded.ok);
    REQUIRE(loaded.graph.findNode(1) != nullptr);
    CHECK(loaded.graph.findNode(1)->typeVersion == 2);
    const sg::Value* migrated = loaded.graph.findNode(1)->setting("migrated");
    REQUIRE(migrated != nullptr);
    CHECK(migrated->asBool() != nullptr);
    CHECK(*migrated->asBool());

    loaded = sg::deserialize("{", registry);
    CHECK_FALSE(loaded.ok);
    for (const std::string& fuzz : {"", "null", "[]", "{\"nodes\": 3}", "{\"schemaVersion\": \"x\"}"}) {
        CHECK_FALSE(sg::deserialize(fuzz, registry).ok);
    }
    CHECK(sg::textureDefaultTypeFromString("Gray") ==
          std::optional<std::string>("grey"));
    CHECK(sg::textureDefaultTypeFromString("normal") ==
          std::optional<std::string>("bump"));
    CHECK_FALSE(sg::textureDefaultTypeFromString("pink").has_value());
    const std::string badDefault =
        R"({"schemaVersion":1,"kind":"graph","name":"X","target":{"id":"lit","subTarget":"lit"},"properties":[{"name":"X","refName":"_X","type":"vector4","default":["a","b","c","d"]}]})";
    CHECK_FALSE(sg::deserialize(badDefault, registry).ok);
    const std::string badDefaultType =
        R"({"schemaVersion":1,"kind":"graph","name":"X","target":{"id":"lit","subTarget":"lit"},"properties":[{"name":"X","refName":"_X","type":"texture2d","defaultType":"pink"}]})";
    CHECK_FALSE(sg::deserialize(badDefaultType, registry).ok);
}

TEST_CASE("unknown fields warn and unknown nodes fall back") {
    sgt::NodeRegistry registry = sgt::makeRegistry();
    const std::string json = R"({
        "schemaVersion": 1,
        "kind": "graph",
        "name": "Unknown",
        "target": {"id": "lit", "subTarget": "lit"},
        "junk": true,
        "nodes": [{"id": 1, "type": "Missing", "typeVersion": 1}]
    })";
    sg::GraphLoadResult loaded = sg::deserialize(json, registry);
    REQUIRE(loaded.ok);
    CHECK(sgt::hasCode(loaded.diagnostics, sg::kErrorUnknownField));
    REQUIRE(loaded.graph.findNode(1) != nullptr);
    CHECK(loaded.graph.findNode(1)->unknown);
    const sg::ValidationResult result = sg::validate(loaded.graph, registry);
    CHECK_FALSE(result.ok);
    CHECK(sgt::hasCode(result.diagnostics, sg::kErrorUnknownNode));
}

TEST_CASE("layout sidecar is independent and rejects malformed files") {
    sg::GraphLayout layout;
    layout.revision = 42;
    layout.pan = {1, 2};
    layout.zoom = 0.5f;
    layout.nodes.push_back({1, {10, 20}, false});
    const std::string text = sg::serializeLayout(layout);
    const sg::LayoutLoadResult loaded = sg::deserializeLayout(text);
    REQUIRE(loaded.ok);
    CHECK(loaded.layout.revision == 42);
    CHECK(loaded.layout.nodes.size() == 1);
    CHECK_FALSE(sg::deserializeLayout("{").ok);
}

TEST_CASE("node registry loads specs and falls back for unknown types") {
    sg::NodeRegistry registry;
    const std::string specs = R"([
        {"type": "Add", "typeVersion": 1, "title": "Add", "category": "Math",
         "precision": "half",
         "slots": [
            {"id": 0, "name": "A", "dir": "in", "type": "dynamicvector", "default": [0, 0, 0, 0]},
            {"id": 1, "name": "B", "dir": "in", "type": "dynamicvector"},
            {"id": 2, "name": "Out", "dir": "out", "type": "dynamicvector"}
         ]}
    ])";
    std::vector<sg::Diagnostic> diagnostics;
    REQUIRE(registry.loadSpecs(specs, diagnostics));
    const sg::NodeDef* add = registry.find("Add");
    REQUIRE(add != nullptr);
    CHECK(static_cast<int>(add->defaultPrecision) == static_cast<int>(sg::Precision::Half));
    CHECK(add->slots.size() == 3);
    CHECK(registry.instantiate("Add", 5).slots.size() == 3);
    CHECK_FALSE(registry.add(*add));
    CHECK(registry.instantiate("Missing", 6).unknown);
}
