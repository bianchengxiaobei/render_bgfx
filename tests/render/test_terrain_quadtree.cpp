#include "render/terrain/terrain_quadtree.h"

#include <doctest/doctest.h>

#include <array>
#include <vector>

using namespace hbg::render;
using hbg::Vec3;
using hbg::Vec4;

TEST_CASE("terrain quadtree split distance follows Proland scaling") {
    TerrainQuadTreeSettings settings;
    const float base = TerrainQuadTree::computeSplitDistance(settings);
    CHECK(base == doctest::Approx(3.399f).epsilon(0.002f));

    settings.viewportHeight = 2160.0f;
    CHECK(TerrainQuadTree::computeSplitDistance(settings) == doctest::Approx(base * 2.0f).epsilon(0.001f));

    settings.fovYRadians = 0.5f;
    CHECK(TerrainQuadTree::computeSplitDistance(settings) > base);

    settings.splitFactor = 0.0f;
    CHECK(TerrainQuadTree::computeSplitDistance(settings) == doctest::Approx(1.1f));
}

TEST_CASE("terrain quadtree splits at the camera and merges when it moves away") {
    TerrainQuadTree tree;
    TerrainQuadTreeSettings settings;
    settings.maxLevel = 2;
    tree.init(0.0f, 0.0f, 1000.0f, -10.0f, 20.0f, settings);

    tree.update(Vec3(500.0f, 0.0f, 500.0f), 0.0f);
    CHECK(tree.stats().leafCount == 16);
    CHECK(tree.stats().nodeCount == 21);
    CHECK(tree.stats().maxDepth == 2);
    CHECK(tree.root().children[0]->zmin == doctest::Approx(-10.0f));
    CHECK(tree.root().children[3]->zmax == doctest::Approx(20.0f));

    std::vector<const TerrainQuadNode*> leaves;
    tree.collectLeaves(leaves);
    CHECK(leaves.size() == tree.stats().leafCount);
    for (const TerrainQuadNode* leaf : leaves) {
        CHECK(leaf->isLeaf());
        CHECK(leaf->level == 2);
        CHECK(leaf->size == doctest::Approx(250.0f));
    }

    tree.update(Vec3(5000.0f, 0.0f, 5000.0f), 0.0f);
    CHECK(tree.stats().leafCount == 1);
    CHECK(tree.stats().nodeCount == 1);
    CHECK(tree.root().isLeaf());
}

TEST_CASE("terrain quadtree respects max level and camera height") {
    TerrainQuadTree tree;
    TerrainQuadTreeSettings settings;
    settings.maxLevel = 0;
    settings.splitFactor = 100.0f;
    tree.init(0.0f, 0.0f, 1000.0f, 0.0f, 100.0f, settings);
    tree.update(Vec3(500.0f, 0.0f, 500.0f), 0.0f);
    CHECK(tree.stats().leafCount == 1);

    TerrainQuadTreeSettings deep;
    deep.maxLevel = 4;
    deep.splitFactor = 100.0f;
    tree.init(0.0f, 0.0f, 1000.0f, 0.0f, 100.0f, deep);
    tree.update(Vec3(500.0f, 0.0f, 500.0f), 0.0f);
    CHECK(tree.stats().maxDepth == 4);
    CHECK(tree.stats().leafCount == 256);

    const TerrainQuadTreeSettings& active = tree.settings();
    CHECK(active.maxLevel == 4);
    CHECK(active.splitFactor == doctest::Approx(100.0f));
}

TEST_CASE("terrain quadtree culls outside frustum and follows Proland child order") {
    TerrainQuadTree tree;
    TerrainQuadTreeSettings settings;
    settings.maxLevel = 2;
    settings.splitFactor = 100.0f;
    tree.init(0.0f, 0.0f, 1000.0f, 0.0f, 100.0f, settings);
    tree.setNodeHeightRange(1, 0, 0, -20.0f, 40.0f);

    std::array<Vec4, 6> planes = {
        Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f),
        Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f),
    };
    planes[0] = Vec4(-1.0f, 0.0f, 0.0f, 400.0f);

    tree.update(Vec3(200.0f, 0.0f, 200.0f), 0.0f, &planes);
    CHECK(tree.stats().leafCount == 10);
    CHECK(tree.stats().visibleLeafCount == 8);
    CHECK(tree.stats().nodeCount == 13);
    CHECK(tree.root().children[0]->zmin == doctest::Approx(-20.0f));
    CHECK(tree.root().children[0]->zmax == doctest::Approx(40.0f));

    std::vector<const TerrainQuadNode*> leaves;
    tree.collectLeaves(leaves);
    REQUIRE(leaves.size() == 10);
    CHECK(leaves[0]->level == 2);
    CHECK(leaves[0]->ox == doctest::Approx(0.0f));
    CHECK(leaves[0]->oy == doctest::Approx(0.0f));
    CHECK(leaves[1]->ox == doctest::Approx(250.0f));
    CHECK(leaves[1]->oy == doctest::Approx(0.0f));
    CHECK(leaves[2]->ox == doctest::Approx(0.0f));
    CHECK(leaves[2]->oy == doctest::Approx(250.0f));
    CHECK(leaves[3]->ox == doctest::Approx(250.0f));
    CHECK(leaves[3]->oy == doctest::Approx(250.0f));
    CHECK_FALSE(leaves[4]->visible);
    CHECK(leaves[4]->level == 1);
}

