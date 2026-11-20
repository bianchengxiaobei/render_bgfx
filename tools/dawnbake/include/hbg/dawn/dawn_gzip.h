#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace hbg::dawn {

std::vector<uint8_t> gzipCompressStored(std::span<const uint8_t> data);
std::vector<uint8_t> gzipDecompressStored(std::span<const uint8_t> data);
std::vector<uint8_t> gzipDecompress(std::span<const uint8_t> data);
void writeGzipFile(const std::filesystem::path& path, std::span<const uint8_t> data);

}  // namespace hbg::dawn
