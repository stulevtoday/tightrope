#include "ws_proxy.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <glaze/glaze.hpp>

#include "account_traffic.h"
#include "internal/proxy_error_policy.h"
#include "internal/proxy_service_helpers.h"
#include "logging/logger.h"
#include "openai/provider_contract.h"
#include "openai/upstream_headers.h"
#include "openai/upstream_request_plan.h"
#include "session/http_bridge.h"
#include "session/sticky_affinity.h"
#include "text/json_escape.h"
#include "upstream_transport.h"

namespace tightrope::proxy {

namespace {

using Json = glz::generic;

std::string sanitize_route_for_filename(const std::string_view route) {
    std::string sanitized;
    sanitized.reserve(route.size());
    for (const unsigned char ch : route) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            sanitized.push_back(static_cast<char>(ch));
        } else {
            sanitized.push_back('_');
        }
    }
    if (sanitized.empty()) {
        return "route";
    }
    return sanitized;
}

std::string capture_timestamp_id() {
    const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    return std::to_string(now.time_since_epoch().count());
}

std::optional<std::string> capture_invalid_payload_snapshot(
    const std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers,
    const std::string_view parser_error
) {
    const char* configured_dir = std::getenv("TIGHTROPE_INVALID_PAYLOAD_CAPTURE_DIR");
    const std::filesystem::path capture_dir =
        configured_dir != nullptr && *configured_dir != '\0'
            ? std::filesystem::path(configured_dir)
            : std::filesystem::path("/tmp/tightrope-invalid-payloads");

    std::error_code ec;
    std::filesystem::create_directories(capture_dir, ec);
    if (ec) {
        return std::nullopt;
    }

    const auto file_stem = capture_timestamp_id() + "_" + sanitize_route_for_filename(route);
    const auto payload_path = capture_dir / (file_stem + ".payload.bin");
    const auto meta_path = capture_dir / (file_stem + ".meta.txt");

    {
        std::ofstream payload_file(payload_path, std::ios::binary);
        if (!payload_file.is_open()) {
            return std::nullopt;
        }
        payload_file.write(raw_request_body.data(), static_cast<std::streamsize>(raw_request_body.size()));
        payload_file.flush();
        if (!payload_file.good()) {
            return std::nullopt;
        }
    }
    {
        std::ofstream meta_file(meta_path);
        if (!meta_file.is_open()) {
            return std::nullopt;
        }
        meta_file << "route: " << route << '\n';
        meta_file << "body_bytes: " << raw_request_body.size() << '\n';
        meta_file << "parser_error: " << parser_error << '\n';
        meta_file << "headers:\n";
        for (const auto& [key, value] : inbound_headers) {
            meta_file << "  " << key << ": " << value << '\n';
        }
    }

    return payload_path.string();
}

ProxyWsResult build_websocket_error_result(
    int status,
    bool accepted,
    int close_code,
    const std::string& error_code,
    const std::string& message,
    const std::string& error_type = "server_error",
    const std::string& error_param = "",
    const std::string& routed_account_id = "",
    const bool sticky_reused = false
) {
    return {
        .status = status,
        .accepted = accepted,
        .close_code = close_code,
        .frames = {openai::build_websocket_error_event_json(status, error_code, message, error_type, error_param)},
        .routed_account_id = routed_account_id,
        .sticky_reused = sticky_reused,
    };
}

std::vector<std::string> normalize_upstream_frames(const std::vector<std::string>& frames) {
    std::vector<std::string> normalized;
    normalized.reserve(frames.size());
    for (const auto& frame : frames) {
        normalized.push_back(openai::normalize_stream_event_payload_json(frame));
    }
    return normalized;
}

std::optional<std::string> fallback_body_without_previous_response(
    const std::string& request_body,
    const bool continuation_request,
    const bool contains_function_call_output
) {
    if (!continuation_request) {
        return request_body;
    }
    if (contains_function_call_output) {
        return std::nullopt;
    }
    if (const auto stripped = session::strip_previous_response_id(request_body); stripped.has_value()) {
        return *stripped;
    }
    return std::nullopt;
}

std::optional<std::string> fallback_body_with_local_context(
    const std::string& request_body,
    const openai::HeaderMap& headers,
    const bool continuation_request,
    const bool contains_function_call_output
) {
    if (!continuation_request) {
        return request_body;
    }
    if (contains_function_call_output) {
        return std::nullopt;
    }
    if (const auto recovered = session::rebuild_request_body_with_local_context(request_body, headers);
        recovered.has_value()) {
        return recovered;
    }
    return fallback_body_without_previous_response(request_body, continuation_request, contains_function_call_output);
}

