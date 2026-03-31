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
    std::optional<std::string> plan_type;
    std::optional<int> quota_primary_percent;
    std::optional<int> quota_secondary_percent;
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

AccountsResponse list_accounts(sqlite3* db = nullptr);
AccountMutationResponse import_account(std::string_view email, std::string_view provider, sqlite3* db = nullptr);
AccountMutationResponse pause_account(std::string_view account_id, sqlite3* db = nullptr);
AccountMutationResponse reactivate_account(std::string_view account_id, sqlite3* db = nullptr);
AccountMutationResponse delete_account(std::string_view account_id, sqlite3* db = nullptr);
AccountMutationResponse refresh_account_usage(std::string_view account_id, sqlite3* db = nullptr);
AccountTrafficResponse list_account_proxy_traffic(sqlite3* db = nullptr);

} // namespace tightrope::server::controllers
