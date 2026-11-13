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
#include "render/lighting/light_builder.h"
#include "render/lighting/light_list_builder.h"
#include "render/material/material_system.h"
#include "render/pipeline/frame_settings.h"
#include "render/pipeline/pipeline_settings.h"
#include "render/postprocess/post_process_chain.h"
#include "render/postprocess/post_process_settings.h"
#include "render/postprocess/ssao_settings.h"
#include "render/postprocess/ssr_settings.h"
#include "render/postprocess/ssgi_settings.h"
#include "render/scene/render_scene.h"
#include "render/shadows/shadow_manager.h"
#include "render/sky/probe_manager.h"
#include "render/sky/sky_manager.h"
#include "shader/shader_library.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hbg::gfx {
class Encoder;
}

namespace hbg::render {

class ForwardRenderer {
  public:
    struct FrameDiagnostics {
        uint32_t passCount = 0;
        uint32_t gbufferTargets = 0;
        uint32_t postProcessPasses = 0;
        bool deferred = false;
        bool stencil = false;
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
        uint32_t volumetricFogPunctualLights = 0;
        uint32_t volumetricFogLocalVolumes = 0;
        uint32_t volumetricFogLocalMasks = 0;
        uint32_t shadowDrawnItems = 0;
        uint32_t shadowCulledItems = 0;
        bool shadowCacheActive = false;
        uint32_t shadowCachedRequests = 0;
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
        bool gpuShadowCulling = false;
        bool gpuShadowIndirect = false;
        uint32_t gpuShadowCullDispatches = 0;
        uint32_t gpuShadowIndirectDraws = 0;
        bool asyncGpuLightListRequested = false;
        bool asyncGpuLightListActive = false;
    };

    ForwardRenderer() = default;
    ~ForwardRenderer() = default;

    ForwardRenderer(const ForwardRenderer&) = delete;
    ForwardRenderer& operator=(const ForwardRenderer&) = delete;

    bool init(gfx::ResourceManager& resources, const RenderScene& scene, uint16_t width, uint16_t height);
    bool setSky(const std::string& path, float intensity = 1.0f, float rotationDegrees = 0.0f);
    void resize(uint16_t width, uint16_t height);
    void shutdown(gfx::ResourceManager& resources);

    void render(const RenderScene& scene, uint16_t width, uint16_t height, float time);
    bool valid() const { return m_valid; }
    void setDebugLightData(bool enabled) {
        m_debugDisplayMode = enabled ? DebugDisplayMode::LightData : DebugDisplayMode::None;
    }
    void setDebugDisplayMode(DebugDisplayMode mode) {
        m_debugDisplayMode = mode;
        m_shadowManager.setCacheSuppressed(mode == DebugDisplayMode::ShadowAtlasPunctual);
    }
    void setDebugLightClusterSlice(int slice) { m_debugLightClusterSlice = slice; }
    void setLightClusterEnabled(bool enabled) { m_lightClusterEnabled = enabled; }
    void setGpuLightListEnabled(bool enabled);
    void setGpuLightListBufferEnabled(bool enabled);
    bool gpuLightListActive() const { return m_gpuLightListActive; }
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

    shader::ShaderLibrary& shaderLibrary() { return m_shaderLibrary; }
    const shader::ShaderLibrary& shaderLibrary() const { return m_shaderLibrary; }

