#include "ws_proxy.h"

#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "internal/proxy_error_policy.h"
#include "internal/proxy_service_helpers.h"
#include "logging/logger.h"
#include "openai/provider_contract.h"
#include "openai/upstream_request_plan.h"
#include "session/http_bridge.h"
#include "session/sticky_affinity.h"
#include "upstream_transport.h"

namespace tightrope::proxy {

namespace {

std::string upstream_disconnect_message(const int close_code) {
    if (close_code > 0) {
        return "Upstream websocket closed before response.completed (close_code=" + std::to_string(close_code) + ")";
    }
    return "Upstream websocket closed before response.completed";
}

ProxyWsResult build_websocket_error_result(
    int status,
    bool accepted,
    int close_code,
    const std::string& error_code,
    const std::string& message,
    const std::string& error_type = "server_error"
) {
    return {
        .status = status,
        .accepted = accepted,
        .close_code = close_code,
        .frames = {openai::build_websocket_error_event_json(status, error_code, message, error_type)},
    };
}

ProxyWsResult build_response_failed_result(
    int status,
    bool accepted,
    int close_code,
    const std::string& error_code,
    const std::string& message
) {
    return {
        .status = status,
        .accepted = accepted,
        .close_code = close_code,
        .frames = {openai::build_websocket_response_failed_event_json(error_code, message)},
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

} // namespace

ProxyWsResult proxy_responses_websocket(
    const std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers
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

    auto bridged_headers = inbound_headers;
    const auto turn_state = session::ensure_turn_state_header(bridged_headers);
    static_cast<void>(turn_state);
    const auto bridged_request_body = session::inject_previous_response_id_from_bridge(raw_request_body, bridged_headers);

    const auto affinity = session::resolve_sticky_affinity(bridged_request_body, bridged_headers);
    std::string access_token;
    auto resolved_affinity = affinity;
    if (const auto credentials = session::resolve_upstream_account_credentials(affinity.account_id); credentials.has_value()) {
        resolved_affinity.account_id = credentials->account_id;
        access_token = credentials->access_token;
    }
    openai::UpstreamRequestPlan plan;
    try {
        plan = openai::build_responses_websocket_request_plan(
            bridged_request_body,
            bridged_headers,
            access_token,
            resolved_affinity.account_id,
            ""
        );
    } catch (const std::exception&) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "proxy", "responses_ws_payload_invalid");
        return build_websocket_error_result(
            400,
            false,
            1008,
            "invalid_request_error",
            "Invalid request payload",
            "invalid_request_error"
        );
    }
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "responses_ws_upstream_plan_ready",
        internal::build_upstream_plan_detail(plan)
    );
    internal::maybe_log_upstream_payload_trace(route, plan);
    session::persist_sticky_affinity(resolved_affinity);

    auto upstream = internal::execute_upstream_with_retry_budget(
        plan,
        internal::kStreamRetryBudget,
        internal::should_retry_stream_result,
        "responses_ws_upstream_retry"
    );
    if (upstream.status == 401 && !resolved_affinity.account_id.empty()) {
        if (const auto refreshed = session::refresh_upstream_account_credentials(resolved_affinity.account_id);
            refreshed.has_value()) {
            resolved_affinity.account_id = refreshed->account_id;
            access_token = refreshed->access_token;
            try {
                plan = openai::build_responses_websocket_request_plan(
                    bridged_request_body,
                    bridged_headers,
                    access_token,
                    resolved_affinity.account_id,
                    ""
                );
                upstream = internal::execute_upstream_with_retry_budget(
                    plan,
                    internal::kStreamRetryBudget,
                    internal::should_retry_stream_result,
                    "responses_ws_upstream_retry"
                );
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
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "responses_ws_upstream_result",
        internal::build_upstream_result_detail(upstream)
    );
    if (upstream.status >= 400) {
        const auto code = internal::resolved_upstream_error_code(upstream);
        const auto message = internal::default_error_message_for_code(code);
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_ws_upstream_error",
            "status=" + std::to_string(upstream.status) + " code=" + code
        );
        return build_websocket_error_result(upstream.status, false, 1011, code, message);
    }

    const bool accepted = upstream.accepted || upstream.status == 101 || !upstream.events.empty();
    const int close_code = upstream.close_code > 0 ? upstream.close_code : (accepted ? 1000 : 1011);
    const auto error_code = internal::resolved_upstream_error_code(upstream);

    if (!accepted) {
        const auto status = upstream.status > 0 ? upstream.status : 502;
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
            internal::default_error_message_for_code(error_code)
        );
    }

    auto normalized_frames = normalize_upstream_frames(upstream.events);
    if (normalized_frames.empty()) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_ws_stream_incomplete",
            "close_code=" + std::to_string(close_code)
        );
        return build_response_failed_result(
            101,
            true,
            close_code,
            "stream_incomplete",
            upstream_disconnect_message(close_code)
        );
    }

    session::remember_response_id_from_events(bridged_headers, upstream.events);

    return {
        .status = 101,
        .accepted = true,
        .close_code = close_code,
        .frames = std::move(normalized_frames),
    };
}

} // namespace tightrope::proxy
