#pragma once

#include <string>
#include <string_view>

namespace tightrope::proxy::openai {

std::string normalize_event_type_alias(std::string_view event_type);
std::string normalize_stream_event_payload_json(const std::string& payload_json);
std::string normalize_sse_data_line(std::string_view line);
std::string normalize_sse_event_block(std::string_view event_block);
std::string build_websocket_response_create_payload_json(const std::string& payload_json);
std::string build_websocket_error_event_json(
    int status,
    const std::string& code,
    const std::string& message,
    const std::string& type = "server_error",
    const std::string& param = ""
);
std::string build_response_failed_event_json(
    const std::string& code,
    const std::string& message,
    const std::string& error_type = "server_error",
    const std::string& response_id = "",
    const std::string& error_param = ""
);
std::string build_websocket_response_failed_event_json(
    const std::string& code,
    const std::string& message,
    const std::string& error_type = "server_error",
    const std::string& response_id = "",
    const std::string& error_param = ""
);

} // namespace tightrope::proxy::openai
