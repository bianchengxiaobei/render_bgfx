#include <doctest/doctest.h>

#include "vfx/asset.h"
#include "vfx/attribute_layout.h"
#include "vfx/graph.h"

#include <string>
#include <string_view>

namespace {

constexpr std::string_view kValidAsset = R"json(
{
  "version": 1,
  "name": "Sparks",
  "properties": [
    {"name": "Tint", "type": "color", "default": [1.0, 0.8, 0.4, 1.0]},
    {"name": "MainTex", "type": "texture", "default": "textures/spark.png"},
    {"name": "SizeCurve", "type": "curve", "keys": [[1.0, 0.0], [0.0, 1.0], [0.5, 0.4]]},
    {"name": "ColorRamp", "type": "gradient", "keys": [
      {"t": 1.0, "color": [1.0, 0.0, 0.0, 0.0]},
      {"t": 0.0, "color": [1.0, 1.0, 1.0, 1.0]}
    ]}
  ],
  "attributes": [
    {"name": "user0", "type": "float4", "default": [1.0, 2.0, 3.0, 4.0]}
  ],
  "systems": [
    {
      "name": "Main",
      "capacity": 100,
      "space": "world",
      "bounds": {"mode": "manual", "center": [0, 1, 0], "extents": [2, 3, 4]},
      "sort": {"mode": "distance", "order": "descending"},
      "spawner": {
        "rate": 25.0,
        "bursts": [
          {"time": 0.0, "count": 10},
          {"time": 0.0, "count": 3, "period": 0.5, "repeat": 2}
        ]
      },
      "init": {"blocks": [
        {"type": "set_position_sphere", "center": [0, 1, 0], "radius": 2.0, "surface": true},
        {"type": "set_alpha", "alpha": {"op": "lifetime_ratio"}}
      ]},
      "update": {"blocks": [
        {"type": "integrate"},
        {"type": "age"},
        {"type": "gravity", "gravity": [0, -9.81, 0]},
        {"type": "reap"}
      ]},
      "outputs": [
        {"type": "quad", "lit": false, "blend": "additive", "texture": "MainTex", "sort": "distance"}
      ]
    }
  ]
}
)json";

vfx::ParseResult parse(std::string_view json) {
    return vfx::parseGraphJson(json);
}

} // namespace

TEST_CASE("vfx asset parses a complete graph") {
    const auto result = parse(kValidAsset);
    REQUIRE(result.ok());
    REQUIRE(result.graph != nullptr);

    const vfx::Graph& graph = *result.graph;
    CHECK(graph.version == 1);
    CHECK(graph.name == "Sparks");
    REQUIRE(graph.properties.size() == 4);
    CHECK(graph.properties[0].name == "Tint");
    CHECK(static_cast<int>(graph.properties[0].type) == static_cast<int>(vfx::PropertyType::Color));
    CHECK(static_cast<int>(graph.properties[1].type) == static_cast<int>(vfx::PropertyType::Texture));
    CHECK(graph.properties[2].curve.size() == 3);
    CHECK(graph.properties[2].curve.front().time == doctest::Approx(0.0f));
    CHECK(graph.properties[2].curve.back().time == doctest::Approx(1.0f));
    CHECK(graph.properties[3].gradient.size() == 2);
    CHECK(graph.properties[3].gradient.front().time == doctest::Approx(0.0f));
    CHECK(graph.attributes.size() == 1);
    CHECK(graph.attributes[0].name == "user0");

    REQUIRE(graph.systems.size() == 1);
    const vfx::SystemDesc& system = graph.systems[0];
    CHECK(system.requestedCapacity == 100);
    CHECK(system.capacity == 128);
    CHECK(static_cast<int>(system.space) == static_cast<int>(vfx::SystemSpace::World));
    CHECK(system.bounds.center.y == doctest::Approx(1.0f));
    CHECK(system.bounds.extents.z == doctest::Approx(4.0f));
    CHECK(static_cast<int>(system.sort.mode) == static_cast<int>(vfx::SortMode::Distance));
    CHECK(static_cast<int>(system.sort.order) == static_cast<int>(vfx::SortOrder::Descending));
    CHECK(system.spawner.rate == doctest::Approx(25.0f));
    REQUIRE(system.spawner.bursts.size() == 2);
    CHECK(system.spawner.bursts[0].count == 10);
    CHECK_FALSE(system.spawner.bursts[0].period.has_value());
    CHECK(system.spawner.bursts[1].period.value_or(0.0f) == doctest::Approx(0.5f));
    CHECK(system.spawner.bursts[1].repeat.value_or(0u) == 2u);
    REQUIRE(system.initBlocks.size() == 2);
    CHECK(system.initBlocks[0].type == "set_position_sphere");
    CHECK(system.initBlocks[1].parameters.find("alpha") != nullptr);
    CHECK(system.initBlocks[1].parameter("alpha")->isExpression());
    CHECK(system.initBlocks[1].parameter("alpha")->expression->op == "lifetime_ratio");
    REQUIRE(system.updateBlocks.size() == 4);
    CHECK(system.updateBlocks[2].parameter("gravity")->array.size() == 3);
    REQUIRE(system.outputs.size() == 1);
    CHECK(static_cast<int>(system.outputs[0].type) == static_cast<int>(vfx::OutputType::Quad));
    CHECK(static_cast<int>(system.outputs[0].blend) == static_cast<int>(vfx::BlendMode::Additive));
    CHECK(system.outputs[0].texture == "MainTex");
    CHECK(system.outputs[0].sort.has_value());
    CHECK(static_cast<int>(system.outputs[0].sort->mode) == static_cast<int>(vfx::SortMode::Distance));
}

