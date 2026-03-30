#include <catch2/catch_test_macros.hpp>

#include "openai/model_registry.h"
#include "openai/stream_transport.h"

TEST_CASE("configured stream transport honors override precedence", "[proxy][provider]") {
    REQUIRE(tightrope::proxy::openai::configured_stream_transport("default", "") == "default");
    REQUIRE(tightrope::proxy::openai::configured_stream_transport("default", "websocket") == "websocket");
    REQUIRE(tightrope::proxy::openai::configured_stream_transport("http", "websocket") == "websocket");
}

TEST_CASE("native codex header detection mirrors codex-lb behavior", "[proxy][provider]") {
    const tightrope::proxy::openai::HeaderMap with_originator = {
        {"Originator", "codex_cli_rs"},
    };
    REQUIRE(tightrope::proxy::openai::has_native_codex_transport_headers(with_originator));

    const tightrope::proxy::openai::HeaderMap with_transport_header = {
        {"X-Codex-Turn-State", "state-1"},
    };
    REQUIRE(tightrope::proxy::openai::has_native_codex_transport_headers(with_transport_header));

    const tightrope::proxy::openai::HeaderMap without_native_signals = {
        {"User-Agent", "curl/8.0"},
    };
    REQUIRE_FALSE(tightrope::proxy::openai::has_native_codex_transport_headers(without_native_signals));
}

TEST_CASE("stream transport resolver matches codex-lb selection order", "[proxy][provider]") {
    const tightrope::proxy::openai::ModelRegistry empty_registry;
    const tightrope::proxy::openai::HeaderMap native_headers = {
        {"X-Codex-Turn-Metadata", "metadata"},
    };

    REQUIRE(
        tightrope::proxy::openai::resolve_stream_transport("websocket", "", "gpt-5.4", {}, empty_registry) ==
        "websocket"
    );
    REQUIRE(tightrope::proxy::openai::resolve_stream_transport("http", "", "gpt-5.4", native_headers, empty_registry) == "http");
    REQUIRE(
        tightrope::proxy::openai::resolve_stream_transport("default", "", "gpt-5.4", native_headers, empty_registry) ==
        "websocket"
    );
    REQUIRE(
        tightrope::proxy::openai::resolve_stream_transport("auto", "", "gpt-5.4", {}, empty_registry) == "websocket"
    );

    const tightrope::proxy::openai::ModelRegistry prefer_ws_registry({
        tightrope::proxy::openai::ModelInfo{"gpt-5.4", true, true, true},
    });
    REQUIRE(
        tightrope::proxy::openai::resolve_stream_transport("auto", "", "gpt-5.4", {}, prefer_ws_registry) ==
        "websocket"
    );
    REQUIRE(tightrope::proxy::openai::resolve_stream_transport("auto", "", "gpt-4o", {}, prefer_ws_registry) == "http");
}
