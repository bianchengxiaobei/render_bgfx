#pragma once

#include "gfx/buffer.h"
#include "gfx/generated/uniforms.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/atmosphere/volumetric_fog_settings.h"
#include "render/camera/view_state.h"
#include "render/culling/draw_list.h"
#include "render/debug/debug_display.h"
#include "render/graph/render_graph.h"
#include "render/lighting/cookie_atlas.h"
#include "render/lighting/light_builder.h"
#include "render/lighting/light_list_builder.h"
#include "render/lightmap/lightmap.h"
#include "render/material/material_system.h"
#include "render/passes/deferred_variant.h"
#include "render/passes/forward_lighting.h"
#include "render/pipeline/frame_settings.h"
#include "render/pipeline/pipeline_settings.h"
#include "render/postprocess/post_process_chain.h"
#include "render/postprocess/post_process_settings.h"
#include "render/postprocess/ssao_settings.h"
#include "render/postprocess/ssr_settings.h"
#include "render/postprocess/ssgi_settings.h"
#include "render/scene/render_scene.h"
#include "render/shadergraph/graph_material_system.h"
#include "render/shadows/shadow_manager.h"
#include "render/sky/probe_manager.h"
#include "render/sky/sky_manager.h"
#include "shader/shader_library.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hbg::gfx {
class Encoder;
}

namespace hbg::vfxbridge {
class HbgVfxBackend;
}

namespace vfx {
class Runtime;
class Effect;
struct FrameContext;
} // namespace vfx

namespace hbg::render {

class ForwardRenderer {
  public:
    struct FrameDiagnostics {
        uint32_t passCount = 0;
        uint32_t gbufferTargets = 0;
        uint32_t postProcessPasses = 0;
        bool deferred = false;
        bool deferredAdvanced = false;
        bool litVariantsRequested = false;
        bool litVariantsActive = false;
        bool litVariantTilesRequested = false;
        bool litVariantTilesActive = false;
        uint32_t litVariantLightFlags = 0;
        bool stencil = false;
        bool bakedGIActive = false;
        uint32_t bakedGIObjects = 0;
        bool postProcessActive = false;
        bool postProcessComputeSupported = false;
        bool postProcessHistoryValid = false;
        bool postProcessTaaActive = false;
        bool postProcessTaaHistoryValid = false;
        uint32_t postProcessTaaHistoryFrames = 0;
        float postProcessWallMs = 0.0f;
        bool postProcessExposureActive = false;
        bool postProcessUberActive = false;
        bool postProcessBloomActive = false;
        bool postProcessDofActive = false;
        bool postProcessMotionBlurActive = false;
        bool postProcessFxaaActive = false;
        bool volumetricFogActive = false;
        uint32_t volumetricFogSteps = 0;
        uint16_t volumetricFogWidth = 0;
        uint16_t volumetricFogHeight = 0;
        float volumetricFogScale = 1.0f;
        bool volumetricFogTemporal = false;
        bool volumetricFogHistoryValid = false;
        bool ssaoTemporal = false;
        bool ssaoHistoryValid = false;
        uint32_t ssaoHistoryFrames = 0;
        bool ssrTemporal = false;
        bool ssrHistoryValid = false;
        uint32_t ssrHistoryFrames = 0;
        bool ssgiTemporal = false;
        bool ssgiHistoryValid = false;
        uint32_t ssgiHistoryFrames = 0;
        uint32_t volumetricFogPunctualLights = 0;
        uint32_t volumetricFogLocalVolumes = 0;
        uint32_t volumetricFogLocalMasks = 0;
        uint32_t shadowDrawnItems = 0;
        uint32_t shadowCulledItems = 0;
        bool shadowDirectionalCacheActive = false;
        bool shadowDirectionalMixedCached = false;
        uint32_t shadowCachedDirectionalRequests = 0;
        bool shadowCacheActive = false;
        uint32_t shadowCachedRequests = 0;
        bool shadowAreaCacheActive = false;
        uint32_t shadowCachedAreaRequests = 0;
        bool contactShadowsActive = false;
        uint16_t contactShadowWidth = 0;
        uint16_t contactShadowHeight = 0;
        bool gpuLightListRequested = false;
        bool gpuLightListActive = false;
        bool gpuLightListFallback = false;
        bool gpuLightListBufferPath = false;
        bool gpuLightListIndirect = false;
        uint32_t gpuLightListPasses = 0;
        float lightListCpuMs = 0.0f;
        uint32_t lightListCapacityRequested = 0;
        uint32_t lightListCapacityEffective = 0;
        uint32_t lightListStride = 0;
        uint32_t lightListTruncated = 0;
        bool lightListCapacityClamped = false;
        bool lightListFeatureFlagsActive = false;
        bool gpuShadowCulling = false;
        bool gpuShadowIndirect = false;
        uint32_t gpuShadowCullDispatches = 0;
        uint32_t gpuShadowIndirectDraws = 0;
        bool gpuSkinningRequested = false;
        bool gpuSkinningActive = false;
        uint32_t gpuSkinningMeshes = 0;
        uint32_t gpuSkinningVertices = 0;
        uint32_t gpuSkinningDispatches = 0;
        bool depthPyramidComputeRequested = false;
        bool depthPyramidComputeActive = false;
        uint16_t depthPyramidMips = 0;
        bool gpuDrivenRequested = false;
        bool gpuDrivenActive = false;
        bool gpuDrivenHiZActive = false;
        bool gpuDrivenForwardActive = false;
        bool gpuDrivenGbufferActive = false;
        bool gpuDrivenTransparentActive = false;
        bool gpuDrivenTransparentSorted = false;
        bool gpuDrivenPrepassActive = false;
        bool gpuDrivenBindlessActive = false;
        bool gpuDrivenIndirectCountSupported = false;
        uint32_t gpuDrivenItems = 0;
        uint32_t gpuDrivenPrepassItems = 0;
        uint32_t gpuDrivenBatches = 0;
        uint32_t gpuDrivenCullingPasses = 0;
        bool asyncGpuLightListRequested = false;
        bool asyncGpuLightListActive = false;
        bool clusterStatsValid = false;
        bool clusterStatsGpu = false;
        bool clusterStatsReadbackPending = false;
        uint32_t clusterStatsClusters = 0;
        uint32_t clusterStatsActiveClusters = 0;
        uint32_t clusterStatsOverflowClusters = 0;
        uint32_t clusterStatsOverflowLights = 0;
        uint32_t clusterStatsMaxOverflow = 0;
        uint32_t clusterStatsMaxLights = 0;
        uint32_t clusterStatsFeatureFlagsChecked = 0;
        uint32_t clusterStatsFeatureFlagMismatch = 0;
        bool vfxActive = false;
        uint32_t vfxEffects = 0;
        uint32_t vfxSystems = 0;
        uint32_t vfxParticles = 0;
        uint32_t vfxDraws = 0;
        uint32_t vfxShadowDraws = 0;
        uint32_t vfxDispatches = 0;
        uint32_t vfxSortPasses = 0;
        uint32_t vfxEvents = 0;
        float vfxUpdateMs = 0.0f;
        float vfxSimulationMs = 0.0f;
        float vfxOutputMs = 0.0f;
        float vfxMs = 0.0f;
    };

    struct ClusterStats {
        bool valid = false;
        bool gpu = false;
        uint32_t clusters = 0;
        uint32_t activeClusters = 0;
        uint32_t overflowClusters = 0;
        uint32_t overflowLights = 0;
        uint32_t maxOverflow = 0;
        uint32_t maxLights = 0;
        uint32_t featureFlagsChecked = 0;
        uint32_t featureFlagMismatch = 0;
    };

    ForwardRenderer();
    ~ForwardRenderer();

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

