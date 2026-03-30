#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "api_key_repo.h"

namespace SQLite {
class Database;
class Statement;
} // namespace SQLite

namespace tightrope::db::api_key_repo_internal {

[[nodiscard]] bool exec_sql(sqlite3* db, const char* sql) noexcept;
[[nodiscard]] bool exec_sql(SQLite::Database& db, const char* sql) noexcept;
[[nodiscard]] bool has_column(sqlite3* db, std::string_view table, std::string_view column) noexcept;
[[nodiscard]] bool has_column(SQLite::Database& db, std::string_view table, std::string_view column) noexcept;
[[nodiscard]] bool ensure_column(
    sqlite3* db,
    std::string_view table,
    std::string_view column,
    const char* alter_sql
) noexcept;
[[nodiscard]] bool ensure_column(
    SQLite::Database& db,
    std::string_view table,
    std::string_view column,
    const char* alter_sql
) noexcept;
[[nodiscard]] bool bind_optional_text(SQLite::Statement& stmt, int index, const std::optional<std::string>& value) noexcept;
[[nodiscard]] std::optional<std::int64_t> find_api_key_pk(sqlite3* db, std::string_view key_id);
[[nodiscard]] std::optional<std::int64_t> find_api_key_pk(SQLite::Database& db, std::string_view key_id);
[[nodiscard]] std::vector<ApiKeyLimitRecord> load_limits(sqlite3* db, std::int64_t api_key_pk);
[[nodiscard]] std::vector<ApiKeyLimitRecord> load_limits(SQLite::Database& db, std::int64_t api_key_pk);
[[nodiscard]] std::optional<ApiKeyRecord>
load_key_by_clause(sqlite3* db, const char* clause, std::string_view value);
[[nodiscard]] std::optional<ApiKeyRecord>
load_key_by_clause(SQLite::Database& db, const char* clause, std::string_view value);

} // namespace tightrope::db::api_key_repo_internal
