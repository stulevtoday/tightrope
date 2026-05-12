#include "proxy_service.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>

#include <glaze/glaze.hpp>

#include "internal/proxy_error_policy.h"
#include "internal/proxy_service_helpers.h"
#include "logging/logger.h"
#include "openai/error_envelope.h"
#include "openai/model_registry.h"
#include "openai/payload_normalizer.h"
#include "openai/upstream_request_plan.h"
#include "account_traffic.h"
#include "session/http_bridge.h"
#include "session/sticky_affinity.h"
#include "stream/compact_handler.h"
#include "stream/sse_handler.h"
#include "stream/transcribe_handler.h"
#include "text/ascii.h"
#include "upstream_transport.h"

namespace tightrope::proxy {

namespace {

using Json = glz::generic;
using JsonObject = Json::object_t;

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

std::optional<std::string> serialize_json(const Json& payload) {
    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        return std::nullopt;
    }
    return serialized.value_or("{}");
}

std::optional<std::string> compact_upstream_message_from_object(const JsonObject& object) {
    constexpr std::array<std::string_view, 3> kMessageKeys = {
        "message",
        "detail",
        "error",
    };
    for (const auto key : kMessageKeys) {
        const auto it = object.find(std::string(key));
        if (it == object.end() || !it->second.is_string()) {
            continue;
        }
        const auto normalized = std::string(core::text::trim_ascii(it->second.get_string()));
        if (!normalized.empty()) {
            return normalized;
        }
    }
    return std::nullopt;
}

bool compact_output_shape_valid(const JsonObject& payload) {
    const auto output_it = payload.find("output");
    if (output_it == payload.end()) {
        return false;
    }
    return output_it->second.is_array();
}

std::string compact_upstream_fallback_message(const int status) {
    return "Upstream error: HTTP " + std::to_string(status);
}

std::string build_compact_error_body_from_upstream(
    const UpstreamExecutionResult& upstream,
    const std::string_view normalized_error_code
) {
    if (!upstream.body.empty()) {
        Json parsed;
        if (const auto ec = glz::read_json(parsed, upstream.body); !ec && parsed.is_object()) {
            const auto& object = parsed.get_object();
            const auto error_it = object.find("error");
            if (error_it != object.end() && error_it->second.is_object()) {
                return upstream.body;
            }
            if (const auto message = compact_upstream_message_from_object(object); message.has_value()) {
                return openai::build_error_envelope("upstream_error", *message, "server_error");
            }
        } else {
            const auto body_message = std::string(core::text::trim_ascii(upstream.body));
            if (!body_message.empty()) {
                return openai::build_error_envelope("upstream_error", body_message, "server_error");
            }
        }
    }

    const auto normalized_code =
        normalized_error_code.empty() ? std::string("upstream_error") : std::string(normalized_error_code);
    auto message = normalized_code == "upstream_error" ? compact_upstream_fallback_message(upstream.status)
                                                        : internal::default_error_message_for_code(normalized_code);
    if (message.empty()) {
        message = compact_upstream_fallback_message(upstream.status);
    }
    return openai::build_error_envelope(normalized_code, message, "server_error");
}

enum class CompactPayloadValidationError {
    None,
    InvalidJson,
    UnexpectedPayload,
};

CompactPayloadValidationError validate_compact_success_payload(const std::string& payload_body) {
    Json payload;
    if (const auto ec = glz::read_json(payload, payload_body); ec || !payload.is_object()) {
        return CompactPayloadValidationError::InvalidJson;
    }
    if (!compact_output_shape_valid(payload.get_object())) {
        return CompactPayloadValidationError::UnexpectedPayload;
    }
    return CompactPayloadValidationError::None;
}

bool json_array_empty_or_missing(const JsonObject& object, const std::string_view key) {
    const auto it = object.find(std::string(key));
    if (it == object.end()) {
        return true;
    }
    return it->second.is_array() && it->second.get_array().empty();
}

std::string normalize_model_slug(const std::string_view value) {
    return core::text::to_lower_ascii(core::text::trim_ascii(value));
}

std::optional<std::unordered_set<std::string>> resolve_allowed_model_filter(const ModelListPolicy& policy) {
    std::optional<std::unordered_set<std::string>> allowed_filter;
    if (!policy.allowed_models.empty()) {
        allowed_filter.emplace();
        for (const auto& model : policy.allowed_models) {
            const auto normalized = normalize_model_slug(model);
            if (!normalized.empty()) {
                allowed_filter->insert(normalized);
            }
        }
    }

    if (policy.enforced_model.has_value() && !policy.enforced_model->empty()) {
        const auto enforced = normalize_model_slug(*policy.enforced_model);
        if (!enforced.empty()) {
            if (!allowed_filter.has_value()) {
                allowed_filter.emplace();
                allowed_filter->insert(enforced);
            } else {
                std::unordered_set<std::string> intersected;
                if (allowed_filter->contains(enforced)) {
                    intersected.insert(enforced);
                }
                *allowed_filter = std::move(intersected);
            }
        }
    }
    return allowed_filter;
}

bool is_model_visible(const openai::ModelInfo& model, const std::optional<std::unordered_set<std::string>>& filter) {
    if (!filter.has_value()) {
        return true;
    }
    return filter->contains(normalize_model_slug(model.id));
}

openai::ModelRegistry apply_model_list_policy(const openai::ModelRegistry& registry, const ModelListPolicy& policy) {
    const auto filter = resolve_allowed_model_filter(policy);
    if (!filter.has_value()) {
        return registry;
    }

    std::vector<openai::ModelInfo> filtered_models;
    for (const auto& model : registry.list_models()) {
        if (is_model_visible(model, filter)) {
            filtered_models.push_back(model);
        }
    }
    return openai::ModelRegistry(std::move(filtered_models));
}

std::optional<std::string> collect_response_body_from_stream_events(const std::vector<std::string>& events) {
    std::map<std::int64_t, Json> output_items;

    for (const auto& event : events) {
        Json payload;
        if (const auto ec = glz::read_json(payload, event); ec || !payload.is_object()) {
            continue;
        }
        const auto& object = payload.get_object();
        const auto type_it = object.find("type");
        if (type_it == object.end() || !type_it->second.is_string()) {
            continue;
        }
        const auto type = type_it->second.get_string();

        if (type == "error") {
            const auto error_it = object.find("error");
            if (error_it != object.end()) {
                Json envelope = JsonObject{};
                envelope["error"] = error_it->second;
                return serialize_json(envelope);
            }
            continue;
        }

        if (type == "response.output_item.added" || type == "response.output_item.done") {
            const auto idx_it = object.find("output_index");
            const auto item_it = object.find("item");
            if (idx_it != object.end() && idx_it->second.is_number() && item_it != object.end() && item_it->second.is_object()) {
                output_items[static_cast<std::int64_t>(idx_it->second.get_number())] = item_it->second;
            }
            continue;
        }

        if (type != "response.completed" && type != "response.incomplete" && type != "response.failed") {
            continue;
        }

        const auto response_it = object.find("response");
        if (response_it == object.end() || !response_it->second.is_object()) {
            continue;
        }

        Json response_payload = response_it->second;
        auto& response_object = response_payload.get_object();
        if (json_array_empty_or_missing(response_object, "output") && !output_items.empty()) {
            Json output = Json::array_t{};
            for (auto& [index, item] : output_items) {
                static_cast<void>(index);
                output.get_array().push_back(std::move(item));
            }
            response_object["output"] = std::move(output);
        }

        return serialize_json(response_payload);
    }

    return std::nullopt;
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

bool should_retry_previous_response_not_found(
    const bool,
    const bool continuation_request,
    const bool contains_function_call_output,
    const UpstreamExecutionResult& upstream
) {
    if (!continuation_request || contains_function_call_output) {
        return false;
    }
    return internal::resolved_upstream_error_code(upstream) == "previous_response_not_found";
}

bool should_retry_previous_response_with_bridge_rewrite(
    const bool backend_codex_route,
    const bool continuation_request,
    const bool contains_function_call_output,
    const UpstreamExecutionResult& upstream
) {
    if (!backend_codex_route || !continuation_request || !contains_function_call_output) {
        return false;
    }
    return internal::resolved_upstream_error_code(upstream) == "previous_response_not_found";
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

std::string default_error_type_for_code(const std::string_view code) {
    const auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(code));
    if (normalized == "upstream_error" || normalized == "upstream_unavailable" ||
        normalized == "upstream_request_timeout" || normalized == "upstream_incomplete" ||
        normalized == "stream_incomplete" || normalized == "server_error") {
        return "server_error";
    }
    return "invalid_request_error";
}

std::string resolve_error_type(const internal::UpstreamErrorDetails& details, const std::string_view error_code) {
    if (!details.type.empty()) {
        return details.type;
    }
    return default_error_type_for_code(error_code);
}

openai::HeaderMap build_downstream_response_headers(
    const std::string_view account_id,
    const openai::HeaderMap& upstream_headers
) {
    auto headers = internal::build_codex_usage_headers_for_account(account_id);
    internal::append_upstream_passthrough_headers(headers, upstream_headers);
    return headers;
}

} // namespace

ProxyJsonResult collect_responses_json(
    const std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers
) {
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "responses_json_request_received",
        internal::build_proxy_request_detail(route, raw_request_body.size(), inbound_headers)
    );
    if (!internal::is_supported_responses_route(route)) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_route_rejected",
            "route=" + std::string(route)
        );
        return {
            .status = 404,
            .body = openai::build_error_envelope("not_found", "Route not found"),
        };
    }

    auto bridged_headers = inbound_headers;
    const auto turn_state = session::ensure_turn_state_header(bridged_headers);
    static_cast<void>(turn_state);
    const bool backend_codex_route = route == "/backend-api/codex/responses";

    const auto affinity = session::resolve_sticky_affinity(raw_request_body, bridged_headers);
    const auto continuation_guard =
        internal::guard_backend_codex_previous_response(route, raw_request_body, bridged_headers, affinity);
    std::string request_body = continuation_guard.request_body;
    bool continuation_request = continuation_guard.continuation_request;
    bool continuation_contains_function_call_output = continuation_guard.contains_function_call_output;
    bool sticky_reused = affinity.from_persistence;
    const auto& preferred_account_id = continuation_guard.preferred_account_id;
    const auto& continuity_account_id = continuation_guard.continuity_account_id;
    if (continuation_request &&
        (internal::optional_string_has_value(preferred_account_id) ||
         internal::optional_string_has_value(continuity_account_id))) {
        sticky_reused = true;
    }
    std::string access_token;
    std::string traffic_account_id;
    auto resolved_affinity = affinity;
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
                      request_body,
                      bridged_headers,
                      continuation_request,
                      continuation_contains_function_call_output
                  );
        if (stripped.has_value()) {
            request_body = *stripped;
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
            "responses_previous_response_account_unavailable",
            "route=" + std::string(route) + " account_id=" + strict_credential_preference
        );
        return {
            .status = 409,
            .body = openai::build_error_envelope(
                "previous_response_account_unavailable",
                "Previous response account is unavailable",
                "invalid_request_error",
                "previous_response_id"
            ),
            .sticky_reused = sticky_reused,
        };
    }
    openai::UpstreamRequestPlan plan;
    try {
        plan = openai::build_responses_http_request_plan(
            request_body,
            bridged_headers,
            access_token,
            resolved_affinity.account_id,
            ""
        );
    } catch (const std::exception& error) {
        const auto capture_path =
            capture_invalid_payload_snapshot(route, request_body, inbound_headers, std::string_view(error.what()));
        auto detail = std::string("error=") + error.what();
        if (capture_path.has_value()) {
            detail += " capture_path=" + *capture_path;
        }
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_payload_invalid",
            detail
        );
        return {
            .status = 400,
            .body = openai::build_error_envelope(
                "invalid_request_error",
                "Invalid request payload",
                "invalid_request_error",
                "input"
            ),
        };
    }
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "responses_json_upstream_plan_ready",
        internal::build_upstream_plan_detail(plan)
    );
    internal::maybe_log_upstream_payload_trace(route, plan);
    session::persist_sticky_affinity(resolved_affinity);

    auto execute_upstream = [&](const openai::UpstreamRequestPlan& current_plan) {
        const ScopedAccountTrafficContext traffic_scope(traffic_account_id);
        return internal::execute_upstream_with_retry_budget(
            current_plan,
            internal::kStreamRetryBudget,
            internal::should_retry_stream_result_with_incomplete,
            "responses_upstream_retry"
        );
    };

    auto upstream = execute_upstream(plan);
    if (upstream.status == 401 && !resolved_affinity.account_id.empty()) {
        if (!handle_deactivated_401_if_present(upstream, resolved_affinity.account_id)) {
            if (const auto refreshed = session::refresh_upstream_account_credentials(resolved_affinity.account_id);
                refreshed.has_value()) {
                resolved_affinity.account_id = refreshed->account_id;
                access_token = refreshed->access_token;
                if (refreshed->internal_account_id > 0) {
                    traffic_account_id = std::to_string(refreshed->internal_account_id);
                }
                try {
                    plan = openai::build_responses_http_request_plan(
                        request_body,
                        bridged_headers,
                        access_token,
                        resolved_affinity.account_id,
                        ""
                    );
                    upstream = execute_upstream(plan);
                    (void)handle_deactivated_401_if_present(upstream, resolved_affinity.account_id);
                } catch (const std::exception&) {
                    core::logging::log_event(
                        core::logging::LogLevel::Warning,
                        "runtime",
                        "proxy",
                        "responses_refresh_rebuild_failed"
                    );
                }
            }
        }
    }
    if (should_retry_previous_response_with_bridge_rewrite(
            backend_codex_route,
            continuation_request,
            continuation_contains_function_call_output,
            upstream
        )) {
        if (const auto rewritten = session::replace_previous_response_id_from_bridge(request_body, bridged_headers);
            rewritten.has_value()) {
            core::logging::log_event(
                core::logging::LogLevel::Info,
                "runtime",
                "proxy",
                "responses_previous_response_bridge_retry",
                "route=" + std::string(route)
            );
            try {
                plan = openai::build_responses_http_request_plan(
                    *rewritten,
                    bridged_headers,
                    access_token,
                    resolved_affinity.account_id,
                    ""
                );
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
    if (should_retry_previous_response_not_found(
            backend_codex_route,
            continuation_request,
            continuation_contains_function_call_output,
            upstream
        )) {
        if (const auto stripped = fallback_body_with_local_context(
                request_body,
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
                plan = openai::build_responses_http_request_plan(
                    *stripped,
                    bridged_headers,
                    access_token,
                    resolved_affinity.account_id,
                    ""
                );
                request_body = *stripped;
                continuation_request = session::request_has_previous_response_id(request_body);
                continuation_contains_function_call_output =
                    continuation_request && session::request_contains_function_call_output(request_body);
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
    const bool marked_exhausted = handle_exhausted_account_if_present(upstream, resolved_affinity.account_id);
    if (marked_exhausted && !affinity.from_header) {
        if (const auto failover_body =
                fallback_body_with_local_context(
                    request_body,
                    bridged_headers,
                    continuation_request,
                    continuation_contains_function_call_output
                );
            failover_body.has_value()) {
            session::StickyAffinityResolution failover_affinity = affinity;
            std::string failover_access_token;
            std::string failover_traffic_account_id;
            if (apply_resolved_credentials(
                    session::resolve_upstream_account_credentials("", affinity.request_model, false),
                    failover_affinity,
                    failover_access_token,
                    failover_traffic_account_id
                )) {
                try {
                    plan = openai::build_responses_http_request_plan(
                        *failover_body,
                        bridged_headers,
                        failover_access_token,
                        failover_affinity.account_id,
                        ""
                    );
                    resolved_affinity = failover_affinity;
                    access_token = std::move(failover_access_token);
                    traffic_account_id = std::move(failover_traffic_account_id);
                    request_body = *failover_body;
                    continuation_request = session::request_has_previous_response_id(request_body);
                    continuation_contains_function_call_output =
                        continuation_request && session::request_contains_function_call_output(request_body);
                    session::persist_sticky_affinity(resolved_affinity);
                    core::logging::log_event(
                        core::logging::LogLevel::Info,
                        "runtime",
                        "proxy",
                        "responses_exhausted_account_failover",
                        "route=" + std::string(route) + " account_id=" + resolved_affinity.account_id
                    );
                    upstream = execute_upstream(plan);
                    (void)handle_exhausted_account_if_present(upstream, resolved_affinity.account_id);
                } catch (const std::exception&) {
                    core::logging::log_event(
                        core::logging::LogLevel::Warning,
                        "runtime",
                        "proxy",
                        "responses_exhausted_account_failover_rebuild_failed"
                    );
                }
            }
        }
    }
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "responses_json_upstream_result",
        internal::build_upstream_result_detail(upstream)
    );
    const auto response_headers = build_downstream_response_headers(resolved_affinity.account_id, upstream.headers);
    if (upstream.status >= 400) {
        const auto details = internal::extract_upstream_error_details(upstream);
        const auto error_code = details.code.empty() ? std::string("upstream_error") : details.code;
        const auto error_type = resolve_error_type(details, error_code);
        const auto message =
            details.message.empty() ? internal::default_error_message_for_code(error_code) : details.message;
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_upstream_error",
            "status=" + std::to_string(upstream.status) + " code=" + error_code + " type=" + error_type
        );
        return {
            .status = upstream.status,
            .body = upstream.body.empty()
                        ? openai::build_error_envelope(error_code, message, error_type, details.param)
                                          : upstream.body,
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    }
    if (!upstream.body.empty()) {
        session::remember_response_id_from_json(bridged_headers, plan.body, upstream.body, resolved_affinity.account_id);
        return {
            .status = upstream.status,
            .body = upstream.body,
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    }

    if (!upstream.events.empty()) {
        session::remember_response_id_from_events(bridged_headers, plan.body, upstream.events, resolved_affinity.account_id);
        if (const auto collected = collect_response_body_from_stream_events(upstream.events); collected.has_value()) {
            return {
                .status = upstream.status,
                .body = *collected,
                .headers = response_headers,
                .sticky_reused = sticky_reused,
            };
        }
    }

    return {
        .status = 502,
        .body = openai::build_error_envelope("upstream_incomplete", "Upstream response was incomplete", "server_error"),
        .headers = response_headers,
        .sticky_reused = sticky_reused,
    };
}

ProxyJsonResult collect_responses_compact(
    const std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers
) {
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "compact_request_received",
        internal::build_proxy_request_detail(route, raw_request_body.size(), inbound_headers)
    );
    if (!internal::is_supported_compact_route(route)) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "compact_route_rejected",
            "route=" + std::string(route)
        );
        return {
            .status = 404,
            .body = openai::build_error_envelope("not_found", "Route not found"),
        };
    }

    const auto affinity = session::resolve_sticky_affinity(raw_request_body, inbound_headers);
    const bool sticky_reused = affinity.from_persistence;
    std::string access_token;
    std::string traffic_account_id;
    auto resolved_affinity = affinity;
    if (const auto credentials =
            session::resolve_upstream_account_credentials(affinity.account_id, affinity.request_model);
        credentials.has_value()) {
        resolved_affinity.account_id = credentials->account_id;
        access_token = credentials->access_token;
        if (credentials->internal_account_id > 0) {
            traffic_account_id = std::to_string(credentials->internal_account_id);
        }
    }
    openai::UpstreamRequestPlan plan;
    try {
        plan = openai::build_compact_http_request_plan(
            raw_request_body,
            inbound_headers,
            access_token,
            resolved_affinity.account_id,
            ""
        );
    } catch (const std::exception& error) {
        const auto capture_path =
            capture_invalid_payload_snapshot(route, raw_request_body, inbound_headers, std::string_view(error.what()));
        auto detail = std::string("error=") + error.what();
        if (capture_path.has_value()) {
            detail += " capture_path=" + *capture_path;
        }
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "proxy", "compact_payload_invalid", detail);
        return {
            .status = 400,
            .body = openai::build_error_envelope(
                "invalid_request_error",
                "Invalid request payload",
                "invalid_request_error",
                "input"
            ),
        };
    }
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "compact_upstream_plan_ready",
        internal::build_upstream_plan_detail(plan)
    );
    internal::maybe_log_upstream_payload_trace(route, plan);
    session::persist_sticky_affinity(resolved_affinity);

    auto execute_upstream = [&](const openai::UpstreamRequestPlan& current_plan) {
        const ScopedAccountTrafficContext traffic_scope(traffic_account_id);
        return internal::execute_upstream_with_retry_budget(
            current_plan,
            internal::kCompactRetryBudget,
            internal::should_retry_compact_result,
            "compact_upstream_retry"
        );
    };

    auto upstream = execute_upstream(plan);
    if (upstream.status == 401 && !resolved_affinity.account_id.empty()) {
        if (!handle_deactivated_401_if_present(upstream, resolved_affinity.account_id)) {
            if (const auto refreshed = session::refresh_upstream_account_credentials(resolved_affinity.account_id);
                refreshed.has_value()) {
                resolved_affinity.account_id = refreshed->account_id;
                access_token = refreshed->access_token;
                if (refreshed->internal_account_id > 0) {
                    traffic_account_id = std::to_string(refreshed->internal_account_id);
                }
                try {
                    plan = openai::build_compact_http_request_plan(
                        raw_request_body,
                        inbound_headers,
                        access_token,
                        resolved_affinity.account_id,
                        ""
                    );
                    upstream = execute_upstream(plan);
                    (void)handle_deactivated_401_if_present(upstream, resolved_affinity.account_id);
                } catch (const std::exception&) {
                    core::logging::log_event(
                        core::logging::LogLevel::Warning,
                        "runtime",
                        "proxy",
                        "compact_refresh_rebuild_failed"
                    );
                }
            }
        }
    }
    (void)handle_exhausted_account_if_present(upstream, resolved_affinity.account_id);
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "compact_upstream_result",
        internal::build_upstream_result_detail(upstream)
    );
    const auto response_headers = build_downstream_response_headers(resolved_affinity.account_id, upstream.headers);
    if (upstream.status >= 400) {
        const auto error_code = internal::resolved_upstream_error_code(upstream);
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "compact_upstream_error",
            "status=" + std::to_string(upstream.status) + " code=" + error_code
        );
        return {
            .status = upstream.status,
            .body = build_compact_error_body_from_upstream(upstream, error_code),
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    }
    switch (validate_compact_success_payload(upstream.body)) {
    case CompactPayloadValidationError::None:
        return {
            .status = upstream.status,
            .body = upstream.body,
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    case CompactPayloadValidationError::InvalidJson:
        return {
            .status = 502,
            .body = openai::build_error_envelope("upstream_error", "Invalid JSON from upstream", "server_error"),
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    case CompactPayloadValidationError::UnexpectedPayload:
        return {
            .status = 502,
            .body = openai::build_error_envelope("upstream_error", "Unexpected upstream payload", "server_error"),
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    }
    return {
        .status = 502,
        .body = openai::build_error_envelope("upstream_error", "Unexpected upstream payload", "server_error"),
        .headers = response_headers,
        .sticky_reused = sticky_reused,
    };
}

ProxyJsonResult collect_memories_trace_summarize(
    const std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers
) {
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "memories_trace_summarize_request_received",
        internal::build_proxy_request_detail(route, raw_request_body.size(), inbound_headers)
    );
    if (!internal::is_supported_memories_route(route)) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "memories_trace_summarize_route_rejected",
            "route=" + std::string(route)
        );
        return {
            .status = 404,
            .body = openai::build_error_envelope("not_found", "Route not found"),
        };
    }

    const auto affinity = session::resolve_sticky_affinity("", inbound_headers);
    const bool sticky_reused = affinity.from_persistence;
    std::string access_token;
    std::string traffic_account_id;
    auto resolved_affinity = affinity;
    if (const auto credentials = session::resolve_upstream_account_credentials(affinity.account_id, "");
        credentials.has_value()) {
        resolved_affinity.account_id = credentials->account_id;
        access_token = credentials->access_token;
        if (credentials->internal_account_id > 0) {
            traffic_account_id = std::to_string(credentials->internal_account_id);
        }
    }

    openai::UpstreamRequestPlan plan;
    try {
        plan = openai::build_memories_trace_summarize_http_request_plan(
            raw_request_body,
            inbound_headers,
            access_token,
            resolved_affinity.account_id,
            ""
        );
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
            "memories_trace_summarize_payload_invalid",
            detail
        );
        return {
            .status = 400,
            .body = openai::build_error_envelope(
                "invalid_request_error",
                "Invalid request payload",
                "invalid_request_error",
                "input"
            ),
        };
    }

    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "memories_trace_summarize_upstream_plan_ready",
        internal::build_upstream_plan_detail(plan)
    );
    internal::maybe_log_upstream_payload_trace(route, plan);
    session::persist_sticky_affinity(resolved_affinity);

    auto execute_upstream = [&](const openai::UpstreamRequestPlan& current_plan) {
        const ScopedAccountTrafficContext traffic_scope(traffic_account_id);
        return internal::execute_upstream_with_retry_budget(
            current_plan,
            internal::kCompactRetryBudget,
            internal::should_retry_compact_result,
            "memories_trace_summarize_upstream_retry"
        );
    };

    auto upstream = execute_upstream(plan);
    if (upstream.status == 401 && !resolved_affinity.account_id.empty()) {
        if (!handle_deactivated_401_if_present(upstream, resolved_affinity.account_id)) {
            if (const auto refreshed = session::refresh_upstream_account_credentials(resolved_affinity.account_id);
                refreshed.has_value()) {
                resolved_affinity.account_id = refreshed->account_id;
                access_token = refreshed->access_token;
                if (refreshed->internal_account_id > 0) {
                    traffic_account_id = std::to_string(refreshed->internal_account_id);
                }
                try {
                    plan = openai::build_memories_trace_summarize_http_request_plan(
                        raw_request_body,
                        inbound_headers,
                        access_token,
                        resolved_affinity.account_id,
                        ""
                    );
                    upstream = execute_upstream(plan);
                    (void)handle_deactivated_401_if_present(upstream, resolved_affinity.account_id);
                } catch (const std::exception&) {
                    core::logging::log_event(
                        core::logging::LogLevel::Warning,
                        "runtime",
                        "proxy",
                        "memories_trace_summarize_refresh_rebuild_failed"
                    );
                }
            }
        }
    }
    (void)handle_exhausted_account_if_present(upstream, resolved_affinity.account_id);
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "memories_trace_summarize_upstream_result",
        internal::build_upstream_result_detail(upstream)
    );

    const auto response_headers = build_downstream_response_headers(resolved_affinity.account_id, upstream.headers);
    if (upstream.status >= 400) {
        const auto details = internal::extract_upstream_error_details(upstream);
        const auto error_code = details.code.empty() ? std::string("upstream_error") : details.code;
        const auto error_type = resolve_error_type(details, error_code);
        const auto message =
            details.message.empty() ? internal::default_error_message_for_code(error_code) : details.message;
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "memories_trace_summarize_upstream_error",
            "status=" + std::to_string(upstream.status) + " code=" + error_code + " type=" + error_type
        );
        return {
            .status = upstream.status,
            .body = upstream.body.empty()
                        ? openai::build_error_envelope(error_code, message, error_type, details.param)
                                          : upstream.body,
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    }

    if (!upstream.body.empty()) {
        return {
            .status = upstream.status,
            .body = upstream.body,
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    }

    return {
        .status = 502,
        .body = openai::build_error_envelope("upstream_incomplete", "Upstream response was incomplete", "server_error"),
        .headers = response_headers,
        .sticky_reused = sticky_reused,
    };
}

