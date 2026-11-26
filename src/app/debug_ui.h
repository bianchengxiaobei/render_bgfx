#pragma once

#include "app/window.h"
#include "core/profiler.h"
#include "gfx/resource_manager.h"
#include "render/debug/debug_display.h"
#include "shader/shader_library.h"

#include <cstdint>
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
    void setPanelVisible(bool visible) { m_visible = visible; }
    const ProfilerStats& profilerStats() const { return m_profiler.stats(); }
    void setGpuLightListStatus(bool requested, bool active, bool bufferPath, bool indirect, uint32_t passes,
                               float cpuMs, bool shadowGpu, bool shadowIndirect, bool asyncRequested,
                               bool asyncActive, uint32_t capacityRequested, uint32_t capacityEffective,
                               uint32_t listStride, uint32_t truncated, bool capacityClamped) {
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

    void setGpuSkinningStatus(bool requested, bool active, uint32_t meshes, uint32_t vertices,
                              uint32_t dispatches) {
        m_gpuSkinningRequested = requested;
        m_gpuSkinningActive = active;
        m_gpuSkinningMeshes = meshes;
        m_gpuSkinningVertices = vertices;
        m_gpuSkinningDispatches = dispatches;
    }

    void setDepthPyramidStatus(bool requested, bool active, uint32_t mips) {
        m_depthPyramidComputeRequested = requested;
        m_depthPyramidComputeActive = active;
        m_depthPyramidMips = mips;
    }

    void setGpuDrivenStatus(bool requested, bool active, bool hiZ, bool forward, bool gbuffer, bool transparent,
                            bool transparentSorted, bool prepass, bool bindless, uint32_t items,
                            uint32_t prepassItems, uint32_t batches, uint32_t cullingPasses,
                            bool indirectCountSupported) {
        m_gpuDrivenRequested = requested;
        m_gpuDrivenActive = active;
        m_gpuDrivenHiZActive = hiZ;
        m_gpuDrivenForwardActive = forward;
        m_gpuDrivenGbufferActive = gbuffer;
        m_gpuDrivenTransparentActive = transparent;
        m_gpuDrivenTransparentSorted = transparentSorted;
        m_gpuDrivenPrepassActive = prepass;
        m_gpuDrivenBindlessActive = bindless;
        m_gpuDrivenItems = items;
        m_gpuDrivenPrepassItems = prepassItems;
        m_gpuDrivenBatches = batches;
        m_gpuDrivenCullingPasses = cullingPasses;
        m_gpuDrivenIndirectCountSupported = indirectCountSupported;
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
    void endFrame(uint16_t width, uint16_t height);

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
    bool m_gpuSkinningRequested = false;
    bool m_gpuSkinningActive = false;
    uint32_t m_gpuSkinningMeshes = 0;
    uint32_t m_gpuSkinningVertices = 0;
    uint32_t m_gpuSkinningDispatches = 0;
    bool m_depthPyramidComputeRequested = false;
    bool m_depthPyramidComputeActive = false;
    uint32_t m_depthPyramidMips = 0;
    bool m_gpuDrivenRequested = false;
    bool m_gpuDrivenActive = false;
    bool m_gpuDrivenHiZActive = false;
    bool m_gpuDrivenForwardActive = false;
    bool m_gpuDrivenGbufferActive = false;
    bool m_gpuDrivenTransparentActive = false;
    bool m_gpuDrivenTransparentSorted = false;
    bool m_gpuDrivenPrepassActive = false;
    bool m_gpuDrivenBindlessActive = false;
    uint32_t m_gpuDrivenItems = 0;
    uint32_t m_gpuDrivenPrepassItems = 0;
    uint32_t m_gpuDrivenBatches = 0;
    uint32_t m_gpuDrivenCullingPasses = 0;
    bool m_gpuDrivenIndirectCountSupported = false;
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
};

} // namespace hbg
