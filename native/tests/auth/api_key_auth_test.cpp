#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "api_keys/key_validator.h"
#include "api_keys/limit_enforcer.h"

TEST_CASE("api key validator issues and verifies sk-clb keys", "[auth][api-keys]") {
    const auto issued = tightrope::auth::api_keys::issue_api_key_material();
    REQUIRE(issued.has_value());
    REQUIRE(issued->plain_key.rfind("sk-clb-", 0) == 0);
    REQUIRE_FALSE(issued->key_hash.empty());
    REQUIRE_FALSE(issued->key_prefix.empty());
    REQUIRE(tightrope::auth::api_keys::verify_key_hash(issued->plain_key, issued->key_hash));
    REQUIRE_FALSE(tightrope::auth::api_keys::verify_key_hash("sk-clb-invalid", issued->key_hash));
}

TEST_CASE("api key limit helpers validate windows, types, and reasoning effort", "[auth][api-keys]") {
    using tightrope::auth::api_keys::is_supported_limit_type;
    using tightrope::auth::api_keys::normalize_reasoning_effort;
    using tightrope::auth::api_keys::window_to_period_seconds;

    REQUIRE(is_supported_limit_type("total_tokens"));
    REQUIRE(is_supported_limit_type("input_tokens"));
    REQUIRE(is_supported_limit_type("output_tokens"));
    REQUIRE(is_supported_limit_type("cost_usd"));
    REQUIRE_FALSE(is_supported_limit_type("requests"));

    REQUIRE(window_to_period_seconds("daily").has_value());
    REQUIRE(*window_to_period_seconds("daily") == 86400);
    REQUIRE(*window_to_period_seconds("weekly") == 604800);
    REQUIRE(*window_to_period_seconds("monthly") == 2592000);
    REQUIRE_FALSE(window_to_period_seconds("hourly").has_value());

    REQUIRE(normalize_reasoning_effort(" HIGH ").has_value());
    REQUIRE(*normalize_reasoning_effort(" HIGH ") == "high");
    REQUIRE(normalize_reasoning_effort("").has_value());
    REQUIRE(normalize_reasoning_effort("turbo") == std::nullopt);
}