  private:
    struct FramePassResources {
        RenderGraphTextureHandle color;
        RenderGraphTextureHandle depth;
        RenderGraphTextureHandle normal;
        RenderGraphTextureHandle motionVector;
        RenderGraphTextureHandle depthPyramid;
        RenderGraphTextureHandle lightData;
        RenderGraphTextureHandle lightList;
        RenderGraphTextureHandle clusterLightList;
        RenderGraphTextureHandle lightScreenBounds;
        RenderGraphTextureHandle lightBigTile;
        RenderGraphBufferHandle lightScreenBoundsBuffer;
        RenderGraphBufferHandle lightBigTileBuffer;
        RenderGraphBufferHandle lightFptlBuffer;
        RenderGraphBufferHandle lightClusterBuffer;
        RenderGraphBufferHandle lightIndirectArgs;
        RenderGraphBufferHandle shadowRequestBuffer;
        RenderGraphBufferHandle shadowCasterBuffer;
        RenderGraphBufferHandle shadowBatchBuffer;
        RenderGraphBufferHandle shadowInstanceBuffer;
        RenderGraphBufferHandle shadowIndirectBuffer;
        RenderGraphBufferHandle shadowStatsBuffer;
        RenderGraphTextureHandle directionalShadow;
        RenderGraphTextureHandle punctualShadow;
        RenderGraphTextureHandle shadowContext;
        RenderGraphTextureHandle contactShadow;
        RenderGraphTextureHandle environment;
        RenderGraphTextureHandle skyEnvironment;
        RenderGraphTextureHandle ssao;
        RenderGraphTextureHandle ssaoRaw;
        RenderGraphTextureHandle ssaoBlur;
        RenderGraphTextureHandle ssr;
        RenderGraphTextureHandle ssgiRaw;
        RenderGraphTextureHandle ssgi;
        RenderGraphBufferHandle fullscreen;
        std::array<RenderGraphTextureHandle, 3> decal{};
        std::array<RenderGraphTextureHandle, 4> gbuffer{};
    };

