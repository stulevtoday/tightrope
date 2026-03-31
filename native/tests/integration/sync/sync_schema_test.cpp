#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>

#include <sqlite3.h>

#include "conflict_resolver.h"
#include "sync_schema.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-sync-schema-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

void exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err != nullptr) {
        INFO(std::string(err));
        sqlite3_free(err);
    }
    REQUIRE(rc == SQLITE_OK);
}

bool table_exists(sqlite3* db, const char* table_name) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(
        sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1;", -1, &stmt, nullptr) ==
        SQLITE_OK
    );
    REQUIRE(stmt != nullptr);
    sqlite3_bind_text(stmt, 1, table_name, -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    return found;
}

bool column_exists(sqlite3* db, const char* table_name, const char* column_name) {
    const std::string sql = std::string("PRAGMA table_info(") + table_name + ");";
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name != nullptr && std::string_view(name) == column_name) {
            sqlite3_finalize(stmt);
            return true;
        }
    }
    sqlite3_finalize(stmt);
    return false;
}

void create_minimal_app_tables(sqlite3* db) {
    exec_sql(db, "CREATE TABLE accounts (id INTEGER PRIMARY KEY, email TEXT);");
    exec_sql(db, "CREATE TABLE dashboard_settings (id INTEGER PRIMARY KEY);");
    exec_sql(db, "CREATE TABLE api_keys (id INTEGER PRIMARY KEY, key_id TEXT);");
    exec_sql(db, "CREATE TABLE api_key_limits (id INTEGER PRIMARY KEY, api_key_id INTEGER);");
    exec_sql(db, "CREATE TABLE ip_allowlist (id INTEGER PRIMARY KEY, ip TEXT);");
    exec_sql(db, "CREATE TABLE usage_history (id INTEGER PRIMARY KEY, count INTEGER);");
    exec_sql(db, "CREATE TABLE sticky_sessions (session_key TEXT PRIMARY KEY, account_id TEXT);");
    exec_sql(db, "CREATE TABLE request_logs (id INTEGER PRIMARY KEY, path TEXT);");
}

} // namespace

TEST_CASE("sync schema creates journal and tombstones tables", "[sync][schema][integration]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    create_minimal_app_tables(db);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));

    REQUIRE(table_exists(db, "_sync_journal"));
    REQUIRE(table_exists(db, "_sync_tombstones"));

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("sync schema adds HLC columns to replicated tables", "[sync][schema][integration]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    create_minimal_app_tables(db);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));

    const auto replicated = tightrope::sync::replicated_table_names();
    for (const auto& table : replicated) {
        const std::string t(table);
        INFO("Checking table: " << t);
        REQUIRE(column_exists(db, t.c_str(), "_hlc_wall"));
        REQUIRE(column_exists(db, t.c_str(), "_hlc_counter"));
        REQUIRE(column_exists(db, t.c_str(), "_hlc_site"));
    }

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("sync schema does NOT add HLC columns to non-replicated tables", "[sync][schema][integration]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    create_minimal_app_tables(db);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));

    REQUIRE_FALSE(column_exists(db, "request_logs", "_hlc_wall"));
    REQUIRE_FALSE(column_exists(db, "request_logs", "_hlc_counter"));
    REQUIRE_FALSE(column_exists(db, "request_logs", "_hlc_site"));

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("ensure_sync_schema sets WAL mode and synchronous FULL", "[sync][schema][integration][p0]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    create_minimal_app_tables(db);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));

    // Verify WAL journal mode
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA journal_mode;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    std::string mode(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    REQUIRE(mode == "wal");

    // Verify synchronous=FULL (value 2)
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA synchronous;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const int sync_val = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    REQUIRE(sync_val == 2);

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("ensure_sync_durability is idempotent", "[sync][schema][integration][p0]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    REQUIRE(tightrope::sync::ensure_sync_durability(db));
    REQUIRE(tightrope::sync::ensure_sync_durability(db));

    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "PRAGMA journal_mode;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    std::string mode(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    REQUIRE(mode == "wal");

    sqlite3_close(db);
    std::remove(path.c_str());
}

TEST_CASE("ensure_sync_durability rejects null db", "[sync][schema][integration][p0]") {
    REQUIRE_FALSE(tightrope::sync::ensure_sync_durability(nullptr));
}

TEST_CASE("sync schema is idempotent", "[sync][schema][integration]") {
    const auto path = make_temp_db_path();
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);

    create_minimal_app_tables(db);
    REQUIRE(tightrope::sync::ensure_sync_schema(db));
    REQUIRE(tightrope::sync::ensure_sync_schema(db));

    REQUIRE(table_exists(db, "_sync_journal"));
    REQUIRE(table_exists(db, "_sync_tombstones"));

    sqlite3_close(db);
    std::remove(path.c_str());
}
