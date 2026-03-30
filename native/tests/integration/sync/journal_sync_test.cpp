#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>
#include <unistd.h>

#include <sqlite3.h>

#include "checksum.h"

namespace {

struct JournalRow {
    std::int64_t seq = 0;
    std::int64_t hlc_wall = 0;
    std::int64_t hlc_counter = 0;
    std::int64_t site_id = 0;
    std::string table_name;
    std::string row_pk;
    std::string op;
    std::string old_values;
    std::string new_values;
    std::string checksum;
    std::int64_t applied = 1;
    std::string batch_id;
};

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-sync-XXXXXX";
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
    }
    if (err != nullptr) {
        sqlite3_free(err);
    }
    REQUIRE(rc == SQLITE_OK);
}

void init_sync_journal(sqlite3* db) {
    exec_sql(db, R"sql(
        CREATE TABLE _sync_journal (
          seq         INTEGER PRIMARY KEY,
          hlc_wall    INTEGER NOT NULL,
          hlc_counter INTEGER NOT NULL,
          site_id     INTEGER NOT NULL,
          table_name  TEXT    NOT NULL,
          row_pk      TEXT    NOT NULL,
          op          TEXT    NOT NULL,
          old_values  TEXT,
          new_values  TEXT,
          checksum    TEXT    NOT NULL,
          applied     INTEGER DEFAULT 1,
          batch_id    TEXT
        );
    )sql");
}

void insert_row(sqlite3* db, const JournalRow& row) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"sql(
        INSERT INTO _sync_journal (
            seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12);
    )sql";
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);

    sqlite3_bind_int64(stmt, 1, row.seq);
    sqlite3_bind_int64(stmt, 2, row.hlc_wall);
    sqlite3_bind_int64(stmt, 3, row.hlc_counter);
    sqlite3_bind_int64(stmt, 4, row.site_id);
    sqlite3_bind_text(stmt, 5, row.table_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, row.row_pk.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, row.op.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, row.old_values.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, row.new_values.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 10, row.checksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 11, row.applied);
    sqlite3_bind_text(stmt, 12, row.batch_id.c_str(), -1, SQLITE_TRANSIENT);

    REQUIRE(sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
}

std::vector<JournalRow> fetch_entries_after(sqlite3* db, const std::int64_t seq) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"sql(
        SELECT seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        FROM _sync_journal
        WHERE seq > ?1
        ORDER BY seq ASC;
    )sql";
    REQUIRE(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    sqlite3_bind_int64(stmt, 1, seq);

    std::vector<JournalRow> rows;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        JournalRow row;
        row.seq = sqlite3_column_int64(stmt, 0);
        row.hlc_wall = sqlite3_column_int64(stmt, 1);
        row.hlc_counter = sqlite3_column_int64(stmt, 2);
        row.site_id = sqlite3_column_int64(stmt, 3);
        row.table_name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        row.row_pk = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        row.op = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        row.old_values = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        row.new_values = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));
        row.checksum = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        row.applied = sqlite3_column_int64(stmt, 10);
        row.batch_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));
        rows.push_back(std::move(row));
    }

    sqlite3_finalize(stmt);
    return rows;
}

