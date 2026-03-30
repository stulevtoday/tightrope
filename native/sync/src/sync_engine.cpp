#include "sync_engine.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "checksum.h"
#include "conflict_resolver.h"
#include "journal_batch_id.h"
#include "sync_logging.h"

namespace tightrope::sync {

namespace {

std::string sqlite_error(sqlite3* db) {
    const char* text = sqlite3_errmsg(db);
    return text == nullptr ? std::string("sqlite error") : std::string(text);
}

bool exec_sql(sqlite3* db, const char* sql, std::string* error) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc == SQLITE_OK) {
        return true;
    }
    if (error != nullptr) {
        *error = err != nullptr ? std::string(err) : sqlite_error(db);
    }
    if (err != nullptr) {
        sqlite3_free(err);
    }
    return false;
}

std::string column_text(sqlite3_stmt* stmt, const int index) {
    const auto* text = sqlite3_column_text(stmt, index);
    return text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(text));
}

std::string existing_checksum_for_seq(sqlite3* db, sqlite3_stmt* stmt, const std::uint64_t seq) {
    sqlite3_reset(stmt);
    sqlite3_clear_bindings(stmt);
    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(seq));
    const int step = sqlite3_step(stmt);
    if (step == SQLITE_ROW) {
        return column_text(stmt, 0);
    }
    return {};
}

} // namespace

bool SyncEngine::recompute_checksums(sqlite3* db) {
    if (db == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "recompute_checksums_rejected_null_db");
        return false;
    }
    log_sync_event(SyncLogLevel::Debug, "sync_engine", "recompute_checksums_begin");

    sqlite3_stmt* select_stmt = nullptr;
    const char* select_sql = R"sql(
        SELECT seq, table_name, row_pk, op, old_values, new_values
        FROM _sync_journal
        ORDER BY seq ASC;
    )sql";
    if (sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK || select_stmt == nullptr) {
        if (select_stmt != nullptr) {
            sqlite3_finalize(select_stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "sync_engine",
            "recompute_checksums_prepare_select_failed",
            sqlite_error(db));
        return false;
    }

    std::vector<std::pair<std::uint64_t, std::string>> updates;
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        const std::uint64_t seq = static_cast<std::uint64_t>(sqlite3_column_int64(select_stmt, 0));
        const auto checksum = journal_checksum(
            column_text(select_stmt, 1),
            column_text(select_stmt, 2),
            column_text(select_stmt, 3),
            column_text(select_stmt, 4),
            column_text(select_stmt, 5)
        );
        updates.emplace_back(seq, checksum);
    }
    sqlite3_finalize(select_stmt);
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_engine",
        "recompute_checksums_selected",
        "rows=" + std::to_string(updates.size()));

    sqlite3_stmt* update_stmt = nullptr;
    const char* update_sql = "UPDATE _sync_journal SET checksum = ?1 WHERE seq = ?2;";
    if (sqlite3_prepare_v2(db, update_sql, -1, &update_stmt, nullptr) != SQLITE_OK || update_stmt == nullptr) {
        if (update_stmt != nullptr) {
            sqlite3_finalize(update_stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "sync_engine",
            "recompute_checksums_prepare_update_failed",
            sqlite_error(db));
        return false;
    }

    for (const auto& update : updates) {
        sqlite3_reset(update_stmt);
        sqlite3_clear_bindings(update_stmt);
        sqlite3_bind_text(update_stmt, 1, update.second.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(update_stmt, 2, static_cast<sqlite3_int64>(update.first));
        if (sqlite3_step(update_stmt) != SQLITE_DONE) {
            sqlite3_finalize(update_stmt);
            log_sync_event(
                SyncLogLevel::Error,
                "sync_engine",
                "recompute_checksums_update_failed",
                "seq=" + std::to_string(update.first) + " error=" + sqlite_error(db));
            return false;
        }
    }
    sqlite3_finalize(update_stmt);
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_engine",
        "recompute_checksums_complete",
        "rows=" + std::to_string(updates.size()));
    return true;
}

