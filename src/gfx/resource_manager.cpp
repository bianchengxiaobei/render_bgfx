#include "gfx/resource_manager.h"

#include "core/log.h"

#include <utility>

namespace hbg::gfx {

ResourceManager::~ResourceManager() {
    if (m_initialized) {
        shutdown();
    }
}

void ResourceManager::init() {
    if (m_initialized) {
        HBG_LOGW("ResourceManager::init called while already initialized");
        return;
    }
    m_hadLeaks = false;
    m_initialized = true;
}

void ResourceManager::shutdown() {
    if (!m_initialized) {
        return;
    }

    reportLeaks();
    destroyAll();
    flushPending(true);
    m_programCache.clear();
    m_uniformsByName.clear();
    m_initialized = false;
}

TextureHandle ResourceManager::createTexture(const TextureDesc& desc) {
    if (!m_initialized) {
        HBG_LOGE("ResourceManager::createTexture called before init");
        return {};
    }

    Texture texture;
    if (!texture.create(desc)) {
        return {};
    }
    return m_textures.add(std::move(texture));
}

void ResourceManager::destroyTexture(TextureHandle handle) {
    Texture* texture = m_textures.get(handle);
    if (texture == nullptr) {
        HBG_LOGW("ResourceManager::destroyTexture received an invalid handle");
        return;
    }

    const std::vector<FrameBufferHandle> attachedFrameBuffers = m_frameBufferCache.findContaining(handle);
    for (const FrameBufferHandle frameBufferHandle : attachedFrameBuffers) {
        if (FrameBuffer* frameBuffer = m_frameBuffers.get(frameBufferHandle); frameBuffer != nullptr) {
            frameBuffer->refCount = 1;
            destroyFrameBuffer(frameBufferHandle);
        }
    }

    const bgfx::TextureHandle native = texture->takeNative();
    m_textures.remove(handle);
    if (bgfx::isValid(native)) {
        deferDestroy([native]() { bgfx::destroy(native); });
    }
}

Texture* ResourceManager::texture(TextureHandle handle) {
    return m_textures.get(handle);
}

const Texture* ResourceManager::texture(TextureHandle handle) const {
    return m_textures.get(handle);
}

bgfx::TextureHandle ResourceManager::native(TextureHandle handle) const {
    const Texture* texture = m_textures.get(handle);
    return texture != nullptr ? texture->native() : bgfx::TextureHandle{bgfx::kInvalidHandle};
}

bool ResourceManager::updateTexture(TextureHandle handle, const void* data, uint32_t size, uint16_t width,
                                    uint16_t height, uint16_t pitch, uint16_t layer, uint8_t mip, uint16_t x,
                                    uint16_t y) {
    Texture* texture = m_textures.get(handle);
    if (texture == nullptr) {
        HBG_LOGW("ResourceManager::updateTexture received an invalid handle");
        return false;
    }
    return texture->update(data, size, width, height, pitch, layer, mip, x, y);
}

BufferHandle ResourceManager::createBuffer(const BufferDesc& desc) {
    if (!m_initialized) {
        HBG_LOGE("ResourceManager::createBuffer called before init");
        return {};
    }

    Buffer buffer;
    if (!buffer.create(desc)) {
        return {};
    }
    return m_buffers.add(std::move(buffer));
}

void ResourceManager::destroyBuffer(BufferHandle handle) {
    Buffer* buffer = m_buffers.get(handle);
    if (buffer == nullptr) {
        HBG_LOGW("ResourceManager::destroyBuffer received an invalid handle");
        return;
    }

    const BufferNative native = buffer->takeNative();
    m_buffers.remove(handle);
    if (native.index != bgfx::kInvalidHandle) {
        deferDestroy([native]() { Buffer::destroyNative(native); });
    }
}

Buffer* ResourceManager::buffer(BufferHandle handle) {
    return m_buffers.get(handle);
}

const Buffer* ResourceManager::buffer(BufferHandle handle) const {
    return m_buffers.get(handle);
}

UniformHandle ResourceManager::createUniform(const UniformDesc& desc) {
    if (!m_initialized) {
        HBG_LOGE("ResourceManager::createUniform called before init");
        return {};
    }

    const auto existing = m_uniformsByName.find(desc.name);
    if (existing != m_uniformsByName.end()) {
        Uniform* uniform = m_uniforms.get(existing->second);
        if (uniform != nullptr) {
            if (uniform->desc().type != desc.type || uniform->desc().num != desc.num) {
                HBG_LOGW("uniform '{}' already exists with a different type or element count", desc.name);
            }
            return existing->second;
        }
        m_uniformsByName.erase(existing);
    }

    Uniform uniform;
    if (!uniform.create(desc)) {
        return {};
    }

    const UniformHandle handle = m_uniforms.add(std::move(uniform));
    m_uniformsByName.emplace(desc.name, handle);
    return handle;
}

void ResourceManager::destroyUniform(UniformHandle handle) {
    Uniform* uniform = m_uniforms.get(handle);
    if (uniform == nullptr) {
        HBG_LOGW("ResourceManager::destroyUniform received an invalid handle");
        return;
    }

    const std::string name = uniform->desc().name;
    const bgfx::UniformHandle native = uniform->takeNative();
    m_uniforms.remove(handle);

    const auto iterator = m_uniformsByName.find(name);
    if (iterator != m_uniformsByName.end() && iterator->second == handle) {
        m_uniformsByName.erase(iterator);
    }

    if (bgfx::isValid(native)) {
        deferDestroy([native]() { bgfx::destroy(native); });
    }
}

Uniform* ResourceManager::uniform(UniformHandle handle) {
    return m_uniforms.get(handle);
}

const Uniform* ResourceManager::uniform(UniformHandle handle) const {
    return m_uniforms.get(handle);
}

bgfx::UniformHandle ResourceManager::native(UniformHandle handle) const {
    const Uniform* uniform = m_uniforms.get(handle);
    return uniform != nullptr ? uniform->native() : bgfx::UniformHandle{bgfx::kInvalidHandle};
}

ProgramHandle ResourceManager::createProgram(const void* vertexData, uint32_t vertexSize, const void* fragmentData,
                                             uint32_t fragmentSize, const std::string& name) {
    if (!m_initialized) {
        HBG_LOGE("ResourceManager::createProgram called before init");
        return {};
    }

    Program program;
    if (!program.create(vertexData, vertexSize, fragmentData, fragmentSize, name)) {
        return {};
    }
    return m_programs.add(std::move(program));
}

ProgramHandle ResourceManager::createProgramFromFiles(const std::string& vertexPath, const std::string& fragmentPath,
                                                      const std::string& name) {
    std::vector<uint8_t> vertexData;
    std::vector<uint8_t> fragmentData;
    if (!m_programCache.loadShader(vertexPath, vertexData) || !m_programCache.loadShader(fragmentPath, fragmentData)) {
        return {};
    }

    return createProgram(vertexData.data(), static_cast<uint32_t>(vertexData.size()), fragmentData.data(),
                         static_cast<uint32_t>(fragmentData.size()), name);
}

void ResourceManager::destroyProgram(ProgramHandle handle) {
    Program* program = m_programs.get(handle);
    if (program == nullptr) {
        HBG_LOGW("ResourceManager::destroyProgram received an invalid handle");
        return;
    }

    const bgfx::ProgramHandle native = program->takeNative();
    m_programs.remove(handle);
    if (bgfx::isValid(native)) {
        deferDestroy([native]() { bgfx::destroy(native); });
    }
}

Program* ResourceManager::program(ProgramHandle handle) {
    return m_programs.get(handle);
}

const Program* ResourceManager::program(ProgramHandle handle) const {
    return m_programs.get(handle);
}

FrameBufferHandle ResourceManager::createFrameBuffer(const FrameBufferKey& key, const std::string& name) {
    if (!m_initialized) {
        HBG_LOGE("ResourceManager::createFrameBuffer called before init");
        return {};
    }
    if (key.attachments.empty()) {
        HBG_LOGE("ResourceManager::createFrameBuffer requires at least one attachment");
        return {};
    }

    const FrameBufferHandle cached = m_frameBufferCache.find(key);
    if (cached.valid()) {
        FrameBuffer* frameBuffer = m_frameBuffers.get(cached);
        if (frameBuffer != nullptr) {
            ++frameBuffer->refCount;
            return cached;
        }
        m_frameBufferCache.erase(key);
    }

    std::vector<bgfx::TextureHandle> attachments;
    attachments.reserve(key.attachments.size());
    for (const TextureHandle textureHandle : key.attachments) {
        const Texture* texture = m_textures.get(textureHandle);
        if (texture == nullptr || !texture->valid()) {
            HBG_LOGE("ResourceManager::createFrameBuffer received an invalid texture attachment");
            return {};
        }
        attachments.push_back(texture->native());
    }

    FrameBuffer frameBuffer;
    if (!frameBuffer.create(key, attachments, name)) {
        return {};
    }
    frameBuffer.refCount = 1;

    const FrameBufferHandle handle = m_frameBuffers.add(std::move(frameBuffer));
    m_frameBufferCache.insert(key, handle);
    return handle;
}

void ResourceManager::destroyFrameBuffer(FrameBufferHandle handle) {
    FrameBuffer* frameBuffer = m_frameBuffers.get(handle);
    if (frameBuffer == nullptr) {
        HBG_LOGW("ResourceManager::destroyFrameBuffer received an invalid handle");
        return;
    }
    if (frameBuffer->refCount > 1) {
        --frameBuffer->refCount;
        return;
    }

    const FrameBufferKey key = frameBuffer->key;
    const bgfx::FrameBufferHandle native = frameBuffer->takeNative();
    m_frameBuffers.remove(handle);
    m_frameBufferCache.erase(key);

    if (bgfx::isValid(native)) {
        deferDestroy([native]() { bgfx::destroy(native); });
    }
}

FrameBuffer* ResourceManager::frameBuffer(FrameBufferHandle handle) {
    return m_frameBuffers.get(handle);
}

const FrameBuffer* ResourceManager::frameBuffer(FrameBufferHandle handle) const {
    return m_frameBuffers.get(handle);
}

void ResourceManager::endFrame() {
    for (PendingDestroy& pending : m_pending) {
        if (pending.framesRemaining > 0) {
            --pending.framesRemaining;
        }
    }
    flushPending(false);
}

void ResourceManager::flushPending(bool force) {
    for (std::size_t index = 0; index < m_pending.size();) {
        PendingDestroy& pending = m_pending[index];
        if (force || pending.framesRemaining == 0) {
            pending.destroy();
            m_pending.erase(m_pending.begin() + static_cast<std::ptrdiff_t>(index));
        } else {
            ++index;
        }
    }
}

std::size_t ResourceManager::liveResourceCount() const {
    return m_textures.size() + m_buffers.size() + m_uniforms.size() + m_programs.size() + m_frameBuffers.size();
}

void ResourceManager::deferDestroy(std::function<void()> destroy) {
    m_pending.push_back(PendingDestroy{std::move(destroy), 2});
}

void ResourceManager::destroyAll() {
    std::vector<FrameBufferHandle> frameBuffers;
    m_frameBuffers.forEach([&frameBuffers](FrameBufferHandle handle, FrameBuffer&) { frameBuffers.push_back(handle); });
    for (const FrameBufferHandle handle : frameBuffers) {
        FrameBuffer* frameBuffer = m_frameBuffers.get(handle);
        if (frameBuffer != nullptr) {
            frameBuffer->refCount = 1;
            destroyFrameBuffer(handle);
        }
    }
    m_frameBufferCache.clear();

    std::vector<ProgramHandle> programs;
    m_programs.forEach([&programs](ProgramHandle handle, Program&) { programs.push_back(handle); });
    for (const ProgramHandle handle : programs) {
        destroyProgram(handle);
    }

    std::vector<BufferHandle> buffers;
    m_buffers.forEach([&buffers](BufferHandle handle, Buffer&) { buffers.push_back(handle); });
    for (const BufferHandle handle : buffers) {
        destroyBuffer(handle);
    }

    std::vector<UniformHandle> uniforms;
    m_uniforms.forEach([&uniforms](UniformHandle handle, Uniform&) { uniforms.push_back(handle); });
    for (const UniformHandle handle : uniforms) {
        destroyUniform(handle);
    }

    std::vector<TextureHandle> textures;
    m_textures.forEach([&textures](TextureHandle handle, Texture&) { textures.push_back(handle); });
    for (const TextureHandle handle : textures) {
        destroyTexture(handle);
    }
}

void ResourceManager::reportLeaks() {
    if (liveResourceCount() == 0) {
        return;
    }

    m_hadLeaks = true;
    HBG_LOGW("ResourceManager shutdown with live resources: textures={} buffers={} uniforms={} programs={} "
             "framebuffers={}",
             m_textures.size(), m_buffers.size(), m_uniforms.size(), m_programs.size(), m_frameBuffers.size());
}

} // namespace hbg::gfx
