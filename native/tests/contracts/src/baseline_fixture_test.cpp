#include <catch2/catch_test_macros.hpp>

#include <filesystem>

TEST_CASE("baseline db and crypto fixtures are present", "[contracts][baseline]") {
    REQUIRE(std::filesystem::exists("native/tests/contracts/fixtures/db/baseline_store.db"));
    REQUIRE(std::filesystem::exists("native/tests/contracts/fixtures/crypto/bootstrap.key"));
    REQUIRE(std::filesystem::exists("native/tests/contracts/fixtures/crypto/token_blob_v1.txt"));
}
