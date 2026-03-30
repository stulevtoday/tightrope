#include "account_repo.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sqlite_repo_utils.h"
#include "text/ascii.h"

namespace tightrope::db {

namespace {

constexpr const char* kEnsureAccountsSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS accounts (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    email TEXT,
    provider TEXT,
    chatgpt_account_id TEXT,
    plan_type TEXT,
    access_token_encrypted BLOB,
    refresh_token_encrypted BLOB,
    id_token_encrypted BLOB,
    last_refresh TEXT,
    deactivation_reason TEXT,
    status TEXT NOT NULL DEFAULT 'active',
    created_at TEXT NOT NULL DEFAULT (datetime('now')),
    updated_at TEXT NOT NULL DEFAULT (datetime('now'))
);
)SQL";

constexpr const char* kAddChatgptAccountIdSql = "ALTER TABLE accounts ADD COLUMN chatgpt_account_id TEXT;";
constexpr const char* kAddPlanTypeSql = "ALTER TABLE accounts ADD COLUMN plan_type TEXT;";
constexpr const char* kAddAccessTokenSql = "ALTER TABLE accounts ADD COLUMN access_token_encrypted BLOB;";
constexpr const char* kAddRefreshTokenSql = "ALTER TABLE accounts ADD COLUMN refresh_token_encrypted BLOB;";
constexpr const char* kAddIdTokenSql = "ALTER TABLE accounts ADD COLUMN id_token_encrypted BLOB;";
constexpr const char* kAddLastRefreshSql = "ALTER TABLE accounts ADD COLUMN last_refresh TEXT;";
constexpr const char* kAddDeactivationReasonSql = "ALTER TABLE accounts ADD COLUMN deactivation_reason TEXT;";
constexpr const char* kAddQuotaPrimaryPercentSql = "ALTER TABLE accounts ADD COLUMN quota_primary_percent INTEGER;";
constexpr const char* kAddQuotaSecondaryPercentSql = "ALTER TABLE accounts ADD COLUMN quota_secondary_percent INTEGER;";
constexpr const char* kAddTelemetryRefreshedAtSql =
    "ALTER TABLE accounts ADD COLUMN usage_telemetry_refreshed_at TEXT;";

bool ensure_schema(SQLite::Database& db) noexcept {
    if (!sqlite_repo_utils::exec_sql(db, kEnsureAccountsSchemaSql)) {
        return false;
    }
    return sqlite_repo_utils::ensure_column(db, "accounts", "chatgpt_account_id", kAddChatgptAccountIdSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "plan_type", kAddPlanTypeSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "access_token_encrypted", kAddAccessTokenSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "refresh_token_encrypted", kAddRefreshTokenSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "id_token_encrypted", kAddIdTokenSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "last_refresh", kAddLastRefreshSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "deactivation_reason", kAddDeactivationReasonSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "quota_primary_percent", kAddQuotaPrimaryPercentSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "quota_secondary_percent", kAddQuotaSecondaryPercentSql) &&
           sqlite_repo_utils::ensure_column(
               db,
               "accounts",
               "usage_telemetry_refreshed_at",
               kAddTelemetryRefreshedAtSql);
}

std::optional<AccountRecord> read_account_row(SQLite::Statement& stmt) {
    if (!stmt.executeStep()) {
        return std::nullopt;
    }

    AccountRecord record;
    record.id = stmt.getColumn(0).getInt64();
    if (!stmt.getColumn(1).isNull()) {
        record.email = stmt.getColumn(1).getString();
    }
    if (!stmt.getColumn(2).isNull()) {
        record.provider = stmt.getColumn(2).getString();
    }
    if (!stmt.getColumn(3).isNull()) {
        record.status = stmt.getColumn(3).getString();
    }
    return record;
}

} // namespace

bool ensure_accounts_schema(sqlite3* db) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return false;
    }
    return ensure_schema(*handle.db);
}

std::vector<AccountRecord> list_accounts(sqlite3* db) noexcept {
    std::vector<AccountRecord> records;
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return records;
    }

