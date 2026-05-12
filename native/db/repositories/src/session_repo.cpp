#include "session_repo.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sqlite_repo_utils.h"
#include "text/ascii.h"

namespace tightrope::db {

namespace {

constexpr std::string_view kEnsureProxyStickySchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS proxy_sticky_sessions (
    session_key TEXT PRIMARY KEY,
    account_id TEXT NOT NULL,
    kind TEXT NOT NULL DEFAULT 'sticky_thread',
    updated_at_ms INTEGER NOT NULL,
    expires_at_ms INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_proxy_sticky_sessions_expires_at
    ON proxy_sticky_sessions(expires_at_ms);
)SQL";
constexpr const char* kEnsureProxyStickySessionKindColumnSql =
    "ALTER TABLE proxy_sticky_sessions ADD COLUMN kind TEXT NOT NULL DEFAULT 'sticky_thread';";

constexpr std::string_view kEnsureProxyResponseContinuitySchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS proxy_response_continuity (
    continuity_key TEXT NOT NULL,
    api_key_scope TEXT NOT NULL DEFAULT '',
    response_id TEXT NOT NULL,
    account_id TEXT NOT NULL DEFAULT '',
    recovery_input_json TEXT NOT NULL DEFAULT '',
    updated_at_ms INTEGER NOT NULL,
    expires_at_ms INTEGER NOT NULL,
    PRIMARY KEY(response_id, api_key_scope)
);
CREATE INDEX IF NOT EXISTS idx_proxy_response_continuity_key_scope_updated
    ON proxy_response_continuity(continuity_key, api_key_scope, updated_at_ms DESC);
CREATE INDEX IF NOT EXISTS idx_proxy_response_continuity_expires_at
    ON proxy_response_continuity(expires_at_ms);
)SQL";
constexpr const char* kEnsureProxyResponseContinuityRecoveryInputColumnSql =
    "ALTER TABLE proxy_response_continuity ADD COLUMN recovery_input_json TEXT NOT NULL DEFAULT '';";

bool ensure_schema(SQLite::Database& db) noexcept {
    return sqlite_repo_utils::exec_sql(db, kEnsureProxyStickySchemaSql.data()) &&
           sqlite_repo_utils::ensure_column(
               db,
               "proxy_sticky_sessions",
               "kind",
               kEnsureProxyStickySessionKindColumnSql
           );
}

std::string normalize_sticky_session_kind(std::string_view kind) {
    auto normalized = core::text::to_lower_ascii(core::text::trim_ascii(kind));
    if (normalized == "prompt_cache" || normalized == "codex_session" || normalized == "sticky_thread") {
        return normalized;
    }
    return "sticky_thread";
}

bool ensure_response_continuity_schema(SQLite::Database& db) noexcept {
    return sqlite_repo_utils::exec_sql(db, kEnsureProxyResponseContinuitySchemaSql.data()) &&
           sqlite_repo_utils::ensure_column(
               db,
               "proxy_response_continuity",
               "recovery_input_json",
               kEnsureProxyResponseContinuityRecoveryInputColumnSql
           );
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
    const std::int64_t ttl_ms,
    const std::string_view kind
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || session_key.empty() || account_id.empty() || !ensure_schema(*handle.db)) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
INSERT INTO proxy_sticky_sessions(session_key, account_id, kind, updated_at_ms, expires_at_ms)
VALUES (?1, ?2, ?3, ?4, ?5)
ON CONFLICT(session_key) DO UPDATE SET
    account_id = excluded.account_id,
    kind = excluded.kind,
    updated_at_ms = excluded.updated_at_ms,
    expires_at_ms = excluded.expires_at_ms;
)SQL";

    const std::int64_t safe_ttl_ms = std::max<std::int64_t>(1, ttl_ms);
    const std::int64_t expires_at_ms = now_ms + safe_ttl_ms;
    const auto normalized_kind = normalize_sticky_session_kind(kind);

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(session_key));
        stmt.bind(2, std::string(account_id));
        stmt.bind(3, normalized_kind);
        stmt.bind(4, now_ms);
        stmt.bind(5, expires_at_ms);
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

std::size_t purge_proxy_sticky_sessions_for_account(sqlite3* db, const std::string_view account_id) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || account_id.empty() || !ensure_schema(*handle.db)) {
        return 0;
    }

    constexpr const char* kSql = "DELETE FROM proxy_sticky_sessions WHERE account_id = ?1;";
    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(account_id));
        (void)stmt.exec();
        return static_cast<std::size_t>(handle.db->getChanges());
    } catch (...) {
        return 0;
    }
}