JournalBatchFrame SyncEngine::build_batch(sqlite3* db, const std::uint64_t after_seq, const std::size_t limit) {
    JournalBatchFrame frame;
    frame.from_seq = after_seq;
    frame.to_seq = after_seq;
    if (db == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "sync_engine", "build_batch_rejected_null_db");
        return frame;
    }
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_engine",
        "build_batch_begin",
        "after_seq=" + std::to_string(after_seq) + " limit=" + std::to_string(limit));

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"sql(
        SELECT seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        FROM _sync_journal
        WHERE seq > ?1
        ORDER BY seq ASC
        LIMIT ?2;
    )sql";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "sync_engine",
            "build_batch_prepare_failed",
            sqlite_error(db));
        return frame;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(after_seq));
    sqlite3_bind_int(stmt, 2, static_cast<int>(limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        JournalWireEntry entry;
        entry.seq = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 0));
        entry.hlc_wall = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
        entry.hlc_counter = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 2));
        entry.site_id = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 3));
        entry.table_name = column_text(stmt, 4);
        entry.row_pk = column_text(stmt, 5);
        entry.op = column_text(stmt, 6);
        entry.old_values = column_text(stmt, 7);
        entry.new_values = column_text(stmt, 8);
        entry.checksum = column_text(stmt, 9);
        entry.applied = sqlite3_column_int(stmt, 10);
        entry.batch_id = column_text(stmt, 11);

        frame.to_seq = std::max(frame.to_seq, entry.seq);
        frame.entries.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_engine",
        "build_batch_complete",
        "from_seq=" + std::to_string(frame.from_seq) + " to_seq=" + std::to_string(frame.to_seq) + " entries=" +
            std::to_string(frame.entries.size()));
    return frame;
}

