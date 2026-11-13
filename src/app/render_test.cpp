#include "app/render_test.h"

#include "asset/scene_loader.h"
#include "core/log.h"
#include "core/math.h"
#include "render/passes/forward_renderer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

namespace hbg {
namespace {

void updateOrbitCamera(render::RenderScene& scene, uint32_t frame, uint16_t width, uint16_t height) {
    if (scene.cameras.empty()) {
        scene.cameras.emplace_back();
    }

    const Bounds& bounds = scene.bounds;
    const Vec3 center = bounds.valid() ? (bounds.min + bounds.max) * 0.5f : Vec3(0.0f, 0.0f, 0.0f);
    const float radius = bounds.valid() ? std::max(length(bounds.max - bounds.min) * 0.5f, 0.1f) : 1.0f;
    const float fov = render::Camera{}.yfov;
    const float distance = radius / std::tan(fov * 0.5f) * 1.15f;
    const float angle = static_cast<float>(frame) * 0.02f;
    const Vec3 position = center + Vec3(std::sin(angle) * distance, 0.0f, std::cos(angle) * distance);

    render::Camera& camera = scene.cameras.front();
    camera.projection = render::Camera::Projection::Perspective;
    camera.world = inverse(lookAtRH(position, center, Vec3(0.0f, 1.0f, 0.0f)));
    camera.yfov = fov;
    camera.aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.7777778f;
    camera.nearClip = std::max(radius * 0.01f, 0.01f);
    camera.farClip = radius * 20.0f + distance;
}

} // namespace

bool runRenderTest(gfx::ResourceManager& resources, const std::string& path, uint16_t width, uint16_t height,
                   uint32_t frames, const std::string& screenshotPath, render::DebugDisplayMode debugMode,
                   int lightClusterSlice, bool lightClusterEnabled, bool cameraOrbit, bool shadowsEnabled,
                   const render::PipelineSettings& pipelineSettings,
                   const render::FrameSettings& frameSettings, const render::ShadowSettings& shadowSettings,
                   const render::PostProcessProfile& postProcessProfile,
                   const render::VolumetricFogSettings& volumetricFogSettings, float renderScale,
                   const std::string& skyPath, float skyIntensity, float skyRotation) {
    HBG_LOGI("running render test: '{}'", path);
    asset::SceneLoadResult loaded = asset::SceneLoader::load(path);
    if (!loaded.ok()) {
        HBG_LOGE("scene load failed: {}", loaded.error);
        return false;
    }
    if (cameraOrbit) {
        updateOrbitCamera(*loaded.scene, 0, width, height);
    }

    render::ForwardRenderer renderer;
    if (!renderer.init(resources, *loaded.scene, width, height)) {
        renderer.shutdown(resources);
        return false;
    }
    render::PipelineSettings pipeline = pipelineSettings;
    render::FrameSettings frameOverride = frameSettings;
    if (shadowSettings.contactShadows.enabled) {
        pipeline.supportContactShadows = true;
        frameOverride.setEnabled(render::FrameSettingsField::ContactShadows, true);
    }
    renderer.setSettings(pipeline, frameOverride);
    renderer.setShadowSettings(shadowSettings);
    renderer.setPostProcessProfile(postProcessProfile);
    renderer.setVolumetricFogSettings(volumetricFogSettings);
    renderer.setRenderScale(renderScale);
    renderer.setDebugDisplayMode(debugMode);
    renderer.setDebugLightClusterSlice(lightClusterSlice);
    renderer.setLightClusterEnabled(lightClusterEnabled);
    renderer.setShadowsEnabled(shadowsEnabled);
    if (!skyPath.empty() && !renderer.setSky(skyPath, skyIntensity, skyRotation)) {
        renderer.shutdown(resources);
        return false;
    }

    const uint32_t count = frames > 0 ? frames : 1;
    const auto renderStart = std::chrono::steady_clock::now();
    for (uint32_t frame = 0; frame < count; ++frame) {
        if (cameraOrbit) {
            updateOrbitCamera(*loaded.scene, frame, width, height);
        }
        renderer.render(*loaded.scene, width, height, static_cast<float>(frame) / 60.0f);
        if (frame + 1 == count && !screenshotPath.empty()) {
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, screenshotPath.c_str());
        }
        bgfx::frame();
        resources.endFrame();
    }

