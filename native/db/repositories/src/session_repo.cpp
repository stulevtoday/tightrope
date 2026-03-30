#include "session_repo.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "sqlite_repo_utils.h"

namespace tightrope::db {

namespace {

constexpr std::string_view kEnsureProxyStickySchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS proxy_sticky_sessions (
    session_key TEXT PRIMARY KEY,
    account_id TEXT NOT NULL,
    updated_at_ms INTEGER NOT NULL,
    expires_at_ms INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_proxy_sticky_sessions_expires_at
    ON proxy_sticky_sessions(expires_at_ms);
)SQL";

bool ensure_schema(SQLite::Database& db) noexcept {
    return sqlite_repo_utils::exec_sql(db, kEnsureProxyStickySchemaSql.data());
}

} // namespace

bool ensure_proxy_sticky_session_schema(sqlite3* db) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return false;
    }
    return ensure_schema(*handle.db);
}

bool upsert_proxy_sticky_session(
    sqlite3* db,
    std::string_view session_key,
    std::string_view account_id,
    const std::int64_t now_ms,
    const std::int64_t ttl_ms
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || session_key.empty() || account_id.empty() || !ensure_schema(*handle.db)) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
INSERT INTO proxy_sticky_sessions(session_key, account_id, updated_at_ms, expires_at_ms)
VALUES (?1, ?2, ?3, ?4)
ON CONFLICT(session_key) DO UPDATE SET
    account_id = excluded.account_id,
    updated_at_ms = excluded.updated_at_ms,
    expires_at_ms = excluded.expires_at_ms;
)SQL";

    const std::int64_t safe_ttl_ms = std::max<std::int64_t>(1, ttl_ms);
    const std::int64_t expires_at_ms = now_ms + safe_ttl_ms;

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(session_key));
        stmt.bind(2, std::string(account_id));
        stmt.bind(3, now_ms);
        stmt.bind(4, expires_at_ms);
        return stmt.exec() > 0;
    } catch (...) {
        return false;
    }
}

std::optional<std::string>
find_proxy_sticky_session_account(sqlite3* db, std::string_view session_key, const std::int64_t now_ms) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || session_key.empty() || !ensure_schema(*handle.db)) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
SELECT account_id
FROM proxy_sticky_sessions
WHERE session_key = ?1
  AND expires_at_ms > ?2
LIMIT 1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(session_key));
        stmt.bind(2, now_ms);
        if (!stmt.executeStep() || stmt.getColumn(0).isNull()) {
            return std::nullopt;
        }
        const auto value = stmt.getColumn(0).getString();
        if (value.empty()) {
            return std::nullopt;
        }
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

std::size_t purge_expired_proxy_sticky_sessions(sqlite3* db, const std::int64_t now_ms) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return 0;
    }

    constexpr const char* kSql = "DELETE FROM proxy_sticky_sessions WHERE expires_at_ms <= ?1;";
    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, now_ms);
        (void)stmt.exec();
        return static_cast<std::size_t>(handle.db->getChanges());
    } catch (...) {
        return 0;
    }
}

} // namespace tightrope::db