    bool shadowsActive() const;
    bool contactShadowsActive() const;
    bool motionVectorsActive() const;
    bool objectMotionVectorsActive() const;
    const char* deferredUnavailableReason() const;
    bool ensureGBufferTextures(uint16_t renderWidth, uint16_t renderHeight);
    void releaseGBufferTextures();
    bool ensureDecalTextures(uint16_t renderWidth, uint16_t renderHeight);
    void releaseDecalTextures();
    gfx::TextureFormat selectHdrFormat() const;
    gfx::TextureFormat selectDepthFormat() const;
    gfx::TextureFormat selectNormalFormat() const;
    gfx::TextureFormat selectMotionVectorFormat() const;
    gfx::TextureFormat selectDepthPyramidFormat() const;
    gfx::TextureFormat selectSsaoFormat() const;
    void buildDrawLists(const RenderScene& scene);
    const DrawList& forwardDrawList(bool transparent) const;
    void updateCamera(const RenderScene& scene, uint16_t width, uint16_t height);
    void syncTaaJitter();
    void updateGlobalUniforms();
    void updateLights(const RenderScene& scene);
    void uploadLightData();
    bool ensureLightListTexture(uint16_t renderWidth, uint16_t renderHeight);
    bool ensureLightBigTileTexture(uint16_t renderWidth, uint16_t renderHeight);
    bool ensureClusterLightListTexture(uint16_t renderWidth, uint16_t renderHeight);
    bool ensureLightListBuffers(uint16_t renderWidth, uint16_t renderHeight);
    void releaseLightListBuffers();
    bool ensureShadowGpuBuffers(uint32_t requestCount, uint32_t casterCount, uint32_t batchCount);
    void releaseShadowGpuBuffers();
    bool updateShadowGpuData(const RenderScene& scene);
    void addGpuShadowCullPass(RenderGraph& graph, const FramePassResources& resources);
    void updateLightList();
    void updateCpuLightList();
    bool uploadCpuLightList();
    void uploadClusterLightList();
    void releaseLightListTexture();
    void releaseLightBigTileTexture();
    void releaseClusterLightListTexture();
    bool gpuLightListPathAvailable();
    gfx::ProgramHandle lightListProgram(shader::PassId pass, uint64_t materialKey = 0);
    void bindLightList(gfx::Encoder& encoder, bool cluster) const;
    void bindLightListTexture(gfx::Encoder& encoder, bool cluster) const;
    void updateEnvironmentUniforms();
    void bindProbeTextures(gfx::Encoder& encoder) const;
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
    void addShadowPass(RenderGraph& graph, RenderGraphTextureHandle directionalShadow,
                       RenderGraphTextureHandle punctualShadow, const RenderScene& scene);
    void addGpuLightListPasses(RenderGraph& graph, const FramePassResources& resources);
    RenderGraphTextureHandle addContactShadowPass(RenderGraph& graph, const FramePassResources& resources,
                                                  bool deferred);
    void addShadowAtlasClearPass(RenderGraph& graph, RenderGraphTextureHandle atlas, uint16_t size);
    void addClearPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                      bool debugBlack);
    void addPrepassPass(RenderGraph& graph, RenderGraphTextureHandle normal, RenderGraphTextureHandle depth,
                        const RenderScene& scene, bool depthOnly);
    void addObjectsMotionVectorsPass(RenderGraph& graph, RenderGraphTextureHandle motionVector,
                                     RenderGraphTextureHandle depth, const RenderScene& scene);
    void addCameraMotionVectorsPass(RenderGraph& graph, RenderGraphTextureHandle motionVector,
                                    RenderGraphTextureHandle depth, RenderGraphTextureHandle depthPyramid);
    void addDepthPyramidPasses(RenderGraph& graph, RenderGraphTextureHandle depth,
                               RenderGraphTextureHandle depthPyramid);
    void addSkyPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle environment);
    RenderGraphTextureHandle addVolumetricFogPass(RenderGraph& graph, const FramePassResources& resources,
                                                  RenderGraphTextureHandle color, const RenderScene& scene);
    void addGBufferClearPass(RenderGraph& graph, const std::array<RenderGraphTextureHandle, 4>& gbuffer,
                             RenderGraphTextureHandle depth, RenderGraphBufferHandle fullscreen);
    void addDecalPasses(RenderGraph& graph, const FramePassResources& resources, const RenderScene& scene);
    void addGBufferPass(RenderGraph& graph, const FramePassResources& resources, const RenderScene& scene);
    void addSSAOPasses(RenderGraph& graph, const FramePassResources& resources);
    void addSSRPasses(RenderGraph& graph, const FramePassResources& resources);
    void addSSGIPasses(RenderGraph& graph, const FramePassResources& resources);
    void addDeferredLightingPass(RenderGraph& graph, const FramePassResources& resources);
    void recordForwardFrame(RenderGraph& graph, FramePassResources& resources, const RenderScene& scene,
                            DebugDisplayMode debugMode, bool materialDebug);
    void recordDeferredFrame(RenderGraph& graph, FramePassResources& resources, const RenderScene& scene,
                             DebugDisplayMode debugMode, bool materialDebug);
    void addForwardPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                        RenderGraphTextureHandle lightData, RenderGraphTextureHandle lightList,
                        RenderGraphTextureHandle clusterLightList, RenderGraphBufferHandle lightFptlBuffer,
                        RenderGraphBufferHandle lightClusterBuffer, RenderGraphTextureHandle directionalShadow,
                        RenderGraphTextureHandle punctualShadow, RenderGraphTextureHandle shadowContext,
                        RenderGraphTextureHandle contactShadow, const RenderScene& scene, bool transparent,
                        bool forwardOnly = false);
    void addWireframePass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                          const RenderScene& scene);
    void addDebugMaterialPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                              const RenderScene& scene, DebugDisplayMode debugMode);
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
                          const RenderScene& scene, uint32_t requestIndex);
    void renderPrepass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                       const RenderScene& scene, bool depthOnly);
    void renderObjectsMotionVectors(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                    const RenderScene& scene);
    void renderCameraMotionVectors(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                   gfx::TextureHandle depth, bool cameraOnly);
    void renderDepthPyramidMip(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                               gfx::TextureHandle source, uint16_t sourceWidth, uint16_t sourceHeight, uint16_t width,
                               uint16_t height, uint8_t sourceMip, bool copyDepth);
    void renderSkyPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                       gfx::TextureHandle environment);
    void renderGBufferPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                           const RenderScene& scene, bool decals);
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
    void renderSSAOUpsamplePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                uint16_t width, uint16_t height, gfx::TextureHandle ssaoInput,
                                gfx::TextureHandle depthPyramid, gfx::TextureHandle normalBuffer);
    void renderSSRTracePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                            uint16_t width, uint16_t height, gfx::TextureHandle sceneColor,
                            gfx::TextureHandle depthPyramid, gfx::TextureHandle gbuffer0,
                            gfx::TextureHandle gbuffer1, gfx::TextureHandle gbuffer2,
                            gfx::TextureHandle environment, gfx::TextureHandle ssao);
    void renderSSRCompositePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                uint16_t width, uint16_t height, gfx::TextureHandle ssr);
    void renderSSGITracePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                             uint16_t width, uint16_t height, gfx::TextureHandle sceneColor,
                             gfx::TextureHandle depthPyramid, gfx::TextureHandle gbuffer2,
                             gfx::TextureHandle environment);
    void renderSSGIDenoisePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                               uint16_t width, uint16_t height, gfx::TextureHandle ssgiRaw,
                               gfx::TextureHandle depthPyramid);
    void renderSSGICompositePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                 uint16_t width, uint16_t height, gfx::TextureHandle ssgi,
                                 gfx::TextureHandle depthPyramid, gfx::TextureHandle gbuffer0,
                                 gfx::TextureHandle gbuffer1, gfx::TextureHandle ssao);
    void renderContactShadowPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::TextureHandle output,
                                 gfx::TextureHandle depth, gfx::TextureHandle lightData,
                                 gfx::TextureHandle lightList, bool gbufferTarget, bool halfResolution,
                                 uint16_t width, uint16_t height);
    void renderDeferredLighting(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                gfx::TextureHandle depthPyramid, const std::array<gfx::TextureHandle, 4>& gbuffer,
                                gfx::TextureHandle lightData, gfx::TextureHandle lightList,
                                gfx::TextureHandle directionalShadow, gfx::TextureHandle punctualShadow,
                                gfx::TextureHandle shadowContext, gfx::TextureHandle environment,
                                gfx::TextureHandle ssao);
    void renderForwardPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                           const RenderScene& scene, bool transparent, bool forwardOnly,
                           gfx::TextureHandle contactShadow);
    void renderWireframePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                             const RenderScene& scene);
    void renderDebugMaterialPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                 const RenderScene& scene, DebugDisplayMode debugMode);
    void renderDebugGBufferPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                const std::array<gfx::TextureHandle, 4>& gbuffer, gfx::TextureHandle depth,
                                DebugDisplayMode debugMode);
    void renderPresentation(gfx::Encoder& encoder, bgfx::ViewId view, gfx::TextureHandle color,
                            gfx::TextureHandle depth, gfx::TextureHandle lightData, gfx::TextureHandle lightList,
                            gfx::TextureHandle normal, gfx::TextureHandle motionVector, gfx::TextureHandle depthPyramid,
                            gfx::TextureHandle directionalShadow, gfx::TextureHandle punctualShadow,
                            gfx::TextureHandle shadowContext, gfx::TextureHandle contactShadow,
                            gfx::TextureHandle ssao, gfx::TextureHandle ssr,
                            gfx::TextureHandle ssgi, gfx::TextureHandle decal0, gfx::BufferHandle fullscreen,
                            DebugDisplayMode debugMode,
                            gfx::TextureHandle exposureValue = {}, gfx::TextureHandle exposureHistogram = {},
                            gfx::TextureHandle colorGradingLut = {}, gfx::TextureHandle bloom = {},
                            gfx::TextureHandle taaHistory = {}, gfx::TextureHandle dofCoc = {});

    gfx::ResourceManager* m_resources = nullptr;
    MaterialSystem m_materialSystem;
    RenderGraph m_renderGraph{"Forward Renderer"};
    shader::ShaderLibrary m_shaderLibrary;
    MaterialSamplers m_materialSamplers;
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

    std::array<gfx::TextureHandle, 4> m_gbufferTextures{};
    std::array<gfx::UniformHandle, 4> m_gbufferSamplers{};
    uint16_t m_gbufferWidth = 0;
    uint16_t m_gbufferHeight = 0;
    std::array<gfx::TextureHandle, 3> m_decalTextures{};
    std::array<gfx::TextureHandle, 3> m_decalFallbackTextures{};
    std::array<gfx::UniformHandle, 3> m_decalSamplers{};
    gfx::UniformHandle m_decalAlbedoSampler;
    gfx::UniformHandle m_decalNormalSampler;
    gfx::UniformHandle m_decalMaskSampler;
    uint16_t m_decalWidth = 0;
    uint16_t m_decalHeight = 0;
    gfx::BufferHandle m_fullscreenVertexBuffer;
    gfx::UniformHandle m_hdrSampler;
    gfx::TextureHandle m_environmentFallback;
    gfx::UniformHandle m_environmentSampler;
    std::array<gfx::UniformHandle, 2> m_probeSamplers{};
    gfx::TextureHandle m_ssaoFallback;
    gfx::TextureHandle m_contactShadowFallback;
    gfx::UniformHandle m_ssaoSampler;
    gfx::UniformHandle m_ssaoInputSampler;
    gfx::UniformHandle m_ssrSampler;
    gfx::UniformHandle m_ssgiSampler;
    gfx::UniformHandle m_ssgiRawSampler;
    gfx::TextureHandle m_lightDataTexture;
    gfx::UniformHandle m_lightDataSampler;
    gfx::TextureHandle m_lightListTexture;
    gfx::TextureHandle m_clusterLightListTexture;
    gfx::TextureHandle m_lightScreenBoundsTexture;
    gfx::TextureHandle m_lightBigTileTexture;
    gfx::TextureHandle m_lightViewRowsTexture;
    gfx::BufferHandle m_lightScreenBoundsBuffer;
    gfx::BufferHandle m_lightBigTileBuffer;
    gfx::BufferHandle m_lightFptlBuffer;
    gfx::BufferHandle m_lightClusterBuffer;
    gfx::BufferHandle m_lightIndirectArgsBuffer;
    gfx::BufferHandle m_activeLightListBuffer;
    gfx::BufferHandle m_activeClusterListBuffer;
    gfx::UniformHandle m_lightListSampler;
    gfx::UniformHandle m_lightViewRowsSampler;
    gfx::UniformHandle m_lightScreenBoundsSampler;
    gfx::UniformHandle m_lightBigTileSampler;
    gfx::UniformHandle m_shadowSampler;
    gfx::UniformHandle m_shadowPunctualSampler;
    gfx::UniformHandle m_shadowPointSampler;
    gfx::UniformHandle m_shadowPointPunctualSampler;
    gfx::UniformHandle m_shadowContextSampler;
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
    DrawList m_transparentDrawList;
    DrawList m_shadowDrawList;
    DrawList m_prepassDrawList;
    DrawList m_gbufferDrawList;
    DrawList m_motionVectorDrawList;
    DrawList m_wireframeDrawList;
    ViewState m_viewState;
    Camera m_camera;
    Vec3 m_cameraPosition{0.0f, 0.0f, 0.0f};
    Vec3 m_cameraForward{0.0f, 0.0f, -1.0f};
    Vec3 m_ambientColor{0.08f, 0.09f, 0.11f};
    LightBuilder m_lightBuilder;
    LightListBuilder m_lightListBuilder;
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
    mutable bool m_formatWarningLogged = false;
    bool m_shadowsEnabled = true;
    bool m_lightClusterEnabled = true;
    bool m_gpuLightListEnabled = false;
    bool m_gpuLightListActive = false;
    bool m_gpuLightListBufferEnabled = false;
    bool m_gpuLightListBufferActive = false;
    bool m_gpuLightListIndirectActive = false;
    bool m_gpuLightListWarningLogged = false;
    bool m_asyncGpuLightListWarningLogged = false;
    uint32_t m_lightListCapacity = kLightListDefaultCapacity;
    bool m_lightListCapacityWarningLogged = false;
    uint32_t m_gpuLightListPasses = 0;
    double m_lightListCpuMs = 0.0;
    bool m_wireframe = false;
    bool m_jitterEnabled = false;
    bool m_depthPrepassActive = false;
    bool m_deferredActive = false;
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
    mutable bool m_deferredFallbackLogged = false;
    mutable bool m_decalFallbackLogged = false;
    float m_renderScale = 1.0f;
    float m_lastRenderTime = 0.0f;
    double m_postProcessWallTotalMs = 0.0;
    uint32_t m_postProcessWallFrames = 0;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    uint16_t m_depthPyramidMipCount = 1;
    uint32_t m_frameIndex = 0;
    bool m_hasLastRenderTime = false;
    bool m_valid = false;
};

} // namespace hbg::render
