#pragma once

#include "core/math.h"
#include "gfx/resource_manager.h"
#include "gfx/uniform.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::gfx {

class UniformBlock {
  public:
    UniformBlock() = default;
    ~UniformBlock() = default;

    UniformBlock(const UniformBlock&) = delete;
    UniformBlock& operator=(const UniformBlock&) = delete;

    bool init(ResourceManager& resources, std::span<const UniformDesc> descriptors, std::string_view prefix);
    void shutdown(ResourceManager& resources);

    bool valid() const;
    std::span<const UniformHandle> handles() const { return m_handles; }
    const UniformDesc& descriptor(std::size_t index) const { return m_descriptors[index]; }

    void validateShader(bgfx::ShaderHandle shader) const;

    void set(std::size_t index, const Vec4& value) const;
    void set(std::size_t index, const Mat3& value) const;
    void set(std::size_t index, const Mat4& value) const;
    void setVec2(std::size_t index, const Vec2& value) const;
    void setVec3(std::size_t index, const Vec3& value) const;
    void setScalar(std::size_t index, float value) const;
    void setInt(std::size_t index, std::int32_t value) const;
    void setBool(std::size_t index, bool value) const;

    void setArray(std::size_t index, const Vec4* values, uint16_t num) const;
    void setArray(std::size_t index, const Mat4* values, uint16_t num) const;

  private:
    bgfx::UniformHandle handleAt(std::size_t index) const;
    bool checkType(std::size_t index, UniformType expected) const;
    bool checkArray(std::size_t index, UniformType expected, const void* values, uint16_t num) const;

    ResourceManager* m_resources = nullptr;
    std::vector<UniformHandle> m_handles;
    std::vector<UniformDesc> m_descriptors;
    std::string m_prefix;
};

} // namespace hbg::gfx
