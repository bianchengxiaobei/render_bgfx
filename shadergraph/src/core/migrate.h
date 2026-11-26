#pragma once

#include "value_json.h"

#include <string>
#include <vector>

namespace sg {
namespace detail {

bool migrateJson(Json& root, std::vector<Diagnostic>& diagnostics, std::string& error);

} // namespace detail
} // namespace sg
