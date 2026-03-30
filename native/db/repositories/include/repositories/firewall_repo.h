#pragma once
// firewall CRUD operations

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <sqlite3.h>

namespace tightrope::db {

struct FirewallIpRecord {
    std::int64_t id = 0;
    std::string ip_address;
    std::string created_at;
};

enum class FirewallWriteResult {
    Created,
    Deleted,
    Duplicate,
    InvalidIp,
    NotFound,
    Error,
};

[[nodiscard]] bool ensure_firewall_allowlist_schema(sqlite3* db) noexcept;
[[nodiscard]] std::vector<FirewallIpRecord> list_firewall_allowlist_entries(sqlite3* db) noexcept;
[[nodiscard]] FirewallWriteResult
add_firewall_allowlist_ip(sqlite3* db, std::string_view ip_address, FirewallIpRecord* created = nullptr) noexcept;
[[nodiscard]] FirewallWriteResult delete_firewall_allowlist_ip(sqlite3* db, std::string_view ip_address) noexcept;
[[nodiscard]] bool is_firewall_ip_allowed(sqlite3* db, const std::optional<std::string_view>& ip_address) noexcept;

} // namespace tightrope::db