TEST_CASE("terrain quadtree does not split a fully invisible root by default") {
    TerrainQuadTree tree;
    TerrainQuadTreeSettings settings;
    settings.maxLevel = 4;
    settings.splitFactor = 100.0f;
    tree.init(0.0f, 0.0f, 1000.0f, 0.0f, 100.0f, settings);

    std::array<Vec4, 6> planes = {
        Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f),
        Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f), Vec4(0.0f, 0.0f, 0.0f, 1.0f),
    };
    planes[0] = Vec4(0.0f, 0.0f, 0.0f, -1.0f);

    tree.update(Vec3(500.0f, 0.0f, 500.0f), 0.0f, &planes);
    CHECK(tree.root().visible == false);
    CHECK(tree.stats().leafCount == 1);
    CHECK(tree.stats().visibleLeafCount == 0);
    CHECK(tree.stats().nodeCount == 1);
}

TEST_CASE("terrain quadtree height range provider supplies node bounds lazily") {
    TerrainQuadTree tree;
    TerrainQuadTreeSettings settings;
    settings.maxLevel = 1;
    settings.splitFactor = 100.0f;
    tree.setHeightRangeProvider([](int level, int tx, int ty, float& zmin, float& zmax) {
        if (level == 0) {
            zmin = -7.0f;
            zmax = 11.0f;
            return true;
        }
        if (level == 1 && tx == 0 && ty == 0) {
            zmin = -3.0f;
            zmax = 4.0f;
            return true;
        }
        return false;
    });
    tree.init(0.0f, 0.0f, 1000.0f, 0.0f, 100.0f, settings);
    tree.update(Vec3(500.0f, 0.0f, 500.0f), 0.0f);

    CHECK(tree.root().zmin == doctest::Approx(-7.0f));
    CHECK(tree.root().zmax == doctest::Approx(11.0f));
    CHECK(tree.root().children[0]->zmin == doctest::Approx(-3.0f));
    CHECK(tree.root().children[0]->zmax == doctest::Approx(4.0f));
    CHECK(tree.root().children[1]->zmin == doctest::Approx(-7.0f));
    CHECK(tree.root().children[1]->zmax == doctest::Approx(11.0f));
}

TEST_CASE("terrain quadtree horizon culling uses front-to-back occluders") {
    TerrainQuadTree tree;
    TerrainQuadTreeSettings settings;
    settings.maxLevel = 2;
    settings.splitFactor = 100.0f;
    tree.init(0.0f, 0.0f, 1000.0f, 0.0f, 300.0f, settings);
    tree.setNodeHeightRange(2, 0, 2, 300.0f, 300.0f);
    tree.setNodeHeightRange(2, 1, 2, 300.0f, 300.0f);
    tree.setNodeHeightRange(2, 0, 3, 100.0f, 100.0f);
    tree.setNodeHeightRange(2, 1, 3, 100.0f, 100.0f);

    const Vec3 forward(0.0f, 0.0f, 1.0f);
    tree.update(Vec3(250.0f, 10.0f, 400.0f), 10.0f, nullptr, &forward);
    CHECK(tree.stats().leafCount == 16);
    CHECK(tree.stats().horizonCulledLeafCount >= 2);
    CHECK(tree.stats().visibleLeafCount < tree.stats().leafCount);

    std::vector<const TerrainQuadNode*> leaves;
    tree.collectLeaves(leaves);
    for (const TerrainQuadNode* leaf : leaves) {
        if (leaf->level == 2 && leaf->oy == doctest::Approx(500.0f)) {
            CHECK(leaf->visible);
            CHECK_FALSE(leaf->horizonCulled);
        }
        if (leaf->level == 2 && leaf->oy == doctest::Approx(750.0f) &&
            (leaf->ox == doctest::Approx(0.0f) || leaf->ox == doctest::Approx(250.0f))) {
            CHECK_FALSE(leaf->visible);
            CHECK(leaf->horizonCulled);
        }
    }

    tree.update(Vec3(250.0f, 10.0f, 400.0f), 10.0f, nullptr, nullptr);
    CHECK(tree.stats().horizonCulledLeafCount == 0);
    CHECK(tree.stats().visibleLeafCount == tree.stats().leafCount);
}

TEST_CASE("terrain quadtree distance uses horizontal box distance and ground height") {
    TerrainQuadTree tree;
    TerrainQuadTreeSettings settings;
    settings.maxLevel = 0;
    tree.init(0.0f, 0.0f, 1000.0f, 0.0f, 100.0f, settings);
    const TerrainQuadNode& root = tree.root();

    CHECK(TerrainQuadTree::distanceToQuad(root, Vec3(500.0f, 0.0f, 500.0f), 0.0f) ==
          doctest::Approx(500.0f));
    CHECK(TerrainQuadTree::distanceToQuad(root, Vec3(-100.0f, 0.0f, -100.0f), 0.0f) ==
          doctest::Approx(100.0f));
    CHECK(TerrainQuadTree::distanceToQuad(root, Vec3(500.0f, 150.0f, 500.0f), 0.0f) ==
          doctest::Approx(500.0f));
    CHECK(TerrainQuadTree::distanceToQuad(root, Vec3(500.0f, 0.0f, 500.0f), 1000.0f) ==
          doctest::Approx(1000.0f));
}
