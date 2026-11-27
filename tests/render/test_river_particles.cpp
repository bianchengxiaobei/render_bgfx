#include "render/river/river_particles.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

using namespace hbg;
using namespace hbg::render;

namespace {

struct TestCamera {
    Mat4 viewProj;
    Mat4 invViewProj;
    Vec3 position;
};

Vec4 transform(const Mat4& matrix, const Vec4& value) {
    return Vec4(matrix.values[0] * value.x + matrix.values[4] * value.y + matrix.values[8] * value.z +
                    matrix.values[12] * value.w,
                matrix.values[1] * value.x + matrix.values[5] * value.y + matrix.values[9] * value.z +
                    matrix.values[13] * value.w,
                matrix.values[2] * value.x + matrix.values[6] * value.y + matrix.values[10] * value.z +
                    matrix.values[14] * value.w,
                matrix.values[3] * value.x + matrix.values[7] * value.y + matrix.values[11] * value.z +
                    matrix.values[15] * value.w);
}

TestCamera makeCamera() {
    const Vec3 eye(300.0f, 220.0f, 390.0f);
    const Vec3 target(100.0f, 0.0f, -60.0f);
    const Mat4 view = lookAtRH(eye, target, Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 projection =
        perspectiveRH(55.0f * 3.14159265358979323846f / 180.0f, 1280.0f / 720.0f, 0.1f, 5000.0f, true);
    const Mat4 viewProj = projection * view;
    return {viewProj, inverse(viewProj), eye};
}

RiverSpec makeRiver() {
    RiverSpec river;
    river.enabled = true;
    river.graph = RiverGraph::meander(1600.0f, 64.0f, 180.0f, 0.75f, 128);
    river.mesh.segments = 128;
    river.mesh.crossSegments = 8;
    river.mesh.baseHeight = 0.05f;
    river.mesh.uvLength = 180.0f;
    river.particles.textureSize = 64;
    river.particles.waveTileSize = 32;
    river.particles.particleRadius = 15.0f;
    river.particles.maxParticles = 1024;
    return river;
}

std::vector<float> makeGroundDepthSnapshot(const TestCamera& camera, uint16_t width, uint16_t height,
                                           float groundY) {
    std::vector<float> depths(static_cast<std::size_t>(width) * height, 1.0f);
    for (uint16_t y = 0; y < height; ++y) {
        for (uint16_t x = 0; x < width; ++x) {
            const float screenX = static_cast<float>(x) + 0.5f;
            const float screenY = static_cast<float>(y) + 0.5f;
            const float ndcX = 2.0f * screenX / static_cast<float>(width) - 1.0f;
            const float ndcY = 1.0f - 2.0f * screenY / static_cast<float>(height);
            const Vec4 farClip = transform(camera.invViewProj, Vec4(ndcX, ndcY, 1.0f, 1.0f));
            if (farClip.w <= 1.0e-6f) {
                continue;
            }
            const Vec3 farPosition(farClip.x / farClip.w, farClip.y / farClip.w, farClip.z / farClip.w);
            const Vec3 direction = normalize(farPosition - camera.position);
            if (std::abs(direction.y) <= 1.0e-5f) {
                continue;
            }
            const float distance = (groundY - camera.position.y) / direction.y;
            if (distance <= 0.0f) {
                continue;
            }
            const Vec3 world(camera.position.x + direction.x * distance, camera.position.y + direction.y * distance,
                             camera.position.z + direction.z * distance);
            const Vec4 clip = transform(camera.viewProj, Vec4(world.x, world.y, world.z, 1.0f));
            if (clip.w <= 1.0e-6f) {
                continue;
            }
            float depth = clip.z / clip.w * 0.5f + 0.5f;
            if (!std::isfinite(depth) || depth < 0.0f || depth > 1.0f) {
                continue;
            }
            const std::size_t row = static_cast<std::size_t>(height - 1 - y);
            depths[row * width + x] = depth;
        }
    }
    return depths;
}

float sampleDepthAt(const std::vector<float>& depths, uint16_t width, uint16_t height, Vec2 screen) {
    const int pixelX = std::clamp(static_cast<int>(std::floor(screen.x)), 0, static_cast<int>(width) - 1);
    const int pixelY = std::clamp(static_cast<int>(std::floor(screen.y)), 0, static_cast<int>(height) - 1);
    const std::size_t row = static_cast<std::size_t>(height - 1 - pixelY);
    return depths[row * width + static_cast<std::size_t>(pixelX)];
}

} // namespace

TEST_CASE("river particle system seeds a full viewport cloud with river world positions") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    CHECK(particles.valid());
    CHECK(particles.textureSize() == 64);
    REQUIRE(particles.baseColorPixels().size() == 64u * 64u * 4u);
    REQUIRE(particles.normalPixels().size() == 64u * 64u * 4u);

    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    CHECK(particles.particleCount() > 0);
    CHECK(particles.particleCount() <= river.particles.maxParticles);
    CHECK(particles.fadingParticleCount() == 0u);
    CHECK(particles.worldParticleCount() > 0u);
    CHECK(particles.particleCount() > particles.worldParticleCount());

    for (const RiverParticle& particle : particles.particles()) {
        if (!particle.worldValid) {
            continue;
        }
        Vec2 uv;
        CHECK(particles.worldToRiverUv(particle.world, uv));
        CHECK(uv.x >= 0.0f);
        CHECK(uv.x <= 1.0f);
    }
}

TEST_CASE("river particle cloud covers the full viewport but only river particles reach the residual") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);

    REQUIRE(particles.particleCount() > particles.worldParticleCount());
    CHECK(particles.worldParticleCount() > 0u);
    CHECK(particles.gpuParticleCount() <= particles.worldParticleCount());
    bool sawScreenOnly = false;
    for (const RiverParticle& particle : particles.particles()) {
        CHECK(particle.screenValid);
        CHECK(particle.screen.x >= 0.0f);
        CHECK(particle.screen.x < 1280.0f);
        CHECK(particle.screen.y >= 0.0f);
        CHECK(particle.screen.y < 720.0f);
        if (!particle.worldValid) {
            sawScreenOnly = true;
            continue;
        }
        Vec2 uv;
        REQUIRE(particles.worldToRiverUv(particle.world, uv));
        CHECK(uv.x >= 0.0f);
        CHECK(uv.x <= 1.0f);
        Vec2 projected;
        REQUIRE(particles.projectToScreen(Vec3(particle.world.x, river.mesh.baseHeight, particle.world.y),
                                          camera.viewProj, 1280, 720, projected));
        CHECK(projected.x == doctest::Approx(particle.screen.x).epsilon(0.5));
        CHECK(projected.y == doctest::Approx(particle.screen.y).epsilon(0.5));
    }
    CHECK(sawScreenOnly);
}

