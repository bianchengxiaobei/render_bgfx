#pragma once

#include <cstdint>

namespace hbg {

struct ProfilerStats {
    double fps = 0.0;
    double cpuFrameMs = 0.0;
    double gpuFrameMs = 0.0;
    double waitRenderMs = 0.0;
    uint32_t drawCalls = 0;
    uint32_t computeCalls = 0;
    uint16_t textures = 0;
    uint16_t frameBuffers = 0;
    uint16_t programs = 0;
    uint16_t views = 0;
    int64_t textureMemoryBytes = 0;
    int64_t renderTargetMemoryBytes = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint32_t gpuFrame = 0;
};

class Profiler {
  public:
    void update(double deltaSeconds);
    const ProfilerStats& stats() const { return m_stats; }

  private:
    ProfilerStats m_stats{};
};

} // namespace hbg
