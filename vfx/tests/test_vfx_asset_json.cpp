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

TEST_CASE("vfx asset reports source line and column for diagnostics") {
    constexpr std::string_view json = R"json(
{
  "version": 1,
  "name": "Located",
  "systems": [{
    "name": "Main",
    "capacity": 0,
    "outputs": [{}]
  }]
}
)json";
    const auto result = vfx::parseGraphJson(json, "effect.vfx.json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    const vfx::ParseError& error = result.errors[0];
    CHECK(error.path == "systems[0].capacity");
    CHECK(error.location == vfx::ErrorLocationKind::Json);
    CHECK(error.line == 7u);
    CHECK(error.column == 5u);
    CHECK(error.source == "effect.vfx.json");
    CHECK(vfx::formatParseError(error).find("effect.vfx.json:7:5: systems[0].capacity") !=
          std::string::npos);

    const auto missing = vfx::parseGraphJson(
        R"json({"name":"x","systems":[{"name":"s","capacity":1,"outputs":[{}]}]})json",
        "missing.vfx.json");
    REQUIRE_FALSE(missing.ok());
    REQUIRE_FALSE(missing.errors.empty());
    CHECK(missing.errors[0].path == "version");
    CHECK(missing.errors[0].line == 1u);
    CHECK(missing.errors[0].column == 1u);

    const auto syntax = vfx::parseGraphJson("{\n  \"version\": 1,\n", "syntax.vfx.json");
    REQUIRE_FALSE(syntax.ok());
    REQUIRE_FALSE(syntax.errors.empty());
    CHECK(syntax.errors[0].location == vfx::ErrorLocationKind::Json);
    CHECK(syntax.errors[0].line > 0u);
    CHECK(syntax.errors[0].column > 0u);
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

TEST_CASE("vfx asset flattens pure-function and block subgraphs") {
    const auto result = parse(R"json(
    {
      "version": 1,
      "name": "SubgraphParity",
      "subgraphs": [
        {
          "name": "OffsetColor",
          "inputs": [
            {"name": "Color", "type": "vector3", "default": [0.1, 0.1, 0.1]},
            {"name": "Offset", "type": "vector3", "default": [0.1, 0.2, 0.3]}
          ],
          "outputs": [
            {"name": "Result", "type": "vector3",
             "value": {"op": "add", "a": {"op": "input", "name": "Color"},
                       "b": {"op": "input", "name": "Offset"}}}
          ],
          "init": {"blocks": []},
          "update": {"blocks": []}
        },
        {
          "name": "Fall",
          "inputs": [
            {"name": "Gravity", "type": "vector3", "default": [0, -9.81, 0]},
            {"name": "Drag", "type": "float", "default": 0.2}
          ],
          "outputs": [],
          "update": {"blocks": [
            {"type": "gravity", "gravity": {"op": "input", "name": "Gravity"}},
            {"type": "drag", "drag": {"op": "input", "name": "Drag"}},
            {"type": "integrate"}
          ]}
        }
      ],
      "systems": [{
        "name": "Main",
        "capacity": 64,
        "init": {"blocks": [
          {"type": "set_color", "color": {"op": "subgraph", "name": "OffsetColor",
            "args": {"Color": [0.2, 0.4, 0.6]}}}
        ]},
        "update": {"blocks": [
          {"type": "subgraph", "name": "Fall", "args": {"Gravity": [0, -4, 0]}},
          {"type": "age"}
        ]},
        "outputs": [{"type": "quad", "lit": false, "blend": "alpha"}]
      }]
    }
    )json");
    REQUIRE(result.ok());
    REQUIRE(result.graph != nullptr);
    CHECK(result.graph->subgraphs.empty());
    REQUIRE(result.graph->systems.size() == 1u);
    const vfx::SystemDesc& system = result.graph->systems[0];
    REQUIRE(system.initBlocks.size() == 1u);
    const vfx::Value* color = system.initBlocks[0].parameter("color");
    REQUIRE(color != nullptr);
    REQUIRE(color->kind == vfx::Value::Kind::Expression);
    REQUIRE(color->expression != nullptr);
    CHECK(color->expression->op == "add");
    REQUIRE(color->expression->arguments.size() == 2u);
    const vfx::Value& colorBase = color->expression->arguments[0].second;
    const vfx::Value& colorOffset = color->expression->arguments[1].second;
    REQUIRE(colorBase.kind == vfx::Value::Kind::Array);
    REQUIRE(colorOffset.kind == vfx::Value::Kind::Array);
    CHECK(colorBase.array[0].number == doctest::Approx(0.2));
    CHECK(colorOffset.array[0].number == doctest::Approx(0.1));
    CHECK(colorOffset.array[2].number == doctest::Approx(0.3));

    REQUIRE(system.updateBlocks.size() == 4u);
    CHECK(system.updateBlocks[0].type == "gravity");
    CHECK(system.updateBlocks[1].type == "drag");
    CHECK(system.updateBlocks[2].type == "integrate");
    CHECK(system.updateBlocks[3].type == "age");
    const vfx::Value* gravity = system.updateBlocks[0].parameter("gravity");
    REQUIRE(gravity != nullptr);
    REQUIRE(gravity->kind == vfx::Value::Kind::Array);
    CHECK(gravity->array[1].number == doctest::Approx(-4.0));
    const vfx::Value* drag = system.updateBlocks[1].parameter("drag");
    REQUIRE(drag != nullptr);
    CHECK(drag->number == doctest::Approx(0.2));
}

TEST_CASE("vfx asset reports subgraph cycles and excessive nesting") {
    const auto cycle = parse(R"json(
    {"version":1,"name":"Cycle",
     "subgraphs":[
       {"name":"A","inputs":[],"outputs":[],"update":{"blocks":[
         {"type":"subgraph","name":"B","args":{}}]}},
       {"name":"B","inputs":[],"outputs":[],"update":{"blocks":[
         {"type":"subgraph","name":"A","args":{}}]}}
     ],
     "systems":[{"name":"s","capacity":1,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(cycle.ok());
    REQUIRE_FALSE(cycle.errors.empty());
    CHECK(cycle.errors[0].path == "subgraphs[0]");
    CHECK(cycle.errors[0].message.find("cycle") != std::string::npos);

    std::string deep = R"json({"version":1,"name":"Deep","subgraphs":[)json";
    for (std::uint32_t i = 0; i < 17u; ++i) {
        if (i != 0u) {
            deep += ",";
        }
        deep += "{\"name\":\"S" + std::to_string(i) +
                "\",\"inputs\":[],\"outputs\":[],\"update\":{\"blocks\":[";
        if (i + 1u < 17u) {
            deep += "{\"type\":\"subgraph\",\"name\":\"S" + std::to_string(i + 1u) + "\",\"args\":{}}";
        }
        deep += "]}}";
    }
    deep += R"json(],"systems":[{"name":"s","capacity":1,"outputs":[{}]}]})json";
    const auto tooDeep = parse(deep);
    REQUIRE_FALSE(tooDeep.ok());
    REQUIRE_FALSE(tooDeep.errors.empty());
    CHECK(tooDeep.errors[0].path == "subgraphs[0]");
    CHECK(tooDeep.errors[0].message.find("depth") != std::string::npos);
}

TEST_CASE("vfx asset reports missing subgraph arguments") {
    const auto result = parse(R"json(
    {"version":1,"name":"MissingArg",
     "subgraphs":[
       {"name":"NeedsSpeed","inputs":[{"name":"Speed","type":"float"}],"outputs":[],
        "init":{"blocks":[{"type":"set_size","size":{"op":"input","name":"Speed"}}]}}
     ],
     "systems":[{"name":"s","capacity":1,
       "init":{"blocks":[{"type":"subgraph","name":"NeedsSpeed","args":{}}]},
       "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(result.ok());
    REQUIRE_FALSE(result.errors.empty());
    CHECK(result.errors[0].path == "systems[0].init.blocks[0].args");
    CHECK(result.errors[0].message.find("Speed") != std::string::npos);
}

TEST_CASE("vfx asset expands nested subgraphs and selects outputs") {
    const auto result = parse(R"json(
    {"version":1,"name":"NestedSubgraphs",
     "subgraphs":[
       {"name":"Inner","inputs":[{"name":"Drag","type":"float","default":0.1}],
        "outputs":[{"name":"Value","type":"float","value":{"op":"input","name":"Drag"}}],
        "update":{"blocks":[{"type":"drag","drag":{"op":"input","name":"Drag"}}]}},
       {"name":"Outer","inputs":[{"name":"Drag","type":"float","default":0.2}],
        "outputs":[{"name":"Value","type":"float","value":{"op":"input","name":"Drag"}}],
        "update":{"blocks":[
          {"type":"subgraph","name":"Inner","args":{"Drag":{"op":"input","name":"Drag"}}},
          {"type":"integrate"}]}},
       {"name":"Pair","inputs":[],
        "outputs":[
          {"name":"A","type":"float","value":0.25},
          {"name":"B","type":"float","value":0.5}],
        "init":{"blocks":[]}}
     ],
     "systems":[{"name":"Main","capacity":64,
       "init":{"blocks":[{"type":"set_alpha","alpha":{"op":"subgraph","name":"Pair","output":"B"}}]},
       "update":{"blocks":[
         {"type":"subgraph","name":"Outer","args":{"Drag":0.3}},
         {"type":"age"}]},
       "outputs":[{}]}]}
    )json");
    REQUIRE(result.ok());
    const vfx::SystemDesc& system = result.graph->systems[0];
    REQUIRE(system.initBlocks.size() == 1u);
    const vfx::Value* alpha = system.initBlocks[0].parameter("alpha");
    REQUIRE(alpha != nullptr);
    CHECK(alpha->kind == vfx::Value::Kind::Number);
    CHECK(alpha->number == doctest::Approx(0.5));
    REQUIRE(system.updateBlocks.size() == 3u);
    CHECK(system.updateBlocks[0].type == "drag");
    CHECK(system.updateBlocks[1].type == "integrate");
    CHECK(system.updateBlocks[2].type == "age");
    const vfx::Value* drag = system.updateBlocks[0].parameter("drag");
    REQUIRE(drag != nullptr);
    CHECK(drag->number == doctest::Approx(0.3));
}

TEST_CASE("vfx asset validates custom HLSL blocks") {
    const auto valid = parse(R"json(
    {"version":1,"name":"Custom",
     "properties":[{"name":"Scale","type":"float","default":2.0}],
     "systems":[{"name":"Main","capacity":8,
       "init":{"blocks":[
         {"type":"custom_hlsl","code":"VFX_SET_ATTR(position, float3(0.0, 1.0, 0.0));",
          "writes":["position"]}]},
       "update":{"blocks":[
         {"type":"custom_hlsl",
          "code":"// #define ignored\nVFX_SET_ATTR(position, VFX_ATTR(position) + float3(0.0, VFX_DT, 0.0) * VFX_PROP(Scale));",
          "reads":["position"],"writes":["position"]},
         {"type":"age"}]},
       "outputs":[{}]}]}
    )json");
    REQUIRE(valid.ok());
    REQUIRE(valid.graph->systems.size() == 1u);
    const vfx::SystemDesc& system = valid.graph->systems[0];
    REQUIRE(system.initBlocks.size() == 1u);
    REQUIRE(system.updateBlocks.size() == 2u);
    CHECK(system.initBlocks[0].type == "custom_hlsl");
    CHECK(system.initBlocks[0].parameter("code")->string.find("VFX_SET_ATTR") != std::string::npos);
    CHECK(system.updateBlocks[0].parameter("reads")->array.size() == 1u);

    const auto preprocessor = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl","code":"#define X 1","writes":[]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(preprocessor.ok());
    CHECK(preprocessor.errors[0].path == "systems[0].update.blocks[0].code");
    CHECK(preprocessor.errors[0].location == vfx::ErrorLocationKind::Hlsl);
    CHECK(preprocessor.errors[0].line == 1u);
    CHECK(preprocessor.errors[0].column == 1u);
    CHECK(preprocessor.errors[0].message.find("preprocessor") != std::string::npos);

    const auto undeclaredRead = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl",
        "code":"VFX_SET_ATTR(position, VFX_ATTR(velocity));",
        "reads":["position"],"writes":["position"]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(undeclaredRead.ok());
    CHECK(undeclaredRead.errors[0].message.find("declared in reads") != std::string::npos);

    const auto undeclaredWrite = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl",
        "code":"VFX_SET_ATTR(alive, false);","writes":["position"]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(undeclaredWrite.ok());
    CHECK(undeclaredWrite.errors[0].message.find("declared in writes") != std::string::npos);

    const auto directAccess = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl","code":"a_alive = false;","writes":["alive"]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(directAccess.ok());
    CHECK(directAccess.errors[0].message.find("direct attribute") != std::string::npos);

    const auto missingProperty = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl",
        "code":"VFX_SET_ATTR(position, float3(VFX_PROP(Missing), 0.0, 0.0));","writes":["position"]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(missingProperty.ok());
    CHECK(missingProperty.errors[0].message.find("unknown property") != std::string::npos);

    const auto readOnlyMacro = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl",
        "code":"VFX_ATTR(position) += float3(0.0, 1.0, 0.0);","reads":["position"],"writes":["position"]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(readOnlyMacro.ok());
    CHECK(readOnlyMacro.errors[0].message.find("VFX_SET_ATTR") != std::string::npos);

    const auto swizzleWrite = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl",
        "code":"(VFX_ATTR(position)).x = 1.0;","reads":["position"],"writes":["position"]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(swizzleWrite.ok());
    CHECK(swizzleWrite.errors[0].message.find("VFX_SET_ATTR") != std::string::npos);

    const auto unknownAttribute = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl","code":"VFX_PROP(Scale);","reads":["missing"]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(unknownAttribute.ok());
    CHECK(unknownAttribute.errors[0].path.find(".reads[0]") != std::string::npos);
    CHECK(unknownAttribute.errors[0].message.find("unknown attribute") != std::string::npos);

    const auto lifetime = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl","code":"float x = VFX_LIFETIME_RATIO;"}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(lifetime.ok());
    CHECK(lifetime.errors[0].message.find("requires reads age and lifetime") != std::string::npos);

    const auto parseCode = [](std::string_view code) {
        std::string json = R"json(
        {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
          "update":{"blocks":[{"type":"custom_hlsl","code":")json";
        json += code;
        json += R"json("}]},"outputs":[{}]}]}
        )json";
        return parse(json);
    };
    for (const std::string_view code : {"uniform float x;", "#define X 1", "Buffer<float> values;",
                                        "StructuredBuffer<float> values;", "return;"}) {
        const auto result = parseCode(code);
        REQUIRE_FALSE(result.ok());
        CHECK(result.errors[0].path == "systems[0].update.blocks[0].code");
        CHECK(result.errors[0].location == vfx::ErrorLocationKind::Hlsl);
        CHECK(result.errors[0].line == 1u);
        CHECK(result.errors[0].column == 1u);
    }
}

TEST_CASE("vfx asset validates lit material properties") {
    const auto valid = parse(R"json(
    {"version":1,"name":"Lit",
     "properties":[{"name":"Metallic","type":"float","default":0.25},
                   {"name":"Roughness","type":"float","default":0.4}],
     "systems":[{"name":"s","capacity":1,
       "outputs":[{"type":"quad","lit":true,"metallic":"Metallic","roughness":0.6}]}]}
    )json");
    REQUIRE(valid.ok());
    REQUIRE(valid.graph->systems.size() == 1u);
    REQUIRE(valid.graph->systems[0].outputs.size() == 1u);
    CHECK(valid.graph->systems[0].outputs[0].lit);
    CHECK(valid.graph->systems[0].outputs[0].metallicProperty == "Metallic");
    CHECK(valid.graph->systems[0].outputs[0].roughnessProperty.empty());
    CHECK(valid.graph->systems[0].outputs[0].roughness == doctest::Approx(0.6));

    const auto unknown = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "outputs":[{"type":"quad","lit":true,"metallic":"Missing"}]}]}
    )json");
    REQUIRE_FALSE(unknown.ok());
    CHECK(unknown.errors[0].path == "systems[0].outputs[0].metallic");
    CHECK(unknown.errors[0].message.find("unknown property") != std::string::npos);

    const auto wrongType = parse(R"json(
    {"version":1,"name":"Bad",
     "properties":[{"name":"Tint","type":"color","default":[1,1,1,1]}],
     "systems":[{"name":"s","capacity":1,
       "outputs":[{"type":"quad","lit":true,"roughness":"Tint"}]}]}
    )json");
    REQUIRE_FALSE(wrongType.ok());
    CHECK(wrongType.errors[0].path == "systems[0].outputs[0].roughness");
    CHECK(wrongType.errors[0].message.find("must be a float") != std::string::npos);

    const auto outOfRange = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "outputs":[{"type":"quad","lit":true,"metallic":2.0}]}]}
    )json");
    REQUIRE_FALSE(outOfRange.ok());
    CHECK(outOfRange.errors[0].path == "systems[0].outputs[0].metallic");
    CHECK(outOfRange.errors[0].message.find("between 0 and 1") != std::string::npos);
}

