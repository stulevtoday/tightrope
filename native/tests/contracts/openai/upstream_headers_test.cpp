#include <catch2/catch_test_macros.hpp>

#include <string>

#include "openai/upstream_headers.h"

TEST_CASE("inbound header filter mirrors reference-upstream drop rules", "[proxy][provider]") {
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"Authorization", "Bearer client"},
        {"Host", "localhost"},
        {"Content-Encoding", "gzip"},
        {"Forwarded", "for=1.2.3.4"},
        {"X-Forwarded-For", "1.2.3.4"},
        {"CF-Ray", "abc"},
        {"x-tightrope-api-key-id", "k_123"},
        {"User-Agent", "codex-cli"},
        {"X-OpenAI-Client-Version", "1.0"},
    };

    const auto filtered = tightrope::proxy::openai::filter_inbound_headers(inbound);
    REQUIRE(filtered.find("Authorization") == filtered.end());
    REQUIRE(filtered.find("Host") == filtered.end());
    REQUIRE(filtered.find("Content-Encoding") == filtered.end());
    REQUIRE(filtered.find("Forwarded") == filtered.end());
    REQUIRE(filtered.find("X-Forwarded-For") == filtered.end());
    REQUIRE(filtered.find("CF-Ray") == filtered.end());
    REQUIRE(filtered.find("x-tightrope-api-key-id") == filtered.end());
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

TEST_CASE("json upstream headers normalize case-variant accept and content-type", "[proxy][provider]") {
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"accept", "application/problem+json"},
        {"content-type", "text/plain"},
        {"User-Agent", "codex-cli"},
    };
    const auto headers = tightrope::proxy::openai::build_upstream_headers(
        inbound,
        "access-token",
        "acc-1",
        "application/json",
        "req-compact-1"
    );

    REQUIRE(headers.find("accept") == headers.end());
    REQUIRE(headers.find("content-type") == headers.end());
    REQUIRE(headers.find("Accept") != headers.end());
    REQUIRE(headers.at("Accept") == "application/json");
    REQUIRE(headers.find("Content-Type") != headers.end());
    REQUIRE(headers.at("Content-Type") == "application/json");
}

TEST_CASE("transcribe headers preserve reference-upstream minimal forwarding", "[proxy][provider]") {
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
        {"Content-Encoding", "zstd"},
        {"Upgrade", "websocket"},
        {"Cookie", "session=secret"},
        {"X-Custom-Hop", "drop"},
        {"Sec-WebSocket-Key", "abc"},
        {"User-Agent", "codex-cli"},
    };

    const auto headers =
        tightrope::proxy::openai::build_upstream_websocket_headers(inbound, "access-token", "acc-ws", "req-ws-1");
    REQUIRE(headers.find("Connection") == headers.end());
    REQUIRE(headers.find("Accept") == headers.end());
    REQUIRE(headers.find("Content-Type") == headers.end());
    REQUIRE(headers.find("Content-Encoding") == headers.end());
    REQUIRE(headers.find("Upgrade") == headers.end());
    REQUIRE(headers.find("Cookie") == headers.end());
    REQUIRE(headers.find("X-Custom-Hop") == headers.end());
    REQUIRE(headers.find("Sec-WebSocket-Key") != headers.end());
    REQUIRE(headers.find("User-Agent") != headers.end());
    REQUIRE(headers.find("Authorization") != headers.end());
    REQUIRE(headers.at("Authorization") == "Bearer access-token");
    REQUIRE(headers.find("chatgpt-account-id") != headers.end());
    REQUIRE(headers.at("chatgpt-account-id") == "acc-ws");
    REQUIRE(headers.find("openai-beta") != headers.end());
    REQUIRE(headers.at("openai-beta") == "responses_websockets=2026-02-06");
    REQUIRE(headers.find("x-request-id") != headers.end());
    REQUIRE(headers.at("x-request-id") == "req-ws-1");
}

TEST_CASE("websocket headers append required responses websocket beta token", "[proxy][provider]") {
    const tightrope::proxy::openai::HeaderMap inbound = {
        {"OpenAI-Beta", "assistants=v2"},
    };

    const auto headers = tightrope::proxy::openai::build_upstream_websocket_headers(inbound, "access-token", "", "");

    REQUIRE(headers.find("OpenAI-Beta") != headers.end());
    REQUIRE(headers.at("OpenAI-Beta") == "assistants=v2, responses_websockets=2026-02-06");
}
