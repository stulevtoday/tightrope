#include <catch2/catch_test_macros.hpp>

#include <sqlite3.h>

#include <string>

#include "crdt/crdt_store.h"
#include "crdt/crdt_sync.h"
#include "crdt/g_counter.h"
#include "crdt/lww_register.h"
#include "crdt/or_set.h"
#include "crdt/pn_counter.h"

TEST_CASE("g counter merge keeps max per-site counts", "[sync][crdt]") {
    tightrope::sync::crdt::GCounter<> local;
    local.increment(1, 3);
    local.increment(2, 1);

    tightrope::sync::crdt::GCounter<> remote;
    remote.increment(1, 5);
    remote.increment(3, 4);

    local.merge(remote);

    REQUIRE(local.value() == 10);
    REQUIRE(local.counts().at(1) == 5);
    REQUIRE(local.counts().at(2) == 1);
    REQUIRE(local.counts().at(3) == 4);
}

TEST_CASE("pn counter merge is commutative", "[sync][crdt]") {
    tightrope::sync::crdt::PNCounter a;
    tightrope::sync::crdt::PNCounter b;
    a.add(1, 3);
    a.add(1, -1);
    b.add(2, 5);
    b.add(2, -2);

    auto left = a;
    auto right = b;
    left.merge(b);
    right.merge(a);

    REQUIRE(left.value() == right.value());
    REQUIRE(left.value() == 5);
}

TEST_CASE("lww register keeps higher HLC then site_id tiebreak", "[sync][crdt]") {
    tightrope::sync::crdt::LWWRegister<std::string> local;
    local.set("local", {.wall = 100, .counter = 1, .site_id = 4}, 4);

    tightrope::sync::crdt::LWWRegister<std::string> newer;
    newer.set("remote-newer", {.wall = 101, .counter = 0, .site_id = 2}, 2);

    local.merge(newer);
    REQUIRE(local.value() == "remote-newer");

    tightrope::sync::crdt::LWWRegister<std::string> tie_breaker;
    tie_breaker.set("remote-tie", {.wall = 101, .counter = 0, .site_id = 9}, 9);
    local.merge(tie_breaker);

    REQUIRE(local.value() == "remote-tie");
}

TEST_CASE("or-set merge preserves add-wins behavior", "[sync][crdt]") {
    tightrope::sync::crdt::ORSet left;
    left.add("10.0.0.1", 1);
    left.add("10.0.0.2", 1);

    auto right = left;
    left.remove("10.0.0.1");
    right.add("10.0.0.1", 2);

    left.merge(right);

    REQUIRE(left.contains("10.0.0.1"));
    REQUIRE(left.contains("10.0.0.2"));
}

TEST_CASE("crdt store round-trips pn/lww/or-set state in sqlite", "[sync][crdt]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(db != nullptr);

    REQUIRE(tightrope::sync::crdt::CrdtStore::ensure_schema(db));

    tightrope::sync::crdt::PNCounter usage;
    usage.add(1, 9);
    usage.add(1, -3);
    REQUIRE(tightrope::sync::crdt::CrdtStore::save_pn_counter(db, "usage_history", usage));

    tightrope::sync::crdt::LWWRegister<std::string> sticky;
    sticky.set("acct-2", {.wall = 300, .counter = 2, .site_id = 7}, 7);
    REQUIRE(tightrope::sync::crdt::CrdtStore::save_lww_string(db, "session:abc", sticky));

    tightrope::sync::crdt::ORSet allowlist;
    allowlist.add("192.168.1.10", 1);
    allowlist.add("192.168.1.11", 2);
    REQUIRE(tightrope::sync::crdt::CrdtStore::save_or_set(db, "ip_allowlist", allowlist));

    tightrope::sync::crdt::PNCounter loaded_usage;
    tightrope::sync::crdt::LWWRegister<std::string> loaded_sticky;
    tightrope::sync::crdt::ORSet loaded_allowlist;
    REQUIRE(tightrope::sync::crdt::CrdtStore::load_pn_counter(db, "usage_history", loaded_usage));
    REQUIRE(tightrope::sync::crdt::CrdtStore::load_lww_string(db, "session:abc", loaded_sticky));
    REQUIRE(tightrope::sync::crdt::CrdtStore::load_or_set(db, "ip_allowlist", loaded_allowlist));

    REQUIRE(loaded_usage.value() == 6);
    REQUIRE(loaded_sticky.value() == "acct-2");
    REQUIRE(loaded_allowlist.contains("192.168.1.10"));
    REQUIRE(loaded_allowlist.contains("192.168.1.11"));

    sqlite3_close(db);
}

TEST_CASE("crdt sync merges remote state and persists merged result", "[sync][crdt]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);
    REQUIRE(db != nullptr);

    REQUIRE(tightrope::sync::crdt::CrdtStore::ensure_schema(db));

    tightrope::sync::crdt::PNCounter usage_local;
    usage_local.add(1, 10);
    REQUIRE(tightrope::sync::crdt::CrdtStore::save_pn_counter(db, "usage_history", usage_local));

    tightrope::sync::crdt::PNCounter usage_remote;
    usage_remote.add(2, 4);
    tightrope::sync::crdt::PNCounter merged_usage;
    REQUIRE(tightrope::sync::crdt::CrdtSync::merge_usage_counter(db, "usage_history", usage_remote, &merged_usage));
    REQUIRE(merged_usage.value() == 14);

    tightrope::sync::crdt::LWWRegister<std::string> sticky_local;
    sticky_local.set("acct-1", {.wall = 100, .counter = 0, .site_id = 1}, 1);
    REQUIRE(tightrope::sync::crdt::CrdtStore::save_lww_string(db, "sticky:s1", sticky_local));

    tightrope::sync::crdt::LWWRegister<std::string> sticky_remote;
    sticky_remote.set("acct-9", {.wall = 101, .counter = 0, .site_id = 3}, 3);
    tightrope::sync::crdt::LWWRegister<std::string> merged_sticky;
    REQUIRE(tightrope::sync::crdt::CrdtSync::merge_sticky_session(db, "sticky:s1", sticky_remote, &merged_sticky));
    REQUIRE(merged_sticky.value() == "acct-9");

    tightrope::sync::crdt::ORSet allowlist_local;
    allowlist_local.add("10.0.0.1", 1);
    REQUIRE(tightrope::sync::crdt::CrdtStore::save_or_set(db, "ip_allowlist", allowlist_local));

    tightrope::sync::crdt::ORSet allowlist_remote;
    allowlist_remote.add("10.0.0.2", 2);
    tightrope::sync::crdt::ORSet merged_allowlist;
    REQUIRE(tightrope::sync::crdt::CrdtSync::merge_ip_allowlist(
        db,
        "ip_allowlist",
        allowlist_remote,
        &merged_allowlist
    ));
    REQUIRE(merged_allowlist.contains("10.0.0.1"));
    REQUIRE(merged_allowlist.contains("10.0.0.2"));

    sqlite3_close(db);
}