TEST_CASE("river depth snapshot reconstructs world positions on the ground plane") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    constexpr uint16_t width = 320;
    constexpr uint16_t height = 180;
    std::vector<float> depths = makeGroundDepthSnapshot(camera, width, height, 0.0f);
    particles.setDepthSnapshot(depths, width, height, camera.viewProj);
    CHECK(particles.hasDepthSnapshot());
    CHECK(particles.depthSnapshotMatches(camera.viewProj));
    CHECK(!particles.depthSnapshotMatches(Mat4::identity()));

    std::size_t validSamples = 0;
    for (uint16_t y = 90; y < height; y += 15) {
        for (uint16_t x = 20; x < width; x += 20) {
            const float screenX = static_cast<float>(x) + 0.5f;
            const float screenY = static_cast<float>(y) + 0.5f;
            Vec3 world;
            if (!particles.screenToDepthWorld(screenX, screenY, world)) {
                continue;
            }
            ++validSamples;
            CHECK(world.y == doctest::Approx(0.0f).epsilon(0.1));
            Vec2 projected;
            REQUIRE(particles.projectToScreen(world, camera.viewProj, width, height, projected));
            CHECK(projected.x == doctest::Approx(screenX).epsilon(0.02));
            CHECK(projected.y == doctest::Approx(screenY).epsilon(0.02));
        }
    }
    CHECK(validSamples > 20);
    particles.clearDepthSnapshot();
    CHECK(!particles.hasDepthSnapshot());
}

TEST_CASE("river particles backfill non-river world positions from the depth snapshot") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    constexpr uint16_t width = 640;
    constexpr uint16_t height = 360;
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width, height);
    const uint32_t riverBefore = particles.worldParticleCount();
    REQUIRE(particles.particleCount() > riverBefore);

    std::vector<float> depths = makeGroundDepthSnapshot(camera, width, height, 0.0f);
    particles.setDepthSnapshot(depths, width, height, camera.viewProj);
    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width,
                     height);
    CHECK(particles.depthWorldParticleCount() > riverBefore);
    CHECK(particles.depthWorldParticleCount() > particles.worldParticleCount());
    CHECK(particles.gpuParticleCount() <= particles.worldParticleCount());
    bool sawDepthOnly = false;
    for (const RiverParticle& particle : particles.particles()) {
        if (!particle.depthWorldValid || particle.worldValid) {
            continue;
        }
        sawDepthOnly = true;
        CHECK(std::isfinite(particle.worldHeight));
        CHECK(particle.worldHeight > -50.0f);
        CHECK(particle.worldHeight < 50.0f);
    }
    CHECK(sawDepthOnly);
}

TEST_CASE("river new particles prefer depth world positions before screen-to-water spawns") {
    const std::filesystem::path root(HBG_PROJECT_ROOT);
    std::ifstream file(root / "src/render/river/river_particles.cpp");
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    const auto spawn = source.find("const auto spawnAtScreen");
    REQUIRE(spawn != std::string::npos);
    const auto depthSpawn = source.find("if (depthSnapshotUsable) {", spawn);
    const auto planeSpawn = source.find("screenToWaterPlane(screenX, screenY, invViewProj", spawn);
    REQUIRE(depthSpawn != std::string::npos);
    REQUIRE(planeSpawn != std::string::npos);
    CHECK(depthSpawn < planeSpawn);

    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    constexpr uint16_t width = 640;
    constexpr uint16_t height = 360;
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width, height);
    uint32_t maxIdBefore = 0;
    for (const RiverParticle& particle : particles.particles()) {
        maxIdBefore = std::max(maxIdBefore, particle.id);
    }

    particles.setDepthSnapshot(makeGroundDepthSnapshot(camera, width, height, 0.0f), width, height,
                               camera.viewProj);
    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width,
                     height);
    bool sawNewDepth = false;
    bool sawNewWorld = false;
    for (const RiverParticle& particle : particles.particles()) {
        if (particle.id <= maxIdBefore) {
            continue;
        }
        sawNewDepth = sawNewDepth || particle.depthWorldValid;
        sawNewWorld = sawNewWorld || particle.worldValid;
    }
    CHECK(sawNewDepth);
    // A depth snapshot is usable for this view, so even river pixels must be
    // born depth-only; FlowTile promotes them on a later update.
    CHECK_FALSE(sawNewWorld);
}

TEST_CASE("river depth world positions join FlowTile when inside the river") {
    RiverSpec river = makeRiver();
    // A raised water plane separates screen-to-water spawns from the ground
    // depth snapshot, so ground points inside the river start depth-only.
    river.mesh.baseHeight = 100.0f;
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    constexpr uint16_t width = 640;
    constexpr uint16_t height = 360;
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width, height);
    particles.setDepthSnapshot(makeGroundDepthSnapshot(camera, width, height, 0.0f), width, height,
                               camera.viewProj);
    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width,
                     height);
    particles.update(2.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width,
                     height);

    CHECK(particles.depthFlowPromotionCount() > 0u);
    bool sawPromotedInside = false;
    for (const RiverParticle& particle : particles.particles()) {
        if (!particle.depthWorldValid || !particle.worldValid) {
            continue;
        }
        sawPromotedInside = sawPromotedInside || particle.flowStatus == RiverFlowStatus::Inside;
        CHECK(particle.worldHeight == doctest::Approx(river.mesh.baseHeight));
        CHECK(particle.firstVelocityQuery == false);
    }
    CHECK(sawPromotedInside);
}

TEST_CASE("river depth pack resolves requested screen particles before a full snapshot") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    constexpr uint16_t width = 320;
    constexpr uint16_t height = 180;

    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width, height);
    CHECK(particles.depthRequestMode() == RiverDepthRequestMode::Particles);
    std::vector<RiverDepthQuery> queries = particles.takeDepthQueries();
    REQUIRE(!queries.empty());
    const std::vector<float> ground = makeGroundDepthSnapshot(camera, width, height, 0.0f);
    std::vector<float> packedDepths;
    packedDepths.reserve(queries.size());
    for (const RiverDepthQuery& query : queries) {
        packedDepths.push_back(sampleDepthAt(ground, width, height, query.screen));
    }
    const uint32_t riverBefore = particles.worldParticleCount();
    particles.submitParticleDepths(packedDepths, queries, camera.viewProj, width, height);
    CHECK(particles.hasPendingDepthResults());
    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width,
                     height);
    CHECK(!particles.hasPendingDepthResults());
    CHECK(particles.depthWorldParticleCount() > riverBefore);
    CHECK(particles.depthRequestMode() == RiverDepthRequestMode::Full);
    CHECK(particles.takeDepthQueries().empty());

    particles.setDepthSnapshot(makeGroundDepthSnapshot(camera, width, height, 0.0f), width, height,
                               camera.viewProj);
    particles.update(2.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width,
                     height);
    CHECK(particles.depthRequestMode() == RiverDepthRequestMode::None);
    CHECK(particles.depthSnapshotMatches(camera.viewProj, width, height));
}

