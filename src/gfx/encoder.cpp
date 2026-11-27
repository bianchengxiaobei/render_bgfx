#include "gfx/encoder.h"

#include "core/log.h"
#include "gfx/resource_manager.h"

namespace hbg::gfx {

void Encoder::setMarker(const char* name) {
    bgfx::setMarker(name);
}

void Encoder::setViewName(bgfx::ViewId view, const char* name) {
    bgfx::setViewName(view, name);
}

void Encoder::setViewRect(bgfx::ViewId view, int16_t x, int16_t y, uint16_t width, uint16_t height) {
    bgfx::setViewRect(view, x, y, width, height);
}

void Encoder::setViewScissor(bgfx::ViewId view, uint16_t x, uint16_t y, uint16_t width, uint16_t height) {
    bgfx::setViewScissor(view, x, y, width, height);
}

void Encoder::setViewClear(bgfx::ViewId view, uint16_t flags, uint32_t rgba, float depth, uint8_t stencil) {
    bgfx::setViewClear(view, flags, rgba, depth, stencil);
}

void Encoder::setViewMode(bgfx::ViewId view, bgfx::ViewMode::Enum mode) {
    bgfx::setViewMode(view, mode);
}

void Encoder::setViewFrameBuffer(bgfx::ViewId view, FrameBufferHandle frameBuffer) {
    if (!frameBuffer.valid()) {
        bgfx::setViewFrameBuffer(view, BGFX_INVALID_HANDLE);
        return;
    }

    const FrameBuffer* resource = m_resources.frameBuffer(frameBuffer);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::setViewFrameBuffer received an invalid frame buffer");
        return;
    }
    bgfx::setViewFrameBuffer(view, resource->native());
}

void Encoder::setViewTransform(bgfx::ViewId view, const float* viewMatrix, const float* projectionMatrix) {
    bgfx::setViewTransform(view, viewMatrix, projectionMatrix);
}

void Encoder::setViewDepthBias(bgfx::ViewId view, int32_t constant, float slopeScale, float clamp) {
    bgfx::setViewDepthBias(view, constant, slopeScale, clamp);
}

void Encoder::setState(uint64_t state, uint32_t rgba) {
    bgfx::setState(state, rgba);
}

void Encoder::setStencil(uint32_t front, uint32_t back) {
    bgfx::setStencil(front, back);
}

void Encoder::setTransform(const float* modelMatrix) {
    bgfx::setTransform(modelMatrix);
}

void Encoder::setUniform(UniformHandle uniform, const void* data, uint16_t num) {
    const Uniform* resource = m_resources.uniform(uniform);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::setUniform received an invalid uniform");
        return;
    }
    if (resource->desc().type == UniformType::Sampler) {
        HBG_LOGE("Encoder::setUniform cannot upload sampler uniform '{}'", resource->desc().name);
        return;
    }
    bgfx::setUniform(resource->native(), data, num);
}

void Encoder::setTexture(uint8_t stage, UniformHandle sampler, TextureHandle texture, uint32_t flags) {
    const Uniform* samplerResource = m_resources.uniform(sampler);
    const Texture* textureResource = m_resources.texture(texture);
    if (samplerResource == nullptr || !samplerResource->valid()) {
        HBG_LOGE("Encoder::setTexture received an invalid sampler uniform");
        return;
    }
    if (samplerResource->desc().type != UniformType::Sampler) {
        HBG_LOGE("Encoder::setTexture received non-sampler uniform '{}'", samplerResource->desc().name);
        return;
    }
    if (textureResource == nullptr || !textureResource->valid()) {
        HBG_LOGE("Encoder::setTexture received an invalid texture");
        return;
    }

    bgfx::setTexture(stage, samplerResource->native(), textureResource->native(), flags);
}

void Encoder::setTextureMip(uint8_t stage, UniformHandle sampler, TextureHandle texture, uint8_t firstMip,
                            uint8_t numMips, uint32_t flags) {
    const Uniform* samplerResource = m_resources.uniform(sampler);
    const Texture* textureResource = m_resources.texture(texture);
    if (samplerResource == nullptr || !samplerResource->valid()) {
        HBG_LOGE("Encoder::setTextureMip received an invalid sampler uniform");
        return;
    }
    if (samplerResource->desc().type != UniformType::Sampler) {
        HBG_LOGE("Encoder::setTextureMip received non-sampler uniform '{}'", samplerResource->desc().name);
        return;
    }
    if (textureResource == nullptr || !textureResource->valid() || firstMip >= textureResource->desc().mipCount ||
        numMips == 0 ||
        static_cast<uint16_t>(firstMip) + static_cast<uint16_t>(numMips) > textureResource->desc().mipCount) {
        HBG_LOGE("Encoder::setTextureMip received an invalid texture or mip range");
        return;
    }

    bgfx::setTexture(stage, samplerResource->native(), textureResource->native(), 0, 1, firstMip, numMips, flags);
}

