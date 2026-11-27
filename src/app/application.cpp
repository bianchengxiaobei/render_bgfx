#include "app/application.h"

#include "app/asset_test.h"
#include "app/gfx_test.h"
#include "app/render_test.h"
#include "asset/scene_loader.h"
#include "core/log.h"
#include "render/water/water_screen_grid.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>

namespace hbg {
namespace {

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

int Application::run(const AppOptions& options) {
    log::setLevel(options.verbose ? log::Level::Debug : log::Level::Info);

    if (!loadSettings(options)) {
        return 1;
    }

    if (!init(options)) {
        shutdown();
        return 1;
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

    if (!options.renderTestPath.empty()) {
        const bool ok = runRenderTest(
            m_device.resources(), options.renderTestPath, static_cast<uint16_t>(m_window.width()),
            static_cast<uint16_t>(m_window.height()), static_cast<uint32_t>(options.frames), options.screenshotPath,
            options.debugDisplayMode, options.debugLightClusterSlice, !options.disableLightCluster, options.cameraOrbit,
            options.cameraLookSun, !options.disableShadows, options.clusterStatsReadback, m_pipelineSettings,
            m_frameSettings, m_shadowSettings, m_ssaoSettings, m_ssrSettings, m_ssgiSettings, m_postProcessProfile,
            m_volumetricFogSettings, m_atmosphereSettings, options.renderScale, options.skyPath, options.skyIntensity,
            options.skyRotation);
        shutdown();
        return ok ? 0 : 1;
    }

    if (!options.scenePath.empty() && !loadScene(options.scenePath, options)) {
        shutdown();
        return 1;
    }
    if (options.riverEditDemo && m_scene != nullptr && m_riverEditor.valid()) {
        if (m_riverEditor.applyDemoEdit()) {
            m_scene->river.graph = m_riverEditor.graph();
            if (!m_forwardRenderer.rebuildRiver(*m_scene)) {
                HBG_LOGW("river editor demo rebuild failed");
            }
            m_riverEditor.consumeDirty();
        }
    }
    if (options.riverEditSave && m_riverEditor.valid()) {
        std::string error;
        if (m_riverEditor.save(m_riverEditPath, &error)) {
            HBG_LOGI("saved river edits: {}", m_riverEditPath);
        } else {
            HBG_LOGW("river edit save failed: {}", error);
        }
    }

    if (m_forwardRenderer.valid()) {
        if (!options.skyPath.empty() &&
            !m_forwardRenderer.setSky(options.skyPath, options.skyIntensity, options.skyRotation)) {
            HBG_LOGW("sky load failed; continuing without an environment");
        }
        m_forwardRenderer.setRenderScale(options.renderScale);
        m_forwardRenderer.setLightClusterEnabled(!options.disableLightCluster);
        m_forwardRenderer.setPostProcessProfile(m_postProcessProfile);
        m_forwardRenderer.setVolumetricFogSettings(m_volumetricFogSettings);
        m_forwardRenderer.setAtmosphereSettings(m_atmosphereSettings);
    }

    if (m_forwardRenderer.valid() && options.debugUi) {
        if (!m_debugUi.init(m_device.resources(), m_forwardRenderer.shaderLibrary())) {
            HBG_LOGW("debug ui initialization failed; continuing without the panel");
        }
    }
    if (m_debugUi.valid()) {
        m_debugUi.setRiverEditPath(m_riverEditPath);
        render::DebugDisplaySettings& settings = m_debugUi.settings();
        settings.fullscreen = options.debugDisplayMode;
        settings.lightClusterSlice = options.debugLightClusterSlice;
        settings.wireframe = options.debugWireframe;
        settings.shadows = !options.disableShadows;
        settings.gpuLightList = m_pipelineSettings.supportGpuLightList;
        settings.renderScale = options.renderScale;
        settings.postProcess = m_postProcessProfile.settings;
    } else {
        m_forwardRenderer.setDebugDisplayMode(options.debugDisplayMode);
        m_forwardRenderer.setDebugLightClusterSlice(options.debugLightClusterSlice);
        m_forwardRenderer.setWireframe(options.debugWireframe);
        m_forwardRenderer.setShadowsEnabled(!options.disableShadows);
    }

    if (m_forwardRenderer.valid()) {
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
        if (m_window.takeResize()) {
            const int width = m_window.width();
            const int height = m_window.height();
            if (width > 0 && height > 0) {
                m_device.resize(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
                m_forwardRenderer.resize(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
                bgfx::frame();
                if (m_scene != nullptr) {
                    m_forwardRenderer.rebuildWaterScreenGrid(*m_scene, static_cast<uint16_t>(width),
                                                             static_cast<uint16_t>(height));
                }
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

        if (finalFrame) {
            break;
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
             m_shadowSettings.shadowNearPlane, m_shadowSettings.softness, m_shadowSettings.blockerSamples,
             m_shadowSettings.filterSamples, m_shadowSettings.minFilterSize, m_shadowSettings.cascadeFade,
             m_shadowSettings.microShadows, m_shadowSettings.microShadowOpacity);
    HBG_LOGI("per-light shadow defaults: dimmer={:.2f} fade_distance={:.1f} tint=({:.2f}, {:.2f}, {:.2f}) "
             "directional_cache={} punctual_cache={} area_cache={}",
             m_shadowSettings.shadowDimmer, m_shadowSettings.shadowFadeDistance, m_shadowSettings.shadowTint.x,
             m_shadowSettings.shadowTint.y, m_shadowSettings.shadowTint.z,
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
             m_volumetricFogSettings.anisotropy, m_volumetricFogSettings.depthExtent, m_volumetricFogSettings.stepCount,
             m_volumetricFogSettings.directionalShadows ? "true" : "false", m_volumetricFogSettings.resolutionScale,
             m_volumetricFogSettings.temporal ? "true" : "false",
             m_volumetricFogSettings.punctualLights ? "true" : "false",
             m_volumetricFogSettings.punctualShadows ? "true" : "false", m_volumetricFogSettings.maxPunctualLights,
             m_volumetricFogSettings.localVolumes ? "true" : "false");

    if (!options.atmosphereProfilePath.empty() &&
        !render::loadAtmosphereSettings(options.atmosphereProfilePath, m_atmosphereSettings, error)) {
        HBG_LOGE("atmosphere profile load failed: {}", error);
        return false;
    }
    if (options.atmosphereEnabled) {
        m_atmosphereSettings.enabled = *options.atmosphereEnabled;
    }
    render::sanitizeAtmosphereSettings(m_atmosphereSettings);
    HBG_LOGI("atmosphere: enabled={} precomputed={} sky_steps={} aerial_steps={} sun_intensity={:.1f} "
             "planet_radius={:.0f} atmosphere_radius={:.0f}",
             m_atmosphereSettings.enabled ? "true" : "false",
             m_atmosphereSettings.usePrecomputedScattering ? "true" : "false", m_atmosphereSettings.skyStepCount,
             m_atmosphereSettings.aerialStepCount, m_atmosphereSettings.sunIntensity, m_atmosphereSettings.planetRadius,
             m_atmosphereSettings.atmosphereRadius);

    if (options.litShaderMode) {
        m_frameSettings.setLitShaderMode(*options.litShaderMode);
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
    if (options.gpuLightList) {
        m_pipelineSettings.supportGpuLightList = *options.gpuLightList;
    }
    if (options.gpuLightListBuffers) {
        m_pipelineSettings.supportGpuLightListBuffers = *options.gpuLightListBuffers;
    }
    if (options.gpuLightListIndirect) {
        m_pipelineSettings.supportGpuLightListIndirect = *options.gpuLightListIndirect;
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
    HBG_LOGI("settings: lit={} shadows={} gpuLightList={} lightCapacity={} featureFlags={} opaque={} transparent={} "
             "postprocess={} "
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

bool Application::loadScene(const std::string& path, const AppOptions& options) {
    asset::SceneLoadResult loaded = asset::SceneLoader::load(path);
    if (!loaded.ok()) {
        HBG_LOGE("scene load failed: {}", loaded.error);
        return false;
    }
    m_scene = std::move(loaded.scene);
    m_scene->river.editable = true;

    const std::filesystem::path scenePath(path);
    const std::filesystem::path defaultEditPath = scenePath.parent_path() / (scenePath.stem().string() + ".edits.json");
    m_riverEditPath = options.riverEditPath.empty() ? defaultEditPath.string() : options.riverEditPath;

    const uint16_t width = static_cast<uint16_t>(m_window.width());
    const uint16_t height = static_cast<uint16_t>(m_window.height());
    if (m_scene->waterFftEnabled && m_scene->waterMeshIndex < m_scene->meshes.size()) {
        render::WaterScreenGridDesc desc;
        if (!render::configureWaterScreenGrid(m_scene->meshes[m_scene->waterMeshIndex], width, height, &desc)) {
            HBG_LOGW("water screen grid configuration failed; keeping authored water mesh");
        }
    }

    if (!m_forwardRenderer.init(m_device.resources(), *m_scene, width, height)) {
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
    m_forwardRenderer.setAtmosphereSettings(m_atmosphereSettings);

    m_riverEditor = render::RiverEditor{};
    if (m_scene->river.enabled) {
        if (!m_riverEditor.init(m_scene->river.graph, m_scene->river.mesh, m_scene->river.mesh.baseHeight)) {
            HBG_LOGW("river editor initialization failed");
        } else {
            m_riverEditor.setActive(true);
            std::error_code editPathError;
            const bool editFileExists =
                !m_riverEditPath.empty() && std::filesystem::exists(m_riverEditPath, editPathError);
            if (!options.riverEditDemo && !editPathError && editFileExists) {
                std::string error;
                if (m_riverEditor.load(m_riverEditPath, &error)) {
                    m_scene->river.graph = m_riverEditor.graph();
                    if (!m_forwardRenderer.rebuildRiver(*m_scene)) {
                        HBG_LOGW("river edit load rebuild failed");
                    }
                    m_riverEditor.consumeDirty();
                    HBG_LOGI("loaded river edits: {}", m_riverEditPath);
                } else {
                    HBG_LOGW("river edit load failed: {}", error);
                }
            }
        }
    }

    m_editorCamera = render::EditorCamera{};
    if (!m_scene->cameras.empty() && !m_editorCamera.init(m_scene->cameras.front(), m_scene->bounds)) {
        HBG_LOGW("river editor camera initialization failed");
    }
    m_prevSavePressed = false;
    return true;
}

void Application::updateEditorCamera(const WindowInput& input) {
    if (m_scene == nullptr || m_scene->cameras.empty() || !m_editorCamera.valid()) {
        return;
    }
    render::EditorCameraInput cameraInput;
    cameraInput.mouseX = input.mouseX;
    cameraInput.mouseY = input.mouseY;
    cameraInput.scrollY = input.scrollY;
    cameraInput.rightDown = input.mouseDown[1];
    cameraInput.middleDown = input.mouseDown[2];
    cameraInput.mouseCaptured = m_debugUi.valid() && m_debugUi.wantsMouse();
    m_editorCamera.update(cameraInput, m_scene->cameras.front());
}

void Application::shutdown() {
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
            diagnostics.gpuLightListRequested, diagnostics.gpuLightListActive, diagnostics.gpuLightListBufferPath,
            diagnostics.gpuLightListIndirect, diagnostics.gpuLightListPasses, diagnostics.lightListCpuMs,
            diagnostics.gpuShadowCulling, diagnostics.gpuShadowIndirect, diagnostics.asyncGpuLightListRequested,
            diagnostics.asyncGpuLightListActive, diagnostics.lightListCapacityRequested,
            diagnostics.lightListCapacityEffective, diagnostics.lightListStride, diagnostics.lightListTruncated,
            diagnostics.lightListCapacityClamped);
        m_debugUi.setClusterStatsStatus(
            diagnostics.clusterStatsReadbackPending, diagnostics.clusterStatsValid, diagnostics.clusterStatsGpu,
            diagnostics.clusterStatsClusters, diagnostics.clusterStatsActiveClusters,
            diagnostics.clusterStatsOverflowClusters, diagnostics.clusterStatsOverflowLights,
            diagnostics.clusterStatsMaxOverflow, diagnostics.clusterStatsMaxLights);
        m_debugUi.beginFrame(input, width, height, deltaSeconds);
        if (m_debugUi.consumeClusterStatsReadbackRequest() && m_forwardRenderer.valid()) {
            m_forwardRenderer.requestClusterStatsReadback();
        }
        const render::DebugDisplaySettings& settings = m_debugUi.settings();
        if (settings.gpuLightList != m_pipelineSettings.supportGpuLightList && m_forwardRenderer.valid()) {
            m_pipelineSettings.supportGpuLightList = settings.gpuLightList;
            m_forwardRenderer.setGpuLightListEnabled(settings.gpuLightList);
        }
        m_forwardRenderer.setShadowsEnabled(settings.shadows);
        m_forwardRenderer.setWireframe(settings.wireframe);
        m_forwardRenderer.setRenderScale(settings.renderScale);
        m_forwardRenderer.setDebugDisplayMode(settings.fullscreen);
        m_forwardRenderer.setDebugLightClusterSlice(settings.lightClusterSlice);
        m_forwardRenderer.setPostProcessSettings(settings.postProcess);

        if (m_scene != nullptr && m_forwardRenderer.valid() && m_riverEditor.valid()) {
            const render::ViewConstants& constants = m_forwardRenderer.viewState().constants();
            m_debugUi.drawRiverEditor(m_riverEditor, input, constants.nonJitteredViewProj,
                                      constants.nonJitteredInvViewProj, width, height);
            if (m_riverEditor.consumeDirty()) {
                m_scene->river.graph = m_riverEditor.graph();
                if (!m_forwardRenderer.rebuildRiver(*m_scene)) {
                    HBG_LOGW("river editor rebuild failed");
                }
            }
        }
    }

    bool saveRequested = m_debugUi.consumeRiverSaveRequest();
    const bool reloadRequested = m_debugUi.consumeRiverReloadRequest();
    const bool savePressed = input.keyS && (input.keyAlt || input.keyCtrl);
    if (savePressed && !m_prevSavePressed) {
        saveRequested = true;
    }
    m_prevSavePressed = savePressed;
    if (m_scene != nullptr && m_forwardRenderer.valid() && m_riverEditor.valid()) {
        if (reloadRequested) {
            std::string error;
            if (m_riverEditor.load(m_riverEditPath, &error)) {
                m_scene->river.graph = m_riverEditor.graph();
                if (!m_forwardRenderer.rebuildRiver(*m_scene)) {
                    HBG_LOGW("river edit reload rebuild failed");
                }
                m_riverEditor.consumeDirty();
                HBG_LOGI("reloaded river edits: {}", m_riverEditPath);
            } else {
                HBG_LOGW("river edit reload failed: {}", error);
            }
        }
        if (saveRequested) {
            std::string error;
            if (m_riverEditor.save(m_riverEditPath, &error)) {
                HBG_LOGI("saved river edits: {}", m_riverEditPath);
            } else {
                HBG_LOGW("river edit save failed: {}", error);
            }
        }
    }

    updateEditorCamera(input);

    if (m_scene != nullptr && m_forwardRenderer.valid()) {
        m_forwardRenderer.render(*m_scene, width, height, m_time);
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
