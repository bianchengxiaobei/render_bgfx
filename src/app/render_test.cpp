#include "app/render_test.h"

#include "asset/scene_loader.h"
#include "core/log.h"
#include "core/math.h"
#include "render/passes/forward_renderer.h"

#include <algorithm>
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
                   bool cameraOrbit, const render::PipelineSettings& pipelineSettings,
                   const render::FrameSettings& frameSettings, const render::PostProcessSettings& postProcessSettings,
                   float renderScale, const std::string& skyPath, float skyIntensity, float skyRotation) {
    HBG_LOGI("running forward render test: '{}'", path);
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
    renderer.setSettings(pipelineSettings, frameSettings);
    renderer.setPostProcessSettings(postProcessSettings);
    renderer.setRenderScale(renderScale);
    renderer.setDebugDisplayMode(debugMode);
    if (!skyPath.empty() && !renderer.setSky(skyPath, skyIntensity, skyRotation)) {
        renderer.shutdown(resources);
        return false;
    }

    const uint32_t count = frames > 0 ? frames : 1;
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

    const bgfx::Stats* stats = bgfx::getStats();
    if (stats != nullptr) {
        HBG_LOGI("forward render stats: draw={} compute={} textures={} triangles={} gpuFrame={}", stats->numDraw,
                 stats->numCompute, stats->numTextures, stats->numPrims[bgfx::Topology::TriList], stats->gpuFrameNum);
    }

    renderer.shutdown(resources);
    resources.flushPending(true);
    if (resources.liveResourceCount() != 0) {
        HBG_LOGE("forward render test leaked {} resources", resources.liveResourceCount());
        return false;
    }
    HBG_LOGI("forward render test passed");
    return true;
}

} // namespace hbg
