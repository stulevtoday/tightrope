#include "internal/proxy_service_helpers.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#include <glaze/glaze.hpp>

#include "logging/logger.h"
#include "text/ascii.h"
#include "usage_fetcher.h"

namespace tightrope::proxy::internal {

namespace {

using Json = glz::generic;
using JsonArray = Json::array_t;
using JsonObject = Json::object_t;

std::string to_lower(std::string value) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); }
    );
    return value;
}

bool env_enabled(const char* key) {
    if (key == nullptr) {
        return false;
    }
    const char* raw = std::getenv(key);
    if (raw == nullptr) {
        return false;
    }
    const auto normalized = to_lower(core::text::trim_ascii(std::string_view(raw)));
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

std::string truncated_for_log(const std::string_view value, const std::size_t max_bytes) {
    if (value.size() <= max_bytes) {
        return std::string(value);
    }
    std::string out;
    out.reserve(max_bytes + 64);
    out.append(value.substr(0, max_bytes));
    out.append("...(truncated ");
    out.append(std::to_string(value.size() - max_bytes));
    out.append(" bytes)");
    return out;
}

std::string find_header_value_case_insensitive(const openai::HeaderMap& headers, const std::string_view key) {
    const auto value = core::text::find_value_case_insensitive(headers, key);
    if (value.has_value()) {
        return std::string(*value);
    }
    return {};
}

std::string write_json_payload(const Json& payload) {
    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        throw std::runtime_error("failed to serialize proxy JSON payload");
    }
    return serialized.value_or("{}");
}

std::string bool_to_header_value(const bool value) {
    return value ? "true" : "false";
}

std::string format_one_decimal(const int value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(1) << static_cast<double>(value);
    return out.str();
}

std::string format_credit_balance(const std::string& balance) {
    const auto trimmed = core::text::trim_ascii(balance);
    if (trimmed.empty()) {
        return trimmed;
    }
    try {
        const auto numeric = std::stod(trimmed);
        std::ostringstream out;
        out << std::fixed << std::setprecision(2) << numeric;
        return out.str();
    } catch (...) {
        return trimmed;
    }
}

void append_window_headers(
    openai::HeaderMap& headers,
    const std::string_view window_label,
    const std::optional<usage::UsageWindowSnapshot>& window
) {
    if (!window.has_value() || !window->used_percent.has_value() || !window->limit_window_seconds.has_value()) {
        return;
    }
    headers["x-codex-" + std::string(window_label) + "-used-percent"] = format_one_decimal(*window->used_percent);
    headers["x-codex-" + std::string(window_label) + "-window-minutes"] =
        std::to_string(*window->limit_window_seconds / 60);
    if (window->reset_at.has_value()) {
        headers["x-codex-" + std::string(window_label) + "-reset-at"] = std::to_string(*window->reset_at);
    }
}

} // namespace

bool is_supported_responses_route(const std::string_view route) {
    return route == "/v1/responses" || route == "/backend-api/codex/responses";
}

bool is_supported_compact_route(const std::string_view route) {
    return route == "/v1/responses/compact" || route == "/backend-api/codex/responses/compact";
}

bool is_supported_models_route(const std::string_view route) {
    return route == "/api/models" || route == "/v1/models" || route == "/backend-api/codex/models";
}

bool is_supported_transcribe_route(const std::string_view route) {
    return route == "/backend-api/transcribe" || route == "/v1/audio/transcriptions";
}

bool is_proxy_sse_contract_event(const std::string& event) {
    return event.find("\"contract\":\"proxy-streaming-v1\"") != std::string::npos;
}

std::string build_success_json_body() {
    Json payload = JsonObject{};
    payload["id"] = "resp_proxy_success";
    payload["object"] = "response";
    payload["status"] = "completed";

    Json output_item = JsonObject{};
    output_item["type"] = "output_text";
    output_item["text"] = "ok";

    Json output = JsonArray{};
    output.get_array().push_back(std::move(output_item));
    payload["output"] = std::move(output);
    return write_json_payload(payload);
}

std::string build_dashboard_models_payload(const openai::ModelRegistry& registry) {
    Json payload = JsonObject{};
    Json models = JsonArray{};

    for (const auto& model : registry.list_models()) {
        Json item = JsonObject{};
        item["id"] = model.id;
        item["name"] = model.id;
        models.get_array().push_back(std::move(item));
    }

    payload["models"] = std::move(models);
    return write_json_payload(payload);
}

