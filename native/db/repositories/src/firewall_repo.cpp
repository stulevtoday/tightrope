#include "firewall_repo.h"

#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>

#include <optional>
#include <string>
#include <vector>

#include "net/ip_address.h"
#include "sqlite_repo_utils.h"

namespace tightrope::db {

namespace {

constexpr const char* kCreateSchemaSql = R"SQL(
CREATE TABLE IF NOT EXISTS api_firewall_allowlist (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    ip TEXT NOT NULL UNIQUE,
    created_at TEXT NOT NULL DEFAULT (datetime('now'))
);
)SQL";

std::optional<std::string> normalize_ip(const std::string_view ip_address) {
    return core::net::normalize_ip_address(ip_address);
}

bool ensure_schema(SQLite::Database& db) noexcept {
    return sqlite_repo_utils::exec_sql(db, kCreateSchemaSql);
}

std::optional<FirewallIpRecord> find_by_ip(SQLite::Database& db, std::string_view ip_address) {
    constexpr const char* kSql =
        "SELECT id, ip, created_at FROM api_firewall_allowlist WHERE ip = ?1 LIMIT 1;";
    try {
        SQLite::Statement stmt(db, kSql);
        stmt.bind(1, std::string(ip_address));
        if (!stmt.executeStep()) {
            return std::nullopt;
        }

        FirewallIpRecord record;
        record.id = stmt.getColumn(0).getInt64();
        if (!stmt.getColumn(1).isNull()) {
            record.ip_address = stmt.getColumn(1).getString();
        }
        if (!stmt.getColumn(2).isNull()) {
            record.created_at = stmt.getColumn(2).getString();
        }
        return record;
    } catch (...) {
        return std::nullopt;
    }
}

} // namespace

bool ensure_firewall_allowlist_schema(sqlite3* db) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid()) {
        return false;
    }
    return ensure_schema(*handle.db);
}

std::vector<FirewallIpRecord> list_firewall_allowlist_entries(sqlite3* db) noexcept {
    std::vector<FirewallIpRecord> records;
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return records;
    }

    constexpr const char* kSql = R"SQL(
SELECT id, ip, created_at
FROM api_firewall_allowlist
ORDER BY created_at ASC, ip ASC;
)SQL";

    try {
        SQLite::Statement stmt(*handle.db, kSql);
        while (stmt.executeStep()) {
            FirewallIpRecord record;
            record.id = stmt.getColumn(0).getInt64();
            if (!stmt.getColumn(1).isNull()) {
                record.ip_address = stmt.getColumn(1).getString();
            }
            if (!stmt.getColumn(2).isNull()) {
                record.created_at = stmt.getColumn(2).getString();
            }
            records.push_back(std::move(record));
        }
    } catch (...) {
        return {};
    }
    return records;
}

FirewallWriteResult
add_firewall_allowlist_ip(sqlite3* db, const std::string_view ip_address, FirewallIpRecord* created) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return FirewallWriteResult::Error;
    }

    const auto normalized = normalize_ip(ip_address);
    if (!normalized.has_value()) {
        return FirewallWriteResult::InvalidIp;
    }

    if (find_by_ip(*handle.db, *normalized).has_value()) {
        return FirewallWriteResult::Duplicate;
    }

    constexpr const char* kInsertSql = "INSERT INTO api_firewall_allowlist(ip) VALUES (?1);";
    try {
        SQLite::Statement stmt(*handle.db, kInsertSql);
        stmt.bind(1, *normalized);
        if (stmt.exec() <= 0) {
            return FirewallWriteResult::Error;
        }
    } catch (...) {
        return FirewallWriteResult::Error;
    }

    if (created != nullptr) {
        const auto record = find_by_ip(*handle.db, *normalized);
        if (record.has_value()) {
            *created = *record;
        } else {
            created->ip_address = *normalized;
            created->created_at.clear();
        }
    }
    return FirewallWriteResult::Created;
}

FirewallWriteResult delete_firewall_allowlist_ip(sqlite3* db, const std::string_view ip_address) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return FirewallWriteResult::Error;
    }

    const auto normalized = normalize_ip(ip_address);
    if (!normalized.has_value()) {
        return FirewallWriteResult::InvalidIp;
    }

    constexpr const char* kSql = "DELETE FROM api_firewall_allowlist WHERE ip = ?1;";
    try {
        SQLite::Statement stmt(*handle.db, kSql);
        stmt.bind(1, *normalized);
        (void)stmt.exec();
        return handle.db->getChanges() > 0 ? FirewallWriteResult::Deleted : FirewallWriteResult::NotFound;
    } catch (...) {
        return FirewallWriteResult::Error;
    }
}

bool is_firewall_ip_allowed(sqlite3* db, const std::optional<std::string_view>& ip_address) noexcept {
    auto handle = sqlite_repo_utils::resolve_database(db);
    if (!handle.valid() || !ensure_schema(*handle.db)) {
        return false;
    }

    constexpr const char* kCountSql = "SELECT COUNT(1) FROM api_firewall_allowlist;";
    std::int64_t allowlist_size = 0;
    try {
        SQLite::Statement count_stmt(*handle.db, kCountSql);
        if (!count_stmt.executeStep()) {
            return false;
        }
        allowlist_size = count_stmt.getColumn(0).getInt64();
    } catch (...) {
        return false;
    }

    if (allowlist_size == 0) {
        return true;
    }

    if (!ip_address.has_value()) {
        return false;
    }

    const auto normalized = normalize_ip(*ip_address);
    if (!normalized.has_value()) {
        return false;
    }

    return find_by_ip(*handle.db, *normalized).has_value();
}

} // namespace tightrope::db

