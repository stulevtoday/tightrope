#pragma once

#include <string>

#include "proxy_service.h"
#include "openai/upstream_headers.h"

namespace tightrope::proxy {

[[nodiscard]] bool chat_completions_stream_requested(const std::string& raw_request_body);

[[nodiscard]] ProxyJsonResult collect_chat_completions_json(
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers = {}
);

[[nodiscard]] ProxySseResult stream_chat_completions_sse(
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers = {}
);

} // namespace tightrope::proxy
