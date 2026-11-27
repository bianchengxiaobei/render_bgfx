#include "gfx/uniform_block.h"

#include "core/log.h"

#include <utility>

namespace hbg::gfx {

bool UniformBlock::init(ResourceManager& resources, std::span<const UniformDesc> descriptors,
                        std::string_view prefix) {
    if (valid()) {
        HBG_LOGE("UniformBlock::init called while already initialized");
        return false;
    }
    if (descriptors.empty() || prefix.empty()) {
        HBG_LOGE("UniformBlock::init requires descriptors and a prefix");
        return false;
    }

    m_resources = &resources;
    m_prefix = std::string(prefix);
    m_handles.reserve(descriptors.size());
    m_descriptors.reserve(descriptors.size());

    for (std::size_t index = 0; index < descriptors.size(); ++index) {
        const UniformDesc& desc = descriptors[index];
        if (!desc.name.starts_with(prefix) || desc.type == UniformType::Sampler || desc.num == 0) {
            HBG_LOGE("UniformBlock::init received an invalid descriptor '{}'", desc.name);
            shutdown(resources);
            return false;
        }

        for (std::size_t previous = 0; previous < index; ++previous) {
            if (descriptors[previous].name == desc.name) {
                HBG_LOGE("UniformBlock::init received duplicate descriptor '{}'", desc.name);
                shutdown(resources);
                return false;
            }
        }

        const UniformHandle handle = resources.createUniform(desc);
        if (!handle.valid()) {
            HBG_LOGE("UniformBlock::init failed to create uniform '{}'", desc.name);
            shutdown(resources);
            return false;
        }

        m_handles.push_back(handle);
        m_descriptors.push_back(desc);
    }

    return true;
}

void UniformBlock::shutdown(ResourceManager& resources) {
    for (const UniformHandle handle : m_handles) {
        resources.destroyUniform(handle);
    }

    m_handles.clear();
    m_descriptors.clear();
    m_prefix.clear();
    m_resources = nullptr;
}

bool UniformBlock::valid() const {
    if (m_resources == nullptr || m_handles.empty() || m_handles.size() != m_descriptors.size()) {
        return false;
    }
    for (const UniformHandle handle : m_handles) {
        if (!handle.valid()) {
            return false;
        }
    }
    return true;
}

void UniformBlock::validateShader(bgfx::ShaderHandle shader) const {
    if (!valid()) {
        HBG_LOGF("UniformBlock::validateShader called before init");
    }
    if (!bgfx::isValid(shader)) {
        HBG_LOGF("UniformBlock::validateShader received an invalid shader");
    }

    const uint16_t count = bgfx::getShaderUniforms(shader, nullptr, 0);
    std::vector<bgfx::UniformHandle> uniforms(count);
    if (count > 0) {
        bgfx::getShaderUniforms(shader, uniforms.data(), count);
    }

    for (const bgfx::UniformHandle uniform : uniforms) {
        bgfx::UniformInfo info{};
        bgfx::getUniformInfo(uniform, info);

        const std::string_view name(info.name);
        if (!name.starts_with(m_prefix)) {
            continue;
        }

        const UniformDesc* found = nullptr;
        for (const UniformDesc& desc : m_descriptors) {
            if (desc.name == name) {
                found = &desc;
                break;
            }
        }

        if (found == nullptr) {
            HBG_LOGF("shader requires uniform '{}' that is missing from the generated uniform block", name);
        }
        if (info.type != toBgfx(found->type) || info.num != found->num) {
            HBG_LOGF("shader uniform '{}' type/count mismatch (shader type={}, num={}; generated type={}, num={})",
                     name, static_cast<int>(info.type), info.num, static_cast<int>(toBgfx(found->type)),
                     found->num);
        }
    }
}

void UniformBlock::set(std::size_t index, const Vec4& value) const {
    if (!checkType(index, UniformType::Vec4)) {
        return;
    }
    bgfx::setUniform(handleAt(index), &value.x, 1);
}

void UniformBlock::set(std::size_t index, const Mat3& value) const {
    if (!checkType(index, UniformType::Mat3)) {
        return;
    }
    bgfx::setUniform(handleAt(index), value.data(), 1);
}

void UniformBlock::set(std::size_t index, const Mat4& value) const {
    if (!checkType(index, UniformType::Mat4)) {
        return;
    }
    bgfx::setUniform(handleAt(index), value.data(), 1);
}

void UniformBlock::setVec2(std::size_t index, const Vec2& value) const {
    if (!checkType(index, UniformType::Vec4)) {
        return;
    }
    const float data[4] = {value.x, value.y, 0.0f, 0.0f};
    bgfx::setUniform(handleAt(index), data, 1);
}

void UniformBlock::setVec3(std::size_t index, const Vec3& value) const {
    if (!checkType(index, UniformType::Vec4)) {
        return;
    }
    const float data[4] = {value.x, value.y, value.z, 0.0f};
    bgfx::setUniform(handleAt(index), data, 1);
}

void UniformBlock::setScalar(std::size_t index, float value) const {
    if (!checkType(index, UniformType::Vec4)) {
        return;
    }
    const float data[4] = {value, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(handleAt(index), data, 1);
}

void UniformBlock::setInt(std::size_t index, std::int32_t value) const {
    if (!checkType(index, UniformType::Vec4)) {
        return;
    }
    const float data[4] = {static_cast<float>(value), 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(handleAt(index), data, 1);
}

void UniformBlock::setBool(std::size_t index, bool value) const {
    if (!checkType(index, UniformType::Vec4)) {
        return;
    }
    const float data[4] = {value ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
    bgfx::setUniform(handleAt(index), data, 1);
}

void UniformBlock::setArray(std::size_t index, const Vec4* values, uint16_t num) const {
    if (!checkArray(index, UniformType::Vec4, values, num)) {
        return;
    }
    bgfx::setUniform(handleAt(index), values, num);
}

void UniformBlock::setArray(std::size_t index, const Mat4* values, uint16_t num) const {
    if (!checkArray(index, UniformType::Mat4, values, num)) {
        return;
    }
    bgfx::setUniform(handleAt(index), values, num);
}

bgfx::UniformHandle UniformBlock::handleAt(std::size_t index) const {
    if (m_resources == nullptr || index >= m_handles.size() || !m_handles[index].valid()) {
        HBG_LOGE("UniformBlock::handleAt received an invalid index {}", index);
        return bgfx::UniformHandle{bgfx::kInvalidHandle};
    }
    return m_resources->native(m_handles[index]);
}

bool UniformBlock::checkType(std::size_t index, UniformType expected) const {
    if (index >= m_descriptors.size()) {
        HBG_LOGE("UniformBlock::checkType received an invalid index {}", index);
        return false;
    }
    if (m_descriptors[index].type != expected) {
        HBG_LOGE("UniformBlock uniform '{}' has a different type", m_descriptors[index].name);
        return false;
    }
    return true;
}

bool UniformBlock::checkArray(std::size_t index, UniformType expected, const void* values, uint16_t num) const {
    if (!checkType(index, expected)) {
        return false;
    }
    if (values == nullptr || num == 0 || num > m_descriptors[index].num) {
        HBG_LOGE("UniformBlock::setArray received an invalid array for '{}'", m_descriptors[index].name);
        return false;
    }
    return true;
}

} // namespace hbg::gfx
