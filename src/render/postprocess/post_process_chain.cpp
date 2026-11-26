#include "render/postprocess/post_process_chain.h"

#include "core/log.h"
#include "gfx/state.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace hbg::render {

bool PostProcessHistoryState::beginFrame(uint16_t width, uint16_t height, float renderScale, uint64_t effectsKey,
                                         bool externalValid) {
    const bool shapeChanged = width != m_width || height != m_height ||
                              std::fabs(renderScale - m_renderScale) > 1.0e-6f || effectsKey != m_effectsKey;
    if (!m_hasFrame || shapeChanged || !externalValid) {
        m_width = width;
        m_height = height;
        m_renderScale = renderScale;
        m_effectsKey = effectsKey;
        m_hasFrame = true;
        m_valid = false;
        m_frameCount = 0;
        return false;
    }

    if (!m_valid) {
        m_valid = true;
        m_frameCount = 1;
        return false;
    }

    ++m_frameCount;
    return true;
}

void PostProcessHistoryState::reset() {
    m_valid = false;
    m_hasFrame = false;
    m_width = 0;
    m_height = 0;
    m_renderScale = 1.0f;
    m_effectsKey = 0;
    m_frameCount = 0;
    ++m_resetCount;
}

bool PostProcessChain::init(gfx::ResourceManager& resources, shader::ShaderLibrary& shaderLibrary,
                            uint16_t targetWidth, uint16_t targetHeight) {
    if (m_initialized) {
        return true;
    }
    m_resources = &resources;
    m_shaderLibrary = &shaderLibrary;
    m_targetWidth = targetWidth;
    m_targetHeight = targetHeight;
    m_exposureIndex = 0;
    m_postPasses = 0;

    const bgfx::Caps* caps = bgfx::getCaps();
    m_computeSupported = caps != nullptr && (caps->supported & BGFX_CAPS_COMPUTE) != 0 &&
                         (caps->supported & BGFX_CAPS_IMAGE_RW) != 0;
    if (!m_computeSupported) {
        HBG_LOGW("post-process: compute/imageRW unavailable; exposure histogram falls back to raster");
    }

    if (!m_uniformBinder.init(resources)) {
        HBG_LOGE("post-process: failed to initialize uniform bindings");
        return false;
    }
    m_ldrSampler = resources.createUniform({"s_ldrColor", gfx::UniformType::Sampler, 1});
    m_exposureSampler = resources.createUniform({"s_exposure", gfx::UniformType::Sampler, 1});
    m_histogramSampler = resources.createUniform({"s_histogram", gfx::UniformType::Sampler, 1});
    m_sourceSampler = resources.createUniform({"s_source", gfx::UniformType::Sampler, 1});
    m_secondarySampler = resources.createUniform({"s_source2", gfx::UniformType::Sampler, 1});
    m_luminanceSampler = resources.createUniform({"s_luminance", gfx::UniformType::Sampler, 1});
    m_previousExposureSampler = resources.createUniform({"s_previousExposure", gfx::UniformType::Sampler, 1});
    m_colorGradingLutSampler = resources.createUniform({"s_colorGradingLut", gfx::UniformType::Sampler, 1});
    m_bloomSampler = resources.createUniform({"s_bloom", gfx::UniformType::Sampler, 1});
    m_taaHistorySampler = resources.createUniform({"s_taaHistory", gfx::UniformType::Sampler, 1});
    m_taaVelocitySampler = resources.createUniform({"s_taaVelocityHistory", gfx::UniformType::Sampler, 1});
    m_taaMotionSampler = resources.createUniform({"s_taaMotionVectors", gfx::UniformType::Sampler, 1});
    m_taaDepthSampler = resources.createUniform({"s_taaDepth", gfx::UniformType::Sampler, 1});
    m_dofCocSampler = resources.createUniform({"s_dofCoc", gfx::UniformType::Sampler, 1});
    m_dofDepthSampler = resources.createUniform({"s_dofDepth", gfx::UniformType::Sampler, 1});
    m_dofGatherSampler = resources.createUniform({"s_dofGather", gfx::UniformType::Sampler, 1});
    m_motionBlurSampler = resources.createUniform({"s_motionBlurVectors", gfx::UniformType::Sampler, 1});
    m_motionBlurDepthSampler = resources.createUniform({"s_motionBlurDepth", gfx::UniformType::Sampler, 1});
    if (!m_ldrSampler.valid() || !m_exposureSampler.valid() || !m_histogramSampler.valid() ||
        !m_sourceSampler.valid() || !m_secondarySampler.valid() || !m_luminanceSampler.valid() ||
        !m_previousExposureSampler.valid() || !m_colorGradingLutSampler.valid() || !m_bloomSampler.valid() ||
        !m_taaHistorySampler.valid() || !m_taaVelocitySampler.valid() || !m_taaMotionSampler.valid() ||
        !m_taaDepthSampler.valid() ||
        !m_dofCocSampler.valid() || !m_dofDepthSampler.valid() || !m_dofGatherSampler.valid() ||
        !m_motionBlurSampler.valid() || !m_motionBlurDepthSampler.valid() || !ensureExposureTextures() ||
        !ensureHistogramTexture() || !ensureBloomFallback()) {
        HBG_LOGE("post-process: failed to create chain resources");
        shutdown(resources);
        return false;
    }

    m_initialized = true;
    HBG_LOGI("post-process chain ready: exposure=1x1 RGBA16F ping-pong, histogram={}x1 R32U, compute={}, "
             "target={}x{}",
             kExposureHistogramBins, m_computeSupported ? "true" : "false", m_targetWidth, m_targetHeight);
    return true;
}

void PostProcessChain::shutdown(gfx::ResourceManager& resources) {
    releaseExposureTextures();
    releaseHistogramTexture();
    releaseColorGradingLut();
    releaseBloomFallback();
    releaseTaaHistory();
    if (m_ldrSampler.valid()) {
        resources.destroyUniform(m_ldrSampler);
        m_ldrSampler = {};
    }
    if (m_exposureSampler.valid()) {
        resources.destroyUniform(m_exposureSampler);
        m_exposureSampler = {};
    }
    if (m_histogramSampler.valid()) {
        resources.destroyUniform(m_histogramSampler);
        m_histogramSampler = {};
    }
    if (m_sourceSampler.valid()) {
        resources.destroyUniform(m_sourceSampler);
        m_sourceSampler = {};
    }
    if (m_secondarySampler.valid()) {
        resources.destroyUniform(m_secondarySampler);
        m_secondarySampler = {};
    }
    if (m_luminanceSampler.valid()) {
        resources.destroyUniform(m_luminanceSampler);
        m_luminanceSampler = {};
    }
    if (m_previousExposureSampler.valid()) {
        resources.destroyUniform(m_previousExposureSampler);
        m_previousExposureSampler = {};
    }
    if (m_colorGradingLutSampler.valid()) {
        resources.destroyUniform(m_colorGradingLutSampler);
        m_colorGradingLutSampler = {};
    }
    if (m_bloomSampler.valid()) {
        resources.destroyUniform(m_bloomSampler);
        m_bloomSampler = {};
    }
    if (m_taaHistorySampler.valid()) {
        resources.destroyUniform(m_taaHistorySampler);
        m_taaHistorySampler = {};
    }
    if (m_taaVelocitySampler.valid()) {
        resources.destroyUniform(m_taaVelocitySampler);
        m_taaVelocitySampler = {};
    }
    if (m_taaMotionSampler.valid()) {
        resources.destroyUniform(m_taaMotionSampler);
        m_taaMotionSampler = {};
    }
    if (m_taaDepthSampler.valid()) {
        resources.destroyUniform(m_taaDepthSampler);
        m_taaDepthSampler = {};
    }
    if (m_dofCocSampler.valid()) {
        resources.destroyUniform(m_dofCocSampler);
        m_dofCocSampler = {};
    }
    if (m_dofDepthSampler.valid()) {
        resources.destroyUniform(m_dofDepthSampler);
        m_dofDepthSampler = {};
    }
    if (m_dofGatherSampler.valid()) {
        resources.destroyUniform(m_dofGatherSampler);
        m_dofGatherSampler = {};
    }
    if (m_motionBlurSampler.valid()) {
        resources.destroyUniform(m_motionBlurSampler);
        m_motionBlurSampler = {};
    }
    if (m_motionBlurDepthSampler.valid()) {
        resources.destroyUniform(m_motionBlurDepthSampler);
        m_motionBlurDepthSampler = {};
    }
    m_bloomOutput = {};
    m_taaOutputHistory = {};
    m_dofCoc = {};
    m_bloomActive = false;
    m_taaActive = false;
    m_taaHistoryValid = false;
    m_dofActive = false;
    m_motionBlurActive = false;
    m_taaHistoryFrame = 0;
    m_dofCocWidth = 0;
    m_dofCocHeight = 0;
    m_dofTaps = 0;
    m_motionBlurSamples = 0;
    m_bloomLogged = false;
    m_taaLogged = false;
    m_taaUpscaleSharpeningLogged = false;
    m_taaUpscaleAfterPostFallbackLogged = false;
    m_dofLogged = false;
    m_motionBlurLogged = false;
    m_uniformBinder.shutdown(resources);
    m_initialized = false;
    m_resources = nullptr;
    m_shaderLibrary = nullptr;
    m_history.reset();
}

void PostProcessChain::resize(uint16_t targetWidth, uint16_t targetHeight) {
    if (targetWidth == m_targetWidth && targetHeight == m_targetHeight) {
        return;
    }
    m_targetWidth = targetWidth;
    m_targetHeight = targetHeight;
    resetHistory();
}

void PostProcessChain::setProfile(const PostProcessProfile& profile, const PipelineSettings& pipeline,
                                  const FrameSettings& frameSettings) {
    m_profile = profile;
    setSettings(profile.settings, pipeline, frameSettings);
}