bool apply_resolved_credentials(
    const std::optional<session::UpstreamAccountCredentials>& credentials,
    session::StickyAffinityResolution& resolved_affinity,
    std::string& access_token,
    std::string& traffic_account_id
) {
    if (!credentials.has_value()) {
        return false;
    }
    resolved_affinity.account_id = credentials->account_id;
    access_token = credentials->access_token;
    traffic_account_id.clear();
    if (credentials->internal_account_id > 0) {
        traffic_account_id = std::to_string(credentials->internal_account_id);
    }
    return true;
}

bool is_ascii_ws_trim_char(const unsigned char value) {
    return value <= 0x20 || value == 0x7F;
}

std::string trim_ws_payload_edges(std::string_view payload) {
    if (payload.empty()) {
        return {};
    }

    std::size_t begin = 0;
    std::size_t end = payload.size();
    if (end >= 3 && static_cast<unsigned char>(payload[0]) == 0xEF &&
        static_cast<unsigned char>(payload[1]) == 0xBB && static_cast<unsigned char>(payload[2]) == 0xBF) {
        begin = 3;
    }

    while (begin < end && is_ascii_ws_trim_char(static_cast<unsigned char>(payload[begin]))) {
        ++begin;
    }
    while (end > begin && is_ascii_ws_trim_char(static_cast<unsigned char>(payload[end - 1]))) {
        --end;
    }
    return std::string(payload.substr(begin, end - begin));
}

void append_utf8_code_point(std::string& target, const std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        target.push_back(static_cast<char>(code_point));
        return;
    }
    if (code_point <= 0x7FF) {
        target.push_back(static_cast<char>(0xC0 | ((code_point >> 6) & 0x1F)));
        target.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        return;
    }
    if (code_point <= 0xFFFF) {
        target.push_back(static_cast<char>(0xE0 | ((code_point >> 12) & 0x0F)));
        target.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        target.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        return;
    }
    if (code_point <= 0x10FFFF) {
        target.push_back(static_cast<char>(0xF0 | ((code_point >> 18) & 0x07)));
        target.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        target.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        target.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

std::optional<std::string> try_decode_utf16_json(std::string_view payload) {
    if (payload.size() < 4 || (payload.size() % 2) != 0) {
        return std::nullopt;
    }

    bool little_endian = false;
    std::size_t offset = 0;
    const auto first = static_cast<unsigned char>(payload[0]);
    const auto second = static_cast<unsigned char>(payload[1]);
    if (first == 0xFF && second == 0xFE) {
        little_endian = true;
        offset = 2;
    } else if (first == 0xFE && second == 0xFF) {
        little_endian = false;
        offset = 2;
    } else {
        const std::size_t sample_units = std::min<std::size_t>(payload.size() / 2, 64);
        std::size_t zero_even = 0;
        std::size_t zero_odd = 0;
        for (std::size_t index = 0; index < sample_units; ++index) {
            if (payload[index * 2] == '\0') {
                ++zero_even;
            }
            if (payload[index * 2 + 1] == '\0') {
                ++zero_odd;
            }
        }
        const bool looks_little_endian = zero_odd * 10 >= sample_units * 7 && zero_even * 10 <= sample_units * 3;
        const bool looks_big_endian = zero_even * 10 >= sample_units * 7 && zero_odd * 10 <= sample_units * 3;
        if (!looks_little_endian && !looks_big_endian) {
            return std::nullopt;
        }
        little_endian = looks_little_endian;
    }

    if (offset >= payload.size() || ((payload.size() - offset) % 2) != 0) {
        return std::nullopt;
    }

    std::string decoded;
    decoded.reserve((payload.size() - offset) / 2);
    std::uint16_t pending_high_surrogate = 0;

    for (std::size_t index = offset; index + 1 < payload.size(); index += 2) {
        const auto first_byte = static_cast<unsigned char>(payload[index]);
        const auto second_byte = static_cast<unsigned char>(payload[index + 1]);
        const std::uint16_t unit = little_endian
                                       ? static_cast<std::uint16_t>(first_byte | (second_byte << 8))
                                       : static_cast<std::uint16_t>((first_byte << 8) | second_byte);
        if (unit == 0) {
            continue;
        }
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (pending_high_surrogate != 0) {
                return std::nullopt;
            }
            pending_high_surrogate = unit;
            continue;
        }

        if (unit >= 0xDC00 && unit <= 0xDFFF) {
            if (pending_high_surrogate == 0) {
                return std::nullopt;
            }
            const auto code_point = static_cast<std::uint32_t>(
                0x10000 + ((pending_high_surrogate - 0xD800) << 10) + (unit - 0xDC00)
            );
            append_utf8_code_point(decoded, code_point);
            pending_high_surrogate = 0;
            continue;
        }

        if (pending_high_surrogate != 0) {
            return std::nullopt;
        }
        append_utf8_code_point(decoded, unit);
    }

    if (pending_high_surrogate != 0) {
        return std::nullopt;
    }
    return decoded;
}

