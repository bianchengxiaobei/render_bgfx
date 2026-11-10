#pragma once

#include "core/handle.h"
#include "core/log.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>

namespace hbg::gfx {

enum class UniformType : uint8_t {
    Sampler,
    Vec4,
    Mat3,
    Mat4,
};

inline bgfx::UniformType::Enum toBgfx(UniformType type) {
    switch (type) {
    case UniformType::Sampler:
        return bgfx::UniformType::Sampler;
    case UniformType::Vec4:
        return bgfx::UniformType::Vec4;
    case UniformType::Mat3:
        return bgfx::UniformType::Mat3;
    case UniformType::Mat4:
        return bgfx::UniformType::Mat4;
    }
    return bgfx::UniformType::Vec4;
}

struct UniformTag {};
using UniformHandle = Handle<UniformTag>;

struct UniformDesc {
    std::string name;
    UniformType type = UniformType::Vec4;
    uint16_t num = 1;
};

class Uniform {
  public:
    Uniform() = default;
    ~Uniform() { destroy(); }

    Uniform(const Uniform&) = delete;
    Uniform& operator=(const Uniform&) = delete;
    Uniform(Uniform&& other) noexcept : m_native(other.m_native), m_desc(std::move(other.m_desc)) {
        other.m_native = bgfx::UniformHandle{bgfx::kInvalidHandle};
    }
    Uniform& operator=(Uniform&& other) noexcept {
        if (this != &other) {
            destroy();
            m_native = other.m_native;
            m_desc = std::move(other.m_desc);
            other.m_native = bgfx::UniformHandle{bgfx::kInvalidHandle};
        }
        return *this;
    }

    bool create(const UniformDesc& desc) {
        if (valid()) {
            HBG_LOGE("Uniform::create called on an existing uniform '{}'", m_desc.name);
            return false;
        }
        if (desc.name.empty() || desc.num == 0) {
            HBG_LOGE("Uniform::create requires a name and a non-zero element count");
            return false;
        }

        m_native = bgfx::createUniform(desc.name.c_str(), toBgfx(desc.type), desc.num);
        if (!valid()) {
            HBG_LOGE("failed to create uniform '{}'", desc.name);
            return false;
        }

        m_desc = desc;
        return true;
    }

    void destroy() {
        if (valid()) {
            bgfx::destroy(m_native);
            m_native = bgfx::UniformHandle{bgfx::kInvalidHandle};
        }
    }

    bool valid() const { return bgfx::isValid(m_native); }
    bgfx::UniformHandle native() const { return m_native; }
    bgfx::UniformHandle takeNative() {
        const bgfx::UniformHandle native = m_native;
        m_native = bgfx::UniformHandle{bgfx::kInvalidHandle};
        return native;
    }

    const UniformDesc& desc() const { return m_desc; }

  private:
    bgfx::UniformHandle m_native{bgfx::kInvalidHandle};
    UniformDesc m_desc{};
};

} // namespace hbg::gfx
