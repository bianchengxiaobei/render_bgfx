#pragma once

#include "gfx/buffer.h"
#include "gfx/framebuffer_cache.h"
#include "gfx/instance_data.h"
#include "gfx/program_cache.h"
#include "gfx/texture.h"
#include "gfx/uniform.h"

#include <bgfx/bgfx.h>

#include <cstdint>

namespace hbg::gfx {

class ResourceManager;

class Encoder {
  public:
    explicit Encoder(ResourceManager& resources) : m_resources(resources) {}

    void setMarker(const char* name);
    void setViewName(bgfx::ViewId view, const char* name);
    void setViewRect(bgfx::ViewId view, int16_t x, int16_t y, uint16_t width, uint16_t height);
    void setViewScissor(bgfx::ViewId view, uint16_t x, uint16_t y, uint16_t width, uint16_t height);
    void setViewClear(bgfx::ViewId view, uint16_t flags, uint32_t rgba = 0x000000ff, float depth = 1.0f,
                      uint8_t stencil = 0);
    void setViewMode(bgfx::ViewId view, bgfx::ViewMode::Enum mode);
    void setViewFrameBuffer(bgfx::ViewId view, FrameBufferHandle frameBuffer);
    void setViewTransform(bgfx::ViewId view, const float* viewMatrix, const float* projectionMatrix);
    void setViewDepthBias(bgfx::ViewId view, int32_t constant = 0, float slopeScale = 0.0f, float clamp = 0.0f);

    void setState(uint64_t state, uint32_t rgba = 0);
    void setStencil(uint32_t front, uint32_t back = BGFX_STENCIL_NONE);
    void setTransform(const float* modelMatrix);
    void setUniform(UniformHandle uniform, const void* data, uint16_t num = UINT16_MAX);
    void setTexture(uint8_t stage, UniformHandle sampler, TextureHandle texture, uint32_t flags = UINT32_MAX);
    void setTextureMip(uint8_t stage, UniformHandle sampler, TextureHandle texture, uint8_t firstMip, uint8_t numMips,
                       uint32_t flags = UINT32_MAX);
    void setImage(uint8_t stage, TextureHandle texture, uint8_t mip, bgfx::Access::Enum access,
                  TextureFormat format = TextureFormat::Count);
    void setImage(uint8_t stage, TextureHandle texture, uint16_t firstLayer, uint16_t numLayers, uint8_t mip,
                  bgfx::Access::Enum access, TextureFormat format = TextureFormat::Count);

    void setVertexBuffer(uint8_t stream, BufferHandle buffer);
    void setVertexBuffer(uint8_t stream, BufferHandle buffer, uint32_t startVertex, uint32_t numVertices);
    void setIndexBuffer(BufferHandle buffer);
    void setIndexBuffer(BufferHandle buffer, uint32_t firstIndex, uint32_t numIndices);
    void setInstanceDataBuffer(const InstanceData& instances);
    void setInstanceDataBuffer(BufferHandle buffer, uint32_t startVertex, uint32_t numInstances);
    void setStorageBuffer(uint8_t stage, BufferHandle buffer, bgfx::Access::Enum access, uint32_t offset = 0,
                          uint32_t size = UINT32_MAX);

    void submit(bgfx::ViewId view, ProgramHandle program, uint32_t depth = 0, uint8_t flags = BGFX_DISCARD_ALL);
    void submitIndirect(bgfx::ViewId view, ProgramHandle program, BufferHandle indirect, uint32_t start = 0,
                        uint32_t num = 1, uint32_t depth = 0, uint8_t flags = BGFX_DISCARD_ALL);
    void dispatch(bgfx::ViewId view, ProgramHandle program, uint32_t numX = 1, uint32_t numY = 1, uint32_t numZ = 1,
                  uint8_t flags = BGFX_DISCARD_ALL);
    void dispatchIndirect(bgfx::ViewId view, ProgramHandle program, BufferHandle indirect, uint32_t start = 0,
                          uint32_t num = 1, uint8_t flags = BGFX_DISCARD_ALL);
    void touch(bgfx::ViewId view);
    void discard(uint8_t flags = BGFX_DISCARD_ALL);

  private:
    ResourceManager& m_resources;
};

} // namespace hbg::gfx
