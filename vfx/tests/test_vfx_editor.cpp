#include <doctest/doctest.h>

#include "vfx/editor.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

namespace fs = std::filesystem;

fs::path sourceAsset() {
    return fs::path(VFX_SOURCE_DIR) / "tests" / "assets" / "minimal_unlit.vfx.json";
}

struct TempAsset {
    fs::path path;
    fs::path layoutPath;

    explicit TempAsset(const std::string& name) {
        static std::atomic<std::uint32_t> counter{0};
        path = fs::temp_directory_path() /
               ("hbg_vfx_editor_" + name + "_" + std::to_string(counter.fetch_add(1)) + ".vfx.json");
        layoutPath = vfx::editor::defaultLayoutPath(path);
        std::error_code error;
        fs::remove(path, error);
        fs::remove(layoutPath, error);
        fs::copy_file(sourceAsset(), path, fs::copy_options::overwrite_existing, error);
        REQUIRE_FALSE(error);
    }

    ~TempAsset() {
        std::error_code error;
        fs::remove(path, error);
        fs::remove(layoutPath, error);
    }

    TempAsset(const TempAsset&) = delete;
    TempAsset& operator=(const TempAsset&) = delete;
};

const vfx::editor::NodeLayout* findNode(const vfx::editor::Document& document, vfx::editor::ContextRef context) {
    for (const vfx::editor::NodeLayout& node : document.layout()) {
        if (node.context == context) {
            return &node;
        }
    }
    return nullptr;
}

void writeFile(const fs::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.good());
    file << contents;
    REQUIRE(file.good());
}

} // namespace

TEST_CASE("vfx editor context paths are stable") {
    using vfx::editor::ContextKind;
    using vfx::editor::contextPath;
    using vfx::editor::ContextRef;
    using vfx::editor::parseContextPath;

    CHECK(contextPath(ContextRef{2, ContextKind::Spawner, 0}) == "systems/2/spawner");
    CHECK(contextPath(ContextRef{2, ContextKind::Init, 0}) == "systems/2/init");
    CHECK(contextPath(ContextRef{2, ContextKind::Update, 0}) == "systems/2/update");
    CHECK(contextPath(ContextRef{2, ContextKind::Output, 3}) == "systems/2/outputs/3");

    for (const char* path : {"systems/2/spawner", "systems/2/init", "systems/2/update", "systems/2/outputs/3"}) {
        const auto parsed = parseContextPath(path);
        REQUIRE(parsed.has_value());
        CHECK(contextPath(*parsed) == path);
    }
    CHECK_FALSE(parseContextPath("systems/2").has_value());
    CHECK_FALSE(parseContextPath("systems/x/update").has_value());
    CHECK_FALSE(parseContextPath("systems/2/outputs").has_value());
    CHECK_FALSE(parseContextPath("systems/2/outputs/x").has_value());
}

TEST_CASE("vfx editor maps compile errors to graph focus") {
    using vfx::editor::ContextKind;
    using vfx::editor::ContextRef;
    using vfx::editor::errorFocus;

    auto system = errorFocus("systems[1].capacity");
    REQUIRE(system.has_value());
    REQUIRE(system->context.has_value());
    CHECK(*system->context == (ContextRef{1, ContextKind::Update, 0}));
    CHECK(system->block == vfx::editor::kAppend);

    auto spawner = errorFocus("systems[2].spawner.bursts[0].count");
    REQUIRE(spawner.has_value());
    REQUIRE(spawner->context.has_value());
    CHECK(*spawner->context == (ContextRef{2, ContextKind::Spawner, 0}));

    auto initBlock = errorFocus("systems[2].init.blocks[3].code");
    REQUIRE(initBlock.has_value());
    REQUIRE(initBlock->context.has_value());
    CHECK(*initBlock->context == (ContextRef{2, ContextKind::Init, 0}));
    CHECK(initBlock->block == 3);

    auto updateBlock = errorFocus("systems[0].update.blocks[1].alpha");
    REQUIRE(updateBlock.has_value());
    REQUIRE(updateBlock->context.has_value());
    CHECK(*updateBlock->context == (ContextRef{0, ContextKind::Update, 0}));
    CHECK(updateBlock->block == 1);

    auto output = errorFocus("systems[1].outputs[2].texture");
    REQUIRE(output.has_value());
    REQUIRE(output->context.has_value());
    CHECK(*output->context == (ContextRef{1, ContextKind::Output, 2}));

    auto property = errorFocus("properties[4].keys");
    REQUIRE(property.has_value());
    CHECK_FALSE(property->context.has_value());
    CHECK(property->property == 4);

    CHECK_FALSE(errorFocus("subgraphs[0].inputs[0].type").has_value());
    CHECK_FALSE(errorFocus("$").has_value());
}

