#include "internal/proxy_error_policy.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <glaze/glaze.hpp>

#include "logging/logger.h"
#include "sync_event_emitter.h"

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

UpstreamErrorDetails details_from_error_object(const JsonObject& error_object) {
    UpstreamErrorDetails details;
    const auto code_it = error_object.find("code");
    if (code_it != error_object.end() && code_it->second.is_string()) {
        details.code = normalize_upstream_error_code(code_it->second.get_string());
    }
    const auto type_it = error_object.find("type");
    if (type_it != error_object.end() && type_it->second.is_string()) {
        details.type = type_it->second.get_string();
        if (details.code.empty()) {
            details.code = normalize_upstream_error_code("", details.type);
        }
    }
    const auto message_it = error_object.find("message");
    if (message_it != error_object.end() && message_it->second.is_string()) {
        details.message = message_it->second.get_string();
    }
    const auto param_it = error_object.find("param");
    if (param_it != error_object.end() && param_it->second.is_string()) {
        details.param = param_it->second.get_string();
    }
    return details;
}

UpstreamErrorDetails merge_error_details(UpstreamErrorDetails base, const UpstreamErrorDetails& overlay) {
    if (base.code.empty() && !overlay.code.empty()) {
        base.code = overlay.code;
    }
    if (base.message.empty() && !overlay.message.empty()) {
        base.message = overlay.message;
    }
    if (base.type.empty() && !overlay.type.empty()) {
        base.type = overlay.type;
    }
    if (base.param.empty() && !overlay.param.empty()) {
        base.param = overlay.param;
    }
    return base;
}

std::optional<UpstreamErrorDetails> extract_error_details_from_payload(std::string_view payload) {
    Json parsed;
    if (const auto ec = glz::read_json(parsed, payload); ec || !parsed.is_object()) {
        return std::nullopt;
    }

    const auto& object = parsed.get_object();
    const auto type_it = object.find("type");
    if (type_it != object.end() && type_it->second.is_string()) {
        const auto event_type = type_it->second.get_string();
        if (event_type == "error") {
            const auto error_it = object.find("error");
            if (error_it != object.end() && error_it->second.is_object()) {
                return details_from_error_object(error_it->second.get_object());
            }
        }
        if (event_type == "response.failed") {
            const auto error_it = object.find("error");
            if (error_it != object.end() && error_it->second.is_object()) {
                return details_from_error_object(error_it->second.get_object());
            }
            const auto response_it = object.find("response");
            if (response_it != object.end() && response_it->second.is_object()) {
                const auto& response_object = response_it->second.get_object();
                const auto response_error_it = response_object.find("error");
                if (response_error_it != response_object.end() && response_error_it->second.is_object()) {
                    return details_from_error_object(response_error_it->second.get_object());
                }
            }
        }
    }

    const auto error_it = object.find("error");
    if (error_it != object.end() && error_it->second.is_object()) {
        return details_from_error_object(error_it->second.get_object());
    }

    const auto response_it = object.find("response");
    if (response_it != object.end() && response_it->second.is_object()) {
        const auto& response_object = response_it->second.get_object();
        const auto response_error_it = response_object.find("error");
        if (response_error_it != response_object.end() && response_error_it->second.is_object()) {
            return details_from_error_object(response_error_it->second.get_object());
        }
    }
    return std::nullopt;
}

bool contains_deactivated_marker(const std::string_view payload) {
    if (payload.empty()) {
        return false;
    }
    auto normalized = lower_ascii(std::string(payload));
    return normalized.find("deactivated") != std::string::npos;
}

bool contains_quota_marker(const std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const auto normalized = lower_ascii(std::string(value));
    return normalized.find("quota") != std::string::npos ||
           normalized.find("insufficient_quota") != std::string::npos ||
           normalized.find("quota_exceeded") != std::string::npos ||
           normalized.find("usage limit") != std::string::npos ||
           normalized.find("credits") != std::string::npos ||
           normalized.find("billing") != std::string::npos ||
           normalized.find("hard limit") != std::string::npos;
}

bool contains_rate_limit_marker(const std::string_view value) {
    if (value.empty()) {
        return false;
    }
    const auto normalized = lower_ascii(std::string(value));
    return normalized.find("rate limit") != std::string::npos ||
           normalized.find("rate_limited") != std::string::npos ||
           normalized.find("rate_limit_exceeded") != std::string::npos ||
           normalized.find("too many requests") != std::string::npos;
}

template <typename Predicate>
bool any_event_matches(const std::vector<std::string>& events, Predicate&& predicate) {
    for (const auto& event : events) {
        if (predicate(event)) {
            return true;
        }
    }
    return false;
}

template <typename Predicate>
bool any_header_matches(const openai::HeaderMap& headers, Predicate&& predicate) {
    for (const auto& [name, value] : headers) {
        if (predicate(name) || predicate(value)) {
            return true;
        }
    }
    return false;
}

void emit_runtime_signal(
    const std::string_view level,
    const std::string_view code,
    const std::string_view message
) {
    sync::SyncEventEmitter::get().emit(sync::SyncEventRuntimeSignal{
        .level = std::string(level),
        .code = std::string(code),
        .message = std::string(message),
        .account_id = {},
    });
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
    return extract_upstream_error_details(upstream).code;
}

