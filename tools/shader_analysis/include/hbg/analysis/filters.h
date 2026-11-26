#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace hbg::analysis {

bool globMatch(std::string_view pattern, std::string_view text);

struct PassFilter {
    std::vector<std::string> includePatterns;
    std::vector<std::string> excludePatterns;

    static PassFilter parse(std::string_view text);
    bool empty() const;
    bool matches(std::string_view passName, std::string_view fullStem,
                 std::string_view stage) const;
};

struct KeywordFilter {
    std::vector<std::vector<std::string>> groups;

    static KeywordFilter parse(std::string_view text);
    bool empty() const;
    bool matches(const std::vector<std::string>& defines) const;
};

struct DefineFilter {
    std::vector<std::string> required;

    static DefineFilter parse(const std::vector<std::string>& values);
    bool empty() const;
    bool matches(const std::vector<std::string>& defines) const;
};

} // namespace hbg::analysis
