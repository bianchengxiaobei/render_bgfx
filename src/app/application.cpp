#include "app/application.h"

#include "app/asset_test.h"
#include "app/gfx_test.h"
#include "app/render_test.h"
#include "app/sg_spike.h"
#include "render/vfx/vfx_reference.h"
#include "render/vfx/vfx_spike.h"
#include "asset/animation.h"
#include "asset/scene_loader.h"
#include "core/log.h"
#include "vfx/editor.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#ifndef HBG_VFX_SHADERC_DEFAULT
#define HBG_VFX_SHADERC_DEFAULT ""
#endif
#ifndef HBG_VFX_BGFX_SOURCE_DEFAULT
#define HBG_VFX_BGFX_SOURCE_DEFAULT ""
#endif
#ifndef HBG_VFX_SHADER_PACK_DEFAULT
#define HBG_VFX_SHADER_PACK_DEFAULT ""
#endif

namespace hbg {
namespace {

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string environmentValue(const char* name) {
#ifdef _WIN32
    char* value = nullptr;
    std::size_t size = 0;
    if (_dupenv_s(&value, &size, name) == 0 && value != nullptr) {
        std::string result(value);
        std::free(value);
        return result;
    }
    return {};
#else
    const char* value = std::getenv(name);
    return value != nullptr ? value : std::string();
#endif
}

std::filesystem::path compilePath(const std::string& cli, const char* environment, const char* fallback) {
    if (!cli.empty()) {
        return cli;
    }
    std::string value = environmentValue(environment);
    if (!value.empty()) {
        return value;
    }
    return fallback;
}

VfxEditorCompileOptions editorCompileOptions(const AppOptions& options) {
    VfxEditorCompileOptions compile;
    compile.shaderc =
        compilePath(options.vfxShadercPath, "HBG_SHADERC", HBG_VFX_SHADERC_DEFAULT);
    compile.bgfxSource =
        compilePath(options.vfxBgfxSourcePath, "HBG_BGFX_SOURCE_DIR", HBG_VFX_BGFX_SOURCE_DEFAULT);
    compile.shaderPack =
        compilePath(options.vfxShaderPackPath, "HBG_VFX_SHADER_PACK", HBG_VFX_SHADER_PACK_DEFAULT);
    return compile;
}

std::filesystem::path sceneVfxPath(const std::filesystem::path& editorAssetPath) {
    return vfx::editor::defaultBinaryPath(editorAssetPath);
}

} // namespace

int Application::run(const AppOptions& options) {
    log::setLevel(options.verbose ? log::Level::Debug : log::Level::Info);

    if (!loadSettings(options)) {
        return 1;
    }
    m_animationTime = options.animationTime;

    if (!init(options)) {
        shutdown();
        return 1;
    }

    if (!options.sgEditorPath.empty()) {
        SgEditorHost::Options hostOptions;
        hostOptions.documentPath = options.sgEditorPath;
        hostOptions.root = options.sgRoot.empty() ? std::filesystem::current_path() : options.sgRoot;
        hostOptions.webRoot = options.sgWebRoot;
        hostOptions.previewBackend =
            bgfx::getRendererType() == bgfx::RendererType::Vulkan ? "spirv" : "dxbc";
        hostOptions.port = static_cast<std::uint16_t>(std::clamp(options.sgPort, 1, 65535));
        hostOptions.token = options.sgToken;
        auto host = std::make_unique<SgEditorHost>();
        std::string hostError;
        if (!host->start(hostOptions, hostError)) {
            HBG_LOGE("SG editor host failed to start: {}", hostError);
            shutdown();
            return 1;
        }
        m_sgEditor = std::move(host);
        auto previewRenderer = std::make_unique<SgGraphPreviewRenderer>();
        std::string previewError;
        if (previewRenderer->init(m_device.resources(), &previewError)) {
            SgGraphPreviewRenderer* renderer = previewRenderer.get();
            m_sgEditor->setPreviewCallback(
                [renderer](const std::vector<std::uint8_t>& sgb, const SgPreviewOptions& preview,
                           std::vector<std::uint8_t>& jpeg, std::string& error) {
                    return renderer->render(sgb, preview, jpeg, error);
                });
            m_sgPreview = std::move(previewRenderer);
        } else {
            HBG_LOGW("SG preview renderer initialization failed: {}", previewError);
        }
        HBG_LOGI("SG editor host ready on port {}", m_sgEditor->port());
    }

    if (!options.assetTestPath.empty()) {
        const bool ok = runAssetTest(m_device.resources(), options.assetTestPath);
        shutdown();
        return ok ? 0 : 1;
    }

    if (options.gfxTest) {
        const bool ok = runGfxTest(m_device.resources(), static_cast<uint16_t>(m_window.width()),
                                   static_cast<uint16_t>(m_window.height()));
        shutdown();
        return ok ? 0 : 1;
    }

    if (options.vfxSpike) {
        const bool ok = runVfxSpike(m_device.resources(), static_cast<uint16_t>(m_window.width()),
                                    static_cast<uint16_t>(m_window.height()));
        shutdown();
        return ok ? 0 : 1;
    }

    if (options.sgSpike || options.sgPreview) {
        SgSpikeOptions sgOptions;
        sgOptions.assetPath = options.sgAssetPath;
        sgOptions.screenshotPath = options.screenshotPath;
        sgOptions.token = options.sgToken;
        sgOptions.port = static_cast<uint16_t>(std::clamp(options.sgPort, 1, 65535));
        sgOptions.frames = static_cast<std::uint32_t>(std::max(options.frames, 0));
        sgOptions.framesSpecified = options.framesSpecified;
        sgOptions.preview = options.sgPreview;
        const bool ok = runSgSpike(m_device.resources(), static_cast<std::uint16_t>(512),
                                   static_cast<std::uint16_t>(512), sgOptions);
        shutdown();
        return ok ? 0 : 1;
    }

    if (options.vfxCpuEventReference) {
        const std::string assetPath = options.vfxReferencePath.empty()
                                          ? std::string("vfx/tests/assets/vfx_cpu_event.vfxb")
                                          : options.vfxReferencePath;
        const bool ok =
            runVfxCpuEventReference(m_device.resources(), assetPath, options.vfxReferenceGraphPath);
        shutdown();
        return ok ? 0 : 1;
    }

    if (options.vfxEventReference) {
        const std::string assetPath = options.vfxReferencePath.empty()
                                          ? std::string("vfx/tests/assets/vfx_event.vfxb")
                                          : options.vfxReferencePath;
        const bool ok =
            runVfxEventReference(m_device.resources(), assetPath, options.vfxReferenceGraphPath);
        shutdown();
        return ok ? 0 : 1;
    }

    if (options.vfxReference) {
        const std::string assetPath = options.vfxReferencePath.empty()
                                          ? std::string("vfx/tests/assets/reference_1024.vfxb")
                                          : options.vfxReferencePath;
        const bool ok = runVfxReference(m_device.resources(), assetPath, options.vfxReferenceGraphPath);
        shutdown();
        return ok ? 0 : 1;
    }

    if (!options.renderTestPath.empty()) {
        const bool ok = runRenderTest(
            m_device.resources(), options.renderTestPath, static_cast<uint16_t>(m_window.width()),
            static_cast<uint16_t>(m_window.height()), static_cast<uint32_t>(options.frames), options.screenshotPath,
            options.debugDisplayMode, options.debugLightmapMask, options.debugLightClusterSlice,
            !options.disableLightCluster, options.cameraOrbit,
            !options.disableShadows, options.clusterStatsReadback, m_pipelineSettings, m_frameSettings,
            m_shadowSettings, m_ssaoSettings, m_ssrSettings, m_ssgiSettings, m_postProcessProfile,
            m_volumetricFogSettings, options.renderScale, options.dynamicResolutionSettings, options.skyPath,
            options.skyIntensity, options.skyRotation, options.resizeTest);
        shutdown();
        return ok ? 0 : 1;
    }

    if (options.vfxEditorCompile && options.vfxEditorPath.empty()) {
        HBG_LOGE("--vfx-editor-compile requires --vfx-editor=<path>");
        shutdown();
        return 1;
    }
    if (options.vfxEditorPreview && options.vfxEditorPath.empty()) {
        HBG_LOGE("--vfx-editor-preview requires --vfx-editor=<path>");
        shutdown();
        return 1;
    }
    if (!options.vfxEditorPath.empty() && options.scenePath.empty()) {
        HBG_LOGE("--vfx-editor requires --scene=<path> for the preview window");
        shutdown();
        return 1;
    }
    if (!options.scenePath.empty() && !loadScene(options.scenePath)) {
        shutdown();
        return 1;
    }
    if (m_forwardRenderer.valid()) {
        if (!options.skyPath.empty()) {
            if (!m_forwardRenderer.setSky(options.skyPath, options.skyIntensity, options.skyRotation)) {
                HBG_LOGW("sky load failed; continuing without an environment");
            }
        } else if (m_scene != nullptr && m_scene->sky.enabled &&
                   m_scene->sky.mode == render::SceneSkyMode::Skybox && !m_scene->sky.texture.empty()) {
            if (!m_forwardRenderer.setSky(m_scene->sky.texture.string(), m_scene->sky.intensity,
                                          m_scene->sky.rotationDegrees)) {
                HBG_LOGW("sidecar skybox load failed; continuing without an environment");
            }
        }
        m_forwardRenderer.setRenderScale(options.renderScale);
        m_dynamicResolution.configure(options.dynamicResolutionSettings);
        m_dynamicResolution.reset(options.renderScale);
        m_dynamicResolutionScale = options.renderScale;
        m_forwardRenderer.setLightClusterEnabled(!options.disableLightCluster);
        m_forwardRenderer.setPostProcessProfile(m_postProcessProfile);
        m_forwardRenderer.setVolumetricFogSettings(m_volumetricFogSettings);
    }

    const bool needsUi = options.debugUi || !options.vfxEditorPath.empty();
    if (m_forwardRenderer.valid() && needsUi) {
        if (!m_debugUi.init(m_device.resources(), m_forwardRenderer.shaderLibrary())) {
            HBG_LOGW("debug ui initialization failed; continuing without the panel");
        }
    }
    if (m_debugUi.valid()) {
        render::DebugDisplaySettings& settings = m_debugUi.settings();
        settings.fullscreen = options.debugDisplayMode;
        settings.lightClusterSlice = options.debugLightClusterSlice;
        settings.wireframe = options.debugWireframe;
        settings.shadows = !options.disableShadows;
        settings.gpuLightList = m_pipelineSettings.supportGpuLightList;
        settings.gpuDrivenGeometry = m_pipelineSettings.supportGpuDrivenGeometry;
        settings.renderScale = options.renderScale;
        settings.postProcess = m_postProcessProfile.settings;
    } else {
        m_forwardRenderer.setDebugDisplayMode(options.debugDisplayMode);
        m_forwardRenderer.setDebugLightClusterSlice(options.debugLightClusterSlice);
        m_forwardRenderer.setWireframe(options.debugWireframe);
        m_forwardRenderer.setShadowsEnabled(!options.disableShadows);
    }

    if (!options.vfxEditorPath.empty()) {
        if (!m_debugUi.valid()) {
            HBG_LOGE("VFX editor requires an initialized debug UI");
            shutdown();
            return 1;
        }
        const VfxEditorCompileOptions compile = editorCompileOptions(options);
        m_vfxEditor.setCompileOptions(compile);
        m_vfxEditor.setReloadCallback(
            [this](const std::filesystem::path& binaryPath, std::string& error) {
                return m_forwardRenderer.reloadVfxAsset(binaryPath, error);
            });
        m_vfxEditor.setPreviewCallback(
            [this](const std::filesystem::path& sourceAssetPath,
                   const std::filesystem::path& binaryPath, std::string& error) {
                return m_forwardRenderer.previewVfxAsset(sceneVfxPath(sourceAssetPath), binaryPath,
                                                         error);
            });
        m_vfxEditor.setPlaybackCallback(
            [this](const std::filesystem::path& assetPath, VfxEditorPlaybackAction action,
                   float value, std::string& error) {
                const std::filesystem::path sceneAssetPath = sceneVfxPath(assetPath);
                switch (action) {
                case VfxEditorPlaybackAction::TogglePlayPause:
                    return m_forwardRenderer.toggleVfxPaused(sceneAssetPath, error);
                case VfxEditorPlaybackAction::Step:
                    return m_forwardRenderer.stepVfx(sceneAssetPath, error);
                case VfxEditorPlaybackAction::Restart:
                    return m_forwardRenderer.restartVfx(sceneAssetPath, error);
                case VfxEditorPlaybackAction::SetPlayRate:
                    return m_forwardRenderer.setVfxPlayRate(sceneAssetPath, value, error);
                }
                error = "unknown VFX playback action";
                return false;
            });
        m_vfxEditor.setPlaybackStatusProvider([this](const std::filesystem::path& assetPath) {
            const render::ForwardRenderer::VfxPlaybackStatus playback =
                m_forwardRenderer.vfxPlaybackStatus(sceneVfxPath(assetPath));
            VfxEditorPlaybackStatus status;
            status.available = playback.available;
            status.paused = playback.paused;
            status.playRate = playback.playRate;
            status.randomSeed = playback.randomSeed;
            status.instances = playback.instances;
            status.preview = playback.preview;
            status.boundsValid = playback.bounds.valid();
            if (status.boundsValid) {
                status.boundsMin[0] = playback.bounds.min.x;
                status.boundsMin[1] = playback.bounds.min.y;
                status.boundsMin[2] = playback.bounds.min.z;
                status.boundsMax[0] = playback.bounds.max.x;
                status.boundsMax[1] = playback.bounds.max.y;
                status.boundsMax[2] = playback.bounds.max.z;
            }
            return status;
        });
        m_vfxEditor.setDiagnosticsProvider([this] {
            const render::ForwardRenderer::FrameDiagnostics diagnostics =
                m_forwardRenderer.frameDiagnostics();
            VfxEditorRuntimeDiagnostics result;
            result.active = diagnostics.vfxActive;
            result.effects = diagnostics.vfxEffects;
            result.systems = diagnostics.vfxSystems;
            result.particles = diagnostics.vfxParticles;
            result.draws = diagnostics.vfxDraws;
            result.dispatches = diagnostics.vfxDispatches;
            result.sortPasses = diagnostics.vfxSortPasses;
            result.events = diagnostics.vfxEvents;
            result.updateMs = diagnostics.vfxUpdateMs;
            result.simulationMs = diagnostics.vfxSimulationMs;
            result.outputMs = diagnostics.vfxOutputMs;
            result.totalMs = diagnostics.vfxMs;
            return result;
        });
        std::string editorError;
        if (!m_vfxEditor.open(options.vfxEditorPath, editorError)) {
            HBG_LOGE("VFX editor load failed: {}", editorError);
            shutdown();
            return 1;
        }
        m_debugUi.setPanelVisible(false);
        HBG_LOGI("VFX editor: '{}' (layout '{}', shaderc '{}')", options.vfxEditorPath,
                 m_vfxEditor.valid() ? "loaded" : "unavailable", compile.shaderc.string());
        if (options.vfxEditorCompile) {
            std::string compileError;
            const bool compiled = m_vfxEditor.compile(compileError);
            if (!compiled) {
                HBG_LOGE("VFX editor compile failed: {}", compileError);
            } else {
                HBG_LOGI("VFX editor headless compile succeeded");
            }
            shutdown();
            return compiled ? 0 : 1;
        }
        if (options.vfxEditorPreview) {
            std::string previewError;
            const bool previewed = m_vfxEditor.preview(previewError);
            if (!previewed) {
                HBG_LOGE("VFX editor preview failed: {}", previewError);
            } else {
                HBG_LOGI("VFX editor headless preview succeeded");
            }
            shutdown();
            return previewed ? 0 : 1;
        }
    }

    if (m_forwardRenderer.valid()) {
        m_forwardRenderer.setDebugLightmapMask(options.debugLightmapMask);
        HBG_LOGI("running forward renderer: close the window or press Esc to exit");
    } else {
        HBG_LOGI("running: close the window or press Esc to exit");
    }

    m_lastFrameTime = nowSeconds();
    uint32_t frameIndex = 0;
    bool clusterStatsRequested = false;
    bool clusterStatsReported = false;
    if (options.clusterStatsReadback && m_forwardRenderer.valid()) {
        m_forwardRenderer.requestClusterStatsReadback();
        clusterStatsRequested = true;
    }
    WindowInput input;
    while (m_window.pollEvents()) {
        if (m_sgEditor) {
            m_sgEditor->poll();
        }
        if (m_window.shouldClose()) {
            if (m_vfxEditor.valid() && m_vfxEditor.dirty() && m_debugUi.valid() &&
                !m_vfxEditor.closeApproved()) {
                m_window.setShouldClose(false);
                m_vfxEditor.requestClose();
            } else {
                break;
            }
        }

        if (m_window.takeResize()) {
            const int width = m_window.width();
            const int height = m_window.height();
            if (width > 0 && height > 0) {
                m_device.resize(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
                m_forwardRenderer.resize(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
            }
        }

        const int width = m_window.width();
        const int height = m_window.height();
        if (width <= 0 || height <= 0) {
            m_window.waitEvents(0.05);
            continue;
        }

        m_window.sampleInput(input);
        const double now = nowSeconds();
        const double deltaSeconds = std::max(now - m_lastFrameTime, 0.0);
        m_lastFrameTime = now;

        const bool finalFrame = options.framesSpecified && frameIndex + 1 >= static_cast<uint32_t>(options.frames);
        frame(input, deltaSeconds, finalFrame, options.screenshotPath);
        ++frameIndex;

        if (const std::optional<float> nextScale = m_dynamicResolution.update(deltaSeconds)) {
            m_dynamicResolutionScale = *nextScale;
            HBG_LOGI("dynamic resolution: scale={:.3f} avg_frame_ms={:.3f} target_ms={:.3f} adjustments={}",
                     m_dynamicResolutionScale, m_dynamicResolution.averageFrameMs(),
                     m_dynamicResolution.settings().targetFrameMs, m_dynamicResolution.adjustmentCount());
        }

        if (clusterStatsRequested && !clusterStatsReported) {
            const render::ForwardRenderer::FrameDiagnostics diagnostics = m_forwardRenderer.frameDiagnostics();
            if (diagnostics.clusterStatsValid) {
                HBG_LOGI("cluster stats readback: path={} clusters={} active={} overflow_clusters={} "
                         "overflow_lights={} max_overflow={} max_lights={} feature_flags_checked={} "
                         "feature_flag_mismatch={}",
                         diagnostics.clusterStatsGpu ? "gpu" : "cpu", diagnostics.clusterStatsClusters,
                         diagnostics.clusterStatsActiveClusters, diagnostics.clusterStatsOverflowClusters,
                         diagnostics.clusterStatsOverflowLights, diagnostics.clusterStatsMaxOverflow,
                         diagnostics.clusterStatsMaxLights, diagnostics.clusterStatsFeatureFlagsChecked,
                         diagnostics.clusterStatsFeatureFlagMismatch);
                clusterStatsReported = true;
            }
        }

        if (finalFrame || m_vfxEditor.closeApproved()) {
            break;
        }
    }

    if (m_vfxEditor.valid() && m_vfxEditor.dirty() && !m_vfxEditor.closeApproved()) {
        std::string recoveryError;
        if (m_vfxEditor.saveRecovery(recoveryError)) {
            HBG_LOGW("VFX editor closed with unsaved changes; recovery snapshot saved");
        } else {
            HBG_LOGW("VFX editor closed with unsaved changes; recovery failed: {}", recoveryError);
        }
    }
    shutdown();
    return 0;
}

bool Application::init(const AppOptions& options) {
    WindowDesc windowDesc;
    windowDesc.width = options.width;
    windowDesc.height = options.height;
    windowDesc.title = "hdrp-bgfx";

    if (!m_window.init(windowDesc)) {
        return false;
    }

    gfx::DeviceDesc deviceDesc;
    deviceDesc.backend = options.backend;
    deviceDesc.nativeWindow = m_window.nativeHandle();
    deviceDesc.width = static_cast<uint16_t>(m_window.width());
    deviceDesc.height = static_cast<uint16_t>(m_window.height());
    deviceDesc.vsync = options.vsync;
    deviceDesc.debug = options.bgfxDebug;

    return m_device.init(deviceDesc);
}

bool Application::loadSettings(const AppOptions& options) {
    m_pipelineSettings = render::PipelineSettings::defaults();
    m_frameSettings = render::FrameSettings::defaults(render::FrameSettingsRenderType::Camera);
    m_postProcessProfile = render::baselinePostProcessProfile();
    m_shadowSettings = render::ShadowSettings{};

    std::string error;
    if (!options.pipelineSettingsPath.empty() &&
        !render::loadPipelineSettings(options.pipelineSettingsPath, m_pipelineSettings, error)) {
        HBG_LOGE("pipeline settings load failed: {}", error);
        return false;
    }
    if (!options.shadowSettingsPath.empty() &&
        !render::loadShadowSettings(options.shadowSettingsPath, m_shadowSettings, error)) {
        HBG_LOGE("shadow settings load failed: {}", error);
        return false;
    }
    applyShadowOverrides(options.shadowOverrides, m_shadowSettings);
    render::sanitizeShadowSettings(m_shadowSettings);
    HBG_LOGI("shadow settings: distance={:.1f} cascades={} dir_filter={} punct_filter={} dir_res={} point_res={} "
             "spot_res={} atlas={} max_requests={} normal_bias={:.2f} slope_bias={:.2f} near_plane={:.2f} "
             "softness={:.2f} blocker={} filter={} min_filter={:.3f} cascade_fade={:.2f} micro={} micro_opacity={:.2f}",
             m_shadowSettings.maxDistance, m_shadowSettings.cascadeCount,
             render::shadowFilterName(m_shadowSettings.directionalFilter),
             render::shadowFilterName(m_shadowSettings.punctualFilter), m_shadowSettings.directionalResolution,
             m_shadowSettings.pointResolution, m_shadowSettings.spotResolution, m_shadowSettings.atlasSize,
             m_shadowSettings.maxRequests, m_shadowSettings.normalBias, m_shadowSettings.slopeBias,
             m_shadowSettings.shadowNearPlane,              m_shadowSettings.softness, m_shadowSettings.blockerSamples,
             m_shadowSettings.filterSamples, m_shadowSettings.minFilterSize, m_shadowSettings.cascadeFade,
             m_shadowSettings.microShadows, m_shadowSettings.microShadowOpacity);
    HBG_LOGI("directional IMS: light_angle={:.2f} deg max_depth_bias={:.4f} kernel={}",
             m_shadowSettings.directionalLightAngle, m_shadowSettings.directionalMaxDepthBias,
             m_shadowSettings.imsKernelSize);
    HBG_LOGI("per-light shadow defaults: dimmer={:.2f} fade_distance={:.1f} tint=({:.2f}, {:.2f}, {:.2f}) "
             "directional_cache={} punctual_cache={} area_cache={}",
             m_shadowSettings.shadowDimmer, m_shadowSettings.shadowFadeDistance,
             m_shadowSettings.shadowTint.x, m_shadowSettings.shadowTint.y, m_shadowSettings.shadowTint.z,
             m_shadowSettings.cachedDirectionalShadows ? "true" : "false",
             m_shadowSettings.cachedPunctualShadows ? "true" : "false",
             m_shadowSettings.cachedAreaShadows ? "true" : "false");
    HBG_LOGI("contact shadows: enabled={} length={:.3f} opacity={:.2f} samples={} half_res={} directional_only={} "
             "distance=[{:.2f}, {:.2f}] fade={:.2f}/{:.2f} bias={:.2f} thickness={:.3f}",
             m_shadowSettings.contactShadows.enabled ? "true" : "false", m_shadowSettings.contactShadows.length,
             m_shadowSettings.contactShadows.opacity, m_shadowSettings.contactShadows.sampleCount,
             m_shadowSettings.contactShadows.halfResolution ? "true" : "false",
             m_shadowSettings.contactShadows.directionalOnly ? "true" : "false",
             m_shadowSettings.contactShadows.minDistance, m_shadowSettings.contactShadows.maxDistance,
             m_shadowSettings.contactShadows.fadeInDistance, m_shadowSettings.contactShadows.fadeDistance,
             m_shadowSettings.contactShadows.rayBias, m_shadowSettings.contactShadows.thicknessScale);
    if (!options.frameSettingsPath.empty() &&
        !render::loadFrameSettings(options.frameSettingsPath, m_frameSettings, error)) {
        HBG_LOGE("frame settings load failed: {}", error);
        return false;
    }
    if (m_shadowSettings.contactShadows.enabled) {
        m_pipelineSettings.supportContactShadows = true;
        m_frameSettings.setEnabled(render::FrameSettingsField::ContactShadows, true);
    }
    if (!options.postProcessProfilePath.empty() &&
        !render::loadPostProcessProfile(options.postProcessProfilePath, m_postProcessProfile, error)) {
        HBG_LOGE("post-process profile load failed: {}", error);
        return false;
    }
    applyPostProcessOverrides(options.postProcessOverrides, m_postProcessProfile.settings);
    render::sanitizePostProcessSettings(m_postProcessProfile.settings);
    HBG_LOGI("post-process profile: '{}' exposure={} tonemap={} advanced={}", m_postProcessProfile.name,
             render::exposureModeName(m_postProcessProfile.settings.exposureMode),
             render::tonemappingModeName(m_postProcessProfile.settings.tonemapping),
             m_postProcessProfile.settings.hasAdvancedEffects() ? "requested" : "off");

    if (!options.volumetricFogProfilePath.empty() &&
        !render::loadVolumetricFogSettings(options.volumetricFogProfilePath, m_volumetricFogSettings, error)) {
        HBG_LOGE("volumetric fog profile load failed: {}", error);
        return false;
    }
    applyVolumetricFogOverrides(options.volumetricFogOverrides, m_volumetricFogSettings);
    render::sanitizeVolumetricFogSettings(m_volumetricFogSettings);
    if (m_volumetricFogSettings.enabled) {
        m_pipelineSettings.supportVolumetrics = true;
        m_frameSettings.setEnabled(render::FrameSettingsField::AtmosphericScattering, true);
        m_frameSettings.setEnabled(render::FrameSettingsField::Volumetrics, true);
    } else if (options.volumetricFogOverrides.enabled && !*options.volumetricFogOverrides.enabled) {
        m_frameSettings.setEnabled(render::FrameSettingsField::Volumetrics, false);
    }
    HBG_LOGI("volumetric fog: enabled={} mean_free_path={:.2f} base_height={:.2f} maximum_height={:.2f} "
             "anisotropy={:.2f} depth_extent={:.2f} steps={} shadows={} scale={:.2f} temporal={} "
             "punctual_lights={} punctual_shadows={} max_punctual_lights={} local_volumes={}",
             m_volumetricFogSettings.enabled ? "true" : "false", m_volumetricFogSettings.meanFreePath,
             m_volumetricFogSettings.baseHeight, m_volumetricFogSettings.maximumHeight,
             m_volumetricFogSettings.anisotropy, m_volumetricFogSettings.depthExtent,
             m_volumetricFogSettings.stepCount, m_volumetricFogSettings.directionalShadows ? "true" : "false",
             m_volumetricFogSettings.resolutionScale, m_volumetricFogSettings.temporal ? "true" : "false",
             m_volumetricFogSettings.punctualLights ? "true" : "false",
             m_volumetricFogSettings.punctualShadows ? "true" : "false",
             m_volumetricFogSettings.maxPunctualLights,
             m_volumetricFogSettings.localVolumes ? "true" : "false");

    if (options.litShaderMode) {
        m_frameSettings.setLitShaderMode(*options.litShaderMode);
        if (*options.litShaderMode == render::LitShaderMode::Deferred &&
            m_pipelineSettings.supportedLitShaderMode == render::SupportedLitShaderMode::ForwardOnly) {
            m_pipelineSettings.supportedLitShaderMode = render::SupportedLitShaderMode::DeferredOnly;
        } else if (*options.litShaderMode == render::LitShaderMode::Forward &&
                   m_pipelineSettings.supportedLitShaderMode == render::SupportedLitShaderMode::DeferredOnly) {
            m_pipelineSettings.supportedLitShaderMode = render::SupportedLitShaderMode::ForwardOnly;
        }
    }
    if (options.litVariants) {
        m_pipelineSettings.supportLitVariants = *options.litVariants;
    }
    if (options.litVariantTiles) {
        m_pipelineSettings.supportLitVariantTiles = *options.litVariantTiles;
        if (*options.litVariantTiles) {
            m_pipelineSettings.supportLitVariants = true;
        }
    }
    if (options.ssao) {
        m_pipelineSettings.supportSSAO = *options.ssao;
        m_frameSettings.setEnabled(render::FrameSettingsField::SSAO, *options.ssao);
    }
    if (options.ssaoTemporal) {
        m_ssaoSettings.temporalAccumulation = *options.ssaoTemporal;
    }
    if (options.ssr) {
        m_pipelineSettings.supportSSR = *options.ssr;
        m_frameSettings.setEnabled(render::FrameSettingsField::SSR, *options.ssr);
    }
    if (options.ssrTemporal) {
        m_ssrSettings.temporalAccumulation = *options.ssrTemporal;
    }
    if (options.ssgi) {
        m_pipelineSettings.supportSSGI = *options.ssgi;
        m_frameSettings.setEnabled(render::FrameSettingsField::SSGI, *options.ssgi);
    }
    if (options.ssgiTemporal) {
        m_ssgiSettings.temporalAccumulation = *options.ssgiTemporal;
    }
    if (options.decals) {
        m_pipelineSettings.supportDecals = *options.decals;
        m_frameSettings.setEnabled(render::FrameSettingsField::Decals, *options.decals);
    }
    if (options.reflectionProbes) {
        m_frameSettings.setEnabled(render::FrameSettingsField::ReflectionProbe, *options.reflectionProbes);
    }
    if (options.vfx) {
        m_pipelineSettings.supportVFX = *options.vfx;
    } else if (!options.vfxEditorPath.empty()) {
        m_pipelineSettings.supportVFX = true;
    }
    if (options.vfxInstancing) {
        m_pipelineSettings.supportVFXInstancing = true;
    }
    if (options.vfxTransparentSort) {
        m_pipelineSettings.supportVFXTransparentSort = true;
    }
    if (options.vfxInstanceBatchSize) {
        m_pipelineSettings.vfxInstanceBatchSize = *options.vfxInstanceBatchSize;
    }
    if (options.gpuLightList) {
        m_pipelineSettings.supportGpuLightList = *options.gpuLightList;
    }
    if (options.gpuLightListBuffers) {
        m_pipelineSettings.supportGpuLightListBuffers = *options.gpuLightListBuffers;
    }
    if (options.gpuLightListIndirect) {
        m_pipelineSettings.supportGpuLightListIndirect = *options.gpuLightListIndirect;
    }
    if (options.gpuSkinning) {
        m_pipelineSettings.supportGpuSkinning = *options.gpuSkinning;
    }
    if (options.depthPyramidCompute) {
        m_pipelineSettings.supportDepthPyramidCompute = *options.depthPyramidCompute;
    }
    if (options.gpuDrivenGeometry) {
        m_pipelineSettings.supportGpuDrivenGeometry = *options.gpuDrivenGeometry;
    }
    if (options.transparentBackface) {
        m_pipelineSettings.supportTransparentBackface = *options.transparentBackface;
    }
    if (options.transparentPrepass) {
        m_pipelineSettings.supportTransparentDepthPrepass = *options.transparentPrepass;
        m_frameSettings.setEnabled(render::FrameSettingsField::TransparentPrepass, *options.transparentPrepass);
    }
    if (options.transparentRefraction) {
        m_pipelineSettings.supportTransparentRefraction = *options.transparentRefraction;
        m_frameSettings.setEnabled(render::FrameSettingsField::Refraction, *options.transparentRefraction);
    }
    if (options.refractionDepthRejection) {
        m_pipelineSettings.supportRefractionDepthRejection = *options.refractionDepthRejection;
    }
    if (options.refractionRoughness) {
        m_pipelineSettings.supportRefractionRoughness = *options.refractionRoughness;
    }
    if (options.lightListFeatureFlags) {
        m_pipelineSettings.supportLightListFeatureFlags = *options.lightListFeatureFlags;
    }
    if (options.cookieAtlas) {
        m_pipelineSettings.supportCookieAtlas = *options.cookieAtlas;
    }
    if (options.cookieAtlasSize) {
        m_pipelineSettings.cookieAtlasSize = *options.cookieAtlasSize;
    }
    if (options.lightListCapacity) {
        m_pipelineSettings.lightListCapacity = *options.lightListCapacity;
    }
    if (options.lightLayers) {
        m_pipelineSettings.supportLightLayers = *options.lightLayers;
    }
    if (options.gpuShadowCulling) {
        m_pipelineSettings.supportGpuShadowCulling = *options.gpuShadowCulling;
    }
    if (options.gpuShadowIndirect) {
        m_pipelineSettings.supportGpuShadowIndirect = *options.gpuShadowIndirect;
    }
    if (options.asyncGpuLightList) {
        m_pipelineSettings.supportAsyncGpuLightList = *options.asyncGpuLightList;
    }

    m_frameSettings.sanitize(m_pipelineSettings);
    HBG_LOGI("settings: lit={} shadows={} gpuLightList={} lightCapacity={} featureFlags={} opaque={} transparent={} postprocess={} "
             "motionVectors={} ssao={} ssr={} ssgi={} decals={} reflectionProbes={}",
             m_frameSettings.litShaderMode() == render::LitShaderMode::Deferred ? "deferred" : "forward",
             m_frameSettings.isEnabled(render::FrameSettingsField::ShadowMaps),
             m_pipelineSettings.supportGpuLightList ? "true" : "false", m_pipelineSettings.lightListCapacity,
             m_pipelineSettings.supportLightListFeatureFlags ? "true" : "false",
             m_frameSettings.isEnabled(render::FrameSettingsField::OpaqueObjects),
             m_frameSettings.isEnabled(render::FrameSettingsField::TransparentObjects),
             m_frameSettings.isEnabled(render::FrameSettingsField::Postprocess),
             m_frameSettings.isEnabled(render::FrameSettingsField::MotionVectors),
             m_frameSettings.isEnabled(render::FrameSettingsField::SSAO),
             m_frameSettings.isEnabled(render::FrameSettingsField::SSR),
             m_frameSettings.isEnabled(render::FrameSettingsField::SSGI),
             m_frameSettings.isEnabled(render::FrameSettingsField::Decals),
             m_frameSettings.isEnabled(render::FrameSettingsField::ReflectionProbe));
    return true;
}

bool Application::loadScene(const std::string& path) {
    asset::SceneLoadResult loaded = asset::SceneLoader::load(path);
    if (!loaded.ok()) {
        HBG_LOGE("scene load failed: {}", loaded.error);
        return false;
    }
    m_scene = std::move(loaded.scene);

    if (!m_forwardRenderer.init(m_device.resources(), *m_scene, static_cast<uint16_t>(m_window.width()),
                                static_cast<uint16_t>(m_window.height()))) {
        HBG_LOGE("forward renderer initialization failed");
        m_forwardRenderer.shutdown(m_device.resources());
        m_scene.reset();
        return false;
    }
    m_forwardRenderer.setSettings(m_pipelineSettings, m_frameSettings);
    m_forwardRenderer.setSsaoSettings(m_ssaoSettings);
    m_forwardRenderer.setSsrSettings(m_ssrSettings);
    m_forwardRenderer.setSsgiSettings(m_ssgiSettings);
    m_forwardRenderer.setShadowSettings(m_shadowSettings);
    return true;
}

void Application::shutdown() {
    if (m_sgEditor) {
        m_sgEditor->stop();
        m_sgEditor.reset();
    }
    if (m_sgPreview) {
        if (m_device.isValid()) {
            m_sgPreview->shutdown(m_device.resources());
        }
        m_sgPreview.reset();
    }
    if (m_device.isValid()) {
        if (m_debugUi.valid()) {
            m_debugUi.shutdown(m_device.resources());
        }
        m_forwardRenderer.shutdown(m_device.resources());
    }
    m_scene.reset();
    m_device.shutdown();
    m_window.shutdown();
}

void Application::frame(const WindowInput& input, double deltaSeconds, bool finalFrame,
                        const std::string& screenshotPath) {
    const uint16_t width = static_cast<uint16_t>(m_window.width());
    const uint16_t height = static_cast<uint16_t>(m_window.height());

    if (m_debugUi.valid()) {
        const render::ForwardRenderer::FrameDiagnostics diagnostics = m_forwardRenderer.frameDiagnostics();
        m_debugUi.setGpuLightListStatus(
            diagnostics.gpuLightListRequested, diagnostics.gpuLightListActive,
            diagnostics.gpuLightListBufferPath, diagnostics.gpuLightListIndirect, diagnostics.gpuLightListPasses,
            diagnostics.lightListCpuMs, diagnostics.gpuShadowCulling, diagnostics.gpuShadowIndirect,
            diagnostics.asyncGpuLightListRequested, diagnostics.asyncGpuLightListActive,
            diagnostics.lightListCapacityRequested, diagnostics.lightListCapacityEffective,
            diagnostics.lightListStride, diagnostics.lightListTruncated, diagnostics.lightListCapacityClamped);
        m_debugUi.setGpuSkinningStatus(diagnostics.gpuSkinningRequested, diagnostics.gpuSkinningActive,
                                      diagnostics.gpuSkinningMeshes, diagnostics.gpuSkinningVertices,
                                      diagnostics.gpuSkinningDispatches);
        m_debugUi.setDepthPyramidStatus(diagnostics.depthPyramidComputeRequested,
                                        diagnostics.depthPyramidComputeActive, diagnostics.depthPyramidMips);
        m_debugUi.setGpuDrivenStatus(
            diagnostics.gpuDrivenRequested, diagnostics.gpuDrivenActive, diagnostics.gpuDrivenHiZActive,
            diagnostics.gpuDrivenForwardActive, diagnostics.gpuDrivenGbufferActive,
            diagnostics.gpuDrivenTransparentActive, diagnostics.gpuDrivenTransparentSorted,
            diagnostics.gpuDrivenPrepassActive,
            diagnostics.gpuDrivenBindlessActive, diagnostics.gpuDrivenItems,
            diagnostics.gpuDrivenPrepassItems, diagnostics.gpuDrivenBatches,
            diagnostics.gpuDrivenCullingPasses, diagnostics.gpuDrivenIndirectCountSupported);
        m_debugUi.setClusterStatsStatus(
            diagnostics.clusterStatsReadbackPending, diagnostics.clusterStatsValid, diagnostics.clusterStatsGpu,
            diagnostics.clusterStatsClusters, diagnostics.clusterStatsActiveClusters,
            diagnostics.clusterStatsOverflowClusters, diagnostics.clusterStatsOverflowLights,
            diagnostics.clusterStatsMaxOverflow, diagnostics.clusterStatsMaxLights);
        m_debugUi.beginFrame(input, width, height, deltaSeconds);
        if (m_vfxEditor.valid()) {
            m_vfxEditor.draw();
        }
        if (m_debugUi.consumeClusterStatsReadbackRequest() && m_forwardRenderer.valid()) {
            m_forwardRenderer.requestClusterStatsReadback();
        }
        const render::DebugDisplaySettings& settings = m_debugUi.settings();
        if (settings.gpuLightList != m_pipelineSettings.supportGpuLightList && m_forwardRenderer.valid()) {
            m_pipelineSettings.supportGpuLightList = settings.gpuLightList;
            m_forwardRenderer.setGpuLightListEnabled(settings.gpuLightList);
        }
        if (settings.gpuDrivenGeometry != m_pipelineSettings.supportGpuDrivenGeometry &&
            m_forwardRenderer.valid()) {
            m_pipelineSettings.supportGpuDrivenGeometry = settings.gpuDrivenGeometry;
            m_forwardRenderer.setGpuDrivenGeometryEnabled(settings.gpuDrivenGeometry);
        }
        m_forwardRenderer.setShadowsEnabled(settings.shadows);
        m_forwardRenderer.setWireframe(settings.wireframe);
        m_forwardRenderer.setRenderScale(settings.renderScale);
        m_forwardRenderer.setDebugDisplayMode(settings.fullscreen);
        m_forwardRenderer.setDebugLightClusterSlice(settings.lightClusterSlice);
        m_forwardRenderer.setPostProcessSettings(settings.postProcess);
    }
    if (m_dynamicResolution.settings().enabled && m_forwardRenderer.valid()) {
        m_forwardRenderer.setRenderScale(m_dynamicResolutionScale);
    }

    if (m_scene != nullptr && m_forwardRenderer.valid()) {
        const float animationTime = m_animationTime.value_or(m_time);
        asset::applyAnimation(*m_scene, animationTime);
        m_forwardRenderer.render(*m_scene, width, height, animationTime);
        if (!m_debugUi.valid() || !m_debugUi.settings().freezeTime) {
            m_time += static_cast<float>(deltaSeconds);
        }
    } else {
        bgfx::setViewRect(0, 0, 0, width, height);
        bgfx::setViewClear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x1a1a1aff, 1.0f, 0);
        bgfx::touch(0);
    }

    if (m_debugUi.valid()) {
        m_debugUi.endFrame(width, height);
    }

    if (finalFrame && !screenshotPath.empty()) {
        bgfx::requestScreenShot(BGFX_INVALID_HANDLE, screenshotPath.c_str());
        HBG_LOGI("screenshot requested: '{}'", screenshotPath);
    }

    bgfx::frame();
    m_device.resources().endFrame();
}

} // namespace hbg
