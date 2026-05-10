#pragma once
// accounts API controller

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace tightrope::server::controllers {

struct AccountPayload {
    std::string account_id;
    std::string email;
    std::string provider;
    std::string status;
    bool pinned = false;
    std::optional<std::string> plan_type;
    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
    std::optional<int> quota_primary_window_seconds;
    std::optional<int> quota_secondary_window_seconds;
    std::optional<std::int64_t> quota_primary_reset_at_ms;
    std::optional<std::int64_t> quota_secondary_reset_at_ms;
    std::optional<std::uint64_t> requests_24h;
    std::optional<double> total_cost_24h_usd;
    std::optional<double> cost_norm;
};

struct AccountsResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::vector<AccountPayload> accounts;
};

struct AccountMutationResponse {
    int status = 500;
    std::string code;
    std::string message;
    AccountPayload account;
};

struct AccountTrafficPayload {
    std::string account_id;
    std::uint64_t up_bytes = 0;
    std::uint64_t down_bytes = 0;
    std::int64_t last_up_at_ms = 0;
    std::int64_t last_down_at_ms = 0;
};

struct AccountTrafficResponse {
    int status = 500;
    std::string code;
    std::string message;
    std::int64_t generated_at_ms = 0;
    std::vector<AccountTrafficPayload> accounts;
};

struct AccountTokenMigrationPayload {
    std::int64_t scanned_accounts = 0;
    std::int64_t plaintext_accounts = 0;
    std::int64_t plaintext_tokens = 0;
    std::int64_t migrated_accounts = 0;
    std::int64_t migrated_tokens = 0;
    std::int64_t failed_accounts = 0;
    bool dry_run = false;
    bool strict_mode_enabled = false;
    bool migrate_plaintext_on_read_enabled = true;
};

struct AccountTokenMigrationResponse {
    int status = 500;
    std::string code;
    std::string message;
    AccountTokenMigrationPayload migration;
};

AccountsResponse list_accounts(sqlite3* db = nullptr);
AccountMutationResponse import_account(std::string_view email, std::string_view provider, sqlite3* db = nullptr);
AccountMutationResponse import_account_with_tokens(std::string_view email, std::string_view provider, std::string_view access_token, std::string_view refresh_token, sqlite3* db = nullptr);
AccountMutationResponse pin_account(std::string_view account_id, sqlite3* db = nullptr);
AccountMutationResponse unpin_account(std::string_view account_id, sqlite3* db = nullptr);
AccountMutationResponse pause_account(std::string_view account_id, sqlite3* db = nullptr);
AccountMutationResponse reactivate_account(std::string_view account_id, sqlite3* db = nullptr);
AccountMutationResponse delete_account(std::string_view account_id, sqlite3* db = nullptr);
AccountMutationResponse refresh_account_token(std::string_view account_id, sqlite3* db = nullptr);
AccountMutationResponse refresh_account_usage(std::string_view account_id, sqlite3* db = nullptr);
AccountTokenMigrationResponse migrate_account_token_storage(bool dry_run = false, sqlite3* db = nullptr);
AccountTrafficResponse list_account_proxy_traffic(sqlite3* db = nullptr);

} // namespace tightrope::server::controllers
