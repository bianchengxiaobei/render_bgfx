#pragma once

#include "gfx/generated/uniforms.h"
#include "gfx/resource_manager.h"
#include "render/debug/debug_display.h"
#include "render/graph/render_graph.h"
#include "render/pipeline/frame_settings.h"
#include "render/pipeline/pipeline_settings.h"
#include "render/postprocess/color_grading_lut.h"
#include "render/postprocess/post_process_profile.h"
#include "render/postprocess/post_process_settings.h"
#include "shader/shader_library.h"

#include <cstdint>

namespace hbg::render {

struct PostProcessInputs {
    RenderGraphTextureHandle color;
    RenderGraphBufferHandle fullscreen;
    RenderGraphTextureHandle depth;
    RenderGraphTextureHandle depthPyramid;
    RenderGraphTextureHandle motionVectors;
    RenderGraphTextureHandle normal;
    RenderGraphTextureHandle stencil;
    uint16_t renderWidth = 0;
    uint16_t renderHeight = 0;
    uint16_t targetWidth = 0;
    uint16_t targetHeight = 0;
    float renderScale = 1.0f;
    float deltaSeconds = 0.0f;
    uint32_t frameIndex = 0;
    bool historyValid = true;
    bool forceColorGradingLut = false;
    bool forceBloom = false;
    bool forceDof = false;
    bool dofCocOnly = false;
};

struct PostProcessOutput {
    RenderGraphTextureHandle color;
    RenderGraphTextureHandle bloom;
    RenderGraphTextureHandle taaHistory;
    RenderGraphTextureHandle dofCoc;
    bool usedUber = false;
    bool effectsActive = false;
    bool bloomActive = false;
    bool taaActive = false;
    bool dofActive = false;
    bool motionBlurActive = false;
    bool fxaaActive = false;
};

struct PostProcessHistoryState {
    bool beginFrame(uint16_t width, uint16_t height, float renderScale, uint64_t effectsKey, bool externalValid);
    void reset();
    bool valid() const { return m_valid; }
    uint32_t frameCount() const { return m_frameCount; }

  private:
    bool m_valid = false;
    bool m_hasFrame = false;
    uint16_t m_width = 0;
    uint16_t m_height = 0;
    float m_renderScale = 1.0f;
    uint64_t m_effectsKey = 0;
    uint32_t m_frameCount = 0;
    uint32_t m_resetCount = 0;
};

class PostProcessChain {
  public:
    bool init(gfx::ResourceManager& resources, shader::ShaderLibrary& shaderLibrary, uint16_t targetWidth,
              uint16_t targetHeight);
    void shutdown(gfx::ResourceManager& resources);
    void resize(uint16_t targetWidth, uint16_t targetHeight);
    void setProfile(const PostProcessProfile& profile, const PipelineSettings& pipeline,
                    const FrameSettings& frameSettings);
    void setSettings(const PostProcessSettings& settings, const PipelineSettings& pipeline,
                     const FrameSettings& frameSettings);
    void resetHistory();
    void uploadUniforms(const gfx::generated::PostProcessUniforms& uniforms) const {
        m_uniformBinder.upload(uniforms);
    }

    PostProcessOutput record(RenderGraph& graph, const PostProcessInputs& inputs);
    bool addFinalPass(RenderGraph& graph, RenderGraphTextureHandle ldrColor, RenderGraphBufferHandle fullscreen,
                      uint16_t targetWidth, uint16_t targetHeight);
    void endFrame();

