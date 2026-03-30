#pragma once
// ws stream handler

#include <string>
#include <string_view>
#include <vector>

namespace tightrope::proxy::stream {

struct WsTranscript {
    bool accepted = false;
    int close_code = 1000;
    std::vector<std::string> frames;
};

WsTranscript replay_ws_contract(std::string_view route);
std::string build_upstream_response_create_payload(const std::string& raw_request_body);

} // namespace tightrope::proxy::stream
