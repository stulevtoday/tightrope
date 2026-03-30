#pragma once

#include <string>
#include <string_view>

#include "model_registry.h"
#include "upstream_headers.h"

namespace tightrope::proxy::openai {

struct UpstreamRequestPlan {
    std::string method;
    std::string path;
    std::string transport;
    std::string body;
    HeaderMap headers;
};

UpstreamRequestPlan build_responses_http_request_plan(
    const std::string& raw_request_body,
    const HeaderMap& inbound_headers,
    std::string_view access_token,
    std::string_view account_id,
    std::string_view request_id = ""
);

UpstreamRequestPlan build_responses_websocket_request_plan(
    const std::string& raw_request_body,
    const HeaderMap& inbound_headers,
    std::string_view access_token,
    std::string_view account_id,
    std::string_view request_id = ""
);

UpstreamRequestPlan build_responses_stream_request_plan(
    const std::string& raw_request_body,
    const HeaderMap& inbound_headers,
    std::string_view access_token,
    std::string_view account_id,
    const ModelRegistry& model_registry,
    std::string_view upstream_stream_transport = "default",
    std::string_view upstream_stream_transport_override = "",
    std::string_view request_id = ""
);

UpstreamRequestPlan build_compact_http_request_plan(
    const std::string& raw_request_body,
    const HeaderMap& inbound_headers,
    std::string_view access_token,
    std::string_view account_id,
    std::string_view request_id = ""
);

UpstreamRequestPlan build_transcribe_http_request_plan(
    std::string_view prompt,
    std::string_view filename,
    std::string_view content_type,
    std::string_view audio_bytes,
    const HeaderMap& inbound_headers,
    std::string_view access_token,
    std::string_view account_id
);

} // namespace tightrope::proxy::openai
