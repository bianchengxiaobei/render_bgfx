#pragma once

#include "hbg/dawn/dawn_launch.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace hbg::dawn {

struct AgentTaskSpec {
    std::string taskId;
    std::string parameters;
    int32_t cost = 0;
    std::vector<std::string> resultFiles;
};

struct AgentJobSpec {
    std::string address = "http://127.0.0.1:5009";
    std::string missionId;
    std::string jobId;
    std::string parameters;
    std::filesystem::path jobDirectory;
    std::filesystem::path localExecutablePath;
    std::string exeTag = "default";
    int32_t exeVersionMain = 3;
    int32_t exeVersionSub = 1;
    std::string exeDirectory = "./";
    std::string fileCacheDirectory;
    std::vector<std::string> jobFiles;
    std::vector<std::string> exeFiles;
    std::vector<AgentTaskSpec> tasks;
};

struct AgentJobResult {
    DawnBakeError error = DawnBakeError::None;
    int32_t ackCode = 0;
    std::string message;
    std::size_t completedTasks = 0;
    std::size_t totalTasks = 0;
};

class AgentClient {
  public:
    explicit AgentClient(std::string address);
    ~AgentClient();

    AgentClient(const AgentClient&) = delete;
    AgentClient& operator=(const AgentClient&) = delete;

    AgentJobResult runJob(const AgentJobSpec& spec, std::chrono::milliseconds timeout,
                          const std::function<void(std::size_t completed, std::size_t total)>& onProgress = {});

    bool uploadFile(const std::string& agentAddress, const std::string& jobId, const std::filesystem::path& localPath,
                    const std::string& remotePath, std::chrono::milliseconds timeout, std::string* error = nullptr);

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace hbg::dawn
