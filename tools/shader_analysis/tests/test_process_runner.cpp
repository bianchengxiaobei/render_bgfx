#include "hbg/analysis/process_runner.h"

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <thread>

#ifndef HBG_ANALYSIS_FAKE_COMPILER
#define HBG_ANALYSIS_FAKE_COMPILER ""
#endif

using namespace hbg::analysis;
namespace fs = std::filesystem;

namespace {

fs::path fakeCompiler() {
    const fs::path path(HBG_ANALYSIS_FAKE_COMPILER);
    if (!fs::is_regular_file(path)) {
        throw std::runtime_error("fake compiler is missing: " + path.string());
    }
    return path;
}

} // namespace

TEST_CASE("process runner captures stdout stderr and exit code") {
    const ProcessResult result =
        runProcess(fakeCompiler(), {"--stdout", "hello", "--stderr", "warning", "--exit", "3"},
                   fs::current_path(), 5000);
    CHECK(result.launched);
    CHECK_FALSE(result.timedOut);
    CHECK_FALSE(result.cancelled);
    CHECK(result.exitCode == 3);
    CHECK(result.stdoutText.find("hello") != std::string::npos);
    CHECK(result.stderrText.find("warning") != std::string::npos);
}

TEST_CASE("process runner preserves arguments containing spaces") {
    const ProcessResult result =
        runProcess(fakeCompiler(), {"--echo-arguments", "two words"},
                   fs::current_path(), 5000);
    CHECK(result.launched);
    CHECK(result.exitCode == 0);
    CHECK(result.stdoutText.find("two words") != std::string::npos);
}

TEST_CASE("process runner terminates on timeout") {
    const auto started = std::chrono::steady_clock::now();
    const ProcessResult result =
        runProcess(fakeCompiler(), {"--sleep-ms", "5000"}, fs::current_path(), 150);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    CHECK(result.launched);
    CHECK(result.timedOut);
    CHECK_FALSE(result.cancelled);
    CHECK(elapsed < 4000);
}

TEST_CASE("process runner terminates when cancelled") {
    std::atomic<bool> cancel{false};
    std::thread canceller([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        cancel = true;
    });
    const auto started = std::chrono::steady_clock::now();
    const ProcessResult result = runProcess(fakeCompiler(), {"--sleep-ms", "5000"},
                                            fs::current_path(), 0, &cancel);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    canceller.join();
    CHECK(result.launched);
    CHECK(result.cancelled);
    CHECK_FALSE(result.timedOut);
    CHECK(elapsed < 4000);
}

TEST_CASE("process runner reports launch failures") {
    const ProcessResult result =
        runProcess(fs::current_path() / "does-not-exist.exe", {}, fs::current_path(), 1000);
    CHECK_FALSE(result.launched);
    CHECK_FALSE(result.launchError.empty());
}