std::string normalize_websocket_request_payload(std::string_view payload) {
    if (const auto utf16 = try_decode_utf16_json(payload); utf16.has_value()) {
        return trim_ws_payload_edges(*utf16);
    }
    const auto trimmed = trim_ws_payload_edges(payload);
    if (const auto utf16 = try_decode_utf16_json(trimmed); utf16.has_value()) {
        return trim_ws_payload_edges(*utf16);
    }
    return trimmed;
}

std::optional<std::string> try_extract_json_object_slice(std::string_view payload) {
    const auto start = payload.find('{');
    if (start == std::string_view::npos) {
        return std::nullopt;
    }

    bool in_string = false;
    bool escape = false;
    int depth = 0;

    for (std::size_t index = start; index < payload.size(); ++index) {
        const char ch = payload[index];
        if (in_string) {
            if (escape) {
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }

        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '{') {
            ++depth;
            continue;
        }
        if (ch != '}') {
            continue;
        }

        --depth;
        if (depth != 0) {
            continue;
        }

        return std::string(payload.substr(start, index - start + 1));
    }

    return std::nullopt;
}

bool is_json_whitespace_byte(const unsigned char value) {
    return value == 0x09 || value == 0x0A || value == 0x0D || value == 0x20;
}

bool is_disallowed_json_control_byte(const unsigned char value) {
    return value < 0x20 || value == 0x7F;
}

std::string strip_invalid_utf8_bytes(std::string_view input) {
    std::string sanitized;
    sanitized.reserve(input.size());

    for (std::size_t i = 0; i < input.size();) {
        const auto lead = static_cast<unsigned char>(input[i]);
        if (lead <= 0x7F) {
            sanitized.push_back(static_cast<char>(lead));
            ++i;
            continue;
        }

        std::size_t width = 0;
        if (lead >= 0xC2 && lead <= 0xDF) {
            width = 2;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            width = 3;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            width = 4;
        } else {
            ++i;
            continue;
        }

        if (i + width > input.size()) {
            ++i;
            continue;
        }

        bool valid = true;
        for (std::size_t j = 1; j < width; ++j) {
            const auto cont = static_cast<unsigned char>(input[i + j]);
            if ((cont & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) {
            ++i;
            continue;
        }

        const auto second = static_cast<unsigned char>(input[i + 1]);
        if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second > 0x9F) ||
            (lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second > 0x8F)) {
            ++i;
            continue;
        }

        sanitized.append(input.substr(i, width));
        i += width;
    }

    return sanitized;
}

std::string escape_control_bytes_in_json_strings(std::string_view payload) {
    std::string sanitized;
    sanitized.reserve(payload.size());

    bool in_string = false;
    bool escape = false;
    for (const unsigned char ch : payload) {
        if (!in_string) {
            if (is_disallowed_json_control_byte(ch) && !is_json_whitespace_byte(ch)) {
                // Strip non-whitespace controls outside strings so framed/junk-appended
                // payloads can still recover a valid JSON object.
                continue;
            }
            sanitized.push_back(static_cast<char>(ch));
            if (ch == '"') {
                in_string = true;
            }
            continue;
        }

        if (escape) {
            sanitized.push_back(static_cast<char>(ch));
            escape = false;
            continue;
        }
        if (ch == '\\') {
            sanitized.push_back('\\');
            escape = true;
            continue;
        }
        if (ch == '"') {
            sanitized.push_back('"');
            in_string = false;
            continue;
        }

        if (is_disallowed_json_control_byte(ch)) {
            // Canonicalize raw JSON control whitespace bytes; drop non-whitespace
            // controls (for example NUL/ESC/DEL) that make payloads brittle.
            // Invalid UTF-8 bytes are already stripped before this pass.
            switch (ch) {
            case '\b':
                sanitized += "\\b";
                break;
            case '\f':
                sanitized += "\\f";
                break;
            case '\n':
                sanitized += "\\n";
                break;
            case '\r':
                sanitized += "\\r";
                break;
            case '\t':
                sanitized += "\\t";
                break;
            default:
                break;
            }
            continue;
        }

        sanitized.push_back(static_cast<char>(ch));
    }
    return sanitized;
}