TEST_CASE("river depth requests return to particles after the view changes") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    constexpr uint16_t width = 320;
    constexpr uint16_t height = 180;

    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width, height);
    std::vector<RiverDepthQuery> queries = particles.takeDepthQueries();
    REQUIRE(!queries.empty());
    particles.submitParticleDepths(std::vector<float>(queries.size(), 1.0f), queries, camera.viewProj, width,
                                   height);
    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width,
                     height);
    CHECK(particles.depthRequestMode() == RiverDepthRequestMode::Full);

    particles.setDepthSnapshot(std::vector<float>(static_cast<std::size_t>(width) * height, 1.0f), width, height,
                               camera.viewProj);
    particles.update(2.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width,
                     height);
    CHECK(particles.depthRequestMode() == RiverDepthRequestMode::None);

    const Vec3 eye = camera.position;
    const Vec3 targetPoint(100.0f, 0.0f, -60.0f);
    const Vec3 shiftedEye(eye.x + 4.0f, eye.y, eye.z);
    const Vec3 shiftedTarget(targetPoint.x + 4.0f, targetPoint.y, targetPoint.z);
    const Mat4 shiftedView = lookAtRH(shiftedEye, shiftedTarget, Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 shiftedProjection = perspectiveRH(55.0f * 3.14159265358979323846f / 180.0f,
                                                 static_cast<float>(width) / static_cast<float>(height), 0.1f,
                                                 5000.0f, true);
    const Mat4 shiftedViewProj = shiftedProjection * shiftedView;
    particles.update(3.0f / 60.0f, 1.0f / 60.0f, shiftedViewProj, inverse(shiftedViewProj), shiftedEye, width,
                     height);
    CHECK(particles.depthRequestMode() == RiverDepthRequestMode::Particles);
    CHECK(!particles.takeDepthQueries().empty());
}

TEST_CASE("river depth world particles reproject when the camera moves") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    constexpr uint16_t width = 640;
    constexpr uint16_t height = 360;
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width, height);
    std::vector<float> depths = makeGroundDepthSnapshot(camera, width, height, 0.0f);
    particles.setDepthSnapshot(depths, width, height, camera.viewProj);
    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, width,
                     height);

    const RiverParticle* target = nullptr;
    for (const RiverParticle& particle : particles.particles()) {
        if (!particle.depthWorldValid || particle.worldValid || particle.fadingOut) {
            continue;
        }
        if (particle.screen.x < 40.0f || particle.screen.x >= width - 40.0f || particle.screen.y < 40.0f ||
            particle.screen.y >= height - 40.0f) {
            continue;
        }
        target = &particle;
        break;
    }
    REQUIRE(target != nullptr);
    const Vec2 world = target->world;
    const Vec2 oldScreen = target->screen;

    const Vec3 eye = camera.position;
    const Vec3 targetPoint(100.0f, 0.0f, -60.0f);
    const Vec3 shiftedEye(eye.x + 4.0f, eye.y, eye.z);
    const Vec3 shiftedTarget(targetPoint.x + 4.0f, targetPoint.y, targetPoint.z);
    const Mat4 shiftedView = lookAtRH(shiftedEye, shiftedTarget, Vec3(0.0f, 1.0f, 0.0f));
    const Mat4 shiftedProjection = perspectiveRH(55.0f * 3.14159265358979323846f / 180.0f,
                                                 static_cast<float>(width) / static_cast<float>(height), 0.1f,
                                                 5000.0f, true);
    const Mat4 shiftedViewProj = shiftedProjection * shiftedView;
    particles.update(0.0f, 0.0f, shiftedViewProj, inverse(shiftedViewProj), shiftedEye, width, height);

    bool found = false;
    for (const RiverParticle& particle : particles.particles()) {
        if (particle.world.x != world.x || particle.world.y != world.y) {
            continue;
        }
        found = true;
        CHECK(particle.depthWorldValid);
        CHECK(!particle.worldValid);
        CHECK(std::abs(particle.screen.x - oldScreen.x) > 0.25f);
        Vec2 projected;
        REQUIRE(particles.projectToScreen(Vec3(world.x, particle.worldHeight, world.y), shiftedViewProj, width,
                                          height, projected));
        CHECK(projected.x == doctest::Approx(particle.screen.x).epsilon(0.02));
        CHECK(projected.y == doctest::Approx(particle.screen.y).epsilon(0.02));
        break;
    }
    CHECK(found);
}

TEST_CASE("river screen projection round trips to the water plane") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();

    const Vec2 axisCenter = river.graph.axis[river.graph.axis.size() / 2];
    const Vec3 center(axisCenter.x, river.mesh.baseHeight, axisCenter.y);
    Vec2 screen;
    REQUIRE(particles.projectToScreen(center, camera.viewProj, 1280, 720, screen));
    CHECK(screen.x > 0.0f);
    CHECK(screen.x < 1280.0f);
    CHECK(screen.y > 0.0f);
    CHECK(screen.y < 720.0f);

    Vec2 hit;
    REQUIRE(particles.screenToRiver(screen.x, screen.y, camera.invViewProj, camera.position, 1280, 720, hit));
    CHECK(std::abs(hit.x - center.x) < 0.5f);
    CHECK(std::abs(hit.y - center.z) < 0.5f);
}

TEST_CASE("river wave texture has bank mask and animated normal data") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);

    const auto& color = particles.baseColorPixels();
    const auto& normal = particles.normalPixels();
    const std::size_t edge = (32u * 64u + 0u) * 4u;
    const std::size_t center = (32u * 64u + 32u) * 4u;
    const int edgeLuma = color[edge] + color[edge + 1] + color[edge + 2];
    const int centerLuma = color[center] + color[center + 1] + color[center + 2];
    CHECK(edgeLuma > centerLuma);
    CHECK(color[edge + 3] < color[center + 3]);
    CHECK(color[center + 3] > 200);
    CHECK(normal[edge + 3] < normal[center + 3]);
    CHECK(normal[center + 3] > 0.78f);
    CHECK(normal[center + 2] == doctest::Approx(1.0f));

    int positiveSlopes = 0;
    int negativeSlopes = 0;
    int variedSlopes = 0;
    for (std::size_t y = 16; y < 48; ++y) {
        for (std::size_t x = 16; x < 48; ++x) {
            const std::size_t index = (y * 64u + x) * 4u;
            if (normal[index] > 1.0e-4f || normal[index + 1] > 1.0e-4f) {
                ++positiveSlopes;
            }
            if (normal[index] < -1.0e-4f || normal[index + 1] < -1.0e-4f) {
                ++negativeSlopes;
            }
            if (std::abs(normal[index]) > 1.0e-4f || std::abs(normal[index + 1]) > 1.0e-4f) {
                ++variedSlopes;
            }
        }
    }
    CHECK(positiveSlopes > 100);
    CHECK(negativeSlopes > 100);
    CHECK(variedSlopes > 100);
}
TEST_CASE("river wave base normal builds a height-field mip chain") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));

    const auto& mips = particles.baseNormalMips();
    REQUIRE(mips.size() == 7u);
    CHECK(mips.front() == particles.baseNormalPixels());
    uint32_t size = river.particles.textureSize;
    for (std::size_t mip = 0; mip < mips.size(); ++mip) {
        CHECK(mips[mip].size() == static_cast<std::size_t>(size) * size * 4u);
        if (size > 1) {
            size /= 2;
        }
    }
    CHECK(mips.back().size() == 4u);

    const uint32_t halfSize = river.particles.textureSize / 2u;
    std::vector<float> boxFiltered(static_cast<std::size_t>(halfSize) * halfSize * 4u, 0.0f);
    for (uint32_t y = 0; y < halfSize; ++y) {
        for (uint32_t x = 0; x < halfSize; ++x) {
            const std::size_t target = (static_cast<std::size_t>(y) * halfSize + x) * 4u;
            for (uint32_t channel = 0; channel < 4u; ++channel) {
                const auto sample = [&](uint32_t sx, uint32_t sy) {
                    const std::size_t source =
                        (static_cast<std::size_t>(sy) * river.particles.textureSize + sx) * 4u + channel;
                    return mips.front()[source];
                };
                const float sum =
                    sample(x * 2u, y * 2u) + sample(x * 2u + 1u, y * 2u) + sample(x * 2u, y * 2u + 1u) +
                    sample(x * 2u + 1u, y * 2u + 1u);
                boxFiltered[target + channel] = sum * 0.25f;
            }
        }
    }
    CHECK(mips[1] != boxFiltered);
    REQUIRE(particles.normalMips().size() == mips.size());
    CHECK(particles.normalMips().front() == particles.normalPixels());
}

