#pragma once
// proxy API controller

#include <string>
#include <string_view>
#include <vector>

#include "openai/upstream_headers.h"
#include "proxy_service.h"

namespace tightrope::server::controllers {

struct ProxyHttpResponse {
    int status = 500;
    std::string content_type;
    std::string body;
    proxy::openai::HeaderMap headers;
};

struct ProxySseResponse {
    int status = 500;
    std::string content_type;
    std::vector<std::string> events;
    proxy::openai::HeaderMap headers;
};

struct ProxyWsResponse {
    int status = 500;
    bool accepted = false;
    int close_code = 1000;
    std::vector<std::string> frames;
};

ProxyHttpResponse post_proxy_responses_json(
    std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers = {}
);
ProxySseResponse post_proxy_responses_sse(
    std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers = {}
);
ProxyWsResponse proxy_responses_websocket(
    std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers = {}
);
ProxyHttpResponse post_proxy_responses_compact(
    std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers = {}
);
ProxyHttpResponse get_proxy_models(
    std::string_view route,
    const proxy::ModelListPolicy& policy = {},
    const proxy::openai::HeaderMap& headers = {}
);
ProxyHttpResponse post_proxy_transcribe(
    std::string_view route,
    std::string_view model,
    std::string_view prompt,
    std::string_view audio_bytes,
    const proxy::openai::HeaderMap& headers = {}
);
[[nodiscard]] bool proxy_chat_completions_stream_requested(const std::string& body);
ProxyHttpResponse post_proxy_chat_completions_json(
    std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers = {}
);
ProxySseResponse post_proxy_chat_completions_sse(
    std::string_view route,
    const std::string& body,
    const proxy::openai::HeaderMap& headers = {}
);

} // namespace tightrope::server::controllers
