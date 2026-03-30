#include "persistent_journal.h"

#include <algorithm>
#include <limits>
#include <string>

#include "checksum.h"
#include "journal_batch_id.h"
#include "sync_logging.h"

namespace tightrope::sync {

namespace {

std::string column_text(sqlite3_stmt* stmt, const int column) {
    const auto* text = sqlite3_column_text(stmt, column);
    return text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(text));
}

std::string sqlite_error(sqlite3* db) {
    if (db == nullptr) {
        return "sqlite db is null";
    }
    const char* text = sqlite3_errmsg(db);
    return text == nullptr ? std::string("sqlite error") : std::string(text);
}

JournalEntry read_entry(sqlite3_stmt* stmt) {
    JournalEntry entry;
    entry.seq = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 0));
    entry.hlc.wall = static_cast<std::uint64_t>(sqlite3_column_int64(stmt, 1));
    entry.hlc.counter = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 2));
    entry.hlc.site_id = static_cast<std::uint32_t>(sqlite3_column_int(stmt, 3));
    entry.table_name = column_text(stmt, 4);
    entry.row_pk = column_text(stmt, 5);
    entry.op = column_text(stmt, 6);
    entry.old_values = column_text(stmt, 7);
    entry.new_values = column_text(stmt, 8);
    entry.checksum = column_text(stmt, 9);
    entry.applied = sqlite3_column_int(stmt, 10);
    entry.batch_id = column_text(stmt, 11);
    return entry;
}

} // namespace

PersistentJournal::PersistentJournal(sqlite3* db) : db_(db) {}

std::optional<JournalEntry> PersistentJournal::append(const PendingJournalEntry& entry) {
    if (db_ == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "persistent_journal", "append_rejected_null_db");
        return std::nullopt;
    }
    log_sync_event(
        SyncLogLevel::Trace,
        "persistent_journal",
        "append_begin",
        "site_id=" + std::to_string(entry.hlc.site_id) + " table=" + entry.table_name + " row_pk=" + entry.row_pk +
            " op=" + entry.op + " applied=" + std::to_string(entry.applied));

    const auto batch_id = entry.batch_id.empty() ? generate_batch_id() : entry.batch_id;
    const auto checksum =
        journal_checksum(entry.table_name, entry.row_pk, entry.op, entry.old_values, entry.new_values);

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"sql(
        INSERT INTO _sync_journal (
          hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        ) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11);
    )sql";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "append_prepare_failed",
            sqlite_error(db_));
        return std::nullopt;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(entry.hlc.wall));
    sqlite3_bind_int(stmt, 2, static_cast<int>(entry.hlc.counter));
    sqlite3_bind_int(stmt, 3, static_cast<int>(entry.hlc.site_id));
    sqlite3_bind_text(stmt, 4, entry.table_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, entry.row_pk.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 6, entry.op.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 7, entry.old_values.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, entry.new_values.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 9, checksum.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 10, entry.applied);
    sqlite3_bind_text(stmt, 11, batch_id.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "append_insert_failed",
            sqlite_error(db_));
        return std::nullopt;
    }
    sqlite3_finalize(stmt);

    JournalEntry created;
    created.seq = static_cast<std::uint64_t>(sqlite3_last_insert_rowid(db_));
    created.hlc = entry.hlc;
    created.table_name = entry.table_name;
    created.row_pk = entry.row_pk;
    created.op = entry.op;
    created.old_values = entry.old_values;
    created.new_values = entry.new_values;
    created.checksum = checksum;
    created.applied = entry.applied;
    created.batch_id = batch_id;
    log_sync_event(
        SyncLogLevel::Debug,
        "persistent_journal",
        "append_complete",
        "seq=" + std::to_string(created.seq) + " batch_id=" + created.batch_id);
    return created;
}