ProxyJsonResult collect_models(
    const std::string_view route,
    const ModelListPolicy& policy,
    const openai::HeaderMap& inbound_headers
) {
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "models_request_received",
        "route=" + std::string(route)
    );
    if (!internal::is_supported_models_route(route)) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "models_route_rejected",
            "route=" + std::string(route)
        );
        return {
            .status = 404,
            .body = openai::build_error_envelope("not_found", "Route not found"),
        };
    }

    auto model_headers = inbound_headers;
    const auto affinity = session::resolve_sticky_affinity("", inbound_headers);
    std::string usage_account_id = affinity.account_id;
    if (const auto credentials = session::resolve_upstream_account_credentials(affinity.account_id, "");
        credentials.has_value()) {
        model_headers["authorization"] = "Bearer " + credentials->access_token;
        model_headers["chatgpt-account-id"] = credentials->account_id;
        usage_account_id = credentials->account_id;
    }
    const auto response_headers = internal::build_codex_usage_headers_for_account(usage_account_id);

    const auto registry = apply_model_list_policy(openai::build_model_registry_from_upstream(model_headers), policy);
    if (route == "/api/models" || route == "/backend-api/codex/models") {
        return {
            .status = 200,
            .body = internal::build_dashboard_models_payload(registry),
            .headers = response_headers,
        };
    }

    return {
        .status = 200,
        .body = internal::build_public_models_payload(registry),
        .headers = response_headers,
    };
}

