#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

namespace tightrope::proxy::openai {

using HeaderMap = std::unordered_map<std::string, std::string>;

bool should_drop_inbound_header(std::string_view name);
HeaderMap filter_inbound_headers(const HeaderMap& headers);

HeaderMap build_upstream_headers(
    const HeaderMap& inbound,
    std::string_view access_token,
    std::string_view account_id,
    std::string_view accept = "text/event-stream",
    std::string_view request_id = ""
);

HeaderMap build_upstream_transcribe_headers(
    const HeaderMap& inbound,
    std::string_view access_token,
    std::string_view account_id
);

HeaderMap build_upstream_websocket_headers(
    const HeaderMap& inbound,
    std::string_view access_token,
    std::string_view account_id,
    std::string_view request_id = ""
);

} // namespace tightrope::proxy::openai
