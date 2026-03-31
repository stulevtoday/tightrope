#include "sync_schema.h"

#include <string>

#include "conflict_resolver.h"
#include "sync_logging.h"

namespace tightrope::sync {

namespace {

bool exec_sql(sqlite3* db, const std::string& sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        log_sync_event(
            SyncLogLevel::Error,
            "sync_schema",
            "exec_sql_failed",
            err != nullptr ? std::string(err) : std::string(sqlite3_errmsg(db)));
    }
    if (err != nullptr) {
        sqlite3_free(err);
    }
    return rc == SQLITE_OK;
}

bool table_exists(sqlite3* db, const std::string& table_name) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1;", -1, &stmt, nullptr) !=
            SQLITE_OK ||
        stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);
    const bool found = sqlite3_step(stmt) == SQLITE_ROW;
    sqlite3_finalize(stmt);
    log_sync_event(
        SyncLogLevel::Trace,
        "sync_schema",
        "table_exists",
        "table=" + table_name + " found=" + std::string(found ? "1" : "0"));
    return found;
}

bool has_column(sqlite3* db, const std::string& table_name, const std::string& column_name) {
    const std::string sql = "PRAGMA table_info(" + table_name + ");";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        if (name != nullptr && column_name == name) {
            sqlite3_finalize(stmt);
            return true;
        }
    }
    sqlite3_finalize(stmt);
    return false;
}

bool ensure_column(sqlite3* db, const std::string& table_name, const std::string& column_name) {
    if (has_column(db, table_name, column_name)) {
        log_sync_event(
            SyncLogLevel::Trace,
            "sync_schema",
            "column_exists",
            "table=" + table_name + " column=" + column_name);
        return true;
    }
    const std::string sql = "ALTER TABLE " + table_name + " ADD COLUMN " + column_name + " INTEGER DEFAULT 0;";
    log_sync_event(
        SyncLogLevel::Debug,
        "sync_schema",
        "column_add_begin",
        "table=" + table_name + " column=" + column_name);
    return exec_sql(db, sql);
}

} // namespace

bool ensure_sync_durability(sqlite3* db) {
    if (db == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "sync_schema", "durability_rejected_null_db");
        return false;
    }
    log_sync_event(SyncLogLevel::Debug, "sync_schema", "ensure_durability_begin");

    if (!exec_sql(db, "PRAGMA journal_mode=WAL;")) {
        return false;
    }
    if (!exec_sql(db, "PRAGMA synchronous=FULL;")) {
        return false;
    }

    log_sync_event(SyncLogLevel::Info, "sync_schema", "ensure_durability_complete");
    return true;
}

bool ensure_sync_schema(sqlite3* db) {
    if (db == nullptr) {
        log_sync_event(SyncLogLevel::Warning, "sync_schema", "ensure_rejected_null_db");
        return false;
    }
    log_sync_event(SyncLogLevel::Debug, "sync_schema", "ensure_begin");

    if (!ensure_sync_durability(db)) {
        return false;
    }

    if (!exec_sql(
            db,
            R"sql(
        CREATE TABLE IF NOT EXISTS _sync_journal (
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
    )sql"
        )) {
        return false;
    }

    if (!exec_sql(
            db,
            R"sql(
        CREATE TABLE IF NOT EXISTS _sync_tombstones (
          table_name TEXT    NOT NULL,
          row_pk     TEXT    NOT NULL,
          deleted_at INTEGER NOT NULL,
          site_id    INTEGER NOT NULL,
          PRIMARY KEY (table_name, row_pk)
        );
    )sql"
        )) {
        return false;
    }

    for (const auto& name : replicated_table_names()) {
        const std::string table_name(name);
        if (!table_exists(db, table_name)) {
            log_sync_event(
                SyncLogLevel::Trace,
                "sync_schema",
                "replicated_table_missing",
                "table=" + table_name);
            continue;
        }
        if (!ensure_column(db, table_name, "_hlc_wall")) {
            return false;
        }
        if (!ensure_column(db, table_name, "_hlc_counter")) {
            return false;
        }
        if (!ensure_column(db, table_name, "_hlc_site")) {
            return false;
        }
    }

    log_sync_event(SyncLogLevel::Info, "sync_schema", "ensure_complete");
    return true;
}

} // namespace tightrope::sync
