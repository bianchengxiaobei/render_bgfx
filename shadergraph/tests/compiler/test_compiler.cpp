#include "sg/compile.h"
#include "sg/node_library.h"
#include "sg/serialize.h"
#include "sg/validate.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>

#include "compile/compile_internal.h"

TEST_CASE("builtin node library exposes the SG-2 core set") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    CHECK(registry.size() >= 60);
    CHECK(registry.find("Multiply") != nullptr);
    CHECK(registry.find("Sample Texture 2D") != nullptr);
    CHECK(registry.find("BlockBaseColor") != nullptr);
    for (const auto& [type, definition] : registry.definitions()) {
        CHECK(!type.empty());
        CHECK(!definition.title.empty());
        std::set<sg::SlotId> ids;
        for (const sg::SlotSpec& slot : definition.slots) {
            CHECK(ids.insert(slot.id).second);
        }
    }
}

TEST_CASE("builtin nodes instantiate with stable slots") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::NodeInstance multiply = registry.instantiate("Multiply", 7);
    REQUIRE(multiply.slots.size() == 3);
    CHECK(multiply.slots[0].isInput());
    CHECK(multiply.slots[2].isOutput());
    CHECK(multiply.typeVersion == 1);
}


TEST_CASE("function registry deduplicates by name and detects conflicts") {
    sg::detail::FunctionRegistry registry;
    CHECK(registry.provide("SG_Add", "float SG_Add(float a, float b) { return a + b; }\n"));
    CHECK(registry.provide("SG_Add", "float SG_Add(float a, float b) { return a + b; }\n"));
    std::string error;
    CHECK_FALSE(registry.provide("SG_Add", "float SG_Add(float a, float b) { return a - b; }\n",
                                 &error));
    CHECK(error.find("conflicting") != std::string::npos);
    CHECK(registry.names.size() == 1);
    CHECK(registry.render().find("return a + b") != std::string::npos);
}

TEST_CASE("property collection sorts by reference name and assigns texture stages") {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Property texture;
    texture.name = "Main Tex";
    texture.refName = "_MainTex";
    texture.type = sg::ValueType::Texture2D;
    texture.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "tex.png");
    graph.properties.push_back(texture);
    sg::Property tint;
    tint.name = "Tint";
    tint.refName = "_Tint";
    tint.type = sg::ValueType::Color;
    tint.defaultValue = sg::Value::makeColor({1.0f, 1.0f, 1.0f, 1.0f});
    graph.properties.push_back(tint);

    sg::NodeInstance uv = registry.instantiate("UV", 1);
    sg::NodeInstance sample = registry.instantiate("Sample Texture 2D", 2);
    sample.settings["texture"] = sg::Value::makeString(sg::ValueType::String, "_MainTex");
    graph.nodes.push_back(uv);
    graph.nodes.push_back(sample);
    const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
    REQUIRE(target != nullptr);
    sg::detail::ActiveGraph active = sg::detail::collectActiveNodes(graph, registry, *target);
    sg::detail::PropertySet properties = sg::detail::collectProperties(graph, active, *target);
    CHECK(properties.ok);
    REQUIRE(properties.properties.size() == 2);
    CHECK(properties.properties[0].refName == "_MainTex");
    CHECK(properties.properties[0].samplerStage == 0);
    CHECK(properties.properties[1].refName == "_Tint");
    CHECK(properties.properties[1].declaration.find("uniform vec4") != std::string::npos);
}

