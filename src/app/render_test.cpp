#include "app/render_test.h"

#include "asset/animation.h"
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
                   bool debugLightmapMask, int lightClusterSlice, bool lightClusterEnabled, bool cameraOrbit,
                   bool shadowsEnabled,
                   bool clusterStatsReadback, const render::PipelineSettings& pipelineSettings,
                   const render::FrameSettings& frameSettings, const render::ShadowSettings& shadowSettings,
                   const render::SsaoSettings& ssaoSettings, const render::SsrSettings& ssrSettings,
                   const render::SsgiSettings& ssgiSettings,
                   const render::PostProcessProfile& postProcessProfile,
                   const render::VolumetricFogSettings& volumetricFogSettings, float renderScale,
                   const render::DynamicResolutionSettings& dynamicResolutionSettings,
                   const std::string& skyPath, float skyIntensity, float skyRotation, bool resizeTest) {
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
    renderer.setSsaoSettings(ssaoSettings);
    renderer.setSsrSettings(ssrSettings);
    renderer.setSsgiSettings(ssgiSettings);
    renderer.setShadowSettings(shadowSettings);
    renderer.setPostProcessProfile(postProcessProfile);
    renderer.setVolumetricFogSettings(volumetricFogSettings);
    renderer.setRenderScale(renderScale);
    render::DynamicResolutionController dynamicResolution;
    dynamicResolution.configure(dynamicResolutionSettings);
    dynamicResolution.reset(renderScale);
    renderer.setDebugDisplayMode(debugMode);
    renderer.setDebugLightmapMask(debugLightmapMask);
    renderer.setDebugLightClusterSlice(lightClusterSlice);
    renderer.setLightClusterEnabled(lightClusterEnabled);
    renderer.setShadowsEnabled(shadowsEnabled);
    if (!skyPath.empty()) {
        if (!renderer.setSky(skyPath, skyIntensity, skyRotation)) {
            renderer.shutdown(resources);
            return false;
        }
    } else if (loaded.scene->sky.enabled && loaded.scene->sky.mode == render::SceneSkyMode::Skybox &&
               !loaded.scene->sky.texture.empty()) {
        if (!renderer.setSky(loaded.scene->sky.texture.string(), loaded.scene->sky.intensity,
                             loaded.scene->sky.rotationDegrees)) {
            renderer.shutdown(resources);
            return false;
        }
    }

    if (clusterStatsReadback) {
        renderer.requestClusterStatsReadback();
    }
    const uint32_t requestedFrames = frames > 0 ? frames : 1;
    const uint32_t count = clusterStatsReadback ? std::max(requestedFrames, 8u) : requestedFrames;
    const auto renderStart = std::chrono::steady_clock::now();
    for (uint32_t frame = 0; frame < count; ++frame) {
        const bool halfSize = resizeTest && (frame % 2u) != 0u;
        const uint16_t renderWidth = halfSize ? std::max<uint16_t>(1u, width / 2u) : width;
        const uint16_t renderHeight = halfSize ? std::max<uint16_t>(1u, height / 2u) : height;
        const float sceneTime = static_cast<float>(frame) / 60.0f;
        asset::applyAnimation(*loaded.scene, sceneTime);
        if (cameraOrbit) {
            updateOrbitCamera(*loaded.scene, frame, renderWidth, renderHeight);
        }
        const auto frameStart = std::chrono::steady_clock::now();
        renderer.render(*loaded.scene, renderWidth, renderHeight, sceneTime);
        if (frame + 1 == count && !screenshotPath.empty()) {
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, screenshotPath.c_str());
        }
        bgfx::frame();
        resources.endFrame();
        const double frameMs =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - frameStart).count();
        if (!resizeTest) {
            if (const std::optional<float> nextScale = dynamicResolution.update(frameMs / 1000.0)) {
                renderer.setRenderScale(*nextScale);
                HBG_LOGI("dynamic resolution: scale={:.3f} avg_frame_ms={:.3f} target_ms={:.3f} adjustments={}",
                         *nextScale, dynamicResolution.averageFrameMs(),
                         dynamicResolution.settings().targetFrameMs, dynamicResolution.adjustmentCount());
            }
        }
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
        HBG_LOGI("depth pyramid: requested={} active={} mips={}",
                 diagnostics.depthPyramidComputeRequested ? "true" : "false",
                 diagnostics.depthPyramidComputeActive ? "true" : "false", diagnostics.depthPyramidMips);
        HBG_LOGI("gpu skinning: requested={} active={} meshes={} vertices={} dispatches={}",
                 diagnostics.gpuSkinningRequested ? "true" : "false",
                 diagnostics.gpuSkinningActive ? "true" : "false", diagnostics.gpuSkinningMeshes,
                 diagnostics.gpuSkinningVertices, diagnostics.gpuSkinningDispatches);
        HBG_LOGI("gpu-driven geometry: requested={} active={} hi_z={} forward={} gbuffer={} transparent={} "
                 "sort={} prepass={} bindless={} items={} prepass_items={} batches={} culling_passes={} "
                 "indirect_count_cap={}",
                 diagnostics.gpuDrivenRequested ? "true" : "false",
                 diagnostics.gpuDrivenActive ? "true" : "false",
                 diagnostics.gpuDrivenHiZActive ? "true" : "false",
                 diagnostics.gpuDrivenForwardActive ? "true" : "false",
                 diagnostics.gpuDrivenGbufferActive ? "true" : "false",
                 diagnostics.gpuDrivenTransparentActive ? "true" : "false",
                 diagnostics.gpuDrivenTransparentSorted ? "true" : "false",
                 diagnostics.gpuDrivenPrepassActive ? "true" : "false",
                 diagnostics.gpuDrivenBindlessActive ? "true" : "false", diagnostics.gpuDrivenItems,
                 diagnostics.gpuDrivenPrepassItems, diagnostics.gpuDrivenBatches,
                 diagnostics.gpuDrivenCullingPasses,
                 diagnostics.gpuDrivenIndirectCountSupported ? "true" : "false");
        HBG_LOGI("render performance: frames={} avg_frame_ms={:.3f} fps={:.1f} cpu_submit_ms={:.3f} "
                 "gpu_frame_ms={:.3f} draw={} compute={} textures={} triangles={} passes={} deferred={} "
                 "vfx={} vfx_effects={} vfx_systems={} vfx_particles={} vfx_draws={} vfx_shadow_draws={} vfx_dispatches={} "
                 "vfx_sort_passes={} vfx_events={} vfx_ms={:.3f} vfx_update_ms={:.3f} "
                 "vfx_sim_ms={:.3f} vfx_output_ms={:.3f} "
                 "gbuffer_mrt={} stencil={} postprocess={} post_passes={} postprocess_ms={:.3f} post_compute={} "
                 "post_history={} post_exposure={} post_uber={} post_bloom={} post_taa={} post_dof={} "
                 "post_motion_blur={} post_fxaa={} fog={} fog_steps={} fog_scale={:.3f} fog_lowres={}x{} "
                 "fog_temporal={} fog_history={} fog_lights={} fog_local={} fog_local_masks={} "
                 "shadow_drawn={} shadow_culled={} shadow_dir_cache={} shadow_dir_mixed={} shadow_cached_dir={} shadow_cache={} shadow_cached={} shadow_area_cache={} "
                 "shadow_cached_area={} contact={} contact_res={}x{} "
                 "gpu_light_list={} gpu_light_list_requested={} gpu_light_list_fallback={} light_list_passes={} "
                 "light_list_cpu_ms={:.3f} shadow_cull_path={} shadow_indirect={} shadow_cull_dispatches={} "
                 "shadow_indirect_draws={} async_light_list_requested={} async_light_list_active={} "
                 "light_list_capacity_requested={} light_list_capacity_effective={} light_list_stride={} "
                 "light_list_truncated={} light_list_capacity_clamped={} light_list_feature_flags={} cluster_stats_valid={} "
                 "cluster_stats_gpu={} cluster_stats_pending={} cluster_stats_clusters={} "
                 "cluster_stats_active={} cluster_stats_overflow_clusters={} cluster_stats_overflow_lights={} "
                 "cluster_stats_max_overflow={} cluster_stats_max_lights={} "
                 "taa_history={} taa_frames={} ssao_temporal={} ssao_history={} ssao_frames={} "
                 "ssr_temporal={} ssr_history={} ssr_frames={} ssgi_temporal={} ssgi_history={} "
                 "ssgi_frames={} gpuFrame={}",
                 count, averageFrameMs, averageFrameMs > 0.0 ? 1000.0 / averageFrameMs : 0.0, cpuSubmitMs,
                 gpuFrameMs, stats->numDraw, stats->numCompute, stats->numTextures,
                 stats->numPrims[bgfx::Topology::TriList], diagnostics.passCount,
                 diagnostics.deferred ? "true" : "false",
                 diagnostics.vfxActive ? "true" : "false", diagnostics.vfxEffects, diagnostics.vfxSystems,
                 diagnostics.vfxParticles, diagnostics.vfxDraws, diagnostics.vfxShadowDraws,
                 diagnostics.vfxDispatches,
                 diagnostics.vfxSortPasses, diagnostics.vfxEvents, diagnostics.vfxMs,
                 diagnostics.vfxUpdateMs, diagnostics.vfxSimulationMs, diagnostics.vfxOutputMs,
                 diagnostics.gbufferTargets,
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
                 diagnostics.shadowDirectionalCacheActive ? "true" : "false",
                 diagnostics.shadowDirectionalMixedCached ? "true" : "false",
                 diagnostics.shadowCachedDirectionalRequests,
                 diagnostics.shadowCacheActive ? "true" : "false", diagnostics.shadowCachedRequests,
                 diagnostics.shadowAreaCacheActive ? "true" : "false", diagnostics.shadowCachedAreaRequests,
                 diagnostics.contactShadowsActive ? "true" : "false", diagnostics.contactShadowWidth,
                 diagnostics.contactShadowHeight, diagnostics.gpuLightListActive ? "true" : "false",
                 diagnostics.gpuLightListRequested ? "true" : "false",
                 diagnostics.gpuLightListFallback ? "true" : "false", diagnostics.gpuLightListPasses,
                 diagnostics.lightListCpuMs, diagnostics.gpuShadowCulling ? "gpu" : "cpu",
                 diagnostics.gpuShadowIndirect ? "true" : "false", diagnostics.gpuShadowCullDispatches,
                 diagnostics.gpuShadowIndirectDraws, diagnostics.asyncGpuLightListRequested ? "true" : "false",
                 diagnostics.asyncGpuLightListActive ? "true" : "false",
                 diagnostics.lightListCapacityRequested, diagnostics.lightListCapacityEffective,
                 diagnostics.lightListStride, diagnostics.lightListTruncated,
                 diagnostics.lightListCapacityClamped ? "true" : "false",
                 diagnostics.lightListFeatureFlagsActive ? "true" : "false",
                 diagnostics.clusterStatsValid ? "true" : "false", diagnostics.clusterStatsGpu ? "true" : "false",
                 diagnostics.clusterStatsReadbackPending ? "true" : "false", diagnostics.clusterStatsClusters,
                 diagnostics.clusterStatsActiveClusters, diagnostics.clusterStatsOverflowClusters,
                 diagnostics.clusterStatsOverflowLights, diagnostics.clusterStatsMaxOverflow,
                 diagnostics.clusterStatsMaxLights, diagnostics.postProcessTaaHistoryValid ? "true" : "false",
                 diagnostics.postProcessTaaHistoryFrames, diagnostics.ssaoTemporal ? "true" : "false",
                 diagnostics.ssaoHistoryValid ? "true" : "false", diagnostics.ssaoHistoryFrames,
                 diagnostics.ssrTemporal ? "true" : "false",
                 diagnostics.ssrHistoryValid ? "true" : "false", diagnostics.ssrHistoryFrames,
                 diagnostics.ssgiTemporal ? "true" : "false",
                 diagnostics.ssgiHistoryValid ? "true" : "false", diagnostics.ssgiHistoryFrames,
                 stats->gpuFrameNum);
    }

    bool clusterStatsOk = true;
    if (clusterStatsReadback) {
        const render::ForwardRenderer::FrameDiagnostics diagnostics = renderer.frameDiagnostics();
        if (diagnostics.clusterStatsValid) {
            HBG_LOGI("cluster stats readback: path={} clusters={} active={} overflow_clusters={} "
                     "overflow_lights={} max_overflow={} max_lights={} feature_flags_checked={} "
                     "feature_flag_mismatch={}",
                     diagnostics.clusterStatsGpu ? "gpu" : "cpu", diagnostics.clusterStatsClusters,
                     diagnostics.clusterStatsActiveClusters, diagnostics.clusterStatsOverflowClusters,
                     diagnostics.clusterStatsOverflowLights, diagnostics.clusterStatsMaxOverflow,
                     diagnostics.clusterStatsMaxLights, diagnostics.clusterStatsFeatureFlagsChecked,
                     diagnostics.clusterStatsFeatureFlagMismatch);
            if (diagnostics.clusterStatsGpu && diagnostics.clusterStatsFeatureFlagsChecked == 0) {
                HBG_LOGE("cluster stats readback: GPU feature flags were not checked");
                clusterStatsOk = false;
            }
            if (diagnostics.clusterStatsFeatureFlagMismatch != 0) {
                HBG_LOGE("cluster stats readback: {} feature flag mismatches",
                         diagnostics.clusterStatsFeatureFlagMismatch);
                clusterStatsOk = false;
            }
        } else {
            HBG_LOGE("cluster stats readback requested but no stats were produced (pending={})",
                     diagnostics.clusterStatsReadbackPending ? "true" : "false");
            clusterStatsOk = false;
        }
    }

    renderer.shutdown(resources);
    if (!clusterStatsOk) {
        return false;
    }
    resources.flushPending(true);
    if (resources.liveResourceCount() != 0) {
        HBG_LOGE("render test leaked {} resources", resources.liveResourceCount());
        return false;
    }
    HBG_LOGI("render test passed");
    return true;
}

} // namespace hbg
