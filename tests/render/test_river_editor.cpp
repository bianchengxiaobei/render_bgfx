#include "render/river/river_editor.h"

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>

using namespace hbg;
using namespace hbg::render;

namespace {

struct EditorCamera {
    Mat4 viewProj;
    Mat4 invViewProj;
};

EditorCamera makeEditorCamera() {
    const Vec3 eye(300.0f, 220.0f, 600.0f);
    const Vec3 target(0.0f, 0.0f, 0.0f);
    const Mat4 view = lookAtRH(eye, target, Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 projection =
        perspectiveRH(55.0f * 3.14159265358979323846f / 180.0f, 1280.0f / 720.0f, 0.1f, 5000.0f, true);
    const Mat4 viewProj = projection * view;
    return {viewProj, inverse(viewProj)};
}

RiverMeshSettings editorMesh() {
    RiverMeshSettings mesh;
    mesh.segments = 320;
    mesh.crossSegments = 12;
    mesh.baseHeight = 0.05f;
    mesh.uvLength = 180.0f;
    return mesh;
}

RiverEditorInput inputAt(Vec2 screen, bool leftDown = false) {
    RiverEditorInput input;
    input.mouseX = screen.x;
    input.mouseY = screen.y;
    input.leftDown = leftDown;
    return input;
}

} // namespace

TEST_CASE("river graph control points preserve endpoints and banks") {
    const std::vector<Vec2> points = {Vec2(0.0f, -500.0f), Vec2(100.0f, 0.0f), Vec2(-50.0f, 500.0f)};
    const RiverGraph graph = RiverGraph::fromControlPoints(points, 40.0f, 64);

    REQUIRE(graph.axis.size() == 65);
    CHECK(graph.width == doctest::Approx(40.0f));
    CHECK(graph.axis.front() == points.front());
    CHECK(graph.axis.back() == points.back());
    REQUIRE(graph.leftBank.points.size() == graph.axis.size());
    REQUIRE(graph.rightBank.points.size() == graph.axis.size());
    CHECK(graph.leftBank.potential == doctest::Approx(0.0f));
    CHECK(graph.rightBank.potential == doctest::Approx(1.0f));
}

TEST_CASE("river editor projects control points and drags them") {
    RiverEditor editor;
    const EditorCamera camera = makeEditorCamera();
    REQUIRE(editor.init(RiverGraph::straight(1200.0f, 64.0f, 320), editorMesh(), 0.05f));
    CHECK(editor.valid());
    CHECK(editor.active());
    REQUIRE(editor.controlPoints().size() == 9);

    const Vec2 before = editor.controlPoints()[4];
    Vec2 screen;
    REQUIRE(editor.projectToScreen(before, camera.viewProj, 1280, 720, screen));
    CHECK(screen.x > 0.0f);
    CHECK(screen.x < 1280.0f);
    CHECK(screen.y > 0.0f);
    CHECK(screen.y < 720.0f);

    editor.update(inputAt(screen, true), camera.viewProj, camera.invViewProj, 1280, 720);
    CHECK(editor.selectedPoint() == 4);

    const Vec2 targetScreen(screen.x + 36.0f, screen.y);
    CHECK(editor.update(inputAt(targetScreen, true), camera.viewProj, camera.invViewProj, 1280, 720));
    CHECK(editor.controlPoints()[4] != before);
    CHECK(editor.consumeDirty());

    Vec2 hit;
    REQUIRE(editor.screenToWorld(targetScreen.x, targetScreen.y, camera.invViewProj, 1280, 720, hit));
    CHECK(std::abs(hit.x - editor.controlPoints()[4].x) < 0.5f);
    CHECK(std::abs(hit.y - editor.controlPoints()[4].y) < 0.5f);
}

TEST_CASE("river editor inserts and removes control points") {
    RiverEditor editor;
    const EditorCamera camera = makeEditorCamera();
    REQUIRE(editor.init(RiverGraph::straight(1200.0f, 64.0f, 320), editorMesh(), 0.05f));
    const std::size_t initialCount = editor.controlPoints().size();

    Vec2 a;
    Vec2 b;
    REQUIRE(editor.projectToScreen(editor.controlPoints()[4], camera.viewProj, 1280, 720, a));
    REQUIRE(editor.projectToScreen(editor.controlPoints()[5], camera.viewProj, 1280, 720, b));
    const Vec2 midpoint((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);

    RiverEditorInput insert = inputAt(midpoint);
    insert.insertPressed = true;
    CHECK(editor.update(insert, camera.viewProj, camera.invViewProj, 1280, 720));
    CHECK(editor.controlPoints().size() == initialCount + 1);
    CHECK(editor.selectedPoint() == 5);
    CHECK(editor.consumeDirty());

    CHECK(editor.removeSelected());
    CHECK(editor.controlPoints().size() == initialCount);
}

TEST_CASE("river editor saves and loads control points in a sidecar") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "hbg_river_editor_roundtrip.json";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);