TEST_CASE("keyword collection computes deterministic boolean and enum variants") {
    sg::Graph graph;
    graph.target.settings["alphaClip"] = sg::Value::makeBool(true);
    sg::Keyword boolean;
    boolean.name = "Feature";
    boolean.refName = "_FEATURE";
    boolean.type = sg::KeywordType::Boolean;
    boolean.defaultValue = 1;
    graph.keywords.push_back(boolean);
    sg::Keyword mode;
    mode.name = "Mode";
    mode.refName = "_MODE";
    mode.type = sg::KeywordType::Enum;
    mode.defaultValue = 2;
    mode.entries.push_back({1, "A", "MODE_A"});
    mode.entries.push_back({2, "B", "MODE_B"});
    graph.keywords.push_back(mode);

    const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
    REQUIRE(target != nullptr);
    sg::detail::KeywordSet used = sg::detail::collectKeywords(graph, *target, sg::VariantPolicy::Used, 64);
    CHECK(used.ok);
    REQUIRE(used.variants.size() == 1);
    CHECK((used.variants[0].key & 1ull) != 0);
    sg::detail::KeywordSet all = sg::detail::collectKeywords(graph, *target, sg::VariantPolicy::All, 64);
    CHECK(all.ok);
    CHECK(all.variants.size() == 4);
    std::set<std::uint64_t> keys;
    for (const sg::detail::VariantInfo& variant : all.variants) {
        CHECK(keys.insert(variant.key).second);
    }
}

TEST_CASE("keyword collection enforces the variant cap") {
    sg::Graph graph;
    for (int index = 0; index < 7; ++index) {
        sg::Keyword keyword;
        keyword.name = "K" + std::to_string(index);
        keyword.refName = "_K" + std::to_string(index);
        keyword.type = sg::KeywordType::Boolean;
        graph.keywords.push_back(keyword);
    }
    const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
    REQUIRE(target != nullptr);
    sg::detail::KeywordSet keywords =
        sg::detail::collectKeywords(graph, *target, sg::VariantPolicy::All, 64);
    CHECK_FALSE(keywords.ok);
    CHECK(std::any_of(keywords.diagnostics.begin(), keywords.diagnostics.end(),
                      [](const sg::Diagnostic& diagnostic) {
                          return diagnostic.code == sg::kErrorVariantLimit;
                      }));
}

TEST_CASE("surface emission produces node body and property declarations") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/smoke_lit.sgraph.json", registry);
    REQUIRE(loaded.ok);
    std::vector<sg::Diagnostic> prep;
    REQUIRE(sg::detail::preparePropertyNodes(loaded.graph, prep));
    sg::ValidationResult validation = sg::validate(loaded.graph, registry, true);
    REQUIRE(validation.ok);
    const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
    REQUIRE(target != nullptr);
    sg::detail::ActiveGraph active =
        sg::detail::collectActiveNodes(loaded.graph, registry, *target);
    sg::detail::PropertySet properties =
        sg::detail::collectProperties(loaded.graph, active, *target);
    REQUIRE(properties.ok);
    sg::detail::KeywordSet keywords =
        sg::detail::collectKeywords(loaded.graph, *target, sg::VariantPolicy::Used, 64);
    REQUIRE(keywords.ok);
    REQUIRE(keywords.variants.size() == 1);
    sg::detail::EmittedGraph emitted = sg::detail::emitSurface(
        loaded.graph, registry, active, *target, properties, keywords, keywords.variants[0]);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("GraphSurface hbgEvaluateGraphSurface") != std::string::npos);
    CHECK(emitted.source.find("SAMPLER2D(s_sg__MainTex, 0);") != std::string::npos);
    CHECK(emitted.source.find("n4_s2 = n3_s0 * n2_s0;") != std::string::npos);
    CHECK(emitted.source.find("surface.baseColor") != std::string::npos);
}