std::vector<ProxyStickySessionRecord>
list_proxy_sticky_sessions(sqlite3* db, const std::size_t limit, const std::size_t offset) noexcept {
    std::vector<ProxyStickySessionRecord> records;
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || limit == 0) {
        return records;
    }

    constexpr const char* kSql = R"SQL(
SELECT session_key, account_id, kind, updated_at_ms, expires_at_ms
FROM proxy_sticky_sessions
ORDER BY updated_at_ms DESC, session_key ASC
LIMIT ?1 OFFSET ?2;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, static_cast<std::int64_t>(limit));
        stmt.bind(2, static_cast<std::int64_t>(offset));

        while (stmt.executeStep()) {
            ProxyStickySessionRecord record;
            if (!stmt.getColumn(0).isNull()) {
                record.session_key = stmt.getColumn(0).getString();
            }
            if (!stmt.getColumn(1).isNull()) {
                record.account_id = stmt.getColumn(1).getString();
            }
            if (!stmt.getColumn(2).isNull()) {
                record.kind = normalize_sticky_session_kind(stmt.getColumn(2).getString());
            }
            if (!stmt.getColumn(3).isNull()) {
                record.updated_at_ms = stmt.getColumn(3).getInt64();
            }
            if (!stmt.getColumn(4).isNull()) {
                record.expires_at_ms = stmt.getColumn(4).getInt64();
            }
            if (record.session_key.empty() || record.account_id.empty()) {
                continue;
            }
            records.push_back(std::move(record));
        }
    } catch (...) {
        return {};
    }

    return records;
}

bool ensure_proxy_response_continuity_schema(sqlite3* db) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return false;
    }
    return ensure_response_continuity_schema(*handle.db);
}

bool upsert_proxy_response_continuity(
    sqlite3* db,
    const std::string_view continuity_key,
    const std::string_view api_key_scope,
    const std::string_view response_id,
    const std::string_view account_id,
    const std::int64_t now_ms,
    const std::int64_t ttl_ms,
    const std::string_view recovery_input_json
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || continuity_key.empty() || response_id.empty() || !ensure_response_continuity_schema(*handle.db)) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
INSERT INTO proxy_response_continuity(
    continuity_key,
    api_key_scope,
    response_id,
    account_id,
    recovery_input_json,
    updated_at_ms,
    expires_at_ms
)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
ON CONFLICT(response_id, api_key_scope) DO UPDATE SET
    continuity_key = excluded.continuity_key,
    account_id = excluded.account_id,
    recovery_input_json = CASE
        WHEN excluded.recovery_input_json != '' THEN excluded.recovery_input_json
        ELSE proxy_response_continuity.recovery_input_json
    END,
    updated_at_ms = excluded.updated_at_ms,
    expires_at_ms = excluded.expires_at_ms;
)SQL";

    const std::int64_t safe_ttl_ms = std::max<std::int64_t>(1, ttl_ms);
    const std::int64_t expires_at_ms = now_ms + safe_ttl_ms;

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(continuity_key));
        stmt.bind(2, std::string(api_key_scope));
        stmt.bind(3, std::string(response_id));
        stmt.bind(4, std::string(account_id));
        stmt.bind(5, std::string(recovery_input_json));
        stmt.bind(6, now_ms);
        stmt.bind(7, expires_at_ms);
        return stmt.exec() > 0;
    } catch (...) {
        return false;
    }
}

std::optional<ProxyResponseContinuityRecord> find_proxy_response_continuity(
    sqlite3* db,
    const std::string_view continuity_key,
    const std::string_view api_key_scope,
    const std::int64_t now_ms
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || continuity_key.empty() || !ensure_response_continuity_schema(*handle.db)) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
SELECT continuity_key, api_key_scope, response_id, account_id, recovery_input_json, updated_at_ms, expires_at_ms
FROM proxy_response_continuity
WHERE continuity_key = ?1
  AND api_key_scope = ?2
  AND expires_at_ms > ?3
