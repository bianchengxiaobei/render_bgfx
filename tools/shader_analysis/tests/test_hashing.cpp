#include "hbg/analysis/hashing.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace hbg::analysis;

TEST_CASE("sha256 matches standard vectors") {
    CHECK(sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
          "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("canonical defines sort and deduplicate") {
    const std::vector<std::string> canonical = canonicalDefines({"B=2", "A=1", "B=2"});
    CHECK(canonical == std::vector<std::string>{"A=1", "B=2"});
    CHECK(joinCanonicalDefines(canonical) == "A=1\nB=2");
}

TEST_CASE("variant id follows the report key formula") {
    CHECK(makeVariantId("shaders/passes/forward.fs.sc", "fragment", "dxbc",
                        {"B=2", "A=1"}) == "27a361396d8d");
    CHECK(makeVariantId("shaders/passes/forward.fs.sc", "fragment", "spirv",
                        {"A=1", "B=2"}) != "27a361396d8d");
}

TEST_CASE("define names and keywords use the name before equals") {
    CHECK(defineName("HAS_NORMAL_MAP=1") == "HAS_NORMAL_MAP");
    CHECK(defineName("HAS_NORMAL_MAP") == "HAS_NORMAL_MAP");
    CHECK(keywordNames({"HAS_NORMAL_MAP=1", "ALPHA_CLIP=1", "HAS_NORMAL_MAP=0"}) ==
          std::vector<std::string>{"ALPHA_CLIP", "HAS_NORMAL_MAP"});
}
