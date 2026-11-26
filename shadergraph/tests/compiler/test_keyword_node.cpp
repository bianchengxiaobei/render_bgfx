#include "compile/compile_internal.h"
#include "sg/concretize.h"
#include "sg/node_library.h"
#include "sg/serialize.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace {

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

sg::Graph graphWithKeyword(const sg::NodeRegistry& registry, sg::Keyword keyword,
                           const char* setting = "keyword", const char* ref = "_MODE") {
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    REQUIRE(graph.addKeyword(std::move(keyword)));
    sg::NodeInstance node = registry.instantiate("Keyword", 1);
    node.settings[setting] = sg::Value::makeString(sg::ValueType::String, ref);
    REQUIRE(graph.addNode(std::move(node)));
    return graph;
}

} // namespace

TEST_CASE("Keyword node exposes Unity Utility/Keyword metadata") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    const sg::NodeDef* definition = registry.find("Keyword");
    REQUIRE(definition != nullptr);
    CHECK(definition->category == "Utility");
    CHECK(definition->title == "Keyword");
    CHECK(definition->allowInMainGraph);
    CHECK(definition->allowInSubGraph);
    CHECK(definition->settings == std::vector<std::string>{"keyword"});
    REQUIRE_FALSE(definition->slots.empty());
    CHECK(definition->slots.front().id == 0);
    CHECK(static_cast<int>(definition->slots.front().direction) ==
          static_cast<int>(sg::SlotDirection::Output));
    CHECK(static_cast<int>(definition->slots.front().type) ==
          static_cast<int>(sg::ValueType::DynamicVector));
    CHECK(static_cast<bool>(definition->updateSlots));
}

TEST_CASE("Keyword node emits boolean On Off branches") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Keyword keyword;
    keyword.name = "Mode";
    keyword.refName = "_MODE";
    keyword.type = sg::KeywordType::Boolean;
    keyword.defaultValue = 0;
    sg::Graph graph = graphWithKeyword(registry, std::move(keyword), "keywordRef");
    sg::NodeInstance on = registry.instantiate("Float", 2);
    on.settings["value"] = sg::Value::makeFloat(0.8f);
    sg::NodeInstance off = registry.instantiate("Float", 3);
    off.settings["value"] = sg::Value::makeFloat(0.2f);
    REQUIRE(graph.addNode(std::move(on)));
    REQUIRE(graph.addNode(std::move(off)));
    REQUIRE(graph.addEdge({2, 0, 1, 1}));
    REQUIRE(graph.addEdge({3, 0, 1, 2}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("#ifdef _MODE") != std::string::npos);
    CHECK(emitted.source.find("n1_s0 = n2_s0;") != std::string::npos);
    CHECK(emitted.source.find("n1_s0 = n3_s0;") != std::string::npos);
    const sg::NodeInstance* node = graph.findNode(1);
    REQUIRE(node != nullptr);
    REQUIRE(node->slots.size() == 3);
    CHECK(node->findSlot(1)->spec.name == "On");
    CHECK(node->findSlot(2)->spec.name == "Off");
    CHECK(node->settings.find("keywordRef") == node->settings.end());
    REQUIRE(node->settings.find("keyword") != node->settings.end());
    REQUIRE(node->settings.at("keyword").asString() != nullptr);
    CHECK(*node->settings.at("keyword").asString() == "_MODE");
}

TEST_CASE("Keyword node expands enum entries into conditional values") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Keyword keyword;
    keyword.name = "Mode";
    keyword.refName = "_MODE";
    keyword.type = sg::KeywordType::Enum;
    keyword.defaultValue = 2;
    keyword.entries = {{1, "A", "MODE_A"}, {2, "B", "MODE_B"}};
    sg::Graph graph = graphWithKeyword(registry, std::move(keyword));
    sg::NodeInstance a = registry.instantiate("Float", 2);
    a.settings["value"] = sg::Value::makeFloat(0.25f);
    sg::NodeInstance b = registry.instantiate("Float", 3);
    b.settings["value"] = sg::Value::makeFloat(0.75f);
    REQUIRE(graph.addNode(std::move(a)));
    REQUIRE(graph.addNode(std::move(b)));
    REQUIRE(graph.addEdge({2, 0, 1, 1}));
    REQUIRE(graph.addEdge({3, 0, 1, 2}));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("#if defined(_MODE_MODE_A)") != std::string::npos);
    CHECK(emitted.source.find("#elif defined(_MODE_MODE_B)") != std::string::npos);
    CHECK(emitted.source.find("n1_s0 = n2_s0;") != std::string::npos);
    CHECK(emitted.source.find("n1_s0 = n3_s0;") != std::string::npos);
    const sg::NodeInstance* node = graph.findNode(1);
    REQUIRE(node != nullptr);
    REQUIRE(node->slots.size() == 3);
    CHECK(node->findSlot(1)->spec.displayName == "A");
    CHECK(node->findSlot(2)->spec.displayName == "B");
}

TEST_CASE("Keyword node reports a missing graph keyword") {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::Graph graph;
    graph.kind = sg::GraphKind::Graph;
    graph.target.id = "lit";
    sg::NodeInstance node = registry.instantiate("Keyword", 1);
    node.settings["keyword"] = sg::Value::makeString(sg::ValueType::String, "_MISSING");
    REQUIRE(graph.addNode(std::move(node)));

    const sg::detail::EmittedGraph emitted = emitGraph(registry, graph);
    CHECK_FALSE(emitted.ok);
    CHECK(std::any_of(emitted.diagnostics.begin(), emitted.diagnostics.end(),
                      [](const sg::Diagnostic& diagnostic) {
                          return diagnostic.code == sg::kErrorMissingInput &&
                                 diagnostic.message.find("Keyword node") != std::string::npos;
                      }));
}

TEST_CASE("Keyword node emits the checked-in keyword_bool graph") {
    const std::filesystem::path root(SG_ROOT);
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    sg::GraphLoadResult loaded =
        sg::loadGraph(root / "tests/shadergraph/assets/keyword_bool.sgraph.json", registry);
    REQUIRE(loaded.ok);
    const sg::detail::EmittedGraph emitted = emitGraph(registry, loaded.graph);
    REQUIRE(emitted.ok);
    CHECK(emitted.source.find("#ifdef _MODE") != std::string::npos);
    CHECK(emitted.source.find("n1_s0 = n3_s0;") != std::string::npos);
    CHECK(emitted.source.find("n1_s0 = n2_s0;") != std::string::npos);
}