TEST_CASE("river wave frames advance with the configured time step") {
    RiverSpec river = makeRiver();
    river.particles.waveTimeLoop = 2;
    river.particles.waveTimeStep = 0.5f;
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    CHECK(particles.waveFrameCount() == 2);
    const TestCamera camera = makeCamera();

    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    CHECK(particles.waveFrame() == 0);
    const std::vector<float> frame0 = particles.baseNormalPixels();

    particles.update(0.6f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    CHECK(particles.waveFrame() == 1);
    CHECK(particles.baseNormalPixels() != frame0);

    particles.update(1.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    CHECK(particles.waveFrame() == 0);
    REQUIRE(particles.normalMips().size() == particles.baseNormalMips().size());
    CHECK(particles.normalMips().front() == particles.normalPixels());
}

TEST_CASE("river particles pack GPU screen-space sprite parameters") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    REQUIRE(particles.particleCount() > 0);
    REQUIRE(particles.gpuParticleCount() > 0);
    CHECK(particles.gpuParticleCount() <= particles.worldParticleCount());
    CHECK(particles.gpuParticleCount() <= particles.particleCount());
    REQUIRE(particles.gpuParticleParams().size() == static_cast<std::size_t>(particles.gpuParticleCapacity()) * 8u);
    const RiverParticle* first = nullptr;
    for (const RiverParticle& particle : particles.particles()) {
        if (particle.worldValid && particle.flowStatus != RiverFlowStatus::Unknown &&
            particle.flowStatus <= RiverFlowStatus::Near) {
            first = &particle;
            break;
        }
    }
    REQUIRE(first != nullptr);
    const float* params = particles.gpuParticleParams().data();
    CHECK(params[0] == doctest::Approx(first->screen.x));
    CHECK(params[1] == doctest::Approx(first->screen.y));
    CHECK(params[2] == doctest::Approx(first->world.x));
    CHECK(params[3] == doctest::Approx(first->world.y));
    CHECK(params[4] >= -1.0f);
    CHECK(params[4] <= 1.0f);
    CHECK(params[5] >= -1.0f);
    CHECK(params[5] <= 1.0f);
    CHECK(params[6] >= 0.0f);
    CHECK(params[6] <= 1.0f);
    CHECK(params[7] == doctest::Approx(river.particles.particleRadius * kRiverParticleRadiusFactor));
    CHECK(params[7] == doctest::Approx(45.0f));
    CHECK(particles.baseNormalPixels().size() == 64u * 64u * 4u);
    CHECK(particles.baseHeight() == doctest::Approx(river.mesh.baseHeight));
    CHECK(particles.waveLength() == doctest::Approx(river.particles.waveLength));
}

TEST_CASE("river particles build a screen-space sprite table for grid lookups") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    REQUIRE(particles.gpuParticleCount() > 0);
    REQUIRE(particles.gpuGridWidth() > 0);
    REQUIRE(particles.gpuGridHeight() > 0);
    CHECK(particles.gpuGridWidth() == 170u);
    CHECK(particles.gpuGridHeight() == 96u);
    CHECK(particles.gpuGridCellWidth() == doctest::Approx(1280.0f / 170.0f));
    CHECK(particles.gpuGridCellHeight() == doctest::Approx(7.5f));
    CHECK(particles.gpuGridCellWidth() * particles.gpuGridWidth() == doctest::Approx(1280.0f));
    CHECK(particles.gpuGridCellHeight() * particles.gpuGridHeight() == doctest::Approx(720.0f));
    REQUIRE(particles.gpuSpriteTable().size() == static_cast<std::size_t>(particles.gpuGridWidth()) *
                                                     particles.gpuGridHeight() *
                                                     RiverParticleSystem::kMaxParticlesPerCell);
    const RiverParticle* firstParticle = nullptr;
    std::size_t firstParticleIndex = particles.particles().size();
    for (std::size_t index = 0; index < particles.particles().size(); ++index) {
        const RiverParticle& particle = particles.particles()[index];
        if (particle.worldValid &&
            (particle.flowStatus == RiverFlowStatus::Inside || particle.flowStatus == RiverFlowStatus::Leaving)) {
            firstParticle = &particle;
            firstParticleIndex = index;
            break;
        }
    }
    REQUIRE(firstParticle != nullptr);
    uint32_t firstParticleParamIndex = 0;
    for (std::size_t index = 0; index < firstParticleIndex; ++index) {
        const RiverParticle& particle = particles.particles()[index];
        if (particle.worldValid && particle.flowStatus != RiverFlowStatus::Unknown &&
            particle.flowStatus <= RiverFlowStatus::Near) {
            ++firstParticleParamIndex;
        }
    }
    const int firstCellX =
        std::clamp(static_cast<int>(std::floor(firstParticle->screen.x / 1280.0f * particles.gpuGridWidth())), 0,
                   static_cast<int>(particles.gpuGridWidth()) - 1);
    const int firstCellY =
        std::clamp(static_cast<int>(std::floor(firstParticle->screen.y / 720.0f * particles.gpuGridHeight())), 0,
                   static_cast<int>(particles.gpuGridHeight()) - 1);
    bool firstParticleInQueryCell = false;
    const std::size_t firstCellBase =
        (static_cast<std::size_t>(firstCellY) * particles.gpuGridWidth() + static_cast<std::size_t>(firstCellX)) *
        RiverParticleSystem::kMaxParticlesPerCell;
    for (uint32_t k = 0; k < RiverParticleSystem::kMaxParticlesPerCell; ++k) {
        if (particles.gpuSpriteTable()[firstCellBase + k] == static_cast<float>(firstParticleParamIndex)) {
            firstParticleInQueryCell = true;
        }
    }
    CHECK(firstParticleInQueryCell);

    std::size_t spriteCount = 0;
    std::size_t invalidSpriteCount = 0;
    std::size_t outOfRangeCount = 0;
    bool foundFirstParticle = false;
    for (float id : particles.gpuSpriteTable()) {
        if (id < 0.0f) {
            continue;
        }
        ++spriteCount;
        const auto spriteId = static_cast<uint32_t>(id + 0.5f);
        if (id != static_cast<float>(spriteId)) {
            ++invalidSpriteCount;
        }
        if (spriteId >= particles.gpuParticleCount()) {
            ++outOfRangeCount;
        }
        if (spriteId == firstParticleParamIndex) {
            foundFirstParticle = true;
        }
    }
    CHECK(spriteCount > 0u);
    CHECK(invalidSpriteCount == 0u);
    CHECK(outOfRangeCount == 0u);
    CHECK(foundFirstParticle);
}