void PostProcessChain::setSettings(const PostProcessSettings& settings, const PipelineSettings& pipeline,
                                   const FrameSettings& frameSettings) {
    const uint64_t previousEffectsKey = effectsKey();
    const uint64_t previousExposureKey = exposureKey();
    const bool wasActive = m_effectsActive;
    const bool requested = settings.hasAdvancedEffects();
    const BloomSettings previousBloom = m_settings.bloom;
    const TaaSettings previousTaa = m_settings.taa;
    const DepthOfFieldSettings previousDof = m_settings.depthOfField;
    const MotionBlurSettings previousMotionBlur = m_settings.motionBlur;

    m_settings = settings;
    sanitizePostProcessSettings(m_settings);
    m_pipeline = pipeline;
    m_frame = frameSettings;
    applySupportGates();
    if (!(previousBloom == m_settings.bloom)) {
        m_bloomLogged = false;
    }
    if (!(previousTaa == m_settings.taa)) {
        m_taaLogged = false;
        m_taaUpscaleSharpeningLogged = false;
        m_taaUpscaleAfterPostFallbackLogged = false;
    }
    if (!(previousDof == m_settings.depthOfField)) {
        m_dofLogged = false;
    }
    if (!(previousMotionBlur == m_settings.motionBlur)) {
        m_motionBlurLogged = false;
    }

    const uint64_t nextEffectsKey = effectsKey();
    const uint64_t nextExposureKey = exposureKey();
    if (nextEffectsKey != previousEffectsKey || m_effectsActive != wasActive ||
        nextExposureKey != previousExposureKey) {
        resetHistory();
    }
    if (requested && !m_effectsActive && !m_advancedUnavailableLogged) {
        HBG_LOGW("post-process: advanced effects requested but unavailable; keeping the M2 tonemap path");
        m_advancedUnavailableLogged = true;
    }
    if (m_settings.exposureMode != ExposureMode::Fixed && !m_computeSupported && !m_exposureFallbackLogged) {
        HBG_LOGW("post-process: dynamic exposure requires compute/imageRW; using the legacy tonemap exposure path");
        m_exposureFallbackLogged = true;
    }
    if (m_settings.exposureMode == ExposureMode::Histogram && m_computeSupported &&
        !m_pipeline.supportExposureHistogram && !m_exposureFallbackLogged) {
        HBG_LOGW("post-process: histogram exposure is disabled in pipeline settings; falling back to automatic");
        m_exposureFallbackLogged = true;
    }
}

void PostProcessChain::applySupportGates() {
    if (!m_pipeline.supportTAA || !m_frame.isEnabled(FrameSettingsField::Antialiasing) ||
        !m_pipeline.supportMotionVectors || !m_frame.isEnabled(FrameSettingsField::MotionVectors)) {
        m_settings.taa.enabled = false;
    }
    if (!m_pipeline.supportFXAA || !m_frame.isEnabled(FrameSettingsField::Antialiasing)) {
        m_settings.fxaa.enabled = false;
    }
    if (!m_pipeline.supportBloom || !m_frame.isEnabled(FrameSettingsField::Bloom)) {
        m_settings.bloom.enabled = false;
    }
    if (!m_pipeline.supportColorGrading || !m_frame.isEnabled(FrameSettingsField::ColorGrading)) {
        m_settings.colorGrading.enabled = false;
    }
    if (!m_frame.isEnabled(FrameSettingsField::Vignette)) {
        m_settings.vignette.intensity = 0.0f;
    }
    if (!m_pipeline.supportDepthOfField || !m_frame.isEnabled(FrameSettingsField::DepthOfField)) {
        m_settings.depthOfField.enabled = false;
    }
    if (!m_pipeline.supportMotionBlur || !m_frame.isEnabled(FrameSettingsField::MotionBlur)) {
        m_settings.motionBlur.enabled = false;
    }
    if (!m_frame.isEnabled(FrameSettingsField::Postprocess)) {
        m_settings.taa.enabled = false;
        m_settings.fxaa.enabled = false;
        m_settings.bloom.enabled = false;
        m_settings.colorGrading.enabled = false;
        m_settings.depthOfField.enabled = false;
        m_settings.motionBlur.enabled = false;
    }
    if (m_settings.taa.enabled) {
        m_settings.fxaa.enabled = false;
    }
    const bool postProcessEnabled = m_frame.isEnabled(FrameSettingsField::Postprocess);
    if (!postProcessEnabled) {
        m_settings.vignette.intensity = 0.0f;
    }
    m_effectiveTonemapping = postProcessEnabled && m_frame.isEnabled(FrameSettingsField::Tonemapping)
                                 ? m_settings.tonemapping
                                 : TonemappingMode::None;
    m_effectsActive = postProcessEnabled && m_settings.hasAdvancedEffects();
    m_effectiveExposureMode = resolveExposureMode();
}

ExposureMode PostProcessChain::resolveExposureMode() const {
    if (m_settings.exposureMode == ExposureMode::Fixed) {
        return ExposureMode::Fixed;
    }
    if (m_settings.exposureMode == ExposureMode::Histogram && m_computeSupported &&
        m_pipeline.supportExposureHistogram) {
        return ExposureMode::Histogram;
    }
    return ExposureMode::Automatic;
}

uint64_t PostProcessChain::effectsKey() const {
    uint64_t key = 0;
    key |= m_settings.taa.enabled ? (1ull << 0) : 0;
    key |= (m_settings.fxaa.enabled || m_settings.taa.upscale) ? (1ull << 1) : 0;
    key |= m_settings.bloom.active() ? (1ull << 2) : 0;
    key |= m_settings.colorGrading.enabled ? (1ull << 3) : 0;
    key |= m_settings.depthOfField.active() ? (1ull << 4) : 0;
    key |= m_settings.motionBlur.active() ? (1ull << 5) : 0;
    key |= m_settings.vignette.active() ? (1ull << 6) : 0;
    key |= static_cast<uint64_t>(m_effectiveTonemapping) << 7;
    key |= static_cast<uint64_t>(m_settings.taa.quality & 0xffu) << 8;
    key |= static_cast<uint64_t>(std::lround(m_settings.taa.jitterScale * 255.0f) & 0xffff) << 16;
    key |= static_cast<uint64_t>(std::lround(m_settings.taa.motionVectorRejection * 255.0f) & 0xff) << 32;
    key |= static_cast<uint64_t>(std::lround(m_settings.taa.velocityRejection * 255.0f) & 0xff) << 40;
    key |= static_cast<uint64_t>(std::lround(m_settings.taa.sharpening * 255.0f) & 0xff) << 48;
    key |= static_cast<uint64_t>(m_settings.taa.upscaleInjection) << 56;
    return key;
}

uint64_t PostProcessChain::exposureKey() const {
    uint64_t key = static_cast<uint64_t>(m_settings.exposureMode);
    key |= static_cast<uint64_t>(m_effectiveExposureMode) << 8;
    key |= m_frame.isEnabled(FrameSettingsField::Postprocess) ? (1ull << 16) : 0;
    key |= m_frame.isEnabled(FrameSettingsField::ExposureControl) ? (1ull << 17) : 0;
    key |= m_computeSupported ? (1ull << 18) : 0;
    return key;
}

