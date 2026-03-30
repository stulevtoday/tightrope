#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openai/provider_contract.h"
#include "stream/ws_handler.h"
#include "contracts/fixture_loader.h"

TEST_CASE("sse data line aliases event types to OpenAI-compatible names", "[proxy][provider]") {
    const std::string line = R"(data: {"type":"response.text.delta","delta":"hello"})";
    const auto normalized = tightrope::proxy::openai::normalize_sse_data_line(line);
    REQUIRE(normalized.rfind("data: ", 0) == 0);
    REQUIRE(normalized.find("response.output_text.delta") != std::string::npos);
    REQUIRE(normalized.find("response.text.delta") == std::string::npos);

    const auto untouched = tightrope::proxy::openai::normalize_sse_data_line(R"(event: keep)");
    REQUIRE(untouched == "event: keep");
}

TEST_CASE("sse event block preserves line endings while normalizing aliases", "[proxy][provider]") {
    const std::string event_block =
        "event: response\r\ndata: {\"type\":\"response.audio_transcript.delta\",\"value\":\"a\"}\r\n\r\n";
    const auto normalized = tightrope::proxy::openai::normalize_sse_event_block(event_block);
    REQUIRE(normalized.ends_with("\r\n\r\n"));
    REQUIRE(normalized.find("response.output_audio_transcript.delta") != std::string::npos);
    REQUIRE(normalized.find("response.audio_transcript.delta") == std::string::npos);

    const std::string done_block = "data: [DONE]\n\n";
    REQUIRE(tightrope::proxy::openai::normalize_sse_event_block(done_block) == done_block);
}

TEST_CASE("stream event payload aliasing mirrors codex-lb mapping", "[proxy][provider]") {
    const auto normalized =
        tightrope::proxy::openai::normalize_stream_event_payload_json(R"({"type":"response.audio.delta","seq":2})");
    REQUIRE(normalized.find("\"type\":\"response.output_audio.delta\"") != std::string::npos);
    REQUIRE(normalized.find("\"seq\":2") != std::string::npos);
}

TEST_CASE("websocket response.create payload strips stream-only fields", "[proxy][provider]") {
    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const auto request_payload = tightrope::proxy::stream::build_upstream_response_create_payload(fixture.request.body);
    REQUIRE(request_payload.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(request_payload.find("\"stream\"") == std::string::npos);
    REQUIRE(request_payload.find("\"background\"") == std::string::npos);
    REQUIRE(request_payload.find("\"model\":") != std::string::npos);
}

TEST_CASE("websocket error event envelope matches codex-lb shape", "[proxy][provider]") {
    const auto event = tightrope::proxy::openai::build_websocket_error_event_json(
        400,
        "invalid_request_error",
        "Invalid request payload",
        "invalid_request_error"
    );
    REQUIRE(event.find("\"type\":\"error\"") != std::string::npos);
    REQUIRE(event.find("\"status\":400") != std::string::npos);
    REQUIRE(event.find("\"code\":\"invalid_request_error\"") != std::string::npos);
    REQUIRE(event.find("\"message\":\"Invalid request payload\"") != std::string::npos);
    REQUIRE(event.find("\"type\":\"invalid_request_error\"") != std::string::npos);
}

TEST_CASE("websocket response.failed envelope matches codex-lb shape", "[proxy][provider]") {
    const auto event = tightrope::proxy::openai::build_websocket_response_failed_event_json(
        "stream_incomplete",
        "Upstream websocket closed before response.completed (close_code=1011)"
    );
    REQUIRE(event.find("\"type\":\"response.failed\"") != std::string::npos);
    REQUIRE(event.find("\"object\":\"response\"") != std::string::npos);
    REQUIRE(event.find("\"status\":\"failed\"") != std::string::npos);
    REQUIRE(event.find("\"code\":\"stream_incomplete\"") != std::string::npos);
    REQUIRE(event.find("\"incomplete_details\":null") != std::string::npos);
    REQUIRE(event.find("\"created_at\":") != std::string::npos);
}