TEST_CASE("river particle GPU row spans cover occupied sprite cells") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);

    const auto& spans = particles.gpuGridSpans();
    REQUIRE(!spans.empty());
    CHECK(spans.size() <= particles.gpuGridHeight());
    std::vector<bool> sawRow(particles.gpuGridHeight(), false);
    for (const RiverGridSpan& span : spans) {
        REQUIRE(span.row < particles.gpuGridHeight());
        CHECK_FALSE(sawRow[span.row]);
        sawRow[span.row] = true;
        CHECK(span.minX <= span.maxX);
        CHECK(span.maxX < particles.gpuGridWidth());
        bool occupied = false;
        for (uint32_t cellX = span.minX; cellX <= span.maxX; ++cellX) {
            const std::size_t cellBase =
                (static_cast<std::size_t>(span.row) * particles.gpuGridWidth() + cellX) *
                RiverParticleSystem::kMaxParticlesPerCell;
            if (particles.gpuSpriteTable()[cellBase] >= 0.0f) {
                occupied = true;
                break;
            }
        }
        CHECK(occupied);
    }

    const auto& table = particles.gpuSpriteTable();
    for (uint32_t row = 0; row < particles.gpuGridHeight(); ++row) {
        const RiverGridSpan* span = nullptr;
        for (const RiverGridSpan& candidate : spans) {
            if (candidate.row == row) {
                span = &candidate;
                break;
            }
        }
        for (uint32_t cellX = 0; cellX < particles.gpuGridWidth(); ++cellX) {
            const std::size_t cellBase =
                (static_cast<std::size_t>(row) * particles.gpuGridWidth() + cellX) *
                RiverParticleSystem::kMaxParticlesPerCell;
            const bool occupied = table[cellBase] >= 0.0f;
            if (occupied) {
                REQUIRE(span != nullptr);
                CHECK(cellX >= span->minX);
                CHECK(cellX <= span->maxX);
            }
        }
    }
}

TEST_CASE("river particle GPU sprite grid rebuilds at original cell density") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 640, 360);
    CHECK(kRiverSpriteGridFactor == doctest::Approx(6.0f));
    CHECK(particles.gpuGridWidth() == 85u);
    CHECK(particles.gpuGridHeight() == 48u);
    CHECK(particles.gpuGridCellWidth() == doctest::Approx(640.0f / 85.0f));
    CHECK(particles.gpuGridCellHeight() == doctest::Approx(7.5f));
    CHECK(particles.gpuSpriteTable().size() == static_cast<std::size_t>(particles.gpuGridWidth()) *
                                                     particles.gpuGridHeight() *
                                                     RiverParticleSystem::kMaxParticlesPerCell);
}

TEST_CASE("river particle CPU normal splat can defer to the GPU residual") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    CHECK(particles.cpuNormalSplatEnabled());
    particles.setCpuNormalSplatEnabled(false);
    CHECK_FALSE(particles.cpuNormalSplatEnabled());
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    CHECK(particles.normalPixels() == particles.baseNormalPixels());
}

TEST_CASE("river particles advect across successive frames") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    const std::vector<float> firstNormal = particles.normalPixels();
    REQUIRE(particles.particleCount() > 0);
    for (int frame = 1; frame <= 12; ++frame) {
        particles.update(static_cast<float>(frame) / 30.0f, 1.0f / 30.0f, camera.viewProj, camera.invViewProj,
                         camera.position, 1280, 720);
    }
    CHECK(particles.particleCount() > 0);
    CHECK(particles.normalPixels() != firstNormal);
}

TEST_CASE("river particles enter lifecycle fade before removal") {
    RiverSpec river = makeRiver();
    river.particles.maxParticles = 64;
    river.particles.fadeInSeconds = 0.1f;
    river.particles.activeSeconds = 0.5f;
    river.particles.fadeOutSeconds = 0.2f;
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 0.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    REQUIRE(particles.particleCount() > 0);

    bool sawFading = false;
    for (int frame = 1; frame <= 180; ++frame) {
        particles.update(static_cast<float>(frame) / 30.0f, 1.0f / 30.0f, camera.viewProj, camera.invViewProj,
                         camera.position, 1280, 720);
        if (particles.fadingParticleCount() > 0) {
            sawFading = true;
            break;
        }
    }
    CHECK(sawFading);
    CHECK(particles.particleCount() > 0);
}

TEST_CASE("river particle spawn keeps two-radius spacing") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    const auto& spawned = particles.particles();
    REQUIRE(spawned.size() > 1);
    float minSquared = 1.0e30f;
    for (std::size_t i = 0; i < spawned.size(); ++i) {
        if (!spawned[i].screenValid) {
            continue;
        }
        for (std::size_t j = i + 1; j < spawned.size(); ++j) {
            if (!spawned[j].screenValid) {
                continue;
            }
            const float dx = spawned[i].screen.x - spawned[j].screen.x;
            const float dy = spawned[i].screen.y - spawned[j].screen.y;
            minSquared = std::min(minSquared, dx * dx + dy * dy);
        }
    }
    CHECK(minSquared >= 29.0f * 29.0f);
}

TEST_CASE("river particle spawn uses original angular neighbor ranges") {
    const std::filesystem::path root(HBG_PROJECT_ROOT);
    std::ifstream file(root / "src/render/river/river_particles.cpp");
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(source.find("class AngleRangeList") != std::string::npos);
    CHECK(source.find("const float rangeSquared = 16.0f * radius * radius;") != std::string::npos);
    CHECK(source.find("0.25f * distance / radius") != std::string::npos);
    CHECK(source.find("ranges.subtract(angle - kPi / 3.0f, angle + kPi / 3.0f)") != std::string::npos);
    CHECK(source.find("const float spawnDistance = 2.0f * radius;") != std::string::npos);
    CHECK(source.find("candidates.push_back(screen);") != std::string::npos);
}

TEST_CASE("river particle overlap fade uses original Poisson radius") {
    const std::filesystem::path root(HBG_PROJECT_ROOT);
    std::ifstream file(root / "src/render/river/river_particles.cpp");
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(source.find("const float overlapSq = 0.96f * 4.0f * radius * radius;") != std::string::npos);
    CHECK(source.find("beginFadeOut(particle, RiverFadeReason::PoissonDisk);") != std::string::npos);
    CHECK(source.find("particle.fadeOutElapsed += dt;") != std::string::npos);
    CHECK(source.find("particle.fadeOutElapsed >= particle.fadeOutDuration") != std::string::npos);
    CHECK(source.find("particle.fadeOutDuration = particle.fadeOutStartFade * m_settings.fadeOutSeconds") !=
          std::string::npos);
    CHECK(source.find("particle.fadeOutStartFade * remaining") != std::string::npos);
    CHECK(source.find("if (!fading && screenInside)") != std::string::npos);
    CHECK(source.find("acceptedScreens.push_back(particle.screen);") != std::string::npos);
    CHECK(source.find("const float margin = 2.0f * radius;") != std::string::npos);
    CHECK(source.find("beginFadeOut(particle, RiverFadeReason::OutsideViewport);") != std::string::npos);
    CHECK(source.find("bool RiverParticleSystem::isFading") != std::string::npos);
    CHECK(source.find("particle.age >= m_settings.fadeInSeconds + m_settings.activeSeconds") != std::string::npos);
    CHECK(source.find("riverLifecycleFade(particle.age, m_settings.fadeInSeconds, m_settings.activeSeconds,") !=
          std::string::npos);
    CHECK(source.find("particle.fadeReason != RiverFadeReason::OutsideViewport") != std::string::npos);
    CHECK(source.find("for (int attempt = 0; attempt < 64") == std::string::npos);
    CHECK(source.find("screenToWaterPlane") != std::string::npos);
    CHECK(source.find("const float screenX = random01() * width;") != std::string::npos);
    CHECK(source.find("spawnParticle(planePosition, Vec2(screenX, screenY), true);") != std::string::npos);
    CHECK(source.find("screenToDepthWorld(screenX, screenY, depthWorld)") != std::string::npos);
}

