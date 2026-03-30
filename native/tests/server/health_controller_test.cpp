#include <catch2/catch_test_macros.hpp>

#include "controllers/health_controller.h"

TEST_CASE("health controller reports ok after startup", "[server][health]") {
    tightrope::server::Runtime runtime;
    auto health = tightrope::server::controllers::get_health(runtime);

    REQUIRE(health.status == "ok");
    REQUIRE(health.uptime_ms < 5000);
}