PostProcessOutput PostProcessChain::record(RenderGraph& graph, const PostProcessInputs& inputs) {
    m_postPasses = 0;
    m_exposureApplied = false;
    m_uberActive = false;
    m_bloomActive = false;
    m_taaActive = false;
    m_dofActive = false;
    m_motionBlurActive = false;
    m_bloomOutput = {};
    m_taaOutputHistory = {};
    m_dofCoc = {};
    m_dofCocWidth = 0;
    m_dofCocHeight = 0;
    m_dofTaps = 0;
    m_motionBlurSamples = 0;
    m_fxaaActive = false;
    updateScreenUniforms(inputs);
    m_effectsActive = m_frame.isEnabled(FrameSettingsField::Postprocess) && m_settings.hasAdvancedEffects();
    const bool postProcessEnabled = m_frame.isEnabled(FrameSettingsField::Postprocess);
    const bool exposureRequested = postProcessEnabled && m_frame.isEnabled(FrameSettingsField::ExposureControl) &&
                                   m_settings.exposureMode != ExposureMode::Fixed;
    m_exposureActive = exposureRequested && m_computeSupported && inputs.fullscreen.valid();

    if (m_effectsActive) {
        const bool historyValid = m_history.beginFrame(inputs.renderWidth, inputs.renderHeight, inputs.renderScale,
                                                        effectsKey(), inputs.historyValid);
        if (!historyValid && inputs.frameIndex > 0 && !m_advancedUnavailableLogged) {
            HBG_LOGD("post-process: history reset at {}x{} scale={}", inputs.renderWidth, inputs.renderHeight,
                     inputs.renderScale);
        }
    } else {
        m_history.reset();
    }

    RenderGraphTextureHandle color = inputs.color;
    if (m_exposureActive) {
        updateExposureUniforms(inputs);
        if (m_effectiveExposureMode == ExposureMode::Histogram) {
            m_exposureActive = addHistogramExposurePasses(graph, inputs);
        } else {
            m_exposureActive = addAutomaticExposurePasses(graph, inputs);
        }
        if (m_exposureActive) {
            const RenderGraphTextureHandle exposed = addApplyExposurePass(graph, inputs);
            if (exposed.valid()) {
                color = exposed;
                m_exposureApplied = true;
                m_exposureHistoryValid = true;
            } else {
                m_exposureActive = false;
            }
        }
    }

    const bool exposureSatisfied = m_settings.exposureMode == ExposureMode::Fixed || m_exposureApplied;
    const bool taaRequested = m_settings.taa.enabled && postProcessEnabled && exposureSatisfied &&
                              inputs.fullscreen.valid() && inputs.motionVectors.valid() && inputs.depth.valid();
    const bool taaUpscaleRequested =
        taaRequested && m_settings.taa.upscale &&
        (inputs.targetWidth > inputs.renderWidth || inputs.targetHeight > inputs.renderHeight);
    if (taaRequested && !taaUpscaleRequested) {
        updateTaaUniforms(inputs, inputs.historyValid && m_taaHistoryValid, inputs.renderWidth,
                          inputs.renderHeight, false);
        const RenderGraphTextureHandle resolved = addTaaPass(graph, color, inputs, false);
        if (resolved.valid()) {
            color = resolved;
            m_taaActive = true;
            if (m_settings.taa.sharpening > 0.0f) {
                const RenderGraphTextureHandle sharpened = addTaaSharpenPass(graph, color, inputs);
                if (sharpened.valid()) {
                    color = sharpened;
                }
            }
        }
    }

    // HDRP uses DynamicResolutionHandler.UpsamplerScheduleType for TAAU:
    // BeforePost runs after exposure and before DOF; AfterDepthOfField runs after DOF;
    // AfterPost runs after the Uber post pass. DOF/MB/Bloom consume the resolution group
    // active at their point in the chain, while depth/motion stay at render resolution.
    TaaUpsampleInjection taaUpscaleInjection = m_settings.taa.upscaleInjection;
    if (taaUpscaleRequested && taaUpscaleInjection == TaaUpsampleInjection::AfterPost &&
        !m_settings.needsUber()) {
        if (!m_taaUpscaleAfterPostFallbackLogged) {
            HBG_LOGW("taa: AfterPost injection requires the Uber path; using BeforePost");
            m_taaUpscaleAfterPostFallbackLogged = true;
        }
        taaUpscaleInjection = TaaUpsampleInjection::BeforePost;
    }
    const bool taaUpscaleBeforePost =
        taaUpscaleRequested && taaUpscaleInjection == TaaUpsampleInjection::BeforePost;
    const bool taaUpscaleAfterDof =
        taaUpscaleRequested && taaUpscaleInjection == TaaUpsampleInjection::AfterDepthOfField;
    const bool taaUpscaleAfterPost =
        taaUpscaleRequested && taaUpscaleInjection == TaaUpsampleInjection::AfterPost;

    PostProcessInputs effectInputs = inputs;
    const auto resolveUpscale = [&]() -> bool {
        const RenderGraphTextureHandle resolved = addTaaPass(graph, color, inputs, true);
        if (!resolved.valid()) {
            return false;
        }
        color = resolved;
        m_taaActive = true;
        effectInputs.renderWidth = inputs.targetWidth;
        effectInputs.renderHeight = inputs.targetHeight;
        if (m_settings.taa.sharpening > 0.0f && !m_taaUpscaleSharpeningLogged) {
            HBG_LOGI("taa: temporal upsampling ignores post-TAA sharpening");
            m_taaUpscaleSharpeningLogged = true;
        }
        return true;
    };

    if (taaUpscaleBeforePost) {
        resolveUpscale();
    }

    // BeforePost and AfterDepthOfField run DOF in different resolution groups.
    const PostProcessInputs& dofInputs = taaUpscaleBeforePost ? effectInputs : inputs;
    const bool dofRequested =
        postProcessEnabled && exposureSatisfied && m_pipeline.supportDepthOfField &&
        m_frame.isEnabled(FrameSettingsField::DepthOfField) && (m_settings.depthOfField.active() || inputs.forceDof) &&
        dofInputs.fullscreen.valid() && dofInputs.depth.valid();
    if (dofRequested) {
        const RenderGraphTextureHandle coc = addDofCocPass(graph, dofInputs);
        if (coc.valid()) {
            m_dofCoc = coc;
            m_dofActive = true;
            if (!dofInputs.dofCocOnly) {
                const RenderGraphTextureHandle gathered =
                    addDofGatherPass(graph, color, coc, m_dofCocWidth, m_dofCocHeight, dofInputs);
                if (gathered.valid()) {
                    const RenderGraphTextureHandle combined =
                        addDofCombinePass(graph, color, gathered, dofInputs);
                    if (combined.valid()) {
                        color = combined;
                    } else {
                        m_dofActive = false;
                    }
                } else {
                    m_dofActive = false;
                }
            }
        }
    }

    if (taaUpscaleAfterDof) {
        resolveUpscale();
    }

    const bool motionBlurRequested =
        postProcessEnabled && exposureSatisfied && m_pipeline.supportMotionBlur &&
        m_frame.isEnabled(FrameSettingsField::MotionBlur) && m_settings.motionBlur.active() &&
        effectInputs.historyValid && m_history.valid() && effectInputs.fullscreen.valid() &&
        effectInputs.motionVectors.valid() && effectInputs.depth.valid();
    if (motionBlurRequested) {
        updateMotionBlurUniforms(effectInputs);
        const RenderGraphTextureHandle blurred = addMotionBlurPass(graph, color, effectInputs);
        if (blurred.valid()) {
            color = blurred;
            m_motionBlurActive = true;
            m_motionBlurSamples = m_settings.motionBlur.sampleCount;
        }
    }

    const bool bloomRequested = (m_settings.bloom.active() || effectInputs.forceBloom) && postProcessEnabled &&
                                m_pipeline.supportBloom && m_frame.isEnabled(FrameSettingsField::Bloom) &&
                                effectInputs.fullscreen.valid();
    if (bloomRequested) {
        updateExposureUniforms(inputs);
        updateUberUniforms();
        m_bloomOutput = addBloomPasses(graph, color, effectInputs);
        m_bloomActive = m_bloomOutput.valid();
    }

    const bool uberRequested = m_effectsActive && m_settings.needsUber() && exposureSatisfied &&
                               inputs.fullscreen.valid() && postProcessEnabled && !inputs.forceBloom;
    if (uberRequested || inputs.forceColorGradingLut) {
        updateExposureUniforms(inputs);
        updateUberUniforms();
        const bool lutReady = ensureColorGradingLut();
        if (uberRequested && lutReady) {
            const RenderGraphTextureHandle ldrColor = addUberPostPass(graph, color, m_bloomOutput, effectInputs);
            if (ldrColor.valid()) {
                color = ldrColor;
                m_uberActive = true;
            }
        }
    }

    bool requiresFinalCopy = false;
    if (taaUpscaleAfterPost) {
        resolveUpscale();
        requiresFinalCopy = true;
    }

    m_fxaaActive = postProcessEnabled && m_pipeline.supportFXAA &&
                   m_frame.isEnabled(FrameSettingsField::Antialiasing) && m_settings.fxaa.enabled &&
                   m_uberActive;

    PostProcessOutput output;
    output.color = color;
    output.bloom = m_bloomOutput;
    output.taaHistory = m_taaOutputHistory;
    output.dofCoc = m_dofCoc;
    output.usedUber = m_uberActive;
    output.effectsActive = m_effectsActive;
    output.bloomActive = m_bloomActive;
    output.taaActive = m_taaActive;
    output.dofActive = m_dofActive;
    output.motionBlurActive = m_motionBlurActive;
    output.fxaaActive = m_fxaaActive;
    output.requiresFinalCopy = requiresFinalCopy;
    return output;
}

bool PostProcessChain::addAutomaticExposurePasses(RenderGraph& graph, const PostProcessInputs& inputs) {
    if (m_shaderLibrary == nullptr || !m_exposureTextures[0].valid() || !m_sourceSampler.valid() ||
        !m_luminanceSampler.valid() || !m_previousExposureSampler.valid()) {
        return false;
    }
    const gfx::ProgramHandle prepassProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::PostProcessExposurePrepass, 0, 0});
    const gfx::ProgramHandle reduceProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::PostProcessExposureReduce, 0, 0});
    const gfx::ProgramHandle finalProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::PostProcessExposureReduceFinal, 0, 0});
    if (!prepassProgram.valid() || !reduceProgram.valid() || !finalProgram.valid()) {
        HBG_LOGW("post-process: automatic exposure compute programs unavailable");
        return false;
    }

    gfx::TextureDesc prepassDesc;
    prepassDesc.width = 256;
    prepassDesc.height = 256;
    prepassDesc.format = gfx::TextureFormat::RG16F;
    prepassDesc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    prepassDesc.computeWrite = true;
    prepassDesc.name = "postprocess.exposure.prepass";
    const RenderGraphTextureHandle prepass = graph.createTexture(prepassDesc);

    gfx::TextureDesc reduceDesc = prepassDesc;
    reduceDesc.width = 32;
    reduceDesc.height = 32;
    reduceDesc.name = "postprocess.exposure.reduce";
    const RenderGraphTextureHandle reduced = graph.createTexture(reduceDesc);

    const RenderGraphTextureHandle exposure =
        graph.importTexture(m_exposureTextures[m_exposureIndex], "PostProcess Exposure");
    const RenderGraphTextureHandle previous =
        graph.importTexture(m_exposureTextures[m_exposureIndex ^ 1u], "PostProcess Previous Exposure");

    graph.addComputePass("Exposure Prepass")
        .readTexture(inputs.color)
        .writeTexture(prepass)
        .allowCulling(false)
        .setRenderFunc([this, inputs, prepass, prepassProgram](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_sourceSampler, context.texture(inputs.color),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setImage(1, context.texture(prepass), 0, bgfx::Access::Write, gfx::TextureFormat::RG16F);
            encoder.dispatch(context.viewId(), prepassProgram, 32, 32, 1);
        });
    graph.addComputePass("Exposure Reduce 32")
        .readTexture(prepass)
        .writeTexture(reduced)
        .allowCulling(false)
        .setRenderFunc([this, prepass, reduced, reduceProgram](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_luminanceSampler, context.texture(prepass),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setImage(1, context.texture(reduced), 0, bgfx::Access::Write, gfx::TextureFormat::RG16F);
            encoder.dispatch(context.viewId(), reduceProgram, 4, 4, 1);
        });
    graph.addComputePass("Exposure Reduce Final")
        .readTexture(reduced)
        .readTexture(previous)
        .writeTexture(exposure)
        .allowCulling(false)
        .setRenderFunc([this, reduced, previous, exposure, finalProgram](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_luminanceSampler, context.texture(reduced),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(1, m_previousExposureSampler, context.texture(previous),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setImage(2, context.texture(exposure), 0, bgfx::Access::Write,
                             gfx::TextureFormat::RGBA16F);
            encoder.dispatch(context.viewId(), finalProgram, 1, 1, 1);
        });
    m_postPasses += 3;
    return true;
}