TEST_CASE("river particles follow original FlowTile status transitions") {
    const std::filesystem::path root(HBG_PROJECT_ROOT);
    std::ifstream file(root / "src/render/river/river_particles.cpp");
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(source.find("RiverFlowTransition transitionRiverFlow(") != std::string::npos);
    CHECK(source.find("transition.status = RiverFlowStatus::Outside;") != std::string::npos);
    CHECK(source.find("transition.status = RiverFlowStatus::Near;") != std::string::npos);
    CHECK(source.find("transition.status = RiverFlowStatus::Leaving;") != std::string::npos);
    CHECK(source.find("transition.status = RiverFlowStatus::Inside;") != std::string::npos);
    CHECK(source.find("transition.kill = true;") != std::string::npos);
    CHECK(source.find("transition.beginFade") == std::string::npos);
    CHECK(source.find("RiverFadeReason::NearRiver") == std::string::npos);
    CHECK(source.find("LeftRiver") == std::string::npos);
    CHECK(source.find("contributesRiverParams") != std::string::npos);
    CHECK(source.find("particle.flowStatus <= RiverFlowStatus::Near") != std::string::npos);
    CHECK(source.find("if (transition.kill)") != std::string::npos);
    CHECK(source.find("++m_nearKillCount;") != std::string::npos);
    CHECK(source.find("particle.firstVelocityQuery = false;") != std::string::npos);
    CHECK(source.find("particle.flowStatus = RiverFlowStatus::Unknown;") != std::string::npos);
    CHECK(source.find("particle.firstVelocityQuery = true;") != std::string::npos);
    CHECK(source.find("particle.worldValid || particle.depthWorldValid") != std::string::npos);
    CHECK(source.find("hasInsideNeighbor(particleIndex, particle.screen)") != std::string::npos);
    CHECK(source.find("|| isFading(particle) || !particle.screenValid") != std::string::npos);
    CHECK(source.find("|| particle.fadingOut || !particle.screenValid") == std::string::npos);
    CHECK(source.find("particle.worldValid = true;") != std::string::npos);
    CHECK(source.find("++m_depthFlowPromotions;") != std::string::npos);
    CHECK(source.find("return particle.worldValid && particle.flowStatus == RiverFlowStatus::Leaving;") !=
          std::string::npos);
}

TEST_CASE("river FlowTile transition follows TerrainParticleLayer conditions") {
    const Vec2 oldVelocity(3.0f, 4.0f);
    const Vec2 sampleVelocity(5.0f, 6.0f);

    RiverFlowTransition transition =
        transitionRiverFlow(RiverFlowStatus::Inside, false, oldVelocity, false, sampleVelocity, false);
    CHECK(transition.status == RiverFlowStatus::Leaving);
    CHECK(transition.velocity.x == oldVelocity.x);
    CHECK_FALSE(transition.kill);

    transition = transitionRiverFlow(RiverFlowStatus::Inside, true, oldVelocity, false, sampleVelocity, true);
    CHECK(transition.status == RiverFlowStatus::Near);
    CHECK(transition.velocity.x == 0.0f);
    CHECK(transition.kill);

    transition = transitionRiverFlow(RiverFlowStatus::Leaving, false, oldVelocity, false, sampleVelocity, true);
    CHECK(transition.status == RiverFlowStatus::Leaving);
    CHECK(transition.velocity.x == oldVelocity.x);
    CHECK_FALSE(transition.kill);

    transition = transitionRiverFlow(RiverFlowStatus::Leaving, false, oldVelocity, false, sampleVelocity, false);
    CHECK(transition.status == RiverFlowStatus::Outside);
    CHECK(transition.velocity.x == 0.0f);
    CHECK_FALSE(transition.kill);

    transition = transitionRiverFlow(RiverFlowStatus::Leaving, false, oldVelocity, true, sampleVelocity, false);
    CHECK(transition.status == RiverFlowStatus::Inside);
    CHECK(transition.velocity.x == sampleVelocity.x);
    CHECK_FALSE(transition.kill);

    transition = transitionRiverFlow(RiverFlowStatus::Outside, false, oldVelocity, false, sampleVelocity, true);
    CHECK(transition.status == RiverFlowStatus::Near);
    CHECK(transition.kill);
}

TEST_CASE("river lifecycle follows original fade in active fade out") {
    CHECK(riverLifecycleFade(0.0f, 2.0f, 30.0f, 2.0f) == doctest::Approx(0.0f));
    CHECK(riverLifecycleFade(1.0f, 2.0f, 30.0f, 2.0f) == doctest::Approx(0.5f));
    CHECK(riverLifecycleFade(2.0f, 2.0f, 30.0f, 2.0f) == doctest::Approx(1.0f));
    CHECK(riverLifecycleFade(31.9f, 2.0f, 30.0f, 2.0f) == doctest::Approx(1.0f));
    CHECK(riverLifecycleFade(33.0f, 2.0f, 30.0f, 2.0f) == doctest::Approx(0.5f));
    CHECK(riverLifecycleFade(34.0f, 2.0f, 30.0f, 2.0f) == doctest::Approx(0.0f));
}

TEST_CASE("river particle settings match Proland river1 storage and lifecycle") {
    const RiverParticleSettings settings;
    CHECK(settings.maxParticles == 5000u);
    CHECK(settings.particleRadius == doctest::Approx(15.0f));
    CHECK(settings.fadeInSeconds == doctest::Approx(2.0f));
    CHECK(settings.activeSeconds == doctest::Approx(30.0f));
    CHECK(settings.fadeOutSeconds == doctest::Approx(2.0f));
    CHECK(settings.randomOffsetMin.x == doctest::Approx(-1.0f));
    CHECK(settings.randomOffsetMin.y == doctest::Approx(-1.0f));
    CHECK(settings.randomOffsetMin.z == doctest::Approx(-1.0f));
    CHECK(settings.randomOffsetMax.x == doctest::Approx(1.0f));
    CHECK(settings.randomOffsetMax.y == doctest::Approx(1.0f));
    CHECK(settings.randomOffsetMax.z == doctest::Approx(1.0f));
}

TEST_CASE("river random particle offsets use the original RandomParticleLayer bounds") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 0.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    REQUIRE(particles.particleCount() > 0);

    bool sawNegative = false;
    bool sawPositive = false;
    for (const RiverParticle& particle : particles.particles()) {
        CHECK(particle.randomOffset.x >= -1.0f);
        CHECK(particle.randomOffset.x <= 1.0f);
        CHECK(particle.randomOffset.y >= -1.0f);
        CHECK(particle.randomOffset.y <= 1.0f);
        CHECK(particle.randomOffset.z >= -1.0f);
        CHECK(particle.randomOffset.z <= 1.0f);
        sawNegative = sawNegative || particle.randomOffset.x < 0.0f || particle.randomOffset.y < 0.0f ||
                      particle.randomOffset.z < 0.0f;
        sawPositive = sawPositive || particle.randomOffset.x > 0.0f || particle.randomOffset.y > 0.0f ||
                      particle.randomOffset.z > 0.0f;
    }
    CHECK(sawNegative);
    CHECK(sawPositive);
}

