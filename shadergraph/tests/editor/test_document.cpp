#include "sg/editor.h"
#include "sg/node_library.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace {

std::filesystem::path testRoot() {
    static const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "hbg_sg_editor_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);
    return root;
}

std::filesystem::path sourceGraph() {
    return std::filesystem::path(SG_ROOT) / "tests/shadergraph/assets/smoke_lit.sgraph.json";
}

std::unique_ptr<sg::editor::Document> openSource(std::string& error) {
    sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    return sg::editor::Document::open(sourceGraph(), registry, error);
}

std::string command(std::uint64_t revision, std::string_view body) {
    return "{\"baseRevision\":" + std::to_string(revision) + ",\"commands\":[" +
           std::string(body) + "]}";
}

} // namespace

TEST_CASE("editor document opens and round-trips graph and layout") {
    std::string error;
    auto document = openSource(error);
    REQUIRE_MESSAGE(document != nullptr, error);
    CHECK(document->revision() == 1);
    CHECK_FALSE(document->dirty());
    CHECK(document->graph().nodes.size() > 0);
    CHECK(document->layout().nodes.size() == document->graph().nodes.size());
    REQUIRE(document->graph().nodes.size() > 1);
    CHECK(document->graph().nodes.front().position.x != document->graph().nodes.back().position.x);
    CHECK(sg::editor::defaultLayoutPath(document->assetPath()).filename().string() ==
          "smoke_lit.sgraph.editor.json");

    const std::filesystem::path saved = testRoot() / "roundtrip.sgraph.json";
    REQUIRE(document->saveAs(saved, error));
    CHECK_FALSE(document->dirty());
    CHECK(std::filesystem::exists(saved));
    CHECK(std::filesystem::exists(sg::editor::defaultLayoutPath(saved)));

    const sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    auto reopened = sg::editor::Document::open(saved, registry, error);
    REQUIRE_MESSAGE(reopened != nullptr, error);
    CHECK(reopened->revision() == 1);
    CHECK(reopened->graph().nodes.size() == document->graph().nodes.size());
    CHECK(reopened->layout().nodes.size() == document->layout().nodes.size());
}

TEST_CASE("editor commands are atomic, revisioned, and undoable") {
    std::string error;
    auto document = openSource(error);
    REQUIRE_MESSAGE(document != nullptr, error);
    const std::size_t initialNodes = document->graph().nodes.size();
    const std::size_t initialUndo = document->canUndo() ? 1 : 0;
    CHECK(initialUndo == 0);

    sg::editor::CommandResult result = document->applyCommands(command(
        1, R"({"op":"addNode","type":"Multiply","id":100,"position":[32,48]})"));
    REQUIRE(result.ok);
    CHECK(result.applied == 1);
    CHECK(document->revision() == 2);
    CHECK(document->dirty());
    CHECK(document->graph().nodes.size() == initialNodes + 1);
    REQUIRE(document->graph().findNode(100) != nullptr);
    CHECK(document->layout().nodes.size() == document->graph().nodes.size());
    CHECK(document->canUndo());

    result = document->applyCommands(command(
        1, R"({"op":"addNode","type":"Multiply","id":101})"));
    CHECK_FALSE(result.ok);
    CHECK(result.code == "SG_STALE_REVISION");
    CHECK(document->graph().findNode(101) == nullptr);
    CHECK(document->revision() == 2);

    result = document->applyCommands(command(
        2, R"({"op":"addNode","type":"Multiply","id":101},{"op":"connect","from":{"node":101,"slot":0},"to":{"node":100,"slot":0}})"));
    CHECK_FALSE(result.ok);
    CHECK(result.code == "SG_INVALID_EDGE");
    CHECK(document->graph().findNode(101) == nullptr);
    CHECK(document->revision() == 2);
    CHECK(document->graph().nodes.size() == initialNodes + 1);

    result = document->applyCommands(command(
        2, R"({"op":"connect","from":{"node":100,"slot":2},"to":{"node":100,"slot":0}})"));
    CHECK_FALSE(result.ok);
    CHECK(result.code == "SG_CYCLE");

    REQUIRE(document->undo(error));
    CHECK(document->graph().findNode(100) == nullptr);
    CHECK(document->revision() == 3);
    CHECK(document->canRedo());
    REQUIRE(document->redo(error));
    CHECK(document->graph().findNode(100) != nullptr);
    CHECK(document->revision() == 4);
}