bool PostProcessChain::addHistogramExposurePasses(RenderGraph& graph, const PostProcessInputs& inputs) {
    if (m_shaderLibrary == nullptr || !m_exposureTextures[0].valid() || !m_histogramTexture.valid() ||
        !m_sourceSampler.valid() || !m_previousExposureSampler.valid()) {
        return false;
    }
    const gfx::ProgramHandle clearProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::PostProcessHistogramClear, 0, 0});
    const gfx::ProgramHandle genProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::PostProcessHistogramGen, 0, 0});
    const gfx::ProgramHandle reduceProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::PostProcessHistogramReduce, 0, 0});
    if (!clearProgram.valid() || !genProgram.valid() || !reduceProgram.valid()) {
        HBG_LOGW("post-process: histogram exposure compute programs unavailable");
        return false;
    }

    const RenderGraphTextureHandle histogram = graph.importTexture(m_histogramTexture, "PostProcess Histogram");
    const RenderGraphTextureHandle exposure =
        graph.importTexture(m_exposureTextures[m_exposureIndex], "PostProcess Exposure");
    const RenderGraphTextureHandle previous =
        graph.importTexture(m_exposureTextures[m_exposureIndex ^ 1u], "PostProcess Previous Exposure");

    graph.addComputePass("Exposure Histogram Clear")
        .writeTexture(histogram)
        .allowCulling(false)
        .setRenderFunc([this, histogram, clearProgram](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setImage(1, context.texture(histogram), 0, bgfx::Access::Write, gfx::TextureFormat::R32U);
            encoder.dispatch(context.viewId(), clearProgram, 1, 1, 1);
        });
    const uint32_t dispatchX = (static_cast<uint32_t>(inputs.renderWidth) / 2u + 15u) / 16u;
    const uint32_t dispatchY = (static_cast<uint32_t>(inputs.renderHeight) / 2u + 7u) / 8u;
    graph.addComputePass("Exposure Histogram Generate")
        .readTexture(inputs.color)
        .readTexture(histogram)
        .writeTexture(histogram)
        .allowCulling(false)
        .setRenderFunc([this, inputs, histogram, genProgram, dispatchX, dispatchY](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_sourceSampler, context.texture(inputs.color),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setImage(1, context.texture(histogram), 0, bgfx::Access::ReadWrite, gfx::TextureFormat::R32U);
            encoder.dispatch(context.viewId(), genProgram, dispatchX, dispatchY, 1);
        });
    graph.addComputePass("Exposure Histogram Reduce")
        .readTexture(histogram)
        .readTexture(previous)
        .writeTexture(exposure)
        .allowCulling(false)
        .setRenderFunc([this, histogram, previous, exposure, reduceProgram](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            m_uniformBinder.upload(m_uniforms);
            encoder.setImage(0, context.texture(histogram), 0, bgfx::Access::Read, gfx::TextureFormat::R32U);
            encoder.setTexture(1, m_previousExposureSampler, context.texture(previous),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setImage(2, context.texture(exposure), 0, bgfx::Access::Write,
                             gfx::TextureFormat::RGBA16F);
            encoder.dispatch(context.viewId(), reduceProgram, 1, 1, 1);
        });
    m_postPasses += 3;
    return true;
}

RenderGraphTextureHandle PostProcessChain::addApplyExposurePass(RenderGraph& graph,
                                                               const PostProcessInputs& inputs) {
    const gfx::ProgramHandle program =
        m_shaderLibrary != nullptr ? m_shaderLibrary->program(shader::ProgramKey{shader::PassId::ApplyExposure, 0, 0})
                                   : gfx::ProgramHandle{};
    if (!program.valid() || !m_exposureSampler.valid() || !inputs.color.valid() || !inputs.fullscreen.valid()) {
        return {};
    }

    gfx::TextureDesc desc;
    desc.width = inputs.renderWidth;
    desc.height = inputs.renderHeight;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    desc.name = "postprocess.exposure.applied";
    const RenderGraphTextureHandle output = graph.createTexture(desc);
    const RenderGraphTextureHandle exposure =
        graph.importTexture(m_exposureTextures[m_exposureIndex], "PostProcess Exposure");

    graph.addRasterPass("Apply Exposure")
        .readTexture(inputs.color)
        .readTexture(exposure)
        .readBuffer(inputs.fullscreen)
        .setColorAttachment(output)
        .setViewSize(inputs.renderWidth, inputs.renderHeight)
        .allowCulling(false)
        .setRenderFunc([this, inputs, output, exposure, program](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, inputs.renderWidth, inputs.renderHeight);
            encoder.setViewFrameBuffer(context.viewId(), context.frameBuffer());
            encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            encoder.touch(context.viewId());
            encoder.setState(gfx::state::kFullscreen);
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_sourceSampler, context.texture(inputs.color),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(1, m_exposureSampler, context.texture(exposure),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setVertexBuffer(0, context.buffer(inputs.fullscreen));
            encoder.submit(context.viewId(), program);
        });
    ++m_postPasses;
    return output;
}

bool PostProcessChain::ensureTaaHistory(uint16_t renderWidth, uint16_t renderHeight) {
    if (m_resources == nullptr || renderWidth == 0 || renderHeight == 0) {
        return false;
    }
    if (m_taaHistoryTextures[0].valid() && m_taaHistoryTextures[1].valid() &&
        m_taaVelocityHistoryTextures[0].valid() && m_taaVelocityHistoryTextures[1].valid() &&
        m_taaHistoryWidth == renderWidth && m_taaHistoryHeight == renderHeight) {
        return true;
    }

    releaseTaaHistory();
    gfx::TextureDesc desc;
    desc.width = renderWidth;
    desc.height = renderHeight;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP;
    for (uint32_t index = 0; index < 2; ++index) {
        desc.name = "postprocess.taa.history." + std::to_string(index);
        m_taaHistoryTextures[index] = m_resources->createTexture(desc);
        if (!m_taaHistoryTextures[index].valid()) {
            HBG_LOGE("post-process: failed to create TAA history texture");
            releaseTaaHistory();
            return false;
        }
    }

    // HDRP stores the TAA velocity magnitude history separately from color
    // (R16F in GrabVelocityMagnitudeHistoryTextures). Use the same format and
    // fall back to R32F only when the backend cannot render to R16F.
    desc.format = gfx::TextureFormat::R16F;
    for (uint32_t index = 0; index < 2; ++index) {
        desc.name = "postprocess.taa.velocity." + std::to_string(index);
        m_taaVelocityHistoryTextures[index] = m_resources->createTexture(desc);
    }
    if (!m_taaVelocityHistoryTextures[0].valid() || !m_taaVelocityHistoryTextures[1].valid()) {
        for (gfx::TextureHandle& texture : m_taaVelocityHistoryTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
        desc.format = gfx::TextureFormat::R32F;
        for (uint32_t index = 0; index < 2; ++index) {
            desc.name = "postprocess.taa.velocity." + std::to_string(index);
            m_taaVelocityHistoryTextures[index] = m_resources->createTexture(desc);
        }
        if (!m_taaVelocityHistoryTextures[0].valid() || !m_taaVelocityHistoryTextures[1].valid()) {
            HBG_LOGE("post-process: failed to create TAA velocity history texture");
            releaseTaaHistory();
            return false;
        }
    }
    m_taaHistoryWidth = renderWidth;
    m_taaHistoryHeight = renderHeight;
    m_taaHistoryIndex = 0;
    m_taaHistoryFrame = 0;
    m_taaHistoryValid = false;
    return true;
}

void PostProcessChain::releaseTaaHistory() {
    if (m_resources != nullptr) {
        for (gfx::TextureHandle& texture : m_taaHistoryTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
        for (gfx::TextureHandle& texture : m_taaVelocityHistoryTextures) {
            if (texture.valid()) {
                m_resources->destroyTexture(texture);
            }
            texture = {};
        }
    } else {
        for (gfx::TextureHandle& texture : m_taaHistoryTextures) {
            texture = {};
        }
        for (gfx::TextureHandle& texture : m_taaVelocityHistoryTextures) {
            texture = {};
        }
    }
    m_taaHistoryWidth = 0;
    m_taaHistoryHeight = 0;
    m_taaHistoryIndex = 0;
    m_taaHistoryFrame = 0;
    m_taaHistoryValid = false;
    m_taaOutputHistory = {};
}

RenderGraphTextureHandle PostProcessChain::addTaaPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                                       const PostProcessInputs& inputs, bool upscale) {
    if (m_shaderLibrary == nullptr || !color.valid() || !inputs.fullscreen.valid() ||
        !inputs.motionVectors.valid() || !inputs.depth.valid() || inputs.renderWidth == 0 ||
        inputs.renderHeight == 0 || !m_sourceSampler.valid() || !m_taaHistorySampler.valid() ||
        !m_taaVelocitySampler.valid() || !m_taaMotionSampler.valid() || !m_taaDepthSampler.valid()) {
        return {};
    }
    if (upscale && (inputs.targetWidth == 0 || inputs.targetHeight == 0)) {
        return {};
    }

    const gfx::ProgramHandle program =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::Taa, 0, 0});
    if (!program.valid()) {
        HBG_LOGW("post-process: TAA shader program is unavailable");
        return {};
    }
    const uint16_t historyWidth = upscale ? inputs.targetWidth : inputs.renderWidth;
    const uint16_t historyHeight = upscale ? inputs.targetHeight : inputs.renderHeight;
    if (!ensureTaaHistory(historyWidth, historyHeight)) {
        return {};
    }

    const bool historyValid = inputs.historyValid && m_taaHistoryValid && m_taaHistoryFrame > 0;
    updateExposureUniforms(inputs);
    updateTaaUniforms(inputs, historyValid, historyWidth, historyHeight, upscale);

    const uint32_t previousIndex = m_taaHistoryIndex;
    const uint32_t nextIndex = previousIndex ^ 1u;
    const RenderGraphTextureHandle previous =
        graph.importTexture(m_taaHistoryTextures[previousIndex], "TAA Previous History");
    const RenderGraphTextureHandle next = graph.importTexture(m_taaHistoryTextures[nextIndex], "TAA History");
    const RenderGraphTextureHandle previousVelocity =
        graph.importTexture(m_taaVelocityHistoryTextures[previousIndex], "TAA Previous Velocity History");
    const RenderGraphTextureHandle nextVelocity =
        graph.importTexture(m_taaVelocityHistoryTextures[nextIndex], "TAA Velocity History");

    graph.addRasterPass(upscale ? "Temporal Anti-Aliasing (TAAU)" : "Temporal Anti-Aliasing")
        .readTexture(color)
        .readTexture(inputs.motionVectors)
        .readTexture(inputs.depth)
        .readTexture(previous)
        .readTexture(previousVelocity)
        .setColorAttachment(next)
        .setColorAttachment(nextVelocity, 1)
        .setViewSize(historyWidth, historyHeight)
        .allowCulling(false)
        .setRenderFunc([this, color, previous, previousVelocity, inputs, program, historyWidth,
                       historyHeight](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, historyWidth, historyHeight);
            encoder.setViewFrameBuffer(context.viewId(), context.frameBuffer());
            encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            encoder.touch(context.viewId());
            encoder.setState(gfx::state::kFullscreen);
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_sourceSampler, context.texture(color), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTexture(1, m_taaHistorySampler, context.texture(previous), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTexture(2, m_taaMotionSampler, context.texture(inputs.motionVectors),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(3, m_taaDepthSampler, context.texture(inputs.depth),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(4, m_taaVelocitySampler, context.texture(previousVelocity),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setVertexBuffer(0, context.buffer(inputs.fullscreen));
            encoder.submit(context.viewId(), program);
        });
    ++m_postPasses;
    m_taaHistoryIndex = nextIndex;
    m_taaHistoryValid = true;
    ++m_taaHistoryFrame;
    m_taaOutputHistory = next;

    if (!m_taaLogged) {
        HBG_LOGI("taa: history={}x{} RGBA16F, quality={}, base_history_weight={:.3f}, "
                 "motion_rejection={:.3f}, velocity_rejection={:.3f}, jitter_scale={:.3f}, sharpen={:.3f}, "
                 "upscale={}, injection={}",
                 historyWidth, historyHeight, m_settings.taa.quality, taaBaseHistoryWeight(m_settings.taa.quality),
                 m_settings.taa.motionVectorRejection, m_settings.taa.velocityRejection, m_settings.taa.jitterScale,
                 m_settings.taa.sharpening, upscale ? "true" : "false",
                 taaUpsampleInjectionName(m_settings.taa.upscaleInjection));
        m_taaLogged = true;
    }
    return next;
}

RenderGraphTextureHandle PostProcessChain::addTaaSharpenPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                                              const PostProcessInputs& inputs) {
    if (m_shaderLibrary == nullptr || !color.valid() || !inputs.fullscreen.valid() || inputs.renderWidth == 0 ||
        inputs.renderHeight == 0 || !m_sourceSampler.valid()) {
        return {};
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::TaaSharpen, 0, 0});
    if (!program.valid()) {
        HBG_LOGW("post-process: TAA sharpen shader program is unavailable");
        return {};
    }

    gfx::TextureDesc desc;
    desc.width = inputs.renderWidth;
    desc.height = inputs.renderHeight;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    desc.name = "postprocess.taa.sharpened";
    const RenderGraphTextureHandle output = graph.createTexture(desc);

    graph.addRasterPass("TAA Sharpen")
        .readTexture(color)
        .setColorAttachment(output)
        .setViewSize(inputs.renderWidth, inputs.renderHeight)
        .allowCulling(false)
        .setRenderFunc([this, color, output, inputs, program](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, inputs.renderWidth, inputs.renderHeight);
            encoder.setViewFrameBuffer(context.viewId(), context.frameBuffer());
            encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            encoder.touch(context.viewId());
            encoder.setState(gfx::state::kFullscreen);
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_sourceSampler, context.texture(color), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setVertexBuffer(0, context.buffer(inputs.fullscreen));
            encoder.submit(context.viewId(), program);
        });
    ++m_postPasses;
    return output;
}