enum class WebsocketPayloadMode {
    NormalizeResponseCreate,
    Passthrough,
};

struct PreparedWebsocketPayload {
    std::string body;
    WebsocketPayloadMode mode = WebsocketPayloadMode::NormalizeResponseCreate;
};

PreparedWebsocketPayload prepare_websocket_request_payload(const std::string& raw_request_body) {
    auto normalized_input = strip_invalid_utf8_bytes(normalize_websocket_request_payload(raw_request_body));

    Json payload;
    auto parse_object = [&payload](const std::string_view candidate) {
        payload = Json{};
        const auto ec = glz::read_json(payload, candidate);
        return !ec && payload.is_object();
    };

    if (!parse_object(normalized_input)) {
        auto candidate = normalized_input;
        if (const auto extracted = try_extract_json_object_slice(candidate); extracted.has_value()) {
            candidate = normalize_websocket_request_payload(*extracted);
        }

        if (!parse_object(candidate)) {
            const auto sanitized = escape_control_bytes_in_json_strings(strip_invalid_utf8_bytes(candidate));
            if (parse_object(sanitized)) {
                normalized_input = sanitized;
            } else {
                return PreparedWebsocketPayload{.body = raw_request_body, .mode = WebsocketPayloadMode::Passthrough};
            }
        } else {
            normalized_input = candidate;
        }
    }

    const auto& object = payload.get_object();
    const auto type_it = object.find("type");
    if (type_it != object.end() &&
        (!type_it->second.is_string() || type_it->second.get_string() != "response.create")) {
        return PreparedWebsocketPayload{.body = raw_request_body, .mode = WebsocketPayloadMode::Passthrough};
    }

    Json normalized = Json::object_t{};
    auto& normalized_object = normalized.get_object();
    for (const auto& [key, value] : object) {
        if (key == "type" || key == "response") {
            continue;
        }
        normalized_object[key] = value;
    }
    if (const auto response_it = object.find("response");
        response_it != object.end() && response_it->second.is_object()) {
        for (const auto& [key, value] : response_it->second.get_object()) {
            normalized_object[key] = value;
        }
    }
    if (normalized_object.empty()) {
        throw std::runtime_error("response.create payload is missing request body");
    }

    const auto serialized = glz::write_json(normalized);
    if (!serialized) {
        throw std::runtime_error("failed to serialize response.create request payload");
    }
    auto normalized_body = core::text::sanitize_serialized_json(serialized.value_or("{}"));
    Json reparsed_payload;
    if (const auto reparsed_error = glz::read_json(reparsed_payload, normalized_body);
        reparsed_error || !reparsed_payload.is_object()) {
        auto hardened_body = escape_control_bytes_in_json_strings(strip_invalid_utf8_bytes(normalized_body));
        reparsed_payload = Json{};
        if (const auto hardened_error = glz::read_json(reparsed_payload, hardened_body);
            hardened_error || !reparsed_payload.is_object()) {
            throw std::runtime_error("failed to sanitize response.create request payload");
        }
        normalized_body = std::move(hardened_body);
    }
    return PreparedWebsocketPayload{
        .body = std::move(normalized_body),
        .mode = WebsocketPayloadMode::NormalizeResponseCreate,
    };
}

openai::UpstreamRequestPlan build_responses_websocket_passthrough_plan(
    const std::string& raw_request_body,
    const bool binary_frame,
    const openai::HeaderMap& inbound_headers,
    const std::string_view access_token,
    const std::string_view account_id,
    const std::string_view request_id
) {
    const auto prepared_inbound = openai::filter_inbound_headers(inbound_headers);
    return openai::UpstreamRequestPlan{
        .method = "POST",
        .path = "/codex/responses",
        .transport = "websocket",
        .body = raw_request_body,
        .headers = openai::build_upstream_websocket_headers(
            prepared_inbound,
            access_token,
            account_id,
            request_id
        ),
        .websocket_binary_payload = binary_frame,
    };
}