std::vector<JournalEntry> PersistentJournal::entries_after(const std::uint64_t after_seq, const std::size_t limit) const {
    std::vector<JournalEntry> entries;
    if (db_ == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "persistent_journal", "entries_after_rejected_null_db");
        return entries;
    }
    log_sync_event(
        SyncLogLevel::Trace,
        "persistent_journal",
        "entries_after_begin",
        "after_seq=" + std::to_string(after_seq) + " limit=" + std::to_string(limit));

    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"sql(
        SELECT seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        FROM _sync_journal
        WHERE seq > ?1
        ORDER BY seq ASC
        LIMIT ?2;
    )sql";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "entries_after_prepare_failed",
            sqlite_error(db_));
        return entries;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(after_seq));
    const auto clamped_limit = std::min<std::size_t>(limit, static_cast<std::size_t>(std::numeric_limits<int>::max()));
    sqlite3_bind_int(stmt, 2, static_cast<int>(clamped_limit));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        entries.push_back(read_entry(stmt));
    }
    sqlite3_finalize(stmt);
    log_sync_event(
        SyncLogLevel::Trace,
        "persistent_journal",
        "entries_after_complete",
        "count=" + std::to_string(entries.size()));
    return entries;
}

std::vector<JournalEntry> PersistentJournal::rollback_batch(const std::string_view batch_id) {
    std::vector<JournalEntry> removed;
    if (db_ == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "persistent_journal", "rollback_batch_rejected_null_db");
        return removed;
    }
    log_sync_event(
        SyncLogLevel::Debug,
        "persistent_journal",
        "rollback_batch_begin",
        "batch_id=" + std::string(batch_id));

    const std::string batch_id_str(batch_id);

    sqlite3_stmt* returning_stmt = nullptr;
    const char* returning_sql = R"sql(
        DELETE FROM _sync_journal
        WHERE batch_id = ?1
        RETURNING seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id;
    )sql";
    if (sqlite3_prepare_v2(db_, returning_sql, -1, &returning_stmt, nullptr) == SQLITE_OK && returning_stmt != nullptr) {
        sqlite3_bind_text(returning_stmt, 1, batch_id_str.c_str(), -1, SQLITE_TRANSIENT);
        int rc = SQLITE_ROW;
        while ((rc = sqlite3_step(returning_stmt)) == SQLITE_ROW) {
            removed.push_back(read_entry(returning_stmt));
        }
        sqlite3_finalize(returning_stmt);
        if (rc == SQLITE_DONE) {
            std::sort(removed.begin(), removed.end(), [](const JournalEntry& lhs, const JournalEntry& rhs) {
                return lhs.seq > rhs.seq;
            });
            log_sync_event(
                SyncLogLevel::Debug,
                "persistent_journal",
                "rollback_batch_complete",
                "batch_id=" + std::string(batch_id) + " removed=" + std::to_string(removed.size()));
            return removed;
        }
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "rollback_batch_returning_failed",
            "batch_id=" + std::string(batch_id) + " error=" + sqlite_error(db_));
        return {};
    }
    if (returning_stmt != nullptr) {
        sqlite3_finalize(returning_stmt);
    }

    sqlite3_stmt* select_stmt = nullptr;
    const char* select_sql = R"sql(
        SELECT seq, hlc_wall, hlc_counter, site_id, table_name, row_pk, op, old_values, new_values, checksum, applied, batch_id
        FROM _sync_journal
        WHERE batch_id = ?1
        ORDER BY seq DESC;
    )sql";
    if (sqlite3_prepare_v2(db_, select_sql, -1, &select_stmt, nullptr) != SQLITE_OK || select_stmt == nullptr) {
        if (select_stmt != nullptr) {
            sqlite3_finalize(select_stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "rollback_batch_select_failed",
            sqlite_error(db_));
        return {};
    }

    sqlite3_bind_text(select_stmt, 1, batch_id_str.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(select_stmt) == SQLITE_ROW) {
        removed.push_back(read_entry(select_stmt));
    }
    sqlite3_finalize(select_stmt);

    if (removed.empty()) {
        log_sync_event(
            SyncLogLevel::Trace,
            "persistent_journal",
            "rollback_batch_noop",
            "batch_id=" + std::string(batch_id));
        return removed;
    }

    sqlite3_stmt* delete_stmt = nullptr;
    const char* delete_sql = "DELETE FROM _sync_journal WHERE batch_id = ?1;";
    if (sqlite3_prepare_v2(db_, delete_sql, -1, &delete_stmt, nullptr) != SQLITE_OK || delete_stmt == nullptr) {
        if (delete_stmt != nullptr) {
            sqlite3_finalize(delete_stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "rollback_batch_delete_prepare_failed",
            sqlite_error(db_));
        return {};
    }
    sqlite3_bind_text(delete_stmt, 1, batch_id_str.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(delete_stmt) != SQLITE_DONE) {
        sqlite3_finalize(delete_stmt);
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "rollback_batch_delete_failed",
            sqlite_error(db_));
        return {};
    }
    sqlite3_finalize(delete_stmt);

    std::sort(removed.begin(), removed.end(), [](const JournalEntry& lhs, const JournalEntry& rhs) {
        return lhs.seq > rhs.seq;
    });
    log_sync_event(
        SyncLogLevel::Debug,
        "persistent_journal",
        "rollback_batch_complete",
        "batch_id=" + std::string(batch_id) + " removed=" + std::to_string(removed.size()));
    return removed;
}

