#include <doctest/doctest.h>

#include "vfx/asset.h"
#include "vfx/reference.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

namespace {

std::filesystem::path sourcePath() {
#ifdef VFX_SOURCE_DIR
    return std::filesystem::path(VFX_SOURCE_DIR);
#else
    return std::filesystem::path(".");
#endif
}

float length(const vfx::Vec3& value) {
    return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

} // namespace

TEST_CASE("vfx CPU reference sorts KVP by key and value with both orders") {
    std::vector<vfx::reference::SortKVP> kvp;
    std::uint32_t state = 0x12345678u;
    for (std::uint32_t i = 0; i < 4096u; ++i) {
        state = state * 1664525u + 1013904223u;
        const float key = static_cast<float>(static_cast<int>(state % 37u) - 18);
        kvp.push_back({key, i});
    }
    for (const vfx::SortOrder order : {vfx::SortOrder::Ascending, vfx::SortOrder::Descending}) {
        std::vector<vfx::reference::SortKVP> actual = kvp;
        vfx::reference::sortKVP(actual, order);
        std::vector<vfx::reference::SortKVP> expected = kvp;
        std::sort(expected.begin(), expected.end(),
                  [order](const vfx::reference::SortKVP& a, const vfx::reference::SortKVP& b) {
                      if (a.key == b.key) {
                          return a.value < b.value;
                      }
                      return order == vfx::SortOrder::Descending ? a.key > b.key : a.key < b.key;
                  });
        REQUIRE(actual.size() == expected.size());
        for (std::size_t i = 0; i < actual.size(); ++i) {
            CHECK(actual[i].key == expected[i].key);
            CHECK(actual[i].value == expected[i].value);
        }
    }
}

TEST_CASE("vfx CPU reference spawner merges bursts and keeps period boundaries exact") {
    const vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/spawner_bursts.vfx.json");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.graph->systems.size() == 1u);
    const vfx::SpawnerDesc& spawner = parsed.graph->systems[0].spawner;

    vfx::reference::SpawnerState state;
    vfx::reference::resetSpawner(spawner, state);
    CHECK(state.burstFired.size() == 3u);
    CHECK(vfx::reference::updateSpawner(spawner, 0.1f, state) == 14u);
    CHECK(state.time == doctest::Approx(0.1));

    const std::vector<std::uint32_t> expected = {2u, 2u, 2u, 2u, 0u, 0u, 0u};
    for (const std::uint32_t spawnCount : expected) {
        CHECK(vfx::reference::updateSpawner(spawner, 0.1f, state) == spawnCount);
    }
    CHECK(state.time == doctest::Approx(0.8));
}

TEST_CASE("vfx CPU reference spawner clamps delta time and handles delayed bursts") {
    vfx::SpawnerDesc spawner;
    spawner.rate = 40.0f;
    spawner.bursts.push_back(vfx::Burst{0.1f, 3u, std::nullopt, std::nullopt});

    vfx::reference::SpawnerState state;
    vfx::reference::resetSpawner(spawner, state);
    CHECK(vfx::reference::updateSpawner(spawner, 1.0f, state) == 7u);
    CHECK(state.time == doctest::Approx(0.1));
    CHECK(vfx::reference::updateSpawner(spawner, -1.0f, state) == 0u);
    CHECK(vfx::reference::updateSpawner(spawner, std::numeric_limits<float>::quiet_NaN(), state) == 0u);
    CHECK(vfx::reference::updateSpawner(spawner, 0.0f, state) == 0u);

    vfx::SpawnerDesc delayed;
    delayed.bursts.push_back(vfx::Burst{0.1f, 1u, std::nullopt, std::nullopt});
    vfx::reference::SpawnerState delayedState;
    vfx::reference::resetSpawner(delayed, delayedState);
    CHECK(vfx::reference::updateSpawner(delayed, 0.05f, delayedState) == 0u);
    CHECK(vfx::reference::updateSpawner(delayed, 0.05f, delayedState) == 1u);
}

TEST_CASE("vfx CPU reference simulates 1024 particles for 10 frames") {
    const vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/reference_1024.vfx.json");
    REQUIRE(parsed.ok());
    REQUIRE(parsed.graph->systems.size() == 1u);

    std::vector<bool> seen(1024u, false);
    for (std::uint32_t spawnIndex = 0u; spawnIndex < 1024u; ++spawnIndex) {
        vfx::reference::ParticleState state;
        std::string error;
        REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, spawnIndex, 0u, 0.1f, 10u, state,
                                                 &error));
        INFO(error);
        CHECK(state.spawnIndex == spawnIndex);
        CHECK(state.particleId == spawnIndex);
        CHECK(state.alive);
        CHECK(state.age == doctest::Approx(1.0f));
        CHECK(state.lifetime == doctest::Approx(100.0f));
        CHECK(state.alpha == doctest::Approx(0.75f));
        CHECK(state.size == doctest::Approx(0.1f));
        CHECK(state.color.x == doctest::Approx(0.2f));
        CHECK(state.color.y == doctest::Approx(0.4f));
        CHECK(state.color.z == doctest::Approx(0.6f));
        CHECK(state.position.x >= doctest::Approx(-2.0f));
        CHECK(state.position.x <= doctest::Approx(2.0f));
        CHECK(state.position.y >= doctest::Approx(-0.25f));
        CHECK(state.position.y <= doctest::Approx(2.25f));
        CHECK(state.position.z >= doctest::Approx(-3.0f));
        CHECK(state.position.z <= doctest::Approx(3.0f));
        CHECK(length(state.velocity) >= doctest::Approx(0.25f));
        CHECK(length(state.velocity) <= doctest::Approx(0.75f));
        seen[state.spawnIndex] = true;
    }
    for (const bool value : seen) {
        CHECK(value);
    }
}

