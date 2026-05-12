#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openai/upstream_headers.h"

namespace tightrope::proxy::session {

[[nodiscard]] std::string ensure_turn_state_header(openai::HeaderMap& headers);
[[nodiscard]] std::string inject_previous_response_id_from_bridge(
    const std::string& raw_request_body,
    const openai::HeaderMap& headers
);
[[nodiscard]] std::optional<std::string> strip_previous_response_id(const std::string& raw_request_body);
[[nodiscard]] std::optional<std::string> replace_previous_response_id_from_bridge(
    const std::string& raw_request_body,
    const openai::HeaderMap& headers
);
[[nodiscard]] std::optional<std::string>
resolve_preferred_account_id_from_previous_response(const std::string& raw_request_body, const openai::HeaderMap& headers);
[[nodiscard]] std::optional<std::string> resolve_continuity_account_id(const openai::HeaderMap& headers);
[[nodiscard]] bool request_has_previous_response_id(const std::string& raw_request_body);
[[nodiscard]] bool request_contains_function_call_output(const std::string& raw_request_body);
[[nodiscard]] std::optional<std::string> rebuild_request_body_with_local_context(
    const std::string& raw_request_body,
    const openai::HeaderMap& headers
);

void remember_response_id_from_json(
    const openai::HeaderMap& headers,
    const std::string& response_body,
    std::string_view account_id = {}
);
void remember_response_id_from_json(
    const openai::HeaderMap& headers,
    const std::string& request_body,
    const std::string& response_body,
    std::string_view account_id
);
void remember_response_id_from_events(
    const openai::HeaderMap& headers,
    const std::vector<std::string>& events,
    std::string_view account_id = {}
);
void remember_response_id_from_events(
    const openai::HeaderMap& headers,
    const std::string& request_body,
    const std::vector<std::string>& events,
    std::string_view account_id
);

void reset_response_bridge_for_tests();

} // namespace tightrope::proxy::session
