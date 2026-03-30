#pragma once
// account CRUD operations

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace tightrope::db {

struct AccountRecord {
    std::int64_t id = 0;
    std::string email;
    std::string provider;
    std::string status;
    std::optional<std::string> plan_type;
    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
};

struct OauthAccountUpsert {
    std::string email;
    std::string provider;
    std::optional<std::string> chatgpt_account_id;
    std::optional<std::string> plan_type;
    std::string access_token_encrypted;
    std::string refresh_token_encrypted;
    std::string id_token_encrypted;
};

struct AccountUsageCredentials {
    std::string chatgpt_account_id;
    std::string access_token;
};

[[nodiscard]] bool ensure_accounts_schema(sqlite3* db) noexcept;
[[nodiscard]] std::vector<AccountRecord> list_accounts(sqlite3* db) noexcept;
[[nodiscard]] std::optional<AccountRecord> import_account(sqlite3* db, std::string_view email, std::string_view provider)
    noexcept;
[[nodiscard]] std::optional<AccountRecord> upsert_oauth_account(sqlite3* db, const OauthAccountUpsert& account) noexcept;
[[nodiscard]] std::optional<AccountRecord> update_account_status(sqlite3* db, std::int64_t account_id, std::string_view status)
    noexcept;
[[nodiscard]] bool delete_account(sqlite3* db, std::int64_t account_id) noexcept;
[[nodiscard]] std::optional<AccountUsageCredentials> account_usage_credentials(sqlite3* db, std::int64_t account_id) noexcept;
[[nodiscard]] bool update_account_usage_telemetry(
    sqlite3* db,
    std::int64_t account_id,
    std::optional<int> quota_primary_percent,
    std::optional<int> quota_secondary_percent,
    std::optional<std::string> plan_type = std::nullopt
) noexcept;

} // namespace tightrope::db