RenderGraphTextureHandle PostProcessChain::addDofCocPass(RenderGraph& graph, const PostProcessInputs& inputs) {
    if (m_shaderLibrary == nullptr || !inputs.fullscreen.valid() || !inputs.depth.valid() ||
        inputs.renderWidth == 0 || inputs.renderHeight == 0 || !m_dofDepthSampler.valid()) {
        return {};
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::DofCoc, 0, 0});
    if (!program.valid()) {
        HBG_LOGW("post-process: DOF CoC shader program is unavailable");
        return {};
    }

    const float scale = dofResolutionScale(m_settings.depthOfField.resolution);
    const uint16_t cocWidth = static_cast<uint16_t>(
        std::max(1, static_cast<int>(std::lround(static_cast<float>(inputs.renderWidth) * scale))));
    const uint16_t cocHeight = static_cast<uint16_t>(
        std::max(1, static_cast<int>(std::lround(static_cast<float>(inputs.renderHeight) * scale))));
    m_dofCocWidth = cocWidth;
    m_dofCocHeight = cocHeight;
    m_dofTaps = dofGatherTapCount(m_settings.depthOfField.highQuality);
    updateDofUniforms(inputs, cocWidth, cocHeight);

    gfx::TextureDesc desc;
    desc.width = cocWidth;
    desc.height = cocHeight;
    desc.format = gfx::TextureFormat::RG16F;
    desc.renderTarget = true;
    desc.name = "postprocess.dof.coc";
    const RenderGraphTextureHandle coc = graph.createTexture(desc);

    graph.addRasterPass("Depth of Field CoC")
        .readTexture(inputs.depth)
        .setColorAttachment(coc)
        .setViewSize(cocWidth, cocHeight)
        .allowCulling(false)
        .setRenderFunc([this, coc, cocWidth, cocHeight, inputs, program](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, cocWidth, cocHeight);
            encoder.setViewFrameBuffer(context.viewId(), context.frameBuffer());
            encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            encoder.touch(context.viewId());
            encoder.setState(gfx::state::kFullscreen);
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_dofDepthSampler, context.texture(inputs.depth),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setVertexBuffer(0, context.buffer(inputs.fullscreen));
            encoder.submit(context.viewId(), program);
        });
    ++m_postPasses;

    if (!m_dofLogged) {
        HBG_LOGI("dof: coc={}x{} RG16F, taps={}, resolution={}, high_quality={}, near=[{:.2f},{:.2f}] "
                 "far=[{:.2f},{:.2f}], max_blur_px={:.2f}/{:.2f}",
                 cocWidth, cocHeight, m_dofTaps, m_settings.depthOfField.resolution,
                 m_settings.depthOfField.highQuality ? "true" : "false", m_settings.depthOfField.nearFocusStart,
                 m_settings.depthOfField.nearFocusEnd, m_settings.depthOfField.farFocusStart,
                 m_settings.depthOfField.farFocusEnd,
                 m_settings.depthOfField.nearMaxBlur * dofBlurScale(inputs.renderHeight),
                 m_settings.depthOfField.farMaxBlur * dofBlurScale(inputs.renderHeight));
        m_dofLogged = true;
    }
    return coc;
}

RenderGraphTextureHandle PostProcessChain::addDofGatherPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                                           RenderGraphTextureHandle coc, uint16_t cocWidth,
                                                           uint16_t cocHeight, const PostProcessInputs& inputs) {
    if (m_shaderLibrary == nullptr || !color.valid() || !coc.valid() || !inputs.fullscreen.valid() ||
        cocWidth == 0 || cocHeight == 0 || !m_sourceSampler.valid() || !m_dofCocSampler.valid()) {
        return {};
    }
    const bool highQuality = m_settings.depthOfField.highQuality;
    const gfx::ProgramHandle program =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::DofGather, highQuality ? 1u : 0u, 0});
    if (!program.valid()) {
        HBG_LOGW("post-process: DOF gather shader program is unavailable");
        return {};
    }

    gfx::TextureDesc desc;
    desc.width = cocWidth;
    desc.height = cocHeight;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    desc.name = "postprocess.dof.gather";
    const RenderGraphTextureHandle gather = graph.createTexture(desc);

    graph.addRasterPass("Depth of Field Gather")
        .readTexture(color)
        .readTexture(coc)
        .setColorAttachment(gather)
        .setViewSize(cocWidth, cocHeight)
        .allowCulling(false)
        .setRenderFunc([this, color, coc, gather, cocWidth, cocHeight, inputs, program](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, cocWidth, cocHeight);
            encoder.setViewFrameBuffer(context.viewId(), context.frameBuffer());
            encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            encoder.touch(context.viewId());
            encoder.setState(gfx::state::kFullscreen);
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_sourceSampler, context.texture(color), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTexture(1, m_dofCocSampler, context.texture(coc),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setVertexBuffer(0, context.buffer(inputs.fullscreen));
            encoder.submit(context.viewId(), program);
        });
    ++m_postPasses;
    return gather;
}