TEST_CASE("vfx CPU reference applies absolute and relative force") {
    const auto simulate = [](std::string_view json) {
        const vfx::ParseResult parsed = vfx::parseGraphJson(json);
        REQUIRE(parsed.ok());
        vfx::reference::ParticleState state;
        std::string error;
        REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 0u, 0u, 0.1f, 1u, state, &error));
        INFO(error);
        return state;
    };

    const vfx::reference::ParticleState absolute = simulate(R"json({
      "version": 1, "name": "AbsoluteForce", "properties": [],
      "systems": [{"name": "Main", "capacity": 1,
        "spawner": {"rate": 0, "bursts": [{"time": 0.0, "count": 1}]},
        "init": {"blocks": [
          {"type": "set_position_box", "center": [0, 0, 0], "size": [0, 0, 0]},
          {"type": "set_velocity_direction", "direction": [0, 0, 0], "speed": 0.0},
          {"type": "set_lifetime", "lifetime": 10.0}
        ]},
        "update": {"blocks": [
          {"type": "force", "force": [1.0, 0.0, 0.0]},
          {"type": "integrate"}
        ]},
        "outputs": [{}]}]
    })json");
    CHECK(absolute.velocity.x == doctest::Approx(0.1f));
    CHECK(absolute.position.x == doctest::Approx(0.01f));

    const vfx::reference::ParticleState relative = simulate(R"json({
      "version": 1, "name": "RelativeForce", "properties": [],
      "systems": [{"name": "Main", "capacity": 1,
        "spawner": {"rate": 0, "bursts": [{"time": 0.0, "count": 1}]},
        "init": {"blocks": [
          {"type": "set_position_box", "center": [0, 0, 0], "size": [0, 0, 0]},
          {"type": "set_velocity_direction", "direction": [1, 0, 0], "speed": 1.0},
          {"type": "set_lifetime", "lifetime": 10.0}
        ]},
        "update": {"blocks": [
          {"type": "force", "force": [0.0, 0.0, 0.0], "mode": "relative", "drag": 1.0},
          {"type": "integrate"}
        ]},
        "outputs": [{}]}]
    })json");
    CHECK(relative.velocity.x == doctest::Approx(0.9f));
    CHECK(relative.position.x == doctest::Approx(0.09f));
}

TEST_CASE("vfx CPU reference applies size-aware linear drag") {
    const vfx::ParseResult parsed = vfx::parseGraphJson(R"json({
      "version": 1, "name": "SizeDrag", "properties": [],
      "systems": [{"name": "Main", "capacity": 1,
        "spawner": {"rate": 0, "bursts": [{"time": 0.0, "count": 1}]},
        "init": {"blocks": [
          {"type": "set_position_box", "center": [0, 0, 0], "size": [0, 0, 0]},
          {"type": "set_velocity_direction", "direction": [1, 0, 0], "speed": 1.0},
          {"type": "set_size", "size": 0.5},
          {"type": "set_scale", "scale": [2.0, 1.0, 1.0]},
          {"type": "set_lifetime", "lifetime": 10.0}
        ]},
        "update": {"blocks": [{"type": "drag", "drag": 2.0, "useSize": true}]},
        "outputs": [{}]}]
    })json");
    REQUIRE(parsed.ok());
    vfx::reference::ParticleState state;
    std::string error;
    REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 0u, 0u, 0.1f, 1u, state, &error));
    CHECK(state.velocity.x == doctest::Approx(std::exp(-0.1f)));
    CHECK(state.velocity.y == doctest::Approx(0.0f));
}