TEST_CASE("SGB packing is deterministic and contains required sections") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/smoke_lit.sgraph.json", registry);
    REQUIRE(loaded.ok);
    std::vector<sg::Diagnostic> prep;
    REQUIRE(sg::detail::preparePropertyNodes(loaded.graph, prep));
    const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
    REQUIRE(target != nullptr);
    sg::detail::ActiveGraph active =
        sg::detail::collectActiveNodes(loaded.graph, registry, *target);
    sg::detail::PropertySet properties =
        sg::detail::collectProperties(loaded.graph, active, *target);
    REQUIRE(properties.ok);
    sg::detail::KeywordSet keywords =
        sg::detail::collectKeywords(loaded.graph, *target, sg::VariantPolicy::Used, 64);
    REQUIRE(keywords.ok);

    sg::detail::PackedBlob blob;
    blob.pass = "forward";
    blob.passId = 0;
    blob.variantKey = keywords.variants[0].key;
    blob.stage = 1;
    blob.backend = sg::Backend::Dxbc;
    blob.data = {1, 2, 3, 4};

    sg::detail::PackInput input;
    input.graph = &loaded.graph;
    input.target = target;
    input.properties = properties.properties;
    input.keywords = keywords.keywords;
    input.variants = keywords.variants;
    input.blobs.push_back(blob);
    input.contentHash = sg::contentHash(loaded.graph);
    input.sourceHash = input.contentHash;
    std::string error;
    std::vector<std::uint8_t> first;
    std::vector<std::uint8_t> second;
    REQUIRE(sg::detail::packSgb(input, first, error));
    REQUIRE(sg::detail::packSgb(input, second, error));
    CHECK(first == second);
    REQUIRE(first.size() > 32);
    CHECK(std::string(reinterpret_cast<const char*>(first.data()), 4) == "SGB1");
    const std::string text(reinterpret_cast<const char*>(first.data()), first.size());
    CHECK(text.find("E:\\\\") == std::string::npos);
}


TEST_CASE("property collection enforces texture and uniform budgets") {
    sg::Graph graph;
    graph.target.id = "lit";
    for (int index = 0; index < 6; ++index) {
        sg::Property property;
        property.name = "Tex" + std::to_string(index);
        property.refName = "_Tex" + std::to_string(index);
        property.type = sg::ValueType::Texture2D;
        property.defaultValue = sg::Value::makeString(sg::ValueType::Texture2D, "tex.png");
        graph.properties.push_back(property);
    }
    const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
    REQUIRE(target != nullptr);
    sg::detail::ActiveGraph active;
    active.ok = true;
    sg::detail::PropertySet properties = sg::detail::collectProperties(graph, active, *target);
    CHECK_FALSE(properties.ok);
    CHECK(std::any_of(properties.diagnostics.begin(), properties.diagnostics.end(),
                      [](const sg::Diagnostic& diagnostic) {
                          return diagnostic.code == sg::kErrorBudgetExceeded;
                      }));
}

TEST_CASE("pass plan stage map is unique and covers the SG-0 contract") {
    const sg::detail::TargetSpec* target = sg::detail::findTarget("lit");
    REQUIRE(target != nullptr);
    std::string error;
    CHECK(sg::detail::validatePassPlan(*target, error));
    std::set<int> stages;
    for (const sg::detail::StageBinding& binding : sg::detail::stageMap()) {
        CHECK(stages.insert(binding.stage).second);
        CHECK(binding.stage >= 0);
        CHECK(binding.stage <= 15);
    }
    CHECK(stages.contains(5));
    CHECK(stages.contains(8));
    CHECK(stages.contains(15));
}

TEST_CASE("compiler fixture directory contains at least twenty graphs") {
    const std::filesystem::path root(SG_ROOT);
    int count = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(root / "tests/shadergraph/assets")) {
        if (entry.is_regular_file() && entry.path().filename().string().ends_with(".sgraph.json")) {
            ++count;
        }
    }
    CHECK(count >= 20);
}


TEST_CASE("node spec JSON can describe emit metadata") {
    sg::NodeRegistry registry;
    std::vector<sg::Diagnostic> diagnostics;
    const char* spec = R"([
      {
        "type": "SpecEmit",
        "title": "Spec Emit",
        "category": "Test",
        "slots": [
          {"id": 0, "name": "In", "dir": "in", "type": "float"},
          {"id": 1, "name": "Out", "dir": "out", "type": "float"}
        ],
        "emit": {"body": "$1 = $0;"},
        "requirements": ["uv0"]
      }
    ])";
    CHECK(registry.loadSpecs(spec, diagnostics));
    const sg::NodeDef* definition = registry.find("SpecEmit");
    REQUIRE(definition != nullptr);
    CHECK(definition->emitBody == "$1 = $0;");
    CHECK(definition->requirements.size() == 1);
}
