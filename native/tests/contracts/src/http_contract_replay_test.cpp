#include <catch2/catch_test_macros.hpp>

#include "fixture_loader.h"

TEST_CASE("health contract replay returns expected payload shape", "[contracts][replay]") {
    const auto fixture = tightrope::tests::contracts::load_http_fixture_by_route("GET", "/health");
    REQUIRE(fixture.response.status == 200);
    REQUIRE(fixture.response.body.find("\"status\"") != std::string::npos);
}