TEST_CASE("river random particle offsets use independent per-axis bounds") {
    RiverSpec river = makeRiver();
    // x is deliberately reversed to verify the per-axis min/max sanitize.
    river.particles.randomOffsetMin = Vec3(3.0f, 2.0f, -5.0f);
    river.particles.randomOffsetMax = Vec3(-1.0f, 4.0f, -2.0f);
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 0.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    REQUIRE(particles.particleCount() > 0);

    bool xInRange = true;
    bool yInRange = true;
    bool zInRange = true;
    for (const RiverParticle& particle : particles.particles()) {
        xInRange = xInRange && particle.randomOffset.x >= -1.0f && particle.randomOffset.x <= 3.0f;
        yInRange = yInRange && particle.randomOffset.y >= 2.0f && particle.randomOffset.y <= 4.0f;
        zInRange = zInRange && particle.randomOffset.z >= -5.0f && particle.randomOffset.z <= -2.0f;
    }
    CHECK(xInRange);
    CHECK(yInRange);
    CHECK(zInRange);
}

TEST_CASE("river particles are born UNKNOWN like TerrainParticleLayer") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    REQUIRE(particles.particleCount() > 0);
    for (const RiverParticle& particle : particles.particles()) {
        CHECK(particle.flowStatus == RiverFlowStatus::Unknown);
        CHECK(particle.firstVelocityQuery);
    }
    CHECK(particles.gpuParticleCount() == 0u);
    CHECK(particles.gpuGridSpans().empty());

    particles.update(1.0f / 60.0f, 1.0f / 60.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    CHECK(particles.gpuParticleCount() > 0u);
    for (const RiverParticle& particle : particles.particles()) {
        if (particle.worldValid && particle.flowStatus != RiverFlowStatus::Unknown &&
            particle.flowStatus <= RiverFlowStatus::Near) {
            CHECK_FALSE(particle.firstVelocityQuery);
        }
    }
}

TEST_CASE("river particles spawn at age zero like LifeCycleParticleLayer") {
    RiverSpec river = makeRiver();
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 0.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    REQUIRE(particles.particleCount() > 0);
    for (const RiverParticle& particle : particles.particles()) {
        CHECK(particle.age == doctest::Approx(0.0f));
        CHECK(particle.life ==
              doctest::Approx(river.particles.fadeInSeconds + river.particles.activeSeconds +
                              river.particles.fadeOutSeconds));
    }
}