TEST_CASE("vfx CPU reference resolves plane collision modes") {
    const auto simulate = [](std::string_view collision) {
        const std::string json = R"json({
          "version": 1, "name": "PlaneCollision", "properties": [],
          "systems": [{"name": "Main", "capacity": 1,
            "spawner": {"rate": 0, "bursts": [{"time": 0.0, "count": 1}]},
            "init": {"blocks": [
              {"type": "set_position_box", "center": [0, 0.05, 0], "size": [0, 0, 0]},
              {"type": "set_velocity_direction", "direction": [0, -1, 0], "speed": 2.0},
              {"type": "set_lifetime", "lifetime": 10.0}
            ]},
            "update": {"blocks": [
              {"type": "collision_plane", "normal": [0, 1, 0], "distance": 0.0, )json" +
                              std::string(collision) + R"json(}
            ]},
            "outputs": [{}]}]}
        )json";
        const vfx::ParseResult parsed = vfx::parseGraphJson(json);
        REQUIRE(parsed.ok());
        vfx::reference::ParticleState state;
        std::string error;
        REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 0u, 0u, 0.1f, 1u, state, &error));
        INFO(error);
        return state;
    };

    const vfx::reference::ParticleState bounce =
        simulate(R"json("mode": "bounce", "restitution": 0.5, "friction": 0.0)json");
    CHECK(bounce.alive);
    CHECK(bounce.position.y == doctest::Approx(0.0f));
    CHECK(bounce.velocity.y == doctest::Approx(1.0f));

    const vfx::reference::ParticleState stick =
        simulate(R"json("mode": "stick")json");
    CHECK(stick.alive);
    CHECK(stick.position.y == doctest::Approx(0.0f));
    CHECK(stick.velocity.x == doctest::Approx(0.0f));
    CHECK(stick.velocity.y == doctest::Approx(0.0f));
    CHECK(stick.velocity.z == doctest::Approx(0.0f));

    const vfx::reference::ParticleState kill =
        simulate(R"json("mode": "kill")json");
    CHECK_FALSE(kill.alive);
}

TEST_CASE("vfx CPU reference sweeps a sphere and reflects restitution") {
    const vfx::ParseResult parsed = vfx::parseGraphJson(R"json({
      "version": 1, "name": "SphereCollision", "properties": [],
      "systems": [{"name": "Main", "capacity": 1,
        "spawner": {"rate": 0, "bursts": [{"time": 0.0, "count": 1}]},
        "init": {"blocks": [
          {"type": "set_position_box", "center": [0, 0, 0], "size": [0, 0, 0]},
          {"type": "set_velocity_direction", "direction": [0, 1, 0], "speed": 5.0},
          {"type": "set_lifetime", "lifetime": 10.0}
        ]},
        "update": {"blocks": [
          {"type": "collision_sphere", "center": [0, 1, 0], "radius": 0.5,
           "mode": "bounce", "restitution": 0.5, "friction": 0.0}
        ]},
        "outputs": [{}]}]}
    )json");
    REQUIRE(parsed.ok());
    vfx::reference::ParticleState state;
    std::string error;
    REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 0u, 0u, 0.1f, 1u, state, &error));
    INFO(error);
    CHECK(state.alive);
    CHECK(state.position.y == doctest::Approx(0.5f));
    CHECK(state.velocity.y == doctest::Approx(-2.5f));
    CHECK(std::isfinite(state.position.x));
    CHECK(std::isfinite(state.position.z));
}