TEST_CASE("vfx asset validates soft particle output") {
    const auto valid = parse(R"json(
    {"version":1,"name":"Soft","systems":[{"name":"s","capacity":1,
      "outputs":[{"type":"quad","softParticles":true,"softDistance":0.75}]}]}
    )json");
    REQUIRE(valid.ok());
    REQUIRE(valid.graph->systems.size() == 1u);
    REQUIRE(valid.graph->systems[0].outputs.size() == 1u);
    CHECK(valid.graph->systems[0].outputs[0].softParticles);
    CHECK(valid.graph->systems[0].outputs[0].softDistance == doctest::Approx(0.75f));

    const auto defaults = parse(R"json(
    {"version":1,"name":"Soft","systems":[{"name":"s","capacity":1,"outputs":[{}]}]}
    )json");
    REQUIRE(defaults.ok());
    CHECK_FALSE(defaults.graph->systems[0].outputs[0].softParticles);
    CHECK(defaults.graph->systems[0].outputs[0].softDistance == doctest::Approx(0.1f));

    const auto tooSmall = parse(R"json(
    {"version":1,"name":"Soft","systems":[{"name":"s","capacity":1,
      "outputs":[{"softParticles":true,"softDistance":0.0}]}]}
    )json");
    REQUIRE_FALSE(tooSmall.ok());
    CHECK(tooSmall.errors[0].path == "systems[0].outputs[0].softDistance");
    CHECK(tooSmall.errors[0].message.find("at least 0.001") != std::string::npos);

    const auto wrongType = parse(R"json(
    {"version":1,"name":"Soft","systems":[{"name":"s","capacity":1,
      "outputs":[{"softDistance":true}]}]}
    )json");
    REQUIRE_FALSE(wrongType.ok());
    CHECK(wrongType.errors[0].path == "systems[0].outputs[0].softDistance");
}

