#include "upstream_request_plan.h"

#include <cstdint>
#include <stdexcept>

#include <glaze/glaze.hpp>

#include "payload_normalizer.h"
#include "provider_contract.h"
#include "stream_transport.h"

namespace tightrope::proxy::openai {

namespace {

constexpr std::string_view kResponsesPath = "/codex/responses";
constexpr std::string_view kCompactPath = "/codex/responses/compact";
constexpr std::string_view kTranscribePath = "/transcribe";

HeaderMap prepare_inbound(const HeaderMap& inbound_headers) {
    return filter_inbound_headers(inbound_headers);
}

std::string extract_model_from_payload(const std::string& payload_body) {
    glz::generic payload;
    if (auto ec = glz::read_json(payload, payload_body); ec || !payload.is_object()) {
        return "";
    }
    const auto& object = payload.get_object();
    const auto model = object.find("model");
    if (model == object.end() || !model->second.is_string()) {
        return "";
    }
    return model->second.get_string();
}

UpstreamRequestPlan build_responses_http_plan_from_normalized(
    const NormalizedRequest& normalized,
    const HeaderMap& prepared_inbound,
    const std::string_view access_token,
    const std::string_view account_id,
    const std::string_view request_id
) {
    return UpstreamRequestPlan{
        .method = "POST",
        .path = std::string(kResponsesPath),
        .transport = "http-sse",
        .body = normalized.body,
        .headers = build_upstream_headers(
            prepared_inbound,
            access_token,
            account_id,
            "text/event-stream",
            request_id
        ),
    };
}

UpstreamRequestPlan build_responses_websocket_plan_from_normalized(
    const NormalizedRequest& normalized,
    const HeaderMap& prepared_inbound,
    const std::string_view access_token,
    const std::string_view account_id,
    const std::string_view request_id
) {
    return UpstreamRequestPlan{
        .method = "POST",
        .path = std::string(kResponsesPath),
        .transport = "websocket",
        .body = build_websocket_response_create_payload_json(normalized.body),
        .headers = build_upstream_websocket_headers(
            prepared_inbound,
            access_token,
            account_id,
            request_id
        ),
    };
}

std::string build_transcribe_payload_summary(
    const std::string_view prompt,
    const std::string_view filename,
    const std::string_view content_type,
    const std::string_view audio_bytes
) {
    glz::generic payload = glz::generic::object_t{};
    payload["filename"] = std::string(filename);
    payload["content_type"] = std::string(content_type);
    payload["prompt"] = std::string(prompt);
    payload["audio_bytes"] = static_cast<std::uint64_t>(audio_bytes.size());
    const auto serialized = glz::write_json(payload);
    if (!serialized) {
        throw std::runtime_error("failed to serialize transcribe payload summary");
    }
    return serialized.value_or("{}");
}

} // namespace

UpstreamRequestPlan build_responses_http_request_plan(
    const std::string& raw_request_body,
    const HeaderMap& inbound_headers,
    const std::string_view access_token,
    const std::string_view account_id,
    const std::string_view request_id
) {
    const auto normalized = normalize_request(raw_request_body);
    const auto prepared_inbound = prepare_inbound(inbound_headers);
    return build_responses_http_plan_from_normalized(normalized, prepared_inbound, access_token, account_id, request_id);
}

UpstreamRequestPlan build_responses_websocket_request_plan(
    const std::string& raw_request_body,
    const HeaderMap& inbound_headers,
    const std::string_view access_token,
    const std::string_view account_id,
    const std::string_view request_id
) {
    const auto normalized = normalize_request(raw_request_body);
    const auto prepared_inbound = prepare_inbound(inbound_headers);
    return build_responses_websocket_plan_from_normalized(
        normalized,
        prepared_inbound,
        access_token,
        account_id,
        request_id
    );
}

UpstreamRequestPlan build_responses_stream_request_plan(
    const std::string& raw_request_body,
    const HeaderMap& inbound_headers,
    const std::string_view access_token,
    const std::string_view account_id,
    const ModelRegistry& model_registry,
    const std::string_view upstream_stream_transport,
    const std::string_view upstream_stream_transport_override,
    const std::string_view request_id
) {
    const auto normalized = normalize_request(raw_request_body);
    const auto prepared_inbound = prepare_inbound(inbound_headers);
    const auto model = extract_model_from_payload(normalized.body);

    const auto transport = resolve_stream_transport(
        upstream_stream_transport,
        upstream_stream_transport_override,
        model,
        prepared_inbound,
        model_registry
    );
    if (transport == "websocket") {
        return build_responses_websocket_plan_from_normalized(
            normalized,
            prepared_inbound,
            access_token,
            account_id,
            request_id
        );
    }
    return build_responses_http_plan_from_normalized(normalized, prepared_inbound, access_token, account_id, request_id);
}

UpstreamRequestPlan build_compact_http_request_plan(
    const std::string& raw_request_body,
    const HeaderMap& inbound_headers,
    const std::string_view access_token,
    const std::string_view account_id,
    const std::string_view request_id
) {
    const auto normalized = normalize_compact_request(raw_request_body);
    const auto prepared_inbound = prepare_inbound(inbound_headers);

    return UpstreamRequestPlan{
        .method = "POST",
        .path = std::string(kCompactPath),
        .transport = "http-json",
        .body = normalized.body,
        .headers = build_upstream_headers(
            prepared_inbound,
            access_token,
            account_id,
            "application/json",
            request_id
        ),
    };
}

UpstreamRequestPlan build_transcribe_http_request_plan(
    const std::string_view prompt,
    const std::string_view filename,
    const std::string_view content_type,
    const std::string_view audio_bytes,
    const HeaderMap& inbound_headers,
    const std::string_view access_token,
    const std::string_view account_id
) {
    const auto prepared_inbound = prepare_inbound(inbound_headers);
    return UpstreamRequestPlan{
        .method = "POST",
        .path = std::string(kTranscribePath),
        .transport = "http-multipart",
        .body = build_transcribe_payload_summary(prompt, filename, content_type, audio_bytes),
        .headers = build_upstream_transcribe_headers(prepared_inbound, access_token, account_id),
    };
}

} // namespace tightrope::proxy::openai
