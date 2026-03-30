#include <catch2/catch_test_macros.hpp>

#include "journal.h"

TEST_CASE("journal append assigns sequence and checksum", "[sync][journal]") {
    tightrope::sync::Journal journal;

    const auto first = journal.append({
        .hlc = {.wall = 100, .counter = 1, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"1"})",
        .op = "INSERT",
        .old_values = "",
        .new_values = R"({"email":"a@x.com"})",
        .applied = 1,
        .batch_id = "batch-1",
    });
    const auto second = journal.append({
        .hlc = {.wall = 110, .counter = 2, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"2"})",
        .op = "INSERT",
        .old_values = "",
        .new_values = R"({"email":"b@x.com"})",
        .applied = 1,
        .batch_id = "batch-2",
    });

    REQUIRE(first.seq == 1);
    REQUIRE(second.seq == 2);
    REQUIRE_FALSE(first.checksum.empty());
    REQUIRE(first.checksum.size() == 64);
}

TEST_CASE("journal query by sequence and batch rollback ordering", "[sync][journal]") {
    tightrope::sync::Journal journal;
    journal.append({
        .hlc = {.wall = 100, .counter = 1, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"1"})",
        .op = "INSERT",
        .old_values = "",
        .new_values = R"({"email":"a@x.com"})",
        .applied = 1,
        .batch_id = "batch-a",
    });
    journal.append({
        .hlc = {.wall = 101, .counter = 2, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"1"})",
        .op = "UPDATE",
        .old_values = R"({"email":"a@x.com"})",
        .new_values = R"({"email":"b@x.com"})",
        .applied = 1,
        .batch_id = "batch-a",
    });
    journal.append({
        .hlc = {.wall = 102, .counter = 3, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"2"})",
        .op = "INSERT",
        .old_values = "",
        .new_values = R"({"email":"c@x.com"})",
        .applied = 1,
        .batch_id = "batch-b",
    });

    const auto after_first = journal.entries_after(1);
    REQUIRE(after_first.size() == 2);
    REQUIRE(after_first.front().seq == 2);
    REQUIRE(after_first.back().seq == 3);

    const auto rolled_back = journal.rollback_batch("batch-a");
    REQUIRE(rolled_back.size() == 2);
    REQUIRE(rolled_back.front().seq == 2);
    REQUIRE(rolled_back.back().seq == 1);
    REQUIRE(journal.size() == 1);
}

TEST_CASE("journal mark applied updates entry state", "[sync][journal]") {
    tightrope::sync::Journal journal;
    const auto inserted = journal.append({
        .hlc = {.wall = 100, .counter = 1, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"1"})",
        .op = "INSERT",
        .old_values = "",
        .new_values = R"({"email":"a@x.com"})",
        .applied = 1,
        .batch_id = "",
    });

    REQUIRE(journal.mark_applied(inserted.seq, 2));
    const auto entries = journal.entries_after(0);
    REQUIRE(entries.size() == 1);
    REQUIRE(entries.front().applied == 2);
}

TEST_CASE("journal compact removes only entries older than cutoff and acknowledged", "[sync][journal]") {
    tightrope::sync::Journal journal;
    journal.append({
        .hlc = {.wall = 100, .counter = 1, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"1"})",
        .op = "INSERT",
        .old_values = "",
        .new_values = R"({"email":"a@x.com"})",
        .applied = 1,
        .batch_id = "batch-1",
    });
    journal.append({
        .hlc = {.wall = 120, .counter = 2, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"2"})",
        .op = "INSERT",
        .old_values = "",
        .new_values = R"({"email":"b@x.com"})",
        .applied = 1,
        .batch_id = "batch-2",
    });
    journal.append({
        .hlc = {.wall = 200, .counter = 3, .site_id = 7},
        .table_name = "accounts",
        .row_pk = R"({"id":"3"})",
        .op = "INSERT",
        .old_values = "",
        .new_values = R"({"email":"c@x.com"})",
        .applied = 1,
        .batch_id = "batch-3",
    });

    const auto removed = journal.compact(/*cutoff_wall=*/150, /*max_ack_seq=*/2);
    REQUIRE(removed == 2);

    const auto remaining = journal.entries_after(0);
    REQUIRE(remaining.size() == 1);
    REQUIRE(remaining.front().seq == 3);
}
