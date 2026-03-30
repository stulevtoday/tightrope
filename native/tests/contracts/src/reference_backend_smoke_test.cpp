#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <string>

TEST_CASE("reference backend health endpoint is reachable", "[contracts][reference][.]") {
    const char* raw_url = std::getenv("REFERENCE_BACKEND_URL");
    REQUIRE(raw_url != nullptr);

    std::string url(raw_url);
    std::string command = "curl -fsS \"" + url + "/health\" > /dev/null";

    REQUIRE(std::system(command.c_str()) == 0);
}
