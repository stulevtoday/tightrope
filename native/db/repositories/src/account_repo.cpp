#include "account_repo.h"

#include <SQLiteCpp/Column.h>
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "sqlite_repo_utils.h"
#include "text/ascii.h"
#include "token_store.h"

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
    token_refresh_last_success_at_ms INTEGER,
    token_refresh_next_due_at_ms INTEGER,
    token_refresh_needs_attention INTEGER NOT NULL DEFAULT 0,
    token_refresh_last_error TEXT,
    deactivation_reason TEXT,
    status TEXT NOT NULL DEFAULT 'active',
    quota_primary_percent INTEGER,
    quota_secondary_percent INTEGER,
    quota_primary_limit_window_seconds INTEGER,
    quota_secondary_limit_window_seconds INTEGER,
    quota_primary_reset_at_ms INTEGER,
    quota_secondary_reset_at_ms INTEGER,
    usage_telemetry_refreshed_at TEXT,
    routing_pinned INTEGER NOT NULL DEFAULT 0,
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
constexpr const char* kAddTokenRefreshLastSuccessAtMsSql =
    "ALTER TABLE accounts ADD COLUMN token_refresh_last_success_at_ms INTEGER;";
constexpr const char* kAddTokenRefreshNextDueAtMsSql =
    "ALTER TABLE accounts ADD COLUMN token_refresh_next_due_at_ms INTEGER;";
constexpr const char* kAddTokenRefreshNeedsAttentionSql =
    "ALTER TABLE accounts ADD COLUMN token_refresh_needs_attention INTEGER NOT NULL DEFAULT 0;";
constexpr const char* kAddTokenRefreshLastErrorSql = "ALTER TABLE accounts ADD COLUMN token_refresh_last_error TEXT;";
constexpr const char* kAddDeactivationReasonSql = "ALTER TABLE accounts ADD COLUMN deactivation_reason TEXT;";
constexpr const char* kAddQuotaPrimaryPercentSql = "ALTER TABLE accounts ADD COLUMN quota_primary_percent INTEGER;";
constexpr const char* kAddQuotaSecondaryPercentSql = "ALTER TABLE accounts ADD COLUMN quota_secondary_percent INTEGER;";
constexpr const char* kAddQuotaPrimaryLimitWindowSecondsSql =
    "ALTER TABLE accounts ADD COLUMN quota_primary_limit_window_seconds INTEGER;";
constexpr const char* kAddQuotaSecondaryLimitWindowSecondsSql =
    "ALTER TABLE accounts ADD COLUMN quota_secondary_limit_window_seconds INTEGER;";
constexpr const char* kAddQuotaPrimaryResetAtMsSql = "ALTER TABLE accounts ADD COLUMN quota_primary_reset_at_ms INTEGER;";
constexpr const char* kAddQuotaSecondaryResetAtMsSql =
    "ALTER TABLE accounts ADD COLUMN quota_secondary_reset_at_ms INTEGER;";
constexpr const char* kAddRoutingPinnedSql =
    "ALTER TABLE accounts ADD COLUMN routing_pinned INTEGER NOT NULL DEFAULT 0;";
constexpr const char* kAddTelemetryRefreshedAtSql =
    "ALTER TABLE accounts ADD COLUMN usage_telemetry_refreshed_at TEXT;";
constexpr const char* kUpdateAccessTokenByIdSql = R"SQL(
UPDATE accounts
SET access_token_encrypted = ?1,
    updated_at = datetime('now')
WHERE id = ?2;
)SQL";
constexpr const char* kSelectAccountTokensForMigrationSql = R"SQL(
SELECT id, access_token_encrypted, refresh_token_encrypted, id_token_encrypted
FROM accounts
ORDER BY id ASC;
)SQL";
constexpr const char* kUpdateAccountTokensByIdSql = R"SQL(
UPDATE accounts
SET access_token_encrypted = ?1,
    refresh_token_encrypted = ?2,
    id_token_encrypted = ?3,
    updated_at = datetime('now')