TEST_CASE("vfx editor binary path mirrors the graph asset") {
    CHECK(vfx::editor::defaultBinaryPath("spark.vfx.json").filename() == "spark.vfxb");
    CHECK(vfx::editor::defaultBinaryPath("assets/spark.vfx.json").filename() == "spark.vfxb");
    CHECK(vfx::editor::defaultBinaryPath("assets/spark.json").filename() == "spark.vfxb");
}

TEST_CASE("vfx editor loads a graph and creates a default context layout") {
    const TempAsset asset("load");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);
    CHECK(document->graph().name == "MinimalUnlit");
    CHECK(document->assetPath() == asset.path);
    CHECK(document->layoutPath() == asset.layoutPath);
    CHECK_FALSE(document->dirty());
    CHECK_FALSE(document->layoutDirty());
    CHECK_FALSE(document->layoutStale());
    REQUIRE(document->layout().size() == 4);
    CHECK(findNode(*document, {0, vfx::editor::ContextKind::Spawner, 0}) != nullptr);
    CHECK(findNode(*document, {0, vfx::editor::ContextKind::Init, 0}) != nullptr);
    CHECK(findNode(*document, {0, vfx::editor::ContextKind::Update, 0}) != nullptr);
    CHECK(findNode(*document, {0, vfx::editor::ContextKind::Output, 0}) != nullptr);
    CHECK_FALSE(document->serialize().empty());
}

TEST_CASE("vfx editor validates edits and preserves the document on failure") {
    const TempAsset asset("invalid");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);
    const std::uint32_t capacity = document->graph().systems[0].requestedCapacity;

    CHECK_FALSE(document->setSystemCapacity(0, 0));
    CHECK_FALSE(document->lastError().empty());
    CHECK_FALSE(document->errors().empty());
    CHECK(document->graph().systems[0].requestedCapacity == capacity);
    CHECK_FALSE(document->dirty());

    CHECK_FALSE(document->setSpawnerRate(0, -1.0f));
    CHECK_FALSE(document->dirty());
    CHECK_FALSE(document->setOutputBlend(0, 0, vfx::BlendMode::Opaque));
    CHECK_FALSE(document->dirty());
    CHECK_FALSE(document->setBlockEnabled(0, vfx::editor::ContextKind::Update, 99, false));
    CHECK_FALSE(document->dirty());
    CHECK_FALSE(document->setPropertyNumber("Missing", 0, 1.0f));
    CHECK_FALSE(document->dirty());
}

