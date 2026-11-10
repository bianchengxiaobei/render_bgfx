#include "gfx/buffer.h"

#include "core/log.h"

#include <utility>

namespace hbg::gfx {

VertexLayout::VertexLayout() {
    m_layout.begin();
}

VertexLayout& VertexLayout::add(bgfx::Attrib::Enum attribute, uint8_t count, bgfx::AttribType::Enum type,
                                bool normalized) {
    if (m_ready) {
        HBG_LOGE("VertexLayout::add called after end()");
        return *this;
    }
    m_layout.add(attribute, count, type, normalized);
    return *this;
}

VertexLayout& VertexLayout::position() {
    return add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float);
}

VertexLayout& VertexLayout::normal() {
    return add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float);
}

VertexLayout& VertexLayout::tangent() {
    return add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float);
}

VertexLayout& VertexLayout::texCoord0() {
    return add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float);
}

VertexLayout& VertexLayout::texCoord1() {
    return add(bgfx::Attrib::TexCoord1, 2, bgfx::AttribType::Float);
}

VertexLayout& VertexLayout::color0() {
    return add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true);
}

VertexLayout& VertexLayout::end() {
    if (!m_ready) {
        m_layout.end();
        m_ready = true;
    }
    return *this;
}

VertexLayout VertexLayout::positionColor() {
    VertexLayout layout;
    layout.position().color0().end();
    return layout;
}

VertexLayout VertexLayout::positionNormalUv() {
    VertexLayout layout;
    layout.position().normal().texCoord0().end();
    return layout;
}

Buffer::~Buffer() {
    destroy();
}

Buffer::Buffer(Buffer&& other) noexcept
    : m_type(other.m_type), m_nativeIndex(other.m_nativeIndex), m_size(other.m_size), m_count(other.m_count),
      m_name(std::move(other.m_name)) {
    other.m_nativeIndex = bgfx::kInvalidHandle;
    other.m_size = 0;
    other.m_count = 0;
}

Buffer& Buffer::operator=(Buffer&& other) noexcept {
    if (this != &other) {
        destroy();
        m_type = other.m_type;
        m_nativeIndex = other.m_nativeIndex;
        m_size = other.m_size;
        m_count = other.m_count;
        m_name = std::move(other.m_name);
        other.m_nativeIndex = bgfx::kInvalidHandle;
        other.m_size = 0;
        other.m_count = 0;
    }
    return *this;
}

bool Buffer::create(const BufferDesc& desc) {
    if (valid()) {
        HBG_LOGE("Buffer::create called on an existing buffer '{}'", m_name);
        return false;
    }

    const bool needsData = desc.type == BufferType::Vertex || desc.type == BufferType::Index;
    if (needsData && (desc.data == nullptr || desc.size == 0)) {
        HBG_LOGE("Buffer::create '{}' requires data and size", desc.name);
        return false;
    }
    if ((desc.type == BufferType::Vertex || desc.type == BufferType::DynamicVertex) && desc.layout == nullptr) {
        HBG_LOGE("Buffer::create '{}' requires a vertex layout", desc.name);
        return false;
    }
    if (desc.layout != nullptr && !desc.layout->ready()) {
        HBG_LOGE("Buffer::create '{}' received a vertex layout that was not finalized", desc.name);
        return false;
    }

    const bgfx::Memory* memory = nullptr;
    if (desc.data != nullptr && desc.size > 0) {
        memory = bgfx::copy(desc.data, desc.size);
    }

    switch (desc.type) {
    case BufferType::Vertex: {
        const bgfx::VertexBufferHandle handle = bgfx::createVertexBuffer(memory, desc.layout->native(), desc.flags);
        m_nativeIndex = handle.idx;
        break;
    }
    case BufferType::Index: {
        const bgfx::IndexBufferHandle handle = bgfx::createIndexBuffer(memory, desc.flags);
        m_nativeIndex = handle.idx;
        break;
    }
    case BufferType::DynamicVertex: {
        const bgfx::DynamicVertexBufferHandle handle =
            memory != nullptr ? bgfx::createDynamicVertexBuffer(memory, desc.layout->native(), desc.flags)
                              : bgfx::createDynamicVertexBuffer(desc.count, desc.layout->native(), desc.flags);
        m_nativeIndex = handle.idx;
        break;
    }
    case BufferType::DynamicIndex: {
        const bgfx::DynamicIndexBufferHandle handle =
            memory != nullptr ? bgfx::createDynamicIndexBuffer(memory, desc.flags)
                              : bgfx::createDynamicIndexBuffer(desc.count, desc.flags);
        m_nativeIndex = handle.idx;
        break;
    }
    case BufferType::Indirect: {
        const bgfx::IndirectBufferHandle handle = bgfx::createIndirectBuffer(desc.count);
        m_nativeIndex = handle.idx;
        break;
    }
    }

    if (!valid()) {
        HBG_LOGE("failed to create buffer '{}'", desc.name);
        return false;
    }

    m_type = desc.type;
    m_size = desc.size;
    m_count = desc.count;
    m_name = desc.name;
    HBG_LOGD("created buffer '{}' ({} bytes, {} elements)", m_name, m_size, m_count);
    return true;
}

void Buffer::destroy() {
    if (valid()) {
        destroyNative(BufferNative{m_type, m_nativeIndex});
        m_nativeIndex = bgfx::kInvalidHandle;
    }
}

bool Buffer::valid() const {
    return m_nativeIndex != bgfx::kInvalidHandle;
}

BufferNative Buffer::takeNative() {
    const BufferNative native{m_type, m_nativeIndex};
    m_nativeIndex = bgfx::kInvalidHandle;
    return native;
}

void Buffer::destroyNative(BufferNative native) {
    if (native.index == bgfx::kInvalidHandle) {
        return;
    }

    switch (native.type) {
    case BufferType::Vertex:
        bgfx::destroy(bgfx::VertexBufferHandle{native.index});
        break;
    case BufferType::Index:
        bgfx::destroy(bgfx::IndexBufferHandle{native.index});
        break;
    case BufferType::DynamicVertex:
        bgfx::destroy(bgfx::DynamicVertexBufferHandle{native.index});
        break;
    case BufferType::DynamicIndex:
        bgfx::destroy(bgfx::DynamicIndexBufferHandle{native.index});
        break;
    case BufferType::Indirect:
        bgfx::destroy(bgfx::IndirectBufferHandle{native.index});
        break;
    }
}

} // namespace hbg::gfx