WHERE id = ?4;
)SQL";

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
           sqlite_repo_utils::ensure_column(
               db,
               "accounts",
               "token_refresh_last_success_at_ms",
               kAddTokenRefreshLastSuccessAtMsSql) &&
           sqlite_repo_utils::ensure_column(
               db,
               "accounts",
               "token_refresh_next_due_at_ms",
               kAddTokenRefreshNextDueAtMsSql) &&
           sqlite_repo_utils::ensure_column(
               db,
               "accounts",
               "token_refresh_needs_attention",
               kAddTokenRefreshNeedsAttentionSql) &&
           sqlite_repo_utils::ensure_column(
               db,
               "accounts",
               "token_refresh_last_error",
               kAddTokenRefreshLastErrorSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "deactivation_reason", kAddDeactivationReasonSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "quota_primary_percent", kAddQuotaPrimaryPercentSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "quota_secondary_percent", kAddQuotaSecondaryPercentSql) &&
           sqlite_repo_utils::ensure_column(
               db,
               "accounts",
               "quota_primary_limit_window_seconds",
               kAddQuotaPrimaryLimitWindowSecondsSql) &&
           sqlite_repo_utils::ensure_column(
               db,
               "accounts",
               "quota_secondary_limit_window_seconds",
               kAddQuotaSecondaryLimitWindowSecondsSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "quota_primary_reset_at_ms", kAddQuotaPrimaryResetAtMsSql) &&
           sqlite_repo_utils::ensure_column(
               db,
               "accounts",
               "quota_secondary_reset_at_ms",
               kAddQuotaSecondaryResetAtMsSql) &&
           sqlite_repo_utils::ensure_column(db, "accounts", "routing_pinned", kAddRoutingPinnedSql) &&
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

