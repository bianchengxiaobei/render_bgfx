#include "gfx/bgfx_callback.h"

#include "core/log.h"

#include <bimg/bimg.h>
#include <bx/error.h>
#include <bx/file.h>

#include <cstdio>
#include <cstdlib>

namespace hbg::gfx {
namespace {

bimg::TextureFormat::Enum toBimg(bgfx::TextureFormat::Enum format) {
    switch (format) {
    case bgfx::TextureFormat::RGBA8:
        return bimg::TextureFormat::RGBA8;
    case bgfx::TextureFormat::BGRA8:
        return bimg::TextureFormat::BGRA8;
    case bgfx::TextureFormat::R8:
        return bimg::TextureFormat::R8;
    default:
        return bimg::TextureFormat::Count;
    }
}

} // namespace

void BgfxCallback::fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) {
    HBG_LOGE("bgfx fatal ({}:{}): code={} {}", filePath != nullptr ? filePath : "?", line, static_cast<int>(code),
             str != nullptr ? str : "");

    if (code != bgfx::Fatal::DebugCheck) {
        std::abort();
    }
}

void BgfxCallback::traceVargs(const char*, uint16_t, const char* format, va_list argList) {
    char buffer[2048];
    std::vsnprintf(buffer, sizeof(buffer), format != nullptr ? format : "", argList);
    HBG_LOGD("bgfx: {}", buffer);
}

void BgfxCallback::screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch,
                              bgfx::TextureFormat::Enum format, const void* data, uint32_t size, bool yflip) {
    if (filePath == nullptr || data == nullptr || width == 0 || height == 0) {
        return;
    }

    const bimg::TextureFormat::Enum sourceFormat = toBimg(format);
    if (sourceFormat == bimg::TextureFormat::Count) {
        HBG_LOGW("screenshot: unsupported bgfx format {}", static_cast<int>(format));
        return;
    }

    const uint32_t rowBytes = width * bimg::getBitsPerPixel(sourceFormat) / 8;
    if (pitch < rowBytes || size < pitch * height) {
        HBG_LOGW("screenshot: invalid source buffer ({}x{}, pitch={}, size={})", width, height, pitch, size);
        return;
    }

    bx::FileWriter writer;
    bx::Error error;
    if (!writer.open(bx::FilePath(filePath), false, &error)) {
        HBG_LOGW("screenshot: failed to open '{}'", filePath);
        return;
    }

    const int32_t written =
        bimg::imageWritePng(&writer, width, height, pitch, data, sourceFormat, yflip, &error);
    writer.close();
    if (written <= 0 || !error.isOk()) {
        HBG_LOGW("screenshot: PNG encoding failed");
        return;
    }
    HBG_LOGI("screenshot: wrote '{}' ({}x{})", filePath, width, height);
}

} // namespace hbg::gfx