    bool init(gfx::ResourceManager& resources, RenderScene& scene, uint16_t width, uint16_t height);
    bool setSky(const std::string& path, float intensity = 1.0f, float rotationDegrees = 0.0f);
    void resize(uint16_t width, uint16_t height);
    void shutdown(gfx::ResourceManager& resources);

    void render(RenderScene& scene, uint16_t width, uint16_t height, float time);
    bool valid() const { return m_valid; }
    void setDebugLightData(bool enabled) {
        m_debugDisplayMode = enabled ? DebugDisplayMode::LightData : DebugDisplayMode::None;
    }
    void setDebugDisplayMode(DebugDisplayMode mode) {
        m_debugDisplayMode = mode;
        m_shadowManager.setCacheSuppressed(mode == DebugDisplayMode::ShadowAtlasPunctual);
    }
    void setDebugLightClusterSlice(int slice) { m_debugLightClusterSlice = slice; }
    void setDebugLightmapMask(bool enabled) { m_debugLightmapMask = enabled; }
    void setLightClusterEnabled(bool enabled) { m_lightClusterEnabled = enabled; }
    void setGpuLightListEnabled(bool enabled);
    void setGpuLightListBufferEnabled(bool enabled);
    void setGpuDrivenGeometryEnabled(bool enabled);
    void setGpuSkinningEnabled(bool enabled);
    bool gpuSkinningActive() const { return m_gpuSkinningActive; }
    void setLitVariantsEnabled(bool enabled) {
        m_litVariantsEnabled = enabled;
        m_pipelineSettings.supportLitVariants = enabled;
    }
    bool litVariantsActive() const { return m_litVariantsActive; }
    bool gpuLightListActive() const { return m_gpuLightListActive; }
    void requestClusterStatsReadback();
    const ClusterStats& clusterStats() const { return m_clusterStats; }
    bool clusterStatsReadbackPending() const { return m_clusterStatsReadbackPending; }
    bool gpuLightListBufferActive() const { return m_gpuLightListBufferActive; }
    void setShadowsEnabled(bool enabled) { m_shadowsEnabled = enabled; }
    void setWireframe(bool enabled) { m_wireframe = enabled; }
    void setRenderScale(float scale);
    float renderScale() const { return m_renderScale; }
    void setViewJitter(bool enabled) { m_jitterEnabled = enabled; }
    bool viewJitter() const { return m_jitterEnabled; }
    void setSettings(const PipelineSettings& pipelineSettings, const FrameSettings& frameSettings);
    bool setShadowSettings(const ShadowSettings& settings);
    void setPostProcessSettings(const PostProcessSettings& settings);
    void setPostProcessProfile(const PostProcessProfile& profile);
    void setSsaoSettings(const SsaoSettings& settings);
    void setSsrSettings(const SsrSettings& settings);
    void setSsgiSettings(const SsgiSettings& settings);
    void setVolumetricFogSettings(const VolumetricFogSettings& settings);
    const PipelineSettings& pipelineSettings() const { return m_pipelineSettings; }
    const FrameSettings& frameSettings() const { return m_frameSettings; }
    const PostProcessSettings& postProcessSettings() const { return m_postProcessSettings; }
    const SsrSettings& ssrSettings() const { return m_ssrSettings; }
    const SsgiSettings& ssgiSettings() const { return m_ssgiSettings; }
    const VolumetricFogSettings& volumetricFogSettings() const { return m_volumetricFogSettings; }
    const ViewState& viewState() const { return m_viewState; }
    FrameDiagnostics frameDiagnostics() const;

    bool reloadVfxAsset(const std::filesystem::path& binaryPath, std::string& error);
    bool previewVfxAsset(const std::filesystem::path& sourceAssetPath,
                         const std::filesystem::path& binaryPath, std::string& error);
    bool setVfxPlayRate(const std::filesystem::path& assetPath, float rate, std::string& error);
    bool toggleVfxPaused(const std::filesystem::path& assetPath, std::string& error);
    bool stepVfx(const std::filesystem::path& assetPath, std::string& error);
    bool restartVfx(const std::filesystem::path& assetPath, std::string& error);

    struct VfxPlaybackStatus {
        bool available = false;
        bool paused = false;
        float playRate = 1.0f;
        std::uint32_t randomSeed = 0;
        std::uint32_t instances = 0;
        bool preview = false;
        Bounds bounds{};
    };
    VfxPlaybackStatus vfxPlaybackStatus(const std::filesystem::path& assetPath) const;

    shader::ShaderLibrary& shaderLibrary() { return m_shaderLibrary; }
    const shader::ShaderLibrary& shaderLibrary() const { return m_shaderLibrary; }

  private:
    struct VfxInstanceRuntime {
        const VfxInstance* instance = nullptr;
        std::vector<const VfxInstance*> instances;
        ::vfx::Effect* effect = nullptr;
        std::filesystem::path assetPath;
        float playRate = 1.0f;
        std::uint32_t randomSeed = 0;
        bool playing = false;
        bool paused = false;
        bool preview = false;
    };

    bool ensureVfxRuntime();
    bool replaceVfxRuntime(VfxInstanceRuntime& runtime, const std::filesystem::path& binaryPath,
                           bool preview, std::string& error);