std::optional<std::string> normalize_access_token_storage(
    SQLite::Database& db,
    const std::int64_t account_id,
    const std::string& stored_token
) {
    bool migrated = false;
    std::string migration_error;
    const auto normalized = ::tightrope::auth::crypto::migrate_plaintext_token_for_storage(
        stored_token,
        &migrated,
        &migration_error
    );
    if (!normalized.has_value()) {
        return std::nullopt;
    }

    if (migrated) {
        try {
            SQLite::Statement update_stmt(db, kUpdateAccessTokenByIdSql);
            update_stmt.bind(1, *normalized);
            update_stmt.bind(2, account_id);
            (void)update_stmt.exec();
        } catch (...) {
            return std::nullopt;
        }
    }
    return normalized;
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
SELECT
    id,
    email,
    provider,
    status,
    plan_type,
    quota_primary_percent,
    quota_secondary_percent,
    quota_primary_limit_window_seconds,
    quota_secondary_limit_window_seconds,
    quota_primary_reset_at_ms,
    quota_secondary_reset_at_ms,
    routing_pinned
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
            if (!stmt.getColumn(7).isNull()) {
                record.quota_primary_limit_window_seconds = stmt.getColumn(7).getInt();
            }
            if (!stmt.getColumn(8).isNull()) {
                record.quota_secondary_limit_window_seconds = stmt.getColumn(8).getInt();
            }
            if (!stmt.getColumn(9).isNull()) {
                record.quota_primary_reset_at_ms = stmt.getColumn(9).getInt64();
            }
            if (!stmt.getColumn(10).isNull()) {
                record.quota_secondary_reset_at_ms = stmt.getColumn(10).getInt64();
            }
            if (!stmt.getColumn(11).isNull()) {
                record.routing_pinned = stmt.getColumn(11).getInt() != 0;
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

    std::string token_error;
    const auto access_token_encrypted =
        ::tightrope::auth::crypto::encrypt_token_for_storage(account.access_token_encrypted, &token_error);
    if (!access_token_encrypted.has_value()) {
        return std::nullopt;
    }
    const auto refresh_token_encrypted =
        ::tightrope::auth::crypto::encrypt_token_for_storage(account.refresh_token_encrypted, &token_error);
    if (!refresh_token_encrypted.has_value()) {
        return std::nullopt;
    }
    const auto id_token_encrypted =
        ::tightrope::auth::crypto::encrypt_token_for_storage(account.id_token_encrypted, &token_error);
    if (!id_token_encrypted.has_value()) {
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
            (void)sqlite_repo_utils::bind_optional_text(insert_stmt, 3, account.chatgpt_account_id);
            (void)sqlite_repo_utils::bind_optional_text(insert_stmt, 4, account.plan_type);
            insert_stmt.bind(5, *access_token_encrypted);
            insert_stmt.bind(6, *refresh_token_encrypted);
            insert_stmt.bind(7, *id_token_encrypted);
            if (insert_stmt.exec() <= 0) {
                return std::nullopt;
            }
            account_id = handle.db->getLastInsertRowid();
        } else {
            SQLite::Statement update_stmt(*handle.db, kUpdateSql);
            sqlite_repo_utils::bind_optional_text(update_stmt, 1, account.chatgpt_account_id);
            sqlite_repo_utils::bind_optional_text(update_stmt, 2, account.plan_type);
            update_stmt.bind(3, *access_token_encrypted);
            update_stmt.bind(4, *refresh_token_encrypted);
            update_stmt.bind(5, *id_token_encrypted);
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
SET status = ?1,
    routing_pinned = CASE WHEN ?1 = 'active' THEN routing_pinned ELSE 0 END,
    updated_at = datetime('now')
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
        const auto stored_token = stmt.getColumn(1).getString();
        account = core::text::trim_ascii(account);
        auto token = core::text::trim_ascii(stored_token);
        if (account.empty() || token.empty()) {
            return std::nullopt;
        }

        const auto normalized_storage = normalize_access_token_storage(*handle.db, account_id, std::string(token));
        if (!normalized_storage.has_value()) {
            return std::nullopt;
        }

        std::string token_error;
        auto decrypted = ::tightrope::auth::crypto::decrypt_token_from_storage(*normalized_storage, &token_error);
        if (!decrypted.has_value()) {
            return std::nullopt;
        }
        token = core::text::trim_ascii(*decrypted);
        if (token.empty()) {
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

std::optional<TokenStorageMigrationResult> migrate_plaintext_account_tokens(sqlite3* db, const bool dry_run) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return std::nullopt;
    }

    TokenStorageMigrationResult result{};
    try {
        SQLite::Statement select_stmt(*handle.db, kSelectAccountTokensForMigrationSql);
        SQLite::Statement update_stmt(*handle.db, kUpdateAccountTokensByIdSql);
        if (!dry_run) {
            (void)handle.db->exec("BEGIN IMMEDIATE;");
        }

        while (select_stmt.executeStep()) {
            ++result.scanned_accounts;
            const auto account_id = select_stmt.getColumn(0).getInt64();

            auto access_stored = sqlite_repo_utils::optional_text(select_stmt.getColumn(1));
            auto refresh_stored = sqlite_repo_utils::optional_text(select_stmt.getColumn(2));
            auto id_stored = sqlite_repo_utils::optional_text(select_stmt.getColumn(3));

            bool account_has_plaintext = false;
            std::int64_t plaintext_tokens_for_account = 0;
            const auto note_plaintext = [&account_has_plaintext, &plaintext_tokens_for_account](
                                            const std::optional<std::string>& stored_value
                                        ) {
                if (!stored_value.has_value() || stored_value->empty()) {
                    return;
                }
                if (::tightrope::auth::crypto::token_storage_value_is_encrypted(*stored_value)) {
                    return;
                }
                account_has_plaintext = true;
                ++plaintext_tokens_for_account;
            };
            note_plaintext(access_stored);
            note_plaintext(refresh_stored);
            note_plaintext(id_stored);
            if (account_has_plaintext) {
                ++result.plaintext_accounts;
                result.plaintext_tokens += plaintext_tokens_for_account;
            }
            if (dry_run) {
                continue;
            }

            bool account_migrated = false;
            bool account_failed = false;
            std::int64_t migrated_tokens_for_account = 0;

            const auto migrate_token = [&account_migrated, &account_failed, &migrated_tokens_for_account](
                                           std::optional<std::string>* stored_value
                                       ) {
                if (stored_value == nullptr || !stored_value->has_value() || stored_value->value().empty()) {
                    return;
                }
                bool migrated = false;
                std::string migration_error;
                auto migrated_value = ::tightrope::auth::crypto::migrate_plaintext_token_for_storage(
                    stored_value->value(),
                    &migrated,
                    &migration_error
                );
                if (!migrated_value.has_value()) {
                    account_failed = true;
                    return;
                }
                *stored_value = *migrated_value;
                if (migrated) {
                    account_migrated = true;
                    ++migrated_tokens_for_account;
                }
            };

            migrate_token(&access_stored);
            migrate_token(&refresh_stored);
            migrate_token(&id_stored);
            if (account_failed) {
                ++result.failed_accounts;
                continue;
            }
            if (!account_migrated) {
                continue;
            }

            update_stmt.reset();
            update_stmt.clearBindings();
            if (!sqlite_repo_utils::bind_optional_text(update_stmt, 1, access_stored) ||
                !sqlite_repo_utils::bind_optional_text(update_stmt, 2, refresh_stored) ||
                !sqlite_repo_utils::bind_optional_text(update_stmt, 3, id_stored)) {
                ++result.failed_accounts;
                continue;
            }
            update_stmt.bind(4, account_id);
            (void)update_stmt.exec();

            ++result.migrated_accounts;
            result.migrated_tokens += migrated_tokens_for_account;
        }

        if (!dry_run) {
            (void)handle.db->exec("COMMIT;");
        }
        return result;
    } catch (...) {
        try {
            if (!dry_run) {
                (void)handle.db->exec("ROLLBACK;");
            }
        } catch (...) {
        }
        return std::nullopt;
    }
}

std::optional<TokenStoragePassphraseRotationResult> rotate_account_token_storage_passphrase(
    sqlite3* db,
    const std::string_view current_passphrase,
    const std::string_view next_passphrase
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return std::nullopt;
    }

    const auto normalized_current_passphrase = std::string(current_passphrase);
    const auto normalized_next_passphrase = std::string(next_passphrase);
    if (normalized_current_passphrase.empty() || normalized_next_passphrase.empty()) {
        return std::nullopt;
    }

    TokenStoragePassphraseRotationResult result{};
    try {
        SQLite::Statement select_stmt(*handle.db, kSelectAccountTokensForMigrationSql);
        SQLite::Statement update_stmt(*handle.db, kUpdateAccountTokensByIdSql);
        (void)handle.db->exec("BEGIN IMMEDIATE;");
        bool rotation_failed = false;

        while (select_stmt.executeStep()) {
            ++result.scanned_accounts;
            const auto account_id = select_stmt.getColumn(0).getInt64();

            auto access_stored = sqlite_repo_utils::optional_text(select_stmt.getColumn(1));
            auto refresh_stored = sqlite_repo_utils::optional_text(select_stmt.getColumn(2));
            auto id_stored = sqlite_repo_utils::optional_text(select_stmt.getColumn(3));

            bool account_rotated = false;
            bool account_failed = false;
            std::int64_t rotated_tokens_for_account = 0;

            const auto rotate_token = [&account_rotated,
                                       &account_failed,
                                       &rotated_tokens_for_account,
                                       &normalized_current_passphrase,
                                       &normalized_next_passphrase](std::optional<std::string>* stored_value) {
                if (stored_value == nullptr || !stored_value->has_value() || stored_value->value().empty()) {
                    return;
                }

                auto token_plaintext = stored_value->value();
                std::string token_error;
                if (::tightrope::auth::crypto::token_storage_value_is_encrypted(stored_value->value())) {
                    const auto decrypted = ::tightrope::auth::crypto::decrypt_token_from_storage_with_passphrase(
                        stored_value->value(),
                        normalized_current_passphrase,
                        &token_error
                    );
                    if (!decrypted.has_value()) {
                        account_failed = true;
                        return;
                    }
                    token_plaintext = *decrypted;
                }

                const auto rotated = ::tightrope::auth::crypto::encrypt_token_for_storage_with_passphrase(
                    token_plaintext,
                    normalized_next_passphrase,
                    &token_error
                );
                if (!rotated.has_value()) {
                    account_failed = true;
                    return;
                }

                if (*rotated != *stored_value) {
                    *stored_value = *rotated;
                    account_rotated = true;
                    ++rotated_tokens_for_account;
                }
            };

            rotate_token(&access_stored);
            rotate_token(&refresh_stored);
            rotate_token(&id_stored);
            if (account_failed) {
                ++result.failed_accounts;
                rotation_failed = true;
                break;
            }
            if (!account_rotated) {
                continue;
            }

            update_stmt.reset();
            update_stmt.clearBindings();
            if (!sqlite_repo_utils::bind_optional_text(update_stmt, 1, access_stored) ||
                !sqlite_repo_utils::bind_optional_text(update_stmt, 2, refresh_stored) ||
                !sqlite_repo_utils::bind_optional_text(update_stmt, 3, id_stored)) {
                ++result.failed_accounts;
                rotation_failed = true;
                break;
            }
            update_stmt.bind(4, account_id);
            (void)update_stmt.exec();

            ++result.rotated_accounts;
            result.rotated_tokens += rotated_tokens_for_account;
        }

        if (rotation_failed) {
            (void)handle.db->exec("ROLLBACK;");
            return std::nullopt;
        }

        (void)handle.db->exec("COMMIT;");
        return result;
    } catch (...) {
        try {
            (void)handle.db->exec("ROLLBACK;");
        } catch (...) {
        }
        return std::nullopt;
    }
}

bool set_account_routing_pinned(sqlite3* db, const std::int64_t account_id, const bool pinned) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || account_id <= 0) {
        return false;
    }

    constexpr const char* kBeginSql = "BEGIN IMMEDIATE;";
    constexpr const char* kCommitSql = "COMMIT;";
    constexpr const char* kRollbackSql = "ROLLBACK;";
    constexpr const char* kClearAllPinnedSql = R"SQL(
