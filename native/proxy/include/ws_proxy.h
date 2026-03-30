#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "openai/upstream_headers.h"

namespace tightrope::proxy {

struct ProxyWsResult {
    int status = 500;
    bool accepted = false;
    int close_code = 1000;
    std::vector<std::string> frames;
};

ProxyWsResult proxy_responses_websocket(
    std::string_view route,
    const std::string& raw_request_body,
    const openai::HeaderMap& inbound_headers = {}
);

} // namespace tightrope::proxy