constexpr const char* kSql = R"SQL(
SELECT id, email, provider, status, plan_type, quota_primary_percent, quota_secondary_percent
FROM accounts
ORDER BY email ASC, id ASC;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        while (stmt.executeStep()) {
            AccountRecord record;
            record.id = stmt.getColumn(0).getInt64();
            if (!stmt.getColumn(1).isNull()) {
                record.email = stmt.getColumn(1).getString();
            }
            if (!stmt.getColumn(2).isNull()) {
                record.provider = stmt.getColumn(2).getString();
            }
            if (!stmt.getColumn(3).isNull()) {
                record.status = stmt.getColumn(3).getString();
            }
            if (!stmt.getColumn(4).isNull()) {
                record.plan_type = stmt.getColumn(4).getString();
            }
            if (!stmt.getColumn(5).isNull()) {
                record.quota_primary_percent = stmt.getColumn(5).getInt();
            }
            if (!stmt.getColumn(6).isNull()) {
                record.quota_secondary_percent = stmt.getColumn(6).getInt();
            }
            records.push_back(std::move(record));
        }
    } catch (...) {
        return {};
    }

    return records;
}

std::optional<AccountRecord> import_account(sqlite3* db, const std::string_view email, const std::string_view provider) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || email.empty()) {
        return std::nullopt;
    }

    constexpr const char* kInsertSql =
        "INSERT INTO accounts(email, provider, status, updated_at) VALUES(?1, ?2, 'active', datetime('now'));";
    constexpr const char* kSelectSql = "SELECT id, email, provider, status FROM accounts WHERE id = ?1 LIMIT 1;";

    try {
        SQLite::Statement insert_stmt(*handle.db, kInsertSql);
        insert_stmt.bind(1, std::string(email));
        insert_stmt.bind(2, std::string(provider));
        if (insert_stmt.exec() <= 0) {
            return std::nullopt;
        }

        const std::int64_t account_id = handle.db->getLastInsertRowid();
        SQLite::Statement select_stmt(*handle.db, kSelectSql);
        select_stmt.bind(1, account_id);
        return read_account_row(select_stmt);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<AccountRecord> upsert_oauth_account(sqlite3* db, const OauthAccountUpsert& account) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || account.email.empty()) {
        return std::nullopt;
    }

    constexpr const char* kFindSql = "SELECT id FROM accounts WHERE email = ?1 AND provider = ?2 LIMIT 1;";
    constexpr const char* kInsertSql = R"SQL(
INSERT INTO accounts(
    email,
    provider,
    chatgpt_account_id,
    plan_type,
    access_token_encrypted,
    refresh_token_encrypted,
    id_token_encrypted,
    last_refresh,
    status,
    updated_at
) VALUES(
    ?1, ?2, ?3, ?4, ?5, ?6, ?7, datetime('now'), 'active', datetime('now')
);
)SQL";
    constexpr const char* kUpdateSql = R"SQL(
UPDATE accounts
SET chatgpt_account_id = ?1,
    plan_type = ?2,
    access_token_encrypted = ?3,
    refresh_token_encrypted = ?4,
    id_token_encrypted = ?5,
    last_refresh = datetime('now'),
    status = 'active',
    updated_at = datetime('now')
