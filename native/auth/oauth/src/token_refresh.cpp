#include "token_refresh.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "repositories/account_repo.h"
#include "token_store.h"

namespace tightrope::auth::oauth {

namespace {

std::shared_ptr<ProviderClient>& token_refresh_provider_override() {
    static auto* provider = new std::shared_ptr<ProviderClient>();
    return *provider;
}

struct StoredTokenRecord {
    std::int64_t id = 0;
    std::string refresh_token;
    std::string id_token;
};

constexpr const char* kUpdateStoredTokensSql = R"SQL(
UPDATE accounts
SET refresh_token_encrypted = ?1,
    id_token_encrypted = ?2,
    updated_at = datetime('now')
WHERE id = ?3;
)SQL";

std::string sqlite_text_or_empty(const unsigned char* text) {
    return text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(text));
}

bool persist_migrated_stored_tokens(
    sqlite3* db,
    const std::int64_t account_id,
    const std::string& refresh_token_stored,
    const std::string& id_token_stored
) {
    if (db == nullptr || account_id <= 0 || refresh_token_stored.empty()) {
        return false;
    }
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kUpdateStoredTokensSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_bind_text(stmt, 1, refresh_token_stored.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, id_token_stored.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(stmt, 3, account_id) != SQLITE_OK) {
        finalize();
        return false;
    }
    const auto rc = sqlite3_step(stmt);
    finalize();
    return rc == SQLITE_DONE;
}

std::optional<StoredTokenRecord> find_active_account_tokens(sqlite3* db, const std::string_view chatgpt_account_id) {
    if (db == nullptr || chatgpt_account_id.empty()) {
        return std::nullopt;
    }
    if (!db::ensure_accounts_schema(db)) {
        return std::nullopt;
    }

    constexpr const char* kSql = R"SQL(
SELECT id, refresh_token_encrypted, id_token_encrypted
FROM accounts
WHERE chatgpt_account_id = ?1 AND status = 'active'
LIMIT 1;
)SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return std::nullopt;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    if (sqlite3_bind_text(stmt, 1, std::string(chatgpt_account_id).c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        finalize();
        return std::nullopt;
    }

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        finalize();
        return std::nullopt;
    }

    StoredTokenRecord record;
    record.id = sqlite3_column_int64(stmt, 0);
    record.refresh_token = sqlite_text_or_empty(sqlite3_column_text(stmt, 1));
    record.id_token = sqlite_text_or_empty(sqlite3_column_text(stmt, 2));

    finalize();
    if (record.id <= 0 || record.refresh_token.empty()) {
        return std::nullopt;
    }

    bool refresh_migrated = false;
    std::string migration_error;
    const auto refresh_token_stored = ::tightrope::auth::crypto::migrate_plaintext_token_for_storage(
        record.refresh_token,
        &refresh_migrated,
        &migration_error
    );
    if (!refresh_token_stored.has_value() || refresh_token_stored->empty()) {
        return std::nullopt;
    }
    std::optional<std::string> id_token_stored = std::nullopt;
    bool id_migrated = false;
    if (!record.id_token.empty()) {
        id_token_stored = ::tightrope::auth::crypto::migrate_plaintext_token_for_storage(
            record.id_token,
            &id_migrated,
            &migration_error
        );
        if (!id_token_stored.has_value()) {
            return std::nullopt;
        }
    }
    if ((refresh_migrated || id_migrated) &&
        !persist_migrated_stored_tokens(db, record.id, *refresh_token_stored, id_token_stored.value_or(""))) {
        return std::nullopt;
    }

    std::string token_error;
    const auto refresh_token = ::tightrope::auth::crypto::decrypt_token_from_storage(*refresh_token_stored, &token_error);
    if (!refresh_token.has_value() || refresh_token->empty()) {
        return std::nullopt;
    }
    record.refresh_token = *refresh_token;

    if (!record.id_token.empty()) {
        const auto id_token =
            ::tightrope::auth::crypto::decrypt_token_from_storage(id_token_stored.value_or(record.id_token), &token_error);
        if (!id_token.has_value()) {
            return std::nullopt;
        }
        record.id_token = *id_token;
    }

    return record;
}

