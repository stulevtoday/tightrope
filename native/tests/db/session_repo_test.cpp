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
    REQUIRE(tightrope::db::upsert_proxy_sticky_session(db, "session-1", "acc-1", /*now_ms=*/1000, /*ttl_ms=*/500));

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
