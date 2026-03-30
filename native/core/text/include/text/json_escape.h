#pragma once

#include <string>

namespace tightrope::core::text {

std::string escape_json_string(const std::string& value);
std::string quote_json_string(const std::string& value);

} // namespace tightrope::core::text
