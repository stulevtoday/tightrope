#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <sqlite3.h>

namespace SQLite {
class Column;
class Database;
class Statement;
} // namespace SQLite

namespace tightrope::db::sqlite_repo_utils {

struct DatabaseHandle {
    SQLite::Database* db = nullptr;
    std::unique_ptr<SQLite::Database> owned_db;

    [[nodiscard]] bool valid() const noexcept {
        return db != nullptr;
    }

    [[nodiscard]] explicit operator bool() const noexcept {
        return valid();
    }
};

[[nodiscard]] DatabaseHandle resolve_database(sqlite3* db) noexcept;
[[nodiscard]] bool exec_sql(sqlite3* db, const char* sql) noexcept;
[[nodiscard]] bool exec_sql(SQLite::Database& db, const char* sql) noexcept;
[[nodiscard]] bool has_column(sqlite3* db, std::string_view table, std::string_view column) noexcept;
[[nodiscard]] bool has_column(SQLite::Database& db, std::string_view table, std::string_view column) noexcept;
[[nodiscard]] bool
ensure_column(sqlite3* db, std::string_view table, std::string_view column, const char* alter_sql) noexcept;
[[nodiscard]] bool
ensure_column(SQLite::Database& db, std::string_view table, std::string_view column, const char* alter_sql) noexcept;

[[nodiscard]] std::optional<std::string> optional_text(const SQLite::Column& column);
[[nodiscard]] std::optional<std::string> optional_text(sqlite3_stmt* stmt, int index);
[[nodiscard]] std::optional<std::int64_t> optional_i64(const SQLite::Column& column);
[[nodiscard]] std::optional<std::int64_t> optional_i64(sqlite3_stmt* stmt, int index);

[[nodiscard]] bool bind_optional_text(SQLite::Statement& stmt, int index, const std::optional<std::string>& value) noexcept;
[[nodiscard]] bool bind_optional_text(sqlite3_stmt* stmt, int index, const std::optional<std::string>& value) noexcept;

} // namespace tightrope::db::sqlite_repo_utils

