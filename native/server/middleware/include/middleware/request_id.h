#pragma once

#include <string>
#include <string_view>

#include "openai/upstream_headers.h"

namespace tightrope::server::middleware {

[[nodiscard]] std::string ensure_request_id(proxy::openai::HeaderMap& headers);
[[nodiscard]] std::string request_id_from_headers(const proxy::openai::HeaderMap& headers);

} // namespace tightrope::server::middleware
