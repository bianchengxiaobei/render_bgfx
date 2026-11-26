#include "compile_internal.h"

#include <algorithm>
#include <cstdint>

namespace sg::detail {
namespace {

bool engineFlag(const Graph& graph, const char* key) {
    const auto found = graph.target.settings.find(key);
    return found != graph.target.settings.end() && found->second.asBool() != nullptr &&
           *found->second.asBool();
}

int defaultValue(const Keyword& keyword) {
    if (keyword.type == KeywordType::Boolean) {
        return keyword.defaultValue != 0 ? 1 : 0;
    }
    const auto found = std::find_if(keyword.entries.begin(), keyword.entries.end(),
                                    [&](const KeywordEntry& entry) {
                                        return entry.id == keyword.defaultValue;
                                    });
    if (found == keyword.entries.end()) {
        return 0;
    }
    return static_cast<int>(found - keyword.entries.begin());
}

std::string enumDefine(const Keyword& keyword, int value) {
    if (keyword.entries.empty()) {
        return {};
    }
    const KeywordEntry& entry = keyword.entries[static_cast<std::size_t>(value) % keyword.entries.size()];
    return keyword.refName + "_" + entry.referenceName;
}

VariantInfo makeVariant(const Graph& graph, const KeywordSet& set, std::vector<int> values) {
    VariantInfo variant;
    variant.values = std::move(values);
    variant.key = engineFlag(graph, "alphaClip") ? 1ull : 0ull;
    variant.key |= engineFlag(graph, "doubleSided") ? 2ull : 0ull;
    variant.key |= engineFlag(graph, "transparent") ? 4ull : 0ull;
    for (std::size_t index = 0; index < set.keywords.size(); ++index) {
        const Keyword& keyword = set.keywords[index];
        const int value = variant.values[index];
        if (keyword.type == KeywordType::Enum) {
            if (!keyword.entries.empty()) {
                variant.key |= 1ull << (set.bitOffsets[index] + value);
                variant.defines.push_back(enumDefine(keyword, value));
            }
        } else if (value != 0) {
            variant.key |= 1ull << set.bitOffsets[index];
            variant.defines.push_back(keyword.refName + "=1");
        }
    }
    return variant;
}

} // namespace

KeywordSet collectKeywords(const Graph& graph, const TargetSpec& target, VariantPolicy policy,
                           int maxVariants) {
    (void)target;
    KeywordSet result;
    result.keywords = graph.keywords;
    int bitOffset = 8;
    for (const Keyword& keyword : result.keywords) {
        if (keyword.scope != KeywordScope::Local) {
            Diagnostic diagnostic;
            diagnostic.severity = Severity::Error;
            diagnostic.code = kErrorTargetUnsupported;
            diagnostic.message = "global keywords are not supported by the C++ compiler";
            result.diagnostics.push_back(std::move(diagnostic));
            result.ok = false;
        }
        const int bits = keyword.type == KeywordType::Enum
                             ? std::max<int>(1, static_cast<int>(keyword.entries.size()))
                             : 1;
        result.bitOffsets.push_back(bitOffset);
        bitOffset += bits;
    }
    if (bitOffset > 16) {
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorKeywordLimit;
        diagnostic.message = "graph uses more than eight keyword bits";
        result.diagnostics.push_back(std::move(diagnostic));
        result.ok = false;
    }

    if (maxVariants <= 0) {
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorVariantLimit;
        diagnostic.message = "variant limit must be positive";
        result.diagnostics.push_back(std::move(diagnostic));
        result.ok = false;
        return result;
    }
    std::uint64_t count = 1;
    for (const Keyword& keyword : result.keywords) {
        const std::uint64_t factor = keyword.type == KeywordType::Enum
                                         ? std::max<std::uint64_t>(1, keyword.entries.size())
                                         : 2ull;
        if (count > static_cast<std::uint64_t>(maxVariants) / factor) {
            count = static_cast<std::uint64_t>(maxVariants) + 1;
            break;
        }
        count *= factor;
    }
    if (count > static_cast<std::uint64_t>(maxVariants)) {
        Diagnostic diagnostic;
        diagnostic.severity = Severity::Error;
        diagnostic.code = kErrorVariantLimit;
        diagnostic.message = "graph exceeds the variant limit";
        result.diagnostics.push_back(std::move(diagnostic));
        result.ok = false;
        return result;
    }

    std::vector<int> defaults;
    for (const Keyword& keyword : result.keywords) {
        defaults.push_back(defaultValue(keyword));
    }
    if (policy == VariantPolicy::Used) {
        result.variants.push_back(makeVariant(graph, result, defaults));
    } else {
        for (std::uint64_t index = 0; index < count; ++index) {
            std::uint64_t cursor = index;
            std::vector<int> values;
            for (const Keyword& keyword : result.keywords) {
                const std::uint64_t factor = keyword.type == KeywordType::Enum
                                                 ? std::max<std::uint64_t>(1, keyword.entries.size())
                                                 : 2ull;
                values.push_back(static_cast<int>(cursor % factor));
                cursor /= factor;
            }
            result.variants.push_back(makeVariant(graph, result, std::move(values)));
        }
    }
    if (result.diagnostics.empty()) {
        result.ok = true;
    }
    return result;
}

} // namespace sg::detail
