#pragma once

#include "core/handle.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace hbg::gfx {

enum class BufferType : uint8_t {
    Vertex,
    Index,
    DynamicVertex,
    DynamicIndex,
    Storage,
    StorageVertex,
    Indirect,
};

struct BufferTag {};
using BufferHandle = Handle<BufferTag>;

class VertexLayout {
  public:
    VertexLayout();

    VertexLayout& add(bgfx::Attrib::Enum attribute, uint8_t count, bgfx::AttribType::Enum type,
                      bool normalized = false);
    VertexLayout& position();
    VertexLayout& normal();
    VertexLayout& tangent();
    VertexLayout& texCoord0();
    VertexLayout& texCoord1();
    VertexLayout& color0();
    VertexLayout& end();

    const bgfx::VertexLayout& native() const { return m_layout; }
    uint16_t stride() const { return m_layout.getStride(); }
    bool ready() const { return m_ready; }

    static VertexLayout positionColor();
    static VertexLayout positionNormalUv();

  private:
    bgfx::VertexLayout m_layout;
    bool m_ready = false;
};

struct BufferDesc {
    BufferType type = BufferType::Vertex;
    const void* data = nullptr;
    uint32_t size = 0;
    uint32_t count = 0;
    const VertexLayout* layout = nullptr;
    uint16_t flags = BGFX_BUFFER_NONE;
    std::string name;
};

struct BufferNative {
    BufferType type = BufferType::Vertex;
    uint16_t index = bgfx::kInvalidHandle;
};

class Buffer {
  public:
    Buffer() = default;
    ~Buffer();

    Buffer(const Buffer&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer(Buffer&& other) noexcept;
    Buffer& operator=(Buffer&& other) noexcept;

    bool create(const BufferDesc& desc);
    bool update(const void* data, uint32_t size, uint32_t offset = 0);
    void destroy();

    bool valid() const;
    BufferType type() const { return m_type; }
    uint16_t nativeIndex() const { return m_nativeIndex; }
    uint32_t size() const { return m_size; }
    uint32_t count() const { return m_count; }
    uint32_t stride() const { return m_stride; }
    uint16_t flags() const { return m_flags; }
    const std::string& name() const { return m_name; }

    bool storage() const {
        return m_type == BufferType::Storage || m_type == BufferType::StorageVertex;
    }

    BufferNative takeNative();

    static void destroyNative(BufferNative native);

  private:
    BufferType m_type = BufferType::Vertex;
    uint16_t m_nativeIndex = bgfx::kInvalidHandle;
    uint32_t m_size = 0;
    uint32_t m_count = 0;
    uint32_t m_stride = 0;
    uint16_t m_flags = BGFX_BUFFER_NONE;
    std::string m_name;
};

} // namespace hbg::gfx
