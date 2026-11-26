#pragma once

#include <cstddef>
#include <filesystem>

namespace hbg::analysis {

struct ReferenceResult {
    std::size_t summaryCount = 0;
    std::size_t indexEntryCount = 0;
};

ReferenceResult setReference(const std::filesystem::path& fromReportsDir,
                             const std::filesystem::path& toReferenceDir);

} // namespace hbg::analysis