TEST_CASE("river particles kill NEAR like LifeCycleParticleLayer") {
    RiverSpec river = makeRiver();
    river.particles.maxParticles = 512;
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();

    bool sawNearKill = false;
    for (int frame = 0; frame < 200; ++frame) {
        const float time = static_cast<float>(frame) * 0.1f;
        particles.update(time, 0.1f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
        sawNearKill = sawNearKill || particles.nearKillCount() > 0;
        if (sawNearKill) {
            break;
        }
    }
    CHECK(sawNearKill);
    CHECK(particles.nearParticleCount() == 0u);
    CHECK(particles.leavingParticleCount() == 0u);
}

TEST_CASE("river screen particles borrow depth world positions and reproject") {
    const std::filesystem::path root(HBG_PROJECT_ROOT);
    std::ifstream file(root / "src/render/river/river_particles.cpp");
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(source.find("m_depthHeight - 1 - pixelY") != std::string::npos);
    CHECK(source.find("depthSnapshotUsable && !particle.worldValid && !particle.depthWorldValid") !=
          std::string::npos);
    CHECK(source.find("particle.worldValid || particle.depthWorldValid") != std::string::npos);
    CHECK(source.find("particle.depthWorldValid = true;") != std::string::npos);
    CHECK(source.find("spawnParticle(Vec2(depthWorld.x, depthWorld.z), Vec2(screenX, screenY), false, true,") !=
          std::string::npos);
}

TEST_CASE("river particles fade inside original outside margin") {
    RiverSpec river = makeRiver();
    river.particles.maxParticles = 1024;
    RiverParticleSystem particles;
    REQUIRE(particles.init(river.graph, river.mesh, river.particles));
    const TestCamera camera = makeCamera();
    particles.update(0.0f, 0.0f, camera.viewProj, camera.invViewProj, camera.position, 1280, 720);
    REQUIRE(particles.particleCount() > 0);

    const RiverParticle* target = nullptr;
    float targetScreenX = 0.0f;
    float bestScore = std::numeric_limits<float>::max();
    for (const RiverParticle& particle : particles.particles()) {
        if (!particle.worldValid || !particle.screenValid) {
            continue;
        }
        const float score = std::abs(particle.screen.x - 640.0f);
        if (score < bestScore) {
            bestScore = score;
            target = &particle;
            targetScreenX = particle.screen.x;
        }
    }
    REQUIRE(target != nullptr);
    const Vec2 targetWorld = target->world;

    const float targetX = -0.5f * river.particles.particleRadius;
    const float clipW = camera.viewProj.values[3] * targetWorld.x +
                        camera.viewProj.values[7] * river.mesh.baseHeight +
                        camera.viewProj.values[11] * targetWorld.y + camera.viewProj.values[15];
    REQUIRE(clipW > 0.0f);
    Mat4 shifted = camera.viewProj;
    shifted.values[12] += 2.0f * (targetX - targetScreenX) * clipW / 1280.0f;
    particles.update(0.0f, 0.0f, shifted, inverse(shifted), camera.position, 1280, 720);

    const float margin = 2.0f * river.particles.particleRadius;
    bool foundTarget = false;
    for (const RiverParticle& particle : particles.particles()) {
        if (particle.world.x != targetWorld.x || particle.world.y != targetWorld.y) {
            continue;
        }
        foundTarget = true;
        CHECK(particle.screenValid);
        CHECK(particle.screen.x < 0.0f);
        CHECK(particle.screen.x >= -margin);
        CHECK(particle.fadingOut);
        CHECK(particle.fadeReason == RiverFadeReason::OutsideViewport);
    }
    CHECK(foundTarget);

    const auto& params = particles.gpuParticleParams();
    std::size_t outsideParams = 0;
    for (uint32_t index = 0; index < particles.gpuParticleCount(); ++index) {
        const float x = params[static_cast<std::size_t>(index) * 8u];
        const float y = params[static_cast<std::size_t>(index) * 8u + 1u];
        CHECK(x >= -margin - 0.01f);
        CHECK(x < 1280.0f + margin + 0.01f);
        CHECK(y >= -margin - 0.01f);
        CHECK(y < 720.0f + margin + 0.01f);
        if (x < 0.0f || x >= 1280.0f || y < 0.0f || y >= 720.0f) {
            ++outsideParams;
        }
    }
    CHECK(outsideParams > 0);
}

TEST_CASE("river particle grid matches original 4r cells and 64 entry cap") {
    const std::filesystem::path root(HBG_PROJECT_ROOT);
    std::ifstream file(root / "src/render/river/river_particles.cpp");
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    CHECK(source.find("class RiverParticleGrid") != std::string::npos);
    CHECK(source.find("static constexpr int kMaxEntriesPerCell = 64;") != std::string::npos);
    CHECK(source.find("m_particleRadius = std::max(4.0f * particleRadius, 1.0f);") != std::string::npos);
    CHECK(source.find("(screen.x - m_particleRadius) / m_cellWidth") != std::string::npos);
    CHECK(source.find("minIntensity < intensity") != std::string::npos);
    CHECK(source.find("poissonGrid.reset(viewportWidth, viewportHeight, radius);") != std::string::npos);
    CHECK(source.find("poissonGrid.cell(particle.screen.x, particle.screen.y)") != std::string::npos);
    CHECK(source.find("poissonGrid.cell(origin.x, origin.y)") != std::string::npos);
}

TEST_CASE("river particle residual shader uses the original sprite grid") {
    const std::filesystem::path root(HBG_PROJECT_ROOT);
    const auto readSource = [](const std::filesystem::path& path) {
        std::ifstream file(path);
        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    };
    const std::string vertex = readSource(root / "shaders/passes/river_particles.vs.sc");
    const std::string fragment = readSource(root / "shaders/passes/river_particles.fs.sc");
    const std::string gpu = readSource(root / "src/render/river/river_particles_gpu.cpp");
    const std::string packing = readSource(root / "shaders/common/packing.sc");
    const std::string surface = readSource(root / "shaders/material/surface_data.sc");
    const std::string riverMaterial = readSource(root / "shaders/material/river_particles.sc");
    const std::string packVertex = readSource(root / "shaders/passes/river_depth_pack.vs.sc");
    const std::string packFragment = readSource(root / "shaders/passes/river_depth_pack.fs.sc");
    const std::string riverSource = readSource(root / "src/render/river/river_particles.cpp");
    const std::string forwardSource = readSource(root / "src/render/passes/forward_renderer.cpp");
    CHECK(vertex.find("$input a_position") != std::string::npos);
    CHECK(vertex.find("gl_Position = vec4(a_position.xy, 0.0, 1.0);") != std::string::npos);
    CHECK(vertex.find("v_screen") != std::string::npos);
    CHECK(fragment.find("s_riverParticleSpriteTable") != std::string::npos);
    CHECK(fragment.find("s_riverParticleParams") != std::string::npos);
    CHECK(fragment.find("u_riverParticleGrid") != std::string::npos);
    CHECK(fragment.find("texture2DLod(s_riverParticleSpriteTable") != std::string::npos);
    CHECK(fragment.find("texture2DLod(s_riverParticleParams") != std::string::npos);
    CHECK(fragment.find("texture2DGrad(s_riverWaveBase") != std::string::npos);
    CHECK(fragment.find("gl_FragColor = vec4(sumSlopes, sumWeight, 1.0);") != std::string::npos);
    CHECK(fragment.find("gl_FragColor = vec4(slopes * weight, weight * weight, 1.0);") == std::string::npos);
    CHECK(fragment.find("texture2DLod(s_riverWaveBase") == std::string::npos);
    CHECK(fragment.find(".xy * 2.0 - 1.0;") == std::string::npos);
    CHECK(gpu.find("BGFX_STATE_BLEND_ADD") == std::string::npos);
    CHECK(gpu.find("buildRiverParticleSplatVertices") == std::string::npos);
    CHECK(gpu.find("m_parameterTexture") != std::string::npos);
    CHECK(gpu.find("m_spriteTableTexture") != std::string::npos);
    CHECK(gpu.find("updateTexture(m_parameterTexture") != std::string::npos);
    CHECK(gpu.find("const uint32_t parameterRows = std::min(river.gpuParticleCount(), m_capacity);") !=
          std::string::npos);
    CHECK(gpu.find("if (parameterRows > 0)") != std::string::npos);
    CHECK(gpu.find("static_cast<uint16_t>(parameterRows), parameterPitch") != std::string::npos);
    CHECK(gpu.find("updateTexture(m_spriteTableTexture") != std::string::npos);
    CHECK(gpu.find("setTexture(2, m_spriteTableSampler") != std::string::npos);
    CHECK(gpu.find("river.gpuGridSpans()") != std::string::npos);
    CHECK(gpu.find("encoder.touch(view);") != std::string::npos);
    CHECK(gpu.find("gfx::TextureFormat::RGBA32F") != std::string::npos);
    CHECK(gpu.find("gfx::TextureFormat::RGBA16F") == std::string::npos);
    CHECK(gpu.find("normalDesc.flags = BGFX_TEXTURE_NONE;") != std::string::npos);
    CHECK(gpu.find("normalDesc.width = width;") != std::string::npos);
    CHECK(gpu.find("normalDesc.height = height;") != std::string::npos);
    CHECK(gpu.find("ensureTarget(resources, width, height)") != std::string::npos);
    CHECK(gpu.find("target=viewport") != std::string::npos);
    CHECK(gpu.find("m_textureSize") == std::string::npos);
    CHECK(gpu.find("setViewRect(view, 0, 0, static_cast<uint16_t>(m_targetWidth), "
                   "static_cast<uint16_t>(m_targetHeight));") != std::string::npos);
    CHECK(packing.find("hbgUnpackSlopeNormal") != std::string::npos);
    CHECK(packing.find("normalize(vec3(slopes * scale, 1.0))") != std::string::npos);
    CHECK(surface.find("u_material_riverRefractionParams.z > 0.5") != std::string::npos);
    CHECK(surface.find("const vec2 slopes = normalSample;") != std::string::npos);
    CHECK(surface.find("const vec2 slopes = normalSample * 2.0 - 1.0;") == std::string::npos);
    CHECK(surface.find("hbgUnpackSlopeNormal(slopes, max(u_material_riverParticleParams.y, 0.0))") !=
          std::string::npos);
    CHECK(riverMaterial.find("hbgUnpackSlopeNormal(slopes, max(u_material_riverParticleParams.y, 0.0))") !=
          std::string::npos);
    CHECK(riverMaterial.find("const vec2 slopes = particle.rg / sqrt(particle.b);") != std::string::npos);
    CHECK(riverMaterial.find("if (particle.b <= 1.0e-6)") != std::string::npos);
    CHECK(riverMaterial.find("sqrt(particle.a)") == std::string::npos);
    CHECK(RiverParticleSettings{}.waveSlopeFactor == doctest::Approx(10.0f));
    CHECK(RiverParticleSettings{}.spawnCellFactor == doctest::Approx(2.0f));
    CHECK(packVertex.find("u_riverDepthPackSize") != std::string::npos);
    CHECK(packVertex.find("gl_Position") != std::string::npos);
    CHECK(packFragment.find("s_riverDepthSource") != std::string::npos);
    CHECK(packFragment.find("texture2D(s_riverDepthSource, v_texcoord0)") != std::string::npos);
    CHECK(riverSource.find("RiverDepthRequestMode::Particles") != std::string::npos);
    CHECK(riverSource.find("applyPendingDepthResults()") != std::string::npos);
    CHECK(forwardSource.find("River Depth Pack") != std::string::npos);
    CHECK(forwardSource.find("BGFX_STATE_PT_POINTS") != std::string::npos);
    CHECK(forwardSource.find("BGFX_STATE_POINT_SIZE(1)") != std::string::npos);
}