TEST_CASE("vfx asset supplies defaults and aligns capacity") {
    constexpr std::string_view json = R"json(
    {
      "version": 1,
      "name": "Defaults",
      "systems": [{
        "name": "Main",
        "capacity": 1,
        "outputs": [{"lit": false}]
      }]
    }
    )json";
    const auto result = parse(json);
    REQUIRE(result.ok());
    const vfx::SystemDesc& system = result.graph->systems[0];
    CHECK(system.requestedCapacity == 1);
    CHECK(system.capacity == 64);
    CHECK(static_cast<int>(system.space) == static_cast<int>(vfx::SystemSpace::World));
    CHECK(system.bounds.center.x == 0.0f);
    CHECK(system.bounds.extents.x == 1.0f);
    CHECK(static_cast<int>(system.sort.mode) == static_cast<int>(vfx::SortMode::None));
    CHECK(system.spawner.rate == 0.0f);
    CHECK(system.initBlocks.empty());
    CHECK(system.updateBlocks.empty());
    CHECK(static_cast<int>(system.outputs[0].blend) == static_cast<int>(vfx::BlendMode::Alpha));
    CHECK_FALSE(system.outputs[0].sort.has_value());
}

TEST_CASE("vfx asset hash is deterministic") {
    const auto first = parse(kValidAsset);
    const auto second = parse(kValidAsset);
    REQUIRE(first.ok());
    REQUIRE(second.ok());
    CHECK(vfx::graphHash(*first.graph) == vfx::graphHash(*second.graph));

    CHECK(first.graph->properties[0].name == second.graph->properties[0].name);
    CHECK(first.graph->systems[0].initBlocks[0].type == second.graph->systems[0].initBlocks[0].type);
}

TEST_CASE("vfx asset rejects malformed roots and versions") {
    const auto notObject = parse("[]");
    REQUIRE_FALSE(notObject.ok());
    REQUIRE_FALSE(notObject.errors.empty());
    CHECK(notObject.errors[0].path == "$");

    const auto missingVersion = parse(R"json({"name":"x","systems":[{"name":"s","capacity":1,"outputs":[{}]}]})json");
    REQUIRE_FALSE(missingVersion.ok());
    REQUIRE_FALSE(missingVersion.errors.empty());
    CHECK(missingVersion.errors[0].path == "version");

    const auto badVersion = parse(R"json({"version":2,"name":"x","systems":[{"name":"s","capacity":1,"outputs":[{}]}]})json");
    REQUIRE_FALSE(badVersion.ok());
    REQUIRE_FALSE(badVersion.errors.empty());
    CHECK(badVersion.errors[0].path == "version");
}

TEST_CASE("vfx asset reports property paths") {
    constexpr std::string_view json = R"json(
    {
      "version": 1,
      "name": "BadProperty",
      "properties": [{"name": "Tint", "type": "color", "default": [1, 0, 0]}],
      "systems": [{"name": "Main", "capacity": 1, "outputs": [{}]}]
    }
    )json";
    const auto result = parse(json);
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "properties[0].default");
}