    RiverEditor editor;
    REQUIRE(editor.init(RiverGraph::straight(1200.0f, 64.0f, 320), editorMesh(), 0.05f));
    REQUIRE(editor.setWidth(73.5f));
    REQUIRE(editor.setPotentials(-0.4f, 0.6f));
    REQUIRE(editor.applyDemoEdit());
    REQUIRE(editor.save(path.string(), nullptr));

    RiverEditor loaded;
    REQUIRE(loaded.init(RiverGraph::straight(1200.0f, 64.0f, 320), editorMesh(), 0.05f));
    std::string error;
    REQUIRE(loaded.load(path.string(), &error));
    CHECK(error.empty());
    CHECK(loaded.consumeDirty());
    REQUIRE(loaded.controlPoints().size() == editor.controlPoints().size());
    for (std::size_t index = 0; index < loaded.controlPoints().size(); ++index) {
        CHECK(loaded.controlPoints()[index].x == doctest::Approx(editor.controlPoints()[index].x));
        CHECK(loaded.controlPoints()[index].y == doctest::Approx(editor.controlPoints()[index].y));
    }
    CHECK(loaded.width() == doctest::Approx(editor.width()));
    CHECK(loaded.leftPotential() == doctest::Approx(editor.leftPotential()));
    CHECK(loaded.rightPotential() == doctest::Approx(editor.rightPotential()));
    CHECK(loaded.graph().axis.size() == editor.graph().axis.size());

    CHECK(loaded.reset());
    CHECK(loaded.width() == doctest::Approx(64.0f));
    CHECK(loaded.leftPotential() == doctest::Approx(0.0f));
    CHECK(loaded.rightPotential() == doctest::Approx(1.0f));

    std::filesystem::remove(path, ignored);
}

TEST_CASE("river editor rejects malformed edit files without changing state") {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "hbg_river_editor_invalid.json";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    {
        std::ofstream file(path);
        file << R"({"controlPoints": [[0.0, 0.0]], "width": 64.0})";
    }

    RiverEditor editor;
    REQUIRE(editor.init(RiverGraph::straight(1200.0f, 64.0f, 320), editorMesh(), 0.05f));
    const std::size_t count = editor.controlPoints().size();
    const float width = editor.width();
    std::string error;
    CHECK_FALSE(editor.load(path.string(), &error));
    CHECK_FALSE(error.empty());
    CHECK(editor.controlPoints().size() == count);
    CHECK(editor.width() == doctest::Approx(width));

    std::filesystem::remove(path, ignored);
}

TEST_CASE("river editor width potential and undo update the graph") {
    RiverEditor editor;
    REQUIRE(editor.init(RiverGraph::straight(1200.0f, 64.0f, 320), editorMesh(), 0.05f));
    const float initialWidth = editor.width();
    const float initialLeft = editor.leftPotential();
    const float initialRight = editor.rightPotential();

    CHECK(editor.setWidth(80.0f));
    CHECK(editor.width() == doctest::Approx(80.0f));
    CHECK(editor.graph().width == doctest::Approx(80.0f));
    CHECK(editor.setPotentials(-0.5f, 0.25f));
    CHECK(editor.graph().leftBank.potential == doctest::Approx(-0.5f));
    CHECK(editor.graph().rightBank.potential == doctest::Approx(0.25f));
    CHECK(editor.consumeDirty());

    CHECK(editor.undo());
    CHECK(editor.width() == doctest::Approx(80.0f));
    CHECK(editor.leftPotential() == doctest::Approx(initialLeft));
    CHECK(editor.rightPotential() == doctest::Approx(initialRight));

    CHECK(editor.reset());
    CHECK(editor.width() == doctest::Approx(initialWidth));
    CHECK(editor.leftPotential() == doctest::Approx(initialLeft));
    CHECK(editor.rightPotential() == doctest::Approx(initialRight));
    CHECK(editor.consumeDirty());
}