ApplyBatchResult SyncEngine::apply_batch(sqlite3* db, const JournalBatchFrame& batch, const int applied_value) {
    ApplyBatchResult result;
    if (db == nullptr) {
        result.error = "db is null";
        log_sync_event(SyncLogLevel::Error, "sync_engine", "apply_batch_rejected_null_db");
        return result;
    }
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_engine",
        "apply_batch_begin",
        "entries=" + std::to_string(batch.entries.size()) + " from_seq=" + std::to_string(batch.from_seq) +
            " to_seq=" + std::to_string(batch.to_seq) + " applied=" + std::to_string(applied_value));

    std::string sql_error;
    if (!exec_sql(db, "BEGIN IMMEDIATE;", &sql_error)) {
        result.error = sql_error;
        log_sync_event(SyncLogLevel::Error, "sync_engine", "apply_batch_begin_transaction_failed", sql_error);
        return result;
    }

    sqlite3_stmt* insert_stmt = nullptr;
    sqlite3_stmt* select_stmt = nullptr;
    const char* insert_sql = R"sql(
        INSERT OR IGNORE INTO _sync_journal (
          seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12);
    )sql";
    const char* select_sql = "SELECT checksum FROM _sync_journal WHERE seq = ?1;";

    if (sqlite3_prepare_v2(db, insert_sql, -1, &insert_stmt, nullptr) != SQLITE_OK || insert_stmt == nullptr ||
        sqlite3_prepare_v2(db, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK || select_stmt == nullptr) {
        if (insert_stmt != nullptr) {
            sqlite3_finalize(insert_stmt);
        }
        if (select_stmt != nullptr) {
            sqlite3_finalize(select_stmt);
        }
        exec_sql(db, "ROLLBACK;", nullptr);
        result.error = sqlite_error(db);
        log_sync_event(
            SyncLogLevel::Error,
            "sync_engine",
            "apply_batch_prepare_failed",
            result.error);
        return result;
    }

    for (const auto& entry : batch.entries) {
        log_sync_event(
            SyncLogLevel::Trace,
            "sync_engine",
            "apply_batch_entry_begin",
            "seq=" + std::to_string(entry.seq) + " table=" + entry.table_name + " op=" + entry.op);
        if (!is_table_replicated(entry.table_name)) {
            sqlite3_finalize(insert_stmt);
            sqlite3_finalize(select_stmt);
            exec_sql(db, "ROLLBACK;", nullptr);
            result.error = "table is not replicated: " + entry.table_name;
            log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_batch_rejected_table_not_replicated", result.error);
            return result;
        }

        const auto expected =
            journal_checksum(entry.table_name, entry.row_pk, entry.op, entry.old_values, entry.new_values);
        if (expected != entry.checksum) {
            sqlite3_finalize(insert_stmt);
            sqlite3_finalize(select_stmt);
            exec_sql(db, "ROLLBACK;", nullptr);
            result.error = "checksum mismatch for seq " + std::to_string(entry.seq);
            log_sync_event(SyncLogLevel::Warning, "sync_engine", "apply_batch_rejected_checksum_mismatch", result.error);
            return result;
        }

        sqlite3_reset(insert_stmt);
        sqlite3_clear_bindings(insert_stmt);
        const auto batch_id = entry.batch_id.empty() ? generate_batch_id() : entry.batch_id;
        sqlite3_bind_int64(insert_stmt, 1, static_cast<sqlite3_int64>(entry.seq));
        sqlite3_bind_int64(insert_stmt, 2, static_cast<sqlite3_int64>(entry.hlc_wall));
        sqlite3_bind_int64(insert_stmt, 3, static_cast<sqlite3_int64>(entry.hlc_counter));
        sqlite3_bind_int64(insert_stmt, 4, static_cast<sqlite3_int64>(entry.site_id));
        sqlite3_bind_text(insert_stmt, 5, entry.table_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 6, entry.row_pk.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 7, entry.op.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 8, entry.old_values.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 9, entry.new_values.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(insert_stmt, 10, entry.checksum.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(insert_stmt, 11, applied_value);
        sqlite3_bind_text(insert_stmt, 12, batch_id.c_str(), -1, SQLITE_TRANSIENT);

        if (sqlite3_step(insert_stmt) != SQLITE_DONE) {
            sqlite3_finalize(insert_stmt);
            sqlite3_finalize(select_stmt);
            exec_sql(db, "ROLLBACK;", nullptr);
            result.error = sqlite_error(db);
            log_sync_event(
                SyncLogLevel::Error,
                "sync_engine",
                "apply_batch_insert_failed",
                "seq=" + std::to_string(entry.seq) + " error=" + result.error);
            return result;
        }

        if (sqlite3_changes(db) == 0) {
            const auto existing_checksum = existing_checksum_for_seq(db, select_stmt, entry.seq);
            if (existing_checksum != entry.checksum) {
                sqlite3_finalize(insert_stmt);
                sqlite3_finalize(select_stmt);
                exec_sql(db, "ROLLBACK;", nullptr);
                result.error = "existing checksum mismatch for seq " + std::to_string(entry.seq);
                log_sync_event(
                    SyncLogLevel::Warning,
                    "sync_engine",
                    "apply_batch_rejected_existing_checksum_mismatch",
                    result.error);
                return result;
            }
        } else {
            ++result.applied_count;
        }
        result.applied_up_to_seq = std::max(result.applied_up_to_seq, entry.seq);
    }

    sqlite3_finalize(insert_stmt);
    sqlite3_finalize(select_stmt);
    if (!exec_sql(db, "COMMIT;", &sql_error)) {
        exec_sql(db, "ROLLBACK;", nullptr);
        result.error = sql_error;
        log_sync_event(SyncLogLevel::Error, "sync_engine", "apply_batch_commit_failed", sql_error);
        return result;
    }

    result.success = true;
    log_sync_event(
        SyncLogLevel::Info,
        "sync_engine",
        "apply_batch_complete",
        "applied_count=" + std::to_string(result.applied_count) + " applied_up_to_seq=" +
            std::to_string(result.applied_up_to_seq));
    return result;
}

} // namespace tightrope::sync