bool handle_deactivated_401_if_present(const UpstreamExecutionResult& upstream, const std::string_view account_id) {
    if (account_id.empty() || !internal::upstream_401_body_indicates_deactivated_account(upstream)) {
        return false;
    }

    const bool updated = session::mark_upstream_account_unusable(account_id);
    core::logging::log_event(
        updated ? core::logging::LogLevel::Info : core::logging::LogLevel::Warning,
        "runtime",
        "proxy",
        "upstream_account_marked_unusable",
        "account_id=" + std::string(account_id) + " reason=deactivated_401 updated=" + (updated ? "true" : "false")
    );
    return true;
}

bool handle_exhausted_account_if_present(const UpstreamExecutionResult& upstream, const std::string_view account_id) {
    if (account_id.empty()) {
        return false;
    }
    auto exhausted_status = internal::exhausted_account_status_from_upstream(upstream);
    if (!exhausted_status.has_value() && upstream.status >= 500 &&
        session::account_is_in_active_lock_pool(account_id)) {
        const auto normalized_code = internal::normalize_upstream_error_code(internal::resolved_upstream_error_code(upstream));
        if (normalized_code == "upstream_unavailable" || normalized_code == "upstream_error" ||
            normalized_code == "server_error") {
            exhausted_status = std::string("rate_limited");
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "proxy",
                "upstream_lock_pool_failover_marked_exhausted",
                "account_id=" + std::string(account_id) + " status=" + std::to_string(upstream.status) +
                    " code=" + normalized_code
            );
        }
    }
    if (!exhausted_status.has_value()) {
        return false;
    }

    const bool updated = session::mark_upstream_account_exhausted(account_id, *exhausted_status);
    core::logging::log_event(
        updated ? core::logging::LogLevel::Info : core::logging::LogLevel::Warning,
        "runtime",
        "proxy",
        "upstream_account_marked_exhausted",
        "account_id=" + std::string(account_id) + " status=" + *exhausted_status + " updated=" +
            (updated ? "true" : "false")
    );
    return true;
}

} // namespace