    const PostProcessSettings& settings() const { return m_settings; }
    const PostProcessProfile& profile() const { return m_profile; }
    bool computeSupported() const { return m_computeSupported; }
    bool historyValid() const { return m_history.valid(); }
    uint32_t historyFrameCount() const { return m_history.frameCount(); }
    uint32_t postProcessPassCount() const { return m_postPasses; }
    bool effectsActive() const { return m_effectsActive; }
    bool exposureActive() const { return m_exposureActive; }
    bool exposureApplied() const { return m_exposureApplied; }
    bool exposureHistoryValid() const { return m_exposureHistoryValid; }
    ExposureMode effectiveExposureMode() const { return m_effectiveExposureMode; }
    bool uberActive() const { return m_uberActive; }
    bool bloomActive() const { return m_bloomActive; }
    uint32_t bloomMipCount() const { return m_bloomMipCount; }
    BloomMipSize bloomMip0Size() const { return m_bloomMip0; }
    bool taaActive() const { return m_taaActive; }
    bool taaHistoryValid() const { return m_taaHistoryValid; }
    uint32_t taaHistoryFrameCount() const { return m_taaHistoryFrame; }
    gfx::TextureHandle taaHistoryTexture() const { return m_taaHistoryTextures[m_taaHistoryIndex]; }
    bool dofActive() const { return m_dofActive; }
    uint16_t dofCocWidth() const { return m_dofCocWidth; }
    uint16_t dofCocHeight() const { return m_dofCocHeight; }
    uint32_t dofTapCount() const { return m_dofTaps; }
    bool motionBlurActive() const { return m_motionBlurActive; }
    uint32_t motionBlurSampleCount() const { return m_motionBlurSamples; }
    bool fxaaActive() const { return m_fxaaActive; }
    gfx::UniformHandle ldrSampler() const { return m_ldrSampler; }
    gfx::TextureHandle histogramTexture() const { return m_histogramTexture; }
    gfx::TextureHandle exposureTexture() const { return m_exposureTextures[m_exposureIndex]; }
    gfx::TextureHandle colorGradingLutTexture() const { return m_colorGradingLut; }
    gfx::UniformHandle exposureSampler() const { return m_exposureSampler; }
    gfx::UniformHandle histogramSampler() const { return m_histogramSampler; }
    gfx::UniformHandle colorGradingLutSampler() const { return m_colorGradingLutSampler; }
    gfx::UniformHandle bloomSampler() const { return m_bloomSampler; }
    gfx::UniformHandle taaHistorySampler() const { return m_taaHistorySampler; }
    gfx::UniformHandle dofCocSampler() const { return m_dofCocSampler; }
    uint32_t colorGradingLutUploadCount() const { return m_colorGradingLutUploadCount; }
    uint32_t colorGradingLutBytes() const { return colorGradingLutUploadBytes(); }
    const PostProcessHistoryState& historyState() const { return m_history; }

