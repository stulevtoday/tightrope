#pragma once
// api_key CRUD operations

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

namespace tightrope::db {

struct ApiKeyLimitInput {
    std::string limit_type;
    std::string limit_window;
    double max_value = 0.0;
    std::optional<std::string> model_filter;
    double current_value = 0.0;
};

struct ApiKeyLimitRecord {
    std::int64_t id = 0;
    std::int64_t api_key_id = 0;
    std::string limit_type;
    std::string limit_window;
    double max_value = 0.0;
    double current_value = 0.0;
    std::optional<std::string> model_filter;
};

struct ApiKeyRecord {
    std::int64_t id = 0;
    std::string key_id;
    std::string key_hash;
    std::string key_prefix;
    std::string name;
    bool is_active = true;
    std::optional<std::string> allowed_models;
    std::optional<std::string> enforced_model;
    std::optional<std::string> enforced_reasoning_effort;
    std::optional<std::string> expires_at;
    std::string created_at;
    std::optional<std::string> last_used_at;
    std::vector<ApiKeyLimitRecord> limits;
};

struct ApiKeyPatch {
    std::optional<std::string> name;
    std::optional<bool> is_active;
    std::optional<std::string> key_hash;
    std::optional<std::string> key_prefix;
    std::optional<std::string> allowed_models;
    std::optional<std::string> enforced_model;
    std::optional<std::string> enforced_reasoning_effort;
    std::optional<std::string> expires_at;
    std::optional<std::string> last_used_at;
};

struct ApiKeyUsageReservationItemInput {
    std::string metric;
    double amount = 0.0;
};

struct ApiKeyUsageReservationItemRecord {
    std::int64_t id = 0;
    std::string metric;
    double amount = 0.0;
};

struct ApiKeyUsageReservationRecord {
    std::int64_t id = 0;
    std::int64_t api_key_id = 0;
    std::string key_id;
    std::string request_id;
    std::string status;
    std::string reserved_at;
    std::optional<std::string> settled_at;
    std::vector<ApiKeyUsageReservationItemRecord> items;
};

[[nodiscard]] bool ensure_api_key_schema(sqlite3* db) noexcept;

[[nodiscard]] std::optional<ApiKeyRecord> create_api_key(
    sqlite3* db,
    std::string_view key_id,
    std::string_view key_hash,
    std::string_view key_prefix,
    std::string_view name,
    const std::optional<std::string>& allowed_models,
    const std::optional<std::string>& enforced_model,
    const std::optional<std::string>& enforced_reasoning_effort,
    const std::optional<std::string>& expires_at
) noexcept;

[[nodiscard]] std::vector<ApiKeyRecord> list_api_keys(sqlite3* db) noexcept;
[[nodiscard]] std::optional<ApiKeyRecord> get_api_key_by_key_id(sqlite3* db, std::string_view key_id) noexcept;
[[nodiscard]] std::optional<ApiKeyRecord> get_api_key_by_hash(sqlite3* db, std::string_view key_hash) noexcept;
[[nodiscard]] std::optional<ApiKeyRecord> update_api_key(sqlite3* db, std::string_view key_id, const ApiKeyPatch& patch)
    noexcept;
[[nodiscard]] bool delete_api_key(sqlite3* db, std::string_view key_id) noexcept;
[[nodiscard]] std::optional<ApiKeyRecord>
rotate_api_key_secret(sqlite3* db, std::string_view key_id, std::string_view key_hash, std::string_view key_prefix) noexcept;

[[nodiscard]] std::optional<std::vector<ApiKeyLimitRecord>>
replace_api_key_limits(sqlite3* db, std::string_view key_id, const std::vector<ApiKeyLimitInput>& limits) noexcept;

[[nodiscard]] std::optional<ApiKeyUsageReservationRecord> create_api_key_usage_reservation(
    sqlite3* db,
    std::string_view key_id,
    std::string_view request_id,
    const std::vector<ApiKeyUsageReservationItemInput>& items
) noexcept;

[[nodiscard]] std::optional<ApiKeyUsageReservationRecord>
get_api_key_usage_reservation(sqlite3* db, std::string_view request_id) noexcept;

[[nodiscard]] bool transition_api_key_usage_reservation_status(
    sqlite3* db,
    std::string_view request_id,
    std::string_view expected_status,
    std::string_view next_status
) noexcept;

} // namespace tightrope::db