ORDER BY updated_at_ms DESC, rowid DESC
LIMIT 1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(continuity_key));
        stmt.bind(2, std::string(api_key_scope));
        stmt.bind(3, now_ms);
        if (!stmt.executeStep()) {
            return std::nullopt;
        }

        ProxyResponseContinuityRecord record;
        if (!stmt.getColumn(0).isNull()) {
            record.continuity_key = stmt.getColumn(0).getString();
        }
        if (!stmt.getColumn(1).isNull()) {
            record.api_key_scope = stmt.getColumn(1).getString();
        }
        if (!stmt.getColumn(2).isNull()) {
            record.response_id = stmt.getColumn(2).getString();
        }
        if (!stmt.getColumn(3).isNull()) {
            record.account_id = stmt.getColumn(3).getString();
        }
        if (!stmt.getColumn(4).isNull()) {
            record.recovery_input_json = stmt.getColumn(4).getString();
        }
        if (!stmt.getColumn(5).isNull()) {
            record.updated_at_ms = stmt.getColumn(5).getInt64();
        }
        if (!stmt.getColumn(6).isNull()) {
            record.expires_at_ms = stmt.getColumn(6).getInt64();
        }
        if (record.continuity_key.empty() || record.response_id.empty()) {
            return std::nullopt;
        }
        return record;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<ProxyResponseContinuityRecord> find_proxy_response_continuity_by_response_id(
    sqlite3* db,
    const std::string_view response_id,
    const std::string_view api_key_scope,
    const std::int64_t now_ms
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || response_id.empty() || !ensure_response_continuity_schema(*handle.db)) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
SELECT continuity_key, api_key_scope, response_id, account_id, recovery_input_json, updated_at_ms, expires_at_ms
FROM proxy_response_continuity
WHERE response_id = ?1
  AND api_key_scope = ?2
  AND expires_at_ms > ?3
LIMIT 1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(response_id));
        stmt.bind(2, std::string(api_key_scope));
        stmt.bind(3, now_ms);
        if (!stmt.executeStep()) {
            return std::nullopt;
        }

        ProxyResponseContinuityRecord record;
        if (!stmt.getColumn(0).isNull()) {
            record.continuity_key = stmt.getColumn(0).getString();
        }
        if (!stmt.getColumn(1).isNull()) {
            record.api_key_scope = stmt.getColumn(1).getString();
        }
        if (!stmt.getColumn(2).isNull()) {
            record.response_id = stmt.getColumn(2).getString();
        }
        if (!stmt.getColumn(3).isNull()) {
            record.account_id = stmt.getColumn(3).getString();
        }
        if (!stmt.getColumn(4).isNull()) {
            record.recovery_input_json = stmt.getColumn(4).getString();
        }
        if (!stmt.getColumn(5).isNull()) {
            record.updated_at_ms = stmt.getColumn(5).getInt64();
        }
        if (!stmt.getColumn(6).isNull()) {
            record.expires_at_ms = stmt.getColumn(6).getInt64();
        }
        if (record.continuity_key.empty() || record.response_id.empty()) {
            return std::nullopt;
        }
        return record;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> find_proxy_response_continuity_account(
    sqlite3* db,
    const std::string_view response_id,
    const std::string_view api_key_scope,
    const std::int64_t now_ms
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || response_id.empty() || !ensure_response_continuity_schema(*handle.db)) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
SELECT account_id
FROM proxy_response_continuity
WHERE response_id = ?1
  AND api_key_scope = ?2
  AND expires_at_ms > ?3
LIMIT 1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(response_id));
        stmt.bind(2, std::string(api_key_scope));
        stmt.bind(3, now_ms);
        if (stmt.executeStep() && !stmt.getColumn(0).isNull()) {
            const auto account = stmt.getColumn(0).getString();
            if (!account.empty()) {
                return account;
            }
        }
        return std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

std::size_t purge_expired_proxy_response_continuity(sqlite3* db, const std::int64_t now_ms) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_response_continuity_schema(*handle.db)) {
        return 0;
    }

    constexpr const char* kSql = "DELETE FROM proxy_response_continuity WHERE expires_at_ms <= ?1;";
    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, now_ms);
        (void)stmt.exec();
        return static_cast<std::size_t>(handle.db->getChanges());
    } catch (...) {
        return 0;
    }
}

std::size_t purge_proxy_response_continuity_for_account(sqlite3* db, const std::string_view account_id) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || account_id.empty() || !ensure_response_continuity_schema(*handle.db)) {
        return 0;
    }

    constexpr const char* kSql = "DELETE FROM proxy_response_continuity WHERE account_id = ?1;";
    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(account_id));
        (void)stmt.exec();
        return static_cast<std::size_t>(handle.db->getChanges());
    } catch (...) {
        return 0;
    }
}

} // namespace tightrope::db
