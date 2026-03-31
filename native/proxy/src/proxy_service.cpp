#include "proxy_service.h"

#include <cstdint>
#include <cstddef>
#include <exception>
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

std::optional<std::string> serialize_json(const Json& payload) {
    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        return std::nullopt;
    }
    return serialized.value_or("{}");
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

std::size_t upstream_payload_bytes(const UpstreamExecutionResult& upstream) {
    std::size_t bytes = upstream.body.size();
    for (const auto& event : upstream.events) {
        bytes += event.size();
    }
    return bytes;
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
    const auto bridged_request_body = session::inject_previous_response_id_from_bridge(raw_request_body, bridged_headers);

    const auto affinity = session::resolve_sticky_affinity(bridged_request_body, bridged_headers);
    std::string access_token;
    std::string traffic_account_id;
    auto resolved_affinity = affinity;
    if (const auto credentials = session::resolve_upstream_account_credentials(affinity.account_id); credentials.has_value()) {
        resolved_affinity.account_id = credentials->account_id;
        access_token = credentials->access_token;
        if (credentials->internal_account_id > 0) {
            traffic_account_id = std::to_string(credentials->internal_account_id);
        }
    }
    openai::UpstreamRequestPlan plan;
    try {
        plan = openai::build_responses_http_request_plan(
            bridged_request_body,
            bridged_headers,
            access_token,
            resolved_affinity.account_id,
            ""
        );
    } catch (const std::exception&) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "proxy", "responses_payload_invalid");
        return {
            .status = 400,
            .body = openai::build_error_envelope("invalid_request_error", "Invalid request payload"),
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
    const auto response_headers = internal::build_codex_usage_headers_for_account(resolved_affinity.account_id);

    auto execute_upstream = [&](const openai::UpstreamRequestPlan& current_plan) {
        record_account_upstream_egress(traffic_account_id, current_plan.body.size());
        auto result = internal::execute_upstream_with_retry_budget(
            current_plan,
            internal::kStreamRetryBudget,
            internal::should_retry_stream_result,
            "responses_upstream_retry"
        );
        record_account_upstream_ingress(traffic_account_id, upstream_payload_bytes(result));
        return result;
    };

    auto upstream = execute_upstream(plan);
    if (upstream.status == 401 && !resolved_affinity.account_id.empty()) {
        if (const auto refreshed = session::refresh_upstream_account_credentials(resolved_affinity.account_id);
            refreshed.has_value()) {
            resolved_affinity.account_id = refreshed->account_id;
            access_token = refreshed->access_token;
            if (refreshed->internal_account_id > 0) {
                traffic_account_id = std::to_string(refreshed->internal_account_id);
            }
            try {
                plan = openai::build_responses_http_request_plan(
                    bridged_request_body,
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
                    "responses_refresh_rebuild_failed"
                );
            }
        }
    }
    if (upstream.status == 400 && internal::resolved_upstream_error_code(upstream) == "previous_response_not_found") {
        if (const auto stripped_request_body = session::strip_previous_response_id(bridged_request_body);
            stripped_request_body.has_value() && *stripped_request_body != bridged_request_body) {
            core::logging::log_event(
                core::logging::LogLevel::Info,
                "runtime",
                "proxy",
                "responses_previous_response_guard_retry",
                "route=" + std::string(route)
            );
            try {
                plan = openai::build_responses_http_request_plan(
                    *stripped_request_body,
                    bridged_headers,
                    access_token,
                    resolved_affinity.account_id,
                    ""
                );
                internal::maybe_log_upstream_payload_trace(route, plan);
                upstream = execute_upstream(plan);
            } catch (const std::exception&) {
                core::logging::log_event(
                    core::logging::LogLevel::Warning,
                    "runtime",
                    "proxy",
                    "responses_previous_response_guard_rebuild_failed"
                );
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
    if (upstream.status >= 400) {
        const auto error_code = internal::resolved_upstream_error_code(upstream);
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "responses_upstream_error",
            "status=" + std::to_string(upstream.status) + " code=" + error_code
        );
        return {
            .status = upstream.status,
            .body = upstream.body.empty()
                        ? openai::build_error_envelope(error_code, internal::default_error_message_for_code(error_code))
                                          : upstream.body,
            .headers = response_headers,
        };
    }
    if (!upstream.body.empty()) {
        session::remember_response_id_from_json(bridged_headers, upstream.body);
        return {
            .status = upstream.status,
            .body = upstream.body,
            .headers = response_headers,
        };
    }

    if (!upstream.events.empty()) {
        session::remember_response_id_from_events(bridged_headers, upstream.events);
        if (const auto collected = collect_response_body_from_stream_events(upstream.events); collected.has_value()) {
            return {
                .status = upstream.status,
                .body = *collected,
                .headers = response_headers,
            };
        }
    }

    return {
        .status = 502,
        .body = openai::build_error_envelope("upstream_incomplete", "Upstream response was incomplete"),
        .headers = response_headers,
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
    std::string access_token;
    std::string traffic_account_id;
    auto resolved_affinity = affinity;
    if (const auto credentials = session::resolve_upstream_account_credentials(affinity.account_id); credentials.has_value()) {
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
    } catch (const std::exception&) {
        core::logging::log_event(core::logging::LogLevel::Warning, "runtime", "proxy", "compact_payload_invalid");
        return {
            .status = 400,
            .body = openai::build_error_envelope("invalid_request_error", "Invalid request payload"),
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
    const auto response_headers = internal::build_codex_usage_headers_for_account(resolved_affinity.account_id);

    auto execute_upstream = [&](const openai::UpstreamRequestPlan& current_plan) {
        record_account_upstream_egress(traffic_account_id, current_plan.body.size());
        auto result = internal::execute_upstream_with_retry_budget(
            current_plan,
            internal::kCompactRetryBudget,
            internal::should_retry_compact_result,
            "compact_upstream_retry"
        );
        record_account_upstream_ingress(traffic_account_id, upstream_payload_bytes(result));
        return result;
    };

    auto upstream = execute_upstream(plan);
    if (upstream.status == 401 && !resolved_affinity.account_id.empty()) {
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
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "compact_upstream_result",
        internal::build_upstream_result_detail(upstream)
    );
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
            .body = upstream.body.empty()
                        ? openai::build_error_envelope(error_code, internal::default_error_message_for_code(error_code))
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
        .body = openai::build_error_envelope("upstream_incomplete", "Upstream response was incomplete"),
        .headers = response_headers,
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
    if (const auto credentials = session::resolve_upstream_account_credentials(affinity.account_id); credentials.has_value()) {
        model_headers["authorization"] = "Bearer " + credentials->access_token;
        model_headers["chatgpt-account-id"] = credentials->account_id;
        usage_account_id = credentials->account_id;
    }
    const auto response_headers = internal::build_codex_usage_headers_for_account(usage_account_id);

    const auto registry = apply_model_list_policy(openai::build_model_registry_from_upstream(model_headers), policy);
    if (route == "/api/models") {
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
    if (const auto credentials = session::resolve_upstream_account_credentials(account_id); credentials.has_value()) {
        account_id = credentials->account_id;
        access_token = credentials->access_token;
        if (credentials->internal_account_id > 0) {
            traffic_account_id = std::to_string(credentials->internal_account_id);
        }
    }
    const auto response_headers = internal::build_codex_usage_headers_for_account(account_id);

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
        record_account_upstream_egress(traffic_account_id, current_plan.body.size());
        auto result = execute_upstream_plan(current_plan);
        result.error_code = internal::resolved_upstream_error_code(result);
        record_account_upstream_ingress(traffic_account_id, upstream_payload_bytes(result));
        return result;
    };

    auto upstream = execute_upstream(plan);
    if (upstream.status == 401 && !account_id.empty()) {
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
        }
    }
    core::logging::log_event(
        core::logging::LogLevel::Info,
        "runtime",
        "proxy",
        "transcribe_upstream_result",
        internal::build_upstream_result_detail(upstream)
    );
    if (upstream.status >= 400) {
        const auto error_code = internal::resolved_upstream_error_code(upstream);
        core::logging::log_event(
            core::logging::LogLevel::Warning,
            "runtime",
            "proxy",
            "transcribe_upstream_error",
            "status=" + std::to_string(upstream.status) + " code=" + error_code
        );
        return {
            .status = upstream.status,
            .body = upstream.body.empty()
                        ? openai::build_error_envelope(error_code, internal::default_error_message_for_code(error_code))
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
        .body = openai::build_error_envelope("upstream_incomplete", "Upstream response was incomplete"),
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
            .events = stream::build_responses_sse_failure("not_found", "Route not found"),
        };
    }

    auto bridged_headers = inbound_headers;
    const auto turn_state = session::ensure_turn_state_header(bridged_headers);
    static_cast<void>(turn_state);
    const auto bridged_request_body = session::inject_previous_response_id_from_bridge(raw_request_body, bridged_headers);

    const auto affinity = session::resolve_sticky_affinity(bridged_request_body, bridged_headers);
    std::string access_token;
    std::string traffic_account_id;
    auto resolved_affinity = affinity;
    if (const auto credentials = session::resolve_upstream_account_credentials(affinity.account_id); credentials.has_value()) {
        resolved_affinity.account_id = credentials->account_id;
        access_token = credentials->access_token;
        if (credentials->internal_account_id > 0) {
            traffic_account_id = std::to_string(credentials->internal_account_id);
        }
    }
    openai::UpstreamRequestPlan plan;
    try {
        const auto registry = openai::build_default_model_registry();
        plan = openai::build_responses_stream_request_plan(
            bridged_request_body,
            inbound_headers,
            access_token,
            resolved_affinity.account_id,
            registry,
            "default"
        );
    } catch (const std::exception&) {
        return {
            .status = 400,
            .events = stream::build_responses_sse_failure("invalid_request_error", "Invalid request payload"),
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
    const auto response_headers = internal::build_codex_usage_headers_for_account(resolved_affinity.account_id);

    auto execute_upstream = [&](const openai::UpstreamRequestPlan& current_plan) {
        record_account_upstream_egress(traffic_account_id, current_plan.body.size());
        auto result = internal::execute_upstream_with_retry_budget(
            current_plan,
            internal::kStreamRetryBudget,
            internal::should_retry_stream_result,
            "responses_upstream_retry"
        );
        record_account_upstream_ingress(traffic_account_id, upstream_payload_bytes(result));
        return result;
    };

    auto upstream = execute_upstream(plan);
    if (upstream.status == 401 && !resolved_affinity.account_id.empty()) {
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
                    bridged_request_body,
                    inbound_headers,
                    access_token,
                    resolved_affinity.account_id,
                    registry,
                    "default"
                );
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
                    "responses_sse_refresh_rebuild_failed"
                );
            }
        }
    }
    if (upstream.status == 400 && internal::resolved_upstream_error_code(upstream) == "previous_response_not_found") {
        if (const auto stripped_request_body = session::strip_previous_response_id(bridged_request_body);
            stripped_request_body.has_value() && *stripped_request_body != bridged_request_body) {
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
                    *stripped_request_body,
                    inbound_headers,
                    access_token,
                    resolved_affinity.account_id,
                    registry,
                    "default"
                );
                const auto turn_state_it = bridged_headers.find("x-codex-turn-state");
                if (turn_state_it != bridged_headers.end() && !turn_state_it->second.empty()) {
                    plan.headers["x-codex-turn-state"] = turn_state_it->second;
                }
                internal::maybe_log_upstream_payload_trace(route, plan);
                upstream = execute_upstream(plan);
            } catch (const std::exception&) {
                core::logging::log_event(
                    core::logging::LogLevel::Warning,
                    "runtime",
                    "proxy",
                    "responses_previous_response_guard_rebuild_failed"
                );
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
    if (upstream.status >= 400) {
        const auto error_code = internal::resolved_upstream_error_code(upstream);
        return {
            .status = upstream.status,
            .events = stream::build_responses_sse_failure(error_code, internal::default_error_message_for_code(error_code)),
            .headers = response_headers,
        };
    }
    const auto downstream_status = upstream.status == 101 ? 200 : upstream.status;

    if (!upstream.events.empty()) {
        session::remember_response_id_from_events(bridged_headers, upstream.events);
        return {
            .status = downstream_status,
            .events = upstream.events,
            .headers = response_headers,
        };
    }

    return {
        .status = 502,
        .events = stream::build_responses_sse_failure("stream_incomplete", "Upstream stream was incomplete"),
        .headers = response_headers,
    };
}

} // namespace tightrope::proxy