std::string extract_error_code_from_stream_events(const std::vector<std::string>& events) {
    for (const auto& event : events) {
        if (const auto details = extract_error_details_from_payload(event); details.has_value() && !details->code.empty()) {
            return details->code;
        }
    }
    return {};
}

UpstreamErrorDetails extract_upstream_error_details(const UpstreamExecutionResult& upstream) {
    UpstreamErrorDetails details{};
    if (!upstream.error_code.empty()) {
        details.code = normalize_upstream_error_code(upstream.error_code);
    }

    if (!upstream.body.empty()) {
        if (const auto body_details = extract_error_details_from_payload(upstream.body); body_details.has_value()) {
            details = merge_error_details(std::move(details), *body_details);
        }
    }

    for (const auto& event : upstream.events) {
        if (const auto event_details = extract_error_details_from_payload(event); event_details.has_value()) {
            details = merge_error_details(std::move(details), *event_details);
        }
        if (!details.code.empty() && !details.message.empty() && !details.type.empty()) {
            break;
        }
    }

    if (details.code.empty()) {
        details.code = "upstream_error";
    }
    if (details.type.empty()) {
        details.type = details.code == "upstream_request_timeout" ? "timeout_error" : "server_error";
    }
    return details;
}

std::string default_error_message_for_code(const std::string_view normalized_error_code) {
    if (normalized_error_code == "upstream_request_timeout") {
        return "Proxy request budget exhausted";
    }
    return "Upstream error";
}

bool upstream_401_body_indicates_deactivated_account(const UpstreamExecutionResult& upstream) {
    if (upstream.status != 401) {
        return false;
    }
    if (contains_deactivated_marker(upstream.body)) {
        return true;
    }
    for (const auto& event : upstream.events) {
        if (contains_deactivated_marker(event)) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> exhausted_account_status_from_upstream(const UpstreamExecutionResult& upstream) {
    const auto details = extract_upstream_error_details(upstream);
    const auto code = lower_ascii(details.code);
    const auto type = lower_ascii(details.type);
    const bool failed_response = upstream.status >= 400;

    if (code == "insufficient_quota" || code == "quota_exceeded" || code == "credits_exhausted" ||
        code == "billing_hard_limit_reached" || code == "billing_not_active") {
        return std::string("quota_blocked");
    }
    if (failed_response &&
        (contains_quota_marker(details.message) || contains_quota_marker(upstream.body) ||
         any_event_matches(upstream.events, contains_quota_marker) ||
         any_header_matches(upstream.headers, contains_quota_marker))) {
        return std::string("quota_blocked");
    }
    if (code == "rate_limit_exceeded" || code == "rate_limited" || code == "too_many_requests") {
        return std::string("rate_limited");
    }
    if (type == "rate_limit_exceeded" || type == "rate_limited") {
        return std::string("rate_limited");
    }
    if (failed_response &&
        (contains_rate_limit_marker(details.message) || contains_rate_limit_marker(upstream.body) ||
         any_event_matches(upstream.events, contains_rate_limit_marker) ||
         any_header_matches(upstream.headers, contains_rate_limit_marker))) {
        return std::string("rate_limited");
    }
    if (upstream.status == 429) {
        return std::string("rate_limited");
    }
    return std::nullopt;
}

bool should_retry_stream_result(const UpstreamExecutionResult& upstream) {
    if (exhausted_account_status_from_upstream(upstream).has_value()) {
        return false;
    }
    if (upstream.status >= 400) {
        const auto code = normalize_upstream_error_code(upstream.error_code);
        return upstream.status == 500 || code == "server_error";
    }
    const auto code = extract_error_code_from_stream_events(upstream.events);
    return code == "server_error";
}

bool should_retry_stream_result_with_incomplete(const UpstreamExecutionResult& upstream) {
    if (should_retry_stream_result(upstream)) {
        return true;
    }
    const auto code = normalize_upstream_error_code(resolved_upstream_error_code(upstream));
    return code == "stream_incomplete" || code == "upstream_incomplete";
}

bool should_retry_compact_result(const UpstreamExecutionResult& upstream) {
    if (exhausted_account_status_from_upstream(upstream).has_value()) {
        return false;
    }
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
        if (const auto exhausted = exhausted_account_status_from_upstream(upstream); exhausted.has_value()) {
            if (upstream.error_code.empty() || upstream.error_code == "upstream_error") {
                upstream.error_code = *exhausted == "quota_blocked" ? "insufficient_quota" : "rate_limit_exceeded";
            }
        }
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
        emit_runtime_signal(
            "warn",
            "upstream_retry",
            "upstream retry attempt=" + std::to_string(attempt + 1) + " status=" + std::to_string(upstream.status) +
                " code=" + upstream.error_code
        );
        // Fixed backoff between retry attempts to avoid hammering a struggling upstream.
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (upstream.status >= 400) {
        if (const auto exhausted = exhausted_account_status_from_upstream(upstream); exhausted.has_value()) {
            emit_runtime_signal(
                "warn",
                "account_exhausted",
                "upstream signaled account exhaustion status=" + *exhausted + " status=" +
                    std::to_string(upstream.status) + " code=" + upstream.error_code
            );
        } else {
            emit_runtime_signal(
                upstream.status >= 500 ? "error" : "warn",
                "upstream_error",
                "upstream request failed status=" + std::to_string(upstream.status) + " code=" + upstream.error_code
            );
        }
    }
    return upstream;
}

} // namespace tightrope::proxy::internal
