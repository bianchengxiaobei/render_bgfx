#pragma once

#include "app/window.h"
#include "core/profiler.h"
#include "gfx/resource_manager.h"
#include "render/debug/debug_display.h"
#include "render/river/river_editor.h"
#include "shader/shader_library.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

struct ImDrawData;

namespace hbg {

class DebugUI {
  public:
    DebugUI() = default;
    ~DebugUI() = default;

    DebugUI(const DebugUI&) = delete;
    DebugUI& operator=(const DebugUI&) = delete;

    bool init(gfx::ResourceManager& resources, shader::ShaderLibrary& shaderLibrary);
    void shutdown(gfx::ResourceManager& resources);

    bool valid() const { return m_valid; }
    bool renderDocLoaded() const { return m_renderDocLoaded; }
    render::DebugDisplaySettings& settings() { return m_settings; }
    const render::DebugDisplaySettings& settings() const { return m_settings; }
    const ProfilerStats& profilerStats() const { return m_profiler.stats(); }
    void setGpuLightListStatus(bool requested, bool active, bool bufferPath, bool indirect, uint32_t passes,
                               float cpuMs, bool shadowGpu, bool shadowIndirect, bool asyncRequested, bool asyncActive,
                               uint32_t capacityRequested, uint32_t capacityEffective, uint32_t listStride,
                               uint32_t truncated, bool capacityClamped) {
        m_gpuLightListRequested = requested;
        m_gpuLightListActive = active;
        m_gpuLightListBufferPath = bufferPath;
        m_gpuLightListIndirect = indirect;
        m_gpuLightListPasses = passes;
        m_gpuLightListCpuMs = cpuMs;
        m_gpuShadowCulling = shadowGpu;
        m_gpuShadowIndirect = shadowIndirect;
        m_asyncRequested = asyncRequested;
        m_asyncActive = asyncActive;
        m_lightListCapacityRequested = capacityRequested;
        m_lightListCapacityEffective = capacityEffective;
        m_lightListStride = listStride;
        m_lightListTruncated = truncated;
        m_lightListCapacityClamped = capacityClamped;
    }

    void setClusterStatsStatus(bool pending, bool valid, bool gpu, uint32_t clusters, uint32_t activeClusters,
                               uint32_t overflowClusters, uint32_t overflowLights, uint32_t maxOverflow,
                               uint32_t maxLights) {
        m_clusterStatsPending = pending;
        m_clusterStatsValid = valid;
        m_clusterStatsGpu = gpu;
        m_clusterStatsClusters = clusters;
        m_clusterStatsActiveClusters = activeClusters;
        m_clusterStatsOverflowClusters = overflowClusters;
        m_clusterStatsOverflowLights = overflowLights;
        m_clusterStatsMaxOverflow = maxOverflow;
        m_clusterStatsMaxLights = maxLights;
    }
    bool consumeClusterStatsReadbackRequest() {
        const bool requested = m_clusterStatsReadbackRequested;
        m_clusterStatsReadbackRequested = false;
        return requested;
    }

    void beginFrame(const WindowInput& input, uint16_t width, uint16_t height, double deltaSeconds);
    void drawRiverEditor(render::RiverEditor& editor, const WindowInput& input, const Mat4& viewProj,
                         const Mat4& invViewProj, uint16_t width, uint16_t height);
    void endFrame(uint16_t width, uint16_t height);
    bool panelVisible() const { return m_visible; }
    bool wantsMouse() const;

    void setRiverEditPath(std::string path) { m_riverEditPath = std::move(path); }
    bool consumeRiverSaveRequest() {
        const bool requested = m_riverSaveRequested;
        m_riverSaveRequested = false;
        return requested;
    }
    bool consumeRiverReloadRequest() {
        const bool requested = m_riverReloadRequested;
        m_riverReloadRequested = false;
        return requested;
    }

  private:
    void drawPanel();
    void syncTextures(ImDrawData& drawData);
    gfx::TextureHandle textureFor(uint64_t id) const;

    gfx::ResourceManager* m_resources = nullptr;
    shader::ShaderLibrary* m_shaderLibrary = nullptr;
    gfx::ProgramHandle m_program;
    gfx::UniformHandle m_sampler;
    std::vector<gfx::TextureHandle> m_textures;

    render::DebugDisplaySettings m_settings;
    Profiler m_profiler;
    bool m_valid = false;
    bool m_visible = true;
    bool m_renderDocLoaded = false;
    bool m_screenshotRequested = false;
    bool m_gpuLightListRequested = false;
    bool m_gpuLightListActive = false;
    bool m_gpuLightListBufferPath = false;
    bool m_gpuLightListIndirect = false;
    bool m_gpuShadowCulling = false;
    bool m_gpuShadowIndirect = false;
    bool m_asyncRequested = false;
    bool m_asyncActive = false;
    uint32_t m_gpuLightListPasses = 0;
    float m_gpuLightListCpuMs = 0.0f;
    uint32_t m_lightListCapacityRequested = 0;
    uint32_t m_lightListCapacityEffective = 0;
    uint32_t m_lightListStride = 0;
    uint32_t m_lightListTruncated = 0;
    bool m_lightListCapacityClamped = false;
    bool m_clusterStatsReadbackRequested = false;
    bool m_clusterStatsPending = false;
    bool m_clusterStatsValid = false;
    bool m_clusterStatsGpu = false;
    uint32_t m_clusterStatsClusters = 0;
    uint32_t m_clusterStatsActiveClusters = 0;
    uint32_t m_clusterStatsOverflowClusters = 0;
    uint32_t m_clusterStatsOverflowLights = 0;
    uint32_t m_clusterStatsMaxOverflow = 0;
    uint32_t m_clusterStatsMaxLights = 0;
    uint32_t m_screenshotCount = 0;
    std::string m_riverEditPath;
    bool m_riverSaveRequested = false;
    bool m_riverReloadRequested = false;
};

} // namespace hbg
