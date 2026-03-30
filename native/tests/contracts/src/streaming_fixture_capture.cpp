#include "streaming_fixture_capture.h"

#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "contract_io.h"
#include "source_contract_catalog.h"

namespace tightrope::tests::contracts {

namespace {

constexpr std::string_view kBackendResponsesRoute = "/backend-api/codex/responses";
constexpr std::string_view kV1ResponsesRoute = "/v1/responses";
constexpr std::string_view kStreamingContract = "proxy-streaming-v1";

using JsonField = std::pair<std::string, std::string>;

JsonField json_string_field(const std::string& key, const std::string& value) {
    return {key, quote_json_string(value)};
}

JsonField json_bool_field(const std::string& key, const bool value) {
    return {key, value ? "true" : "false"};
}

JsonField json_int_field(const std::string& key, const int value) {
    return {key, std::to_string(value)};
}

std::string json_object_line(const std::vector<JsonField>& fields) {
    std::ostringstream out;
    out << "{";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i > 0) {
            out << ",";
        }
        out << quote_json_string(fields[i].first) << ":" << fields[i].second;
    }
    out << "}";
    return out.str();
}

std::string build_sse_event_line(
    const int seq,
    const std::string& route,
    const std::string& flow_id,
    const std::string& event_type,
    const bool terminal,
    const std::string& error_code = {}
) {
    std::vector<JsonField> fields;
    fields.reserve(8);
    fields.push_back(json_int_field("seq", seq));
    fields.push_back(json_string_field("contract", std::string(kStreamingContract)));
    fields.push_back(json_string_field("transport", "sse"));
    fields.push_back(json_string_field("route", route));
    fields.push_back(json_string_field("flow_id", flow_id));
    fields.push_back(json_string_field("event_type", event_type));
    if (!error_code.empty()) {
        fields.push_back(json_string_field("error_code", error_code));
    }
    fields.push_back(json_bool_field("terminal", terminal));
    return json_object_line(fields);
}

std::string build_ws_line(
    const int seq,
    const std::string& route,
    const std::string& flow_id,
    const std::string& frame,
    const std::string& direction,
    const std::string& event_type = {},
    const bool* terminal = nullptr,
    const int* close_code = nullptr,
    const std::string& maps_to_event_type = {},
    const std::string& error_code = {}
) {
    std::vector<JsonField> fields;
    fields.reserve(11);
    fields.push_back(json_int_field("seq", seq));
    fields.push_back(json_string_field("contract", std::string(kStreamingContract)));
    fields.push_back(json_string_field("transport", "websocket"));
    fields.push_back(json_string_field("route", route));
    fields.push_back(json_string_field("flow_id", flow_id));
    fields.push_back(json_string_field("frame", frame));
    if (!event_type.empty()) {
        fields.push_back(json_string_field("event_type", event_type));
    }
    if (terminal != nullptr) {
        fields.push_back(json_bool_field("terminal", *terminal));
    }
    if (close_code != nullptr) {
        fields.push_back(json_int_field("close_code", *close_code));
    }
    if (!maps_to_event_type.empty()) {
        fields.push_back(json_string_field("maps_to_event_type", maps_to_event_type));
    }
    if (!error_code.empty()) {
        fields.push_back(json_string_field("error_code", error_code));
    }
    fields.push_back(json_string_field("direction", direction));
    return json_object_line(fields);
}

void require_source_token(
    const std::string& source,
    const std::string& token,
    const std::filesystem::path& source_path
) {
    if (source.find(token) == std::string::npos) {
        throw std::runtime_error(
            "Expected token '" + token + "' not found in reference source file: " + source_path.string()
        );
    }
}

void verify_streaming_contract_sources(const std::filesystem::path& reference_repo_root) {
    const auto routes = load_source_route_contracts(reference_repo_root);
    if (!find_source_route_contract(routes, "POST", kBackendResponsesRoute).has_value()) {
        throw std::runtime_error("Missing POST route contract for /backend-api/codex/responses");
    }
    if (!find_source_route_contract(routes, "POST", kV1ResponsesRoute).has_value()) {
        throw std::runtime_error("Missing POST route contract for /v1/responses");
    }

    const auto proxy_api_path = reference_repo_root / "app" / "modules" / "proxy" / "api.py";
    const auto proxy_api_source = read_text_file(proxy_api_path);
    require_source_token(proxy_api_source, "@ws_router.websocket(\"/responses\")", proxy_api_path);
    require_source_token(proxy_api_source, "@v1_ws_router.websocket(\"/responses\")", proxy_api_path);

    const auto proxy_service_path = reference_repo_root / "app" / "modules" / "proxy" / "service.py";
    const auto proxy_service_source = read_text_file(proxy_service_path);
    require_source_token(proxy_service_source, "\"stream_idle_timeout\"", proxy_service_path);
    require_source_token(proxy_service_source, "\"stream_incomplete\"", proxy_service_path);

    const auto proxy_responses_test_path = reference_repo_root / "tests" / "integration" / "test_proxy_responses.py";
    const auto proxy_responses_tests = read_text_file(proxy_responses_test_path);
    require_source_token(proxy_responses_tests, "\"type\":\"response.created\"", proxy_responses_test_path);
    require_source_token(proxy_responses_tests, "\"type\":\"response.output_text.delta\"", proxy_responses_test_path);
    require_source_token(proxy_responses_tests, "\"type\":\"response.completed\"", proxy_responses_test_path);

    const auto proxy_ws_test_path = reference_repo_root / "tests" / "integration" / "test_proxy_websocket_responses.py";
    const auto proxy_ws_tests = read_text_file(proxy_ws_test_path);
    require_source_token(proxy_ws_tests, "\"type\": \"response.created\"", proxy_ws_test_path);
    require_source_token(proxy_ws_tests, "\"type\": \"response.completed\"", proxy_ws_test_path);
    require_source_token(proxy_ws_tests, "close_code=1011", proxy_ws_test_path);
    require_source_token(proxy_ws_tests, "\"stream_incomplete\"", proxy_ws_test_path);
}

