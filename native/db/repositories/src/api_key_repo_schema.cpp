#include "api_key_repo.h"

#include "api_key_repo_internal.h"

namespace tightrope::db {

bool ensure_api_key_schema(sqlite3* db) noexcept {
    if (db == nullptr) {
        return false;
    }

    constexpr const char* kCreateSql = R"SQL(
CREATE TABLE IF NOT EXISTS api_keys (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    key_id TEXT NOT NULL UNIQUE,
    key_hash TEXT NOT NULL UNIQUE,
    key_prefix TEXT,
    name TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'active',
    is_active INTEGER NOT NULL DEFAULT 1,
    allowed_models TEXT,
    enforced_model TEXT,
    enforced_reasoning_effort TEXT,
    expires_at TEXT,
    last_used_at TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
CREATE TABLE IF NOT EXISTS api_key_limits (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    api_key_id INTEGER NOT NULL,
    limit_type TEXT NOT NULL,
    limit_value REAL NOT NULL,
    period_seconds INTEGER NOT NULL,
    limit_window TEXT,
    max_value REAL,
    current_value REAL NOT NULL DEFAULT 0,
    model_filter TEXT,
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    FOREIGN KEY (api_key_id) REFERENCES api_keys(id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS api_key_usage_reservations (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    api_key_id INTEGER NOT NULL,
    request_id TEXT NOT NULL UNIQUE,
    status TEXT NOT NULL DEFAULT 'reserved',
    reserved_at TEXT NOT NULL DEFAULT (datetime('now')),
    settled_at TEXT,
    FOREIGN KEY (api_key_id) REFERENCES api_keys(id) ON DELETE CASCADE
);
CREATE TABLE IF NOT EXISTS api_key_usage_reservation_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    reservation_id INTEGER NOT NULL,
    metric TEXT NOT NULL,
    amount REAL NOT NULL DEFAULT 0,
    FOREIGN KEY (reservation_id) REFERENCES api_key_usage_reservations(id) ON DELETE CASCADE
);
)SQL";
    if (!api_key_repo_internal::exec_sql(db, kCreateSql)) {
        return false;
    }

    if (!api_key_repo_internal::ensure_column(
            db,
            "api_keys",
            "key_prefix",
            "ALTER TABLE api_keys ADD COLUMN key_prefix TEXT;"
        ) ||
        !api_key_repo_internal::ensure_column(
            db,
            "api_keys",
            "is_active",
            "ALTER TABLE api_keys ADD COLUMN is_active INTEGER NOT NULL DEFAULT 1;"
        ) ||
        !api_key_repo_internal::ensure_column(
            db,
            "api_keys",
            "allowed_models",
            "ALTER TABLE api_keys ADD COLUMN allowed_models TEXT;"
        ) ||
        !api_key_repo_internal::ensure_column(
            db,
            "api_keys",
            "enforced_model",
            "ALTER TABLE api_keys ADD COLUMN enforced_model TEXT;"
        ) ||
        !api_key_repo_internal::ensure_column(
            db,
            "api_keys",
            "enforced_reasoning_effort",
            "ALTER TABLE api_keys ADD COLUMN enforced_reasoning_effort TEXT;"
        ) ||
        !api_key_repo_internal::ensure_column(
            db,
            "api_keys",
            "last_used_at",
            "ALTER TABLE api_keys ADD COLUMN last_used_at TEXT;"
        ) ||
        !api_key_repo_internal::ensure_column(
            db,
            "api_key_limits",
            "limit_window",
            "ALTER TABLE api_key_limits ADD COLUMN limit_window TEXT;"
        ) ||
        !api_key_repo_internal::ensure_column(
            db,
            "api_key_limits",
            "max_value",
            "ALTER TABLE api_key_limits ADD COLUMN max_value REAL;"
        ) ||
        !api_key_repo_internal::ensure_column(
            db,
            "api_key_limits",
            "current_value",
            "ALTER TABLE api_key_limits ADD COLUMN current_value REAL NOT NULL DEFAULT 0;"
        ) ||
        !api_key_repo_internal::ensure_column(
            db,
            "api_key_limits",
            "model_filter",
            "ALTER TABLE api_key_limits ADD COLUMN model_filter TEXT;"
        )) {
        return false;
    }

    constexpr const char* kBackfillSql = R"SQL(
UPDATE api_keys SET is_active = CASE WHEN status = 'active' THEN 1 ELSE 0 END WHERE is_active IS NULL;
UPDATE api_keys SET key_prefix = substr(key_id, 1, 15) WHERE key_prefix IS NULL OR key_prefix = '';
UPDATE api_key_limits SET limit_window = CASE period_seconds
    WHEN 86400 THEN 'daily'
    WHEN 604800 THEN 'weekly'
    WHEN 2592000 THEN 'monthly'
    ELSE 'weekly'
END WHERE limit_window IS NULL OR limit_window = '';
UPDATE api_key_limits SET max_value = limit_value WHERE max_value IS NULL;
)SQL";
    return api_key_repo_internal::exec_sql(db, kBackfillSql);
}

} // namespace tightrope::db