RenderGraphTextureHandle PostProcessChain::addDofCombinePass(RenderGraph& graph, RenderGraphTextureHandle color,
                                                            RenderGraphTextureHandle gather,
                                                            const PostProcessInputs& inputs) {
    if (m_shaderLibrary == nullptr || !color.valid() || !gather.valid() || !inputs.fullscreen.valid() ||
        !inputs.depth.valid() || inputs.renderWidth == 0 || inputs.renderHeight == 0 ||
        !m_sourceSampler.valid() || !m_dofGatherSampler.valid() || !m_dofDepthSampler.valid()) {
        return {};
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::DofCombine, 0, 0});
    if (!program.valid()) {
        HBG_LOGW("post-process: DOF combine shader program is unavailable");
        return {};
    }

    gfx::TextureDesc desc;
    desc.width = inputs.renderWidth;
    desc.height = inputs.renderHeight;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    desc.name = "postprocess.dof.combined";
    const RenderGraphTextureHandle output = graph.createTexture(desc);

    graph.addRasterPass("Depth of Field Combine")
        .readTexture(color)
        .readTexture(gather)
        .readTexture(inputs.depth)
        .setColorAttachment(output)
        .setViewSize(inputs.renderWidth, inputs.renderHeight)
        .allowCulling(false)
        .setRenderFunc([this, color, gather, output, inputs, program](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, inputs.renderWidth, inputs.renderHeight);
            encoder.setViewFrameBuffer(context.viewId(), context.frameBuffer());
            encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            encoder.touch(context.viewId());
            encoder.setState(gfx::state::kFullscreen);
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_sourceSampler, context.texture(color), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTexture(1, m_dofGatherSampler, context.texture(gather), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTexture(2, m_dofDepthSampler, context.texture(inputs.depth),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setVertexBuffer(0, context.buffer(inputs.fullscreen));
            encoder.submit(context.viewId(), program);
        });
    ++m_postPasses;
    return output;
}

RenderGraphTextureHandle PostProcessChain::addMotionBlurPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                                            const PostProcessInputs& inputs) {
    if (m_shaderLibrary == nullptr || !color.valid() || !inputs.fullscreen.valid() ||
        !inputs.motionVectors.valid() || !inputs.depth.valid() || inputs.renderWidth == 0 || inputs.renderHeight == 0 ||
        !m_sourceSampler.valid() || !m_motionBlurSampler.valid() || !m_motionBlurDepthSampler.valid()) {
        return {};
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::MotionBlur, 0, 0});
    if (!program.valid()) {
        HBG_LOGW("post-process: motion blur shader program is unavailable");
        return {};
    }

    gfx::TextureDesc desc;
    desc.width = inputs.renderWidth;
    desc.height = inputs.renderHeight;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    desc.name = "postprocess.motionBlur";
    const RenderGraphTextureHandle output = graph.createTexture(desc);

    graph.addRasterPass("Motion Blur")
        .readTexture(color)
        .readTexture(inputs.motionVectors)
        .readTexture(inputs.depth)
        .setColorAttachment(output)
        .setViewSize(inputs.renderWidth, inputs.renderHeight)
        .allowCulling(false)
        .setRenderFunc([this, color, output, inputs, program](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, inputs.renderWidth, inputs.renderHeight);
            encoder.setViewFrameBuffer(context.viewId(), context.frameBuffer());
            encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            encoder.touch(context.viewId());
            encoder.setState(gfx::state::kFullscreen);
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_sourceSampler, context.texture(color), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTexture(1, m_motionBlurSampler, context.texture(inputs.motionVectors),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setTexture(2, m_motionBlurDepthSampler, context.texture(inputs.depth),
                               BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT);
            encoder.setVertexBuffer(0, context.buffer(inputs.fullscreen));
            encoder.submit(context.viewId(), program);
        });
    ++m_postPasses;

    if (!m_motionBlurLogged) {
        HBG_LOGI("motion blur: samples={}, velocity=[{:.2f},{:.2f}] px, depth_extent={:.2f}, camera_motion={}",
                 m_settings.motionBlur.sampleCount, m_settings.motionBlur.minimumVelocity,
                 m_settings.motionBlur.maximumVelocity, m_settings.motionBlur.depthComparisonExtent,
                 m_settings.motionBlur.cameraMotionBlur ? "true" : "false");
        m_motionBlurLogged = true;
    }
    return output;
}

RenderGraphTextureHandle PostProcessChain::addUberPostPass(RenderGraph& graph, RenderGraphTextureHandle color,
                                                           RenderGraphTextureHandle bloom,
                                                           const PostProcessInputs& inputs) {
    const gfx::ProgramHandle program =
        m_shaderLibrary != nullptr
            ? m_shaderLibrary->program(shader::ProgramKey{shader::PassId::UberPost, 0, 0})
            : gfx::ProgramHandle{};
    if (!program.valid() || !color.valid() || !inputs.fullscreen.valid() || !m_colorGradingLut.valid() ||
        !m_colorGradingLutSampler.valid() || !m_bloomSampler.valid() || !m_bloomFallback.valid() ||
        inputs.targetWidth == 0 || inputs.targetHeight == 0) {
        return {};
    }

    gfx::TextureDesc desc;
    desc.width = inputs.targetWidth;
    desc.height = inputs.targetHeight;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.renderTarget = true;
    desc.name = "postprocess.uber.ldr";
    const RenderGraphTextureHandle output = graph.createTexture(desc);
    const RenderGraphTextureHandle lut = graph.importTexture(m_colorGradingLut, "Color Grading LUT");
    const RenderGraphTextureHandle bloomInput =
        bloom.valid() ? bloom : graph.importTexture(m_bloomFallback, "Bloom Fallback");

    graph.addRasterPass("Uber Post")
        .readTexture(color)
        .readTexture(lut)
        .readTexture(bloomInput)
        .readBuffer(inputs.fullscreen)
        .setColorAttachment(output)
        .setViewSize(inputs.targetWidth, inputs.targetHeight)
        .allowCulling(false)
        .setRenderFunc([this, color, lut, bloomInput, output, inputs, program](RenderGraphContext& context) {
            gfx::Encoder& encoder = context.encoder();
            encoder.setViewRect(context.viewId(), 0, 0, inputs.targetWidth, inputs.targetHeight);
            encoder.setViewFrameBuffer(context.viewId(), context.frameBuffer());
            encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
            encoder.touch(context.viewId());
            encoder.setState(gfx::state::kFullscreen);
            m_uniformBinder.upload(m_uniforms);
            encoder.setTexture(0, m_sourceSampler, context.texture(color), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTexture(1, m_colorGradingLutSampler, context.texture(lut), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setTexture(2, m_bloomSampler, context.texture(bloomInput), BGFX_SAMPLER_UVW_CLAMP);
            encoder.setVertexBuffer(0, context.buffer(inputs.fullscreen));
            encoder.submit(context.viewId(), program);
        });
    ++m_postPasses;
    return output;
}

RenderGraphTextureHandle PostProcessChain::addBloomPasses(RenderGraph& graph, RenderGraphTextureHandle color,
                                                          const PostProcessInputs& inputs) {
    if (m_shaderLibrary == nullptr || !color.valid() || !inputs.fullscreen.valid() || inputs.renderWidth == 0 ||
        inputs.renderHeight == 0) {
        return {};
    }

    const bool highQualityPrefilter = m_settings.bloom.highQualityPrefiltering;
    const bool highQualityFiltering =
        m_settings.bloom.highQualityFiltering && inputs.renderWidth >= 800 && inputs.renderHeight >= 450;
    const gfx::ProgramHandle prefilterProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::BloomPrefilter, highQualityPrefilter ? 1u : 0u, 0});
    const gfx::ProgramHandle blurHorizontalProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::BloomBlur, 0, 0});
    const gfx::ProgramHandle blurVerticalProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::BloomBlur, 1, 0});
    const gfx::ProgramHandle downsampleProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::BloomDownsample, 0, 0});
    const gfx::ProgramHandle upsampleProgram =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::BloomUpsample, highQualityFiltering ? 1u : 0u, 0});
    if (!prefilterProgram.valid() || !blurHorizontalProgram.valid() || !blurVerticalProgram.valid() ||
        !downsampleProgram.valid() || !upsampleProgram.valid() || !m_sourceSampler.valid() ||
        !m_secondarySampler.valid()) {
        HBG_LOGW("post-process: bloom shader programs are unavailable");
        return {};
    }

    const uint32_t mipCount =
        ::hbg::render::bloomMipCount(inputs.renderWidth, inputs.renderHeight, m_settings.bloom.resolution);
    m_bloomMipCount = mipCount;
    m_bloomMip0 = bloomMipSize(inputs.renderWidth, inputs.renderHeight, m_settings.bloom.resolution, 0);

    std::array<RenderGraphTextureHandle, kMaxBloomMips> mipsDown{};
    std::array<RenderGraphTextureHandle, kMaxBloomMips> mipsUp{};
    for (uint32_t index = 0; index < mipCount; ++index) {
        const BloomMipSize size =
            bloomMipSize(inputs.renderWidth, inputs.renderHeight, m_settings.bloom.resolution, index);
        gfx::TextureDesc desc;
        desc.width = size.width;
        desc.height = size.height;
        desc.format = gfx::TextureFormat::RGBA16F;
        desc.renderTarget = true;
        desc.name = "postprocess.bloom.down." + std::to_string(index);
        mipsDown[index] = graph.createTexture(desc);
        desc.name = "postprocess.bloom.up." + std::to_string(index);
        mipsUp[index] = graph.createTexture(desc);
    }
    gfx::TextureDesc scratchDesc;
    scratchDesc.width = m_bloomMip0.width;
    scratchDesc.height = m_bloomMip0.height;
    scratchDesc.format = gfx::TextureFormat::RGBA16F;
    scratchDesc.renderTarget = true;
    scratchDesc.name = "postprocess.bloom.scratch";
    const RenderGraphTextureHandle scratch = graph.createTexture(scratchDesc);

    const auto addBloomPass = [this, &graph, fullscreen = inputs.fullscreen](
                                  const char* name, RenderGraphTextureHandle source,
                                  RenderGraphTextureHandle second, RenderGraphTextureHandle destination,
                                  gfx::ProgramHandle program, uint16_t width, uint16_t height, Vec4 texel) {
        RenderGraphBuilder pass = graph.addRasterPass(name);
        pass.readTexture(source);
        if (second.valid()) {
            pass.readTexture(second);
        }
        pass.readBuffer(fullscreen)
            .setColorAttachment(destination)
            .setViewSize(width, height)
            .allowCulling(false)
            .setRenderFunc([this, source, second, destination, program, fullscreen, width, height,
                           texel](RenderGraphContext& context) {
                gfx::Encoder& encoder = context.encoder();
                encoder.setViewRect(context.viewId(), 0, 0, width, height);
                encoder.setViewFrameBuffer(context.viewId(), context.frameBuffer());
                encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
                encoder.touch(context.viewId());
                encoder.setState(gfx::state::kFullscreen);
                m_uniforms.bloomTexelSize = texel;
                m_uniformBinder.upload(m_uniforms);
                encoder.setTexture(0, m_sourceSampler, context.texture(source), BGFX_SAMPLER_UVW_CLAMP);
                if (second.valid()) {
                    encoder.setTexture(1, m_secondarySampler, context.texture(second), BGFX_SAMPLER_UVW_CLAMP);
                }
                encoder.setVertexBuffer(0, context.buffer(fullscreen));
                encoder.submit(context.viewId(), program);
            });
        ++m_postPasses;
    };

    const BloomMipSize mip0 = bloomMipSize(inputs.renderWidth, inputs.renderHeight, m_settings.bloom.resolution, 0);
    const Vec4 mip0Texel(static_cast<float>(inputs.renderWidth), static_cast<float>(inputs.renderHeight),
                         1.0f / static_cast<float>(inputs.renderWidth),
                         1.0f / static_cast<float>(inputs.renderHeight));
    addBloomPass("Bloom Prefilter", color, {}, mipsUp[0], prefilterProgram, mip0.width, mip0.height, mip0Texel);

    const Vec4 mip0SourceTexel = mip0Texel;
    addBloomPass("Bloom Blur Horizontal", mipsUp[0], {}, scratch, blurHorizontalProgram, mip0.width, mip0.height,
                 mip0SourceTexel);
    addBloomPass("Bloom Blur Vertical", scratch, {}, mipsDown[0], blurVerticalProgram, mip0.width, mip0.height,
                 mip0SourceTexel);

    for (uint32_t index = 0; index + 1 < mipCount; ++index) {
        const BloomMipSize sourceSize =
            bloomMipSize(inputs.renderWidth, inputs.renderHeight, m_settings.bloom.resolution, index);
        const BloomMipSize destSize =
            bloomMipSize(inputs.renderWidth, inputs.renderHeight, m_settings.bloom.resolution, index + 1);
        const Vec4 sourceTexel(static_cast<float>(sourceSize.width), static_cast<float>(sourceSize.height),
                               1.0f / static_cast<float>(sourceSize.width),
                               1.0f / static_cast<float>(sourceSize.height));
        addBloomPass(("Bloom Downsample " + std::to_string(index + 1)).c_str(), mipsDown[index], {},
                     mipsDown[index + 1], downsampleProgram, destSize.width, destSize.height, sourceTexel);
    }

    if (mipCount > 1) {
        for (int32_t index = static_cast<int32_t>(mipCount) - 2; index >= 0; --index) {
            const RenderGraphTextureHandle low =
                index == static_cast<int32_t>(mipCount) - 2 ? mipsDown[mipCount - 1] : mipsUp[index + 1];
            const BloomMipSize lowSize = bloomMipSize(inputs.renderWidth, inputs.renderHeight,
                                                      m_settings.bloom.resolution, static_cast<uint32_t>(index + 1));
            const BloomMipSize highSize = bloomMipSize(inputs.renderWidth, inputs.renderHeight,
                                                       m_settings.bloom.resolution, static_cast<uint32_t>(index));
            const Vec4 lowTexel(static_cast<float>(lowSize.width), static_cast<float>(lowSize.height),
                                1.0f / static_cast<float>(lowSize.width),
                                1.0f / static_cast<float>(lowSize.height));
            addBloomPass(("Bloom Upsample " + std::to_string(index)).c_str(), low, mipsDown[index],
                         mipsUp[index], upsampleProgram, highSize.width, highSize.height, lowTexel);
        }
    }

    if (!m_bloomLogged) {
        HBG_LOGI("bloom: mips={}, base={}x{}, resolution={}x, high_quality_prefilter={}, high_quality_filtering={}, "
                 "threshold={:.3f}, intensity={:.3f}, scatter={:.3f}",
                 mipCount, m_bloomMip0.width, m_bloomMip0.height, m_settings.bloom.resolution,
                 highQualityPrefilter ? "true" : "false", highQualityFiltering ? "true" : "false",
                 m_settings.bloom.threshold, m_settings.bloom.intensity, m_settings.bloom.scatter);
        m_bloomLogged = true;
    }

    return mipCount > 1 ? mipsUp[0] : mipsDown[0];
}

