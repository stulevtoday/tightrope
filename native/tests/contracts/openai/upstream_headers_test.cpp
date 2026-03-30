#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openai/upstream_headers.h"

TEST_CASE("inbound header filter mirrors codex-lb drop rules", "[proxy][provider]") {
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Authorization", "Bearer client"},
        {"Host", "localhost"},
        {"Forwarded", "for=1.2.3.4"},
        {"X-Forwarded-For", "1.2.3.4"},
        {"CF-Ray", "abc"},
        {"User-Agent", "codex-cli"},
        {"X-OpenAI-Client-Version", "1.0"},
    };

    const auto filtered = tightrope::proxy::openai::filter_inbound_headers(inbound);
    REQUIRE(filtered.find("Authorization") == filtered.end());
    REQUIRE(filtered.find("Host") == filtered.end());
    REQUIRE(filtered.find("Forwarded") == filtered.end());
    REQUIRE(filtered.find("X-Forwarded-For") == filtered.end());
    REQUIRE(filtered.find("CF-Ray") == filtered.end());
    REQUIRE(filtered.find("User-Agent") != filtered.end());
    REQUIRE(filtered.find("X-OpenAI-Client-Version") != filtered.end());
}

TEST_CASE("json upstream headers inject auth and request id fallback", "[proxy][provider]") {
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"User-Agent", "codex-cli"},
    };
    const auto headers = tightrope::proxy::openai::build_upstream_headers(
        inbound,
        "access-token",
        "acc-1",
        "text/event-stream",
        "req-123"
    );

    REQUIRE(headers.find("Authorization") != headers.end());
    REQUIRE(headers.at("Authorization") == "Bearer access-token");
    REQUIRE(headers.find("Accept") != headers.end());
    REQUIRE(headers.at("Accept") == "text/event-stream");
    REQUIRE(headers.find("Content-Type") != headers.end());
    REQUIRE(headers.at("Content-Type") == "application/json");
    REQUIRE(headers.find("chatgpt-account-id") != headers.end());
    REQUIRE(headers.at("chatgpt-account-id") == "acc-1");
    REQUIRE(headers.find("x-request-id") != headers.end());
    REQUIRE(headers.at("x-request-id") == "req-123");
}

TEST_CASE("transcribe headers preserve codex-lb minimal forwarding", "[proxy][provider]") {
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"User-Agent", "codex-cli"},
        {"X-OpenAI-Client-ID", "cli-1"},
        {"X-Codex-Session-ID", "session-1"},
        {"Accept", "application/json"},
        {"X-Request-ID", "req-should-not-pass"},
        {"Some-Other", "blocked"},
    };

    const auto headers =
        tightrope::proxy::openai::build_upstream_transcribe_headers(inbound, "access-token", "acc-transcribe");
    REQUIRE(headers.find("Authorization") != headers.end());
    REQUIRE(headers.at("Authorization") == "Bearer access-token");
    REQUIRE(headers.find("chatgpt-account-id") != headers.end());
    REQUIRE(headers.at("chatgpt-account-id") == "acc-transcribe");
    REQUIRE(headers.find("User-Agent") != headers.end());
    REQUIRE(headers.find("X-OpenAI-Client-ID") != headers.end());
    REQUIRE(headers.find("X-Codex-Session-ID") != headers.end());
    REQUIRE(headers.find("Accept") == headers.end());
    REQUIRE(headers.find("X-Request-ID") == headers.end());
    REQUIRE(headers.find("Some-Other") == headers.end());
}

TEST_CASE("websocket headers remove hop-by-hop and connection-token headers", "[proxy][provider]") {
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Connection", "keep-alive, Upgrade, X-Custom-Hop"},
        {"Accept", "text/event-stream"},
        {"Content-Type", "application/json"},
        {"Upgrade", "websocket"},
        {"X-Custom-Hop", "drop"},
        {"Sec-WebSocket-Key", "abc"},
        {"User-Agent", "codex-cli"},
    };

    const auto headers =
        tightrope::proxy::openai::build_upstream_websocket_headers(inbound, "access-token", "acc-ws", "req-ws-1");
    REQUIRE(headers.find("Connection") == headers.end());
    REQUIRE(headers.find("Accept") == headers.end());
    REQUIRE(headers.find("Content-Type") == headers.end());
    REQUIRE(headers.find("Upgrade") == headers.end());
    REQUIRE(headers.find("X-Custom-Hop") == headers.end());
    REQUIRE(headers.find("Sec-WebSocket-Key") != headers.end());
    REQUIRE(headers.find("User-Agent") != headers.end());
    REQUIRE(headers.find("Authorization") != headers.end());
    REQUIRE(headers.at("Authorization") == "Bearer access-token");
    REQUIRE(headers.find("chatgpt-account-id") != headers.end());
    REQUIRE(headers.at("chatgpt-account-id") == "acc-ws");
    REQUIRE(headers.find("x-request-id") != headers.end());
    REQUIRE(headers.at("x-request-id") == "req-ws-1");
}
