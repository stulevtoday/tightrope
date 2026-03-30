#include "sse_handler.h"

#include <sstream>

#include "openai/provider_contract.h"

namespace tightrope::proxy::stream {

namespace {

std::string response_id_for_route(const std::string_view route) {
    if (route == "/v1/responses") {
        return "resp_v1_success";
    }
    return "resp_backend_success";
}

std::string build_created_event(const std::string& response_id) {
    std::ostringstream out;
    out << "{\"type\":\"response.created\",\"response\":{\"id\":\"" << response_id << "\",\"status\":\"in_progress\"}}";
    return out.str();
}

std::string build_output_text_delta_event() {
    return R"({"type":"response.output_text.delta","delta":"ok"})";
}

std::string build_completed_event(const std::string& response_id) {
    std::ostringstream out;
    out << "{\"type\":\"response.completed\",\"response\":{\"id\":\"" << response_id
        << "\",\"object\":\"response\",\"status\":\"completed\",\"output\":[],\"usage\":{\"input_tokens\":1,"
           "\"output_tokens\":1,\"total_tokens\":2}}}";
    return out.str();
}

} // namespace

std::vector<std::string> build_responses_sse_success(const std::string_view route) {
    std::vector<std::string> events;
    events.reserve(3);

    const auto response_id = response_id_for_route(route);
    events.push_back(build_created_event(response_id));
    events.push_back(build_output_text_delta_event());
    events.push_back(build_completed_event(response_id));
    return events;
}

std::vector<std::string> build_responses_sse_failure(
    const std::string_view error_code,
    const std::string_view error_message
) {
    return {openai::build_response_failed_event_json(std::string(error_code), std::string(error_message))};
}

} // namespace tightrope::proxy::stream
