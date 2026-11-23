#pragma once

#include "core/handle.h"
#include "gfx/texture.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hbg::gfx {

struct FrameBufferTag {};
using FrameBufferHandle = Handle<FrameBufferTag>;

inline constexpr uint8_t kFrameBufferAttachmentReadOnlyDepth = 1u << 0;

struct FrameBufferKey {
    std::vector<TextureHandle> attachments;
    std::vector<uint16_t> mips;
    std::vector<uint8_t> attachmentFlags{};

    friend bool operator==(const FrameBufferKey&, const FrameBufferKey&) = default;
};

struct FrameBufferKeyHash {
    std::size_t operator()(const FrameBufferKey& key) const noexcept {
        std::size_t seed = key.attachments.size();
        for (std::size_t index = 0; index < key.attachments.size(); ++index) {
            seed ^= HandleHash{}(key.attachments[index]) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            const uint16_t mip = index < key.mips.size() ? key.mips[index] : 0;
            seed ^= static_cast<std::size_t>(mip) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            const uint8_t flags = index < key.attachmentFlags.size() ? key.attachmentFlags[index] : 0;
            seed ^= static_cast<std::size_t>(flags) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

class FrameBuffer {
  public:
    FrameBuffer() = default;
    ~FrameBuffer();

    FrameBuffer(const FrameBuffer&) = delete;
    FrameBuffer& operator=(const FrameBuffer&) = delete;
    FrameBuffer(FrameBuffer&& other) noexcept;
    FrameBuffer& operator=(FrameBuffer&& other) noexcept;

    bool create(const FrameBufferKey& key, const std::vector<bgfx::TextureHandle>& nativeAttachments,
                const std::string& name = {});
    void destroy();

    bool valid() const;
    bgfx::FrameBufferHandle native() const { return m_native; }
    bgfx::FrameBufferHandle takeNative();
    const std::string& name() const { return m_name; }

    FrameBufferKey key;
    uint32_t refCount = 1;

  private:
    bgfx::FrameBufferHandle m_native{bgfx::kInvalidHandle};
    std::string m_name;
};

class FrameBufferCache {
  public:
    FrameBufferHandle find(const FrameBufferKey& key) const;
    std::vector<FrameBufferHandle> findContaining(TextureHandle texture) const;
    void insert(const FrameBufferKey& key, FrameBufferHandle handle);
    void erase(const FrameBufferKey& key);
    void clear();
    std::size_t size() const noexcept { return m_entries.size(); }

  private:
    std::unordered_map<FrameBufferKey, FrameBufferHandle, FrameBufferKeyHash> m_entries;
};

} // namespace hbg::gfx