UPDATE accounts
SET routing_pinned = 0,
    updated_at = datetime('now')
WHERE routing_pinned != 0;
)SQL";
    constexpr const char* kPinByIdSql = R"SQL(
UPDATE accounts
SET routing_pinned = 1,
    updated_at = datetime('now')
WHERE id = ?1;
)SQL";
    constexpr const char* kUnpinByIdSql = R"SQL(
UPDATE accounts
SET routing_pinned = 0,
    updated_at = datetime('now')
WHERE id = ?1;
)SQL";

    try {
        if (pinned) {
            (void)handle.db->exec(kBeginSql);
            try {
                {
                    SQLite::Statement clear_stmt(*handle.db, kClearAllPinnedSql);
                    (void)clear_stmt.exec();
                }
                {
                    SQLite::Statement pin_stmt(*handle.db, kPinByIdSql);
                    pin_stmt.bind(1, account_id);
                    (void)pin_stmt.exec();
                    if (handle.db->getChanges() == 0) {
                        (void)handle.db->exec(kRollbackSql);
                        return false;
                    }
                }
                (void)handle.db->exec(kCommitSql);
                return true;
            } catch (...) {
                try {
                    (void)handle.db->exec(kRollbackSql);
                } catch (...) {
                }
                return false;
            }
        }

        SQLite::Statement unpin_stmt(*handle.db, kUnpinByIdSql);
        unpin_stmt.bind(1, account_id);
        (void)unpin_stmt.exec();
        return handle.db->getChanges() > 0;
    } catch (...) {
        return false;
    }
}

