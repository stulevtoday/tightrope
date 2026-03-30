#pragma once
// Proxy service orchestrator

#include <string>
#include <string_view>
#include <vector>
#include <optional>

#include "openai/upstream_headers.h"

namespace tightrope::proxy {

struct ProxyJsonResult {
    int status = 500;
    std::string body;
    openai::HeaderMap headers;
};

struct ProxySseResult {
    int status = 500;
    std::vector<std::string> events;
    openai::HeaderMap headers;
};

struct ModelListPolicy {
    std::vector<std::string> allowed_models;
    std::optional<std::string> enforced_model;
};

ProxyJsonResult collect_responses_json(
    std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers = {}
);
ProxyJsonResult collect_responses_compact(
    std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers = {}
);
ProxyJsonResult collect_models(
    std::string_view route,
    const ModelListPolicy& policy = {},
    const openai::HeaderMap& inbound_headers = {}
);
ProxyJsonResult collect_transcribe(
    std::string_view route,
    std::string_view model,
    std::string_view prompt,
    std::string_view audio_bytes,
    const openai::HeaderMap& inbound_headers = {}
);
ProxySseResult stream_responses_sse(
    std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers = {}
);

} // namespace tightrope::proxy
