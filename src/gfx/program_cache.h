#pragma once

#include "core/handle.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hbg::gfx {

struct ProgramTag {};
using ProgramHandle = Handle<ProgramTag>;

class Program {
  public:
    Program() = default;
    ~Program();

    Program(const Program&) = delete;
    Program& operator=(const Program&) = delete;
    Program(Program&& other) noexcept;
    Program& operator=(Program&& other) noexcept;

    bool create(const void* vertexData, uint32_t vertexSize, const void* fragmentData, uint32_t fragmentSize,
                const std::string& name = {});
    bool createCompute(const void* computeData, uint32_t computeSize, const std::string& name = {});
    void destroy();

    bool valid() const;
    bgfx::ProgramHandle native() const { return m_native; }
    bgfx::ProgramHandle takeNative();
    const std::string& name() const { return m_name; }

  private:
    bgfx::ProgramHandle m_native{bgfx::kInvalidHandle};
    std::string m_name;
};

class ProgramCache {
  public:
    bool loadShader(const std::string& path, std::vector<uint8_t>& outData);
    void clear();

    std::size_t cachedShaderCount() const { return m_shaderBlobs.size(); }

  private:
    std::unordered_map<std::string, std::vector<uint8_t>> m_shaderBlobs;
};

} // namespace hbg::gfx
