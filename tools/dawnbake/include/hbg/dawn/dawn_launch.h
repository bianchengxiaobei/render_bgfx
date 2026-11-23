#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace hbg::dawn {

enum class DawnBakeError : int32_t {
    None = 0,
    JobCanceled = 1,
    HiveStartupFailure = -1,
    HiveConnectionLost = -2,
    JobStartFailure = -3,
    CheckSceneFailure = -4,
    JobKilled = -5,
    JobFailure = -6,
    ExportSceneFailure = -7,
    ExportJobFailure = -8,
    TaskAddFailure = -10,
    TaskFailure = -11,
    TaskRejected = -12,
    ResultImportFailure = -20,
    ResultEncodingFailure = -21,
    ResultApplyFailure = -22,
    ResultSaveFailure = -23,
    ResultConvertFailure = -24,
    Exception = -100,
    Unknown = -1000,
};

const char* dawnBakeErrorName(DawnBakeError error);

enum class ProcessStatus {
    Completed,
    TimedOut,
    Failed,
};

struct ProcessOptions {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path workingDirectory;
    std::filesystem::path logPath;
    std::chrono::milliseconds timeout{std::chrono::minutes(10)};
    std::chrono::milliseconds pollInterval{100};
    std::function<void()> onPoll;
};

struct ProcessResult {
    ProcessStatus status = ProcessStatus::Failed;
    uint32_t exitCode = 0;
    bool timedOut = false;
    uint64_t elapsedMs = 0;
    std::string error;
    std::filesystem::path logPath;
    std::string logTail;
};

struct DawnLaunchOptions {
    std::filesystem::path dawnExecutable;
    std::filesystem::path jobDirectory;
    std::string guid;
    std::chrono::milliseconds timeout{std::chrono::minutes(10)};
    std::filesystem::path logPath;
    std::vector<std::filesystem::path> expectedResults;
    std::chrono::milliseconds resultGrace{std::chrono::seconds(2)};
    bool keepJob = false;
    bool downloadAgentResults = false;
    bool forceAgentDownload = false;
    std::string agentAddress;
    std::string agentFileAddress;
    std::function<void(size_t completed, size_t total)> onProgress;
};

struct JobWorkspace {
    std::filesystem::path path;
    bool created = false;
    bool keep = false;

    JobWorkspace() = default;
    JobWorkspace(const JobWorkspace&) = delete;
    JobWorkspace& operator=(const JobWorkspace&) = delete;
    ~JobWorkspace();
};

bool prepareJobWorkspace(const std::filesystem::path& requested, const std::string& guid, bool keep,
                         JobWorkspace* workspace, std::string* error);

struct DawnRunResult {
    DawnBakeError error = DawnBakeError::None;
    ProcessResult process;
    std::vector<std::filesystem::path> expectedResults;
    std::vector<std::filesystem::path> resultFiles;
    std::vector<std::filesystem::path> missingResults;
};

ProcessResult runProcess(const ProcessOptions& options);

bool buildDawnArguments(const std::filesystem::path& jobDirectory, const std::string& guid,
                        std::vector<std::string>* arguments, std::string* error);

ProcessResult runDawn(const DawnLaunchOptions& options);

DawnRunResult runDawnJob(const DawnLaunchOptions& options);

} // namespace hbg::dawn