  private:
    bool ensureExposureTextures();
    bool ensureHistogramTexture();
    void releaseExposureTextures();
    void releaseHistogramTexture();
    void applySupportGates();
    ExposureMode resolveExposureMode() const;
    bool addAutomaticExposurePasses(RenderGraph& graph, const PostProcessInputs& inputs);
    bool addHistogramExposurePasses(RenderGraph& graph, const PostProcessInputs& inputs);
    RenderGraphTextureHandle addApplyExposurePass(RenderGraph& graph, const PostProcessInputs& inputs);
    RenderGraphTextureHandle addUberPostPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                             RenderGraphTextureHandle bloom, const PostProcessInputs& inputs);
    RenderGraphTextureHandle addBloomPasses(RenderGraph& graph, RenderGraphTextureHandle color,
                                            const PostProcessInputs& inputs);
    RenderGraphTextureHandle addTaaPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                        const PostProcessInputs& inputs);
    RenderGraphTextureHandle addTaaSharpenPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                               const PostProcessInputs& inputs);
    RenderGraphTextureHandle addDofCocPass(RenderGraph& graph, const PostProcessInputs& inputs);
    RenderGraphTextureHandle addDofGatherPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                              RenderGraphTextureHandle coc, uint16_t cocWidth,
                                              uint16_t cocHeight, const PostProcessInputs& inputs);
    RenderGraphTextureHandle addDofCombinePass(RenderGraph& graph, RenderGraphTextureHandle color,
                                               RenderGraphTextureHandle gather, const PostProcessInputs& inputs);
    RenderGraphTextureHandle addMotionBlurPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                               const PostProcessInputs& inputs);
    bool ensureTaaHistory(uint16_t renderWidth, uint16_t renderHeight);
    void releaseTaaHistory();
    bool ensureBloomFallback();
    void releaseBloomFallback();
    bool ensureColorGradingLut();
    void releaseColorGradingLut();
    void updateExposureUniforms(const PostProcessInputs& inputs);
    void updateTaaUniforms(const PostProcessInputs& inputs, bool historyValid);
    void updateDofUniforms(const PostProcessInputs& inputs, uint16_t cocWidth, uint16_t cocHeight);
    void updateMotionBlurUniforms(const PostProcessInputs& inputs);
    void updateUberUniforms();
    void updateScreenUniforms(const PostProcessInputs& inputs);
    uint64_t effectsKey() const;
    uint64_t exposureKey() const;

    gfx::ResourceManager* m_resources = nullptr;
    shader::ShaderLibrary* m_shaderLibrary = nullptr;
    PostProcessProfile m_profile{};
    PostProcessSettings m_settings{};
    PipelineSettings m_pipeline{};
    FrameSettings m_frame{};
    PostProcessHistoryState m_history;
    gfx::generated::PostProcessUniformBinder m_uniformBinder;
    gfx::generated::PostProcessUniforms m_uniforms{};
    gfx::TextureHandle m_exposureTextures[2]{};
    gfx::TextureHandle m_histogramTexture;
    gfx::TextureHandle m_taaHistoryTextures[2]{};
    gfx::TextureHandle m_colorGradingLut;
    gfx::TextureHandle m_bloomFallback;
    gfx::UniformHandle m_ldrSampler;
    gfx::UniformHandle m_exposureSampler;
    gfx::UniformHandle m_histogramSampler;
    gfx::UniformHandle m_sourceSampler;
    gfx::UniformHandle m_secondarySampler;
    gfx::UniformHandle m_luminanceSampler;
    gfx::UniformHandle m_previousExposureSampler;
    gfx::UniformHandle m_colorGradingLutSampler;
    gfx::UniformHandle m_bloomSampler;
    gfx::UniformHandle m_taaHistorySampler;
    gfx::UniformHandle m_taaMotionSampler;
    gfx::UniformHandle m_taaDepthSampler;
    gfx::UniformHandle m_dofCocSampler;
    gfx::UniformHandle m_dofDepthSampler;
    gfx::UniformHandle m_dofGatherSampler;
    gfx::UniformHandle m_motionBlurSampler;
    gfx::UniformHandle m_motionBlurDepthSampler;
    RenderGraphTextureHandle m_bloomOutput;
    RenderGraphTextureHandle m_taaOutputHistory;
    RenderGraphTextureHandle m_dofCoc;
    BloomMipSize m_bloomMip0{};
    uint16_t m_targetWidth = 0;
    uint16_t m_targetHeight = 0;
    uint16_t m_taaHistoryWidth = 0;
    uint16_t m_taaHistoryHeight = 0;
    uint16_t m_dofCocWidth = 0;
    uint16_t m_dofCocHeight = 0;
    uint32_t m_exposureIndex = 0;
    uint32_t m_postPasses = 0;
    uint32_t m_bloomMipCount = 1;
    uint32_t m_taaHistoryIndex = 0;
    uint32_t m_taaHistoryFrame = 0;
    uint32_t m_dofTaps = 0;
    uint32_t m_motionBlurSamples = 0;
    uint32_t m_colorGradingLutUploadCount = 0;
    ExposureMode m_effectiveExposureMode = ExposureMode::Fixed;
    TonemappingMode m_effectiveTonemapping = TonemappingMode::ACES;
    ColorGradingSettings m_lutGrading{};
    TonemappingMode m_lutTonemapping = TonemappingMode::None;
    bool m_lutValid = false;
    bool m_initialized = false;
    bool m_computeSupported = false;
    bool m_effectsActive = false;
    bool m_exposureActive = false;
    bool m_exposureApplied = false;
    bool m_exposureHistoryValid = false;
    bool m_uberActive = false;
    bool m_bloomActive = false;
    bool m_taaActive = false;
    bool m_taaHistoryValid = false;
    bool m_dofActive = false;
    bool m_motionBlurActive = false;
    bool m_fxaaActive = false;
    bool m_bloomLogged = false;
    bool m_taaLogged = false;
    bool m_dofLogged = false;
    bool m_motionBlurLogged = false;
    bool m_advancedUnavailableLogged = false;
    bool m_exposureFallbackLogged = false;
};

} // namespace hbg::render
