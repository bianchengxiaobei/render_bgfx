#include "app/render_test.h"

#include "asset/scene_loader.h"
#include "core/log.h"
#include "core/math.h"
#include "render/camera/view_state.h"
#include "render/passes/forward_renderer.h"
#include "render/terrain/terrain_lod_controller.h"
#include "render/water/water_screen_grid.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <memory>

namespace hbg {
namespace {

void updateSunCamera(render::RenderScene& scene) {
    if (scene.cameras.empty()) {
        scene.cameras.emplace_back();
    }

    const render::Light* sun = nullptr;
    for (const render::Light& light : scene.lights) {
        if (light.type == render::LightType::Directional && light.intensity > 0.0f) {
            sun = &light;
            break;
        }
    }
    if (sun == nullptr) {
        HBG_LOGW("camera-look-sun: no directional light found");
        return;
    }

    const Vec3 direction = normalize(transformDirection(sun->world, Vec3(0.0f, 0.0f, 1.0f)));
    render::Camera& camera = scene.cameras.front();
    const Vec3 position(camera.world.values[12], camera.world.values[13], camera.world.values[14]);
    const Vec3 up = std::abs(direction.y) > 0.99f ? Vec3(0.0f, 0.0f, 1.0f) : Vec3(0.0f, 1.0f, 0.0f);
    camera.world = inverse(lookAtRH(position, position + direction, up));
}

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

Vec3 sceneCameraPosition(const render::RenderScene& scene) {
    if (scene.cameras.empty()) {
        return Vec3(0.0f, 0.0f, 0.0f);
    }
    const Mat4& world = scene.cameras.front().world;
    return Vec3(world.values[12], world.values[13], world.values[14]);
}

} // namespace

bool runRenderTest(gfx::ResourceManager& resources, const std::string& path, uint16_t width, uint16_t height,
                   uint32_t frames, const std::string& screenshotPath, render::DebugDisplayMode debugMode,
                   int lightClusterSlice, bool lightClusterEnabled, bool cameraOrbit, bool cameraLookSun,
                   bool shadowsEnabled, bool clusterStatsReadback,
                   const render::PipelineSettings& pipelineSettings,
                   const render::FrameSettings& frameSettings, const render::ShadowSettings& shadowSettings,
                   const render::SsaoSettings& ssaoSettings, const render::SsrSettings& ssrSettings,
                   const render::SsgiSettings& ssgiSettings, const render::PostProcessProfile& postProcessProfile,
                   const render::VolumetricFogSettings& volumetricFogSettings,
                   const render::AtmosphereSettings& atmosphereSettings, float renderScale,
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
    if (cameraLookSun) {
        updateSunCamera(*loaded.scene);
    }
    render::TerrainLodController terrainLod;
    std::string terrainLodError;
    if (!terrainLod.load(path, *loaded.scene, &resources, &terrainLodError) && !terrainLodError.empty()) {
        HBG_LOGW("terrain lod disabled: {}", terrainLodError);
    }
    render::ViewState terrainLodViewState;
    if (loaded.scene->waterFftEnabled && loaded.scene->waterMeshIndex < loaded.scene->meshes.size()) {
        render::WaterScreenGridDesc desc;
        if (!render::configureWaterScreenGrid(loaded.scene->meshes[loaded.scene->waterMeshIndex], width, height,
                                              &desc)) {
            HBG_LOGW("water screen grid configuration failed; keeping authored water mesh");
        }
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
    renderer.setAtmosphereSettings(atmosphereSettings);
    renderer.setRenderScale(renderScale);
    renderer.setDebugDisplayMode(debugMode);
    renderer.setDebugLightClusterSlice(lightClusterSlice);
    renderer.setLightClusterEnabled(lightClusterEnabled);
    renderer.setShadowsEnabled(shadowsEnabled);
    if (!skyPath.empty() && !renderer.setSky(skyPath, skyIntensity, skyRotation)) {
        renderer.shutdown(resources);
        return false;
    }

    if (clusterStatsReadback) {
        renderer.requestClusterStatsReadback();
    }
    const uint32_t requestedFrames = frames > 0 ? frames : 1;
    const uint32_t count = clusterStatsReadback ? std::max(requestedFrames, 8u) : requestedFrames;
    const auto renderStart = std::chrono::steady_clock::now();
    for (uint32_t frame = 0; frame < count; ++frame) {
        if (cameraOrbit) {
            updateOrbitCamera(*loaded.scene, frame, width, height);
        }
        if (terrainLod.active()) {
            const Vec3 cameraPosition = sceneCameraPosition(*loaded.scene);
            if (!loaded.scene->cameras.empty()) {
                terrainLodViewState.update(loaded.scene->cameras.front(), width, height, renderScale, false, false);
                const Vec3 cameraForward = terrainLodViewState.cameraForward();
                terrainLod.update(*loaded.scene, cameraPosition, &terrainLodViewState.constants().frustumPlanes,
                                  &cameraForward);
            } else {
                terrainLod.update(*loaded.scene, cameraPosition);
            }
        }
        renderer.render(*loaded.scene, width, height, static_cast<float>(frame) / 60.0f);
        if (frame + 1 == count && !screenshotPath.empty()) {
            bgfx::requestScreenShot(BGFX_INVALID_HANDLE, screenshotPath.c_str());
        }
        bgfx::frame();
        resources.endFrame();
    }

    if (terrainLod.active()) {
        const render::TerrainQuadTreeStats& lodStats = terrainLod.stats();
        HBG_LOGI("terrain lod: leaves={} visible_leaves={} horizon_active={} horizon_culled={} nodes={} depth={} "
                 "visible={} ground={}",
                 lodStats.leafCount, lodStats.visibleLeafCount, lodStats.horizonActive, lodStats.horizonCulledLeafCount,
                 lodStats.nodeCount, lodStats.maxDepth, terrainLod.visibleObjects(), terrainLod.groundHeight());
        if (terrainLod.asyncTilesActive()) {
            const render::HeightmapTileStreamer::Stats tileStats = terrainLod.tileStats();
            HBG_LOGI("terrain tile streamer: hits={} misses={} produced={} failed={} uploads={} evictions={} "
                     "pending={} gpu_layers={} gpu_bytes={}",
                     tileStats.hits, tileStats.misses, tileStats.produced, tileStats.failed, tileStats.uploads,
                     tileStats.evictions, tileStats.pending, tileStats.gpuLayers, tileStats.gpuBytes);
        } else {
            const render::HeightmapTileCache::Stats& cacheStats = terrainLod.heightmapCache().stats();
            if (terrainLod.heightmapCache().loaded()) {
                HBG_LOGI("terrain heightmap cache: hits={} misses={} evictions={} tiles={}", cacheStats.hits,
                         cacheStats.misses, cacheStats.evictions, cacheStats.loadedTiles);
            }
        }
        if (terrainLod.materialTilesActive()) {
            const render::TerrainMaterialTileStreamer::Stats materialStats = terrainLod.materialTileStats();
            HBG_LOGI("terrain material tiles: hits={} fallbacks={} misses={} produced={} failed={} uploads={} "
                     "evictions={} prefetched={} pending={} resident={} gpu_layers={} gpu_bytes={} "
                     "gpu_produced={} gpu_failed={}",
                     materialStats.hits, materialStats.fallbacks, materialStats.misses, materialStats.produced,
                     materialStats.failed, materialStats.uploads, materialStats.evictions, materialStats.prefetched,
                     materialStats.pending, materialStats.residentTiles, materialStats.gpuLayers,
                     materialStats.gpuBytes, materialStats.gpuProduced, materialStats.gpuFailed);
        }
    }
    terrainLod.shutdown();

    const auto renderEnd = std::chrono::steady_clock::now();
    const double averageFrameMs =
        std::chrono::duration<double, std::milli>(renderEnd - renderStart).count() / static_cast<double>(count);
    const bgfx::Stats* stats = bgfx::getStats();
    if (stats != nullptr) {
        const double cpuSubmitMs = stats->cpuTimerFreq > 0 ? static_cast<double>(stats->cpuTimeFrame) * 1000.0 /
                                                                 static_cast<double>(stats->cpuTimerFreq)
                                                           : 0.0;
        const double gpuFrameMs = stats->gpuTimerFreq > 0
                                      ? static_cast<double>(stats->gpuTimeEnd - stats->gpuTimeBegin) * 1000.0 /
                                            static_cast<double>(stats->gpuTimerFreq)
                                      : 0.0;
        const render::ForwardRenderer::FrameDiagnostics diagnostics = renderer.frameDiagnostics();
        HBG_LOGI(
            "render performance: frames={} avg_frame_ms={:.3f} fps={:.1f} cpu_submit_ms={:.3f} "
            "gpu_frame_ms={:.3f} draw={} compute={} textures={} triangles={} passes={} deferred={} "
            "gbuffer_mrt={} stencil={} postprocess={} post_passes={} postprocess_ms={:.3f} post_compute={} "
            "post_history={} post_exposure={} post_uber={} post_bloom={} post_taa={} post_dof={} "
            "post_motion_blur={} post_fxaa={} atmosphere={} atmosphere_sky_steps={} atmosphere_aerial_steps={} "
            "fog={} fog_steps={} fog_scale={:.3f} fog_lowres={}x{} "
            "fog_temporal={} fog_history={} fog_lights={} fog_local={} fog_local_masks={} "
            "shadow_drawn={} shadow_culled={} shadow_dir_cache={} shadow_cached_dir={} shadow_cache={} "
            "shadow_cached={} shadow_area_cache={} "
            "shadow_cached_area={} contact={} contact_res={}x{} "
            "gpu_light_list={} gpu_light_list_requested={} gpu_light_list_fallback={} light_list_passes={} "
            "light_list_cpu_ms={:.3f} shadow_cull_path={} shadow_indirect={} shadow_cull_dispatches={} "
            "shadow_indirect_draws={} terrain_indirect_submissions={} terrain_indirect_records={} "
            "async_light_list_requested={} async_light_list_active={} "
            "light_list_capacity_requested={} light_list_capacity_effective={} light_list_stride={} "
            "light_list_truncated={} light_list_capacity_clamped={} light_list_feature_flags={} cluster_stats_valid={} "
            "cluster_stats_gpu={} cluster_stats_pending={} cluster_stats_clusters={} "
            "cluster_stats_active={} cluster_stats_overflow_clusters={} cluster_stats_overflow_lights={} "
            "cluster_stats_max_overflow={} cluster_stats_max_lights={} "
            "taa_history={} taa_frames={} ssao_temporal={} ssao_history={} ssao_frames={} "
            "ssr_temporal={} ssr_history={} ssr_frames={} ssgi_temporal={} ssgi_history={} "
            "ssgi_frames={} gpuFrame={}",
            count, averageFrameMs, averageFrameMs > 0.0 ? 1000.0 / averageFrameMs : 0.0, cpuSubmitMs, gpuFrameMs,
            stats->numDraw, stats->numCompute, stats->numTextures, stats->numPrims[bgfx::Topology::TriList],
            diagnostics.passCount, diagnostics.deferred ? "true" : "false", diagnostics.gbufferTargets,
            diagnostics.stencil ? "true" : "false", diagnostics.postProcessActive ? "true" : "false",
            diagnostics.postProcessPasses, diagnostics.postProcessWallMs,
            diagnostics.postProcessComputeSupported ? "true" : "false",
            diagnostics.postProcessHistoryValid ? "true" : "false",
            diagnostics.postProcessExposureActive ? "true" : "false",
            diagnostics.postProcessUberActive ? "true" : "false", diagnostics.postProcessBloomActive ? "true" : "false",
            diagnostics.postProcessTaaActive ? "true" : "false", diagnostics.postProcessDofActive ? "true" : "false",
            diagnostics.postProcessMotionBlurActive ? "true" : "false",
            diagnostics.postProcessFxaaActive ? "true" : "false",
            diagnostics.atmosphereActive ? "true" : "false", diagnostics.atmosphereSkySteps,
            diagnostics.atmosphereAerialSteps, diagnostics.volumetricFogActive ? "true" : "false",
            diagnostics.volumetricFogSteps, diagnostics.volumetricFogScale, diagnostics.volumetricFogWidth,
            diagnostics.volumetricFogHeight, diagnostics.volumetricFogTemporal ? "true" : "false",
            diagnostics.volumetricFogHistoryValid ? "true" : "false", diagnostics.volumetricFogPunctualLights,
            diagnostics.volumetricFogLocalVolumes, diagnostics.volumetricFogLocalMasks, diagnostics.shadowDrawnItems,
            diagnostics.shadowCulledItems, diagnostics.shadowDirectionalCacheActive ? "true" : "false",
            diagnostics.shadowCachedDirectionalRequests, diagnostics.shadowCacheActive ? "true" : "false",
            diagnostics.shadowCachedRequests, diagnostics.shadowAreaCacheActive ? "true" : "false",
            diagnostics.shadowCachedAreaRequests, diagnostics.contactShadowsActive ? "true" : "false",
            diagnostics.contactShadowWidth, diagnostics.contactShadowHeight,
            diagnostics.gpuLightListActive ? "true" : "false", diagnostics.gpuLightListRequested ? "true" : "false",
            diagnostics.gpuLightListFallback ? "true" : "false", diagnostics.gpuLightListPasses,
            diagnostics.lightListCpuMs, diagnostics.gpuShadowCulling ? "gpu" : "cpu",
            diagnostics.gpuShadowIndirect ? "true" : "false", diagnostics.gpuShadowCullDispatches,
            diagnostics.gpuShadowIndirectDraws, diagnostics.terrainIndirectSubmissions,
            diagnostics.terrainIndirectRecords, diagnostics.asyncGpuLightListRequested ? "true" : "false",
            diagnostics.asyncGpuLightListActive ? "true" : "false", diagnostics.lightListCapacityRequested,
            diagnostics.lightListCapacityEffective, diagnostics.lightListStride, diagnostics.lightListTruncated,
            diagnostics.lightListCapacityClamped ? "true" : "false",
            diagnostics.lightListFeatureFlagsActive ? "true" : "false",
            diagnostics.clusterStatsValid ? "true" : "false", diagnostics.clusterStatsGpu ? "true" : "false",
            diagnostics.clusterStatsReadbackPending ? "true" : "false", diagnostics.clusterStatsClusters,
            diagnostics.clusterStatsActiveClusters, diagnostics.clusterStatsOverflowClusters,
            diagnostics.clusterStatsOverflowLights, diagnostics.clusterStatsMaxOverflow,
            diagnostics.clusterStatsMaxLights, diagnostics.postProcessTaaHistoryValid ? "true" : "false",
            diagnostics.postProcessTaaHistoryFrames, diagnostics.ssaoTemporal ? "true" : "false",
            diagnostics.ssaoHistoryValid ? "true" : "false", diagnostics.ssaoHistoryFrames,
            diagnostics.ssrTemporal ? "true" : "false", diagnostics.ssrHistoryValid ? "true" : "false",
            diagnostics.ssrHistoryFrames, diagnostics.ssgiTemporal ? "true" : "false",
            diagnostics.ssgiHistoryValid ? "true" : "false", diagnostics.ssgiHistoryFrames, stats->gpuFrameNum);
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