TEST_CASE("vfx editor edits systems, blocks, properties and outputs") {
    const TempAsset asset("edit");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);

    REQUIRE(document->setSystemCapacity(0, 128));
    REQUIRE(document->setSpawnerRate(0, 7.5f));
    REQUIRE(document->setSystemSort(0, vfx::SortMode::Distance, vfx::SortOrder::Descending));
    REQUIRE(document->setSystemBounds(0, {1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}));
    REQUIRE(document->setBlockEnabled(0, vfx::editor::ContextKind::Update, 1, false));
    REQUIRE(document->setOutputBlend(0, 0, vfx::BlendMode::Premultiply));
    REQUIRE(document->setOutputSoftParticles(0, 0, true));
    REQUIRE(document->setOutputSoftDistance(0, 0, 2.5f));
    REQUIRE(document->setOutputMetallic(0, 0, 0.25f));
    REQUIRE(document->setOutputRoughness(0, 0, 0.75f));
    REQUIRE(document->setPropertyNumber("Size", 0, 0.65));
    REQUIRE(document->setPropertyNumber("Tint", 2, 0.2));
    CHECK(document->dirty());

    REQUIRE(document->save(error));
    CHECK_FALSE(document->dirty());

    const auto reloaded = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(reloaded != nullptr, error);
    const vfx::SystemDesc& system = reloaded->graph().systems[0];
    CHECK(system.requestedCapacity == 128);
    CHECK(system.spawner.rate == doctest::Approx(7.5f));
    CHECK(static_cast<int>(system.sort.mode) == static_cast<int>(vfx::SortMode::Distance));
    CHECK(static_cast<int>(system.sort.order) == static_cast<int>(vfx::SortOrder::Descending));
    CHECK(system.bounds.center.y == doctest::Approx(2.0f));
    CHECK(system.bounds.extents.z == doctest::Approx(6.0f));
    REQUIRE(system.updateBlocks.size() == 3);
    CHECK_FALSE(system.updateBlocks[1].enabled);
    CHECK(static_cast<int>(system.outputs[0].blend) == static_cast<int>(vfx::BlendMode::Premultiply));
    CHECK(system.outputs[0].softParticles);
    CHECK(system.outputs[0].softDistance == doctest::Approx(2.5f));
    CHECK(system.outputs[0].metallic == doctest::Approx(0.25f));
    CHECK(system.outputs[0].roughness == doctest::Approx(0.75f));
    REQUIRE(reloaded->graph().properties.size() == 2);
    CHECK(reloaded->graph().properties[1].defaultFloat[0] == doctest::Approx(0.65f));
    CHECK(reloaded->graph().properties[0].defaultFloat[2] == doctest::Approx(0.2f));
}

TEST_CASE("vfx editor round-trips node and view layout") {
    const TempAsset asset("layout");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);
    const vfx::editor::ContextRef update{0, vfx::editor::ContextKind::Update, 0};
    REQUIRE(findNode(*document, update) != nullptr);

    document->setNodePosition(update, {123.0f, 456.0f});
    document->setNodeCollapsed(update, true);
    document->setViewLayout({{10.0f, 20.0f}, 1.5f});
    CHECK(document->layoutDirty());
    REQUIRE(document->saveLayout(error));
    CHECK_FALSE(document->layoutDirty());
    CHECK(fs::exists(asset.layoutPath));

    const auto reloaded = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(reloaded != nullptr, error);
    const vfx::editor::NodeLayout* node = findNode(*reloaded, update);
    REQUIRE(node != nullptr);
    CHECK(node->position.x == doctest::Approx(123.0f));
    CHECK(node->position.y == doctest::Approx(456.0f));
    CHECK(node->collapsed);
    CHECK(reloaded->view().pan.x == doctest::Approx(10.0f));
    CHECK(reloaded->view().pan.y == doctest::Approx(20.0f));
    CHECK(reloaded->view().zoom == doctest::Approx(1.5f));
    CHECK_FALSE(reloaded->layoutDirty());
    CHECK_FALSE(reloaded->layoutStale());

    REQUIRE(reloaded->setSystemCapacity(0, 128));
    CHECK(reloaded->layoutStale());
    REQUIRE(reloaded->saveLayout(error));
    CHECK_FALSE(reloaded->layoutStale());
}

