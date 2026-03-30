#include <catch2/catch_test_macros.hpp>

#include <unordered_set>

#include "journal.h"
#include "journal_batch_id.h"

TEST_CASE("generated journal batch ids are valid UUIDs and unique", "[sync][journal][batch_id]") {
    std::unordered_set<std::string> ids;
    for (int i = 0; i < 64; ++i) {
        const auto id = tightrope::sync::generate_batch_id();
        REQUIRE(tightrope::sync::is_valid_batch_id(id));
        REQUIRE(ids.insert(id).second);
    }
}

TEST_CASE("journal append auto-generates batch id when omitted", "[sync][journal][batch_id]") {
    tightrope::sync::Journal journal;
    const auto created = journal.append({
        .hlc = {.wall = 100, .counter = 1, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"1"})",
        .op = "INSERT",
        .old_values = "",
        .new_values = R"({"email":"x@y.com"})",
        .applied = 1,
        .batch_id = "",
    });

    REQUIRE_FALSE(created.batch_id.empty());
    REQUIRE(tightrope::sync::is_valid_batch_id(created.batch_id));
}
