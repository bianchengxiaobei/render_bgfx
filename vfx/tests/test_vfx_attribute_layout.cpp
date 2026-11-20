#include <doctest/doctest.h>

#include "vfx/asset.h"
#include "vfx/attribute_layout.h"
#include "vfx/graph.h"

#include <array>
#include <bit>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

vfx::Graph makeGraph() {
    vfx::Graph graph;
    graph.name = "Layout";
    graph.attributes.push_back(vfx::AttributeDesc{
        "user0", vfx::AttributeType::Float4, {1.0f, 2.0f, 3.0f, 4.0f}, {}});
    return graph;
}

} // namespace

TEST_CASE("vfx capacity aligns to 64 particles") {
    CHECK(vfx::alignAttributeCapacity(0) == 64u);
    CHECK(vfx::alignAttributeCapacity(1) == 64u);
    CHECK(vfx::alignAttributeCapacity(63) == 64u);
    CHECK(vfx::alignAttributeCapacity(64) == 64u);
    CHECK(vfx::alignAttributeCapacity(65) == 128u);
    CHECK(vfx::alignAttributeCapacity(100) == 128u);
    CHECK(vfx::alignAttributeCapacity(262145) == 262208u);
}

TEST_CASE("vfx default layout follows built-in order") {
    const vfx::AttributeSet set = vfx::defaultAttributeSet(makeGraph());
    REQUIRE(set.attributes.size() == 16);
    CHECK(set.attributes.front().name == "position");
    CHECK(set.attributes[7].name == "seed");
    CHECK(set.attributes[10].name == "alive");
    CHECK(set.attributes[14].name == "oldPosition");
    CHECK(set.attributes.back().name == "user0");

    const vfx::AttributeLayout layout = vfx::buildAttributeLayout(set, 100);
    CHECK(layout.requestedCapacity == 100u);
    CHECK(layout.capacity == 128u);
    CHECK(layout.bucketOffsetWords == 0u);
    CHECK(layout.strideWords == 31u);
    CHECK(layout.bufferSizeWords() == 128u * 31u);
    CHECK(layout.bufferSizeBytes() == 128u * 31u * 4u);

    const vfx::AttributeLayoutEntry* position = layout.find("position");
    const vfx::AttributeLayoutEntry* color = layout.find("color");
    const vfx::AttributeLayoutEntry* alive = layout.find("alive");
    const vfx::AttributeLayoutEntry* user0 = layout.find("user0");
    REQUIRE(position != nullptr);
    REQUIRE(color != nullptr);
    REQUIRE(alive != nullptr);
    REQUIRE(user0 != nullptr);
    CHECK(position->offsetWords == 0u);
    CHECK(color->offsetWords == 6u);
    CHECK(alive->offsetWords == 16u);
    CHECK(user0->offsetWords == 27u);
    CHECK(layout.elementWordOffset(*user0, 3) == 27u + 3u * 31u);
}

TEST_CASE("vfx attribute defaults use raw word layout") {
    const vfx::AttributeDesc* color = vfx::findBuiltInAttribute("color");
    const vfx::AttributeDesc* size = vfx::findBuiltInAttribute("size");
    const vfx::AttributeDesc* alive = vfx::findBuiltInAttribute("alive");
    REQUIRE(color != nullptr);
    REQUIRE(size != nullptr);
    REQUIRE(alive != nullptr);

    const vfx::AttributeWords colorWords = vfx::attributeDefaultWords(*color);
    CHECK(colorWords[0] == std::bit_cast<std::uint32_t>(1.0f));
    CHECK(colorWords[1] == std::bit_cast<std::uint32_t>(1.0f));
    CHECK(colorWords[2] == std::bit_cast<std::uint32_t>(1.0f));
    CHECK(colorWords[3] == 0u);

    const vfx::AttributeWords sizeWords = vfx::attributeDefaultWords(*size);
    CHECK(sizeWords[0] == std::bit_cast<std::uint32_t>(0.1f));
    const vfx::AttributeWords aliveWords = vfx::attributeDefaultWords(*alive);
    CHECK(aliveWords[0] == 1u);
}

