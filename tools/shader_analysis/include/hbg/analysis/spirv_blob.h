#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace hbg::analysis {

struct SpirvBlob {
    std::vector<std::uint8_t> bytes;
    std::size_t offset = 0;
    std::uint32_t declaredSize = 0;
    std::uint8_t attributeCount = 0;
    std::vector<std::uint16_t> attributes;
    std::uint16_t uniformSize = 0;
};

SpirvBlob extractSpirvBlob(const std::vector<std::uint8_t>& binary);
SpirvBlob extractSpirvBlob(const std::filesystem::path& path);

} // namespace hbg::analysis
