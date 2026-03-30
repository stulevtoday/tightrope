#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openai/upstream_request_plan.h"
#include "contracts/fixture_loader.h"

TEST_CASE("responses HTTP plan mirrors codex-lb upstream path and payload shaping", "[proxy][provider]") {
    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Authorization", "Bearer downstream"},
        {"X-OpenAI-Client-Version", "1.2.3"},
        {"X-Forwarded-For", "10.0.0.1"},
    };

    const auto plan = tightrope::proxy::openai::build_responses_http_request_plan(
        fixture.request.body,
        inbound,
        "access-token",
        "acc-1",
        "req-1"
    );

    REQUIRE(plan.method == "POST");
    REQUIRE(plan.path == "/codex/responses");
    REQUIRE(plan.transport == "http-sse");
    REQUIRE(plan.body.find("\"model\":\"") != std::string::npos);
    REQUIRE(plan.body.find("\"prompt_cache_key\":\"") != std::string::npos);
    REQUIRE(plan.body.find("\"stream\":true") != std::string::npos);
    REQUIRE(plan.headers.find("Authorization") != plan.headers.end());
    REQUIRE(plan.headers.at("Authorization") == "Bearer access-token");
    REQUIRE(plan.headers.find("Accept") != plan.headers.end());
    REQUIRE(plan.headers.at("Accept") == "text/event-stream");
    REQUIRE(plan.headers.find("chatgpt-account-id") != plan.headers.end());
    REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-1");
    REQUIRE(plan.headers.find("X-Forwarded-For") == plan.headers.end());
}

TEST_CASE("responses websocket plan builds response.create payload and ws headers", "[proxy][provider]") {
    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Connection", "keep-alive, Upgrade"},
        {"Accept", "text/event-stream"},
        {"Sec-WebSocket-Key", "abc"},
        {"User-Agent", "codex-cli"},
    };

    const auto plan = tightrope::proxy::openai::build_responses_websocket_request_plan(
        fixture.request.body,
        inbound,
        "access-token",
        "acc-2",
        "req-2"
    );

    REQUIRE(plan.method == "POST");
    REQUIRE(plan.path == "/codex/responses");
    REQUIRE(plan.transport == "websocket");
    REQUIRE(plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(plan.body.find("\"stream\"") == std::string::npos);
    REQUIRE(plan.headers.find("Connection") == plan.headers.end());
    REQUIRE(plan.headers.find("Accept") == plan.headers.end());
    REQUIRE(plan.headers.find("Sec-WebSocket-Key") != plan.headers.end());
    REQUIRE(plan.headers.find("Authorization") != plan.headers.end());
    REQUIRE(plan.headers.at("Authorization") == "Bearer access-token");
    REQUIRE(plan.headers.find("chatgpt-account-id") != plan.headers.end());
    REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-2");
}

TEST_CASE("responses stream plan resolves websocket transport for native codex clients", "[proxy][provider]") {
    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Originator", "codex_cli_rs"},
        {"Connection", "keep-alive, Upgrade"},
        {"Sec-WebSocket-Key", "abc"},
    };
    const tightrope::proxy::openai::ModelRegistry registry;

    const auto plan = tightrope::proxy::openai::build_responses_stream_request_plan(
        fixture.request.body,
        inbound,
        "access-token",
        "acc-stream",
        registry,
        "default",
        "",
        "req-stream-1"
    );

    REQUIRE(plan.transport == "websocket");
    REQUIRE(plan.body.find("\"type\":\"response.create\"") != std::string::npos);
    REQUIRE(plan.headers.find("Accept") == plan.headers.end());
    REQUIRE(plan.headers.find("Connection") == plan.headers.end());
    REQUIRE(plan.headers.find("Authorization") != plan.headers.end());
}

