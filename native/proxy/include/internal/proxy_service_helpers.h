#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "openai/model_registry.h"
#include "openai/upstream_headers.h"
#include "openai/upstream_request_plan.h"
#include "session/sticky_affinity.h"
#include "upstream_transport.h"

namespace tightrope::proxy::internal {

inline constexpr std::string_view kTranscriptionModel = "gpt-4o-transcribe";

struct ContinuationRequestGuard {
    std::string request_body;
    bool continuation_request = false;
    bool contains_function_call_output = false;
    bool preflight_rewritten = false;
    bool preflight_stripped = false;
    std::optional<std::string> preferred_account_id;
    std::optional<std::string> continuity_account_id;
};

bool is_supported_responses_route(std::string_view route);
bool is_supported_compact_route(std::string_view route);
bool is_supported_memories_route(std::string_view route);
bool is_supported_models_route(std::string_view route);
bool is_supported_transcribe_route(std::string_view route);
bool is_proxy_sse_contract_event(const std::string& event);
bool optional_string_has_value(const std::optional<std::string>& value);
ContinuationRequestGuard guard_backend_codex_previous_response(
    std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& bridged_headers,
    const session::StickyAffinityResolution& affinity
);

std::string build_success_json_body();
std::string build_dashboard_models_payload(const openai::ModelRegistry& registry);
std::string build_public_models_payload(const openai::ModelRegistry& registry);
std::string build_invalid_transcription_model_error(std::string_view model);
std::string build_proxy_request_detail(std::string_view route, std::size_t body_bytes, const openai::HeaderMap& inbound_headers);
std::string build_upstream_plan_detail(const openai::UpstreamRequestPlan& plan);
std::string build_upstream_result_detail(const UpstreamExecutionResult& upstream);
void append_upstream_passthrough_headers(openai::HeaderMap& downstream, const openai::HeaderMap& upstream);
openai::HeaderMap build_codex_usage_headers_for_account(std::string_view account_id);
bool upstream_payload_trace_enabled();
void maybe_log_upstream_payload_trace(std::string_view route, const openai::UpstreamRequestPlan& plan);

} // namespace tightrope::proxy::internal