ProxyWsResult proxy_responses_websocket(
    const std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers,
    const bool binary_frame
) {
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "responses_ws_request_received",
        internal::build_proxy_request_detail(route, raw_request_body.size(), inbound_headers)
    );

    if (!internal::is_supported_responses_route(route)) {
        return build_websocket_error_result(404, false, 1008, "not_found", "Route not found");
    }

    PreparedWebsocketPayload prepared_payload;
    if (binary_frame) {
        prepared_payload = PreparedWebsocketPayload{
            .body = raw_request_body,
            .mode = WebsocketPayloadMode::Passthrough,
        };
    } else {
        try {
            prepared_payload = prepare_websocket_request_payload(raw_request_body);
        } catch (const std::exception& error) {
            const auto capture_path =
                capture_invalid_payload_snapshot(route, raw_request_body, inbound_headers, std::string_view(error.what()));
            auto detail = std::string("error=") + error.what();
            if (capture_path.has_value()) {
                detail += " capture_path=" + *capture_path;
            }
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "proxy",
                "responses_ws_payload_invalid",
                detail
            );
            return build_websocket_error_result(
                400,
                false,
                1008,
                "invalid_request_error",
                "Invalid request payload",
                "invalid_request_error",
                "input"
            );
        }
    }

    auto bridged_headers = inbound_headers;
    const auto turn_state = session::ensure_turn_state_header(bridged_headers);
    static_cast<void>(turn_state);
    const auto response_create_request = prepared_payload.mode == WebsocketPayloadMode::NormalizeResponseCreate;
    const auto& bridged_request_body = prepared_payload.body;
    const bool backend_codex_route = route == "/backend-api/codex/responses";

    session::StickyAffinityResolution resolved_affinity;
    internal::ContinuationRequestGuard continuation_guard;
    const std::string* effective_request_body = &bridged_request_body;
    std::string fallback_request_body;
    bool continuation_request = false;
    bool continuation_contains_function_call_output = false;
    bool sticky_reused = false;
    bool explicit_account_header = false;
    std::string access_token;
    std::string traffic_account_id;
    if (response_create_request) {
        const auto affinity = session::resolve_sticky_affinity(bridged_request_body, bridged_headers);
        explicit_account_header = affinity.from_header;
        continuation_guard =
            internal::guard_backend_codex_previous_response(route, bridged_request_body, bridged_headers, affinity);
        effective_request_body = &continuation_guard.request_body;
        continuation_request = continuation_guard.continuation_request;
        continuation_contains_function_call_output = continuation_guard.contains_function_call_output;
        sticky_reused = affinity.from_persistence;
        const auto& preferred_account_id = continuation_guard.preferred_account_id;
        const auto& continuity_account_id = continuation_guard.continuity_account_id;
        if (continuation_request &&
            (internal::optional_string_has_value(preferred_account_id) ||
             internal::optional_string_has_value(continuity_account_id))) {
            sticky_reused = true;
        }
        resolved_affinity = affinity;
        std::string credential_preference;
        if (internal::optional_string_has_value(preferred_account_id)) {
            credential_preference = *preferred_account_id;
        } else if (internal::optional_string_has_value(continuity_account_id)) {
            credential_preference = *continuity_account_id;
        } else if (!continuation_request || !affinity.account_id.empty()) {
            credential_preference = affinity.account_id;
        }
        const bool strict_preferred = continuation_request && !credential_preference.empty();
        const auto strict_credential_preference = credential_preference;
        const bool strict_preferred_account_known =
            strict_preferred && session::upstream_account_record_exists(strict_credential_preference);

        if (!apply_resolved_credentials(
                session::resolve_upstream_account_credentials(credential_preference, affinity.request_model, strict_preferred),
                resolved_affinity,
                access_token,
                traffic_account_id
            ) &&
            strict_preferred) {
            const auto stripped =
                affinity.from_header
                    ? std::optional<std::string>{}
                    : fallback_body_with_local_context(
                          *effective_request_body,
                          bridged_headers,
                          continuation_request,
                          continuation_contains_function_call_output
                      );
            if (stripped.has_value()) {
                fallback_request_body = *stripped;
                effective_request_body = &fallback_request_body;
                continuation_request = false;
                continuation_contains_function_call_output = false;
                credential_preference.clear();
                resolved_affinity = affinity;
                (void)apply_resolved_credentials(
                    session::resolve_upstream_account_credentials("", affinity.request_model, false),
                    resolved_affinity,
                    access_token,
                    traffic_account_id
                );
                sticky_reused = affinity.from_persistence;
            }
        }
        if (strict_preferred_account_known && access_token.empty()) {
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "proxy",
                "responses_ws_previous_response_account_unavailable",
                "route=" + std::string(route) + " account_id=" + strict_credential_preference
            );
            return build_websocket_error_result(
                409,
                false,
                1011,
                "previous_response_account_unavailable",
                "Previous response account is unavailable",
                "invalid_request_error",
                "previous_response_id",
                resolved_affinity.account_id,
                sticky_reused
            );
        }
    }

    openai::UpstreamRequestPlan plan;
    if (!response_create_request) {
        core::logging::log_event(
            core::logging::LogLevel::Info,
            "runtime",
            "proxy",
            "responses_ws_payload_passthrough",
            "reason=" + std::string(binary_frame ? "binary_frame" : "non_response_create") +
                " route=" + std::string(route) +
                " body_bytes=" + std::to_string(bridged_request_body.size())
        );
        plan = build_responses_websocket_passthrough_plan(
            bridged_request_body,
            binary_frame,
            bridged_headers,
            access_token,
            resolved_affinity.account_id,
            ""
        );
        plan.preserve_upstream_websocket_session = backend_codex_route;
    } else {
        try {
            plan = openai::build_responses_websocket_request_plan(
                *effective_request_body,
                bridged_headers,
                access_token,
                resolved_affinity.account_id,
                ""
            );
            plan.preserve_upstream_websocket_session = backend_codex_route;
        } catch (const std::exception& error) {
            const auto capture_path =
                capture_invalid_payload_snapshot(route, *effective_request_body, bridged_headers, std::string_view(error.what()));
            auto detail = std::string("error=") + error.what();
            if (capture_path.has_value()) {
                detail += " capture_path=" + *capture_path;
            }
            core::logging::log_event(
                core::logging::LogLevel::Warning,
                "runtime",
                "proxy",
                "responses_ws_payload_invalid",
                detail
            );
            return build_websocket_error_result(
                400,
                false,
                1008,
                "invalid_request_error",
                "Invalid request payload",
                "invalid_request_error",
                "input",
                resolved_affinity.account_id,
                sticky_reused
            );
        }
    }
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "responses_ws_upstream_plan_ready",
        internal::build_upstream_plan_detail(plan)
    );
    internal::maybe_log_upstream_payload_trace(route, plan);
    if (response_create_request) {
        session::persist_sticky_affinity(resolved_affinity);
    }

    auto execute_upstream = [&](const openai::UpstreamRequestPlan& current_plan) {
        const ScopedAccountTrafficContext traffic_scope(traffic_account_id);
        return internal::execute_upstream_with_retry_budget(
            current_plan,
            internal::kStreamRetryBudget,
            internal::should_retry_stream_result,
            "responses_ws_upstream_retry"
        );
    };

    auto upstream = execute_upstream(plan);
    if (response_create_request && upstream.status == 401 && !resolved_affinity.account_id.empty()) {
        if (!handle_deactivated_401_if_present(upstream, resolved_affinity.account_id)) {
            if (const auto refreshed = session::refresh_upstream_account_credentials(resolved_affinity.account_id);
                refreshed.has_value()) {
                resolved_affinity.account_id = refreshed->account_id;
                access_token = refreshed->access_token;
                if (refreshed->internal_account_id > 0) {
                    traffic_account_id = std::to_string(refreshed->internal_account_id);
                }
                try {
                    plan = openai::build_responses_websocket_request_plan(
                        *effective_request_body,
                        bridged_headers,
                        access_token,
                        resolved_affinity.account_id,
                        ""
                    );
                    plan.preserve_upstream_websocket_session = backend_codex_route;
                    upstream = execute_upstream(plan);
                    (void)handle_deactivated_401_if_present(upstream, resolved_affinity.account_id);
                } catch (const std::exception&) {
                    core::logging::log_event(
                        core::logging::LogLevel::Warning,
                        "runtime",
                        "proxy",
                        "responses_ws_refresh_rebuild_failed"
                    );
                }
            }
        }
    }
    if (response_create_request && backend_codex_route && continuation_request && continuation_contains_function_call_output &&
        internal::resolved_upstream_error_code(upstream) == "previous_response_not_found") {
        if (const auto rewritten = session::replace_previous_response_id_from_bridge(*effective_request_body, bridged_headers);
            rewritten.has_value()) {
            core::logging::log_event(
                core::logging::LogLevel::Info,
                "runtime",
                "proxy",
                "responses_previous_response_bridge_retry",
                "route=" + std::string(route)
            );
            try {
                plan = openai::build_responses_websocket_request_plan(
                    *rewritten,
                    bridged_headers,
                    access_token,
                    resolved_affinity.account_id,
                    ""
                );
                plan.preserve_upstream_websocket_session = backend_codex_route;
                upstream = execute_upstream(plan);
            } catch (const std::exception&) {
                core::logging::log_event(
                    core::logging::LogLevel::Warning,
                    "runtime",
                    "proxy",
                    "responses_previous_response_bridge_retry_rebuild_failed"
                );
            }
        }
    }
    if (response_create_request && continuation_request && !continuation_contains_function_call_output &&
        internal::resolved_upstream_error_code(upstream) == "previous_response_not_found") {
        if (const auto stripped = fallback_body_with_local_context(
                *effective_request_body,
                bridged_headers,
                continuation_request,
                continuation_contains_function_call_output
            );
            stripped.has_value()) {
            core::logging::log_event(
                core::logging::LogLevel::Info,
                "runtime",
                "proxy",
                "responses_previous_response_guard_retry",
                "route=" + std::string(route)
            );
            try {
                plan = openai::build_responses_websocket_request_plan(
                    *stripped,
                    bridged_headers,
                    access_token,
                    resolved_affinity.account_id,
                    ""
                );
                plan.preserve_upstream_websocket_session = backend_codex_route;
                fallback_request_body = *stripped;
                effective_request_body = &fallback_request_body;
                continuation_request = session::request_has_previous_response_id(*effective_request_body);
                continuation_contains_function_call_output =
                    continuation_request && session::request_contains_function_call_output(*effective_request_body);
                upstream = execute_upstream(plan);
            } catch (const std::exception&) {
                core::logging::log_event(
                    core::logging::LogLevel::Warning,
                    "runtime",
                    "proxy",
                    "responses_previous_response_guard_retry_rebuild_failed"
                );
            }
        }
    }
    if (response_create_request) {
        const bool marked_exhausted = handle_exhausted_account_if_present(upstream, resolved_affinity.account_id);
        if (marked_exhausted && !explicit_account_header) {
            if (const auto failover_body = fallback_body_with_local_context(
                    *effective_request_body,
                    bridged_headers,
                    continuation_request,
                    continuation_contains_function_call_output
                );
                failover_body.has_value()) {
                session::StickyAffinityResolution failover_affinity = resolved_affinity;
                failover_affinity.account_id.clear();
                std::string failover_access_token;
                std::string failover_traffic_account_id;
                if (apply_resolved_credentials(
                        session::resolve_upstream_account_credentials("", resolved_affinity.request_model, false),
                        failover_affinity,
                        failover_access_token,
                        failover_traffic_account_id
                    )) {
                    try {
                        plan = openai::build_responses_websocket_request_plan(
                            *failover_body,
                            bridged_headers,
                            failover_access_token,
                            failover_affinity.account_id,
                            ""
                        );
                        plan.preserve_upstream_websocket_session = backend_codex_route;
                        resolved_affinity = failover_affinity;
                        access_token = std::move(failover_access_token);
                        traffic_account_id = std::move(failover_traffic_account_id);
                        fallback_request_body = *failover_body;
                        effective_request_body = &fallback_request_body;
                        continuation_request = session::request_has_previous_response_id(*effective_request_body);
                        continuation_contains_function_call_output =
                            continuation_request && session::request_contains_function_call_output(*effective_request_body);
                        session::persist_sticky_affinity(resolved_affinity);
                        core::logging::log_event(
                            core::logging::LogLevel::Info,
                            "runtime",
                            "proxy",
                            "responses_ws_exhausted_account_failover",
                            "route=" + std::string(route) + " account_id=" + resolved_affinity.account_id
                        );
                        upstream = execute_upstream(plan);
                        (void)handle_exhausted_account_if_present(upstream, resolved_affinity.account_id);
                    } catch (const std::exception&) {
                        core::logging::log_event(
                            core::logging::LogLevel::Warning,
                            "runtime",
                            "proxy",
                            "responses_ws_exhausted_account_failover_rebuild_failed"
                        );
                    }
                }
            }
        }
    }
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "responses_ws_upstream_result",
        internal::build_upstream_result_detail(upstream)
    );
    if (upstream.status >= 400) {
        const auto details = internal::extract_upstream_error_details(upstream);
        const auto& code = details.code;
        const auto message = details.message.empty() ? internal::default_error_message_for_code(code) : details.message;
        const auto error_type = details.type.empty() ? std::string("server_error") : details.type;
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_ws_upstream_error",
            "status=" + std::to_string(upstream.status) + " code=" + code
        );
        return build_websocket_error_result(
            upstream.status,
            false,
            1011,
            code,
            message,
            error_type,
            details.param,
            resolved_affinity.account_id,
            sticky_reused
        );
    }

    const bool accepted = upstream.accepted || upstream.status == 101 || !upstream.events.empty();
    const int close_code = upstream.close_code > 0 ? upstream.close_code : (accepted ? 1000 : 1011);
    const auto error_code = internal::resolved_upstream_error_code(upstream);

    if (!accepted) {
        const auto status = upstream.status >= 400 ? upstream.status : 502;
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_ws_not_accepted",
            "status=" + std::to_string(status) + " close_code=" + std::to_string(close_code)
        );
        return build_websocket_error_result(
            status,
            false,
            close_code,
            error_code,
            internal::default_error_message_for_code(error_code),
            "server_error",
            "",
            resolved_affinity.account_id,
            sticky_reused
        );
    }

    auto normalized_frames = normalize_upstream_frames(upstream.events);
    if (normalized_frames.empty()) {
        const int incomplete_close_code = close_code == 1000 ? 1011 : close_code;
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_ws_stream_incomplete",
            "close_code=" + std::to_string(incomplete_close_code)
        );
        return {
            .status = 101,
            .accepted = false,
            .close_code = incomplete_close_code,
            .frames = {},
            .routed_account_id = resolved_affinity.account_id,
            .sticky_reused = sticky_reused,
        };
    }

    session::remember_response_id_from_events(bridged_headers, plan.body, upstream.events, resolved_affinity.account_id);

    return {
        .status = 101,
        .accepted = true,
        .close_code = close_code,
        .frames = std::move(normalized_frames),
        .routed_account_id = resolved_affinity.account_id,
        .sticky_reused = sticky_reused,
    };
}

} // namespace tightrope::proxy
