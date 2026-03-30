#include "request_log_repo.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "account_repo.h"
#include "sqlite_repo_utils.h"

namespace tightrope::db {

namespace {

constexpr const char* kCreateRequestLogsSql = R"SQL(
CREATE TABLE IF NOT EXISTS request_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    account_id INTEGER,
    path TEXT NOT NULL,
    method TEXT NOT NULL,
    status_code INTEGER NOT NULL,
    model TEXT,
    requested_at TEXT NOT NULL DEFAULT (datetime('now')),
    error_code TEXT,
    transport TEXT,
    total_cost REAL NOT NULL DEFAULT 0,
    FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE SET NULL
);
)SQL";

constexpr const char* kCreateRequestedAtIndexSql = R"SQL(
CREATE INDEX IF NOT EXISTS idx_request_logs_requested_at
ON request_logs(requested_at);
)SQL";

constexpr const char* kCreateAccountRequestedAtIndexSql = R"SQL(
CREATE INDEX IF NOT EXISTS idx_request_logs_account_requested_at
ON request_logs(account_id, requested_at);
)SQL";

constexpr const char* kAddAccountIdSql = "ALTER TABLE request_logs ADD COLUMN account_id INTEGER;";
constexpr const char* kAddRequestedAtSql =
    "ALTER TABLE request_logs ADD COLUMN requested_at TEXT NOT NULL DEFAULT (datetime('now'));";
constexpr const char* kAddErrorCodeSql = "ALTER TABLE request_logs ADD COLUMN error_code TEXT;";
constexpr const char* kAddTransportSql = "ALTER TABLE request_logs ADD COLUMN transport TEXT;";
constexpr const char* kAddTotalCostSql = "ALTER TABLE request_logs ADD COLUMN total_cost REAL NOT NULL DEFAULT 0;";

bool ensure_schema(SQLite::Database& db) noexcept {
    if (!sqlite_repo_utils::exec_sql(db, kCreateRequestLogsSql)) {
        return false;
    }
    if (!sqlite_repo_utils::ensure_column(db, "request_logs", "account_id", kAddAccountIdSql) ||
        !sqlite_repo_utils::ensure_column(db, "request_logs", "requested_at", kAddRequestedAtSql) ||
        !sqlite_repo_utils::ensure_column(db, "request_logs", "error_code", kAddErrorCodeSql) ||
        !sqlite_repo_utils::ensure_column(db, "request_logs", "transport", kAddTransportSql) ||
        !sqlite_repo_utils::ensure_column(db, "request_logs", "total_cost", kAddTotalCostSql)) {
        return false;
    }
    if (!sqlite_repo_utils::exec_sql(db, kCreateRequestedAtIndexSql)) {
        return false;
    }
    return sqlite_repo_utils::exec_sql(db, kCreateAccountRequestedAtIndexSql);
}

} // namespace

bool ensure_request_log_schema(sqlite3* db) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return false;
    }
    return ensure_schema(*handle.db);
}

bool append_request_log(sqlite3* db, const RequestLogWrite& log) noexcept {
    if (log.path.empty() || log.method.empty()) {
        return false;
    }
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return false;
    }

    constexpr const char* kInsertSql = R"SQL(
INSERT INTO request_logs (
    account_id,
    path,
    method,
    status_code,
    model,
    error_code,
    transport,
    total_cost,
    requested_at
) VALUES (
    ?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, datetime('now')
);
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kInsertSql);
        if (log.account_id.has_value()) {
            stmt.bind(1, *log.account_id);
        } else {
            stmt.bind(1);
        }
        stmt.bind(2, log.path);
        stmt.bind(3, log.method);
        stmt.bind(4, log.status_code);
        if (!sqlite_repo_utils::bind_optional_text(stmt, 5, log.model) ||
            !sqlite_repo_utils::bind_optional_text(stmt, 6, log.error_code) ||
            !sqlite_repo_utils::bind_optional_text(stmt, 7, log.transport)) {
            return false;
        }
        stmt.bind(8, log.total_cost);
        return stmt.exec() > 0;
    } catch (...) {
        return false;
    }
}

std::vector<RequestLogRecord> list_recent_request_logs(sqlite3* db, const std::size_t limit, const std::size_t offset) noexcept {
    std::vector<RequestLogRecord> records;
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || limit == 0) {
        return records;
    }

    constexpr const char* kSelectSql = R"SQL(
SELECT id, account_id, path, method, status_code, model, requested_at, error_code, transport, total_cost
FROM request_logs
ORDER BY requested_at DESC, id DESC
LIMIT ?1 OFFSET ?2;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSelectSql);
        stmt.bind(1, static_cast<std::int64_t>(limit));
        stmt.bind(2, static_cast<std::int64_t>(offset));

        while (stmt.executeStep()) {
            RequestLogRecord record;
            record.id = stmt.getColumn(0).getInt64();
            if (!stmt.getColumn(1).isNull()) {
                record.account_id = stmt.getColumn(1).getInt64();
            }
            if (!stmt.getColumn(2).isNull()) {
                record.path = stmt.getColumn(2).getString();
            }
            if (!stmt.getColumn(3).isNull()) {
                record.method = stmt.getColumn(3).getString();
            }
            record.status_code = stmt.getColumn(4).isNull() ? 0 : stmt.getColumn(4).getInt();
            if (!stmt.getColumn(5).isNull()) {
                record.model = stmt.getColumn(5).getString();
            }
            if (!stmt.getColumn(6).isNull()) {
                record.requested_at = stmt.getColumn(6).getString();
            }
            if (!stmt.getColumn(7).isNull()) {
                record.error_code = stmt.getColumn(7).getString();
            }
            if (!stmt.getColumn(8).isNull()) {
                record.transport = stmt.getColumn(8).getString();
            }
            if (!stmt.getColumn(9).isNull()) {
                record.total_cost = stmt.getColumn(9).getDouble();
            }
            records.push_back(std::move(record));
        }
    } catch (...) {
        return {};
    }

    return records;
}

std::optional<std::int64_t> find_account_id_by_chatgpt_account_id(sqlite3* db, const std::string_view chatgpt_account_id) noexcept {
    if (chatgpt_account_id.empty()) {
        return std::nullopt;
    }
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return std::nullopt;
    }
    if (!ensure_accounts_schema(db)) {
        return std::nullopt;
    }

    constexpr const char* kSql = "SELECT id FROM accounts WHERE chatgpt_account_id = ?1 LIMIT 1;";
    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(chatgpt_account_id));
        if (!stmt.executeStep() || stmt.getColumn(0).isNull()) {
            return std::nullopt;
        }
        return stmt.getColumn(0).getInt64();
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace tightrope::db
