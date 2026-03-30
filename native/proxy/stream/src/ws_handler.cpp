#include "ws_handler.h"

#include <sstream>

#include "openai/payload_normalizer.h"
#include "openai/provider_contract.h"

namespace tightrope::proxy::stream {

namespace {

std::string build_response_created_event(const std::string_view response_id) {
    std::ostringstream out;
    out << "{\"type\":\"response.created\",\"response\":{\"id\":\"" << response_id << "\",\"status\":\"in_progress\"}}";
    return out.str();
}

std::string build_response_completed_event(const std::string_view response_id) {
    std::ostringstream out;
    out << "{\"type\":\"response.completed\",\"response\":{\"id\":\"" << response_id
        << "\",\"object\":\"response\",\"status\":\"completed\",\"output\":[],\"usage\":{\"input_tokens\":1,"
           "\"output_tokens\":1,\"total_tokens\":2}}}";
    return out.str();
}

} // namespace

WsTranscript replay_ws_contract(const std::string_view route) {
    if (route == "/v1/responses") {
        return {
            .accepted = true,
            .close_code = 1000,
            .frames =
                {
                    build_response_created_event("resp_ws_v1_success"),
                    build_response_completed_event("resp_ws_v1_success"),
                },
        };
    }

    if (route == "/backend-api/codex/responses") {
        return {
            .accepted = true,
            .close_code = 1000,
            .frames =
                {
                    build_response_created_event("resp_ws_backend_success"),
                    build_response_completed_event("resp_ws_backend_success"),
                },
        };
    }

    return {
        .accepted = false,
        .close_code = 1008,
        .frames = {},
    };
}

std::string build_upstream_response_create_payload(const std::string& raw_request_body) {
    const auto normalized = openai::normalize_request(raw_request_body);
    return openai::build_websocket_response_create_payload_json(normalized.body);
}

} // namespace tightrope::proxy::stream