TEST_CASE("vfx attribute CPU read/write checks bounds") {
    const vfx::AttributeSet set = vfx::defaultAttributeSet(makeGraph());
    const vfx::AttributeLayout layout = vfx::buildAttributeLayout(set, 64);
    const vfx::AttributeLayoutEntry* position = layout.find("position");
    const vfx::AttributeLayoutEntry* user0 = layout.find("user0");
    REQUIRE(position != nullptr);
    REQUIRE(user0 != nullptr);

    std::vector<std::uint32_t> buffer(layout.bufferSizeWords(), 0u);
    const vfx::AttributeWords value{
        std::bit_cast<std::uint32_t>(1.5f),
        std::bit_cast<std::uint32_t>(-2.0f),
        std::bit_cast<std::uint32_t>(3.25f),
        0u,
    };
    CHECK(vfx::writeAttribute(layout, *position, buffer, 63u, value));

    vfx::AttributeWords read{};
    CHECK(vfx::readAttribute(layout, *position, buffer, 63u, read));
    CHECK(read[0] == value[0]);
    CHECK(read[1] == value[1]);
    CHECK(read[2] == value[2]);

    CHECK_FALSE(vfx::readAttribute(layout, *position, buffer, 64u, read));
    CHECK_FALSE(vfx::writeAttribute(layout, *user0, buffer, 64u, value));
}

TEST_CASE("vfx generated attribute shader matches the checked-in fixture") {
    const std::string fixture = std::string(VFX_SOURCE_DIR) + "/tests/data/attributes_default.vfx.json";
    const vfx::ParseResult parsed = vfx::loadGraphFile(fixture);
    REQUIRE(parsed.ok());
    REQUIRE_FALSE(parsed.graph->systems.empty());

    const vfx::AttributeSet set = vfx::defaultAttributeSet(*parsed.graph);
    const vfx::AttributeLayout layout =
        vfx::buildAttributeLayout(set, parsed.graph->systems[0].requestedCapacity);
    const std::string expected = vfx::generateAttributeShader(layout);

    const std::string generatedPath = std::string(VFX_SOURCE_DIR) + "/../shaders/vfx/vfx_attributes.sc";
    std::ifstream generatedFile(generatedPath, std::ios::binary);
    REQUIRE(generatedFile.good());
    const std::string actual((std::istreambuf_iterator<char>(generatedFile)),
                             std::istreambuf_iterator<char>());
    CHECK(actual == expected);
}

TEST_CASE("vfx attribute codegen is deterministic and contains raw IO") {
    const vfx::AttributeSet set = vfx::defaultAttributeSet(makeGraph());
    const vfx::AttributeLayout layout = vfx::buildAttributeLayout(set, 100);
    const std::string first = vfx::generateAttributeShader(layout);
    const std::string second = vfx::generateAttributeShader(layout);
    CHECK(first == second);
    CHECK(first.find("#define VFX_ATTRIBUTE_STRIDE 31u") != std::string::npos);
    CHECK(first.find("#define VFX_ATTRIBUTE_STRIDE_BYTES 124u") != std::string::npos);
    CHECK(first.find("#define VFX_ATTRIBUTE_CAPACITY 128u") != std::string::npos);
    CHECK(first.find("#define VFX_ATTRIBUTE_OFFSET_position 0u") != std::string::npos);
    CHECK(first.find("#define VFX_ATTRIBUTE_BYTE_OFFSET_user0 108u") != std::string::npos);
    CHECK(first.find("BUFFER_RAW_RW(s_vfxAttributes, VFX_ATTRIBUTE_BUFFER_STAGE)") !=
          std::string::npos);
    CHECK(first.find("BUFFER_RAW_RO(s_vfxAttributes, VFX_ATTRIBUTE_BUFFER_STAGE)") !=
          std::string::npos);
    CHECK(first.find("Load_position()") != std::string::npos);
    CHECK(first.find("Store_alive(bool value)") != std::string::npos);
    CHECK(first.find("rawLoadFloat") != std::string::npos);
    CHECK(first.find("rawStoreFloat") != std::string::npos);
    CHECK(first.find("rawLoadUint") != std::string::npos);
    CHECK(first.find("rawStoreUint") != std::string::npos);
}