bool PostProcessChain::ensureColorGradingLut() {
    if (m_resources == nullptr || m_shaderLibrary == nullptr) {
        return false;
    }
    if (m_lutValid && m_colorGradingLut.valid() && m_lutGrading == m_settings.colorGrading &&
        m_lutTonemapping == m_effectiveTonemapping) {
        return true;
    }

    ColorGradingLut lut;
    if (!buildColorGradingLut(m_settings.colorGrading, m_effectiveTonemapping, lut) || !lut.valid) {
        HBG_LOGE("post-process: failed to build the color grading LUT");
        m_lutValid = false;
        return false;
    }

    const uint32_t dataSize = static_cast<uint32_t>(lut.texels.size() * sizeof(uint16_t));
    if (!m_colorGradingLut.valid()) {
        gfx::TextureDesc desc;
        desc.width = static_cast<uint16_t>(kColorGradingLutWidth);
        desc.height = static_cast<uint16_t>(kColorGradingLutHeight);
        desc.format = gfx::TextureFormat::RGBA16F;
        desc.flags = BGFX_SAMPLER_UVW_CLAMP;
        desc.data = lut.texels.data();
        desc.dataSize = dataSize;
        desc.name = "postprocess.colorGradingLut";
        m_colorGradingLut = m_resources->createTexture(desc);
        if (!m_colorGradingLut.valid()) {
            HBG_LOGE("post-process: failed to create the color grading LUT texture");
            m_lutValid = false;
            return false;
        }
    } else if (!m_resources->updateTexture(m_colorGradingLut, lut.texels.data(), dataSize,
                                           static_cast<uint16_t>(kColorGradingLutWidth),
                                           static_cast<uint16_t>(kColorGradingLutHeight),
                                           static_cast<uint16_t>(kColorGradingLutWidth * 4u * sizeof(uint16_t)))) {
        HBG_LOGE("post-process: failed to upload the color grading LUT");
        m_lutValid = false;
        return false;
    }

    m_lutGrading = m_settings.colorGrading;
    m_lutTonemapping = m_effectiveTonemapping;
    m_lutValid = true;
    ++m_colorGradingLutUploadCount;
    HBG_LOGI("color grading LUT uploaded: {}^3 -> {}x{} RGBA16F, {} bytes, tonemap={}, grading={}",
             kColorGradingLutSize, kColorGradingLutWidth, kColorGradingLutHeight, colorGradingLutUploadBytes(),
             tonemappingModeName(m_effectiveTonemapping), m_settings.colorGrading.enabled ? "on" : "off");
    return true;
}

void PostProcessChain::releaseColorGradingLut() {
    if (m_resources != nullptr && m_colorGradingLut.valid()) {
        m_resources->destroyTexture(m_colorGradingLut);
    }
    m_colorGradingLut = {};
    m_lutValid = false;
}

void PostProcessChain::updateExposureUniforms(const PostProcessInputs& inputs) {
    const float range = std::max(m_settings.limitMax - m_settings.limitMin, 1.0e-5f);
    const float histogramScale = 1.0f / range;
    const float histogramBias = -m_settings.limitMin * histogramScale;
    float exposureMode = 2.0f;
    float fixedExposure = 1.0f;
    if (m_frame.isEnabled(FrameSettingsField::Postprocess) &&
        m_frame.isEnabled(FrameSettingsField::ExposureControl)) {
        if (m_exposureApplied) {
            exposureMode = 2.0f;
        } else if (m_settings.exposureMode == ExposureMode::Fixed) {
            exposureMode = 0.0f;
            fixedExposure = fixedExposureMultiplier(m_settings);
        } else {
            exposureMode = 1.0f;
        }
    }
    m_uniforms.exposureParams =
        Vec4(exposureMode, fixedExposure, m_settings.compensation + m_settings.debugExposure,
             lensImperfectionExposureScale());
    m_uniforms.autoExposureParams = Vec4(m_settings.limitMin, m_settings.limitMax,
                                         m_settings.adaptationSpeedDarkToLight,
                                         m_settings.adaptationSpeedLightToDark);
    m_uniforms.tonemapParams =
        Vec4(static_cast<float>(m_effectiveTonemapping), m_exposureHistoryValid ? 1.0f : 0.0f,
             m_settings.metering == MeteringMode::CenterWeighted ? 1.0f : 0.0f,
             std::max(inputs.deltaSeconds, 0.0f));
    m_uniforms.frameParams = Vec4(m_frame.isEnabled(FrameSettingsField::Postprocess) ? 1.0f : 0.0f,
                                  m_effectsActive ? 1.0f : 0.0f, 1.0f,
                                  m_exposureHistoryValid ? 1.0f : 0.0f);
    m_uniforms.screenParams = Vec4(static_cast<float>(inputs.renderWidth), static_cast<float>(inputs.renderHeight),
                                   static_cast<float>(inputs.targetWidth), static_cast<float>(inputs.targetHeight));
    m_uniforms.histogramParams =
        Vec4(histogramScale, histogramBias, m_settings.histogramMinPercentile, m_settings.histogramMaxPercentile);
}