TEST_CASE("editor connects and edits dynamic slots") {
    std::string error;
    auto document = openSource(error);
    REQUIRE_MESSAGE(document != nullptr, error);
    const std::uint64_t revision = document->revision();
    sg::editor::CommandResult result = document->applyCommands(command(
        revision,
        R"({"op":"addNode","type":"Multiply","id":300},{"op":"addNode","type":"Add","id":301},{"op":"connect","from":{"node":300,"slot":2},"to":{"node":301,"slot":0}})"));
    REQUIRE(result.ok);
    REQUIRE(document->graph().incomingEdge(301, 0) != nullptr);
    CHECK(document->graph().incomingEdge(301, 0)->fromNode == 300);

    result = document->applyCommands(command(
        document->revision(),
        R"({"op":"setSlotValue","node":301,"slot":1,"mode":"literal","value":2.0,"concreteType":"float"})"));
    REQUIRE(result.ok);
    CHECK(static_cast<int>(document->graph().findSlot(301, 1)->state.mode) ==
          static_cast<int>(sg::SlotMode::Literal));

    result = document->applyCommands(command(
        document->revision(),
        R"({"op":"disconnect","from":{"node":300,"slot":2}})"));
    REQUIRE(result.ok);
    CHECK(document->graph().incomingEdge(301, 0) == nullptr);
}

TEST_CASE("editor commands update layout without creating structural history") {
    std::string error;
    auto document = openSource(error);
    REQUIRE_MESSAGE(document != nullptr, error);
    const sg::NodeId nodeId = document->graph().nodes.front().id;
    const sg::Vec2 original = document->graph().findNode(nodeId)->position;

    const std::uint64_t revision = document->revision();
    sg::editor::CommandResult result = document->applyCommands(command(
        revision, R"({"op":"moveNodes","nodes":[)" + std::to_string(nodeId) +
                      R"(],"delta":[10,20]})"));
    REQUIRE(result.ok);
    CHECK(document->revision() == revision + 1);
    CHECK(document->graph().findNode(nodeId)->position.x == doctest::Approx(original.x + 10.0f));
    CHECK(document->graph().findNode(nodeId)->position.y == doctest::Approx(original.y + 20.0f));
    CHECK_FALSE(document->canUndo());
    CHECK(document->dirty());

    const std::uint64_t layoutRevision = document->revision();
    result = document->applyCommands(command(
        layoutRevision, R"({"op":"setLayout","view":{"pan":[5,6],"zoom":0.75},"nodes":[]})"));
    REQUIRE(result.ok);
    CHECK(document->layout().zoom == doctest::Approx(0.75f));
    CHECK(document->layout().pan.x == doctest::Approx(5.0f));
    CHECK(document->layout().pan.y == doctest::Approx(6.0f));
}

TEST_CASE("editor property, keyword, group, and note commands validate") {
    std::string error;
    auto document = openSource(error);
    REQUIRE_MESSAGE(document != nullptr, error);
    const std::uint64_t revision = document->revision();

    sg::editor::CommandResult result = document->applyCommands(command(
        revision, R"({"op":"addProperty","name":"Brightness","type":"color","default":[0.2,0.4,0.6,1.0]})"));
    REQUIRE(result.ok);
    REQUIRE(document->graph().findProperty("_Brightness") != nullptr);
    CHECK(static_cast<int>(document->graph().findProperty("_Brightness")->defaultValue.type) ==
          static_cast<int>(sg::ValueType::Color));

    result = document->applyCommands(command(
        document->revision(),
        R"({"op":"addKeyword","name":"Mode","type":"enum","default":1,"entries":[{"id":1,"displayName":"A","referenceName":"MODE_A"}]})"));
    REQUIRE(result.ok);
    REQUIRE(document->graph().findKeyword("_Mode") != nullptr);
    CHECK(document->graph().findKeyword("_Mode")->entries.size() == 1);

    result = document->applyCommands(command(
        document->revision(),
        R"({"op":"addGroup","title":"Group","position":[1,2],"size":[300,200],"nodes":[]})"));
    REQUIRE(result.ok);
    CHECK(document->graph().groups.size() == 1);

    result = document->applyCommands(command(
        document->revision(),
        R"({"op":"addNote","title":"Note","position":[4,5],"text":"hello","theme":"classic"})"));
    REQUIRE(result.ok);
    CHECK(document->graph().notes.size() == 1);
    CHECK(document->layout().notes.size() == 1);
}

TEST_CASE("editor invalid layout sidecar does not block graph load") {
    std::string error;
    const std::filesystem::path graph = testRoot() / "bad_layout.sgraph.json";
    std::filesystem::copy_file(sourceGraph(), graph);
    {
        std::ofstream layout(sg::editor::defaultLayoutPath(graph), std::ios::binary);
        layout << "{ this is not json";
    }
    const sg::NodeRegistry registry = sg::makeBuiltinNodeRegistry();
    auto document = sg::editor::Document::open(graph, registry, error);
    REQUIRE_MESSAGE(document != nullptr, error);
    CHECK_FALSE(document->layoutError().empty());
    CHECK(document->graph().nodes.size() > 0);
    CHECK_FALSE(document->dirty());
}

TEST_CASE("editor rejects malformed command batches") {
    std::string error;
    auto document = openSource(error);
    REQUIRE_MESSAGE(document != nullptr, error);
    sg::editor::CommandResult result = document->applyCommands("{");
    CHECK_FALSE(result.ok);
    CHECK(result.code == "SG_INVALID_JSON");
    result = document->applyCommands("{\"commands\":{}}");
    CHECK_FALSE(result.ok);
    CHECK(result.code == "SG_INVALID_JSON");
}
