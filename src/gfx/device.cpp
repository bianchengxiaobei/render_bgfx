#include "gfx/device.h"

#include "core/assert.h"
#include "core/log.h"
#include "gfx/bgfx_callback.h"
#include "gfx/resource_manager.h"

namespace hbg::gfx {
namespace {

const char* rendererName(bgfx::RendererType::Enum type) {
    if (type == bgfx::RendererType::Count) {
        return "auto";
    }
    const char* name = bgfx::getRendererName(type);
    return name != nullptr ? name : "unknown";
}

} // namespace

Device::Device() = default;

Device::~Device() {
    shutdown();
}

bool Device::init(const DeviceDesc& desc) {
    if (m_initialized) {
        HBG_LOGW("Device::init called while already initialized");
        return false;
    }
    if (desc.nativeWindow == nullptr) {
        HBG_LOGE("Device::init requires a native window handle");
        return false;
    }
    if (desc.width == 0 || desc.height == 0) {
        HBG_LOGE("Device::init requires a non-zero swap chain size");
        return false;
    }

    bgfx::Init init;
    init.type = desc.backend;
    init.vendorId = BGFX_PCI_ID_NONE;
    init.swapChain.nwh = desc.nativeWindow;
    init.swapChain.ndt = nullptr;
    init.swapChain.width = desc.width;
    init.swapChain.height = desc.height;
    init.swapChain.flags = BGFX_SWAP_CHAIN_NONE;
    init.reset = desc.vsync ? BGFX_RESET_VSYNC : BGFX_RESET_NONE;
    init.reset |= BGFX_RESET_MAXANISOTROPY;
    init.debug = desc.debug;
    init.profile = desc.profile;
    init.fallback = false;
    m_callback = std::make_unique<BgfxCallback>();
    init.callback = m_callback.get();

    HBG_LOGI("initializing bgfx backend '{}' ({}x{})", rendererName(desc.backend), desc.width, desc.height);

    if (!bgfx::init(init)) {
        HBG_LOGE("bgfx::init failed for backend '{}'", rendererName(desc.backend));
        return false;
    }

    m_initialized = true;
    m_resetFlags = init.reset;

    m_resources = std::make_unique<ResourceManager>();
    m_resources->init();

    logCaps();
    return true;
}

void Device::shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_resources != nullptr) {
        m_resources->shutdown();
        m_resources.reset();
    }

    HBG_LOGI("shutting down bgfx");
    bgfx::shutdown();
    m_initialized = false;
    m_callback.reset();
}

void Device::resize(uint16_t width, uint16_t height) {
    if (!m_initialized || width == 0 || height == 0) {
        return;
    }

    bgfx::SwapChain swapChain;
    swapChain.width = width;
    swapChain.height = height;
    swapChain.flags = BGFX_SWAP_CHAIN_NONE;

    bgfx::reset(m_resetFlags, &swapChain);
    HBG_LOGD("swap chain resized to {}x{}", width, height);
}

const bgfx::Caps* Device::caps() const {
    return m_initialized ? bgfx::getCaps() : nullptr;
}

ResourceManager& Device::resources() {
    HBG_ASSERT(m_resources != nullptr);
    return *m_resources;
}

const ResourceManager& Device::resources() const {
    HBG_ASSERT(m_resources != nullptr);
    return *m_resources;
}

void Device::logCaps() const {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps == nullptr) {
        return;
    }

    HBG_LOGI("renderer: {} (vendor=0x{:04x} device=0x{:04x})", rendererName(caps->rendererType), caps->vendorId,
             caps->deviceId);
    HBG_LOGI("ndc: homogeneousDepth={} originBottomLeft={}", caps->homogeneousDepth, caps->originBottomLeft);

    const bgfx::Caps::Limits& limits = caps->limits;
    HBG_LOGI("limits: maxTextureSize={} maxViews={} maxFBAttachments={} maxTextureSamplers={} "
             "maxComputeBindings={} maxDrawCalls={}",
             limits.maxTextureSize, limits.maxViews, limits.maxFBAttachments, limits.maxTextureSamplers,
             limits.maxComputeBindings, limits.maxDrawCalls);

    const auto has = [caps](uint64_t flag) { return (caps->supported & flag) != 0; };
    HBG_LOGI("caps: compute={} imageRW={} indirect={} indirectCount={} cubeArray={} shaderF16={}",
             has(BGFX_CAPS_COMPUTE), has(BGFX_CAPS_IMAGE_RW), has(BGFX_CAPS_DRAW_INDIRECT),
             has(BGFX_CAPS_DRAW_INDIRECT_COUNT), has(BGFX_CAPS_TEXTURE_CUBE_ARRAY), has(BGFX_CAPS_SHADER_F16));

    struct FormatInfo {
        bgfx::TextureFormat::Enum format;
        const char* name;
    };
    static constexpr FormatInfo kFormats[] = {
        {bgfx::TextureFormat::RGBA8, "RGBA8"},     {bgfx::TextureFormat::BGRA8, "BGRA8"},
        {bgfx::TextureFormat::RGBA16F, "RGBA16F"}, {bgfx::TextureFormat::RGBA32F, "RGBA32F"},
        {bgfx::TextureFormat::R32U, "R32U"},       {bgfx::TextureFormat::R32F, "R32F"},
        {bgfx::TextureFormat::RGB9E5F, "RGB9E5F"}, {bgfx::TextureFormat::D32F, "D32F"},
        {bgfx::TextureFormat::D24S8, "D24S8"},
    };

    for (const FormatInfo& info : kFormats) {
        const uint32_t flags = caps->formats[info.format];
        HBG_LOGI("format {:<8} 2D={} rt={} srgb={} msaa={}", info.name, (flags & BGFX_CAPS_FORMAT_TEXTURE_2D) != 0,
                 (flags & BGFX_CAPS_FORMAT_TEXTURE_FRAMEBUFFER) != 0, (flags & BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB) != 0,
                 (flags & BGFX_CAPS_FORMAT_TEXTURE_MSAA) != 0);
    }
}

} // namespace hbg::gfx
