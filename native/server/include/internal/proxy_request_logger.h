#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "openai/upstream_headers.h"

namespace tightrope::server::internal {

struct ProxyRequestLogContext {
    std::string_view method;
    std::string_view route;
    int status_code = 0;
    std::string_view request_body;
    std::optional<std::string> response_body;
    std::vector<std::string> response_events;
    std::string_view transport;
    const proxy::openai::HeaderMap* headers = nullptr;
};

void persist_proxy_request_log(const ProxyRequestLogContext& context) noexcept;

} // namespace tightrope::server::internal
