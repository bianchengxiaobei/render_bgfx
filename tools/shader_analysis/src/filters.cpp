#include "hbg/analysis/filters.h"

#include "hbg/analysis/hashing.h"

#include <set>

namespace hbg::analysis {
namespace {

std::vector<std::string> split(std::string_view text, char separator) {
    std::vector<std::string> result;
    size_t start = 0;
    while (true) {
        const size_t position = text.find(separator, start);
        if (position == std::string_view::npos) {
            result.emplace_back(text.substr(start));
            break;
        }
        result.emplace_back(text.substr(start, position - start));
        start = position + 1;
    }
    return result;
}

bool matchesAny(const std::vector<std::string>& patterns, std::string_view passName,
                std::string_view fullStem, std::string_view stage) {
    for (const std::string& pattern : patterns) {
        if (globMatch(pattern, passName) || globMatch(pattern, fullStem) ||
            globMatch(pattern, stage)) {
            return true;
        }
    }
    return false;
}

} // namespace

bool globMatch(std::string_view pattern, std::string_view text) {
    size_t patternIndex = 0;
    size_t textIndex = 0;
    size_t starIndex = std::string_view::npos;
    size_t starTextIndex = 0;
    while (textIndex < text.size()) {
        if (patternIndex < pattern.size() &&
            (pattern[patternIndex] == '?' || pattern[patternIndex] == text[textIndex])) {
            ++patternIndex;
            ++textIndex;
        } else if (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
            starIndex = patternIndex++;
            starTextIndex = textIndex;
        } else if (starIndex != std::string_view::npos) {
            patternIndex = starIndex + 1;
            textIndex = ++starTextIndex;
        } else {
            return false;
        }
    }
    while (patternIndex < pattern.size() && pattern[patternIndex] == '*') {
        ++patternIndex;
    }
    return patternIndex == pattern.size();
}

PassFilter PassFilter::parse(std::string_view text) {
    PassFilter result;
    if (text.empty()) {
        return result;
    }
    if (text.front() == '+') {
        result.includePatterns = split(text.substr(1), ',');
    } else if (text.front() == '-') {
        result.excludePatterns = split(text.substr(1), ',');
    } else {
        result.includePatterns = split(text, ',');
    }
    return result;
}

bool PassFilter::empty() const {
    return includePatterns.empty() && excludePatterns.empty();
}

bool PassFilter::matches(std::string_view passName, std::string_view fullStem,
                         std::string_view stage) const {
    if (!includePatterns.empty() && !matchesAny(includePatterns, passName, fullStem, stage)) {
        return false;
    }
    if (!excludePatterns.empty() && matchesAny(excludePatterns, passName, fullStem, stage)) {
        return false;
    }
    return true;
}

KeywordFilter KeywordFilter::parse(std::string_view text) {
    KeywordFilter result;
    if (text.empty()) {
        return result;
    }
    for (const std::string& group : split(text, '|')) {
        result.groups.push_back(split(group, '&'));
    }
    return result;
}

bool KeywordFilter::empty() const {
    return groups.empty();
}

bool KeywordFilter::matches(const std::vector<std::string>& defines) const {
    if (groups.empty()) {
        return true;
    }
    std::set<std::string> names;
    for (const std::string& define : defines) {
        names.insert(defineName(define));
    }
    for (const std::vector<std::string>& group : groups) {
        bool allPresent = true;
        for (const std::string& keyword : group) {
            if (names.find(keyword) == names.end()) {
                allPresent = false;
                break;
            }
        }
        if (allPresent) {
            return true;
        }
    }
    return false;
}

DefineFilter DefineFilter::parse(const std::vector<std::string>& values) {
    DefineFilter result;
    result.required = values;
    return result;
}

bool DefineFilter::empty() const {
    return required.empty();
}

bool DefineFilter::matches(const std::vector<std::string>& defines) const {
    if (required.empty()) {
        return true;
    }
    for (const std::string& requirement : required) {
        bool found = false;
        for (const std::string& define : defines) {
            if (requirement.find('=') == std::string::npos) {
                if (defineName(define) == requirement) {
                    found = true;
                    break;
                }
            } else if (define == requirement) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

} // namespace hbg::analysis
