#include <catch2/catch_test_macros.hpp>

#include "openai/model_registry.h"

TEST_CASE("model registry lookup is case-insensitive for compatibility checks", "[proxy][provider]") {
    const tightrope::proxy::openai::ModelRegistry registry({
        tightrope::proxy::openai::ModelInfo{"GPT-5.4-REALTIME", true, true, true},
    });

    REQUIRE(registry.has_model("gpt-5.4-realtime"));
    REQUIRE(registry.find_model("gpt-5.4-realtime") != nullptr);
    REQUIRE(registry.prefers_websockets("gpt-5.4-realtime"));
}

TEST_CASE("model registry uses bootstrap websocket preference fallback", "[proxy][provider]") {
    const tightrope::proxy::openai::ModelRegistry registry;
    REQUIRE(registry.prefers_websockets("gpt-5.4"));
    REQUIRE(registry.prefers_websockets("gpt-5.4-mini"));
    REQUIRE_FALSE(registry.prefers_websockets("gpt-4o"));
}

TEST_CASE("model-specific websocket preference overrides bootstrap fallback", "[proxy][provider]") {
    const tightrope::proxy::openai::ModelRegistry registry({
        tightrope::proxy::openai::ModelInfo{"gpt-5.4", true, true, false},
    });
    REQUIRE_FALSE(registry.prefers_websockets("gpt-5.4"));
}
