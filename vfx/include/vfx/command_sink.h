#pragma once

#include "vfx/device.h"

#include <cstdint>
#include <string_view>

#ifdef DrawState
#undef DrawState
#endif

namespace vfx {

enum class PrimitiveTopology : std::uint8_t { TriangleList };

struct DrawState {
    bool depthTest = true;
    bool depthWrite = false;
    bool cullBackFace = false;
    BlendMode blend = BlendMode::Alpha;
    CompareFunc depthFunc = CompareFunc::LessEqual;
};

struct PassDesc {
    std::string_view name;
    bool simulation = true;
    bool readsSceneDepth = false;
    bool readsSceneColor = false;
};

class CommandSink {
  public:
    virtual ~CommandSink() = default;

    virtual void beginPass(const PassDesc& desc) = 0;
    virtual void endPass() = 0;

    virtual void setStorageBuffer(std::uint8_t stage, BufferHandle buffer, Access access) = 0;
    virtual void setTexture(std::uint8_t stage, SamplerHandle sampler, TextureHandle texture) = 0;
    virtual void setUniform(UniformHandle uniform, const void* data, std::uint16_t count = 1) = 0;
    virtual void setState(const DrawState& state) = 0;

    virtual void dispatch(ProgramHandle program, std::uint32_t x, std::uint32_t y, std::uint32_t z) = 0;
    virtual void drawIndirect(ProgramHandle program, BufferHandle indirect, std::uint32_t start = 0,
                              std::uint32_t num = 1) = 0;
};

} // namespace vfx
