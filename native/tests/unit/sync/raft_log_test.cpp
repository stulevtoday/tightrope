#include <catch2/catch_test_macros.hpp>

#include "consensus/raft_log.h"

namespace {

tightrope::sync::consensus::LogEntry make_entry(const std::uint64_t term, const std::uint64_t index) {
    return {
        .term = term,
        .index = index,
        .table_name = "accounts",
        .row_pk = R"({"id":"1"})",
        .op = "UPDATE",
        .values = R"({"email":"user@example.com"})",
        .checksum = "checksum",
    };
}

} // namespace

TEST_CASE("raft log appends only contiguous entries", "[sync][raft][log]") {
    tightrope::sync::consensus::RaftLog log;
    REQUIRE(log.last_index() == 0);

    REQUIRE(log.append(make_entry(1, 1)));
    REQUIRE(log.append(make_entry(1, 2)));
    REQUIRE_FALSE(log.append(make_entry(1, 4)));
    REQUIRE(log.last_index() == 2);
}

TEST_CASE("raft log append_entries truncates conflicting suffix", "[sync][raft][log]") {
    tightrope::sync::consensus::RaftLog log;
    REQUIRE(log.append(make_entry(1, 1)));
    REQUIRE(log.append(make_entry(1, 2)));

    std::vector<tightrope::sync::consensus::LogEntry> leader_entries = {
        make_entry(2, 2),
        make_entry(2, 3),
    };
    REQUIRE(log.append_entries(/*prev_index=*/1, /*prev_term=*/1, leader_entries));

    REQUIRE(log.last_index() == 3);
    const auto* second = log.entry_at(2);
    REQUIRE(second != nullptr);
    REQUIRE(second->term == 2);
}