bool PersistentJournal::mark_applied(const std::uint64_t seq, const int applied_value) {
    if (db_ == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "persistent_journal", "mark_applied_rejected_null_db");
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE _sync_journal SET applied = ?1 WHERE seq = ?2;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "mark_applied_prepare_failed",
            sqlite_error(db_));
        return false;
    }

    sqlite3_bind_int(stmt, 1, applied_value);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(seq));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "mark_applied_update_failed",
            sqlite_error(db_));
        return false;
    }
    sqlite3_finalize(stmt);
    const bool updated = sqlite3_changes(db_) > 0;
    log_sync_event(
        SyncLogLevel::Trace,
        "persistent_journal",
        "mark_applied_complete",
        "seq=" + std::to_string(seq) + " applied=" + std::to_string(applied_value) + " updated=" +
            std::string(updated ? "1" : "0"));
    return updated;
}

std::size_t PersistentJournal::compact(const std::uint64_t cutoff_wall, const std::uint64_t max_ack_seq) {
    if (db_ == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "persistent_journal", "compact_rejected_null_db");
        return 0;
    }
    log_sync_event(
        SyncLogLevel::Debug,
        "persistent_journal",
        "compact_begin",
        "cutoff_wall=" + std::to_string(cutoff_wall) + " max_ack_seq=" + std::to_string(max_ack_seq));

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM _sync_journal WHERE hlc_wall < ?1 AND seq <= ?2;";
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "compact_prepare_failed",
            sqlite_error(db_));
        return 0;
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(cutoff_wall));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(max_ack_seq));
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "compact_delete_failed",
            sqlite_error(db_));
        return 0;
    }
    sqlite3_finalize(stmt);
    const auto deleted = static_cast<std::size_t>(sqlite3_changes(db_));
    log_sync_event(
        SyncLogLevel::Debug,
        "persistent_journal",
        "compact_complete",
        "deleted=" + std::to_string(deleted));
    return deleted;
}

std::size_t PersistentJournal::size() const {
    if (db_ == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "persistent_journal", "size_rejected_null_db");
        return 0;
    }

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM _sync_journal;", -1, &stmt, nullptr) != SQLITE_OK ||
        stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        log_sync_event(
            SyncLogLevel::Error,
            "persistent_journal",
            "size_prepare_failed",
            sqlite_error(db_));
        return 0;
    }

    std::size_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
    }
    sqlite3_finalize(stmt);
    log_sync_event(
        SyncLogLevel::Trace,
        "persistent_journal",
        "size_complete",
        "count=" + std::to_string(count));
    return count;
}

} // namespace tightrope::sync