TEST_CASE("vfx asset validates subgraph declarations") {
    const auto unsupportedType = parse(R"json(
    {"version":1,"name":"Bad",
     "subgraphs":[{"name":"Unsupported","inputs":[{"name":"Value","type":"matrix"}],
                   "outputs":[]}],
     "systems":[{"name":"s","capacity":1,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(unsupportedType.ok());
    CHECK(unsupportedType.errors[0].path == "subgraphs[0].inputs[0].type");
    CHECK(unsupportedType.errors[0].message.find("unknown subgraph input type") !=
          std::string::npos);

    const auto duplicateInput = parse(R"json(
    {"version":1,"name":"Bad",
     "subgraphs":[{"name":"Duplicate",
                   "inputs":[{"name":"Value","type":"float"},
                             {"name":"Value","type":"float"}],
                   "outputs":[]}],
     "systems":[{"name":"s","capacity":1,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(duplicateInput.ok());
    CHECK(duplicateInput.errors[0].path == "subgraphs[0].inputs[1].name");
    CHECK(duplicateInput.errors[0].message.find("duplicate input") != std::string::npos);

    const auto duplicateOutput = parse(R"json(
    {"version":1,"name":"Bad",
     "subgraphs":[{"name":"Duplicate",
                   "inputs":[],
                   "outputs":[{"name":"Value","type":"float","value":0.0},
                              {"name":"Value","type":"float","value":1.0}]}],
     "systems":[{"name":"s","capacity":1,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(duplicateOutput.ok());
    CHECK(duplicateOutput.errors[0].path == "subgraphs[0].outputs[1].name");
    CHECK(duplicateOutput.errors[0].message.find("duplicate output") != std::string::npos);

    const auto missingValue = parse(R"json(
    {"version":1,"name":"Bad",
     "subgraphs":[{"name":"Missing","inputs":[],
                   "outputs":[{"name":"Value","type":"float"}]}],
     "systems":[{"name":"s","capacity":1,"outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(missingValue.ok());
    CHECK(missingValue.errors[0].path == "subgraphs[0].outputs[0].value");
    CHECK(missingValue.errors[0].message.find("required") != std::string::npos);
}

TEST_CASE("vfx asset validates custom HLSL field types") {
    const auto missingCode = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl","writes":[]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(missingCode.ok());
    CHECK(missingCode.errors[0].path == "systems[0].update.blocks[0].code");
    CHECK(missingCode.errors[0].message.find("required") != std::string::npos);

    const auto nonStringCode = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl","code":7,"writes":[]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(nonStringCode.ok());
    CHECK(nonStringCode.errors[0].path == "systems[0].update.blocks[0].code");
    CHECK(nonStringCode.errors[0].message.find("string") != std::string::npos);

    const auto readsNotArray = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl","code":"VFX_ATTR(position);","reads":"position"}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(readsNotArray.ok());
    CHECK(readsNotArray.errors[0].path == "systems[0].update.blocks[0].reads");
    CHECK(readsNotArray.errors[0].message.find("array") != std::string::npos);

    const auto nonStringWrite = parse(R"json(
    {"version":1,"name":"Bad","systems":[{"name":"s","capacity":1,
      "update":{"blocks":[{"type":"custom_hlsl","code":"VFX_SET_ATTR(position, 0.0);",
                            "writes":[7]}]},
      "outputs":[{}]}]}
    )json");
    REQUIRE_FALSE(nonStringWrite.ok());
    CHECK(nonStringWrite.errors[0].path == "systems[0].update.blocks[0].writes[0]");
    CHECK(nonStringWrite.errors[0].message.find("string") != std::string::npos);
}