ProxyJsonResult collect_transcribe(
    const std::string_view route,
    const std::string_view model,
    const std::string_view prompt,
    const std::string_view audio_bytes,
    const openai::HeaderMap& inbound_headers
) {
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "transcribe_request_received",
        internal::build_proxy_request_detail(route, audio_bytes.size(), inbound_headers) + " model=" + std::string(model)
    );
    if (!internal::is_supported_transcribe_route(route)) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "transcribe_route_rejected",
            "route=" + std::string(route)
        );
        return {
            .status = 404,
            .body = openai::build_error_envelope("not_found", "Route not found"),
        };
    }

    if (route == "/v1/audio/transcriptions" && model != internal::kTranscriptionModel) {
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "transcribe_model_rejected",
            "model=" + std::string(model)
        );
        return {
            .status = 400,
            .body = internal::build_invalid_transcription_model_error(model),
        };
    }

    if (audio_bytes.empty()) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "proxy", "transcribe_audio_missing");
        return {
            .status = 400,
            .body = openai::build_error_envelope("invalid_request_error", "Audio file is required"),
        };
    }

    const auto affinity = session::resolve_sticky_affinity("", inbound_headers);
    std::string access_token;
    std::string account_id = affinity.account_id;
    std::string traffic_account_id;
    if (const auto credentials = session::resolve_upstream_account_credentials(account_id, model);
        credentials.has_value()) {
        account_id = credentials->account_id;
        access_token = credentials->access_token;
        if (credentials->internal_account_id > 0) {
            traffic_account_id = std::to_string(credentials->internal_account_id);
        }
    }
    const auto plan = openai::build_transcribe_http_request_plan(
        prompt,
        "audio.wav",
        "application/octet-stream",
        audio_bytes,
        inbound_headers,
        access_token,
        account_id
    );
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "transcribe_upstream_plan_ready",
        internal::build_upstream_plan_detail(plan)
    );
    internal::maybe_log_upstream_payload_trace(route, plan);
    auto execute_upstream = [&](const openai::UpstreamRequestPlan& current_plan) {
        const ScopedAccountTrafficContext traffic_scope(traffic_account_id);
        auto result = execute_upstream_plan(current_plan);
        result.error_code = internal::resolved_upstream_error_code(result);
        return result;
    };

    auto upstream = execute_upstream(plan);
    if (upstream.status == 401 && !account_id.empty()) {
        if (!handle_deactivated_401_if_present(upstream, account_id)) {
            if (const auto refreshed = session::refresh_upstream_account_credentials(account_id); refreshed.has_value()) {
                account_id = refreshed->account_id;
                access_token = refreshed->access_token;
                if (refreshed->internal_account_id > 0) {
                    traffic_account_id = std::to_string(refreshed->internal_account_id);
                }
                const auto refreshed_plan = openai::build_transcribe_http_request_plan(
                    prompt,
                    "audio.wav",
                    "application/octet-stream",
                    audio_bytes,
                    inbound_headers,
                    access_token,
                    account_id
                );
                upstream = execute_upstream(refreshed_plan);
                (void)handle_deactivated_401_if_present(upstream, account_id);
            }
        }
    }
    (void)handle_exhausted_account_if_present(upstream, account_id);
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "transcribe_upstream_result",
        internal::build_upstream_result_detail(upstream)
    );
    const auto response_headers = build_downstream_response_headers(account_id, upstream.headers);
    if (upstream.status >= 400) {
        const auto details = internal::extract_upstream_error_details(upstream);
        const auto error_code = details.code.empty() ? std::string("upstream_error") : details.code;
        const auto error_type = resolve_error_type(details, error_code);
        const auto message =
            details.message.empty() ? internal::default_error_message_for_code(error_code) : details.message;
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "transcribe_upstream_error",
            "status=" + std::to_string(upstream.status) + " code=" + error_code + " type=" + error_type
        );
        return {
            .status = upstream.status,
            .body = upstream.body.empty()
                        ? openai::build_error_envelope(error_code, message, error_type, details.param)
                                          : upstream.body,
            .headers = response_headers,
        };
    }
    if (!upstream.body.empty()) {
        return {
            .status = upstream.status,
            .body = upstream.body,
            .headers = response_headers,
        };
    }

    return {
        .status = 502,
        .body = openai::build_error_envelope("upstream_incomplete", "Upstream response was incomplete", "server_error"),
        .headers = response_headers,
    };
}