TEST_CASE("vfx editor ignores a corrupt layout without blocking the graph") {
    const TempAsset asset("corrupt_layout");
    writeFile(asset.layoutPath, "{ not valid json");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);
    CHECK_FALSE(document->layoutError().empty());
    CHECK(document->graph().name == "MinimalUnlit");
    REQUIRE(document->layout().size() == 4);
}

TEST_CASE("vfx editor resets layout to deterministic defaults") {
    const TempAsset asset("reset_layout");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);
    const vfx::editor::ContextRef update{0, vfx::editor::ContextKind::Update, 0};
    document->setNodePosition(update, {999.0f, 999.0f});
    document->setViewLayout({{50.0f, 60.0f}, 2.0f});
    document->resetLayout();
    CHECK(document->layoutDirty());
    const vfx::editor::NodeLayout* node = findNode(*document, update);
    REQUIRE(node != nullptr);
    CHECK(node->position.x != doctest::Approx(999.0f));
    CHECK(node->position.y != doctest::Approx(999.0f));
    CHECK(document->view().pan.x == doctest::Approx(0.0f));
    CHECK(document->view().zoom == doctest::Approx(1.0f));
}

TEST_CASE("vfx editor rejects invalid structural edits") {
    const TempAsset asset("invalid_structure");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);

    CHECK_FALSE(document->removeSystem(0));
    CHECK_FALSE(document->removeOutput(0, 0));
    CHECK_FALSE(document->addBlock(0, vfx::editor::ContextKind::Init, "gravity"));
    CHECK_FALSE(document->addBlock(0, vfx::editor::ContextKind::Update, "subgraph"));
    CHECK_FALSE(document->addSystem("Sparks"));
    CHECK_FALSE(document->removeProperty("Tint"));
    CHECK(document->graph().systems.size() == 1);
    CHECK(document->graph().properties.size() == 2);
    CHECK_FALSE(document->dirty());
    CHECK_FALSE(document->lastError().empty());
}

