#include "hbg/analysis/spirv_blob.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <vector>

#ifndef HBG_ANALYSIS_TEST_ROOT
#define HBG_ANALYSIS_TEST_ROOT "."
#endif

using namespace hbg::analysis;
namespace fs = std::filesystem;

TEST_CASE("real bgfx SPIR-V fixture extracts a validated blob") {
    const fs::path fixture =
        fs::path(HBG_ANALYSIS_TEST_ROOT) / "tools" / "shader_analysis" / "tests" /
        "fixtures" / "forward.vs.bin";
    REQUIRE(fs::is_regular_file(fixture));
    const SpirvBlob blob = extractSpirvBlob(fixture);
    REQUIRE(blob.bytes.size() >= 4);
    REQUIRE(blob.declaredSize == blob.bytes.size());
    CHECK(blob.offset > 0);
    CHECK(blob.bytes[0] == 0x03);
    CHECK(blob.bytes[1] == 0x02);
    CHECK(blob.bytes[2] == 0x23);
    CHECK(blob.bytes[3] == 0x07);
    CHECK(blob.attributeCount == blob.attributes.size());
}

TEST_CASE("SPIR-V extraction rejects data without a valid header") {
    const std::vector<std::uint8_t> data(64, 0x5a);
    CHECK_THROWS_AS(extractSpirvBlob(data), std::runtime_error);
}
