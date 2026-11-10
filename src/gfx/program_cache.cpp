#include "gfx/program_cache.h"

#include "core/log.h"

#include <fstream>
#include <utility>

namespace hbg::gfx {

Program::~Program() {
    destroy();
}

Program::Program(Program&& other) noexcept : m_native(other.m_native), m_name(std::move(other.m_name)) {
    other.m_native = bgfx::ProgramHandle{bgfx::kInvalidHandle};
}

Program& Program::operator=(Program&& other) noexcept {
    if (this != &other) {
        destroy();
        m_native = other.m_native;
        m_name = std::move(other.m_name);
        other.m_native = bgfx::ProgramHandle{bgfx::kInvalidHandle};
    }
    return *this;
}

bool Program::create(const void* vertexData, uint32_t vertexSize, const void* fragmentData, uint32_t fragmentSize,
                     const std::string& name) {
    if (valid()) {
        HBG_LOGE("Program::create called on an existing program '{}'", m_name);
        return false;
    }
    if (vertexData == nullptr || vertexSize == 0 || fragmentData == nullptr || fragmentSize == 0) {
        HBG_LOGE("Program::create '{}' requires vertex and fragment shader binaries", name);
        return false;
    }

    const bgfx::ShaderHandle vertexShader = bgfx::createShader(bgfx::copy(vertexData, vertexSize));
    const bgfx::ShaderHandle fragmentShader = bgfx::createShader(bgfx::copy(fragmentData, fragmentSize));

    if (!bgfx::isValid(vertexShader) || !bgfx::isValid(fragmentShader)) {
        HBG_LOGE("failed to create shaders for program '{}'", name);
        if (bgfx::isValid(vertexShader)) {
            bgfx::destroy(vertexShader);
        }
        if (bgfx::isValid(fragmentShader)) {
            bgfx::destroy(fragmentShader);
        }
        return false;
    }

    if (!name.empty()) {
        const std::string vertexName = name + ".vs";
        const std::string fragmentName = name + ".fs";
        bgfx::setName(vertexShader, vertexName.c_str());
        bgfx::setName(fragmentShader, fragmentName.c_str());
    }

    m_native = bgfx::createProgram(vertexShader, fragmentShader, true);
    if (!valid()) {
        HBG_LOGE("failed to link program '{}'", name);
        bgfx::destroy(vertexShader);
        bgfx::destroy(fragmentShader);
        return false;
    }

    m_name = name;
    return true;
}

void Program::destroy() {
    if (valid()) {
        bgfx::destroy(m_native);
        m_native = bgfx::ProgramHandle{bgfx::kInvalidHandle};
    }
}

bool Program::valid() const {
    return bgfx::isValid(m_native);
}

bgfx::ProgramHandle Program::takeNative() {
    const bgfx::ProgramHandle native = m_native;
    m_native = bgfx::ProgramHandle{bgfx::kInvalidHandle};
    return native;
}

bool ProgramCache::loadShader(const std::string& path, std::vector<uint8_t>& outData) {
    const auto cached = m_shaderBlobs.find(path);
    if (cached != m_shaderBlobs.end()) {
        outData = cached->second;
        return true;
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        HBG_LOGE("failed to open shader '{}'", path);
        return false;
    }

    const std::streamsize size = file.tellg();
    if (size <= 0) {
        HBG_LOGE("shader '{}' is empty", path);
        return false;
    }
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data.data()), size)) {
        HBG_LOGE("failed to read shader '{}'", path);
        return false;
    }

    auto [iterator, inserted] = m_shaderBlobs.emplace(path, std::move(data));
    (void)inserted;
    outData = iterator->second;
    return true;
}

void ProgramCache::clear() {
    m_shaderBlobs.clear();
}

} // namespace hbg::gfx