TEST_CASE("vfx CPU reference keeps plane collisions stable across frames") {
    const vfx::ParseResult parsed = vfx::parseGraphJson(R"json({
      "version": 1, "name": "StablePlane", "properties": [],
      "systems": [{"name": "Main", "capacity": 1,
        "spawner": {"rate": 0, "bursts": [{"time": 0.0, "count": 1}]},
        "init": {"blocks": [
          {"type": "set_position_box", "center": [0, 2, 0], "size": [0, 0, 0]},
          {"type": "set_velocity_direction", "direction": [0, -1, 0], "speed": 5.0},
          {"type": "set_lifetime", "lifetime": 10.0}
        ]},
        "update": {"blocks": [
          {"type": "collision_plane", "normal": [0, 1, 0], "distance": 0.0,
           "mode": "bounce", "restitution": 0.0, "friction": 1.0},
          {"type": "integrate"}
        ]},
        "outputs": [{}]}]}
    )json");
    REQUIRE(parsed.ok());
    vfx::reference::ParticleState state;
    std::string error;
    REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 0u, 0u, 0.1f, 20u, state, &error));
    INFO(error);
    CHECK(state.alive);
    CHECK(state.position.y >= doctest::Approx(0.0f));
    CHECK(std::isfinite(state.position.y));
    CHECK(std::isfinite(state.velocity.y));
}

TEST_CASE("vfx CPU reference turbulence is deterministic") {
    const vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/forces_1024.vfx.json");
    REQUIRE(parsed.ok());

    vfx::reference::ParticleState first;
    vfx::reference::ParticleState second;
    std::string error;
    REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 123u, 0u, 0.1f, 10u, first, &error));
    REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 123u, 0u, 0.1f, 10u, second, &error));
    CHECK(first.position.x == second.position.x);
    CHECK(first.position.y == second.position.y);
    CHECK(first.position.z == second.position.z);
    CHECK(first.velocity.x == second.velocity.x);
    CHECK(first.velocity.y == second.velocity.y);
    CHECK(first.velocity.z == second.velocity.z);
    CHECK(std::isfinite(first.position.x));
    CHECK(std::isfinite(first.position.y));
    CHECK(std::isfinite(first.position.z));
    CHECK(std::isfinite(first.velocity.x));
    CHECK(std::isfinite(first.velocity.y));
    CHECK(std::isfinite(first.velocity.z));
}

TEST_CASE("vfx CPU reference is deterministic and exposes the initial state") {
    const vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/reference_1024.vfx.json");
    REQUIRE(parsed.ok());

    vfx::reference::ParticleState first;
    vfx::reference::ParticleState second;
    std::string error;
    REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 17u, 0u, 0.1f, 0u, first, &error));
    REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 17u, 0u, 0.1f, 0u, second, &error));
    CHECK(first.seed == second.seed);
    CHECK(first.position.x == second.position.x);
    CHECK(first.position.y == second.position.y);
    CHECK(first.position.z == second.position.z);
    CHECK(first.velocity.x == second.velocity.x);
    CHECK(first.velocity.y == second.velocity.y);
    CHECK(first.velocity.z == second.velocity.z);
    CHECK(first.age == doctest::Approx(0.0f));

    vfx::reference::ParticleState changed;
    REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 18u, 0u, 0.1f, 0u, changed, &error));
    CHECK(first.seed != changed.seed);
}

TEST_CASE("vfx CPU reference applies color, alpha and size over life") {
    const vfx::ParseResult parsed = vfx::parseGraphJson(R"json({
      "version": 1, "name": "OverLife", "properties": [
        {"name": "ColorRamp", "type": "gradient", "keys": [
          {"t": 0.0, "color": [0.2, 0.4, 0.6, 1.0]},
          {"t": 1.0, "color": [1.0, 0.5, 0.25, 0.0]}
        ]},
        {"name": "AlphaCurve", "type": "curve", "keys": [[0.0, 1.0], [1.0, 0.0]]},
        {"name": "SizeCurve", "type": "curve", "keys": [[0.0, 0.2], [1.0, 1.2]]}
      ],
      "systems": [{"name": "Main", "capacity": 1,
        "spawner": {"rate": 0, "bursts": [{"time": 0.0, "count": 1}]},
        "init": {"blocks": [
          {"type": "set_position_box", "center": [0, 0, 0], "size": [0, 0, 0]},
          {"type": "set_lifetime", "lifetime": 10.0}
        ]},
        "update": {"blocks": [
          {"type": "age"},
          {"type": "color_over_life", "gradient": "ColorRamp"},
          {"type": "alpha_over_life", "curve": "AlphaCurve"},
          {"type": "size_over_life", "curve": "SizeCurve"}
        ]},
        "outputs": [{}]}]}
    )json");
    REQUIRE(parsed.ok());
    vfx::reference::ParticleState state;
    std::string error;
    REQUIRE(vfx::reference::simulateParticle(*parsed.graph, 0u, 0u, 0u, 0.1f, 10u, state, &error));
    CHECK(state.color.x == doctest::Approx(0.28f));
    CHECK(state.color.y == doctest::Approx(0.41f));
    CHECK(state.color.z == doctest::Approx(0.565f));
    CHECK(state.alpha == doctest::Approx(0.9f));
    CHECK(state.size == doctest::Approx(0.3f));
}

