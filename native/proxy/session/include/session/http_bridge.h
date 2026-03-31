#pragma once

#include <optional>
#include <string>
#include <vector>

#include "openai/upstream_headers.h"

namespace tightrope::proxy::session {

[[nodiscard]] std::string ensure_turn_state_header(openai::HeaderMap& headers);
[[nodiscard]] std::string inject_previous_response_id_from_bridge(
    const std::string& raw_request_body,
    const openai::HeaderMap& headers
);
[[nodiscard]] std::optional<std::string> strip_previous_response_id(const std::string& raw_request_body);

void remember_response_id_from_json(const openai::HeaderMap& headers, const std::string& response_body);
void remember_response_id_from_events(const openai::HeaderMap& headers, const std::vector<std::string>& events);

} // namespace tightrope::proxy::session
