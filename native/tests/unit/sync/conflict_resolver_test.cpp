#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "conflict_resolver.h"

TEST_CASE("conflict resolver LWW applies newer remote HLC", "[sync][conflict]") {
    const tightrope::sync::Hlc remote = {.wall = 200, .counter = 1, .site_id = 8};
    const tightrope::sync::Hlc local = {.wall = 150, .counter = 4, .site_id = 7};

    REQUIRE(tightrope::sync::resolve_lww(remote, local) == tightrope::sync::ConflictDecision::ApplyRemote);
}

TEST_CASE("conflict resolver LWW keeps local when local HLC is newer", "[sync][conflict]") {
    const tightrope::sync::Hlc remote = {.wall = 200, .counter = 2, .site_id = 8};
    const tightrope::sync::Hlc local = {.wall = 200, .counter = 3, .site_id = 7};

    REQUIRE(tightrope::sync::resolve_lww(remote, local) == tightrope::sync::ConflictDecision::KeepLocal);
}

TEST_CASE("conflict resolver LWW uses site_id tiebreaker", "[sync][conflict]") {
    const tightrope::sync::Hlc remote = {.wall = 200, .counter = 3, .site_id = 9};
    const tightrope::sync::Hlc local = {.wall = 200, .counter = 3, .site_id = 7};

    REQUIRE(tightrope::sync::resolve_lww(remote, local) == tightrope::sync::ConflictDecision::ApplyRemote);
}

TEST_CASE("conflict resolver exposes per-table strategies from sync blueprint", "[sync][conflict]") {
    using tightrope::sync::ConflictStrategy;
    using tightrope::sync::conflict_strategy_for_table;

    REQUIRE(conflict_strategy_for_table("accounts") == ConflictStrategy::RaftLinearizable);
    REQUIRE(conflict_strategy_for_table("dashboard_settings") == ConflictStrategy::RaftLinearizable);
    REQUIRE(conflict_strategy_for_table("api_keys") == ConflictStrategy::RaftLinearizable);
    REQUIRE(conflict_strategy_for_table("api_key_limits") == ConflictStrategy::RaftLinearizable);
    REQUIRE(conflict_strategy_for_table("usage_history") == ConflictStrategy::CrdtPnCounterMerge);
    REQUIRE(conflict_strategy_for_table("ip_allowlist") == ConflictStrategy::CrdtOrSetAddWins);
    REQUIRE(conflict_strategy_for_table("sticky_sessions") == ConflictStrategy::LwwByHlc);
    REQUIRE(conflict_strategy_for_table("request_logs") == ConflictStrategy::LocalOnly);
    REQUIRE(conflict_strategy_for_table("_sync_journal") == ConflictStrategy::LocalOnly);
}

TEST_CASE("conflict resolver flags replicated and raft-managed tables", "[sync][conflict]") {
    REQUIRE(tightrope::sync::is_table_replicated("accounts"));
    REQUIRE(tightrope::sync::is_table_replicated("sticky_sessions"));
    REQUIRE_FALSE(tightrope::sync::is_table_replicated("request_logs"));

    REQUIRE(tightrope::sync::table_requires_raft("accounts"));
    REQUIRE(tightrope::sync::table_requires_raft("dashboard_settings"));
    REQUIRE_FALSE(tightrope::sync::table_requires_raft("usage_history"));
}

TEST_CASE("conflict resolver applies table strategy fallback decisions", "[sync][conflict]") {
    const tightrope::sync::Hlc remote = {.wall = 220, .counter = 1, .site_id = 9};
    const tightrope::sync::Hlc local = {.wall = 200, .counter = 3, .site_id = 7};

    REQUIRE(tightrope::sync::resolve_table_conflict("sticky_sessions", remote, local) ==
            tightrope::sync::ConflictDecision::ApplyRemote);

    REQUIRE(tightrope::sync::resolve_table_conflict("accounts", remote, local) ==
            tightrope::sync::ConflictDecision::KeepLocal);
    REQUIRE(tightrope::sync::resolve_table_conflict("request_logs", remote, local) ==
            tightrope::sync::ConflictDecision::KeepLocal);
}

TEST_CASE("conflict resolver exposes replicated table names", "[sync][conflict]") {
    const auto names = tightrope::sync::replicated_table_names();

    REQUIRE(names.size() == 7);

    auto contains = [&](const std::string_view name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };

    REQUIRE(contains("accounts"));
    REQUIRE(contains("dashboard_settings"));
    REQUIRE(contains("api_keys"));
    REQUIRE(contains("api_key_limits"));
    REQUIRE(contains("ip_allowlist"));
    REQUIRE(contains("usage_history"));
    REQUIRE(contains("sticky_sessions"));

    REQUIRE_FALSE(contains("request_logs"));
    REQUIRE_FALSE(contains("_sync_journal"));
    REQUIRE_FALSE(contains("_sync_meta"));
    REQUIRE_FALSE(contains("_sync_last_seen"));
}