    const auto renderEnd = std::chrono::steady_clock::now();
    const double averageFrameMs =
        std::chrono::duration<double, std::milli>(renderEnd - renderStart).count() / static_cast<double>(count);
    const bgfx::Stats* stats = bgfx::getStats();
    if (stats != nullptr) {
        const double cpuSubmitMs = stats->cpuTimerFreq > 0
                                       ? static_cast<double>(stats->cpuTimeFrame) * 1000.0 /
                                             static_cast<double>(stats->cpuTimerFreq)
                                       : 0.0;
        const double gpuFrameMs = stats->gpuTimerFreq > 0
                                      ? static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * 1000.0 /
                                            static_cast<double>(stats->gpuTimerFreq)
                                      : 0.0;
        const render::ForwardRenderer::FrameDiagnostics diagnostics = renderer.frameDiagnostics();
        HBG_LOGI("render performance: frames={} avg_frame_ms={:.3f} fps={:.1f} cpu_submit_ms={:.3f} "
                 "gpu_frame_ms={:.3f} draw={} compute={} textures={} triangles={} passes={} deferred={} "
                 "gbuffer_mrt={} stencil={} postprocess={} post_passes={} postprocess_ms={:.3f} post_compute={} "
                 "post_history={} post_exposure={} post_uber={} post_bloom={} post_taa={} post_dof={} "
                 "post_motion_blur={} post_fxaa={} fog={} fog_steps={} fog_scale={:.3f} fog_lowres={}x{} "
                 "fog_temporal={} fog_history={} fog_lights={} fog_local={} fog_local_masks={} "
                 "shadow_drawn={} shadow_culled={} shadow_cache={} shadow_cached={} contact={} contact_res={}x{} "
                 "gpu_light_list={} gpu_light_list_requested={} gpu_light_list_fallback={} light_list_passes={} "
                 "light_list_cpu_ms={:.3f} shadow_cull_path={} shadow_indirect={} shadow_cull_dispatches={} "
                 "shadow_indirect_draws={} async_light_list_requested={} async_light_list_active={} "
                 "taa_history={} taa_frames={} gpuFrame={}",
                 count, averageFrameMs, averageFrameMs > 0.0 ? 1000.0 / averageFrameMs : 0.0, cpuSubmitMs,
                 gpuFrameMs, stats->numDraw, stats->numCompute, stats->numTextures,
                 stats->numPrims[bgfx::Topology::TriList], diagnostics.passCount,
                 diagnostics.deferred ? "true" : "false", diagnostics.gbufferTargets,
                 diagnostics.stencil ? "true" : "false", diagnostics.postProcessActive ? "true" : "false",
                 diagnostics.postProcessPasses, diagnostics.postProcessWallMs,
                 diagnostics.postProcessComputeSupported ? "true" : "false",
                 diagnostics.postProcessHistoryValid ? "true" : "false",
                 diagnostics.postProcessExposureActive ? "true" : "false",
                 diagnostics.postProcessUberActive ? "true" : "false",
                 diagnostics.postProcessBloomActive ? "true" : "false",
                 diagnostics.postProcessTaaActive ? "true" : "false",
                 diagnostics.postProcessDofActive ? "true" : "false",
                 diagnostics.postProcessMotionBlurActive ? "true" : "false",
                 diagnostics.postProcessFxaaActive ? "true" : "false",
                 diagnostics.volumetricFogActive ? "true" : "false", diagnostics.volumetricFogSteps,
                 diagnostics.volumetricFogScale, diagnostics.volumetricFogWidth, diagnostics.volumetricFogHeight,
                 diagnostics.volumetricFogTemporal ? "true" : "false",
                 diagnostics.volumetricFogHistoryValid ? "true" : "false", diagnostics.volumetricFogPunctualLights,
                 diagnostics.volumetricFogLocalVolumes, diagnostics.volumetricFogLocalMasks,
                 diagnostics.shadowDrawnItems, diagnostics.shadowCulledItems,
                 diagnostics.shadowCacheActive ? "true" : "false", diagnostics.shadowCachedRequests,
                 diagnostics.contactShadowsActive ? "true" : "false", diagnostics.contactShadowWidth,
                 diagnostics.contactShadowHeight, diagnostics.gpuLightListActive ? "true" : "false",
                 diagnostics.gpuLightListRequested ? "true" : "false",
                 diagnostics.gpuLightListFallback ? "true" : "false", diagnostics.gpuLightListPasses,
                 diagnostics.lightListCpuMs, diagnostics.gpuShadowCulling ? "gpu" : "cpu",
                 diagnostics.gpuShadowIndirect ? "true" : "false", diagnostics.gpuShadowCullDispatches,
                 diagnostics.gpuShadowIndirectDraws, diagnostics.asyncGpuLightListRequested ? "true" : "false",
                 diagnostics.asyncGpuLightListActive ? "true" : "false",
                 diagnostics.postProcessTaaHistoryValid ? "true" : "false",
                 diagnostics.postProcessTaaHistoryFrames, stats->gpuFrameNum);
    }

    renderer.shutdown(resources);
    resources.flushPending(true);
    if (resources.liveResourceCount() != 0) {
        HBG_LOGE("render test leaked {} resources", resources.liveResourceCount());
        return false;
    }
    HBG_LOGI("render test passed");
    return true;
}

} // namespace hbg