TEST_CASE("responses stream plan keeps http when transport is explicitly forced", "[proxy][provider]") {
    const auto fixture = tightrope::tests::contracts::load_http_fixture("responses_post");
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Originator", "codex_cli_rs"},
        {"X-Codex-Turn-State", "state-1"},
    };
    const tightrope::proxy::openai::ModelRegistry registry({
        tightrope::proxy::openai::ModelInfo{"gpt-5.4", true, true, true},
    });

    const auto plan = tightrope::proxy::openai::build_responses_stream_request_plan(
        fixture.request.body,
        inbound,
        "access-token",
        "acc-stream",
        registry,
        "http",
        "",
        "req-stream-2"
    );

    REQUIRE(plan.transport == "http-sse");
    REQUIRE(plan.body.find("\"type\":\"response.create\"") == std::string::npos);
    REQUIRE(plan.headers.find("Accept") != plan.headers.end());
    REQUIRE(plan.headers.at("Accept") == "text/event-stream");
}

TEST_CASE("compact HTTP plan strips compact store field and uses compact path", "[proxy][provider]") {
    const std::string payload =
        R"({"model":"gpt-5.4","input":"compact","store":false,"promptCacheKey":"thread_compact","service_tier":"fast"})";

    const auto plan =
        tightrope::proxy::openai::build_compact_http_request_plan(payload, {}, "access-token", "acc-3", "req-3");

    REQUIRE(plan.method == "POST");
    REQUIRE(plan.path == "/codex/responses/compact");
    REQUIRE(plan.transport == "http-json");
    REQUIRE(plan.body.find("\"store\"") == std::string::npos);
    REQUIRE(plan.body.find("\"service_tier\":\"priority\"") != std::string::npos);
    REQUIRE(plan.body.find("\"prompt_cache_key\":\"thread_compact\"") != std::string::npos);
    REQUIRE(plan.headers.find("Accept") != plan.headers.end());
    REQUIRE(plan.headers.at("Accept") == "application/json");
    REQUIRE(plan.headers.find("Authorization") != plan.headers.end());
    REQUIRE(plan.headers.at("Authorization") == "Bearer access-token");
}

TEST_CASE("transcribe HTTP plan uses minimal codex-lb transcribe forwarding contract", "[proxy][provider]") {
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"User-Agent", "codex-cli"},
        {"X-OpenAI-Client-ID", "client-id"},
        {"X-Codex-Session-ID", "sid-1"},
        {"Accept", "application/json"},
        {"X-Request-ID", "should-not-forward"},
    };

    const auto plan = tightrope::proxy::openai::build_transcribe_http_request_plan(
        "prompt text",
        "audio.wav",
        "audio/wav",
        "audio-bytes",
        inbound,
        "access-token",
        "acc-4"
    );

    REQUIRE(plan.method == "POST");
    REQUIRE(plan.path == "/transcribe");
    REQUIRE(plan.transport == "http-multipart");
    REQUIRE(plan.body.find("\"filename\":\"audio.wav\"") != std::string::npos);
    REQUIRE(plan.body.find("\"content_type\":\"audio/wav\"") != std::string::npos);
    REQUIRE(plan.body.find("\"prompt\":\"prompt text\"") != std::string::npos);
    REQUIRE(plan.body.find("\"audio_bytes\":11") != std::string::npos);
    REQUIRE(plan.headers.find("Authorization") != plan.headers.end());
    REQUIRE(plan.headers.at("Authorization") == "Bearer access-token");
    REQUIRE(plan.headers.find("chatgpt-account-id") != plan.headers.end());
    REQUIRE(plan.headers.at("chatgpt-account-id") == "acc-4");
    REQUIRE(plan.headers.find("User-Agent") != plan.headers.end());
    REQUIRE(plan.headers.find("X-OpenAI-Client-ID") != plan.headers.end());
    REQUIRE(plan.headers.find("X-Codex-Session-ID") != plan.headers.end());
    REQUIRE(plan.headers.find("Accept") == plan.headers.end());
    REQUIRE(plan.headers.find("X-Request-ID") == plan.headers.end());
}