TEST_CASE("vfx CPU reference rejects unsupported graphs and systems") {
    const vfx::ParseResult parsed =
        vfx::loadGraphFile(sourcePath() / "tests/assets/reference_1024.vfx.json");
    REQUIRE(parsed.ok());
    vfx::reference::ParticleState state;
    std::string error;
    CHECK_FALSE(vfx::reference::simulateParticle(*parsed.graph, 4u, 0u, 0u, 0.1f, 10u, state, &error));
    CHECK_FALSE(error.empty());
}


TEST_CASE("vfx CPU reference matches subgraph expansion") {
    constexpr std::string_view withSubgraphs = R"json(
    {"version":1,"name":"ReferenceSubgraph",
     "subgraphs":[
       {"name":"Fall","inputs":[
          {"name":"Gravity","type":"vector3","default":[0,-9.81,0]},
          {"name":"Drag","type":"float","default":0.25}],
        "outputs":[],
        "update":{"blocks":[
          {"type":"gravity","gravity":{"op":"input","name":"Gravity"}},
          {"type":"drag","drag":{"op":"input","name":"Drag"}},
          {"type":"integrate"}]}}
     ],
     "systems":[{"name":"Main","capacity":64,
       "init":{"blocks":[
         {"type":"set_position_box","center":[0,1,0],"size":[1,1,1]},
         {"type":"set_color","color":[0.375,0.75,1.0]}]},
       "update":{"blocks":[
         {"type":"subgraph","name":"Fall","args":{"Gravity":[0,-4,0]}},
         {"type":"age"}]},
       "outputs":[{}]}]}
    )json";
    constexpr std::string_view flattened = R"json(
    {"version":1,"name":"ReferenceSubgraph",
     "systems":[{"name":"Main","capacity":64,
       "init":{"blocks":[
         {"type":"set_position_box","center":[0,1,0],"size":[1,1,1]},
         {"type":"set_color","color":[0.375,0.75,1.0]}]},
       "update":{"blocks":[
         {"type":"gravity","gravity":[0,-4,0]},
         {"type":"drag","drag":0.25},
         {"type":"integrate"},
         {"type":"age"}]},
       "outputs":[{}]}]}
    )json";
    const vfx::ParseResult nested = vfx::parseGraphJson(withSubgraphs);
    const vfx::ParseResult direct = vfx::parseGraphJson(flattened);
    REQUIRE(nested.ok());
    REQUIRE(direct.ok());
    vfx::reference::ParticleState nestedParticle;
    vfx::reference::ParticleState directParticle;
    std::string nestedError;
    std::string directError;
    REQUIRE(vfx::reference::simulateParticle(*nested.graph, 0u, 0u, 0u, 0.1f, 3u, nestedParticle,
                                             &nestedError));
    REQUIRE(vfx::reference::simulateParticle(*direct.graph, 0u, 0u, 0u, 0.1f, 3u, directParticle,
                                             &directError));
    CHECK(nestedError.empty());
    CHECK(directError.empty());
    CHECK(nestedParticle.position.x == doctest::Approx(directParticle.position.x));
    CHECK(nestedParticle.position.y == doctest::Approx(directParticle.position.y));
    CHECK(nestedParticle.position.z == doctest::Approx(directParticle.position.z));
    CHECK(nestedParticle.velocity.x == doctest::Approx(directParticle.velocity.x));
    CHECK(nestedParticle.velocity.y == doctest::Approx(directParticle.velocity.y));
    CHECK(nestedParticle.velocity.z == doctest::Approx(directParticle.velocity.z));
    CHECK(nestedParticle.color.x == doctest::Approx(directParticle.color.x));
    CHECK(nestedParticle.color.y == doctest::Approx(directParticle.color.y));
    CHECK(nestedParticle.color.z == doctest::Approx(directParticle.color.z));
    CHECK(nestedParticle.age == doctest::Approx(directParticle.age));
    CHECK(nestedParticle.seed == directParticle.seed);
}
