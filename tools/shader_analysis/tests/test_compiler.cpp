#include "hbg/analysis/compiler.h"

#include <doctest/doctest.h>

#include <string>

using namespace hbg::analysis;

TEST_CASE("shaderc diagnostics are split into warnings and errors") {
    const ShadercLogs logs = parseShadercLogs(
        "shaders/a.fs.sc(12): warning X4000: unused value\n"
        "shaders/a.fs.sc(13): error X3000: syntax error\n",
        "", false);
    REQUIRE(logs.warnings.size() == 1);
    REQUIRE(logs.errors.size() == 1);
    CHECK(logs.warnings[0].message.find("warning X4000") != std::string::npos);
    REQUIRE(logs.warnings[0].stacktrace.size() == 1);
    CHECK(logs.warnings[0].stacktrace[0] == "shaders/a.fs.sc:12");
    CHECK(logs.errors[0].message.find("error X3000") != std::string::npos);
}

TEST_CASE("unparsed failure output is preserved as an error") {
    const ShadercLogs logs =
        parseShadercLogs("shader compiler failed\nsecond line\n", "", false);
    REQUIRE(logs.errors.size() == 2);
    CHECK(logs.errors[0].message.find("shader compiler failed") != std::string::npos);
    CHECK(logs.errors[1].message.find("second line") != std::string::npos);
}

TEST_CASE("stderr diagnostics are parsed as well") {
    const ShadercLogs logs =
        parseShadercLogs("", "shaders/a.fs.sc(7): error: bad input\n", false);
    REQUIRE(logs.errors.size() == 1);
    CHECK(logs.errors[0].message.find("bad input") != std::string::npos);
}

TEST_CASE("successful output without diagnostics is ignored") {
    const ShadercLogs logs =
        parseShadercLogs("compiled successfully\n", "", true);
    CHECK(logs.warnings.empty());
    CHECK(logs.errors.empty());
}
