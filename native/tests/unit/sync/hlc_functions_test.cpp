#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <string>

#include <sqlite3.h>

#include "checksum.h"
#include "hlc.h"
#include "hlc_functions.h"

namespace {

std::string query_text(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::string result = text != nullptr ? text : "";
    sqlite3_finalize(stmt);
    return result;
}

std::int64_t query_int(sqlite3* db, const char* sql) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

} // namespace

TEST_CASE("hlc functions site_id returns configured value", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    tightrope::sync::HlcClock clock(42);
    REQUIRE(tightrope::sync::register_hlc_functions(db, &clock));

    REQUIRE(query_int(db, "SELECT _hlc_site_id();") == 42);

    sqlite3_close(db);
}

TEST_CASE("hlc functions wall returns value close to current time", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    tightrope::sync::HlcClock clock(7);
    REQUIRE(tightrope::sync::register_hlc_functions(db, &clock));

    const auto now_ms = static_cast<std::int64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
    );

    const auto wall = query_int(db, "SELECT _hlc_now_wall();");
    REQUIRE(wall >= now_ms - 1000);
    REQUIRE(wall <= now_ms + 1000);

    sqlite3_close(db);
}

TEST_CASE("hlc functions counter increments on successive calls", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    tightrope::sync::HlcClock clock(7);
    REQUIRE(tightrope::sync::register_hlc_functions(db, &clock));

    (void)query_int(db, "SELECT _hlc_now_wall();");
    const auto c1 = query_int(db, "SELECT _hlc_now_counter();");
    (void)query_int(db, "SELECT _hlc_now_wall();");
    const auto c2 = query_int(db, "SELECT _hlc_now_counter();");

    REQUIRE(c2 > c1);

    sqlite3_close(db);
}

TEST_CASE("hlc functions checksum matches journal_checksum", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    tightrope::sync::HlcClock clock(7);
    REQUIRE(tightrope::sync::register_hlc_functions(db, &clock));

    const auto expected =
        tightrope::sync::journal_checksum("accounts", R"({"id":"1"})", "INSERT", "", R"({"email":"a@x.com"})");

    const auto result = query_text(
        db,
        "SELECT _checksum('accounts', '{\"id\":\"1\"}', 'INSERT', NULL, '{\"email\":\"a@x.com\"}');"
    );

    REQUIRE(result == expected);

    sqlite3_close(db);
}

TEST_CASE("hlc functions fail gracefully without registration", "[sync][hlc_functions]") {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open(":memory:", &db) == SQLITE_OK);

    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db, "SELECT _hlc_site_id();", -1, &stmt, nullptr);
    if (rc == SQLITE_OK) {
        REQUIRE(stmt != nullptr);
        REQUIRE(sqlite3_step(stmt) != SQLITE_ROW);
        sqlite3_finalize(stmt);
    } else {
        REQUIRE(rc == SQLITE_ERROR);
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
    }

    sqlite3_close(db);
}
