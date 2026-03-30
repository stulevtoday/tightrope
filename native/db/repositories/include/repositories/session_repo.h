#pragma once
// session CRUD operations

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <sqlite3.h>

namespace tightrope::db {

[[nodiscard]] bool ensure_proxy_sticky_session_schema(sqlite3* db) noexcept;
[[nodiscard]] bool upsert_proxy_sticky_session(
    sqlite3* db,
    std::string_view session_key,
    std::string_view account_id,
    std::int64_t now_ms,
    std::int64_t ttl_ms
) noexcept;
[[nodiscard]] std::optional<std::string>
find_proxy_sticky_session_account(sqlite3* db, std::string_view session_key, std::int64_t now_ms) noexcept;
[[nodiscard]] std::size_t purge_expired_proxy_sticky_sessions(sqlite3* db, std::int64_t now_ms) noexcept;

} // namespace tightrope::db
