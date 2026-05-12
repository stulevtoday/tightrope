#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include <sqlite3.h>

#include "controllers/sessions_controller.h"
#include "repositories/session_repo.h"

namespace {

std::string make_temp_db_path() {
    const auto file = std::filesystem::temp_directory_path() /
                      std::filesystem::path("tightrope-sessions-controller.sqlite3");
    std::filesystem::remove(file);
    return file.string();
}

} // namespace

TEST_CASE("sessions controller lists sticky sessions from persistence", "[server][sessions]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);

    REQUIRE(tightrope::db::ensure_proxy_sticky_session_schema(db));
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(
        db,
        "ak_prompt_1",
        "acc-1",
        /*now_ms=*/2000,
        /*ttl_ms=*/500,
        "prompt_cache"
    ));
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(
        db,
        "turn_abc",
        "acc-2",
        /*now_ms=*/2500,
        /*ttl_ms=*/1000,
        "codex_session"
    ));

    const auto all = tightrope::server::controllers::list_sticky_sessions(10, 0, db);
    REQUIRE(all.status == 200);
    REQUIRE(all.generated_at_ms > 0);
    REQUIRE(all.sessions.size() == 2);
    REQUIRE(all.sessions[0].session_key == "turn_abc");
    REQUIRE(all.sessions[0].account_id == "acc-2");
    REQUIRE(all.sessions[0].kind == "codex_session");
    REQUIRE(all.sessions[0].updated_at_ms == 2500);
    REQUIRE(all.sessions[0].expires_at_ms == 3500);
    REQUIRE(all.sessions[1].session_key == "ak_prompt_1");
    REQUIRE(all.sessions[1].kind == "prompt_cache");

    const auto paged = tightrope::server::controllers::list_sticky_sessions(1, 1, db);
    REQUIRE(paged.status == 200);
    REQUIRE(paged.sessions.size() == 1);
    REQUIRE(paged.sessions[0].session_key == "ak_prompt_1");

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}

TEST_CASE("sessions controller purges stale sticky sessions", "[server][sessions]") {
    const auto db_path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(db != nullptr);

    REQUIRE(tightrope::db::ensure_proxy_sticky_session_schema(db));
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(
        db,
        "expired_turn",
        "acc-1",
        /*now_ms=*/1000,
        /*ttl_ms=*/1,
        "codex_session"
    ));
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(
        db,
        "future_turn",
        "acc-2",
        /*now_ms=*/9'000'000'000'000,
        /*ttl_ms=*/60'000,
        "codex_session"
    ));

    const auto response = tightrope::server::controllers::purge_stale_sticky_sessions(db);
    REQUIRE(response.status == 200);
    REQUIRE(response.generated_at_ms > 0);
    REQUIRE(response.purged == 1);

    const auto remaining = tightrope::server::controllers::list_sticky_sessions(10, 0, db);
    REQUIRE(remaining.status == 200);
    REQUIRE(remaining.sessions.size() == 1);
    REQUIRE(remaining.sessions[0].session_key == "future_turn");

    sqlite3_close(db);
    std::filesystem::remove(db_path);
}
