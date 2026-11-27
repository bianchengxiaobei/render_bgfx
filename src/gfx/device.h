#pragma once

#include <bgfx/bgfx.h>

#include <cstdint>
#include <memory>

namespace hbg::gfx {

class BgfxCallback;
class ResourceManager;

struct DeviceDesc {
    bgfx::RendererType::Enum backend = bgfx::RendererType::Count;
    void* nativeWindow = nullptr;
    uint16_t width = 1280;
    uint16_t height = 720;
    bool vsync = true;
    bool debug = false;
    bool profile = true;
};

class Device {
  public:
    Device();
    ~Device();

    bool init(const DeviceDesc& desc);
    void shutdown();

    void resize(uint16_t width, uint16_t height);

    const bgfx::Caps* caps() const;
    bool isValid() const { return m_initialized; }

    ResourceManager& resources();
    const ResourceManager& resources() const;

  private:
    void logCaps() const;

    std::unique_ptr<BgfxCallback> m_callback;
    std::unique_ptr<ResourceManager> m_resources;
    bool m_initialized = false;
    uint32_t m_resetFlags = BGFX_RESET_NONE;
};

} // namespace hbg::gfx