    struct FramePassResources {
        RenderGraphTextureHandle color;
        RenderGraphTextureHandle depth;
        RenderGraphTextureHandle transparentPreRefractionDepth;
        RenderGraphTextureHandle transparentPreRefractionColor;
        RenderGraphTextureHandle transparentRefractionSourceColor;
        RenderGraphTextureHandle normal;
        RenderGraphTextureHandle motionVector;
        RenderGraphTextureHandle depthPyramid;
        RenderGraphTextureHandle hiZDepthPyramid;
        RenderGraphTextureHandle hiZHistoryPyramid;
        RenderGraphTextureHandle lightData;
        RenderGraphTextureHandle lightList;
        RenderGraphTextureHandle clusterLightList;
        RenderGraphTextureHandle lightScreenBounds;
        RenderGraphTextureHandle lightBigTile;
        RenderGraphBufferHandle lightScreenBoundsBuffer;
        RenderGraphBufferHandle lightBigTileBuffer;
        RenderGraphBufferHandle lightFptlBuffer;
        RenderGraphBufferHandle lightClusterBuffer;
        RenderGraphBufferHandle lightClusterIndexBuffer;
        RenderGraphBufferHandle lightIndirectArgs;
        RenderGraphBufferHandle shadowRequestBuffer;
        RenderGraphBufferHandle shadowCasterBuffer;
        RenderGraphBufferHandle shadowBatchBuffer;
        RenderGraphBufferHandle shadowInstanceBuffer;
        RenderGraphBufferHandle shadowIndirectBuffer;
        RenderGraphBufferHandle shadowStatsBuffer;
        RenderGraphBufferHandle gpuDrivenForwardInput;
        RenderGraphBufferHandle gpuDrivenForwardVisible;
        RenderGraphBufferHandle gpuDrivenForwardBatches;
        RenderGraphBufferHandle gpuDrivenForwardIndirect;
        RenderGraphBufferHandle gpuDrivenForwardDrawRecords;
        RenderGraphBufferHandle gpuDrivenForwardCount;
        RenderGraphBufferHandle gpuDrivenGbufferInput;
        RenderGraphBufferHandle gpuDrivenGbufferVisible;
        RenderGraphBufferHandle gpuDrivenGbufferBatches;
        RenderGraphBufferHandle gpuDrivenGbufferIndirect;
        RenderGraphBufferHandle gpuDrivenGbufferDrawRecords;
        RenderGraphBufferHandle gpuDrivenGbufferCount;
        RenderGraphBufferHandle gpuDrivenTransparentInput;
        RenderGraphBufferHandle gpuDrivenTransparentVisible;
        RenderGraphBufferHandle gpuDrivenTransparentBatches;
        RenderGraphBufferHandle gpuDrivenTransparentIndirect;
        RenderGraphBufferHandle gpuDrivenTransparentDrawRecords;
        RenderGraphBufferHandle gpuDrivenTransparentCount;
        RenderGraphBufferHandle gpuDrivenTransparentSortedVisible;
        RenderGraphBufferHandle gpuDrivenTransparentSortedIndirect;
        RenderGraphBufferHandle gpuDrivenPrepassInput;
        RenderGraphBufferHandle gpuDrivenPrepassVisible;
        RenderGraphBufferHandle gpuDrivenPrepassBatches;
        RenderGraphBufferHandle gpuDrivenPrepassIndirect;
        RenderGraphBufferHandle gpuDrivenPrepassDrawRecords;
        RenderGraphBufferHandle gpuDrivenPrepassCount;
        RenderGraphBufferHandle gpuDrivenPrepassSecondInput;
        RenderGraphBufferHandle gpuDrivenPrepassSecondVisible;
        RenderGraphBufferHandle gpuDrivenPrepassSecondBatches;
        RenderGraphBufferHandle gpuDrivenPrepassSecondIndirect;
        RenderGraphBufferHandle gpuDrivenPrepassSecondDrawRecords;
        RenderGraphBufferHandle gpuDrivenPrepassSecondCount;
        RenderGraphTextureHandle directionalShadow;
        RenderGraphTextureHandle punctualShadow;
        RenderGraphTextureHandle areaShadow;
        RenderGraphBufferHandle shadowContext;
        RenderGraphTextureHandle contactShadow;
        RenderGraphTextureHandle environment;
        RenderGraphTextureHandle skyEnvironment;
        RenderGraphTextureHandle ssao;
        RenderGraphTextureHandle ssaoRaw;
        RenderGraphTextureHandle ssaoBlur;
        RenderGraphTextureHandle ssaoHistory;
        RenderGraphTextureHandle ssaoHistoryPrevious;
        RenderGraphTextureHandle ssr;
        RenderGraphTextureHandle ssrRaw;
        RenderGraphTextureHandle ssrHistoryPrevious;
        RenderGraphTextureHandle ssgiRaw;
        RenderGraphTextureHandle ssgi;
        RenderGraphTextureHandle ssgiHistoryPrevious;
        RenderGraphTextureHandle bakedIndirect;
        RenderGraphTextureHandle litTileFeatureFlags;
        RenderGraphBufferHandle litIndirectArgs;
        RenderGraphBufferHandle fullscreen;
        std::array<RenderGraphTextureHandle, 4> decal{};
        std::array<RenderGraphTextureHandle, 5> gbuffer{};
    };

    struct GpuDrivenDrawData {
        gfx::BufferHandle input;
        gfx::BufferHandle visible;
        gfx::BufferHandle batches;
        gfx::BufferHandle indirect;
        gfx::BufferHandle drawRecords;
        gfx::BufferHandle sortedVisible;
        gfx::BufferHandle sortedIndirect;
        uint32_t itemCount = 0;
        uint32_t batchCount = 0;
        uint32_t recordCount = 0;
        bool perItemBatches = false;
        bool bindless = false;
        bool sorted = false;
        uint32_t materialVariant = 0;
        gfx::BufferHandle count;
        bool active = false;
    };

