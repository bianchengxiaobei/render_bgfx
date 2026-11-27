#pragma once

#include <bgfx/bgfx.h>

#include <cstdarg>

namespace hbg::gfx {

class BgfxCallback final : public bgfx::CallbackI {
  public:
    ~BgfxCallback() override = default;

    void fatal(const char* filePath, uint16_t line, bgfx::Fatal::Enum code, const char* str) override;
    void traceVargs(const char* filePath, uint16_t line, const char* format, va_list argList) override;

    void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
    void profilerEnd() override {}

    uint32_t cacheReadSize(uint64_t) override { return 0; }
    bool cacheRead(uint64_t, void*, uint32_t) override { return false; }
    void cacheWrite(uint64_t, const void*, uint32_t) override {}

    void screenShot(const char* filePath, uint32_t width, uint32_t height, uint32_t pitch,
                    bgfx::TextureFormat::Enum format, const void* data, uint32_t size, bool yflip) override;
    void captureBegin(uint32_t, uint32_t, uint32_t, bgfx::TextureFormat::Enum, bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*, uint32_t) override {}
};

} // namespace hbg::gfx
