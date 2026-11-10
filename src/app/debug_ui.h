#pragma once

#include "app/window.h"
#include "core/profiler.h"
#include "gfx/resource_manager.h"
#include "render/debug/debug_display.h"
#include "shader/shader_library.h"

#include <cstdint>
#include <vector>

struct ImDrawData;

namespace hbg {

class DebugUI {
  public:
    DebugUI() = default;
    ~DebugUI() = default;

    DebugUI(const DebugUI&) = delete;
    DebugUI& operator=(const DebugUI&) = delete;

    bool init(gfx::ResourceManager& resources, shader::ShaderLibrary& shaderLibrary);
    void shutdown(gfx::ResourceManager& resources);

    bool valid() const { return m_valid; }
    bool renderDocLoaded() const { return m_renderDocLoaded; }
    render::DebugDisplaySettings& settings() { return m_settings; }
    const render::DebugDisplaySettings& settings() const { return m_settings; }
    const ProfilerStats& profilerStats() const { return m_profiler.stats(); }

    void beginFrame(const WindowInput& input, uint16_t width, uint16_t height, double deltaSeconds);
    void endFrame(uint16_t width, uint16_t height);

  private:
    void drawPanel();
    void syncTextures(ImDrawData& drawData);
    gfx::TextureHandle textureFor(uint64_t id) const;

    gfx::ResourceManager* m_resources = nullptr;
    shader::ShaderLibrary* m_shaderLibrary = nullptr;
    gfx::ProgramHandle m_program;
    gfx::UniformHandle m_sampler;
    std::vector<gfx::TextureHandle> m_textures;

    render::DebugDisplaySettings m_settings;
    Profiler m_profiler;
    bool m_valid = false;
    bool m_visible = true;
    bool m_renderDocLoaded = false;
    bool m_screenshotRequested = false;
    uint32_t m_screenshotCount = 0;
};

} // namespace hbg