    bool ensureGpuDrivenDrawData(GpuDrivenDrawData& data, const DrawList& list, const char* prefix,
                                 bool perItemBatches = false, bool bindless = false, bool sorted = false);
    void releaseGpuDrivenDrawData(GpuDrivenDrawData& data);
    bool ensureGpuDrivenHiZTextures(uint16_t width, uint16_t height, uint8_t mipCount);
    void releaseGpuDrivenHiZTextures();
    bool updateGpuDrivenData(const RenderScene& scene, bool deferred, bool depthPrepass);
    void addGpuDrivenCullingPasses(RenderGraph& graph, const FramePassResources& resources);
    bool addGpuDrivenCullingPass(RenderGraph& graph, const GpuDrivenDrawData& data,
                                 RenderGraphBufferHandle input, RenderGraphBufferHandle batches,
                                 RenderGraphBufferHandle visible, RenderGraphBufferHandle indirect,
                                 RenderGraphBufferHandle drawRecords, RenderGraphBufferHandle count,
                                 RenderGraphTextureHandle hiZ,
                                 RenderGraphBufferHandle previousIndirect, const char* name, bool retestCulled);
    bool addGpuDrivenTransparentSortPass(RenderGraph& graph, const GpuDrivenDrawData& data,
                                         RenderGraphBufferHandle input, RenderGraphBufferHandle visible,
                                         RenderGraphBufferHandle drawRecords, RenderGraphBufferHandle count,
                                         RenderGraphBufferHandle sortedVisible,
                                         RenderGraphBufferHandle sortedIndirect);
    void addGpuDrivenPrepassPass(RenderGraph& graph, const GpuDrivenDrawData& data,
                                 RenderGraphTextureHandle normal, RenderGraphTextureHandle depth,
                                 RenderGraphBufferHandle indirect, const RenderScene& scene, bool depthOnly,
                                 bool clearColor, const char* name);
    void renderGpuDrivenPrepass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                const RenderScene& scene, const GpuDrivenDrawData& data, bool depthOnly,
                                bool clearColor);
    bool drawGpuDrivenForwardBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                                   const DrawBatch& batch, uint32_t batchIndex, const RenderScene& scene,
                                   gfx::TextureHandle contactShadow, bool clusterList);
    bool drawGpuDrivenBindlessForward(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                                      const RenderScene& scene, gfx::TextureHandle contactShadow,
                                      bool clusterList);
    bool drawGpuDrivenBindlessTransparent(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                                          const RenderScene& scene, gfx::TextureHandle contactShadow,
                                          bool clusterList);
    bool drawGpuDrivenGBufferBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                                   const DrawBatch& batch, uint32_t batchIndex, const RenderScene& scene);
    bool drawGpuDrivenTransparentItem(gfx::Encoder& encoder, bgfx::ViewId view, const DrawItem& item,
                                      uint32_t itemIndex, const RenderScene& scene,
                                      gfx::TextureHandle contactShadow, bool clusterList);
    bool shadowsActive() const;
    bool contactShadowsActive() const;
    bool motionVectorsActive() const;
    bool objectMotionVectorsActive() const;
    bool transparentMotionVectorsActive() const;
    bool transparentBackfaceActive() const;
    bool transparentDepthPrepassActive() const;
    bool transparentRefractionActive();
    const char* deferredUnavailableReason() const;
    bool ensureGBufferTextures(uint16_t renderWidth, uint16_t renderHeight);
    void releaseGBufferTextures();
    bool ensureDecalTextures(uint16_t renderWidth, uint16_t renderHeight);
    void releaseDecalTextures();
    gfx::TextureFormat selectHdrFormat() const;
    gfx::TextureFormat selectDepthFormat() const;
    gfx::TextureFormat selectNormalFormat() const;
    gfx::TextureFormat selectMotionVectorFormat() const;
    gfx::TextureFormat selectDepthPyramidFormat();
    bool depthPyramidComputeSupported();
    bool depthPyramidMaxComputeSupported();
    gfx::TextureFormat selectSsaoFormat() const;
    void buildDrawLists(const RenderScene& scene);
    const DrawList& forwardDrawList(bool transparent) const;
    void updateCamera(const RenderScene& scene, uint16_t width, uint16_t height);
    void syncTaaJitter();
    void updateGlobalUniforms();
    void updateLights(const RenderScene& scene);
    bool ensureLightDataTexture();
    void uploadLightData();
    bool ensureLightListTexture(uint16_t renderWidth, uint16_t renderHeight);
    bool ensureLightBigTileTexture(uint16_t renderWidth, uint16_t renderHeight);
    bool ensureClusterLightListTextures(uint16_t renderWidth, uint16_t renderHeight);
    bool ensureLightListBuffers(uint16_t renderWidth, uint16_t renderHeight);
    bool ensureLitVariantTileResources(uint16_t renderWidth, uint16_t renderHeight);
    void releaseLitVariantTileResources();
    void addLitVariantTilePasses(RenderGraph& graph, const FramePassResources& resources);
    void addDeferredLightingComputePass(RenderGraph& graph, const FramePassResources& resources);
    void releaseLightListBuffers();
    bool ensureShadowGpuBuffers(uint32_t requestCount, uint32_t casterCount, uint32_t batchCount);
    void releaseShadowGpuBuffers();
    bool updateShadowGpuData(const RenderScene& scene);
    void addGpuShadowCullPass(RenderGraph& graph, const FramePassResources& resources);
    void addGpuSkinningPass(RenderGraph& graph);
    void updateLightList();
    void updateCpuLightList();
    void updateLightListFeatureFlags();
    bool uploadCpuLightList();
    void uploadClusterLightList();
    void pollClusterStatsReadback();
    void submitClusterStatsReadback();
    bool ensureClusterStatsReadbackTexture(uint16_t width, uint16_t height);
    void releaseClusterStatsReadbackTexture();
    void updateClusterStatsFromCpu();
    void releaseLightListTexture();
    void releaseLightBigTileTexture();
    void releaseClusterLightListTextures();
    bool gpuLightListPathAvailable();
    gfx::ProgramHandle lightListProgram(shader::PassId pass, uint64_t materialKey = 0);
    void bindLightList(gfx::Encoder& encoder, bool cluster) const;
    void bindLightListTexture(gfx::Encoder& encoder, bool cluster) const;
    ForwardLightingContext makeForwardLightingContext(const gfx::generated::ObjectUniforms& objectUniforms,
                                                      gfx::TextureHandle contactShadow, bool cluster,
                                                      bool contactShadows, bool useGpuLightList = true) const;
    void updateEnvironmentUniforms();
    void bindProbeTextures(gfx::Encoder& encoder, bool bindCookies = true) const;
    bool ensureCookieAtlasTexture();
    void uploadCookieAtlas();
    void bindCookieAtlas(gfx::Encoder& encoder, uint8_t stage = 15) const;
    void updateContactShadowUniforms();
    void updatePostProcessUniforms();
    void updateSsaoUniforms(bool enabled);
    void updateSsrUniforms(bool enabled);
    void updateSsgiUniforms(bool enabled);
    void updateVolumetricFogUniforms(bool enabled, bool historyValid, bool temporalActive, bool punctualLights,
                                     uint16_t fogWidth, uint16_t fogHeight);
    void updateLocalFogUniforms(const RenderScene& scene, uint32_t& localVolumeCount);
    bool ensureLocalFogMaskTextures(gfx::ResourceManager& resources, const RenderScene& scene);
    void releaseLocalFogMaskTextures(gfx::ResourceManager& resources);
    bool ensureVolumetricFogHistory(uint16_t fogWidth, uint16_t fogHeight);
    void releaseVolumetricFogHistory();
    bool ensureSsaoHistory(uint16_t width, uint16_t height);
    void releaseSsaoHistory();
    bool ensureSsrHistory(uint16_t width, uint16_t height);
    void releaseSsrHistory();
    bool ensureSsgiHistory(uint16_t width, uint16_t height);
    void releaseSsgiHistory();
    void addShadowPass(RenderGraph& graph, RenderGraphTextureHandle directionalShadow,
                       RenderGraphTextureHandle cachedDirectionalShadow,
                       RenderGraphTextureHandle punctualShadow, RenderGraphTextureHandle areaShadow,
                       const RenderScene& scene);
    void addShadowCopyPass(RenderGraph& graph, RenderGraphTextureHandle directionalShadow,
                            RenderGraphTextureHandle cachedDirectionalShadow, uint16_t atlasSize);
    void addShadowMomentPasses(RenderGraph& graph, RenderGraphTextureHandle directionalShadow);
    void addAreaEvsmMomentPasses(RenderGraph& graph, RenderGraphTextureHandle areaShadow);
    void bindShadowMomentSat(gfx::Encoder& encoder, gfx::TextureHandle directionalShadow) const;
    gfx::TextureHandle directionalShadowSamplingTexture(gfx::TextureHandle fallback) const;
    uint32_t directionalShadowSamplingFlags() const;
    void addGpuLightListPasses(RenderGraph& graph, const FramePassResources& resources);
    RenderGraphTextureHandle addContactShadowPass(RenderGraph& graph, const FramePassResources& resources,
                                                  bool deferred);
    void addShadowAtlasClearPass(RenderGraph& graph, RenderGraphTextureHandle atlas, uint16_t size,
                                 bool clear = true);
    void addClearPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                      bool debugBlack);
    void addPrepassPass(RenderGraph& graph, RenderGraphTextureHandle normal, RenderGraphTextureHandle depth,
                        const RenderScene& scene, bool depthOnly);
    void addObjectsMotionVectorsPass(RenderGraph& graph, RenderGraphTextureHandle motionVector,
                                     RenderGraphTextureHandle depth, const RenderScene& scene);
    void addTransparentDepthPrepass(RenderGraph& graph, RenderGraphTextureHandle depth, const RenderScene& scene);
    void addTransparentDepthPrepass(RenderGraph& graph, RenderGraphTextureHandle depth, const RenderScene& scene,
                                    const DrawList& list, const char* passName);
    void addTransparentDepthCopyPass(RenderGraph& graph, RenderGraphTextureHandle depth,
                                     RenderGraphTextureHandle destination);
    void addTransparentPreRefractionCompositePass(RenderGraph& graph, const FramePassResources& resources);
    void addTransparentRefractionSourcePass(RenderGraph& graph, const FramePassResources& resources);
    void addTransparentMotionVectorsPass(RenderGraph& graph, RenderGraphTextureHandle motionVector,
                                         RenderGraphTextureHandle depth, const RenderScene& scene);
    void addCameraMotionVectorsPass(RenderGraph& graph, RenderGraphTextureHandle motionVector,
                                    RenderGraphTextureHandle depth, RenderGraphTextureHandle depthPyramid);
    void addDepthPyramidPasses(RenderGraph& graph, RenderGraphTextureHandle depth,
                               RenderGraphTextureHandle depthPyramid);
    void addDepthPyramidMaxPasses(RenderGraph& graph, RenderGraphTextureHandle depth,
                                  RenderGraphTextureHandle depthPyramidMax);
    void addSkyPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle environment);
    RenderGraphTextureHandle addVolumetricFogPass(RenderGraph& graph, const FramePassResources& resources,
                                                  RenderGraphTextureHandle color, const RenderScene& scene);
    void addGBufferClearPass(RenderGraph& graph, const std::array<RenderGraphTextureHandle, 5>& gbuffer,
                             RenderGraphTextureHandle depth, RenderGraphBufferHandle fullscreen);
    void addDecalPasses(RenderGraph& graph, const FramePassResources& resources, const RenderScene& scene);
    void addGBufferPass(RenderGraph& graph, const FramePassResources& resources, const RenderScene& scene);
    void addBakedGIDrawPass(RenderGraph& graph, const FramePassResources& resources, const RenderScene& scene,
                            const DrawList& list);
    void addBakedGICompensatePass(RenderGraph& graph, const FramePassResources& resources);
    void addBakedGIForwardCompositePass(RenderGraph& graph, const FramePassResources& resources);
    void addSSAOPasses(RenderGraph& graph, FramePassResources& resources);
    void addSSRPasses(RenderGraph& graph, FramePassResources& resources);
    void addSSGIPasses(RenderGraph& graph, FramePassResources& resources);
    void addDeferredLightingPass(RenderGraph& graph, const FramePassResources& resources);
    void recordForwardFrame(RenderGraph& graph, FramePassResources& resources, const RenderScene& scene,
                            DebugDisplayMode debugMode, bool materialDebug);
    void recordDeferredFrame(RenderGraph& graph, FramePassResources& resources, const RenderScene& scene,
                             DebugDisplayMode debugMode, bool materialDebug);
    void addForwardPass(RenderGraph& graph, const FramePassResources& resources, RenderGraphTextureHandle color,
                        RenderGraphTextureHandle depth,
                        RenderGraphTextureHandle lightData, RenderGraphTextureHandle lightList,
                        RenderGraphTextureHandle clusterLightList,
                        RenderGraphBufferHandle lightFptlBuffer, RenderGraphBufferHandle clusterLightListBuffer, RenderGraphTextureHandle directionalShadow,
                        RenderGraphTextureHandle punctualShadow, RenderGraphTextureHandle areaShadow,
                        RenderGraphBufferHandle shadowContext, RenderGraphTextureHandle contactShadow,
                        const RenderScene& scene, bool transparent, bool forwardOnly = false,
                        const DrawList* listOverride = nullptr, RenderGraphTextureHandle depthOverride = {},
                        const char* passName = nullptr);
    void renderBakedGIDraws(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list,
                            const RenderScene& scene);
    void renderBakedGIPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                           const DrawList& list, const RenderScene& scene);
    void renderBakedGICompensatePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                     gfx::FrameBufferHandle frameBuffer, gfx::TextureHandle bakedIndirect,
                                     gfx::TextureHandle depthPyramid, gfx::TextureHandle ssao,
                                     const std::array<gfx::TextureHandle, 5>& gbuffer);
    void renderBakedGIForwardCompositePass(gfx::Encoder& encoder, bgfx::ViewId view,
                                           gfx::FrameBufferHandle frameBuffer,
                                           gfx::TextureHandle bakedIndirect);
    bool bakedGIProgramsReady();
    void addWireframePass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                          const RenderScene& scene);
    void ensureVfx(const RenderScene& scene);
    void releaseVfx();
    void updateVfx(const RenderScene& scene, float deltaSeconds);
    void addVfxSimulationPass(RenderGraph& graph);
    void addVfxOutputPass(RenderGraph& graph, const FramePassResources& resources,
                          ::vfx::Effect* effect = nullptr, const char* passName = nullptr,
                          std::uint32_t batchIndex = 0xffffffffu);
    void addVfxMotionVectorsPass(RenderGraph& graph, const FramePassResources& resources);
    void addTransparentFramePasses(RenderGraph& graph, const RenderScene& scene,
                                  FramePassResources& resources, bool vfxVisible);
    void renderVfxShadow(gfx::Encoder& encoder, bgfx::ViewId view, const ShadowRequest& request);
    bool prepareVfxClusterLighting();
    ::vfx::FrameContext makeVfxFrameContext(const RenderGraphContext& context,
                                          const FramePassResources& resources) const;
    void addDebugMaterialPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                              const RenderScene& scene, DebugDisplayMode debugMode);
    void addDebugLightmapPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                              const RenderScene& scene);
    void addDebugGBufferPass(RenderGraph& graph, const FramePassResources& resources, DebugDisplayMode debugMode);
    void addPresentationPass(RenderGraph& graph, const FramePassResources& resources,
                             RenderGraphTextureHandle presentationColor, DebugDisplayMode debugMode,
                             RenderGraphTextureHandle bloomColor = {}, RenderGraphTextureHandle taaHistory = {},
                             RenderGraphTextureHandle dofCoc = {});
    bool drawShadowBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list, const DrawBatch& batch,
                         const RenderScene& scene);
    bool drawForwardBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list, const DrawBatch& batch,
                          const RenderScene& scene, gfx::TextureHandle contactShadow, bool clusterList);
    bool renderGpuShadowPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                             const RenderScene& scene, const ShadowRequest& request, uint32_t requestIndex);
    void renderShadowPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                          const RenderScene& scene, uint32_t requestIndex, const DrawList& drawList,
                          bool clearDepth);
    void renderPrepass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                       const RenderScene& scene, bool depthOnly);
    void renderObjectsMotionVectors(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                    const RenderScene& scene);
    void renderTransparentDepthPrepass(gfx::Encoder& encoder, bgfx::ViewId view,
                                       gfx::FrameBufferHandle frameBuffer, const RenderScene& scene);
    void renderTransparentDepthPrepass(gfx::Encoder& encoder, bgfx::ViewId view,
                                       gfx::FrameBufferHandle frameBuffer, const RenderScene& scene,
                                       const DrawList& list);
    void renderTransparentMotionVectors(gfx::Encoder& encoder, bgfx::ViewId view,
                                        gfx::FrameBufferHandle frameBuffer, const RenderScene& scene,
                                        bool objectMotion);
    void renderCameraMotionVectors(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                   gfx::TextureHandle depth, bool cameraOnly);
    void renderDepthPyramidMip(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                               gfx::TextureHandle source, uint16_t sourceWidth, uint16_t sourceHeight, uint16_t width,
                               uint16_t height, uint8_t sourceMip, bool copyDepth);
    void renderSkyPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                       gfx::TextureHandle environment);
    void renderGBufferPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                           const RenderScene& scene, bool decals, bool gpuDriven,
                           gfx::TextureHandle sceneDepth);
    void renderGBufferClear(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                            bool useShader);
    void renderDecalClear(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer);
    void renderDecalPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                         const RenderScene& scene, std::size_t decalIndex, gfx::TextureHandle depthPyramid);
    void updateDecalUniforms(const Decal& decal);
    void renderSSAOPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                        uint16_t width, uint16_t height, gfx::TextureHandle depthPyramid,
                        gfx::TextureHandle normalBuffer);
    void renderSSAOBlurPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                            uint16_t width, uint16_t height, gfx::TextureHandle ssaoInput,
                            gfx::TextureHandle depthPyramid, gfx::TextureHandle normalBuffer);
    void renderSSAOTemporalPass(gfx::Encoder& encoder, bgfx::ViewId view,
                                gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                                gfx::TextureHandle ssaoInput, gfx::TextureHandle depthPyramid,
                                gfx::TextureHandle motionVectors, gfx::TextureHandle history,
                                bool historyValid);
    void renderSSAOUpsamplePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                uint16_t width, uint16_t height, gfx::TextureHandle ssaoInput,
                                gfx::TextureHandle depthPyramid, gfx::TextureHandle normalBuffer);
    void renderSSRTracePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                            uint16_t width, uint16_t height, gfx::TextureHandle sceneColor,
                            gfx::TextureHandle depthPyramid, gfx::TextureHandle gbuffer0,
                            gfx::TextureHandle gbuffer1, gfx::TextureHandle gbuffer2,
                            gfx::TextureHandle environment, gfx::TextureHandle ssao);
    void renderSSRTemporalPass(gfx::Encoder& encoder, bgfx::ViewId view,
                               gfx::FrameBufferHandle frameBuffer, uint16_t width, uint16_t height,
                               gfx::TextureHandle ssrInput, gfx::TextureHandle depthPyramid,
                               gfx::TextureHandle motionVectors, gfx::TextureHandle history,
                               bool historyValid);
    void renderSSRCompositePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                uint16_t width, uint16_t height, gfx::TextureHandle ssr);
    void renderSSGITracePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                             uint16_t width, uint16_t height, gfx::TextureHandle sceneColor,
                             gfx::TextureHandle depthPyramid, gfx::TextureHandle gbuffer2,
                             gfx::TextureHandle environment);
    void renderSSGIDenoisePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                               uint16_t width, uint16_t height, gfx::TextureHandle ssgiRaw,
                               gfx::TextureHandle depthPyramid);
    void renderSSGITemporalPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                uint16_t width, uint16_t height, gfx::TextureHandle ssgiRaw,
                                gfx::TextureHandle depthPyramid, gfx::TextureHandle motionVectors,
                                gfx::TextureHandle history, gfx::TextureHandle historyDepth,
                                gfx::TextureHandle historyNormal, gfx::TextureHandle currentNormal,
                                bool historyValid);
    void renderSSGICompositePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                 uint16_t width, uint16_t height, gfx::TextureHandle ssgi,
                                 gfx::TextureHandle depthPyramid, gfx::TextureHandle gbuffer0,
                                 gfx::TextureHandle gbuffer1, gfx::TextureHandle ssao,
                                 gfx::TextureHandle bakedIndirect);
    void renderContactShadowPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::TextureHandle output,
                                 gfx::TextureHandle depth, gfx::TextureHandle lightData,
                                 gfx::TextureHandle lightList, bool gbufferTarget, bool halfResolution,
                                 uint16_t width, uint16_t height);
    void renderDeferredLighting(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                gfx::TextureHandle depthPyramid, const std::array<gfx::TextureHandle, 5>& gbuffer,
                                gfx::TextureHandle lightData,
                                gfx::TextureHandle lightList, gfx::TextureHandle directionalShadow,
                                gfx::TextureHandle punctualShadow, gfx::BufferHandle shadowContext,
                                gfx::TextureHandle environment, gfx::TextureHandle ssao, bool advanced,
                                bool advancedIms);
    void renderForwardPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                           const RenderScene& scene, bool transparent, bool forwardOnly,
                           gfx::TextureHandle contactShadow, gfx::TextureHandle sceneDepth,
                           gfx::TextureHandle refractionSource, bool gpuDriven,
                           const DrawList* listOverride = nullptr);
    void renderWireframePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                             const RenderScene& scene);
    void renderDebugMaterialPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                 const RenderScene& scene, DebugDisplayMode debugMode);
    void renderDebugGBufferPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                const std::array<gfx::TextureHandle, 5>& gbuffer, gfx::TextureHandle depth,
                                DebugDisplayMode debugMode);
    void renderPresentation(gfx::Encoder& encoder, bgfx::ViewId view, gfx::TextureHandle color,
                            gfx::TextureHandle depth, gfx::TextureHandle lightData, gfx::TextureHandle lightList,
                            gfx::TextureHandle normal, gfx::TextureHandle motionVector, gfx::TextureHandle depthPyramid,
                            gfx::TextureHandle directionalShadow, gfx::TextureHandle punctualShadow,
                            gfx::BufferHandle shadowContext, gfx::TextureHandle contactShadow,
                            gfx::TextureHandle ssao, gfx::TextureHandle ssr,
                            gfx::TextureHandle ssgi, gfx::TextureHandle decal0, gfx::BufferHandle fullscreen,
                            DebugDisplayMode debugMode,
                            gfx::TextureHandle exposureValue = {}, gfx::TextureHandle exposureHistogram = {},
                            gfx::TextureHandle colorGradingLut = {}, gfx::TextureHandle bloom = {},
                            gfx::TextureHandle taaHistory = {}, gfx::TextureHandle dofCoc = {});

    gfx::ResourceManager* m_resources = nullptr;
    MaterialSystem m_materialSystem;
    GraphMaterialSystem m_graphMaterialSystem;
    RenderGraph m_renderGraph{"Forward Renderer"};
    shader::ShaderLibrary m_shaderLibrary;
    MaterialSamplers m_materialSamplers;
    lightmap::LightmapSet m_lightmapSet;
    gfx::UniformHandle m_lightmapSampler;
    gfx::generated::GlobalUniformBinder m_globalBinder;
    gfx::generated::PassUniformBinder m_passBinder;
    gfx::generated::ClusterUniformBinder m_clusterBinder;
    gfx::generated::SsaoUniformBinder m_ssaoBinder;
    gfx::generated::SsrUniformBinder m_ssrBinder;
    gfx::generated::SsgiUniformBinder m_ssgiBinder;
    gfx::generated::DecalUniformBinder m_decalBinder;
    gfx::generated::MaterialUniformBinder m_materialBinder;
    gfx::generated::ObjectUniformBinder m_objectBinder;
    gfx::generated::PrepassUniformBinder m_prepassBinder;
    gfx::generated::EnvironmentUniformBinder m_environmentBinder;
    gfx::generated::ProbeUniformBinder m_probeBinder;
    gfx::generated::DebugUniformBinder m_debugBinder;
    gfx::generated::FogUniformBinder m_fogBinder;
    gfx::generated::ContactUniformBinder m_contactBinder;

    std::array<gfx::TextureHandle, 5> m_gbufferTextures{};
    bool m_deferredLightLayersActive = false;
    std::array<gfx::UniformHandle, 5> m_gbufferSamplers{};
    uint16_t m_gbufferWidth = 0;
    uint16_t m_gbufferHeight = 0;
    std::array<gfx::TextureHandle, 4> m_decalTextures{};
    std::array<gfx::TextureHandle, 3> m_decalFallbackTextures{};
    std::array<gfx::UniformHandle, 4> m_decalSamplers{};
    gfx::UniformHandle m_decalAlbedoSampler;
    gfx::UniformHandle m_decalNormalSampler;
    gfx::UniformHandle m_decalMaskSampler;
    uint16_t m_decalWidth = 0;
    uint16_t m_decalHeight = 0;
    gfx::BufferHandle m_fullscreenVertexBuffer;
    gfx::UniformHandle m_hdrSampler;
    gfx::TextureHandle m_environmentFallback;
    gfx::UniformHandle m_environmentSampler;
    gfx::UniformHandle m_probeArraySampler;
    gfx::TextureHandle m_probeArrayFallback;
    gfx::UniformHandle m_cookieAtlasSampler;
    gfx::TextureHandle m_cookieAtlasTexture;
    gfx::TextureHandle m_cookieAtlasFallback;
    gfx::TextureHandle m_ssaoFallback;
    gfx::TextureHandle m_contactShadowFallback;
    gfx::UniformHandle m_ssaoSampler;
    gfx::UniformHandle m_ssaoInputSampler;
    gfx::UniformHandle m_ssaoHistorySampler;
    gfx::UniformHandle m_ssrSampler;
    gfx::UniformHandle m_ssrHistorySampler;
    gfx::UniformHandle m_bakedIndirectSampler;
    gfx::UniformHandle m_refractionSourceDepthSampler;
    gfx::UniformHandle m_refractionPreColorSampler;
    gfx::UniformHandle m_bakedGICompositeModeUniform;
    gfx::TextureHandle m_bakedIndirectFallback;
    gfx::UniformHandle m_ssgiSampler;
    gfx::UniformHandle m_ssgiRawSampler;
    gfx::UniformHandle m_ssgiHistorySampler;
    gfx::UniformHandle m_ssgiHistoryDepthSampler;
    gfx::UniformHandle m_ssgiHistoryNormalSampler;
    gfx::TextureHandle m_lightDataTexture;
    gfx::UniformHandle m_lightDataSampler;
    std::vector<Vec4> m_lightDataUpload;
    gfx::TextureHandle m_lightListTexture;
    gfx::TextureHandle m_clusterTexture;
    gfx::TextureHandle m_lightScreenBoundsTexture;
    gfx::TextureHandle m_lightBigTileTexture;
    gfx::TextureHandle m_lightViewRowsTexture;
    gfx::BufferHandle m_lightScreenBoundsBuffer;
    gfx::BufferHandle m_lightBigTileBuffer;
    gfx::BufferHandle m_lightFptlBuffer;
    gfx::BufferHandle m_lightClusterBuffer;
    gfx::BufferHandle m_lightIndirectArgsBuffer;
    gfx::BufferHandle m_activeLightListBuffer;
    gfx::UniformHandle m_lightListSampler;
    gfx::UniformHandle m_lightViewRowsSampler;
    gfx::UniformHandle m_lightScreenBoundsSampler;
    gfx::UniformHandle m_lightBigTileSampler;
    gfx::UniformHandle m_shadowSampler;
    gfx::UniformHandle m_shadowPunctualSampler;
    gfx::UniformHandle m_shadowPointSampler;
    gfx::UniformHandle m_shadowPointPunctualSampler;
    gfx::UniformHandle m_shadowCopySampler;
    gfx::UniformHandle m_shadowCopyParamsUniform;
    gfx::UniformHandle m_shadowMomentSlotUniform;
    gfx::UniformHandle m_shadowMomentParamsUniform;
    gfx::UniformHandle m_shadowMomentDepthSampler;
    gfx::UniformHandle m_shadowMomentSampler;
    gfx::TextureHandle m_shadowMomentSatFallback;
    gfx::UniformHandle m_contactShadowSampler;
    gfx::UniformHandle m_depthSourceSampler;
    gfx::UniformHandle m_normalSampler;
    gfx::UniformHandle m_motionVectorSampler;
    gfx::UniformHandle m_depthPyramidSampler;
    gfx::UniformHandle m_depthDebugSampler;
    gfx::UniformHandle m_shadowAtlasSampler;
    gfx::UniformHandle m_fogSourceSampler;
    gfx::UniformHandle m_fogHistorySampler;
    gfx::UniformHandle m_fogLowResSampler;
    std::array<gfx::UniformHandle, kMaxLocalFogMasks> m_fogMaskSamplers{};
    std::array<gfx::TextureHandle, kMaxLocalFogMasks> m_fogMaskTextures{};
    gfx::TextureHandle m_fogMaskFallback;
    std::vector<int> m_localFogMaskSlots;
    uint32_t m_localFogMaskCount = 0;

    gfx::generated::GlobalUniforms m_globalUniforms;
    gfx::generated::PassUniforms m_passUniforms;
    gfx::generated::ClusterUniforms m_clusterUniforms;
    gfx::generated::SsaoUniforms m_ssaoUniforms;
    gfx::generated::SsrUniforms m_ssrUniforms;
    gfx::generated::SsgiUniforms m_ssgiUniforms;
    gfx::generated::DecalUniforms m_decalUniforms;
    gfx::generated::ObjectUniforms m_objectUniforms;
    gfx::generated::PrepassUniforms m_prepassUniforms;
    gfx::generated::EnvironmentUniforms m_environmentUniforms;
    gfx::generated::ProbeUniforms m_probeUniforms;
    gfx::generated::PostProcessUniforms m_postProcessUniforms;
    gfx::generated::DebugUniforms m_debugUniforms;
    gfx::generated::FogUniforms m_fogUniforms;
    gfx::generated::ContactUniforms m_contactUniforms;
    DrawList m_opaqueDrawList;
    DrawList m_forwardOpaqueDrawList;
    DrawList m_bakedGIDrawList;
    DrawList m_transparentDrawList;
    DrawList m_transparentPreRefractionDrawList;
    DrawList m_transparentRefractionDrawList;
    DrawList m_transparentBackfaceDrawList;
    DrawList m_shadowDrawList;
    DrawList m_shadowStaticDrawList;
    DrawList m_shadowDynamicDrawList;
    DrawList m_prepassDrawList;
    DrawList m_gbufferDrawList;
    DrawList m_motionVectorDrawList;
    DrawList m_transparentDepthPrepassDrawList;
    DrawList m_transparentPreRefractionDepthPrepassDrawList;
    DrawList m_transparentRefractionDepthPrepassDrawList;
    DrawList m_transparentMotionVectorDrawList;
    DrawList m_wireframeDrawList;
    ViewState m_viewState;
    Camera m_camera;
    Vec3 m_cameraPosition{0.0f, 0.0f, 0.0f};
    Vec3 m_cameraForward{0.0f, 0.0f, -1.0f};
    Vec3 m_ambientColor{0.08f, 0.09f, 0.11f};
    LightBuilder m_lightBuilder;
    CookieAtlas m_cookieAtlas;
    uint64_t m_cookieAtlasUploadHash = 0;
    bool m_cookieAtlasUploaded = false;
    LightListBuilder m_lightListBuilder;
    LightListBuilder m_clusterStatsReferenceBuilder;
    bool m_clusterStatsReferenceValid = false;
    ShadowManager m_shadowManager;
    ShadowSettings m_shadowSettings;
    SkyManager m_skyManager;
    ProbeManager m_probeManager;
    PipelineSettings m_pipelineSettings;
    FrameSettings m_frameSettings;
    PostProcessSettings m_postProcessSettings;
    PostProcessChain m_postProcessChain;
    SsaoSettings m_ssaoSettings;
    SsrSettings m_ssrSettings;
    SsgiSettings m_ssgiSettings;
    VolumetricFogSettings m_volumetricFogSettings;
    DebugDisplayMode m_debugDisplayMode = DebugDisplayMode::None;
    int m_debugLightClusterSlice = -1;
    bool m_debugLightmapMask = false;
    mutable bool m_formatWarningLogged = false;
    bool m_shadowsEnabled = true;
    bool m_lightClusterEnabled = true;
    bool m_gpuLightListEnabled = false;
    bool m_gpuLightListActive = false;
    bool m_gpuLightListBufferEnabled = false;
    bool m_gpuLightListBufferActive = false;
    bool m_gpuLightListIndirectActive = false;
    bool m_gpuLightListWarningLogged = false;
    bool m_graphLightListTextureLogged = false;
    bool m_asyncGpuLightListWarningLogged = false;
    bool m_gpuSkinningEnabled = true;
    bool m_gpuSkinningActive = false;
    bool m_gpuSkinningWarningLogged = false;
    uint32_t m_gpuSkinningMeshes = 0;
    uint32_t m_gpuSkinningVertices = 0;
    uint32_t m_gpuSkinningDispatches = 0;
    gfx::UniformHandle m_skinParamsUniform;
    bool m_lightListCapacityWarningLogged = false;
    uint32_t m_lightListCapacity = kLightListDefaultCapacity;
    uint32_t m_lightListTextureCapacity = kLightListDefaultCapacity;
    uint32_t m_lightListEffectiveCapacity = kLightListDefaultCapacity;
    uint32_t m_clusterCapacity = 0;
    uint32_t m_clusterIndexCapacity = 0;
    uint32_t m_clusterOffsetCount = 0;
    uint16_t m_clusterTextureWidth = 0;
    uint32_t m_gpuLightListPasses = 0;
    double m_lightListCpuMs = 0.0;
    ClusterStats m_clusterStats;
    bool m_clusterStatsReadbackRequested = false;
    bool m_clusterStatsReadbackPending = false;
    bool m_clusterStatsReadbackTexturePath = false;
    uint32_t m_clusterStatsReadbackFrame = 0;
    uint32_t m_clusterStatsReadbackOffsetCount = 0;
    uint32_t m_clusterStatsReadbackFlagOffset = 0;
    uint32_t m_clusterStatsReadbackFlagCount = 0;
    uint32_t m_clusterStatsReadbackTotalCount = 0;
    gfx::TextureHandle m_clusterStatsReadbackTexture;
    std::vector<uint32_t> m_clusterStatsReadbackData;
    bool m_wireframe = false;
    bool m_jitterEnabled = false;
    bool m_depthPrepassActive = false;
    bool m_bakedGIActive = false;
    bool m_deferredActive = false;
    bool m_advancedMaterialActive = false;
    bool m_advancedLightingActive = false;
    bool m_litVariantsEnabled = false;
    bool m_litVariantsActive = false;
    bool m_litVariantTilesEnabled = false;
    bool m_litVariantTileActive = false;
    bool m_litVariantTileWarningLogged = false;
    uint32_t m_litTileListBase = 0;
    uint32_t m_litTileListCount = 0;
    uint16_t m_litTileWidth = 0;
    uint16_t m_litTileHeight = 0;
    gfx::TextureHandle m_litTileFeatureFlagsTexture;
    gfx::BufferHandle m_litIndirectArgsBuffer;
    bool m_litVariantsWarningLogged = false;
    bool m_litVariantImsWarningLogged = false;
    uint32_t m_litLightFlags = 0;
    uint32_t m_usedLitVariantMask = 0;
    bool m_volumetricFogActive = false;
    uint32_t m_volumetricFogSteps = 0;
    uint16_t m_volumetricFogWidth = 0;
    uint16_t m_volumetricFogHeight = 0;
    bool m_volumetricFogTemporalActive = false;
    uint32_t m_volumetricFogPunctualLights = 0;
    uint32_t m_volumetricFogLocalVolumes = 0;
    uint32_t m_volumetricFogLocalMasks = 0;
    gfx::BufferHandle m_shadowRequestBuffer;
    gfx::BufferHandle m_shadowCasterBuffer;
    gfx::BufferHandle m_shadowBatchBuffer;
    gfx::BufferHandle m_shadowInstanceBuffer;
    gfx::BufferHandle m_shadowIndirectBuffer;
    gfx::BufferHandle m_shadowStatsBuffer;
    uint32_t m_shadowGpuRequestCount = 0;
    uint32_t m_shadowGpuCasterCount = 0;
    uint32_t m_shadowGpuBatchCount = 0;
    bool m_shadowGpuCullingActive = false;
    bool m_shadowGpuIndirectActive = false;
    uint32_t m_shadowGpuCullDispatches = 0;
    uint32_t m_shadowIndirectDraws = 0;
    bool m_shadowGpuWarningLogged = false;
    bool m_areaEvsmBlurWarningLogged = false;
    uint32_t m_shadowDrawnItems = 0;
    uint32_t m_shadowCulledItems = 0;
    uint16_t m_contactShadowWidth = 0;
    uint16_t m_contactShadowHeight = 0;
    bool m_contactShadowLogged = false;
    bool m_volumetricFogLogged = false;
    bool m_localFogVolumeWarningLogged = false;
    std::array<gfx::TextureHandle, 2> m_fogHistoryTextures{};
    uint16_t m_fogHistoryWidth = 0;
    uint16_t m_fogHistoryHeight = 0;
    uint32_t m_fogHistoryIndex = 0;
    uint32_t m_fogHistoryFrame = 0;
    bool m_fogHistoryValid = false;
    std::array<gfx::TextureHandle, 2> m_ssaoHistoryTextures{};
    uint16_t m_ssaoHistoryWidth = 0;
    uint16_t m_ssaoHistoryHeight = 0;
    uint32_t m_ssaoHistoryIndex = 0;
    uint32_t m_ssaoHistoryFrame = 0;
    bool m_ssaoHistoryValid = false;
    bool m_ssaoTemporalActive = false;
    bool m_ssaoTemporalFallbackLogged = false;
    std::array<gfx::TextureHandle, 2> m_ssrHistoryTextures{};
    uint16_t m_ssrHistoryWidth = 0;
    uint16_t m_ssrHistoryHeight = 0;
    uint32_t m_ssrHistoryIndex = 0;
    uint32_t m_ssrHistoryFrame = 0;
    bool m_ssrHistoryValid = false;
    bool m_ssrTemporalActive = false;
    std::array<gfx::TextureHandle, 2> m_ssgiHistoryTextures{};
    std::array<gfx::TextureHandle, 2> m_ssgiHistoryDepthTextures{};
    std::array<gfx::TextureHandle, 2> m_ssgiHistoryNormalTextures{};
    uint16_t m_ssgiHistoryWidth = 0;
    uint16_t m_ssgiHistoryHeight = 0;
    uint32_t m_ssgiHistoryIndex = 0;
    uint32_t m_ssgiHistoryFrame = 0;
    bool m_ssgiHistoryValid = false;
    bool m_ssgiTemporalActive = false;
    mutable bool m_deferredFallbackLogged = false;
    mutable bool m_decalFallbackLogged = false;
    float m_renderScale = 1.0f;
    bool m_preserveHistoryOnScaleChange = false;
    float m_lastRenderTime = 0.0f;
    double m_postProcessWallTotalMs = 0.0;
    uint32_t m_postProcessWallFrames = 0;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    uint16_t m_depthPyramidMipCount = 1;
    bool m_depthPyramidComputeActive = false;
    bool m_depthPyramidComputeWarningLogged = false;
    bool m_depthPyramidFormatWarningLogged = false;
    bool m_depthPyramidMaxComputeActive = false;
    bool m_depthPyramidMaxWarningLogged = false;
    bool m_gpuDrivenRequested = false;
    bool m_gpuDrivenWarningLogged = false;
    bool m_gpuDrivenIndirectCountSupported = false;
    uint32_t m_gpuDrivenCullingPasses = 0;
    GpuDrivenDrawData m_gpuDrivenForward;
    GpuDrivenDrawData m_gpuDrivenGbuffer;
    GpuDrivenDrawData m_gpuDrivenTransparent;
    bool m_gpuDrivenTransparentSortActive = false;
    GpuDrivenDrawData m_gpuDrivenPrepass;
    GpuDrivenDrawData m_gpuDrivenPrepassSecond;
    std::array<gfx::TextureHandle, 2> m_hiZPyramidTextures{};
    uint16_t m_hiZPyramidWidth = 0;
    uint16_t m_hiZPyramidHeight = 0;
    uint8_t m_hiZPyramidMipCount = 0;
    uint32_t m_hiZPyramidIndex = 0;
    bool m_hiZHistoryValid = false;
    bool m_gpuDrivenPrepassActive = false;
    gfx::UniformHandle m_gpuDrivenParamsUniform;
    gfx::UniformHandle m_gpuDrivenCullModeUniform;
    gfx::UniformHandle m_gpuSortParamsUniform;
    gfx::UniformHandle m_bindlessMaterialUniform;
    uint32_t m_frameIndex = 0;
    bool m_hasLastRenderTime = false;
    bool m_valid = false;
    bool m_vfxActive = false;
    bool m_vfxInitialized = false;
    bool m_vfxSceneInitialized = false;
    bool m_vfxHasLitOutputs = false;
    bool m_vfxHasShadowCasters = false;
    bool m_vfxClusterReady = false;
    bool m_vfxTransparentSortLogged = false;
    float m_vfxDeltaSeconds = 0.0f;
    Vec4 m_vfxLightListParams{};
    Vec4 m_vfxLightListParams2{};
    Vec4 m_vfxLightScreenSize{};
    gfx::generated::ClusterUniforms m_vfxClusterUniforms{};
    std::unique_ptr<hbg::vfxbridge::HbgVfxBackend> m_vfxBackend;
    std::unique_ptr<::vfx::Runtime> m_vfxRuntime;
    std::vector<VfxInstanceRuntime> m_vfxInstanceRuntimes;
    std::vector<std::unique_ptr<DrawList>> m_vfxTransparentSegments;
};

} // namespace hbg::render