std::int64_t max_seq(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COALESCE(MAX(seq), 0) FROM _sync_journal;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

std::int64_t row_count(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM _sync_journal;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

bool apply_missing_entries(sqlite3* target, const std::vector<JournalRow>& missing, const std::int64_t applied_value) {
    exec_sql(target, "BEGIN IMMEDIATE;");
    for (const auto& row : missing) {
        const auto expected = tightrope::sync::journal_checksum(
            row.table_name,
            row.row_pk,
            row.op,
            row.old_values,
            row.new_values
        );
        if (expected != row.checksum) {
            exec_sql(target, "ROLLBACK;");
            return false;
        }

        auto applied = row;
        applied.applied = applied_value;
        insert_row(target, applied);
    }
    exec_sql(target, "COMMIT;");
    return true;
}

JournalRow make_insert_row(
    const std::int64_t seq,
    const std::int64_t wall,
    const std::int64_t counter,
    const std::int64_t site_id,
    std::string row_pk,
    std::string old_values,
    std::string new_values,
    std::string batch_id
) {
    JournalRow row;
    row.seq = seq;
    row.hlc_wall = wall;
    row.hlc_counter = counter;
    row.site_id = site_id;
    row.table_name = "accounts";
    row.row_pk = std::move(row_pk);
    row.op = old_values.empty() ? "INSERT" : "UPDATE";
    row.old_values = std::move(old_values);
    row.new_values = std::move(new_values);
    row.checksum =
        tightrope::sync::journal_checksum(row.table_name, row.row_pk, row.op, row.old_values, row.new_values);
    row.applied = 1;
    row.batch_id = std::move(batch_id);
    return row;
}

} // namespace

TEST_CASE("journal sync catches up lagging database from peer journal", "[sync][journal][integration]") {
    const auto source_path = make_temp_db_path();
    const auto lagging_path = make_temp_db_path();

    sqlite3* source = nullptr;
    sqlite3* lagging = nullptr;
    REQUIRE(sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_open_v2(lagging_path.c_str(), &lagging, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(source != nullptr);
    REQUIRE(lagging != nullptr);

    init_sync_journal(source);
    init_sync_journal(lagging);

    const auto row1 = make_insert_row(1, 100, 1, 7, R"({"id":"1"})", "", R"({"email":"a@x.com"})", "batch-a");
    const auto row2 = make_insert_row(
        2,
        110,
        2,
        7,
        R"({"id":"1"})",
        R"({"email":"a@x.com"})",
        R"({"email":"b@x.com"})",
        "batch-a"
    );
    const auto row3 = make_insert_row(3, 120, 3, 7, R"({"id":"2"})", "", R"({"email":"c@x.com"})", "batch-b");

    insert_row(source, row1);
    insert_row(source, row2);
    insert_row(source, row3);
    insert_row(lagging, row1); // Lagging DB is behind by seq 2 and 3.

    const auto missing = fetch_entries_after(source, max_seq(lagging));
    REQUIRE(missing.size() == 2);
    REQUIRE(missing.front().seq == 2);
    REQUIRE(missing.back().seq == 3);
    REQUIRE(apply_missing_entries(lagging, missing, /*applied_value=*/2));

    REQUIRE(max_seq(lagging) == 3);
    REQUIRE(row_count(lagging) == 3);

    sqlite3_close(source);
    sqlite3_close(lagging);
    std::remove(source_path.c_str());
    std::remove(lagging_path.c_str());
}

TEST_CASE("journal sync rejects corrupted peer batch and rolls back transaction", "[sync][journal][integration]") {
    const auto source_path = make_temp_db_path();
    const auto lagging_path = make_temp_db_path();

    sqlite3* source = nullptr;
    sqlite3* lagging = nullptr;
    REQUIRE(sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_open_v2(lagging_path.c_str(), &lagging, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(source != nullptr);
    REQUIRE(lagging != nullptr);

    init_sync_journal(source);
    init_sync_journal(lagging);

    auto row1 = make_insert_row(1, 100, 1, 7, R"({"id":"1"})", "", R"({"email":"a@x.com"})", "batch-x");
    auto row2 = make_insert_row(2, 110, 2, 7, R"({"id":"2"})", "", R"({"email":"b@x.com"})", "batch-x");
    row2.checksum = "not-a-valid-checksum";

    insert_row(source, row1);
    insert_row(source, row2);

    const auto missing = fetch_entries_after(source, 0);
    REQUIRE(missing.size() == 2);
    REQUIRE_FALSE(apply_missing_entries(lagging, missing, /*applied_value=*/2));
    REQUIRE(row_count(lagging) == 0); // entire batch rolled back

    sqlite3_close(source);
    sqlite3_close(lagging);
    std::remove(source_path.c_str());
    std::remove(lagging_path.c_str());
}