WHERE id = ?6;
)SQL";
    constexpr const char* kSelectSql = "SELECT id, email, provider, status FROM accounts WHERE id = ?1 LIMIT 1;";

    try {
        std::optional<std::int64_t> existing_id;
        {
            SQLite::Statement find_stmt(*handle.db, kFindSql);
            find_stmt.bind(1, account.email);
            find_stmt.bind(2, account.provider);
            if (find_stmt.executeStep()) {
                existing_id = find_stmt.getColumn(0).getInt64();
            }
        }

        std::int64_t account_id = 0;
        if (!existing_id.has_value()) {
            SQLite::Statement insert_stmt(*handle.db, kInsertSql);
            insert_stmt.bind(1, account.email);
            insert_stmt.bind(2, account.provider);
            sqlite_repo_utils::bind_optional_text(insert_stmt, 3, account.chatgpt_account_id);
            sqlite_repo_utils::bind_optional_text(insert_stmt, 4, account.plan_type);
            insert_stmt.bind(5, account.access_token_encrypted);
            insert_stmt.bind(6, account.refresh_token_encrypted);
            insert_stmt.bind(7, account.id_token_encrypted);
            if (insert_stmt.exec() <= 0) {
                return std::nullopt;
            }
            account_id = handle.db->getLastInsertRowid();
        } else {
            SQLite::Statement update_stmt(*handle.db, kUpdateSql);
            sqlite_repo_utils::bind_optional_text(update_stmt, 1, account.chatgpt_account_id);
            sqlite_repo_utils::bind_optional_text(update_stmt, 2, account.plan_type);
            update_stmt.bind(3, account.access_token_encrypted);
            update_stmt.bind(4, account.refresh_token_encrypted);
            update_stmt.bind(5, account.id_token_encrypted);
            update_stmt.bind(6, *existing_id);
            (void)update_stmt.exec();
            account_id = *existing_id;
        }

        SQLite::Statement select_stmt(*handle.db, kSelectSql);
        select_stmt.bind(1, account_id);
        return read_account_row(select_stmt);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<AccountRecord>
update_account_status(sqlite3* db, const std::int64_t account_id, const std::string_view status) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || account_id <= 0 || status.empty()) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
UPDATE accounts
SET status = ?1, updated_at = datetime('now')
WHERE id = ?2;
)SQL";
    constexpr const char* kSelectSql = "SELECT id, email, provider, status FROM accounts WHERE id = ?1 LIMIT 1;";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, std::string(status));
        stmt.bind(2, account_id);
        (void)stmt.exec();
        if (handle.db->getChanges() == 0) {
            return std::nullopt;
        }

        SQLite::Statement select_stmt(*handle.db, kSelectSql);
        select_stmt.bind(1, account_id);
        return read_account_row(select_stmt);
    } catch (...) {
        return std::nullopt;
    }
}

bool delete_account(sqlite3* db, const std::int64_t account_id) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || account_id <= 0) {
        return false;
    }

    constexpr const char* kSql = "DELETE FROM accounts WHERE id = ?1;";
    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, account_id);
        (void)stmt.exec();
        return handle.db->getChanges() > 0;
    } catch (...) {
        return false;
    }
}

std::optional<AccountUsageCredentials> account_usage_credentials(sqlite3* db, const std::int64_t account_id) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || account_id <= 0) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
SELECT chatgpt_account_id, access_token_encrypted
FROM accounts
WHERE id = ?1
LIMIT 1;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, account_id);
        if (!stmt.executeStep()) {
            return std::nullopt;
        }
        if (stmt.getColumn(0).isNull() || stmt.getColumn(1).isNull()) {
            return std::nullopt;
        }
        auto account = stmt.getColumn(0).getString();
        auto token = stmt.getColumn(1).getString();
        account = core::text::trim_ascii(account);
        token = core::text::trim_ascii(token);
        if (account.empty() || token.empty()) {
            return std::nullopt;
        }
        return AccountUsageCredentials{
            .chatgpt_account_id = std::move(account),
            .access_token = std::move(token),
        };
    } catch (...) {
        return std::nullopt;
    }
}

bool update_account_usage_telemetry(
    sqlite3* db,
    const std::int64_t account_id,
    const std::optional<int> quota_primary_percent,
    const std::optional<int> quota_secondary_percent,
    const std::optional<std::string> plan_type
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || account_id <= 0) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
UPDATE accounts
SET quota_primary_percent = ?1,
    quota_secondary_percent = ?2,
    usage_telemetry_refreshed_at = datetime('now'),
    plan_type = COALESCE(?3, plan_type),
    updated_at = datetime('now')
WHERE id = ?4;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        if (quota_primary_percent.has_value()) {
            stmt.bind(1, *quota_primary_percent);
        } else {
            stmt.bind(1);
        }
        if (quota_secondary_percent.has_value()) {
            stmt.bind(2, *quota_secondary_percent);
        } else {
            stmt.bind(2);
        }
        if (!sqlite_repo_utils::bind_optional_text(stmt, 3, plan_type)) {
            return false;
        }
        stmt.bind(4, account_id);
        (void)stmt.exec();
        return handle.db->getChanges() > 0;
    } catch (...) {
        return false;
    }
}

} // namespace tightrope::db
