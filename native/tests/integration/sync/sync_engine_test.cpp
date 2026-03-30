#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <unistd.h>

#include <sqlite3.h>

#include "sync_engine.h"
#include "sync_protocol.h"

namespace {

std::string make_temp_db_path() {
    char path[] = "/tmp/tightrope-sync-engine-XXXXXX";
    const int fd = mkstemp(path);
    REQUIRE(fd != -1);
    close(fd);
    std::remove(path);
    return std::string(path);
}

void exec_sql(sqlite3* db, const char* sql) {
    char* err = nullptr;
    const auto rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err != nullptr) {
        INFO(std::string(err));
    }
    if (err != nullptr) {
        sqlite3_free(err);
    }
    REQUIRE(rc == SQLITE_OK);
}

void init_journal(sqlite3* db) {
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

std::int64_t row_count(sqlite3* db) {
    sqlite3_stmt* stmt = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM _sync_journal;", -1, &stmt, nullptr) == SQLITE_OK);
    REQUIRE(stmt != nullptr);
    REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
    const auto value = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return value;
}

} // namespace

TEST_CASE("sync engine catches up lagging db via protocol batch", "[sync][engine][integration]") {
    const auto source_path = make_temp_db_path();
    const auto target_path = make_temp_db_path();

    sqlite3* source = nullptr;
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);

    init_journal(source);
    init_journal(target);

    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-a');
    )sql");
    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (2, 110, 2, 7, 'accounts', '{"id":"1"}', 'UPDATE', '{"email":"a@x.com"}', '{"email":"b@x.com"}', 'placeholder', 1, 'batch-a');
    )sql");

    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    const auto wire = tightrope::sync::encode_journal_batch(batch);
    const auto decoded = tightrope::sync::decode_journal_batch(wire);
    REQUIRE(decoded.has_value());

    const auto apply = tightrope::sync::SyncEngine::apply_batch(target, *decoded, /*applied_value=*/2);
    REQUIRE(apply.success);
    REQUIRE(apply.applied_up_to_seq == 2);
    REQUIRE(row_count(target) == 2);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rolls back full batch on checksum mismatch", "[sync][engine][integration]") {
    const auto source_path = make_temp_db_path();
    const auto target_path = make_temp_db_path();

    sqlite3* source = nullptr;
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);

    init_journal(source);
    init_journal(target);

    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (1, 100, 1, 7, 'accounts', '{"id":"1"}', 'INSERT', '', '{"email":"a@x.com"}', 'placeholder', 1, 'batch-b');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    REQUIRE(batch.entries.size() == 1);
    batch.entries.front().checksum = "bad-checksum";

    const auto apply = tightrope::sync::SyncEngine::apply_batch(target, batch, /*applied_value=*/2);
    REQUIRE_FALSE(apply.success);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}

TEST_CASE("sync engine rejects non-replicated table entries from remote batch", "[sync][engine][integration]") {
    const auto source_path = make_temp_db_path();
    const auto target_path = make_temp_db_path();

    sqlite3* source = nullptr;
    sqlite3* target = nullptr;
    REQUIRE(sqlite3_open_v2(source_path.c_str(), &source, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_open_v2(target_path.c_str(), &target, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(source != nullptr);
    REQUIRE(target != nullptr);

    init_journal(source);
    init_journal(target);

    exec_sql(source, R"sql(
        INSERT INTO _sync_journal (seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id)
        VALUES
          (1, 100, 1, 7, 'request_logs', '{"id":"1"}', 'INSERT', '', '{"status":"ok"}', 'placeholder', 1, 'batch-x');
    )sql");
    REQUIRE(tightrope::sync::SyncEngine::recompute_checksums(source));
    auto batch = tightrope::sync::SyncEngine::build_batch(source, /*after_seq=*/0, /*limit=*/500);
    REQUIRE(batch.entries.size() == 1);

    const auto apply = tightrope::sync::SyncEngine::apply_batch(target, batch, /*applied_value=*/2);
    REQUIRE_FALSE(apply.success);
    REQUIRE(row_count(target) == 0);

    sqlite3_close(source);
    sqlite3_close(target);
    std::remove(source_path.c_str());
    std::remove(target_path.c_str());
}