TEST_CASE("vfx editor authors systems, blocks, outputs and properties") {
    const TempAsset asset("author");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);

    REQUIRE(document->addSystem(""));
    REQUIRE(document->graph().systems.size() == 2);
    CHECK(document->graph().systems[1].name == "System 1");
    CHECK(document->layout().size() == 8);
    REQUIRE(document->undo());
    REQUIRE(document->graph().systems.size() == 1);
    CHECK(document->layout().size() == 4);
    CHECK(document->layoutDirty());
    REQUIRE(document->redo());
    REQUIRE(document->graph().systems.size() == 2);

    REQUIRE(document->addBlock(1, vfx::editor::ContextKind::Update, "drag"));
    std::uint32_t dragIndex = static_cast<std::uint32_t>(document->graph().systems[1].updateBlocks.size() - 1);
    CHECK(document->graph().systems[1].updateBlocks[dragIndex].type == "drag");
    REQUIRE(document->setBlockParameterBool(1, vfx::editor::ContextKind::Update, dragIndex, "useSize", true));
    REQUIRE(document->setBlockParameterNumber(1, vfx::editor::ContextKind::Update, dragIndex, "drag", -1, 0.75));

    REQUIRE(document->addBlock(1, vfx::editor::ContextKind::Update, "force"));
    std::uint32_t forceIndex = static_cast<std::uint32_t>(document->graph().systems[1].updateBlocks.size() - 1);
    REQUIRE(document->setBlockParameterString(1, vfx::editor::ContextKind::Update, forceIndex, "mode", "relative"));
    REQUIRE(document->setBlockParameterNumber(1, vfx::editor::ContextKind::Update, forceIndex, "force", 1, 2.0));
    CHECK_FALSE(document->setBlockParameterNumber(1, vfx::editor::ContextKind::Update, forceIndex, "force", 4, 1.0));
    REQUIRE(document->moveBlock(1, vfx::editor::ContextKind::Update, forceIndex, 0));
    CHECK(document->graph().systems[1].updateBlocks[0].type == "force");
    REQUIRE(document->removeBlock(1, vfx::editor::ContextKind::Update, 0));
    CHECK(document->graph().systems[1].updateBlocks.size() == 4);

    REQUIRE(document->addOutput(1));
    REQUIRE(document->graph().systems[1].outputs.size() == 2);
    REQUIRE(document->removeOutput(1, 1));
    REQUIRE(document->graph().systems[1].outputs.size() == 1);

    REQUIRE(document->addProperty("", vfx::PropertyType::Color));
    REQUIRE(document->addProperty("EditorCurve", vfx::PropertyType::Curve));
    REQUIRE(document->addProperty("EditorGradient", vfx::PropertyType::Gradient));
    REQUIRE(document->addProperty("EditorTexture", vfx::PropertyType::Texture));
    REQUIRE(document->graph().properties.size() == 6);
    REQUIRE(document->setPropertyTexture("EditorTexture", "textures/editor.dds"));
    REQUIRE(document->setOutputTexture(1, 0, "EditorTexture"));
    REQUIRE(document->addBlock(0, vfx::editor::ContextKind::Update, "color_over_life"));
    CHECK(document->graph().systems[0].updateBlocks.back().type == "color_over_life");
    REQUIRE(document->removeBlock(0, vfx::editor::ContextKind::Update,
                                  static_cast<std::uint32_t>(document->graph().systems[0].updateBlocks.size() - 1)));
    REQUIRE(document->addBlock(0, vfx::editor::ContextKind::Update, "alpha_over_life"));
    CHECK(document->graph().systems[0].updateBlocks.back().type == "alpha_over_life");
    REQUIRE(document->removeBlock(0, vfx::editor::ContextKind::Update,
                                  static_cast<std::uint32_t>(document->graph().systems[0].updateBlocks.size() - 1)));
    REQUIRE(document->addPropertyCurveKey("EditorCurve"));
    REQUIRE(document->setPropertyCurveKey("EditorCurve", 2, 0.35f, 0.65f));
    REQUIRE(document->addPropertyGradientKey("EditorGradient"));
    REQUIRE(document->setPropertyGradientKey("EditorGradient", 2, 0.7f, {0.1f, 0.2f, 0.3f, 0.4f}));
    REQUIRE(document->removeProperty("Property1"));
    CHECK(document->graph().properties.size() == 5);

    const auto curveBefore = std::find_if(document->graph().properties.begin(), document->graph().properties.end(),
                                          [](const vfx::Property& property) { return property.name == "EditorCurve"; });
    REQUIRE(curveBefore != document->graph().properties.end());
    CHECK(curveBefore->curve.size() == 3);
    REQUIRE(document->save(error));
    const auto reloaded = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(reloaded != nullptr, error);
    REQUIRE(reloaded->graph().systems.size() == 2);
    const vfx::SystemDesc& system = reloaded->graph().systems[1];
    REQUIRE(system.updateBlocks.size() == 4);
    CHECK(system.updateBlocks.back().type == "drag");
    const vfx::Value* drag = system.updateBlocks.back().parameter("drag");
    REQUIRE(drag != nullptr);
    CHECK(drag->number == doctest::Approx(0.75));
    const vfx::Value* useSize = system.updateBlocks.back().parameter("useSize");
    REQUIRE(useSize != nullptr);
    CHECK(useSize->boolean);

    const auto curve = std::find_if(reloaded->graph().properties.begin(), reloaded->graph().properties.end(),
                                    [](const vfx::Property& property) { return property.name == "EditorCurve"; });
    REQUIRE(curve != reloaded->graph().properties.end());
    REQUIRE(curve->curve.size() == 3);
    const auto curveKey = std::find_if(curve->curve.begin(), curve->curve.end(),
                                       [](const vfx::CurveKey& key) { return key.time == doctest::Approx(0.35f); });
    REQUIRE(curveKey != curve->curve.end());
    CHECK(curveKey->value == doctest::Approx(0.65f));
    const auto gradient = std::find_if(reloaded->graph().properties.begin(), reloaded->graph().properties.end(),
                                       [](const vfx::Property& property) { return property.name == "EditorGradient"; });
    REQUIRE(gradient != reloaded->graph().properties.end());
    REQUIRE(gradient->gradient.size() == 3);
    const auto gradientKey =
        std::find_if(gradient->gradient.begin(), gradient->gradient.end(),
                     [](const vfx::GradientKey& key) { return key.time == doctest::Approx(0.7f); });
    REQUIRE(gradientKey != gradient->gradient.end());
    CHECK(gradientKey->color.z == doctest::Approx(0.3f));
    const auto texture = std::find_if(reloaded->graph().properties.begin(), reloaded->graph().properties.end(),
                                      [](const vfx::Property& property) { return property.name == "EditorTexture"; });
    REQUIRE(texture != reloaded->graph().properties.end());
    CHECK(texture->defaultTexture == "textures/editor.dds");
    CHECK(reloaded->graph().systems[1].outputs[0].texture == "EditorTexture");
}