void PostProcessChain::updateTaaUniforms(const PostProcessInputs& inputs, bool historyValid, uint16_t historyWidth,
                                         uint16_t historyHeight, bool upscale) {
    const float width = static_cast<float>(historyWidth);
    const float height = static_cast<float>(historyHeight);
    const float rejection =
        upscale ? m_settings.taa.velocityRejection : m_settings.taa.motionVectorRejection;
    m_uniforms.taaParams = Vec4(historyValid ? 1.0f : 0.0f, taaBaseHistoryWeight(m_settings.taa.quality),
                                rejection, inputs.taaJitter.x);
    m_uniforms.taaParams2 = Vec4(static_cast<float>(m_settings.taa.quality), m_settings.taa.sharpening,
                                 static_cast<float>(m_taaHistoryFrame), inputs.taaJitter.y);
    m_uniforms.taaHistorySize =
        Vec4(width, height, width > 0.0f ? 1.0f / width : 0.0f, height > 0.0f ? 1.0f / height : 0.0f);
}

void PostProcessChain::updateDofUniforms(const PostProcessInputs& inputs, uint16_t cocWidth, uint16_t cocHeight) {
    const DepthOfFieldSettings& dof = m_settings.depthOfField;
    const float blurScale = dofBlurScale(inputs.renderHeight);
    const float width = static_cast<float>(cocWidth);
    const float height = static_cast<float>(cocHeight);
    m_uniforms.dofParams = Vec4(dof.nearFocusStart, dof.nearFocusEnd, dof.farFocusStart, dof.farFocusEnd);
    m_uniforms.dofParams2 =
        Vec4(std::max(dof.nearMaxBlur, 0.0f) * blurScale, std::max(dof.farMaxBlur, 0.0f) * blurScale,
             dofResolutionScale(dof.resolution), dof.highQuality ? 1.0f : 0.0f);
    m_uniforms.dofCocSize =
        Vec4(width, height, width > 0.0f ? 1.0f / width : 0.0f, height > 0.0f ? 1.0f / height : 0.0f);
}

void PostProcessChain::updateMotionBlurUniforms(const PostProcessInputs& inputs) {
    const MotionBlurSettings& motion = m_settings.motionBlur;
    const float screenMagnitude =
        motionBlurScreenMagnitude(inputs.renderWidth, inputs.renderHeight);
    const float maximumNdc =
        screenMagnitude > 0.0f ? std::max(motion.maximumVelocity, 0.0f) / screenMagnitude : 0.0f;
    m_uniforms.motionBlurParams =
        Vec4(motion.intensity, maximumNdc, motion.minimumVelocity, static_cast<float>(motion.sampleCount));
    m_uniforms.motionBlurParams2 =
        Vec4(screenMagnitude, motion.depthComparisonExtent, motion.cameraMotionBlur ? 1.0f : 0.0f, 2.0f);
}

void PostProcessChain::updateScreenUniforms(const PostProcessInputs& inputs) {
    m_uniforms.screenParams = Vec4(static_cast<float>(inputs.renderWidth), static_cast<float>(inputs.renderHeight),
                                   static_cast<float>(inputs.targetWidth), static_cast<float>(inputs.targetHeight));
}

void PostProcessChain::updateUberUniforms() {
    const ColorGradingSettings& grading = m_settings.colorGrading;
    const VignetteSettings& vignette = m_settings.vignette;
    m_uniforms.gradingParams =
        Vec4(std::exp2(grading.postExposure), static_cast<float>(kColorGradingLutSize),
             grading.enabled ? 1.0f : 0.0f, vignette.active() ? 1.0f : 0.0f);
    m_uniforms.vignetteParams = Vec4(vignette.center.x, vignette.center.y, vignette.intensity, vignette.smoothness);
    m_uniforms.vignetteParams2 = Vec4(vignette.roundness, vignette.rounded ? 1.0f : 0.0f, vignette.opacity, 0.0f);
    m_uniforms.vignetteColor = Vec4(vignette.color.x, vignette.color.y, vignette.color.z, 0.0f);
    m_uniforms.bloomParams =
        Vec4(bloomCompositeIntensity(m_settings.bloom.intensity), bloomThresholdLinear(m_settings.bloom.threshold),
             m_bloomActive && m_bloomOutput.valid() ? 1.0f : 0.0f, bloomScatterParam(m_settings.bloom.scatter));
    const Vec3 bloomTint = normalizedBloomTint(m_settings.bloom.tint);
    m_uniforms.bloomTint = Vec4(bloomTint.x, bloomTint.y, bloomTint.z, 0.0f);
    m_uniforms.bloomTexelSize = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
}

bool PostProcessChain::addFinalPass(RenderGraph& graph, RenderGraphTextureHandle ldrColor,
                                    RenderGraphBufferHandle fullscreen, uint16_t targetWidth,
                                    uint16_t targetHeight) {
    if (!m_initialized || m_shaderLibrary == nullptr || !ldrColor.valid() || !fullscreen.valid() ||
        !m_ldrSampler.valid()) {
        return false;
    }
    const gfx::ProgramHandle program =
        m_shaderLibrary->program(shader::ProgramKey{shader::PassId::Final, m_fxaaActive ? 1u : 0u, 0});
    if (!program.valid()) {
        HBG_LOGW("post-process: final pass program unavailable");
        return false;
    }

    RenderGraphBuilder pass = graph.addRasterPass("PostProcess Final");
    pass.readTexture(ldrColor);
    pass.readBuffer(fullscreen);
    pass.writeBackbuffer();
    pass.allowCulling(false);
    pass.setRenderFunc([this, program, ldrColor, fullscreen, targetWidth, targetHeight](RenderGraphContext& context) {
        gfx::Encoder& encoder = context.encoder();
        encoder.setViewRect(context.viewId(), 0, 0, targetWidth, targetHeight);
        encoder.setViewFrameBuffer(context.viewId(), {});
        encoder.setViewClear(context.viewId(), BGFX_CLEAR_COLOR, 0x000000ff, 1.0f, 0);
        encoder.touch(context.viewId());
        encoder.setState(gfx::state::kFullscreen);
        m_uniformBinder.upload(m_uniforms);
        encoder.setTexture(0, m_ldrSampler, context.texture(ldrColor));
        encoder.setVertexBuffer(0, context.buffer(fullscreen));
        encoder.submit(context.viewId(), program);
    });
    ++m_postPasses;
    return true;
}

void PostProcessChain::endFrame() {
    if (m_initialized) {
        m_exposureIndex ^= 1u;
    }
}

bool PostProcessChain::ensureExposureTextures() {
    if (m_resources == nullptr) {
        return false;
    }
    for (gfx::TextureHandle& texture : m_exposureTextures) {
        if (texture.valid()) {
            continue;
        }
        gfx::TextureDesc desc;
        desc.width = 1;
        desc.height = 1;
        desc.format = gfx::TextureFormat::RGBA16F;
        desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
        desc.computeWrite = true;
        desc.name = "postprocess.exposure";
        texture = m_resources->createTexture(desc);
        if (!texture.valid()) {
            releaseExposureTextures();
            return false;
        }
    }
    return true;
}

bool PostProcessChain::ensureHistogramTexture() {
    if (!m_computeSupported) {
        return true;
    }
    if (m_resources == nullptr) {
        return false;
    }
    if (m_histogramTexture.valid()) {
        return true;
    }

    gfx::TextureDesc desc;
    desc.width = static_cast<uint16_t>(kExposureHistogramBins);
    desc.height = 1;
    desc.format = gfx::TextureFormat::R32U;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP | BGFX_SAMPLER_POINT;
    desc.computeWrite = true;
    desc.name = "postprocess.histogram";
    m_histogramTexture = m_resources->createTexture(desc);
    if (!m_histogramTexture.valid()) {
        HBG_LOGE("post-process: failed to create histogram texture");
        return false;
    }
    return true;
}

void PostProcessChain::releaseHistogramTexture() {
    if (m_resources != nullptr && m_histogramTexture.valid()) {
        m_resources->destroyTexture(m_histogramTexture);
    }
    m_histogramTexture = {};
}

bool PostProcessChain::ensureBloomFallback() {
    if (m_resources == nullptr) {
        return false;
    }
    if (m_bloomFallback.valid()) {
        return true;
    }
    const uint16_t zero[4] = {0, 0, 0, 0};
    gfx::TextureDesc desc;
    desc.width = 1;
    desc.height = 1;
    desc.format = gfx::TextureFormat::RGBA16F;
    desc.flags = BGFX_SAMPLER_UVW_CLAMP;
    desc.data = zero;
    desc.dataSize = sizeof(zero);
    desc.name = "postprocess.bloomFallback";
    m_bloomFallback = m_resources->createTexture(desc);
    if (!m_bloomFallback.valid()) {
        HBG_LOGE("post-process: failed to create the bloom fallback texture");
        return false;
    }
    return true;
}

void PostProcessChain::releaseBloomFallback() {
    if (m_resources != nullptr && m_bloomFallback.valid()) {
        m_resources->destroyTexture(m_bloomFallback);
    }
    m_bloomFallback = {};
}

void PostProcessChain::releaseExposureTextures() {
    if (m_resources == nullptr) {
        for (gfx::TextureHandle& texture : m_exposureTextures) {
            texture = {};
        }
        return;
    }
    for (gfx::TextureHandle& texture : m_exposureTextures) {
        if (texture.valid()) {
            m_resources->destroyTexture(texture);
        }
        texture = {};
    }
}

void PostProcessChain::resetHistory() {
    m_history.reset();
    m_exposureHistoryValid = false;
    m_exposureApplied = false;
    releaseTaaHistory();
    m_taaActive = false;
    m_taaLogged = false;
    m_taaUpscaleSharpeningLogged = false;
    m_taaUpscaleAfterPostFallbackLogged = false;
}

void PostProcessChain::resetHistoryPreservingTaa() {
    m_history.reset();
    m_exposureApplied = false;
}

} // namespace hbg::render
