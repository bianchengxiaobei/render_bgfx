#include "app/application.h"

#include "app/asset_test.h"
#include "app/gfx_test.h"
#include "app/render_test.h"
#include "asset/scene_loader.h"
#include "core/log.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
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
            options.debugDisplayMode, options.cameraOrbit, m_pipelineSettings, m_frameSettings, options.postProcess,
            options.renderScale, options.skyPath, options.skyIntensity, options.skyRotation);
        shutdown();
        return ok ? 0 : 1;
    }

    if (!options.scenePath.empty() && !loadScene(options.scenePath)) {
        shutdown();
        return 1;
    }
    if (m_forwardRenderer.valid()) {
        if (!options.skyPath.empty() &&
            !m_forwardRenderer.setSky(options.skyPath, options.skyIntensity, options.skyRotation)) {
            HBG_LOGW("sky load failed; continuing without an environment");
        }
        m_forwardRenderer.setRenderScale(options.renderScale);
        m_forwardRenderer.setPostProcessSettings(options.postProcess);
    }

    if (m_forwardRenderer.valid() && options.debugUi) {
        if (!m_debugUi.init(m_device.resources(), m_forwardRenderer.shaderLibrary())) {
            HBG_LOGW("debug ui initialization failed; continuing without the panel");
        }
    }
    if (m_debugUi.valid()) {
        render::DebugDisplaySettings& settings = m_debugUi.settings();
        settings.fullscreen = options.debugDisplayMode;
        settings.wireframe = options.debugWireframe;
        settings.shadows = !options.disableShadows;
        settings.renderScale = options.renderScale;
        settings.postProcess = options.postProcess;
    } else {
        m_forwardRenderer.setDebugDisplayMode(options.debugDisplayMode);
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
    WindowInput input;
    while (m_window.pollEvents()) {
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

    std::string error;
    if (!options.pipelineSettingsPath.empty() &&
        !render::loadPipelineSettings(options.pipelineSettingsPath, m_pipelineSettings, error)) {
        HBG_LOGE("pipeline settings load failed: {}", error);
        return false;
    }
    if (!options.frameSettingsPath.empty() &&
        !render::loadFrameSettings(options.frameSettingsPath, m_frameSettings, error)) {
        HBG_LOGE("frame settings load failed: {}", error);
        return false;
    }

    m_frameSettings.sanitize(m_pipelineSettings);
    HBG_LOGI("settings: shadows={} opaque={} transparent={} postprocess={} motionVectors={}",
             m_frameSettings.isEnabled(render::FrameSettingsField::ShadowMaps),
             m_frameSettings.isEnabled(render::FrameSettingsField::OpaqueObjects),
             m_frameSettings.isEnabled(render::FrameSettingsField::TransparentObjects),
             m_frameSettings.isEnabled(render::FrameSettingsField::Postprocess),
             m_frameSettings.isEnabled(render::FrameSettingsField::MotionVectors));
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
    return true;
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
        m_debugUi.beginFrame(input, width, height, deltaSeconds);
        const render::DebugDisplaySettings& settings = m_debugUi.settings();
        m_forwardRenderer.setShadowsEnabled(settings.shadows);
        m_forwardRenderer.setWireframe(settings.wireframe);
        m_forwardRenderer.setRenderScale(settings.renderScale);
        m_forwardRenderer.setDebugDisplayMode(settings.fullscreen);
        m_forwardRenderer.setPostProcessSettings(settings.postProcess);
    }

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