bool update_account_tokens(
    sqlite3* db,
    const std::int64_t account_id,
    const OAuthTokens& tokens,
    const std::string& fallback_id_token
) {
    if (db == nullptr || account_id <= 0 || tokens.access_token.empty() || tokens.refresh_token.empty()) {
        return false;
    }

    constexpr const char* kSql = R"SQL(
UPDATE accounts
SET access_token_encrypted = ?1,
    refresh_token_encrypted = ?2,
    id_token_encrypted = ?3,
    last_refresh = datetime('now'),
    token_refresh_last_success_at_ms = (CAST(strftime('%s', 'now') AS INTEGER) * 1000),
    token_refresh_next_due_at_ms = (CAST(strftime('%s', 'now') AS INTEGER) * 1000) + 86400000,
    token_refresh_needs_attention = 0,
    token_refresh_last_error = NULL,
    updated_at = datetime('now'),
    status = 'active'
WHERE id = ?4;
)SQL";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK || stmt == nullptr) {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
        }
        return false;
    }
    const auto finalize = [&stmt]() {
        if (stmt != nullptr) {
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
    };

    const std::string id_token = tokens.id_token.empty() ? fallback_id_token : tokens.id_token;
    std::string token_error;
    const auto access_token_encrypted =
        ::tightrope::auth::crypto::encrypt_token_for_storage(tokens.access_token, &token_error);
    const auto refresh_token_encrypted =
        ::tightrope::auth::crypto::encrypt_token_for_storage(tokens.refresh_token, &token_error);
    const auto id_token_encrypted = ::tightrope::auth::crypto::encrypt_token_for_storage(id_token, &token_error);
    if (!access_token_encrypted.has_value() || !refresh_token_encrypted.has_value() || !id_token_encrypted.has_value()) {
        finalize();
        return false;
    }

    if (sqlite3_bind_text(stmt, 1, access_token_encrypted->c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 2, refresh_token_encrypted->c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_text(stmt, 3, id_token_encrypted->c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK ||
        sqlite3_bind_int64(stmt, 4, account_id) != SQLITE_OK) {
        finalize();
        return false;
    }

    const auto rc = sqlite3_step(stmt);
    const auto changes = sqlite3_changes(db);
    finalize();
    return rc == SQLITE_DONE && changes > 0;
}

} // namespace

RefreshAccessTokenResult refresh_access_token_for_account(
    sqlite3* db,
    const std::string_view chatgpt_account_id,
    std::shared_ptr<ProviderClient> provider
) {
    if (db == nullptr) {
        return {
            .refreshed = false,
            .error_code = "db_unavailable",
            .error_message = "Database unavailable",
        };
    }
    if (chatgpt_account_id.empty()) {
        return {
            .refreshed = false,
            .error_code = "invalid_account",
            .error_message = "chatgpt-account-id is required",
        };
    }

    const auto stored = find_active_account_tokens(db, chatgpt_account_id);
    if (!stored.has_value()) {
        return {
            .refreshed = false,
            .error_code = "account_not_found",
            .error_message = "Active account tokens were not found",
        };
    }

    if (!provider) {
        provider = token_refresh_provider_override();
    }
    if (!provider) {
        provider = make_default_provider_client();
    }
    if (!provider) {
        return {
            .refreshed = false,
            .error_code = "oauth_provider_unavailable",
            .error_message = "OAuth provider is unavailable",
        };
    }

    const auto refreshed = provider->refresh_access_token(stored->refresh_token);
    if (!refreshed.is_ok()) {
        return {
            .refreshed = false,
            .error_code = refreshed.error.has_value() ? refreshed.error->code : "oauth_refresh_failed",
            .error_message = refreshed.error.has_value() ? refreshed.error->message : "OAuth refresh failed",
        };
    }
    if (!update_account_tokens(db, stored->id, *refreshed.value, stored->id_token)) {
        return {
            .refreshed = false,
            .error_code = "persist_failed",
            .error_message = "Failed to persist refreshed OAuth tokens",
        };
    }

    return {
        .refreshed = true,
        .error_code = "",
        .error_message = "",
    };
}

void set_token_refresh_provider_for_testing(std::shared_ptr<ProviderClient> provider) {
    token_refresh_provider_override() = std::move(provider);
}

void clear_token_refresh_provider_for_testing() {
    token_refresh_provider_override().reset();
}

} // namespace tightrope::auth::oauth