std::vector<std::string> build_sse_fixture_lines() {
    std::vector<std::string> lines;
    lines.reserve(8);
    lines.push_back(build_sse_event_line(1, std::string(kBackendResponsesRoute), "backend_success", "response.created", false));
    lines.push_back(
        build_sse_event_line(
            2,
            std::string(kBackendResponsesRoute),
            "backend_success",
            "response.output_text.delta",
            false
        )
    );
    lines.push_back(
        build_sse_event_line(3, std::string(kBackendResponsesRoute), "backend_success", "response.completed", true)
    );
    lines.push_back(build_sse_event_line(4, std::string(kV1ResponsesRoute), "v1_success", "response.created", false));
    lines.push_back(build_sse_event_line(5, std::string(kV1ResponsesRoute), "v1_success", "response.output_text.delta", false));
    lines.push_back(build_sse_event_line(6, std::string(kV1ResponsesRoute), "v1_success", "response.completed", true));
    lines.push_back(
        build_sse_event_line(7, std::string(kBackendResponsesRoute), "backend_idle_timeout", "response.created", false)
    );
    lines.push_back(
        build_sse_event_line(
            8,
            std::string(kBackendResponsesRoute),
            "backend_idle_timeout",
            "response.failed",
            true,
            "stream_idle_timeout"
        )
    );
    return lines;
}

std::vector<std::string> build_ws_fixture_lines() {
    constexpr bool kFalse = false;
    constexpr bool kTrue = true;
    constexpr int kCloseCode1011 = 1011;

    std::vector<std::string> lines;
    lines.reserve(7);
    lines.push_back(
        build_ws_line(
            1,
            std::string(kBackendResponsesRoute),
            "ws_backend_success",
            "response.create",
            "downstream_to_upstream"
        )
    );
    lines.push_back(
        build_ws_line(
            2,
            std::string(kBackendResponsesRoute),
            "ws_backend_success",
            "event",
            "upstream_to_downstream",
            "response.created",
            &kFalse
        )
    );
    lines.push_back(
        build_ws_line(
            3,
            std::string(kBackendResponsesRoute),
            "ws_backend_success",
            "event",
            "upstream_to_downstream",
            "response.completed",
            &kTrue
        )
    );
    lines.push_back(
        build_ws_line(
            4,
            std::string(kV1ResponsesRoute),
            "ws_v1_success",
            "event",
            "upstream_to_downstream",
            "response.created",
            &kFalse
        )
    );
    lines.push_back(
        build_ws_line(
            5,
            std::string(kV1ResponsesRoute),
            "ws_v1_success",
            "event",
            "upstream_to_downstream",
            "response.completed",
            &kTrue
        )
    );
    lines.push_back(
        build_ws_line(
            6,
            std::string(kBackendResponsesRoute),
            "ws_backend_upstream_eof",
            "event",
            "upstream_to_downstream",
            "response.created",
            &kFalse
        )
    );
    lines.push_back(
        build_ws_line(
            7,
            std::string(kBackendResponsesRoute),
            "ws_backend_upstream_eof",
            "upstream_close",
            "upstream_to_downstream",
            {},
            &kTrue,
            &kCloseCode1011,
            "response.failed",
            "stream_incomplete"
        )
    );
    return lines;
}

std::string join_ndjson_lines(const std::vector<std::string>& lines) {
    std::ostringstream out;
    for (const auto& line : lines) {
        out << line << "\n";
    }
    return out.str();
}

} // namespace

CapturedStreamingFixtures capture_streaming_contract_fixtures(const std::filesystem::path& reference_repo_root) {
    verify_streaming_contract_sources(reference_repo_root);
    return {
        .sse_lines = build_sse_fixture_lines(),
        .ws_lines = build_ws_fixture_lines(),
    };
}

void write_captured_streaming_contract_fixtures(
    const CapturedStreamingFixtures& fixtures,
    const std::filesystem::path& fixture_root
) {
    write_text_file(fixture_root / "responses_sse.ndjson", join_ndjson_lines(fixtures.sse_lines));
    write_text_file(fixture_root / "responses_ws.ndjson", join_ndjson_lines(fixtures.ws_lines));
}

} // namespace tightrope::tests::contracts
