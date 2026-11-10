#pragma once

#include "gfx/buffer.h"
#include "gfx/generated/uniforms.h"
#include "gfx/resource_manager.h"
#include "gfx/texture.h"
#include "render/camera/view_state.h"
#include "render/culling/draw_list.h"
#include "render/debug/debug_display.h"
#include "render/graph/render_graph.h"
#include "render/lighting/light_builder.h"
#include "render/lighting/light_list_builder.h"
#include "render/material/material_system.h"
#include "render/pipeline/frame_settings.h"
#include "render/pipeline/pipeline_settings.h"
#include "render/postprocess/post_process_settings.h"
#include "render/scene/render_scene.h"
#include "render/shadows/shadow_manager.h"
#include "render/sky/sky_manager.h"
#include "shader/shader_library.h"

#include <cstdint>
#include <vector>

namespace hbg::gfx {
class Encoder;
}

namespace hbg::render {

class ForwardRenderer {
  public:
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
    void setDebugDisplayMode(DebugDisplayMode mode) { m_debugDisplayMode = mode; }
    void setShadowsEnabled(bool enabled) { m_shadowsEnabled = enabled; }
    void setWireframe(bool enabled) { m_wireframe = enabled; }
    void setRenderScale(float scale);
    float renderScale() const { return m_renderScale; }
    void setViewJitter(bool enabled) { m_jitterEnabled = enabled; }
    bool viewJitter() const { return m_jitterEnabled; }
    void setSettings(const PipelineSettings& pipelineSettings, const FrameSettings& frameSettings);
    void setPostProcessSettings(const PostProcessSettings& settings);
    const PipelineSettings& pipelineSettings() const { return m_pipelineSettings; }
    const FrameSettings& frameSettings() const { return m_frameSettings; }
    const PostProcessSettings& postProcessSettings() const { return m_postProcessSettings; }
    const ViewState& viewState() const { return m_viewState; }

    shader::ShaderLibrary& shaderLibrary() { return m_shaderLibrary; }
    const shader::ShaderLibrary& shaderLibrary() const { return m_shaderLibrary; }

