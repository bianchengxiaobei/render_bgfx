#include "core/profiler.h"

#include <bgfx/bgfx.h>

namespace hbg {
namespace {

double ticksToMilliseconds(int64_t ticks, int64_t frequency) {
    if (frequency <= 0) {
        return 0.0;
    }
    return static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency);
}

} // namespace

void Profiler::update(double deltaSeconds) {
    m_stats = ProfilerStats{};
    m_stats.fps = deltaSeconds > 0.0 ? 1.0 / deltaSeconds : 0.0;

    const bgfx::Stats* stats = bgfx::getStats();
    if (stats == nullptr) {
        return;
    }

    m_stats.cpuFrameMs = ticksToMilliseconds(stats->cpuTimeFrame, stats->cpuTimerFreq);
    m_stats.gpuFrameMs = ticksToMilliseconds(stats->gpuTimeEnd - stats->gpuTimeBegin, stats->gpuTimerFreq);
    if (m_stats.gpuFrameMs <= 0.0 && stats->latencyGpu > 0) {
        m_stats.gpuFrameMs = static_cast<double>(stats->latencyGpu) / 1000000.0;
    }
    m_stats.waitRenderMs = ticksToMilliseconds(stats->waitRender, stats->cpuTimerFreq);
    m_stats.drawCalls = stats->numDraw;
    m_stats.computeCalls = stats->numCompute;
    m_stats.textures = stats->numTextures;
    m_stats.frameBuffers = stats->numFrameBuffers;
    m_stats.programs = stats->numPrograms;
    m_stats.views = stats->numViews;
    m_stats.textureMemoryBytes = stats->textureMemoryUsed;
    m_stats.renderTargetMemoryBytes = stats->rtMemoryUsed;
    m_stats.width = stats->width;
    m_stats.height = stats->height;
    m_stats.gpuFrame = stats->gpuFrameNum;
}

} // namespace hbg
