#pragma once
// session CRUD operations

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

namespace tightrope::db {

struct ProxyStickySessionRecord {
    std::string session_key;
    std::string account_id;
    std::string kind = "sticky_thread";
    std::int64_t updated_at_ms = 0;
    std::int64_t expires_at_ms = 0;
};

struct ProxyResponseContinuityRecord {
    std::string continuity_key;
    std::string api_key_scope;
    std::string response_id;
    std::string account_id;
    std::string recovery_input_json;
    std::int64_t updated_at_ms = 0;
    std::int64_t expires_at_ms = 0;
};

[[nodiscard]] bool ensure_proxy_sticky_session_schema(sqlite3* db) noexcept;
[[nodiscard]] bool upsert_proxy_sticky_session(
    sqlite3* db,
    std::string_view session_key,
    std::string_view account_id,
    std::int64_t now_ms,
    std::int64_t ttl_ms,
    std::string_view kind = "sticky_thread"
) noexcept;
[[nodiscard]] std::optional<std::string>
find_proxy_sticky_session_account(sqlite3* db, std::string_view session_key, std::int64_t now_ms) noexcept;
[[nodiscard]] std::size_t purge_expired_proxy_sticky_sessions(sqlite3* db, std::int64_t now_ms) noexcept;
[[nodiscard]] std::size_t purge_proxy_sticky_sessions_for_account(sqlite3* db, std::string_view account_id) noexcept;
[[nodiscard]] std::vector<ProxyStickySessionRecord>
list_proxy_sticky_sessions(sqlite3* db, std::size_t limit = 500, std::size_t offset = 0) noexcept;

[[nodiscard]] bool ensure_proxy_response_continuity_schema(sqlite3* db) noexcept;
[[nodiscard]] bool upsert_proxy_response_continuity(
    sqlite3* db,
    std::string_view continuity_key,
    std::string_view api_key_scope,
    std::string_view response_id,
    std::string_view account_id,
    std::int64_t now_ms,
    std::int64_t ttl_ms,
    std::string_view recovery_input_json = {}
) noexcept;
[[nodiscard]] std::optional<ProxyResponseContinuityRecord> find_proxy_response_continuity(
    sqlite3* db,
    std::string_view continuity_key,
    std::string_view api_key_scope,
    std::int64_t now_ms
) noexcept;
[[nodiscard]] std::optional<ProxyResponseContinuityRecord> find_proxy_response_continuity_by_response_id(
    sqlite3* db,
    std::string_view response_id,
    std::string_view api_key_scope,
    std::int64_t now_ms
) noexcept;
[[nodiscard]] std::optional<std::string> find_proxy_response_continuity_account(
    sqlite3* db,
    std::string_view response_id,
    std::string_view api_key_scope,
    std::int64_t now_ms
) noexcept;
[[nodiscard]] std::size_t purge_expired_proxy_response_continuity(sqlite3* db, std::int64_t now_ms) noexcept;
[[nodiscard]] std::size_t purge_proxy_response_continuity_for_account(sqlite3* db, std::string_view account_id) noexcept;

} // namespace tightrope::db
