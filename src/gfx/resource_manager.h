#pragma once

#include "core/pool.h"
#include "gfx/buffer.h"
#include "gfx/framebuffer_cache.h"
#include "gfx/program_cache.h"
#include "gfx/texture.h"
#include "gfx/uniform.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace hbg::gfx {

class Encoder;

class ResourceManager {
  public:
    ResourceManager() = default;
    ~ResourceManager();

    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;

    void init();
    void shutdown();
    bool initialized() const { return m_initialized; }

    TextureHandle createTexture(const TextureDesc& desc);
    void destroyTexture(TextureHandle handle);
    Texture* texture(TextureHandle handle);
    const Texture* texture(TextureHandle handle) const;
    bgfx::TextureHandle native(TextureHandle handle) const;
    bool updateTexture(TextureHandle handle, const void* data, uint32_t size, uint16_t width, uint16_t height,
                       uint16_t pitch, uint16_t layer = 0, uint8_t mip = 0, uint16_t x = 0, uint16_t y = 0);

    BufferHandle createBuffer(const BufferDesc& desc);
    bool updateBuffer(BufferHandle handle, const void* data, uint32_t size, uint32_t offset = 0);
    void destroyBuffer(BufferHandle handle);
    Buffer* buffer(BufferHandle handle);
    const Buffer* buffer(BufferHandle handle) const;
    bgfx::BufferHandle native(BufferHandle handle) const;

    UniformHandle createUniform(const UniformDesc& desc);
    UniformHandle uniformHandle(std::string_view name) const;
    void destroyUniform(UniformHandle handle);
    Uniform* uniform(UniformHandle handle);
    const Uniform* uniform(UniformHandle handle) const;
    bgfx::UniformHandle native(UniformHandle handle) const;

    ProgramHandle createProgram(const void* vertexData, uint32_t vertexSize, const void* fragmentData,
                                uint32_t fragmentSize, const std::string& name = {});
    ProgramHandle createProgramFromFiles(const std::string& vertexPath, const std::string& fragmentPath,
                                         const std::string& name = {});
    ProgramHandle createComputeProgram(const void* computeData, uint32_t computeSize, const std::string& name = {});
    ProgramHandle createComputeProgramFromFiles(const std::string& computePath, const std::string& name = {});
    void destroyProgram(ProgramHandle handle);
    Program* program(ProgramHandle handle);
    const Program* program(ProgramHandle handle) const;

    FrameBufferHandle createFrameBuffer(const FrameBufferKey& key, const std::string& name = {});
    void destroyFrameBuffer(FrameBufferHandle handle);
    FrameBuffer* frameBuffer(FrameBufferHandle handle);
    const FrameBuffer* frameBuffer(FrameBufferHandle handle) const;

    void endFrame();
    void flushPending(bool force = false);

    std::size_t liveResourceCount() const;
    bool hadLeaksAtShutdown() const { return m_hadLeaks; }

  private:
    struct PendingDestroy {
        std::function<void()> destroy;
        uint32_t framesRemaining = 0;
    };

    void deferDestroy(std::function<void()> destroy);
    void destroyAll();
    void reportLeaks();

    bool m_initialized = false;
    bool m_hadLeaks = false;

    Pool<TextureTag, Texture> m_textures;
    Pool<BufferTag, Buffer> m_buffers;
    Pool<UniformTag, Uniform> m_uniforms;
    Pool<ProgramTag, Program> m_programs;
    Pool<FrameBufferTag, FrameBuffer> m_frameBuffers;

    std::unordered_map<std::string, UniformHandle> m_uniformsByName;
    FrameBufferCache m_frameBufferCache;
    ProgramCache m_programCache;
    std::vector<PendingDestroy> m_pending;
};

} // namespace hbg::gfx
