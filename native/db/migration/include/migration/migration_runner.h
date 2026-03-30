#pragma once

#include <sqlite3.h>

#include <string>

namespace SQLite {
class Database;
}

// Apply SQL migrations

namespace tightrope::db {

[[nodiscard]] bool
run_migrations(SQLite::Database& db, const std::string& migration_path = "native/migrations/001_baseline.sql") noexcept;
[[nodiscard]] bool run_migrations(sqlite3* db, const std::string& migration_path = "native/migrations/001_baseline.sql") noexcept;
[[nodiscard]] bool table_exists(SQLite::Database& db, const std::string& table_name) noexcept;
[[nodiscard]] bool table_exists(sqlite3* db, const std::string& table_name) noexcept;

} // namespace tightrope::db
