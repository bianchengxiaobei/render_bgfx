#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace sg::spike {

enum class ShaderStage : std::uint8_t { Vertex = 0, Fragment = 1, Compute = 2 };
enum class ShaderBackend : std::uint8_t { Dxbc = 0, Spirv = 1 };

struct ShaderBlob {
    ShaderStage stage = ShaderStage::Vertex;
    ShaderBackend backend = ShaderBackend::Dxbc;
    const std::uint8_t* data = nullptr;
    std::uint32_t size = 0;
};

class Asset {
  public:
    bool load(const std::string& path, std::string& error);
    bool find(ShaderStage stage, ShaderBackend backend, ShaderBlob& blob) const;

    std::uint64_t contentHash() const { return m_contentHash; }

  private:
    struct Entry {
        ShaderStage stage = ShaderStage::Vertex;
        ShaderBackend backend = ShaderBackend::Dxbc;
        std::uint32_t offset = 0;
        std::uint32_t size = 0;
    };

    std::vector<std::uint8_t> m_bytes;
    std::vector<Entry> m_entries;
    std::uint32_t m_blobsOffset = 0;
    std::uint64_t m_contentHash = 0;
};

} // namespace sg::spike