ProxySseResult stream_responses_sse(
    const std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers
) {
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "responses_sse_request_received",
        internal::build_proxy_request_detail(route, raw_request_body.size(), inbound_headers)
    );
    if (!internal::is_supported_responses_route(route)) {
        return {
            .status = 404,
            .events = stream::build_responses_sse_failure("not_found", "Route not found", "invalid_request_error"),
        };
    }

    auto bridged_headers = inbound_headers;
    const auto turn_state = session::ensure_turn_state_header(bridged_headers);
    static_cast<void>(turn_state);
    const bool backend_codex_route = route == "/backend-api/codex/responses";

    const auto affinity = session::resolve_sticky_affinity(raw_request_body, bridged_headers);
    const auto continuation_guard =
        internal::guard_backend_codex_previous_response(route, raw_request_body, bridged_headers, affinity);
    std::string request_body = continuation_guard.request_body;
    bool continuation_request = continuation_guard.continuation_request;
    bool continuation_contains_function_call_output = continuation_guard.contains_function_call_output;
    bool sticky_reused = affinity.from_persistence;
    const auto& preferred_account_id = continuation_guard.preferred_account_id;
    const auto& continuity_account_id = continuation_guard.continuity_account_id;
    if (continuation_request &&
        (internal::optional_string_has_value(preferred_account_id) ||
         internal::optional_string_has_value(continuity_account_id))) {
        sticky_reused = true;
    }
    std::string access_token;
    std::string traffic_account_id;
    auto resolved_affinity = affinity;
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
                      request_body,
                      bridged_headers,
                      continuation_request,
                      continuation_contains_function_call_output
                  );
        if (stripped.has_value()) {
            request_body = *stripped;
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
            "responses_sse_previous_response_account_unavailable",
            "route=" + std::string(route) + " account_id=" + strict_credential_preference
        );
        return {
            .status = 409,
            .events = stream::build_responses_sse_failure(
                "previous_response_account_unavailable",
                "Previous response account is unavailable",
                "invalid_request_error",
                "previous_response_id"
            ),
            .sticky_reused = sticky_reused,
        };
    }
    const auto upstream_stream_transport = session::resolve_upstream_stream_transport_setting();
    openai::UpstreamRequestPlan plan;
    try {
        const auto registry = openai::build_default_model_registry();
        plan = openai::build_responses_stream_request_plan(
            request_body,
            bridged_headers,
            access_token,
            resolved_affinity.account_id,
            registry,
            upstream_stream_transport
        );
        plan.preserve_upstream_websocket_session = backend_codex_route && plan.transport == "websocket";
    } catch (const std::exception& error) {
        const auto capture_path =
            capture_invalid_payload_snapshot(route, request_body, inbound_headers, std::string_view(error.what()));
        auto detail = std::string("error=") + error.what();
        if (capture_path.has_value()) {
            detail += " capture_path=" + *capture_path;
        }
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_sse_payload_invalid",
            detail
        );
        return {
            .status = 400,
            .events = stream::build_responses_sse_failure(
                "invalid_request_error",
                "Invalid request payload",
                "invalid_request_error",
                "input"
            ),
        };
    }
    const auto turn_state_it = bridged_headers.find("x-codex-turn-state");
    if (turn_state_it != bridged_headers.end() && !turn_state_it->second.empty()) {
        plan.headers["x-codex-turn-state"] = turn_state_it->second;
    }
    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "responses_sse_upstream_plan_ready",
        internal::build_upstream_plan_detail(plan)
    );
    internal::maybe_log_upstream_payload_trace(route, plan);
    session::persist_sticky_affinity(resolved_affinity);

    auto execute_upstream = [&](const openai::UpstreamRequestPlan& current_plan) {
        const ScopedAccountTrafficContext traffic_scope(traffic_account_id);
        return internal::execute_upstream_with_retry_budget(
            current_plan,
            internal::kStreamRetryBudget,
            internal::should_retry_stream_result_with_incomplete,
            "responses_upstream_retry"
        );
    };

    auto upstream = execute_upstream(plan);
    if (upstream.status == 401 && !resolved_affinity.account_id.empty()) {
        if (!handle_deactivated_401_if_present(upstream, resolved_affinity.account_id)) {
            if (const auto refreshed = session::refresh_upstream_account_credentials(resolved_affinity.account_id);
                refreshed.has_value()) {
                resolved_affinity.account_id = refreshed->account_id;
                access_token = refreshed->access_token;
                if (refreshed->internal_account_id > 0) {
                    traffic_account_id = std::to_string(refreshed->internal_account_id);
                }
                try {
                    const auto registry = openai::build_default_model_registry();
                    plan = openai::build_responses_stream_request_plan(
                        request_body,
                        bridged_headers,
                        access_token,
                        resolved_affinity.account_id,
                        registry,
                        upstream_stream_transport
                    );
                    plan.preserve_upstream_websocket_session =
                        backend_codex_route && plan.transport == "websocket";
                    const auto turn_state_it = bridged_headers.find("x-codex-turn-state");
                    if (turn_state_it != bridged_headers.end() && !turn_state_it->second.empty()) {
                        plan.headers["x-codex-turn-state"] = turn_state_it->second;
                    }
                    upstream = execute_upstream(plan);
                    (void)handle_deactivated_401_if_present(upstream, resolved_affinity.account_id);
                } catch (const std::exception&) {
                    core::logging::log_event(
                        core::logging::LogLevel::Warning,
                        "runtime",
                        "proxy",
                        "responses_sse_refresh_rebuild_failed"
                    );
                }
            }
        }
    }
    if (should_retry_previous_response_with_bridge_rewrite(
            backend_codex_route,
            continuation_request,
            continuation_contains_function_call_output,
            upstream
        )) {
        if (const auto rewritten = session::replace_previous_response_id_from_bridge(request_body, bridged_headers);
            rewritten.has_value()) {
            core::logging::log_event(
                core::logging::LogLevel::Info,
                "runtime",
                "proxy",
                "responses_previous_response_bridge_retry",
                "route=" + std::string(route)
            );
            try {
                const auto registry = openai::build_default_model_registry();
                plan = openai::build_responses_stream_request_plan(
                    *rewritten,
                    bridged_headers,
                    access_token,
                    resolved_affinity.account_id,
                    registry,
                    upstream_stream_transport
                );
                plan.preserve_upstream_websocket_session = backend_codex_route && plan.transport == "websocket";
                const auto turn_state_it = bridged_headers.find("x-codex-turn-state");
                if (turn_state_it != bridged_headers.end() && !turn_state_it->second.empty()) {
                    plan.headers["x-codex-turn-state"] = turn_state_it->second;
                }
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
    if (should_retry_previous_response_not_found(
            backend_codex_route,
            continuation_request,
            continuation_contains_function_call_output,
            upstream
        )) {
        if (const auto stripped = fallback_body_with_local_context(
                request_body,
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
                const auto registry = openai::build_default_model_registry();
                plan = openai::build_responses_stream_request_plan(
                    *stripped,
                    bridged_headers,
                    access_token,
                    resolved_affinity.account_id,
                    registry,
                    upstream_stream_transport
                );
                plan.preserve_upstream_websocket_session = backend_codex_route && plan.transport == "websocket";
                const auto turn_state_it = bridged_headers.find("x-codex-turn-state");
                if (turn_state_it != bridged_headers.end() && !turn_state_it->second.empty()) {
                    plan.headers["x-codex-turn-state"] = turn_state_it->second;
                }
                request_body = *stripped;
                continuation_request = session::request_has_previous_response_id(request_body);
                continuation_contains_function_call_output =
                    continuation_request && session::request_contains_function_call_output(request_body);
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
    const bool sse_marked_exhausted = handle_exhausted_account_if_present(upstream, resolved_affinity.account_id);
    if (sse_marked_exhausted && !affinity.from_header) {
        if (const auto failover_body =
                fallback_body_with_local_context(
                    request_body,
                    bridged_headers,
                    continuation_request,
                    continuation_contains_function_call_output
                );
            failover_body.has_value()) {
            session::StickyAffinityResolution failover_affinity = affinity;
            std::string failover_access_token;
            std::string failover_traffic_account_id;
            if (apply_resolved_credentials(
                    session::resolve_upstream_account_credentials("", affinity.request_model, false),
                    failover_affinity,
                    failover_access_token,
                    failover_traffic_account_id
                )) {
                try {
                    const auto registry = openai::build_default_model_registry();
                    plan = openai::build_responses_stream_request_plan(
                        *failover_body,
                        bridged_headers,
                        failover_access_token,
                        failover_affinity.account_id,
                        registry,
                        upstream_stream_transport
                    );
                    plan.preserve_upstream_websocket_session =
                        backend_codex_route && plan.transport == "websocket";
                    const auto turn_state_it = bridged_headers.find("x-codex-turn-state");
                    if (turn_state_it != bridged_headers.end() && !turn_state_it->second.empty()) {
                        plan.headers["x-codex-turn-state"] = turn_state_it->second;
                    }
                    resolved_affinity = failover_affinity;
                    access_token = std::move(failover_access_token);
                    traffic_account_id = std::move(failover_traffic_account_id);
                    request_body = *failover_body;
                    continuation_request = session::request_has_previous_response_id(request_body);
                    continuation_contains_function_call_output =
                        continuation_request && session::request_contains_function_call_output(request_body);
                    session::persist_sticky_affinity(resolved_affinity);
                    core::logging::log_event(
                        core::logging::LogLevel::Info,
                        "runtime",
                        "proxy",
                        "responses_sse_exhausted_account_failover",
                        "route=" + std::string(route) + " account_id=" + resolved_affinity.account_id
                    );
                    upstream = execute_upstream(plan);
                    (void)handle_exhausted_account_if_present(upstream, resolved_affinity.account_id);
                } catch (const std::exception&) {
                    core::logging::log_event(
                        core::logging::LogLevel::Warning,
                        "runtime",
                        "proxy",
                        "responses_sse_exhausted_account_failover_rebuild_failed"
                    );
                }
            }
        }
    }
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "responses_sse_upstream_result",
        internal::build_upstream_result_detail(upstream)
    );
    const auto response_headers = build_downstream_response_headers(resolved_affinity.account_id, upstream.headers);
    if (upstream.status >= 400) {
        const auto details = internal::extract_upstream_error_details(upstream);
        const auto error_code = details.code.empty() ? std::string("upstream_error") : details.code;
        const auto error_type = resolve_error_type(details, error_code);
        const auto message =
            details.message.empty() ? internal::default_error_message_for_code(error_code) : details.message;
        return {
            .status = upstream.status,
            .events = stream::build_responses_sse_failure(error_code, message, error_type, details.param),
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    }
    const auto downstream_status = upstream.status == 101 ? 200 : upstream.status;

    if (!upstream.events.empty()) {
        session::remember_response_id_from_events(bridged_headers, plan.body, upstream.events, resolved_affinity.account_id);
        return {
            .status = downstream_status,
            .events = upstream.events,
            .headers = response_headers,
            .sticky_reused = sticky_reused,
        };
    }

    return {
        .status = 502,
        .events = stream::build_responses_sse_failure(
            "stream_incomplete",
            "Upstream stream was incomplete",
            "server_error"
        ),
        .headers = response_headers,
        .sticky_reused = sticky_reused,
    };
}

} // namespace tightrope::proxy
