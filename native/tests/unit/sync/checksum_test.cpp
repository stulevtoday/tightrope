#include <catch2/catch_test_macros.hpp>

#include "checksum.h"

TEST_CASE("sha256 hex output is deterministic and lowercase", "[sync][checksum]") {
    const auto digest = tightrope::sync::sha256_hex("abc");
    REQUIRE(digest == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("journal checksum changes when payload fields change", "[sync][checksum]") {
    const auto base = tightrope::sync::journal_checksum(
        "accounts",
        R"({"id":"1"})",
        "UPDATE",
        R"({"email":"a@x.com"})",
        R"({"email":"b@x.com"})"
    );
    const auto changed = tightrope::sync::journal_checksum(
        "accounts",
        R"({"id":"1"})",
        "UPDATE",
        R"({"email":"a@x.com"})",
        R"({"email":"c@x.com"})"
    );

    REQUIRE_FALSE(base.empty());
    REQUIRE(base.size() == 64);
    REQUIRE(changed.size() == 64);
    REQUIRE(base != changed);
}
