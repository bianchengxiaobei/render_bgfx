#pragma once

#include "app/backend.h"
#include "app/debug_ui.h"
#include "app/window.h"
#include "gfx/device.h"
#include "render/passes/forward_renderer.h"
#include "render/pipeline/frame_settings.h"
#include "render/pipeline/pipeline_settings.h"
#include "render/scene/render_scene.h"

#include <memory>

namespace hbg {

class Application {
  public:
    int run(const AppOptions& options);

  private:
    bool init(const AppOptions& options);
    bool loadSettings(const AppOptions& options);
    bool loadScene(const std::string& path);
    void shutdown();
    void frame(const WindowInput& input, double deltaSeconds, bool finalFrame,
               const std::string& screenshotPath);

    Window m_window;
    gfx::Device m_device;
    std::unique_ptr<render::RenderScene> m_scene;
    render::ForwardRenderer m_forwardRenderer;
    render::PipelineSettings m_pipelineSettings;
    render::FrameSettings m_frameSettings;
    DebugUI m_debugUi;
    float m_time = 0.0f;
    double m_lastFrameTime = 0.0;
};

} // namespace hbg
