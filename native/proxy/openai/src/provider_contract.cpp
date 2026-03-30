#include "provider_contract.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glaze/glaze.hpp>

#include "text/ascii.h"

namespace tightrope::proxy::openai {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

struct EventAlias {
    std::string_view from;
    std::string_view to;
};

constexpr std::array<EventAlias, 3> kSseEventTypeAliases = {
    EventAlias{"response.text.delta", "response.output_text.delta"},
    EventAlias{"response.audio.delta", "response.output_audio.delta"},
    EventAlias{"response.audio_transcript.delta", "response.output_audio_transcript.delta"},
};

constexpr std::array<std::string_view, 2> kWebsocketResponseCreateExcludedFields = {
    "background",
    "stream",
};

std::int64_t unix_timestamp_seconds() {
    const auto now = std::chrono::system_clock::now();
    const auto unix_time = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch());
    return unix_time.count();
}

bool is_excluded_websocket_field(const std::string_view field_name) {
    for (const auto excluded : kWebsocketResponseCreateExcludedFields) {
        if (field_name == excluded) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> split_lines(const std::string_view body, const std::string_view separator) {
    std::vector<std::string> lines;
    if (body.empty()) {
        return lines;
    }

    std::size_t start = 0;
    while (start <= body.size()) {
        const auto pos = body.find(separator, start);
        if (pos == std::string_view::npos) {
            lines.emplace_back(body.substr(start));
            break;
        }
        lines.emplace_back(body.substr(start, pos - start));
        start = pos + separator.size();
        if (start == body.size()) {
            break;
        }
    }
    return lines;
}

std::string join_lines(const std::vector<std::string>& lines, const std::string_view separator) {
    if (lines.empty()) {
        return "";
    }
    std::string joined;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) {
            joined += separator;
        }
        joined += lines[i];
    }
    return joined;
}

} // namespace

std::string normalize_event_type_alias(const std::string_view event_type) {
    for (const auto& alias : kSseEventTypeAliases) {
        if (event_type == alias.from) {
            return std::string(alias.to);
        }
    }
    return std::string(event_type);
}

Json normalize_stream_event_payload(const Json& payload) {
    if (!payload.is_object()) {
        return payload;
    }

    Json normalized = payload;
    auto& object = normalized.get_object();
    const auto type_it = object.find("type");
    if (type_it == object.end() || !type_it->second.is_string()) {
        return normalized;
    }

    const auto aliased = normalize_event_type_alias(type_it->second.get_string());
    if (aliased == type_it->second.get_string()) {
        return normalized;
    }
    type_it->second = aliased;
    return normalized;
}

std::string serialize_json(const Json& payload) {
    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        throw std::runtime_error("failed to serialize provider contract JSON");
    }
    return serialized.value_or("{}");
}

std::string serialize_json_object(const JsonObject& payload) {
    Json wrapped = payload;
    return serialize_json(wrapped);
}

Json parse_json_or_throw(const std::string& payload_json) {
    Json payload;
    if (auto ec = glz::read_json(payload, payload_json); ec) {
        throw std::runtime_error("payload is not valid JSON");
    }
    return payload;
}

std::string normalize_sse_data_line(const std::string_view line) {
    if (!line.starts_with("data:")) {
        return std::string(line);
    }

    const auto data = core::text::trim_ascii(line.substr(5));
    if (data.empty() || data == "[DONE]") {
        return std::string(line);
    }

    Json payload;
    if (auto ec = glz::read_json(payload, data); ec || !payload.is_object()) {
        return std::string(line);
    }

    const auto normalized = normalize_stream_event_payload(payload);
    if (!normalized.is_object()) {
        return std::string(line);
    }

    const auto serialized = serialize_json(normalized);
    if (serialized == data) {
        return std::string(line);
    }
    return "data: " + serialized;
}

std::string normalize_sse_event_block(const std::string_view event_block) {
    if (event_block.empty()) {
        return std::string(event_block);
    }

    std::string_view line_separator = "\n";
    std::string_view terminator = "";
    std::string_view body = event_block;

    if (event_block.ends_with("\r\n\r\n")) {
        line_separator = "\r\n";
        terminator = "\r\n\r\n";
        body = event_block.substr(0, event_block.size() - terminator.size());
    } else if (event_block.ends_with("\n\n")) {
        line_separator = "\n";
        terminator = "\n\n";
        body = event_block.substr(0, event_block.size() - terminator.size());
    } else if (event_block.find("\r\n") != std::string_view::npos) {
        line_separator = "\r\n";
    }

    auto lines = split_lines(body, line_separator);
    if (lines.empty()) {
        return std::string(event_block);
    }

    bool changed = false;
    for (auto& line : lines) {
        const auto normalized_line = normalize_sse_data_line(line);
        if (normalized_line != line) {
            changed = true;
            line = normalized_line;
        }
    }

    if (!changed) {
        return std::string(event_block);
    }

    auto normalized = join_lines(lines, line_separator);
    normalized += terminator;
    return normalized;
}

JsonObject build_websocket_response_create_payload(const JsonObject& payload) {
    JsonObject request_payload;
    for (const auto& [key, value] : payload) {
        if (is_excluded_websocket_field(key)) {
            continue;
        }
        request_payload.emplace(key, value);
    }
    request_payload["type"] = "response.create";
    return request_payload;
}

std::string normalize_stream_event_payload_json(const std::string& payload_json) {
    Json payload;
    if (auto ec = glz::read_json(payload, payload_json); ec) {
        return payload_json;
    }

    const auto normalized = normalize_stream_event_payload(payload);
    return serialize_json(normalized);
}

std::string build_websocket_response_create_payload_json(const std::string& payload_json) {
    const auto payload = parse_json_or_throw(payload_json);
    if (!payload.is_object()) {
        throw std::runtime_error("payload must be a JSON object");
    }
    const auto request_payload = build_websocket_response_create_payload(payload.get_object());
    return serialize_json_object(request_payload);
}

std::string build_websocket_error_event_json(
    const int status,
    const std::string& code,
    const std::string& message,
    const std::string& type,
    const std::string& param
) {
    Json error = JsonObject{};
    error["message"] = message;
    error["type"] = type;
    error["code"] = code;
    if (!param.empty()) {
        error["param"] = param;
    }

    Json event = JsonObject{};
    event["type"] = "error";
    event["status"] = status;
    event["error"] = std::move(error);
    return serialize_json(event);
}

std::string build_websocket_response_failed_event_json(
    const std::string& code,
    const std::string& message,
    const std::string& error_type,
    const std::string& response_id,
    const std::string& error_param
) {
    return build_response_failed_event_json(code, message, error_type, response_id, error_param);
}

std::string build_response_failed_event_json(
    const std::string& code,
    const std::string& message,
    const std::string& error_type,
    const std::string& response_id,
    const std::string& error_param
) {
    Json error = JsonObject{};
    error["message"] = message;
    error["type"] = error_type;
    error["code"] = code;
    if (!error_param.empty()) {
        error["param"] = error_param;
    }

    Json response = JsonObject{};
    response["object"] = "response";
    response["status"] = "failed";
    response["error"] = std::move(error);
    response["incomplete_details"] = Json::null_t{};
    if (!response_id.empty()) {
        response["id"] = response_id;
    }
    response["created_at"] = unix_timestamp_seconds();

    Json event = JsonObject{};
    event["type"] = "response.failed";
    event["response"] = std::move(response);
    return serialize_json(event);
}

} // namespace tightrope::proxy::openai
