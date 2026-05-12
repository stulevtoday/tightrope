#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <optional>
#include <string>

#include <sqlite3.h>

#include "repositories/session_repo.h"

namespace {

std::string make_temp_db_path() {
    const auto file =
        std::filesystem::temp_directory_path() / std::filesystem::path("tightrope-session-repo-sticky.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("sticky session repository stores and expires account mappings", "[db][sticky]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);

    REQUIRE(tightrope::db::ensure_proxy_sticky_session_schema(db));
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(
        db,
        "session-1",
        "acc-1",
        /*now_ms=*/1000,
        /*ttl_ms=*/500,
        "prompt_cache"
    ));
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(
        db,
        "session-2",
        "acc-2",
        /*now_ms=*/1500,
        /*ttl_ms=*/800,
        "codex_session"
    ));
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(
        db,
        "session-3",
        "acc-3",
        /*now_ms=*/1700,
        /*ttl_ms=*/600
    ));

    const auto listed = tightrope::db::list_proxy_sticky_sessions(db, 10, 0);
    REQUIRE(listed.size() == 3);
    REQUIRE(listed[0].session_key == "session-3");
    REQUIRE(listed[0].account_id == "acc-3");
    REQUIRE(listed[0].kind == "sticky_thread");
    REQUIRE(listed[0].updated_at_ms == 1700);
    REQUIRE(listed[0].expires_at_ms == 2300);
    REQUIRE(listed[1].session_key == "session-2");
    REQUIRE(listed[1].kind == "codex_session");
    REQUIRE(listed[2].session_key == "session-1");
    REQUIRE(listed[2].kind == "prompt_cache");

    const auto active = tightrope::db::find_proxy_sticky_session_account(db, "session-1", /*now_ms=*/1200);
    REQUIRE(active.has_value());
    REQUIRE(*active == "acc-1");

    const auto expired = tightrope::db::find_proxy_sticky_session_account(db, "session-1", /*now_ms=*/1600);
    REQUIRE_FALSE(expired.has_value());

    REQUIRE(tightrope::db::purge_expired_proxy_sticky_sessions(db, /*now_ms=*/1600) == 1);
    REQUIRE(tightrope::db::purge_expired_proxy_sticky_sessions(db, /*now_ms=*/1600) == 0);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("response continuity repository persists scoped response snapshots", "[db][continuity]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);
    const std::string recovery_input_json =
        R"([{"role":"user","content":[{"type":"input_text","text":"turn a"}]}])";

    REQUIRE(tightrope::db::ensure_proxy_response_continuity_schema(db));
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "turn-1",
        "api-key-a",
        "resp-a-1",
        "acc-a",
        /*now_ms=*/1000,
        /*ttl_ms=*/500
    ));
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "turn-1",
        "api-key-a",
        "resp-a-2",
        "acc-a",
        /*now_ms=*/1200,
        /*ttl_ms=*/500,
        recovery_input_json
    ));
    REQUIRE(tightrope::db::upsert_proxy_response_continuity(
        db,
        "turn-1",
        "api-key-b",
        "resp-b-1",
        "acc-b",
        /*now_ms=*/1300,
        /*ttl_ms=*/500
    ));

    const auto latest_for_key_a =
        tightrope::db::find_proxy_response_continuity(db, "turn-1", "api-key-a", /*now_ms=*/1300);
    REQUIRE(latest_for_key_a.has_value());
    REQUIRE(latest_for_key_a->response_id == "resp-a-2");
    REQUIRE(latest_for_key_a->account_id == "acc-a");
    REQUIRE(latest_for_key_a->recovery_input_json == recovery_input_json);

    const auto record_for_response =
        tightrope::db::find_proxy_response_continuity_by_response_id(db, "resp-a-2", "api-key-a", /*now_ms=*/1300);
    REQUIRE(record_for_response.has_value());
    REQUIRE(record_for_response->continuity_key == "turn-1");
    REQUIRE(record_for_response->recovery_input_json == recovery_input_json);

    const auto account_for_key_a =
        tightrope::db::find_proxy_response_continuity_account(db, "resp-a-2", "api-key-a", /*now_ms=*/1300);
    REQUIRE(account_for_key_a.has_value());
    REQUIRE(*account_for_key_a == "acc-a");

    const auto account_wrong_scope =
        tightrope::db::find_proxy_response_continuity_account(db, "resp-a-2", "api-key-b", /*now_ms=*/1300);
    REQUIRE_FALSE(account_wrong_scope.has_value());

    const auto account_without_scope =
        tightrope::db::find_proxy_response_continuity_account(db, "resp-a-2", "", /*now_ms=*/1300);
    REQUIRE_FALSE(account_without_scope.has_value());

    REQUIRE(tightrope::db::purge_proxy_response_continuity_for_account(db, "acc-a") == 2);
    REQUIRE_FALSE(tightrope::db::find_proxy_response_continuity_by_response_id(
        db,
        "resp-a-2",
        "api-key-a",
        /*now_ms=*/1300
    ));
    REQUIRE(tightrope::db::purge_expired_proxy_response_continuity(db, /*now_ms=*/1800) == 1);
    REQUIRE(tightrope::db::find_proxy_response_continuity(db, "turn-1", "api-key-a", /*now_ms=*/1800) == std::nullopt);

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
