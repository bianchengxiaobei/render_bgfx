#include "gfx/framebuffer_cache.h"

#include "core/log.h"

#include <algorithm>

#include <utility>

namespace hbg::gfx {

FrameBuffer::~FrameBuffer() {
    destroy();
}

FrameBuffer::FrameBuffer(FrameBuffer&& other) noexcept
    : m_native(other.m_native), m_name(std::move(other.m_name)), key(std::move(other.key)), refCount(other.refCount) {
    other.m_native = bgfx::FrameBufferHandle{bgfx::kInvalidHandle};
    other.refCount = 0;
}

FrameBuffer& FrameBuffer::operator=(FrameBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        m_native = other.m_native;
        m_name = std::move(other.m_name);
        key = std::move(other.key);
        refCount = other.refCount;
        other.m_native = bgfx::FrameBufferHandle{bgfx::kInvalidHandle};
        other.refCount = 0;
    }
    return *this;
}

bool FrameBuffer::create(const FrameBufferKey& frameBufferKey,
                         const std::vector<bgfx::TextureHandle>& nativeAttachments, const std::string& name) {
    if (valid()) {
        HBG_LOGE("FrameBuffer::create called on an existing frame buffer '{}'", m_name);
        return false;
    }
    if (nativeAttachments.empty()) {
        HBG_LOGE("FrameBuffer::create '{}' requires at least one attachment", name);
        return false;
    }
    if (frameBufferKey.attachments.size() != nativeAttachments.size()) {
        HBG_LOGE("FrameBuffer::create '{}' received mismatched attachment counts", name);
        return false;
    }

    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps != nullptr && nativeAttachments.size() > static_cast<std::size_t>(caps->limits.maxFBAttachments)) {
        HBG_LOGE("FrameBuffer::create '{}' has {} attachments, backend supports {}", name, nativeAttachments.size(),
                 caps->limits.maxFBAttachments);
        return false;
    }

    for (const bgfx::TextureHandle attachment : nativeAttachments) {
        if (!bgfx::isValid(attachment)) {
            HBG_LOGE("FrameBuffer::create '{}' received an invalid attachment", name);
            return false;
        }
    }

    const auto mipAt = [&frameBufferKey](std::size_t index) {
        return index < frameBufferKey.mips.size() ? frameBufferKey.mips[index] : static_cast<uint16_t>(0);
    };

    const uint8_t count = static_cast<uint8_t>(nativeAttachments.size());
    // Always use explicit attachments. The TextureHandle overload lets bgfx
    // auto-generate mips for non-depth attachments that own a mip chain; that
    // is invalid on formats without BGFX_CAPS_FORMAT_TEXTURE_MIP_AUTOGEN.
    // Explicit attachments also preserve the requested mip for downsample passes.
    std::vector<bgfx::Attachment> attachments;
    attachments.reserve(nativeAttachments.size());
    for (std::size_t index = 0; index < nativeAttachments.size(); ++index) {
        bgfx::Attachment attachment;
        attachment.init(nativeAttachments[index], bgfx::Access::Write, 0, 1, mipAt(index),
                        BGFX_ATTACHMENT_NONE);
        attachments.push_back(attachment);
    }
    m_native = bgfx::createFrameBuffer(count, attachments.data(), false);
    if (!valid()) {
        HBG_LOGE("failed to create frame buffer '{}'", name);
        return false;
    }

    key = frameBufferKey;
    m_name = name;
    if (!m_name.empty()) {
        bgfx::setName(m_native, m_name.c_str());
    }
    return true;
}

void FrameBuffer::destroy() {
    if (valid()) {
        bgfx::destroy(m_native);
        m_native = bgfx::FrameBufferHandle{bgfx::kInvalidHandle};
    }
}

bool FrameBuffer::valid() const {
    return bgfx::isValid(m_native);
}

bgfx::FrameBufferHandle FrameBuffer::takeNative() {
    const bgfx::FrameBufferHandle native = m_native;
    m_native = bgfx::FrameBufferHandle{bgfx::kInvalidHandle};
    return native;
}

FrameBufferHandle FrameBufferCache::find(const FrameBufferKey& key) const {
    const auto iterator = m_entries.find(key);
    return iterator != m_entries.end() ? iterator->second : FrameBufferHandle{};
}

std::vector<FrameBufferHandle> FrameBufferCache::findContaining(TextureHandle texture) const {
    std::vector<FrameBufferHandle> result;
    for (const auto& [key, handle] : m_entries) {
        if (std::find(key.attachments.begin(), key.attachments.end(), texture) != key.attachments.end()) {
            result.push_back(handle);
        }
    }
    return result;
}

void FrameBufferCache::insert(const FrameBufferKey& key, FrameBufferHandle handle) {
    m_entries.emplace(key, handle);
}

void FrameBufferCache::erase(const FrameBufferKey& key) {
    m_entries.erase(key);
}

void FrameBufferCache::clear() {
    m_entries.clear();
}

} // namespace hbg::gfx