TEST_CASE("vfx asset rejects duplicate and reserved property names") {
    const auto duplicate = parse(R"json(
    {"version":1,"name":"x",
     "properties":[{"name":"A","type":"float","default":0},{"name":"A","type":"float","default":1}],
     "systems":[{"name":"s","capacity":1,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(duplicate.ok());
    REQUIRE_FALSE(duplicate.errors.empty());
    CHECK(duplicate.errors[0].path == "properties[1].name");

    const auto reserved = parse(R"json(
    {"version":1,"name":"x",
     "properties":[{"name":"position","type":"float","default":0}],
     "systems":[{"name":"s","capacity":1,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(reserved.ok());
    REQUIRE_FALSE(reserved.errors.empty());
    CHECK(reserved.errors[0].path == "properties[0].name");
}

TEST_CASE("vfx asset reports block type path") {
    const auto result = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":1,
       "update":{"blocks":[
         {"type":"integrate"},
         {"type":"age"},
         {"type":"gravity","gravity":[0,-9.8,0]},
         {"type":"bogus"}
       ]},
       "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "systems[0].update.blocks[3].type");
}

TEST_CASE("vfx asset reports block field paths") {
    const auto result = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":1,
       "init":{"blocks":[{"type":"set_alpha","alpha":1.0,"bogus":2}]},
       "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "systems[0].init.blocks[0].bogus");
}

TEST_CASE("vfx asset rejects unknown expressions") {
    const auto result = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":1,
       "init":{"blocks":[{"type":"set_alpha","alpha":{"op":"not_an_op"}}]},
       "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "systems[0].init.blocks[0].alpha.op");
}

TEST_CASE("vfx asset validates expression references and types") {
    const auto unknownAttribute = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":1,
       "init":{"blocks":[{"type":"set_alpha","alpha":{"op":"attr","name":"missing"}}]},
       "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(unknownAttribute.ok());
    REQUIRE_FALSE(unknownAttribute.errors.empty());
    CHECK(unknownAttribute.errors[0].path == "systems[0].init.blocks[0].alpha.name");

    const auto mixedTypes = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":1,
       "init":{"blocks":[{"type":"set_alpha","alpha":{"op":"add","a":{"op":"attr","name":"alive"},"b":1}}]},
       "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(mixedTypes.ok());
    REQUIRE_FALSE(mixedTypes.errors.empty());
    CHECK(mixedTypes.errors[0].path == "systems[0].init.blocks[0].alpha.b");
}

TEST_CASE("vfx asset rejects invalid capacities and sort sizes") {
    const auto zero = parse(R"json(
    {"version":1,"name":"x","systems":[{"name":"s","capacity":0,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(zero.ok());
    CHECK(zero.errors[0].path == "systems[0].capacity");

    const auto tooMany = parse(R"json(
    {"version":1,"name":"x","systems":[{"name":"s","capacity":1048577,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(tooMany.ok());
    CHECK(tooMany.errors[0].path == "systems[0].capacity");

    const auto sorted = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":262145,"sort":{"mode":"distance"},"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(sorted.ok());
    CHECK(sorted.errors[0].path == "systems[0].capacity");
}

TEST_CASE("vfx asset requires output sort when system sorting is enabled") {
    const auto result = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":1,"sort":{"mode":"distance"},"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "systems[0].outputs[0].sort");
}

TEST_CASE("vfx asset rejects invalid burst fields") {
    const auto result = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":1,
       "spawner":{"bursts":[{"time":0,"count":1,"repeat":2}]},
       "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "systems[0].spawner.bursts[0].repeat");
}

TEST_CASE("vfx asset rejects unknown output texture properties") {
    const auto result = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":1,"outputs":[{"texture":"Missing"}]}]}
    )json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "systems[0].outputs[0].texture");
}

TEST_CASE("vfx asset rejects invalid collision modes") {
    const auto result = parse(R"json(
    {"version":1,"name":"x",
     "systems":[{"name":"s","capacity":1,
       "update":{"blocks":[{"type":"collision_plane","normal":[0,1,0],"distance":0,"mode":"explode"}]},
       "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "systems[0].update.blocks[0].mode");
}

TEST_CASE("vfx asset rejects duplicate curve keys") {
    const auto result = parse(R"json(
    {"version":1,"name":"x",
     "properties":[{"name":"C","type":"curve","keys":[[0,0],[0,1]]}],
     "systems":[{"name":"s","capacity":1,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "properties[0].keys");
}
