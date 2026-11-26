#include "compile_internal.h"

#include <algorithm>

namespace sg::detail {

bool FunctionRegistry::provide(const std::string& name, const std::string& code, std::string* error) {
    if (name.empty()) {
        if (error != nullptr) {
            *error = "function name must not be empty";
        }
        return false;
    }
    const auto found = sources.find(name);
    if (found != sources.end()) {
        if (found->second != code) {
            if (error != nullptr) {
                *error = "conflicting function implementations: " + name;
            }
            return false;
        }
        return true;
    }
    sources.emplace(name, code);
    names.push_back(name);
    return true;
}

std::string FunctionRegistry::render() const {
    std::string result;
    for (const std::string& name : names) {
        const auto found = sources.find(name);
        if (found == sources.end()) {
            continue;
        }
        result += found->second;
        if (!result.empty() && result.back() != '\n') {
            result.push_back('\n');
        }
    }
    return result;
}

} // namespace sg::detail
