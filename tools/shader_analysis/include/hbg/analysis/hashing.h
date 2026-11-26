#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace hbg::analysis {

std::string sha256Hex(std::string_view bytes);
std::string sha256File(const std::filesystem::path& path);

std::vector<std::string> canonicalDefines(const std::vector<std::string>& defines);
std::string joinCanonicalDefines(const std::vector<std::string>& canonical);
std::string makeVariantId(std::string_view source, std::string_view stage,
                          std::string_view backend, const std::vector<std::string>& defines);

std::string defineName(std::string_view define);
std::vector<std::string> keywordNames(const std::vector<std::string>& defines);

} // namespace hbg::analysis
