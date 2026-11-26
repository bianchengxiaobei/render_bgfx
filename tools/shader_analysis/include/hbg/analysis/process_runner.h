#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hbg::analysis {

struct ProcessResult {
    bool launched = false;
    bool timedOut = false;
    bool cancelled = false;
    int exitCode = -1;
    std::int64_t durationMs = 0;
    std::string stdoutText;
    std::string stderrText;
    std::string launchError;
};

ProcessResult runProcess(const std::filesystem::path& executable,
                         const std::vector<std::string>& arguments,
                         const std::filesystem::path& workingDirectory,
                         int timeoutMs = 0,
                         const std::atomic<bool>* cancelFlag = nullptr);

} // namespace hbg::analysis