bool clear_account_routing_pin_by_chatgpt_account_id(sqlite3* db, const std::string_view chatgpt_account_id) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return false;
    }

    const auto normalized_account_id = std::string(core::text::trim_ascii(chatgpt_account_id));
    if (normalized_account_id.empty()) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
UPDATE accounts
SET routing_pinned = 0,
    updated_at = datetime('now')
WHERE chatgpt_account_id = ?1
  AND routing_pinned != 0;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, normalized_account_id);
        (void)stmt.exec();
        return handle.db->getChanges() > 0;
    } catch (...) {
        return false;
    }
}

bool update_account_usage_telemetry(
    sqlite3* db,
    const std::int64_t account_id,
    const std::optional<int> quota_primary_percent,
    const std::optional<int> quota_secondary_percent,
    const std::optional<std::string> plan_type,
    const std::optional<std::string> status,
    const std::optional<int> quota_primary_limit_window_seconds,
    const std::optional<int> quota_secondary_limit_window_seconds,
    const std::optional<std::int64_t> quota_primary_reset_at_ms,
    const std::optional<std::int64_t> quota_secondary_reset_at_ms
) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db) || account_id <= 0) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
UPDATE accounts
SET quota_primary_percent = ?1,
    quota_secondary_percent = ?2,
    quota_primary_limit_window_seconds = ?3,
    quota_secondary_limit_window_seconds = ?4,
    quota_primary_reset_at_ms = ?5,
    quota_secondary_reset_at_ms = ?6,
    usage_telemetry_refreshed_at = datetime('now'),
    plan_type = COALESCE(?7, plan_type),
    status = COALESCE(?8, status),
    routing_pinned = CASE
        WHEN COALESCE(?8, status) = 'active' THEN routing_pinned
        ELSE 0
    END,
    updated_at = datetime('now')
WHERE id = ?9;
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
        if (quota_primary_limit_window_seconds.has_value()) {
            stmt.bind(3, *quota_primary_limit_window_seconds);
        } else {
            stmt.bind(3);
        }
        if (quota_secondary_limit_window_seconds.has_value()) {
            stmt.bind(4, *quota_secondary_limit_window_seconds);
        } else {
            stmt.bind(4);
        }
        if (quota_primary_reset_at_ms.has_value()) {
            stmt.bind(5, *quota_primary_reset_at_ms);
        } else {
            stmt.bind(5);
        }
        if (quota_secondary_reset_at_ms.has_value()) {
            stmt.bind(6, *quota_secondary_reset_at_ms);
        } else {
            stmt.bind(6);
        }
        if (!sqlite_repo_utils::bind_optional_text(stmt, 7, plan_type)) {
            return false;
        }
        if (!sqlite_repo_utils::bind_optional_text(stmt, 8, status)) {
            return false;
        }
        stmt.bind(9, account_id);
        (void)stmt.exec();
        return handle.db->getChanges() > 0;
    } catch (...) {
        return false;
    }
}

} // namespace tightrope::db