void Encoder::setImage(uint8_t stage, TextureHandle texture, uint8_t mip, bgfx::Access::Enum access,
                       TextureFormat format) {
    const Texture* textureResource = m_resources.texture(texture);
    if (textureResource == nullptr || !textureResource->valid() || mip >= textureResource->desc().mipCount) {
        HBG_LOGE("Encoder::setImage received an invalid texture or mip");
        return;
    }
    if (!textureResource->desc().computeWrite) {
        HBG_LOGW("Encoder::setImage texture '{}' was not created with computeWrite", textureResource->desc().name);
    }

    const bgfx::TextureFormat::Enum imageFormat =
        format == TextureFormat::Count ? toBgfx(textureResource->desc().format) : toBgfx(format);
    if (imageFormat == bgfx::TextureFormat::Count) {
        HBG_LOGE("Encoder::setImage received an unsupported image format");
        return;
    }
    bgfx::setImage(stage, textureResource->native(), mip, access, imageFormat);
}

void Encoder::setImage(uint8_t stage, TextureHandle texture, uint16_t firstLayer, uint16_t numLayers, uint8_t mip,
                       bgfx::Access::Enum access, TextureFormat format) {
    const Texture* textureResource = m_resources.texture(texture);
    if (textureResource == nullptr || !textureResource->valid() || mip >= textureResource->desc().mipCount ||
        firstLayer >= textureResource->desc().layers || numLayers == 0 ||
        static_cast<uint32_t>(firstLayer) + numLayers > textureResource->desc().layers) {
        HBG_LOGE("Encoder::setImage received an invalid texture, layer range, or mip");
        return;
    }
    if (!textureResource->desc().computeWrite) {
        HBG_LOGW("Encoder::setImage texture '{}' was not created with computeWrite", textureResource->desc().name);
    }

    const bgfx::TextureFormat::Enum imageFormat =
        format == TextureFormat::Count ? toBgfx(textureResource->desc().format) : toBgfx(format);
    if (imageFormat == bgfx::TextureFormat::Count) {
        HBG_LOGE("Encoder::setImage received an unsupported image format");
        return;
    }
    bgfx::setImage(stage, textureResource->native(), firstLayer, numLayers, mip, access, imageFormat);
}

void Encoder::setVertexBuffer(uint8_t stream, BufferHandle buffer) {
    const Buffer* resource = m_resources.buffer(buffer);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::setVertexBuffer received an invalid buffer");
        return;
    }

    switch (resource->type()) {
    case BufferType::Vertex:
        bgfx::setVertexBuffer(stream, bgfx::VertexBufferHandle{resource->nativeIndex()});
        break;
    case BufferType::DynamicVertex:
    case BufferType::StorageVertex:
        bgfx::setVertexBuffer(stream, bgfx::DynamicVertexBufferHandle{resource->nativeIndex()});
        break;
    default:
        HBG_LOGE("Encoder::setVertexBuffer received a non-vertex buffer");
        break;
    }
}

void Encoder::setVertexBuffer(uint8_t stream, BufferHandle buffer, uint32_t startVertex, uint32_t numVertices) {
    const Buffer* resource = m_resources.buffer(buffer);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::setVertexBuffer received an invalid buffer");
        return;
    }

    switch (resource->type()) {
    case BufferType::Vertex:
        bgfx::setVertexBuffer(stream, bgfx::VertexBufferHandle{resource->nativeIndex()}, startVertex, numVertices);
        break;
    case BufferType::DynamicVertex:
    case BufferType::StorageVertex:
        bgfx::setVertexBuffer(stream, bgfx::DynamicVertexBufferHandle{resource->nativeIndex()}, startVertex,
                              numVertices);
        break;
    default:
        HBG_LOGE("Encoder::setVertexBuffer received a non-vertex buffer");
        break;
    }
}

void Encoder::setIndexBuffer(BufferHandle buffer) {
    const Buffer* resource = m_resources.buffer(buffer);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::setIndexBuffer received an invalid buffer");
        return;
    }

    switch (resource->type()) {
    case BufferType::Index:
        bgfx::setIndexBuffer(bgfx::IndexBufferHandle{resource->nativeIndex()});
        break;
    case BufferType::DynamicIndex:
        bgfx::setIndexBuffer(bgfx::DynamicIndexBufferHandle{resource->nativeIndex()});
        break;
    default:
        HBG_LOGE("Encoder::setIndexBuffer received a non-index buffer");
        break;
    }
}

void Encoder::setIndexBuffer(BufferHandle buffer, uint32_t firstIndex, uint32_t numIndices) {
    const Buffer* resource = m_resources.buffer(buffer);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::setIndexBuffer received an invalid buffer");
        return;
    }

    switch (resource->type()) {
    case BufferType::Index:
        bgfx::setIndexBuffer(bgfx::IndexBufferHandle{resource->nativeIndex()}, firstIndex, numIndices);
        break;
    case BufferType::DynamicIndex:
        bgfx::setIndexBuffer(bgfx::DynamicIndexBufferHandle{resource->nativeIndex()}, firstIndex, numIndices);
        break;
    default:
        HBG_LOGE("Encoder::setIndexBuffer received a non-index buffer");
        break;
    }
}