std::string build_public_models_payload(const openai::ModelRegistry& registry) {
    Json payload = JsonObject{};
    payload["object"] = "list";

    Json data = JsonArray{};
    for (const auto& model : registry.list_models()) {
        Json metadata = JsonObject{};
        metadata["display_name"] = model.id;
        metadata["description"] = "";
        metadata["context_window"] = 0;
        metadata["input_modalities"] = JsonArray{};
        metadata["supported_reasoning_levels"] = JsonArray{};
        metadata["default_reasoning_level"] = "";
        metadata["supports_reasoning_summaries"] = false;
        metadata["support_verbosity"] = false;
        metadata["default_verbosity"] = "";
        metadata["prefer_websockets"] = model.prefer_websockets;
        metadata["supports_parallel_tool_calls"] = false;
        metadata["supported_in_api"] = model.supported_in_api;
        metadata["minimal_client_version"] = "";
        metadata["priority"] = 0;

        Json item = JsonObject{};
        item["id"] = model.id;
        item["object"] = "model";
        item["created"] = 0;
        item["owned_by"] = "tightrope";
        item["metadata"] = std::move(metadata);
        data.get_array().push_back(std::move(item));
    }

    payload["data"] = std::move(data);
    return write_json_payload(payload);
}

std::string build_invalid_transcription_model_error(const std::string_view model) {
    Json error = JsonObject{};
    error["message"] =
        "Unsupported transcription model '" + std::string(model) + "'. Only '" + std::string(kTranscriptionModel) +
        "' is supported.";
    error["type"] = "invalid_request_error";
    error["code"] = "invalid_request_error";
    error["param"] = "model";

    Json payload = JsonObject{};
    payload["error"] = std::move(error);
    return write_json_payload(payload);
}

std::string build_proxy_request_detail(
    const std::string_view route,
    const std::size_t body_bytes,
    const openai::HeaderMap& inbound_headers
) {
    const auto accept = find_header_value_case_insensitive(inbound_headers, "accept");
    const auto session_id = find_header_value_case_insensitive(inbound_headers, "session_id");
    const auto codex_session_id = find_header_value_case_insensitive(inbound_headers, "x-codex-session-id");
    const auto stream_override = find_header_value_case_insensitive(inbound_headers, "x-tightrope-upstream-stream-transport");

    std::ostringstream detail;
    detail << "route=" << route << " body_bytes=" << body_bytes << " header_count=" << inbound_headers.size()
           << " accept=" << (accept.empty() ? "<none>" : accept)
           << " has_auth=" << (find_header_value_case_insensitive(inbound_headers, "authorization").empty() ? "false" : "true");
    if (!session_id.empty()) {
        detail << " session_id=" << session_id;
    }
    if (!codex_session_id.empty()) {
        detail << " codex_session_id=" << codex_session_id;
    }
    if (!stream_override.empty()) {
        detail << " stream_override=" << stream_override;
    }
    return detail.str();
}

std::string build_upstream_plan_detail(const openai::UpstreamRequestPlan& plan) {
    std::ostringstream detail;
    detail << "method=" << plan.method << " path=" << plan.path << " transport=" << plan.transport
           << " body_bytes=" << plan.body.size() << " header_count=" << plan.headers.size();
    return detail.str();
}

std::string build_upstream_result_detail(const UpstreamExecutionResult& upstream) {
    std::ostringstream detail;
    detail << "status=" << upstream.status << " body_bytes=" << upstream.body.size() << " event_count=" << upstream.events.size()
           << " accepted=" << (upstream.accepted ? "true" : "false") << " close_code=" << upstream.close_code;
    if (!upstream.error_code.empty()) {
        detail << " error_code=" << upstream.error_code;
    }
    return detail.str();
}

openai::HeaderMap build_codex_usage_headers_for_account(const std::string_view account_id) {
    const auto snapshot = usage::cached_usage_payload(account_id);
    if (!snapshot.has_value()) {
        return {};
    }

    openai::HeaderMap headers;
    if (snapshot->rate_limit.has_value()) {
        append_window_headers(headers, "primary", snapshot->rate_limit->primary_window);
        append_window_headers(headers, "secondary", snapshot->rate_limit->secondary_window);
    }
    if (snapshot->credits.has_value()) {
        headers["x-codex-credits-has-credits"] = bool_to_header_value(snapshot->credits->has_credits);
        headers["x-codex-credits-unlimited"] = bool_to_header_value(snapshot->credits->unlimited);
        if (snapshot->credits->balance.has_value()) {
            const auto formatted = format_credit_balance(*snapshot->credits->balance);
            if (!formatted.empty()) {
                headers["x-codex-credits-balance"] = formatted;
            }
        }
    }
    return headers;
}

bool upstream_payload_trace_enabled() {
    return env_enabled("TIGHTROPE_PROXY_TRACE_UPSTREAM_PAYLOAD");
}

void maybe_log_upstream_payload_trace(const std::string_view route, const openai::UpstreamRequestPlan& plan) {
    if (!upstream_payload_trace_enabled()) {
        return;
    }

    std::ostringstream detail;
    detail << "route=" << route << " method=" << plan.method << " path=" << plan.path << " transport=" << plan.transport
           << " body_bytes=" << plan.body.size() << " header_count=" << plan.headers.size();
    if (plan.transport != "http-multipart") {
        detail << " payload=" << truncated_for_log(plan.body, 2048);
    }

    core::logging::log_event(
        core::logging::LogLevel::Debug,
        "runtime",
        "proxy",
        "upstream_payload_trace",
        detail.str()
    );
}

} // namespace tightrope::proxy::internal