TEST_CASE("vfx editor undo and redo restore graph edits") {
    const TempAsset asset("undo");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);
    REQUIRE_FALSE(document->canUndo());
    REQUIRE_FALSE(document->canRedo());

    REQUIRE(document->setSystemCapacity(0, 128));
    REQUIRE(document->renameSystem(0, "UndoTest"));
    REQUIRE(document->addBlock(0, vfx::editor::ContextKind::Update, "drag"));
    REQUIRE(document->canUndo());
    CHECK(document->graph().systems[0].updateBlocks.size() == 4);

    REQUIRE(document->undo());
    CHECK(document->graph().systems[0].name == "UndoTest");
    CHECK(document->graph().systems[0].updateBlocks.size() == 3);
    REQUIRE(document->undo());
    CHECK(document->graph().systems[0].name == "Sparks");
    CHECK(document->graph().systems[0].requestedCapacity == 128);
    REQUIRE(document->undo());
    CHECK(document->graph().systems[0].requestedCapacity == 64);
    CHECK_FALSE(document->dirty());
    CHECK_FALSE(document->canUndo());
    REQUIRE(document->canRedo());

    REQUIRE(document->redo());
    CHECK(document->graph().systems[0].requestedCapacity == 128);
    REQUIRE(document->redo());
    CHECK(document->graph().systems[0].name == "UndoTest");
    REQUIRE(document->redo());
    CHECK(document->graph().systems[0].updateBlocks.size() == 4);
    CHECK_FALSE(document->canRedo());
}

TEST_CASE("vfx editor block catalog defaults are valid") {
    const TempAsset asset("catalog");
    std::string error;
    const auto document = vfx::editor::Document::load(asset.path, error);
    REQUIRE_MESSAGE(document != nullptr, error);

    for (const std::string& type : vfx::editor::blockTypes(vfx::editor::ContextKind::Init)) {
        if (type == "subgraph") {
            continue;
        }
        INFO("init block: " << type);
        REQUIRE(document->addBlock(0, vfx::editor::ContextKind::Init, type));
        REQUIRE(document->removeBlock(0, vfx::editor::ContextKind::Init,
                                      static_cast<std::uint32_t>(document->graph().systems[0].initBlocks.size() - 1)));
    }
    for (const std::string& type : vfx::editor::blockTypes(vfx::editor::ContextKind::Update)) {
        if (type == "subgraph" || type == "trigger_event" || type == "color_over_life" || type == "alpha_over_life" ||
            type == "size_over_life") {
            continue;
        }
        INFO("update block: " << type);
        REQUIRE(document->addBlock(0, vfx::editor::ContextKind::Update, type));
        REQUIRE(
            document->removeBlock(0, vfx::editor::ContextKind::Update,
                                  static_cast<std::uint32_t>(document->graph().systems[0].updateBlocks.size() - 1)));
    }
}