void Encoder::setInstanceDataBuffer(const InstanceData& instances) {
    if (!instances.valid()) {
        return;
    }
    bgfx::setInstanceDataBuffer(instances.native());
}

void Encoder::setInstanceDataBuffer(BufferHandle buffer, uint32_t startVertex, uint32_t numInstances) {
    const Buffer* resource = m_resources.buffer(buffer);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::setInstanceDataBuffer received an invalid buffer");
        return;
    }
    if (resource->type() != BufferType::DynamicVertex && resource->type() != BufferType::StorageVertex) {
        HBG_LOGE("Encoder::setInstanceDataBuffer received a non-vertex buffer");
        return;
    }
    bgfx::setInstanceDataBuffer(bgfx::DynamicVertexBufferHandle{resource->nativeIndex()}, startVertex, numInstances);
}

void Encoder::setStorageBuffer(uint8_t stage, BufferHandle buffer, bgfx::Access::Enum access, uint32_t offset,
                               uint32_t size) {
    const Buffer* resource = m_resources.buffer(buffer);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::setStorageBuffer received an invalid buffer");
        return;
    }
    if (offset % 256u != 0) {
        HBG_LOGE("Encoder::setStorageBuffer '{}' offset {} is not 256-byte aligned", resource->name(), offset);
        return;
    }
    if (stage >= bgfx::getCaps()->limits.maxComputeBindings) {
        HBG_LOGE("Encoder::setStorageBuffer stage {} exceeds the compute binding limit", stage);
        return;
    }

    switch (resource->type()) {
    case BufferType::Storage:
        bgfx::setBuffer(stage, bgfx::DynamicIndexBufferHandle{resource->nativeIndex()}, access, offset, size);
        break;
    case BufferType::StorageVertex:
        bgfx::setBuffer(stage, bgfx::DynamicVertexBufferHandle{resource->nativeIndex()}, access, offset, size);
        break;
    case BufferType::Indirect:
        bgfx::setBuffer(stage, bgfx::IndirectBufferHandle{resource->nativeIndex()}, access);
        break;
    default:
        HBG_LOGE("Encoder::setStorageBuffer '{}' is not a storage or indirect buffer", resource->name());
        break;
    }
}

void Encoder::submit(bgfx::ViewId view, ProgramHandle program, uint32_t depth, uint8_t flags) {
    const Program* resource = m_resources.program(program);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::submit received an invalid program");
        return;
    }
    bgfx::submit(view, resource->native(), depth, flags);
}

void Encoder::submitIndirect(bgfx::ViewId view, ProgramHandle program, BufferHandle indirect, uint32_t start,
                             uint32_t num, uint32_t depth, uint8_t flags) {
    const Program* programResource = m_resources.program(program);
    const Buffer* indirectResource = m_resources.buffer(indirect);
    if (programResource == nullptr || !programResource->valid()) {
        HBG_LOGE("Encoder::submitIndirect received an invalid program");
        return;
    }
    if (indirectResource == nullptr || !indirectResource->valid() ||
        (indirectResource->type() != BufferType::Indirect && indirectResource->type() != BufferType::Vertex)) {
        HBG_LOGE("Encoder::submitIndirect received an invalid indirect buffer");
        return;
    }
    bgfx::submit(view, programResource->native(), bgfx::IndirectBufferHandle{indirectResource->nativeIndex()}, start,
                 num, depth, flags);
}

void Encoder::dispatch(bgfx::ViewId view, ProgramHandle program, uint32_t numX, uint32_t numY, uint32_t numZ,
                       uint8_t flags) {
    const Program* resource = m_resources.program(program);
    if (resource == nullptr || !resource->valid()) {
        HBG_LOGE("Encoder::dispatch received an invalid program");
        return;
    }
    bgfx::dispatch(view, resource->native(), numX, numY, numZ, flags);
}

void Encoder::dispatchIndirect(bgfx::ViewId view, ProgramHandle program, BufferHandle indirect, uint32_t start,
                               uint32_t num, uint8_t flags) {
    const Program* programResource = m_resources.program(program);
    const Buffer* indirectResource = m_resources.buffer(indirect);
    if (programResource == nullptr || !programResource->valid()) {
        HBG_LOGE("Encoder::dispatchIndirect received an invalid program");
        return;
    }
    if (indirectResource == nullptr || !indirectResource->valid() ||
        indirectResource->type() != BufferType::Indirect) {
        HBG_LOGE("Encoder::dispatchIndirect received an invalid indirect buffer");
        return;
    }
    bgfx::dispatch(view, programResource->native(), bgfx::IndirectBufferHandle{indirectResource->nativeIndex()}, start,
                   num, flags);
}

void Encoder::touch(bgfx::ViewId view) {
    bgfx::touch(view);
}

void Encoder::discard(uint8_t flags) {
    bgfx::discard(flags);
}

} // namespace hbg::gfx