  private:
    bool shadowsActive() const;
    bool motionVectorsActive() const;
    bool objectMotionVectorsActive() const;
    gfx::TextureFormat selectHdrFormat() const;
    gfx::TextureFormat selectDepthFormat() const;
    gfx::TextureFormat selectNormalFormat() const;
    gfx::TextureFormat selectMotionVectorFormat() const;
    gfx::TextureFormat selectDepthPyramidFormat() const;
    void buildDrawLists(const RenderScene& scene);
    const DrawList& forwardDrawList(bool transparent) const;
    void updateCamera(const RenderScene& scene, uint16_t width, uint16_t height);
    void updateGlobalUniforms();
    void updateLights(const RenderScene& scene);
    void uploadLightData();
    bool ensureLightListTexture(uint16_t renderWidth, uint16_t renderHeight);
    void updateLightList();
    void releaseLightListTexture();
    void updateEnvironmentUniforms();
    void updatePostProcessUniforms();
    void addShadowPass(RenderGraph& graph, RenderGraphTextureHandle directionalShadow,
                       RenderGraphTextureHandle punctualShadow, const RenderScene& scene);
    void addShadowAtlasClearPass(RenderGraph& graph, RenderGraphTextureHandle atlas, uint16_t size);
    void addClearPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                      bool materialDebug);
    void addPrepassPass(RenderGraph& graph, RenderGraphTextureHandle normal, RenderGraphTextureHandle depth,
                        const RenderScene& scene);
    void addObjectsMotionVectorsPass(RenderGraph& graph, RenderGraphTextureHandle motionVector,
                                     RenderGraphTextureHandle depth, const RenderScene& scene);
    void addCameraMotionVectorsPass(RenderGraph& graph, RenderGraphTextureHandle motionVector,
                                    RenderGraphTextureHandle depth, RenderGraphTextureHandle depthPyramid);
    void addDepthPyramidPasses(RenderGraph& graph, RenderGraphTextureHandle depth,
                               RenderGraphTextureHandle depthPyramid);
    void addSkyPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle environment);
    void addForwardPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                        RenderGraphTextureHandle lightData, RenderGraphTextureHandle lightList,
                        RenderGraphTextureHandle directionalShadow, RenderGraphTextureHandle punctualShadow,
                        RenderGraphTextureHandle shadowContext, const RenderScene& scene, bool transparent);
    void addWireframePass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                          const RenderScene& scene);
    void addDebugMaterialPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                              const RenderScene& scene, DebugDisplayMode debugMode);
    void addPresentationPass(RenderGraph& graph, RenderGraphTextureHandle color, RenderGraphTextureHandle depth,
                             RenderGraphTextureHandle lightData, RenderGraphTextureHandle lightList,
                             RenderGraphTextureHandle normal, RenderGraphTextureHandle motionVector,
                             RenderGraphTextureHandle depthPyramid, RenderGraphTextureHandle directionalShadow,
                             RenderGraphTextureHandle punctualShadow, RenderGraphBufferHandle fullscreen,
                             DebugDisplayMode debugMode);
    bool drawShadowBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list, const DrawBatch& batch,
                         const RenderScene& scene);
    bool drawForwardBatch(gfx::Encoder& encoder, bgfx::ViewId view, const DrawList& list, const DrawBatch& batch,
                          const RenderScene& scene);
    void renderShadowPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                          const RenderScene& scene, uint32_t requestIndex);
    void renderPrepass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                       const RenderScene& scene);
    void renderObjectsMotionVectors(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                    const RenderScene& scene);
    void renderCameraMotionVectors(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                   gfx::TextureHandle depth, bool cameraOnly);
    void renderDepthPyramidMip(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                               gfx::TextureHandle source, uint16_t sourceWidth, uint16_t sourceHeight, uint16_t width,
                               uint16_t height, uint8_t sourceMip, bool copyDepth);
    void renderSkyPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                       gfx::TextureHandle environment);
    void renderForwardPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                           const RenderScene& scene, bool transparent);
    void renderWireframePass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                             const RenderScene& scene);
    void renderDebugMaterialPass(gfx::Encoder& encoder, bgfx::ViewId view, gfx::FrameBufferHandle frameBuffer,
                                 const RenderScene& scene, DebugDisplayMode debugMode);
    void renderPresentation(gfx::Encoder& encoder, bgfx::ViewId view, gfx::TextureHandle color,
                            gfx::TextureHandle depth, gfx::TextureHandle lightData, gfx::TextureHandle lightList,
                            gfx::TextureHandle normal, gfx::TextureHandle motionVector, gfx::TextureHandle depthPyramid,
                            gfx::TextureHandle directionalShadow, gfx::TextureHandle punctualShadow,
                            gfx::BufferHandle fullscreen, DebugDisplayMode debugMode);

    gfx::ResourceManager* m_resources = nullptr;
    MaterialSystem m_materialSystem;
    RenderGraph m_renderGraph{"Forward Renderer"};
    shader::ShaderLibrary m_shaderLibrary;
    MaterialSamplers m_materialSamplers;
    gfx::generated::GlobalUniformBinder m_globalBinder;
    gfx::generated::PassUniformBinder m_passBinder;
    gfx::generated::MaterialUniformBinder m_materialBinder;
    gfx::generated::ObjectUniformBinder m_objectBinder;
    gfx::generated::PrepassUniformBinder m_prepassBinder;
    gfx::generated::EnvironmentUniformBinder m_environmentBinder;
    gfx::generated::PostProcessUniformBinder m_postProcessBinder;
    gfx::generated::DebugUniformBinder m_debugBinder;

    gfx::BufferHandle m_fullscreenVertexBuffer;
    gfx::UniformHandle m_hdrSampler;
    gfx::TextureHandle m_environmentFallback;
    gfx::UniformHandle m_environmentSampler;
    gfx::TextureHandle m_lightDataTexture;
    gfx::UniformHandle m_lightDataSampler;
    gfx::TextureHandle m_lightListTexture;
    gfx::UniformHandle m_lightListSampler;
    gfx::UniformHandle m_shadowSampler;
    gfx::UniformHandle m_shadowPunctualSampler;
    gfx::UniformHandle m_shadowContextSampler;
    gfx::UniformHandle m_depthSourceSampler;
    gfx::UniformHandle m_normalSampler;
    gfx::UniformHandle m_motionVectorSampler;
    gfx::UniformHandle m_depthPyramidSampler;
    gfx::UniformHandle m_depthDebugSampler;
    gfx::UniformHandle m_shadowAtlasSampler;

    gfx::generated::GlobalUniforms m_globalUniforms;
    gfx::generated::PassUniforms m_passUniforms;
    gfx::generated::ObjectUniforms m_objectUniforms;
    gfx::generated::PrepassUniforms m_prepassUniforms;
    gfx::generated::EnvironmentUniforms m_environmentUniforms;
    gfx::generated::PostProcessUniforms m_postProcessUniforms;
    gfx::generated::DebugUniforms m_debugUniforms;
    DrawList m_opaqueDrawList;
    DrawList m_transparentDrawList;
    DrawList m_shadowDrawList;
    DrawList m_prepassDrawList;
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
    SkyManager m_skyManager;
    PipelineSettings m_pipelineSettings;
    FrameSettings m_frameSettings;
    PostProcessSettings m_postProcessSettings;
    DebugDisplayMode m_debugDisplayMode = DebugDisplayMode::None;
    mutable bool m_formatWarningLogged = false;
    bool m_shadowsEnabled = true;
    bool m_wireframe = false;
    bool m_jitterEnabled = false;
    bool m_depthPrepassActive = false;
    float m_renderScale = 1.0f;
    float m_lastRenderTime = 0.0f;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    uint16_t m_depthPyramidMipCount = 1;
    uint32_t m_frameIndex = 0;
    bool m_hasLastRenderTime = false;
    bool m_valid = false;
};

} // namespace hbg::render
