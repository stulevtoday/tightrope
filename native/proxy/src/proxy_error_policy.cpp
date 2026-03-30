#include "internal/proxy_error_policy.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

#include <glaze/glaze.hpp>

#include "logging/logger.h"

namespace tightrope::proxy::internal {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

std::string lower_ascii(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
    );
    return value;
}

std::string map_code_alias(std::string code) {
    if (code == "upstream_transport_failed" || code == "upstream_transport_init_failed") {
        return "upstream_unavailable";
    }
    if (code == "upstream_transport_timeout") {
        return "upstream_request_timeout";
    }
    return code;
}

std::string code_from_error_object(const JsonObject& error_object) {
    std::string code;
    std::string error_type;
    const auto code_it = error_object.find("code");
    if (code_it != error_object.end() && code_it->second.is_string()) {
        code = code_it->second.get_string();
    }
    const auto type_it = error_object.find("type");
    if (type_it != error_object.end() && type_it->second.is_string()) {
        error_type = type_it->second.get_string();
    }
    return normalize_upstream_error_code(code, error_type);
}

} // namespace

std::string normalize_upstream_error_code(const std::string_view code, const std::string_view error_type) {
    if (!code.empty()) {
        return map_code_alias(lower_ascii(std::string(code)));
    }
    if (!error_type.empty()) {
        return map_code_alias(lower_ascii(std::string(error_type)));
    }
    return "upstream_error";
}

std::string resolved_upstream_error_code(const UpstreamExecutionResult& upstream) {
    if (!upstream.error_code.empty()) {
        return normalize_upstream_error_code(upstream.error_code);
    }
    const auto event_error_code = extract_error_code_from_stream_events(upstream.events);
    if (!event_error_code.empty()) {
        return event_error_code;
    }
    return "upstream_error";
}

std::string extract_error_code_from_stream_events(const std::vector<std::string>& events) {
    for (const auto& event : events) {
        Json parsed;
        if (const auto ec = glz::read_json(parsed, event); ec || !parsed.is_object()) {
            continue;
        }
        const auto& object = parsed.get_object();
        const auto type_it = object.find("type");
        if (type_it == object.end() || !type_it->second.is_string()) {
            continue;
        }
        const auto event_type = type_it->second.get_string();
        if (event_type == "error") {
            const auto error_it = object.find("error");
            if (error_it != object.end() && error_it->second.is_object()) {
                return code_from_error_object(error_it->second.get_object());
            }
            continue;
        }
        if (event_type != "response.failed") {
            continue;
        }
        const auto response_it = object.find("response");
        if (response_it == object.end() || !response_it->second.is_object()) {
            continue;
        }
        const auto& response = response_it->second.get_object();
        const auto error_it = response.find("error");
        if (error_it == response.end() || !error_it->second.is_object()) {
            continue;
        }
        return code_from_error_object(error_it->second.get_object());
    }
    return {};
}

std::string default_error_message_for_code(const std::string_view normalized_error_code) {
    if (normalized_error_code == "upstream_request_timeout") {
        return "Proxy request budget exhausted";
    }
    return "Upstream error";
}

bool should_retry_stream_result(const UpstreamExecutionResult& upstream) {
    if (upstream.status >= 400) {
        const auto code = normalize_upstream_error_code(upstream.error_code);
        return upstream.status == 500 || code == "server_error";
    }
    const auto code = extract_error_code_from_stream_events(upstream.events);
    return code == "server_error";
}

bool should_retry_compact_result(const UpstreamExecutionResult& upstream) {
    if (upstream.status < 400) {
        return false;
    }
    return upstream.status == 401 || upstream.status == 500 || upstream.status == 502 || upstream.status == 503 ||
           upstream.status == 504;
}

UpstreamExecutionResult execute_upstream_with_retry_budget(
    const openai::UpstreamRequestPlan& plan,
    const int retry_budget,
    const RetryPredicate should_retry,
    const std::string_view retry_event
) {
    UpstreamExecutionResult upstream{};
    for (int attempt = 0;; ++attempt) {
        upstream = execute_upstream_plan(plan);
        upstream.error_code = resolved_upstream_error_code(upstream);
        if (attempt >= retry_budget || !should_retry(upstream)) {
            break;
        }
        core::logging::log_event(
            core::logging::LogLevel::Info,
            "runtime",
            "proxy",
            std::string(retry_event),
            "attempt=" + std::to_string(attempt + 1) + " status=" + std::to_string(upstream.status) +
                " code=" + upstream.error_code
        );
    }
    return upstream;
}

} // namespace tightrope::proxy::internal
